# Incremental save, load, and change queries

`Document` can persist only the changes added since its latest save, apply an
incremental chunk stream to an existing document, and query subsets of its change history.

## Saving

`save()` retains this copy's established full-save format. A nonempty document is serialized as
concatenated, complete Automerge change chunks in history order. It does not emit automerge-rs's
compact document chunk. An empty document is serialized with the private empty-document marker
described in [change hashes](change-hashes.md). Calling `save()` advances the incremental cursor to
the current heads.

`save_incremental()` serializes every change that is not a transitive dependency of the saved
heads. Its result is a concatenated stream of upstream change chunks and is empty when there are no
new changes. A nonempty result advances the cursor to the current heads. Before the first full or
incremental save, the cursor is empty, so `save_incremental()` returns the complete history.

The cursor is copied by the deep-copy constructor and assignment. `fork()` starts its own empty
cursor, matching automerge-rs `AutoCommit` behavior.

## Loading

`load_incremental(bytes)` accepts concatenated change chunks, an upstream compact document chunk,
or this copy's empty-document marker. Compact document loading supports independently DEFLATE-
compressed change and operation columns. The decoder reconstructs predecessor operations and
change hashes, and verifies that the reconstructed heads match the document chunk.

The complete input is parsed before mutation. Reconstructed changes then pass through the same
validation, deduplication, dependency queue, and apply path as `apply_changes()`. The return type is
`std::expected<std::size_t, Error>`; its value counts changes applied during the call, including
previously queued changes that become ready. A well-formed change with unavailable dependencies is
queued and contributes zero until those dependencies arrive.

## Change-set queries

- `get_changes_since(heads)` returns this document's changes that are not reachable from `heads`,
  in history order. Unknown heads exclude nothing.
- `get_changes_added(other)` returns changes present in `other` but absent from this document, in
  `other`'s history order.
- `get_last_local_change()` returns the newest change authored by the document's current actor, or
  `std::nullopt` when that actor has no change.

## Compatibility fixtures

`tests/fixtures/generator/` is pinned to automerge 0.7.4. It writes compact `.document.am` variants
of the map, list, text, and two-actor fixtures. The incremental fixture contains a compressed base
document followed by two separately saved change chunks. Tests reproduce the fixed-actor edits and
compare both C++ incremental outputs byte-for-byte with automerge-rs, then load the Rust base and
incremental chunks here.

Regenerate and verify with:

```bash
cd tests/fixtures/generator
cargo run --quiet
cd ../../..
cmake --build build && ctest --test-dir build --output-on-failure
```
