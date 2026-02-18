#pragma once

// Columnar encoding/decoding of operations within a change chunk.
//
// Operations are stored across parallel columns. Each column stores one
// field of all ops, using the appropriate column encoding (RLE, delta, boolean).
//
// Column layout for change ops (upstream compatible):
//   OBJ_ACTOR   (0, actor_id)   — actor index of obj, RLE
//   OBJ_COUNTER (0, delta_int)  — counter of obj, Delta-RLE
//   KEY_ACTOR   (1, actor_id)   — actor index of key (for element keys), RLE
//   KEY_COUNTER (1, delta_int)  — counter of key (for element keys), Delta-RLE
//   KEY_STRING  (1, string_rle) — string key (for map keys), RLE
//   INSERT      (3, boolean)    — is this an insert op?
//   ACTION      (4, integer_rle)— action code, RLE
//   VAL_META    (5, value_meta) — value type+length
//   VAL_RAW     (5, value_raw)  — raw value bytes
//   PRED_GROUP  (7, group_card) — predecessor count per op, RLE
//   PRED_ACTOR  (7, actor_id)   — actor of each predecessor, RLE
//   PRED_COUNTER(7, delta_int)  — counter of each predecessor, Delta-RLE
//   EXPAND      (9, boolean)    — expand flag (marks)
//   MARK_NAME   (10, string_rle)— mark name
//
// Internal header — not installed.

#include <automerge-cpp/op.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include "column_spec.hpp"
#include "raw_column.hpp"
#include "value_encoding.hpp"
#include "../../encoding/rle.hpp"
#include "../../encoding/delta_encoder.hpp"
#include "../../encoding/boolean_encoder.hpp"
#include "../../encoding/leb128.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace automerge_cpp::storage {

// Map OpType + Value to upstream action code.
// make_object(map/table) → 0, put → 1, make_object(list/text) → 2,
// del → 3, increment → 4, mark → 5
inline auto op_to_action_code(const Op& op) -> std::uint64_t {
    switch (op.action) {
        case OpType::make_object: {
            auto* ot = std::get_if<ObjType>(&op.value);
            if (ot && (*ot == ObjType::list || *ot == ObjType::text)) {
                return 2;  // make list/text
            }
            return 0;  // make map/table
        }
        case OpType::put:         return 1;
        case OpType::del:         return 3;
        case OpType::increment:   return 4;
        case OpType::mark:        return 5;
        case OpType::insert:
        case OpType::splice_text:
            return 1;  // inserts use put action code + insert flag
    }
    return 1;
}

// Encode a list of operations into columnar format.
// actor_table maps actor -> index.
inline auto encode_change_ops(const std::vector<Op>& ops,
                                const std::vector<ActorId>& actor_table)
    -> std::vector<RawColumn> {

    auto find_actor_idx = [&](const ActorId& actor) -> std::uint64_t {
        for (std::size_t i = 0; i < actor_table.size(); ++i) {
            if (actor_table[i] == actor) return static_cast<std::uint64_t>(i);
        }
        assert(false && "actor not found in actor_table");
        return 0;
    };

    // Column encoders
    auto obj_actor_enc   = encoding::RleEncoder<std::uint64_t>{};
    auto obj_counter_enc = encoding::DeltaEncoder{};
    auto key_actor_enc   = encoding::RleEncoder<std::uint64_t>{};
    auto key_counter_enc = encoding::DeltaEncoder{};
    auto key_string_enc  = encoding::RleEncoder<std::string>{};
    auto insert_enc      = encoding::BooleanEncoder{};
    auto action_enc      = encoding::RleEncoder<std::uint64_t>{};
    auto val_meta         = std::vector<std::byte>{};
    auto val_raw          = std::vector<std::byte>{};
    auto pred_group_enc  = encoding::RleEncoder<std::uint64_t>{};
    auto pred_actor_enc  = encoding::RleEncoder<std::uint64_t>{};
    auto pred_counter_enc = encoding::DeltaEncoder{};
    auto expand_enc      = encoding::BooleanEncoder{};
    auto mark_name_enc   = encoding::RleEncoder<std::string>{};

    bool has_expand = false;
    bool has_mark_name = false;

    for (const auto& op : ops) {
        // OBJ: actor + counter
        if (op.obj.is_root()) {
            obj_actor_enc.append_null();
            obj_counter_enc.append(0);
        } else {
            const auto& obj_op = std::get<OpId>(op.obj.inner);
            obj_actor_enc.append(find_actor_idx(obj_op.actor));
            obj_counter_enc.append(static_cast<std::int64_t>(obj_op.counter));
        }

        // KEY: depends on whether it's a string key or an element reference
        const bool is_map_key = std::holds_alternative<std::string>(op.key);
        if (is_map_key) {
            key_actor_enc.append_null();
            key_counter_enc.append_null();
            key_string_enc.append(std::get<std::string>(op.key));
        } else {
            // For insert ops, key refers to the element being inserted after
            if (op.insert_after) {
                key_actor_enc.append(find_actor_idx(op.insert_after->actor));
                key_counter_enc.append(static_cast<std::int64_t>(op.insert_after->counter));
            } else if (op.action == OpType::insert || op.action == OpType::splice_text) {
                // Insert at head
                key_actor_enc.append_null();
                key_counter_enc.append(0);
            } else {
                // List put/del by index — use pred to find element
                if (!op.pred.empty()) {
                    key_actor_enc.append(find_actor_idx(op.pred[0].actor));
                    key_counter_enc.append(static_cast<std::int64_t>(op.pred[0].counter));
                } else {
                    key_actor_enc.append_null();
                    key_counter_enc.append(0);
                }
            }
            key_string_enc.append_null();
        }

        // INSERT flag
        const bool is_insert = (op.action == OpType::insert ||
                                op.action == OpType::splice_text);
        insert_enc.append(is_insert);

        // ACTION code
        action_enc.append(op_to_action_code(op));

        // VALUE
        encode_value(op.value, val_meta, val_raw);

        // PRED
        pred_group_enc.append(static_cast<std::uint64_t>(op.pred.size()));
        for (const auto& p : op.pred) {
            pred_actor_enc.append(find_actor_idx(p.actor));
            pred_counter_enc.append(static_cast<std::int64_t>(p.counter));
        }

        // EXPAND (mark-related)
        if (op.action == OpType::mark) {
            expand_enc.append(true);
            has_expand = true;
            if (is_map_key) {
                mark_name_enc.append(std::get<std::string>(op.key));
                has_mark_name = true;
            } else {
                mark_name_enc.append_null();
            }
        } else {
            expand_enc.append(false);
            mark_name_enc.append_null();
        }
    }

    // Finish all encoders
    obj_actor_enc.finish();
    obj_counter_enc.finish();
    key_actor_enc.finish();
    key_counter_enc.finish();
    key_string_enc.finish();
    insert_enc.finish();
    action_enc.finish();
    pred_group_enc.finish();
    pred_actor_enc.finish();
    pred_counter_enc.finish();
    expand_enc.finish();
    mark_name_enc.finish();

    // Build columns (in ascending spec order)
    auto columns = std::vector<RawColumn>{};

    auto add_col = [&](ColumnSpec spec, std::vector<std::byte> data) {
        if (!data.empty()) {
            columns.push_back(RawColumn{.spec = spec, .data = std::move(data)});
        }
    };

    add_col(change_op_columns::obj_actor,    obj_actor_enc.take());
    add_col(change_op_columns::obj_counter,  obj_counter_enc.take());
    add_col(change_op_columns::key_actor,    key_actor_enc.take());
    add_col(change_op_columns::key_counter,  key_counter_enc.take());
    add_col(change_op_columns::key_string,   key_string_enc.take());
    add_col(change_op_columns::insert,       insert_enc.take());
    add_col(change_op_columns::action,       action_enc.take());
    add_col(change_op_columns::value_meta,   std::move(val_meta));
    add_col(change_op_columns::value_raw,    std::move(val_raw));
    add_col(change_op_columns::pred_group,   pred_group_enc.take());
    add_col(change_op_columns::pred_actor,   pred_actor_enc.take());
    add_col(change_op_columns::pred_counter, pred_counter_enc.take());

    if (has_expand) {
        add_col(change_op_columns::expand, expand_enc.take());
    }
    if (has_mark_name) {
        add_col(change_op_columns::mark_name, mark_name_enc.take());
    }

    return columns;
}

// Decode operations from columnar format.
// Returns the decoded ops. start_op is the counter for the first op.
inline auto decode_change_ops(const std::vector<RawColumn>& columns,
                                const std::vector<ActorId>& actor_table,
                                ActorId change_actor,
                                std::uint64_t start_op,
                                std::size_t num_ops)
    -> std::optional<std::vector<Op>> {

    // Find columns by spec
    auto find_col = [&](ColumnSpec spec) -> std::span<const std::byte> {
        for (const auto& col : columns) {
            if (col.spec.column_id == spec.column_id &&
                col.spec.type == spec.type) {
                return col.data;
            }
        }
        return {};
    };

    auto obj_actor_data   = find_col(change_op_columns::obj_actor);
    auto obj_counter_data = find_col(change_op_columns::obj_counter);
    auto key_actor_data   = find_col(change_op_columns::key_actor);
    auto key_counter_data = find_col(change_op_columns::key_counter);
    auto key_string_data  = find_col(change_op_columns::key_string);
    auto insert_data      = find_col(change_op_columns::insert);
    auto action_data      = find_col(change_op_columns::action);
    auto val_meta_data    = find_col(change_op_columns::value_meta);
    auto val_raw_data     = find_col(change_op_columns::value_raw);
    auto pred_group_data  = find_col(change_op_columns::pred_group);
    auto pred_actor_data  = find_col(change_op_columns::pred_actor);
    auto pred_counter_data = find_col(change_op_columns::pred_counter);

    // Create decoders
    auto obj_actor_dec   = encoding::RleDecoder<std::uint64_t>{obj_actor_data};
    auto obj_counter_dec = encoding::DeltaDecoder{obj_counter_data};
    auto key_actor_dec   = encoding::RleDecoder<std::uint64_t>{key_actor_data};
    auto key_counter_dec = encoding::DeltaDecoder{key_counter_data};
    auto key_string_dec  = encoding::RleDecoder<std::string>{key_string_data};
    auto insert_dec      = encoding::BooleanDecoder{insert_data};
    auto action_dec      = encoding::RleDecoder<std::uint64_t>{action_data};
    auto pred_group_dec  = encoding::RleDecoder<std::uint64_t>{pred_group_data};
    auto pred_actor_dec  = encoding::RleDecoder<std::uint64_t>{pred_actor_data};
    auto pred_counter_dec = encoding::DeltaDecoder{pred_counter_data};

    auto val_meta_pos = std::size_t{0};
    auto val_raw_pos  = std::size_t{0};

    auto ops = std::vector<Op>{};
    ops.reserve(num_ops);

    for (std::size_t i = 0; i < num_ops; ++i) {
        auto op = Op{};

        // OpId: sequential from start_op
        op.id = OpId{start_op + i, change_actor};

        // OBJ
        auto obj_actor_val = obj_actor_dec.next();
        auto obj_counter_val = obj_counter_dec.next();
        if (!obj_actor_val || !obj_counter_val) return std::nullopt;

        if (!*obj_actor_val || (*obj_counter_val && **obj_counter_val == 0 && !*obj_actor_val)) {
            op.obj = ObjId{};  // root
        } else if (*obj_actor_val && *obj_counter_val) {
            auto actor_idx = **obj_actor_val;
            if (actor_idx >= actor_table.size()) return std::nullopt;
            op.obj = ObjId{OpId{static_cast<std::uint64_t>(**obj_counter_val),
                                 actor_table[static_cast<std::size_t>(actor_idx)]}};
        } else {
            op.obj = ObjId{};  // root
        }

        // KEY
        auto key_actor_val = key_actor_dec.next();
        auto key_counter_val = key_counter_dec.next();
        auto key_string_val = key_string_dec.next();

        if (!key_actor_val || !key_counter_val || !key_string_val) return std::nullopt;

        if (*key_string_val) {
            op.key = Prop{**key_string_val};
        } else {
            op.key = Prop{std::size_t{0}};  // list element key; position resolved via insert_after
        }

        // INSERT
        auto insert_val = insert_dec.next();
        if (!insert_val) return std::nullopt;
        bool is_insert = *insert_val;

        // Resolve insert_after from key columns
        if (is_insert && *key_actor_val && *key_counter_val) {
            auto actor_idx = **key_actor_val;
            if (actor_idx < actor_table.size()) {
                op.insert_after = OpId{static_cast<std::uint64_t>(**key_counter_val),
                                       actor_table[static_cast<std::size_t>(actor_idx)]};
            }
        } else if (is_insert && !*key_actor_val) {
            op.insert_after = std::nullopt;  // insert at head
        }

        // ACTION
        auto action_val = action_dec.next();
        if (!action_val || !*action_val) return std::nullopt;
        auto action_code = **action_val;

        // VALUE
        auto value = decode_value_from_columns(val_meta_data, val_meta_pos,
                                                val_raw_data, val_raw_pos);
        if (!value) return std::nullopt;

        // Map action code + insert flag to OpType + Value
        if (is_insert) {
            op.action = OpType::insert;
            op.value = *value;
            // Check if it's splice_text (insert of a string)
            if (const auto* sv = std::get_if<ScalarValue>(&op.value)) {
                if (std::holds_alternative<std::string>(*sv)) {
                    op.action = OpType::splice_text;
                }
            }
        } else {
            switch (action_code) {
                case 0: {  // make map/table
                    op.action = OpType::make_object;
                    // Recover exact ObjType from encoded value
                    if (const auto* sv = std::get_if<ScalarValue>(& *value)) {
                        if (const auto* v = std::get_if<std::uint64_t>(sv)) {
                            op.value = Value{static_cast<ObjType>(*v)};
                        } else {
                            op.value = Value{ObjType::map};
                        }
                    } else {
                        op.value = Value{ObjType::map};
                    }
                    break;
                }
                case 1:  // put
                    op.action = OpType::put;
                    op.value = *value;
                    break;
                case 2: {  // make list/text
                    op.action = OpType::make_object;
                    if (const auto* sv = std::get_if<ScalarValue>(& *value)) {
                        if (const auto* v = std::get_if<std::uint64_t>(sv)) {
                            op.value = Value{static_cast<ObjType>(*v)};
                        } else {
                            op.value = Value{ObjType::list};
                        }
                    } else {
                        op.value = Value{ObjType::list};
                    }
                    break;
                }
                case 3:  // del
                    op.action = OpType::del;
                    op.value = *value;
                    break;
                case 4:  // increment
                    op.action = OpType::increment;
                    op.value = *value;
                    break;
                case 5:  // mark
                    op.action = OpType::mark;
                    op.value = *value;
                    break;
                default:
                    return std::nullopt;
            }
        }

        // PRED
        auto pred_group_val = pred_group_dec.next();
        if (!pred_group_val || !*pred_group_val) return std::nullopt;
        auto pred_count = **pred_group_val;

        op.pred.reserve(static_cast<std::size_t>(pred_count));
        for (std::uint64_t p = 0; p < pred_count; ++p) {
            auto pa = pred_actor_dec.next();
            auto pc = pred_counter_dec.next();
            if (!pa || !*pa || !pc || !*pc) return std::nullopt;
            auto actor_idx = **pa;
            if (actor_idx >= actor_table.size()) return std::nullopt;
            op.pred.push_back(OpId{static_cast<std::uint64_t>(**pc),
                                    actor_table[static_cast<std::size_t>(actor_idx)]});
        }

        ops.push_back(std::move(op));
    }

    return ops;
}

}  // namespace automerge_cpp::storage
