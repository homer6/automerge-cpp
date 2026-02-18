/// @file value.hpp
/// @brief Value types: ScalarValue, Value, ObjType, and tag types.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace automerge_cpp {

/// Represents a JSON null value.
struct Null {
    auto operator<=>(const Null&) const = default;
    auto operator==(const Null&) const -> bool = default;
};

/// A CRDT counter that supports concurrent increment operations.
///
/// Unlike a plain integer, concurrent increments from different actors
/// are merged additively rather than by last-writer-wins.
struct Counter {
    std::int64_t value{0};  ///< The current counter value.

    auto operator<=>(const Counter&) const = default;
    auto operator==(const Counter&) const -> bool = default;
};

/// A millisecond-precision timestamp.
struct Timestamp {
    std::int64_t millis_since_epoch{0};  ///< Milliseconds since Unix epoch.

    auto operator<=>(const Timestamp&) const = default;
    auto operator==(const Timestamp&) const -> bool = default;
};

/// A byte array value.
using Bytes = std::vector<std::byte>;

/// The four kinds of CRDT container objects.
enum class ObjType : std::uint8_t {
    map,    ///< An unordered key-value map.
    list,   ///< An ordered sequence (RGA).
    text,   ///< A character sequence optimized for text editing.
    table,  ///< A keyed table (map with row semantics).
};

/// Convert an ObjType to its string representation.
constexpr auto to_string_view(ObjType type) noexcept -> std::string_view {
    switch (type) {
        case ObjType::map:   return "map";
        case ObjType::list:  return "list";
        case ObjType::text:  return "text";
        case ObjType::table: return "table";
    }
    return "unknown";
}

/// A closed set of primitive values stored in the document.
///
/// Alternatives: Null, bool, int64_t, uint64_t, double,
/// Counter, Timestamp, string, Bytes.
using ScalarValue = std::variant<
    Null,
    bool,
    std::int64_t,
    std::uint64_t,
    double,
    Counter,
    Timestamp,
    std::string,
    Bytes
>;

/// A value in the document tree: either a nested object type or a scalar.
using Value = std::variant<ObjType, ScalarValue>;

/// Check if a Value holds a scalar (not an object type).
constexpr auto is_scalar(const Value& v) -> bool {
    return std::holds_alternative<ScalarValue>(v);
}

/// Check if a Value holds an object type (map, list, text, table).
constexpr auto is_object(const Value& v) -> bool {
    return std::holds_alternative<ObjType>(v);
}

}  // namespace automerge_cpp
