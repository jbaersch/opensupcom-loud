# Integrating the upstream beat and motion phases into LOUD

The integration starts from fork candidate
`61f9401f0b93268d172ba1a15b89f1c088496663`, whose previous upstream base is
`d98ad20318aed5c44ae861e59a0358d84ed62b99`, and merges the complete landed
upstream target `9f4547810899a2e02b8e2dc94b93a40d52868340`.
Open pull requests are not included.

The upstream [tick-order design](2026-10-18-moho-tick-order-design.md)
describes the new phase contracts and their behavioral sources. The fork's
[existing contracts](2026-10-16-loud-fork-contract.md) remain relevant where
the implementation below retains them.

| Area | Integration decision |
| --- | --- |
| Simulation phases | Adopt upstream's army/weapon/order/Lua/motion/coordinate-commit phases, resource banks, deletion queue, beat scheduling and pause semantics. Remove the fork's old late-fire scheduling. |
| Scripted projectile damage | Adopt upstream's distinction between scripted impact damage and engine-fired fallback projectiles, including its serialized marker. |
| Aim geometry | Retain muzzle-relative and shared-controller residual aiming. Refresh the pose before an aim controller solves, so earlier manipulators' new angles and their bone transforms agree within the motion phase. No second weapon firing phase or relaxed tolerance. |
| Script contracts | Preserve zero-damage scripted targeters, one-time weapon initialization, shared projectile blueprints, explicit dummy exclusion and the corrected nonempty platoon enumeration. Use upstream's unified work focus, retaining the fork's unit-reclaim fallback for the Lua query. |
| Construction and assistance | Preserve shared-site accounting, area assistance, active enhancement/silo costs and callback lifetime checks within the new order/resource phases. |
| Navigation and queries | Keep exact cell membership, rounded-radius bounds, deterministic target tie-breaks, route caching, obstacle invalidation and full physical-segment checks. Collision-cell relinking now belongs to coordinate commit; the extra entity/projectile family remains included. |
| Rendering | Adopt upstream's independent world views, per-view camera and viewport. Retain growing strategic-icon buffers and formation previews, updated in the per-view render path. |
| Live solo pacing | Retain the soft budget between complete beats. Let paused and game-over beat processing follow upstream, including UI and queued-command flow. |
| Test organization | Keep the UI runner split into its own translation unit; port upstream's changes into that file rather than restoring the duplicate block. |

## Test adaptations

Direct synthetic motion and teleport fixtures now explicitly commit coordinates.
Movement following an ordinary pending target change is checked on both sides
of the commit boundary. Synthetic completed economy units initialize their
storage ownership before receiving resources. Task cancellation passes the full
simulation context. Recorded command assertions use beats.

These adaptations retain the geometric, resource-accounting, callback and
lifetime assertions. Initial failing cases and the subsequent results belong
to the integration validation record. The stricter shared-turret direction
assertions remain unchanged.

## Development file identity

The combined replay layout is **18**: upstream 17 introduced beat scheduling,
while the earlier fork independently used 17 for complete player tables.
Upstream layouts 16 and 17 must still decode without a player-table field; the
combined writer and reader introduce that field at 18. Old fork recordings
remain with their original build; there is no cross-build playback promise.

The native snapshot layout is **75**, distinguishing this fork's combined
setup/query contracts from upstream 74. The save container remains **4**.
Build, content and binary identity checks remain enabled.

## Acceptance boundary

Integration is source work, not an automatic replacement of an accepted
executable. Record the tested commit, full local checks, original-data runs,
replay comparisons, missing goldens and platform limits before adoption.
The former `self` and `LOUDSQRT` AI failures require fresh normal-game evidence;
a different schedule that avoids them in one run does not prove the installed
scripts were repaired. Keep the accepted launcher and older saves intact.
