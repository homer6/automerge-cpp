/// @file types.hpp
/// @brief Core identity types: ActorId, ChangeHash, OpId, ObjId, Prop.

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

/// A 16-byte unique identifier for a peer/actor.
///
/// Each document participant has a unique ActorId. Operations are
/// attributed to actors, and actor ordering is used for deterministic
/// tie-breaking during merge. Lexicographic ordering on raw bytes.
struct ActorId {
    static constexpr std::size_t size = 16;  ///< Fixed size in bytes.
    std::array<std::byte, size> bytes{};     ///< Raw identifier bytes.

    constexpr ActorId() = default;

    /// Construct from a byte array.
    explicit constexpr ActorId(std::array<std::byte, size> b) : bytes{b} {}

    /// Construct from a raw uint8_t array (convenience for tests).
    explicit ActorId(const std::uint8_t (&raw)[size]) {
        std::ranges::transform(raw, bytes.begin(),
            [](std::uint8_t b) { return std::byte{b}; });
    }

    auto operator<=>(const ActorId&) const = default;
    auto operator==(const ActorId&) const -> bool = default;

    /// Check if all bytes are zero.
    auto is_zero() const -> bool {
        return std::ranges::all_of(bytes, [](std::byte b) {
            return b == std::byte{0};
        });
    }
};

/// A 32-byte SHA-256 content hash identifying a change.
///
/// Changes are content-addressed: the hash is computed over the
/// serialized change body. This forms the basis of the change DAG
/// and deduplication during sync.
struct ChangeHash {
    static constexpr std::size_t size = 32;  ///< Fixed size in bytes.
    std::array<std::byte, size> bytes{};     ///< Raw hash bytes.

    constexpr ChangeHash() = default;

    /// Construct from a byte array.
    explicit constexpr ChangeHash(std::array<std::byte, size> b) : bytes{b} {}

    /// Construct from a raw uint8_t array.
    explicit ChangeHash(const std::uint8_t (&raw)[size]) {
        std::ranges::transform(raw, bytes.begin(),
            [](std::uint8_t b) { return std::byte{b}; });
    }

    auto operator<=>(const ChangeHash&) const = default;
    auto operator==(const ChangeHash&) const -> bool = default;

    /// Check if all bytes are zero.
    auto is_zero() const -> bool {
        return std::ranges::all_of(bytes, [](std::byte b) {
            return b == std::byte{0};
        });
    }
};

/// Identifies a single operation: (counter, actor).
///
/// OpIds are globally unique and totally ordered. The counter increases
/// monotonically per actor; ties are broken by actor identity.
struct OpId {
    std::uint64_t counter{0};  ///< Monotonically increasing counter per actor.
    ActorId actor{};           ///< The actor that created this operation.

    constexpr OpId() = default;

    /// Construct with a counter and actor.
    constexpr OpId(std::uint64_t c, ActorId a) : counter{c}, actor{a} {}

    auto operator<=>(const OpId&) const = default;
    auto operator==(const OpId&) const -> bool = default;
};

/// Sentinel type representing the document root object.
struct Root {
    auto operator<=>(const Root&) const = default;
    auto operator==(const Root&) const -> bool = default;
};

/// Identifies a CRDT object in the document tree.
///
/// Either the root sentinel or the OpId that created the object.
/// The root is always a Map and always exists.
struct ObjId {
    std::variant<Root, OpId> inner;  ///< Root or the creating OpId.

    /// Default-constructs to the root object.
    constexpr ObjId() : inner{Root{}} {}

    /// Construct from the OpId that created this object.
    explicit constexpr ObjId(OpId id) : inner{id} {}

    /// Check if this is the root object.
    auto is_root() const -> bool {
        return std::holds_alternative<Root>(inner);
    }

    auto operator<=>(const ObjId&) const = default;
    auto operator==(const ObjId&) const -> bool = default;
};

/// The root object -- always a Map, always exists.
inline constexpr auto root = ObjId{};

/// A key into a map (string) or an index into a list/text (size_t).
using Prop = std::variant<std::string, std::size_t>;

/// Create a map key Prop from a string.
inline auto map_key(std::string key) -> Prop { return Prop{std::move(key)}; }

/// Create a list index Prop from an index.
inline auto list_index(std::size_t idx) -> Prop { return Prop{idx}; }

}  // namespace automerge_cpp

// -- std::hash specializations ------------------------------------------------

/// @cond HASH_SPECIALIZATIONS

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

/// @endcond
