# LOUD compatibility and combat-tick work

This is an experimental contribution branch of
[bwiemz/supcom-engine](https://github.com/bwiemz/supcom-engine), not a replacement
for the upstream project. Upstream's MIT license, copyright and third-party
notices remain intact. It contains engine changes and synthetic regression tests;
you must supply your own legally obtained FA installation and LOUD data.

Development continues on `codex/loud` in the public fork. The
[fork workflow](fork-workflow.md) describes commits, validation and normal pushes.

## Branch and review scope

The original fork started at upstream commit
`a799c964e25708b5512f16aee93845dcaca01dc8`. Its 42 engine/test commits remain
separate, in dependency order, followed by publication preparation. Private
workstation notes and runtime captures are not part of this branch or its new
history. Commit author attribution is preserved; AI assistance was used in the
development and review. See the [commit index](loud-commit-index.md) and
[implementation contract](plans/2026-10-16-loud-fork-contract.md).

The initial acceptance checked upstream `main` at
`68c79d06cfe6a40b58c69b765f8cddb4d88ad428`; its subsequent rendering/camera work
was not part of that accepted executable. The separate integration candidate
now includes all landed upstream changes through
`9f4547810899a2e02b8e2dc94b93a40d52868340`: beat-based scheduling, phased
movement/deletion, script-owned projectile damage and independent world views.
The [current integration decisions](plans/2026-10-20-loud-upstream-integration.md)
explain how the retained fork contracts fit those changes. The
[earlier integration record](validation/2026-10-18-loud-upstream-integration.md)
remains evidence for its own source state.
The preserved gameplay build remains separate while long-duration and large-load
acceptance of the candidate is pending.

Useful review groups, in dependency order:

| Area | Behavior and regression coverage |
| --- | --- |
| Source startup | Initialize UI options before building each state, use script-local scenario markers, preserve complete plain-data player entries across the UI/sim boundary, initialize native army slots before brain callbacks, stop failed starts without replacement commanders. `test_ui_startup`, `test_session_start`, `test_session_brain_initialization`, `test_session_failures`, `test_replay`. |
| Script-facing primitives | Category selection, threat aliases, navigator goals, no-self lobby broadcasts, entity Kill, blueprint defaults and shared projectile blueprints. Corresponding `test_ui_category_selection`, `test_influence_map`, `test_navigator_goal`, `test_net_lobby`, `test_entity_kill_contract`, `test_blueprint_defaults`, `test_weapon_projectile_blueprint`. |
| Weapons and diagnostics | Preserve scripted zero-damage targeters, initialize script weapons once, exclude explicit dummy weapons, retain muzzle-relative shared aim within upstream's motion phase, count callback faults, recognize complete assertion-failure tokens. `test_zero_damage_targeter`, `test_unit_weapon_lifecycle`, `test_weapon_dummy_contract`, `test_hover_aim_timing`, `test_lua_log_failures`. |
| Player orders and rendering | Recorded cheat-dialog creation and caps, build-grid snapping, shared construction and area assist, aircraft separation, formation previews and growing strategic-icon buffers. `test_cheat_console`, `test_cheat_spawn`, `test_shared_build_site`, `test_guard_area_assist`, `test_air_separation`, `test_formation_icons`. |
| Navigation | Validate entire smoothed segments, bypass A* only on clear routes, retain corners until passed, replan blocked routes on grid changes, normalize ground-motion orientations, cache exact raw routes. `test_direct_path`, `test_path_smoothing`, `test_dynamic_navigation`, `test_ground_snap`, `test_path_cache`. |
| Query and scheduling cost | Exact rectangle membership, conservative rounded-radius bounds, smaller acceleration cells, enemy prefiltering, unordered boolean reductions with explicit target tie-breaks, category-bit reuse, bounded Lua mass-loss sweeping, yielding between complete solo ticks. `test_entity_rect_query`, `test_spatial_cell_membership`, `test_weapon_candidate_walk`, `test_target_recheck`, `test_category_bit_contract`, `test_lua_gc_budget_contract`, `test_session_tick_budget`. |

These are general engine primitives. No LOUD AI scripts, plans, balance tables or
original executable are supplied or modified.

## Build and data-free checks

Use the existing [README build instructions](../README.md#building) and pinned
vcpkg manifest. A Linux example, after vcpkg setup:

```sh
cmake --preset linux-debug -DVCPKG_MANIFEST_FEATURES= -DOSC_WERROR=ON
cmake --build --preset linux-debug
ctest --preset linux-debug -LE 'data|lint' --output-on-failure
bash tools/check_format.sh a799c964e25708b5512f16aee93845dcaca01dc8
```

The formatting tool requires LLVM 22. The inherited CI covers GCC, Clang,
ASan/UBSan, MSVC and data-free cross-OS play; CI results for this new branch must
be checked after it is published. The local results below are not Windows or
cross-OS acceptance. For targeted tests, for example:

```sh
build/linux-debug/tests/osc_tests '[pathfinding][cache]' --reporter compact
```

## Running with your own LOUD installation

Pass LOUD's own init file explicitly, not the retail init file. Use a separate
preferences file and user directory. On Linux, for example:

```sh
mkdir -p "$PWD/userdata/loud/config" "$PWD/userdata/loud/user" "$PWD/userdata/loud/logs"
./build/linux-debug/opensupcom \
  --fa-path "/path/to/Supreme Commander Forged Alliance" \
  --init "/path/to/Supreme Commander Forged Alliance/LOUD/bin/LoudDataPath.lua" \
  --prefs "$PWD/userdata/loud/config/Game.prefs" \
  --user-dir "$PWD/userdata/loud/user" \
  --log "$PWD/userdata/loud/logs/game.log" --windowed 1600 900
```

Choose your installed mods in the normal lobby. This branch does not bundle or
automatically reproduce the developer's local mod selection. These explicit paths
separate preferences and saves; they do not make the process a filesystem
sandbox. Development runs used a read-only installation and isolated writable
runtime directories. No original game executable was started for these checks.

**Start a fresh match.** The combined beat/player-table replay format is 18;
the save container remains 4 and this integration candidate uses native snapshot
layout 75 (the preserved initial gameplay build used 56). Existing
build/content identity guards remain enabled. Keep older saves and their matching
executables; cross-build replay or snapshot loading is not promised. These
changes do not implement common multiplayer save/resume.

## Historical validation and measured limits

These measurements describe the initial accepted build. They do not transfer
automatically to the newer upstream-integration candidate; see its separate
[acceptance record](validation/2026-10-18-loud-upstream-integration.md).

The pre-export engine/test source passed 1,639 Catch2 cases / 322,904 assertions,
18 architecture checks and four local two-process lockstep checks on Linux.
Source and test blobs in the 42 exported commits end at exactly the same content
as that accepted engine. Publication-only changes and their verification are
listed in [the preparation record](loud-publication-checks.md).

A normal unmodified-LOUD AI skirmish ended naturally at tick 16,149, with 367
AI-built units and zero registered source/thread/test faults. The accepted
binary's replay agreed with all 16,149 independently decoded recorded tick
checksums. Six original weapon classes destroyed their targets. These are
specific local checks, not full LOUD/mod-stack certification.

The last optimization was measured in 22 native battle runs. Eighteen early
battle runs used three variants in both orders for each scene: corrected old
32-unit broadphase without the route cache (A), corrected 32-unit broadphase with
the cache (M32), and corrected 16-unit broadphase with the cache (MG). Both sides
include the radius-boundary correctness fix. Four additional long loss-phase
runs checked functional continuation. Every final comparison passed exact
per-tick state, work/movement, fault and final-state gates.

| Early battle scene | p95 user CPU cycles, A to MG, two orders | Mean instructions, two orders |
| --- | --- | --- |
| 6,400 dense mixed units | -20.23% / -15.67% | -16.02% / -16.01% |
| 6,400 ground/hover units | -11.10% / -10.41% | -9.11% / -9.11% |
| 8,192 mixed units | -19.39% / -13.32% | -12.63% / -12.61% |

Measurements used Clang 22.1.8, Linux x86-64, Release `-O1 -DNDEBUG`,
`-ffp-contract=off`, no fast-math, CPU 2 affinity and nonmultiplexed user-space
hardware counters. They are observations of one machine and these fixtures,
not portable thresholds or a comparison against the original game engine.
Up to 8,131 units actually moved in the same tick. The largest scene reached
5,738 concurrent real projectiles and 1,387 native kills; concurrent projectiles
are not a total-shot count.

Individual expensive ticks remain. Wall-time maxima were mixed, including one
hover maximum increasing from 135.93 to 159.84 ms; no universal frame-rate or
maximum-pause improvement is claimed. The long loss-phase runs have declining
populations and are not sustained 6,400-unit performance acceptance. A late hover
maximum rose 0.37% in CPU cycles. Heap and weapon-priority-filter experiments
without reliable repeated gains were excluded.

Raw recordings, snapshots, game-derived captures, private run logs and local
instrumentation remain outside the published branch. Consequently the large
battle numbers are a reported local result, not a fully reproducible benchmark
shipped with this branch. The synthetic regression tests can be run without FA.
Windows validation of these changes, three-player LAN, shared autosave/resume,
complete coverage of the local 15-mod selection and sustained frame-rate work
remain open.
