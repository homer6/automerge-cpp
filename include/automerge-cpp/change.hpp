/// @file change.hpp
/// @brief Change type: an atomic group of operations.

#pragma once

#include <automerge-cpp/op.hpp>
#include <automerge-cpp/types.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace automerge_cpp {

/// A group of operations applied atomically by a single actor.
///
/// Changes are the unit of replication in Automerge. Each change
/// records its author (actor), sequence number, timestamp, and
/// the operations it contains. Changes form a DAG via their
/// dependency hashes (deps), enabling causal ordering.
struct Change {
    ActorId actor;                       ///< The actor that authored this change.
    std::uint64_t seq{0};                ///< Sequence number (per-actor, 1-based).
    std::uint64_t start_op{0};           ///< Counter of the first operation in this change.
    std::int64_t timestamp{0};           ///< Unix timestamp in milliseconds.
    std::optional<std::string> message;  ///< Optional human-readable commit message.
    std::vector<ChangeHash> deps;        ///< Hashes of the changes this depends on.
    std::vector<Op> operations;          ///< The operations in this change.

    auto operator==(const Change&) const -> bool = default;
};

}  // namespace automerge_cpp
