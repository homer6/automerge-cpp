#include <automerge-cpp/automerge.hpp>

#include "../src/storage/change_chunk.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using automerge_cpp::ChangeHash;
using automerge_cpp::Document;

auto fixture_path(std::string_view filename) -> std::filesystem::path {
    return std::filesystem::path{AUTOMERGE_CPP_SOURCE_DIR} / "tests" / "fixtures" / filename;
}

auto read_bytes(const std::filesystem::path& path) -> std::vector<std::byte> {
    auto input = std::ifstream{path, std::ios::binary};
    const auto chars = std::vector<char>{std::istreambuf_iterator<char>{input}, {}};
    auto bytes = std::vector<std::byte>{};
    bytes.reserve(chars.size());
    for (const auto character : chars) {
        bytes.push_back(static_cast<std::byte>(character));
    }
    return bytes;
}

auto read_lines(const std::filesystem::path& path) -> std::vector<std::string> {
    auto input = std::ifstream{path};
    auto lines = std::vector<std::string>{};
    auto line = std::string{};
    while (std::getline(input, line)) {
        lines.push_back(std::move(line));
    }
    return lines;
}

auto to_hex(const ChangeHash& hash) -> std::string {
    constexpr auto digits = "0123456789abcdef";
    auto result = std::string{};
    result.reserve(hash.bytes.size() * 2);
    for (const auto byte : hash.bytes) {
        const auto value = std::to_integer<unsigned char>(byte);
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0x0F]);
    }
    return result;
}

TEST(ChangeHashFixture, matches_automerge_rs_change_chunks) {
    const auto fixture_names = std::array{
        "map_puts",
        "list_inserts",
        "text_splice",
        "two_actor_merge",
    };

    for (const auto fixture_name : fixture_names) {
        const auto bytes = read_bytes(fixture_path(std::string{fixture_name} + ".am"));
        const auto expected_hashes =
            read_lines(fixture_path(std::string{fixture_name} + ".hashes"));

        auto decoded_hashes = std::vector<std::string>{};
        auto expected_heads = std::vector<ChangeHash>{};
        auto offset = std::size_t{0};
        while (offset < bytes.size()) {
            const auto remaining = std::span<const std::byte>{bytes}.subspan(offset);
            const auto header = automerge_cpp::storage::parse_chunk_header(remaining);
            ASSERT_TRUE(header.has_value()) << fixture_name;
            const auto chunk_size = header->body_offset + header->body_length;
            ASSERT_LE(chunk_size, remaining.size()) << fixture_name;

            const auto change =
                automerge_cpp::storage::parse_change_chunk(remaining.first(chunk_size));
            ASSERT_TRUE(change.has_value()) << fixture_name;
            decoded_hashes.push_back(to_hex(change->hash));
            for (const auto& dependency : change->change.deps) {
                std::erase(expected_heads, dependency);
            }
            expected_heads.push_back(change->hash);
            const auto original_chunk = std::vector<std::byte>{
                remaining.begin(), remaining.begin() + static_cast<std::ptrdiff_t>(chunk_size)};
            const auto encoded_chunk =
                automerge_cpp::storage::serialize_change_chunk(change->change);
            EXPECT_EQ(encoded_chunk.size(), original_chunk.size()) << fixture_name;
            const auto common_size = std::min(encoded_chunk.size(), original_chunk.size());
            for (auto index = std::size_t{0}; index < common_size; ++index) {
                EXPECT_EQ(encoded_chunk[index], original_chunk[index])
                    << fixture_name << " byte " << index;
            }
            offset += chunk_size;
        }

        EXPECT_EQ(decoded_hashes, expected_hashes) << fixture_name;
        const auto document = Document::load(bytes);
        ASSERT_TRUE(document.has_value()) << fixture_name;
        EXPECT_EQ(document->get_changes().size(), expected_hashes.size()) << fixture_name;
        EXPECT_EQ(document->get_heads(), expected_heads) << fixture_name;
    }
}

} // namespace
