# Engine/test commit index

Commits are listed in dependency order from upstream `a799c964`. Review or
cherry-pick the required predecessors too; this is not a promise that every
individual commit applies independently to the newest upstream main.

| Public commit | Change |
| --- | --- |
| `507da4d93f94` | lua: read script-local scenario start positions without strict-global lookup |
| `2057f5127983` | lua: initialize ArmyPool without an AI plan |
| `48bb6c36d07e` | ui: apply source options before building each UI state |
| `9433e117653f` | session: carry complete player data through validated source starts |
| `65437321eeb8` | lua: expose category selection through the existing UI selection path |
| `86e6b99195d3` | sim: recognize source-observed uppercase threat type aliases |
| `3ca3571a8424` | lua: expose active navigator goals as GetGoalPos |
| `725da4b12bad` | lua: exclude the local sender from lobby broadcasts |
| `e4d8f93f012c` | core: match complete Lua assertion failure tokens |
| `92a298aa66f3` | sim: let zero-damage scripted targeters acquire and aim |
| `006729efa99d` | lua: initialize weapon scripts on first source access |
| `47b84f129ce7` | sim: solve shared weapon aim from the muzzle within firing tolerance |
| `41a3687c8f3a` | lua: expose base entity Kill and count damage callback errors |
| `6c5ba4fc23df` | lua: report applied speed levels and notify the single-player HUD |
| `6af859661598` | lua: connect the source cheat dialog and recorded unit creation |
| `32759b562a43` | lua: apply string lobby unit caps to native armies |
| `de64546af24f` | lua: expose native air and fuel blueprint defaults |
| `27af33ae2f12` | lua: snap console-created structures to the build grid |
| `ac7015daf1ef` | lua: retain zero-radius weapons in timed buff calculations |
| `aac0cb5a287b` | sim: normalize orientations committed by ground motion |
| `5479b4753816` | sim: share mobile construction sites between friendly builders |
| `d9d2b56451f0` | renderer: preview formation slots with strategic icons |
| `4a1912feb35f` | sim: separate aircraft during navigator-free circling |
| `cf853692ede2` | tests: exercise aiming during banked and turning hover flight |
| `188ca3fdfa28` | lua: return shared projectile blueprints to weapon scripts |
| `2c2d24deaec4` | sim: restore point assistance through shared work tasks |
| `6808b3ad140d` | fix: exclude explicit dummy blueprints from runtime weapons |
| `55be05be20b0` | fix: expose complete collision-cell entity rectangle queries |
| `a287dce2e6aa` | fix: validate full path segments and bypass A-star for clear routes |
| `c9c96ad7a3ac` | fix: advance weapon aim once after body motion before firing |
| `1356e4a00488` | fix: grow strategic icon buffers for large battles |
| `0d3d5c984a0f` | perf: filter non-enemy unit candidates before query sorting |
| `2781b00ac478` | sim: reduce pure unit query work while preserving target selection |
| `cfb0786ec213` | sim: replan blocked default routes and retain unpassed corners |
| `e7adbd8c9f00` | lua: initialize native army slots before source brain creation |
| `7774b2370985` | sim: reuse category bits for unit weapon restrictions |
| `d235bab9ca9f` | lua: finish mass-loss sweeps before the next collection |
| `9f2d776c8ad8` | app: yield live solo catchup between expensive ticks |
| `f643d4826c35` | map: reuse exact raw routes within one grid version |
| `bdef54ab06da` | sim: retain rounded radius boundary members in indexed queries |
| `a97e5c32e075` | sim: tighten broadphase cells after exact-query comparisons |
| `dd46f4e508fa` | test: name the integral row in route-cache replacement fixtures |

## Publication preparation

- `d0bdd15060d2` — declare GLFW privately for the app and integration runner,
  which include its headers directly. No runtime C++ changes.
- The following documentation/CI commit records fork scope, source contracts,
  validation limits and CI triggers for `codex/**` contribution branches.

## Pinned upstream integration candidate

The candidate merges upstream `d98ad203` into public LOUD base `44f04de3`,
retaining the initial stack and upstream history. These entries follow that
merge; see the [acceptance record](validation/2026-10-18-loud-upstream-integration.md)
for the exact tested source head and limits.

| Commit | Change |
| --- | --- |
| `b2852e19` | Merge the pinned upstream, reconcile construction/flight/ground motion and scaled icon rendering. |
| `b6d4de67` | Share the authoritative native cheat option with UI and queued callbacks. |
| `72c351a3` | Adopt the separately reviewed localized retail-selector tests from `69e6ab4f`. |
| `c82d9a59` | Compose muzzle and shared aiming with the animated barrel frame. |
| `7cab78f0` | Avoid restarting weapon initialization through a destroyed unit's retained handle. |
| `95436317` | Restore mobile-build footprint snapping at preparation. |
| `90c9c14b` | Preserve clear physical route segments, sidestep returns and footprint escape. |
| `65ff5595` | Give synthetic material plates valid unarmed fixtures and check actual projectile-table identity. |
