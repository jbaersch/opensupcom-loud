# Stable source identity for replays and saves

## Observed failure and scope

The [LOUD integration validation](../validation/2026-10-20-loud-full-upstream.md)
found that the same source commit produced different build IDs locally and in
shallow CI clones. `git describe --always --dirty` chose eight versus seven hash
characters. Tags and the history available to a clone can also change that
description without changing its checked-out commit.

This change starts from fork `9abdb82f00e218d242a2cd7057e9df19961a7304`.
Upstream `ede1e89a66b31cebae5b3bf0aea105242353303c` still uses the same
description for identity; its open PRs were checked on 2026-10-22 and do not
address this problem. The newer upstream simulation changes are a separate
integration task.

## Contract

- Keep `core::revision()` and `--version` human-readable, using the existing
  Git description with tags and the dirty marker.
- Generate a separate source identity from the full `git rev-parse --verify
  HEAD` object ID. Tags, clone depth and `core.abbrev` cannot change this value
  for the same commit.
- Append `-dirty` for staged or unstaged tracked changes. Untracked files do
  not change the identity, matching the existing development convention.
- `core::build_id()` and its simulation wrapper use
  `<project-version>-<full-commit>[-dirty]`. Refresh both values at every build,
  but do not rewrite an unchanged generated header.
- Preserve `unknown` when Git cannot identify the source. A failed tracked
  change check must not label a build as a clean commit.

The existing dirty marker is not a hash of local edits. Two different dirty
trees at the same commit can still share it; source archives without Git also
lack a unique identity. This work does not certify those builds, compiler flags
or arbitrary toolchains as interchangeable.

## Compatibility and validation

No game rule, command stream, replay layout, snapshot layout or save container
changes. Existing content, build and native snapshot binary checks remain in
place. Save deserialization still rejects a different build; the CLI replay
path retains its existing mismatch warning and checksum verification. No ID
normalization or bypass is added to readers.

New builds write the full source identity. Keep older saves and recordings
with their matching executables; no automatic migration from their abbreviated
or tag-based IDs is promised. The preserved gameplay launcher is unchanged.

The regression uses synthetic temporary Git repositories and the actual CMake
generator. It checks full and shallow clones, tags, abbreviation settings,
different commits, tracked edits, untracked files, unchanged-header timestamps
and the no-Git fallback. Existing save/replay and snapshot-identity checks
exercise the unchanged consumers. The dated validation record states the exact
tested commit and platform limits.

Behavioral sources are the existing build/reader contracts and the recorded
clone-dependent failure. No game data or original-game implementation is needed.
