#pragma once

// Change chunk serialization/deserialization for the Automerge binary format.
//
// A change chunk body contains:
//   1. Change metadata (deps, actor, seq, start_op, timestamp, message, actors)
//   2. Op columns (columnar-encoded operations)
//
// Internal header — not installed.

#include <automerge-cpp/change.hpp>
#include <automerge-cpp/types.hpp>

#include "../encoding/leb128.hpp"
#include "chunk.hpp"
#include "columns/change_op_columns.hpp"
#include "columns/column_spec.hpp"
#include "columns/compression.hpp"
#include "columns/raw_column.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace automerge_cpp::storage {

inline auto change_actor_table(const Change& change) -> std::vector<ActorId> {
    auto other_actors = std::set<ActorId>{};
    const auto add_actor = [&](const ActorId& actor) {
        if (actor != change.actor) {
            other_actors.insert(actor);
        }
    };

    for (const auto& operation : change.operations) {
        add_actor(operation.id.actor);
        if (!operation.obj.is_root()) {
            add_actor(std::get<OpId>(operation.obj.inner).actor);
        }
        if (operation.insert_after) {
            add_actor(operation.insert_after->actor);
        }
        for (const auto& predecessor : operation.pred) {
            add_actor(predecessor.actor);
        }
    }

    auto actors = std::vector<ActorId>{change.actor};
    actors.insert(actors.end(), other_actors.begin(), other_actors.end());
    return actors;
}

// Serialize a change into its upstream-compatible chunk body bytes.
inline auto serialize_change_body(const Change& change) -> std::vector<std::byte> {

    auto body = std::vector<std::byte>{};
    body.reserve(64 + change.operations.size() * 32);

    // Deps count + hashes
    encoding::encode_uleb128(change.deps.size(), body);
    for (const auto& dep : change.deps) {
        body.insert(body.end(), dep.bytes.begin(), dep.bytes.end());
    }

    const auto actor_table = change_actor_table(change);
    encoding::encode_uleb128(change.actor.bytes.size(), body);
    body.insert(body.end(), change.actor.bytes.begin(), change.actor.bytes.end());

    encoding::encode_uleb128(change.seq, body);
    encoding::encode_uleb128(change.start_op, body);
    encoding::encode_sleb128(change.timestamp, body);

    if (change.message) {
        encoding::encode_uleb128(change.message->size(), body);
        body.insert(
            body.end(),
            reinterpret_cast<const std::byte*>(change.message->data()),
            reinterpret_cast<const std::byte*>(change.message->data() + change.message->size()));
    } else {
        encoding::encode_uleb128(0, body);
    }

    encoding::encode_uleb128(actor_table.size() - 1, body);
    for (const auto& actor : std::span{actor_table}.subspan(1)) {
        encoding::encode_uleb128(actor.bytes.size(), body);
        body.insert(body.end(), actor.bytes.begin(), actor.bytes.end());
    }

    // Op columns
    auto columns = encode_change_ops(change.operations, actor_table);
    write_raw_columns(columns, body);

    return body;
}

inline auto serialize_change_chunk(const Change& change) -> std::vector<std::byte> {
    const auto body = serialize_change_body(change);
    auto chunk = std::vector<std::byte>{};
    write_chunk(ChunkType::change, body, chunk);
    return chunk;
}

// Parse a change from an upstream-compatible chunk body.
inline auto parse_change_body(std::span<const std::byte> body) -> std::optional<Change> {

    auto pos = std::size_t{0};

    auto read_uleb = [&]() -> std::optional<std::uint64_t> {
        if (pos >= body.size())
            return std::nullopt;
        auto r = encoding::decode_uleb128(body.subspan(pos));
        if (!r)
            return std::nullopt;
        pos += r->bytes_read;
        return r->value;
    };

    auto read_sleb = [&]() -> std::optional<std::int64_t> {
        if (pos >= body.size())
            return std::nullopt;
        auto r = encoding::decode_sleb128(body.subspan(pos));
        if (!r)
            return std::nullopt;
        pos += r->bytes_read;
        return r->value;
    };

    // Deps
    auto num_deps = read_uleb();
    if (!num_deps)
        return std::nullopt;
    auto deps = std::vector<ChangeHash>{};
    deps.reserve(static_cast<std::size_t>(*num_deps));
    for (std::uint64_t i = 0; i < *num_deps; ++i) {
        if (pos + 32 > body.size())
            return std::nullopt;
        auto hash = ChangeHash{};
        std::memcpy(hash.bytes.data(), &body[pos], 32);
        pos += 32;
        deps.push_back(hash);
    }

    const auto read_actor = [&]() -> std::optional<ActorId> {
        const auto actor_length = read_uleb();
        if (!actor_length || *actor_length != ActorId::size)
            return std::nullopt;
        if (pos + *actor_length > body.size())
            return std::nullopt;
        auto actor = ActorId{};
        std::memcpy(actor.bytes.data(), &body[pos], actor.bytes.size());
        pos += actor.bytes.size();
        return actor;
    };

    const auto actor = read_actor();
    if (!actor)
        return std::nullopt;

    const auto seq = read_uleb();
    if (!seq)
        return std::nullopt;

    const auto start_op = read_uleb();
    if (!start_op || *start_op == 0)
        return std::nullopt;

    const auto timestamp = read_sleb();
    if (!timestamp)
        return std::nullopt;

    const auto message_length = read_uleb();
    if (!message_length || pos + *message_length > body.size())
        return std::nullopt;
    auto message = std::optional<std::string>{};
    if (*message_length > 0) {
        message = std::string{reinterpret_cast<const char*>(&body[pos]),
                              static_cast<std::size_t>(*message_length)};
    }
    pos += static_cast<std::size_t>(*message_length);

    const auto other_actor_count = read_uleb();
    if (!other_actor_count)
        return std::nullopt;
    auto actor_table = std::vector<ActorId>{*actor};
    actor_table.reserve(static_cast<std::size_t>(*other_actor_count) + 1);
    for (std::uint64_t i = 0; i < *other_actor_count; ++i) {
        const auto other_actor = read_actor();
        if (!other_actor)
            return std::nullopt;
        actor_table.push_back(*other_actor);
    }

    // Parse columns
    auto columns = parse_raw_columns(body, pos);

    // Decompress any deflated columns
    for (auto& col : columns) {
        if (col.spec.deflate && !col.data.empty()) {
            auto col_pos = std::size_t{0};
            auto uncomp_len_r =
                encoding::decode_uleb128(std::span<const std::byte>{col.data}.subspan(col_pos));
            if (!uncomp_len_r)
                return std::nullopt;
            col_pos += uncomp_len_r->bytes_read;

            auto compressed = std::vector<std::byte>(
                col.data.begin() + static_cast<std::ptrdiff_t>(col_pos), col.data.end());
            auto decompressed = deflate_decompress(compressed);
            if (!decompressed)
                return std::nullopt;
            col.data = std::move(*decompressed);
            col.spec.deflate = false;
        }
    }

    // Decode ops
    auto action_column = std::span<const std::byte>{};
    for (const auto& column : columns) {
        if (column.spec == change_op_columns::action) {
            action_column = column.data;
            break;
        }
    }

    auto action_decoder = encoding::RleDecoder<std::uint64_t>{action_column};
    auto operation_count = std::size_t{0};
    while (action_decoder.next()) {
        ++operation_count;
    }
    if (!action_decoder.done())
        return std::nullopt;

    auto operations = decode_change_ops(columns, actor_table, *actor, *start_op, operation_count);
    if (!operations)
        return std::nullopt;

    return Change{
        .actor = *actor,
        .seq = *seq,
        .start_op = *start_op,
        .timestamp = *timestamp,
        .message = std::move(message),
        .deps = std::move(deps),
        .operations = std::move(*operations),
    };
}

struct ParsedChangeChunk {
    Change change;
    ChangeHash hash;
};

// Parse and validate a complete upstream-compatible change chunk.
inline auto parse_change_chunk(std::span<const std::byte> chunk)
    -> std::optional<ParsedChangeChunk> {
    const auto header = parse_chunk_header(chunk);
    if (!header || header->type != ChunkType::change)
        return std::nullopt;
    if (!validate_chunk_checksum(*header, chunk))
        return std::nullopt;
    if (header->body_offset + header->body_length != chunk.size())
        return std::nullopt;

    const auto body = chunk.subspan(header->body_offset, header->body_length);
    const auto change = parse_change_body(body);
    if (!change)
        return std::nullopt;

    const auto digest = compute_change_hash_from_chunk(ChunkType::change, body);
    auto hash = ChangeHash{};
    std::memcpy(hash.bytes.data(), digest.data(), hash.bytes.size());
    return ParsedChangeChunk{.change = std::move(*change), .hash = hash};
}

} // namespace automerge_cpp::storage
