// thread_safe_demo — one document, many threads
//
// Demonstrates: Document is thread-safe (shared_mutex internally).
// 120 threads can read and write the same document simultaneously.
// No wrapper class. No manual locking.
//
// Build: cmake --build build
// Run:   ./build/examples/thread_safe_demo

#include <automerge-cpp/automerge.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <execution>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace am = automerge_cpp;

int main() {
    std::printf("Hardware threads: %u\n", std::thread::hardware_concurrency());

    // =========================================================================
    // Scenario 1: Concurrent reads — std::execution::par + Document::get
    // =========================================================================
    std::printf("\n=== Scenario 1: Parallel reads with std::execution::par ===\n");

    auto doc = am::Document{};

    // Populate
    doc.transact([](auto& tx) {
        for (int i = 0; i < 1000; ++i) {
            tx.put(am::root, "key_" + std::to_string(i), std::int64_t{i * 100});
        }
    });

    // Read 1000 keys in parallel using standard algorithms.
    // Document::get() acquires a shared lock — N readers run concurrently.
    auto indices = std::vector<int>(1000);
    std::iota(indices.begin(), indices.end(), 0);

    auto values = std::vector<std::optional<am::Value>>(1000);
    std::transform(std::execution::par, indices.begin(), indices.end(), values.begin(),
        [&doc](int i) {
            return doc.get(am::root, "key_" + std::to_string(i));
        }
    );

    auto found = std::ranges::count_if(values, [](const auto& v) { return v.has_value(); });
    std::printf("Parallel get of 1000 keys: %lld found\n", static_cast<long long>(found));

    // =========================================================================
    // Scenario 2: Concurrent writes from 120 threads
    // =========================================================================
    std::printf("\n=== Scenario 2: 120 concurrent writers ===\n");

    // Each thread runs its own transaction. Document serializes writes
    // (exclusive lock), but the caller doesn't manage synchronization.
    auto thread_ids = std::vector<int>(120);
    std::iota(thread_ids.begin(), thread_ids.end(), 0);

    std::for_each(std::execution::par, thread_ids.begin(), thread_ids.end(),
        [&doc](int t) {
            for (int i = 0; i < 10; ++i) {
                doc.transact([t, i](auto& tx) {
                    auto key = "t" + std::to_string(t) + "_" + std::to_string(i);
                    tx.put(am::root, key, std::int64_t{t * 1000 + i});
                });
            }
        }
    );

    std::printf("120 threads x 10 writes = %zu total keys\n", doc.length(am::root));

    // =========================================================================
    // Scenario 3: Parallel reads + concurrent writer thread
    // =========================================================================
    std::printf("\n=== Scenario 3: Readers + writer simultaneously ===\n");

    am::ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(am::root, "content", am::ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });

    auto stop = std::atomic<bool>{false};
    auto reads_done = std::atomic<int>{0};

    // Writer thread appends text
    auto writer = std::jthread{[&doc, &text_id, &stop]() {
        for (int i = 0; i < 200 && !stop.load(std::memory_order_relaxed); ++i) {
            doc.transact([&text_id](auto& tx) {
                auto len = doc.length(text_id);
                tx.splice_text(text_id, len, 0, ".");
            });
        }
        stop.store(true, std::memory_order_relaxed);
    }};

    // Reader threads read concurrently with the writer
    auto reader_ids = std::vector<int>(60);
    std::iota(reader_ids.begin(), reader_ids.end(), 0);

    std::for_each(std::execution::par, reader_ids.begin(), reader_ids.end(),
        [&](int) {
            while (!stop.load(std::memory_order_relaxed)) {
                auto txt = doc.text(text_id);
                reads_done.fetch_add(1, std::memory_order_relaxed);
                (void)txt;
            }
        }
    );

    writer.join();
    std::printf("Writer done. %d concurrent reads completed.\n", reads_done.load());
    std::printf("Text length: %zu\n", doc.length(text_id));

    // =========================================================================
    // Scenario 4: Parallel saves — save() is a read (shared lock)
    // =========================================================================
    std::printf("\n=== Scenario 4: 10 concurrent saves ===\n");

    auto save_ids = std::vector<int>(10);
    std::iota(save_ids.begin(), save_ids.end(), 0);
    auto saved = std::vector<std::vector<std::byte>>(10);

    std::transform(std::execution::par, save_ids.begin(), save_ids.end(), saved.begin(),
        [&doc](int) {
            return doc.save();
        }
    );

    auto all_same = std::ranges::all_of(saved, [&](const auto& s) { return s == saved[0]; });
    std::printf("10 concurrent saves: %zu bytes each, deterministic=%s\n",
                saved[0].size(), all_same ? "yes" : "NO");

    // =========================================================================
    // Scenario 5: read()/write() scoped accessors for multi-step operations
    // =========================================================================
    std::printf("\n=== Scenario 5: Scoped read()/write() ===\n");

    // read() holds the shared lock for the entire lambda.
    // Guarantees a consistent snapshot across multiple reads.
    auto snapshot = doc.read([&text_id](const am::Document& d) {
        return std::pair{d.length(text_id), d.text(text_id)};
    });
    std::printf("Snapshot: len=%zu text=\"%.30s%s\"\n",
                snapshot.first, snapshot.second.c_str(),
                snapshot.second.size() > 30 ? "..." : "");

    // write() holds the exclusive lock for the entire lambda.
    // Multiple mutations appear atomic to readers.
    doc.write([&text_id](am::Document& d) {
        d.transact([&text_id](auto& tx) {
            tx.splice_text(text_id, 0, 0, "[");
        });
        d.transact([&text_id](auto& tx) {
            auto len = d.length(text_id);
            tx.splice_text(text_id, len, 0, "]");
        });
    });

    std::printf("After write(): \"%.*s\"\n", 40, doc.text(text_id).c_str());

    std::printf("\nDone.\n");
    return 0;
}
