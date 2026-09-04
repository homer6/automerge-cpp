#include <automerge-cpp/automerge.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using automerge_cpp::Document;
using automerge_cpp::root;
using automerge_cpp::SyncMessage;
using automerge_cpp::SyncState;

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

auto sync_message_paths() -> std::vector<std::filesystem::path> {
    auto paths = std::vector<std::filesystem::path>{};
    const auto directory = fixture_path("sync_exchange");
    for (const auto& entry : std::filesystem::directory_iterator{directory}) {
        if (entry.path().extension() == ".sync") {
            paths.push_back(entry.path());
        }
    }
    std::ranges::sort(paths);
    return paths;
}

auto sync_documents(Document& first,
                    Document& second,
                    SyncState& first_state,
                    SyncState& second_state) -> bool {
    for (auto round = 0; round < 20; ++round) {
        const auto first_message = first.generate_sync_message(first_state);
        const auto second_message = second.generate_sync_message(second_state);
        if (!first_message && !second_message) {
            return true;
        }
        if (first_message) {
            const auto received = second.receive_sync_message(second_state, *first_message);
            if (!received) {
                return false;
            }
        }
        if (second_message) {
            const auto received = first.receive_sync_message(first_state, *second_message);
            if (!received) {
                return false;
            }
        }
    }
    return false;
}

auto load_sync_document(std::string_view name) -> std::optional<Document> {
    return Document::load(
        read_bytes(fixture_path(std::string{"sync_exchange/"} + std::string{name})));
}

void expect_fixture_exchange_with_replacement(bool replace_first) {
    SCOPED_TRACE(replace_first ? "replace first" : "replace second");
    auto replacement = load_sync_document(replace_first ? "first.am" : "second.am");
    ASSERT_TRUE(replacement.has_value());
    auto state = SyncState{};
    const auto paths = sync_message_paths();
    ASSERT_EQ(paths.size(), 6);

    for (auto round = std::size_t{0}; round < paths.size(); round += 2) {
        const auto first_bytes = read_bytes(paths[round]);
        const auto second_bytes = read_bytes(paths[round + 1]);
        const auto& outgoing_bytes = replace_first ? first_bytes : second_bytes;
        const auto& incoming_bytes = replace_first ? second_bytes : first_bytes;

        const auto outgoing = replacement->generate_sync_message(state);
        ASSERT_TRUE(outgoing.has_value()) << "round " << round / 2;
        EXPECT_EQ(outgoing->encode(), outgoing_bytes) << "round " << round / 2;

        const auto incoming = SyncMessage::decode(incoming_bytes);
        ASSERT_TRUE(incoming.has_value()) << "round " << round / 2;
        const auto received = replacement->receive_sync_message(state, *incoming);
        ASSERT_TRUE(received.has_value()) << "round " << round / 2;
    }

    EXPECT_EQ(replacement->get<std::string>(root, "first"), "one");
    EXPECT_EQ(replacement->get<std::int64_t>(root, "second"), 2);
    EXPECT_EQ(replacement->get<bool>(root, "remote"), true);
}

TEST(SyncMessageWireFormat, empty_message_matches_upstream_bytes) {
    const auto message = SyncMessage{};
    const auto expected = std::vector<std::byte>{
        std::byte{0x42},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
    };

    EXPECT_EQ(message.encode(), expected);
    const auto decoded = SyncMessage::decode(expected);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, message);
}

TEST(SyncMessageWireFormat, automerge_rs_messages_round_trip_byte_identically) {
    const auto paths = sync_message_paths();
    ASSERT_FALSE(paths.empty());
    for (const auto& path : paths) {
        const auto bytes = read_bytes(path);
        const auto message = SyncMessage::decode(bytes);
        ASSERT_TRUE(message.has_value()) << path.filename();
        EXPECT_EQ(message->encode(), bytes) << path.filename();
    }
}

TEST(SyncMessageWireFormat, modifying_decoded_changes_invalidates_cached_chunks) {
    const auto bytes = read_bytes(fixture_path("sync_exchange/02-first-to-second.sync"));
    auto message = SyncMessage::decode(bytes);
    ASSERT_TRUE(message.has_value());
    ASSERT_FALSE(message->changes.empty());

    message->changes.front().message = "modified after decode";
    const auto modified_bytes = message->encode();
    EXPECT_NE(modified_bytes, bytes);

    const auto decoded = SyncMessage::decode(modified_bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_FALSE(decoded->changes.empty());
    EXPECT_EQ(decoded->changes.front().message, "modified after decode");
}

TEST(SyncMessageWireFormat, message_with_changes_applies_after_decode) {
    const auto bytes = read_bytes(fixture_path("sync_exchange/02-first-to-second.sync"));
    const auto message = SyncMessage::decode(bytes);
    ASSERT_TRUE(message.has_value());
    ASSERT_EQ(message->changes.size(), 2);

    auto target = load_sync_document("second.am");
    ASSERT_TRUE(target.has_value());
    auto state = SyncState{};
    const auto patches = target->receive_sync_message_with_patches(state, *message);
    ASSERT_TRUE(patches.has_value());
    EXPECT_FALSE(patches->empty());
    EXPECT_EQ(target->get<std::string>(root, "first"), "one");
    EXPECT_EQ(target->get<std::int64_t>(root, "second"), 2);
}

TEST(SyncMessageWireFormat, fixture_exchange_accepts_either_cpp_peer) {
    expect_fixture_exchange_with_replacement(true);
    expect_fixture_exchange_with_replacement(false);
}

TEST(SyncStatePersistence, decoded_state_resumes_exchange) {
    auto first = Document{};
    auto second = Document{};
    first.transact([](auto& transaction) { transaction.put(root, "before", 1); });

    auto first_state = SyncState{};
    auto second_state = SyncState{};
    ASSERT_TRUE(sync_documents(first, second, first_state, second_state));

    const auto persisted_first = SyncState::decode(first_state.encode());
    const auto persisted_second = SyncState::decode(second_state.encode());
    ASSERT_TRUE(persisted_first.has_value());
    ASSERT_TRUE(persisted_second.has_value());
    first_state = *persisted_first;
    second_state = *persisted_second;

    first.transact([](auto& transaction) { transaction.put(root, "after", 2); });
    ASSERT_TRUE(sync_documents(first, second, first_state, second_state));
    EXPECT_EQ(second.get<std::int64_t>(root, "before"), 1);
    EXPECT_EQ(second.get<std::int64_t>(root, "after"), 2);
}

} // namespace
