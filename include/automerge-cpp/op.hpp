#pragma once

#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace automerge_cpp {

// -- OpType -------------------------------------------------------------------
// The kind of mutation an operation represents.

enum class OpType : std::uint8_t {
    put,           // set a value at a key/index
    del,           // delete a key/index
    insert,        // insert into a sequence
    make_object,   // create a nested object
    increment,     // increment a counter
    splice_text,   // splice text content
    mark,          // apply a rich-text mark
};

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

// -- Op -----------------------------------------------------------------------
// A single operation in the CRDT log.

struct Op {
    OpId id;
    ObjId obj;
    Prop key;
    OpType action;
    Value value;
    std::vector<OpId> pred;  // predecessor ops (for conflict tracking)

    auto operator==(const Op&) const -> bool = default;
};

}  // namespace automerge_cpp
