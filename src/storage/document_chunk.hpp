#pragma once

// Upstream document chunk decoding.
//
// A document chunk stores global actor and head tables followed by two sets of
// column metadata: the change graph and the operation set. The column data is
// stored after both metadata tables and may be compressed independently.

#include <automerge-cpp/change.hpp>

#include "change_chunk.hpp"
#include "chunk.hpp"
#include "columns/compression.hpp"
#include "columns/raw_column.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace automerge_cpp::storage {

namespace document_columns {
inline constexpr auto actor = ColumnSpec{0, ColumnType::actor_id, false};
inline constexpr auto sequence = ColumnSpec{0, ColumnType::delta_int, false};
inline constexpr auto max_operation = ColumnSpec{1, ColumnType::delta_int, false};
inline constexpr auto timestamp = ColumnSpec{2, ColumnType::delta_int, false};
inline constexpr auto message = ColumnSpec{3, ColumnType::string_rle, false};
inline constexpr auto dependency_count = ColumnSpec{4, ColumnType::group_card, false};
inline constexpr auto dependency_index = ColumnSpec{4, ColumnType::delta_int, false};
inline constexpr auto extra_metadata = ColumnSpec{5, ColumnType::value_meta, false};
inline constexpr auto extra_raw = ColumnSpec{5, ColumnType::value_raw, false};
} // namespace document_columns

namespace document_op_columns {
inline constexpr auto id_actor = ColumnSpec{column_id::id_actor, ColumnType::actor_id, false};
inline constexpr auto id_counter = ColumnSpec{column_id::id_counter, ColumnType::delta_int, false};
inline constexpr auto obj_actor = ColumnSpec{column_id::obj_actor, ColumnType::actor_id, false};
inline constexpr auto obj_counter =
    ColumnSpec{column_id::obj_counter, ColumnType::integer_rle, false};
inline constexpr auto key_actor = ColumnSpec{column_id::key_actor, ColumnType::actor_id, false};
inline constexpr auto key_counter =
    ColumnSpec{column_id::key_counter, ColumnType::delta_int, false};
inline constexpr auto key_string = ColumnSpec{column_id::key_string, ColumnType::string_rle, false};
inline constexpr auto insert = ColumnSpec{column_id::insert, ColumnType::boolean, false};
inline constexpr auto action = ColumnSpec{column_id::action, ColumnType::integer_rle, false};
inline constexpr auto value_metadata =
    ColumnSpec{column_id::value_meta, ColumnType::value_meta, false};
inline constexpr auto value_raw = ColumnSpec{column_id::value_raw, ColumnType::value_raw, false};
inline constexpr auto successor_count =
    ColumnSpec{column_id::succ_group, ColumnType::group_card, false};
inline constexpr auto successor_actor =
    ColumnSpec{column_id::succ_actor, ColumnType::actor_id, false};
inline constexpr auto successor_counter =
    ColumnSpec{column_id::succ_counter, ColumnType::delta_int, false};
inline constexpr auto expand = ColumnSpec{column_id::expand, ColumnType::boolean, false};
inline constexpr auto mark_name = ColumnSpec{column_id::mark_name, ColumnType::string_rle, false};
} // namespace document_op_columns

struct DocumentColumnLayout {
    ColumnSpec spec;
    std::size_t offset;
    std::size_t length;
};

inline auto read_document_uleb(std::span<const std::byte> input, std::size_t& position)
    -> std::optional<std::uint64_t> {
    if (position >= input.size()) {
        return std::nullopt;
    }
    const auto decoded = encoding::decode_uleb128(input.subspan(position));
    if (!decoded) {
        return std::nullopt;
    }
    position += decoded->bytes_read;
    return decoded->value;
}

inline auto parse_document_column_layout(std::span<const std::byte> input, std::size_t& position)
    -> std::optional<std::vector<DocumentColumnLayout>> {
    const auto count = read_document_uleb(input, position);
    if (!count || *count > input.size()) {
        return std::nullopt;
    }

    auto columns = std::vector<DocumentColumnLayout>{};
    columns.reserve(static_cast<std::size_t>(*count));
    auto offset = std::size_t{0};
    for (auto index = std::uint64_t{0}; index < *count; ++index) {
        const auto raw_spec = read_document_uleb(input, position);
        const auto length = read_document_uleb(input, position);
        if (!raw_spec || !length || *raw_spec > std::numeric_limits<std::uint32_t>::max() ||
            *length > input.size() - std::min(offset, input.size())) {
            return std::nullopt;
        }
        columns.push_back(DocumentColumnLayout{
            .spec = ColumnSpec::from_u32(static_cast<std::uint32_t>(*raw_spec)),
            .offset = offset,
            .length = static_cast<std::size_t>(*length),
        });
        offset += static_cast<std::size_t>(*length);
    }
    return columns;
}

inline auto document_column_data_length(const std::vector<DocumentColumnLayout>& columns)
    -> std::size_t {
    if (columns.empty()) {
        return 0;
    }
    return columns.back().offset + columns.back().length;
}

inline auto read_document_columns(std::span<const std::byte> input,
                                  std::size_t& position,
                                  const std::vector<DocumentColumnLayout>& layout)
    -> std::optional<std::vector<RawColumn>> {
    const auto length = document_column_data_length(layout);
    if (position > input.size() || length > input.size() - position) {
        return std::nullopt;
    }
    const auto data = input.subspan(position, length);
    position += length;

    auto columns = std::vector<RawColumn>{};
    columns.reserve(layout.size());
    for (const auto& entry : layout) {
        const auto encoded = data.subspan(entry.offset, entry.length);
        auto bytes = std::vector<std::byte>{encoded.begin(), encoded.end()};
        if (entry.spec.deflate) {
            const auto decompressed = deflate_decompress(bytes);
            if (!decompressed) {
                return std::nullopt;
            }
            bytes = std::move(*decompressed);
        }
        auto spec = entry.spec;
        spec.deflate = false;
        columns.push_back(RawColumn{.spec = spec, .data = std::move(bytes)});
    }
    return columns;
}

inline auto find_document_column(const std::vector<RawColumn>& columns, ColumnSpec spec)
    -> std::span<const std::byte> {
    const auto found = std::ranges::find_if(columns, [&](const RawColumn& column) {
        return column.spec.column_id == spec.column_id && column.spec.type == spec.type;
    });
    return found == columns.end() ? std::span<const std::byte>{} : found->data;
}

inline auto decode_document_rle(std::span<const std::byte> data)
    -> std::optional<std::vector<std::uint64_t>> {
    auto decoder = encoding::RleDecoder<std::uint64_t>{data};
    auto values = std::vector<std::uint64_t>{};
    while (const auto value = decoder.next()) {
        if (!*value) {
            return std::nullopt;
        }
        values.push_back(**value);
    }
    if (!decoder.done()) {
        return std::nullopt;
    }
    return values;
}

inline auto decode_document_delta(std::span<const std::byte> data)
    -> std::optional<std::vector<std::int64_t>> {
    auto decoder = encoding::DeltaDecoder{data};
    auto values = std::vector<std::int64_t>{};
    while (const auto value = decoder.next()) {
        if (!*value) {
            return std::nullopt;
        }
        values.push_back(**value);
    }
    if (!decoder.done()) {
        return std::nullopt;
    }
    return values;
}

template <typename Decoder, typename Value>
inline auto decode_document_nullable(Decoder decoder, std::size_t count, bool column_present)
    -> std::optional<std::vector<std::optional<Value>>> {
    if (!column_present) {
        return std::vector<std::optional<Value>>(count);
    }
    auto values = std::vector<std::optional<Value>>{};
    values.reserve(count);
    for (auto index = std::size_t{0}; index < count; ++index) {
        const auto value = decoder.next();
        if (!value) {
            return std::nullopt;
        }
        values.push_back(*value);
    }
    if (!decoder.done()) {
        return std::nullopt;
    }
    return values;
}

inline auto decode_document_booleans(std::span<const std::byte> data, std::size_t count)
    -> std::optional<std::vector<bool>> {
    if (data.empty()) {
        return std::vector<bool>(count, false);
    }
    auto decoder = encoding::BooleanDecoder{data};
    auto values = std::vector<bool>{};
    values.reserve(count);
    for (auto index = std::size_t{0}; index < count; ++index) {
        const auto value = decoder.next();
        if (!value) {
            return std::nullopt;
        }
        values.push_back(*value);
    }
    if (!decoder.done()) {
        return std::nullopt;
    }
    return values;
}

struct DocumentChangeMetadata {
    ActorId actor;
    std::uint64_t sequence;
    std::uint64_t start_operation;
    std::uint64_t max_operation;
    std::int64_t timestamp;
    std::optional<std::string> message;
    std::vector<std::size_t> dependencies;
    std::vector<std::optional<Op>> operations;
};

inline auto decode_document_changes(const std::vector<RawColumn>& columns,
                                    const std::vector<ActorId>& actors)
    -> std::optional<std::vector<DocumentChangeMetadata>> {
    const auto actor_indexes =
        decode_document_rle(find_document_column(columns, document_columns::actor));
    const auto sequences =
        decode_document_delta(find_document_column(columns, document_columns::sequence));
    const auto max_operations =
        decode_document_delta(find_document_column(columns, document_columns::max_operation));
    const auto dependency_counts =
        decode_document_rle(find_document_column(columns, document_columns::dependency_count));
    const auto dependency_indexes =
        decode_document_delta(find_document_column(columns, document_columns::dependency_index));
    if (!actor_indexes || !sequences || !max_operations || !dependency_counts ||
        !dependency_indexes) {
        return std::nullopt;
    }

    const auto count = actor_indexes->size();
    if (sequences->size() != count || max_operations->size() != count ||
        dependency_counts->size() != count) {
        return std::nullopt;
    }
    const auto timestamp_data = find_document_column(columns, document_columns::timestamp);
    const auto message_data = find_document_column(columns, document_columns::message);
    const auto timestamps = decode_document_nullable<encoding::DeltaDecoder, std::int64_t>(
        encoding::DeltaDecoder{timestamp_data}, count, !timestamp_data.empty());
    const auto messages = decode_document_nullable<encoding::RleDecoder<std::string>, std::string>(
        encoding::RleDecoder<std::string>{message_data}, count, !message_data.empty());
    if (!timestamps || !messages ||
        !find_document_column(columns, document_columns::extra_raw).empty()) {
        return std::nullopt;
    }

    auto changes = std::vector<DocumentChangeMetadata>{};
    changes.reserve(count);
    auto dependency_position = std::size_t{0};
    for (auto index = std::size_t{0}; index < count; ++index) {
        const auto actor_index = (*actor_indexes)[index];
        const auto sequence = (*sequences)[index];
        const auto max_operation = (*max_operations)[index];
        if (actor_index >= actors.size() || sequence <= 0 || max_operation <= 0) {
            return std::nullopt;
        }

        auto dependencies = std::vector<std::size_t>{};
        const auto dependency_count = (*dependency_counts)[index];
        dependencies.reserve(static_cast<std::size_t>(dependency_count));
        auto dependency_max_operation = std::uint64_t{0};
        for (auto dependency = std::uint64_t{0}; dependency < dependency_count; ++dependency) {
            if (dependency_position >= dependency_indexes->size()) {
                return std::nullopt;
            }
            const auto dependency_index = (*dependency_indexes)[dependency_position++];
            if (dependency_index < 0 || static_cast<std::size_t>(dependency_index) >= index) {
                return std::nullopt;
            }
            const auto resolved_index = static_cast<std::size_t>(dependency_index);
            dependencies.push_back(resolved_index);
            dependency_max_operation =
                std::max(dependency_max_operation, changes[resolved_index].max_operation);
        }
        const auto start_operation = dependency_max_operation + 1;
        if (static_cast<std::uint64_t>(max_operation) < start_operation) {
            return std::nullopt;
        }
        const auto operation_count = static_cast<std::size_t>(
            static_cast<std::uint64_t>(max_operation) - start_operation + 1);
        changes.push_back(DocumentChangeMetadata{
            .actor = actors[static_cast<std::size_t>(actor_index)],
            .sequence = static_cast<std::uint64_t>(sequence),
            .start_operation = start_operation,
            .max_operation = static_cast<std::uint64_t>(max_operation),
            .timestamp = (*timestamps)[index].value_or(0),
            .message = (*messages)[index],
            .dependencies = std::move(dependencies),
            .operations = std::vector<std::optional<Op>>(operation_count),
        });
    }
    if (dependency_position != dependency_indexes->size()) {
        return std::nullopt;
    }
    return changes;
}

struct DocumentOperationRow {
    Op operation;
    std::vector<OpId> successors;
};

inline auto document_object_type(std::uint64_t action) -> std::optional<ObjType> {
    switch (action) {
    case 0:
        return ObjType::map;
    case 2:
        return ObjType::list;
    case 4:
        return ObjType::text;
    case 6:
        return ObjType::table;
    default:
        return std::nullopt;
    }
}

inline auto decode_document_action(std::uint64_t action, bool insert, Value value)
    -> std::optional<std::pair<OpType, Value>> {
    if (const auto object_type = document_object_type(action)) {
        return std::pair{insert ? OpType::insert : OpType::make_object, Value{*object_type}};
    }
    if (action == 1) {
        auto operation_type = OpType::put;
        if (insert) {
            operation_type = OpType::insert;
            const auto* scalar = std::get_if<ScalarValue>(&value);
            if (scalar && std::holds_alternative<std::string>(*scalar)) {
                operation_type = OpType::splice_text;
            }
        }
        return std::pair{operation_type, std::move(value)};
    }
    if (action == 3) {
        return std::pair{OpType::del, std::move(value)};
    }
    if (action == 5) {
        const auto* scalar = std::get_if<ScalarValue>(&value);
        const auto* delta = scalar ? std::get_if<std::int64_t>(scalar) : nullptr;
        if (!delta) {
            return std::nullopt;
        }
        return std::pair{OpType::increment, Value{ScalarValue{Counter{*delta}}}};
    }
    if (action == 7) {
        return std::pair{OpType::mark, std::move(value)};
    }
    return std::nullopt;
}

struct DecodedDocumentOperationColumns {
    std::vector<std::uint64_t> id_actors;
    std::vector<std::int64_t> id_counters;
    std::vector<std::optional<std::uint64_t>> obj_actors;
    std::vector<std::optional<std::uint64_t>> obj_counters;
    std::vector<std::optional<std::uint64_t>> key_actors;
    std::vector<std::optional<std::int64_t>> key_counters;
    std::vector<std::optional<std::string>> key_strings;
    std::vector<bool> inserts;
    std::vector<std::uint64_t> actions;
    std::vector<std::uint64_t> value_metadata;
    std::span<const std::byte> value_raw;
    std::vector<std::uint64_t> successor_counts;
    std::vector<std::uint64_t> successor_actors;
    std::vector<std::int64_t> successor_counters;
    std::vector<bool> expands;
    std::vector<std::optional<std::string>> mark_names;
};

inline auto decode_document_operation_columns(const std::vector<RawColumn>& columns)
    -> std::optional<DecodedDocumentOperationColumns> {
    using UintDecoder = encoding::RleDecoder<std::uint64_t>;
    using StringDecoder = encoding::RleDecoder<std::string>;

    const auto id_actors =
        decode_document_rle(find_document_column(columns, document_op_columns::id_actor));
    const auto id_counters =
        decode_document_delta(find_document_column(columns, document_op_columns::id_counter));
    const auto actions =
        decode_document_rle(find_document_column(columns, document_op_columns::action));
    const auto value_metadata =
        decode_document_rle(find_document_column(columns, document_op_columns::value_metadata));
    const auto successor_counts =
        decode_document_rle(find_document_column(columns, document_op_columns::successor_count));
    if (!id_actors || !id_counters || !actions || !value_metadata || !successor_counts) {
        return std::nullopt;
    }
    const auto count = id_actors->size();
    if (id_counters->size() != count || actions->size() != count ||
        value_metadata->size() != count || successor_counts->size() != count) {
        return std::nullopt;
    }

    const auto obj_actor_data = find_document_column(columns, document_op_columns::obj_actor);
    const auto obj_counter_data = find_document_column(columns, document_op_columns::obj_counter);
    const auto key_actor_data = find_document_column(columns, document_op_columns::key_actor);
    const auto key_counter_data = find_document_column(columns, document_op_columns::key_counter);
    const auto key_string_data = find_document_column(columns, document_op_columns::key_string);
    const auto obj_actors = decode_document_nullable<UintDecoder, std::uint64_t>(
        UintDecoder{obj_actor_data}, count, !obj_actor_data.empty());
    const auto obj_counters = decode_document_nullable<UintDecoder, std::uint64_t>(
        UintDecoder{obj_counter_data}, count, !obj_counter_data.empty());
    const auto key_actors = decode_document_nullable<UintDecoder, std::uint64_t>(
        UintDecoder{key_actor_data}, count, !key_actor_data.empty());
    const auto key_counters = decode_document_nullable<encoding::DeltaDecoder, std::int64_t>(
        encoding::DeltaDecoder{key_counter_data}, count, !key_counter_data.empty());
    const auto key_strings = decode_document_nullable<StringDecoder, std::string>(
        StringDecoder{key_string_data}, count, !key_string_data.empty());
    const auto inserts =
        decode_document_booleans(find_document_column(columns, document_op_columns::insert), count);
    const auto expands =
        decode_document_booleans(find_document_column(columns, document_op_columns::expand), count);
    const auto mark_name_data = find_document_column(columns, document_op_columns::mark_name);
    const auto mark_names = decode_document_nullable<StringDecoder, std::string>(
        StringDecoder{mark_name_data}, count, !mark_name_data.empty());
    if (!obj_actors || !obj_counters || !key_actors || !key_counters || !key_strings || !inserts ||
        !expands || !mark_names) {
        return std::nullopt;
    }

    const auto successor_total =
        std::accumulate(successor_counts->begin(), successor_counts->end(), std::uint64_t{0});
    const auto successor_actors =
        decode_document_rle(find_document_column(columns, document_op_columns::successor_actor));
    const auto successor_counters = decode_document_delta(
        find_document_column(columns, document_op_columns::successor_counter));
    if (!successor_actors || !successor_counters || successor_actors->size() != successor_total ||
        successor_counters->size() != successor_total) {
        return std::nullopt;
    }

    return DecodedDocumentOperationColumns{
        .id_actors = std::move(*id_actors),
        .id_counters = std::move(*id_counters),
        .obj_actors = std::move(*obj_actors),
        .obj_counters = std::move(*obj_counters),
        .key_actors = std::move(*key_actors),
        .key_counters = std::move(*key_counters),
        .key_strings = std::move(*key_strings),
        .inserts = std::move(*inserts),
        .actions = std::move(*actions),
        .value_metadata = std::move(*value_metadata),
        .value_raw = find_document_column(columns, document_op_columns::value_raw),
        .successor_counts = std::move(*successor_counts),
        .successor_actors = std::move(*successor_actors),
        .successor_counters = std::move(*successor_counters),
        .expands = std::move(*expands),
        .mark_names = std::move(*mark_names),
    };
}

inline auto decode_document_operation(const DecodedDocumentOperationColumns& columns,
                                      const std::vector<ActorId>& actors,
                                      std::size_t index,
                                      std::size_t& value_position,
                                      std::size_t& successor_position)
    -> std::optional<DocumentOperationRow> {
    if (columns.id_actors[index] >= actors.size() || columns.id_counters[index] <= 0) {
        return std::nullopt;
    }
    const auto id = OpId{static_cast<std::uint64_t>(columns.id_counters[index]),
                         actors[static_cast<std::size_t>(columns.id_actors[index])]};

    auto object = ObjId{};
    if (columns.obj_actors[index] || columns.obj_counters[index]) {
        if (!columns.obj_actors[index] || !columns.obj_counters[index] ||
            *columns.obj_actors[index] >= actors.size()) {
            return std::nullopt;
        }
        object = ObjId{OpId{*columns.obj_counters[index],
                            actors[static_cast<std::size_t>(*columns.obj_actors[index])]}};
    }

    auto key = Prop{std::size_t{0}};
    if (columns.key_strings[index]) {
        key = Prop{*columns.key_strings[index]};
    }
    const auto value = decode_value_from_metadata(
        columns.value_metadata[index], columns.value_raw, value_position);
    const auto action = value ? decode_document_action(columns.actions[index],
                                                       columns.inserts[index],
                                                       std::move(*value))
                              : std::nullopt;
    if (!action) {
        return std::nullopt;
    }

    auto insert_after = std::optional<OpId>{};
    if (columns.inserts[index] && columns.key_actors[index]) {
        if (!columns.key_counters[index] || *columns.key_actors[index] >= actors.size() ||
            *columns.key_counters[index] <= 0) {
            return std::nullopt;
        }
        insert_after = OpId{
            static_cast<std::uint64_t>(*columns.key_counters[index]),
            actors[static_cast<std::size_t>(*columns.key_actors[index])],
        };
    }

    auto successors = std::vector<OpId>{};
    successors.reserve(static_cast<std::size_t>(columns.successor_counts[index]));
    for (auto successor = std::uint64_t{0}; successor < columns.successor_counts[index];
         ++successor) {
        if (successor_position >= columns.successor_actors.size() ||
            columns.successor_actors[successor_position] >= actors.size() ||
            columns.successor_counters[successor_position] <= 0) {
            return std::nullopt;
        }
        successors.push_back(OpId{
            static_cast<std::uint64_t>(columns.successor_counters[successor_position]),
            actors[static_cast<std::size_t>(columns.successor_actors[successor_position])],
        });
        ++successor_position;
    }

    return DocumentOperationRow{
        .operation =
            Op{
                .id = id,
                .obj = object,
                .key = std::move(key),
                .action = action->first,
                .value = std::move(action->second),
                .pred = {},
                .insert_after = insert_after,
                .expand = columns.expands[index],
                .mark_name = columns.mark_names[index],
            },
        .successors = std::move(successors),
    };
}

inline auto decode_document_operations(const std::vector<RawColumn>& raw_columns,
                                       const std::vector<ActorId>& actors)
    -> std::optional<std::vector<DocumentOperationRow>> {
    const auto columns = decode_document_operation_columns(raw_columns);
    if (!columns) {
        return std::nullopt;
    }

    auto value_position = std::size_t{0};
    auto successor_position = std::size_t{0};
    auto rows = std::vector<DocumentOperationRow>{};
    rows.reserve(columns->id_actors.size());
    for (auto index = std::size_t{0}; index < columns->id_actors.size(); ++index) {
        auto row =
            decode_document_operation(*columns, actors, index, value_position, successor_position);
        if (!row) {
            return std::nullopt;
        }
        rows.push_back(std::move(*row));
    }
    if (value_position != columns->value_raw.size() ||
        successor_position != columns->successor_actors.size()) {
        return std::nullopt;
    }
    return rows;
}

struct DocumentOperationLocation {
    ObjId object;
    Prop key;
};

inline auto restore_document_predecessors(std::vector<DocumentOperationRow> rows)
    -> std::optional<std::vector<Op>> {
    auto predecessors = std::unordered_map<OpId, std::vector<OpId>>{};
    auto locations = std::unordered_map<OpId, DocumentOperationLocation>{};
    auto present = std::unordered_set<OpId>{};
    std::ranges::for_each(rows, [&](const DocumentOperationRow& row) {
        present.insert(row.operation.id);
        std::ranges::for_each(row.successors, [&](const OpId& successor) {
            predecessors[successor].push_back(row.operation.id);
            locations.try_emplace(successor,
                                  DocumentOperationLocation{
                                      .object = row.operation.obj,
                                      .key = row.operation.key,
                                  });
        });
    });

    auto operations = std::vector<Op>{};
    operations.reserve(rows.size() + predecessors.size());
    std::ranges::for_each(rows, [&](DocumentOperationRow& row) {
        row.operation.pred = predecessors[row.operation.id];
        operations.push_back(std::move(row.operation));
    });
    std::ranges::for_each(predecessors, [&](const auto& entry) {
        if (present.contains(entry.first)) {
            return;
        }
        const auto location = locations.find(entry.first);
        if (location == locations.end()) {
            return;
        }
        operations.push_back(Op{
            .id = entry.first,
            .obj = location->second.object,
            .key = location->second.key,
            .action = OpType::del,
            .value = Value{ScalarValue{Null{}}},
            .pred = entry.second,
        });
    });
    return operations;
}

inline auto assemble_document_changes(std::vector<DocumentChangeMetadata> metadata,
                                      std::vector<Op> operations,
                                      const std::vector<ChangeHash>& expected_heads)
    -> std::optional<std::vector<Change>> {
    for (auto& operation : operations) {
        const auto change = std::ranges::find_if(metadata, [&](const auto& candidate) {
            return candidate.actor == operation.id.actor &&
                   operation.id.counter >= candidate.start_operation &&
                   operation.id.counter <= candidate.max_operation;
        });
        if (change == metadata.end()) {
            return std::nullopt;
        }
        const auto offset =
            static_cast<std::size_t>(operation.id.counter - change->start_operation);
        if (change->operations[offset]) {
            return std::nullopt;
        }
        change->operations[offset] = std::move(operation);
    }

    auto changes = std::vector<Change>{};
    auto hashes = std::vector<ChangeHash>{};
    changes.reserve(metadata.size());
    hashes.reserve(metadata.size());
    for (auto& entry : metadata) {
        if (std::ranges::any_of(entry.operations,
                                [](const auto& operation) { return !operation.has_value(); })) {
            return std::nullopt;
        }
        auto dependencies = std::vector<ChangeHash>{};
        dependencies.reserve(entry.dependencies.size());
        for (const auto dependency : entry.dependencies) {
            if (dependency >= hashes.size()) {
                return std::nullopt;
            }
            dependencies.push_back(hashes[dependency]);
        }
        auto change_operations = std::vector<Op>{};
        change_operations.reserve(entry.operations.size());
        std::ranges::transform(entry.operations,
                               std::back_inserter(change_operations),
                               [](auto& operation) { return std::move(*operation); });
        auto change = Change{
            .actor = entry.actor,
            .seq = entry.sequence,
            .start_op = entry.start_operation,
            .timestamp = entry.timestamp,
            .message = std::move(entry.message),
            .deps = std::move(dependencies),
            .operations = std::move(change_operations),
        };
        const auto body = serialize_change_body(change);
        const auto digest = compute_change_hash_from_chunk(ChunkType::change, body);
        auto hash = ChangeHash{};
        std::memcpy(hash.bytes.data(), digest.data(), hash.bytes.size());
        hashes.push_back(hash);
        changes.push_back(std::move(change));
    }

    auto derived_heads = hashes;
    std::ranges::for_each(changes, [&](const Change& change) {
        std::ranges::for_each(change.deps, [&](const ChangeHash& dependency) {
            std::erase(derived_heads, dependency);
        });
    });
    std::ranges::sort(derived_heads);
    auto sorted_expected_heads = expected_heads;
    std::ranges::sort(sorted_expected_heads);
    if (derived_heads != sorted_expected_heads) {
        return std::nullopt;
    }
    return changes;
}

inline auto parse_document_chunk(std::span<const std::byte> chunk)
    -> std::optional<std::vector<Change>> {
    const auto header = parse_chunk_header(chunk);
    if (!header || header->type != ChunkType::document ||
        header->body_offset + header->body_length != chunk.size() ||
        !validate_chunk_checksum(*header, chunk)) {
        return std::nullopt;
    }
    const auto body = chunk.subspan(header->body_offset, header->body_length);
    auto position = std::size_t{0};

    const auto actor_count = read_document_uleb(body, position);
    if (!actor_count || *actor_count > body.size()) {
        return std::nullopt;
    }
    auto actors = std::vector<ActorId>{};
    actors.reserve(static_cast<std::size_t>(*actor_count));
    for (auto index = std::uint64_t{0}; index < *actor_count; ++index) {
        const auto actor_length = read_document_uleb(body, position);
        if (!actor_length || *actor_length != ActorId::size ||
            *actor_length > body.size() - std::min(position, body.size())) {
            return std::nullopt;
        }
        auto actor = ActorId{};
        std::memcpy(actor.bytes.data(), body.data() + position, actor.bytes.size());
        position += actor.bytes.size();
        actors.push_back(actor);
    }

    const auto head_count = read_document_uleb(body, position);
    if (!head_count || *head_count > body.size() / ChangeHash::size) {
        return std::nullopt;
    }
    auto heads = std::vector<ChangeHash>{};
    heads.reserve(static_cast<std::size_t>(*head_count));
    for (auto index = std::uint64_t{0}; index < *head_count; ++index) {
        if (ChangeHash::size > body.size() - std::min(position, body.size())) {
            return std::nullopt;
        }
        auto hash = ChangeHash{};
        std::memcpy(hash.bytes.data(), body.data() + position, hash.bytes.size());
        position += hash.bytes.size();
        heads.push_back(hash);
    }

    const auto change_layout = parse_document_column_layout(body, position);
    const auto operation_layout = parse_document_column_layout(body, position);
    if (!change_layout || !operation_layout) {
        return std::nullopt;
    }
    const auto change_columns = read_document_columns(body, position, *change_layout);
    const auto operation_columns = read_document_columns(body, position, *operation_layout);
    if (!change_columns || !operation_columns) {
        return std::nullopt;
    }
    for (auto index = std::uint64_t{0}; index < *head_count; ++index) {
        if (!read_document_uleb(body, position)) {
            return std::nullopt;
        }
    }
    if (position != body.size()) {
        return std::nullopt;
    }

    auto metadata = decode_document_changes(*change_columns, actors);
    const auto rows = decode_document_operations(*operation_columns, actors);
    const auto operations = rows ? restore_document_predecessors(*rows) : std::nullopt;
    if (!metadata || !operations) {
        return std::nullopt;
    }
    return assemble_document_changes(std::move(*metadata), std::move(*operations), heads);
}

} // namespace automerge_cpp::storage
