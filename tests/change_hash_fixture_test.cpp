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
        "commit_metadata",
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
            std::ranges::sort(expected_heads);
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

        auto compact_document = Document{};
        const auto compact_result = compact_document.load_incremental(
            read_bytes(fixture_path(std::string{fixture_name} + ".document.am")));
        ASSERT_TRUE(compact_result.has_value()) << fixture_name;
        EXPECT_EQ(*compact_result, expected_hashes.size()) << fixture_name;
        EXPECT_EQ(compact_document.get_heads(), expected_heads) << fixture_name;
    }

    auto actor_bytes = std::array<std::byte, automerge_cpp::ActorId::size>{};
    actor_bytes.fill(std::byte{0x77});
    auto document = Document{};
    document.set_actor_id(automerge_cpp::ActorId{actor_bytes});
    document.transact(
        [](auto& transaction) { transaction.put(automerge_cpp::root, "metadata", true); },
        automerge_cpp::CommitOptions{
            .message = "import customer record",
            .time = std::int64_t{1725000000123},
        });
    const auto expected_hashes = read_lines(fixture_path("commit_metadata.hashes"));
    const auto heads = document.get_heads();
    ASSERT_EQ(expected_hashes.size(), 1u);
    ASSERT_EQ(heads.size(), 1u);
    EXPECT_EQ(to_hex(heads.front()), expected_hashes.front());
    EXPECT_EQ(document.save_incremental(), read_bytes(fixture_path("commit_metadata.am")));
}

TEST(IncrementalSaveFixture, matches_automerge_rs_change_chunks) {
    auto actor_bytes = std::array<std::byte, automerge_cpp::ActorId::size>{};
    actor_bytes.fill(std::byte{0x66});
    auto document = Document{};
    document.set_actor_id(automerge_cpp::ActorId{actor_bytes});
    document.transact([](auto& transaction) {
        transaction.put(automerge_cpp::root, "stage", std::string(400, 'b'));
    });
    static_cast<void>(document.save());

    document.transact([](auto& transaction) {
        // Create the first incremental change.
        transaction.put(automerge_cpp::root, "first", std::int64_t{1});
    });
    EXPECT_EQ(document.save_incremental(), read_bytes(fixture_path("incremental_first.am")));

    document.transact([](auto& transaction) {
        // Create the second incremental change.
        transaction.put(automerge_cpp::root, "second", std::int64_t{2});
    });
    EXPECT_EQ(document.save_incremental(), read_bytes(fixture_path("incremental_second.am")));
    EXPECT_TRUE(document.save_incremental().empty());
}

TEST(IncrementalLoadFixture, applies_automerge_rs_document_and_change_chunks) {
    auto document = Document{};
    const auto base = document.load_incremental(read_bytes(fixture_path("incremental_base.am")));
    ASSERT_TRUE(base.has_value()) << base.error().message;
    EXPECT_EQ(*base, 1u);
    EXPECT_EQ(document.get<std::string>(automerge_cpp::root, "stage"), std::string(400, 'b'));

    const auto first = document.load_incremental(read_bytes(fixture_path("incremental_first.am")));
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_EQ(*first, 1u);
    EXPECT_EQ(document.get<std::int64_t>(automerge_cpp::root, "first"), 1);

    const auto second =
        document.load_incremental(read_bytes(fixture_path("incremental_second.am")));
    ASSERT_TRUE(second.has_value()) << second.error().message;
    EXPECT_EQ(*second, 1u);
    EXPECT_EQ(document.get<std::int64_t>(automerge_cpp::root, "second"), 2);
}

} // namespace
