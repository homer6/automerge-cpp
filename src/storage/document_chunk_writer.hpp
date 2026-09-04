#pragma once

// Upstream-compatible compact document chunk encoding.

#include <automerge-cpp/change.hpp>

#include "change_chunk.hpp"
#include "chunk.hpp"
#include "columns/compression.hpp"
#include "columns/raw_column.hpp"
#include "columns/value_encoding.hpp"
#include "document_chunk.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace automerge_cpp::storage {

struct DocumentWriteRow {
    Op operation;
    std::vector<OpId> successors;
};

struct DocumentListElement {
    OpId insert_id;
    std::optional<OpId> insert_after;
};

inline auto document_change_hash(const Change& change) -> ChangeHash {
    const auto body = serialize_change_body(change);
    const auto digest = compute_change_hash_from_chunk(ChunkType::change, body);
    auto hash = ChangeHash{};
    std::memcpy(hash.bytes.data(), digest.data(), hash.bytes.size());
    return hash;
}

inline auto document_actor_table(const std::vector<Change>& changes) -> std::vector<ActorId> {
    auto actors = std::set<ActorId>{};
    std::ranges::for_each(changes, [&](const Change& change) {
        actors.insert(change.actor);
        std::ranges::for_each(change.operations, [&](const Op& operation) {
            actors.insert(operation.id.actor);
            if (!operation.obj.is_root()) {
                actors.insert(std::get<OpId>(operation.obj.inner).actor);
            }
            if (operation.insert_after) {
                actors.insert(operation.insert_after->actor);
            }
            std::ranges::for_each(
                operation.pred, [&](const OpId& predecessor) { actors.insert(predecessor.actor); });
        });
    });
    return {actors.begin(), actors.end()};
}

inline auto document_actor_index(const std::vector<ActorId>& actors, const ActorId& actor)
    -> std::uint64_t {
    const auto found = std::ranges::lower_bound(actors, actor);
    return static_cast<std::uint64_t>(std::distance(actors.begin(), found));
}

inline auto encode_document_change_columns(const std::vector<Change>& changes,
                                           const std::vector<ActorId>& actors)
    -> std::vector<RawColumn> {
    auto actor = encoding::RleEncoder<std::uint64_t>{};
    auto sequence = encoding::DeltaEncoder{};
    auto max_operation = encoding::DeltaEncoder{};
    auto timestamp = encoding::DeltaEncoder{};
    auto message = encoding::RleEncoder<std::string>{};
    auto dependency_count = encoding::RleEncoder<std::uint64_t>{};
    auto dependency_index = encoding::DeltaEncoder{};
    auto extra_metadata = encoding::RleEncoder<std::uint64_t>{};
    auto hashes = std::unordered_map<ChangeHash, std::size_t>{};
    auto has_message = false;

    std::ranges::for_each(changes, [&](const Change& change) {
        actor.append(document_actor_index(actors, change.actor));
        sequence.append(static_cast<std::int64_t>(change.seq));
        const auto max_op = change.operations.empty()
                                ? change.start_op - 1
                                : change.start_op + change.operations.size() - 1;
        max_operation.append(static_cast<std::int64_t>(max_op));
        timestamp.append(change.timestamp);
        if (change.message) {
            message.append(*change.message);
            has_message = true;
        } else {
            message.append_null();
        }
        dependency_count.append(static_cast<std::uint64_t>(change.deps.size()));
        std::ranges::for_each(change.deps, [&](const ChangeHash& dependency) {
            dependency_index.append(static_cast<std::int64_t>(hashes.at(dependency)));
        });
        extra_metadata.append(static_cast<std::uint64_t>(ValueTag::bytes_tag));
        hashes.emplace(document_change_hash(change), hashes.size());
    });

    actor.finish();
    sequence.finish();
    max_operation.finish();
    timestamp.finish();
    message.finish();
    dependency_count.finish();
    dependency_index.finish();
    extra_metadata.finish();

    auto columns = std::vector<RawColumn>{};
    const auto add = [&](ColumnSpec spec, std::vector<std::byte> data) {
        if (!data.empty()) {
            columns.push_back(RawColumn{.spec = spec, .data = std::move(data)});
        }
    };
    add(document_columns::actor, actor.take());
    add(document_columns::sequence, sequence.take());
    add(document_columns::max_operation, max_operation.take());
    add(document_columns::timestamp, timestamp.take());
    if (has_message) {
        add(document_columns::message, message.take());
    }
    add(document_columns::dependency_count, dependency_count.take());
    add(document_columns::dependency_index, dependency_index.take());
    add(document_columns::extra_metadata, extra_metadata.take());
    return columns;
}

inline auto document_list_ranks(const std::vector<Change>& changes)
    -> std::unordered_map<OpId, std::size_t> {
    auto elements = std::unordered_map<ObjId, std::vector<DocumentListElement>>{};
    std::ranges::for_each(changes, [&](const Change& change) {
        std::ranges::for_each(change.operations, [&](const Op& operation) {
            if (operation.action != OpType::insert && operation.action != OpType::splice_text &&
                operation.action != OpType::mark) {
                return;
            }
            auto& object = elements[operation.obj];
            auto position = std::size_t{0};
            if (operation.insert_after) {
                const auto origin = std::ranges::find(
                    object, *operation.insert_after, &DocumentListElement::insert_id);
                position =
                    origin == object.end()
                        ? object.size()
                        : static_cast<std::size_t>(std::distance(object.begin(), origin)) + 1;
            }
            auto scanned = std::unordered_set<OpId>{};
            if (operation.insert_after) {
                scanned.insert(*operation.insert_after);
            }
            const auto first = object.begin() + static_cast<std::ptrdiff_t>(position);
            const auto stop = std::ranges::find_if(first, object.end(), [&](const auto& element) {
                const auto same_origin = element.insert_after == operation.insert_after;
                const auto origin_in_scan =
                    same_origin ||
                    (element.insert_after && scanned.contains(*element.insert_after));
                if (!origin_in_scan || (same_origin && element.insert_id <= operation.id)) {
                    return true;
                }
                scanned.insert(element.insert_id);
                return false;
            });
            position = static_cast<std::size_t>(std::distance(object.begin(), stop));
            object.insert(object.begin() + static_cast<std::ptrdiff_t>(position),
                          DocumentListElement{
                              .insert_id = operation.id,
                              .insert_after = operation.insert_after,
                          });
        });
    });

    auto ranks = std::unordered_map<OpId, std::size_t>{};
    std::ranges::for_each(elements, [&](const auto& object) {
        auto rank = std::size_t{0};
        std::ranges::for_each(object.second, [&](const DocumentListElement& element) {
            ranks.emplace(element.insert_id, rank++);
        });
    });
    return ranks;
}

inline auto document_register(const Op& operation) -> std::optional<OpId> {
    if (operation.action == OpType::insert || operation.action == OpType::splice_text ||
        operation.action == OpType::mark) {
        return operation.id;
    }
    return operation.pred.empty() ? std::nullopt : std::optional{operation.pred.front()};
}

inline auto collect_document_rows(const std::vector<Change>& changes)
    -> std::vector<DocumentWriteRow> {
    auto rows = std::vector<DocumentWriteRow>{};
    auto row_by_id = std::unordered_map<OpId, std::size_t>{};
    std::ranges::for_each(changes, [&](const Change& change) {
        std::ranges::for_each(change.operations, [&](const Op& operation) {
            if (operation.action != OpType::del || operation.pred.empty()) {
                row_by_id.emplace(operation.id, rows.size());
                rows.push_back(DocumentWriteRow{.operation = operation, .successors = {}});
            }
        });
    });
    std::ranges::for_each(changes, [&](const Change& change) {
        std::ranges::for_each(change.operations, [&](const Op& operation) {
            std::ranges::for_each(operation.pred, [&](const OpId& predecessor) {
                const auto row = row_by_id.find(predecessor);
                if (row != row_by_id.end()) {
                    rows[row->second].successors.push_back(operation.id);
                }
            });
        });
    });
    std::ranges::for_each(rows, [](DocumentWriteRow& row) { std::ranges::sort(row.successors); });

    const auto ranks = document_list_ranks(changes);
    std::ranges::sort(rows, [&](const DocumentWriteRow& left, const DocumentWriteRow& right) {
        const auto& left_op = left.operation;
        const auto& right_op = right.operation;
        if (left_op.obj != right_op.obj) {
            return left_op.obj < right_op.obj;
        }
        const auto* left_key = std::get_if<std::string>(&left_op.key);
        const auto* right_key = std::get_if<std::string>(&right_op.key);
        if (left_key && right_key) {
            return *left_key != *right_key ? *left_key < *right_key : left_op.id < right_op.id;
        }
        const auto left_register = document_register(left_op);
        const auto right_register = document_register(right_op);
        const auto left_rank = left_register ? ranks.at(*left_register) : ranks.size();
        const auto right_rank = right_register ? ranks.at(*right_register) : ranks.size();
        return left_rank != right_rank ? left_rank < right_rank : left_op.id < right_op.id;
    });
    return rows;
}

inline auto encode_document_operation_columns(const std::vector<DocumentWriteRow>& rows,
                                              const std::vector<ActorId>& actors)
    -> std::vector<RawColumn> {
    auto id_actor = encoding::RleEncoder<std::uint64_t>{};
    auto id_counter = encoding::DeltaEncoder{};
    auto object_actor = encoding::RleEncoder<std::uint64_t>{};
    auto object_counter = encoding::RleEncoder<std::uint64_t>{};
    auto key_actor = encoding::RleEncoder<std::uint64_t>{};
    auto key_counter = encoding::DeltaEncoder{};
    auto key_string = encoding::RleEncoder<std::string>{};
    auto insert = encoding::BooleanEncoder{};
    auto action = encoding::RleEncoder<std::uint64_t>{};
    auto value_metadata_bytes = std::vector<std::byte>{};
    auto value_raw = std::vector<std::byte>{};
    auto successor_count = encoding::RleEncoder<std::uint64_t>{};
    auto successor_actor = encoding::RleEncoder<std::uint64_t>{};
    auto successor_counter = encoding::DeltaEncoder{};
    auto expand = encoding::BooleanEncoder{};
    auto mark_name = encoding::RleEncoder<std::string>{};
    auto has_non_root_object = false;
    auto has_map_key = false;
    auto has_list_key = false;
    auto has_expand = false;
    auto has_mark_name = false;

    std::ranges::for_each(rows, [&](const DocumentWriteRow& row) {
        const auto& operation = row.operation;
        id_actor.append(document_actor_index(actors, operation.id.actor));
        id_counter.append(static_cast<std::int64_t>(operation.id.counter));
        if (operation.obj.is_root()) {
            object_actor.append_null();
            object_counter.append_null();
        } else {
            has_non_root_object = true;
            const auto object = std::get<OpId>(operation.obj.inner);
            object_actor.append(document_actor_index(actors, object.actor));
            object_counter.append(object.counter);
        }

        const auto is_mark = operation.action == OpType::mark;
        const auto is_insert = operation.action == OpType::insert ||
                               operation.action == OpType::splice_text || is_mark;
        if (const auto* map_key = std::get_if<std::string>(&operation.key); map_key && !is_mark) {
            has_map_key = true;
            key_actor.append_null();
            key_counter.append_null();
            key_string.append(*map_key);
        } else {
            has_list_key = true;
            const auto register_id =
                is_insert ? operation.insert_after : document_register(operation);
            if (register_id) {
                key_actor.append(document_actor_index(actors, register_id->actor));
                key_counter.append(static_cast<std::int64_t>(register_id->counter));
            } else {
                key_actor.append_null();
                if (is_insert) {
                    key_counter.append(0);
                } else {
                    key_counter.append_null();
                }
            }
            key_string.append_null();
        }

        insert.append(is_insert);
        action.append(op_to_action_code(operation));
        const auto is_object = (operation.action == OpType::make_object || is_insert) &&
                               std::holds_alternative<ObjType>(operation.value);
        if (is_object) {
            encode_value(Value{ScalarValue{Null{}}}, value_metadata_bytes, value_raw);
        } else if (operation.action == OpType::increment) {
            const auto* scalar = std::get_if<ScalarValue>(&operation.value);
            const auto* counter = scalar ? std::get_if<Counter>(scalar) : nullptr;
            encode_value(
                Value{ScalarValue{counter ? counter->value : 0}}, value_metadata_bytes, value_raw);
        } else {
            encode_value(operation.value, value_metadata_bytes, value_raw);
        }

        successor_count.append(static_cast<std::uint64_t>(row.successors.size()));
        std::ranges::for_each(row.successors, [&](const OpId& successor) {
            successor_actor.append(document_actor_index(actors, successor.actor));
            successor_counter.append(static_cast<std::int64_t>(successor.counter));
        });
        if (is_mark) {
            expand.append(operation.expand);
            has_expand = true;
            if (operation.mark_name) {
                mark_name.append(*operation.mark_name);
                has_mark_name = true;
            } else {
                mark_name.append_null();
            }
        } else {
            expand.append(false);
            mark_name.append_null();
        }
    });

    id_actor.finish();
    id_counter.finish();
    object_actor.finish();
    object_counter.finish();
    key_actor.finish();
    key_counter.finish();
    key_string.finish();
    insert.finish();
    action.finish();
    successor_count.finish();
    successor_actor.finish();
    successor_counter.finish();
    expand.finish();
    mark_name.finish();

    auto value_metadata = encoding::RleEncoder<std::uint64_t>{};
    auto metadata_position = std::size_t{0};
    auto metadata_valid = true;
    const auto metadata_steps =
        std::views::iota(std::size_t{0}, value_metadata_bytes.size()) |
        std::views::take_while([&](const auto) {
            return metadata_valid && metadata_position < value_metadata_bytes.size();
        });
    std::ranges::for_each(metadata_steps, [&](const auto) {
        const auto metadata = encoding::decode_uleb128(
            std::span<const std::byte>{value_metadata_bytes}.subspan(metadata_position));
        metadata_valid = metadata.has_value();
        if (metadata_valid) {
            metadata_position += metadata->bytes_read;
            value_metadata.append(metadata->value);
        }
    });
    value_metadata.finish();

    auto columns = std::vector<RawColumn>{};
    const auto add = [&](ColumnSpec spec, std::vector<std::byte> data) {
        if (!data.empty()) {
            columns.push_back(RawColumn{.spec = spec, .data = std::move(data)});
        }
    };
    if (has_non_root_object) {
        add(document_op_columns::obj_actor, object_actor.take());
        add(document_op_columns::obj_counter, object_counter.take());
    }
    if (has_list_key) {
        add(document_op_columns::key_actor, key_actor.take());
        add(document_op_columns::key_counter, key_counter.take());
    }
    if (has_map_key) {
        add(document_op_columns::key_string, key_string.take());
    }
    add(document_op_columns::id_actor, id_actor.take());
    add(document_op_columns::id_counter, id_counter.take());
    add(document_op_columns::insert, insert.take());
    add(document_op_columns::action, action.take());
    add(document_op_columns::value_metadata, value_metadata.take());
    add(document_op_columns::value_raw, std::move(value_raw));
    add(document_op_columns::successor_count, successor_count.take());
    add(document_op_columns::successor_actor, successor_actor.take());
    add(document_op_columns::successor_counter, successor_counter.take());
    if (has_expand) {
        add(document_op_columns::expand, expand.take());
    }
    if (has_mark_name) {
        add(document_op_columns::mark_name, mark_name.take());
    }
    std::ranges::sort(columns, {}, [](const RawColumn& column) { return column.spec.to_u32(); });
    return columns;
}

inline auto compress_document_columns(std::vector<RawColumn> columns) -> std::vector<RawColumn> {
    std::ranges::for_each(columns, [](RawColumn& column) {
        if (column.data.size() < deflate_threshold) {
            return;
        }
        const auto compressed = deflate_compress(column.data);
        if (compressed) {
            column.spec.deflate = true;
            column.data = *compressed;
        }
    });
    return columns;
}

inline void write_document_column_layout(const std::vector<RawColumn>& columns,
                                         std::vector<std::byte>& output) {
    encoding::encode_uleb128(columns.size(), output);
    std::ranges::for_each(columns, [&](const RawColumn& column) {
        encoding::encode_uleb128(column.spec.to_u32(), output);
        encoding::encode_uleb128(column.data.size(), output);
    });
}

inline void write_document_column_data(const std::vector<RawColumn>& columns,
                                       std::vector<std::byte>& output) {
    std::ranges::for_each(columns, [&](const RawColumn& column) {
        output.insert(output.end(), column.data.begin(), column.data.end());
    });
}

inline auto serialize_document_chunk(const std::vector<Change>& changes,
                                     std::vector<ChangeHash> heads) -> std::vector<std::byte> {
    const auto actors = document_actor_table(changes);
    auto change_columns =
        compress_document_columns(encode_document_change_columns(changes, actors));
    auto operation_columns = compress_document_columns(
        encode_document_operation_columns(collect_document_rows(changes), actors));
    std::ranges::sort(heads);

    auto body = std::vector<std::byte>{};
    encoding::encode_uleb128(actors.size(), body);
    std::ranges::for_each(actors, [&](const ActorId& actor) {
        encoding::encode_uleb128(actor.bytes.size(), body);
        body.insert(body.end(), actor.bytes.begin(), actor.bytes.end());
    });
    encoding::encode_uleb128(heads.size(), body);
    std::ranges::for_each(heads, [&](const ChangeHash& head) {
        body.insert(body.end(), head.bytes.begin(), head.bytes.end());
    });
    write_document_column_layout(change_columns, body);
    write_document_column_layout(operation_columns, body);
    write_document_column_data(change_columns, body);
    write_document_column_data(operation_columns, body);

    auto change_indexes = std::unordered_map<ChangeHash, std::size_t>{};
    std::ranges::for_each(changes, [&](const Change& change) {
        change_indexes.emplace(document_change_hash(change), change_indexes.size());
    });
    std::ranges::for_each(heads, [&](const ChangeHash& head) {
        encoding::encode_uleb128(change_indexes.at(head), body);
    });

    auto output = std::vector<std::byte>{};
    write_chunk(ChunkType::document, body, output);
    return output;
}

} // namespace automerge_cpp::storage
