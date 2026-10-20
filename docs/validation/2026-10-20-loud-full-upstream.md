# Full upstream integration into LOUD, 2026-10-20

## Source and decision boundary

The complete landed upstream target is
`9f4547810899a2e02b8e2dc94b93a40d52868340`. It is merged by
`a383ef82349b640dc7ba62e967b5d5f242cbcfa1` into fork candidate
`61f9401f0b93268d172ba1a15b89f1c088496663` (previous upstream base
`d98ad20318aed5c44ae861e59a0358d84ed62b99`). The final tested engine head is
`5c74c847d586a37c27ad54e85d2a2f34d87b7e40`. Later documentation commits do
not change the tested engine. Open upstream pull requests are outside this scope.

The [integration decisions](../plans/2026-10-20-loud-upstream-integration.md)
explain adopted phase ordering, deletion, beat scheduling, scripted damage,
independent world views and retained fork behavior. This record supports source
integration; replacing the previously accepted gameplay launcher remains a
separate playtesting decision. Older executables, profiles, saves and recordings
are preserved.

## Local build and regression checks

Linux x86-64, Clang 22.1.8, Release `-O1 -DNDEBUG`, `-ffp-contract=off`, no
fast-math, first-party warnings as errors, Catch2 3.12.0. Original FA/LOUD files
were mounted read-only with separate writable runtime state. No original game
executable or modified replacement game scripts were used. Captures, logs,
recordings and private instrumentation remain outside Git.

| Check | Result and limits |
| --- | --- |
| Final data-free CTest, `-LE 'data|lint|bench'` | 1,835 registered: 1,834 passed, zero failed, one Vulkan validation-layer manifest skip. Includes 1,813 Catch2 cases, 18 architecture checks and four local two-process MP checks. |
| Full LLVM 22 clang-tidy ratchet | 590 translation units; 30 findings against the unchanged 30-finding baseline. Repeated after the repair correction. |
| Changed-line format | Passed against both the CI base `origin/main` and pinned `upstream/main`; final repair lines checked as well. |
| Full retail gate, `ctest -L gate` | Initial integrated run: 270 tests, 259 passed, one actual missile-repair failure, ten skips (three unavailable FAF cases and seven absent goldens). |
| Corrected retail gate subset | All eight unchanged tests passed: missile, repair, shield, enhance, guard-engage, build, save/load and save/load/replay. The initial full run is not represented as an all-green final rerun. |
| Replay 18 layout regression | Upstream 17 has beats without the older fork's player table. The added legacy section failed before the reader threshold changed to 18 and passed afterward; 16 assertions. Build/content guards remain enabled. |
| Damaged active-work repair regression | 18 failing assertions before the correction; afterward 245 assertions across 14 related cases pass. Damaged hull/shield is repaired before helping the next silo/enhancement work item. Healthy work assistance is retained. |

Some old synthetic fixtures assumed immediate coordinates or storage ownership.
They now explicitly cross the upstream coordinate-commit boundary and initialize
completed units' storage. Geometric and accounting assertions were retained.
The shared-turret failure needed an actual pose refresh before each aim solver;
its strict direction tolerance was not relaxed.

## Original LOUD source and gameplay

The selected installation contained 15 mods (11 simulation, four UI). Private
bounded drivers enter the original source startup and regular simulation/window
paths; assertions count source callback and Lua thread faults as failures.

- Eight aircraft spanning four factions completed two Landing2 cycles; cap,
  blueprint defaults, ground assistance, enhancement and silo contracts passed.
- Six real weapon classes destroyed their targets, with 213 distinct real
  projectile IDs and no registered source/thread/test faults.
- Original menu/lobby launch completed 1,200 beats; the product executable's
  replay trace was byte-identical.
- A player-input construction/combat flow recorded eight commands and ended
  naturally at beat 6,597. A free-position extractor completed at tick 262.
  Product replay matched all 6,597 trace rows. A captured in-game combat frame
  was visually inspected; no obvious missing HUD, models or icons were found.
- Winged Arena, seed 150102026, cap 3,000: two actual planning LOUD AIs ran
  36,000 ticks, with production by both armies. Zero registered faults or
  invalid positions; complete product replay matched 36,000 trace rows.
- Seton's Clutch 40, seed 4242, cap 3,000, NoRush 20: the same 36,000-tick
  two-AI and full replay checks passed. Peak run RSS was about 617 MiB; Winged
  Arena about 525 MiB.

The earlier `self` and `LOUDSQRT` content failures did not recur in these two
runs. No installed Lua was repaired, so this does not prove those failures
impossible for other seeds, timings or configurations. Warnings were retained;
zero registered faults does not mean an empty log or exhaustive mod coverage.

## Large battles and cost

The preserved accepted MG build was compared with the final integrated engine
using CPU-2-pinned, nonmultiplexed user-space cycle/instruction counters. Each
scene ran in ABBA order: 6,400 dense mixed units, 6,400 ground/hover units and
8,192 dense mixed units. Each run had 100 settling, 60 movement and 240 combat
ticks. All 12 runs completed with no faults or invalid positions; repeated
runs of each build produced byte-identical checksum traces. The two builds
have different adopted gameplay rules, so cross-build state equality is neither
expected nor claimed.

| Scene | Movement p95 wall / cycles | Combat p95 wall / cycles |
| --- | --- | --- |
| Dense 6,400 | +5.09% / +0.92% | -3.14% / -6.01% |
| Ground/hover 6,400 | +7.46% / +4.82% | -1.42% / -2.32% |
| Dense 8,192, first ABBA | +14.03% / +8.39% | +3.10% / -1.20% |
| Dense 8,192, predefined repeat ABBA | -3.05% / -0.33% | -6.77% / -4.83% |
| Dense 8,192, both ABBAs combined | +5.80% / +4.02% | -1.69% / -2.97% |

The 8,192 repeat was requested by the first movement result, not selected from
many trials. All four repeat runs also passed and were deterministic per build.
Combined percentages compare the means of all four per-run p95 values for each
build. CPU clocks varied; there is no universal speedup or tight portable
performance threshold. The 8,192 movement p95 instruction count rose 10.81%;
combat fell 5.93%. Movement cost remains worth profiling separately. All combined movement/combat
wall-time and cycle medians are within 4% of the former build. Individual
spikes did not all improve: dense 6,400 combat maxima rose 10.36% in wall time
and 13.87% in cycles, despite its lower p95. Ground/hover movement maxima rose
27.31% / 26.90%, and its p95 instruction count rose 23.32%. These changed-rule
workloads do not justify a general claim about smoother frames or shorter pauses.

The new 8,192 scene reached 8,020 simultaneously moving units, 7,264 concurrent
projectiles and 2,505 kills. Its population fell below 6,000. The 6,400 scenes
also lost units: these are active large-battle checks, not sustained 6,000-unit
throughput or user-visible FPS acceptance.

A separate extended 8,192-unit run continued through 100 settling, 60 movement
and 1,200 combat ticks. It finished with 3,299 live units and 4,894 kills, zero
registered faults and no nonfinite positions. It verifies continuation into a
loss phase, not sustained peak population; its timing was not used in the ABBA
performance comparison.

## Platform, rendering and remaining acceptance

[CI run 39413260983](https://github.com/jbaersch/opensupcom-loud/actions/runs/39413260983)
is fully green on the exact engine head `5c74c847`: GCC, Clang plus the full lint
ratchet, ASan/UBSan, MSVC and synthetic cross-OS lockstep. This is data-free CI.
The upstream main ref was checked again at the final validation boundary and
still named the complete pinned target `9f454781`.

The paired Linux GCC and Windows MSVC CI artifacts also recorded/played a real
four-AI retail game (SCMP_009, seed 4242, scripted orders). Both product replay
checks passed and all checksum fields in their 3,000-tick traces matched, final checksum
`ea889040`, with no logged script errors. Windows ran under Wine against the
same read-only data, not on a native Windows desktop.

A fresh original-LOUD Winged Arena game with both planning AIs, seed 150102026,
cap 3,000 and the selected 11 simulation/four UI mods ran 12,000 ticks. The
Clang recording and both CI artifact replays match every trace row and field,
final checksum `2b1d5b4b`; neither playback logged script errors. Windows writes
CRLF rather than LF: after only newline normalization, all three trace files
are byte-identical. The two armies built 307/222 units and scored 35/30 kills.
Its first 12,000 ticks also match the earlier native 36,000-tick run exactly.

The same full source commit had an eight-character Git abbreviation locally
and seven characters in the shallow CI artifacts. For this fresh recording,
the unchanged local source was rebuilt with process-local `core.abbrev=7` to
match the CI build identity. No global Git setting, replay bytes or identity
check was changed; old eight-character binaries/recordings are retained.
Reproducible build-ID formatting across clone histories remains a tooling
follow-up, not a reason to bypass replay guards.

The retail `--weapon-test` cannot directly start this LOUD installation: both
platforms reject its incomplete test setup because `PlayerCount` is absent.
It produced empty traces and is not counted as a passed LOUD platform test.
The LOUD source-start driver supplies the original lobby options instead.

Two strict retail render dumps of the final engine were byte-identical. A
private test-driver override enabled Lua-error counting for this dump mode,
which normally only logs such errors; both runs had zero registered errors.
These are repeatability checks, not old/new pixel equality across an intentional
render-path change. Retail's world-view gate also passed in the full gate.

Near and strategic rendering of 6,400 initially spawned real LOUD units each
completed 60 warm-up and 240 measured hidden-window frames with the original
HUD/mods and zero registered faults. Both ended at tick 215 with 6,001 live
units and 3,361 projectiles. The strategic view reached 9,001 icon quads, grew
both buffers to 16,384 and never truncated an upload. The near view used mesh
rendering. This is functional rendering evidence on an RTX 5060 Ti; the fixed
clock, hidden swapchain and other concurrent functional checks make these runs
unsuitable as user-visible FPS benchmarks. A separate +10-speed near-view run
also passed (60 warm-up/120 measured frames), reaching tick 595 with 2,594
survivors and zero faults. Its declining population is not a same-work FPS
comparison with +0.

Replay layout is **18**, native snapshot **75**, save container **4**. Preserve
matching builds for all old saves/replays. The exact historical manual command stream at tick 27,385 was not recreated;
the new 36,000-tick games are fresh long-run evidence, not an exact replay of
that old hang. Native Windows desktop rendering,
three-player mixed-OS LOUD LAN, shared multiplayer save/resume, unavailable FAF
and golden cases, exhaustive 15-mod coverage and sustained high-load frame rates
remain separate acceptance work.

AI assistance: Codex for implementation and review; the exact model identifier
was not verified for this session. No independent agent review is claimed.
