#include "../src/storage/columns/column_spec.hpp"
#include "../src/storage/columns/raw_column.hpp"
#include "../src/storage/columns/compression.hpp"

#include <gtest/gtest.h>

using namespace automerge_cpp::storage;

// -- ColumnSpec tests ---------------------------------------------------------

TEST(ColumnSpec, round_trip_u32) {
    auto spec = ColumnSpec{.column_id = 7, .type = ColumnType::actor_id, .deflate = false};
    auto u32 = spec.to_u32();
    auto decoded = ColumnSpec::from_u32(u32);
    EXPECT_EQ(decoded.column_id, 7u);
    EXPECT_EQ(decoded.type, ColumnType::actor_id);
    EXPECT_FALSE(decoded.deflate);
    EXPECT_EQ(spec, decoded);
}

TEST(ColumnSpec, u32_encoding_matches_upstream) {
    // Column ID 7, type actor_id (1), no deflate
    // u32 = (7 << 4) | (0 << 3) | 1 = 112 | 1 = 113
    auto spec = ColumnSpec{.column_id = 7, .type = ColumnType::actor_id, .deflate = false};
    EXPECT_EQ(spec.to_u32(), 113u);

    // Column ID 0, type group_card (0), no deflate
    // u32 = 0
    auto spec2 = ColumnSpec{.column_id = 0, .type = ColumnType::group_card, .deflate = false};
    EXPECT_EQ(spec2.to_u32(), 0u);
}

TEST(ColumnSpec, deflate_flag_in_u32) {
    auto spec = ColumnSpec{.column_id = 5, .type = ColumnType::value_raw, .deflate = true};
    auto u32 = spec.to_u32();
    // (5 << 4) | (1 << 3) | 6 = 80 | 8 | 6 = 94
    EXPECT_EQ(u32, 94u);
    auto decoded = ColumnSpec::from_u32(u32);
    EXPECT_TRUE(decoded.deflate);
    EXPECT_EQ(decoded.column_id, 5u);
    EXPECT_EQ(decoded.type, ColumnType::value_raw);
}

TEST(ColumnSpec, all_types_round_trip) {
    for (std::uint8_t t = 0; t <= 7; ++t) {
        auto spec = ColumnSpec{.column_id = 42, .type = static_cast<ColumnType>(t), .deflate = false};
        EXPECT_EQ(ColumnSpec::from_u32(spec.to_u32()), spec);
    }
}

// -- RawColumn parse/write tests ----------------------------------------------

TEST(RawColumn, write_and_parse_round_trip) {
    auto columns = std::vector<RawColumn>{
        {.spec = ColumnSpec{.column_id = 0, .type = ColumnType::actor_id, .deflate = false},
         .data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}},
        {.spec = ColumnSpec{.column_id = 1, .type = ColumnType::delta_int, .deflate = false},
         .data = {std::byte{0x0A}, std::byte{0x0B}}},
        {.spec = ColumnSpec{.column_id = 4, .type = ColumnType::integer_rle, .deflate = false},
         .data = {std::byte{0xFF}}},
    };

    auto output = std::vector<std::byte>{};
    write_raw_columns(columns, output);

    auto pos = std::size_t{0};
    auto parsed = parse_raw_columns(output, pos);

    ASSERT_EQ(parsed.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(parsed[i].spec, columns[i].spec);
        EXPECT_EQ(parsed[i].data, columns[i].data);
    }
    EXPECT_EQ(pos, output.size());
}

TEST(RawColumn, empty_columns) {
    auto columns = std::vector<RawColumn>{};
    auto output = std::vector<std::byte>{};
    write_raw_columns(columns, output);
    EXPECT_TRUE(output.empty());
}

TEST(RawColumn, column_with_empty_data) {
    auto columns = std::vector<RawColumn>{
        {.spec = ColumnSpec{.column_id = 0, .type = ColumnType::group_card, .deflate = false},
         .data = {}},
    };

    auto output = std::vector<std::byte>{};
    write_raw_columns(columns, output);

    auto pos = std::size_t{0};
    auto parsed = parse_raw_columns(output, pos);

    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_TRUE(parsed[0].data.empty());
}

// -- Compression tests --------------------------------------------------------

TEST(Compression, round_trip_small_data) {
    auto input = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    auto compressed = deflate_compress(input);
    ASSERT_TRUE(compressed.has_value());

    auto decompressed = deflate_decompress(*compressed);
    ASSERT_TRUE(decompressed.has_value());
    EXPECT_EQ(*decompressed, input);
}

TEST(Compression, round_trip_large_data) {
    auto input = std::vector<std::byte>(1024, std::byte{0x42});
    auto compressed = deflate_compress(input);
    ASSERT_TRUE(compressed.has_value());

    // Repeated data should compress well
    EXPECT_LT(compressed->size(), input.size());

    auto decompressed = deflate_decompress(*compressed);
    ASSERT_TRUE(decompressed.has_value());
    EXPECT_EQ(*decompressed, input);
}

TEST(Compression, empty_data) {
    auto input = std::vector<std::byte>{};
    auto compressed = deflate_compress(input);
    ASSERT_TRUE(compressed.has_value());
    EXPECT_TRUE(compressed->empty());

    auto decompressed = deflate_decompress(*compressed);
    ASSERT_TRUE(decompressed.has_value());
    EXPECT_TRUE(decompressed->empty());
}

TEST(Compression, threshold_check) {
    // Data below threshold should not be compressed (caller decides)
    auto small = std::vector<std::byte>(100, std::byte{0x00});
    EXPECT_LT(small.size(), deflate_threshold);

    auto large = std::vector<std::byte>(300, std::byte{0x00});
    EXPECT_GT(large.size(), deflate_threshold);
}
