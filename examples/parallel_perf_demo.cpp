// parallel_perf_demo — monoid-powered parallelism
//
// CRDT merge is a monoid (associative, commutative, idempotent).
// This means we can fork N copies, mutate in parallel, and merge back —
// the result is identical to sequential execution.
//
// Build: cmake --build build -DCMAKE_BUILD_TYPE=Release
// Run:   ./build/examples/parallel_perf_demo

#include <automerge-cpp/automerge.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace am = automerge_cpp;

struct Timer {
    std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
    auto ms() const -> double {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
    }
};

int main() {
    auto hw = std::thread::hardware_concurrency();
    std::printf("Hardware threads: %u\n", hw);

    // =========================================================================
    // 1. Fork/merge batch put — the core parallelism pattern
    //
    //    fork N → mutate in parallel → merge back
    //    Same result as sequential because merge is a monoid.
    // =========================================================================
    std::printf("\n=== Fork/merge batch put ===\n");

    constexpr int num_forks = 8;
    constexpr int keys_per_fork = 500;
    constexpr int total_keys = num_forks * keys_per_fork;

    // Sequential baseline
    {
        auto doc = am::Document{1u};
        auto t = Timer{};
        doc.transact([](auto& tx) {
            for (int i = 0; i < total_keys; ++i)
                tx.put(am::root, "k" + std::to_string(i), std::int64_t{i});
        });
        std::printf("  Sequential %d puts: %.1f ms\n", total_keys, t.ms());
    }

    // Parallel: fork, mutate on threads, merge
    auto doc = am::Document{1u};
    {
        auto t = Timer{};

        auto forks = std::vector<am::Document>{};
        forks.reserve(num_forks);
        for (int f = 0; f < num_forks; ++f)
            forks.push_back(doc.fork());

        auto threads = std::vector<std::thread>{};
        for (int f = 0; f < num_forks; ++f) {
            threads.emplace_back([&forks, f]() {
                forks[f].transact([f](auto& tx) {
                    for (int i = 0; i < keys_per_fork; ++i)
                        tx.put(am::root, "k" + std::to_string(f * keys_per_fork + i),
                               std::int64_t{f * keys_per_fork + i});
                });
            });
        }
        for (auto& t : threads) t.join();

        for (auto& f : forks) doc.merge(f);

        std::printf("  Parallel %d puts (%d forks): %.1f ms, %zu keys\n",
                    total_keys, num_forks, t.ms(), doc.length(am::root));
    }

    // =========================================================================
    // 2. Parallel save/load of independent documents
    // =========================================================================
    std::printf("\n=== Parallel save/load: 500 documents ===\n");

    constexpr int doc_count = 500;
    auto docs = std::vector<am::Document>(doc_count);
    auto saved = std::vector<std::vector<std::byte>>(doc_count);

    // Create docs
    {
        auto threads = std::vector<std::thread>{};
        for (int i = 0; i < doc_count; ++i) {
            threads.emplace_back([&docs, i]() {
                docs[i] = am::Document{1u};
                docs[i].transact([i](auto& tx) {
                    for (int k = 0; k < 50; ++k)
                        tx.put(am::root, "f" + std::to_string(k),
                               std::int64_t{i * 1000 + k});
                });
            });
        }
        for (auto& t : threads) t.join();
    }

    // Parallel save
    {
        auto t = Timer{};
        auto threads = std::vector<std::thread>{};
        for (int i = 0; i < doc_count; ++i) {
            threads.emplace_back([&docs, &saved, i]() {
                saved[i] = docs[i].save();
            });
        }
        for (auto& t : threads) t.join();
        std::printf("  Parallel save: %.1f ms\n", t.ms());
    }

    // Parallel load
    {
        auto loaded = std::vector<std::optional<am::Document>>(doc_count);
        auto t = Timer{};
        auto threads = std::vector<std::thread>{};
        for (int i = 0; i < doc_count; ++i) {
            threads.emplace_back([&saved, &loaded, i]() {
                loaded[i] = am::Document::load(saved[i]);
            });
        }
        for (auto& t : threads) t.join();
        auto ok = 0;
        for (const auto& d : loaded) ok += d.has_value();
        std::printf("  Parallel load: %.1f ms (%d/%d ok)\n", t.ms(), ok, doc_count);
    }

    // =========================================================================
    // 3. Tree reduce — parallel merge of 100 peers
    //
    //    Merge is associative, so we can merge pairs in parallel at each
    //    level of the tree: O(log N) rounds instead of O(N) sequential.
    // =========================================================================
    std::printf("\n=== Tree reduce: merge 100 peers ===\n");

    constexpr int peer_count = 100;
    auto peers = std::vector<am::Document>(peer_count);

    // Create peers in parallel
    {
        auto threads = std::vector<std::thread>{};
        for (int p = 0; p < peer_count; ++p) {
            threads.emplace_back([&peers, p]() {
                peers[p] = am::Document{1u};
                peers[p].transact([p](auto& tx) {
                    for (int k = 0; k < 10; ++k)
                        tx.put(am::root,
                               "p" + std::to_string(p) + "_" + std::to_string(k),
                               std::int64_t{p * 100 + k});
                });
            });
        }
        for (auto& t : threads) t.join();
    }

    // Sequential reduce
    {
        auto t = Timer{};
        auto result = am::Document{1u};
        for (const auto& peer : peers) result.merge(peer);
        std::printf("  Sequential merge: %.1f ms, %zu keys\n",
                    t.ms(), result.length(am::root));
    }

    // Parallel tree reduce
    {
        auto t = Timer{};

        // Copy peers into work vector
        auto work = std::vector<am::Document>{};
        work.reserve(peer_count);
        for (const auto& p : peers) {
            auto copy = am::Document{1u};
            copy.merge(p);
            work.push_back(std::move(copy));
        }

        while (work.size() > 1) {
            auto next = std::vector<am::Document>{};
            auto pairs = work.size() / 2;
            next.resize(pairs + (work.size() % 2));

            auto threads = std::vector<std::thread>{};
            for (std::size_t i = 0; i < pairs; ++i) {
                threads.emplace_back([&work, &next, i]() {
                    work[i * 2].merge(work[i * 2 + 1]);
                    next[i] = std::move(work[i * 2]);
                });
            }
            if (work.size() % 2 == 1) next[pairs] = std::move(work.back());

            for (auto& t : threads) t.join();
            work = std::move(next);
        }

        std::printf("  Tree reduce merge: %.1f ms, %zu keys\n",
                    t.ms(), work[0].length(am::root));
    }

    // =========================================================================
    // 4. Parallel sync — each pair syncs independently
    // =========================================================================
    std::printf("\n=== Parallel sync: 50 pairs ===\n");

    constexpr int sync_pairs = 50;
    auto sources = std::vector<am::Document>(sync_pairs);
    auto targets = std::vector<am::Document>(sync_pairs);

    // Create sources
    {
        auto threads = std::vector<std::thread>{};
        for (int i = 0; i < sync_pairs; ++i) {
            threads.emplace_back([&sources, i]() {
                sources[i] = am::Document{1u};
                sources[i].transact([i](auto& tx) {
                    for (int k = 0; k < 20; ++k)
                        tx.put(am::root, "k" + std::to_string(k),
                               std::int64_t{i * 100 + k});
                });
            });
        }
        for (auto& t : threads) t.join();
    }

    // Sync all pairs in parallel
    {
        auto t = Timer{};
        auto threads = std::vector<std::thread>{};
        for (int i = 0; i < sync_pairs; ++i) {
            threads.emplace_back([&sources, &targets, i]() {
                targets[i] = am::Document{1u};
                auto ss = am::SyncState{};
                auto ts = am::SyncState{};
                for (int round = 0; round < 10; ++round) {
                    auto progress = false;
                    if (auto m = sources[i].generate_sync_message(ss)) {
                        targets[i].receive_sync_message(ts, *m);
                        progress = true;
                    }
                    if (auto m = targets[i].generate_sync_message(ts)) {
                        sources[i].receive_sync_message(ss, *m);
                        progress = true;
                    }
                    if (!progress) break;
                }
            });
        }
        for (auto& t : threads) t.join();

        auto ok = 0;
        for (int i = 0; i < sync_pairs; ++i) ok += (targets[i].length(am::root) == 20);
        std::printf("  Parallel sync: %.1f ms (%d/%d correct)\n", t.ms(), ok, sync_pairs);
    }

    std::printf("\nDone.\n");
    return 0;
}
