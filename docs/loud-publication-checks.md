# Publication preparation checks — 2026-10-16

The contribution branch contains 42 engine/test commits exported from a local
development line, then a separate GLFW build-dependency fix and fork documents.
It preserves the public upstream ancestry at `a799c964`. The local development
history containing workstation notes is not an ancestor of this branch.

## Fresh export checks

| Check | Result |
| --- | --- |
| New checkout configuration | Passed with CMake 3.31.8 and Clang 22.1.8 on Linux x86-64. |
| Build | `opensupcom`, `osc_tests` and `osc_integration` built with `OSC_WERROR=ON`, `-O1 -DNDEBUG` and the normal strict FP settings. No private gameplay/test-harness targets were injected. |
| Catch2 suite | All 1,639 cases / 322,904 assertions passed in the freshly built export. |
| Architecture | All 18 CTest architecture checks passed, including current documentation metrics. |
| Local two-process multiplayer | All four data-free CTest lockstep checks passed. These are not a real three-player LOUD/LAN test. |
| Changed-line formatting | Full contribution range from `a799c964` passed LLVM 22 formatting. |
| Whitespace and version | Full contribution diff passed `git diff --check`; version consistency check passed for 0.1.0. |
| Source equivalence | Every C/C++ source/header matches the accepted local engine. The only build-input changes during preparation are explicit private GLFW links in `src/app/CMakeLists.txt` and `tests/integration/CMakeLists.txt`. |
| Provenance | Upstream license text is unchanged. Added tests use synthetic data. Game assets, recordings, saves, captures and private run logs are excluded. |

This machine used cached native dependencies through an **external local CMake
adapter**, rather than the standard vcpkg preset. It added no source patches or
private test targets. The adapter marks third-party targets as system dependencies
and works around an include defect in its cached development version of Catch2;
neither workaround changes this branch. The checked-in vcpkg presets and their
pinned manifest remain intact. A full vcpkg build and this branch's GitHub CI have
not yet run; neither has Windows, GCC or sanitizer acceptance of the new export.

The clean-header check reproduced missing `GLFW/glfw3.h` in `app/ui_clicks.cpp`:
the app and integration runner consumed GLFW directly while only the renderer
linked it privately. Commit `d0bdd15060d2` declares both direct dependencies.
The same translation unit and then all three full targets compiled successfully.
The standalone fix is a useful first upstream review candidate.

## Earlier gameplay and performance checks

The [LOUD findings](loud-fork.md#validation-and-measured-limits) report the normal
AI match, exact replay, original weapon probes and large-battle comparisons made
before the export. Their implementation is preserved byte-for-byte in the C/C++
sources, but the new Git revision gives newly built executables a different build
identity. Old replay/snapshot acceptance is not inferred from source equivalence.
The large-battle suite was not rerun against the publication binary.

The last local clang-tidy check covered the five C++ translation units changed
in the combat-tick optimization, with LLVM 22.1.8 and no new findings. Preparation
did not change C++ or run a new whole-project lint scan at that stage.

During the later CI/documentation follow-up on 2026-10-16, the full local
`src/` and `tests/` clang-tidy ratchet also completed with LLVM 22: 30 accepted
findings against a baseline of 30, with no new findings. This used the same
cached native dependency build described above; it does not substitute for the
separate GitHub compiler, sanitizer or cross-OS results.

## Content and publication boundary

The intended publication ref contains only source, tests and documentation on top
of the existing public upstream history. The preparation checks examine every new
historical blob and commit message for workstation paths and credential patterns,
as well as every new commit tree for private runtime directories and game-data
file types. Such checks supplement, rather than prove, provenance.

Private local history is retained separately. Do not publish that development
branch or use a mirror/all-branches push. The prepared bundle contains only this
contribution ref and its public ancestors. GitHub CI, fork metadata and the
published commit must be checked when an actual upload takes place. No upstream
PR, issue, release or developer notification is part of this preparation.
