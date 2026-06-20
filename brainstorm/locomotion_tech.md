# Locomotion tech — `go_to` pathfinding + reliable manipulation + (next) lasers

Design + phased plan for making locomotion/manipulation a **trustworthy primitive**, so a
failed macro never contaminates the reasoning-vs-locomotion measurement. Sits under ROADMAP
priority #2 ("locomotion tech"). Read after [ROADMAP.md](ROADMAP.md) and
[macro_executor_impl_plan.md](macro_executor_impl_plan.md) (the as-built executor).

> Motivating evidence: the first_light/second_light forensics. `go_to` straight-line march
> BLOCKED ×4 in first_light (never reached the exit), the agent's own blind march knocked the
> cube off the button (`on_button` True→False), and `release` only *transiently* seated cubes
> because it's open-loop. Both runs died on the "last mile," not on reasoning.

**Roadmap of this doc:** §1–§4 are the immediate work — make walking + placing reliable
(`go_to` pathfinding + closed-loop place-on-button). §5 is **lasers — the next big planning
frontier**, the first element family that turns locomotion into a *routing* problem and reuses the
same closed-loop micro-adjust spine.

---

## 1. Thesis

`go_to mark=N` is the "locomotion handed to the model for free" primitive. **If `go_to` fails on a
path that exists, we can't tell whether the model mis-reasoned or the actuator just couldn't walk
— that poisons the experiment.** So the bar is **completeness + reliability** first, optimality
(shortest path, fewer ticks, prettier trajectories) second. Manipulation (`release` onto a button,
later: aiming a redirection cube) must close the loop on ground-truth state, not act-and-pray.

Non-goals (explicit): `move` stays dumb open-loop (the manual override); raw BSP brush parsing;
Source NavMesh / `info_node` AI graph (neither is present in PeTI chambers). **Lasers are NOT a
non-goal** — they're the next planned frontier, fully specced in §5 (build deferred until the
locomotion core lands + a recon spike).

---

## 2. Substrate (recon results — don't re-derive)

- **Traces are point-rays today** — `engine->Trace` zeroes `Ray_t.m_Extents`
  ([Engine.cpp:281](../src/Modules/Engine.cpp#L281)). The infra supports **hull-swept** traces:
  set `m_Extents` to the player half-hull. Player hull via
  `server->GetPlayer(1)->collision().OBBMins()/OBBMaxs()` (main thread).
- `CGameTrace` reliably carries `fraction`, `plane.normal` (steer-along surface), and `m_pEnt`
  (classify blocker: movable cube vs world brush) — the harness only reads `fraction` today.
- **Movement is body-frame, decoupled from the view** ([MacroExecutor.cpp:175](../src/Features/Harness/MacroExecutor.cpp#L175),
  `TasPlayer::ApplyMoveAnalog`): set side/fwd analog to strafe **while the camera stays aimed at
  the target**. This is what makes "walk around it without losing the goal" trivial.
- **The held cube is NOT filtered from the wall guard** ([MacroExecutor.cpp:219-220](../src/Features/Harness/MacroExecutor.cpp#L219))
  → a carried cube can false-trigger `WALL`. Latent bug; fixed in P0.
- **The executor can read live status fields synchronously on the main thread** via
  `EntField::getServerOffset` (the snapshotter's reader): `prop_floor_button.m_bButtonState` (net),
  `prop_weighted_cube.m_bActivated` = on_button (dm), `point_laser_target.m_bPowered` (dm),
  `env_portal_laser.m_bLaserOn` (net), `m_nCubeType` (0=standard, 2=reflective). So closed-loop
  verbs verifying against ground truth are cheap, and the **laser success signal already flows**.
- **NavMesh / brush graph: absent.** The geometry oracle is the **hull trace**. "Read brushes → A*"
  collapses in practice to "probe with hull traces → occupancy grid → A*."

---

## 3. Architecture: global planner + local controller (they compose)

```
go_to(mark):
  1. straight hull-march                       ← fast path; most chamber go_tos are line-of-sight
  2. if blocked → A* over a LAZY occupancy grid → waypoint chain   ← "which corridor"
  3. execute each leg via hull-march + ray-fan steering            ← "don't clip the doorframe"
  4. Bug2 wall-follow fallback when the grid is stale/incomplete or
     a dynamic obstacle (closed door, a cube you moved) blocks a leg ← completeness net + dynamics
```

- **A\*** = which way around (global, static world). **Local controller** = don't catch the corner
  (per-leg, sees dynamic stuff). **Wall-follow** = completeness guarantee + dynamic-obstacle handler.
  None redundant.
- **Greedy is out as a standalone** — it survives only as the horizon-1 inner step of the march
  (neither complete nor optimal alone).
- **Lazy grid:** A* probes neighbour cells on demand and caches; only the explored frontier gets
  hull-traced — no big reset-time sweep, naturally bounded.
- **Static grid + dynamic-local split:** A* plans over static world geometry; the carried cube / an
  opening door are handled by the local layer (re-probed each step). Keeps the grid simple + correct.

### Locked design decisions
- Cell rep is **2.5D-ready** (store per-cell floor z + step-height edges) but connectivity is **2D
  for now** (v0 chambers ~flat). Cheap insurance against the M3 multi-level refactor.
- Phase split: **P1 (local controller) then P2 (A\*)** — P1 is the executor every A* waypoint needs
  anyway, ships complete locomotion on its own, and is independently testable.
- `release <button-mark>` is **upgraded** to closed-loop (no new `place_on` verb).
- The closed-loop micro-adjust controller keeps its **(predicate, perturbation) seam pluggable** so
  the laser verb (§5) drops in as "same controller, different predicate + nudge."
- **Local controller is a 360° VFH polar histogram** (see the 2026-06-19-later pivot note in §4), not
  the bearing-offset steer: clearance histogram (wall/door rays + analytic cube OBBs) → freest valley
  nearest goal, willing to move backward. It subsumes the line-65 "ray-fan steering" and the line-66
  Bug2 fallback into one mechanism; A* (P2) still sits above it as the global "which corridor" layer.

---

## 4. Phased plan — locomotion + manipulation (C++ before Python)

Each phase is verified by hand through `py/macro_repl.py` on a live game (the user runs the game,
not the agent). Result codes are plain strings in `MacroResult.result_code` (no proto change unless
noted).

> **Update 2026-06-19 — predictive → reactive pivot.** P0.1's *predictive* hull guard was built,
> tested in-game, and **rejected**: a body-width hull trace that hard-stops the march can't model
> Source's wall-*sliding*, so it false-`WALL`'d on open floor near a doorframe and needed
> width/band tuning. The replacement (now P1.1, **shipped**) is **reactive**: hold forward, let the
> engine slide the player, and read a true dead-end off lack of *real* closest-approach progress —
> no hull, nothing map-tuned. `TraceHull` (Engine) was reverted with it and is **deferred to P2**
> (it's the right tool for A\* grid cell-passability, not a march-stopper). The held-entity trace
> filter (P0.2) went with it — the reactive march has no forward trace for a carried cube to fool.

> **Update 2026-06-19 (later) — reactive → VFH-360 pivot.** The shipped reactive march (P1.1) has
> two field-reported failures. (1) **It shoves cubes**: Phase B's only guard is the down-ray pit
> check ([MacroExecutor.cpp:500](../src/Features/Harness/MacroExecutor.cpp#L500)), so it holds
> forward into a `prop_weighted_cube` and the physics solver resolves player-vs-*movable* by pushing
> the cube; the shove still closes the 2D feet→target distance
> ([:464](../src/Features/Harness/MacroExecutor.cpp#L464)) so `stuckBatches` never fires. (2) **It
> gives up on side doors**: the steer machine instant-re-homes on any 4u gain
> ([:467](../src/Features/Harness/MacroExecutor.cpp#L467)→`homeYaw`
> [:488](../src/Features/Harness/MacroExecutor.cpp#L488)) with bearing-relative offsets
> ([:46](../src/Features/Harness/MacroExecutor.cpp#L46)), so start-near-wall + far-side-door
> oscillates against the wall and the 6-offset cycle exhausts → `BLOCKED`.
>
> **Decision — the new local controller is a 360° VFH polar histogram, and `go_to` may move
> backward/sideways** (not just a forward cone) when that improves the approach:
> - **Histogram** (per batch, K≈24 bins around the full circle): wall/door clearance from one ray
>   per bin; **cube obstacles injected analytically** from their known OBBs (the snapshotter already
>   enumerates grabbable props — no trace), skipping the `go_to` target and the held cube by identity.
> - **Steer = freest valley nearest the goal bearing.** Goal-bearing-open is the fast path (= today's
>   line-of-sight march); a blocked front lets a sideways/backward valley win. Hysteresis/commitment
>   on the chosen valley so it doesn't thrash. This makes willingness-to-regress the *escape* from the
>   concave/U pocket that defeated the bearing-offset steer.
> - **Movement is body-frame** ([MacroExecutor.cpp:175](../src/Features/Harness/MacroExecutor.cpp#L175),
>   `ApplyMoveAnalog`): camera stays toward the target while the body strafes any direction — so a
>   backward step doesn't spin the view, and `go_to` ends aimed at the target.
> - **Held identity is reused, not re-inferred.** `pick_up` already confirms the grab
>   ([MacroExecutor.cpp:775](../src/Features/Harness/MacroExecutor.cpp#L775)); cache that entity on the
>   executor (set on SUCCESS, cleared in `Release`) and the histogram skips it. No per-tick flag, **no
>   proto change**. Supersedes the reverted P0.2 — driven by the pick_up confirmation, not the
>   unreliable `m_hOwnerEntity` networked handle. The Python side already tracks this as `held_mark`
>   ([testchamber_session.py:91](../py/testchamber_session.py#L91)); the C++ cache mirrors it where
>   the march runs.
> - **VFH subsumes the planned `plane.normal` Bug2 wall-follow (P1.2).** "Follow the wall toward the
>   door" falls out of "freest valley nearest goal" when you're flush against a wall (the grazing ray
>   is the open valley). One mechanism, not two. Standalone Bug2 is kept only as the documented
>   completeness fallback for concave/U pockets where greedy VFH can still local-min.
> - **Termination must go global.** Per-batch regress is now *allowed* (backing out of a pocket), so a
>   stalled batch can no longer mean `BLOCKED`; use a **global closest-approach** check + the
>   `kGoToMaxTicks` cap instead.
> - **Caveats:** ~24 point-rays/batch (still ≪ the ~1ms framebuffer read — cap + measure); a point-ray
>   fan can slip a thin feature / clear a ray but clip the hull (densify or hull-ify if it misses);
>   per-direction edge check must run on the *chosen* heading, including backward, before committing.

### P0 — shared primitives (C++)
- **P0.1 — hull-swept guard.** ~~Add a `HullTrace` helper + swap `CheckGuard`'s point-ray for it.~~
  **REVERTED** (predictive→reactive pivot, see note above): superseded by the reactive P1.1; the
  forward wall guard is gone (`CheckGuard`→`CheckEdge`, edge-only). `TraceHull` deferred to P2.
- **P0.2 — held-entity-aware trace filter.** ~~Cache the grabbed entity, 2-entity filter.~~
  **REVERTED** — the reactive march has no forward trace, so a carried cube can't fool it. Re-add a
  C++ held-entity handle when P-manip/laser actually needs one.
- **P0.3 — `ReadBoolField(ent, name)`** wrapping `EntField::getServerOffset` (also the §5 laser
  predicate reader). **Deferred to P-manip** (its first consumer — avoids landing dead code now).

### P1 — local controller (C++) — *complete locomotion*
- **P1.1 — reactive march + perturbation steering. ✅ SHIPPED + adversarially reviewed (2026-06-19).**
  No predictive forward trace. Hold forward toward the target; the engine slides the player along
  walls. Track `bestDist` (closest 2D approach); a batch that beats it by `kGoToProgressEps`(4u) =
  progress (sliding toward target counts). After `kGoToStuckBatches`(3) homing batches with no
  progress, steer an offset heading (`{±50,±90,±140}°` off bearing, escalating) for
  `kGoToSteerBatches`(6) batches, then re-home; give up `BLOCKED` only after a full offset cycle
  makes no net gain. `EDGE` stays predictive (`CheckEdge` down-ray — a pit step is irreversible);
  `kGoToMaxTicks`(400) hard cap. Reports `SUCCESS` / `ADVANCED` (real ground covered, re-plan) /
  `BLOCKED` / `UNREACHABLE`. Review: termination clean, steering off-by-one-free, 0 confirmed bugs.
  *Verify:* `go_to` past the airlock jamb / a lone cube — slides/veers through instead of dead-stop.
  **→ Steering core superseded by P-VFH.1 (360° histogram); P1.1's reach/edge/termination scaffold is reused.**
- **P1.2 — Bug2 wall-follow. SUPERSEDED by P-VFH** — the 360° "freest valley nearest goal" reproduces
  wall-following without a separate state machine. **Retained only as the documented completeness
  fallback for concave/U pockets** where greedy VFH can still local-min: follow the obstacle boundary
  via `plane.normal` until the start→goal line clears. Build *only* if a real chamber defeats VFH.

### P-VFH — 360° VFH local controller (C++ before Python) — *supersedes the P1.1 steer + P1.2*

Each phase hand-verified via `py/macro_repl.py` on a live game (the user runs the game). C++ before
Python; P-VFH.1 ships the side-door fix on its own. No proto change anywhere in this group.

- **P-VFH.1 — VFH steering core. ✅ SHIPPED + adversarially reviewed (2026-06-19).** Replaced the
  offset-list steer with a 360° clearance histogram (`RayClearance` one ray per bin →
  `ChooseVfhHeading`: highest-scoring passable, non-cliff bin, score `= -|angle-to-goal| (dominant) +
  kVfhClearWeight·clearance + kVfhHystBonus` hysteresis), body-frame strafe `(-sin Δ, cos Δ)` with the
  camera held on the target, and **global** closest-approach + `kGoToMaxTicks` termination (regress is
  now allowed, so a single stalled batch ≠ `BLOCKED`; only `kGoToGlobalStall`=40 no-gain batches blocks).
  Constants: bins=24, probe=96u, clearMin=40u, clearWeight=0.15, hyst=15°, stall=40. *Fixes the
  side-door give-up: oscillation, the far door, move-away pockets.* Review (6 finders + refute + a
  completeness critic) confirmed the strafe math, trace/yaw frame, threading, and both verify scenarios;
  one in-scope fix applied: the `kGoToMaxTicks` cap fallthrough was mislabelled `UNREACHABLE` (a code the
  agent prompt doesn't handle) → now `BLOCKED` (cap-with-net-progress already upgrades to `ADVANCED`), so
  `UNREACHABLE` is dropped until P2 A* gives it a real "no path" meaning. *Verify:* start flush to a wall
  with the door far to one side — it follows the wall to the door instead of `BLOCKED`; a U-pocket no
  longer dead-locks. **Known deferred gaps (by design, confirmed in review):** `go_to` *to* a solid prop
  (cube/turret) or carrying a cube probes through it as a wall → P-VFH.2/.3 (target/held identity skip);
  point-ray-vs-hull clip + flush startsolid → P-VFH.4; deep concave-pocket escape → P1.2 Bug2.
- **P-VFH.1b — wedge detector + telemetry (2026-06-19, follow-up).** Field repro: `go_to` on a workshop
  map veered 51° off-goal into the curved rim of a circular door and froze — telemetry (`clr=96`,
  `moved=0.0` for 40 batches, `hdg` frozen) proved a **ray-blind wedge**: the point-ray reads the lane
  open but the player hull/lip can't enter, and the static histogram re-picks the identical bin forever.
  VFH steers only on ray-sensed clearance, so it never elected another valley (the old offset-steer
  blind-veered on any stall; VFH had dropped that fallback). Fix: a **reactive wedge detector** —
  commanded a move but `moved < kWedgeEps`(3u) for `kWedgeStuckBatches`(2) ⇒ block that heading's bin
  ±1 in a per-bin `ttl` map for `kWedgeCooldown`(16) batches (decaying), so `ChooseVfhHeading` skips it
  and the next pick veers/backs out. This is the "physical block" sense the rays miss — complements (not
  replaces) the P-VFH.4 hull-swept rays. Also added cvar `sar_harness_goto_debug` (per-batch
  `dist/best/hdg/clr/moved/wb` log; off by default) — keep it; it's the diagnosis tool for this family.
  *Verify:* re-run the rim repro with the cvar on — on a `moved≈0` batch `hdg` should now **change** and
  the player slides off the rim instead of freezing.
- **P-VFH.2 — held-entity cache. ✅ SHIPPED + adversarially reviewed (2026-06-20).** Not a
  `heldEntity_` member: `MacroExecutor` is rebuilt per macro step ([Portal2HarnessImpl.cpp:576](../src/Features/Harness/Portal2HarnessImpl.cpp#L576)),
  so the cache is a file-scope `std::atomic<uint32_t> g_heldEntityKey` (packed index<<16|serial), set
  on `pick_up` SUCCESS, cleared on `Release`, **and cleared on `ON_EVENT(SESSION_START)`** (review
  catch — a cube held when an episode resets reloads into the same slot, so a stale key would skip a
  real, no-longer-held cube). gRPC-thread write / main-thread read, no race (verbs are serial).
- **P-VFH.3 — obstacle OBBs into the histogram. ✅ SHIPPED + adversarially reviewed (2026-06-20).**
  `InjectObstacles` (in `ChooseVfhHeading`, after the ray fan) walks the live entity list and, per
  obstacle prop, lowers the bins its footprint covers to `freeDist = dist − (bounding-circle +
  player-half-width)`. A bin below `kVfhClearMin`(40u) is unpassable → the march veers with a **≥40u
  body-edge standoff**, never picking a heading into a prop. **Scope widened past cubes** per user
  ask: `IsGoToObstacleClass` = weighted/monster cube + box + floor turret + floor buttons
  (`prop_floor_button`/`_cube_`/`_ball_`/`prop_under_floor_button`) + pedestal `prop_button`. Skips
  the `go_to` target + held cube by identity. The planned "`ClearFramebulk` to kill forward creep"
  was **unneeded** — the veer is elected the *same* batch (VFH never commits a forward step first),
  so there is no creep to cancel. ~100 LOC. *Verify:* `go_to` past a cube on a button leaves
  `on_button` True; `go_to` *to* a cube still approaches; walking while carrying isn't self-blocked.
- **P-VFH.4 — flush-wall hardening.** Handle `startsolid`/`allsolid` rays at point-blank (garbage
  normal in exactly the flush case), cap traces/batch, per-direction edge check on the chosen heading
  including backward. ~25 LOC. *Verify:* `go_to` while spawned point-blank against a wall doesn't pick
  a random heading.
- **P-VFH.5 — grammar / prompt / smoke (Python).** Rewrite the `go_to` doc in `macro_grammar.py`
  (routes around cubes, finds side doors, may step back; drop the straight-line caveat), update result
  codes + the `gemini_agent.py` feedback notes, and add `agentloop_smoke` assertions
  (go_to-around-a-cube leaves it seated; go_to-to-a-far-side-door arrives). ~40 LOC.

### P2 — global A* (C++) — *optimal + plannable*

> **Detailed design + decision-forks + phasing: [astar_routing_design.md](astar_routing_design.md)**
> (2026-06-20). The bullets below are the original sketch; the design doc supersedes them and also
> records why the save/restore tree-search alternative is parked at the puzzle layer (C9), not here.
>
> **A\* searches *static world geometry only* and is blind to causality by construction** (no
> button→door wiring, no affordance envelopes). An **offline BSP preprocessing layer**
> ([offline_map_preprocessing.md](offline_map_preprocessing.md), 2026-06-20) is the *complement* —
> but at the **causality** layer, not here: its load-bearing payload is the I/O causal graph +
> affordance prior the model otherwise guesses. Offline *geometry* is **parked** — runtime A\*
> already has ground-truth, dynamics-aware geometry and strictly dominates a static grid for "which
> corridor" (a static grid can't even see the dynamic cube-pocket A\* exists to fix).
- **P2.1 — lazy occupancy grid.** Cell ≈ player-hull-width; `WalkableCell` (floor hull-probe + body
  clearance, stores floor z) + `Passable(a,b)` (hull-sweep between adjacent cells), evaluated on
  demand + cached. *Verify:* a debug condump of probed cells matches the visible floor/walls.
- **P2.2 — A\* → waypoints → P1 executor.** On a blocked straight march, A* over the lazy grid
  yields a waypoint chain fed leg-by-leg to P1. `BLOCKED` only when A* finds no path. *Verify:*
  `go_to` across a chamber needing a route around a central obstacle / through a doorway.
- **P2.3 (optional) — surface the plan** (`repeated Vec3 path` / `path_length` in `MacroResult`):
  proto change → `make proto` + rebuild + smoke. Nice telemetry; skip if not needed.

### P-manip — reliable place-on-button (C++) — *parallelizable with P1/P2 (needs only P0.3)*
- Upgrade `Release(mark)`: when the mark's class is a button, run a closed-loop place — position the
  held cube over the button OBB-top center → drop (`PulseUse`) → settle → `ReadBoolField` the
  button/cube → seated? `SEATED` : re-grab + micro-step toward center + retry ≤K → `NOT_SEATED`.
  Non-button mark keeps look-down/drop. **This is the prototype for the §5 laser controller** —
  same perturb→settle→read-predicate→retry skeleton. *Verify:* on testchamber_000, `go_to` button
  then `release <button>` reliably seats the cube (`pressed` stays True) across repeats.

### P-py — grammar / prompt / smoke (after the C++ lands)
- `macro_grammar.py`: rewrite `go_to` doc ("walks to a mark, routing around obstacles; fails only if
  no path exists") and **drop the STRAIGHT-LINE `CAVEAT`** (it'll be false). Update `release` doc
  (closed-loop onto a button). Mirror any new result codes.
- `gemini_agent.py` system prompt: update the `last_result` feedback notes accordingly.
- `percept_grammar_smoke` (examples still validate) + `agentloop_smoke` (go_to-around-obstacle +
  place-on-button assertions).

---

## 5. Lasers — the next big deal 🔴

This is the **next major planning frontier** after the locomotion core. Lasers are the first stock
element that turns the chamber into a **beam-routing** problem: the agent must hold a *Discouragement
Redirection Cube* (reflective, `m_nCubeType==2`) in an emitter's beam and steer the **re-emitted**
beam onto a catcher/relay, which powers a door/the exit. It is the first verb whose success depends
on a **continuous 6-DOF-ish control loop** (cube position + orientation), and it's the cleanest test
of the "same closed-loop spine, new predicate" claim. Per ROADMAP it's part of M3; this section is
the design we're committing to so the locomotion controller is shaped to absorb it.

### 5.1 The mechanics (Portal 2 Thermal Discouragement Beam)
- **Emitter** `env_portal_laser` — fires a straight beam along its facing; `m_bLaserOn` (net) = on.
  Beam direction = emitter `abs_angles` forward.
- **Redirection cube** `prop_weighted_cube` with `m_nCubeType==2` — intercepts the beam and re-emits
  a new beam from the cube along a cube-fixed axis (NOT a mirror reflection — it routes the beam out
  a defined face). When **held**, the cube's orientation tracks the player (the exact coupling is the
  #1 unknown — see the recon spike). The held cube also **shields the player** from the beam (the
  beam kills on contact: "discouragement"), which is *why* you manipulate it by holding it in-beam.
- **Catcher** `prop_laser_catcher` / **relay** `prop_laser_relay` — the beam's destination; powers an
  output (door, etc.). Neither prop carries a beam-hit status field directly — the hit is detected by
  a child **`point_laser_target`** whose `m_bPowered` (dm) flips true while struck. *(Catcher = latch
  /permanent-while-struck; relay = can be toggled/retriggered. Behaviour differs; handle both.)*
- The beam can chain through multiple redirection cubes in advanced chambers. **v0 laser scope =
  single emitter, single redirection cube, single catcher** (one hop). Multi-hop is P1.

### 5.2 The two hard problems (both must be solved before the verb)
1. **Beam controllability (the physics unknown).** Can we steer the re-emitted beam with the
   primitives we have (player position via `move`/`go_to`, cube orientation via `aim`/`look`)? Is the
   redirect axis **continuous** with cube yaw or **face-quantized** (90° snaps)? How tightly does the
   held cube's orientation follow the view, and is there lag/spring? **Answer comes from the recon
   spike (L0); we do NOT design the control law blind.**
2. **Catcher↔target association + the "which catcher" question (ROADMAP 1b, currently DEFERRED).**
   The percept must tell the agent *which* catcher is the goal and *what it powers*. Today
   `point_laser_target.m_bPowered` flows but nothing associates a target to its parent
   catcher/relay, and catchers/relays aren't even registered as marks. status_field_recon flags the
   association ("by parent `m_hParent`/`m_hMoveParent` or proximity") as an open design question.

### 5.3 Laser phases (gated; built after §4 + the L0 spike)
- **L0 — recon spike (the gateway, do FIRST).** On a laser chamber: pick up the reflective cube,
  stand in the beam, sweep view yaw/pitch + small position steps; at each step log cube
  `abs_origin`+`abs_angles`, emitter forward, the traced re-emitted beam endpoint, and every
  `point_laser_target.m_bPowered`. **Deliverables:** (a) the (player view, player pos) → (cube
  orientation) → (redirect direction) mapping; (b) whether it's continuous or quantized; (c) whether
  m_bPowered alone is a dense-enough signal or we need beam-endpoint tracing for gradient. Write it up
  like `status_field_recon.md`. *This decides whether L3 is "hill-climb on view+pos" or "solve for the
  one face orientation."* Reuses the `sar_harness_dump_fields`/macro_repl recon discipline.
- **L1 — laser entities into the snapshotter.** Register `prop_laser_catcher`, `prop_laser_relay`
  (as marks; their only fields are health/lifeState), confirm `env_portal_laser.m_bLaserOn` is
  captured, and surface `point_laser_target.m_bPowered`. *Verify:* a laser chamber's percept lists
  emitter/catcher/relay marks + a powered bit.
- **L2 — catcher↔target association + percept enrichment.** Associate each catcher/relay with its
  child `point_laser_target` (try `m_hParent`/`m_hMoveParent` first, proximity fallback) so the
  catcher mark itself reports `powered`. **Stretch (ties to [exit_criteria_structure.md](exit_criteria_structure.md)):**
  read the catcher's `OnPowered` output target so the percept can say "catcher 5 powers @exit_door" —
  the beam→door dependency the agent currently has to guess. *Verify:* powered state shows on the
  catcher mark; output-target resolves to the right door.
- **L3 — the `aim_laser <target-mark>` verb (the closed-loop controller).** Precondition: holding a
  reflective cube, a beam exists. Loop, reusing the **P-manip micro-adjust skeleton**:
  1. ensure the cube is **in the incoming beam** (trace emitter→cube forward is unobstructed and the
     cube intercepts it; if not, `go_to`/`move` into the beam corridor while keeping the cube as
     shield — never step the player into the live beam);
  2. **open-loop seed** (if L0 says steerable): trace from the cube along the (now-known) redirect
     axis, compute the view/position nudge that points it at the target catcher;
  3. **closed-loop confirm**: perturb view (±small) + position (±small body-frame, staying in cover),
     advance ticks, `ReadBoolField(target, "m_bPowered")`, hill-climb to true;
  4. on powered + stable → `POWERED` (and, if the chamber needs the beam held, the agent keeps
     holding or places the cube on a pedestal/holder — decide per L0/chamber); else `NOT_POWERED`.
  Player-safety is a hard constraint in the loop: every perturbation keeps the cube between the
  player and the beam. *Verify:* on a single-hop laser chamber, `go_to` cube → `pick_up` →
  `aim_laser <catcher>` powers the catcher and opens the door.
- **L-py** — grammar/prompt: add `aim_laser` to `VERB_SPECS` (mark='required', maybe a held-reflective
  precondition in `validate`), examples, prompt note; smoke check on a laser chamber.

### 5.4 The common spine (why this is cheap once §4 exists)
P-manip and L3 are the *same controller*: `perturb → settle → ReadBoolField(predicate) → accept/retry`.
- **place-on-button:** predicate = `m_bButtonState`; perturbation = drop position / re-grab + step.
- **aim_laser:** predicate = `point_laser_target.m_bPowered`; perturbation = view + in-beam position.
So L3's net-new code is mostly the **beam-geometry helper** (trace emitter→cube→catcher) + the
in-beam-cover constraint; the loop, the predicate reader (P0.3), and the grab/hold mechanics are
already built. That's the payoff for keeping the (predicate, perturbation) seam pluggable in §3.

### 5.5 Laser risks / unknowns
- **#1: held-cube orientation control** — if the cube auto-orients to a fixed pose and view doesn't
  rotate the redirect axis, L3 becomes "position the player so the right face intercepts," a harder,
  more constrained problem. L0 must settle this *before* L3.
- **Beam kills the player** — the in-cover constraint is load-bearing, not optional; a bad
  perturbation = death + respawn = ruined episode. Trace the beam each iteration and veto unsafe nudges.
- **m_bPowered may be a flat (non-gradient) signal** — powered is binary, so hill-climbing needs a
  surrogate gradient (beam-endpoint distance-to-catcher from tracing) until it flips; L0 confirms
  whether tracing the redirect beam is reliable enough to provide that gradient.
- **Relay vs catcher semantics** (latch vs toggle), **multi-hop beams** (deferred to laser-P1),
  **funnels/light bridges are NOT lasers** (separate "surface family", out of this doc).
- **Association ambiguity** — proximity fallback can mis-pair a target to the wrong catcher in dense
  chambers; prefer the parent handle, log when proximity is used.

---

## 6. Cross-cutting risks / open questions (locomotion)
- **Trace budget on the main thread** — measure worst-case A* expansion traces/episode so lazy
  probing never stalls the tick (the EntitySnapshotter perf lesson). Cap A* node expansion.
- **`startsolid` hull traces** — player flush against geometry must not read as "clear" (P0.1).
- **Wall-follow termination** — perimeter budget + the Bug2 leave-condition (re-cross the start→goal
  line closer than the hit point) to guarantee no infinite loop.
- **Doors** — a closed door is solid; door open-state isn't in the percept. The local layer treats it
  as a dynamic block → wall-follow/re-plan. Acceptable for v0; revisit with door-state resolution
  (M2/animation family).
- **Held-entity cache staleness** (P0.2) — a stale handle in the trace filter is harmless and
  self-corrects on next pick_up/release.
- **Z/multi-level** — 2D connectivity misses stairs/ledges; the 2.5D-ready cell rep contains the M3
  refactor to "add step-height edges," not "rewrite the grid."
