# Profiling Analysis — v0.3.0 Bottleneck Identification

## Methodology

### macOS (Apple Silicon)

- **Platform:** Apple M3 Max (16 cores), macOS 14.6.1, Apple Clang
- **Build:** RelWithDebInfo (`-O2 -g`) for symbol visibility with optimization
- **Tool:** macOS `sample` (sampling profiler, 1ms intervals, 3s duration per benchmark)
- **Benchmarks:** Google Benchmark with `--benchmark_min_time=3s` for statistical stability

Each benchmark was run in a separate process. The profiler sampled the call stack ~2000-3000
times per run. Sample counts below reflect relative time spent in each function.

### Linux (x86_64 Server)

- **Platform:** Intel Xeon Platinum 8358 (30 cores @ 2.60 GHz), Linux 6.11.0, GCC 13.3.0
- **CPU features:** AVX-512, SHA-NI (hardware SHA-256), 32 KiB L1d, 4 MiB L2, 16 MiB L3
- **Build:** Release (`-O3`) with `-fno-omit-frame-pointer -g` for profiling
- **Tool:** `perf record -g --call-graph dwarf` + `perf report --stdio`
- **Benchmarks:** Google Benchmark with `--benchmark_min_time=3s` for statistical stability

Profiles captured with `perf_event_paranoid=-1`. Samples reflect CPU cycle attribution.

---

## Per-Benchmark Profiles (macOS)

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

---

## Per-Benchmark Profiles (Linux — Intel Xeon Platinum 8358, 30 cores)

### Baseline Results (Release build, single-threaded)

| Benchmark | Time/iter | Throughput | vs macOS |
|-----------|-----------|------------|----------|
| text_splice_bulk | 13.0 ms | 7.7 K chars/s | 3.5x slower (no NEON) |
| list_insert_append | 252 µs | 4.0 K ops/s | 3.1x slower |
| list_insert_front | 74 µs | 13.5 K ops/s | comparable |
| sync_full_round_trip | 234 µs | 4.3 K ops/s | comparable |
| save (100 keys) | 77 µs | 13.2 K ops/s | 2.9x slower |
| save_large (1000 items) | 142 µs | 427 B/s | 1.7x slower |
| get_at | 8.3 µs | 120 K ops/s | comparable |
| map_put | 1.14 µs | 876 K ops/s | comparable |
| map_get | 37 ns | 27.1 M ops/s | comparable |
| merge | 3.9 µs | 258 K ops/s | comparable |
| cursor_resolve | 489 ns | 2.0 M ops/s | 3x slower |

### text_splice_bulk (13.0ms/iter — slowest benchmark)

| Function | % Cycles | Notes |
|----------|----------|-------|
| `DocState::list_insert` (via `visible_index_to_real`) | **98.35%** | O(n) linear scan per character |
| `Transaction::splice_text` overhead | 1.11% | Op construction |

**Confirms macOS finding:** `visible_index_to_real()` dominates at 98.35% (vs 98.2% on macOS).
IPC is only 1.13 — the linear scan thrashes L1 data cache (5.1 billion L1d misses in 12.5s).

### list_insert_append (252µs/iter)

| Function | % Cycles | Notes |
|----------|----------|-------|
| `DocState::list_insert` | **49.17%** | O(n) scan to find end |
| `Transaction::insert` | 49.13% | Parent frame (includes list_insert) |

**Confirms macOS finding:** `list_insert` (containing `visible_index_to_real`) is the bottleneck.
The two entries represent self vs inclusive time of the same call chain.

### sync_full_round_trip (234µs/iter)

| Function | % Cycles | Notes |
|----------|----------|-------|
| `crypto::sha256` | **56.66%** | Software SHA-256 dominates |
| `malloc` | 5.94% | Heap alloc in SHA-256 padding + vectors |
| `DocState::compute_change_hash` | 5.90% | Wrapper around sha256 |
| `_int_free` | 4.04% | Heap dealloc |
| `__memcmp_evex_movbe` | 3.59% | Hash comparison |
| `cfree` | 2.10% | More dealloc |
| `_int_malloc` | 1.83% | More alloc |
| `vector::_M_range_insert` | 1.20% | Serialization buffer copies |
| `Document::generate_sync_message` | 1.10% | |
| `__memmove_evex_unaligned_erms` | 1.02% | Buffer moves |
| `operator new` | 1.00% | |

**Confirms macOS finding:** SHA-256 at 56.66% (vs 87.3% on macOS). Lower % because heap
allocation is more visible on Linux (malloc/free total ~14%). IPC is excellent at 3.68 —
the SHA-256 computation is well-pipelined but algorithmically redundant (recomputes every call).

**New insight:** malloc/free overhead is **14% of sync** on Linux. The SHA-256 padding buffer
heap allocation (11B.4) and serialization vector copies (11B.3) are more impactful here than
on macOS. This is because macOS's allocator (magazine malloc) is faster for small allocations.

### save (100 keys, 77µs/iter)

| Function | % Cycles | Notes |
|----------|----------|-------|
| `[kernel]` (page faults, syscalls) | 9.57% | Kernel overhead |
| `zlib deflate` | 9.55% | DEFLATE compression |
| `crypto::sha256` | **7.32%** | Much lower than macOS (55.9%) |
| `__memset_evex_unaligned_erms` | 3.37% | Buffer zeroing |
| `encode_change_ops` | 3.06% | Column encoding |
| `Hashtable::_M_insert_unique` (ActorId) | 2.84% | Actor table construction |
| `encode_uleb128` | 2.48% | LEB128 variable-length ints |
| `encode_sleb128` | 1.93% | Signed LEB128 |
| `RleEncoder::flush_literals` | 1.64% | RLE encoding |
| `zlib` | 1.34% | More DEFLATE |
| `malloc` | 1.24% | |
| `zlib` | 1.23% | |
| `RleEncoder::flush_run` | 1.10% | RLE encoding |

**Key difference from macOS:** SHA-256 is only **7.32%** on Linux (vs 55.9% on macOS). This is
because Linux GCC optimizes the software SHA-256 more aggressively with AVX2/SSE4.2 (though not
using SHA-NI intrinsics). The bottleneck shifts to zlib DEFLATE (~12%) and column encoding (~10%).

**New insight:** Actor table hash insertion is **2.84%** — the `build_actor_table()` function
uses `std::unordered_set<ActorId>` and rebuilds it on every save. Caching would help.

### save_large (1000 items, 142µs/iter)

| Function | % Cycles | Notes |
|----------|----------|-------|
| `encode_change_ops` | **20.79%** | Column encoding dominates |
| `Hashtable::_M_insert_unique` (ActorId) | **15.58%** | Actor table rebuild per save! |
| `crypto::sha256` | 5.40% | Lower than macOS (25.9%) |
| `encode_sleb128` | 4.80% | Signed LEB128 |
| `encode_uleb128` | 4.65% | Unsigned LEB128 |
| `RleEncoder<string>::flush_run` | 4.52% | RLE string column |
| `zlib deflate` | ~6.1% | DEFLATE across multiple entries |
| `RleEncoder<long>::append` | 2.85% | |
| `RleEncoder<unsigned long>::append` | 2.69% | |
| `__memset_evex_unaligned_erms` | 2.15% | |

**Key difference from macOS:** Actor table hash insertion is **15.58%** — second highest cost!
With 1000 ops, `build_actor_table()` iterates all ops and inserts each ActorId into an
`unordered_set`. This is a new finding not visible on macOS (likely hidden by SHA-256 cost).

**New optimization target:** Cache the actor table. It only changes when new actors appear
(which happens at merge/load, not during normal single-actor transactions).

### get_at / time travel (8.3µs/iter)

| Function | % Cycles | Notes |
|----------|----------|-------|
| `crypto::sha256` | **73.24%** | Still dominates |
| `DocState::compute_change_hash` | 4.46% | Wrapper |
| `malloc` | 3.96% | SHA-256 padding alloc |
| `__memcmp_evex_movbe` | 3.34% | Hash comparison |
| `_int_free` | 2.77% | |
| `cfree` | 1.49% | |
| `DocState::change_hash_index` | 1.48% | Rebuilds full index per call |
| `vector::_M_range_insert` | 1.08% | |

**Confirms macOS finding:** SHA-256 at 73.24% (vs 93.8% on macOS). Hash caching (11A.3)
would eliminate nearly all of this — `change_hash_index()` recomputes all hashes every call.

### Hardware Counter Analysis (perf stat)

| Benchmark | IPC | L1d Misses | Cache Misses | Branch Misses |
|-----------|-----|------------|--------------|---------------|
| text_splice_bulk | **1.13** | 5.1 B | 2.7 M | 464 K |
| sync_full_round_trip | **3.68** | 31 M | 154 K | 30.6 M |
| save_large | **3.67** | 442 M | 119 K | 6.5 M |

**Key observations:**
- **text_splice_bulk IPC of 1.13** — the linear scan is memory-bound, not compute-bound.
  The 5.1 billion L1d misses confirm the working set exceeds L1 cache. A Fenwick tree would
  dramatically reduce cache pressure by accessing O(log n) elements instead of O(n).
- **sync and save IPC of 3.67-3.68** — these are compute-bound (SHA-256, RLE encoding).
  The CPU pipeline is well-utilized. Speedup comes from algorithmic changes (hash caching)
  and hardware acceleration (SHA-NI), not from cache optimization.
- **sync branch misses: 30.6M** — the SHA-256 implementation has unpredictable branches
  in the compression function. SHA-NI intrinsics would eliminate these entirely.

### SHA-NI Opportunity

This Xeon Platinum 8358 supports **SHA-NI** (Intel SHA Extensions). The current software
SHA-256 takes 7-73% of cycles across benchmarks. SHA-NI provides ~3-5x speedup for SHA-256
on x86_64, similar to ARM Crypto Extensions on Apple Silicon. The v0.4.0 plan's 11D.1 should
include x86 SHA-NI alongside ARM Crypto Extensions.

**Detection:** `#if defined(__SHA__) || (defined(__x86_64__) && defined(__SSE4_2__))`

---

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
2. **Hardware SHA-256 intrinsics** — ARM Crypto Extensions (`vsha256hq_u32` etc.) on Apple
   Silicon / ARMv8.2+; x86 SHA-NI (`_mm_sha256rnds2_epu32` etc.) on Intel Xeon / AMD Zen.
   Expected 3-10x per hash depending on platform.
3. **Stack-allocated padding buffer** — avoid heap allocation in SHA-256 for small inputs
   (< 247 bytes, which covers typical change hashing). Especially impactful on Linux where
   malloc/free is 14% of sync.
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

| Priority | Optimization | macOS Bottleneck | Linux Bottleneck | Expected Speedup | Effort |
|----------|-------------|-----------------|-----------------|-------------------|--------|
| **P0** | Fenwick tree for visible_index_to_real | 98% of list/text | 98% of list/text | 7-25x | 2 hr |
| **P0** | Cache change hash index | 87-94% of sync/time-travel | 57-73% of sync/time-travel | 5-10x | 30 min |
| **P1** | Hardware SHA-256 (ARM Crypto + x86 SHA-NI) | 56-94% of save/sync | 7-73% of save/sync | 3-10x per hash | 2 hr |
| **P1** | SHA-256 stack buffer | (included above) | 14% malloc/free in sync | 10-20% per hash | 30 min |
| **P1** | Cache actor table | <1% (hidden by SHA-256) | **15.6% of save_large** | 1.2-1.5x save | 30 min |
| **P2** | Parallel change serialization (Taskflow) | 40% of save_large | 21% of save_large | min(N,cores)x | 45 min |
| **P2** | Parallel chunk parsing (Taskflow) | load hot path | load hot path | min(N,cores)x | 45 min |
| **P2** | Parallel hash computation (Taskflow) | hash cache rebuild | hash cache rebuild | min(N,cores)x | 30 min |
| **P3** | Buffer pre-sizing (reserve) | RLE encoder allocs | RLE encoder allocs | 1.5x save | 15 min |
| **P3** | Parallel DEFLATE compress/decompress | 0.5% of save | ~12% of save | 2-4x per change | 30 min |

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
# Build Release with frame pointers and debug symbols for profiling
cmake -B build-profile -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer -g" \
    -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON -DAUTOMERGE_CPP_BUILD_TESTS=OFF

cmake --build build-profile -j$(nproc)

# Enable perf for non-root users (requires sudo)
sudo sysctl kernel.perf_event_paranoid=-1

# Record with perf (dwarf call graphs for full stack traces)
perf record -g --call-graph dwarf -o /tmp/perf_splice.data -- \
    ./build-profile/benchmarks/automerge_cpp_benchmarks \
    --benchmark_filter=bm_text_splice_bulk --benchmark_min_time=3s

# View flat hotspot report (top functions by self time)
perf report -i /tmp/perf_splice.data --stdio --no-children -g none --percent-limit 1

# View hierarchical call tree
perf report -i /tmp/perf_splice.data --hierarchy --sort=dso,symbol

# Or generate a flame graph SVG (requires FlameGraph tools):
perf script -i /tmp/perf_splice.data | stackcollapse-perf.pl | flamegraph.pl > splice_bulk.svg

# Per-benchmark hardware counters (IPC, cache misses, branch misses):
perf stat -e cycles,instructions,cache-misses,branch-misses,L1-dcache-load-misses -- \
    ./build-profile/benchmarks/automerge_cpp_benchmarks \
    --benchmark_filter=bm_sync_full_round_trip --benchmark_min_time=3s

# Save JSON baseline for before/after comparison
./build-profile/benchmarks/automerge_cpp_benchmarks \
    --benchmark_format=json --benchmark_out=linux-baseline.json
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
