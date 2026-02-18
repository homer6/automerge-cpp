// automerge-cpp benchmarks — measures throughput of core operations.

#include <automerge-cpp/automerge.hpp>
#include "../src/thread_pool.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace automerge_cpp;

// =============================================================================
// Map operations
// =============================================================================

static void bm_map_put(benchmark::State& state) {
    auto doc = Document{};
    std::int64_t i = 0;
    for (auto _ : state) {
        doc.transact([&](auto& tx) {
            tx.put(root, "key", i++);
        });
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_map_put);

static void bm_map_put_batch(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    auto doc = Document{};
    std::int64_t val = 0;
    for (auto _ : state) {
        doc.transact([&](auto& tx) {
            for (std::size_t i = 0; i < n; ++i) {
                tx.put(root, "key" + std::to_string(i), val++);
            }
        });
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * n));
}
BENCHMARK(bm_map_put_batch)->Range(10, 1000);

static void bm_map_get(benchmark::State& state) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        for (int i = 0; i < 100; ++i) {
            tx.put(root, "key" + std::to_string(i), std::int64_t{i});
        }
    });

    for (auto _ : state) {
        auto val = doc.get(root, "key50");
        benchmark::DoNotOptimize(val);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_map_get);

static void bm_map_keys(benchmark::State& state) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        for (int i = 0; i < 100; ++i) {
            tx.put(root, "key" + std::to_string(i), std::int64_t{i});
        }
    });

    for (auto _ : state) {
        auto keys = doc.keys(root);
        benchmark::DoNotOptimize(keys);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_map_keys);

// =============================================================================
// List operations
// =============================================================================

static void bm_list_insert_append(benchmark::State& state) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
    });

    std::size_t idx = 0;
    for (auto _ : state) {
        doc.transact([&](auto& tx) {
            tx.insert(list_id, idx++, std::int64_t{42});
        });
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_list_insert_append);

static void bm_list_insert_front(benchmark::State& state) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
    });

    for (auto _ : state) {
        doc.transact([&](auto& tx) {
            tx.insert(list_id, 0, std::int64_t{42});
        });
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_list_insert_front);

static void bm_list_get(benchmark::State& state) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
        for (int i = 0; i < 1000; ++i) {
            tx.insert(list_id, static_cast<std::size_t>(i), std::int64_t{i});
        }
    });

    for (auto _ : state) {
        auto val = doc.get(list_id, std::size_t{500});
        benchmark::DoNotOptimize(val);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_list_get);

// =============================================================================
// Text operations
// =============================================================================

static void bm_text_splice_append(benchmark::State& state) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "text", ObjType::text);
    });

    std::size_t pos = 0;
    for (auto _ : state) {
        doc.transact([&](auto& tx) {
            tx.splice_text(text_id, pos, 0, "x");
            ++pos;
        });
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_text_splice_append);

static void bm_text_splice_bulk(benchmark::State& state) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "text", ObjType::text);
    });

    const auto chunk = std::string(100, 'a');
    std::size_t pos = 0;
    for (auto _ : state) {
        doc.transact([&](auto& tx) {
            tx.splice_text(text_id, pos, 0, chunk);
            pos += chunk.size();
        });
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * chunk.size()));
}
BENCHMARK(bm_text_splice_bulk);

static void bm_text_read(benchmark::State& state) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "text", ObjType::text);
        tx.splice_text(text_id, 0, 0, std::string(1000, 'x'));
    });

    for (auto _ : state) {
        auto text = doc.text(text_id);
        benchmark::DoNotOptimize(text);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_text_read);

// =============================================================================
// Save / Load
// =============================================================================

static void bm_save(benchmark::State& state) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        for (int i = 0; i < 100; ++i) {
            tx.put(root, "key" + std::to_string(i), std::int64_t{i});
        }
    });

    for (auto _ : state) {
        auto bytes = doc.save();
        benchmark::DoNotOptimize(bytes);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_save);

static void bm_load(benchmark::State& state) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        for (int i = 0; i < 100; ++i) {
            tx.put(root, "key" + std::to_string(i), std::int64_t{i});
        }
    });
    auto bytes = doc.save();

    for (auto _ : state) {
        auto loaded = Document::load(bytes);
        benchmark::DoNotOptimize(loaded);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_load);

static void bm_save_large(benchmark::State& state) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "data", ObjType::list);
        for (int i = 0; i < 1000; ++i) {
            tx.insert(list_id, static_cast<std::size_t>(i), std::int64_t{i});
        }
    });

    for (auto _ : state) {
        auto bytes = doc.save();
        benchmark::DoNotOptimize(bytes);
        state.SetBytesProcessed(static_cast<std::int64_t>(bytes.size()));
    }
}
BENCHMARK(bm_save_large);

// =============================================================================
// Fork / Merge
// =============================================================================

static void bm_fork(benchmark::State& state) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        for (int i = 0; i < 100; ++i) {
            tx.put(root, "key" + std::to_string(i), std::int64_t{i});
        }
    });

    for (auto _ : state) {
        auto forked = doc.fork();
        benchmark::DoNotOptimize(forked);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_fork);

static void bm_merge(benchmark::State& state) {
    const std::uint8_t raw1[16] = {1};
    const std::uint8_t raw2[16] = {2};

    for (auto _ : state) {
        state.PauseTiming();
        auto doc1 = Document{};
        doc1.set_actor_id(ActorId{raw1});
        doc1.transact([](auto& tx) {
            tx.put(root, "base", std::int64_t{0});
        });
        auto doc2 = doc1.fork();
        doc2.set_actor_id(ActorId{raw2});

        doc1.transact([](auto& tx) {
            for (int i = 0; i < 10; ++i) {
                tx.put(root, "a" + std::to_string(i), std::int64_t{i});
            }
        });
        doc2.transact([](auto& tx) {
            for (int i = 0; i < 10; ++i) {
                tx.put(root, "b" + std::to_string(i), std::int64_t{i});
            }
        });
        state.ResumeTiming();

        doc1.merge(doc2);
        benchmark::DoNotOptimize(doc1);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_merge);

// =============================================================================
// Sync protocol
// =============================================================================

static void bm_sync_generate_message(benchmark::State& state) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        for (int i = 0; i < 50; ++i) {
            tx.put(root, "key" + std::to_string(i), std::int64_t{i});
        }
    });

    for (auto _ : state) {
        auto sync_state = SyncState{};
        auto msg = doc.generate_sync_message(sync_state);
        benchmark::DoNotOptimize(msg);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_sync_generate_message);

static void bm_sync_full_round_trip(benchmark::State& state) {
    const std::uint8_t raw1[16] = {1};
    const std::uint8_t raw2[16] = {2};

    for (auto _ : state) {
        state.PauseTiming();
        auto doc_a = Document{};
        doc_a.set_actor_id(ActorId{raw1});
        doc_a.transact([](auto& tx) {
            for (int i = 0; i < 20; ++i) {
                tx.put(root, "a" + std::to_string(i), std::int64_t{i});
            }
        });
        auto doc_b = Document{};
        doc_b.set_actor_id(ActorId{raw2});
        doc_b.transact([](auto& tx) {
            for (int i = 0; i < 20; ++i) {
                tx.put(root, "b" + std::to_string(i), std::int64_t{i});
            }
        });
        state.ResumeTiming();

        auto state_a = SyncState{};
        auto state_b = SyncState{};
        for (int round = 0; round < 10; ++round) {
            bool progress = false;
            if (auto msg = doc_a.generate_sync_message(state_a)) {
                doc_b.receive_sync_message(state_b, *msg);
                progress = true;
            }
            if (auto msg = doc_b.generate_sync_message(state_b)) {
                doc_a.receive_sync_message(state_a, *msg);
                progress = true;
            }
            if (!progress) break;
        }
        benchmark::DoNotOptimize(doc_a);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_sync_full_round_trip);

// =============================================================================
// Patches
// =============================================================================

static void bm_transact_with_patches(benchmark::State& state) {
    auto doc = Document{};
    std::int64_t val = 0;
    for (auto _ : state) {
        auto patches = doc.transact_with_patches([&](auto& tx) {
            tx.put(root, "key", val++);
        });
        benchmark::DoNotOptimize(patches);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_transact_with_patches);

static void bm_transact_with_patches_text(benchmark::State& state) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "text", ObjType::text);
    });

    std::size_t pos = 0;
    for (auto _ : state) {
        auto patches = doc.transact_with_patches([&](auto& tx) {
            tx.splice_text(text_id, pos, 0, "hello");
            pos += 5;
        });
        benchmark::DoNotOptimize(patches);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_transact_with_patches_text);

// =============================================================================
// Time travel
// =============================================================================

static void bm_get_at(benchmark::State& state) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });
    auto heads_v1 = doc.get_heads();

    // Add more changes so there's history to traverse
    for (int i = 2; i <= 10; ++i) {
        doc.transact([&](auto& tx) {
            tx.put(root, "x", std::int64_t{i});
        });
    }

    for (auto _ : state) {
        auto val = doc.get_at(root, "x", heads_v1);
        benchmark::DoNotOptimize(val);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_get_at);

static void bm_text_at(benchmark::State& state) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "text", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello");
    });
    auto heads_v1 = doc.get_heads();

    doc.transact([&](auto& tx) {
        tx.splice_text(text_id, 5, 0, " World of CRDTs and more text");
    });

    for (auto _ : state) {
        auto text = doc.text_at(text_id, heads_v1);
        benchmark::DoNotOptimize(text);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_text_at);

// =============================================================================
// Cursors
// =============================================================================

static void bm_cursor_create(benchmark::State& state) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
        for (int i = 0; i < 1000; ++i) {
            tx.insert(list_id, static_cast<std::size_t>(i), std::int64_t{i});
        }
    });

    for (auto _ : state) {
        auto cur = doc.cursor(list_id, 500);
        benchmark::DoNotOptimize(cur);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_cursor_create);

static void bm_cursor_resolve(benchmark::State& state) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
        for (int i = 0; i < 1000; ++i) {
            tx.insert(list_id, static_cast<std::size_t>(i), std::int64_t{i});
        }
    });

    auto cur = doc.cursor(list_id, 500);

    for (auto _ : state) {
        auto idx = doc.resolve_cursor(list_id, *cur);
        benchmark::DoNotOptimize(idx);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_cursor_resolve);

// =============================================================================
// Fork/merge batch — sequential vs parallel
// =============================================================================

static void bm_fork_merge_batch_sequential(benchmark::State& state) {
    const auto total_keys = static_cast<int>(state.range(0));
    for (auto _ : state) {
        auto doc = Document{};
        doc.transact([total_keys](auto& tx) {
            for (int i = 0; i < total_keys; ++i) {
                tx.put(root, "k" + std::to_string(i), std::int64_t{i});
            }
        });
        benchmark::DoNotOptimize(doc);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * total_keys);
}
BENCHMARK(bm_fork_merge_batch_sequential)->Arg(400)->Arg(2000)->Arg(4000);

static void bm_fork_merge_batch_parallel(benchmark::State& state) {
    const auto num_forks = static_cast<int>(state.range(0));
    constexpr int keys_per_fork = 500;

    for (auto _ : state) {
        auto doc = Document{};
        auto forks = std::vector<Document>{};
        forks.reserve(num_forks);
        for (int f = 0; f < num_forks; ++f) {
            forks.push_back(doc.fork());
        }

        auto threads = std::vector<std::jthread>{};
        for (int f = 0; f < num_forks; ++f) {
            threads.emplace_back([&forks, f]() {
                forks[f].transact([f](auto& tx) {
                    for (int i = 0; i < keys_per_fork; ++i) {
                        tx.put(root, "k" + std::to_string(f * keys_per_fork + i),
                               std::int64_t{f * keys_per_fork + i});
                    }
                });
            });
        }
        threads.clear();  // join all

        for (auto& f : forks) {
            doc.merge(f);
        }
        benchmark::DoNotOptimize(doc);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * num_forks * keys_per_fork);
}
BENCHMARK(bm_fork_merge_batch_parallel)->Arg(2)->Arg(4)->Arg(8);

// =============================================================================
// Concurrent reads — thread-safe Document, N readers
// =============================================================================

static void bm_concurrent_reads(benchmark::State& state) {
    const auto num_threads = static_cast<int>(state.range(0));

    auto doc = Document{};
    doc.transact([](auto& tx) {
        for (int i = 0; i < 1000; ++i) {
            tx.put(root, "key_" + std::to_string(i), std::int64_t{i * 100});
        }
    });

    for (auto _ : state) {
        auto threads = std::vector<std::jthread>{};
        constexpr int reads_per_thread = 100;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&doc, t]() {
                for (int i = 0; i < reads_per_thread; ++i) {
                    auto key = "key_" + std::to_string((t * reads_per_thread + i) % 1000);
                    auto val = doc.get(root, key);
                    benchmark::DoNotOptimize(val);
                }
            });
        }
        threads.clear();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * num_threads * 100);
}
BENCHMARK(bm_concurrent_reads)->Arg(1)->Arg(4)->Arg(8);

// =============================================================================
// Concurrent writers — thread-safe Document, N writers
// =============================================================================

static void bm_concurrent_writes(benchmark::State& state) {
    const auto num_threads = static_cast<int>(state.range(0));

    for (auto _ : state) {
        auto doc = Document{};
        auto threads = std::vector<std::jthread>{};
        constexpr int writes_per_thread = 50;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&doc, t]() {
                for (int i = 0; i < writes_per_thread; ++i) {
                    doc.transact([t, i](auto& tx) {
                        tx.put(root, "t" + std::to_string(t) + "_" + std::to_string(i),
                               std::int64_t{t * 1000 + i});
                    });
                }
            });
        }
        threads.clear();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * num_threads * 50);
}
BENCHMARK(bm_concurrent_writes)->Arg(1)->Arg(4)->Arg(8);

// =============================================================================
// Parallel save — save N independent documents concurrently
// =============================================================================

static void bm_parallel_save(benchmark::State& state) {
    const auto doc_count = static_cast<int>(state.range(0));
    auto hw = std::max(1u, std::thread::hardware_concurrency());

    // Prepare documents
    auto docs = std::vector<Document>(doc_count);
    for (int i = 0; i < doc_count; ++i) {
        docs[i] = Document{1u};
        docs[i].transact([i](auto& tx) {
            for (int k = 0; k < 50; ++k) {
                tx.put(root, "f" + std::to_string(k), std::int64_t{i * 1000 + k});
            }
        });
    }

    for (auto _ : state) {
        auto saved = std::vector<std::vector<std::byte>>(doc_count);
        auto threads = std::vector<std::jthread>{};
        auto chunk = doc_count / static_cast<int>(hw);
        if (chunk < 1) chunk = 1;
        for (unsigned int w = 0; w < hw && static_cast<int>(w) * chunk < doc_count; ++w) {
            auto begin = static_cast<int>(w) * chunk;
            auto end = (w == hw - 1) ? doc_count : std::min(begin + chunk, doc_count);
            threads.emplace_back([&docs, &saved, begin, end]() {
                for (int i = begin; i < end; ++i) {
                    saved[i] = docs[i].save();
                }
            });
        }
        threads.clear();
        benchmark::DoNotOptimize(saved);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * doc_count);
}
BENCHMARK(bm_parallel_save)->Arg(100)->Arg(500);

// =============================================================================
// Parallel load — load N documents concurrently from saved bytes
// =============================================================================

static void bm_parallel_load(benchmark::State& state) {
    const auto doc_count = static_cast<int>(state.range(0));
    auto hw = std::max(1u, std::thread::hardware_concurrency());

    // Prepare saved bytes
    auto saved = std::vector<std::vector<std::byte>>(doc_count);
    for (int i = 0; i < doc_count; ++i) {
        auto doc = Document{1u};
        doc.transact([i](auto& tx) {
            for (int k = 0; k < 50; ++k) {
                tx.put(root, "f" + std::to_string(k), std::int64_t{i * 1000 + k});
            }
        });
        saved[i] = doc.save();
    }

    for (auto _ : state) {
        auto loaded = std::vector<std::optional<Document>>(doc_count);
        auto threads = std::vector<std::jthread>{};
        auto chunk = doc_count / static_cast<int>(hw);
        if (chunk < 1) chunk = 1;
        for (unsigned int w = 0; w < hw && static_cast<int>(w) * chunk < doc_count; ++w) {
            auto begin = static_cast<int>(w) * chunk;
            auto end = (w == hw - 1) ? doc_count : std::min(begin + chunk, doc_count);
            threads.emplace_back([&saved, &loaded, begin, end]() {
                for (int i = begin; i < end; ++i) {
                    loaded[i] = Document::load(saved[i]);
                }
            });
        }
        threads.clear();
        benchmark::DoNotOptimize(loaded);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * doc_count);
}
BENCHMARK(bm_parallel_load)->Arg(100)->Arg(500);

// =============================================================================
// Tree reduce merge — parallel pairwise merge of N peers
// =============================================================================

static void bm_tree_reduce_merge(benchmark::State& state) {
    const auto peer_count = static_cast<int>(state.range(0));

    // Pre-build peers (outside the timed loop)
    auto base_peers = std::vector<Document>(peer_count);
    for (int p = 0; p < peer_count; ++p) {
        base_peers[p] = Document{1u};
        base_peers[p].transact([p](auto& tx) {
            for (int k = 0; k < 10; ++k) {
                tx.put(root, "p" + std::to_string(p) + "_k" + std::to_string(k),
                       std::int64_t{p * 100 + k});
            }
        });
    }

    for (auto _ : state) {
        // Copy peers into work vector
        auto work = std::vector<Document>{};
        work.reserve(peer_count);
        for (const auto& p : base_peers) {
            auto copy = Document{1u};
            copy.merge(p);
            work.push_back(std::move(copy));
        }

        // Tree reduce: merge pairs in parallel
        while (work.size() > 1) {
            auto next = std::vector<Document>{};
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
        benchmark::DoNotOptimize(work);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * peer_count);
}
BENCHMARK(bm_tree_reduce_merge)->Arg(16)->Arg(64);

// =============================================================================
// Thread pool overhead — parallel_for with trivial work
// =============================================================================

static void bm_thread_pool_overhead(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    auto pool = std::make_shared<automerge_cpp::detail::ThreadPool>(
        std::thread::hardware_concurrency());

    auto sink = std::vector<std::int64_t>(count, 0);

    for (auto _ : state) {
        pool->parallel_for(count, [&sink](std::size_t i) {
            sink[i] = static_cast<std::int64_t>(i * i);
        });
        benchmark::DoNotOptimize(sink);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}
BENCHMARK(bm_thread_pool_overhead)->Arg(100)->Arg(10000);

// =============================================================================
// Document constructor — pool creation overhead
// =============================================================================

static void bm_document_constructor_default(benchmark::State& state) {
    for (auto _ : state) {
        auto doc = Document{};
        benchmark::DoNotOptimize(doc);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_document_constructor_default);

static void bm_document_constructor_shared_pool(benchmark::State& state) {
    auto pool = std::make_shared<automerge_cpp::detail::ThreadPool>(
        std::thread::hardware_concurrency());

    for (auto _ : state) {
        auto doc = Document{pool};
        benchmark::DoNotOptimize(doc);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_document_constructor_shared_pool);
