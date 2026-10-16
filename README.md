# OpenSupCom-LOUD

[![CI](https://github.com/jbaersch/opensupcom-loud/actions/workflows/ci.yml/badge.svg?branch=codex%2Floud)](https://github.com/jbaersch/opensupcom-loud/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**Running unmodified LOUD on OpenSupCom, with compatibility fixes and measured
large-battle improvements.**

This is a contribution fork of [bwiemz/supcom-engine](https://github.com/bwiemz/supcom-engine),
the open-source C++20 reimplementation of the *Supreme Commander: Forged Alliance*
engine. All credit for the engine foundation belongs to its upstream contributors.
Our work focuses on the engine primitives LOUD needs: session startup, Lua APIs,
construction, combat, navigation and query cost. LOUD's scripts, AI plans and
balance remain unchanged.

> Experimental development branch: **`codex/loud`**. You need your own
> *Forged Alliance* installation and LOUD data. This repository includes no game
> assets, LOUD distribution, original executable or saves.

[LOUD findings](docs/loud-fork.md) · [Changes to review](docs/loud-commit-index.md) ·
[Development workflow](docs/fork-workflow.md) · [Upstream project](https://github.com/bwiemz/supcom-engine)

## What this fork adds

| Area | Examples |
| --- | --- |
| LOUD startup and Lua compatibility | Initialize menu options before UI construction, preserve complete player-slot tables and supply missing native blueprint defaults. Failed source starts stop with a diagnostic. |
| Construction and orders | Multiple builders contribute to one shared construction site; shared work and ground-assist paths retain resource accounting and lifecycle callbacks. |
| Combat and movement | Scripted zero-damage targeters, weapon initialization and aim timing; normalized ground-motion orientations and safer dynamic navigation. |
| Large battles | Bounded raw-path reuse, exact spatial-query reductions and conservative float-radius bounds; growing strategic-icon buffers and formation previews at strategic zoom. |

The changes are separate, dependency-ordered commits with synthetic regression
tests. For a quick review, start with [blueprint defaults](tests/test_blueprint_defaults.cpp),
[ground-motion stability](tests/test_ground_snap.cpp),
[shared construction](tests/test_shared_build_site.cpp) and
[formation icons](tests/test_formation_icons.cpp). Behavioral sources, caveats and
query invariants are in the [implementation contract](docs/plans/2026-10-16-loud-fork-contract.md).
AI assistance was used in development and review.

## What has been checked

| Evidence | Scope |
| --- | --- |
| Normal LOUD AI skirmish | A local original-script match ended naturally at tick 16,149, with real AI construction/combat and no registered source/thread faults. Its accepted build's replay matched every recorded tick checksum. |
| Large battle fixtures | Controlled 6,400- and 8,192-unit scenes, with up to 8,131 units moving in the same tick. Exact state/work gates accompany the reported CPU measurements. |
| Local data-free checks | 1,639 Catch2 cases / 322,904 assertions, 18 architecture checks and four local two-process lockstep checks in the prepared Linux export. |
| GitHub CI | The badge above shows the actual branch result. The workflow builds GCC, Clang, ASan/UBSan and MSVC, then checks data-free Linux/Windows lockstep. |

These are bounded checks, not certification of every LOUD mod or a frame-rate
promise. The local 15-mod selection is not bundled or automatically reproduced.
Some expensive combat ticks remain. Three-player LOUD LAN, common multiplayer
autosave/resume and full mod coverage remain open. A data-free Windows CI pass
would not by itself establish a playable Windows LOUD match.

See [measurements and limitations](docs/loud-fork.md#validation-and-measured-limits)
and the [export validation record](docs/loud-publication-checks.md).

## Building

Requirements: **CMake 3.21+**, **vcpkg** with `VCPKG_ROOT` set, and a C++20
compiler. Linux uses Ninja and GCC 13+ or Clang 17+; Windows uses Visual Studio
2022 with the C++ desktop workload. Windowed play needs a Vulkan driver.
vcpkg resolves the pinned dependencies in [vcpkg.json](vcpkg.json).

```sh
git clone --branch codex/loud https://github.com/jbaersch/opensupcom-loud.git
cd opensupcom-loud
```

**Linux:** install the system development packages listed in the
[CI workflow](.github/workflows/ci.yml), then:

```sh
cmake --preset linux-debug -DVCPKG_MANIFEST_FEATURES= -DOSC_WERROR=ON
cmake --build --preset linux-debug
ctest --preset linux-debug -LE 'data|lint' --output-on-failure
# Game: build/linux-debug/opensupcom
```

**Windows**, from a Visual Studio developer terminal with vcpkg configured:

```powershell
cmake --preset default
cmake --build --preset default
ctest --test-dir build -C Debug -LE data --output-on-failure
# Game: build/Debug/opensupcom.exe
```

Use `linux-release` for an optimized Linux build or `linux-asan` for sanitizers.
Formatting and clang-tidy use LLVM 22; see [CONTRIBUTING.md](CONTRIBUTING.md).
For packaging, additional engine modes and the broader engine overview, see the
[upstream README at this branch's base](https://github.com/bwiemz/supcom-engine/blob/a799c964e25708b5512f16aee93845dcaca01dc8/README.md).

## Running LOUD

Install LOUD separately into your own game installation. Pass **LOUD's init
file** explicitly; automatic retail/FAF discovery does not select the intended
LOUD setup. Start with separate preferences, logs and saves, for example on Linux:

```sh
mkdir -p "$PWD/userdata/loud/config" "$PWD/userdata/loud/user" "$PWD/userdata/loud/logs"
./build/linux-debug/opensupcom \
  --fa-path "/path/to/Supreme Commander Forged Alliance" \
  --init "/path/to/Supreme Commander Forged Alliance/LOUD/bin/LoudDataPath.lua" \
  --prefs "$PWD/userdata/loud/config/Game.prefs" \
  --user-dir "$PWD/userdata/loud/user" \
  --log "$PWD/userdata/loud/logs/game.log" --windowed 1600 900
```

Replace the installation paths with your own. On Windows use the Windows binary
with the same flags and your installation paths. Select your installed mods and
AI in the normal lobby. Keep the source installation read-only; the explicit
output paths above separate player data but do not create a filesystem sandbox.

**Start a fresh match.** Development replays/saves are build-bound. Keep old saves
with their matching executables; this branch does not promise cross-build resume.
Replay format is 17, save container 4 and native snapshot layout 56.

## Contributing and provenance

Work on small `codex/<topic>` branches, keep one logical change per commit and
record the relevant tests. Follow the [fork workflow](docs/fork-workflow.md),
[contribution guide](CONTRIBUTING.md) and
[provenance policy](docs/provenance-and-compatibility.md).

The aim is to make useful, reviewable fixes available to upstream. Engine
compatibility reports should include the exact build and mod setup; keep game
data, screenshots, snapshots and raw captures outside Git. A fix for a source
contract should not replace the original gameplay logic.

## License

Engine code is under the [MIT License](LICENSE), with upstream copyright and
third-party notices retained. The license does not grant rights to proprietary
FA/LOUD data; users supply their own installation.
