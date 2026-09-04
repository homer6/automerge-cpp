#include <automerge-cpp/document.hpp>
#include <automerge-cpp/cursor.hpp>
#include <automerge-cpp/mark.hpp>
#include <automerge-cpp/patch.hpp>

#include "doc_state.hpp"
#include <automerge-cpp/thread_pool.hpp>
#include "encoding/leb128.hpp"
#include "storage/serializer.hpp"
#include "storage/deserializer.hpp"
#include "storage/chunk.hpp"
#include "storage/change_chunk.hpp"
#include "sync/bloom_filter.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <expected>
#include <limits>
#include <new>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <unordered_set>

namespace automerge_cpp {

static auto make_default_pool() -> std::shared_ptr<thread_pool> {
    auto n = std::thread::hardware_concurrency();
    if (n <= 1) return nullptr;
    return std::make_shared<thread_pool>(n);
}

static auto make_pool(unsigned int num_threads) -> std::shared_ptr<thread_pool> {
    if (num_threads == 1) return nullptr;
    auto n = (num_threads == 0) ? std::thread::hardware_concurrency() : num_threads;
    if (n <= 1) return nullptr;
    return std::make_shared<thread_pool>(n);
}

Document::Document()
    : state_{std::make_unique<detail::DocState>()},
      pool_{nullptr} {}

Document::Document(unsigned int num_threads)
    : state_{std::make_unique<detail::DocState>()},
      pool_{make_pool(num_threads)} {}

Document::Document(std::shared_ptr<thread_pool> pool)
    : state_{std::make_unique<detail::DocState>()},
      pool_{std::move(pool)} {}

Document::~Document() = default;

Document::Document(Document&& other) noexcept
    : state_{std::move(other.state_)},
      pool_{std::move(other.pool_)} {}

auto Document::operator=(Document&& other) noexcept -> Document& {
    if (this != &other) {
        auto lock = std::unique_lock{mutex_};
        state_ = std::move(other.state_);
        pool_ = std::move(other.pool_);
    }
    return *this;
}

Document::Document(const Document& other)
    : state_{std::make_unique<detail::DocState>(*other.state_)},
      pool_{other.pool_} {}

auto Document::operator=(const Document& other) -> Document& {
    if (this != &other) {
        auto lock = std::unique_lock{mutex_};
        state_ = std::make_unique<detail::DocState>(*other.state_);
        pool_ = other.pool_;
    }
    return *this;
}

auto Document::get_thread_pool() const -> std::shared_ptr<thread_pool> {
    return pool_;
}

void Document::set_read_locking(bool enabled) {
    read_locking_ = enabled;
}

auto Document::read_locking() const -> bool {
    return read_locking_;
}

auto Document::read_guard() const -> ReadGuard {
    return ReadGuard{mutex_, read_locking_};
}

auto Document::actor_id() const -> const ActorId& {
    auto guard = read_guard();
    return state_->actor;
}

void Document::set_actor_id(ActorId id) {
    auto lock = std::unique_lock{mutex_};
    state_->actor = id;
}

void Document::transact(const std::function<void(Transaction&)>& fn) {
    auto lock = std::unique_lock{mutex_};
    auto tx = Transaction{*state_};
    fn(tx);
    tx.commit();
}

auto Document::get(const ObjId& obj, std::string_view key) const -> std::optional<Value> {
    auto guard = read_guard();
    return state_->map_get(obj, std::string{key});
}

auto Document::get_all(const ObjId& obj, std::string_view key) const -> std::vector<Value> {
    auto guard = read_guard();
    return state_->map_get_all(obj, std::string{key});
}

auto Document::get(const ObjId& obj, std::size_t index) const -> std::optional<Value> {
    auto guard = read_guard();
    return state_->list_get(obj, index);
}

auto Document::keys(const ObjId& obj) const -> std::vector<std::string> {
    auto guard = read_guard();
    return state_->map_keys(obj);
}

auto Document::values(const ObjId& obj) const -> std::vector<Value> {
    auto guard = read_guard();
    const auto type = state_->object_type(obj);
    if (!type) return {};

    switch (*type) {
        case ObjType::map:
        case ObjType::table:
            return state_->map_values(obj);
        case ObjType::list:
        case ObjType::text:
            return state_->list_values(obj);
    }
    return {};
}

auto Document::length(const ObjId& obj) const -> std::size_t {
    auto guard = read_guard();
    return state_->object_length(obj);
}

auto Document::text(const ObjId& obj) const -> std::string {
    auto guard = read_guard();
    return state_->text_content(obj);
}

auto Document::object_type(const ObjId& obj) const -> std::optional<ObjType> {
    auto guard = read_guard();
    return state_->object_type(obj);
}

// -- Child object lookup ------------------------------------------------------

auto Document::get_obj_id(const ObjId& obj, std::string_view key) const -> std::optional<ObjId> {
    auto guard = read_guard();
    return state_->get_obj_id_for_key(obj, std::string{key});
}

auto Document::get_obj_id(const ObjId& obj, std::size_t index) const -> std::optional<ObjId> {
    auto guard = read_guard();
    return state_->get_obj_id_for_index(obj, index);
}

// -- Phase 3: Fork and Merge --------------------------------------------------

auto Document::fork() const -> Document {
    auto guard = read_guard();
    static std::atomic<std::uint64_t> fork_counter{1};
    auto forked = Document{pool_};
    *forked.state_ = *state_;
    // Create a unique actor by stamping a monotonic counter into the last 8 bytes
    auto new_actor = state_->actor;
    auto counter = fork_counter.fetch_add(1, std::memory_order_relaxed);
    std::memcpy(&new_actor.bytes[8], &counter, sizeof(counter));
    forked.state_->actor = new_actor;
    return forked;
}

static auto validate_change(const Change& change) -> std::expected<void, Error> {
    if (change.seq == 0) {
        return std::unexpected{
            Error{ErrorKind::invalid_change, "change sequence numbers start at one"}};
    }
    if (change.start_op == 0) {
        return std::unexpected{
            Error{ErrorKind::invalid_change, "change operation counters start at one"}};
    }
    if (change.operations.empty()) {
        return std::unexpected{
            Error{ErrorKind::invalid_change, "a change must contain at least one operation"}};
    }
    if (change.operations.size() >
        std::numeric_limits<std::uint64_t>::max() - change.start_op + 1) {
        return std::unexpected{
            Error{ErrorKind::invalid_change, "change operation counters overflow"}};
    }

    auto next_counter = change.start_op;
    const auto operations_match_change =
        std::ranges::all_of(change.operations, [&](const Op& operation) {
            const auto action_is_valid = static_cast<std::uint8_t>(operation.action) <=
                                         static_cast<std::uint8_t>(OpType::mark);
            const auto operation_matches = action_is_valid && operation.id.actor == change.actor &&
                                           operation.id.counter == next_counter;
            ++next_counter;
            return operation_matches;
        });
    if (!operations_match_change) {
        return std::unexpected{
            Error{ErrorKind::invalid_change,
                  "change operations do not match its actor or operation range"}};
    }
    return {};
}

static auto queued_sequence_conflicts(const detail::DocState& state,
                                      const Change& incoming,
                                      const ChangeHash& incoming_hash) -> bool {
    const auto actor = state.pending_sequences.find(incoming.actor);
    if (actor == state.pending_sequences.end()) {
        return false;
    }
    const auto sequence = actor->second.find(incoming.seq);
    return sequence != actor->second.end() && sequence->second != incoming_hash;
}

static void apply_change(detail::DocState& state, const Change& change) {
    std::ranges::for_each(change.operations,
                          [&](const Op& operation) { state.apply_op(operation); });
    state.clock[change.actor] = change.seq;
    const auto hash = detail::DocState::compute_change_hash(change);
    std::ranges::for_each(
        change.deps, [&](const ChangeHash& dependency) { std::erase(state.heads, dependency); });
    state.heads.push_back(hash);
    state.change_history.push_back(change);
}

static auto dependencies_are_available(const detail::DocState& state, const Change& change)
    -> bool {
    return std::ranges::all_of(change.deps, [&](const ChangeHash& dependency) {
        return state.has_change_hash(dependency);
    });
}

static void remove_queued_change(detail::DocState& state, const ChangeHash& hash) {
    const auto queued = state.pending_changes.find(hash);
    if (queued == state.pending_changes.end()) {
        return;
    }
    const auto& change = queued->second;
    const auto actor = state.pending_sequences.find(change.actor);
    if (actor != state.pending_sequences.end()) {
        actor->second.erase(change.seq);
        if (actor->second.empty()) {
            state.pending_sequences.erase(actor);
        }
    }
    std::ranges::for_each(change.deps, [&](const ChangeHash& dependency) {
        const auto dependents = state.pending_dependents.find(dependency);
        if (dependents == state.pending_dependents.end()) {
            return;
        }
        dependents->second.erase(hash);
        if (dependents->second.empty()) {
            state.pending_dependents.erase(dependents);
        }
    });
    state.pending_changes.erase(queued);
}

static void apply_ready_changes(detail::DocState& state) {
    auto ready_hashes = std::vector<ChangeHash>{};
    ready_hashes.reserve(state.pending_changes.size());
    std::ranges::for_each(state.pending_changes, [&](const auto& entry) {
        if (dependencies_are_available(state, entry.second)) {
            ready_hashes.push_back(entry.first);
        }
    });
    const auto pending_count = state.pending_changes.size();
    std::ranges::for_each(std::views::iota(std::size_t{0}, pending_count), [&](std::size_t) {
        if (ready_hashes.empty()) {
            return;
        }
        const auto hash = ready_hashes.back();
        ready_hashes.pop_back();
        const auto ready = state.pending_changes.find(hash);
        if (ready == state.pending_changes.end() ||
            !dependencies_are_available(state, ready->second)) {
            return;
        }
        auto dependents = std::vector<ChangeHash>{};
        const auto dependents_entry = state.pending_dependents.find(hash);
        if (dependents_entry != state.pending_dependents.end()) {
            dependents.reserve(dependents_entry->second.size());
            std::ranges::copy(dependents_entry->second, std::back_inserter(dependents));
            state.pending_dependents.erase(dependents_entry);
        }
        auto change = std::move(ready->second);
        remove_queued_change(state, hash);
        apply_change(state, change);
        std::ranges::for_each(dependents, [&](const ChangeHash& dependent) {
            const auto pending = state.pending_changes.find(dependent);
            if (pending != state.pending_changes.end() &&
                dependencies_are_available(state, pending->second)) {
                ready_hashes.push_back(dependent);
            }
        });
    });
}

struct ChangeAdmission {
    ChangeHash hash;
    const Change* change;
};

static auto validate_changes(const detail::DocState& state, const std::vector<Change>& changes)
    -> std::expected<std::vector<ChangeAdmission>, Error> {
    auto admissions = std::vector<ChangeAdmission>{};
    admissions.reserve(changes.size());
    auto supplied_hashes = std::unordered_set<ChangeHash>{};
    supplied_hashes.reserve(changes.size());
    auto supplied_sequences =
        std::unordered_map<ActorId, std::unordered_map<std::uint64_t, ChangeHash>>{};
    auto error = std::optional<Error>{};
    std::ranges::for_each(changes, [&](const Change& change) {
        if (error) {
            return;
        }
        const auto hash = detail::DocState::compute_change_hash(change);
        if (state.has_change_hash(hash) || state.pending_changes.contains(hash)) {
            return;
        }
        if (!supplied_hashes.insert(hash).second) {
            return;
        }
        const auto validation = validate_change(change);
        if (!validation) {
            error = validation.error();
            return;
        }
        const auto clock_entry = state.clock.find(change.actor);
        if (clock_entry != state.clock.end() && clock_entry->second >= change.seq) {
            error = Error{ErrorKind::invalid_change,
                          "change actor sequence contradicts document history"};
            return;
        }
        if (queued_sequence_conflicts(state, change, hash)) {
            error = Error{ErrorKind::invalid_change,
                          "change actor sequence contradicts a queued change"};
            return;
        }
        auto& sequences = supplied_sequences[change.actor];
        const auto [sequence, inserted] = sequences.emplace(change.seq, hash);
        if (!inserted && sequence->second != hash) {
            error = Error{ErrorKind::invalid_change,
                          "change actor sequence contradicts another supplied change"};
            return;
        }
        admissions.push_back(ChangeAdmission{.hash = hash, .change = &change});
    });
    if (error) {
        return std::unexpected{std::move(*error)};
    }
    return admissions;
}

static void queue_change(detail::DocState& state, const ChangeAdmission& admission) {
    const auto inserted = state.pending_changes.emplace(admission.hash, *admission.change).second;
    if (!inserted) {
        return;
    }
    try {
        auto& sequences = state.pending_sequences[admission.change->actor];
        sequences.emplace(admission.change->seq, admission.hash);
        std::ranges::for_each(admission.change->deps, [&](const ChangeHash& dependency) {
            state.pending_dependents[dependency].insert(admission.hash);
        });
    } catch (const std::bad_alloc&) {
        remove_queued_change(state, admission.hash);
        throw;
    }
}

static void admit_changes(detail::DocState& state, const std::vector<ChangeAdmission>& admissions) {
    auto admitted_hashes = std::vector<ChangeHash>{};
    admitted_hashes.reserve(admissions.size());
    try {
        std::ranges::for_each(admissions, [&](const ChangeAdmission& admission) {
            admitted_hashes.push_back(admission.hash);
            queue_change(state, admission);
        });
    } catch (const std::bad_alloc&) {
        std::ranges::for_each(admitted_hashes,
                              [&](const ChangeHash& hash) { remove_queued_change(state, hash); });
        throw;
    }
    apply_ready_changes(state);
}

auto Document::merge(const Document& other) -> std::expected<void, Error> {
    return apply_changes(other.get_changes());
}

auto Document::get_changes() const -> std::vector<Change> {
    auto guard = read_guard();
    return state_->change_history;
}

auto Document::apply_changes(const std::vector<Change>& changes) -> std::expected<void, Error> {
    auto lock = std::unique_lock{mutex_};
    const auto admissions = validate_changes(*state_, changes);
    if (!admissions) {
        return std::unexpected{admissions.error()};
    }
    admit_changes(*state_, *admissions);
    return {};
}

auto Document::get_heads() const -> std::vector<ChangeHash> {
    auto guard = read_guard();
    return state_->heads;
}

auto Document::get_missing_deps(const std::vector<ChangeHash>& heads) const
    -> std::vector<ChangeHash> {
    auto guard = read_guard();
    return state_->get_missing_deps(heads);
}

// -- Phase 4: Binary Serialization --------------------------------------------

auto Document::save() const -> std::vector<std::byte> {
    auto guard = read_guard();
    auto output = std::vector<std::byte>{};
    for (const auto& change : state_->change_history) {
        const auto chunk = storage::serialize_change_chunk(change);
        output.insert(output.end(), chunk.begin(), chunk.end());
    }

    if (output.empty()) {
        auto body = std::vector<std::byte>{std::byte{0x43}};
        body.insert(body.end(), state_->actor.bytes.begin(), state_->actor.bytes.end());
        storage::write_chunk(storage::ChunkType::document, body, output);
    }
    return output;
}

// Intermediate struct for parsed document data (avoids private access issues).
struct ParsedDocData {
    ActorId local_actor;
    std::uint64_t next_counter;
    std::uint64_t local_seq;
    std::vector<Change> changes;
    std::vector<ChangeHash> heads;
    std::map<ActorId, std::uint64_t> clock;
};

static auto parse_empty_document(std::span<const std::byte> data) -> std::optional<ParsedDocData> {
    const auto header = storage::parse_chunk_header(data);
    if (!header || header->type != storage::ChunkType::document)
        return std::nullopt;
    if (!storage::validate_chunk_checksum(*header, data))
        return std::nullopt;
    if (header->body_offset + header->body_length != data.size())
        return std::nullopt;

    const auto body = data.subspan(header->body_offset, header->body_length);
    if (body.size() != ActorId::size + 1 || body.front() != std::byte{0x43}) {
        return std::nullopt;
    }

    auto actor = ActorId{};
    std::memcpy(actor.bytes.data(), body.data() + 1, actor.bytes.size());
    return ParsedDocData{
        .local_actor = actor,
        .next_counter = 1,
        .local_seq = 0,
        .changes = {},
        .heads = {},
        .clock = {},
    };
}

static auto parse_change_stream(std::span<const std::byte> data) -> std::optional<ParsedDocData> {
    if (data.empty())
        return std::nullopt;

    auto changes = std::vector<Change>{};
    auto heads = std::vector<ChangeHash>{};
    auto clock = std::map<ActorId, std::uint64_t>{};
    auto position = std::size_t{0};
    auto local_actor = ActorId{};
    auto next_counter = std::uint64_t{1};
    auto local_seq = std::uint64_t{0};

    while (position < data.size()) {
        const auto remaining = data.subspan(position);
        const auto header = storage::parse_chunk_header(remaining);
        if (!header || header->body_offset + header->body_length > remaining.size()) {
            return std::nullopt;
        }

        const auto chunk_size = header->body_offset + header->body_length;
        const auto parsed_change = storage::parse_change_chunk(remaining.first(chunk_size));
        if (!parsed_change)
            return std::nullopt;

        if (changes.empty()) {
            local_actor = parsed_change->change.actor;
        }
        for (const auto& dependency : parsed_change->change.deps) {
            std::erase(heads, dependency);
        }
        heads.push_back(parsed_change->hash);
        clock[parsed_change->change.actor] =
            std::max(clock[parsed_change->change.actor], parsed_change->change.seq);
        if (parsed_change->change.actor == local_actor) {
            local_seq = std::max(local_seq, parsed_change->change.seq);
            for (const auto& operation : parsed_change->change.operations) {
                next_counter = std::max(next_counter, operation.id.counter + 1);
            }
        }
        changes.push_back(parsed_change->change);
        position += chunk_size;
    }

    return ParsedDocData{
        .local_actor = local_actor,
        .next_counter = next_counter,
        .local_seq = local_seq,
        .changes = std::move(changes),
        .heads = std::move(heads),
        .clock = std::move(clock),
    };
}

auto Document::load(std::span<const std::byte> data) -> std::optional<Document> {
    if (data.size() < 5)
        return std::nullopt;
    auto parsed = parse_empty_document(data);
    if (!parsed) {
        parsed = parse_change_stream(data);
    }
    if (!parsed)
        return std::nullopt;

    auto doc = Document{1u}; // no pool — caller can assign one later
    doc.set_actor_id(parsed->local_actor);
    doc.state_->next_counter = parsed->next_counter;
    doc.state_->local_seq = parsed->local_seq;

    // Replay all changes to rebuild the document state
    for (const auto& change : parsed->changes) {
        for (const auto& op : change.operations) {
            doc.state_->apply_op(op);
        }
    }

    doc.state_->change_history = std::move(parsed->changes);
    doc.state_->heads = std::move(parsed->heads);
    doc.state_->clock = std::move(parsed->clock);

    return doc;
}

// -- Phase 5: Sync Protocol ---------------------------------------------------

// SyncState encode/decode

auto SyncState::encode() const -> std::vector<std::byte> {
    auto s = storage::Serializer{};
    s.write_u8(0x43);  // sync state marker
    s.write_uleb128(shared_heads_.size());
    for (const auto& h : shared_heads_) {
        s.write_change_hash(h);
    }
    return s.take();
}

auto SyncState::decode(std::span<const std::byte> data) -> std::optional<SyncState> {
    auto d = storage::Deserializer{data};
    auto marker = d.read_u8();
    if (!marker || *marker != 0x43) return std::nullopt;

    auto num_heads = d.read_uleb128();
    if (!num_heads) return std::nullopt;

    auto state = SyncState{};
    state.shared_heads_.reserve(static_cast<std::size_t>(*num_heads));
    for (std::uint64_t i = 0; i < *num_heads; ++i) {
        auto h = d.read_change_hash();
        if (!h) return std::nullopt;
        state.shared_heads_.push_back(*h);
    }
    return state;
}

// Determine which change hashes to send based on peer's bloom filter and needs.
static auto get_hashes_to_send(
    const detail::DocState& state,
    const std::vector<Have>& their_have,
    const std::vector<ChangeHash>& their_need) -> std::vector<ChangeHash> {

    if (their_have.empty()) {
        return their_need;
    }

    // Collect all last_sync heads from the Have entries
    auto last_sync_heads = std::vector<ChangeHash>{};
    auto bloom_filters = std::vector<sync::BloomFilter>{};
    for (const auto& have : their_have) {
        last_sync_heads.insert(last_sync_heads.end(),
            have.last_sync.begin(), have.last_sync.end());
        auto bf = sync::BloomFilter::from_bytes(have.bloom_bytes);
        bloom_filters.push_back(bf.value_or(sync::BloomFilter{}));
    }

    // Get all changes since last_sync
    auto hashes = state.get_changes_since(last_sync_heads);

    // Build dependency graph (hash → dependents)
    const auto& hash_idx = state.hash_index();
    auto dependents = std::map<ChangeHash, std::vector<ChangeHash>>{};
    auto change_hashes_set = std::set<ChangeHash>(hashes.begin(), hashes.end());

    for (const auto& h : hashes) {
        auto it = hash_idx.find(h);
        if (it == hash_idx.end()) continue;
        for (const auto& dep : state.change_history[it->second].deps) {
            dependents[dep].push_back(h);
        }
    }

    // Find hashes NOT in any bloom filter
    auto hashes_to_send = std::set<ChangeHash>{};
    for (const auto& h : hashes) {
        auto in_any = std::ranges::any_of(bloom_filters,
            [&](const auto& bf) { return bf.contains_hash(h); });
        if (!in_any) {
            hashes_to_send.insert(h);
        }
    }

    // Transitive closure: if we send X, send everything that depends on X
    auto stack = std::vector<ChangeHash>(hashes_to_send.begin(), hashes_to_send.end());
    while (!stack.empty()) {
        auto h = stack.back();
        stack.pop_back();
        auto dep_it = dependents.find(h);
        if (dep_it != dependents.end()) {
            for (const auto& dependent : dep_it->second) {
                if (hashes_to_send.insert(dependent).second) {
                    stack.push_back(dependent);
                }
            }
        }
    }

    // Build result: explicitly needed first, then our additions in document order
    auto result = std::vector<ChangeHash>{};
    for (const auto& h : their_need) {
        if (!hashes_to_send.contains(h)) {
            result.push_back(h);
        }
    }
    for (const auto& h : hashes) {
        if (hashes_to_send.contains(h)) {
            result.push_back(h);
        }
    }
    return result;
}

auto Document::generate_sync_message(SyncState& sync_state) const
    -> std::optional<SyncMessage> {
    auto guard = read_guard();

    auto our_heads = state_->heads;

    // Determine what we need from them
    auto our_need = std::vector<ChangeHash>{};
    if (sync_state.their_heads_) {
        our_need = state_->get_missing_deps(*sync_state.their_heads_);
    }

    // Build our "have" bloom filter
    auto our_have = std::vector<Have>{};
    auto their_heads_set = std::set<ChangeHash>{};
    if (sync_state.their_heads_) {
        their_heads_set.insert(sync_state.their_heads_->begin(),
                               sync_state.their_heads_->end());
    }

    // Check if all our needs are in their heads (normal case)
    auto all_needs_in_their_heads = std::ranges::all_of(our_need,
        [&](const auto& h) { return their_heads_set.contains(h); });

    if (all_needs_in_their_heads) {
        auto changes_since = state_->get_changes_since(sync_state.shared_heads_);
        auto bloom_hashes = std::vector<ChangeHash>{};
        for (const auto& h : changes_since) {
            bloom_hashes.push_back(h);
        }
        auto bloom = sync::BloomFilter::from_hashes(bloom_hashes);
        our_have.push_back(Have{
            .last_sync = sync_state.shared_heads_,
            .bloom_bytes = bloom.to_bytes(),
        });
    }

    // Determine changes to send
    auto changes_to_send = std::vector<Change>{};
    if (sync_state.their_have_ && sync_state.their_need_) {
        auto hashes = get_hashes_to_send(*state_,
            *sync_state.their_have_, *sync_state.their_need_);

        // Deduplicate with sent_hashes
        auto unsent = std::vector<ChangeHash>{};
        for (const auto& h : hashes) {
            if (!sync_state.sent_hashes_.contains(h)) {
                unsent.push_back(h);
            }
        }

        changes_to_send = state_->get_changes_by_hash(unsent);

        // Track what we're sending
        for (const auto& h : unsent) {
            sync_state.sent_hashes_.insert(h);
        }
    }

    // Should we send a message?
    auto heads_unchanged = (sync_state.last_sent_heads_ == our_heads);
    auto heads_equal = (sync_state.their_heads_.has_value() &&
                        *sync_state.their_heads_ == our_heads);

    if (heads_unchanged && sync_state.have_responded_) {
        if (heads_equal && changes_to_send.empty()) {
            return std::nullopt;  // fully synced
        }
        if (sync_state.in_flight_) {
            return std::nullopt;  // waiting for ack
        }
    }

    // Build and return message
    sync_state.have_responded_ = true;
    sync_state.last_sent_heads_ = our_heads;
    sync_state.in_flight_ = true;

    return SyncMessage{
        .heads = std::move(our_heads),
        .need = std::move(our_need),
        .have = std::move(our_have),
        .changes = std::move(changes_to_send),
    };
}

auto Document::receive_sync_message(SyncState& sync_state, const SyncMessage& message)
    -> std::expected<void, Error> {
    auto result = apply_changes(message.changes);
    if (!result) {
        return std::unexpected{result.error()};
    }

    auto lock = std::unique_lock{mutex_};

    // Clear in-flight flag (ack)
    sync_state.in_flight_ = false;

    if (!message.changes.empty()) {
        // Advance shared_heads: keep new heads that appeared
        sync_state.shared_heads_ = state_->heads;
    }

    // Trim sent_hashes: remove ancestors of their acknowledged heads
    if (!message.heads.empty()) {
        const auto& hash_idx = state_->hash_index();
        auto ancestors = std::set<ChangeHash>{};
        auto queue = std::vector<ChangeHash>{};

        for (const auto& h : message.heads) {
            if (hash_idx.contains(h)) {
                ancestors.insert(h);
                queue.push_back(h);
            }
        }
        while (!queue.empty()) {
            auto h = queue.back();
            queue.pop_back();
            auto it = hash_idx.find(h);
            if (it == hash_idx.end()) continue;
            for (const auto& dep : state_->change_history[it->second].deps) {
                if (ancestors.insert(dep).second) {
                    queue.push_back(dep);
                }
            }
        }

        std::erase_if(sync_state.sent_hashes_,
            [&](const auto& h) { return ancestors.contains(h); });
    }

    // Update shared_heads based on what we now know they have
    auto known_heads = std::vector<ChangeHash>{};
    for (const auto& h : message.heads) {
        if (state_->has_change_hash(h)) {
            known_heads.push_back(h);
        }
    }

    if (known_heads.size() == message.heads.size()) {
        // We know all their heads
        sync_state.shared_heads_ = message.heads;
        if (message.heads.empty()) {
            sync_state.last_sent_heads_.clear();
            sync_state.sent_hashes_.clear();
        }
    } else {
        // Merge known heads into shared
        for (const auto& h : known_heads) {
            if (std::ranges::find(sync_state.shared_heads_, h) ==
                sync_state.shared_heads_.end()) {
                sync_state.shared_heads_.push_back(h);
            }
        }
        std::ranges::sort(sync_state.shared_heads_);
    }

    // Store peer info for next generate_sync_message
    sync_state.their_have_ = message.have;
    sync_state.their_heads_ = message.heads;
    sync_state.their_need_ = message.need;
    return {};
}

// -- Phase 6: Patches ---------------------------------------------------------

// Convert a sequence of ops (from a transaction) into patches.
static auto ops_to_patches(const std::vector<Op>& ops) -> std::vector<Patch> {
    auto patches = std::vector<Patch>{};
    std::size_t i = 0;

    while (i < ops.size()) {
        const auto& op = ops[i];

        // Coalesce list deletes (may be part of splice_text)
        if (op.action == OpType::del && std::holds_alternative<std::size_t>(op.key)) {
            auto obj = op.obj;
            auto index = std::get<std::size_t>(op.key);
            std::size_t count = 1;
            while (i + count < ops.size() &&
                   ops[i + count].action == OpType::del &&
                   ops[i + count].obj == obj &&
                   std::holds_alternative<std::size_t>(ops[i + count].key) &&
                   std::get<std::size_t>(ops[i + count].key) == index) {
                ++count;
            }

            // Check if followed by splice_text inserts (= splice_text call)
            auto splice_start = i + count;
            if (splice_start < ops.size() &&
                ops[splice_start].action == OpType::splice_text &&
                ops[splice_start].obj == obj) {
                auto text = std::string{};
                auto j = splice_start;
                while (j < ops.size() &&
                       ops[j].action == OpType::splice_text &&
                       ops[j].obj == obj) {
                    if (const auto* sv = std::get_if<ScalarValue>(&ops[j].value)) {
                        if (const auto* s = std::get_if<std::string>(sv)) {
                            text += *s;
                        }
                    }
                    ++j;
                }
                patches.push_back(Patch{
                    .obj = obj,
                    .key = list_index(index),
                    .action = PatchSpliceText{index, count, std::move(text)},
                });
                i = j;
            } else {
                patches.push_back(Patch{
                    .obj = obj,
                    .key = list_index(index),
                    .action = PatchDelete{index, count},
                });
                i += count;
            }
            continue;
        }

        // Coalesce splice_text inserts (insert-only, no preceding deletes)
        if (op.action == OpType::splice_text) {
            auto obj = op.obj;
            auto index = std::get<std::size_t>(op.key);
            auto text = std::string{};
            auto j = i;
            while (j < ops.size() &&
                   ops[j].action == OpType::splice_text &&
                   ops[j].obj == obj) {
                if (const auto* sv = std::get_if<ScalarValue>(&ops[j].value)) {
                    if (const auto* s = std::get_if<std::string>(sv)) {
                        text += *s;
                    }
                }
                ++j;
            }
            patches.push_back(Patch{
                .obj = obj,
                .key = list_index(index),
                .action = PatchSpliceText{index, 0, std::move(text)},
            });
            i = j;
            continue;
        }

        // Single-op patches
        if (op.action == OpType::put || op.action == OpType::make_object) {
            patches.push_back(Patch{
                .obj = op.obj,
                .key = op.key,
                .action = PatchPut{op.value, false},
            });
        } else if (op.action == OpType::insert) {
            auto index = std::get<std::size_t>(op.key);
            patches.push_back(Patch{
                .obj = op.obj,
                .key = op.key,
                .action = PatchInsert{index, op.value},
            });
        } else if (op.action == OpType::del) {
            // Map key deletion
            patches.push_back(Patch{
                .obj = op.obj,
                .key = op.key,
                .action = PatchDelete{0, 1},
            });
        } else if (op.action == OpType::increment) {
            if (const auto* sv = std::get_if<ScalarValue>(&op.value)) {
                if (const auto* c = std::get_if<Counter>(sv)) {
                    patches.push_back(Patch{
                        .obj = op.obj,
                        .key = op.key,
                        .action = PatchIncrement{c->value},
                    });
                }
            }
        }
        ++i;
    }

    return patches;
}

auto Document::transact_with_patches(const std::function<void(Transaction&)>& fn)
    -> std::vector<Patch> {
    auto lock = std::unique_lock{mutex_};
    auto tx = Transaction{*state_};
    fn(tx);

    // Capture ops before commit moves them
    auto ops = tx.pending_ops_;
    tx.commit();

    return ops_to_patches(ops);
}

// -- Phase 6: Historical reads ------------------------------------------------

auto Document::get_at(const ObjId& obj, std::string_view key,
                      const std::vector<ChangeHash>& heads) const -> std::optional<Value> {
    auto guard = read_guard();
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.map_get(obj, std::string{key});
}

auto Document::get_at(const ObjId& obj, std::size_t index,
                      const std::vector<ChangeHash>& heads) const -> std::optional<Value> {
    auto guard = read_guard();
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.list_get(obj, index);
}

auto Document::keys_at(const ObjId& obj,
                       const std::vector<ChangeHash>& heads) const -> std::vector<std::string> {
    auto guard = read_guard();
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.map_keys(obj);
}

auto Document::values_at(const ObjId& obj,
                         const std::vector<ChangeHash>& heads) const -> std::vector<Value> {
    auto guard = read_guard();
    auto snapshot = state_->rebuild_state_at(heads);
    auto type = snapshot.object_type(obj);
    if (!type) return {};
    switch (*type) {
        case ObjType::map:
        case ObjType::table:
            return snapshot.map_values(obj);
        case ObjType::list:
        case ObjType::text:
            return snapshot.list_values(obj);
    }
    return {};
}

auto Document::length_at(const ObjId& obj,
                         const std::vector<ChangeHash>& heads) const -> std::size_t {
    auto guard = read_guard();
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.object_length(obj);
}

auto Document::text_at(const ObjId& obj,
                       const std::vector<ChangeHash>& heads) const -> std::string {
    auto guard = read_guard();
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.text_content(obj);
}

// -- Phase 6: Cursors ---------------------------------------------------------

auto Document::cursor(const ObjId& obj, std::size_t index) const
    -> std::optional<Cursor> {
    auto guard = read_guard();
    auto id = state_->list_element_id_at(obj, index);
    if (!id) return std::nullopt;
    return Cursor{*id};
}

auto Document::resolve_cursor(const ObjId& obj, const Cursor& cur) const
    -> std::optional<std::size_t> {
    auto guard = read_guard();
    return state_->find_element_visible_index(obj, cur.position);
}

// -- Rich text marks ----------------------------------------------------------

static auto collect_marks(const detail::DocState& state, const ObjId& obj)
    -> std::vector<Mark> {
    const auto* obj_state = state.get_object(obj);
    if (!obj_state) return {};

    auto result = std::vector<Mark>{};
    for (const auto& entry : obj_state->marks) {
        auto indices = state.resolve_mark_indices(*obj_state, entry);
        if (!indices) continue;
        auto [start, end] = *indices;
        result.push_back(Mark{
            .start = start,
            .end = end,
            .name = entry.name,
            .value = entry.value,
        });
    }
    return result;
}

auto Document::marks(const ObjId& obj) const -> std::vector<Mark> {
    auto guard = read_guard();
    return collect_marks(*state_, obj);
}

auto Document::marks_at(const ObjId& obj,
                        const std::vector<ChangeHash>& heads) const -> std::vector<Mark> {
    auto guard = read_guard();
    auto snapshot = state_->rebuild_state_at(heads);
    return collect_marks(snapshot, obj);
}

// -- Phase 12A: Modern API helpers --------------------------------------------

auto Document::ops_to_patches_internal(const std::vector<Op>& ops) -> std::vector<Patch> {
    return ops_to_patches(ops);
}

auto Document::get_path_impl(std::span<const Prop> path) const -> std::optional<Value> {
    auto guard = read_guard();
    if (path.empty()) return std::nullopt;

    auto current_obj = root;

    // Walk all but the last path element to resolve intermediate ObjIds.
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        auto val = std::visit(overload{
            [&](const std::string& key) { return state_->map_get(current_obj, key); },
            [&](std::size_t index) { return state_->list_get(current_obj, index); },
        }, path[i]);

        if (!val) return std::nullopt;

        // The value must be an ObjType to continue navigating.
        if (!is_object(*val)) return std::nullopt;

        // We need the ObjId, not the ObjType. For intermediate objects,
        // the ObjId is the OpId that created them. We look it up via
        // the internal state's object-at-key resolution.
        auto obj_id = state_->get_obj_id_at(current_obj, path[i]);
        if (!obj_id) return std::nullopt;
        current_obj = *obj_id;
    }

    // Final element: return the value.
    return std::visit(overload{
        [&](const std::string& key) { return state_->map_get(current_obj, key); },
        [&](std::size_t index) { return state_->list_get(current_obj, index); },
    }, path.back());
}

}  // namespace automerge_cpp
