#pragma once

#include "doc_state.hpp"

#include <cassert>
#include <optional>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

namespace automerge_cpp::detail {

struct TransactionRollback {
    explicit TransactionRollback(const DocState& state) : start_op_{state.next_counter} {}

    auto is_active() const -> bool {
        return active_;
    }

    void assert_active() const {
        assert(active_);
    }

    void record_map_key(DocState& state, const ObjId& obj, const std::string& key) {
        auto* object = state.get_object(obj);
        assert(object);

        const auto found = object->map_entries.find(key);
        if (!first_map_key_before_ && found != object->map_entries.end() &&
            found->second.size() == 1) {
            first_map_key_before_.emplace(MapKeyBefore{
                .obj = obj,
                .key = key,
                .entries =
                    MapEntryBefore{
                        .op_id = found->second.front().op_id,
                        .value = std::move(found->second.front().value),
                    },
            });
            return;
        }
        auto entries = [&]() -> MapEntriesBefore {
            if (found == object->map_entries.end()) {
                return MapKeyAbsent{};
            }
            if (found->second.size() == 1) {
                return MapEntryBefore{
                    .op_id = found->second.front().op_id,
                    .value = found->second.front().value,
                };
            }
            auto result = std::vector<MapEntryBefore>{};
            result.reserve(found->second.size());
            std::ranges::transform(
                found->second, std::back_inserter(result), [](const MapEntry& entry) {
                    return MapEntryBefore{
                        .op_id = entry.op_id,
                        .value = entry.value,
                    };
                });
            return result;
        }();
        auto before = MapKeyBefore{
            .obj = obj,
            .key = key,
            .entries = std::move(entries),
        };
        if (!first_map_key_before_) {
            first_map_key_before_.emplace(std::move(before));
            return;
        }
        map_keys_before_.push_back(std::move(before));
    }

    void record_list_insert(const ObjId& obj, OpId element_id) {
        list_inserts_.push_back(ListInsert{
            .obj = obj,
            .element_id = element_id,
        });
    }

    void record_list_value(DocState& state, const ObjId& obj, std::size_t index) {
        const auto* object = state.get_object(obj);
        assert(object);
        const auto real_index = state.visible_index_to_real(*object, index);
        assert(real_index < object->list_elements.size());
        const auto& element = object->list_elements[real_index];
        list_values_before_.push_back(ListValueBefore{
            .obj = obj,
            .element_id = element.insert_id,
            .value = element.value,
        });
    }

    void record_list_visibility(DocState& state, const ObjId& obj, std::size_t index) {
        const auto* object = state.get_object(obj);
        assert(object);
        const auto real_index = state.visible_index_to_real(*object, index);
        assert(real_index < object->list_elements.size());
        const auto& element = object->list_elements[real_index];
        list_visibility_before_.push_back(ListVisibilityBefore{
            .obj = obj,
            .element_id = element.insert_id,
            .visible = element.visible,
        });
    }

    void record_mark_insert(const ObjId& obj, OpId mark_id) {
        mark_inserts_.push_back(MarkInsert{
            .obj = obj,
            .mark_id = mark_id,
        });
    }

    void record_object_insert(const ObjId& obj) {
        object_inserts_.push_back(ObjectInsert{.obj = obj});
    }

    void begin_commit(DocState& state) {
        const auto clock_entry = state.clock.find(state.actor);
        const auto actor_clock = clock_entry == state.clock.end()
                                     ? std::optional<std::uint64_t>{}
                                     : std::optional<std::uint64_t>{clock_entry->second};
        commit_before_.emplace(CommitBefore{
            .local_seq = state.local_seq,
            .heads = std::move(state.heads),
            .actor_clock = actor_clock,
            .change_history_size = state.change_history.size(),
        });
    }

    auto heads_before() const -> const std::vector<ChangeHash>& {
        assert(commit_before_);
        return commit_before_->heads;
    }

    auto start_op() const -> std::uint64_t {
        return start_op_;
    }

    void finish_commit() {
        active_ = false;
    }

    void rollback(DocState& state) {
        if (!active_) {
            return;
        }

        if (commit_before_) {
            state.local_seq = commit_before_->local_seq;
            state.heads = std::move(commit_before_->heads);
            if (commit_before_->actor_clock) {
                state.clock[state.actor] = *commit_before_->actor_clock;
            } else {
                state.clock.erase(state.actor);
            }
            state.change_history.resize(commit_before_->change_history_size);
        }

        state.next_counter = start_op_;

        std::ranges::for_each(mark_inserts_ | std::views::reverse, [&](const MarkInsert& inserted) {
            auto* object = state.get_object(inserted.obj);
            if (!object) {
                return;
            }
            std::erase_if(object->marks,
                          [&](const MarkEntry& mark) { return mark.mark_id == inserted.mark_id; });
        });

        std::ranges::for_each(
            list_visibility_before_ | std::views::reverse, [&](const ListVisibilityBefore& before) {
                auto* object = state.get_object(before.obj);
                if (!object) {
                    return;
                }
                const auto found = std::ranges::find(
                    object->list_elements, before.element_id, &ListElement::insert_id);
                if (found != object->list_elements.end()) {
                    found->visible = before.visible;
                }
            });

        std::ranges::for_each(
            list_values_before_ | std::views::reverse, [&](const ListValueBefore& before) {
                auto* object = state.get_object(before.obj);
                if (!object) {
                    return;
                }
                const auto found = std::ranges::find(
                    object->list_elements, before.element_id, &ListElement::insert_id);
                if (found != object->list_elements.end()) {
                    found->value = before.value;
                }
            });

        std::ranges::for_each(list_inserts_ | std::views::reverse, [&](const ListInsert& inserted) {
            auto* object = state.get_object(inserted.obj);
            if (!object) {
                return;
            }
            std::erase_if(object->list_elements, [&](const ListElement& element) {
                return element.insert_id == inserted.element_id;
            });
        });

        const auto restore_map_key = [&](MapKeyBefore& before) {
            auto* object = state.get_object(before.obj);
            if (!object) {
                return;
            }
            std::visit(
                overload{
                    [&](MapKeyAbsent&) { object->map_entries.erase(before.key); },
                    [&](MapEntryBefore& entry) {
                        auto restored = std::vector<MapEntry>{};
                        restored.push_back(MapEntry{
                            .op_id = entry.op_id,
                            .value = std::move(entry.value),
                        });
                        object->map_entries.insert_or_assign(before.key, std::move(restored));
                    },
                    [&](std::vector<MapEntryBefore>& entries) {
                        auto restored = std::vector<MapEntry>{};
                        restored.reserve(entries.size());
                        std::ranges::transform(
                            entries, std::back_inserter(restored), [](MapEntryBefore& entry) {
                                return MapEntry{
                                    .op_id = entry.op_id,
                                    .value = std::move(entry.value),
                                };
                            });
                        object->map_entries.insert_or_assign(before.key, std::move(restored));
                    },
                },
                before.entries);
        };
        std::ranges::for_each(map_keys_before_ | std::views::reverse, restore_map_key);
        if (first_map_key_before_) {
            restore_map_key(*first_map_key_before_);
        }

        std::ranges::for_each(
            object_inserts_ | std::views::reverse,
            [&](const ObjectInsert& inserted) { state.objects.erase(inserted.obj); });

        active_ = false;
    }

private:
    struct MapEntryBefore {
        OpId op_id;
        Value value;
    };

    struct MapKeyAbsent {};

    using MapEntriesBefore =
        std::variant<MapKeyAbsent, MapEntryBefore, std::vector<MapEntryBefore>>;

    struct MapKeyBefore {
        ObjId obj;
        std::string key;
        MapEntriesBefore entries;
    };

    struct ListInsert {
        ObjId obj;
        OpId element_id;
    };

    struct ListValueBefore {
        ObjId obj;
        OpId element_id;
        Value value;
    };

    struct ListVisibilityBefore {
        ObjId obj;
        OpId element_id;
        bool visible;
    };

    struct MarkInsert {
        ObjId obj;
        OpId mark_id;
    };

    struct ObjectInsert {
        ObjId obj;
    };

    struct CommitBefore {
        std::uint64_t local_seq;
        std::vector<ChangeHash> heads;
        std::optional<std::uint64_t> actor_clock;
        std::size_t change_history_size;
    };

    std::optional<MapKeyBefore> first_map_key_before_;
    std::vector<MapKeyBefore> map_keys_before_;
    std::vector<ListInsert> list_inserts_;
    std::vector<ListValueBefore> list_values_before_;
    std::vector<ListVisibilityBefore> list_visibility_before_;
    std::vector<MarkInsert> mark_inserts_;
    std::vector<ObjectInsert> object_inserts_;
    std::optional<CommitBefore> commit_before_;
    std::uint64_t start_op_;
    bool active_{true};
};

} // namespace automerge_cpp::detail
