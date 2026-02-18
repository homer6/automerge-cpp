#pragma once

// Chunk envelope for the Automerge binary format.
//
// A chunk is the top-level container in the binary format:
//   magic (4 bytes: 0x85 0x6f 0x4a 0x83)
//   checksum (4 bytes: first 4 bytes of SHA-256 of body)
//   chunk_type (1 byte)
//   body_length (ULEB128)
//   body (body_length bytes)
//
// Internal header — not installed.

#include "../crypto/sha256.hpp"
#include "../encoding/leb128.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace automerge_cpp::storage {

// Magic bytes at the start of every Automerge binary document.
inline constexpr std::array<std::byte, 4> chunk_magic = {
    std::byte{0x85}, std::byte{0x6F}, std::byte{0x4A}, std::byte{0x83}
};

// Chunk types in the binary format.
enum class ChunkType : std::uint8_t {
    document   = 0x00,
    change     = 0x01,
    compressed = 0x02,
};

// The header of a parsed chunk.
struct ChunkHeader {
    ChunkType type;
    std::array<std::byte, 4> checksum;
    std::size_t body_offset;  // offset into the original data where body starts
    std::size_t body_length;
};

// Compute the SHA-256 based checksum for a chunk body (first 4 bytes of SHA-256).
inline auto compute_chunk_checksum(std::span<const std::byte> body)
    -> std::array<std::byte, 4> {
    auto full_hash = crypto::sha256(body);
    auto result = std::array<std::byte, 4>{};
    std::memcpy(result.data(), full_hash.data(), 4);
    return result;
}

// Compute the change hash: SHA-256 of (deps_hashes + chunk_type_byte + body).
// This matches the upstream Rust implementation.
inline auto compute_change_hash_from_chunk(
    const std::vector<std::array<std::byte, 32>>& dep_hashes,
    std::span<const std::byte> body) -> std::array<std::byte, 32> {

    auto hash_input = std::vector<std::byte>{};
    // Prepend all dependency hashes
    for (const auto& dep : dep_hashes) {
        hash_input.insert(hash_input.end(), dep.begin(), dep.end());
    }
    // Append chunk type byte (change = 0x01)
    hash_input.push_back(static_cast<std::byte>(ChunkType::change));
    // Append body
    hash_input.insert(hash_input.end(), body.begin(), body.end());

    return crypto::sha256(hash_input);
}

// Parse a chunk header from the beginning of data.
// Returns nullopt if the data is malformed.
inline auto parse_chunk_header(std::span<const std::byte> data)
    -> std::optional<ChunkHeader> {

    if (data.size() < 4) return std::nullopt;

    // Verify magic bytes
    if (std::memcmp(data.data(), chunk_magic.data(), 4) != 0) {
        return std::nullopt;
    }

    auto pos = std::size_t{4};

    // Checksum (4 bytes)
    if (pos + 4 > data.size()) return std::nullopt;
    auto checksum = std::array<std::byte, 4>{};
    std::memcpy(checksum.data(), &data[pos], 4);
    pos += 4;

    // Chunk type (1 byte)
    if (pos >= data.size()) return std::nullopt;
    auto type = static_cast<ChunkType>(data[pos]);
    ++pos;

    // Body length (ULEB128)
    auto len_result = encoding::decode_uleb128(data.subspan(pos));
    if (!len_result) return std::nullopt;
    pos += len_result->bytes_read;

    return ChunkHeader{
        .type = type,
        .checksum = checksum,
        .body_offset = pos,
        .body_length = static_cast<std::size_t>(len_result->value),
    };
}

// Validate the checksum of a chunk body against the header checksum.
inline auto validate_chunk_checksum(const ChunkHeader& header,
                                     std::span<const std::byte> data) -> bool {
    if (header.body_offset + header.body_length > data.size()) return false;
    auto body = data.subspan(header.body_offset, header.body_length);
    auto expected = compute_chunk_checksum(body);
    return std::memcmp(header.checksum.data(), expected.data(), 4) == 0;
}

// Write a complete chunk to output: magic + checksum + type + LEB128(length) + body.
inline void write_chunk(ChunkType type, std::span<const std::byte> body,
                         std::vector<std::byte>& output) {
    // Magic
    output.insert(output.end(), chunk_magic.begin(), chunk_magic.end());

    // Checksum (first 4 bytes of SHA-256 of body)
    auto checksum = compute_chunk_checksum(body);
    output.insert(output.end(), checksum.begin(), checksum.end());

    // Chunk type
    output.push_back(static_cast<std::byte>(type));

    // Body length (ULEB128)
    encoding::encode_uleb128(body.size(), output);

    // Body
    output.insert(output.end(), body.begin(), body.end());
}

}  // namespace automerge_cpp::storage
