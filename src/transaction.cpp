#include <automerge-cpp/transaction.hpp>

#include "doc_state.hpp"

namespace automerge_cpp {

Transaction::Transaction(detail::DocState& state)
    : state_{state}, start_op_{state.next_counter} {}

void Transaction::put(const ObjId& obj, std::string_view key, ScalarValue val) {
    auto pred = state_.map_pred(obj, std::string{key});
    auto op_id = state_.next_op_id();
    auto value = Value{std::move(val)};
    state_.map_put(obj, std::string{key}, op_id, value);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::string{key}),
        .action = OpType::put,
        .value = std::move(value),
        .pred = std::move(pred),
    };
    state_.op_log.push_back(op);
    pending_ops_.push_back(std::move(op));
}

auto Transaction::put_object(const ObjId& obj, std::string_view key, ObjType type) -> ObjId {
    auto pred = state_.map_pred(obj, std::string{key});
    auto op_id = state_.next_op_id();
    auto new_obj = state_.create_object(op_id, type);
    auto value = Value{type};
    state_.map_put(obj, std::string{key}, op_id, value);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::string{key}),
        .action = OpType::make_object,
        .value = std::move(value),
        .pred = std::move(pred),
    };
    state_.op_log.push_back(op);
    pending_ops_.push_back(std::move(op));
    return new_obj;
}

void Transaction::delete_key(const ObjId& obj, std::string_view key) {
    auto pred = state_.map_pred(obj, std::string{key});
    auto op_id = state_.next_op_id();
    state_.map_delete(obj, std::string{key});
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::string{key}),
        .action = OpType::del,
        .value = Value{ScalarValue{Null{}}},
        .pred = std::move(pred),
    };
    state_.op_log.push_back(op);
    pending_ops_.push_back(std::move(op));
}

void Transaction::insert(const ObjId& obj, std::size_t index, ScalarValue val) {
    auto insert_after = state_.insert_after_for(obj, index);
    auto op_id = state_.next_op_id();
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
    state_.op_log.push_back(op);
    pending_ops_.push_back(std::move(op));
}

auto Transaction::insert_object(const ObjId& obj, std::size_t index, ObjType type) -> ObjId {
    auto insert_after = state_.insert_after_for(obj, index);
    auto op_id = state_.next_op_id();
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
    state_.op_log.push_back(op);
    pending_ops_.push_back(std::move(op));
    return new_obj;
}

void Transaction::set(const ObjId& obj, std::size_t index, ScalarValue val) {
    auto pred = state_.list_pred(obj, index);
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
    state_.op_log.push_back(op);
    pending_ops_.push_back(std::move(op));
}

void Transaction::delete_index(const ObjId& obj, std::size_t index) {
    auto pred = state_.list_pred(obj, index);
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
    state_.op_log.push_back(op);
    pending_ops_.push_back(std::move(op));
}

void Transaction::splice_text(const ObjId& obj, std::size_t pos, std::size_t del,
                               std::string_view text) {
    // Delete characters
    for (std::size_t i = 0; i < del; ++i) {
        auto pred = state_.list_pred(obj, pos);
        auto op_id = state_.next_op_id();
        state_.list_delete(obj, pos);
        auto op = Op{
            .id = op_id,
            .obj = obj,
            .key = list_index(pos),
            .action = OpType::del,
            .value = Value{ScalarValue{Null{}}},
            .pred = std::move(pred),
        };
        state_.op_log.push_back(op);
        pending_ops_.push_back(std::move(op));
    }

    // Insert characters — one op per character, chaining insert_after
    auto prev_op_id = std::optional<OpId>{};
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto insert_after = std::optional<OpId>{};
        if (i == 0) {
            insert_after = state_.insert_after_for(obj, pos);
        } else {
            insert_after = prev_op_id;
        }

        auto op_id = state_.next_op_id();
        prev_op_id = op_id;
        auto char_val = Value{ScalarValue{std::string(1, text[i])}};
        state_.list_insert(obj, pos + i, op_id, char_val, insert_after);
        auto op = Op{
            .id = op_id,
            .obj = obj,
            .key = list_index(pos + i),
            .action = OpType::splice_text,
            .value = std::move(char_val),
            .pred = {},
            .insert_after = insert_after,
        };
        state_.op_log.push_back(op);
        pending_ops_.push_back(std::move(op));
    }
}

void Transaction::increment(const ObjId& obj, std::string_view key, std::int64_t delta) {
    auto pred = state_.map_pred(obj, std::string{key});
    auto op_id = state_.next_op_id();
    state_.counter_increment(obj, std::string{key}, delta);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::string{key}),
        .action = OpType::increment,
        .value = Value{ScalarValue{Counter{delta}}},
        .pred = std::move(pred),
    };
    state_.op_log.push_back(op);
    pending_ops_.push_back(std::move(op));
}

void Transaction::mark(const ObjId& obj, std::size_t start, std::size_t end,
                       std::string_view name, ScalarValue value) {
    // Resolve start and end to element OpIds
    auto start_elem = state_.list_element_id_at(obj, start);
    assert(start_elem);
    auto end_elem = state_.list_element_id_at(obj, end > 0 ? end - 1 : 0);
    assert(end_elem);

    auto op_id = state_.next_op_id();
    state_.mark_range(obj, op_id, *start_elem, *end_elem,
                      std::string{name}, value);
    auto op = Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::string{name}),
        .action = OpType::mark,
        .value = Value{value},
        .pred = {*start_elem, *end_elem},
    };
    state_.op_log.push_back(op);
    pending_ops_.push_back(std::move(op));
}

void Transaction::commit() {
    if (pending_ops_.empty()) return;

    auto change = Change{
        .actor = state_.actor,
        .seq = ++state_.local_seq,
        .start_op = start_op_,
        .timestamp = 0,
        .message = std::nullopt,
        .deps = state_.heads,
        .operations = std::move(pending_ops_),
    };

    auto hash = detail::DocState::compute_change_hash(change);

    // Update heads: remove deps, add new hash
    auto new_heads = std::vector<ChangeHash>{};
    for (const auto& h : state_.heads) {
        if (std::ranges::find(change.deps, h) == change.deps.end()) {
            new_heads.push_back(h);
        }
    }
    new_heads.push_back(hash);
    state_.heads = std::move(new_heads);

    // Update clock
    state_.clock[state_.actor] = change.seq;

    // Store change
    state_.change_history.push_back(std::move(change));
}

}  // namespace automerge_cpp
