# Benchmark Results

v0.3.0 — Release build on Apple M3 Max (16 cores), macOS, Apple Clang.

```
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON
cmake --build build-release
./build-release/benchmarks/automerge_cpp_benchmarks
```

## Results

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

## Notes

- List/text insert and splice throughput is lower because each transaction creates a change entry with hashing and head tracking overhead. Batched operations inside a single transaction are significantly faster.
- map_get at 28.5M ops/s reflects the efficiency of `std::map` lookups with no CRDT overhead on reads.
- Cursor operations are very fast because they are simple linear scans over the element list.
- These numbers are for a debug-free release build (`-DCMAKE_BUILD_TYPE=Release`). Debug builds are ~20-30x slower.
