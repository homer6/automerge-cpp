#pragma once

// LEB128 (Little Endian Base 128) variable-length integer encoding.
// Used throughout the Automerge binary format for compact integer storage.
// Internal header — not installed.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace automerge_cpp::encoding {

// -- Unsigned LEB128 ----------------------------------------------------------

// Encode a uint64 as unsigned LEB128, appending bytes to output.
inline void encode_uleb128(std::uint64_t value, std::vector<std::byte>& output) {
    do {
        auto byte = static_cast<std::byte>(value & 0x7F);
        value >>= 7;
        if (value != 0) {
            byte |= std::byte{0x80};  // more bytes follow
        }
        output.push_back(byte);
    } while (value != 0);
}

// Encode a uint64 as unsigned LEB128, returning the bytes.
inline auto encode_uleb128(std::uint64_t value) -> std::vector<std::byte> {
    auto result = std::vector<std::byte>{};
    encode_uleb128(value, result);
    return result;
}

// Result of a decode operation: decoded value + number of bytes consumed.
struct DecodeResult {
    std::uint64_t value;
    std::size_t bytes_read;
};

// Decode an unsigned LEB128 value from a byte span.
// Returns nullopt if the input is truncated (no terminating byte found).
inline auto decode_uleb128(std::span<const std::byte> input) -> std::optional<DecodeResult> {
    auto value = std::uint64_t{0};
    auto shift = 0u;

    for (std::size_t i = 0; i < input.size(); ++i) {
        if (shift >= 64) return std::nullopt;  // overflow

        auto byte = input[i];
        value |= (static_cast<std::uint64_t>(byte) & 0x7F) << shift;
        shift += 7;

        if ((byte & std::byte{0x80}) == std::byte{0}) {
            return DecodeResult{.value = value, .bytes_read = i + 1};
        }
    }

    return std::nullopt;  // truncated input
}

// -- Signed LEB128 ------------------------------------------------------------

// Encode an int64 as signed LEB128, appending bytes to output.
inline void encode_sleb128(std::int64_t value, std::vector<std::byte>& output) {
    auto more = true;
    while (more) {
        auto byte = static_cast<std::byte>(value & 0x7F);
        value >>= 7;  // arithmetic shift preserves sign

        // If the sign bit of the byte matches the remaining value, we're done
        const bool sign_bit = (byte & std::byte{0x40}) != std::byte{0};
        if ((value == 0 && !sign_bit) || (value == -1 && sign_bit)) {
            more = false;
        } else {
            byte |= std::byte{0x80};  // more bytes follow
        }
        output.push_back(byte);
    }
}

// Encode an int64 as signed LEB128, returning the bytes.
inline auto encode_sleb128(std::int64_t value) -> std::vector<std::byte> {
    auto result = std::vector<std::byte>{};
    encode_sleb128(value, result);
    return result;
}

// Result of a signed decode operation.
struct SignedDecodeResult {
    std::int64_t value;
    std::size_t bytes_read;
};

// Decode a signed LEB128 value from a byte span.
// Returns nullopt if the input is truncated.
inline auto decode_sleb128(std::span<const std::byte> input) -> std::optional<SignedDecodeResult> {
    auto value = std::int64_t{0};
    auto shift = 0u;

    for (std::size_t i = 0; i < input.size(); ++i) {
        if (shift >= 64) return std::nullopt;  // overflow

        auto byte = input[i];
        value |= (static_cast<std::int64_t>(byte) & 0x7F) << shift;
        shift += 7;

        if ((byte & std::byte{0x80}) == std::byte{0}) {
            // Sign extend if the sign bit is set
            if (shift < 64 && (byte & std::byte{0x40}) != std::byte{0}) {
                value |= -(std::int64_t{1} << shift);
            }
            return SignedDecodeResult{.value = value, .bytes_read = i + 1};
        }
    }

    return std::nullopt;  // truncated input
}

// -- Delta encoding helpers ---------------------------------------------------

// Encode a sequence of uint64 values using delta encoding + unsigned LEB128.
// Each value is stored as the difference from the previous value.
inline void encode_delta(std::span<const std::uint64_t> values, std::vector<std::byte>& output) {
    auto prev = std::uint64_t{0};
    for (auto val : values) {
        encode_uleb128(val - prev, output);
        prev = val;
    }
}

// Encode a sequence of boolean values using run-length encoding.
// Format: count of first value (always starting with false count, possibly 0),
// then alternating counts.
inline void encode_rle_bool(std::span<const bool> values, std::vector<std::byte>& output) {
    if (values.empty()) return;

    auto current = false;  // always start counting false
    auto count = std::uint64_t{0};

    for (auto val : values) {
        if (val == current) {
            ++count;
        } else {
            encode_uleb128(count, output);
            count = 1;
            current = val;
        }
    }
    encode_uleb128(count, output);
}

}  // namespace automerge_cpp::encoding
