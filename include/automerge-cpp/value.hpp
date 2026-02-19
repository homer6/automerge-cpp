/// @file value.hpp
/// @brief Value types: ScalarValue, Value, ObjType, and tag types.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
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

// -- Initializer list helpers -------------------------------------------------

/// Initializer for creating populated lists via `put` or `insert`.
///
/// @code
/// auto items = tx.put(root, "items", List{"Milk", "Eggs", "Bread"});
/// auto empty = tx.put(root, "data", List{});
/// @endcode
struct List {
    std::vector<ScalarValue> values;
    List() = default;
    List(std::initializer_list<ScalarValue> v) : values(v) {}
};

/// Initializer for creating populated maps via `put` or `insert`.
///
/// @code
/// auto config = tx.put(root, "config", Map{{"port", 8080}, {"host", "localhost"}});
/// auto empty = tx.put(root, "data", Map{});
/// @endcode
struct Map {
    std::vector<std::pair<std::string, ScalarValue>> entries;
    Map() = default;
    Map(std::initializer_list<std::pair<std::string_view, ScalarValue>> e) {
        entries.reserve(e.size());
        for (const auto& [k, v] : e) entries.emplace_back(std::string{k}, v);
    }
};

// -- Variant visitor helper ---------------------------------------------------

/// Helper for constructing ad-hoc visitors from lambdas.
///
/// @code
/// std::visit(overload{
///     [](std::string s) { printf("%s\n", s.c_str()); },
///     [](std::int64_t i) { printf("%lld\n", i); },
///     [](auto&&) { printf("other\n"); },
/// }, some_variant);
/// @endcode
template <typename... Ts>
struct overload : Ts... { using Ts::operator()...; };

template <typename... Ts>
overload(Ts...) -> overload<Ts...>;

// -- Typed scalar extraction helpers ------------------------------------------

/// Extract a typed scalar from a Value, or nullopt on type mismatch.
/// @code
/// auto name = get_scalar<std::string>(value);
/// @endcode
template <typename T>
auto get_scalar(const Value& v) -> std::optional<T> {
    if (const auto* sv = std::get_if<ScalarValue>(&v)) {
        if (const auto* t = std::get_if<T>(sv)) {
            return *t;
        }
    }
    return std::nullopt;
}

/// Extract a typed scalar from an optional<Value>.
template <typename T>
auto get_scalar(const std::optional<Value>& v) -> std::optional<T> {
    if (!v) return std::nullopt;
    return get_scalar<T>(*v);
}


}  // namespace automerge_cpp
