# AGENTS.md

Read `CLAUDE.md` first. It is the instruction file for this repository and applies in full to
every agent, Codex included. Then `README.md` and `docs/README.md`.

This is oramasearch's private copy of automerge-cpp, consumed by `oramasearch/liquid` as the
submodule `modules/automerge-cpp`. liquid's CRDT layer (`src/crdt/`) is its only client; what
liquid needs from it is tracked at https://github.com/oramasearch/automerge-cpp/issues/13.

Non-negotiables, restated from `CLAUDE.md` and the org conventions in
[`oramasearch/orama-bridge`](https://github.com/oramasearch/orama-bridge):

- Every change documents itself in `docs/*.md` **in the same commit**, and updates the status
  lines in `README.md` and `CLAUDE.md` when they stop being true.
- **No AI attribution anywhere.** No `Co-Authored-By`, no "Generated with", no session or
  model trailers, in commits or pull requests.
- No time estimates. The word "seam" is banned.
- Work on a feature branch named for the issue, open a pull request that references the issue,
  and never push to `main` directly. Commit means push.
- Upstream compatibility is the bar: saved bytes, change hashes and sync messages must
  interoperate with automerge-rs; fixtures under `tests/fixtures/` prove it.
- Work is tracked from the master epic https://github.com/oramasearch/liquid-bridge/issues/7;
  the structure is in the bridge's `docs/tracking.md`.
