#include <automerge-cpp/transaction.hpp>

#include "doc_state.hpp"
#include "transaction_rollback.hpp"
#include "utf8.hpp"

namespace automerge_cpp {

Transaction::Transaction(detail::DocState& state)
    : state_{state}, rollback_state_{std::make_unique<detail::TransactionRollback>(state)} {}

Transaction::~Transaction() {
    rollback();
}

void Transaction::rollback() {
    rollback_state_->rollback(state_);
    pending_ops_.clear();
}

void Transaction::put(const ObjId& obj, std::string_view key, ScalarValue val) {
    rollback_state_->assert_active();
    auto key_str = std::string{key};
    auto pred = state_.map_pred(obj, key_str);
    rollback_state_->record_map_key(state_, obj, key_str);
    auto op_id = state_.next_op_id();
    auto value = Value{std::move(val)};
    state_.map_put(obj, key_str, op_id, value);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::move(key_str)),
        .action = OpType::put,
        .value = std::move(value),
        .pred = std::move(pred),
    };
    pending_ops_.push_back(std::move(op));
}

auto Transaction::put_object(const ObjId& obj, std::string_view key, ObjType type) -> ObjId {
    rollback_state_->assert_active();
    auto key_str = std::string{key};
    auto pred = state_.map_pred(obj, key_str);
    rollback_state_->record_map_key(state_, obj, key_str);
    auto op_id = state_.next_op_id();
    rollback_state_->record_object_insert(ObjId{op_id});
    auto new_obj = state_.create_object(op_id, type);
    auto value = Value{type};
    state_.map_put(obj, key_str, op_id, value);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::move(key_str)),
        .action = OpType::make_object,
        .value = std::move(value),
        .pred = std::move(pred),
    };
    pending_ops_.push_back(std::move(op));
    return new_obj;
}

void Transaction::delete_key(const ObjId& obj, std::string_view key) {
    rollback_state_->assert_active();
    auto key_str = std::string{key};
    auto pred = state_.map_pred(obj, key_str);
    rollback_state_->record_map_key(state_, obj, key_str);
    auto op_id = state_.next_op_id();
    state_.map_delete(obj, key_str);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::move(key_str)),
        .action = OpType::del,
        .value = Value{ScalarValue{Null{}}},
        .pred = std::move(pred),
    };
    pending_ops_.push_back(std::move(op));
}

void Transaction::insert(const ObjId& obj, std::size_t index, ScalarValue val) {
    rollback_state_->assert_active();
    auto insert_after = state_.insert_after_for(obj, index);
    auto op_id = state_.next_op_id();
    rollback_state_->record_list_insert(obj, op_id);
    auto value = Value{std::move(val)};
    state_.list_insert(obj, index, op_id, value, insert_after);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = list_index(index),
        .action = OpType::insert,
        .value = std::move(value),
        .pred = {},
        .insert_after = insert_after,
    };
    pending_ops_.push_back(std::move(op));
}

auto Transaction::insert_object(const ObjId& obj, std::size_t index, ObjType type) -> ObjId {
    rollback_state_->assert_active();
    auto insert_after = state_.insert_after_for(obj, index);
    auto op_id = state_.next_op_id();
    rollback_state_->record_list_insert(obj, op_id);
    rollback_state_->record_object_insert(ObjId{op_id});
    auto new_obj = state_.create_object(op_id, type);
    auto value = Value{type};
    state_.list_insert(obj, index, op_id, value, insert_after);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = list_index(index),
        .action = OpType::insert,
        .value = std::move(value),
        .pred = {},
        .insert_after = insert_after,
    };
    pending_ops_.push_back(std::move(op));
    return new_obj;
}

void Transaction::set(const ObjId& obj, std::size_t index, ScalarValue val) {
    rollback_state_->assert_active();
    auto pred = state_.list_pred(obj, index);
    rollback_state_->record_list_value(state_, obj, index);
    auto op_id = state_.next_op_id();
    auto value = Value{std::move(val)};
    state_.list_set(obj, index, op_id, value);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = list_index(index),
        .action = OpType::put,
        .value = std::move(value),
        .pred = std::move(pred),
    };
    pending_ops_.push_back(std::move(op));
}

void Transaction::delete_index(const ObjId& obj, std::size_t index) {
    rollback_state_->assert_active();
    auto pred = state_.list_pred(obj, index);
    rollback_state_->record_list_visibility(state_, obj, index);
    auto op_id = state_.next_op_id();
    state_.list_delete(obj, index);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = list_index(index),
        .action = OpType::del,
        .value = Value{ScalarValue{Null{}}},
        .pred = std::move(pred),
    };
    pending_ops_.push_back(std::move(op));
}

auto Transaction::splice_text(const ObjId& obj,
                              std::size_t pos,
                              std::size_t del,
                              std::string_view text) -> std::expected<void, Error> {
    rollback_state_->assert_active();
    const auto scalars = detail::split_utf8_scalars(text);
    if (!scalars) {
        return std::unexpected{scalars.error()};
    }

    // Insert one complete scalar value per op, chaining insert_after.
    auto prev_op_id = std::optional<OpId>{};
    for (std::size_t i = 0; i < scalars->size(); ++i) {
        auto insert_after = std::optional<OpId>{};
        if (i == 0) {
            insert_after = state_.insert_after_for(obj, pos);
        } else {
            insert_after = prev_op_id;
        }

        auto op_id = state_.next_op_id();
        rollback_state_->record_list_insert(obj, op_id);
        prev_op_id = op_id;
        auto scalar_value = Value{ScalarValue{std::string{(*scalars)[i]}}};
        state_.list_insert(obj, pos + i, op_id, scalar_value, insert_after);
        auto op = Op{
            .id = op_id,
            .obj = obj,
            .key = list_index(pos + i),
            .action = OpType::splice_text,
            .value = std::move(scalar_value),
            .pred = {},
            .insert_after = insert_after,
        };
        pending_ops_.push_back(std::move(op));
    }

    // Upstream assigns insertion operation IDs before deletion operation IDs.
    const auto delete_position = pos + scalars->size();
    for (std::size_t i = 0; i < del; ++i) {
        auto pred = state_.list_pred(obj, delete_position);
        rollback_state_->record_list_visibility(state_, obj, delete_position);
        auto op_id = state_.next_op_id();
        state_.list_delete(obj, delete_position);
        auto op = Op{
            .id = op_id,
            .obj = obj,
            .key = list_index(pos),
            .action = OpType::del,
            .value = Value{ScalarValue{Null{}}},
            .pred = std::move(pred),
        };
        pending_ops_.push_back(std::move(op));
    }
    return {};
}

void Transaction::increment(const ObjId& obj, std::string_view key, std::int64_t delta) {
    rollback_state_->assert_active();
    auto key_str = std::string{key};
    auto pred = state_.map_pred(obj, key_str);
    rollback_state_->record_map_key(state_, obj, key_str);
    auto op_id = state_.next_op_id();
    state_.counter_increment(obj, key_str, delta);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::move(key_str)),
        .action = OpType::increment,
        .value = Value{ScalarValue{Counter{delta}}},
        .pred = std::move(pred),
    };
    pending_ops_.push_back(std::move(op));
}

void Transaction::mark(const ObjId& obj, std::size_t start, std::size_t end,
                       std::string_view name, ScalarValue value) {
    rollback_state_->assert_active();
    const auto start_elem = state_.list_element_id_at(obj, start);
    assert(start_elem);
    const auto end_elem = state_.list_element_id_at(obj, end > 0 ? end - 1 : 0);
    assert(end_elem);
    const auto start_after = state_.insert_after_for(obj, start);
    const auto end_after = state_.insert_after_for(obj, end);

    const auto name_string = std::string{name};
    const auto begin_id = state_.next_op_id();
    const auto end_id = state_.next_op_id();
    rollback_state_->record_mark_insert(obj, begin_id);
    state_.mark_range(obj, begin_id, *start_elem, *end_elem, name_string, value);
    auto begin = Op{
        .id = begin_id,
        .obj = obj,
        .key = list_index(start),
        .action = OpType::mark,
        .value = Value{value},
        .pred = {},
        .insert_after = start_after,
        .expand = true,
        .mark_name = name_string,
    };
    auto finish = Op{
        .id = end_id,
        .obj = obj,
        .key = list_index(end),
        .action = OpType::mark,
        .value = Value{ScalarValue{Null{}}},
        .pred = {},
        .insert_after = end_after,
        .expand = true,
    };
    pending_ops_.push_back(std::move(begin));
    pending_ops_.push_back(std::move(finish));
}

void Transaction::commit(CommitOptions options) {
    if (!rollback_state_->is_active()) {
        return;
    }
    if (pending_ops_.empty()) {
        rollback_state_->finish_commit();
        return;
    }

    rollback_state_->begin_commit(state_);

    auto dependencies = rollback_state_->heads_before();
    if (state_.compaction_checkpoint) {
        std::ranges::copy_if(state_.compaction_checkpoint->heads,
                             std::back_inserter(dependencies),
                             [&](const ChangeHash& head) {
                                 return std::ranges::find(dependencies, head) == dependencies.end();
                             });
    }

    auto change = Change{
        .actor = state_.actor,
        .seq = ++state_.local_seq,
        .start_op = rollback_state_->start_op(),
        .timestamp = options.time.value_or(0),
        .message = std::move(options.message),
        .deps = std::move(dependencies),
        .operations = std::move(pending_ops_),
    };

    // Update clock
    state_.clock[state_.actor] = change.seq;

    // Store change
    state_.append_local_change(std::move(change));
    rollback_state_->finish_commit();
}

// =============================================================================
// Read methods (lock-free, safe within transact's exclusive lock)
// =============================================================================

auto Transaction::get(const ObjId& obj, std::string_view key) const -> std::optional<Value> {
    return state_.map_get(obj, std::string{key});
}

auto Transaction::get(const ObjId& obj, std::size_t index) const -> std::optional<Value> {
    return state_.list_get(obj, index);
}

auto Transaction::object_type(const ObjId& obj) const -> std::optional<ObjType> {
    return state_.object_type(obj);
}

auto Transaction::get_obj_id(const ObjId& obj, std::string_view key) const -> std::optional<ObjId> {
    return state_.get_obj_id_for_key(obj, std::string{key});
}

auto Transaction::get_obj_id(const ObjId& obj, std::size_t index) const -> std::optional<ObjId> {
    return state_.get_obj_id_for_index(obj, index);
}

auto Transaction::length(const ObjId& obj) const -> std::size_t {
    return state_.object_length(obj);
}

auto Transaction::keys(const ObjId& obj) const -> std::vector<std::string> {
    return state_.map_keys(obj);
}

auto Transaction::text(const ObjId& obj) const -> std::string {
    return state_.text_content(obj);
}

}  // namespace automerge_cpp
