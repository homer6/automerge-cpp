// parallel_perf_demo — monoid-powered parallelism across documents
//
// Demonstrates: CRDT merge is a monoid (associative, commutative,
// idempotent with an empty Document as identity). This means we can
// fork N copies, mutate in parallel, and merge back — getting the
// same result as sequential execution.
//
// All parallelism uses std::jthread — no external dependencies.
//
// Build: cmake --build build -DCMAKE_BUILD_TYPE=Release
// Run:   ./build/examples/parallel_perf_demo

#include <automerge-cpp/automerge.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace am = automerge_cpp;

struct Timer {
    std::chrono::steady_clock::time_point start;
    Timer() : start{std::chrono::steady_clock::now()} {}
    auto elapsed_ms() const -> double {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(now - start).count();
    }
};

int main() {
    auto hw = std::thread::hardware_concurrency();
    std::printf("Hardware threads: %u\n", hw);

    // =========================================================================
    // Fork/merge batch put — the core parallelism pattern
    //
    // Because merge is a monoid, fork N → mutate in parallel → merge back
    // produces the same result as sequential execution.
    // =========================================================================
    std::printf("\n=== Fork/merge batch put ===\n");

    auto doc = am::Document{};
    doc.transact([](auto& tx) {
        tx.put(am::root, "base", std::string{"exists"});
    });

    constexpr int num_forks = 8;
    constexpr int keys_per_fork = 500;

    // Sequential baseline
    {
        auto seq_doc = am::Document{};
        auto t = Timer{};
        seq_doc.transact([](auto& tx) {
            for (int i = 0; i < num_forks * keys_per_fork; ++i) {
                tx.put(am::root, "k" + std::to_string(i), std::int64_t{i});
            }
        });
        std::printf("Sequential %d puts: %.1f ms\n",
                    num_forks * keys_per_fork, t.elapsed_ms());
    }

    // Parallel: fork, mutate on threads, merge back
    {
        auto t = Timer{};
        auto forks = std::vector<am::Document>{};
        forks.reserve(num_forks);
        for (int f = 0; f < num_forks; ++f) {
            forks.push_back(doc.fork());
        }

        auto threads = std::vector<std::jthread>{};
        for (int f = 0; f < num_forks; ++f) {
            threads.emplace_back([&forks, f]() {
                forks[f].transact([f](auto& tx) {
                    for (int i = 0; i < keys_per_fork; ++i) {
                        auto key = "k" + std::to_string(f * keys_per_fork + i);
                        tx.put(am::root, key, std::int64_t{f * keys_per_fork + i});
                    }
                });
            });
        }
        threads.clear();  // join all

        for (auto& f : forks) {
            doc.merge(f);
        }

        std::printf("Parallel fork/merge %d puts (%d forks x %d): %.1f ms, %zu keys\n",
                    num_forks * keys_per_fork, num_forks, keys_per_fork,
                    t.elapsed_ms(), doc.length(am::root));
    }

    // =========================================================================
    // Parallel document creation and save
    // =========================================================================
    std::printf("\n=== Parallel create + save: 1000 documents ===\n");

    constexpr int doc_count = 1000;
    auto docs = std::vector<am::Document>(doc_count);

    // Create documents in parallel
    {
        auto t = Timer{};
        auto threads = std::vector<std::jthread>{};
        auto chunk = doc_count / static_cast<int>(hw);
        for (unsigned int w = 0; w < hw; ++w) {
            auto begin = static_cast<int>(w) * chunk;
            auto end = (w == hw - 1) ? doc_count : begin + chunk;
            threads.emplace_back([&docs, begin, end]() {
                for (int i = begin; i < end; ++i) {
                    docs[i] = am::Document{1u};  // sequential per-doc
                    docs[i].transact([i](auto& tx) {
                        tx.put(am::root, "id", std::int64_t{i});
                        for (int k = 0; k < 50; ++k) {
                            tx.put(am::root, "field_" + std::to_string(k),
                                   std::int64_t{i * 1000 + k});
                        }
                    });
                }
            });
        }
        threads.clear();
        std::printf("Parallel create(%d docs, %u threads): %.1f ms\n",
                    doc_count, hw, t.elapsed_ms());
    }

    // Save in parallel
    auto saved = std::vector<std::vector<std::byte>>(doc_count);
    {
        auto t = Timer{};
        auto threads = std::vector<std::jthread>{};
        auto chunk = doc_count / static_cast<int>(hw);
        for (unsigned int w = 0; w < hw; ++w) {
            auto begin = static_cast<int>(w) * chunk;
            auto end = (w == hw - 1) ? doc_count : begin + chunk;
            threads.emplace_back([&docs, &saved, begin, end]() {
                for (int i = begin; i < end; ++i) {
                    saved[i] = docs[i].save();
                }
            });
        }
        threads.clear();
        auto total_bytes = std::size_t{0};
        for (const auto& s : saved) total_bytes += s.size();
        std::printf("Parallel save(%d docs, %u threads): %.1f ms, %.1f KB total\n",
                    doc_count, hw, t.elapsed_ms(),
                    static_cast<double>(total_bytes) / 1024.0);
    }

    // Sequential save for comparison
    {
        auto t = Timer{};
        for (int i = 0; i < doc_count; ++i) {
            saved[i] = docs[i].save();
        }
        std::printf("Sequential save(%d docs): %.1f ms\n", doc_count, t.elapsed_ms());
    }

    // =========================================================================
    // Parallel load
    // =========================================================================
    std::printf("\n=== Parallel load: 1000 documents ===\n");

    auto loaded = std::vector<std::optional<am::Document>>(doc_count);
    {
        auto t = Timer{};
        auto threads = std::vector<std::jthread>{};
        auto chunk = doc_count / static_cast<int>(hw);
        for (unsigned int w = 0; w < hw; ++w) {
            auto begin = static_cast<int>(w) * chunk;
            auto end = (w == hw - 1) ? doc_count : begin + chunk;
            threads.emplace_back([&saved, &loaded, begin, end]() {
                for (int i = begin; i < end; ++i) {
                    loaded[i] = am::Document::load(saved[i]);
                }
            });
        }
        threads.clear();
        auto ok = 0;
        for (const auto& d : loaded) { if (d.has_value()) ++ok; }
        std::printf("Parallel load(%d docs, %u threads): %.1f ms, %d/%d ok\n",
                    doc_count, hw, t.elapsed_ms(), ok, doc_count);
    }

    // =========================================================================
    // Monoid reduce — merge 100 peer documents
    //
    // CRDT merge is a monoid:
    //   - Binary op:  merge(a, b)
    //   - Identity:   empty Document{}
    //   - Associative: merge(merge(a, b), c) == merge(a, merge(b, c))
    //   - Commutative: merge(a, b) == merge(b, a)
    //   - Idempotent:  merge(a, a) == a
    // =========================================================================
    std::printf("\n=== Monoid reduce: merge 100 peers ===\n");

    constexpr int peer_count = 100;
    auto peers = std::vector<am::Document>(peer_count);

    // Create peers in parallel
    {
        auto threads = std::vector<std::jthread>{};
        for (int p = 0; p < peer_count; ++p) {
            threads.emplace_back([&peers, p]() {
                peers[p] = am::Document{1u};
                peers[p].transact([p](auto& tx) {
                    for (int k = 0; k < 10; ++k) {
                        tx.put(am::root,
                               "peer" + std::to_string(p) + "_k" + std::to_string(k),
                               std::int64_t{p * 100 + k});
                    }
                });
            });
        }
        threads.clear();
    }

    // Sequential reduce
    {
        auto t = Timer{};
        auto merged = am::Document{1u};
        for (const auto& peer : peers) {
            merged.merge(peer);
        }
        std::printf("Sequential merge(%d peers): %.1f ms, %zu keys\n",
                    peer_count, t.elapsed_ms(), merged.length(am::root));
    }

    // Parallel tree reduce: merge pairs in parallel, then pairs of pairs, etc.
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

        // Tree reduce
        while (work.size() > 1) {
            auto next = std::vector<am::Document>{};
            auto threads = std::vector<std::jthread>{};
            auto pairs = work.size() / 2;
            next.resize(pairs + (work.size() % 2));

            for (std::size_t i = 0; i < pairs; ++i) {
                threads.emplace_back([&work, &next, i]() {
                    work[i * 2].merge(work[i * 2 + 1]);
                    next[i] = std::move(work[i * 2]);
                });
            }
            if (work.size() % 2 == 1) {
                next[pairs] = std::move(work.back());
            }
            threads.clear();
            work = std::move(next);
        }

        std::printf("Parallel tree merge(%d peers): %.1f ms, %zu keys\n",
                    peer_count, t.elapsed_ms(), work[0].length(am::root));
    }

    // =========================================================================
    // Parallel sync — each pair syncs independently
    // =========================================================================
    std::printf("\n=== Parallel sync: 100 pairs ===\n");

    constexpr int sync_pairs = 100;
    auto sources = std::vector<am::Document>(sync_pairs);
    auto targets = std::vector<am::Document>(sync_pairs);

    // Set up source documents in parallel
    {
        auto threads = std::vector<std::jthread>{};
        for (int i = 0; i < sync_pairs; ++i) {
            threads.emplace_back([&sources, i]() {
                sources[i] = am::Document{1u};
                sources[i].transact([i](auto& tx) {
                    for (int k = 0; k < 20; ++k) {
                        tx.put(am::root, "k_" + std::to_string(k),
                               std::int64_t{i * 100 + k});
                    }
                });
            });
        }
        threads.clear();
    }

    // Sync all pairs in parallel
    {
        auto t = Timer{};
        auto threads = std::vector<std::jthread>{};
        for (int i = 0; i < sync_pairs; ++i) {
            threads.emplace_back([&sources, &targets, i]() {
                targets[i] = am::Document{1u};
                auto state_src = am::SyncState{};
                auto state_tgt = am::SyncState{};
                for (int round = 0; round < 10; ++round) {
                    bool progress = false;
                    if (auto msg = sources[i].generate_sync_message(state_src)) {
                        targets[i].receive_sync_message(state_tgt, *msg);
                        progress = true;
                    }
                    if (auto msg = targets[i].generate_sync_message(state_tgt)) {
                        sources[i].receive_sync_message(state_src, *msg);
                        progress = true;
                    }
                    if (!progress) break;
                }
            });
        }
        threads.clear();
        std::printf("Parallel sync(%d pairs): %.1f ms\n", sync_pairs, t.elapsed_ms());
    }

    auto sync_ok = true;
    for (int i = 0; i < sync_pairs; ++i) {
        if (targets[i].length(am::root) != 20) { sync_ok = false; break; }
    }
    std::printf("All syncs correct: %s\n", sync_ok ? "yes" : "NO");

    std::printf("\nDone.\n");
    return 0;
}
