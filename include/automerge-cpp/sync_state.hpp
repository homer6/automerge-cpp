/// @file sync_state.hpp
/// @brief Sync protocol types: SyncState, SyncMessage, Have.

#pragma once

#include <automerge-cpp/change.hpp>
#include <automerge-cpp/types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace automerge_cpp {

/// A summary of what a peer has: a snapshot point plus a bloom filter
/// of all changes since that point.
struct Have {
    std::vector<ChangeHash> last_sync;    ///< The heads at the last sync point.
    std::vector<std::byte> bloom_bytes;   ///< Serialized bloom filter of changes since last_sync.

    auto operator==(const Have&) const -> bool = default;
};

/// A sync message exchanged between peers.
///
/// The sync protocol exchanges messages containing heads, explicit
/// needs, bloom filter summaries, and changes. See Document::generate_sync_message()
/// and Document::receive_sync_message().
struct SyncMessage {
    std::vector<ChangeHash> heads;    ///< Sender's current DAG heads.
    std::vector<ChangeHash> need;     ///< Hashes the sender explicitly needs.
    std::vector<Have> have;           ///< Bloom filter summaries of what the sender has.
    std::vector<Change> changes;      ///< Changes for the recipient to apply.

    auto operator==(const SyncMessage&) const -> bool = default;
};

/// Per-peer synchronization state machine.
///
/// Create one SyncState per peer you are synchronizing with.
/// Pass it to Document::generate_sync_message() and
/// Document::receive_sync_message() to drive the sync protocol.
///
/// SyncState can be persisted with encode()/decode() for resumable sync.
class SyncState {
public:
    SyncState() = default;

    /// The hashes which we know both peers share.
    auto shared_heads() const -> const std::vector<ChangeHash>& {
        return shared_heads_;
    }

    /// The heads we last sent to this peer.
    auto last_sent_heads() const -> const std::vector<ChangeHash>& {
        return last_sent_heads_;
    }

    /// Encode persistent state (shared_heads only) for storage.
    auto encode() const -> std::vector<std::byte>;

    /// Decode persistent state from storage.
    /// @return The decoded SyncState, or nullopt if the data is invalid.
    static auto decode(std::span<const std::byte> data) -> std::optional<SyncState>;

private:
    friend class Document;

    // What we know both sides have
    std::vector<ChangeHash> shared_heads_;

    // What we last told them our heads are
    std::vector<ChangeHash> last_sent_heads_;

    // What they last told us their heads are
    std::optional<std::vector<ChangeHash>> their_heads_;

    // Hashes they explicitly said they need
    std::optional<std::vector<ChangeHash>> their_need_;

    // Bloom filter summaries they sent us
    std::optional<std::vector<Have>> their_have_;

    // Hashes we've already sent in this session
    std::set<ChangeHash> sent_hashes_;

    // Whether there's a message in-flight (waiting for ack)
    bool in_flight_{false};

    // Whether we've sent at least one message
    bool have_responded_{false};
};

}  // namespace automerge_cpp
