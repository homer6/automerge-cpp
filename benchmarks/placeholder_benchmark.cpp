// automerge-cpp benchmarks — measures throughput of core operations.

#include <automerge-cpp/automerge.hpp>
#include <automerge-cpp/thread_pool.hpp>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace automerge_cpp;

// One pool for the entire benchmark suite. Every Document and every
// parallelize_loop shares this pool — no extra threads are ever created.
static auto g_pool = std::make_shared<thread_pool>(std::thread::hardware_concurrency());

static auto make_doc() -> Document { return Document{g_pool}; }

// =============================================================================
// Map operations
// =============================================================================

static void bm_map_put(benchmark::State& state) {
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
        auto doc1 = make_doc();
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
    auto doc = make_doc();
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
        auto doc_a = make_doc();
        doc_a.set_actor_id(ActorId{raw1});
        doc_a.transact([](auto& tx) {
            for (int i = 0; i < 20; ++i) {
                tx.put(root, "a" + std::to_string(i), std::int64_t{i});
            }
        });
        auto doc_b = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
    auto doc = make_doc();
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
// Fork/merge batch put — 4000 keys total
//
// Sequential: 1 transaction, 4000 puts.
// Parallel: fork N copies (N = pool threads), each puts 4000/N keys, merge.
// Arg: 0 = sequential, 1 = parallel (uses g_pool).
// =============================================================================

static void bm_fork_merge_batch(benchmark::State& state) {
    const bool parallel = state.range(0) != 0;
    constexpr int total_keys = 4000;

    for (auto _ : state) {
        if (!parallel) {
            auto doc = make_doc();
            doc.transact([](auto& tx) {
                for (int i = 0; i < total_keys; ++i) {
                    tx.put(root, "k" + std::to_string(i), std::int64_t{i});
                }
            });
            benchmark::DoNotOptimize(doc);
        } else {
            auto doc = make_doc();
            auto num_forks = static_cast<int>(g_pool->get_thread_count());
            auto keys_per_fork = total_keys / num_forks;
            auto forks = std::vector<Document>{};
            forks.reserve(num_forks);
            for (int f = 0; f < num_forks; ++f) {
                forks.push_back(doc.fork());
            }

            g_pool->parallelize_loop(0, num_forks, [&](int start, int end) {
                for (int f = start; f < end; ++f) {
                    forks[f].transact([f, keys_per_fork](auto& tx) {
                        for (int i = 0; i < keys_per_fork; ++i) {
                            auto idx = f * keys_per_fork + i;
                            tx.put(root, "k" + std::to_string(idx), std::int64_t{idx});
                        }
                    });
                }
            });

            for (auto& f : forks) {
                doc.merge(f);
            }
            benchmark::DoNotOptimize(doc);
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * total_keys);
    state.SetLabel(parallel ? "parallel" : "sequential");
}
BENCHMARK(bm_fork_merge_batch)->Arg(0)->Arg(1);

// =============================================================================
// Save 500 independent documents — sequential vs parallel
//
// Same 500 docs. Arg: 0 = sequential, 1 = parallel (uses g_pool).
// =============================================================================

static void bm_save_docs(benchmark::State& state) {
    const bool parallel = state.range(0) != 0;
    constexpr int doc_count = 500;

    auto docs = std::vector<Document>(doc_count);
    for (int i = 0; i < doc_count; ++i) {
        docs[i] = make_doc();
        docs[i].transact([i](auto& tx) {
            for (int k = 0; k < 50; ++k) {
                tx.put(root, "f" + std::to_string(k), std::int64_t{i * 1000 + k});
            }
        });
    }

    for (auto _ : state) {
        auto saved = std::vector<std::vector<std::byte>>(doc_count);
        if (!parallel) {
            for (int i = 0; i < doc_count; ++i) {
                saved[i] = docs[i].save();
            }
        } else {
            g_pool->parallelize_loop(0, doc_count, [&](int start, int end) {
                for (int i = start; i < end; ++i) {
                    saved[i] = docs[i].save();
                }
            });
        }
        benchmark::DoNotOptimize(saved);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * doc_count);
    state.SetLabel(parallel ? "parallel" : "sequential");
}
BENCHMARK(bm_save_docs)->Arg(0)->Arg(1);

// =============================================================================
// Load 500 documents from bytes — sequential vs parallel
//
// Same 500 blobs. Arg: 0 = sequential, 1 = parallel (uses g_pool).
// =============================================================================

static void bm_load_docs(benchmark::State& state) {
    const bool parallel = state.range(0) != 0;
    constexpr int doc_count = 500;

    auto saved = std::vector<std::vector<std::byte>>(doc_count);
    for (int i = 0; i < doc_count; ++i) {
        auto doc = make_doc();
        doc.transact([i](auto& tx) {
            for (int k = 0; k < 50; ++k) {
                tx.put(root, "f" + std::to_string(k), std::int64_t{i * 1000 + k});
            }
        });
        saved[i] = doc.save();
    }

    for (auto _ : state) {
        auto loaded = std::vector<std::optional<Document>>(doc_count);
        if (!parallel) {
            for (int i = 0; i < doc_count; ++i) {
                loaded[i] = Document::load(saved[i]);
            }
        } else {
            g_pool->parallelize_loop(0, doc_count, [&](int start, int end) {
                for (int i = start; i < end; ++i) {
                    loaded[i] = Document::load(saved[i]);
                }
            });
        }
        benchmark::DoNotOptimize(loaded);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * doc_count);
    state.SetLabel(parallel ? "parallel" : "sequential");
}
BENCHMARK(bm_load_docs)->Arg(0)->Arg(1);

// =============================================================================
// Read 1000 keys from one document — sequential vs parallel
//
// Shared-lock readers. Arg: 0 = sequential, 1 = parallel (uses g_pool).
// =============================================================================

static void bm_concurrent_reads(benchmark::State& state) {
    const bool parallel = state.range(0) != 0;
    constexpr int total_reads = 1000;

    auto doc = make_doc();
    doc.transact([](auto& tx) {
        for (int i = 0; i < total_reads; ++i) {
            tx.put(root, "key_" + std::to_string(i), std::int64_t{i * 100});
        }
    });

    for (auto _ : state) {
        if (!parallel) {
            for (int i = 0; i < total_reads; ++i) {
                auto val = doc.get(root, "key_" + std::to_string(i));
                benchmark::DoNotOptimize(val);
            }
        } else {
            g_pool->parallelize_loop(0, total_reads, [&](int start, int end) {
                for (int i = start; i < end; ++i) {
                    auto val = doc.get(root, "key_" + std::to_string(i));
                    benchmark::DoNotOptimize(val);
                }
            });
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * total_reads);
    state.SetLabel(parallel ? "parallel" : "sequential");
}
BENCHMARK(bm_concurrent_reads)->Arg(0)->Arg(1);

// =============================================================================
// Tree reduce merge — 64 peers, sequential vs parallel
//
// Sequential: fold left into one accumulator.
// Parallel: pairwise tree reduce via g_pool.
// Arg: 0 = sequential, 1 = parallel.
// =============================================================================

static void bm_merge_reduce(benchmark::State& state) {
    const bool parallel = state.range(0) != 0;
    constexpr int peer_count = 64;

    auto base_peers = std::vector<Document>(peer_count);
    for (int p = 0; p < peer_count; ++p) {
        base_peers[p] = make_doc();
        base_peers[p].transact([p](auto& tx) {
            for (int k = 0; k < 10; ++k) {
                tx.put(root, "p" + std::to_string(p) + "_k" + std::to_string(k),
                       std::int64_t{p * 100 + k});
            }
        });
    }

    for (auto _ : state) {
        auto work = std::vector<Document>{};
        work.reserve(peer_count);
        for (const auto& p : base_peers) {
            auto copy = make_doc();
            copy.merge(p);
            work.push_back(std::move(copy));
        }

        if (!parallel) {
            for (std::size_t i = 1; i < work.size(); ++i) {
                work[0].merge(work[i]);
            }
        } else {
            while (work.size() > 1) {
                auto pairs = work.size() / 2;
                auto next = std::vector<Document>{};
                next.resize(pairs + (work.size() % 2));

                g_pool->parallelize_loop(std::size_t{0}, pairs, [&](std::size_t start, std::size_t end) {
                    for (auto i = start; i < end; ++i) {
                        work[i * 2].merge(work[i * 2 + 1]);
                        next[i] = std::move(work[i * 2]);
                    }
                });
                if (work.size() % 2 == 1) {
                    next[pairs] = std::move(work.back());
                }
                work = std::move(next);
            }
        }
        benchmark::DoNotOptimize(work);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * peer_count);
    state.SetLabel(parallel ? "parallel" : "sequential");
}
BENCHMARK(bm_merge_reduce)->Arg(0)->Arg(1);

// =============================================================================
// Document constructor — default (no pool) vs shared pool
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
    for (auto _ : state) {
        auto doc = Document{g_pool};
        benchmark::DoNotOptimize(doc);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bm_document_constructor_shared_pool);
