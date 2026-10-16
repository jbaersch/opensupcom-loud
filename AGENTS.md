# Contribution branch guidance

Read `CONTRIBUTING.md`, `docs/provenance-and-compatibility.md` and
`docs/loud-fork.md` before changes. Keep upstream copyright/license notices.

- Engine primitives run the original scripts; do not embed or distribute game data.
- Use synthetic regression tests and the existing deterministic command stream.
- Preserve replay/save identity guards and document development incompatibilities.
- Keep source installs read-only and give test runs separate writable state.
- See `docs/loud-commit-index.md` for dependency order and the tested upstream base.
- Private notes, assets, captures, logs and runtime state do not belong in Git.
- Run the applicable checks in `CONTRIBUTING.md`; distinguish Linux local results
  from Windows, cross-OS, full-mod and multiplayer acceptance.
