# Transaction rollback

## What exists

Transactions are atomic. A throwing `Document::transact()` callable rolls back its writes and
rethrows. A `Transaction` destroyed before commit also rolls back. `Transaction::rollback()` is
public, idempotent, and prevents the transaction from committing. `CommitOptions` supplied to a
throwing transaction are discarded with its operations and cannot affect a later transaction.

## Design

The implementation applies local writes eagerly so transaction reads retain their existing
read-your-writes behavior. The source-only `detail::TransactionRollback` journal is held through
an opaque pointer, keeping its inverse-record types out of the installed public header. Each
mutator records a compact inverse before it changes `DocState`:

- map mutations retain the previous key entries;
- list and text mutations retain inserted element IDs, previous element values, or visibility;
- marks and newly created objects retain their IDs for removal;
- commit retains the prior sequence number, heads, actor clock entry, and change-history size.

Rollback applies these records in reverse dependency order, restores `next_counter`, clears
pending operations, and marks the transaction inactive. Commit marks the transaction inactive
only after it has appended the change, so an exception while constructing a change can still be
rolled back. Commit options are passed by value only after the transaction callable succeeds;
they are not retained in document state before commit.

## Why this design

Staging a copy of document state would make reads require an overlay and would copy growing
documents for ordinary transactions. The inverse journal keeps eager reads and allocates only
for data touched by the transaction.

## Performance

The benchmark command below compared `origin/main` after the upstream chunk-encoding change
with this rollback implementation on an Apple M3 Max release build. Five repetitions with a
0.5-second minimum produced these median CPU times:

| Benchmark | Before | After | Difference |
|---|---:|---:|---:|
| `bm_map_put` | 2,050 ns | 2,108 ns | +2.8% |
| `bm_list_insert_append` | 60,172 ns | 59,278 ns | -1.5% |
| `bm_text_splice_bulk` | 3,864,727 ns | 3,738,530 ns | -3.3% |

The list and text baseline runs varied substantially under system load. The map result varied
by less than 1.3% in each run and shows a 2.8% increase, attributable to allocating the opaque
journal when the transaction is created. The journal is not on the per-operation hot path.

```bash
./build-release/benchmarks/automerge_cpp_benchmarks \
    --benchmark_filter='^(bm_map_put|bm_list_insert_append|bm_text_splice_bulk)$' \
    --benchmark_min_time=0.5s \
    --benchmark_repetitions=5 \
    --benchmark_report_aggregates_only=true
```

## Verify and change safely

Run the full test suite and the benchmark command above. The regression tests cover a throwing
transaction across map, list, text, counter, mark, and nested-object writes; explicit rollback;
read-your-writes; discarded commit options; and fixed committed bytes using the current upstream
chunk encoding.
`ChangeHashFixture.matches_automerge_rs_change_chunks` independently verifies that encoding and
the resulting hashes against automerge-rs fixtures. When adding a mutator, add its inverse record
before the eager `DocState` write and extend the throwing-transaction test.
