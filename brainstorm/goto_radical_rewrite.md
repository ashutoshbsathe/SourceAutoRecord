<!-- Radical rewrite design + implementation plan for go_to locomotion. Written 2026-07-05. Sequel to
goto_planner_deep_dive.md (the "why the current stack is broken" half — read that first). Design
settled with the user across a judge-panel + several review passes; §9 logs every decision. Forcing
function: azorae_stride_postmortem.md (single-Z go_to drowned ~24% of a run's budget). STATUS: the
floor primitive was RESOLVED by recon (nav_floor_primitive_recon.md) and the flood layer F1-F4 is
SHIPPED through a0424d56 — flood cells + cluster graph replaced the BSP-face surface graph (deleted).
Read §0 for the resume point; §1-§9 below predate the flood and read "Surface" where the shipped
code says "CellCluster". -->

# Radical rewrite: `NavSkeleton` — a BSP-backed multi-Z surface-graph go_to

## 0. TOP OF MIND — 2026-07-08: P5 follower + THE SWAP BUILT (in-game verify pending), old stack DELETED

The mid-P2 park resolved in one day. The recon
([nav_floor_primitive_recon.md](nav_floor_primitive_recon.md)) replaced the BSP-face
primitive outright: **walkability = a seeded runtime hull-trace flood** (down-ray +
standing-hull fit per 32u cell, swept-hull links, dual climb cap 18u flat / 34u slope,
one-way DROP ≤128u, goo rejected); BSP floor faces are only the seed list. Shipped and
user-verified in-game (screenshot `noteworthy_trajectories/Screenshot_20260706_224555.png`):

- **F1** `NavSkeleton::Flood` (`5167a7ba`) — critical pre-commit review catch: a hull fit
  at ray-z+2 startsolids on any slope >~7° (a box rests on its uphill corner); the slope
  support-offset lift fixed it and retroactively explains why `CanStand` never saw stairs.
- **F2** cell-carpet lens (`f5e6bae3`) — `sar_harness_nav_draw_cells`, radius default 1024,
  ≤0 = whole map; toggling `sar_harness_nav_draw` re-floods at live entity state.
- **F3+F4** (`a0424d56`) — cluster graph (union-find flat levels + small-cluster connector
  runs, typed directed edges, `nav_dump` prints it) and **wholesale deletion** of the old
  system (`Surface`/`CanStand`/`BuildEdges`/old visualizer). Terminology map for the rest
  of this doc: Surface → `CellCluster`, surface edges → cluster edges; the two-level plan
  (global A* / local routing) now means global-over-clusters / local-over-cells (`nbr[4]`
  is populated for exactly this).

**Resume here — in order:**
1. **Nit sweep — DONE 2026-07-07**: narrow-axis-span chain-merge (`kRunSpan` = 2 cells;
   wide treads merge, ≤64u catwalks now count as runs — accepted); NavVisualize latches
   only on `Ready()` and `Build` skips the .bsp parse without a player; dead includes /
   WALK arm removed; `nbr` overwrite invariant asserted (live assert, no NDEBUG);
   `FloorSurface` trimmed to z/mins/maxs (normal/corners had zero readers).
2. **P3 gates — SHIPPED 2026-07-07 (verified in-game on azorae stride: retracting
   stairs46 removed the connector cluster + its gated edges, ramp folded into walkable
   floor; the stairs-only upper platform correctly dropped out as unreachable), REFRAMED**: the
   flood traces movers at live pose, so live-pose gating happens for free (retracted stairs
   = no connector cells = no edge) and a Gate is **annotation, not a plan-time filter**:
   `{mover targetname, z at flood time, controlling button}`. Cells resting on a named
   brush entity (`m_ModelName` starts `*`; named movable props excluded) carry the gate;
   a cluster edge is gated iff either crossing cell is. Freshness = **flood-per-plan**
   (`go_to` rebuilds per call, 20–180 ms; cached-graph + staleness check deferred).
   Buttons via `EnumerateIoLinks` (**1-hop** entText connections parse, `srcClass`
   contains "button") — PeTI stairs' button→relay→door chain stays unresolved in v0
   (transitive inference is the `py/bsp_recon` sidecar's job; recon facts on
   `m_toggle_state`/relays above remain valid for whoever needs to *drive* the stairs).
   Post-verify notes: retracted step slabs are named func_brushes, so floor cells over
   them pick up silent intra-cluster gates (harmless, arguably true); the visualizer
   refreshes on map change / draw-toggle only — a moverZ-drift auto-refresh (~15 LOC)
   is an accepted-if-wanted nicety.
3. **P4 planner — SHIPPED 2026-07-08 (verified in-game: stair descent SUCCESS, elevator
   tower REACHED_PROJECTION + reason, ghost ribbon correct, re-flood tracks retracted
   stairs), FLAT**: user
   approved dropping the two-level cluster A* — routing is one single-source Dijkstra
   over flood cells (walk symmetric, drops directed +64 cost; clusters stay the
   legibility/annotation layer). `Plan(start, target) const`: start = feet's own column
   first, neighbor columns 3D-scored and capped at step height above the feet (review
   catch: |dz|-only scoring resolved starts to arbitrary corner columns / unmountable
   ledges); two-tier goal = cheapest reachable cell in the actionable envelope
   (kReachXy 64 / up 88 / down 16) → SUCCESS, else nearest reachable = projection →
   REACHED_PROJECTION where every projection now carries a reason (SEVERED /
   ABOVE_REACH / new BELOW_REACH / NO_FLOOR-laterally; review catch: NONE read as clean
   arrival). Steps emit at heading/type changes. `sar_harness_nav_plan <x y z | mark>`
   prints the plan + hands a ghost path to the nav draw (edge-type colors, white cross
   at standPos, amber residual stub both signs). Review: 4 confirmed findings fixed, 2
   refuted; integration lens incomplete (session limit) — its risk spots hand-checked.
   Prior caveat stands: edge `via` is ADVISORY — cell routing is authoritative.
4. **P5 follower + THE SWAP — BUILT 2026-07-08 (compiles + links; IN-GAME VERIFY PENDING),
   both sign-off calls resolved:** (a) blessed the breakage window (P6 grammar sync folded
   in this session, below); (b) **held-entity flood skip PUNTED** — the flood still skips
   only the player, so a carried cube can false-NO_ROUTE a carry (accepted detour, 1-line
   fallback if it bites). Built on `yeeh`, unpushed, **not committed** (awaiting the in-game
   eyeball). Net −282 LOC in MacroExecutor + 570 LOC of GoToPlanner gone.
   - **P5.1** `FollowTo(context, dest, reachRadius, tickBudget) → FollowOutcome` in
     MacroExecutor.cpp anon-ns: Build+Plan the flood → straight-line waypoint follow
     (`kFollowAdvance` 24u, NO VFH), view-then-move per 4-tick batch (ApplyAbsoluteView
     clears the framebulk → drive AFTER), no-progress detector (`kFollowStallEps`/
     `kFollowStallBatches`) → re-Build+re-Plan ×`kMaxReplans` → STUCK, 3D arrival vs
     `plan.standPos`. Codes SUCCESS / REACHED_PROJECTION / NO_ROUTE / STUCK (+ NO_PLAYER /
     CANCELLED). **As-built: FollowTo OWNS the stop** (unconditional clear + velocity-zero
     + settle) so all 4 callers dropped theirs.
   - **P5.2** interpose / pass_through (keeps its ≤`kMouthReach` tolerance) / drop_into
     swapped MarchTo+RouteAround → FollowTo.
   - **P5.3 DELETE:** GoToPlanner.{hpp,cpp} whole, MarchTo/RouteAround/ChooseVfhHeading/
     InjectObstacles/RayClearance/VfhBin/VfhPick + all VFH/wedge constants,
     `sar_harness_goto_plan` (replaced by `sar_harness_nav_plan`). **As-built KEEP list
     revised:** `TargetFootprint`+`IsGoToObstacleClass`+`kApproachGap` also DELETED (the
     grabbable-standoff inflation that used them is obsolete — see review #2 below), so
     **both** obstacle-class lists are now gone as the recon wanted. Kept: `CheckEdge`
     (Move), `InterposeGate` (comment reworded), `kGoTo*`/`kStuckEps`/`kProbe*`/`kStepDown*`;
     `sar_harness_goto_debug` repurposed for the follower.
   - **Adversarial review (4-lens workflow + per-finding verify): 5 confirmed, 0 refuted,
     all FIXED.** #1/#3 (terminal stop): the initial `drove`-gated stop skipped velocity-zero
     on immediate arrival (coast-into-target after pass_through) and framebulk-clear on
     mid-follow NO_PLAYER → made the stop **unconditional**, NO_PLAYER breaks through it,
     `finalFeet` defaults to start feet (honest moved=0). #2 (MEDIUM): the grabbable-standoff
     reachRadius was center-calibrated but FollowTo arrives at `standPos` (offset beside the
     solid) → double standoff → follow-up pick_up OUT_OF_REACH → **deleted the inflation**
     (flood provides the standoff for free) and go_to now passes `kGoToReach`=32 (tighter
     than 48) so a grab lands in range. #4/#5 (P6 sync, folded in per sign-off (a)):
     `agentloop_smoke` go_to whitelist + `macro_grammar` CAVEAT/doc + `gemini_agent` notes
     updated to the new codes (BLOCKED/ADVANCED retired).
   - **Second review round (focused on the fix delta): 2 more confirmed, both FIXED.**
     **⭐ P4 goal-selection bug (load-bearing for ALL callers):** `NavSkeleton::Plan`'s SUCCESS
     branch picked the *cheapest-to-reach* envelope cell (min Dijkstra cost) while the
     projection branch picked *closest-to-target* — so a SUCCESS `standPos` parked at the
     envelope's **near boundary (~kReachXy=64u short of the target)**, not beside it. That
     defeated `kGoToReach`=32 (feet ended ~96u from a cube center → pick_up straddled
     `kGrabRange`=96) AND would have stopped interpose/pass_through/drop_into ~64u short of
     their seat/mouth-front. **Fix: SUCCESS now also picks the closest-to-target reachable
     cell** (consistent with the projection branch) — a walk ENDS beside its target. The
     second fix: `moved_dist` was `|startFeet|` (not 0) on a pre-plan NO_PLAYER → guarded.
     ⚠️ **The Plan change touches P4** (verified for stair-descent/tower, but NOT for
     stop-close-to-a-solid) — re-verify `sar_harness_nav_plan` puts the goal cross beside the
     target, not ~64u short.
   - **Behavior note (NOT a code shift):** `go_to <cube/button>` still returns **SUCCESS**
     (reached=true) — the flood can't stand *on* the solid, but a side cell within the 64u
     `kReachXy` envelope is reachable, so `Plan` picks it as the SUCCESS goal (standPos beside
     the solid, ~one lattice step off center). What changed is only the stop *distance* (now
     standPos-based, not center-based) — hence the `kGoToReach`=32 fix so a follow-up grab
     lands. **REACHED_PROJECTION fires only for genuinely unreachable targets** (no reachable
     cell in the envelope: a high ledge, a severed gap, over goo). So `interact`→button (gates
     on `reached`) and `go_to`→cube→`pick_up` both keep working. `interact` still calls
     MacroExecutor::GoTo (signature unchanged).

   **NEXT:** in-game verify (azorae stride + a cube-grab chamber) → one `agentloop_smoke`
   run (confirms the P6 sync) → commit. Then the **laser-verb gravity fix** (interpose/redirect
   teleport-snaps must leave the cube under live gravity — the vphysics-sleep freeze lets a
   mid-air cube reach any point; POWERED-while-frozen is fake, see
   [azorae_stride_postmortem.md](azorae_stride_postmortem.md)), **P6** py grammar +
   `agentloop_smoke`, then the azorae stride end-to-end acceptance rerun.

## 1. Why a radical rewrite is justified NOW (and wasn't before)

The deep dive proved the shipped stack is single-plane, over-determined (~5 independent flat gates),
and duplicative. Two facts turn *patch* into *rewrite*:

**(a) We parse the live map's `.bsp` in-engine now.** `BspFilePanelSource.cpp` already loads
PLANES / VERTEXES / EDGES / SURFEDGES / FACES / TEXINFO / TEXDATA / MODELS / ENTITIES(text) at map
load, zero deps. `ExtractFace` (BspFilePanelSource.cpp:178) yields full winding + normal + material +
flags for **any** face — it's normal-agnostic; `IsPortalable` just filters it to white tile.
`ExtractDynamicRests` (:270) poses **brush-entity** faces by the **live** entity transform, and
`PanelSession.cpp:51-102` is the working poser (`abs_origin + R(abs_angles)·corner`). So multi-Z
walkable surfaces, stairs/ledges, **moving brush floors**, and — from the same already-loaded ent
text — the button→door **causal graph** are all reachable from machinery that already runs at
`ON_EVENT(SESSION_START)`.

**(b) The old "park offline geometry" verdict does not bind this design.**
[offline_map_preprocessing.md:169](offline_map_preprocessing.md) parked a static nav grid because
*"A\* already has ground-truth, dynamics-aware geometry for free; a static grid can't see the dynamic
cube-pocket."* That assumed the runtime planner sees **all** geometry for free — but the deep dive
proved it can't: it is single-plane, blind to any floor outside `[refZ−128, refZ+40]`. So **multi-Z
connectivity is not "dominated by runtime" — runtime cannot recover it, making BSP the only source.**
And the objection's actual worry (the dynamic cube) is answered by keeping **movable props runtime**
and brush floors posed from **live** transforms. We build a runtime-parsed multi-Z **skeleton** (what
runtime is blind to) with a **runtime dynamic overlay** (what BSP is blind to). The objection
dissolves along that seam.

## 2. Architecture: `NavSkeleton` — a two-level surface graph

A brand-new class `NavSkeleton` (**not** a mutation of `GoToPlanner` — that dies, §4). No global voxel
grid. The planner is **two levels** (the hybrid you called for):

- **GLOBAL — surface-to-surface.** Nodes = walkable **surfaces** (BSP floor faces clustered into one
  node per contiguous walkable plane — a whole ledge is one node). Edges = typed transitions between
  surfaces (WALK / STEP_UP / STEP_DOWN / DROP, plus conditional PORTAL/mover edges). A* over surfaces
  answers *"which surfaces do I cross, and through which transition points."* Tens of nodes on a PeTI
  chamber — human-inspectable, which is exactly why it pairs with the ghost-path verify loop.
- **LOCAL — within-surface.** Given the entry/exit points on each surface in the global path, a local
  planner routes *across* that surface **avoiding movable props on it** (cubes, buttons, turrets). This
  is where "dodging" lives — plan-time, over a local grid/funnel bounded to the one surface polygon,
  **never reactive.** (This is the same thing as the follower-dodge question: dodge = the local
  planner, not the follower.)

This split *is* the floorZ-provenance hybrid: the global graph's topology + nominal Z come from BSP;
the local layer and the props on it are runtime. How much runtime floor-tracing the arrival Z needs is
settled empirically by the visualizer eyeball (§3, §6-P2).

Four moving parts, each killing a shipped sin:

| part | what it is | kills |
|---|---|---|
| **Static surface graph** | built once at `SESSION_START` from BSP floor faces + banded edges | single-plane `refZ`; per-call rebuild |
| **Two-level A\*** | global surface A* + local within-surface route; returns one rich `PlanResult` | two disagreeing traced models; per-batch 8192 rescan |
| **Dynamic overlay** | brush-floor surfaces posed live + conditional (pose-gated) edges + movable-prop stamp | frozen-snapshot staleness |
| **Dumb z-aware follower** | walks the `PlanResult` polyline; no avoidance (all in A\*) | VFH `MarchTo` + wedge/stall/cliff; ~200 hops/march |

## 3. The design in detail

### Data model (fresh, in `NavSkeleton.hpp`)
```cpp
struct Surface {                 // one contiguous walkable plane = one node
  uint32_t id;
  Vector normal;                 // ~+Z (floor); banked ramps allowed (normal.z > 0.7)
  float   z;                     // nominal stand height (plane dist projected)
  Vector  mins, maxs;            // AABB; polygon kept for local routing + point-in-surface
  std::vector<Vector> poly;      // outer winding (world space)
  uint32_t dynEnt;               // 0 = static world; else the brush entity posing this surface
};
enum EdgeType : uint8_t { WALK, STEP_UP, STEP_DOWN, DROP, PORTAL, FLING };
struct Edge {
  uint32_t from, to;
  uint8_t  type;
  Vector   via;                  // transition point (shared lip / mouth / mover contact)
  uint32_t gate;                 // 0 = always-open; else a Gate id (mover pose / button)
};
struct Gate { uint32_t ctrlEnt; float enableZ; /* live-pose predicate */ uint32_t button; };
```
`PlanResult` is the **one struct both the follower and the visualizer consume** — the load-bearing
invariant (the moment they need different data, the two-models sin is regrowing):
```cpp
struct PlanStep { Vector pos; float floorZ; uint8_t edgeType; uint32_t surface; };
struct PlanResult {
  std::vector<PlanStep> steps;   // global+local, goal last
  bool     reached;              // reached an actionable stand point (3D)
  Vector   standPos;             // where the body ends up (== goal, or the projection)
  Vector   target;               // the requested point
  float    residualDz, residualDxy;  // 0 on SUCCESS; the gap on REACHED_PROJECTION
  uint8_t  code;                 // SUCCESS/REACHED_PROJECTION/NO_ROUTE/STUCK/...
  uint8_t  blockReason;          // NO_FLOOR/IN_WALL/SEVERED/ABOVE_REACH...
};
```
**Structural guard:** the follower is handed only `PlanResult` + the framebulk API — no `NavSkeleton&`,
no entity-list handle. If it can't reach the world, it can't grow a second geometry model.

### Build pipeline — load / runtime
- **At load** (`ON_EVENT(SESSION_START)`, main thread, once, beside `EnumerateDynamicRests` at
  PanelSession.cpp:33): `IsFloor` floor-face enumerate (= `ClusterPanels`'s loop with `IsPortalable →
  IsFloor`: `normal.z > 0.7` after the winding cross-product re-rectify at :283-287; drop
  `SURF_NODRAW/SKIP/TRIGGER/SKY`; keep the `(0,0,0)` origin-junk filter) → cluster contiguous floor
  faces into `Surface`s. Band 4-neighbour surface adjacency by `|Δz|` → WALK/STEP/DROP. Enumerate
  **brush-entity floor** rests (`ExtractDynamicRests` w/ `IsFloor`) as dynamic surfaces. Parse the ent
  I/O connections (tokenizer extension) → `Gate`s tagging conditional edges with their controlling
  button. Nothing lazy — the static graph is immutable for the map's life (the lazy-discard was a sin).
- **Runtime, per-plan (not per tick-batch):** re-pose dynamic surfaces from live transforms; evaluate
  each `Gate` against the mover's live pose to include/exclude its edge; stamp movable-prop footprints
  onto the local grid of the surface they sit on (one entityList scan).

### Dynamic geometry — brush movers as first-class, conditional edges (your #1 + #3)
The poser is **class-agnostic**: any *named* brush entity with floor faces becomes a dynamic
`Surface`, posed by its live `abs_origin + R(angles)`. So PeTI stairs / flip-panels / piston platforms
/ lifts (`func_movelinear`, `func_door(_rotating)`, `func_brush`, `func_platform` — recon P3 confirms
which carry floors, but we don't special-case classes) are all handled by posing whatever named brush
has a floor. Their edges are **conditional**, gated on the mover's **live pose**:
- **v0 = live-pose gating.** An edge is in the graph iff its mover is currently in the enabling pose
  (stair extended, lift at this level). "Take the appropriate paths" = A* only sees currently-open
  edges. No BFS needed for *traversal*.
- **Causal parse = annotation.** The I/O parse tells us *which button controls which edge* (button →
  OnPressed → mover). v0 stores this to (a) tell the agent "to open this edge, press button N" and
  (b) enable future prerequisite planning ("press first, then cross"). Deferred: the transitive
  button→relay→counter BFS (reuse `py/bsp_recon` sidecar; absent → all-gates-open). **Do NOT
  re-implement the BFS in C++.**

### Goal resolution — the stand point, always make progress (your projection rule)
`go_to <mark>` targets are often not stand-*on* places (wall portal, wall button, cube on a ledge).
`Plan` resolves in two tiers and **never refuses to move**:
1. *Actionable stand point* — a walkable surface within the target's actionable envelope (verb-specific
   height band + horizontal reach), searched in 3D. Reachable → route → 3D arrival → `SUCCESS`.
2. *Closest reachable projection* — if no actionable stand point is reachable, walk to the reachable
   surface **closest to the target** (its projection onto the reachable set: the wall base under the
   portal, or the stair top if that's as high as the graph reaches) and stop there with
   `REACHED_PROJECTION` + the residual `dz/dxy`. Maximal progress; the ceiling rides on a real arrival.

### Terminal codes (honest, distinct — ADVANCED is gone)
- `SUCCESS` — reached an actionable stand point, **3D** arrival (XY within ε **and** `|Δz|` in band).
  Kills the false-SUCCESS where the body stands at a wall base 100u below an elevated target.
- `REACHED_PROJECTION` (`ok=true, reached=false`) — no actionable stand point reachable; walked to the
  closest reachable surface, detail carries `standPos` + residual `dz/dxy`. Agent signal: *"you're at
  the projection; nothing taller/nearer is reachable on foot — get elevated (stairs / fling / floor
  portal)."* The azorae signal, attached to a body that moved.
- `NO_ROUTE` + `blockReason` — even the closest projection is severed from the player.
- `STUCK` — diverged and couldn't recover after N replans (loop-breaker below).
- `NO_PLAYER` / `CANCELLED` / `BAD_MARK` — unchanged.

### Follower contract — strict-zero + loop-breaker
A z-aware pure-pursuit follower consuming `PlanResult`: per batch, **one** `RunOnMainThreadSync`
(vs ~200/march) reads feet + sets framebulk toward the lookahead step; advance cursor on **3D**
arrival (XY within ε **and** `|feetZ − step.floorZ|` in band — kills the Length2D arrival that
conflated a walkway with the pit beneath it); WALK/STEP → walk (engine auto-steps ≤18u), DROP → walk
off, never refuse on `|Δfloor|`; camera pitch **free**. Dodging is the planner's job (already routed
around); pockets are impossible (A* is complete). Re-plan only on divergence (feet stray > R, or a
gate flips) — a µs A* re-route beats a myopic twitch. **Loop-breaker:** on divergence zero
`m_vecVelocity` then re-plan; N replans from ≈the same spot with ≈the same failing path → `STUCK`.
**Never grows reactive dodge** — the instant it does, that's VFH rebuilt.

### floorZ provenance — hybrid, settled by the eyeball
Global surface Z from BSP; local arrival Z possibly from a runtime down-trace. We do **not** decide
the split up front — P0–P2 (enumerate + visualize + a `BSP-Z vs down-trace` delta probe at the feet)
decides it on real maps before the follower ships. Tight delta → pure-BSP; noisy → the follower's
arrival band re-sources from a per-waypoint down-trace (topology BSP, Z runtime).

## 4. Code structure — the `verbs/` refactor, and what dies (your ruthless-efficiency call)

**No legacy carried. Backwards-compat be damned (in code).** Concretely:

- **New:** `src/Features/Harness/NavSkeleton.{hpp,cpp}` (the graph + two-level A* + goal resolution).
  `src/Features/Harness/verbs/` (new directory). `src/Features/Harness/verbs/GoTo.cpp` (the go_to verb
  impl: resolve → `NavSkeleton::Plan` → follower). `src/Features/Harness/NavVisualize.cpp` (the
  ghost-path Feature, §5).
- **MacroExecutor.cpp becomes dispatch-only.** It keeps the verb switch and the shared execution
  primitives (`RunOnMainThreadSync`, `AdvanceTicksBlocking`, `ApplyAbsoluteView`, framebulk helpers)
  exposed via a small `verbs/VerbContext.hpp`; each verb is a free function in `verbs/` taking that
  context. The switch calls `verbs::GoTo(ctx, req)`.
- **DELETED at the swap (§6-P5):** `GoToPlanner.{hpp,cpp}` entirely; and from MacroExecutor.cpp:
  `MarchTo`, `RouteAround`, `ChooseVfhHeading`, `InjectObstacles`, `RayClearance`, `VfhBin`,
  `CheckEdge`, `VfhPick`, `WedgeState`, `TargetFootprint`, all `kVfh*`/`kWedge*`/`kGoTo*` constants,
  the duplicate obstacle-class lists, and `Move()`'s copy-pasted stop-tail.
- **TODO (marked, not now):** once go_to is robust, migrate **every** verb (`pick_up`, `release`,
  `interact`, `place_portal`, `interpose`, `redirect_to`, `pass_through`, `drop_into`, `aim_at`,
  `look`, `wait`, …) into `verbs/`, one file each, individually inspectable; MacroExecutor.cpp ends as
  pure dispatch. **This turn: only go_to moves. Leave the rest in place, but add the tracking TODO.**

## 5. The ghost-path visualizer (`NavVisualize.cpp`)

Cvar-gated in-world draw of a **read-only dry-run** plan — zero player movement — reusing the **same**
`PlanResult` the real go_to walks (no third geometry model). The human-verify loop the rewrite is
gated on (and a candidate model percept later). Modeled 1:1 on PuzzleAnnotate (a cvar + `CON_COMMAND`
+ `ON_EVENT(RENDER)`, no `Feature` subclass).

- **Two commands.** `sar_harness_nav_draw` — draw the whole graph (surfaces as translucent quads at
  their Z, edges as colored lines by type). This is the **floorZ-bet instrument** (P2). And
  `sar_harness_visualize_path <mark>|<x y z>` — dry-run `Plan`, draw the ribbon.
- **Draw recipe (immediate-mode, per frame; `PlayerTrace.cpp:270-274` idiom):** one
  `OverlayRender::createMesh` per edge type, colored via the wireframe `RenderCallback::constant`;
  per-segment `addLine` at `floorZ+1.5` (the PuzzleAnnotate floor-nudge, :285). **Legend:** walk=green,
  step-up=cyan, step-down=amber, drop=orange, portal=magenta(no-depth), hazard=red, blocked-tail=grey.
  `REACHED_PROJECTION` draws the walk in green + an amber no-depth stub from the projection **up/across
  to the target** with a `+Δz unreachable` label. `addText` START/GOAL/BLOCKED clamp labels
  (:249-253). Default depth-tested (occlusion = the ground-truth signal); `sar_harness_nav_through_walls
  1` flips to no-depth.
- **Ownership:** a file-static `g_ghost`/`g_graphDraw`, written only by the command, read only by
  RENDER, both main-thread → no lock. Not auto-replanned per frame (that's the per-batch rescan
  sneaking back); stale is fine for a dry-run tool.

## 6. Multi-phase implementation plan (many small, hand-verifiable; C++ before Python)

Each phase is independently buildable and eyeball-verifiable — mostly via a dump or the visualizer,
**without moving the body** — until the follower/swap. go_to keeps its OLD behavior until P5; the
legacy is deleted atomically at the swap, not carried. `./format.sh` (Harness only) + `make` after
each. Do not launch the game from the agent terminal — describe each verify command for the user.

**Phase 0 — scaffold (structural, zero behavior change)**
- **P0.1** Create `verbs/` + `verbs/VerbContext.hpp`; move the `go_to` dispatch into `verbs/GoTo.cpp`
  as a thin forwarder to the *existing* logic. Add the `// TODO(verbs): migrate all verbs here` marker.
  *Verify:* every existing go_to behaves identically (the regression net).
- **P0.2** Create `NavSkeleton.{hpp,cpp}` with the data model (`Surface`/`Edge`/`Gate`/`PlanResult`),
  empty methods. *Verify:* compiles + links.

**Phase 1 — static surface graph from BSP**
- **P1.1** `IsFloor` + floor-face enumerator + cluster into `Surface`s. `sar_harness_nav_dump` prints
  surface count + each surface's Z/AABB. **Add the `BSP-Z vs down-trace` delta probe at the player's
  feet** (the floorZ-bet data). *Verify:* flat map → one surface at floor Z; a two-level map → two.
- **P1.2** Surface adjacency + banded static edges (WALK/STEP_UP/STEP_DOWN/DROP by `|Δz|`); dump prints
  edges + types. *Verify:* staircase → STEP chain; ledge → DROP; gap → no edge.

**Phase 2 — visualize the graph (SETTLE THE floorZ BET)**
- **P2.1** `NavVisualize.cpp`: `sar_harness_nav_draw` draws surfaces (translucent quads @ BSP Z) +
  edges (colored lines). *Verify on 3–4 real maps (map_candidates.txt):* surfaces sit on the real
  floor; edges match visible stairs/ledges. **← DECIDE pure-BSP vs hybrid-Z here, from the eyeball +
  the P1.1 delta probe.** Body never moves.

**Phase 3 — dynamic brush surfaces + conditional edges**
- **P3.1** Enumerate brush-entity floor surfaces (`ExtractDynamicRests` w/ `IsFloor`), pose live per
  plan. *Verify:* `nav_draw` shows a func_movelinear stair/lift surface at its live position; move it →
  the surface follows.
- **P3.2** Parse ent I/O connections → `Gate`s; live-pose gating (edge in-graph iff mover enabling).
  Store button→edge mapping (annotation). *Verify:* a lift edge shows in `nav_draw` only when the lift
  is at level; press the controlling button (manually) → the edge appears.

**Phase 4 — two-level A\* + PlanResult + path visualize**
- **P4.1** Global surface A* + goal resolution (2-tier stand point) → `PlanResult` (+ `REACHED_PROJECTION`).
  Read-only, main-thread. *Verify via viz (P4.3):* a route across a step-down the old planner refused;
  an elevated portal → `REACHED_PROJECTION` + amber up-stub.
- **P4.2** Local within-surface traversal: a bounded local grid/funnel over each surface polygon,
  avoiding movable-prop footprints on it. *Verify:* drop a cube on a surface → the path bends around it
  within that surface.
- **P4.3** `sar_harness_visualize_path <mark>` — dry-run ghost ribbon + labels. *Verify:* the
  human-in-the-loop preview reads correctly across the candidate maps.

**Phase 5 — the follower + THE SWAP (delete legacy)**
- **P5.1** z-aware pure-pursuit follower (framebulk only; strict-zero + velocity-kill + `STUCK`).
  *Verify:* a short march on a flat map lands cleanly.
- **P5.2** **THE SWAP** — `verbs/GoTo.cpp` becomes resolve → `NavSkeleton::Plan` → follower.
  **DELETE** `GoToPlanner.{hpp,cpp}` + all the MarchTo/VFH/RouteAround machinery + constants + dup
  lists from MacroExecutor.cpp (§4). *Verify:* go_to across a ledge walks it; go_to to an elevated
  portal → `REACHED_PROJECTION`; flat go_to still works; `agentloop_smoke` green.

**Phase 6 — Python grammar + smoke sync (last)**
- **P6.1** `macro_grammar.py` go_to doc rewrite (multi-Z; `SUCCESS`/`REACHED_PROJECTION`/`NO_ROUTE`/
  `STUCK`); `agentloop_smoke.py` assertions for the new codes. *Verify:* smoke passes.

**Deferred (post-v0, marked):** movable-prop overlay refinements; PORTAL/FLING affordance edges (the
portal/launch verbs insert them); transitive button→relay prerequisite inference via the `py/bsp_recon`
sidecar; **migrate all other verbs into `verbs/`**; `LUMP_DISPINFO` (displacement floors) if the eval
corpus needs it (grep for `dispinfo` first).

## 7. Risks + guards

1. **floorZ provenance** — the core bet; P0–P2 decide it before the follower ships (§3, §6-P2).
2. **Surface adjacency rebuild** — BSP hands no face adjacency; quantize-then-neighbour can drop two
   coplanar floors into non-adjacent surfaces at a thin lip → false severance (the old bug relocated).
   The `nav_draw` eyeball is the instrument that catches it.
3. **Two-models regrowth** — structural guard: the follower gets `PlanResult` + framebulk only.
4. **Per-batch rescan regrowth** — dynamic re-pose is per-plan + event-driven, never per-follow-batch.
5. **Reactive-dodge regrowth** — strict-zero + loop-breaker; dodge is the *local planner*, not the
   follower.
6. **Displacements** (`LUMP_DISPINFO` unparsed) — flat base quad where the real floor is a heightfield;
   rare in 128u PeTI brushwork. Grep the eval corpus for `dispinfo` before betting; a start-cell
   down-trace is the stopgap.

## 8. Resolved decisions log (frozen 2026-07-05)

1. **Node granularity → surface-graph ONLY.** No global voxel grid. `func_brush`-family movers (angled
   panels acting as stairs, lifts) dynamically mutate the graph as first-class dynamic surfaces.
2. **floorZ provenance → hybrid, settled by the visualizer.** Global surface-to-surface planner (BSP
   topology + nominal Z) + local within-surface traversal (runtime prop avoidance). P0–P2 decide how
   much runtime floor-tracing the arrival Z needs.
3. **Causal graph → live-pose gating in v0.** Read the brush-family movers' live pose to gate
   conditional edges (stairs open/close, lift at level). Parse the I/O to *annotate* which button
   controls which edge (agent hint + future prerequisite planning). Transitive BFS deferred to the
   `py/bsp_recon` sidecar — not re-implemented in C++.
4. **Follower dodge → strict-zero + loop-breaker.** Dodge = the local within-surface planner (same as
   #2), not the follower. Re-plan on divergence; `STUCK` after N failed replans. No reactive twitch.
5. **Grid resolution → n/a globally** (surface-graph has no global grid). The *local* within-surface
   planner uses a bounded grid whose resolution is a local detail (start at 16u, tune later).
6. **Class → fresh `NavSkeleton`.** `GoToPlanner` is deleted, not mutated.
7. **Ruthless refactor → `verbs/` directory.** go_to moves to `verbs/GoTo.cpp`; MacroExecutor.cpp
   becomes dispatch-only; all VFH/MarchTo/RouteAround/GoToPlanner legacy is DELETED at the swap. Once
   go_to is robust, **every** verb migrates to `verbs/` (tracked TODO). No backwards-compat in code.
8. **REACHED_PROJECTION** — go_to always makes maximal progress: walk to the closest reachable
   projection of an unreachable target and report the residual honestly; never refuse to move.
