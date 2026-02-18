#pragma once

#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace automerge_cpp {

// A path element: either a map key or a list index.
using PathElement = std::variant<std::string, std::size_t>;

// A path into the document tree (e.g. root / "config" / "items" / 0).
using Path = std::vector<PathElement>;

// Actions that can appear in a patch.

struct PatchPut {
    Value value;
    bool conflict{false};  // true if this creates or resolves a conflict
    auto operator==(const PatchPut&) const -> bool = default;
};

struct PatchInsert {
    std::size_t index;
    Value value;
    auto operator==(const PatchInsert&) const -> bool = default;
};

struct PatchDelete {
    std::size_t index;
    std::size_t count{1};
    auto operator==(const PatchDelete&) const -> bool = default;
};

struct PatchIncrement {
    std::int64_t delta;
    auto operator==(const PatchIncrement&) const -> bool = default;
};

struct PatchSpliceText {
    std::size_t index;
    std::size_t delete_count;
    std::string text;
    auto operator==(const PatchSpliceText&) const -> bool = default;
};

using PatchAction = std::variant<
    PatchPut,
    PatchInsert,
    PatchDelete,
    PatchIncrement,
    PatchSpliceText
>;

// A single patch describing one atomic change to the document.
struct Patch {
    ObjId obj;            // the object modified
    Prop key;             // the property/index modified
    PatchAction action;   // what happened

    auto operator==(const Patch&) const -> bool = default;
};

}  // namespace automerge_cpp
