#pragma once

#include <automerge-cpp/op.hpp>
#include <automerge-cpp/types.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace automerge_cpp {

// -- Change -------------------------------------------------------------------
// A group of operations applied atomically by a single actor.
// Changes form a DAG via their deps (dependency hashes).

struct Change {
    ActorId actor;
    std::uint64_t seq{0};
    std::uint64_t start_op{0};
    std::int64_t timestamp{0};
    std::optional<std::string> message;
    std::vector<ChangeHash> deps;
    std::vector<Op> operations;

    auto operator==(const Change&) const -> bool = default;
};

}  // namespace automerge_cpp
