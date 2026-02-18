#include "../src/storage/chunk.hpp"

#include <gtest/gtest.h>

#include <cstring>

using namespace automerge_cpp::storage;

TEST(Chunk, magic_bytes_are_correct) {
    EXPECT_EQ(chunk_magic[0], std::byte{0x85});
    EXPECT_EQ(chunk_magic[1], std::byte{0x6F});
    EXPECT_EQ(chunk_magic[2], std::byte{0x4A});
    EXPECT_EQ(chunk_magic[3], std::byte{0x83});
}

TEST(Chunk, write_and_parse_header_round_trip) {
    auto body = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    auto output = std::vector<std::byte>{};
    write_chunk(ChunkType::change, body, output);

    auto header = parse_chunk_header(output);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->type, ChunkType::change);
    EXPECT_EQ(header->body_length, 3u);
}

TEST(Chunk, checksum_validates) {
    auto body = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    auto output = std::vector<std::byte>{};
    write_chunk(ChunkType::document, body, output);

    auto header = parse_chunk_header(output);
    ASSERT_TRUE(header.has_value());
    EXPECT_TRUE(validate_chunk_checksum(*header, output));
}

TEST(Chunk, tampered_body_fails_checksum) {
    auto body = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    auto output = std::vector<std::byte>{};
    write_chunk(ChunkType::change, body, output);

    auto header = parse_chunk_header(output);
    ASSERT_TRUE(header.has_value());

    // Tamper with the body
    output[header->body_offset] = std::byte{0xFF};
    EXPECT_FALSE(validate_chunk_checksum(*header, output));
}

TEST(Chunk, tampered_checksum_fails_validation) {
    auto body = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}};
    auto output = std::vector<std::byte>{};
    write_chunk(ChunkType::change, body, output);

    // Tamper with checksum (bytes 4-7)
    output[4] ^= std::byte{0xFF};

    auto header = parse_chunk_header(output);
    ASSERT_TRUE(header.has_value());
    EXPECT_FALSE(validate_chunk_checksum(*header, output));
}

TEST(Chunk, empty_body) {
    auto body = std::vector<std::byte>{};
    auto output = std::vector<std::byte>{};
    write_chunk(ChunkType::document, body, output);

    auto header = parse_chunk_header(output);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->type, ChunkType::document);
    EXPECT_EQ(header->body_length, 0u);
    EXPECT_TRUE(validate_chunk_checksum(*header, output));
}

TEST(Chunk, large_body) {
    auto body = std::vector<std::byte>(10000, std::byte{0xAB});
    auto output = std::vector<std::byte>{};
    write_chunk(ChunkType::compressed, body, output);

    auto header = parse_chunk_header(output);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->type, ChunkType::compressed);
    EXPECT_EQ(header->body_length, 10000u);
    EXPECT_TRUE(validate_chunk_checksum(*header, output));
}

TEST(Chunk, truncated_data_returns_nullopt) {
    auto data = std::vector<std::byte>{std::byte{0x85}, std::byte{0x6F}};
    auto header = parse_chunk_header(data);
    EXPECT_FALSE(header.has_value());
}

TEST(Chunk, wrong_magic_returns_nullopt) {
    auto data = std::vector<std::byte>{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00},
    };
    auto header = parse_chunk_header(data);
    EXPECT_FALSE(header.has_value());
}

TEST(Chunk, all_chunk_types_round_trip) {
    for (auto type : {ChunkType::document, ChunkType::change, ChunkType::compressed}) {
        auto body = std::vector<std::byte>{std::byte{0x42}};
        auto output = std::vector<std::byte>{};
        write_chunk(type, body, output);

        auto header = parse_chunk_header(output);
        ASSERT_TRUE(header.has_value());
        EXPECT_EQ(header->type, type);
        EXPECT_TRUE(validate_chunk_checksum(*header, output));
    }
}

TEST(Chunk, body_extraction) {
    auto body = std::vector<std::byte>{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
    auto output = std::vector<std::byte>{};
    write_chunk(ChunkType::change, body, output);

    auto header = parse_chunk_header(output);
    ASSERT_TRUE(header.has_value());

    // Extract body and verify it matches
    auto extracted = std::vector<std::byte>(
        output.begin() + static_cast<std::ptrdiff_t>(header->body_offset),
        output.begin() + static_cast<std::ptrdiff_t>(header->body_offset + header->body_length));
    EXPECT_EQ(extracted, body);
}
