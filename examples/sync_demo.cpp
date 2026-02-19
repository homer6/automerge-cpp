// sync_demo — two documents syncing over a simulated network
//
// Demonstrates: SyncState, generate_sync_message, receive_sync_message,
//               typed get<T>()

#include <automerge-cpp/automerge.hpp>

#include <cstdint>
#include <cstdio>
#include <string>

namespace am = automerge_cpp;

// Simulate a full sync between two peers, counting messages exchanged.
static auto sync_peers(am::Document& a, am::Document& b) -> int {
    auto state_a = am::SyncState{};
    auto state_b = am::SyncState{};
    int messages = 0;

    for (int round = 0; round < 20; ++round) {
        bool progress = false;

        if (auto msg = a.generate_sync_message(state_a)) {
            b.receive_sync_message(state_b, *msg);
            ++messages;
            progress = true;
        }
        if (auto msg = b.generate_sync_message(state_b)) {
            a.receive_sync_message(state_a, *msg);
            ++messages;
            progress = true;
        }

        if (!progress) break;
    }

    return messages;
}

int main() {
    const std::uint8_t id_a[16] = {1};
    const std::uint8_t id_b[16] = {2};

    // --- Scenario 1: One-way sync ---
    std::printf("=== Scenario 1: One-way sync ===\n");

    auto peer_a = am::Document{};
    peer_a.set_actor_id(am::ActorId{id_a});
    peer_a.transact([](auto& tx) {
        tx.put(am::root, "name", "Alice");
        tx.put(am::root, "score", std::int64_t{100});
    });

    auto peer_b = am::Document{};
    peer_b.set_actor_id(am::ActorId{id_b});

    std::printf("Peer A has %zu keys, Peer B has %zu keys\n",
                peer_a.length(am::root), peer_b.length(am::root));

    auto msgs = sync_peers(peer_a, peer_b);
    std::printf("Synced in %d messages\n", msgs);
    std::printf("Peer B now has %zu keys\n", peer_b.length(am::root));

    // --- Scenario 2: Bidirectional sync ---
    std::printf("\n=== Scenario 2: Bidirectional sync ===\n");

    peer_a.transact([](auto& tx) {
        tx.put(am::root, "from_a", "hello from A");
    });
    peer_b.transact([](auto& tx) {
        tx.put(am::root, "from_b", "hello from B");
    });

    std::printf("Peer A keys: %zu, Peer B keys: %zu\n",
                peer_a.length(am::root), peer_b.length(am::root));

    msgs = sync_peers(peer_a, peer_b);
    std::printf("Synced in %d messages\n", msgs);
    std::printf("Peer A keys: %zu, Peer B keys: %zu\n",
                peer_a.length(am::root), peer_b.length(am::root));

    // --- Scenario 3: Three-peer transitive sync ---
    std::printf("\n=== Scenario 3: Three-peer relay ===\n");

    const std::uint8_t id_c[16] = {3};
    auto peer_c = am::Document{};
    peer_c.set_actor_id(am::ActorId{id_c});

    // A makes a change, syncs to B, B syncs to C
    peer_a.transact([](auto& tx) {
        tx.put(am::root, "relay_test", "from A via B");
    });

    sync_peers(peer_a, peer_b);
    msgs = sync_peers(peer_b, peer_c);
    std::printf("B->C synced in %d messages\n", msgs);

    if (auto val = peer_c.get<std::string>(am::root, "relay_test")) {
        std::printf("Peer C received: \"%s\"\n", val->c_str());
    }

    // --- SyncState persistence ---
    std::printf("\n=== SyncState persistence ===\n");
    auto state = am::SyncState{};
    peer_a.generate_sync_message(state);  // populate state

    auto encoded = state.encode();
    std::printf("SyncState encoded: %zu bytes\n", encoded.size());

    auto decoded = am::SyncState::decode(encoded);
    std::printf("SyncState decode: %s\n", decoded ? "success" : "failed");

    std::printf("\nDone.\n");
    return 0;
}
