#pragma once

#include <automerge-cpp/types.hpp>

#include <cstddef>
#include <optional>

namespace automerge_cpp {

// A stable position in a list or text object.
// Survives insertions, deletions, and merges because it is
// backed by the OpId of the element at the cursor position.
struct Cursor {
    OpId position;  // the OpId of the element this cursor points to

    auto operator==(const Cursor&) const -> bool = default;
    auto operator<=>(const Cursor&) const = default;
};

}  // namespace automerge_cpp
