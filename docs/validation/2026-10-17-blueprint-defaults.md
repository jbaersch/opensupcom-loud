# Native air and fuel blueprint defaults

Candidate `4c079451b4375471b6ba58ecae956fba1e085c05`, one commit on upstream
`d98ad20318aed5c44ae861e59a0358d84ed62b99`. This record covers that isolated
candidate, not adoption of the new upstream engine by the LOUD integration branch.

## Problem and source

Unmodified LOUD's build-button details read `Air.TurnSpeed` and
`Physics.FuelUseTime` without nil fallbacks. An omitted value can abort the
rollover before the button has latched its hovered state, preventing its click.
The engine should expose the typed native defaults: 1 rad/s and 0 seconds
(infinite fuel). Explicit blueprint values, including zero, stay unchanged.

The rules were checked against the Air and Physics constructors in
[faf-re RUnitBlueprint.cpp](https://github.com/Draiget/faf-re/blob/1f6b9b25b51304e271d16cc1a730409e8d3437f8/src/sdk/moho/resource/blueprints/RUnitBlueprint.cpp),
including Air constructor `0x0051EF00`. Only the rules are used; the candidate
contains no copied game scripts or reference-engine implementation.

## Validation

Linux x86-64, Clang / clang-tidy / clang-format 22.1.8, CMake 3.31.8,
Release `-O1 -DNDEBUG`, warnings as errors and `-ffp-contract=off`.
The local build uses prepared source/system dependencies rather than the vcpkg
preset. Catch2 was aligned to 3.12.0, the repository's pinned vcpkg baseline;
the local GLFW adapter needs its existing header directory on `CPATH`.
These local setup details are not bundled into the fix.

The synthetic regression checks omitted fields and missing whole sections,
explicit zero and custom values, the rollover failure path and the blueprint
view exported to Lua. Before the fix it fails five of seven checks; after the
fix all seven pass. The RED check was also repeated with the pinned dependency
by linking the unchanged upstream blueprint-registration object against the same
synthetic tests. The fixed source checkout and validated executables were
untouched by that baseline check.

| Check | Result |
| --- | --- |
| Synthetic RED / GREEN, pinned Catch2 | Upstream registration object: 5 of 7 checks fail. Fixed object: all 7 pass. |
| Data-free CTest | All 1,604 pass: 1,582 Catch2 cases, 18 architecture checks and 4 two-process MP checks. The 4 optional timing benchmarks were excluded. |
| Full LLVM 22 clang-tidy ratchet | All 552 first-party compilation units analyzed; 30 findings against baseline 30, no new finding. |
| Retail gate, 269 entries | 256 pass, 6 fail, 7 skip. This gate is **not green**. |

Five failures are English-label assumptions in `combo-click-test`,
`edit-text-test` and the three front-end goldens. They reproduce identically
with and without the fix, serially with fresh profiles on the German retail
installation. The source searches for `Cancel`, `Create` and `Skirmish`;
the installed controls are localized. Using the actual German Skirmish label
passes the click prerequisite and reaches the missing-golden skip. Follow-up:
[fork issue #3](https://github.com/jbaersch/opensupcom-loud/issues/3).

The sixth failure, `skirt-render-test`, passed on both serial fresh-profile
repeats. Its initial parallel-run failure remains unexplained; no renderer fix
or visual acceptance is claimed. The seven skips are three unavailable FAF
checks and four missing goldens. Do not replace data or regenerate baselines
merely to make this run green.

CI follow-up, checked on 2026-10-18: the exact candidate head passed all five
jobs in [the fork CI run](https://github.com/jbaersch/opensupcom-loud/actions/runs/39064427794):
GCC, Clang, ASan/UBSan, Windows MSVC and cross-OS play (all eight synthetic
pairs). The maintained fork's documentation head `4ec6b715` also passed
[all five jobs](https://github.com/jbaersch/opensupcom-loud/actions/runs/39064554361).
These data-free results do not change the local retail-gate figures above.

Commands, with the same isolated build directory substituted for `<build>`:

```sh
<build>/tests/osc_tests '[air-turn-default]' --reporter compact
ctest --test-dir <build> -LE 'data|lint|bench' --output-on-failure -j 4
ctest --test-dir <build> -L gate --output-on-failure -j 3
python tools/clang_tidy_ratchet.py -p <build> -j 6
```

The retail gate pins `OSC_FA_PATH` and `OSC_INIT_FILE` to the local retail
installation. Source, installation and existing profiles were read-only;
writable XDG state and temporary files were isolated. No original executable
was run. Captures, saves, logs and raw results remain local.

The LLVM 22 changed-line formatter passes with Git's mnemonic/no-prefix settings
explicitly disabled. The checker was confirmed to inspect both changed C++ files;
its separate Git-prefix bug is not bundled into this candidate.

## Limits and handoff

Windows compilation/data-free tests and synthetic cross-OS play passed in CI.
Real-data Windows/cross-OS and full LOUD/mod-stack acceptance remain unverified.
No save fields or format numbers change. Existing build identity
guards remain active; the accepted LOUD launcher and its layout-56 saves stay
with their matching old build. This candidate's upstream base uses layout 60.

Submitted on 2026-10-18 as [upstream PR #733](https://github.com/bwiemz/supcom-engine/pull/733),
after confirming that main still omitted both defaults and the other 13 open
PRs did not implement this change. It contains one commit and three files.
The [upstream CI run](https://github.com/bwiemz/supcom-engine/actions/runs/39108836806)
awaits approval from a maintainer; no jobs have run there yet. Next action:
maintainer review and the upstream checks after approval. See the
[contribution queue](../upstream-contributions.md) and
[fork issue #1](https://github.com/jbaersch/opensupcom-loud/issues/1).

AI assistance: Codex prepared the current-base candidate and additional synthetic
coverage. The original fix's historical model is unknown; its author is retained.
Model metadata is provenance, not evidence of correctness.
