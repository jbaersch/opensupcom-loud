# Platoon enumeration after a same-tick transfer

## Problem and behavioral source

An installed LOUD merge routine enumerates allied platoons, reads each
candidate's position and passes it to `VDist3Sq`. The integration candidate
included a platoon whose last remaining unit had transferred away during the
same tick. Its position was correctly nil, but exposing it in this enumeration
caused a script-thread failure.

The reference distinguishes the native army pool from its Lua-facing list.
At [faf-re revision da3926cf](https://github.com/Draiget/faf-re/tree/da3926cf0ce39c3124f9748e228a44362d72bf1d):

- [`cfunc_CAiBrainGetPlatoonsListL`, address 0x0058D1C0](https://github.com/Draiget/faf-re/blob/da3926cf0ce39c3124f9748e228a44362d72bf1d/src/sdk/moho/ai/CAiBrain.cpp)
  filters out zero-member platoons and the uniquely named ArmyPool, compared
  without case sensitivity. `PlatoonExists` checks object validity separately.
- [`CArmyImpl::GetPlatoonsList` and `CleanUpPlatoons`](https://github.com/Draiget/faf-re/blob/da3926cf0ce39c3124f9748e228a44362d72bf1d/src/sdk/moho/sim/CArmyImpl.cpp)
  describe native enumeration and periodic cleanup. A transfer does not require
  immediate destruction of the emptied group.
- [`CPlatoon`'s position binder](https://github.com/Draiget/faf-re/blob/da3926cf0ce39c3124f9748e228a44362d72bf1d/src/sdk/moho/sim/CPlatoon.cpp)
  returns nil when there are no members. Membership lasts until a unit is
  actually released, as described in the earlier
  [lifetime correction](2026-10-18-loud-upstream-integration.md#subsequent-platoon-lifetime-correction).

These references establish behavioral rules, not copied implementation.
The tests are synthetic. No original executable was run, and no game scripts,
game-derived traces or source text are included in this change.

## Change and comparison bases

This change starts from the local integration candidate
`d55fa9cd1002797b8bf0343bf3e9b4ab7d220d7e`, containing upstream
`d98ad20318aed5c44ae861e59a0358d84ed62b99`.

`brain:GetPlatoonsList()` now omits ArmyPool and platoons without any retained
members. Looking up retained members also excludes stale IDs left after actual
unit release, without dropping a member merely because Destroy queued it.
The list remains dense and preserves the army's existing enumeration order.
Direct platoon handles, position semantics and cleanup timing are unchanged.

Upstream was fetched and compared at
`9f4547810899a2e02b8e2dc94b93a40d52868340`. Its newer tick-order implementation
still fails the same synthetic transfer contract. It was not merged into this
candidate. In particular, its snapshot layout 74 is separate from this
candidate's layout 60.

## Validation

The synthetic transfer case covers losing the last member, retaining another
member, receiving a replacement before cleanup, and keeping a uniquely named
empty group alive. In each case a later Lua thread queries the list in the same
tick; the next tick checks cleanup and destruction callbacks.

A second case checks ArmyPool exclusion, including mixed-case unique naming,
and member visibility before and after the deletion queue drains. The deliberate
invalid distance call is caught only inside the synthetic test; original-data
runs retain ordinary fault reporting.

Local platform: Linux x86-64, Clang 22.1.8, Release `-O1 -DNDEBUG`,
warnings treated as errors, and the existing local dependency adapters with
Catch2 3.12.0. Build and runtime output use separate writable directories;
original installations are mounted read-only.

| Check | Result |
| --- | --- |
| Candidate before the fix, both new cases | RED: three failed assertions across two cases. |
| Latest upstream, the same transfer case | RED: both empty-group sections fail. |
| Corrected binding, both new cases | GREEN: 31 assertions in two cases. |
| Normal CMake build | `osc_tests`, `opensupcom`, `osc_integration` and the private reproducer build with warnings treated as errors. |
| `ctest -LE 'data\|lint\|bench' --output-on-failure` | 1,740 pass: 1,718 Catch2 cases, 18 architecture checks and four local two-process tests. |
| `ctest -R '^data\.(platoon-test\|replay_roundtrip\|save_load\|save_load_replay)$'` | Four pass; no skips. |
| LLVM 22 changed-line format and changed-file clang-tidy | Pass; no findings in the two changed C++ files. |
| Complete LLVM 22 analysis | 585 translation units, 30 existing findings. The initial ratchet failed on the inherited UI-file split's attribution; the saved complete findings match the corrected baseline after the path-only correction described below. |

The baseline original-data reproducer fails again at tick 12,859, with a
byte-identical checksum trace to the preserved earlier run. A diagnostic link
replacing only the corrected binding continues to natural GameOver at tick
13,164, with zero recorded test, Lua-thread or source-callback faults. All
per-tick checksum domains agree with the baseline through tick 12,858; the
first difference is exactly the former failure tick. Four ordinary AI
navigation-marker warnings remain, without suppressing them.

The earlier verbatim test split in `40525af2` had moved two
`readability-redundant-string-cstr` findings into `ui_contracts_test.cpp` while
leaving all 23 allowances on `integration_tests.cpp`. The separate bookkeeping
commit `16cd3da0` redistributes these as 21 plus two, retaining the total of 30.
Both moved source lines were verified against their originals and a fresh
analysis of the new file. Recomparison uses the complete run's reported
per-file deltas; it is not a second full clang-tidy run. No baseline allowance
was added for a new finding.

## Final committed-build check

Engine commit `f87915a69560a8577920cab13031967edc14d23b` was rebuilt with its
own build identity, `OpenSupCom 0.1.0 (f87915a6)`. The normal CMake reproducer
reaches natural GameOver at tick 13,164 with zero recorded test, Lua-thread or
source-callback faults. The normal `opensupcom` executable replays the recording
successfully; the complete 13,164-tick checksum traces are byte-identical.
The first difference from the old failing run remains tick 12,859.

The complete data-free CTest selection was repeated on this committed build:
all 1,740 checks pass again. The targeted retail checks above exercised the
same engine sources before committing; the committed-build LOUD record/replay
is the final identity check. Later changes to this report do not change the
tested engine.

The protected original profile, standard launcher and accepted executable
retain their earlier hashes. All 18 checked installed LOUD inputs also match.
The focused branch remains local; the unaccepted integration stack is not
published or adopted into `codex/loud`.

## Acceptance limits

The original tick-12,859 reproducer has only one planning AI because of its
historical player-name fixture. It is suitable for reproducing this failure,
not for two-AI or long-duration acceptance. The separate undefined `self` and
`LOUDSQRT` failures remain open. This correction does not establish acceptance
of the full integration candidate, its mod selection, Windows or cross-OS play.

Replay/save identity guards remain enabled. Even though this change does not
alter the serialized layout, script-visible enumeration can alter gameplay:
keep recordings and saves with the build that wrote them.

AI assistance: Codex. The exact model identifier was not recorded.
