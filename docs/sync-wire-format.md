# Sync wire format

`SyncMessage::encode()` and `SyncMessage::decode()` exchange Automerge v1 sync messages without
an application-specific transport encoding. The bytes are compatible with automerge-rs messages
whose changes are complete change chunks.

## Message layout

All counts and byte lengths are unsigned LEB128 values. Hashes are the 32 raw bytes of a
`ChangeHash`. Fields occur in this order:

1. `0x42`, the Automerge v1 sync message type;
2. the number of `heads`, followed by each hash;
3. the number of `need` hashes, followed by each hash;
4. the number of `have` summaries;
5. for each summary, its length-prefixed `last_sync` hashes, then the byte length and bytes of its
   bloom filter;
6. the number of changes, each encoded as a byte length followed by one complete Automerge change
   chunk; and
7. when present, the number of supported capabilities followed by their one-byte identifiers.

Capability `0x01` means v1 messages. This copy advertises only that capability so an automerge-rs
peer continues to send individually length-prefixed change chunks. It accepts and preserves other
capability identifiers when decoding and re-encoding a v1 message. Automerge's `0x43` v2 message
extension can carry a saved document chunk and is outside this change-chunk codec.

An empty message is `42 00 00 00 00`. Decode failures return `ErrorKind::decoding_error`; truncated
fields, invalid bloom filters, invalid change chunks, unsupported message types, and trailing bytes
after a capability list are rejected.

## Bloom filters

The default filter matches automerge-rs: 10 bits per entry and 7 probes. The encoded filter is:

1. entry count;
2. bits per entry;
3. probe count; and
4. `ceil(entry_count * bits_per_entry / 8)` bytes, least-significant bit first within each byte.

The first three 32-bit little-endian words of a change hash initialize `x`, `y`, and `z`, reduced
modulo the filter's bit count. The first probe is `x`; each later probe updates `x = x + y` and
`y = y + z`, reducing both modulo the bit count. Decoding retains the parameters carried by the
filter rather than assuming host byte order.

## Persisted sync state

`SyncState::encode()` matches upstream's persistent record: byte `0x43`, then the length-prefixed
`shared_heads`. These heads are the connection-independent fact that both peers have acknowledged.
Session fields such as the last sent and received heads, requested hashes, sent hashes, and
in-flight flags are deliberately reset by `decode()`. The decoded state starts a new handshake from
the shared heads and can resume incremental synchronization after a restart.

## Compatibility fixtures

`tests/fixtures/generator/` is pinned to automerge 0.7.4. It creates two documents with fixed actor
IDs, constrains both peers to capability `0x01`, runs their complete sync exchange, and writes the
initial `.am` files plus every raw `.sync` message in transmission order under
`tests/fixtures/sync_exchange/`.

Regenerate all upstream fixtures with:

```bash
cd tests/fixtures/generator
cargo run --quiet
```

`tests/sync_wire_format_test.cpp` decodes and re-encodes every message byte-identically, applies a
decoded message through the patch-returning receive API, replays the exchange with either Rust peer
replaced by this copy, and resumes synchronization from encoded states.

## Verification

Run the complete suite:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
