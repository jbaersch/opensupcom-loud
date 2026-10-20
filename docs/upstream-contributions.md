# Upstream contribution queue

Checked against upstream `9f4547810899a2e02b8e2dc94b93a40d52868340`
on 2026-10-20, including [batch #749](https://github.com/bwiemz/supcom-engine/pull/749),
the [tick-order stack #756](https://github.com/bwiemz/supcom-engine/pull/756)
and the [scripted projectile damage fix #764](https://github.com/bwiemz/supcom-engine/pull/764).
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
| Format checker and Git prefixes | Still assumes Git's default diff prefixes. | Second candidate: [fork issue #2](https://github.com/jbaersch/opensupcom-loud/issues/2); refresh its older base after the first contribution. |
| Localized retail test selectors | Five checks assume English button labels and fail on the German installation, with and without our fix. | [Fork issue #3](https://github.com/jbaersch/opensupcom-loud/issues/3); keep test portability separate from gameplay behavior. |

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
the full pinned upstream target. Upstream's native snapshot layout is 74; the
combined fork uses 75 and replay layout 18. The initially accepted LOUD gameplay
build uses snapshot layout 56.
Keep the accepted launcher, executable, saves and replays together until the new
build passes its own tests. A successful small upstream contribution does not
establish LOUD acceptance of the entire new upstream tree.

Follow the [collaboration guide](collaboration.md) for compact handoffs and the
[fork workflow](fork-workflow.md) for branches and publication.
