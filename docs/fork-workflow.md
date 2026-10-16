# Working in the public LOUD fork

The development repository is
[jbaersch/opensupcom-loud](https://github.com/jbaersch/opensupcom-loud), a public
GitHub fork of [bwiemz/supcom-engine](https://github.com/bwiemz/supcom-engine).
The existing [contribution guide](../CONTRIBUTING.md) applies; this page specifies
the fork's branch and publishing workflow. The tested engine starts from upstream
`a799c964`; see [scope and evidence](loud-fork.md) before interpreting newer
upstream features as part of the LOUD acceptance.

## Branches and remotes

- `origin`: this public fork. `codex/loud` is the maintained LOUD branch and the
  fork's default branch.
- `upstream`: the original repository, used for fetching and comparison.
- `main`: upstream's line, kept separate from the LOUD integration branch.
- `codex/<topic>`: focused fixes or experiments based on `codex/loud`.
- `codex/upstream-<topic>`: one upstream contribution based on `upstream/main`,
  developed in its own worktree. It must not inherit the LOUD integration stack.

```sh
git fetch origin
git fetch upstream
git switch codex/loud
git pull --ff-only origin codex/loud
git switch -c codex/short-topic
```

Use worktrees for concurrent checkouts. Inspect upstream changes and merge them
on a dedicated integration branch with their own checks; do not replace a tested
development build during acceptance. A fetch does not adopt new engine behavior.

For a small contribution to the original project:

```sh
git fetch upstream
git worktree add .worktrees/upstream-short-topic \
  -b codex/upstream-short-topic upstream/main
# Apply only the selected fix and its tests in that worktree.
```

Before publishing, review `upstream/main..HEAD`, check for newer competing PRs,
and validate the candidate on that base. A clean cherry-pick is not a passed test.
Inherited CI failures must be identified separately; never loosen a gate or copy
an already-open fix into an unrelated contribution just to make it green.

## Commit and validate

Keep one logical change per commit. Explain the trigger, resulting behavior and
why it is compatible with the game's source contracts. Use a scoped imperative
subject such as `sim: preserve ...` or `lua: expose ...`. Stage explicit files;
inspect the staged diff, including newly added files, before committing.

Use the lowest meaningful regression test. Run the applicable checks from
`CONTRIBUTING.md`, including LLVM 22 formatting, the lint ratchet where C++ changes,
architecture/serialization checks and data-free tests. For example:

```sh
git diff --cached --check
bash tools/check_format.sh origin/codex/loud
cmake --build --preset linux-debug
ctest --preset linux-debug -LE 'data|lint' --output-on-failure
```

Changes to gameplay contracts need the corresponding original-data check on a
developer's own installation. Simulation changes need deterministic continuation
and replay checks; arithmetic changes also need cross-OS validation before
claiming cross-OS acceptance. Rendering changes need their relevant visual checks.
Documentation-only changes need content/link/diff checks rather than a repeated
full gameplay suite. Record what was tested and what remains unverified.

## Push and review

```sh
git status --short
git log --oneline origin/codex/loud..HEAD
git push -u origin HEAD
```

On a topic branch this publishes the topic, not the maintained branch. When it is
reviewed and validated, integrate it into `codex/loud` and push normally. Preserve
published history: no force pushes, branch deletion, mirror pushes or all-ref
uploads as routine development. Keep fixes intended for upstream review separate
from broad performance or presentation work. CI results belong to their exact
commit; a queued run is not a passed gate.

Upstream PRs and maintainer messages are separate actions. A public fork makes
the changes available for inspection without granting maintainers write access
or automatically sending a contribution proposal.

The [collaboration guide](collaboration.md) defines the small contribution queue,
test handoffs and AI provenance notes. Use the PR template to keep each proposal
reviewable without requiring access to a private chat or local evidence files.

## Content boundary

Publish engine source, synthetic tests and their documentation. Preserve all
license/copyright notices and disclose behavioral sources. Keep installation
assets, extracted scripts, game captures, saves, raw runtime evidence, machine
configuration and private planning outside Git, including historical commits.
Never merge a private development/history branch into this public line.

Builds and saves remain tied to their source/build identity. Retain their matching
executables, use isolated writable runtime directories and read-only game data,
and announce incompatible development changes. A new commit or a successful build
does not by itself establish LOUD compatibility, Windows support or a speed gain.
