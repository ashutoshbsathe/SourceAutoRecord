<!-- Deep dive on the shipped go_to locomotion stack (GoToPlanner + MarchTo/RouteAround VFH),
written 2026-07-05 to ground a replacement design. Produced from a full read of the source plus a
6-dimension multi-agent dissection, each finding adversarially verified against the exact lines.
This is the "understand current state" half; the redesign (§8 tastefulness calls) is the sequel.
Forcing function: azorae_stride_postmortem.md — ~24% of a 100-step budget drowned in descent
flailing this stack structurally cannot do. -->

# go_to deep dive — the current planner, why it's flat, and where a replacement earns its keep

Every mechanism below carries a `file:line`. Where a claim survived a hostile re-read it's stated
flat; where it's conditional it's marked. `GoToPlanner.{hpp,cpp}` = the A* grid;
`MacroExecutor.cpp` = the verb + the VFH march.

---

## 1. The contract — input, output, expected behavior (start basic)

**What go_to is asked to do:** walk the player from wherever they stand to a named mark, on foot,
routing around obstacles, and report whether it arrived.

**INPUT** is two things, one explicit and one implicit-and-load-bearing:
- The **mark string** — classified by `ClassifyTarget` (MacroExecutor.cpp:534-564) into `PORTAL`
  (`Pb`/`Po`), `PANEL` (`S<digits>[@u,v]`), or `ENTITY` (`<digits>`), then resolved by
  `ResolveTarget` (:566-593) into a world-space `center` + a `targetKey` (only for ENTITY marks,
  :2443-2448). Every resolve failure on this path collapses to a single code `BAD_MARK` — go_to
  never emits `AMBIGUOUS`/`WRONG_TARGET` (those live in `RequireEntityMark`, which go_to doesn't
  call). A malformed panel suffix (`S5@0.3`, no comma) silently degrades to `BAD_MARK`; `u,v` are
  `clamp01`'d, never rejected (:546-556).
- The **player's feet-z at call time** — the implicit input. It becomes `refZ`, the single
  horizontal reference plane the entire A* grid is measured against (§4). This is the input nobody
  passes on purpose and that quietly decides half the failures.

**OUTPUT** is a `MacroResult`: `result_code`, `reached`, `final_dist`, `moved_dist`, `detail`. The
codes go_to can emit, exhaustively:

| code | exact condition | file:line |
|---|---|---|
| `SUCCESS` | arrived within `reachRadius` (straight march or final A* leg) | :1028-1031, :1108-1111 |
| `ADVANCED` | `!reached && moved > kReachRadius(48) && code == BLOCKED` | :2544-2546 |
| `BLOCKED` | global stall **or** empty A* plan (no route) **or** tick budget spent, and `moved ≤ 48` | :1038, :1085, budget fallthrough |
| `NO_PLAYER` | player vanished mid-march | :1024 |
| `CANCELLED` | gRPC stream dropped (resolve / march / settle) | :2473, :2498, :2529 |
| `BAD_MARK` | resolve failed | :2478-2483 |

**Expected behavior, stated honestly:** on a flat, line-of-sight PeTI chamber go_to is correct and
cheap. Its designed scope is exactly that — `MacroExecutor.cpp:50` says *"flat chambers,"* the A*
doc is titled for *"flat chambers,"* multi-level was punted to milestone M3
(astar_routing_design.md:73-77). It is not a general locomotion primitive; it is a flat-floor one
with a fallback pocket-escaper.

---

## 2. Architecture — two layers, and the inversion

go_to is a **reactive controller with a planner bolted underneath**, not a planner with a
follower. Control flow (`GoTo`, MacroExecutor.cpp:2489-2497):

1. **Primary: `MarchTo`** — a greedy 360° VFH march runs *straight at the target* (:2489-2491).
   No grid, no plan, no lookahead. Most go_tos finish here and spend *zero* grid traces.
2. **Fallback: `RouteAround`** — only if the straight march returns `BLOCKED` (:2495) does the
   real A* run (`GoToPlanner::Plan`), and it then **marches each waypoint leg back through the
   same `MarchTo`** (:1095) at a loose `kLegRadius=24` arrival, final leg at the true `reachRadius`.

The inversion is deliberate: the doc chose *"A\* sits above the shipped VFH executor … touched
only by extract-method, never rewritten"* (astar_routing_design.md:40-42). That single decision —
don't rewrite VFH — is why the greedy layer is authoritative and why it carries ~250 LOC of
trap-escape heuristics (§6) that a plan-follower would not need. **The real planner is the
fallback; the plan-less controller is primary.** Everything downstream flows from this.

`RouteAround` is a *second* control loop layered on `MarchTo`: it decrements one shared
`kGoToMaxTicks=400` budget across legs (:1101) and re-plans up to `kMaxReplans=2` on a
dynamically-blocked leg (:1067). So a routed go_to runs two nested march loops.

---

## 3. Perception — how it sees the world

**Everything is live engine traces on the main thread. No nav mesh, no BSP nav graph, no
persistent cache.** Source ships no navmesh for P2; the planner is built from scratch per go_to.

- **Floor** = one straight-down point-ray, `MASK_PLAYERSOLID`, from `refZ+40` down 168u
  (GoToPlanner.cpp:107-116). Miss → `BLOCKED`/`NO_FLOOR`. `floorZ = endpos.z`.
- **Walkable** = a zero-length player-hull `TraceHull` at `floorZ+2`; `startsolid`/`allsolid` →
  `BLOCKED`/`IN_WALL` (:122-129). Relies on the documented quirk that a hull embedded in a wall
  returns `fraction 1` with a garbage normal, so the code keys off the bool (startsolid), not
  fraction (:118-121).
- **Edge traversal** = a swept `TraceHull` between cell centers at `max(floorZ_a,floorZ_b)+2`
  (`Passable`, :146-167), plus a diagonal corner-cut guard (no cutting a blocked corner) and a
  source-cell exemption (A* may *leave* a start cell that probes BLOCKED — you're standing on it).
- **Obstacles** are **class-name gated, not geometric**: only the 8 classes in `IsObstacleClass`
  (cube / monster_box / floor turret / button family, :34-44) are perceived as obstacles. Anything
  else solid is invisible to the stamp and caught only if it happens to fail a hull trace.

Consequences of "live traces from one plane":
- **Portals are invisible.** The flat grid has only 8 planar neighbor moves (:213-214); an open
  portal pair reads as two disconnected components → `Plan` returns `{}` → SEVERED. The
  `sar_harness_portal_reachability_test` recon (:404-502) exists precisely to demonstrate this.
  Portal traversal is a separate verb family by design.
- **The recon commands lie slightly.** `sar_harness_probe_cells` / `_reachability_test` build a
  throwaway planner with `targetKey=0` (:278,316,457), so the occupancy map an operator eyeballs
  does *not* apply the target/held skip — a cube-on-target reads `#`/`O` in recon but is skipped in
  the live plan.

---

## 4. Why it's single-plane — and why it's OVER-determined

This is the crown jewel, and it's the direct cause of azorae's 24-step descent flail. Single-plane
is **not one bug**; it's ~5 independent mechanisms across two subsystems. Fixing any one leaves the
others enforcing flatness — which is exactly why the postmortem's lesson is *"you must relax BOTH
the planner AND the executor."*

**On the MARCH (the primary fast path — no grid involved at all):**
- **M1 — `CheckEdge` refuses any drop > 64u.** The march's cliff guard casts a down-ray from
  `feet+18`, 24u ahead, reaching only `feet−64` (`kStepDownMax`, MacroExecutor.cpp:747-761).
  `ChooseVfhHeading` zeroes any bin CheckEdge flags and re-picks (:902-903). So the straight march
  *structurally refuses to step off any ledge deeper than 64u* — and this is the **only** flat gate
  on the fast path; `refZ`/the grid don't even exist here.
- **M2 — the camera is pinned to pitch 0.** `ApplyAbsoluteView(QAngle{0, goalBearing, 0})` every
  batch (:999). The walker never aims up or down.
- **M3 — arrival, progress, and stall are all `Length2D()`.** The reach test forces `forward.z=0`
  (:963-965); the global-stall/`bestDist` and the wedge all measure horizontal displacement
  (:1035, :974). A purely vertical move reads as *zero progress* → trips stall → `BLOCKED`. Even a
  target directly above/below reads "arrived" on horizontal proximity alone.

**On the GRID (the A* fallback, reached only after the straight march BLOCKEDs):**
- **G1 — the `refZ` anchor.** `refZ` = feet-z captured at planner construction
  (GoToPlanner.cpp:49; call site MacroExecutor.cpp:1077). Every cell's floor is probed from
  `refZ+40` down 168u, so a floor is detectable only in **z ∈ [refZ−128, refZ+40]** — asymmetric:
  a **40u** step-up ceiling (the ray *originates* at refZ+40; a higher floor is above the emitter →
  never hit → `NO_FLOOR`) versus a **128u** step-down floor. 3.2× more tolerant of drops than
  rises, purely because it probes from above. *(Correction to the naive claim: refZ is not "frozen
  forever" — `RouteAround` rebuilds the planner from current feet on each of up to 2 replans, so it
  re-anchors up to 3× per go_to. But never mid-march.)*
- **G2 — `Passable` never gates `|Δfloor|`; connectivity is pure 2D.** `floorZ` is stored per cell
  but its *only* use is lifting the flat hull-sweep to `max(floorZ_a,floorZ_b)+2` (:159). There is
  **no vertical/step edge type** — A* has 8 planar neighbors, each gated solely through this flat
  sweep. Across a step-down the sweep runs at the *higher* floor and the riser/lip occludes it →
  edge severed. *(Medium confidence: severance is conditional on the riser intruding the swept
  hull; a very shallow lip within `kHullLift` may clear. Needs a live TraceHull to pin the exact
  threshold.)* `floorZ` is the *"2.5D-ready seam"* the doc pre-provisioned and never wired
  (astar_routing_design.md:76-77) — dead weight for traversability today.
- **G3 — the two subsystems disagree on the vertical budget.** Grid probes **128u** down; CheckEdge
  caps at **64u**. In the 64–128u band the grid calls a cell walkable but the march refuses to walk
  there — a self-inconsistent envelope that produces false-positive plans *if* G2's flat sweep
  happens to clear the riser (the same conditional as G2).

**The takeaway:** descent is refused redundantly. Even a perfect layered-Z planner (fixing G1+G2)
still hands legs to a `MarchTo` whose CheckEdge (M1) and `Length2D` progress (M3) independently
refuse the drop. A replacement must move the planner *and* the walker together, or it ships a plan
the body won't take. This is the single highest-leverage fact in the document.

---

## 5. How it avoids things — both layers, almost-but-not-quite the same

Obstacle avoidance is **duplicated across the two subsystems**, with two byte-identical class lists
and three copies of the footprint math.

**Static walls:** `Probe`'s startsolid hull-test + `Passable`'s swept hull + corner-cut guard
(above). The per-cell wall test is *zero-length* (`TraceHull(at,at)`) while the per-edge test is
*swept* — so a cell can be WALKABLE yet every edge into it non-Passable.

**Obstacle props (cubes/boxes/turrets/buttons):**
- **A* layer** — the ctor snapshots each prop *once* as a circle `{x, y, footprintR + halfWidth}`
  (GoToPlanner.cpp:70-89); `Probe` step-3 stamps any cell whose *center* falls inside as `BLOCKED`
  (:133-140). Static per plan — a prop that moves after construction is stale in A*. Center-only
  sampling means a cube whose circle covers only cell corners leaves the cell WALKABLE (16u
  granularity gap).
- **VFH layer** — `InjectObstacles` re-scans the *entire live entity list* every batch (:822-860)
  and lowers per-bin clearance analytically. This is the *only* layer that tracks moving props. But
  when the player footprint *overlaps* an obstacle it blocks the **whole 90° arc** toward it
  (`halfWidth=90`, :851-852) — a sledgehammer that boxes out huge sectors when standing beside a
  cube.
- The two class lists (`IsObstacleClass` GoToPlanner.cpp:34-44 vs `IsGoToObstacleClass`
  MacroExecutor.cpp:785-795) are **verbatim duplicates in two translation units** — edit one, the
  A* and VFH avoidance sets silently desync.

**Identity skips** (so the route isn't blocked by its own goal or cargo): `targetKey` (the
destination prop), `heldKey` (the carried cube, `g_heldEntityKey`), and an *overlaps-the-target*
skip (so the button a target cube sits on doesn't block the approach). This skip logic is written
**three times** — the ctor inlines it (:54-89), `InjectObstacles` duplicates it (:841-846), and
`TargetFootprint` (:802-816) is a partial helper the ctor doesn't even use.

**The wedge detector** (MarchTo, :969-986) papers over a real failure the point-ray *cannot* see:
ray says clear, feet don't move (a rim/lip/hull-clip wider than the ray). After 2 stuck batches it
blocks the committed heading ±1 bin for 16 batches so VFH veers off. It's a *reactive* patch for
the point-ray-vs-swept-hull fidelity mismatch — it only learns the block *after* failing.

**What is NOT avoided at all:**
- **Goo / slime / lava** — the down-ray uses `MASK_PLAYERSOLID` and lands on the goo-*bottom*
  world brush, reading it as walkable floor. No point-contents read anywhere. Documented as a known
  false-pass at MacroExecutor.cpp:1147-1149. go_to will route a player straight into goo.
- **Fizzlers / lasers** — not obstacle classes, non-solid to the mask; routed straight through.
- **Doors** — no door class; a closed door is caught only as a static wall *at probe time*; a door
  that opens/closes after the snapshot is picked up only by the one replan.
- **Moving props at plan time** — the A* snapshot is frozen; only VFH + the replan react.

---

## 6. The "how many times" audit — repeated work (the part you asked me to count)

go_to is dominated by two operations that both run inside `ChooseVfhHeading` on **every** MarchTo
batch (batch = 4 ticks; up to `kGoToMaxTicks/4 = 100` batches per march):

| what | multiplicity | cost | why it's redundant |
|---|---|---|---|
| **Full entity-list scan** (`InjectObstacles`) | 1× per batch → ~100× per march | linear over all **8192** edict slots, re-deriving obstacle OBB footprints from scratch | obstacles barely move in a ~1.7s march; ~99% of scans reproduce identical circles. No memo. (Note: 8192 is *slot visits*; classname/OBB math only on populated/obstacle slots.) |
| **24 clearance point-rays** (`RayClearance`) + up to 24 `CheckEdge` re-picks | 24× per batch → ~2,400 per march | one engine trace each | the body moves a few units/batch; consecutive 360° fans are near-identical, all re-traced |
| **Blocking main-thread hops** | **~2× per batch** → ~200 per march | `RunOnMainThreadSync` (a 100µs busy-poll, HarnessThread.hpp:45-47) **+** `AdvanceTicksBlocking` (a condvar wait, :22-32) | two separate cross-thread round-trips serialize the verb behind the tick loop |
| **`TargetFootprint`** (target OBB re-derivation) | ctor-inline + standoff(:2463) + **~100× via InjectObstacles** + ×(1+replans) | live OBB read | the target's footprint is recomputed ~100-400× per go_to |
| **Fresh `GoToPlanner` per replan** | up to 3× per go_to | each ctor re-scans 8192 slots; the lazy `cells_` cache is **discarded** and re-probed | the warm cell cache built on attempt N is thrown away for attempt N+1 |
| **Nothing persists across go_to calls** | every call | full rebuild of grid + obstacle snapshot over *static* chamber geometry | two consecutive go_tos in the same room re-scan the same 8192 entities, re-probe overlapping cells |
| **`Passable` edge sweep** | every A* edge, every replan | `TraceHull` between centers, **uncached** (only the endpoint `At()` cells memoize; 2-4 hash lookups/edge on top) | the expensive directional sweep is recomputed for every edge revisited |
| **Two disagreeing geometry models traced separately** | every routed go_to | planner hull-sweeps (128u probe) **+** march point-rays (64u CheckEdge) | same physical geometry queried twice at different fidelities that can disagree — duplicated work *and* the root correctness hazard |
| **Four open-coded "trace-down-to-floor-then-hull-fit" copies** | — | Probe(168u) / CheckEdge(82u) / standoff-seat(82u) / seat resolvers | one primitive rewritten 4× with 3 different windows — the drifting windows *are* the G3 disagreement |

The blast radius is wider than "literal go_to": `interpose`/carry verbs reuse the same
`MarchTo`+`RouteAround` stack (:1151-1154), so every count above multiplies across them.

The one-line version: **a routed go_to pays the reactive march cost roughly twice (the speculative
straight march that BLOCKEDs, then the per-leg re-march), plus the planner's hull sweeps, plus a
full 8192-entity rescan every 4 ticks — none of it cached across batches, replans, or calls.**

---

## 7. Shortcomings — consolidated

Locomotion / correctness:
1. **Single-plane, over-determined** (§4) — 5 independent flat gates; can't descend a plain ledge
   >64u or climb >40u; drowned ~24% of azorae's budget.
2. **`ADVANCED` is semantically overloaded** — it means only `moved>48 && BLOCKED`, conflating
   "severed by wall/height, retry futile" with "just slow, ran out of ticks, retry helps." The
   disambiguating `BlockReason` enum (`NO_FLOOR`/`IN_WALL`/`OBSTACLE`) is *computed per cell and
   thrown away* — "diagnostic only, A* never reads it" (GoToPlanner.hpp:24). No distinct `TIMEOUT`
   code; budget-exhaustion is indistinguishable from a genuine no-route. This is what let azorae
   retry `go_to 15` ~8× with no behavioral change.
3. **Goo/slime/lava blindness** — a documented false-pass; go_to routes into hazard liquid.
4. **Planner ↔ march disagreement** — two traced geometry models, two floor windows (128 vs 64);
   the planner can promise a leg the body refuses (the wedge table exists *because* of this).
5. **Portals invisible** — the far side reads SEVERED; placing a portal is inert for traversal.

Efficiency (§6): per-batch 8192-entity rescan; ~200 blocking hops/march; planner rebuilt-and-cache-
discarded per replan; nothing persists across calls; jagged 16u path re-VFH'd leg-by-leg.

Duplication: two byte-identical obstacle-class lists; triplicated footprint/skip math; four
open-coded floor-fit primitives; `Move()` duplicates GoTo's stop+settle+ADVANCED tail.

Legibility / edge cases (from the completeness pass):
- **Start-cell-blocked is march-unsafe** — `Passable` exempts the source, but the *straight march*
  has no such exemption; spawn inside an obstacle's influence and every bin boxes → stall → BLOCKED
  before A* ever runs.
- **`SnapGoal` is dead code on the fast path** — goal-on-a-cube is only snapped inside `Plan`; the
  straight march arrives on `reachRadius` alone, so the two "close enough" mechanisms can disagree.
- **Shared leg-budget can starve the final leg** — a long jagged intermediate path spends the
  budget so the true-`reachRadius` final approach BLOCKEDs a few units short → reported ADVANCED.
- **`kPlanMaxCells=400` cap is indistinguishable from a severed grid** — dense obstacle fields
  exhaust the cap → `{}` → BLOCKED, same as a genuine no-route; and a replan can burn all attempts
  making sub-400 progress each time.
- **Cancel-during-settle is latent** — the 24-tick settle `AdvanceTicksBlocking` has no cancel
  check; a drop isn't observed until the next sync.
- **Camera left facing the goal, no restore** — every follow-up verb needing a specific aim pays an
  orientation-churn re-slew.
- **Recon commands use `targetKey=0`** — the occupancy map you eyeball disagrees with what go_to
  actually plans.

---

## 8. Tastefulness calls for the replacement

The design surface, organized. Tags: **[REWRITE]** = the coherent architectural change; **[SEAM]** =
a seam the doc pre-provisioned and azorae now forces; **[REVISIT]** = a past decision worth
reopening; **[FALLS-OUT]** = a cleanup that's nearly free once the rewrite lands; **[NON-BUILD]** =
resist it. The big three (1-3) are one coherent move; do them together.

**1. [REWRITE] One geometry oracle — the march reads the planner's cells, not its own private
ray fan.** Today two disagreeing traced models exist. Collapse to one: the lazy grid *is* the
occupancy truth, and the march steers by querying cells it already probed (`At().state/.floorZ`),
clearance = distance-to-nearest-BLOCKED on the same grid. The wedge table's entire reason to exist
(ray says clear, hull can't enter) evaporates because walkability *was* a hull fit. **Deletes**
`RayClearance`, `CheckEdge`, `InjectObstacles`, `WedgeState`, one class list, the footprint dup.
KISS win: ~250 LOC gone and the whole "plan says go, body refuses" bug class is impossible by
construction. Tradeoff: steering quantizes to 16u cells (the current sub-cell precision is a lie
anyway — the wedge table proves the point-ray already doesn't match the hull).

**2. [REWRITE] Invert the control flow — plan first, follow the path.** The greedy-primary /
A*-fallback split was the deliberate "never rewrite VFH" decision; azorae is the bill coming due.
Flip it: plan first (LOS fast-path = one straight hull-sweep for the common case; skip the grid if
clear), then a **dumb pure-pursuit follower**. A good plan has no pocket to escape, so the wedge
table, global-stall counter, cliff guard-loop, and regress-allowed logic are all dead code —
five mechanisms → one follower. Dynamic obstacles are handled by *re-plan*, not reactive dodge
(RouteAround already does this). Bonus: `BLOCKED` finally means "A* found no path," and
`RouteAround`'s nested-loop scaffolding collapses into the mainline (**[FALLS-OUT]**).

**3. [SEAM] Layered-Z / discovered-floorZ + step-edge `Passable` — wire the seam that was already
paid for.** Two sizes:
- *Minimal (KISS, ~40-60 LOC, no grid-key change):* re-anchor the probe window, add a two-segment
  step-down test (horizontal at the upper floor to the lip, then a vertical drop-clearance, gated on
  a landing-hull-fits probe — *not* a single lowered sweep, which false-startsolids in the riser),
  bump `kStepDownMax`. Handles stairs/ledges within the probe window; a sheer >128u pit correctly
  stays a portal problem.
- *Full:* probe each cell's floor relative to its *neighbor's* discovered floorZ (the frontier
  carries its own z), key cells by `(cx,cy,layer)`, and make `Passable` a real step-edge test
  (`|Δfloor| ≤ stepUp/stepDown`, else a typed fall/climb edge).
Either way the fix is only half-done unless the walker moves with it — **must** relax M1 (CheckEdge)
and M3 (Length2D progress) in lockstep (§4), and add a **z-aware arrival test** (today everything
is horizontal). This is the azorae unblock.

**4. [SEAM] Thread `BlockReason` into distinct result codes; retire overloaded `ADVANCED`.** The
signal is computed and discarded — threading one already-populated byte out is *less* work than the
current compute-print-to-debug-then-reconstruct-ambiguity. `SUCCESS / TIMEOUT (retry helps) /
NO_ROUTE (futile) / HEIGHT_SEVERED / GOAL_BLOCKED / NO_PLAYER / CANCELLED / BAD_MARK`, with
`ADVANCED` demoted to a boolean flag alongside an honest terminal reason. ~15 LOC; **ship even if 1-3
slip** — it converts azorae's 8× infinite retry into a one-shot "switch strategy" signal. (Touches
`macro_grammar.py` + the smoke gate.)

**5. [REWRITE-adjacent] One persistent world model per go_to.** Build the obstacle list + cell
cache once; hand the *same* warm planner to every replan (re-probes almost nothing). If obstacles
become grid cells (call 1), the per-batch 8192-scan vanishes entirely — obstacles are cells,
invalidated only when a prop's origin actually moves. Simplest correct version: persist within one
go_to (survives all replans), skip cross-call caching. Captures 3-rebuilds→1 and ~100-scans→0 with
zero staleness risk.

**6. [SEAM] Point-contents hazard in the one floor probe.** After finding floorZ, one
`CONTENTS_SLIME/LAVA` read → cell HAZARD (block for v0, or a large A* g-penalty via the clearance-
cost seam the doc already specced, astar_routing_design.md:98-121). ~5 LOC *because* the geometry
model was unified first — versus patching the same blind spot in three probe sites today.
Distinguish wade-able water from slime/lava.

**7. [FALLS-OUT] LOS string-pull the jagged 16u path inside `Plan`.** Greedily drop any waypoint
whose skip is hull-sweep-clear, reusing the `Passable` sweep already in the file — 20 hops → 2-4
clean legs. The follower rides straight lines; fewer legs → fewer replans → fewer re-entered
pockets. (This is the unbuilt P2.7.)

**8. [REVISIT] Decouple the march camera from the goal bearing.** `camera==goal` is an artifact of
the body-frame strafe basis (:999-1004), not a locomotion requirement, and it taxes every follow-up
verb. Drive movement in world-frame off the chosen heading and leave/restore the entry view.
**Gotcha the critic caught:** the view slew *also* clears the framebulk for a boxed hold (:997-998)
— any decouple must re-add an explicit `ClearFramebulk()` on the no-pick branch or a boxed batch
keeps the previous strafe.

**9. [SEAM, later] Portal (and fling/funnel) as a first-class graph EDGE.** The grid already has an
edge abstraction (8 neighbors through `Passable`); a portal is one more bidirectional edge
`frontCell(blue)↔frontCell(orange)`. The engine does the teleport; A* only needs to *know* the
components are connected. Prereq: layered-Z for cross-height islands. One mechanism (graph edge)
generalizes to flings/funnels later instead of a bespoke traversal mode per element.

**10. [NON-BUILD] Preprocessed static nav is the wrong lever — resist it.** The docs already
adjudicated (offline_map_preprocessing.md §2a, astar_routing_design.md §7): runtime traces have
ground-truth, dynamics-aware geometry (they see the dynamic cube-on-button pocket a static grid
cannot); a static nav grid adds a permanent offline↔runtime desync and is blind to the exact
obstacle the planner exists for. The "results-now vs long-term-reuse" answer: **geometry stays
runtime** (reuse via the warm cache, call 5); the offline budget goes to the **I/O causal graph +
affordance prior** (button→door wiring, portalable surfaces) — the non-spatial knowledge runtime
tracing structurally can't recover.

**11-12. [FALLS-OUT] Unify the 4 floor-fit primitives** into one `FloorFit(x,y,refZ,hull,window)`
(the drifting 128/64 windows *are* the G3 bug), and **fold the triplicated obstacle-class list +
footprint math** into one shared helper (the byte-identical duplication is a silent-desync footgun).
Both largely subsumed by calls 1-3 — sequence them as the cleanup that falls out.

---

## 9. The shortest path if we do almost nothing

If the rewrite (1-2) is too big to swallow at once: **call 4 (distinct codes, ~15 LOC) + call 3-
minimal (step-height patch, ~40-60 LOC, relaxing CheckEdge/Length2D in lockstep)** converts the
whole *class* of multi-level chambers from "conflated locomotion+reasoning failure" back into clean
reasoning signal — which is the entire point of the benchmark. That is the azorae fix. Everything
else is efficiency and taste that the same chamber also wants, but 3+4 are what unblock it.
