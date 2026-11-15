# Unicode scalar-value text indexing

## What exists

Text objects use Automerge's default `UnicodeCodePoint` indexing. Each Unicode scalar value is
stored in one operation, encoded as its complete UTF-8 byte sequence. All positions and lengths
for text objects count scalar values:

| API surface | Unit for text objects |
|---|---|
| `Transaction::splice_text` `pos` and `del` | Unicode scalar values |
| `Document::length` and `Document::length_at` | Unicode scalar values |
| `Document::get`, `Document::values`, and historical equivalents | One scalar value per element |
| `Document::cursor` and `Document::resolve_cursor` | Unicode scalar values |
| `Mark::start` and `Mark::end` | Unicode scalar values |
| `PatchSpliceText::index` and `delete_count` | Unicode scalar values |

`Document::text` and `Document::text_at` return ordinary UTF-8 `std::string` values. The C++
`std::string::size()` result is therefore a byte count; use `Document::length` when a text index
or scalar-value count is needed.

`Transaction::splice_text` validates all inserted text before changing the document and returns
`std::expected<void, Error>`. Invalid UTF-8 produces `ErrorKind::invalid_operation`. Incoming
changes are also rejected when a text insertion does not contain exactly one valid UTF-8 scalar
value.

## Why

The native automerge-rs default is `TextEncoding::UnicodeCodePoint`. Matching that model keeps
operation boundaries intact for change hashing and columnar encoding, and prevents concurrent
edits from leaving partial UTF-8 byte sequences in a document.

A grapheme cluster can contain several scalar values. For example, a family emoji joined with
zero-width joiners and a base letter followed by a combining mark each occupy multiple text
positions. Local or concurrent edits may split those clusters, as upstream Automerge allows, but
they cannot split an individual scalar value or produce invalid UTF-8.

## How to verify

Run the focused text tests:

```bash
./build/tests/automerge_cpp_tests --gtest_filter='Document.*unicode*:\
Document.text_splice*:\
Document.concurrent_inserts_in_multibyte_text_merge_as_valid_utf8:\
Document.marks_and_cursors_use_unicode_scalar_indices:\
Document.apply_changes_rejects_text_ops_with_multiple_scalars:\
Transaction.rollback_restores_multibyte_text_scalar_splice'
```

Run the complete suite before committing:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

## How to change it

UTF-8 validation and scalar splitting live in `src/utf8.hpp`. Text operation construction is in
`src/transaction.cpp`; incoming operation validation is in `src/document.cpp`. Any change to the
indexing rule must update those paths together with cursor, mark, patch, merge, and save/load tests,
then update this page and `docs/api.md`.
