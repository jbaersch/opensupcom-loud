# Stable build identity validation, 2026-10-22

## Source and behavior

Base: `9abdb82f00e218d242a2cd7057e9df19961a7304`.
Engine change: `7cdbc9cc80115030454365d2471e0ed127b2f6f7`.
Upstream comparison: `ede1e89a66b31cebae5b3bf0aea105242353303c`;
its newer simulation changes are outside this change.

The [contract](../plans/2026-10-22-build-identity.md) separates the readable Git
description from the full-commit source identity used by `core::build_id()`.
The original failure was observed during the
[previous LOUD acceptance](2026-10-20-loud-full-upstream.md): identical source
commits produced different identity strings in full and shallow clones.
Behavioral sources are that observation and the existing build/reader contracts.

Replay layout remains 18, snapshot layout 75, save container 4. Readers and
snapshot binary checks are unchanged. Older saves and recordings retain their
original IDs and matching executables; the accepted gameplay launcher was not
replaced.

## Local checks

Linux x86-64, Clang/LLVM 22.1.8, CMake 3.31.8, Release `-O1 -DNDEBUG`,
`-ffp-contract=off`, first-party warnings as errors, Catch2 3.12.0. A fresh build
directory preserved the earlier accepted binaries. Commands below use `$BUILD`
for that directory and the source checkout as the working directory.

| Check | Command and result |
| --- | --- |
| Isolated Git regression | `python3 tools/test_build_identity.py . <cmake>` failed before implementation because the generated header only held a tag/abbreviated revision. It passed afterward, including full/shallow clones, abbreviations 7/8/12, tags, tracked edits, untracked files, unchanged timestamps and no-Git fallback. An independent subagent also ran it successfully. |
| Fresh product and test build | `cmake --build "$BUILD" --target osc_tests osc_integration opensupcom --parallel 4` passed. After the source commit, the clean build was refreshed and its generated full identity verified. |
| Complete data-free suite | `ctest --test-dir "$BUILD" -LE 'data\|lint\|bench' -j4 --output-on-failure`: 1,836 registered, 1,835 passed, zero failed, one existing Vulkan validation-manifest skip. Includes 1,813 Catch2 cases, 19 architecture checks and four local MP checks. This run used the final source diff before the clean-commit rebuild. |
| Clean-commit identity and save/replay checks | Six focused CTest cases passed on `7cdbc9cc`: the new identity check, replay/save roundtrips and error handling, plus snapshot signature validation. The wrong-build save case still returns `WrongVersion`; a different installation key or altered signed binary ID is still rejected. |
| Python and formatting | `ruff check tools/test_build_identity.py`, `basedpyright tools/test_build_identity.py`, `bash tools/check_format.sh origin/codex/loud` and `git diff --check` passed. |
| Full LLVM 22 ratchet | `python3 tools/clang_tidy_ratchet.py -p "$BUILD" -j4` passed across 590 first-party translation units: 30 findings against the unchanged baseline of 30. |
| Original retail subset | `ctest --test-dir "$BUILD" -R '^data\.(replay_roundtrip\|save_load\|save_load_replay)$' -j1 --output-on-failure`: three passed, zero failed/skipped on the clean engine commit. This is the relevant subset, not a new full retail gate run. |

Original-data runs mounted the installation read-only and used separate writable
preferences, caches and runtime directories. No original game executable was
started. Raw logs, recordings and synthetic runtime evidence remain outside Git.

A separate product recording on the clean commit stored exactly
`0.1.0-7cdbc9cc80115030454365d2471e0ed127b2f6f7` in its replay build field.
Recording and playback both exited zero. All 120 beat rows matched byte for
byte (121 trace lines including the header), with final checksum `66904c32`.
The logs contained no error or Lua-thread-error lines. This verifies the actual
recording consumer in addition to the generated-header regression.

## CI and limits

[CI run 39741420602](https://github.com/jbaersch/opensupcom-loud/actions/runs/39741420602)
tests the exact engine commit `7cdbc9cc`. At this handoff, GCC and ASan/UBSan
jobs had passed. Clang and Windows/MSVC builds and data-free tests had also
passed; Clang's full lint and Windows' Release artifact build were still
running. The dependent Cross-OS job had not started. No failing step was
reported, but the overall result remained pending. This is not a completed
all-green CI claim. The maintained-branch documentation push may trigger a
separate run; its outcome is not part of this record.

The ordinary fork publication is based on the completed local checks and
independent review above. Monitoring stops at this handoff. Check the remaining
CI result before starting another work package; no follow-up automation is set.

This is an identity/tooling correction, not a new long-game, full-mod or native
Windows gameplay acceptance. Simulation arithmetic and game rules are unchanged.
Dirty and Git-less builds still lack unique content identities; this change does
not make different toolchains or snapshot binaries interchangeable.

AI assistance: Codex with separate subagents for implementation, validation,
GitHub comparison and independent code/documentation review. Exact model
identifiers were not independently recorded; no model is inferred from Git
authorship.
