// thread_safe_demo — one document, many threads
//
// Demonstrates: Document is thread-safe (shared_mutex internally).
// Multiple threads can read and write the same document simultaneously.
// No wrapper class. No manual locking.
//
// Build: cmake --build build
// Run:   ./build/examples/thread_safe_demo

#include <automerge-cpp/automerge.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace am = automerge_cpp;

int main() {
    std::printf("Hardware threads: %u\n", std::thread::hardware_concurrency());

    // =========================================================================
    // Scenario 1: Concurrent reads — many threads calling get()
    // =========================================================================
    std::printf("\n=== Scenario 1: Concurrent reads ===\n");

    auto doc = am::Document{};

    // Populate
    doc.transact([](auto& tx) {
        for (int i = 0; i < 1000; ++i) {
            tx.put(am::root, "key_" + std::to_string(i), std::int64_t{i * 100});
        }
    });

    // Read 1000 keys across 8 threads.
    // Document::get() acquires a shared lock — N readers run concurrently.
    {
        auto found = std::atomic<int>{0};
        auto threads = std::vector<std::thread>{};
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&doc, &found, t]() {
                for (int i = t * 125; i < (t + 1) * 125; ++i) {
                    auto v = doc.get(am::root, "key_" + std::to_string(i));
                    if (v.has_value()) found.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& t : threads) t.join();
        std::printf("8 threads, 1000 concurrent gets: %d found\n", found.load());
    }

    // =========================================================================
    // Scenario 2: Concurrent writes from many threads
    // =========================================================================
    std::printf("\n=== Scenario 2: Concurrent writers ===\n");

    {
        constexpr int num_writers = 30;
        auto threads = std::vector<std::thread>{};
        for (int t = 0; t < num_writers; ++t) {
            threads.emplace_back([&doc, t]() {
                for (int i = 0; i < 10; ++i) {
                    doc.transact([t, i](auto& tx) {
                        auto key = "t" + std::to_string(t) + "_" + std::to_string(i);
                        tx.put(am::root, key, std::int64_t{t * 1000 + i});
                    });
                }
            });
        }
        for (auto& t : threads) t.join();
        std::printf("%d threads x 10 writes = %zu total keys\n",
                    num_writers, doc.length(am::root));
    }

    // =========================================================================
    // Scenario 3: Readers + writer simultaneously
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
    auto writer = std::thread{[&doc, &text_id, &stop]() {
        for (int i = 0; i < 200 && !stop.load(std::memory_order_relaxed); ++i) {
            auto len = doc.length(text_id);
            doc.transact([&text_id, len](auto& tx) {
                tx.splice_text(text_id, len, 0, ".");
            });
        }
        stop.store(true, std::memory_order_relaxed);
    }};

    // Reader threads read concurrently with the writer
    {
        auto readers = std::vector<std::thread>{};
        for (int t = 0; t < 8; ++t) {
            readers.emplace_back([&doc, &text_id, &stop, &reads_done]() {
                while (!stop.load(std::memory_order_relaxed)) {
                    auto txt = doc.text(text_id);
                    reads_done.fetch_add(1, std::memory_order_relaxed);
                    (void)txt;
                }
            });
        }
        for (auto& r : readers) r.join();
    }

    writer.join();
    std::printf("Writer done. %d concurrent reads completed.\n", reads_done.load());
    std::printf("Text length: %zu\n", doc.length(text_id));

    // =========================================================================
    // Scenario 4: Concurrent saves — save() is a read (shared lock)
    // =========================================================================
    std::printf("\n=== Scenario 4: 10 concurrent saves ===\n");

    auto saved = std::vector<std::vector<std::byte>>(10);
    {
        auto threads = std::vector<std::thread>{};
        for (int t = 0; t < 10; ++t) {
            threads.emplace_back([&doc, &saved, t]() {
                saved[t] = doc.save();
            });
        }
        for (auto& t : threads) t.join();
    }

    auto all_same = true;
    for (int i = 1; i < 10; ++i) {
        if (saved[i] != saved[0]) { all_same = false; break; }
    }
    std::printf("10 concurrent saves: %zu bytes each, deterministic=%s\n",
                saved[0].size(), all_same ? "yes" : "NO");

    // =========================================================================
    // Scenario 5: Shared thread pool across documents
    // =========================================================================
    std::printf("\n=== Scenario 5: Shared thread pool ===\n");

    auto pool = doc.get_thread_pool();
    auto doc2 = am::Document{pool};
    auto doc3 = am::Document{pool};

    doc2.transact([](auto& tx) {
        tx.put(am::root, "source", std::string{"doc2"});
    });
    doc3.transact([](auto& tx) {
        tx.put(am::root, "source", std::string{"doc3"});
    });

    std::printf("doc2 and doc3 share pool: pool=%s\n",
                (doc2.get_thread_pool() == doc3.get_thread_pool()) ? "shared" : "different");

    std::printf("\nDone.\n");
    return 0;
}
