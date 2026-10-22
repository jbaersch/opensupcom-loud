# Upstream contribution queue

Checked against upstream `ede1e89a66b31cebae5b3bf0aea105242353303c`
on 2026-10-22, including [batch #765](https://github.com/bwiemz/supcom-engine/pull/765)
and [batch #785](https://github.com/bwiemz/supcom-engine/pull/785).
This is a comparison snapshot; check GitHub again before starting a contribution.
It does not certify those upstream changes on the LOUD integration branch.

| Area | Current upstream work | Action here |
| --- | --- | --- |
| Hover orientation | [#695](https://github.com/bwiemz/supcom-engine/pull/695) is merged. | Support with additional regression findings if useful; no duplicate implementation PR. |
| Shared mobile build site | [#715](https://github.com/bwiemz/supcom-engine/pull/715) is merged, including occupied-site rules. | Integrated with the retained preparation-snap and shared-accounting checks; no duplicate proposal. |
| Aircraft separation | [#693](https://github.com/bwiemz/supcom-engine/pull/693) removes in-flight separation to match Moho. | Adopted in the integration candidate; retain landing/formation/guard checks without reintroducing airborne repulsion. |
| Barrel aiming and strategic icons | [#696](https://github.com/bwiemz/supcom-engine/pull/696) and [#687](https://github.com/bwiemz/supcom-engine/pull/687) are merged. | Reconcile shared aim with the new motion phase and retain previews/dynamic buffers in the per-view renderer. |
| Air/fuel blueprint defaults | [PR #733](https://github.com/bwiemz/supcom-engine/pull/733) was included through batch #749, commit `b2ef1bc8`; the individual PR is closed. | Preserve the integrated implementation and expanded tests; no resubmission. |
| Platoon enumeration | The current upstream still exposes empty platoons in its Lua list. | The fork fix is integrated; any upstream proposal must be isolated on the current upstream deletion model. See the [contract and reproducer](validation/2026-10-20-platoon-transfer.md). |
| Format checker and Git prefixes | Still assumes Git's default diff prefixes. | Implemented and regression-tested in this fork. The isolated candidate for [fork issue #2](https://github.com/jbaersch/opensupcom-loud/issues/2) needs a current upstream base before submission. |
| Localized retail test selectors | English button labels still make five checks fail on German retail. | Localization-token selectors are implemented in this fork. Refresh the isolated candidate for [fork issue #3](https://github.com/jbaersch/opensupcom-loud/issues/3) separately from gameplay changes. |
| Build identity | Still derives identity from `git describe`, which varies with clone depth, tags and abbreviation settings. No overlapping open PR found. | Use a full-commit identity while preserving the readable version and reader checks; see the [contract](plans/2026-10-22-build-identity.md). Any upstream submission needs its own current-base candidate. |
| Economy and army statistics | Open [#786](https://github.com/bwiemz/supcom-engine/pull/786), [#787](https://github.com/bwiemz/supcom-engine/pull/787), [#788](https://github.com/bwiemz/supcom-engine/pull/788), [#789](https://github.com/bwiemz/supcom-engine/pull/789) and [#790](https://github.com/bwiemz/supcom-engine/pull/790). | Avoid competing implementations; inspect landed behavior before the next integration. |

The isolated blueprint candidate is `4c079451` on
`codex/upstream-blueprint-defaults-current`; its
[validation record](validation/2026-10-17-blueprint-defaults.md) documents the
exact base, reproducer, source and test limits. It contains none of the LOUD
integration stack. Submitted on 2026-10-18 as
[PR #733](https://github.com/bwiemz/supcom-engine/pull/733). The exact source
head passed all five fork CI jobs, including MSVC and eight synthetic cross-OS
pairs. The earlier approval-waiting workflow was the submission-time state;
the contribution is now included in upstream's batch #749.

The former clang-tidy blocker is resolved by merged
[#692](https://github.com/bwiemz/supcom-engine/pull/692). It is no longer a reason
to hold a current-base candidate.

## Current integration boundary

The [current integration](plans/2026-10-20-loud-upstream-integration.md) contains
the full pinned upstream target `9f454781`, not the newer batches above.
That target's native snapshot layout is 74; the
combined fork uses 75 and replay layout 18. The initially accepted LOUD gameplay
build uses snapshot layout 56.
The [full integration record](validation/2026-10-20-loud-full-upstream.md)
records the bounded acceptance of engine `5c74c847` for source adoption into
`codex/loud`. Keep the previously accepted launcher, executable, saves and
replays together; source adoption does not silently replace that gameplay build.

The new upstream delta spans 162 files and advances its snapshot layout to 77.
It changes steering/collision rules, blueprint defaults, resource sharing and
checksums, overlapping the fork's simulation and serialization contracts.
Integrate it on a dedicated branch with fresh LOUD and replay evidence.

A merge preview from `7cdbc9cc` to `ede1e89a` (common base `9f454781`)
reports five conflict paths: `README.md`, `docs/current-state.md`,
`src/lua/bindings/sim/weapon.cpp`, `src/sim/navigator.cpp` and
`src/sim/state_io.cpp`. No integration or build was performed. In particular,
resolve the shared projectile-blueprint lookup and both navigation modes
explicitly. Upstream's intermediate snapshot 75 already collides with the
fork's 75; choose a distinct combined layout beyond upstream 77. Textually
clean construction/economy/rendering changes still need their corresponding
repair/assist, deterministic save/replay and visual checks.

[Upstream CI](https://github.com/bwiemz/supcom-engine/actions/runs/39700195007)
is green on `ede1e89a`, including macOS ARM64; the
[fork's prior CI](https://github.com/jbaersch/opensupcom-loud/actions/runs/39419630838)
is green on `9abdb82f`. These results do not validate a combined source tree.

Follow the [collaboration guide](collaboration.md) for compact handoffs and the
[fork workflow](fork-workflow.md) for branches and publication.
