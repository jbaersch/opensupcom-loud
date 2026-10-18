# LOUD engine compatibility contracts

This note records the rules behind the LOUD contribution branch. It complements
the synthetic tests and [review index](../loud-commit-index.md). Upstream's
[provenance and compatibility policy](../provenance-and-compatibility.md) applies.

## Sources and scope

FA/LOUD Lua was inspected read-only to learn binding names, arguments, callback
order and ownership. Relevant callers include UI lobby/options code,
`simInit.lua`, `aibrain.lua`, `ScenarioUtilities.lua`, weapon classes, navigator
and build/assist callers. Their contents are not copied into this branch.
Blueprint and map identifiers in tests identify data; fixtures invent their own
tables and callback bodies. The behavioral references cited during development include Moho's
[Unit initialization/motion](https://raw.githubusercontent.com/Draiget/faf-re/master/src/sdk/moho/unit/core/Unit.cpp),
[weapon blueprint binding](https://raw.githubusercontent.com/Draiget/faf-re/master/src/sdk/moho/unit/core/UnitWeapon.cpp)
and [aim manipulator](https://raw.githubusercontent.com/Draiget/faf-re/master/src/sdk/moho/ai/CAimManipulator.cpp).
These inform contracts only; no decompiled source was copied. The inherited
engine conventions remain the reference; uncertain behavior is not promoted to
a promise of full Moho equivalence.

## Startup, errors and serialized launch data

Apply source UI options once per new state before constructing controls. Read
scenario markers from the script's environment without violating strict globals.
An ArmyPool is not an AI with a plan. Native army slots must exist before their
source brain callbacks.

Use the existing deterministic plain-table codec for complete player options,
including nested RGB colors and mod fields. Typed engine fields still determine
army identity; the incoming table cannot replace the scenario's army index/name.
Reject unsupported/cyclic/oversized data rather than silently discarding fields.
Propagate setup, brain, BeginSession and enabled prebuilt-unit errors. A
script-owned failed start receives no fallback commanders. Reload clears partial
state and returns to the front end before attaching gameplay or playback.

Replay format 17 carries the optional player table. Older replay layouts retain
their parsing/version guards; decoding them cannot reconstruct discarded player
fields. The save container remains 4 and build/content/snapshot guards remain.
Campaign extra-army/civilian mapping and common multiplayer resume are not
completed by this change.

## Weapons, orders and lifecycle

Zero native damage does not imply an inert weapon: scripts can use targeting and
aim callbacks without native damage. Explicit dummy blueprints are different and
do not create runtime weapons. Create a source weapon once, at its first source
access; return shared projectile-blueprint tables through the existing registry.
Advance aim after body motion and before firing. Shared turrets solve from their
muzzle under the existing arcs and firing tolerance.

Keep script behavior for death, production and assistance. General work tasks
allow friendly builders to share a site and area-assist commands to select
eligible existing work. Console-created structures use the same footprint snap
as mobile construction; scripted exact positions remain exact. Recorded unit
creation uses the command stream. Ground motion normalizes the orientation it
commits, without changing arbitrary raw entity/Lua setters.

Source callback/thread errors remain failures, and ordinary words containing
failure-like substrings are not assertion failures. No game-specific log
exceptions suppress errors.

## Exact navigation and query reductions

Check every crossed cell of a smoothed path segment. Only bypass A* when the
whole direct segment is clear. Default navigation observes newly blocked
segments, retains corners until their geometry is passed and permits retries
after a relevant grid change. This is the existing default navigator, not the
optional upstream Moho-pathing implementation.

World-segment checks retain the endpoints' offsets inside their grid cells;
a clear centre-to-centre line is not sufficient. A nearby corner is kept until
the actual continuation is clear. A completed temporary sidestep whose return
leg is obstructed requests a new route, while a unit still inside a building's
footprint retains its existing escape-route corner passing. An exact goal keeps
the final cell centre as an extra corner if replacing it would cross an obstacle.
Synthetic offset, sidestep and footprint-exit cases cover these integration
regressions. The retail choke fixture also rejects wall-cell crossings by the
default navigator; its original success criterion only required all units to
get across. These rules do not change the optional Moho navigator or add saved
navigation fields.


Each Pathfinder owns an LRU of at most 128 raw integer-cell routes, each at most
4,096 coordinate pairs (4 MiB coordinate payload bound). Keys include exact
start/goal cells, resolved movement class, effective water-draft float bits and
closed-in mode. Terrain/water/obstacle grid-version changes invalidate it. The
existing request gate and consumption happen before cache lookup; smoothing and
replacement with the caller's precise world endpoint happen afterwards. Misses
retain the old search cost, cap and expansion order. Empty and partial results
may be cached. Cache state is derived, unsaved and absent from sync checksums;
restore creates a fresh cache. `last_nodes_explored()` retains the result's
original search effort on hits, not physically performed expansions.

Registry acceleration cells shrink from 32 to 16 world units. They are separate
from script-observable ordered four-unit CollisionCells. Exact rectangle/radius
predicates, result ordering, target tie-breaks and pure boolean reductions stay
explicit. Enemy filtering avoids sorting rejected units; category-bit tests
replace repeated expression work only for equivalent rules.

Independent flat-scan tests exposed a pre-existing radius-boundary omission at
both cell sizes. A rounded float squared-distance predicate can accept a point
outside naive `center +/- radius` cell bounds. Keep that exact predicate and
widen only the candidate envelope. If R is the rounded radius square and S the
next larger float, an accepted rounded squared term is at most R, with exact
square below S. One outward float step beyond `sqrt(double(S))` also encloses
subtraction rounding. Form endpoints outward in double; floor/clamp cell indices
in double before integer conversion. Nonfinite queries/S use the exact flat
scan. All three radius kernels share the bound. Negative radii, underflow,
overflow, cancellation, entity movement and snapshot reconstruction are tested.
The fix can change outcomes of old boundary-edge recordings; performance controls
must include it on both sides.

## Scheduling and rendering

A larger lazy-sweep budget spreads physical Lua mass-loss cleanup before the
next full collection. It does not change scripts or claim all GC pauses are gone.
Solo live catch-up may yield between fully completed ticks after a soft 16-ms
budget; a single tick is never interrupted. It improves responsiveness at high
speed but can increase the wall time needed for the same number of ticks.

Formation previews use the established strategic-icon pipeline. Icon buffers
grow with the actual visible set rather than truncating a large battle. These
rendering changes do not feed back into the simulation.
