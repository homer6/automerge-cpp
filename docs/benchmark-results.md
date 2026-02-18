# Benchmark Results

## Current Results (v0.4.0)

### macOS (Apple M3 Max, 16 cores, Apple Clang, Release)

```
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON
cmake --build build-release
./build-release/benchmarks/automerge_cpp_benchmarks
```

| Benchmark | Time (ns) | Throughput |
|-----------|-----------|------------|
| **Map operations** | | |
| map_put (single) | 1,050 | 953.1 K ops/s |
| map_put_batch/10 | 3,184 | 3.15 M ops/s |
| map_put_batch/64 | 15,293 | 4.19 M ops/s |
| map_put_batch/512 | 142,088 | 3.61 M ops/s |
| map_put_batch/1000 | 292,073 | 3.44 M ops/s |
| map_get | 35 | 28.5 M ops/s |
| map_keys (100 keys) | 292 | 3.43 M ops/s |
| **List operations** | | |
| list_insert_append | 65,901 | 15.2 K ops/s |
| list_insert_front | 68,438 | 14.6 K ops/s |
| list_get (1000 elements) | 223 | 4.48 M ops/s |
| **Text operations** | | |
| text_splice_append (1 char) | 64,799 | 15.4 K ops/s |
| text_splice_bulk (100 chars) | 3,710,298 | 27.0 K chars/s |
| text_read (1000 chars) | 3,647 | 274.3 K ops/s |
| **Save / Load** | | |
| save (100 keys) | 96,255 | 10.4 K ops/s |
| load (100 keys) | 26,147 | 38.2 K ops/s |
| save_large (1000 list items) | 137,776 | 1.38 KiB/s |
| **Fork / Merge** | | |
| fork (100 keys) | 7,974 | 125.6 K ops/s |
| merge (10+10 concurrent puts) | 4,022 | 248.7 K ops/s |
| **Sync protocol** | | |
| sync_generate_message | 666 | 1.50 M ops/s |
| sync_full_round_trip | 233,957 | 4.28 K ops/s |
| **Patches** | | |
| transact_with_patches (map put) | 1,136 | 881.3 K ops/s |
| transact_with_patches (text) | 111,452 | 9.0 K ops/s |
| **Time travel** | | |
| get_at (10 changes) | 8,298 | 120.6 K ops/s |
| text_at (2 changes) | 2,161 | 463.3 K ops/s |
| **Cursors** | | |
| cursor_create (1000 elements) | 225 | 4.45 M ops/s |
| cursor_resolve (1000 elements) | 165 | 6.04 M ops/s |

### Linux (Intel Xeon Platinum 8358, 30 cores, GCC 13.3, Release)

| Benchmark | Time (ns) | Throughput |
|-----------|-----------|------------|
| **Map operations** | | |
| map_put (single) | 1,086 | 922 K ops/s |
| map_put_batch/10 | 3,735 | 2.68 M ops/s |
| map_put_batch/64 | 15,536 | 4.12 M ops/s |
| map_put_batch/512 | 154,121 | 3.32 M ops/s |
| map_put_batch/1000 | 283,939 | 3.52 M ops/s |
| map_get | 37 | 27.2 M ops/s |
| map_keys (100 keys) | 1,134 | 882 K ops/s |
| **List operations** | | |
| list_insert_append | 157,837 | 6.3 K ops/s |
| list_insert_front | 74,038 | 13.5 K ops/s |
| list_get (1000 elements) | 363 | 2.75 M ops/s |
| **Text operations** | | |
| text_splice_append (1 char) | 155,282 | 6.4 K ops/s |
| text_splice_bulk (100 chars) | 10,330,253 | 9.7 K chars/s |
| text_read (1000 chars) | 3,366 | 297 K ops/s |
| **Save / Load** | | |
| save (100 keys) | 31,494 | 31.8 K ops/s |
| load (100 keys) | 36,080 | 27.7 K ops/s |
| save_large (1000 list items) | 106,553 | 2.4 KiB/s |
| **Fork / Merge** | | |
| fork (100 keys) | 6,907 | 144.8 K ops/s |
| merge (10+10 concurrent puts) | 3,843 | 260 K ops/s |
| **Sync protocol** | | |
| sync_generate_message | 241 | 4.16 M ops/s |
| sync_full_round_trip | 37,861 | 26.4 K ops/s |
| **Patches** | | |
| transact_with_patches (map put) | 1,069 | 935 K ops/s |
| transact_with_patches (text) | 275,892 | 3.6 K ops/s |
| **Time travel** | | |
| get_at (10 changes) | 347 | 2.88 M ops/s |
| text_at (2 changes) | 1,185 | 844 K ops/s |
| **Cursors** | | |
| cursor_create (1000 elements) | 361 | 2.77 M ops/s |
| cursor_resolve (1000 elements) | 345 | 2.90 M ops/s |

## Optimization Impact (Linux, v0.3.0 baseline → v0.4.0)

Optimizations applied: hash cache (11A.3), actor table cache (11A.3b), SHA-256 stack
buffer (11B.4), serialization buffer pre-sizing (11B.3).

| Benchmark | Before | After | Speedup | Primary Cause |
|-----------|--------|-------|---------|---------------|
| get_at | 130 K ops/s | **2.88 M ops/s** | **22.1x** | Hash cache eliminates redundant SHA-256 |
| sync_full_round_trip | 4.7 K ops/s | **26.4 K ops/s** | **5.6x** | Hash cache (4-6 change_hash_index calls → 0) |
| sync_generate_message | 1.50 M ops/s | **4.16 M ops/s** | **2.8x** | Hash cache |
| text_at | 425 K ops/s | **844 K ops/s** | **2.0x** | Hash cache in changes_visible_at() |
| cursor_resolve | 2.04 M ops/s | **2.90 M ops/s** | **1.4x** | Hash cache in time-travel path |
| save_large | 2.4 KiB/s | **2.4 KiB/s** | **1.2x** | Actor table cache (15.6% → 0%) |
| save | 28.8 K ops/s | **31.8 K ops/s** | **1.1x** | Actor table cache + buffer pre-sizing |

## Parallel Scaling (Linux, 30-core Xeon, `-O3 -march=native`)

### Read Scaling (lock-free reads)

Single document with N keys. Sequential reads one-by-one vs parallel reads via
thread pool with `set_read_locking(false)` (no concurrent writers).

| Keys | Sequential | Parallel (30 threads) | Speedup |
|------|-----------|----------------------|---------|
| 100K | 7.1 M ops/s | **95.0 M ops/s** | **13.5x** |
| 500K | 5.7 M ops/s | **66.0 M ops/s** | **11.7x** |
| 1M | 5.7 M ops/s | **55.0 M ops/s** | **9.6x** |

Before the lock-free optimization, parallel gets only reached ~2x due to
`shared_mutex` reader-count cache-line contention (51% of CPU in
`pthread_rwlock_lock`/`unlock`). Disabling read locking eliminated all
synchronization overhead, achieving near-linear scaling.

### Write Scaling (independent documents)

Each thread writes to its own independent document (sharding model). Total keys
fixed, divided across threads.

| Keys | Sequential | Parallel (30 threads) | Speedup |
|------|-----------|----------------------|---------|
| 100K | 2.0 M ops/s | **13.5 M ops/s** | **6.9x** |
| 500K | 1.7 M ops/s | **9.4 M ops/s** | **5.4x** |
| 1M | 1.7 M ops/s | **8.4 M ops/s** | **4.8x** |

Put scaling is limited by allocator contention (~17 heap allocations per
transaction from map entry insertions).

### Batch Parallel Operations

| Benchmark | Sequential | Parallel (30 threads) | Speedup |
|-----------|-----------|----------------------|---------|
| save 500 docs | 95 K docs/s | **806 K docs/s** | **8.4x** |
| load 500 docs | 48 K docs/s | **190 K docs/s** | **3.9x** |

### perf Analysis: shared_mutex Bottleneck (before lock-free fix)

`perf record` on the parallel get path with `shared_mutex` enabled showed:

| Function | % CPU |
|----------|-------|
| `pthread_rwlock_unlock` | **30.2%** |
| `pthread_rwlock_rdlock` | **21.2%** |
| kernel (futex/scheduling) | **~23%** |
| `std::map::find` (actual work) | 2.7% |
| `memcmp` (string compare) | 2.0% |

**51% of CPU** was spent in lock/unlock, 23% in kernel futex calls, and only
~5% was actual useful work. Hardware counters confirmed: IPC of **0.49**
(vs 1.16 after fix), 9.3M context switches per run.

### Thread Pool Configuration

All parallel benchmarks use a single shared `thread_pool` with
`sleep_duration = 0` (yield instead of 500µs sleep). The pool size matches
`hardware_concurrency()` (30 threads on this machine). Documents share the pool
via `Document{pool}`.

## Notes

- List/text insert and splice throughput is lower because each transaction creates a change entry with hashing and head tracking overhead. Batched operations inside a single transaction are significantly faster.
- map_get at 27M+ ops/s reflects the efficiency of `std::unordered_map` lookups with no CRDT overhead on reads.
- Cursor operations are very fast because they are simple linear scans over the element list.
- These numbers are for a debug-free release build (`-DCMAKE_BUILD_TYPE=Release`). Debug builds are ~20-30x slower.
- Linux sync and time-travel benchmarks are significantly faster than macOS because the hash cache optimization was applied after the macOS baseline was captured. macOS will see similar improvements once re-benchmarked.
- Parallel read scaling requires `set_read_locking(false)` — the caller must guarantee no concurrent writers during reads. With read locking enabled (default), parallel gets reach ~2x due to `shared_mutex` contention.
