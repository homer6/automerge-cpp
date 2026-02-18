#pragma once

// Change chunk serialization/deserialization for the Automerge binary format.
//
// A change chunk body contains:
//   1. Change metadata (actor index, seq, start_op, timestamp, message, deps)
//   2. Op columns (columnar-encoded operations)
//
// Internal header — not installed.

#include <automerge-cpp/change.hpp>
#include <automerge-cpp/types.hpp>

#include "chunk.hpp"
#include "columns/change_op_columns.hpp"
#include "columns/column_spec.hpp"
#include "columns/compression.hpp"
#include "columns/raw_column.hpp"
#include "../encoding/leb128.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace automerge_cpp::storage {

// Serialize a change into its chunk body bytes (not including the chunk envelope).
inline auto serialize_change_body(const Change& change,
                                    const std::vector<ActorId>& actor_table)
    -> std::vector<std::byte> {

    auto body = std::vector<std::byte>{};

    // Find actor index
    auto actor_idx = std::uint64_t{0};
    for (std::size_t i = 0; i < actor_table.size(); ++i) {
        if (actor_table[i] == change.actor) {
            actor_idx = static_cast<std::uint64_t>(i);
            break;
        }
    }

    // Actor index
    encoding::encode_uleb128(actor_idx, body);

    // Seq
    encoding::encode_uleb128(change.seq, body);

    // Start op
    encoding::encode_uleb128(change.start_op, body);

    // Timestamp
    encoding::encode_sleb128(change.timestamp, body);

    // Message (length-prefixed, 0 = no message)
    if (change.message) {
        encoding::encode_uleb128(change.message->size(), body);
        body.insert(body.end(),
            reinterpret_cast<const std::byte*>(change.message->data()),
            reinterpret_cast<const std::byte*>(change.message->data() + change.message->size()));
    } else {
        encoding::encode_uleb128(0, body);
    }

    // Deps count + hashes
    encoding::encode_uleb128(change.deps.size(), body);
    for (const auto& dep : change.deps) {
        body.insert(body.end(), dep.bytes.begin(), dep.bytes.end());
    }

    // Number of ops
    encoding::encode_uleb128(change.operations.size(), body);

    // Op columns
    auto columns = encode_change_ops(change.operations, actor_table);

    // Optionally compress columns > threshold
    for (auto& col : columns) {
        if (col.data.size() > deflate_threshold) {
            auto compressed = deflate_compress(col.data);
            if (compressed && compressed->size() < col.data.size()) {
                // Store uncompressed length first, then compressed data
                auto compressed_col = std::vector<std::byte>{};
                encoding::encode_uleb128(col.data.size(), compressed_col);
                compressed_col.insert(compressed_col.end(),
                    compressed->begin(), compressed->end());
                col.data = std::move(compressed_col);
                col.spec.deflate = true;
            }
        }
    }

    write_raw_columns(columns, body);

    return body;
}

// Parse a change from its chunk body bytes.
inline auto parse_change_chunk(std::span<const std::byte> body,
                                 const std::vector<ActorId>& actor_table)
    -> std::optional<Change> {

    auto pos = std::size_t{0};

    auto read_uleb = [&]() -> std::optional<std::uint64_t> {
        if (pos >= body.size()) return std::nullopt;
        auto r = encoding::decode_uleb128(body.subspan(pos));
        if (!r) return std::nullopt;
        pos += r->bytes_read;
        return r->value;
    };

    auto read_sleb = [&]() -> std::optional<std::int64_t> {
        if (pos >= body.size()) return std::nullopt;
        auto r = encoding::decode_sleb128(body.subspan(pos));
        if (!r) return std::nullopt;
        pos += r->bytes_read;
        return r->value;
    };

    // Actor index
    auto actor_idx = read_uleb();
    if (!actor_idx || *actor_idx >= actor_table.size()) return std::nullopt;

    // Seq
    auto seq = read_uleb();
    if (!seq) return std::nullopt;

    // Start op
    auto start_op = read_uleb();
    if (!start_op) return std::nullopt;

    // Timestamp
    auto timestamp = read_sleb();
    if (!timestamp) return std::nullopt;

    // Message
    auto msg_len = read_uleb();
    if (!msg_len) return std::nullopt;
    auto message = std::optional<std::string>{};
    if (*msg_len > 0) {
        if (pos + *msg_len > body.size()) return std::nullopt;
        message = std::string{reinterpret_cast<const char*>(&body[pos]),
                              static_cast<std::size_t>(*msg_len)};
        pos += static_cast<std::size_t>(*msg_len);
    }

    // Deps
    auto num_deps = read_uleb();
    if (!num_deps) return std::nullopt;
    auto deps = std::vector<ChangeHash>{};
    deps.reserve(static_cast<std::size_t>(*num_deps));
    for (std::uint64_t i = 0; i < *num_deps; ++i) {
        if (pos + 32 > body.size()) return std::nullopt;
        auto hash = ChangeHash{};
        std::memcpy(hash.bytes.data(), &body[pos], 32);
        pos += 32;
        deps.push_back(hash);
    }

    // Number of ops
    auto num_ops = read_uleb();
    if (!num_ops) return std::nullopt;

    // Parse columns
    auto columns = parse_raw_columns(body, pos);

    // Decompress any deflated columns
    for (auto& col : columns) {
        if (col.spec.deflate && !col.data.empty()) {
            auto col_pos = std::size_t{0};
            auto uncomp_len_r = encoding::decode_uleb128(
                std::span<const std::byte>{col.data}.subspan(col_pos));
            if (!uncomp_len_r) return std::nullopt;
            col_pos += uncomp_len_r->bytes_read;

            auto compressed = std::vector<std::byte>(
                col.data.begin() + static_cast<std::ptrdiff_t>(col_pos),
                col.data.end());
            auto decompressed = deflate_decompress(compressed);
            if (!decompressed) return std::nullopt;
            col.data = std::move(*decompressed);
            col.spec.deflate = false;
        }
    }

    // Decode ops
    auto operations = decode_change_ops(columns, actor_table,
        actor_table[static_cast<std::size_t>(*actor_idx)],
        *start_op, static_cast<std::size_t>(*num_ops));
    if (!operations) return std::nullopt;

    return Change{
        .actor = actor_table[static_cast<std::size_t>(*actor_idx)],
        .seq = *seq,
        .start_op = *start_op,
        .timestamp = *timestamp,
        .message = std::move(message),
        .deps = std::move(deps),
        .operations = std::move(*operations),
    };
}

}  // namespace automerge_cpp::storage
