# History compaction and shallow documents

History compaction separates the state needed to continue editing from the operation history used
for time travel and old-branch reconciliation. `save_compact()` writes an upstream-compatible
compact document chunk. `load_shallow()` reads that chunk into current state without retaining its
pre-checkpoint changes or operations in the resident `Document`.

## Checkpoint contents and boundary

A shallow checkpoint keeps:

- the current visible maps, lists, text, counters, tables, and marks;
- the document heads represented by that state;
- actor clocks and operation counters needed to allocate non-conflicting identifiers;
- the compact source bytes, so a later `save_compact()` can combine the checkpoint with resident
  post-checkpoint changes; and
- every change accepted after the checkpoint.

It deliberately loses the resident graph before the checkpoint. Consequently, historical reads
such as `get_at()` cannot address heads before the checkpoint, and a change whose dependencies lie
only in the pruned history cannot be applied. Such a change remains in the missing-dependency queue.
Applications that may need either capability must retain the full-history document as an archive.

Every locally authored post-checkpoint change names the checkpoint heads as dependencies in
addition to the current heads. Those stable anchors preserve identity: a post-checkpoint change can
merge with a full-history peer that knows the same checkpoint heads. Remote changes whose dependency
closure reaches the checkpoint are accepted normally.

## Save, load, merge, and sync

`save()` continues to write this copy's change-chunk stream. On a shallow document that stream
contains only resident post-checkpoint changes. `save_compact()` writes one upstream compact
document chunk. The encoder is byte-for-byte checked against the automerge-rs fixtures for maps,
lists, text, and a two-actor merge.

An automerge-rs compact document chunk is a columnar representation of the complete operation graph;
upstream loading reconstructs that graph. It is compact on disk but is not, by itself, a history-
pruning format. Shallow loading is the C++ policy that materializes only visible state and the
checkpoint identity while keeping the source chunk opaque.

A full peer can send a shallow peer changes made after the checkpoint because the shallow peer
recognizes the checkpoint heads. A shallow peer can send only its resident post-checkpoint changes.
The full peer already has the checkpoint closure and can apply those changes. Merge and the sync
protocol support both directions. If the full peer sends a branch based only on a head older than
the checkpoint, the shallow peer queues it and reports that old dependency from `get_missing_deps()`.
This matches upstream's allowance for changes following a document chunk to remain pending when
their dependencies are unavailable; it does not recreate discarded history.

## Bounded historical reads

Historical reads cache the reconstructed object state and heads from the latest request. A later
request descended from that point replays only the intervening changes. The compaction checkpoint is
also a cache floor. A shallow document therefore cannot replay below its checkpoint, while repeated
or nearby reads above it are bounded by their distance from the cached heads rather than the entire
document history.

For a 10,000-change document, a historical read near the tip averaged 19,814.1 microseconds without
a cached point and 0.9687 microseconds from the adjacent cached point. Each result is the median of
three release-build runs; each run averaged 20 fresh document copies.

## Measurements

The table compares full and shallow loading of the same compact document chunk. Heap figures are
the median live heap after loading across three fresh processes. Measurements used a release build
on an Apple M3 Max with 64 GiB of memory and Apple Clang 15.

| Changes | Compact bytes | Before: full load | Before: full heap | After: shallow load | After: shallow heap |
|---:|---:|---:|---:|---:|---:|
| 1,000 | 1,692 | 4.492 ms | 0.800 MiB | 0.741 ms | 0.148 MiB |
| 10,000 | 19,017 | 77.429 ms | 7.571 MiB | 6.666 ms | 1.320 MiB |
| 100,000 | 207,417 | 4,041.650 ms | 79.822 MiB | 70.500 ms | 1.536 MiB |

Reproduce the files and measurements with:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON
cmake --build build-release --target history_compaction_measure
./build-release/benchmarks/history_compaction_measure generate 1000 \
    build-release/1000.am build-release/1000.compact.am
./build-release/benchmarks/history_compaction_measure load full build-release/1000.compact.am
./build-release/benchmarks/history_compaction_measure load shallow build-release/1000.compact.am
./build-release/benchmarks/history_compaction_measure get-at 10000
```

Repeat `generate` and both `load` commands with 10,000 and 100,000 changes. Run each load in a
fresh process and take the median of three results.

## Liquid checkpoint procedure

At a directory checkpoint, liquid should:

1. persist the full-history document or its journal in archival storage;
2. write `save_compact()` output as the checkpoint snapshot and record its heads;
3. atomically publish the snapshot before retiring journal entries covered by those heads;
4. reopen the active directory with `load_shallow()` and replay later drips; and
5. route peers based on history needs: normal post-checkpoint peers use the shallow document, while
   old branches require the archive or a fresh snapshot.

This fits liquid's checkpoint-at-cut and every-64-drips policy. The archive is required until the
application has established that no supported peer or historical query can refer to older heads.

## Upstream references

The pinned automerge-rs sources define compact saving in
[`autocommit.rs`](../upstream/automerge/rust/automerge/src/autocommit.rs) and document-chunk loading
in [`storage/load.rs`](../upstream/automerge/rust/automerge/src/storage/load.rs). The compatibility
fixture generator uses the same pinned dependency under `tests/fixtures/generator/`.

## Verification

`document_test.cpp` covers shallow round trips, local transactions, compact re-saving, bidirectional
merge, bidirectional sync, queued pre-checkpoint changes, and cached historical reads.
`change_hash_fixture_test.cpp` compares compact output with automerge-rs document fixtures.
`history_compaction_measure.cpp` generates the measurement documents and reports load, heap, and
historical-read results.
