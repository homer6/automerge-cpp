#pragma once

// Internal header — not installed. Implementation detail of Document.

#include <automerge-cpp/op.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <algorithm>
#include <cassert>
#include <map>
#include <optional>
#include <ranges>
#include <string>
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

    // -- Map operations -------------------------------------------------------

    void map_put(const ObjId& obj, const std::string& key, OpId op_id, Value value) {
        auto* state = get_object(obj);
        assert(state && (state->type == ObjType::map || state->type == ObjType::table));

        auto& entries = state->map_entries[key];
        // For single-actor: replace existing entries (no conflicts)
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

    void list_insert(const ObjId& obj, std::size_t index, OpId op_id, Value value) {
        auto* state = get_object(obj);
        assert(state && (state->type == ObjType::list || state->type == ObjType::text));

        auto real_idx = visible_index_to_real(*state, index);
        state->list_elements.insert(
            state->list_elements.begin() + static_cast<std::ptrdiff_t>(real_idx),
            ListElement{.insert_id = op_id, .value = std::move(value), .visible = true});
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
};

}  // namespace automerge_cpp::detail
