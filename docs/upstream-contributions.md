# Upstream contribution queue

Checked against upstream `d98ad20318aed5c44ae861e59a0358d84ed62b99`
on 2026-10-18, after [batch #700](https://github.com/bwiemz/supcom-engine/pull/700)
and [batch #719](https://github.com/bwiemz/supcom-engine/pull/719).
This is a comparison snapshot; check GitHub again before starting a contribution.
It does not certify those upstream changes on the LOUD integration branch.

| Area | Current upstream work | Action here |
| --- | --- | --- |
| Hover orientation | [#695](https://github.com/bwiemz/supcom-engine/pull/695) is merged. | Support with additional regression findings if useful; no duplicate implementation PR. |
| Shared mobile build site | [#715](https://github.com/bwiemz/supcom-engine/pull/715) is merged, including occupied-site rules. | Compare the complete rules before replacing our fork's older fix. Remove this from the list of new PR candidates. |
| Aircraft separation | [#693](https://github.com/bwiemz/supcom-engine/pull/693) removes in-flight separation to match Moho. | Our extension conflicts with this rule. Resolve it during a dedicated integration; do not propose it as an original-game compatibility fix. |
| Barrel aiming and strategic icons | [#696](https://github.com/bwiemz/supcom-engine/pull/696) and [#687](https://github.com/bwiemz/supcom-engine/pull/687) are merged. | Recheck our aiming and formation-preview changes against these implementations. |
| Air/fuel blueprint defaults | Still omitted from upstream's Lua registration. | Submitted as [upstream PR #733](https://github.com/bwiemz/supcom-engine/pull/733); [fork issue #1](https://github.com/jbaersch/opensupcom-loud/issues/1) tracks follow-up. |
| Format checker and Git prefixes | Still assumes Git's default diff prefixes. | Second candidate: [fork issue #2](https://github.com/jbaersch/opensupcom-loud/issues/2); refresh its older base after the first contribution. |
| Localized retail test selectors | Five checks assume English button labels and fail on the German installation, with and without our fix. | [Fork issue #3](https://github.com/jbaersch/opensupcom-loud/issues/3); keep test portability separate from gameplay behavior. |

The isolated blueprint candidate is `4c079451` on
`codex/upstream-blueprint-defaults-current`; its
[validation record](validation/2026-10-17-blueprint-defaults.md) documents the
exact base, reproducer, source and test limits. It contains none of the LOUD
integration stack. Submitted on 2026-10-18 as
[PR #733](https://github.com/bwiemz/supcom-engine/pull/733). The exact source
head passed all five fork CI jobs, including MSVC and eight synthetic cross-OS
pairs. The [upstream workflow](https://github.com/bwiemz/supcom-engine/actions/runs/39108836806)
awaits maintainer approval; it has not run its jobs yet.

The former clang-tidy blocker is resolved by merged
[#692](https://github.com/bwiemz/supcom-engine/pull/692). It is no longer a reason
to hold a current-base candidate.

## Next integration package

Integrate new upstream behavior on a separate branch with its own acceptance.
First reconcile the overlapping hover/build-site changes and the contradictory
aircraft-separation rule; then adapt the rendering changes. The current upstream
native snapshot layout is 60; the accepted LOUD gameplay build uses layout 56.
Keep the accepted launcher, executable, saves and replays together until the new
build passes its own tests. A successful small upstream contribution does not
establish LOUD acceptance of the entire new upstream tree.

Follow the [collaboration guide](collaboration.md) for compact handoffs and the
[fork workflow](fork-workflow.md) for branches and publication.
