# automerge-rs compatibility fixtures

The committed fixtures prove byte-level compatibility with automerge-rs 0.7.4. The Rust
generator version is pinned in `tests/fixtures/generator/Cargo.toml`, its resolved dependency tree
is committed in `Cargo.lock`, and `tests/fixtures/generator/VERSION` records the upstream version
for tools and reviewers.

## Cases

Every named case has three generated files: a concatenated change stream named
`<case>.automerge`, a compact document named `<case>.document.automerge`, and one expected change
hash per line in `<case>.hashes`.

| Case | What it proves | C++ to Rust |
|---|---|---|
| `scalar_values` | Null, Boolean, signed integer, unsigned integer, 64-bit float, string, bytes, counter, and timestamp tags | Checked |
| `nested_objects` | Maps and lists nested in both map and list positions | Checked |
| `list_edits` | List insertion, replacement, deletion, and insertion beside a tombstone | Checked |
| `unicode_text` | Text splices index multi-byte UTF-8 by Unicode scalar value | Checked |
| `marks` | Rich-text mark anchors, value, name, and expansion flags | Checked |
| `counter_increments` | Counter creation and positive and negative increment operations | Checked |
| `deletes_tombstones` | Map and list deletes preserve the operation history needed for later insertion | Checked |
| `concurrent_conflicts` | Concurrent map puts retain both values and deterministic heads | Checked |
| `commit_metadata` | A change carries a message and timestamp | Checked |
| `long_history` | A linear history of 1,000 changes, including compressed compact-document columns | Checked |
| `interleaved_histories` | Two fixed actors alternate causally linked changes | Checked |
| `empty_document` | An empty compact document loads and an empty C++ save loads in automerge-rs | Checked |
| `incremental_*` | A compact base followed by two incremental change streams | Checked |
| `sync_exchange/*` | Every v1 sync message in a complete two-peer exchange | Checked |

The original `map_puts`, `list_inserts`, `text_splice`, and `two_actor_merge` `.am` cases remain as
focused change-hash regressions.

## Directions

The always-enabled C++ fixture tests load every committed Rust change stream and compact document.
For change streams they decode each change, verify its hash, re-encode the chunk byte-identically,
and require `Document::save()` to reproduce the full stream.

When Cargo is available, CTest also runs the reverse direction. A C++ fixture writer creates each
supported case under the build directory. The Rust generator loads those files and compares their
change count, operation count, actor count, and hydrated state with the committed Rust case. The
reverse test has the CTest labels `compatibility` and `cargo`; it is not registered when Cargo is
unavailable.

## Regeneration

Run the generator from its package directory:

```bash
cd tests/fixtures/generator
cargo run --quiet -- generate
```

Then configure, build, and run both compatibility directions with:

```bash
cmake -S . -B build -DAUTOMERGE_CPP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure -L compatibility
```

Regeneration is deterministic because every case uses fixed actor IDs, values, operation order,
messages, and timestamps. Review generated binary changes together with the generator source and
this table.
