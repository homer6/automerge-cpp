# Change hashes

`ChangeHash` identifies one encoded Automerge change. It is content-addressed:
the hash changes when any dependency, metadata field, actor reference, column, or operation in
the change changes.

## Definition

For a change chunk body `body`, the hash is SHA-256 over:

1. the change chunk type byte (`0x01`),
2. the ULEB128 encoding of `body.size()`, and
3. `body`.

This is the definition used by automerge-rs. The complete chunk also contains the Automerge magic
bytes and a four-byte checksum before those fields. That checksum is the first four bytes of the
same hash; it is validated when a chunk is loaded and is not separately appended to the hash input.

The body contains dependency hashes, the author actor ID, sequence and start-operation numbers,
timestamp, message, other actor IDs, and the operation columns. Actor references inside a change
are encoded with the author first and all other referenced actors in lexicographic order.

`Document::save()` writes each nonempty history as concatenated complete change chunks. Loading
recomputes heads from the chunk hashes and uses those hashes for dependencies, `get_changes`,
`get_missing_deps`, and synchronization.

## Lazy encoding

A local commit records the semantic change immediately but does not encode or hash it. While a
local chain remains unencoded, each change retains its parent by history index. The first operation
that needs a hash or encoded change resolves those parent links in history order, encodes each
change once, computes its SHA-256 once, and stores the complete chunk with the hash.

Hash-bearing operations include heads and change queries, persistence, synchronization, historical
reads, and checked external change application. `save()` and `save_incremental()` append the stored
chunks directly. Generated and decoded sync messages carry the stored chunks through encoding and
receive; changing a public message's `changes` invalidates that cache and re-encodes the modified
values. Loading a change stream retains its validated chunk bytes. A compact document chunk has no
individual change chunks to retain, so its reconstructed changes are encoded once during loading.

The transaction rollback journal retains the prior deferred head as well as concrete heads. A
failed commit therefore restores both forms of dependency state. Materialization does not change
the hash definition or encoded bytes.

## Compatibility

Documents written by earlier releases used a private nested-change format and a hash that did not
include operation contents. They are intentionally not loaded by this release: `Document::load()`
returns `std::nullopt` rather than silently assigning different hashes to the existing dependency
graph. Recreate or export those documents before upgrading.

## Fixtures

`tests/fixtures/generator/` is a standalone Rust package pinned to automerge 0.7.4. It creates
fixed-actor examples for map puts, list inserts, text splicing, commit metadata, and a two-actor
merge. The metadata case commits the message `import customer record` and the fixed time
`1725000000123`; its automerge-rs hash is
`5dc5159e7f5486cd5c559b567d221026a79315522fcb834d01aafdc820e2acbf`. For each case the
generator writes an incremental saved change stream (`.am`), a compact full save
(`.document.am`), and one lower-case hexadecimal change hash per line (`.hashes`). The
incremental stream contains the complete upstream change chunks that define the hashes; the
compact saves exercise document chunk reconstruction during incremental load.

The generator also writes `incremental_base.am`, `incremental_first.am`, and
`incremental_second.am`. These capture a compressed full save followed by two incremental saves
from a fixed actor.

Regenerate the committed fixtures with:

```bash
cd tests/fixtures/generator
cargo run --quiet
```

The `ChangeHashFixture.matches_automerge_rs_change_chunks` test loads every `.am` file, validates
and decodes its chunks, compares every computed hash to its `.hashes` file, and verifies that
re-encoding produces identical bytes. It also verifies that loaded document heads are rebuilt from
those same hashes.

## Performance

Measurements were taken on an Apple M3 Max with Apple Clang 15 in a release build. Each result is
the median CPU time from five repetitions of `bm_map_put`, which performs one map put per
transaction on a growing document.

| Source | CPU median | Difference from pre-#15 |
|---|---:|---:|
| `5cbc9da^` (before #15) | 1,163 ns | baseline |
| `origin/main` at `edd38eb` | 2,151 ns | +85.0% |
| This change | 241 ns | -79.3% |

The acceptance ceiling is 1,396 ns, 20% above the measured pre-#15 cost. The lazy commit path is
below that ceiling because it does not encode or hash until a hash-bearing operation requests the
result.

The `origin/main` profile sampled 2,895 `bm_map_put` stack observations. SHA-256 accounted for
1,187 samples (41.0%), the change-body serialization branch accounted for 987 (34.1%), and named
`encode_change_ops` frames accounted for 951 recursive samples (32.8%). Allocation and release
functions were concentrated below the column encoders; the five largest allocator leaves were
`_nanov2_free` (352), `nanov2_malloc` (149), `_malloc_zone_malloc` (130), `_free` (121), and
`operator new` (54). This identified encoding allocation and SHA-256 as the redundant commit work.

Configure and run the benchmark with:

```bash
cmake -S . -B build-release \
    -DCMAKE_BUILD_TYPE=Release \
    -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON
cmake --build build-release --parallel
./build-release/benchmarks/automerge_cpp_benchmarks \
    --benchmark_filter='^bm_map_put$' \
    --benchmark_min_time=0.5s \
    --benchmark_repetitions=5
```

Run that command from source trees at `5cbc9da^`, `origin/main`, and this change. On macOS, reproduce
the sampling profile with a longer benchmark run:

```bash
./build-release/benchmarks/automerge_cpp_benchmarks \
    --benchmark_filter='^bm_map_put$' \
    --benchmark_min_time=10s > profile-benchmark.txt 2>&1 &
profile_pid=$!
sample "$profile_pid" 5 -file main.sample
wait "$profile_pid"
```
