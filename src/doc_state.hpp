#pragma once

// Internal header — not installed. Implementation detail of Document.

#include <automerge-cpp/change.hpp>
#include <automerge-cpp/op.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include "crypto/sha256.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
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
    std::vector<ChangeHash> heads;
    std::map<ActorId, std::uint64_t> clock;  // actor -> max seq seen
    std::uint64_t local_seq = 0;

    DocState() {
        objects[root] = ObjectState{.type = ObjType::map, .map_entries = {}, .list_elements = {}};
    }

    auto next_op_id() -> OpId {
        return OpId{next_counter++, actor};
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
                // Mark ops use: key=mark name, value=mark value,
                // pred[0]=start element, pred[1]=end element
                if (!is_map) break;  // name is stored as string key
                if (op.pred.size() < 2) break;
                auto* obj_state = get_object(op.obj);
                if (!obj_state) break;
                obj_state->marks.push_back(MarkEntry{
                    .mark_id = op.id,
                    .start_elem = op.pred[0],
                    .end_elem = op.pred[1],
                    .name = *key_str,
                    .value = std::get_if<ScalarValue>(&op.value)
                        ? *std::get_if<ScalarValue>(&op.value)
                        : ScalarValue{Null{}},
                });
                break;
            }
        }
    }

    // -- Sync helpers (Phase 5) -------------------------------------------------

    // Build a map from change hash → index in change_history.
    auto change_hash_index() const -> std::map<ChangeHash, std::size_t> {
        auto index = std::map<ChangeHash, std::size_t>{};
        for (std::size_t i = 0; i < change_history.size(); ++i) {
            auto hash = compute_change_hash(change_history[i]);
            index[hash] = i;
        }
        return index;
    }

    // Check if we have a change with the given hash.
    auto has_change_hash(const ChangeHash& hash) const -> bool {
        auto idx = change_hash_index();
        return idx.contains(hash);
    }

    // Get all change hashes.
    auto all_change_hashes() const -> std::vector<ChangeHash> {
        auto result = std::vector<ChangeHash>{};
        result.reserve(change_history.size());
        for (const auto& change : change_history) {
            result.push_back(compute_change_hash(change));
        }
        return result;
    }

    // Get change hashes that are NOT ancestors of the given set of hashes.
    // This returns all hashes that are "new" relative to the given set.
    auto get_changes_since(const std::vector<ChangeHash>& since_heads) const
        -> std::vector<ChangeHash> {
        if (since_heads.empty()) return all_change_hashes();

        auto all_hashes = all_change_hashes();
        auto hash_idx = change_hash_index();

        // BFS: find all ancestors of since_heads
        auto ancestors = std::unordered_set<ChangeHash>{};
        auto queue = std::vector<ChangeHash>{};
        for (const auto& h : since_heads) {
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
            for (const auto& dep : change_history[it->second].deps) {
                if (ancestors.insert(dep).second) {
                    queue.push_back(dep);
                }
            }
        }

        // Return hashes not in ancestors
        auto result = std::vector<ChangeHash>{};
        for (const auto& h : all_hashes) {
            if (!ancestors.contains(h)) {
                result.push_back(h);
            }
        }
        return result;
    }

    // Get changes we're missing that would be needed to know the given heads.
    auto get_missing_deps(const std::vector<ChangeHash>& their_heads) const
        -> std::vector<ChangeHash> {
        auto result = std::vector<ChangeHash>{};
        for (const auto& h : their_heads) {
            if (!has_change_hash(h) &&
                std::ranges::find(result, h) == result.end()) {
                result.push_back(h);
            }
        }
        return result;
    }

    // Get changes by their hashes, in the order given.
    auto get_changes_by_hash(const std::vector<ChangeHash>& hashes) const
        -> std::vector<Change> {
        auto idx = change_hash_index();
        auto result = std::vector<Change>{};
        result.reserve(hashes.size());
        for (const auto& h : hashes) {
            auto it = idx.find(h);
            if (it != idx.end()) {
                result.push_back(change_history[it->second]);
            }
        }
        return result;
    }

    // -- Historical reads (Phase 6) ---------------------------------------------

    // Find indices (into change_history) of all changes visible at given heads.
    auto changes_visible_at(const std::vector<ChangeHash>& target_heads) const
        -> std::vector<std::size_t> {
        auto hash_idx = change_hash_index();
        auto visited = std::set<ChangeHash>{};
        auto queue = std::vector<ChangeHash>{};

        for (const auto& h : target_heads) {
            if (hash_idx.contains(h) && visited.insert(h).second) {
                queue.push_back(h);
            }
        }

        while (!queue.empty()) {
            auto h = queue.back();
            queue.pop_back();
            auto it = hash_idx.find(h);
            if (it == hash_idx.end()) continue;
            for (const auto& dep : change_history[it->second].deps) {
                if (visited.insert(dep).second) {
                    queue.push_back(dep);
                }
            }
        }

        auto indices = std::vector<std::size_t>{};
        for (const auto& h : visited) {
            auto it = hash_idx.find(h);
            if (it != hash_idx.end()) {
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

    // -- Change hash computation (SHA-256 based) --------------------------------

    static auto compute_change_hash(const Change& change) -> ChangeHash {
        // Build a deterministic byte representation of the change
        auto input = std::vector<std::byte>{};

        // Actor ID
        input.insert(input.end(), change.actor.bytes.begin(), change.actor.bytes.end());

        // Seq (little-endian 8 bytes)
        for (int i = 0; i < 8; ++i) {
            input.push_back(static_cast<std::byte>(change.seq >> (i * 8)));
        }

        // Start op
        for (int i = 0; i < 8; ++i) {
            input.push_back(static_cast<std::byte>(change.start_op >> (i * 8)));
        }

        // Timestamp
        auto ts = static_cast<std::uint64_t>(change.timestamp);
        for (int i = 0; i < 8; ++i) {
            input.push_back(static_cast<std::byte>(ts >> (i * 8)));
        }

        // Number of operations
        auto num_ops = static_cast<std::uint64_t>(change.operations.size());
        for (int i = 0; i < 8; ++i) {
            input.push_back(static_cast<std::byte>(num_ops >> (i * 8)));
        }

        // Dependency hashes
        for (const auto& dep : change.deps) {
            input.insert(input.end(), dep.bytes.begin(), dep.bytes.end());
        }

        auto digest = crypto::sha256(input);
        ChangeHash result{};
        std::memcpy(result.bytes.data(), digest.data(), 32);
        return result;
    }
};

}  // namespace automerge_cpp::detail
