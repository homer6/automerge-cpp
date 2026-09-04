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

## Compatibility

Documents written by earlier releases used a private nested-change format and a hash that did not
include operation contents. They are intentionally not loaded by this release: `Document::load()`
returns `std::nullopt` rather than silently assigning different hashes to the existing dependency
graph. Recreate or export those documents before upgrading.

## Fixtures

`tests/fixtures/generator/` is a standalone Rust package pinned to automerge 0.7.4. It creates
fixed-actor examples for map puts, list inserts, text splicing, and a two-actor merge. For each
case it writes an incremental saved change stream (`.am`) and one lower-case hexadecimal change
hash per line (`.hashes`). The incremental stream is chosen because it contains the complete
upstream change chunks that define the hashes.

Regenerate the committed fixtures with:

```bash
cd tests/fixtures/generator
cargo run --quiet
```

The `ChangeHashFixture.matches_automerge_rs_change_chunks` test loads every `.am` file, validates
and decodes its chunks, compares every computed hash to its `.hashes` file, and verifies that
re-encoding produces identical bytes. It also verifies that loaded document heads are rebuilt from
those same hashes.
