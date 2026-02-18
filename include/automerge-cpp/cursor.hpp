/// @file cursor.hpp
/// @brief Cursor type for stable position tracking in lists and text.

#pragma once

#include <automerge-cpp/types.hpp>

#include <cstddef>
#include <optional>

namespace automerge_cpp {

/// A stable position in a list or text object.
///
/// Cursors survive insertions, deletions, and merges because they are
/// backed by the OpId of the element at the cursor position (identity-based
/// tracking rather than index-based).
///
/// Create a cursor with Document::cursor() and resolve it back to an
/// index with Document::resolve_cursor().
struct Cursor {
    OpId position;  ///< The OpId of the element this cursor points to.

    auto operator==(const Cursor&) const -> bool = default;
    auto operator<=>(const Cursor&) const = default;
};

}  // namespace automerge_cpp
