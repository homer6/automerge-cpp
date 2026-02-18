// parallel_perf_demo — monoid-powered parallelism across documents
//
// Demonstrates: CRDT merge is a monoid (associative, commutative,
// idempotent with an empty Document as identity). This means
// std::reduce(std::execution::par, ...) parallelizes merge for free.
//
// All parallelism uses standard C++ execution policies — no custom
// thread pool, no wrapper class. The compiler and runtime handle the
// thread pool (TBB on Linux, GCD on macOS, ConcRT on Windows).
//
// Build: cmake --build build -DCMAKE_BUILD_TYPE=Release
// Run:   ./build/examples/parallel_perf_demo

#include <automerge-cpp/automerge.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <execution>
#include <numeric>
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
    std::printf("Hardware threads: %u\n", std::thread::hardware_concurrency());

    // =========================================================================
    // Create 1000 documents
    // =========================================================================
    std::printf("\n=== Creating 1000 documents ===\n");

    constexpr int doc_count = 1000;

    // Even creation can be parallel: generate indices, transform to documents
    auto indices = std::vector<int>(doc_count);
    std::iota(indices.begin(), indices.end(), 0);

    std::vector<am::Document> docs;
    {
        auto t = Timer{};
        docs.resize(doc_count);
        std::transform(std::execution::par, indices.begin(), indices.end(), docs.begin(),
            [](int i) {
                auto doc = am::Document{};
                doc.transact([i](auto& tx) {
                    tx.put(am::root, "id", std::int64_t{i});
                    tx.put(am::root, "name", std::string{"doc_" + std::to_string(i)});
                    for (int k = 0; k < 50; ++k) {
                        tx.put(am::root, "field_" + std::to_string(k),
                               std::int64_t{i * 1000 + k});
                    }
                });
                return doc;
            }
        );
        std::printf("parallel create(%d docs): %.1f ms\n", doc_count, t.elapsed_ms());
    }

    // =========================================================================
    // Parallel save — std::transform(par)
    // =========================================================================
    std::printf("\n=== parallel save: 1000 documents ===\n");

    std::vector<std::vector<std::byte>> saved(doc_count);
    {
        auto t = Timer{};
        std::transform(std::execution::par, docs.begin(), docs.end(), saved.begin(),
            [](const am::Document& doc) { return doc.save(); }
        );
        std::printf("std::transform(par) save(%d docs): %.1f ms\n", doc_count, t.elapsed_ms());
    }

    // Compare with sequential
    {
        auto t = Timer{};
        std::transform(std::execution::seq, docs.begin(), docs.end(), saved.begin(),
            [](const am::Document& doc) { return doc.save(); }
        );
        std::printf("std::transform(seq) save(%d docs): %.1f ms\n", doc_count, t.elapsed_ms());
    }

    auto total_bytes = std::transform_reduce(std::execution::par,
        saved.begin(), saved.end(), std::size_t{0}, std::plus<>{},
        [](const auto& s) { return s.size(); }
    );
    std::printf("Total: %.1f KB\n", static_cast<double>(total_bytes) / 1024.0);

    // =========================================================================
    // Parallel load — std::transform(par)
    // =========================================================================
    std::printf("\n=== parallel load: 1000 documents ===\n");

    auto loaded = std::vector<std::optional<am::Document>>(doc_count);
    {
        auto t = Timer{};
        std::transform(std::execution::par, saved.begin(), saved.end(), loaded.begin(),
            [](const std::vector<std::byte>& bytes) { return am::Document::load(bytes); }
        );
        std::printf("std::transform(par) load(%d docs): %.1f ms\n", doc_count, t.elapsed_ms());
    }

    // Compare with sequential
    {
        auto t = Timer{};
        std::transform(std::execution::seq, saved.begin(), saved.end(), loaded.begin(),
            [](const std::vector<std::byte>& bytes) { return am::Document::load(bytes); }
        );
        std::printf("std::transform(seq) load(%d docs): %.1f ms\n", doc_count, t.elapsed_ms());
    }

    auto ok = std::ranges::count_if(loaded, [](const auto& d) { return d.has_value(); });
    std::printf("Loaded: %lld/%d succeeded\n", static_cast<long long>(ok), doc_count);

    // =========================================================================
    // Parallel mutate — std::for_each(par)
    // =========================================================================
    std::printf("\n=== parallel mutate: 1000 documents ===\n");

    {
        auto t = Timer{};
        std::for_each(std::execution::par, docs.begin(), docs.end(),
            [](am::Document& doc) {
                doc.transact([](auto& tx) {
                    tx.put(am::root, "updated", std::int64_t{1});
                    tx.put(am::root, "version", std::int64_t{2});
                });
            }
        );
        std::printf("std::for_each(par) transact(%d docs): %.1f ms\n",
                    doc_count, t.elapsed_ms());
    }

    // =========================================================================
    // Parallel merge — THE MONOID
    //
    // CRDT merge is a monoid:
    //   - Binary op:  merge(a, b)
    //   - Identity:   empty Document
    //   - Associative: merge(merge(a, b), c) == merge(a, merge(b, c))
    //   - Commutative: merge(a, b) == merge(b, a)
    //   - Idempotent:  merge(a, a) == a
    //
    // Therefore std::reduce(par, ...) parallelizes merge for free.
    // =========================================================================
    std::printf("\n=== parallel merge: monoid reduce ===\n");

    // Create 100 peer documents, each with unique data
    constexpr int peer_count = 100;
    auto peers = std::vector<am::Document>(peer_count);
    {
        auto peer_indices = std::vector<int>(peer_count);
        std::iota(peer_indices.begin(), peer_indices.end(), 0);
        std::transform(std::execution::par,
            peer_indices.begin(), peer_indices.end(), peers.begin(),
            [](int p) {
                auto doc = am::Document{};
                doc.transact([p](auto& tx) {
                    for (int k = 0; k < 10; ++k) {
                        tx.put(am::root, "peer" + std::to_string(p) + "_k" + std::to_string(k),
                               std::int64_t{p * 100 + k});
                    }
                });
                return doc;
            }
        );
    }

    // Parallel reduce: merge all 100 peers into one document.
    // std::reduce splits the work across cores, merges sub-results,
    // and produces the final merged document.
    {
        auto t = Timer{};
        auto merged = std::reduce(std::execution::par,
            peers.begin(), peers.end(),
            am::Document{},  // monoid identity
            [](am::Document acc, const am::Document& peer) {
                acc.merge(peer);
                return acc;
            }
        );
        std::printf("std::reduce(par) merge(%d peers): %.1f ms, %zu keys\n",
                    peer_count, t.elapsed_ms(), merged.length(am::root));
    }

    // Compare with sequential reduce
    {
        auto t = Timer{};
        auto merged = std::reduce(std::execution::seq,
            peers.begin(), peers.end(),
            am::Document{},
            [](am::Document acc, const am::Document& peer) {
                acc.merge(peer);
                return acc;
            }
        );
        std::printf("std::reduce(seq) merge(%d peers): %.1f ms, %zu keys\n",
                    peer_count, t.elapsed_ms(), merged.length(am::root));
    }

    // =========================================================================
    // Parallel sync — std::for_each(par) on indexed pairs
    // =========================================================================
    std::printf("\n=== parallel sync: 100 pairs ===\n");

    constexpr int sync_pairs = 100;
    auto sources = std::vector<am::Document>(sync_pairs);
    auto targets = std::vector<am::Document>(sync_pairs);

    // Set up source documents
    {
        auto pair_indices = std::vector<int>(sync_pairs);
        std::iota(pair_indices.begin(), pair_indices.end(), 0);
        std::transform(std::execution::par,
            pair_indices.begin(), pair_indices.end(), sources.begin(),
            [](int i) {
                auto src = am::Document{};
                src.transact([i](auto& tx) {
                    for (int k = 0; k < 20; ++k) {
                        tx.put(am::root, "k_" + std::to_string(k),
                               std::int64_t{i * 100 + k});
                    }
                });
                return src;
            }
        );
    }

    // Sync all pairs in parallel
    {
        auto pair_indices = std::vector<int>(sync_pairs);
        std::iota(pair_indices.begin(), pair_indices.end(), 0);
        auto t = Timer{};
        std::for_each(std::execution::par,
            pair_indices.begin(), pair_indices.end(),
            [&sources, &targets](int i) {
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
            }
        );
        std::printf("std::for_each(par) sync(%d pairs): %.1f ms\n",
                    sync_pairs, t.elapsed_ms());
    }

    auto sync_ok = std::ranges::all_of(
        std::views::iota(0, sync_pairs),
        [&targets](int i) { return targets[i].length(am::root) == 20; }
    );
    std::printf("All syncs correct: %s\n", sync_ok ? "yes" : "NO");

    // =========================================================================
    // Parallel transform_reduce — total bytes across all documents
    // =========================================================================
    std::printf("\n=== parallel transform_reduce: aggregate stats ===\n");

    {
        auto t = Timer{};
        auto total_keys = std::transform_reduce(std::execution::par,
            docs.begin(), docs.end(),
            std::size_t{0}, std::plus<>{},
            [](const am::Document& doc) { return doc.length(am::root); }
        );
        std::printf("std::transform_reduce(par) total keys across %d docs: %zu (%.1f ms)\n",
                    doc_count, total_keys, t.elapsed_ms());
    }

    // =========================================================================
    // Large single document — internal parallelism is transparent
    // =========================================================================
    std::printf("\n=== Large document: internal parallelism ===\n");

    auto big = am::Document{};
    constexpr int big_changes = 500;

    {
        auto t = Timer{};
        for (int i = 0; i < big_changes; ++i) {
            big.transact([i](auto& tx) {
                tx.put(am::root, "k_" + std::to_string(i), std::int64_t{i});
            });
        }
        std::printf("Built %d-change document: %.1f ms\n", big_changes, t.elapsed_ms());
    }

    // save() internally uses std::execution::par to serialize change chunks
    {
        auto t = Timer{};
        auto bytes = big.save();
        std::printf("save(): %zu bytes in %.1f ms\n", bytes.size(), t.elapsed_ms());
    }

    // load() internally uses std::execution::par to parse/decompress chunks
    {
        auto bytes = big.save();
        auto t = Timer{};
        auto reloaded = am::Document::load(bytes);
        std::printf("load(): %.1f ms, ok=%s\n", t.elapsed_ms(),
                    reloaded ? "yes" : "no");
    }

    std::printf("\nDone.\n");
    return 0;
}
