// automerge-cpp benchmarks — measures throughput of core operations.

#include <automerge-cpp/automerge.hpp>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

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
