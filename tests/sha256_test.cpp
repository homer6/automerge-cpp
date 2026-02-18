#include "../src/crypto/sha256.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

using namespace automerge_cpp::crypto;

// Helper: convert a hex string to bytes
static auto hex_to_bytes(const std::string& hex) -> std::vector<std::byte> {
    auto result = std::vector<std::byte>{};
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        auto byte_str = hex.substr(i, 2);
        result.push_back(static_cast<std::byte>(std::stoul(byte_str, nullptr, 16)));
    }
    return result;
}

// Helper: convert bytes to hex string
static auto bytes_to_hex(std::span<const std::byte> bytes) -> std::string {
    static constexpr char hex_chars[] = "0123456789abcdef";
    auto result = std::string{};
    for (auto b : bytes) {
        auto val = static_cast<std::uint8_t>(b);
        result += hex_chars[val >> 4];
        result += hex_chars[val & 0x0F];
    }
    return result;
}

// Helper: hash a string
static auto sha256_string(const std::string& s) -> std::string {
    auto input = std::vector<std::byte>(s.size());
    std::memcpy(input.data(), s.data(), s.size());
    auto digest = sha256(input);
    return bytes_to_hex(digest);
}

// NIST test vectors

TEST(Sha256, empty_string) {
    auto digest = sha256_string("");
    EXPECT_EQ(digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, abc) {
    auto digest = sha256_string("abc");
    EXPECT_EQ(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256, two_block_message) {
    auto digest = sha256_string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    EXPECT_EQ(digest, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, long_message) {
    auto digest = sha256_string(
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
        "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu");
    EXPECT_EQ(digest, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST(Sha256, single_byte) {
    auto input = std::vector<std::byte>{std::byte{0x00}};
    auto digest = sha256(input);
    auto hex = bytes_to_hex(digest);
    EXPECT_EQ(hex, "6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d");
}

TEST(Sha256, sixty_four_bytes_exact_block) {
    // Exactly one block of data
    auto input = std::vector<std::byte>(64, std::byte{0x41});  // 64 'A's
    auto digest = sha256(input);
    auto hex = bytes_to_hex(digest);
    // Known result for 64 x 'A'
    auto expected = sha256_string(std::string(64, 'A'));
    EXPECT_EQ(hex, expected);
}

TEST(Sha256, span_overload_matches_vector_overload) {
    auto input = std::vector<std::byte>{std::byte{0x48}, std::byte{0x65},
                                         std::byte{0x6c}, std::byte{0x6c}, std::byte{0x6f}};
    auto digest1 = sha256(input);
    auto digest2 = sha256(std::span<const std::byte>{input});
    EXPECT_EQ(digest1, digest2);
}
