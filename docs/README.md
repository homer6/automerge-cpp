# automerge-cpp documentation

One file per subject. A page says what exists, why, how to verify it, and how to change it.

| Page | Subject |
|---|---|
| [`api.md`](api.md) | Public API reference: `Automerge`, transactions, values, save and load, sync |
| [`style.md`](style.md) | Code style for this repository |
| [`json-integration.md`](json-integration.md) | Converting between documents and JSON |
| [`benchmark-results.md`](benchmark-results.md) | Benchmark results (macOS) |
| [`benchmark-results-linux.md`](benchmark-results-linux.md) | Benchmark results (Linux) |
| [`change-application.md`](change-application.md) | Checked external change application and pending dependencies |
| [`change-hashes.md`](change-hashes.md) | Upstream-compatible change hash definition and fixtures |
| [`transactions/rollback.md`](transactions/rollback.md) | Transaction rollback design and verification |
| [`incremental-save.md`](incremental-save.md) | Incremental persistence and public change-set queries |
| [`text/unicode-scalar-values.md`](text/unicode-scalar-values.md) | UTF-8 validation and Unicode scalar-value text indexing |
| [`remote-patches.md`](remote-patches.md) | Patches from apply, merge, sync receive, and load |
| [`sync-wire-format.md`](sync-wire-format.md) | Upstream-compatible sync messages, bloom filters and persisted state |
| [`plans/`](plans/) | Design plans that preceded implementation |
| `Doxyfile` | Doxygen configuration for the generated reference |

This is oramasearch's private copy; what liquid needs from it is tracked at
https://github.com/oramasearch/automerge-cpp/issues/13. Each change under that epic adds or
updates a page here in the same commit.
