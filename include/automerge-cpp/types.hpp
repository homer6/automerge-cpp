#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>

namespace automerge_cpp {

// -- ActorId ------------------------------------------------------------------
// A 16-byte unique identifier for a peer/actor.
// Lexicographic ordering on raw bytes.

struct ActorId {
    static constexpr std::size_t size = 16;
    std::array<std::byte, size> bytes{};

    constexpr ActorId() = default;

    explicit constexpr ActorId(std::array<std::byte, size> b) : bytes{b} {}

    // Construct from a span of raw bytes (must be exactly 16 bytes).
    // For convenience in tests; production code should validate length.
    explicit ActorId(const std::uint8_t (&raw)[size]) {
        std::ranges::transform(raw, bytes.begin(),
            [](std::uint8_t b) { return std::byte{b}; });
    }

    auto operator<=>(const ActorId&) const = default;
    auto operator==(const ActorId&) const -> bool = default;

    auto is_zero() const -> bool {
        return std::ranges::all_of(bytes, [](std::byte b) {
            return b == std::byte{0};
        });
    }
};

// -- ChangeHash ---------------------------------------------------------------
// A 32-byte SHA-256 content hash of a change.

struct ChangeHash {
    static constexpr std::size_t size = 32;
    std::array<std::byte, size> bytes{};

    constexpr ChangeHash() = default;

    explicit constexpr ChangeHash(std::array<std::byte, size> b) : bytes{b} {}

    explicit ChangeHash(const std::uint8_t (&raw)[size]) {
        std::ranges::transform(raw, bytes.begin(),
            [](std::uint8_t b) { return std::byte{b}; });
    }

    auto operator<=>(const ChangeHash&) const = default;
    auto operator==(const ChangeHash&) const -> bool = default;

    auto is_zero() const -> bool {
        return std::ranges::all_of(bytes, [](std::byte b) {
            return b == std::byte{0};
        });
    }
};

// -- OpId ---------------------------------------------------------------------
// Identifies a single operation: (counter, actor).
// Total ordering: counter first, then actor (deterministic tie-breaking).

struct OpId {
    std::uint64_t counter{0};
    ActorId actor{};

    constexpr OpId() = default;
    constexpr OpId(std::uint64_t c, ActorId a) : counter{c}, actor{a} {}

    auto operator<=>(const OpId&) const = default;
    auto operator==(const OpId&) const -> bool = default;
};

// -- ObjId --------------------------------------------------------------------
// Identifies a CRDT object in the document tree.
// Either the root sentinel or an OpId that created the object.

struct Root {
    auto operator<=>(const Root&) const = default;
    auto operator==(const Root&) const -> bool = default;
};

struct ObjId {
    std::variant<Root, OpId> inner;

    constexpr ObjId() : inner{Root{}} {}
    explicit constexpr ObjId(OpId id) : inner{id} {}

    auto is_root() const -> bool {
        return std::holds_alternative<Root>(inner);
    }

    auto operator<=>(const ObjId&) const = default;
    auto operator==(const ObjId&) const -> bool = default;
};

// The root object — always a Map, always exists.
inline constexpr auto root = ObjId{};

// -- Prop ---------------------------------------------------------------------
// A key into a map (string) or an index into a list/text (size_t).

using Prop = std::variant<std::string, std::size_t>;

// Convenience constructors
inline auto map_key(std::string key) -> Prop { return Prop{std::move(key)}; }
inline auto list_index(std::size_t idx) -> Prop { return Prop{idx}; }

}  // namespace automerge_cpp

// -- std::hash specializations ------------------------------------------------

template <>
struct std::hash<automerge_cpp::ActorId> {
    auto operator()(const automerge_cpp::ActorId& id) const noexcept -> std::size_t {
        // FNV-1a over the 16 bytes
        auto h = std::size_t{14695981039346656037ULL};
        for (auto b : id.bytes) {
            h ^= static_cast<std::size_t>(b);
            h *= std::size_t{1099511628211ULL};
        }
        return h;
    }
};

template <>
struct std::hash<automerge_cpp::ChangeHash> {
    auto operator()(const automerge_cpp::ChangeHash& ch) const noexcept -> std::size_t {
        // First 8 bytes of the SHA-256 hash are already well-distributed
        auto result = std::size_t{0};
        const auto* p = reinterpret_cast<const unsigned char*>(ch.bytes.data());
        for (std::size_t i = 0; i < sizeof(std::size_t); ++i) {
            result = (result << 8) | p[i];
        }
        return result;
    }
};

template <>
struct std::hash<automerge_cpp::OpId> {
    auto operator()(const automerge_cpp::OpId& id) const noexcept -> std::size_t {
        auto h1 = std::hash<std::uint64_t>{}(id.counter);
        auto h2 = std::hash<automerge_cpp::ActorId>{}(id.actor);
        return h1 ^ (h2 << 1);
    }
};

template <>
struct std::hash<automerge_cpp::ObjId> {
    auto operator()(const automerge_cpp::ObjId& id) const noexcept -> std::size_t {
        return std::visit([](const auto& v) -> std::size_t {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, automerge_cpp::Root>) {
                return 0;
            } else {
                return std::hash<automerge_cpp::OpId>{}(v);
            }
        }, id.inner);
    }
};
