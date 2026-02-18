#pragma once

// Internal header — not installed. Implementation detail of Document.

#include <automerge-cpp/change.hpp>
#include <automerge-cpp/op.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <map>
#include <optional>
#include <ranges>
#include <string>
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

// The state of a single CRDT object in the document tree.
struct ObjectState {
    ObjType type;
    std::map<std::string, std::vector<MapEntry>> map_entries;  // map/table
    std::vector<ListElement> list_elements;                     // list/text
};

// The complete internal state of a Document.
struct DocState {
    ActorId actor;
    std::uint64_t next_counter = 1;
    std::map<ObjId, ObjectState> objects;
    std::vector<Op> op_log;

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

            if (same_origin && elem.insert_id > new_id) {
                // Higher priority concurrent insert — skip it
                scanned.insert(elem.insert_id);
                ++pos;
            } else if (same_origin) {
                // Lower priority — insert before it
                break;
            } else {
                // Different origin but in scanned set — subtree element, skip
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
            case OpType::mark:
                break;
        }
    }

    // -- Change hash computation (Phase 3 simple hash, SHA-256 in Phase 4) ----

    static auto compute_change_hash(const Change& change) -> ChangeHash {
        auto hash = std::uint64_t{14695981039346656037ULL};
        auto hash_byte = [&](std::byte b) {
            hash ^= static_cast<std::uint64_t>(b);
            hash *= std::uint64_t{1099511628211ULL};
        };
        auto hash_u64 = [&](std::uint64_t v) {
            for (int i = 0; i < 8; ++i) {
                hash_byte(static_cast<std::byte>(v >> (i * 8)));
            }
        };

        for (auto b : change.actor.bytes) hash_byte(b);
        hash_u64(change.seq);
        hash_u64(change.start_op);
        hash_u64(static_cast<std::uint64_t>(change.timestamp));
        hash_u64(change.operations.size());

        ChangeHash result{};
        auto h1 = hash;
        auto h2 = hash * 6364136223846793005ULL + 1442695040888963407ULL;
        auto h3 = h2 * 6364136223846793005ULL + 1442695040888963407ULL;
        auto h4 = h3 * 6364136223846793005ULL + 1442695040888963407ULL;
        std::memcpy(&result.bytes[0], &h1, 8);
        std::memcpy(&result.bytes[8], &h2, 8);
        std::memcpy(&result.bytes[16], &h3, 8);
        std::memcpy(&result.bytes[24], &h4, 8);
        return result;
    }
};

}  // namespace automerge_cpp::detail
