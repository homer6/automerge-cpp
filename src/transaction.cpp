#include <automerge-cpp/transaction.hpp>

#include "doc_state.hpp"

namespace automerge_cpp {

Transaction::Transaction(detail::DocState& state)
    : state_{state} {}

void Transaction::put(const ObjId& obj, std::string_view key, ScalarValue val) {
    auto op_id = state_.next_op_id();
    auto value = Value{std::move(val)};
    state_.map_put(obj, std::string{key}, op_id, value);
    state_.op_log.push_back(Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::string{key}),
        .action = OpType::put,
        .value = std::move(value),
        .pred = {},
    });
}

auto Transaction::put_object(const ObjId& obj, std::string_view key, ObjType type) -> ObjId {
    auto op_id = state_.next_op_id();
    auto new_obj = state_.create_object(op_id, type);
    auto value = Value{type};
    state_.map_put(obj, std::string{key}, op_id, value);
    state_.op_log.push_back(Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::string{key}),
        .action = OpType::make_object,
        .value = std::move(value),
        .pred = {},
    });
    return new_obj;
}

void Transaction::delete_key(const ObjId& obj, std::string_view key) {
    auto op_id = state_.next_op_id();
    state_.map_delete(obj, std::string{key});
    state_.op_log.push_back(Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::string{key}),
        .action = OpType::del,
        .value = Value{ScalarValue{Null{}}},
        .pred = {},
    });
}

void Transaction::insert(const ObjId& obj, std::size_t index, ScalarValue val) {
    auto op_id = state_.next_op_id();
    auto value = Value{std::move(val)};
    state_.list_insert(obj, index, op_id, value);
    state_.op_log.push_back(Op{
        .id = op_id,
        .obj = obj,
        .key = list_index(index),
        .action = OpType::insert,
        .value = std::move(value),
        .pred = {},
    });
}

auto Transaction::insert_object(const ObjId& obj, std::size_t index, ObjType type) -> ObjId {
    auto op_id = state_.next_op_id();
    auto new_obj = state_.create_object(op_id, type);
    auto value = Value{type};
    state_.list_insert(obj, index, op_id, value);
    state_.op_log.push_back(Op{
        .id = op_id,
        .obj = obj,
        .key = list_index(index),
        .action = OpType::insert,
        .value = std::move(value),
        .pred = {},
    });
    return new_obj;
}

void Transaction::set(const ObjId& obj, std::size_t index, ScalarValue val) {
    auto op_id = state_.next_op_id();
    auto value = Value{std::move(val)};
    state_.list_set(obj, index, op_id, value);
    state_.op_log.push_back(Op{
        .id = op_id,
        .obj = obj,
        .key = list_index(index),
        .action = OpType::put,
        .value = std::move(value),
        .pred = {},
    });
}

void Transaction::delete_index(const ObjId& obj, std::size_t index) {
    auto op_id = state_.next_op_id();
    state_.list_delete(obj, index);
    state_.op_log.push_back(Op{
        .id = op_id,
        .obj = obj,
        .key = list_index(index),
        .action = OpType::del,
        .value = Value{ScalarValue{Null{}}},
        .pred = {},
    });
}

void Transaction::splice_text(const ObjId& obj, std::size_t pos, std::size_t del,
                               std::string_view text) {
    // Delete characters
    for (std::size_t i = 0; i < del; ++i) {
        auto op_id = state_.next_op_id();
        state_.list_delete(obj, pos);
        state_.op_log.push_back(Op{
            .id = op_id,
            .obj = obj,
            .key = list_index(pos),
            .action = OpType::del,
            .value = Value{ScalarValue{Null{}}},
            .pred = {},
        });
    }

    // Insert characters — one op per character
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto op_id = state_.next_op_id();
        auto char_val = Value{ScalarValue{std::string(1, text[i])}};
        state_.list_insert(obj, pos + i, op_id, char_val);
        state_.op_log.push_back(Op{
            .id = op_id,
            .obj = obj,
            .key = list_index(pos + i),
            .action = OpType::splice_text,
            .value = std::move(char_val),
            .pred = {},
        });
    }
}

void Transaction::increment(const ObjId& obj, std::string_view key, std::int64_t delta) {
    auto op_id = state_.next_op_id();
    state_.counter_increment(obj, std::string{key}, delta);
    state_.op_log.push_back(Op{
        .id = op_id,
        .obj = obj,
        .key = map_key(std::string{key}),
        .action = OpType::increment,
        .value = Value{ScalarValue{Counter{delta}}},
        .pred = {},
    });
}

void Transaction::commit() {
    // Phase 2: no-op. Change hashing is Phase 3.
}

}  // namespace automerge_cpp
