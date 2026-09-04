#include <automerge-cpp/transaction.hpp>

#include "doc_state.hpp"
#include "transaction_rollback.hpp"
#include "utf8.hpp"

namespace automerge_cpp {

Transaction::Transaction(detail::DocState& state)
    : state_{state}, rollback_state_{std::make_unique<detail::TransactionRollback>(state)} {}

Transaction::~Transaction() {
    if (rollback_state_->is_active()) {
        rollback_state_->rollback(state_);
        pending_ops_.clear();
    }
}

auto Transaction::validate_active() const -> std::expected<void, Error> {
    if (!rollback_state_->is_active()) {
        return std::unexpected{
            Error{ErrorKind::inactive_transaction, "transaction is no longer active"}};
    }
    return {};
}

auto Transaction::validate_key(std::string_view key) const -> std::expected<void, Error> {
    if (!detail::split_utf8_scalars(key)) {
        return std::unexpected{Error{ErrorKind::invalid_key, "map keys must be valid UTF-8"}};
    }
    return {};
}

auto Transaction::validate_scalar(const ScalarValue& value) const -> std::expected<void, Error> {
    const auto* string = std::get_if<std::string>(&value);
    if (string && !detail::split_utf8_scalars(*string)) {
        return std::unexpected{Error{ErrorKind::invalid_utf8, "string values must be valid UTF-8"}};
    }
    return {};
}

auto Transaction::validate_object(const ObjId& obj,
                                  std::initializer_list<ObjType> allowed_types) const
    -> std::expected<const detail::ObjectState*, Error> {
    const auto* object = state_.get_object(obj);
    if (!object) {
        return std::unexpected{
            Error{ErrorKind::invalid_obj_id, "object identifier does not exist"}};
    }
    if (std::ranges::find(allowed_types, object->type) == allowed_types.end()) {
        return std::unexpected{
            Error{ErrorKind::invalid_object_type, "object has the wrong type for this operation"}};
    }
    return object;
}

auto Transaction::validate_index(const ObjId& obj, std::size_t index, bool allow_end) const
    -> std::expected<void, Error> {
    const auto object = validate_object(obj, {ObjType::list, ObjType::text});
    if (!object) {
        return std::unexpected{object.error()};
    }
    const auto length = state_.list_length(obj);
    const auto valid = allow_end ? index <= length : index < length;
    if (!valid) {
        return std::unexpected{
            Error{ErrorKind::index_out_of_bounds, "list or text index is out of bounds"}};
    }
    return {};
}

auto Transaction::state_length(const ObjId& obj) const -> std::size_t {
    return state_.list_length(obj);
}

auto Transaction::rollback() -> std::expected<void, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    rollback_state_->rollback(state_);
    pending_ops_.clear();
    return {};
}

auto Transaction::put(const ObjId& obj, std::string_view key, ScalarValue val)
    -> std::expected<void, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto object = validate_object(obj, {ObjType::map, ObjType::table});
    if (!object) {
        return std::unexpected{object.error()};
    }
    const auto valid_key = validate_key(key);
    if (!valid_key) {
        return std::unexpected{valid_key.error()};
    }
    const auto valid_value = validate_scalar(val);
    if (!valid_value) {
        return std::unexpected{valid_value.error()};
    }
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
    return {};
}

auto Transaction::put_object(const ObjId& obj, std::string_view key, ObjType type)
    -> std::expected<ObjId, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto object = validate_object(obj, {ObjType::map, ObjType::table});
    if (!object) {
        return std::unexpected{object.error()};
    }
    const auto valid_key = validate_key(key);
    if (!valid_key) {
        return std::unexpected{valid_key.error()};
    }
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

auto Transaction::delete_key(const ObjId& obj, std::string_view key) -> std::expected<void, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto object = validate_object(obj, {ObjType::map, ObjType::table});
    if (!object) {
        return std::unexpected{object.error()};
    }
    const auto valid_key = validate_key(key);
    if (!valid_key) {
        return std::unexpected{valid_key.error()};
    }
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
    return {};
}

auto Transaction::insert(const ObjId& obj, std::size_t index, ScalarValue val)
    -> std::expected<void, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto valid_index = validate_index(obj, index, true);
    if (!valid_index) {
        return std::unexpected{valid_index.error()};
    }
    const auto valid_value = validate_scalar(val);
    if (!valid_value) {
        return std::unexpected{valid_value.error()};
    }
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
    return {};
}

auto Transaction::insert_object(const ObjId& obj, std::size_t index, ObjType type)
    -> std::expected<ObjId, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto valid_index = validate_index(obj, index, true);
    if (!valid_index) {
        return std::unexpected{valid_index.error()};
    }
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

auto Transaction::set(const ObjId& obj, std::size_t index, ScalarValue val)
    -> std::expected<void, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto valid_index = validate_index(obj, index, false);
    if (!valid_index) {
        return std::unexpected{valid_index.error()};
    }
    const auto valid_value = validate_scalar(val);
    if (!valid_value) {
        return std::unexpected{valid_value.error()};
    }
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
    return {};
}

auto Transaction::delete_index(const ObjId& obj, std::size_t index) -> std::expected<void, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto valid_index = validate_index(obj, index, false);
    if (!valid_index) {
        return std::unexpected{valid_index.error()};
    }
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
    return {};
}

auto Transaction::splice_text(const ObjId& obj,
                              std::size_t pos,
                              std::size_t del,
                              std::string_view text) -> std::expected<void, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto object = validate_object(obj, {ObjType::text});
    if (!object) {
        return std::unexpected{object.error()};
    }
    const auto length = state_.list_length(obj);
    if (pos > length || del > length - pos) {
        return std::unexpected{
            Error{ErrorKind::index_out_of_bounds, "text splice range is out of bounds"}};
    }
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

auto Transaction::increment(const ObjId& obj, std::string_view key, std::int64_t delta)
    -> std::expected<void, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto object = validate_object(obj, {ObjType::map, ObjType::table});
    if (!object) {
        return std::unexpected{object.error()};
    }
    const auto valid_key = validate_key(key);
    if (!valid_key) {
        return std::unexpected{valid_key.error()};
    }
    auto key_str = std::string{key};
    const auto value = state_.map_get(obj, key_str);
    const auto* scalar = value ? std::get_if<ScalarValue>(&*value) : nullptr;
    if (!scalar || !std::holds_alternative<Counter>(*scalar)) {
        return std::unexpected{
            Error{ErrorKind::invalid_value_type, "increment requires an existing counter"}};
    }
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
    return {};
}

auto Transaction::mark(const ObjId& obj,
                       std::size_t start,
                       std::size_t end,
                       std::string_view name,
                       ScalarValue value) -> std::expected<void, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto object = validate_object(obj, {ObjType::list, ObjType::text});
    if (!object) {
        return std::unexpected{object.error()};
    }
    const auto length = state_.list_length(obj);
    if (start >= end || end > length) {
        return std::unexpected{
            Error{ErrorKind::invalid_mark, "mark range must be non-empty and within the object"}};
    }
    const auto valid_name = validate_key(name);
    if (!valid_name) {
        return std::unexpected{valid_name.error()};
    }
    if (name.empty()) {
        return std::unexpected{
            Error{ErrorKind::invalid_mark, "mark names must be non-empty"}};
    }
    const auto valid_value = validate_scalar(value);
    if (!valid_value) {
        return std::unexpected{valid_value.error()};
    }
    const auto start_elem = state_.list_element_id_at(obj, start);
    const auto end_elem = state_.list_element_id_at(obj, end > 0 ? end - 1 : 0);
    if (!start_elem || !end_elem) {
        return std::unexpected{
            Error{ErrorKind::invalid_mark, "mark endpoints do not resolve to visible elements"}};
    }
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
    return {};
}

auto Transaction::commit(CommitOptions options) -> std::expected<void, Error> {
    if (!rollback_state_->is_active()) {
        return std::unexpected{
            Error{ErrorKind::inactive_transaction, "transaction is no longer active"}};
    }
    if (pending_ops_.empty()) {
        rollback_state_->finish_commit();
        return {};
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
    return {};
}

// =============================================================================
// Read methods (lock-free, safe within transact's exclusive lock)
// =============================================================================

auto Transaction::get(const ObjId& obj, std::string_view key) const
    -> std::expected<std::optional<Value>, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto object = validate_object(obj, {ObjType::map, ObjType::table});
    if (!object) {
        return std::unexpected{object.error()};
    }
    const auto valid_key = validate_key(key);
    if (!valid_key) {
        return std::unexpected{valid_key.error()};
    }
    return state_.map_get(obj, std::string{key});
}

auto Transaction::get(const ObjId& obj, std::size_t index) const -> std::expected<Value, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto valid_index = validate_index(obj, index, false);
    if (!valid_index) {
        return std::unexpected{valid_index.error()};
    }
    return *state_.list_get(obj, index);
}

auto Transaction::object_type(const ObjId& obj) const -> std::expected<ObjType, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto* object = state_.get_object(obj);
    if (!object) {
        return std::unexpected{
            Error{ErrorKind::invalid_obj_id, "object identifier does not exist"}};
    }
    return object->type;
}

auto Transaction::get_obj_id(const ObjId& obj, std::string_view key) const
    -> std::expected<std::optional<ObjId>, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto object = validate_object(obj, {ObjType::map, ObjType::table});
    if (!object) {
        return std::unexpected{object.error()};
    }
    const auto valid_key = validate_key(key);
    if (!valid_key) {
        return std::unexpected{valid_key.error()};
    }
    return state_.get_obj_id_for_key(obj, std::string{key});
}

auto Transaction::get_obj_id(const ObjId& obj, std::size_t index) const
    -> std::expected<std::optional<ObjId>, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto valid_index = validate_index(obj, index, false);
    if (!valid_index) {
        return std::unexpected{valid_index.error()};
    }
    return state_.get_obj_id_for_index(obj, index);
}

auto Transaction::length(const ObjId& obj) const -> std::expected<std::size_t, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto* object = state_.get_object(obj);
    if (!object) {
        return std::unexpected{
            Error{ErrorKind::invalid_obj_id, "object identifier does not exist"}};
    }
    return state_.object_length(obj);
}

auto Transaction::keys(const ObjId& obj) const -> std::expected<std::vector<std::string>, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto object = validate_object(obj, {ObjType::map, ObjType::table});
    if (!object) {
        return std::unexpected{object.error()};
    }
    return state_.map_keys(obj);
}

auto Transaction::text(const ObjId& obj) const -> std::expected<std::string, Error> {
    const auto active = validate_active();
    if (!active) {
        return std::unexpected{active.error()};
    }
    const auto object = validate_object(obj, {ObjType::text});
    if (!object) {
        return std::unexpected{object.error()};
    }
    return state_.text_content(obj);
}

} // namespace automerge_cpp
