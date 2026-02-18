#pragma once

// Bloom filter for the Automerge sync protocol.
// Internal header — not installed.
//
// Mirrors the upstream Rust implementation:
// - 10 bits per entry, 7 probes → ~1% false positive rate
// - LFSR-based multi-hash from the first 12 bytes of a ChangeHash
// - Serialized as: LEB128(num_entries) + LEB128(bits_per_entry)
//                  + LEB128(num_probes) + raw_bits

#include <automerge-cpp/types.hpp>
#include "../encoding/leb128.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace automerge_cpp::sync {

class BloomFilter {
public:
    static constexpr std::uint32_t default_bits_per_entry = 10;
    static constexpr std::uint32_t default_num_probes = 7;

    BloomFilter() = default;

    // Construct with a known number of entries (pre-sizes the bit vector).
    explicit BloomFilter(std::uint32_t num_entries)
        : num_entries_{num_entries}
        , num_bits_per_entry_{default_bits_per_entry}
        , num_probes_{default_num_probes}
        , bits_(bit_capacity(num_entries, default_bits_per_entry), 0) {}

    // Build from an iterator of hashes.
    template <typename It>
    static auto from_hashes(It first, It last) -> BloomFilter {
        auto count = static_cast<std::uint32_t>(std::distance(first, last));
        auto bf = BloomFilter{count};
        for (auto it = first; it != last; ++it) {
            bf.add_hash(*it);
        }
        return bf;
    }

    static auto from_hashes(const std::vector<ChangeHash>& hashes) -> BloomFilter {
        return from_hashes(hashes.begin(), hashes.end());
    }

    void add_hash(const ChangeHash& hash) {
        if (bits_.empty()) return;
        auto probes = get_probes(hash);
        for (auto p : probes) {
            set_bit(p);
        }
    }

    auto contains_hash(const ChangeHash& hash) const -> bool {
        if (bits_.empty()) return false;
        auto probes = get_probes(hash);
        for (auto p : probes) {
            if (!get_bit(p)) return false;
        }
        return true;
    }

    auto empty() const -> bool { return num_entries_ == 0; }

    // Serialize to bytes.
    auto to_bytes() const -> std::vector<std::byte> {
        if (num_entries_ == 0) return {};
        auto result = std::vector<std::byte>{};
        encoding::encode_uleb128(num_entries_, result);
        encoding::encode_uleb128(num_bits_per_entry_, result);
        encoding::encode_uleb128(num_probes_, result);
        for (auto b : bits_) {
            result.push_back(static_cast<std::byte>(b));
        }
        return result;
    }

    // Deserialize from bytes.
    static auto from_bytes(std::span<const std::byte> data) -> std::optional<BloomFilter> {
        if (data.empty()) return BloomFilter{};

        std::size_t pos = 0;

        auto num_entries = encoding::decode_uleb128(data.subspan(pos));
        if (!num_entries) return std::nullopt;
        pos += num_entries->bytes_read;

        auto bits_per_entry = encoding::decode_uleb128(data.subspan(pos));
        if (!bits_per_entry) return std::nullopt;
        pos += bits_per_entry->bytes_read;

        auto num_probes = encoding::decode_uleb128(data.subspan(pos));
        if (!num_probes) return std::nullopt;
        pos += num_probes->bytes_read;

        auto remaining = data.size() - pos;
        auto bf = BloomFilter{};
        bf.num_entries_ = static_cast<std::uint32_t>(num_entries->value);
        bf.num_bits_per_entry_ = static_cast<std::uint32_t>(bits_per_entry->value);
        bf.num_probes_ = static_cast<std::uint32_t>(num_probes->value);
        bf.bits_.resize(remaining);
        for (std::size_t i = 0; i < remaining; ++i) {
            bf.bits_[i] = static_cast<std::uint8_t>(data[pos + i]);
        }
        return bf;
    }

    auto operator==(const BloomFilter&) const -> bool = default;

private:
    std::uint32_t num_entries_{0};
    std::uint32_t num_bits_per_entry_{default_bits_per_entry};
    std::uint32_t num_probes_{default_num_probes};
    std::vector<std::uint8_t> bits_;

    static auto bit_capacity(std::uint32_t entries, std::uint32_t bits_per_entry)
        -> std::size_t {
        auto total_bits = static_cast<std::uint64_t>(entries) * bits_per_entry;
        return static_cast<std::size_t>((total_bits + 7) / 8);
    }

    auto modulo() const -> std::uint32_t {
        return static_cast<std::uint32_t>(bits_.size() * 8);
    }

    // Extract probe positions using LFSR-based multi-hash.
    auto get_probes(const ChangeHash& hash) const -> std::vector<std::uint32_t> {
        auto m = modulo();
        if (m == 0) return {};

        std::uint32_t x{}, y{}, z{};
        std::memcpy(&x, &hash.bytes[0], 4);
        std::memcpy(&y, &hash.bytes[4], 4);
        std::memcpy(&z, &hash.bytes[8], 4);
        x %= m;
        y %= m;
        z %= m;

        auto probes = std::vector<std::uint32_t>{};
        probes.reserve(num_probes_);
        probes.push_back(x);
        for (std::uint32_t i = 1; i < num_probes_; ++i) {
            x = (x + y) % m;
            y = (y + z) % m;
            probes.push_back(x);
        }
        return probes;
    }

    auto get_bit(std::uint32_t pos) const -> bool {
        return (bits_[pos >> 3] & (1u << (pos & 7))) != 0;
    }

    void set_bit(std::uint32_t pos) {
        bits_[pos >> 3] |= static_cast<std::uint8_t>(1u << (pos & 7));
    }
};

}  // namespace automerge_cpp::sync
