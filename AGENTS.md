# OpenSupCom-LOUD contribution workflow

Read `CONTRIBUTING.md`, `docs/provenance-and-compatibility.md` and
`docs/loud-fork.md` before changes. Keep upstream copyright/license notices.
Read `docs/fork-workflow.md` for branches, checks and publishing. If a local
`LOCAL_WORKSPACE.md` exists, read its machine-specific boundaries too; it stays
ignored and must never enter a public commit.

- `origin` is `https://github.com/jbaersch/opensupcom-loud.git`, the public fork.
  `upstream` is `https://github.com/bwiemz/supcom-engine.git`; never push upstream.
- Work on `codex/loud` or a focused `codex/<topic>` branch from it. Keep upstream's
  `main` separate; inspect updates before an explicit integration and validation.
- Make one logical change per commit, include the reason and relevant tests,
  and stage specific files. Do not rewrite published history or force-push.
- Complete the applicable checks before a normal fast-forward push to this fork.
  Publication and ordinary pushes of reviewed project work are authorized.
  Opening upstream PRs, filing issues or contacting maintainers requires a request.

- Engine primitives run the original scripts; do not embed or distribute game data.
- Use synthetic regression tests and the existing deterministic command stream.
- Preserve replay/save identity guards and document development incompatibilities.
- Keep source installs read-only and give test runs separate writable state.
- See `docs/loud-commit-index.md` for dependency order and the tested upstream base.
- Private notes, assets, captures, logs and runtime state do not belong in Git.
- Run the applicable checks in `CONTRIBUTING.md`; distinguish Linux local results
  from Windows, cross-OS, full-mod and multiplayer acceptance.
