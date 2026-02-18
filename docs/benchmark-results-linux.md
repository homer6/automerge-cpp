# Benchmark Results (Linux)

v0.3.0 — Release build on Intel Xeon Platinum 8358 (30 vCPUs @ 2.60 GHz), Linux 6.11.0, GCC 13.3.0.

```
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON
cmake --build build-release
./build-release/benchmarks/automerge_cpp_benchmarks
```

## Environment

| Property | Value |
|----------|-------|
| CPU | Intel Xeon Platinum 8358 @ 2.60 GHz |
| vCPUs | 30 |
| L1d / L1i / L2 / L3 | 32 KiB / 32 KiB / 4 MiB / 16 MiB (per core) |
| OS | Ubuntu 24.04, Linux 6.11.0-29-generic |
| Compiler | GCC 13.3.0 |
| Build | Release (`-O3`, no sanitizers) |

## Results

| Benchmark | Time (ns) | Throughput |
|-----------|-----------|------------|
| **Map operations** | | |
| map_put (single) | 1,293 | 773.8 K ops/s |
| map_put_batch/10 | 5,329 | 1.88 M ops/s |
| map_put_batch/64 | 25,974 | 2.46 M ops/s |
| map_put_batch/512 | 272,280 | 1.88 M ops/s |
| map_put_batch/1000 | 479,723 | 2.08 M ops/s |
| map_get | 31 | 32.4 M ops/s |
| map_keys (100 keys) | 1,094 | 915.9 K ops/s |
| **List operations** | | |
| list_insert_append | 151,249 | 6.6 K ops/s |
| list_insert_front | 76,063 | 13.1 K ops/s |
| list_get (1000 elements) | 343 | 2.92 M ops/s |
| **Text operations** | | |
| text_splice_append (1 char) | 149,721 | 6.7 K ops/s |
| text_splice_bulk (100 chars) | 10,311,167 | 9.7 K chars/s |
| text_read (1000 chars) | 3,285 | 304.4 K ops/s |
| **Save / Load** | | |
| save (100 keys) | 34,678 | 28.8 K ops/s |
| load (100 keys) | 39,813 | 25.1 K ops/s |
| save_large (1000 list items) | 127,653 | 2.38 KiB/s |
| **Fork / Merge** | | |
| fork (100 keys) | 8,397 | 119.1 K ops/s |
| merge (10+10 concurrent puts) | 4,466 | 224.0 K ops/s |
| **Sync protocol** | | |
| sync_generate_message | 677 | 1.48 M ops/s |
| sync_full_round_trip | 213,212 | 4.7 K ops/s |
| **Patches** | | |
| transact_with_patches (map put) | 1,280 | 781.1 K ops/s |
| transact_with_patches (text) | 291,023 | 3.4 K ops/s |
| **Time travel** | | |
| get_at (10 changes) | 7,504 | 133.3 K ops/s |
| text_at (2 changes) | 2,226 | 449.4 K ops/s |
| **Cursors** | | |
| cursor_create (1000 elements) | 342 | 2.93 M ops/s |
| cursor_resolve (1000 elements) | 328 | 3.05 M ops/s |

## Raw Output

```
2026-02-18T05:06:51+00:00
Running ./build-release/benchmarks/automerge_cpp_benchmarks
Run on (30 X 2594 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x30)
  L1 Instruction 32 KiB (x30)
  L2 Unified 4096 KiB (x30)
  L3 Unified 16384 KiB (x30)
Load Average: 9.70, 5.28, 4.48
----------------------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------
bm_map_put                          1293 ns         1292 ns       517202 items_per_second=773.821k/s
bm_map_put_batch/10                 5329 ns         5314 ns       100000 items_per_second=1.88171M/s
bm_map_put_batch/64                25974 ns        25970 ns        31454 items_per_second=2.46442M/s
bm_map_put_batch/512              272280 ns       272266 ns         4335 items_per_second=1.88051M/s
bm_map_put_batch/1000             479723 ns       479711 ns         1703 items_per_second=2.08459M/s
bm_map_get                          30.9 ns         30.9 ns     22967862 items_per_second=32.4135M/s
bm_map_keys                         1094 ns         1092 ns       641865 items_per_second=915.889k/s
bm_list_insert_append             151249 ns       151185 ns        75268 items_per_second=6.61443k/s
bm_list_insert_front               76063 ns        76053 ns        34051 items_per_second=13.1487k/s
bm_list_get                          343 ns          343 ns      2027803 items_per_second=2.91669M/s
bm_text_splice_append             149721 ns       149703 ns        75348 items_per_second=6.67988k/s
bm_text_splice_bulk             10311167 ns     10309847 ns         1000 items_per_second=9.69946k/s
bm_text_read                        3285 ns         3285 ns       213543 items_per_second=304.419k/s
bm_save                            34678 ns        34672 ns        20264 items_per_second=28.8417k/s
bm_load                            39813 ns        39807 ns        17323 items_per_second=25.1212k/s
bm_save_large                     127653 ns       127634 ns         5488 bytes_per_second=2.37847Ki/s
bm_fork                             8397 ns         8397 ns        80707 items_per_second=119.088k/s
bm_merge                            4466 ns         4464 ns       159162 items_per_second=224.001k/s
bm_sync_generate_message             677 ns          677 ns      1036961 items_per_second=1.47811M/s
bm_sync_full_round_trip           213212 ns       213225 ns         3292 items_per_second=4.68988k/s
bm_transact_with_patches            1280 ns         1280 ns       606750 items_per_second=781.065k/s
bm_transact_with_patches_text     291023 ns       290942 ns        10000 items_per_second=3.43712k/s
bm_get_at                           7504 ns         7501 ns        93983 items_per_second=133.31k/s
bm_text_at                          2226 ns         2225 ns       319943 items_per_second=449.369k/s
bm_cursor_create                     342 ns          342 ns      2048406 items_per_second=2.92794M/s
bm_cursor_resolve                    328 ns          328 ns      2142973 items_per_second=3.05306M/s
```

## Comparison with macOS (Apple M3 Max)

| Benchmark | Linux (Xeon 8358) | macOS (M3 Max) | Ratio |
|-----------|-------------------|----------------|-------|
| map_put | 773.8 K/s | 953.1 K/s | 0.81x |
| map_put_batch/64 | 2.46 M/s | 4.19 M/s | 0.59x |
| map_get | 32.4 M/s | 28.5 M/s | 1.14x |
| list_get | 2.92 M/s | 4.48 M/s | 0.65x |
| save (100 keys) | 28.8 K/s | 10.4 K/s | 2.77x |
| load (100 keys) | 25.1 K/s | 38.2 K/s | 0.66x |
| merge | 224.0 K/s | 248.7 K/s | 0.90x |
| cursor_resolve | 3.05 M/s | 6.04 M/s | 0.50x |

## Notes

- List/text insert and splice throughput is lower because each transaction creates a change entry with hashing and head tracking overhead. Batched operations inside a single transaction are significantly faster.
- map_get at 32.4M ops/s reflects the efficiency of `std::map` lookups with no CRDT overhead on reads.
- Cursor operations are very fast because they are simple linear scans over the element list.
- These numbers are for a release build (`-DCMAKE_BUILD_TYPE=Release`). Debug builds are ~20-30x slower.
- The Xeon and M3 Max have different microarchitectures; some operations favor one over the other. The M3 Max generally wins on single-threaded throughput due to its wider out-of-order engine, while the Xeon shows a notable advantage on save (possibly due to zlib/DEFLATE performance differences between GCC and Apple Clang).
