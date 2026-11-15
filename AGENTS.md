# AGENTS.md

Read `CLAUDE.md` first. It is the instruction file for this repository and applies in full to
every agent, Codex included. Then `README.md` and `docs/README.md`.

Non-negotiables, restated from `CLAUDE.md`:

- Every change documents itself in `docs/*.md` **in the same commit**, and updates the status
  lines in `README.md` and `CLAUDE.md` when they stop being true.
- **No AI attribution anywhere.** No `Co-Authored-By`, no "Generated with", no session or
  model trailers, in commits or pull requests.
- No time estimates. The word "seam" is banned.
- Work on a feature branch and open a pull request; never push to `main` directly.
- Upstream compatibility is the bar: saved bytes, change hashes and sync messages must
  interoperate with automerge-rs; fixtures under `tests/fixtures/` prove it.
