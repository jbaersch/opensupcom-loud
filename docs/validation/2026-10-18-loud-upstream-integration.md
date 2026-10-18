# Pinned upstream integration: initial technical acceptance

This is the integration candidate on `codex/loud-upstream-integration`, based on
LOUD head `44f04de3befd6422c5a874719ae1a19000399a26` and upstream target
`d98ad20318aed5c44ae861e59a0358d84ed62b99`. Their merge base is
`a799c964e25708b5512f16aee93845dcaca01dc8`. The preserved, previously accepted
LOUD executable and its launcher remain separate. Long-duration and large-load
acceptance of this candidate is still pending; this record is not adoption into
the maintained gameplay line.

The engine/test head checked below is
`65ff55952084d82fc9184d9e8f2884f229b32c0b` (the following documentation changes do not change engine sources).
No original executable was run. Original installations were read-only for the
original-data acceptance runs; writable profiles, recordings and captures were
isolated. Game content and raw evidence remain outside Git.

## Integration decisions

| Area | Decision |
| --- | --- |
| Flight | Adopt landed upstream #693: aircraft do not repel each other in flight. Preserve landing reservations and tests for shared destinations, formation and guard. |
| Ground orientation | Keep normalization at all fork native ground-stance exits and retain both the long varying-slope regressions and upstream #695's check. |
| Shared construction | Adopt upstream #715's native shared-frame and occupied-site rules. Scripts decide whether to reject another army's frame. Remove the duplicate early repair detour; preserve preparation snapping and helper accounting/cleanup checks. |
| Aiming | Combine upstream #696's animated barrel frame with muzzle-relative and shared-controller aiming. Remove every shared pitch contribution before calculating residual aim; never widen firing tolerance to obtain a shot. |
| Rendering | Adopt the target's fog, cartographic, camera and HiDPI changes; keep growing icon buffers and formation previews, projected using logical UI dimensions. |
| Startup and identities | Keep complete player tables, native army initialization, failed-start cleanup, blueprint defaults and build/content guards. Unify the UI and native cheat option. |
| Navigation and query cost | Keep exact queries, category reuse, bounded raw-route cache and the default navigator. Check actual world segments, including endpoint offsets; replan blocked sidestep returns and preserve footprint escape. |
| Scheduling | Keep bounded physical GC sweeping and yielding between complete live solo ticks; honor upstream GameOver in the yielding path. |

Unmerged PRs #720–#732 were comparison material, not bundled contributions. The
broad tick-order redesign remains separate. Existing source-contract tests do
not establish full original-game firing-cadence or hit-rate equivalence.

## Regressions found and corrected

The first merged build passed its initial overlap tests but exposed a split
cheat authority in the full suite. Three existing cheat/replay cases failed;
the corrected native/UI authority passed the source and replay regressions.

Original-data checks then found overlapping animated/muzzle aim, late weapon
handles restarting `OnCreate`, a lost preparation snap, and incomplete default
path clearance. Small synthetic reproducers were retained alongside the data
cases. The final choke probe sends all 24 vehicles through the gap: zero wall
crossings and zero stranded units. The upstream control passes its older gate
with six vehicles crossing wall cells. The candidate strengthens that gate.
Factory rally and precise rolloff also pass without relaxing their assertions.

Two synthetic material fixtures used a one-bone plate with real turret weapons;
they now create an unarmed plate and restore the source blueprint afterward.
The old projectile-id expectation now checks the actual shared blueprint table.
Source-error counting, pixel checks and their tolerances remain enabled.

## Initial validation

Local platform: Linux x86-64, Clang 22.1.8, Release C/C++ `-O1 -DNDEBUG`,
`-ffp-contract=off`, no fast-math, warnings treated as errors. Local dependency
adapters and Catch2 3.12.0 were used without modifying shared dependency sources.
This does not substitute for the standard-vcpkg or Windows CI matrix.

| Check | Result |
| --- | --- |
| Full build / LLVM-22 changed-line format | Pass; format also checked against the CI base `origin/main`. |
| Data-free CTest | 1,737 pass: 1,715 Catch2 cases, 18 architecture checks, four local two-process lockstep checks. Data, lint and separately labelled data-backed benchmarks excluded from this count. |
| Full clang-tidy ratchet | 583 translation units; 30 findings against baseline 30, no new findings. |
| Retail gate, complete first repaired-candidate run | 269 registered: 258 pass, nine skips, two X11/RANDR `BadRRCrtc` startup failures before the relevant assertions. |
| Bounded retail retries | Candidate and upstream control both pass interpolation. Both build-drag retries reach the missing-golden skip. The two startup failures were not reproduced; their cause is not isolated. |
| Outstanding reference/data skips | Three FAF-data cases and seven missing golden references. Reaching capture is not a pixel-comparison pass. |
| Original LOUD source contracts | Existing 15-mod selection: 11 simulation and four UI mods. Eight aircraft across all four factions complete two Landing buff cycles; original ranges recover. Four-faction ground assist and enhancement/silo costs and helper stop pass. |
| Original weapon fixture | Six classes destroy their intended targets; 139 distinct projectile IDs observed. This is neither a total-shot count nor proof of original cadence. |
| Real menu/lobby/launch | Source UI path runs 1,200 ticks; the product replay matches every tick and the complete checksum traces are byte-identical. |
| Scripted player UI flow | Selection, build-button click, completed off-deposit extractor, movement and combat run 7,002 ticks. All seven recorded commands replay in the product with byte-identical per-tick traces. Captures were visually inspected; this is not subjective player acceptance. |

The retained Landing fixture predated the explicit-dummy-weapon fix. Its obsolete
expectation that SSA0305 expose a dummy as a zero-radius runtime weapon was
replaced with exact count and identity checks for the surviving original weapon
rows. All reduction, expiration and restoration checks remain. No installed
scripts or balance data were changed.

Two UI startup diagnostics came from an inherited Wayland socket name beneath a
long private runtime path. Later X11 test runs explicitly unset that child-process
variable. The X11/RANDR failures above are recorded separately.

## Identity and remaining acceptance

Replay layout is 17, save container 4 and native snapshot layout 60. Build and
content identity checks remain active. Start fresh matches and keep old saves
with their matching executables; no cross-build loading or shared multiplayer
resume is promised.

Still pending for this candidate: fresh normal long games beyond tick 27,385
(target 36,000), a second representative map/configuration, repeated active
6,400/8,192-unit comparisons, extended-load and frame-pause measurements,
exact-head remote CI and real-data cross-OS replay. The historical hang's exact
command stream is not reconstructed, so a new long run cannot establish an
identical repetition. Real Windows gameplay and three-player LAN remain separate.

AI assistance: Codex. The exact model identifier was not recorded for this run;
historical model metadata is not inferred. Generated changes were reviewed and
tested; model identity is not validation evidence.
