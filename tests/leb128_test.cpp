#include "../src/encoding/leb128.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

using namespace automerge_cpp::encoding;

// -- Unsigned LEB128 ----------------------------------------------------------

TEST(Leb128, encode_uleb128_zero) {
    auto bytes = encode_uleb128(0);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], std::byte{0x00});
}

TEST(Leb128, encode_uleb128_single_byte) {
    auto bytes = encode_uleb128(42);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], std::byte{42});
}

TEST(Leb128, encode_uleb128_max_single_byte) {
    auto bytes = encode_uleb128(127);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], std::byte{0x7F});
}

TEST(Leb128, encode_uleb128_two_bytes) {
    // 128 = 0x80 → LEB128: [0x80, 0x01]
    auto bytes = encode_uleb128(128);
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], std::byte{0x80});
    EXPECT_EQ(bytes[1], std::byte{0x01});
}

TEST(Leb128, encode_uleb128_300) {
    // 300 = 0x12C → LEB128: [0xAC, 0x02]
    auto bytes = encode_uleb128(300);
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], std::byte{0xAC});
    EXPECT_EQ(bytes[1], std::byte{0x02});
}

TEST(Leb128, encode_uleb128_large_value) {
    // 624485 = 0x98765 → LEB128: [0xE5, 0x8E, 0x26]
    auto bytes = encode_uleb128(624485);
    ASSERT_EQ(bytes.size(), 3u);
    EXPECT_EQ(bytes[0], std::byte{0xE5});
    EXPECT_EQ(bytes[1], std::byte{0x8E});
    EXPECT_EQ(bytes[2], std::byte{0x26});
}

TEST(Leb128, encode_uleb128_max_uint64) {
    auto bytes = encode_uleb128(std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(bytes.size(), 10u);  // 64 bits needs ceil(64/7) = 10 bytes
}

TEST(Leb128, decode_uleb128_zero) {
    auto input = std::vector<std::byte>{std::byte{0x00}};
    auto result = decode_uleb128(input);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 0u);
    EXPECT_EQ(result->bytes_read, 1u);
}

TEST(Leb128, decode_uleb128_single_byte) {
    auto input = std::vector<std::byte>{std::byte{42}};
    auto result = decode_uleb128(input);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 42u);
    EXPECT_EQ(result->bytes_read, 1u);
}

TEST(Leb128, decode_uleb128_two_bytes) {
    auto input = std::vector<std::byte>{std::byte{0x80}, std::byte{0x01}};
    auto result = decode_uleb128(input);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 128u);
    EXPECT_EQ(result->bytes_read, 2u);
}

TEST(Leb128, decode_uleb128_truncated_returns_nullopt) {
    // Byte has continuation bit set but no more data
    auto input = std::vector<std::byte>{std::byte{0x80}};
    auto result = decode_uleb128(input);
    EXPECT_FALSE(result.has_value());
}

TEST(Leb128, decode_uleb128_empty_returns_nullopt) {
    auto result = decode_uleb128(std::span<const std::byte>{});
    EXPECT_FALSE(result.has_value());
}

TEST(Leb128, uleb128_round_trip) {
    auto test_values = std::vector<std::uint64_t>{
        0, 1, 42, 127, 128, 255, 256, 300, 624485,
        65535, 1000000, std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint64_t>::max()
    };

    for (auto val : test_values) {
        auto bytes = encode_uleb128(val);
        auto result = decode_uleb128(bytes);
        ASSERT_TRUE(result.has_value()) << "Failed for value " << val;
        EXPECT_EQ(result->value, val) << "Round-trip failed for value " << val;
        EXPECT_EQ(result->bytes_read, bytes.size());
    }
}

// -- Signed LEB128 ------------------------------------------------------------

TEST(Leb128, encode_sleb128_zero) {
    auto bytes = encode_sleb128(0);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], std::byte{0x00});
}

TEST(Leb128, encode_sleb128_positive) {
    auto bytes = encode_sleb128(42);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], std::byte{42});
}

TEST(Leb128, encode_sleb128_negative_one) {
    // -1 in signed LEB128 = [0x7F]
    auto bytes = encode_sleb128(-1);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], std::byte{0x7F});
}

TEST(Leb128, encode_sleb128_negative_128) {
    // -128 = 0xFFFFFF80 → LEB128: [0x80, 0x7F]
    auto bytes = encode_sleb128(-128);
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], std::byte{0x80});
    EXPECT_EQ(bytes[1], std::byte{0x7F});
}

TEST(Leb128, decode_sleb128_zero) {
    auto input = std::vector<std::byte>{std::byte{0x00}};
    auto result = decode_sleb128(input);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 0);
    EXPECT_EQ(result->bytes_read, 1u);
}

TEST(Leb128, decode_sleb128_negative_one) {
    auto input = std::vector<std::byte>{std::byte{0x7F}};
    auto result = decode_sleb128(input);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, -1);
}

TEST(Leb128, decode_sleb128_truncated_returns_nullopt) {
    auto input = std::vector<std::byte>{std::byte{0x80}};
    auto result = decode_sleb128(input);
    EXPECT_FALSE(result.has_value());
}

TEST(Leb128, sleb128_round_trip) {
    auto test_values = std::vector<std::int64_t>{
        0, 1, -1, 42, -42, 63, -64, 64, -65,
        127, -128, 128, -129, 255, -256,
        65535, -65536, 1000000, -1000000,
        std::numeric_limits<std::int64_t>::max(),
        std::numeric_limits<std::int64_t>::min()
    };

    for (auto val : test_values) {
        auto bytes = encode_sleb128(val);
        auto result = decode_sleb128(bytes);
        ASSERT_TRUE(result.has_value()) << "Failed for value " << val;
        EXPECT_EQ(result->value, val) << "Round-trip failed for value " << val;
        EXPECT_EQ(result->bytes_read, bytes.size());
    }
}

// -- Multiple values in sequence ----------------------------------------------

TEST(Leb128, decode_multiple_uleb128_values_from_stream) {
    auto output = std::vector<std::byte>{};
    encode_uleb128(100, output);
    encode_uleb128(200, output);
    encode_uleb128(300, output);

    auto span = std::span<const std::byte>{output};
    auto r1 = decode_uleb128(span);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->value, 100u);

    span = span.subspan(r1->bytes_read);
    auto r2 = decode_uleb128(span);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->value, 200u);

    span = span.subspan(r2->bytes_read);
    auto r3 = decode_uleb128(span);
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(r3->value, 300u);
}
