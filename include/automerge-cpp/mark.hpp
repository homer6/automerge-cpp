/// @file mark.hpp
/// @brief Mark type for rich text annotations.

#pragma once

#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace automerge_cpp {

/// A rich-text mark spanning a range of a text or list object.
///
/// Marks annotate ranges with named attributes (e.g. "bold", "italic",
/// "link"). They survive insertions, deletions, and merges because they
/// are anchored to element OpIds internally, not indices.
///
/// Apply marks with Transaction::mark() and query them with
/// Document::marks() or Document::marks_at().
struct Mark {
    std::size_t start;   ///< Start index (inclusive).
    std::size_t end;     ///< End index (exclusive).
    std::string name;    ///< The mark name (e.g. "bold", "italic", "link").
    ScalarValue value;   ///< The mark value (e.g. true, "https://...").

    auto operator==(const Mark&) const -> bool = default;
};

}  // namespace automerge_cpp
