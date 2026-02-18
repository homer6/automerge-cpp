#pragma once

#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace automerge_cpp {

// A rich-text mark spanning a range of a text or list object.
// Marks survive insertions, deletions, and merges because they
// are anchored to element OpIds, not indices.
struct Mark {
    std::size_t start;      // start index (inclusive)
    std::size_t end;        // end index (exclusive)
    std::string name;       // e.g. "bold", "italic", "link"
    ScalarValue value;      // e.g. true, "https://..."

    auto operator==(const Mark&) const -> bool = default;
};

}  // namespace automerge_cpp
