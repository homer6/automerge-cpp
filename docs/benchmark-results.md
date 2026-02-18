# Benchmark Results

Release build on Apple M3 Max (16 cores), macOS, Apple Clang.

```
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON
cmake --build build-release
./build-release/benchmarks/automerge_cpp_benchmarks
```

## Results

| Benchmark | Time (ns) | Throughput |
|-----------|-----------|------------|
| **Map operations** | | |
| map_put (single) | 304 | 3.29 M ops/s |
| map_put_batch/10 | 2,328 | 4.29 M ops/s |
| map_put_batch/64 | 14,398 | 4.45 M ops/s |
| map_put_batch/512 | 138,736 | 3.69 M ops/s |
| map_put_batch/1000 | 283,208 | 3.53 M ops/s |
| map_get | 34 | 29.3 M ops/s |
| map_keys (100 keys) | 289 | 3.46 M ops/s |
| **List operations** | | |
| list_insert_append | 75,865 | 13.2 K ops/s |
| list_insert_front | 69,495 | 14.4 K ops/s |
| list_get (1000 elements) | 224 | 4.46 M ops/s |
| **Text operations** | | |
| text_splice_append (1 char) | 70,316 | 14.2 K ops/s |
| text_splice_bulk (100 chars) | 3,742,127 | 26.7 K chars/s |
| text_read (1000 chars) | 3,599 | 277.9 K ops/s |
| **Save / Load** | | |
| save (100 keys) | 4,206 | 237.7 K ops/s |
| load (100 keys) | 18,634 | 53.7 K ops/s |
| save_large (1000 list items) | 45,633 | 24.2 KiB/s |
| **Fork / Merge** | | |
| fork (100 keys) | 8,025 | 124.6 K ops/s |
| merge (10+10 concurrent puts) | 3,270 | 305.8 K ops/s |
| **Sync protocol** | | |
| sync_generate_message | 254 | 3.94 M ops/s |
| sync_full_round_trip | 44,987 | 22.2 K ops/s |
| **Patches** | | |
| transact_with_patches (map put) | 343 | 2.91 M ops/s |
| transact_with_patches (text) | 110,974 | 9.0 K ops/s |
| **Time travel** | | |
| get_at (10 changes) | 784 | 1.27 M ops/s |
| text_at (2 changes) | 832 | 1.20 M ops/s |
| **Cursors** | | |
| cursor_create (1000 elements) | 222 | 4.50 M ops/s |
| cursor_resolve (1000 elements) | 163 | 6.13 M ops/s |

## Notes

- List/text insert and splice throughput is lower because each transaction creates a change entry with hashing and head tracking overhead. Batched operations inside a single transaction are significantly faster.
- map_get at 29.3M ops/s reflects the efficiency of `std::map` lookups with no CRDT overhead on reads.
- Cursor operations are very fast because they are simple linear scans over the element list.
- These numbers are for a debug-free release build (`-DCMAKE_BUILD_TYPE=Release`). Debug builds are ~20-30x slower.
