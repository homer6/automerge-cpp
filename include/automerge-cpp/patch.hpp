/// @file patch.hpp
/// @brief Patch types for incremental change notifications.

#pragma once

#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace automerge_cpp {

/// A path element: either a map key or a list index.
using PathElement = std::variant<std::string, std::size_t>;

/// A path into the document tree (e.g. root / "config" / "items" / 0).
using Path = std::vector<PathElement>;

/// A value was put at a map key or list index.
struct PatchPut {
    Value value;           ///< The value that was set.
    bool conflict{false};  ///< True if this creates or resolves a conflict.
    auto operator==(const PatchPut&) const -> bool = default;
};

/// A value was inserted into a list or text.
struct PatchInsert {
    std::size_t index;  ///< The index where the value was inserted.
    Value value;        ///< The inserted value.
    auto operator==(const PatchInsert&) const -> bool = default;
};

/// One or more elements were deleted from a list or text.
struct PatchDelete {
    std::size_t index;      ///< The starting index of the deletion.
    std::size_t count{1};   ///< The number of elements deleted.
    auto operator==(const PatchDelete&) const -> bool = default;
};

/// A counter was incremented.
struct PatchIncrement {
    std::int64_t delta;  ///< The increment amount (may be negative).
    auto operator==(const PatchIncrement&) const -> bool = default;
};

/// Text was spliced (inserted and/or deleted).
struct PatchSpliceText {
    std::size_t index;         ///< The starting index of the splice.
    std::size_t delete_count;  ///< The number of characters deleted.
    std::string text;          ///< The text that was inserted.
    auto operator==(const PatchSpliceText&) const -> bool = default;
};

/// The set of possible patch actions.
using PatchAction = std::variant<
    PatchPut,
    PatchInsert,
    PatchDelete,
    PatchIncrement,
    PatchSpliceText
>;

/// A single patch describing one atomic change to the document.
///
/// Patches are produced by Document::transact_with_patches() and
/// describe the externally visible effects of a transaction.
struct Patch {
    ObjId obj;            ///< The object that was modified.
    Prop key;             ///< The property or index that was modified.
    PatchAction action;   ///< What happened.

    auto operator==(const Patch&) const -> bool = default;
};

}  // namespace automerge_cpp
