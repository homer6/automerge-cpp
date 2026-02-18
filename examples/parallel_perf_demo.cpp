// parallel_perf_demo — monoid-powered parallelism
//
// CRDT merge is a monoid (associative, commutative, idempotent).
// This means we can fork N copies, mutate in parallel, and merge back —
// the result is identical to sequential execution.
//
// Uses Document's built-in thread pool (BS::thread_pool) for all
// parallelism via pool->parallelize_loop().
//
// Build: cmake --build build -DCMAKE_BUILD_TYPE=Release
// Run:   ./build/examples/parallel_perf_demo

#include <automerge-cpp/automerge.hpp>
#include <automerge-cpp/thread_pool.hpp>

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
    // All documents share one pool — no extra threads are ever created.
    auto pool = std::make_shared<am::thread_pool>(
        std::thread::hardware_concurrency());
    pool->sleep_duration = 0;  // yield instead of 500us sleep

    std::printf("Thread pool: %lu threads\n",
                static_cast<unsigned long>(pool->get_thread_count()));

    // =========================================================================
    // 1. Fork/merge batch put — the core parallelism pattern
    //
    //    fork N → mutate in parallel → merge back
    //    Same result as sequential because merge is a monoid.
    // =========================================================================
    std::printf("\n=== Fork/merge batch put ===\n");

    constexpr int keys_per_fork = 500;

    // Sequential baseline
    {
        auto doc = am::Document{1u};  // single-threaded, no pool
        auto num_forks = static_cast<int>(pool->get_thread_count());
        auto total_keys = num_forks * keys_per_fork;
        auto t = Timer{};
        doc.transact([total_keys](auto& tx) {
            for (int i = 0; i < total_keys; ++i)
                tx.put(am::root, "k" + std::to_string(i), std::int64_t{i});
        });
        std::printf("  Sequential %d puts: %.1f ms\n", total_keys, t.ms());
    }

    // Parallel: fork, mutate via pool, merge
    {
        auto doc = am::Document{pool};
        auto num_forks = static_cast<int>(pool->get_thread_count());
        auto total_keys = num_forks * keys_per_fork;

        auto t = Timer{};

        auto forks = std::vector<am::Document>{};
        forks.reserve(num_forks);
        for (int f = 0; f < num_forks; ++f)
            forks.push_back(doc.fork());

        pool->parallelize_loop(0, num_forks, [&](int start, int end) {
            for (int f = start; f < end; ++f) {
                forks[f].transact([f, keys_per_fork](auto& tx) {
                    for (int i = 0; i < keys_per_fork; ++i)
                        tx.put(am::root, "k" + std::to_string(f * keys_per_fork + i),
                               std::int64_t{f * keys_per_fork + i});
                });
            }
        });

        for (auto& f : forks) doc.merge(f);

        std::printf("  Parallel %d puts (%d forks): %.1f ms, %zu keys\n",
                    total_keys, num_forks, t.ms(), doc.length(am::root));
    }

    // =========================================================================
    // 2. Parallel save/load of independent documents
    // =========================================================================
    std::printf("\n=== Parallel save/load: 500 documents ===\n");

    constexpr int doc_count = 500;

    // Create docs
    auto docs = std::vector<am::Document>(doc_count);
    for (int i = 0; i < doc_count; ++i) {
        docs[i] = am::Document{1u};
        docs[i].transact([i](auto& tx) {
            for (int k = 0; k < 50; ++k)
                tx.put(am::root, "f" + std::to_string(k),
                       std::int64_t{i * 1000 + k});
        });
    }

    // Parallel save via pool
    auto saved = std::vector<std::vector<std::byte>>(doc_count);
    {
        auto t = Timer{};
        pool->parallelize_loop(0, doc_count, [&](int start, int end) {
            for (int i = start; i < end; ++i)
                saved[i] = docs[i].save();
        });
        std::printf("  Parallel save: %.1f ms\n", t.ms());
    }

    // Parallel load via pool
    {
        auto loaded = std::vector<std::optional<am::Document>>(doc_count);
        auto t = Timer{};
        pool->parallelize_loop(0, doc_count, [&](int start, int end) {
            for (int i = start; i < end; ++i)
                loaded[i] = am::Document::load(saved[i]);
        });
        auto ok = 0;
        for (const auto& d : loaded) ok += d.has_value();
        std::printf("  Parallel load: %.1f ms (%d/%d ok)\n", t.ms(), ok, doc_count);
    }

    // =========================================================================
    // 3. Lock-free parallel reads
    //
    //    set_read_locking(false) eliminates shared_mutex contention.
    //    pool->parallelize_loop fans reads across all cores.
    // =========================================================================
    std::printf("\n=== Lock-free parallel reads ===\n");
    {
        auto doc = am::Document{pool};
        constexpr int num_keys = 10000;

        doc.transact([](auto& tx) {
            for (int i = 0; i < num_keys; ++i)
                tx.put(am::root, "k" + std::to_string(i), std::int64_t{i});
        });

        // Sequential baseline
        {
            auto t = Timer{};
            for (int i = 0; i < num_keys; ++i) {
                auto v = doc.get(am::root, "k" + std::to_string(i));
                (void)v;
            }
            std::printf("  Sequential %d gets: %.1f ms\n", num_keys, t.ms());
        }

        // Parallel with lock-free reads
        {
            doc.set_read_locking(false);
            auto t = Timer{};
            pool->parallelize_loop(0, num_keys, [&](int start, int end) {
                for (int i = start; i < end; ++i) {
                    auto v = doc.get(am::root, "k" + std::to_string(i));
                    (void)v;
                }
            });
            doc.set_read_locking(true);
            std::printf("  Parallel %d lock-free gets: %.1f ms\n", num_keys, t.ms());
        }
    }

    // =========================================================================
    // 4. Tree reduce — parallel merge of 100 peers
    //
    //    Merge is associative, so we can merge pairs in parallel at each
    //    level of the tree: O(log N) rounds instead of O(N) sequential.
    // =========================================================================
    std::printf("\n=== Tree reduce: merge 100 peers ===\n");

    constexpr int peer_count = 100;
    auto peers = std::vector<am::Document>(peer_count);

    // Create peers in parallel
    pool->parallelize_loop(0, peer_count, [&](int start, int end) {
        for (int p = start; p < end; ++p) {
            peers[p] = am::Document{1u};
            peers[p].transact([p](auto& tx) {
                for (int k = 0; k < 10; ++k)
                    tx.put(am::root,
                           "p" + std::to_string(p) + "_" + std::to_string(k),
                           std::int64_t{p * 100 + k});
            });
        }
    });

    // Sequential reduce
    {
        auto t = Timer{};
        auto result = am::Document{1u};
        for (const auto& peer : peers) result.merge(peer);
        std::printf("  Sequential merge: %.1f ms, %zu keys\n",
                    t.ms(), result.length(am::root));
    }

    // Parallel tree reduce via pool
    {
        auto t = Timer{};

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

            pool->parallelize_loop(std::size_t{0}, pairs, [&](std::size_t start, std::size_t end) {
                for (auto i = start; i < end; ++i) {
                    work[i * 2].merge(work[i * 2 + 1]);
                    next[i] = std::move(work[i * 2]);
                }
            });

            if (work.size() % 2 == 1) next[pairs] = std::move(work.back());
            work = std::move(next);
        }

        std::printf("  Tree reduce merge: %.1f ms, %zu keys\n",
                    t.ms(), work[0].length(am::root));
    }

    // =========================================================================
    // 5. Parallel sync — each pair syncs independently
    // =========================================================================
    std::printf("\n=== Parallel sync: 50 pairs ===\n");

    constexpr int sync_pairs = 50;
    auto sources = std::vector<am::Document>(sync_pairs);
    auto targets = std::vector<am::Document>(sync_pairs);

    // Create sources via pool
    pool->parallelize_loop(0, sync_pairs, [&](int start, int end) {
        for (int i = start; i < end; ++i) {
            sources[i] = am::Document{1u};
            sources[i].transact([i](auto& tx) {
                for (int k = 0; k < 20; ++k)
                    tx.put(am::root, "k" + std::to_string(k),
                           std::int64_t{i * 100 + k});
            });
        }
    });

    // Sync all pairs via pool
    {
        auto t = Timer{};
        pool->parallelize_loop(0, sync_pairs, [&](int start, int end) {
            for (int i = start; i < end; ++i) {
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
            }
        });

        auto ok = 0;
        for (int i = 0; i < sync_pairs; ++i) ok += (targets[i].length(am::root) == 20);
        std::printf("  Parallel sync: %.1f ms (%d/%d correct)\n", t.ms(), ok, sync_pairs);
    }

    std::printf("\nDone.\n");
    return 0;
}
