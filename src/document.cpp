#include <automerge-cpp/cursor.hpp>
#include <automerge-cpp/document.hpp>
#include <automerge-cpp/mark.hpp>
#include <automerge-cpp/patch.hpp>
#include <automerge-cpp/thread_pool.hpp>

#include "doc_state.hpp"
#include "encoding/leb128.hpp"
#include "storage/change_chunk.hpp"
#include "storage/chunk.hpp"
#include "storage/deserializer.hpp"
#include "storage/document_chunk.hpp"
#include "storage/document_chunk_writer.hpp"
#include "storage/serializer.hpp"
#include "sync/bloom_filter.hpp"
#include "sync_message_encoding.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cstdlib>
#include <expected>
#include <limits>
#include <new>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>

#include <bcrypt.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <sys/random.h>
#else
#include <unistd.h>
#endif

namespace automerge_cpp {

static void fill_random_bytes(std::span<std::byte> bytes) {
#if defined(_WIN32)
    const auto status = BCryptGenRandom(nullptr,
                                        reinterpret_cast<PUCHAR>(bytes.data()),
                                        static_cast<ULONG>(bytes.size()),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        std::abort();
    }
#else
    if (getentropy(bytes.data(), bytes.size()) != 0) {
        std::abort();
    }
#endif
}

static auto make_random_actor_id() -> ActorId {
    auto actor = ActorId{};
    fill_random_bytes(actor.bytes);
    if (actor.is_zero()) {
        return make_random_actor_id();
    }
    return actor;
}

static auto make_default_pool() -> std::shared_ptr<thread_pool> {
    auto n = std::thread::hardware_concurrency();
    if (n <= 1)
        return nullptr;
    return std::make_shared<thread_pool>(n);
}

static auto make_pool(unsigned int num_threads) -> std::shared_ptr<thread_pool> {
    if (num_threads == 1)
        return nullptr;
    auto n = (num_threads == 0) ? std::thread::hardware_concurrency() : num_threads;
    if (n <= 1)
        return nullptr;
    return std::make_shared<thread_pool>(n);
}

Document::Document() : state_{std::make_unique<detail::DocState>()}, pool_{nullptr} {
    state_->actor = make_random_actor_id();
}

Document::Document(unsigned int num_threads)
    : state_{std::make_unique<detail::DocState>()}, pool_{make_pool(num_threads)} {
    state_->actor = make_random_actor_id();
}

Document::Document(std::shared_ptr<thread_pool> pool)
    : state_{std::make_unique<detail::DocState>()}, pool_{std::move(pool)} {
    state_->actor = make_random_actor_id();
}

Document::~Document() = default;

Document::Document(Document&& other) noexcept
    : state_{std::move(other.state_)}, pool_{std::move(other.pool_)},
      save_heads_{std::move(other.save_heads_)} {}

auto Document::operator=(Document&& other) noexcept -> Document& {
    if (this != &other) {
        auto lock = std::unique_lock{mutex_};
        state_ = std::move(other.state_);
        pool_ = std::move(other.pool_);
        save_heads_ = std::move(other.save_heads_);
    }
    return *this;
}

Document::Document(const Document& other)
    : state_{std::make_unique<detail::DocState>(*other.state_)}, pool_{other.pool_},
      save_heads_{other.save_heads_} {}

auto Document::operator=(const Document& other) -> Document& {
    if (this != &other) {
        auto lock = std::unique_lock{mutex_};
        state_ = std::make_unique<detail::DocState>(*other.state_);
        pool_ = other.pool_;
        save_heads_ = other.save_heads_;
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
    const auto clock = state_->clock.find(id);
    state_->local_seq = clock == state_->clock.end() ? 0 : clock->second;
}

void Document::transact(const std::function<void(Transaction&)>& fn, CommitOptions options) {
    auto lock = std::unique_lock{mutex_};
    auto tx = Transaction{*state_};
    try {
        fn(tx);
        tx.commit(std::move(options));
    } catch (...) {
        tx.rollback();
        throw;
    }
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
    if (!type)
        return {};

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
    auto forked = Document{pool_};
    const auto actor = forked.state_->actor;
    *forked.state_ = *state_;
    forked.state_->actor = actor;
    forked.state_->local_seq = 0;
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

static auto validate_text_operations(const detail::DocState& state,
                                     const std::vector<Change>& changes)
    -> std::expected<void, Error> {
    auto incoming_object_types = std::unordered_map<ObjId, ObjType>{};
    std::ranges::for_each(changes, [&](const Change& change) {
        std::ranges::for_each(change.operations, [&](const Op& operation) {
            if (const auto* type = std::get_if<ObjType>(&operation.value)) {
                incoming_object_types.emplace(ObjId{operation.id}, *type);
            }
        });
    });
    const auto text_operations_are_scalars =
        std::ranges::all_of(changes, [&](const Change& change) {
            return std::ranges::all_of(change.operations, [&](const Op& operation) {
                if (operation.action != OpType::splice_text) {
                    return true;
                }
                const auto incoming_type = incoming_object_types.find(operation.obj);
                const auto type = incoming_type != incoming_object_types.end()
                                      ? std::optional{incoming_type->second}
                                      : state.object_type(operation.obj);
                if (!type || *type != ObjType::text) {
                    return true;
                }
                const auto* scalar = std::get_if<ScalarValue>(&operation.value);
                if (!scalar) {
                    return false;
                }
                const auto* text = std::get_if<std::string>(scalar);
                return text && detail::is_single_utf8_scalar(*text);
            });
        });
    if (!text_operations_are_scalars) {
        return std::unexpected{
            Error{ErrorKind::invalid_change,
                  "text insertion operations must contain one valid UTF-8 scalar value"}};
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

static void
apply_change(detail::DocState& state, const Change& change, storage::EncodedChange encoded) {
    std::ranges::for_each(change.operations,
                          [&](const Op& operation) { state.apply_op(operation); });
    state.clock[change.actor] = change.seq;
    std::ranges::for_each(
        change.deps, [&](const ChangeHash& dependency) { std::erase(state.heads, dependency); });
    state.heads.push_back(encoded.hash);
    std::ranges::sort(state.heads);
    state.append_materialized_change(change, std::move(encoded));
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
    state.pending_change_encodings.erase(hash);
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
        const auto encoding_entry = state.pending_change_encodings.find(hash);
        assert(encoding_entry != state.pending_change_encodings.end());
        auto encoded = std::move(encoding_entry->second);
        remove_queued_change(state, hash);
        apply_change(state, change, std::move(encoded));
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
    storage::EncodedChange encoded;
};

static auto validate_changes(const detail::DocState& state,
                             const std::vector<Change>& changes,
                             std::vector<storage::EncodedChange>* encodings = nullptr)
    -> std::expected<std::vector<ChangeAdmission>, Error> {
    const auto text_validation = validate_text_operations(state, changes);
    if (!text_validation) {
        return std::unexpected{text_validation.error()};
    }
    auto admissions = std::vector<ChangeAdmission>{};
    admissions.reserve(changes.size());
    auto supplied_hashes = std::unordered_set<ChangeHash>{};
    supplied_hashes.reserve(changes.size());
    auto supplied_sequences =
        std::unordered_map<ActorId, std::unordered_map<std::uint64_t, ChangeHash>>{};
    auto error = std::optional<Error>{};
    if (encodings != nullptr) {
        assert(encodings->size() == changes.size());
    }
    std::ranges::for_each(std::views::iota(std::size_t{0}, changes.size()), [&](std::size_t index) {
        const auto& change = changes[index];
        if (error) {
            return;
        }
        auto encoded =
            encodings == nullptr ? storage::encode_change(change) : std::move((*encodings)[index]);
        const auto hash = encoded.hash;
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
        admissions.push_back(ChangeAdmission{
            .hash = hash,
            .change = &change,
            .encoded = std::move(encoded),
        });
    });
    if (error) {
        return std::unexpected{std::move(*error)};
    }
    return admissions;
}

static void queue_change(detail::DocState& state, ChangeAdmission& admission) {
    const auto inserted = state.pending_changes.emplace(admission.hash, *admission.change).second;
    if (!inserted) {
        return;
    }
    try {
        state.pending_change_encodings.emplace(admission.hash, std::move(admission.encoded));
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

static void admit_changes(detail::DocState& state, std::vector<ChangeAdmission>& admissions) {
    auto admitted_hashes = std::vector<ChangeHash>{};
    admitted_hashes.reserve(admissions.size());
    try {
        std::ranges::for_each(admissions, [&](ChangeAdmission& admission) {
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

using ObjectMap = std::unordered_map<ObjId, detail::ObjectState>;

static auto winning_entry(const std::vector<detail::MapEntry>& entries) -> const detail::MapEntry* {
    if (entries.empty()) {
        return nullptr;
    }
    return &*std::ranges::max_element(
        entries, [](const auto& left, const auto& right) { return left.op_id < right.op_id; });
}

static auto map_entries_equal(const std::vector<detail::MapEntry>& before,
                              const std::vector<detail::MapEntry>& after) -> bool {
    if (before.size() != after.size()) {
        return false;
    }
    return std::ranges::all_of(before, [&](const detail::MapEntry& entry) {
        return std::ranges::any_of(after, [&](const detail::MapEntry& candidate) {
            return candidate.op_id == entry.op_id && candidate.value == entry.value;
        });
    });
}

static auto counter_value(const Value& value) -> std::optional<std::int64_t> {
    const auto* scalar = std::get_if<ScalarValue>(&value);
    if (scalar == nullptr) {
        return std::nullopt;
    }
    const auto* counter = std::get_if<Counter>(scalar);
    if (counter == nullptr) {
        return std::nullopt;
    }
    return counter->value;
}

static auto counter_increment(const std::vector<detail::MapEntry>& before,
                              const std::vector<detail::MapEntry>& after)
    -> std::optional<std::int64_t> {
    if (before.size() != after.size() || before.empty()) {
        return std::nullopt;
    }
    auto delta = std::optional<std::int64_t>{};
    const auto all_counters_changed_equally =
        std::ranges::all_of(before, [&](const detail::MapEntry& entry) {
            const auto candidate = std::ranges::find(after, entry.op_id, &detail::MapEntry::op_id);
            if (candidate == after.end()) {
                return false;
            }
            const auto before_value = counter_value(entry.value);
            const auto after_value = counter_value(candidate->value);
            if (!before_value || !after_value) {
                return false;
            }
            const auto candidate_delta = *after_value - *before_value;
            if (!delta) {
                delta = candidate_delta;
            }
            return *delta == candidate_delta;
        });
    if (!all_counters_changed_equally || !delta || *delta == 0) {
        return std::nullopt;
    }
    return delta;
}

static auto map_entries(const detail::ObjectState* object, const std::string& key)
    -> const std::vector<detail::MapEntry>* {
    if (object == nullptr) {
        return nullptr;
    }
    const auto entry = object->map_entries.find(key);
    if (entry == object->map_entries.end()) {
        return nullptr;
    }
    return &entry->second;
}

static void append_map_patches(const ObjId& object_id,
                               const detail::ObjectState* before,
                               const detail::ObjectState* after,
                               std::vector<Patch>& patches) {
    auto keys = std::set<std::string>{};
    if (before != nullptr) {
        std::ranges::transform(before->map_entries,
                               std::inserter(keys, keys.end()),
                               [](const auto& entry) { return entry.first; });
    }
    if (after != nullptr) {
        std::ranges::transform(after->map_entries,
                               std::inserter(keys, keys.end()),
                               [](const auto& entry) { return entry.first; });
    }
    std::ranges::for_each(keys, [&](const std::string& key) {
        const auto* before_entries = map_entries(before, key);
        const auto* after_entries = map_entries(after, key);
        if (before_entries != nullptr && after_entries != nullptr &&
            map_entries_equal(*before_entries, *after_entries)) {
            return;
        }
        if (after_entries == nullptr) {
            patches.push_back(Patch{
                .obj = object_id,
                .key = key,
                .action = PatchDelete{.index = 0, .count = 1},
            });
            return;
        }
        if (before_entries != nullptr) {
            const auto increment = counter_increment(*before_entries, *after_entries);
            if (increment) {
                patches.push_back(Patch{
                    .obj = object_id,
                    .key = key,
                    .action = PatchIncrement{.delta = *increment},
                });
                return;
            }
        }
        const auto* winner = winning_entry(*after_entries);
        if (winner == nullptr) {
            return;
        }
        patches.push_back(Patch{
            .obj = object_id,
            .key = key,
            .action =
                PatchPut{
                    .value = winner->value,
                    .conflict = after_entries->size() > 1,
                },
        });
    });
}

static auto visible_elements(const detail::ObjectState* object)
    -> std::vector<detail::ListElement> {
    if (object == nullptr) {
        return {};
    }
    auto elements = std::vector<detail::ListElement>{};
    elements.reserve(object->list_elements.size());
    std::ranges::copy_if(
        object->list_elements, std::back_inserter(elements), &detail::ListElement::visible);
    return elements;
}

static void append_list_patches(const ObjId& object_id,
                                const detail::ObjectState* before,
                                const detail::ObjectState* after,
                                std::vector<Patch>& patches) {
    auto current = visible_elements(before);
    const auto target = visible_elements(after);
    auto index = std::size_t{0};
    std::ranges::for_each(target, [&](const detail::ListElement& element) {
        if (index < current.size() && current[index].insert_id == element.insert_id) {
            if (current[index].value != element.value) {
                patches.push_back(Patch{
                    .obj = object_id,
                    .key = list_index(index),
                    .action = PatchPut{.value = element.value, .conflict = false},
                });
                current[index].value = element.value;
            }
            ++index;
            return;
        }

        const auto remaining = std::ranges::subrange{
            current.begin() + static_cast<std::ptrdiff_t>(index),
            current.end(),
        };
        const auto existing =
            std::ranges::find(remaining, element.insert_id, &detail::ListElement::insert_id);
        if (existing != current.end()) {
            const auto count = static_cast<std::size_t>(std::distance(remaining.begin(), existing));
            patches.push_back(Patch{
                .obj = object_id,
                .key = list_index(index),
                .action = PatchDelete{.index = index, .count = count},
            });
            current.erase(remaining.begin(), existing);
            if (current[index].value != element.value) {
                patches.push_back(Patch{
                    .obj = object_id,
                    .key = list_index(index),
                    .action = PatchPut{.value = element.value, .conflict = false},
                });
                current[index].value = element.value;
            }
            ++index;
            return;
        }

        patches.push_back(Patch{
            .obj = object_id,
            .key = list_index(index),
            .action = PatchInsert{.index = index, .value = element.value},
        });
        current.insert(current.begin() + static_cast<std::ptrdiff_t>(index), element);
        ++index;
    });
    if (index < current.size()) {
        patches.push_back(Patch{
            .obj = object_id,
            .key = list_index(index),
            .action = PatchDelete{.index = index, .count = current.size() - index},
        });
    }
}

static auto text_content(const detail::ObjectState* object) -> std::string {
    if (object == nullptr) {
        return {};
    }
    auto text = std::string{};
    std::ranges::for_each(object->list_elements, [&](const detail::ListElement& element) {
        if (!element.visible) {
            return;
        }
        const auto* scalar = std::get_if<ScalarValue>(&element.value);
        if (scalar == nullptr) {
            return;
        }
        const auto* value = std::get_if<std::string>(scalar);
        if (value != nullptr) {
            text += *value;
        }
    });
    return text;
}

static void append_text_patches(const ObjId& object_id,
                                const detail::ObjectState* before,
                                const detail::ObjectState* after,
                                std::vector<Patch>& patches) {
    const auto before_text = text_content(before);
    const auto after_text = text_content(after);
    if (before_text == after_text) {
        return;
    }
    const auto prefix_mismatch = std::ranges::mismatch(before_text, after_text);
    const auto prefix =
        static_cast<std::size_t>(std::distance(before_text.begin(), prefix_mismatch.in1));
    const auto before_tail = std::string_view{before_text}.substr(prefix);
    const auto after_tail = std::string_view{after_text}.substr(prefix);
    const auto before_reverse = std::views::reverse(before_tail);
    const auto after_reverse = std::views::reverse(after_tail);
    const auto suffix_mismatch = std::ranges::mismatch(before_reverse, after_reverse);
    const auto suffix =
        static_cast<std::size_t>(std::distance(before_reverse.begin(), suffix_mismatch.in1));
    const auto delete_count = before_text.size() - prefix - suffix;
    const auto inserted = after_text.substr(prefix, after_text.size() - prefix - suffix);
    patches.push_back(Patch{
        .obj = object_id,
        .key = list_index(prefix),
        .action =
            PatchSpliceText{
                .index = prefix,
                .delete_count = delete_count,
                .text = inserted,
            },
    });
}

static auto visible_state_patches(const ObjectMap& before, const ObjectMap& after)
    -> std::vector<Patch> {
    auto object_ids = std::set<ObjId>{};
    std::ranges::transform(before,
                           std::inserter(object_ids, object_ids.end()),
                           [](const auto& entry) { return entry.first; });
    std::ranges::transform(after,
                           std::inserter(object_ids, object_ids.end()),
                           [](const auto& entry) { return entry.first; });
    auto patches = std::vector<Patch>{};
    std::ranges::for_each(object_ids, [&](const ObjId& object_id) {
        const auto before_entry = before.find(object_id);
        const auto after_entry = after.find(object_id);
        const auto* before_object = before_entry == before.end() ? nullptr : &before_entry->second;
        const auto* after_object = after_entry == after.end() ? nullptr : &after_entry->second;
        const auto type = after_object != nullptr ? after_object->type : before_object->type;
        switch (type) {
        case ObjType::map:
        case ObjType::table:
            append_map_patches(object_id, before_object, after_object, patches);
            break;
        case ObjType::list:
            append_list_patches(object_id, before_object, after_object, patches);
            break;
        case ObjType::text:
            append_text_patches(object_id, before_object, after_object, patches);
            break;
        }
    });
    return patches;
}

auto Document::merge(const Document& other) -> std::expected<void, Error> {
    return apply_changes(get_changes_added(other));
}

auto Document::merge_with_patches(const Document& other)
    -> std::expected<std::vector<Patch>, Error> {
    return apply_changes_with_patches(get_changes_added(other));
}

auto Document::get_changes() const -> std::vector<Change> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    return state_->change_history;
}

auto Document::get_changes_since(const std::vector<ChangeHash>& heads) const
    -> std::vector<Change> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    return state_->get_changes_by_hash(state_->get_changes_since(heads));
}

auto Document::get_changes_added(const Document& other) const -> std::vector<Change> {
    const auto checkpoint_heads = [&]() {
        auto lock = std::unique_lock{mutex_};
        state_->materialize_changes();
        return state_->compaction_checkpoint ? state_->compaction_checkpoint->heads
                                             : std::vector<ChangeHash>{};
    }();
    const auto other_changes =
        checkpoint_heads.empty() ? other.get_changes() : other.get_changes_since(checkpoint_heads);
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto result = std::vector<Change>{};
    result.reserve(other_changes.size());
    std::ranges::copy_if(other_changes, std::back_inserter(result), [&](const Change& change) {
        return !state_->has_change_hash(detail::DocState::compute_change_hash(change));
    });
    return result;
}

auto Document::get_last_local_change() const -> std::optional<Change> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    const auto change = std::ranges::find_if(
        state_->change_history.rbegin(),
        state_->change_history.rend(),
        [&](const Change& candidate) { return candidate.actor == state_->actor; });
    return change == state_->change_history.rend() ? std::nullopt : std::optional<Change>{*change};
}

auto Document::apply_changes(const std::vector<Change>& changes) -> std::expected<void, Error> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto admissions = validate_changes(*state_, changes);
    if (!admissions) {
        return std::unexpected{admissions.error()};
    }
    admit_changes(*state_, *admissions);
    return {};
}

auto Document::apply_changes_with_patches(const std::vector<Change>& changes)
    -> std::expected<std::vector<Patch>, Error> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto admissions = validate_changes(*state_, changes);
    if (!admissions) {
        return std::unexpected{admissions.error()};
    }
    const auto before = state_->objects;
    admit_changes(*state_, *admissions);
    return visible_state_patches(before, state_->objects);
}

auto Document::get_heads() const -> std::vector<ChangeHash> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    return state_->heads;
}

auto Document::get_missing_deps(const std::vector<ChangeHash>& heads) const
    -> std::vector<ChangeHash> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    return state_->get_missing_deps(heads);
}

// -- Phase 4: Binary Serialization --------------------------------------------

auto Document::save() const -> std::vector<std::byte> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto output = std::vector<std::byte>{};
    for (const auto& encoded : state_->encoded_changes_) {
        output.insert(output.end(), encoded.bytes.begin(), encoded.bytes.end());
    }

    if (output.empty()) {
        const auto body = std::vector<std::byte>(4, std::byte{0});
        storage::write_chunk(storage::ChunkType::document, body, output);
    }
    if (!output.empty()) {
        save_heads_ = state_->heads;
    }
    return output;
}

auto Document::save_incremental() const -> std::vector<std::byte> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    const auto hashes = state_->get_changes_since(save_heads_);
    auto output = std::vector<std::byte>{};
    std::ranges::for_each(hashes, [&](const ChangeHash& hash) {
        const auto change = state_->cached_hash_index_.find(hash);
        assert(change != state_->cached_hash_index_.end());
        const auto& chunk = state_->encoded_changes_[change->second].bytes;
        output.insert(output.end(), chunk.begin(), chunk.end());
    });
    if (!output.empty()) {
        save_heads_ = state_->heads;
    }
    return output;
}

auto Document::save_compact() const -> std::vector<std::byte> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    if (!state_->compaction_checkpoint) {
        save_heads_ = state_->heads;
        return storage::serialize_document_chunk(state_->change_history, state_->heads);
    }
    if (state_->change_history.empty()) {
        save_heads_ = state_->heads;
        return state_->compaction_bytes;
    }

    auto full_document = load(state_->compaction_bytes);
    if (!full_document) {
        return {};
    }
    const auto applied = full_document->apply_changes(state_->change_history);
    if (!applied) {
        return {};
    }
    save_heads_ = state_->heads;
    return storage::serialize_document_chunk(full_document->state_->change_history,
                                             full_document->state_->heads);
}

// Intermediate struct for parsed document data (avoids private access issues).
struct ParsedDocData {
    ActorId local_actor;
    std::uint64_t next_counter;
    std::uint64_t local_seq;
    std::vector<Change> changes;
    std::vector<storage::EncodedChange> encoded_changes;
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
        .encoded_changes = {},
        .heads = {},
        .clock = {},
    };
}

static auto parse_change_stream(std::span<const std::byte> data) -> std::optional<ParsedDocData> {
    if (data.empty())
        return std::nullopt;

    auto changes = std::vector<Change>{};
    auto encoded_changes = std::vector<storage::EncodedChange>{};
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
        auto chunk_changes = std::vector<Change>{};
        auto chunk_encodings = std::vector<storage::EncodedChange>{};
        if (header->type == storage::ChunkType::change) {
            const auto parsed_change = storage::parse_change_chunk(remaining.first(chunk_size));
            if (!parsed_change) {
                return std::nullopt;
            }
            chunk_changes.push_back(parsed_change->change);
            chunk_encodings.push_back(storage::EncodedChange{
                .bytes = {remaining.begin(), remaining.begin() + chunk_size},
                .hash = parsed_change->hash,
            });
        } else if (header->type == storage::ChunkType::document) {
            const auto parsed_changes = storage::parse_document_chunk(remaining.first(chunk_size));
            if (!parsed_changes) {
                return std::nullopt;
            }
            chunk_changes = *parsed_changes;
            chunk_encodings.reserve(chunk_changes.size());
            std::ranges::transform(
                chunk_changes, std::back_inserter(chunk_encodings), storage::encode_change);
        } else {
            return std::nullopt;
        }

        std::ranges::for_each(
            std::views::iota(std::size_t{0}, chunk_changes.size()), [&](std::size_t index) {
                const auto& change = chunk_changes[index];
                const auto hash = chunk_encodings[index].hash;
                if (changes.empty()) {
                    local_actor = change.actor;
                }
                std::ranges::for_each(change.deps, [&](const ChangeHash& dependency) {
                    std::erase(heads, dependency);
                });
                heads.push_back(hash);
                clock[change.actor] = std::max(clock[change.actor], change.seq);
                if (change.actor == local_actor) {
                    local_seq = std::max(local_seq, change.seq);
                    std::ranges::for_each(change.operations, [&](const Op& operation) {
                        next_counter = std::max(next_counter, operation.id.counter + 1);
                    });
                }
                changes.push_back(change);
                encoded_changes.push_back(std::move(chunk_encodings[index]));
            });
        position += chunk_size;
    }

    return ParsedDocData{
        .local_actor = local_actor,
        .next_counter = next_counter,
        .local_seq = local_seq,
        .changes = std::move(changes),
        .encoded_changes = std::move(encoded_changes),
        .heads = std::move(heads),
        .clock = std::move(clock),
    };
}

auto Document::load_incremental(std::span<const std::byte> data)
    -> std::expected<std::size_t, Error> {
    if (data.empty()) {
        return std::size_t{0};
    }
    auto parsed = parse_empty_document(data);
    if (!parsed) {
        parsed = parse_change_stream(data);
    }
    if (!parsed) {
        return std::unexpected{
            Error{ErrorKind::invalid_document, "incremental data is not a valid chunk stream"}};
    }

    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto admissions = validate_changes(*state_, parsed->changes, &parsed->encoded_changes);
    if (!admissions) {
        return std::unexpected{admissions.error()};
    }
    const auto before = state_->change_history.size();
    admit_changes(*state_, *admissions);
    return state_->change_history.size() - before;
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

    const auto changes_are_valid = std::ranges::all_of(
        parsed->changes, [](const Change& change) { return validate_change(change).has_value(); });

    auto doc = Document{1u}; // no pool — caller can assign one later
    if (!changes_are_valid || !validate_text_operations(*doc.state_, parsed->changes)) {
        return std::nullopt;
    }
    if (!parsed->changes.empty()) {
        doc.set_actor_id(parsed->local_actor);
    }
    doc.state_->next_counter = parsed->next_counter;
    doc.state_->local_seq = parsed->local_seq;

    // Replay all changes to rebuild the document state
    for (const auto& change : parsed->changes) {
        for (const auto& op : change.operations) {
            doc.state_->apply_op(op);
        }
    }

    doc.state_->change_history = std::move(parsed->changes);
    doc.state_->encoded_changes_ = std::move(parsed->encoded_changes);
    doc.state_->deferred_change_parents_.resize(doc.state_->change_history.size());
    std::ranges::for_each(std::views::iota(std::size_t{0}, doc.state_->encoded_changes_.size()),
                          [&](std::size_t index) {
                              doc.state_->cached_hash_index_.emplace(
                                  doc.state_->encoded_changes_[index].hash, index);
                          });
    doc.state_->heads = std::move(parsed->heads);
    std::ranges::sort(doc.state_->heads);
    doc.state_->clock = std::move(parsed->clock);

    return doc;
}

static void prune_shallow_objects(detail::DocState& state) {
    std::ranges::for_each(state.objects, [](auto& object_entry) {
        auto& object = object_entry.second;
        std::erase_if(object.list_elements,
                      [](const detail::ListElement& element) { return !element.visible; });
        auto visible_ids = std::unordered_set<OpId>{};
        std::ranges::transform(object.list_elements,
                               std::inserter(visible_ids, visible_ids.end()),
                               &detail::ListElement::insert_id);
        std::erase_if(object.marks, [&](const detail::MarkEntry& mark) {
            return !visible_ids.contains(mark.start_elem) || !visible_ids.contains(mark.end_elem);
        });
    });

    auto reachable = std::unordered_set<ObjId>{root};
    auto queue = std::vector<ObjId>{root};
    const auto traversal = std::views::iota(std::size_t{0}, state.objects.size()) |
                           std::views::take_while([&](const auto) { return !queue.empty(); });
    std::ranges::for_each(traversal, [&](const auto) {
        const auto object_id = queue.back();
        queue.pop_back();
        const auto object = state.objects.find(object_id);
        if (object == state.objects.end()) {
            return;
        }
        const auto add_child = [&](const OpId& operation_id, const Value& value) {
            if (!std::holds_alternative<ObjType>(value)) {
                return;
            }
            const auto child = ObjId{operation_id};
            if (reachable.insert(child).second) {
                queue.push_back(child);
            }
        };
        std::ranges::for_each(object->second.map_entries, [&](const auto& map_entry) {
            std::ranges::for_each(map_entry.second, [&](const detail::MapEntry& value) {
                add_child(value.op_id, value.value);
            });
        });
        std::ranges::for_each(object->second.list_elements,
                              [&](const detail::ListElement& element) {
                                  add_child(element.insert_id, element.value);
                              });
    });
    std::erase_if(state.objects, [&](const auto& object_entry) {
        return !reachable.contains(object_entry.first);
    });
}

auto Document::load_shallow(std::span<const std::byte> data) -> std::optional<Document> {
    const auto header = storage::parse_chunk_header(data);
    if (!header || header->type != storage::ChunkType::document ||
        header->body_offset + header->body_length != data.size()) {
        return std::nullopt;
    }
    auto decoded = storage::parse_shallow_document_chunk(data);
    if (!decoded) {
        return std::nullopt;
    }

    auto document = Document{1u};
    if (!decoded->metadata.empty()) {
        document.state_->actor = decoded->metadata.front().actor;
    }
    std::ranges::for_each(decoded->metadata, [&](const storage::DocumentChangeMetadata& change) {
        document.state_->clock[change.actor] =
            std::max(document.state_->clock[change.actor], change.sequence);
        if (change.actor == document.state_->actor) {
            document.state_->local_seq = std::max(document.state_->local_seq, change.sequence);
            document.state_->next_counter =
                std::max(document.state_->next_counter, change.max_operation + 1);
        }
        std::ranges::for_each(change.operations, [&](const std::optional<Op>& operation) {
            document.state_->apply_op(*operation);
        });
    });
    document.state_->heads = std::move(decoded->heads);
    std::ranges::sort(document.state_->heads);
    prune_shallow_objects(*document.state_);
    document.state_->compaction_checkpoint = detail::StateCheckpoint{
        .heads = document.state_->heads,
        .objects = document.state_->objects,
    };
    document.state_->compaction_bytes.assign(data.begin(), data.end());
    return document;
}

auto Document::load_with_patches(std::span<const std::byte> data, Document& document)
    -> std::expected<std::vector<Patch>, Error> {
    auto loaded = load(data);
    if (!loaded) {
        return std::unexpected{
            Error{ErrorKind::invalid_document, "document data is malformed or corrupt"}};
    }
    const auto empty = detail::DocState{};
    auto patches = visible_state_patches(empty.objects, loaded->state_->objects);
    document = std::move(*loaded);
    return patches;
}

// -- Phase 5: Sync Protocol ---------------------------------------------------

// Determine which change hashes to send based on peer's bloom filter and needs.
static auto get_hashes_to_send(const detail::DocState& state,
                               const std::vector<Have>& their_have,
                               const std::vector<ChangeHash>& their_need)
    -> std::vector<ChangeHash> {

    if (their_have.empty()) {
        return their_need;
    }

    // Collect all last_sync heads from the Have entries
    auto last_sync_heads = std::vector<ChangeHash>{};
    auto bloom_filters = std::vector<sync::BloomFilter>{};
    for (const auto& have : their_have) {
        last_sync_heads.insert(last_sync_heads.end(), have.last_sync.begin(), have.last_sync.end());
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
        if (it == hash_idx.end())
            continue;
        for (const auto& dep : state.change_history[it->second].deps) {
            dependents[dep].push_back(h);
        }
    }

    // Find hashes NOT in any bloom filter
    auto hashes_to_send = std::set<ChangeHash>{};
    for (const auto& h : hashes) {
        auto in_any =
            std::ranges::any_of(bloom_filters, [&](const auto& bf) { return bf.contains_hash(h); });
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

auto Document::generate_sync_message(SyncState& sync_state) const -> std::optional<SyncMessage> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();

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
        their_heads_set.insert(sync_state.their_heads_->begin(), sync_state.their_heads_->end());
    }

    // Check if all our needs are in their heads (normal case)
    auto all_needs_in_their_heads =
        std::ranges::all_of(our_need, [&](const auto& h) { return their_heads_set.contains(h); });

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
    auto encoded_changes_to_send = std::vector<storage::EncodedChange>{};
    if (sync_state.their_have_ && sync_state.their_need_) {
        auto hashes = get_hashes_to_send(*state_, *sync_state.their_have_, *sync_state.their_need_);

        // Deduplicate with sent_hashes
        auto unsent = std::vector<ChangeHash>{};
        for (const auto& h : hashes) {
            if (!sync_state.sent_hashes_.contains(h)) {
                unsent.push_back(h);
            }
        }

        changes_to_send = state_->get_changes_by_hash(unsent);
        encoded_changes_to_send.reserve(unsent.size());
        std::ranges::transform(
            unsent, std::back_inserter(encoded_changes_to_send), [&](const ChangeHash& hash) {
                const auto change = state_->cached_hash_index_.find(hash);
                assert(change != state_->cached_hash_index_.end());
                return state_->encoded_changes_[change->second];
            });

        // Track what we're sending
        for (const auto& h : unsent) {
            sync_state.sent_hashes_.insert(h);
        }
    }

    // Should we send a message?
    auto heads_unchanged = (sync_state.last_sent_heads_ == our_heads);
    auto heads_equal =
        (sync_state.their_heads_.has_value() && *sync_state.their_heads_ == our_heads);

    if (heads_unchanged && sync_state.have_responded_) {
        if (heads_equal && changes_to_send.empty()) {
            return std::nullopt; // fully synced
        }
        if (sync_state.in_flight_) {
            return std::nullopt; // waiting for ack
        }
    }

    // Build and return message
    sync_state.have_responded_ = true;
    sync_state.last_sent_heads_ = our_heads;
    sync_state.in_flight_ = true;
    auto encoding_cache = std::make_shared<detail::SyncMessageEncoding>(detail::SyncMessageEncoding{
        .changes = changes_to_send,
        .encoded_changes = std::move(encoded_changes_to_send),
    });

    return SyncMessage{
        .heads = std::move(our_heads),
        .need = std::move(our_need),
        .have = std::move(our_have),
        .changes = std::move(changes_to_send),
        .supported_capabilities = std::vector<std::uint8_t>{0x01},
        .encoding_cache = std::move(encoding_cache),
    };
}

static auto validate_sync_message(const detail::DocState& state, const SyncMessage& message)
    -> std::expected<std::vector<ChangeAdmission>, Error> {
    if (!message.encoding_cache || message.encoding_cache->changes != message.changes ||
        message.encoding_cache->encoded_changes.size() != message.changes.size()) {
        return validate_changes(state, message.changes);
    }
    auto encodings = message.encoding_cache->encoded_changes;
    return validate_changes(state, message.changes, &encodings);
}

auto Document::receive_sync_message(SyncState& sync_state, const SyncMessage& message)
    -> std::expected<void, Error> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto admissions = validate_sync_message(*state_, message);
    if (!admissions) {
        return std::unexpected{admissions.error()};
    }
    admit_changes(*state_, *admissions);
    update_sync_state_after_receive(sync_state, message);
    return {};
}

auto Document::receive_sync_message_with_patches(SyncState& sync_state, const SyncMessage& message)
    -> std::expected<std::vector<Patch>, Error> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto admissions = validate_sync_message(*state_, message);
    if (!admissions) {
        return std::unexpected{admissions.error()};
    }
    const auto before = state_->objects;
    admit_changes(*state_, *admissions);
    auto patches = visible_state_patches(before, state_->objects);
    update_sync_state_after_receive(sync_state, message);
    return patches;
}

void Document::update_sync_state_after_receive(SyncState& sync_state, const SyncMessage& message) {

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
            if (it == hash_idx.end())
                continue;
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
            if (std::ranges::find(sync_state.shared_heads_, h) == sync_state.shared_heads_.end()) {
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
            while (i + count < ops.size() && ops[i + count].action == OpType::del &&
                   ops[i + count].obj == obj &&
                   std::holds_alternative<std::size_t>(ops[i + count].key) &&
                   std::get<std::size_t>(ops[i + count].key) == index) {
                ++count;
            }

            // Check if followed by splice_text inserts (= splice_text call)
            auto splice_start = i + count;
            if (splice_start < ops.size() && ops[splice_start].action == OpType::splice_text &&
                ops[splice_start].obj == obj) {
                auto text = std::string{};
                auto j = splice_start;
                while (j < ops.size() && ops[j].action == OpType::splice_text &&
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

        // Coalesce splice_text inserts and any following deletes from the same splice call.
        if (op.action == OpType::splice_text) {
            auto obj = op.obj;
            auto index = std::get<std::size_t>(op.key);
            auto text = std::string{};
            auto j = i;
            while (j < ops.size() && ops[j].action == OpType::splice_text && ops[j].obj == obj) {
                if (const auto* sv = std::get_if<ScalarValue>(&ops[j].value)) {
                    if (const auto* s = std::get_if<std::string>(sv)) {
                        text += *s;
                    }
                }
                ++j;
            }
            auto delete_count = std::size_t{0};
            while (j + delete_count < ops.size() && ops[j + delete_count].action == OpType::del &&
                   ops[j + delete_count].obj == obj &&
                   std::holds_alternative<std::size_t>(ops[j + delete_count].key) &&
                   std::get<std::size_t>(ops[j + delete_count].key) == index) {
                ++delete_count;
            }
            patches.push_back(Patch{
                .obj = obj,
                .key = list_index(index),
                .action = PatchSpliceText{index, delete_count, std::move(text)},
            });
            i = j + delete_count;
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

auto Document::transact_with_patches(const std::function<void(Transaction&)>& fn,
                                     CommitOptions options) -> std::vector<Patch> {
    auto lock = std::unique_lock{mutex_};
    auto tx = Transaction{*state_};
    try {
        fn(tx);

        // Capture ops before commit moves them
        auto ops = tx.pending_ops_;
        tx.commit(std::move(options));

        return ops_to_patches(ops);
    } catch (...) {
        tx.rollback();
        throw;
    }
}

// -- Phase 6: Historical reads ------------------------------------------------

auto Document::get_at(const ObjId& obj,
                      std::string_view key,
                      const std::vector<ChangeHash>& heads) const -> std::optional<Value> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.map_get(obj, std::string{key});
}

auto Document::get_at(const ObjId& obj,
                      std::size_t index,
                      const std::vector<ChangeHash>& heads) const -> std::optional<Value> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.list_get(obj, index);
}

auto Document::keys_at(const ObjId& obj, const std::vector<ChangeHash>& heads) const
    -> std::vector<std::string> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.map_keys(obj);
}

auto Document::values_at(const ObjId& obj, const std::vector<ChangeHash>& heads) const
    -> std::vector<Value> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto snapshot = state_->rebuild_state_at(heads);
    auto type = snapshot.object_type(obj);
    if (!type)
        return {};
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

auto Document::length_at(const ObjId& obj, const std::vector<ChangeHash>& heads) const
    -> std::size_t {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.object_length(obj);
}

auto Document::text_at(const ObjId& obj, const std::vector<ChangeHash>& heads) const
    -> std::string {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto snapshot = state_->rebuild_state_at(heads);
    return snapshot.text_content(obj);
}

// -- Phase 6: Cursors ---------------------------------------------------------

auto Document::cursor(const ObjId& obj, std::size_t index) const -> std::optional<Cursor> {
    auto guard = read_guard();
    auto id = state_->list_element_id_at(obj, index);
    if (!id)
        return std::nullopt;
    return Cursor{*id};
}

auto Document::resolve_cursor(const ObjId& obj, const Cursor& cur) const
    -> std::optional<std::size_t> {
    auto guard = read_guard();
    return state_->find_element_visible_index(obj, cur.position);
}

// -- Rich text marks ----------------------------------------------------------

static auto collect_marks(const detail::DocState& state, const ObjId& obj) -> std::vector<Mark> {
    const auto* obj_state = state.get_object(obj);
    if (!obj_state)
        return {};

    auto result = std::vector<Mark>{};
    for (const auto& entry : obj_state->marks) {
        auto indices = state.resolve_mark_indices(*obj_state, entry);
        if (!indices)
            continue;
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

auto Document::marks_at(const ObjId& obj, const std::vector<ChangeHash>& heads) const
    -> std::vector<Mark> {
    auto lock = std::unique_lock{mutex_};
    state_->materialize_changes();
    auto snapshot = state_->rebuild_state_at(heads);
    return collect_marks(snapshot, obj);
}

// -- Phase 12A: Modern API helpers --------------------------------------------

auto Document::ops_to_patches_internal(const std::vector<Op>& ops) -> std::vector<Patch> {
    return ops_to_patches(ops);
}

auto Document::get_path_impl(std::span<const Prop> path) const -> std::optional<Value> {
    auto guard = read_guard();
    if (path.empty())
        return std::nullopt;

    auto current_obj = root;

    // Walk all but the last path element to resolve intermediate ObjIds.
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        auto val = std::visit(
            overload{
                [&](const std::string& key) { return state_->map_get(current_obj, key); },
                [&](std::size_t index) { return state_->list_get(current_obj, index); },
            },
            path[i]);

        if (!val)
            return std::nullopt;

        // The value must be an ObjType to continue navigating.
        if (!is_object(*val))
            return std::nullopt;

        // We need the ObjId, not the ObjType. For intermediate objects,
        // the ObjId is the OpId that created them. We look it up via
        // the internal state's object-at-key resolution.
        auto obj_id = state_->get_obj_id_at(current_obj, path[i]);
        if (!obj_id)
            return std::nullopt;
        current_obj = *obj_id;
    }

    // Final element: return the value.
    return std::visit(overload{
                          [&](const std::string& key) { return state_->map_get(current_obj, key); },
                          [&](std::size_t index) { return state_->list_get(current_obj, index); },
                      },
                      path.back());
}

} // namespace automerge_cpp
