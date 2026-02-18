# Profiling Analysis — v0.3.0 Bottleneck Identification

## Methodology

- **Platform:** Apple M3 Max (16 cores), macOS 14.6.1, Apple Clang
- **Build:** RelWithDebInfo (`-O2 -g`) for symbol visibility with optimization
- **Tool:** macOS `sample` (sampling profiler, 1ms intervals, 3s duration per benchmark)
- **Benchmarks:** Google Benchmark with `--benchmark_min_time=3s` for statistical stability

Each benchmark was run in a separate process. The profiler sampled the call stack ~2000-3000
times per run. Sample counts below reflect relative time spent in each function.

## Per-Benchmark Profiles

### text_splice_bulk (3.7ms/iter — slowest benchmark)

Inserts 100 characters per iteration via `splice_text()`.

| Function | Samples | % | Notes |
|----------|---------|---|-------|
| `DocState::list_insert` (via `visible_index_to_real`) | 2,393 | 98.2% | O(n) linear scan per character |
| `Transaction::splice_text` overhead | 30 | 1.2% | Op construction, variant moves |
| `vector::push_back` (Op reallocation) | 4 | 0.2% | |

**Root cause:** `visible_index_to_real()` is O(n) and called once per character. For a
document with k existing characters, inserting 100 more is O(k * 100). As the document
grows, this becomes O(n^2).

### list_insert_append (80µs/iter)

Appends one element per iteration (list grows over time).

| Function | Samples | % | Notes |
|----------|---------|---|-------|
| `DocState::list_insert` (via `visible_index_to_real`) | 1,174 | 87.6% | O(n) scan to find end |
| `Transaction::insert` overhead | 1,340 | — | Parent frame (includes list_insert) |
| `crypto::sha256` | 40 | 3.0% | Change hash in commit() |

**Root cause:** Same O(n) linear scan. Appending to a 1000-element list scans all 1000
elements to find the insertion point.

### transact_with_patches_text (109µs/iter)

Splices "hello" (5 chars) per iteration with patch generation.

| Function | Samples | % | Notes |
|----------|---------|---|-------|
| `DocState::list_insert` (via `visible_index_to_real`) | 2,127 | 83.5% | O(n) per character |
| `Transaction::splice_text` overhead | 412 | 16.2% | |
| `crypto::sha256` | 8 | 0.3% | |

### sync_full_round_trip (228µs/iter)

Full sync between two documents (20 keys each, multiple rounds).

| Function | Samples | % | Notes |
|----------|---------|---|-------|
| `crypto::sha256` | 1,603 | 87.3% | Dominates everything |
| `DocState::compute_change_hash` | 110 | 6.0% | Wrapper around sha256 |
| `DocState::get_changes_since` | 43 | 2.3% | Calls change_hash_index() |
| `DocState::has_change_hash` | 38 | 2.1% | Rebuilds FULL hash index per call |
| `Document::receive_sync_message` | 31 | 1.7% | Calls change_hash_index() again |
| `Document::generate_sync_message` | 11 | 0.6% | |

**Root cause:** `change_hash_index()` recomputes SHA-256 for ALL changes every time it's
called. `has_change_hash()` calls `change_hash_index()` per hash check, making
`get_missing_deps()` O(n^2) in number of changes. A single sync round trip calls
`change_hash_index()` 4-6 times.

### save (100 keys, 26µs/iter)

Serializes a document with 100 map keys in 1 change.

| Function | Samples | % | Notes |
|----------|---------|---|-------|
| `crypto::sha256` | 559 | 55.9% | Chunk checksum + change hash |
| RLE encoders (flush_nulls, flush_run, flush_literals) | ~220 | 22.0% | Column encoding |
| `encode_uleb128` | 114 | 11.4% | Variable-length integers |
| `encode_change_ops` | 35 | 3.5% | Op column encoding orchestration |
| `encode_value` | 28 | 2.8% | Value type/length encoding |
| `Document::save` | 7 | 0.7% | Outer loop |
| `deflate` (zlib) | 5 | 0.5% | DEFLATE compression |

**Root cause:** SHA-256 (software implementation) takes over half the time. The chunk
envelope computes SHA-256 over the entire body. Column encoding is the other major cost.

### save_large (1000 list items, 84µs/iter)

Serializes a document with 1000 list items in 1 change.

| Function | Samples | % | Notes |
|----------|---------|---|-------|
| `encode_change_ops` | 432 | 40.3% | Column encoding dominates with more ops |
| `crypto::sha256` | 278 | 25.9% | Still significant |
| `encode_uleb128` | 111 | 10.3% | |
| RLE encoders (various flush methods) | ~182 | 17.0% | |
| `encode_value` | 58 | 5.4% | |
| `Document::save` | 12 | 1.1% | |

**Root cause:** With 1000 ops, column encoding (RLE + delta + boolean encoders for 14
columns) becomes the dominant cost. SHA-256 is still 26%.

### get_at / time travel (8.8µs/iter)

Reads a value at a historical point (10 changes in history).

| Function | Samples | % | Notes |
|----------|---------|---|-------|
| `crypto::sha256` | 1,924 | 93.8% | Overwhelmingly dominant |
| `DocState::changes_visible_at` | 64 | 3.1% | BFS over change DAG |
| `DocState::compute_change_hash` | 63 | 3.1% | Called by change_hash_index() |

**Root cause:** `changes_visible_at()` calls `change_hash_index()` which recomputes SHA-256
for all 10 changes. With only 10 changes this is 93.8% of the time. At 1000 changes this
would be catastrophic.

## Summary: The Three Dominant Costs

### 1. `visible_index_to_real()` — O(n) linear scan (98% of list/text operations)

**Where:** `src/doc_state.hpp:193`

```cpp
auto visible_index_to_real(const ObjectState& state, std::size_t index) const -> std::size_t {
    auto visible_count = std::size_t{0};
    for (std::size_t i = 0; i < state.list_elements.size(); ++i) {  // O(n)
        if (state.list_elements[i].visible) {
            if (visible_count == index) return i;
            ++visible_count;
        }
    }
    return state.list_elements.size();
}
```

**Impact:** Every list/text operation (insert, delete, set, get, splice_text) calls this.
For splice_text inserting k characters into a document of n elements: O(n * k).

**Fix:** Fenwick tree (Binary Indexed Tree) for O(log n) `find_kth` queries. Maintained
incrementally on insert (+1) and delete (toggle visibility).

**Affected benchmarks:** text_splice_bulk (25x), list_insert_append (13x),
list_insert_front (14x), list_get (7x), text_splice_append, transact_with_patches_text,
cursor_create, cursor_resolve.

### 2. `sha256` — software SHA-256 (87-94% of sync/save/time-travel)

**Where:** `src/crypto/sha256.hpp` (vendored software implementation)

**Impact:** SHA-256 is called for:
- Change hash computation (`compute_change_hash`) — once per change per call to
  `change_hash_index()`
- Chunk checksum in `save()` — once over the entire document body
- Bloom filter construction uses change hashes

The problem is compounded by `change_hash_index()` which recomputes ALL hashes on every
call and is itself called multiple times per sync round trip and time travel operation.

**Fixes (multiplicative):**
1. **Cache the hash index** — compute once, invalidate on append. Eliminates redundant
   recomputation. Expected 5-10x for sync, get_at.
2. **ARM SHA-256 intrinsics** — `vsha256hq_u32` etc. on Apple Silicon / ARMv8.2+.
   Expected 5-10x per hash.
3. **Stack-allocated padding buffer** — avoid heap allocation in SHA-256 for small inputs
   (< 247 bytes, which covers typical change hashing).
4. **Parallel hash computation** — when rebuilding hash cache on load, hash all changes
   across cores via Taskflow. Expected min(N_changes, cores)x.

**Affected benchmarks:** sync_full_round_trip (5x from cache alone), get_at (10x),
save (2x from ARM intrinsics), save_large (1.3x).

### 3. `encode_change_ops` + RLE encoders (40% of save_large)

**Where:** `src/storage/columns/change_op_columns.hpp`

**Impact:** Column encoding (14 parallel columns: obj, key, insert, action, value_meta,
value_raw, pred_group, pred_actor, pred_counter, succ_group, succ_actor, succ_counter,
insert_after_actor, insert_after_counter) is the dominant cost for large changes.

The op iteration loop is inherently sequential (encoder state depends on previous ops),
but the 14 `finish()` + `take()` calls are independent.

**Fixes:**
1. **Parallel change serialization** — when saving a document with N changes, serialize
   each change body on a separate core. Expected min(N_changes, cores)x.
2. **Parallel column compression** — compress columns > threshold in parallel. Each
   `z_stream` is independent.
3. **Buffer pre-sizing** — `reserve()` calls to avoid vector reallocation during encoding.

**Affected benchmarks:** save_large (2-30x depending on number of changes and cores),
save (1.5-2x).

## Parallelization Opportunity Map

### What CAN be parallelized (independent work units)

| Operation | Parallel Unit | Independence Guarantee | Min Items for Parallel |
|-----------|--------------|----------------------|----------------------|
| save: change body serialization | Per-change | Each reads const Change + const actor_table, writes independent output | >2 changes |
| save: column DEFLATE compression | Per-column | Each z_stream is independent | >2 columns > threshold |
| load: change chunk parsing | Per-chunk | Each reads own byte range + const actor_table | >2 chunks |
| load: column DEFLATE decompression | Per-column | Each z_stream is independent | >2 compressed columns |
| hash cache rebuild | Per-change | Each reads const Change, writes independent hash | >2 changes |
| bloom filter construction | Per-hash | Probe computation is pure; bit-setting uses atomic OR | >32 hashes |
| `all_change_hashes()` | Per-change | Same as hash cache rebuild | >2 changes |

### What CANNOT be parallelized (sequential dependencies)

| Operation | Why Sequential |
|-----------|---------------|
| Op application (`apply_op`) | CRDT ordering — each op may depend on the result of previous ops |
| Column encoding (op loop) | Encoder state (RLE run tracking, delta accumulator) depends on previous ops |
| Transaction ops | Each op reads/modifies shared DocState |
| Change DAG traversal (BFS) | Graph traversal has data-dependent control flow |

### Current benchmark sizes vs. parallelization threshold

| Benchmark | Changes | Ops/Change | Parallel Opportunity |
|-----------|---------|-----------|---------------------|
| save (100 keys) | 1 | 100 | None (1 change) |
| save_large (1000 items) | 1 | 1000 | None (1 change) |
| sync_full_round_trip | 2 | 20 each | Minimal (2 changes) |
| merge | 2 | 10 each | Minimal (2 changes) |

**Key insight:** Current benchmarks use 1-2 changes. Multi-core parallelism needs
benchmarks with 100-1000 changes to show meaningful speedups.

### Recommended new benchmarks for parallelism measurement

| Benchmark | Setup | What It Measures |
|-----------|-------|-----------------|
| save_many_changes/N | N changes of 10 ops each | Parallel change serialization |
| load_many_changes/N | Load document with N changes | Parallel chunk parsing + decompression |
| hash_rebuild/N | N changes, rebuild hash index | Parallel SHA-256 |
| merge_large/N | Merge N changes from M actors | Parallel hash + DAG traversal |
| sync_large/N | Sync with N divergent changes | Full parallel pipeline |

## Optimization Priority (by measured impact)

| Priority | Optimization | Measured Bottleneck | Expected Speedup | Effort |
|----------|-------------|--------------------|--------------------|--------|
| **P0** | Fenwick tree for visible_index_to_real | 98% of list/text ops | 7-25x | 2 hr |
| **P0** | Cache change hash index | 87-94% of sync/time-travel | 5-10x | 30 min |
| **P1** | ARM SHA-256 intrinsics | 56-94% of save/sync/get_at | 5-10x per hash | 2 hr |
| **P1** | SHA-256 stack buffer | Same as above | 10-20% per hash | 30 min |
| **P2** | Parallel change serialization (Taskflow) | 40% of save_large | min(N,cores)x | 45 min |
| **P2** | Parallel chunk parsing (Taskflow) | load hot path | min(N,cores)x | 45 min |
| **P2** | Parallel hash computation (Taskflow) | hash cache rebuild | min(N,cores)x | 30 min |
| **P3** | Buffer pre-sizing (reserve) | RLE encoder allocs | 1.5x save | 15 min |
| **P3** | Parallel DEFLATE compress/decompress | 0.5% of save (small docs) | 2-4x per change | 30 min |

## Reproducing This Analysis

### macOS (Apple Silicon)

```bash
# Build with debug symbols
cmake -B build-profile -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON -DAUTOMERGE_CPP_BUILD_TESTS=OFF

cmake --build build-profile

# Run all benchmarks
./build-profile/benchmarks/automerge_cpp_benchmarks

# Profile a specific benchmark with macOS sample
./build-profile/benchmarks/automerge_cpp_benchmarks \
    --benchmark_filter=bm_text_splice_bulk --benchmark_min_time=5s &
BENCH_PID=$!
sleep 2
sample $BENCH_PID -d 3 -f /tmp/profile_splice.txt
wait $BENCH_PID
# Extract top functions:
grep -E "^\s+(automerge|storage|crypto)" /tmp/profile_splice.txt | head -20
```

### Linux (perf)

```bash
# Build with debug symbols and frame pointers
cmake -B build-profile -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer" \
    -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON -DAUTOMERGE_CPP_BUILD_TESTS=OFF

cmake --build build-profile

# Record with perf (run as root or with perf_event_paranoid=1)
perf record -g --call-graph dwarf \
    ./build-profile/benchmarks/automerge_cpp_benchmarks \
    --benchmark_filter=bm_text_splice_bulk --benchmark_min_time=5s

# View flame graph
perf report --hierarchy --sort=dso,symbol

# Or generate a flame graph SVG (requires FlameGraph tools):
perf script | stackcollapse-perf.pl | flamegraph.pl > splice_bulk.svg

# Profile all benchmarks at once:
perf stat -d ./build-profile/benchmarks/automerge_cpp_benchmarks

# Per-benchmark perf stat (cache misses, branch misses, IPC):
perf stat -e cycles,instructions,cache-misses,branch-misses,L1-dcache-load-misses \
    ./build-profile/benchmarks/automerge_cpp_benchmarks \
    --benchmark_filter=bm_sync_full_round_trip --benchmark_min_time=5s
```

### Comparing before/after

```bash
# Save baseline
./build-profile/benchmarks/automerge_cpp_benchmarks \
    --benchmark_format=json --benchmark_out=baseline.json

# After optimization, compare:
./build-profile/benchmarks/automerge_cpp_benchmarks \
    --benchmark_format=json --benchmark_out=optimized.json

# Use google benchmark tools to compare:
pip install google-benchmark
compare.py benchmarks baseline.json optimized.json
```
