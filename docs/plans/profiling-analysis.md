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

## Post-Optimization Results (Linux)

Optimizations implemented: **11A.3** (hash cache), **11A.3b** (actor table cache),
**11B.3** (buffer pre-sizing), **11B.4** (SHA-256 stack buffer).

### Before/After Benchmark Comparison

| Benchmark | Before (ns) | After (ns) | Speedup | Primary Cause |
|-----------|-------------|------------|---------|---------------|
| **bm_get_at** | 7,660 | 347 | **22.1x** | Hash cache eliminates redundant SHA-256 |
| **bm_sync_full_round_trip** | 212,600 | 37,880 | **5.6x** | Hash cache (4-6 calls to change_hash_index → 0) |
| **bm_sync_generate_message** | 669 | 241 | **2.8x** | Hash cache + bloom filter uses cached hashes |
| **bm_text_at** | 2,355 | 1,185 | **2.0x** | Hash cache in changes_visible_at() |
| **bm_cursor_resolve** | 489 | 345 | **1.4x** | Hash cache in time-travel path |
| **bm_cursor_create** | 402 | 361 | **1.1x** | Minor hash cache benefit |
| **bm_save_large** | 130,660 | 106,550 | **1.2x** | Actor table cache (15.6% → 0%) |
| **bm_save** | 34,760 | 31,490 | **1.1x** | Actor table cache + buffer pre-sizing |
| bm_map_put | 1,143 | 1,086 | 1.05x | Minor (within noise) |
| bm_merge | 3,881 | 3,843 | 1.01x | Unchanged (expected) |
| bm_text_splice_bulk | 10,763,000 | 10,330,000 | 1.04x | Unchanged (bottleneck is visible_index_to_real) |
| bm_load | 36,235 | 36,080 | 1.00x | Unchanged (expected) |

No regressions observed.

### Post-Optimization Profile: sync_full_round_trip (37.9µs/iter, was 213µs)

| Function | % Cycles | Before % | Notes |
|----------|----------|----------|-------|
| `Hashtable::find` (ChangeHash) | **12.56%** | — | Hash lookups now visible (was hidden under SHA-256) |
| `_int_malloc` | 7.07% | 1.83% | Allocator now larger fraction of smaller total |
| `_int_free` | 5.73% | 4.04% | |
| `crypto::sha256` | **5.57%** | **56.66%** | Down 10x — only called for new changes, not cached |
| `__memcmp_evex_movbe` | 5.35% | 3.59% | Hash comparisons |
| `malloc` | 5.20% | 5.94% | |
| `Document::generate_sync_message` | 4.23% | 1.10% | Now a visible fraction |
| `DocState::get_changes_since` | 3.80% | — | Hash index lookup instead of rebuild |
| `cfree` | 3.31% | 2.10% | |
| `malloc_consolidate` | 3.07% | — | |
| `Transaction::put` | 2.56% | — | Actual work now visible |
| `Hashtable::_M_insert_unique` (ChangeHash) | 2.14% | — | Cache population |

**Analysis:** SHA-256 dropped from **56.66% → 5.57%** (10x reduction in share). The workload
shifted from compute-bound SHA-256 to memory-bound hash table operations and allocator overhead.
malloc/free is now **~24%** of total cycles — the next optimization frontier for sync.

### Post-Optimization Profile: save_large (106.6µs/iter, was 131µs)

| Function | % Cycles | Before % | Notes |
|----------|----------|----------|-------|
| `encode_change_ops` | **24.20%** | **20.79%** | Now dominant (larger share of smaller total) |
| `crypto::sha256` | 6.89% | 5.40% | Chunk checksum (unavoidable) |
| `encode_sleb128` | 6.01% | 4.80% | |
| `encode_uleb128` | 5.23% | 4.65% | |
| `RleEncoder<string>::flush_run` | 4.74% | 4.52% | |
| zlib deflate | ~5.96% | ~6.10% | |
| `RleEncoder<long>::append` | 3.43% | 2.85% | |
| `RleEncoder<unsigned long>::append` | 3.32% | 2.69% | |
| `__memset_evex_unaligned_erms` | 2.45% | 2.15% | |
| `Hashtable::_M_insert_unique` (ActorId) | **0%** | **15.58%** | Completely eliminated by actor table cache |

**Analysis:** Actor table hash insertion dropped from **15.58% → 0%** (fully cached). Column
encoding (`encode_change_ops` + RLE + LEB128) is now ~45% of total — the dominant cost is
inherently sequential encoding work. Further speedup requires parallel change serialization
(when multiple changes exist) or hardware acceleration.

### Post-Optimization Profile: get_at (347ns/iter, was 7.66µs)

| Function | % Cycles | Before % | Notes |
|----------|----------|----------|-------|
| `Hashtable::find` (ChangeHash) | **24.06%** | — | Hash index lookup (O(1) per hash) |
| `_int_free` | 8.09% | 2.77% | DocState copy teardown |
| `cfree` | 7.10% | 1.49% | |
| `malloc` | 5.81% | 3.96% | DocState copy construction |
| `_Prime_rehash_policy::_M_need_rehash` | 5.10% | — | Hash table rebuild in DocState copy |
| `ObjectState::operator[]` (ObjId) | 4.83% | — | Object lookup |
| `_Prime_rehash_policy::_M_next_bkt` | 4.53% | — | |
| `DocState::DocState()` | 4.22% | — | Copy constructor for time travel |
| `DocState::changes_visible_at` | 3.41% | — | DAG traversal (now visible) |
| `DocState::apply_op` | 3.40% | — | Op replay |
| `crypto::sha256` | **0%** | **73.24%** | Completely eliminated by hash cache |

**Analysis:** SHA-256 dropped from **73.24% → 0%** — the hash cache completely eliminated
all redundant hashing. The bottleneck shifted to hash table operations (~34%), allocator
overhead (~21%), and DocState copy construction (~8%). The 22.1x speedup comes entirely from
avoiding recomputation of all change hashes on every `get_at()` call. Further optimization
of `get_at` would require avoiding the full DocState copy (e.g., persistent/immutable data
structures or snapshot-based time travel).

### Post-Optimization Hardware Counters

| Benchmark | IPC (before → after) | L1d Misses | Cache Misses | Branch Misses |
|-----------|---------------------|------------|--------------|---------------|
| text_splice_bulk | 1.13 → **1.30** | 5.1B → 5.1B | 2.7M → 3.1M | 464K → 527K |
| sync_full_round_trip | 3.68 → **2.13** | 31M → 132M | 154K → 649K | 30.6M → 47.2M |
| save_large | 3.67 → **3.59** | 442M → 385M | 119K → 243K | 6.5M → 6.4M |
| get_at | — → **2.97** | — → 1.4M | — → 87K | — → 3.0M |

**Key observations:**
- **sync IPC dropped from 3.68 → 2.13** — expected. SHA-256 is compute-dense (high IPC);
  hash table lookups and malloc/free are memory-bound (lower IPC). Despite lower IPC, the
  benchmark is 5.6x faster because vastly less total work is done.
- **sync L1d misses increased 4x** (31M → 132M) — the hash table random-access pattern causes
  more L1 misses than SHA-256's sequential block processing, but across far fewer total cycles.
- **text_splice_bulk unchanged** — expected, no optimization targeted `visible_index_to_real()`.
  Still IPC 1.30, still 5.1B L1d misses. Fenwick tree (11A.4) is needed.
- **save_large IPC stable at 3.59** — column encoding remains compute-bound.
- **get_at IPC 2.97** — a mix of hash lookups (memory-bound) and op replay (compute-bound).

### Remaining Bottlenecks After Optimization

| Benchmark | Primary Bottleneck | % of Time | Next Optimization |
|-----------|-------------------|-----------|-------------------|
| text_splice_bulk | `visible_index_to_real` O(n) scan | 98% | **11A.4 Fenwick tree** (7-25x) |
| sync_full_round_trip | malloc/free (~24%) + hash lookups (13%) | 37% | Arena allocator or pool; hardware SHA-NI for remaining hashes |
| save_large | `encode_change_ops` + RLE/LEB128 | 45% | **Parallel change serialization** (when N>1 changes) |
| get_at | Hash table ops (~34%) + allocator (~21%) | 55% | Persistent data structures (avoid DocState copy) |
| save | zlib (~12%) + column encoding (~10%) | 22% | **Parallel DEFLATE**; hardware SHA-NI |

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
   across cores via internal thread pool. Expected min(N_changes, cores)x.

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

| Priority | Optimization | macOS Bottleneck | Linux Bottleneck | Expected Speedup | Status |
|----------|-------------|-----------------|-----------------|-------------------|--------|
| **P0** | Fenwick tree for visible_index_to_real | 98% of list/text | 98% of list/text | 7-25x | **TODO** |
| ~~P0~~ | ~~Cache change hash index~~ | ~~87-94% of sync/time-travel~~ | ~~57-73% of sync/time-travel~~ | ~~5-10x~~ | **DONE** — 22.1x get_at, 5.6x sync |
| **P1** | Hardware SHA-256 (ARM Crypto + x86 SHA-NI) | 56-94% of save/sync | 5.6-6.9% of save/sync (post-cache) | 3-10x per hash | **TODO** |
| ~~P1~~ | ~~SHA-256 stack buffer~~ | ~~(included above)~~ | ~~14% malloc/free in sync~~ | ~~10-20% per hash~~ | **DONE** — stack alloc for <256B |
| ~~P1~~ | ~~Cache actor table~~ | ~~<1%~~ | ~~**15.6% of save_large**~~ | ~~1.2-1.5x save~~ | **DONE** — 1.2x save_large |
| **P2** | Parallel change serialization (thread pool) | 40% of save_large | 24% of save_large (post-opt) | min(N,cores)x | **TODO** |
| **P2** | Parallel chunk parsing (thread pool) | load hot path | load hot path | min(N,cores)x | **TODO** |
| **P2** | Parallel hash computation (thread pool) | hash cache rebuild | hash cache rebuild | min(N,cores)x | **TODO** |
| ~~P3~~ | ~~Buffer pre-sizing (reserve)~~ | ~~RLE encoder allocs~~ | ~~RLE encoder allocs~~ | ~~1.5x save~~ | **DONE** — reserve in save/serialize/hash |
| **P3** | Parallel DEFLATE compress/decompress | 0.5% of save | ~6% of save (post-opt) | 2-4x per change | **TODO** |

## Parallel Profiling: shared_mutex Bottleneck and Lock-Free Fix

### The Problem: shared_mutex Reader-Count Contention

`perf record` on parallel `bm_get_scale/1/1000000` (30 threads, 1M keys, single
shared document) revealed that **51% of CPU** was spent inside pthread_rwlock:

| Function | % CPU |
|----------|-------|
| `pthread_rwlock_unlock` (shared_mutex unlock_shared) | **30.23%** |
| `pthread_rwlock_rdlock` (shared_mutex lock_shared) | **21.21%** |
| kernel futex/scheduling | **~23%** |
| `std::map::find` (actual map lookup work) | 2.73% |
| `__memcmp_evex_movbe` (string comparison) | 1.97% |
| `thread_pool::worker()` | 1.35% |

**Root cause:** `std::shared_mutex` uses `pthread_rwlock`, which maintains an
atomic reader count. With 30 threads all acquiring `shared_lock` simultaneously,
every lock/unlock bounces the reader-count cache line across all 30 cores via the
MESI protocol. Each bounce costs ~100ns on this Xeon (cross-socket coherence).

**Hardware counter evidence:**
- **IPC: 0.49** (should be 2-4 for compute-bound workloads)
- **9.3M context switches** in 44 seconds (futex wait/wake)
- **576s sys time vs 330s user time** — kernel spent more time than userspace
- Only **2.97% L1d miss rate** — the data itself fits in cache; the stalls are
  from atomic coherence traffic, not data cache misses

### The Fix: Lock-Free Read Path

Added `set_read_locking(bool)` to Document. When disabled, read methods skip the
`shared_lock` entirely. Safe when the caller guarantees no concurrent writers
(which is the common case for read-heavy workloads and all benchmarks).

Implementation: lightweight RAII `ReadGuard` that conditionally acquires the lock:
```cpp
struct ReadGuard {
    std::shared_lock<std::shared_mutex> lock_;
    bool engaged_;
    explicit ReadGuard(std::shared_mutex& mtx, bool engage)
        : lock_{mtx, std::defer_lock}, engaged_{engage} {
        if (engaged_) lock_.lock();
    }
};
```

### Results: Before vs After

| Keys | Before (shared_mutex) | After (lock-free) | Improvement |
|------|----------------------|-------------------|-------------|
| 100K | 10.6 M ops/s (2.4x) | **95.0 M ops/s (13.5x)** | 9x faster |
| 500K | 10.2 M ops/s (1.8x) | **66.0 M ops/s (11.7x)** | 6.5x faster |
| 1M | 10.2 M ops/s (1.9x) | **55.0 M ops/s (9.6x)** | 5.4x faster |

(Speedup numbers in parentheses are vs sequential single-threaded.)

**Post-fix hardware counters:** IPC improved from **0.49 → 1.16** (2.4x better
CPU utilization). The CPU is now spending time on actual map lookups instead of
stalling on cache-line coherence traffic.

### Why Not Linear (30x)?

At 1M keys, we achieve 9.6x on 30 cores (32% efficiency). The remaining gap is:

1. **Thread pool dispatch overhead** — `parallelize_loop` divides work into blocks,
   each wrapped in `std::function` (one heap allocation per block)
2. **NUMA effects** — this Xeon has 30 cores across 2 sockets; cross-socket memory
   access adds ~100ns latency vs ~10ns same-socket
3. **std::map tree traversal** — `map_entries.find(key)` does O(log n) pointer-chasing
   through a red-black tree, which is inherently cache-unfriendly. With 1M keys,
   each lookup traverses ~20 tree nodes, many causing L1/L2 cache misses
4. **String comparison overhead** — `memcmp` for key matching shows up at 2% even
   in the lock-free profile

### Remaining Optimization Opportunities for Reads

| Approach | Expected Impact | Complexity |
|----------|----------------|------------|
| Replace `std::map` with `std::unordered_map` for map_entries | 2-3x (O(1) vs O(log n) lookup) | Medium |
| Replace `std::unordered_map` with open-addressing hash map | 1.5-2x (cache-friendly) | Medium |
| NUMA-aware memory placement | 1.3-1.5x on multi-socket | High |
| Eliminate `std::function` in parallelize_loop | 1.1-1.2x (remove heap alloc) | Low |

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
