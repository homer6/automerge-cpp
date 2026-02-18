// thread_safe_demo — one document, many threads
//
// Document is thread-safe via std::shared_mutex:
//   - Read methods (get, text, keys, save, ...) take a shared lock — N readers run concurrently
//   - Write methods (transact, merge, apply_changes, ...) take an exclusive lock
//   - set_read_locking(false) disables the shared lock for maximum read throughput
//     when the caller guarantees no concurrent writers
//
// Build: cmake --build build
// Run:   ./build/examples/thread_safe_demo

#include <automerge-cpp/automerge.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace am = automerge_cpp;

int main() {
    std::printf("Hardware threads: %u\n\n", std::thread::hardware_concurrency());

    auto doc = am::Document{};

    // Populate with 1000 keys
    doc.transact([](auto& tx) {
        for (int i = 0; i < 1000; ++i) {
            tx.put(am::root, "key_" + std::to_string(i), std::int64_t{i});
        }
    });

    // -- Concurrent reads -----------------------------------------------------
    // get() acquires a shared lock — all 8 threads read simultaneously.
    std::printf("=== Concurrent reads ===\n");
    {
        auto found = std::atomic<int>{0};
        auto threads = std::vector<std::thread>{};
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&doc, &found, t]() {
                for (int i = t * 125; i < (t + 1) * 125; ++i) {
                    if (doc.get(am::root, "key_" + std::to_string(i)))
                        found.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& t : threads) t.join();
        std::printf("  8 threads x 125 gets: %d found\n\n", found.load());
    }

    // -- Concurrent writers ---------------------------------------------------
    // transact() acquires an exclusive lock — writers are serialized.
    std::printf("=== Concurrent writers ===\n");
    {
        auto threads = std::vector<std::thread>{};
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&doc, t]() {
                for (int i = 0; i < 10; ++i) {
                    doc.transact([t, i](auto& tx) {
                        tx.put(am::root, "w" + std::to_string(t) + "_" + std::to_string(i),
                               std::int64_t{t * 100 + i});
                    });
                }
            });
        }
        for (auto& t : threads) t.join();
        std::printf("  8 threads x 10 writes: %zu total keys\n\n", doc.length(am::root));
    }

    // -- Readers + writer simultaneously --------------------------------------
    // One writer appends text while 4 readers read concurrently.
    std::printf("=== Readers + writer ===\n");
    {
        am::ObjId text_id;
        doc.transact([&](auto& tx) {
            text_id = tx.put_object(am::root, "content", am::ObjType::text);
            tx.splice_text(text_id, 0, 0, "Hello");
        });

        auto stop = std::atomic<bool>{false};
        auto reads_done = std::atomic<int>{0};

        auto threads = std::vector<std::thread>{};

        // Writer
        threads.emplace_back([&]() {
            for (int i = 0; i < 100; ++i) {
                auto len = doc.length(text_id);
                doc.transact([&text_id, len](auto& tx) {
                    tx.splice_text(text_id, len, 0, ".");
                });
            }
            stop.store(true, std::memory_order_release);
        });

        // Readers
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&]() {
                while (!stop.load(std::memory_order_acquire)) {
                    (void)doc.text(text_id);
                    reads_done.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& t : threads) t.join();
        std::printf("  Writer done. %d concurrent reads. Text length: %zu\n\n",
                    reads_done.load(), doc.length(text_id));
    }

    // -- Lock-free reads ------------------------------------------------------
    // When no writers are active, disable read locking for maximum throughput.
    // This eliminates shared_mutex cache-line contention across cores.
    std::printf("=== Lock-free reads ===\n");
    {
        doc.set_read_locking(false);  // caller guarantees no concurrent writers

        auto found = std::atomic<int>{0};
        auto threads = std::vector<std::thread>{};
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&doc, &found, t]() {
                for (int i = t * 125; i < (t + 1) * 125; ++i) {
                    if (doc.get(am::root, "key_" + std::to_string(i)))
                        found.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& t : threads) t.join();

        doc.set_read_locking(true);  // re-enable before any writes
        std::printf("  8 threads x 125 lock-free gets: %d found\n\n", found.load());
    }

    // -- Shared thread pool ---------------------------------------------------
    // Documents can share a thread pool via the constructor.
    std::printf("=== Shared thread pool ===\n");
    {
        auto pool = doc.get_thread_pool();
        auto doc2 = am::Document{pool};
        auto doc3 = am::Document{pool};

        doc2.transact([](auto& tx) { tx.put(am::root, "src", std::string{"doc2"}); });
        doc3.transact([](auto& tx) { tx.put(am::root, "src", std::string{"doc3"}); });

        std::printf("  doc2 and doc3 share pool: %s\n",
                    (doc2.get_thread_pool() == doc3.get_thread_pool()) ? "yes" : "no");
    }

    std::printf("\nDone.\n");
    return 0;
}
