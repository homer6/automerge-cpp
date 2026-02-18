#pragma once

// Header-only SHA-256 implementation.
// Produces a 32-byte digest conforming to FIPS 180-4.
// Internal header — not installed.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace automerge_cpp::crypto {

namespace detail {

inline constexpr std::uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline constexpr auto rotr(std::uint32_t x, unsigned n) -> std::uint32_t {
    return (x >> n) | (x << (32 - n));
}

inline constexpr auto ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) -> std::uint32_t {
    return (x & y) ^ (~x & z);
}

inline constexpr auto maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) -> std::uint32_t {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline constexpr auto sigma0(std::uint32_t x) -> std::uint32_t {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline constexpr auto sigma1(std::uint32_t x) -> std::uint32_t {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline constexpr auto gamma0(std::uint32_t x) -> std::uint32_t {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline constexpr auto gamma1(std::uint32_t x) -> std::uint32_t {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

inline auto read_be32(const std::byte* p) -> std::uint32_t {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           (static_cast<std::uint32_t>(p[3]));
}

inline void write_be32(std::byte* p, std::uint32_t v) {
    p[0] = static_cast<std::byte>(v >> 24);
    p[1] = static_cast<std::byte>(v >> 16);
    p[2] = static_cast<std::byte>(v >> 8);
    p[3] = static_cast<std::byte>(v);
}

inline void write_be64(std::byte* p, std::uint64_t v) {
    p[0] = static_cast<std::byte>(v >> 56);
    p[1] = static_cast<std::byte>(v >> 48);
    p[2] = static_cast<std::byte>(v >> 40);
    p[3] = static_cast<std::byte>(v >> 32);
    p[4] = static_cast<std::byte>(v >> 24);
    p[5] = static_cast<std::byte>(v >> 16);
    p[6] = static_cast<std::byte>(v >> 8);
    p[7] = static_cast<std::byte>(v);
}

}  // namespace detail

// Compute SHA-256 digest of the input bytes.
inline auto sha256(std::span<const std::byte> input) -> std::array<std::byte, 32> {
    // Initial hash values
    auto h = std::array<std::uint32_t, 8>{
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };

    // Padding: message + 0x80 + zeros + 64-bit big-endian length
    const auto msg_len = input.size();
    const auto bit_len = static_cast<std::uint64_t>(msg_len) * 8;

    // Pad to 64-byte blocks: need 1 byte (0x80) + enough zeros + 8 bytes length
    auto padded_len = ((msg_len + 1 + 8 + 63) / 64) * 64;

    // Stack buffer for small inputs (covers typical change hashing ~80-120 bytes).
    // Falls back to heap for inputs requiring >256 bytes padded.
    static constexpr std::size_t stack_buf_size = 256;
    auto stack_buf = std::array<std::byte, stack_buf_size>{};
    auto heap_buf = std::vector<std::byte>{};

    std::byte* padded = nullptr;
    if (padded_len <= stack_buf_size) {
        stack_buf.fill(std::byte{0});
        padded = stack_buf.data();
    } else {
        heap_buf.resize(padded_len, std::byte{0});
        padded = heap_buf.data();
    }
    std::memcpy(padded, input.data(), msg_len);
    padded[msg_len] = std::byte{0x80};
    detail::write_be64(padded + padded_len - 8, bit_len);

    // Process each 64-byte block
    auto w = std::array<std::uint32_t, 64>{};

    for (std::size_t block = 0; block < padded_len; block += 64) {
        const auto* bp = padded + block;

        // Prepare message schedule
        for (int i = 0; i < 16; ++i) {
            w[i] = detail::read_be32(bp + static_cast<std::ptrdiff_t>(i) * 4);
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = detail::gamma1(w[i - 2]) + w[i - 7] +
                   detail::gamma0(w[i - 15]) + w[i - 16];
        }

        // Working variables
        auto a = h[0], b = h[1], c = h[2], d = h[3];
        auto e = h[4], f = h[5], g = h[6], hh = h[7];

        // 64 rounds
        for (int i = 0; i < 64; ++i) {
            auto t1 = hh + detail::sigma1(e) + detail::ch(e, f, g) + detail::k[i] + w[i];
            auto t2 = detail::sigma0(a) + detail::maj(a, b, c);
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    // Produce output
    auto result = std::array<std::byte, 32>{};
    for (int i = 0; i < 8; ++i) {
        detail::write_be32(result.data() + static_cast<std::ptrdiff_t>(i) * 4, h[i]);
    }
    return result;
}

// Convenience: hash a vector of bytes.
inline auto sha256(const std::vector<std::byte>& input) -> std::array<std::byte, 32> {
    return sha256(std::span<const std::byte>{input});
}

}  // namespace automerge_cpp::crypto
