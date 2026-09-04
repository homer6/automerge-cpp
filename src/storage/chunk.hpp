#pragma once

// Chunk envelope for the Automerge binary format.
//
// A chunk is the top-level container in the binary format:
//   magic (4 bytes: 0x85 0x6f 0x4a 0x83)
//   checksum (4 bytes: first 4 bytes of SHA-256 of type + length + body)
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
    std::byte{0x85}, std::byte{0x6F}, std::byte{0x4A}, std::byte{0x83}};

// Chunk types in the binary format.
enum class ChunkType : std::uint8_t {
    document = 0x00,
    change = 0x01,
    compressed = 0x02,
};

// The header of a parsed chunk.
struct ChunkHeader {
    ChunkType type;
    std::array<std::byte, 4> checksum;
    std::size_t body_offset; // offset into the original data where body starts
    std::size_t body_length;
};

// Compute the upstream chunk hash: SHA-256 of the chunk type, ULEB128 body
// length, and body. The first four bytes are also the chunk checksum.
inline auto compute_change_hash_from_chunk(ChunkType type, std::span<const std::byte> body)
    -> std::array<std::byte, 32> {

    auto hash_input = std::vector<std::byte>{};
    hash_input.reserve(1 + 10 + body.size());
    hash_input.push_back(static_cast<std::byte>(type));
    encoding::encode_uleb128(body.size(), hash_input);
    hash_input.insert(hash_input.end(), body.begin(), body.end());

    return crypto::sha256(hash_input);
}

// Compute the SHA-256 based checksum for a chunk body (first four hash bytes).
inline auto compute_chunk_checksum(ChunkType type, std::span<const std::byte> body)
    -> std::array<std::byte, 4> {
    const auto full_hash = compute_change_hash_from_chunk(type, body);
    auto result = std::array<std::byte, 4>{};
    std::memcpy(result.data(), full_hash.data(), result.size());
    return result;
}

// Parse a chunk header from the beginning of data.
// Returns nullopt if the data is malformed.
inline auto parse_chunk_header(std::span<const std::byte> data) -> std::optional<ChunkHeader> {

    if (data.size() < 4)
        return std::nullopt;

    // Verify magic bytes
    if (std::memcmp(data.data(), chunk_magic.data(), 4) != 0) {
        return std::nullopt;
    }

    auto pos = std::size_t{4};

    // Checksum (4 bytes)
    if (pos + 4 > data.size())
        return std::nullopt;
    auto checksum = std::array<std::byte, 4>{};
    std::memcpy(checksum.data(), &data[pos], 4);
    pos += 4;

    // Chunk type (1 byte)
    if (pos >= data.size())
        return std::nullopt;
    auto type = static_cast<ChunkType>(data[pos]);
    ++pos;

    // Body length (ULEB128)
    auto len_result = encoding::decode_uleb128(data.subspan(pos));
    if (!len_result)
        return std::nullopt;
    pos += len_result->bytes_read;

    return ChunkHeader{
        .type = type,
        .checksum = checksum,
        .body_offset = pos,
        .body_length = static_cast<std::size_t>(len_result->value),
    };
}

// Validate the checksum of a chunk body against the header checksum.
inline auto validate_chunk_checksum(const ChunkHeader& header, std::span<const std::byte> data)
    -> bool {
    if (header.body_offset + header.body_length > data.size())
        return false;
    auto body = data.subspan(header.body_offset, header.body_length);
    auto expected = compute_chunk_checksum(header.type, body);
    return std::memcmp(header.checksum.data(), expected.data(), 4) == 0;
}

// Write a complete chunk to output: magic + checksum + type + LEB128(length) +
// body.
inline void write_chunk(ChunkType type,
                        std::span<const std::byte> body,
                        std::span<const std::byte, 32> hash,
                        std::vector<std::byte>& output) {
    // Magic
    output.insert(output.end(), chunk_magic.begin(), chunk_magic.end());

    // Checksum (first four bytes of SHA-256 of type + length + body)
    output.insert(output.end(), hash.begin(), hash.begin() + 4);

    // Chunk type
    output.push_back(static_cast<std::byte>(type));

    // Body length (ULEB128)
    encoding::encode_uleb128(body.size(), output);

    // Body
    output.insert(output.end(), body.begin(), body.end());
}

inline void
write_chunk(ChunkType type, std::span<const std::byte> body, std::vector<std::byte>& output) {
    const auto hash = compute_change_hash_from_chunk(type, body);
    write_chunk(type, body, hash, output);
}

} // namespace automerge_cpp::storage
