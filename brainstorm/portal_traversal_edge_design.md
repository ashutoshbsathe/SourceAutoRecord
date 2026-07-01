# `go_to` through portals — the GoToPlanner portal edge (fork-B)

**Status:** design pass, 2026-07-01. Not yet built. Read `portal_verb_recon_design.md` §5.3 for the R5
recon that motivates this; this doc is the build plan.

## What R5 established (the two facts this design turns into code)

Ran `sar_harness_portal_reachability_test` on `sp_a2_laser_over_goo` with an open blue↔orange pair across
the goo:

1. **The flat planner reads the far side as a disconnected component** — SEVERED both directions, pristine
   `goal_cell=WALKABLE`, `dz=0`, well under the expansion cap. `GoToPlanner::Plan` returns no path across
   the gap because its 2D lattice has no edge bridging the two islands.
2. **The engine teleports the player through on mouth-contact** — getpos before/after a walk-in is a clean
   portal transit (same z/pitch/roll, yaw flipped ~180°, mirrored standoff). No planner, no intent.

So placing a portal is inert for traversal until `go_to` grows an explicit edge. **Consequence for the
design:** we don't *teleport the player* ourselves — the engine already does that faithfully on contact.
The job is (a) make A\* treat the open pair as a graph edge, and (b) make the local controller drive the
body *into the mouth* and pick up on the far side.

## How `go_to` works today (the seams)

- **`MacroExecutor::GoTo(mark)`** ([MacroExecutor.cpp:1634](../src/Features/Harness/MacroExecutor.cpp#L1634))
  — resolve mark → straight `MarchTo`; on `BLOCKED` → `RouteAround`.
- **`RouteAround`** ([:936](../src/Features/Harness/MacroExecutor.cpp#L936)) — re-plan loop: reads feet,
  builds `GoToPlanner(mins, maxs, feet.z, targetKey, heldKey)`, calls `planner.Plan(feet, target)` →
  `vector<Vector>` legs, marches each leg via `MarchTo`, re-plans from current feet on a dynamic block.
- **`MarchTo`** ([:799](../src/Features/Harness/MacroExecutor.cpp#L799)) — the VFH local controller: per
  4-tick batch, steer the body toward the freest goal-ward heading (`ChooseVfhHeading`), camera on target,
  strafe framebulk, `AdvanceTicksBlocking`. VFH **avoids walls** — the crux obstacle for driving into a
  wall portal.
- **`GoToPlanner::Plan`** ([GoToPlanner.cpp:182](../src/Features/Harness/GoToPlanner.cpp#L182)) — lazy
  hull-probed 16u grid, 8-neighbour A\*, `SnapGoal` for an in-prop goal, 400-cell expansion cap.

A portal target routes through `RouteAround` automatically: the straight `MarchTo` BLOCKs at the gap, and
`RouteAround` re-plans — which is exactly where the portal edge lives.

## Design

### A. Enumerate open pairs + exit-landing geometry (in `GoToPlanner`)

At construction, walk the entity list for `prop_portal` with `m_bActivated==true`, pair via
`m_hLinkedPortal` (the `sar_harness_portal_probe` reads already do this). For each portal compute the
**front cell** = the stand-able point the body enters from / lands at:

```
normal  = AngleVectors(SE(portal)->abs_angles())      // portal forward, out of the wall
frontXY = portal.abs_origin + normal * kStandoff       // clear the wall + half hull (~24u)
frontZ  = down-trace from frontXY to the floor          // reuse the Probe floor trace
```

The pair yields one **bidirectional edge**: `frontCell(blue) ↔ frontCell(orange)` (entering blue lands you
in front of orange and vice-versa — approach and exit are the same front cell per portal). The **mouth
center** (`portal.abs_origin`) is stored too — the local controller steers at it, not at the front cell.

### B. The portal edge in A\* (bridge the components)

In `Plan`'s neighbour expansion, when the current cell is a portal's front cell, add the linked portal's
front cell as an extra neighbour with a small fixed cost (the transit is ~free vs walking). That single
injected edge connects the two grid components, so `Plan(feet_on_near, goal_on_far)` now returns a path.

**The path must carry the transit** so `RouteAround` doesn't try to *walk* the straight line between two
front cells that are separated by goo. Change `Plan`'s return type:

```
struct Waypoint { Vector pos; bool portalHop = false; Vector mouthCenter; };
std::vector<Waypoint> Plan(...);
```

A `portalHop` waypoint means "the leg ending here is a portal transit; drive into `mouthCenter`, don't walk
the straight line." Ripple is tiny: the two reachability CON_COMMANDs read `.pos`/`.size()`; `RouteAround`
reads `(*legs)[i].pos` and branches on `.portalHop`.

### C. Local controller — portal-approach mode (in `MacroExecutor`)

New leg handler `MarchThroughPortal(context, mouthCenter, exitPos)`:
1. Yaw the body at `mouthCenter.xy`, full forward framebulk, **VFH off** (drive straight at the wall — the
   one place we *want* to hit it, because the mouth transits instead of blocking).
2. Each batch, watch feet for a **teleport discontinuity**: a single-batch jump greater than a threshold
   that lands near `exitPos`. That's the engine's transit → return `SUCCESS`, body is on the far side.
3. Timeout (no transit within N batches) → `BLOCKED`.

`RouteAround` marches the pre-portal legs with normal `MarchTo`, calls `MarchThroughPortal` for a
`portalHop` leg, then continues `MarchTo` from the emerged position.

**RESOLVED (2026-07-01): simulate-only, no fallback.** The body walks into the mouth and the *engine*
transits it (on-thesis — the verb-grammar reserves teleport for inert objects; the player is simulated).
On timeout the leg returns `BLOCKED` and surfaces honestly — **we never FCPS-teleport the player to fake a
crossing.** A portal the body can't enter is a real failure, reported as one, not papered over. This keeps
the actuator strictly to mechanics the game itself performs.

### D. Derail guard (avoid accidental transits)

A `go_to` on the *reachable* side whose VFH path grazes an open mouth would transit unexpectedly and strand
the agent on a component the planner thinks is severed. Guard: stamp open-portal-mouth cells as an obstacle
in `Probe`, but leave the edge's front cells walkable.

**Scope note:** a *wall* portal sits on a solid wall, so its cell already probes `IN_WALL`/BLOCKED — wall
portals **self-guard**, no code needed. Only *floor/ceiling* portals (mouth on walkable floor) need the
explicit stamp. Since v0 is wall-portals-only (below), the guard is essentially deferred with floor
portals; note it so it isn't forgotten.

## Decisions (locked 2026-07-01)

1. **v0 = wall portals only.** Floor/ceiling portals emerge with vertical momentum (a fling), a different
   traversal mechanic — deferred; v0 handles vertical-surface pairs (the common PeTI case).
2. **v0 = similar-height islands (single-`refZ` planner).** `GoToPlanner` is anchored at one `refZ` with a
   ±~128u floor-probe window; stepped floors are a separate parked workstream. v0 assumes the two islands
   are within that window (both were `z=32` on the R5 map).
3. **`Plan` returns `vector<Waypoint>`** — a typed waypoint carries the transit; tiny ripple to the two
   reachability commands.
4. **Simulate-only, no fallback** (§C) — walk the body through; timeout → `BLOCKED`, never teleport.

## Phasing (small, C++-only, each in-engine verifiable)

- **P1 — enumerate + geometry (read-only).** `GoToPlanner` pair enumeration + front/exit/mouth cells; a
  `sar_harness_portal_edge_dump` recon command prints them. *Verify:* dump on `laser_over_goo` matches the
  two portal positions + sane front cells on the floor.
- **P2 — the A\* edge + typed `Waypoint`.** Inject the edge; switch `Plan`'s return type. *Verify:* the
  **already-built `sar_harness_portal_reachability_test` flips SEVERED → REACHABLE** on the same setup —
  a zero-scaffold regression check.
- **P3 — `MarchThroughPortal`.** Drive-into-mouth + transit detection. *Verify:* macro_repl `go_to` a
  far-island mark → the body physically transits and arrives.
- **P4 — wire the portal leg into `RouteAround`.** Branch on `portalHop`. *Verify:* end-to-end `go_to`
  across the goo reaches the far mark, `SUCCESS`.
- **P5 — derail guard.** Floor-mouth obstacle stamp (deferred while wall-only self-guards). *Verify:* a
  non-portal `go_to` near an open mouth doesn't transit.
- **P6 — smoke + prompt.** `agentloop_smoke` go_to-through-portal round-trip; update the `go_to` grammar
  doc ("routes through open portals").

## Scope estimate

~280 LOC C++, no proto change until P6's smoke (the edge is internal to `GoToPlanner` + `MacroExecutor`;
`place_portal` already exposes portal creation). Python untouched except the P6 smoke/prompt. Deferred:
floor/ceiling portals, stepped-floor islands, multi-pair routing (>1 open pair — the enumeration supports
it but v0 verifies a single pair).
