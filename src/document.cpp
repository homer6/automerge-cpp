#include <automerge-cpp/document.hpp>
#include <automerge-cpp/cursor.hpp>
#include <automerge-cpp/patch.hpp>

#include "doc_state.hpp"
#include "storage/serializer.hpp"
#include "storage/deserializer.hpp"
#include "sync/bloom_filter.hpp"

#include <algorithm>
#include <ranges>
#include <set>
#include <unordered_set>

namespace automerge_cpp {

Document::Document()
    : state_{std::make_unique<detail::DocState>()} {}

Document::~Document() = default;

Document::Document(Document&&) noexcept = default;
auto Document::operator=(Document&&) noexcept -> Document& = default;

Document::Document(const Document& other)
    : state_{std::make_unique<detail::DocState>(*other.state_)} {}

auto Document::operator=(const Document& other) -> Document& {
    if (this != &other) {
        state_ = std::make_unique<detail::DocState>(*other.state_);
    }
    return *this;
}

auto Document::actor_id() const -> const ActorId& {
    return state_->actor;
}

void Document::set_actor_id(ActorId id) {
    state_->actor = std::move(id);
}

void Document::transact(std::function<void(Transaction&)> fn) {
    auto tx = Transaction{*state_};
    fn(tx);
    tx.commit();
}

auto Document::get(const ObjId& obj, std::string_view key) const -> std::optional<Value> {
    return state_->map_get(obj, std::string{key});
}

auto Document::get_all(const ObjId& obj, std::string_view key) const -> std::vector<Value> {
    return state_->map_get_all(obj, std::string{key});
}

auto Document::get(const ObjId& obj, std::size_t index) const -> std::optional<Value> {
    return state_->list_get(obj, index);
}

auto Document::keys(const ObjId& obj) const -> std::vector<std::string> {
    return state_->map_keys(obj);
}

auto Document::values(const ObjId& obj) const -> std::vector<Value> {
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
    return state_->object_length(obj);
}

auto Document::text(const ObjId& obj) const -> std::string {
    return state_->text_content(obj);
}

auto Document::object_type(const ObjId& obj) const -> std::optional<ObjType> {
    return state_->object_type(obj);
}

// -- Phase 3: Fork and Merge --------------------------------------------------

auto Document::fork() const -> Document {
    auto forked = Document{*this};
    // Create a unique actor by incrementing the last byte
    auto new_actor = state_->actor;
    new_actor.bytes[15] = static_cast<std::byte>(
        static_cast<std::uint8_t>(new_actor.bytes[15]) + 1);
    forked.set_actor_id(new_actor);
    return forked;
}

void Document::merge(const Document& other) {
    // Find changes from other that we haven't seen
    auto missing = std::vector<Change>{};
    for (const auto& change : other.state_->change_history) {
        auto it = state_->clock.find(change.actor);
        if (it == state_->clock.end() || it->second < change.seq) {
            missing.push_back(change);
        }
    }
    // Sort by start_op for causal ordering
    std::ranges::sort(missing, [](const Change& a, const Change& b) {
        return a.start_op < b.start_op;
    });
    apply_changes(missing);
}

auto Document::get_changes() const -> std::vector<Change> {
    return state_->change_history;
}

void Document::apply_changes(const std::vector<Change>& changes) {
    for (const auto& change : changes) {
        // Apply each operation
        for (const auto& op : change.operations) {
            state_->apply_op(op);
            state_->op_log.push_back(op);
        }

        // Update clock
        auto& seq = state_->clock[change.actor];
        seq = std::max(seq, change.seq);

        // Update heads
        auto hash = detail::DocState::compute_change_hash(change);
        // Remove deps from heads
        for (const auto& dep : change.deps) {
            std::erase(state_->heads, dep);
        }
        state_->heads.push_back(hash);

        // Store change
        state_->change_history.push_back(change);
    }
}

auto Document::get_heads() const -> std::vector<ChangeHash> {
    return state_->heads;
}

// -- Phase 4: Binary Serialization --------------------------------------------

// Format: Magic(4) + Version(1) + ActorTable + Changes + Heads + LocalState

static constexpr std::uint8_t MAGIC[] = {0x85, 0x6F, 0x4A, 0x83};
static constexpr std::uint8_t FORMAT_VERSION = 0x01;

// Build a deduplicated actor table from all changes
static auto build_actor_table(const detail::DocState& state) -> std::vector<ActorId> {
    auto seen = std::unordered_set<ActorId>{};
    auto table = std::vector<ActorId>{};

    // Local actor first
    if (seen.insert(state.actor).second) {
        table.push_back(state.actor);
    }

    for (const auto& change : state.change_history) {
        if (seen.insert(change.actor).second) {
            table.push_back(change.actor);
        }
        for (const auto& op : change.operations) {
            if (seen.insert(op.id.actor).second) {
                table.push_back(op.id.actor);
            }
        }
    }
    return table;
}

auto Document::save() const -> std::vector<std::byte> {
    auto s = storage::Serializer{};

    auto actor_table = build_actor_table(*state_);

    // Magic bytes + version
    s.write_raw_bytes(MAGIC, 4);
    s.write_u8(FORMAT_VERSION);

    // Actor table
    s.write_uleb128(actor_table.size());
    for (const auto& actor : actor_table) {
        s.write_actor_id(actor);
    }

    // Local actor index
    for (std::size_t i = 0; i < actor_table.size(); ++i) {
        if (actor_table[i] == state_->actor) {
            s.write_uleb128(i);
            break;
        }
    }

    // Local state
    s.write_uleb128(state_->next_counter);
    s.write_uleb128(state_->local_seq);

    // Changes
    s.write_uleb128(state_->change_history.size());
    for (const auto& change : state_->change_history) {
        // Actor index
        for (std::size_t i = 0; i < actor_table.size(); ++i) {
            if (actor_table[i] == change.actor) {
                s.write_uleb128(i);
                break;
            }
        }
        s.write_uleb128(change.seq);
        s.write_uleb128(change.start_op);
        s.write_sleb128(change.timestamp);

        // Message
        if (change.message) {
            s.write_u8(1);
            s.write_string(*change.message);
        } else {
            s.write_u8(0);
        }

        // Deps
        s.write_uleb128(change.deps.size());
        for (const auto& dep : change.deps) {
            s.write_change_hash(dep);
        }

        // Operations
        s.write_uleb128(change.operations.size());
        for (const auto& op : change.operations) {
            s.write_op_id(op.id, actor_table);
            s.write_obj_id(op.obj, actor_table);
            s.write_prop(op.key);
            s.write_u8(static_cast<std::uint8_t>(op.action));
            s.write_value(op.value);

            // Predecessors
            s.write_uleb128(op.pred.size());
            for (const auto& p : op.pred) {
                s.write_op_id(p, actor_table);
            }

            // Insert_after
            if (op.insert_after) {
                s.write_u8(1);
                s.write_op_id(*op.insert_after, actor_table);
            } else {
                s.write_u8(0);
            }
        }
    }

    // Heads
    s.write_uleb128(state_->heads.size());
    for (const auto& h : state_->heads) {
        s.write_change_hash(h);
    }

    // Clock
    s.write_uleb128(state_->clock.size());
    for (const auto& [actor, seq] : state_->clock) {
        for (std::size_t i = 0; i < actor_table.size(); ++i) {
            if (actor_table[i] == actor) {
                s.write_uleb128(i);
                break;
            }
        }
        s.write_uleb128(seq);
    }

    return s.take();
}

auto Document::load(std::span<const std::byte> data) -> std::optional<Document> {
    auto d = storage::Deserializer{data};

    // Verify magic bytes
    auto magic = d.read_bytes(4);
    if (!magic) return std::nullopt;
    for (int i = 0; i < 4; ++i) {
        if ((*magic)[i] != static_cast<std::byte>(MAGIC[i])) return std::nullopt;
    }

    // Version
    auto version = d.read_u8();
    if (!version || *version != FORMAT_VERSION) return std::nullopt;

    // Actor table
    auto num_actors = d.read_uleb128();
    if (!num_actors) return std::nullopt;
    auto actor_table = std::vector<ActorId>{};
    actor_table.reserve(static_cast<std::size_t>(*num_actors));
    for (std::uint64_t i = 0; i < *num_actors; ++i) {
        auto actor = d.read_actor_id();
        if (!actor) return std::nullopt;
        actor_table.push_back(*actor);
    }

    // Local actor index
    auto actor_idx = d.read_uleb128();
    if (!actor_idx || *actor_idx >= actor_table.size()) return std::nullopt;

    // Local state
    auto next_counter = d.read_uleb128();
    if (!next_counter) return std::nullopt;
    auto local_seq = d.read_uleb128();
    if (!local_seq) return std::nullopt;

    // Changes
    auto num_changes = d.read_uleb128();
    if (!num_changes) return std::nullopt;

    auto changes = std::vector<Change>{};
    changes.reserve(static_cast<std::size_t>(*num_changes));

    for (std::uint64_t ci = 0; ci < *num_changes; ++ci) {
        auto change_actor_idx = d.read_uleb128();
        if (!change_actor_idx || *change_actor_idx >= actor_table.size()) return std::nullopt;

        auto seq = d.read_uleb128();
        if (!seq) return std::nullopt;
        auto start_op = d.read_uleb128();
        if (!start_op) return std::nullopt;
        auto timestamp = d.read_sleb128();
        if (!timestamp) return std::nullopt;

        // Message
        auto has_msg = d.read_u8();
        if (!has_msg) return std::nullopt;
        auto message = std::optional<std::string>{};
        if (*has_msg == 1) {
            auto msg = d.read_string();
            if (!msg) return std::nullopt;
            message = std::move(*msg);
        }

        // Deps
        auto num_deps = d.read_uleb128();
        if (!num_deps) return std::nullopt;
        auto deps = std::vector<ChangeHash>{};
        deps.reserve(static_cast<std::size_t>(*num_deps));
        for (std::uint64_t di = 0; di < *num_deps; ++di) {
            auto dep = d.read_change_hash();
            if (!dep) return std::nullopt;
            deps.push_back(*dep);
        }

        // Operations
        auto num_ops = d.read_uleb128();
        if (!num_ops) return std::nullopt;
        auto operations = std::vector<Op>{};
        operations.reserve(static_cast<std::size_t>(*num_ops));

        for (std::uint64_t oi = 0; oi < *num_ops; ++oi) {
            auto id = d.read_op_id(actor_table);
            if (!id) return std::nullopt;
            auto obj = d.read_obj_id(actor_table);
            if (!obj) return std::nullopt;
            auto key = d.read_prop();
            if (!key) return std::nullopt;
            auto action = d.read_u8();
            if (!action) return std::nullopt;
            auto value = d.read_value();
            if (!value) return std::nullopt;

            // Predecessors
            auto num_pred = d.read_uleb128();
            if (!num_pred) return std::nullopt;
            auto pred = std::vector<OpId>{};
            pred.reserve(static_cast<std::size_t>(*num_pred));
            for (std::uint64_t pi = 0; pi < *num_pred; ++pi) {
                auto p = d.read_op_id(actor_table);
                if (!p) return std::nullopt;
                pred.push_back(*p);
            }

            // Insert_after
            auto has_ia = d.read_u8();
            if (!has_ia) return std::nullopt;
            auto insert_after = std::optional<OpId>{};
            if (*has_ia == 1) {
                auto ia = d.read_op_id(actor_table);
                if (!ia) return std::nullopt;
                insert_after = *ia;
            }

            operations.push_back(Op{
                .id = *id,
                .obj = *obj,
                .key = std::move(*key),
                .action = static_cast<OpType>(*action),
                .value = std::move(*value),
                .pred = std::move(pred),
                .insert_after = insert_after,
            });
        }

        changes.push_back(Change{
            .actor = actor_table[static_cast<std::size_t>(*change_actor_idx)],
            .seq = *seq,
            .start_op = *start_op,
            .timestamp = *timestamp,
            .message = std::move(message),
            .deps = std::move(deps),
            .operations = std::move(operations),
        });
    }

    // Heads
    auto num_heads = d.read_uleb128();
    if (!num_heads) return std::nullopt;
    auto heads = std::vector<ChangeHash>{};
    heads.reserve(static_cast<std::size_t>(*num_heads));
    for (std::uint64_t i = 0; i < *num_heads; ++i) {
        auto h = d.read_change_hash();
        if (!h) return std::nullopt;
        heads.push_back(*h);
    }

    // Clock
    auto num_clock = d.read_uleb128();
    if (!num_clock) return std::nullopt;
    auto clock = std::map<ActorId, std::uint64_t>{};
    for (std::uint64_t i = 0; i < *num_clock; ++i) {
        auto cidx = d.read_uleb128();
        if (!cidx || *cidx >= actor_table.size()) return std::nullopt;
        auto cseq = d.read_uleb128();
        if (!cseq) return std::nullopt;
        clock[actor_table[static_cast<std::size_t>(*cidx)]] = *cseq;
    }

    // Reconstruct document by replaying changes
    auto doc = Document{};
    doc.set_actor_id(actor_table[static_cast<std::size_t>(*actor_idx)]);
    doc.state_->next_counter = *next_counter;
    doc.state_->local_seq = *local_seq;

    // Replay all changes to rebuild the document state
    for (const auto& change : changes) {
        for (const auto& op : change.operations) {
            doc.state_->apply_op(op);
            doc.state_->op_log.push_back(op);
        }
    }

    // Restore metadata
    doc.state_->change_history = std::move(changes);
    doc.state_->heads = std::move(heads);
    doc.state_->clock = std::move(clock);

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
    auto hash_idx = state.change_hash_index();
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

    auto our_heads = get_heads();

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

void Document::receive_sync_message(SyncState& sync_state,
                                     const SyncMessage& message) {
    // Clear in-flight flag (ack)
    sync_state.in_flight_ = false;

    // Apply changes from message
    if (!message.changes.empty()) {
        apply_changes(message.changes);

        // Advance shared_heads: keep new heads that appeared
        auto after_heads = get_heads();
        sync_state.shared_heads_ = after_heads;
    }

    // Trim sent_hashes: remove ancestors of their acknowledged heads
    if (!message.heads.empty()) {
        auto hash_idx = state_->change_hash_index();
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

auto Document::transact_with_patches(std::function<void(Transaction&)> fn)
    -> std::vector<Patch> {
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
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.map_get(obj, std::string{key});
}

auto Document::get_at(const ObjId& obj, std::size_t index,
                      const std::vector<ChangeHash>& heads) const -> std::optional<Value> {
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.list_get(obj, index);
}

auto Document::keys_at(const ObjId& obj,
                       const std::vector<ChangeHash>& heads) const -> std::vector<std::string> {
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.map_keys(obj);
}

auto Document::values_at(const ObjId& obj,
                         const std::vector<ChangeHash>& heads) const -> std::vector<Value> {
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
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.object_length(obj);
}

auto Document::text_at(const ObjId& obj,
                       const std::vector<ChangeHash>& heads) const -> std::string {
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.text_content(obj);
}

// -- Phase 6: Cursors ---------------------------------------------------------

auto Document::cursor(const ObjId& obj, std::size_t index) const
    -> std::optional<Cursor> {
    auto id = state_->list_element_id_at(obj, index);
    if (!id) return std::nullopt;
    return Cursor{*id};
}

auto Document::resolve_cursor(const ObjId& obj, const Cursor& cur) const
    -> std::optional<std::size_t> {
    return state_->find_element_visible_index(obj, cur.position);
}

}  // namespace automerge_cpp
