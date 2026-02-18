/// @file op.hpp
/// @brief Operation types for the CRDT log.

#pragma once

#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace automerge_cpp {

/// The kind of mutation an operation represents.
enum class OpType : std::uint8_t {
    put,           ///< Set a value at a key or index.
    del,           ///< Delete a key or index.
    insert,        ///< Insert into a sequence (list or text).
    make_object,   ///< Create a nested object (map, list, text, table).
    increment,     ///< Increment a counter value.
    splice_text,   ///< Splice text content (insert/delete characters).
    mark,          ///< Apply a rich-text mark annotation.
};

/// Convert an OpType to its string representation.
constexpr auto to_string_view(OpType type) noexcept -> std::string_view {
    switch (type) {
        case OpType::put:         return "put";
        case OpType::del:         return "del";
        case OpType::insert:      return "insert";
        case OpType::make_object: return "make_object";
        case OpType::increment:   return "increment";
        case OpType::splice_text: return "splice_text";
        case OpType::mark:        return "mark";
    }
    return "unknown";
}

/// A single operation in the CRDT op log.
///
/// Operations are the fundamental unit of change in Automerge. Each
/// operation has a globally unique OpId, targets an object and property,
/// and carries a value. Operations are immutable once created.
struct Op {
    OpId id;                                 ///< Globally unique operation identifier.
    ObjId obj;                               ///< The object this operation targets.
    Prop key;                                ///< The property (map key or list index).
    OpType action;                           ///< The type of mutation.
    Value value;                             ///< The value being set/inserted.
    std::vector<OpId> pred;                  ///< Predecessor ops (for conflict tracking).
    std::optional<OpId> insert_after{};      ///< For insert/splice: the element to insert after.

    auto operator==(const Op&) const -> bool = default;
};

}  // namespace automerge_cpp
