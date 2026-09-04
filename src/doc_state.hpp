#pragma once

// Internal header — not installed. Implementation detail of Document.

#include <automerge-cpp/change.hpp>
#include <automerge-cpp/op.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include "storage/change_chunk.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace automerge_cpp::detail {

// An entry at a map key. Multiple entries at the same key = conflict.
struct MapEntry {
    OpId op_id;
    Value value;
};

// An element in a list or text sequence.
struct ListElement {
    OpId insert_id;
    std::optional<OpId> insert_after;  // which element this was inserted after (nullopt = HEAD)
    Value value;
    bool visible = true;
};

// A rich-text mark anchored by element OpIds (survives edits and merges).
struct MarkEntry {
    OpId mark_id;       // the OpId of the mark operation itself
    OpId start_elem;    // the OpId of the first element in the range
    OpId end_elem;      // the OpId of the last element in the range (inclusive)
    std::string name;
    ScalarValue value;
};

struct PendingMark {
    ObjId obj;
    OpId mark_id;
    std::optional<OpId> start_after;
    std::string name;
    ScalarValue value;
};

// The state of a single CRDT object in the document tree.
struct ObjectState {
    ObjType type;
    std::map<std::string, std::vector<MapEntry>> map_entries;  // map/table
    std::vector<ListElement> list_elements;                     // list/text
    std::vector<MarkEntry> marks;                               // rich-text marks
};

// The complete internal state of a Document.
struct DocState {
    ActorId actor;
    std::uint64_t next_counter = 1;
    std::unordered_map<ObjId, ObjectState> objects;

    // Change tracking (Phase 3)
    std::vector<Change> change_history;
    std::unordered_map<ChangeHash, Change> pending_changes;
    std::unordered_map<ChangeHash, storage::EncodedChange> pending_change_encodings;
    std::unordered_map<ChangeHash, std::unordered_set<ChangeHash>> pending_dependents;
    std::unordered_map<ActorId, std::unordered_map<std::uint64_t, ChangeHash>> pending_sequences;
    std::vector<ChangeHash> heads;
    std::map<ActorId, std::uint64_t> clock;  // actor -> max seq seen
    std::uint64_t local_seq = 0;
    std::unordered_map<ActorId, PendingMark> pending_marks;

    // Encoded changes form a materialized prefix of change_history. Local commits after that
    // prefix retain their parent by history index until a hash-bearing API needs the bytes.
    std::vector<storage::EncodedChange> encoded_changes_;
    std::vector<std::optional<std::size_t>> deferred_change_parents_;
    std::optional<std::size_t> deferred_head_index_;
    std::unordered_map<ChangeHash, std::size_t> cached_hash_index_; // hash → index

    // Cached actor table (11A.3b) — incrementally maintained.
    // Invalidated by tracking how many changes have been scanned.
    mutable std::vector<ActorId> cached_actor_table_;
    mutable std::unordered_set<ActorId> cached_actor_set_;
    mutable std::size_t cached_actor_table_size_ = 0;  // changes scanned so far
    mutable bool cached_actor_table_has_local_ = false;

    DocState() {
        objects[root] = ObjectState{.type = ObjType::map, .map_entries = {}, .list_elements = {}};
    }

    auto next_op_id() -> OpId {
        return OpId{next_counter++, actor};
    }

    void append_local_change(Change change) {
        const auto index = change_history.size();
        change_history.push_back(std::move(change));
        deferred_change_parents_.push_back(deferred_head_index_);
        heads.clear();
        deferred_head_index_ = index;
    }

    void append_materialized_change(Change change, storage::EncodedChange encoded) {
        assert(encoded_changes_.size() == change_history.size());
        const auto index = change_history.size();
        cached_hash_index_.emplace(encoded.hash, index);
        change_history.push_back(std::move(change));
        encoded_changes_.push_back(std::move(encoded));
        deferred_change_parents_.push_back(std::nullopt);
    }

    void materialize_changes() {
        if (encoded_changes_.size() == change_history.size()) {
            return;
        }
        encoded_changes_.reserve(change_history.size());
        deferred_change_parents_.resize(change_history.size());
        cached_hash_index_.reserve(change_history.size());
        for (auto index = encoded_changes_.size(); index < change_history.size(); ++index) {
            auto resolved = change_history[index];
            const auto parent = deferred_change_parents_[index];
            if (parent) {
                assert(*parent < encoded_changes_.size());
                resolved.deps.push_back(encoded_changes_[*parent].hash);
            }
            auto encoded = storage::encode_change(resolved);
            const auto [entry, inserted] = cached_hash_index_.emplace(encoded.hash, index);
            assert(inserted);
            static_cast<void>(entry);
            change_history[index] = std::move(resolved);
            deferred_change_parents_[index] = std::nullopt;
            encoded_changes_.push_back(std::move(encoded));
        }
        if (deferred_head_index_) {
            heads = {encoded_changes_[*deferred_head_index_].hash};
            deferred_head_index_.reset();
        }
    }

    auto get_object(const ObjId& id) -> ObjectState* {
        auto it = objects.find(id);
        return it != objects.end() ? &it->second : nullptr;
    }

    auto get_object(const ObjId& id) const -> const ObjectState* {
        auto it = objects.find(id);
        return it != objects.end() ? &it->second : nullptr;
    }

    // -- Predecessor queries (for Transaction) --------------------------------

    auto map_pred(const ObjId& obj, const std::string& key) const -> std::vector<OpId> {
        const auto* state = get_object(obj);
        if (!state) return {};
        auto it = state->map_entries.find(key);
        if (it == state->map_entries.end() || it->second.empty()) return {};
        auto result = std::vector<OpId>{};
        result.reserve(it->second.size());
        std::ranges::transform(it->second, std::back_inserter(result),
            [](const MapEntry& e) { return e.op_id; });
        return result;
    }

    auto list_pred(const ObjId& obj, std::size_t visible_index) const -> std::vector<OpId> {
        const auto* state = get_object(obj);
        if (!state) return {};
        auto real_idx = visible_index_to_real(*state, visible_index);
        if (real_idx >= state->list_elements.size()) return {};
        return {state->list_elements[real_idx].insert_id};
    }

    auto insert_after_for(const ObjId& obj, std::size_t visible_index) const -> std::optional<OpId> {
        if (visible_index == 0) return std::nullopt;
        const auto* state = get_object(obj);
        if (!state) return std::nullopt;
        auto real_idx = visible_index_to_real(*state, visible_index - 1);
        if (real_idx >= state->list_elements.size()) return std::nullopt;
        return state->list_elements[real_idx].insert_id;
    }

    // -- Map operations -------------------------------------------------------

    void map_put(const ObjId& obj, const std::string& key, OpId op_id, Value value) {
        auto* state = get_object(obj);
        assert(state && (state->type == ObjType::map || state->type == ObjType::table));

        auto& entries = state->map_entries[key];
        // For local operations: replace existing entries (no conflicts from same actor)
        entries.clear();
        entries.push_back(MapEntry{.op_id = op_id, .value = std::move(value)});
    }

    void map_delete(const ObjId& obj, const std::string& key) {
        auto* state = get_object(obj);
        assert(state && (state->type == ObjType::map || state->type == ObjType::table));

        state->map_entries.erase(key);
    }

    auto map_get(const ObjId& obj, const std::string& key) const -> std::optional<Value> {
        const auto* state = get_object(obj);
        if (!state) return std::nullopt;

        auto it = state->map_entries.find(key);
        if (it == state->map_entries.end() || it->second.empty()) return std::nullopt;

        // Return the winning value (highest OpId)
        const auto& entries = it->second;
        auto winner = std::ranges::max_element(entries,
            [](const MapEntry& a, const MapEntry& b) { return a.op_id < b.op_id; });
        return winner->value;
    }

    auto map_get_all(const ObjId& obj, const std::string& key) const -> std::vector<Value> {
        const auto* state = get_object(obj);
        if (!state) return {};

        auto it = state->map_entries.find(key);
        if (it == state->map_entries.end()) return {};

        auto result = std::vector<Value>{};
        result.reserve(it->second.size());
        std::ranges::transform(it->second, std::back_inserter(result),
            [](const MapEntry& e) { return e.value; });
        return result;
    }

    auto map_keys(const ObjId& obj) const -> std::vector<std::string> {
        const auto* state = get_object(obj);
        if (!state) return {};

        auto result = std::vector<std::string>{};
        result.reserve(state->map_entries.size());
        std::ranges::transform(state->map_entries, std::back_inserter(result),
            [](const auto& pair) { return pair.first; });
        return result;
    }

    auto map_values(const ObjId& obj) const -> std::vector<Value> {
        const auto* state = get_object(obj);
        if (!state) return {};

        auto result = std::vector<Value>{};
        result.reserve(state->map_entries.size());
        for (const auto& [key, entries] : state->map_entries) {
            if (entries.empty()) continue;
            auto winner = std::ranges::max_element(entries,
                [](const MapEntry& a, const MapEntry& b) { return a.op_id < b.op_id; });
            result.push_back(winner->value);
        }
        return result;
    }

    // -- List operations ------------------------------------------------------

    auto visible_index_to_real(const ObjectState& state, std::size_t index) const -> std::size_t {
        auto visible_count = std::size_t{0};
        for (std::size_t i = 0; i < state.list_elements.size(); ++i) {
            if (state.list_elements[i].visible) {
                if (visible_count == index) return i;
                ++visible_count;
            }
        }
        // index == visible_count means "past the end" (for insert at end)
        return state.list_elements.size();
    }

    void list_insert(const ObjId& obj, std::size_t index, OpId op_id, Value value,
                     std::optional<OpId> insert_after = std::nullopt) {
        auto* state = get_object(obj);
        assert(state && (state->type == ObjType::list || state->type == ObjType::text));

        auto real_idx = visible_index_to_real(*state, index);
        state->list_elements.insert(
            state->list_elements.begin() + static_cast<std::ptrdiff_t>(real_idx),
            ListElement{.insert_id = op_id, .insert_after = insert_after,
                        .value = std::move(value), .visible = true});
    }

    void list_set(const ObjId& obj, std::size_t index, OpId op_id, Value value) {
        auto* state = get_object(obj);
        assert(state && (state->type == ObjType::list || state->type == ObjType::text));

        auto real_idx = visible_index_to_real(*state, index);
        assert(real_idx < state->list_elements.size());
        state->list_elements[real_idx].value = std::move(value);
    }

    void list_delete(const ObjId& obj, std::size_t index) {
        auto* state = get_object(obj);
        assert(state && (state->type == ObjType::list || state->type == ObjType::text));

        auto real_idx = visible_index_to_real(*state, index);
        assert(real_idx < state->list_elements.size());
        state->list_elements[real_idx].visible = false;
    }

    auto list_get(const ObjId& obj, std::size_t index) const -> std::optional<Value> {
        const auto* state = get_object(obj);
        if (!state) return std::nullopt;

        auto real_idx = visible_index_to_real(*state, index);
        if (real_idx >= state->list_elements.size()) return std::nullopt;
        if (!state->list_elements[real_idx].visible) return std::nullopt;
        return state->list_elements[real_idx].value;
    }

    auto list_length(const ObjId& obj) const -> std::size_t {
        const auto* state = get_object(obj);
        if (!state) return 0;

        return static_cast<std::size_t>(std::ranges::count_if(
            state->list_elements, &ListElement::visible));
    }

    auto list_values(const ObjId& obj) const -> std::vector<Value> {
        const auto* state = get_object(obj);
        if (!state) return {};

        auto result = std::vector<Value>{};
        for (const auto& elem : state->list_elements) {
            if (elem.visible) result.push_back(elem.value);
        }
        return result;
    }

    // -- Nested object ID resolution ------------------------------------------

    /// Resolve the ObjId of a nested object at a map key.
    /// Returns nullopt if the key doesn't exist or holds a scalar.
    auto get_obj_id_for_key(const ObjId& obj, const std::string& key) const -> std::optional<ObjId> {
        const auto* state = get_object(obj);
        if (!state) return std::nullopt;

        auto it = state->map_entries.find(key);
        if (it == state->map_entries.end() || it->second.empty()) return std::nullopt;

        // Find the winning entry (highest OpId)
        const auto& entries = it->second;
        auto winner = std::ranges::max_element(entries,
            [](const MapEntry& a, const MapEntry& b) { return a.op_id < b.op_id; });

        // Must be an object type, not a scalar
        if (!std::holds_alternative<ObjType>(winner->value)) return std::nullopt;
        return ObjId{winner->op_id};
    }

    /// Resolve the ObjId of a nested object at a list index.
    /// Returns nullopt if the index is out of bounds or holds a scalar.
    auto get_obj_id_for_index(const ObjId& obj, std::size_t index) const -> std::optional<ObjId> {
        const auto* state = get_object(obj);
        if (!state) return std::nullopt;

        auto real_idx = visible_index_to_real(*state, index);
        if (real_idx >= state->list_elements.size()) return std::nullopt;
        if (!state->list_elements[real_idx].visible) return std::nullopt;

        const auto& elem = state->list_elements[real_idx];
        if (!std::holds_alternative<ObjType>(elem.value)) return std::nullopt;
        return ObjId{elem.insert_id};
    }

    /// Resolve the ObjId of a nested object at a Prop (key or index).
    auto get_obj_id_at(const ObjId& obj, const Prop& prop) const -> std::optional<ObjId> {
        return std::visit(overload{
            [&](const std::string& key) { return get_obj_id_for_key(obj, key); },
            [&](std::size_t index) { return get_obj_id_for_index(obj, index); },
        }, prop);
    }

    // -- Text operations ------------------------------------------------------

    auto text_content(const ObjId& obj) const -> std::string {
        const auto* state = get_object(obj);
        if (!state) return {};

        auto result = std::string{};
        for (const auto& elem : state->list_elements) {
            if (!elem.visible) continue;
            if (auto* sv = std::get_if<ScalarValue>(&elem.value)) {
                if (auto* s = std::get_if<std::string>(sv)) {
                    result += *s;
                }
            }
        }
        return result;
    }

    // -- Counter operations ---------------------------------------------------

    void counter_increment(const ObjId& obj, const std::string& key, std::int64_t delta) {
        auto* state = get_object(obj);
        assert(state && (state->type == ObjType::map || state->type == ObjType::table));

        auto it = state->map_entries.find(key);
        assert(it != state->map_entries.end() && !it->second.empty());

        auto& entry = it->second.back();
        auto* sv = std::get_if<ScalarValue>(&entry.value);
        assert(sv);
        auto* counter = std::get_if<Counter>(sv);
        assert(counter);
        counter->value += delta;
    }

    // -- Mark operations ------------------------------------------------------

    void mark_range(const ObjId& obj, OpId mark_id, OpId start_elem, OpId end_elem,
                    const std::string& name, ScalarValue value) {
        auto* state = get_object(obj);
        assert(state && (state->type == ObjType::list || state->type == ObjType::text));

        // Remove any existing mark with the same name and mark_id actor
        // (same actor overriding their own mark with the same name)
        state->marks.push_back(MarkEntry{
            .mark_id = mark_id,
            .start_elem = start_elem,
            .end_elem = end_elem,
            .name = name,
            .value = std::move(value),
        });
    }

    // Resolve a MarkEntry to visible indices. Returns nullopt if either
    // endpoint is no longer visible.
    auto resolve_mark_indices(const ObjectState& state, const MarkEntry& entry) const
        -> std::optional<std::pair<std::size_t, std::size_t>> {
        auto start_idx = std::optional<std::size_t>{};
        auto end_idx = std::optional<std::size_t>{};
        auto visible_count = std::size_t{0};

        for (const auto& elem : state.list_elements) {
            if (elem.insert_id == entry.start_elem && elem.visible) {
                start_idx = visible_count;
            }
            if (elem.insert_id == entry.end_elem && elem.visible) {
                end_idx = visible_count;
            }
            if (elem.visible) ++visible_count;
        }

        if (!start_idx || !end_idx) return std::nullopt;
        return std::pair{*start_idx, *end_idx + 1};  // end is exclusive
    }

    // -- Generic queries ------------------------------------------------------

    auto object_type(const ObjId& obj) const -> std::optional<ObjType> {
        const auto* state = get_object(obj);
        if (!state) return std::nullopt;
        return state->type;
    }

    auto object_length(const ObjId& obj) const -> std::size_t {
        const auto* state = get_object(obj);
        if (!state) return 0;

        switch (state->type) {
            case ObjType::map:
            case ObjType::table:
                return state->map_entries.size();
            case ObjType::list:
            case ObjType::text:
                return list_length(obj);
        }
        return 0;
    }

    auto create_object(OpId id, ObjType type) -> ObjId {
        auto obj_id = ObjId{id};
        objects[obj_id] = ObjectState{.type = type, .map_entries = {}, .list_elements = {}};
        return obj_id;
    }

    // -- RGA merge support (Phase 3) ------------------------------------------

    // Find the real index to insert a new element using the RGA algorithm.
    // insert_after: the OpId of the element this is inserted after (nullopt = HEAD)
    // new_id: the OpId of the element being inserted
    auto find_rga_position(const ObjectState& state, std::optional<OpId> insert_after,
                           OpId new_id) const -> std::size_t {
        // Step 1: Find the position after the origin element
        std::size_t pos = 0;

        if (insert_after) {
            bool found = false;
            for (std::size_t i = 0; i < state.list_elements.size(); ++i) {
                if (state.list_elements[i].insert_id == *insert_after) {
                    pos = i + 1;
                    found = true;
                    break;
                }
            }
            if (!found) {
                return state.list_elements.size();
            }
        }

        // Step 2: Scan right — skip elements that have higher priority or belong
        // to subtrees of higher-priority concurrent inserts.
        auto scanned = std::unordered_set<OpId>{};
        if (insert_after) scanned.insert(*insert_after);

        while (pos < state.list_elements.size()) {
            const auto& elem = state.list_elements[pos];

            const bool same_origin = (elem.insert_after == insert_after);
            bool origin_in_scan = same_origin;

            if (!same_origin && elem.insert_after) {
                origin_in_scan = scanned.contains(*elem.insert_after);
            }

            if (!origin_in_scan) break;

            if (same_origin && elem.insert_id <= new_id) {
                // Lower priority — insert before it
                break;
            } else {
                // Higher priority concurrent insert or subtree element — skip
                scanned.insert(elem.insert_id);
                ++pos;
            }
        }

        return pos;
    }

    static auto first_visible_after(const ObjectState& state, const std::optional<OpId>& after)
        -> std::optional<OpId> {
        auto begin = state.list_elements.begin();
        if (after) {
            const auto anchor =
                std::ranges::find(state.list_elements, *after, &ListElement::insert_id);
            if (anchor == state.list_elements.end()) {
                return std::nullopt;
            }
            begin = std::next(anchor);
        }
        const auto element = std::find_if(
            begin, state.list_elements.end(), [](const auto& entry) { return entry.visible; });
        return element == state.list_elements.end() ? std::nullopt
                                                    : std::optional{element->insert_id};
    }

    static auto last_visible_at_or_before(const ObjectState& state,
                                          const std::optional<OpId>& before)
        -> std::optional<OpId> {
        if (!before) {
            return std::nullopt;
        }
        const auto anchor =
            std::ranges::find(state.list_elements, *before, &ListElement::insert_id);
        if (anchor == state.list_elements.end()) {
            return std::nullopt;
        }
        const auto element = std::find_if(std::make_reverse_iterator(std::next(anchor)),
                                          state.list_elements.rend(),
                                          [](const auto& entry) { return entry.visible; });
        return element == state.list_elements.rend() ? std::nullopt
                                                     : std::optional{element->insert_id};
    }

    // -- Remote operation application (Phase 3) -------------------------------

    void apply_op(const Op& op) {
        // Ensure our counter stays ahead of any ops we see
        next_counter = std::max(next_counter, op.id.counter + 1);

        // If the value represents a nested object, ensure it exists
        if (std::holds_alternative<ObjType>(op.value)) {
            auto obj_id = ObjId{op.id};
            if (objects.find(obj_id) == objects.end()) {
                create_object(op.id, std::get<ObjType>(op.value));
            }
        }

        auto* key_str = std::get_if<std::string>(&op.key);
        const bool is_map = key_str != nullptr;

        switch (op.action) {
            case OpType::put:
            case OpType::make_object: {
                if (is_map) {
                    // Map put with conflict handling
                    auto* obj_state = get_object(op.obj);
                    if (!obj_state) break;
                    auto& entries = obj_state->map_entries[*key_str];
                    // Remove predecessors (values being overridden)
                    std::erase_if(entries, [&](const MapEntry& e) {
                        return std::ranges::find(op.pred, e.op_id) != op.pred.end();
                    });
                    entries.push_back(MapEntry{.op_id = op.id, .value = op.value});
                } else {
                    // List set — find target element by pred, update value
                    auto* obj_state = get_object(op.obj);
                    if (!obj_state) break;
                    for (auto& elem : obj_state->list_elements) {
                        if (std::ranges::find(op.pred, elem.insert_id) != op.pred.end()) {
                            elem.value = op.value;
                            break;
                        }
                    }
                }
                break;
            }
            case OpType::insert:
            case OpType::splice_text: {
                auto* obj_state = get_object(op.obj);
                if (!obj_state) break;
                auto rga_pos = find_rga_position(*obj_state, op.insert_after, op.id);
                obj_state->list_elements.insert(
                    obj_state->list_elements.begin() + static_cast<std::ptrdiff_t>(rga_pos),
                    ListElement{.insert_id = op.id, .insert_after = op.insert_after,
                                .value = op.value, .visible = true});
                break;
            }
            case OpType::del: {
                if (is_map) {
                    auto* obj_state = get_object(op.obj);
                    if (!obj_state) break;
                    auto it = obj_state->map_entries.find(*key_str);
                    if (it != obj_state->map_entries.end()) {
                        std::erase_if(it->second, [&](const MapEntry& e) {
                            return std::ranges::find(op.pred, e.op_id) != op.pred.end();
                        });
                        if (it->second.empty()) {
                            obj_state->map_entries.erase(it);
                        }
                    }
                } else {
                    // List delete — find element by pred, mark invisible
                    auto* obj_state = get_object(op.obj);
                    if (!obj_state) break;
                    for (auto& elem : obj_state->list_elements) {
                        if (std::ranges::find(op.pred, elem.insert_id) != op.pred.end()) {
                            elem.visible = false;
                            break;
                        }
                    }
                }
                break;
            }
            case OpType::increment: {
                if (!is_map) break;
                auto* obj_state = get_object(op.obj);
                if (!obj_state) break;
                auto it = obj_state->map_entries.find(*key_str);
                if (it == obj_state->map_entries.end()) break;
                auto* delta_sv = std::get_if<ScalarValue>(&op.value);
                if (!delta_sv) break;
                auto* delta_counter = std::get_if<Counter>(delta_sv);
                if (!delta_counter) break;
                // Increment all conflict entries' counters
                for (auto& entry : it->second) {
                    auto* sv = std::get_if<ScalarValue>(&entry.value);
                    if (!sv) continue;
                    auto* counter = std::get_if<Counter>(sv);
                    if (!counter) continue;
                    counter->value += delta_counter->value;
                }
                break;
            }
            case OpType::mark: {
                auto* obj_state = get_object(op.obj);
                if (!obj_state) break;
                if (op.mark_name) {
                    const auto* value = std::get_if<ScalarValue>(&op.value);
                    if (!value)
                        break;
                    pending_marks.insert_or_assign(op.id.actor,
                                                   PendingMark{
                                                       .obj = op.obj,
                                                       .mark_id = op.id,
                                                       .start_after = op.insert_after,
                                                       .name = *op.mark_name,
                                                       .value = *value,
                                                   });
                    break;
                }

                const auto pending = pending_marks.find(op.id.actor);
                if (pending == pending_marks.end() || pending->second.obj != op.obj)
                    break;
                const auto start_elem =
                    first_visible_after(*obj_state, pending->second.start_after);
                const auto end_elem = last_visible_at_or_before(*obj_state, op.insert_after);
                if (start_elem && end_elem) {
                    obj_state->marks.push_back(MarkEntry{
                        .mark_id = pending->second.mark_id,
                        .start_elem = *start_elem,
                        .end_elem = *end_elem,
                        .name = pending->second.name,
                        .value = pending->second.value,
                    });
                }
                pending_marks.erase(pending);
                break;
            }
        }
    }

    // -- Cached actor table (11A.3b) --------------------------------------------

    void ensure_actor_table() const {
        // Ensure local actor is first
        if (!cached_actor_table_has_local_) {
            if (cached_actor_set_.insert(actor).second) {
                cached_actor_table_.push_back(actor);
            }
            cached_actor_table_has_local_ = true;
        }
        // Scan only newly appended changes
        for (auto i = cached_actor_table_size_; i < change_history.size(); ++i) {
            const auto& change = change_history[i];
            if (cached_actor_set_.insert(change.actor).second) {
                cached_actor_table_.push_back(change.actor);
            }
            for (const auto& op : change.operations) {
                if (cached_actor_set_.insert(op.id.actor).second) {
                    cached_actor_table_.push_back(op.id.actor);
                }
                if (!op.obj.is_root()) {
                    const auto& obj_op = std::get<OpId>(op.obj.inner);
                    if (cached_actor_set_.insert(obj_op.actor).second) {
                        cached_actor_table_.push_back(obj_op.actor);
                    }
                }
                if (op.insert_after) {
                    if (cached_actor_set_.insert(op.insert_after->actor).second) {
                        cached_actor_table_.push_back(op.insert_after->actor);
                    }
                }
                for (const auto& p : op.pred) {
                    if (cached_actor_set_.insert(p.actor).second) {
                        cached_actor_table_.push_back(p.actor);
                    }
                }
            }
        }
        cached_actor_table_size_ = change_history.size();
    }

    auto actor_table() const -> const std::vector<ActorId>& {
        ensure_actor_table();
        return cached_actor_table_;
    }

    // -- Sync helpers (Phase 5) -------------------------------------------------

    void ensure_hash_index() const {
        assert(encoded_changes_.size() == change_history.size());
    }

    // Get the cached hash index (read-only reference).
    auto hash_index() const -> const std::unordered_map<ChangeHash, std::size_t>& {
        ensure_hash_index();
        return cached_hash_index_;
    }

    // Check if we have a change with the given hash. O(1) via cached index.
    auto has_change_hash(const ChangeHash& hash) const -> bool {
        ensure_hash_index();
        return cached_hash_index_.contains(hash);
    }

    // Get all change hashes in change_history order (cached, no recomputation).
    auto all_change_hashes() const -> std::vector<ChangeHash> {
        ensure_hash_index();
        auto hashes = std::vector<ChangeHash>{};
        hashes.reserve(encoded_changes_.size());
        std::ranges::transform(encoded_changes_,
                               std::back_inserter(hashes),
                               [](const storage::EncodedChange& encoded) { return encoded.hash; });
        return hashes;
    }

    // Get change hashes that are NOT ancestors of the given set of hashes.
    // This returns all hashes that are "new" relative to the given set.
    auto get_changes_since(const std::vector<ChangeHash>& since_heads) const
        -> std::vector<ChangeHash> {
        if (since_heads.empty()) return all_change_hashes();

        ensure_hash_index();
        const auto& idx = cached_hash_index_;

        // BFS: find all ancestors of since_heads
        auto ancestors = std::unordered_set<ChangeHash>{};
        auto queue = std::vector<ChangeHash>{};
        for (const auto& h : since_heads) {
            if (idx.contains(h)) {
                ancestors.insert(h);
                queue.push_back(h);
            }
        }

        while (!queue.empty()) {
            auto h = queue.back();
            queue.pop_back();
            auto it = idx.find(h);
            if (it == idx.end()) continue;
            for (const auto& dep : change_history[it->second].deps) {
                if (ancestors.insert(dep).second) {
                    queue.push_back(dep);
                }
            }
        }

        // Return hashes not in ancestors, in change_history order
        auto result = std::vector<ChangeHash>{};
        for (const auto& encoded : encoded_changes_) {
            const auto& hash = encoded.hash;
            if (!ancestors.contains(hash)) {
                result.push_back(hash);
            }
        }
        return result;
    }

    // Get changes we're missing that would be needed to know the given heads.
    auto get_missing_deps(const std::vector<ChangeHash>& their_heads) const
        -> std::vector<ChangeHash> {
        ensure_hash_index();
        auto result = std::vector<ChangeHash>{};
        auto add_if_missing = [&](const ChangeHash& hash) {
            if (!cached_hash_index_.contains(hash) && !pending_changes.contains(hash) &&
                std::ranges::find(result, hash) == result.end()) {
                result.push_back(hash);
            }
        };
        std::ranges::for_each(their_heads, add_if_missing);
        std::ranges::for_each(pending_changes, [&](const auto& entry) {
            std::ranges::for_each(entry.second.deps, add_if_missing);
        });
        return result;
    }

    // Get changes by their hashes, in the order given.
    auto get_changes_by_hash(const std::vector<ChangeHash>& hashes) const
        -> std::vector<Change> {
        ensure_hash_index();
        auto result = std::vector<Change>{};
        result.reserve(hashes.size());
        for (const auto& h : hashes) {
            auto it = cached_hash_index_.find(h);
            if (it != cached_hash_index_.end()) {
                result.push_back(change_history[it->second]);
            }
        }
        return result;
    }

    // -- Historical reads (Phase 6) ---------------------------------------------

    // Find indices (into change_history) of all changes visible at given heads.
    auto changes_visible_at(const std::vector<ChangeHash>& target_heads) const
        -> std::vector<std::size_t> {
        ensure_hash_index();
        const auto& idx = cached_hash_index_;
        auto visited = std::set<ChangeHash>{};
        auto queue = std::vector<ChangeHash>{};

        for (const auto& h : target_heads) {
            if (idx.contains(h) && visited.insert(h).second) {
                queue.push_back(h);
            }
        }

        while (!queue.empty()) {
            auto h = queue.back();
            queue.pop_back();
            auto it = idx.find(h);
            if (it == idx.end()) continue;
            for (const auto& dep : change_history[it->second].deps) {
                if (visited.insert(dep).second) {
                    queue.push_back(dep);
                }
            }
        }

        auto indices = std::vector<std::size_t>{};
        for (const auto& h : visited) {
            auto it = idx.find(h);
            if (it != idx.end()) {
                indices.push_back(it->second);
            }
        }
        std::ranges::sort(indices);
        return indices;
    }

    // Rebuild a fresh DocState by replaying only the changes visible at given heads.
    auto rebuild_state_at(const std::vector<ChangeHash>& target_heads) const -> DocState {
        auto indices = changes_visible_at(target_heads);
        auto snapshot = DocState{};
        snapshot.actor = actor;

        for (auto idx : indices) {
            for (const auto& op : change_history[idx].operations) {
                snapshot.apply_op(op);
            }
        }
        return snapshot;
    }

    // -- Cursor helpers (Phase 6) -----------------------------------------------

    // Get the insert_id of the element at visible index in a list/text.
    auto list_element_id_at(const ObjId& obj, std::size_t index) const
        -> std::optional<OpId> {
        const auto* state = get_object(obj);
        if (!state) return std::nullopt;
        auto real_idx = visible_index_to_real(*state, index);
        if (real_idx >= state->list_elements.size()) return std::nullopt;
        if (!state->list_elements[real_idx].visible) return std::nullopt;
        return state->list_elements[real_idx].insert_id;
    }

    // Find the visible index of an element by its insert_id.
    auto find_element_visible_index(const ObjId& obj, const OpId& id) const
        -> std::optional<std::size_t> {
        const auto* state = get_object(obj);
        if (!state) return std::nullopt;
        auto visible_count = std::size_t{0};
        for (const auto& elem : state->list_elements) {
            if (elem.insert_id == id) {
                return elem.visible ? std::optional{visible_count} : std::nullopt;
            }
            if (elem.visible) ++visible_count;
        }
        return std::nullopt;
    }

    // -- Change hash computation (SHA-256 based) ---------------------------

    static auto compute_change_hash(const Change& change) -> ChangeHash {
        return storage::encode_change(change).hash;
    }
};

}  // namespace automerge_cpp::detail
