# Lens: locomotion-actuation — "fix the legs"

*Diverge-mode brainstorm, 2026-06-12. One of ten council lenses. Grounded in
`src/Features/Harness/MacroExecutor.cpp` (read in full), `brainstorm/first_light_and_next_steps.md`,
and the council briefs. First light's verdict was: reasoning solved, locomotion is the wall.
This lens owns the wall.*

**Grounding facts found while reading the code (load-bearing for several ideas below):**

- `go_to` is a straight-line march in 4-tick batches with ONE forward wall ray + ONE floor-edge
  ray (`CheckGuard`, MacroExecutor.cpp:205). The comment itself says "a fan-of-rays nav-compass
  is a follow-on."
- The EDGE guard (`kStepDownMax = 64.0f`) means the agent can **never descend** anything taller
  than 64 units. Most multi-level PeTI chambers are unwalkable today, independent of pathfinding.
- `CheckGuard`'s trace filter passes **only the player** (`filter.SetPassEntity(player)`) — a
  held cube in front of the player can trip the WALL ray. Carrying-while-walking is probably
  partially broken right now (and the step-12 cube-bump is the physical version of the same gap).
- `go_to(mark)` targets the entity's **OBB center**. For a door, that point is inside the wall
  plane; `kReachRadius = 48` of a door center is often unreachable by construction. Some of first
  light's BLOCKED on `go_to 8` (the exit door) is plausibly this, not just the glass.
- `Interact` already composes GoTo→AimAt→PulseUse — the auto-approach pattern exists in-repo;
  `PickUp` just doesn't use it (fails `OUT_OF_REACH` instead).
- Upstream SAR ships tick-perfect movement tooling we are not using:
  `src/Features/Tas/TasTools/{StrafeTool,AutoJumpTool,ShootTool,DuckTool,AbsoluteMoveTool}.cpp` —
  a speedrun-grade autostrafer and a portal-fire tool, already in the binary.
- No nav-mesh code anywhere in SAR; whether Portal 2's engine still carries Source's `CNavMesh` /
  `nav_generate` is an open recon question (idea 5).

---

## The ideas (ordered roughly by leverage)

### 1. Trace-sampled walkability grid + A* `go_to` ("give them legs")
**Build:** On `go_to`, sample an on-demand walkability lattice (~24u pitch) in a bounding box
around player+target using the already-wired `engine->Trace` + `CTraceFilterSimple` machinery:
a cell is walkable if a hull-ish probe finds floor within step-down, headroom clear, no solid at
torso height. Run A* over the 8-connected grid; feed the waypoint chain to the existing march loop
(each leg = today's straight-line go_to). The world is frozen between macros, so a few thousand
traces of grid-build cost are invisible. Crucially the grid encodes **walkable-NOW** space:
a closed door is a wall, no portal/funnel edges, no state changes — it formally cannot solve
puzzles (see spicy take).
**Why:** Fixes ~all of first-light steps 8–24 (the glass enclosure), unblocks the entire chamber
ladder. The model's plans are already correct; this is the named #1 lever.
**Effort:** M (grid sampler, A*, waypoint follower, failure plumbing, tuning).
**Depends:** nothing (idea 5 recon may simplify it; don't block on it).
**Unlocks:** SOLVED on testchamber_000; every other chamber; ideas 10, 20.

### 2. Failure surface v2: BLOCKED becomes a percept, not a verdict
**Build:** When a guard trips or A* fails, attach structure to `MacroResult`: blocking entity
(trace hit entity → MarkTable reverse lookup → `blocker_mark`/`blocker_class`), hit distance and
bearing, and a 16-ray free-space fan ("open headings: 100°–250°"). For A*-UNREACHABLE: nearest
reachable point to the target and its distance/bearing. All data is already in hand at failure
time; this is plumbing, not new sensing.
**Why:** The model recovers well when it *understands* failures (step 12 proves it). "BLOCKED"
with no referent forced 16 steps of blind probing. This is the replan fuel, and it stays
necessary even after pathfinding lands (UNREACHABLE needs an explanation too).
**Effort:** S.
**Depends:** none; synergizes with 1.
**Unlocks:** model-side replanning; the failure-taxonomy dataset (idea 22).

### 3. Auto-approach `pick_up` + per-class stand-point resolver
**Build:** (a) `PickUp` on `OUT_OF_REACH` internally runs `GoTo(mark)` first — literally the
composition `Interact` already uses, factored. (b) A per-class **stand-point resolver** replacing
naive OBB-center targets: button → stand point in front; door → near-side threshold (and a
`through=true` variant targeting the far threshold); dropper → beneath; cube → adjacent, not
centered (walking *to* a cube's center is how you punt it — step 12's bump).
**Why:** First light burned steps 2–4 on manual approach; door-center targeting is likely a
silent contributor to the BLOCKED loop. Cheapest real-win in the whole list.
**Effort:** S (days).
**Depends:** none.
**Unlocks:** fewer wasted steps per chamber → directly attacks the context blow-up too.

### 4. LocoGym: a locomotion-only chamber suite + LocoScore CI gate
**Build:** 10–20 PeTI chambers with **zero puzzle content**: the first-light glass enclosure
(test #1, verbatim), U-maze, pillar slalom, stairs, a 128u drop, narrow catwalk, doorway-while-
carrying-a-cube. A scripted agent (`header.model="scripted"` already supported by the trajectory
format) issues only `go_to <exit_mark>`. LocoScore = success rate + ticks vs a human `.hdem`
baseline. Run on every actuator change, agentloop_smoke-style.
**Why:** Operationalizes the doc's own principle — "a locomotion failure is an engine bug to fix,
never charged to the model." Without this, every actuator change is vibes; with it, actuation has
a regression suite and the pitch has a clean chart (LocoScore over time).
**Effort:** S–M (chamber authoring is PeTI-fast; the runner is ~100 lines on existing seams).
**Depends:** none; should land *before or with* idea 1 so the A* PR proves itself.
**Unlocks:** safe iteration on everything else in this lens.

### 5. Recon: is Source's NavMesh alive in Portal 2?
**Build:** One day of recon: `sv_cheats 1; nav_generate` in a PeTI map; check for `.nav` output
and whether `CNavMesh` queries survive in server.so. If alive: `go_to` becomes a nav-mesh query,
or at minimum the chamber-suite build step pre-generates `.nav` per chamber and the harness
parses it (the format is documented on the VDC wiki).
**Why:** Could collapse idea 1 from "build a planner" to "call the engine's planner." Even a
negative result is valuable (it justifies the custom grid in the writeup).
**Effort:** S (timeboxed: 1–2 days).
**Depends:** none. Sequencing: do this *first*, this week.
**Unlocks:** possibly a much cheaper idea 1.

### 6. Engine-truth held state (kill the client-side `held_mark`)
**Build:** Run the existing `sar_harness_dump_fields diff` recon protocol **on the player entity**
while grabbing/dropping: find the field that flips (candidates: the HL2-lineage grab-controller
handle, use-entity handle, attached-object handle). Surface `held_mark` in `GameState`.
**Why:** The documented desync (physics knocks a cube out of your hands; `release` that silently
grabs) lives entirely on the missing signal. With engine truth: `release` can honestly fail
`NOT_HOLDING`, the percept stops lying, and carry-mode locomotion (idea 7) gets its trigger.
**Effort:** S — the recon command and the curated-field append path both exist.
**Depends:** none.
**Unlocks:** 7; honest release/grab semantics; removes a whole ambiguity class from evals.

### 7. Carry-aware locomotion
**Build:** When holding (per idea 6): add the held entity as a second pass-entity in
`CheckGuard`'s trace filter (today it can trip WALL on your own cube); widen clearance checks for
the player+cube compound hull through doorways; optionally auto-lower the carried object while
marching. Report `BUMPED(mark)` if the held/carried object contacts a marked entity mid-march.
**Why:** "Carry cube through doorway" is *the* canonical Portal 2 locomotion act, and it is the
exact physical mechanism of first light's step-12 button-bump. This is a probable live bug, not
just a feature.
**Effort:** S–M.
**Depends:** 6 (or a client-supplied held hint until then).
**Unlocks:** reliable cube logistics — half of all PeTI puzzles.

### 8. Nav-compass percept — the A/B control arm for pathfinding
**Build:** Per-observation free-space rose: 16 rays at step height, distances + walkable flags,
emitted as JSON (and optionally drawn as overlay arcs). ~70 lines on the existing trace + percept
path.
**Why:** This is the *scientific control* for idea 1: arm A = dumb actuator + nav percept (model
does its own pathfinding at reasoning altitude), arm B = smart actuator (engine paths). Whichever
wins, "where should navigation live — percept or actuator?" is a publishable question the harness
is uniquely positioned to answer, and the briefs say active-perception infrastructure is cheap.
**Effort:** S.
**Depends:** none.
**Unlocks:** the altitude experiment (idea 13) gets its locomotion axis; egocentric Track B reuses
the same ray machinery.

### 9. Vertical policy: safe-descent, `jump`, `jump_to(mark)`
**Build:** Replace the blanket EDGE refusal with a descent rule: trace down at the ledge; if
floor exists within safe-fall height and isn't goo/void, descending is allowed (Portal 2 long-fall
boots make almost all descent survivable — the real check is "floor vs death"). New verbs: `jump`,
`drop_down`, `jump_to(mark)` (run-up + tick-timed jump via the framebulk + landing validation).
**Why:** Today the actuator cannot go DOWN 65 units. Multi-level chambers — most of PeTI — are
unwalkable regardless of pathfinding. Also required by the A* grid (descent edges).
**Effort:** S–M.
**Depends:** none; folds into 1's edge semantics.
**Unlocks:** multi-level chambers; faith-plate/pit chambers; the difficulty ladder past tier 1.

### 10. Topology-aware edges: path THROUGH portals and open doors
**Build:** Extend the nav graph (idea 1) with **dynamic edges derived from snapshot state**:
an open door contributes a threshold-to-threshold edge; a linked portal pair (`m_hLinkedPortal`
is already snapshotted) contributes a zero-length edge between the two surface stand-points,
with approach orientation. `go_to(mark_behind_portal)` then routes through the portal the model
placed.
**Why:** This is where the altitude line becomes crisp and defensible: **the model changes
topology (places portals, opens doors); the engine traverses whatever topology exists.** Portals
are locomotion. An agent that places a correct portal pair and then can't walk through them would
be first light all over again, one rung up.
**Effort:** M.
**Depends:** 1 (graph), 11 (portals to traverse), door open-state percept (other lens).
**Unlocks:** the portal-using chamber ladder actually being *completable*.

### 11. `place_portal(color, mark|point)` — portals ARE actuation
**Build:** Compose: visible-point aim (idea 19) → the green/red **portalable-surface predicate
already computed server-side** by `HarnessAnnotate`'s reticle → fire (the mechanics exist in
upstream `TasTools/ShootTool.cpp`). Failure surface: `NOT_PORTALABLE` / `NO_LOS` /
`SURFACE_TOO_SMALL`, each carrying the predicate's reason. `harness.proto` already reserves the
color/point/direction fields on `MacroRequest`.
**Why:** It's the game's name. Every chamber past tier 1 needs it, and all three ingredients
(aim primitive, surface predicate, fire tool) already exist — this verb is assembly, not research.
Claimed by this lens because portals are the game's primary *movement* mechanism.
**Effort:** M (assembly + the failure surface + smoke tests).
**Depends:** 19 helps; nothing blocks.
**Unlocks:** tiers 2+; idea 10; the speedrun axis (portal routes).

### 12. Interruptible macros: event-triggered early return
**Build:** Long macros are blind for up to 400 ticks. Add watch conditions to the march loop:
abort early with `INTERRUPTED` + detail when (a) any marked entity's curated status flips
mid-macro (button unpressed, door closed — the snapshotter already memcmps every tick; this is
a changeVersion poll per 4-tick batch), or (b) the held object detaches (with idea 6).
**Why:** First light discovered the cube-bump a full step late. Mid-macro world feedback turns
"surprise next frame" into "alert in the result," exactly the structured-feedback channel the
model demonstrably uses well.
**Effort:** M.
**Depends:** 6 for (b); none for (a).
**Unlocks:** longer/fatter macros become safe → enables idea 13's L3 verbs without blindness.

### 13. The altitude ladder, made executable (one knob, four levels)
**Build:** Formalize and implement: **L0** raw framebulk (the RL surface, exists) → **L1**
`move/look/jump` → **L2** `go_to/pick_up/place_portal` (today+) → **L3**
`fetch(cube_mark, dest_mark)` / `navigate_to(exit)`. Each Ln implemented strictly in terms of
L(n−1); a cvar caps a run's maximum altitude. L3 `fetch` is deliberately the over-helpful extreme.
Run the same model on the same chambers at L1/L2/L3; plot steps-to-solve, tokens, and solve rate
vs altitude.
**Why:** "Where is the right reasoning/actuation altitude" is THE long-term question of this lens,
and this turns it from a doc argument into a measured curve — a figure made for the pitch deck
("here is how much reasoning each actuation level absorbs").
**Effort:** M (most L3 verbs are compositions of existing verbs + idea 1).
**Depends:** 1, 3; 12 makes L3 safe.
**Unlocks:** the headline methodological contribution beyond first light.

### 14. Learned `go_to`: resurrect the PPO stack as the macro backend (cortex/cerebellum)
**Build:** Give the dormant RL stack a *tractable* task: point-goal navigation in procedurally
generated PeTI rooms. Obs: nav-compass rays + goal offset (+ optionally the frozen-ViT embedding);
action: framebulk; reward: dense progress (no puzzle, no shaping arms-race — navigation has a
clean potential function). Serve via the existing `InferenceServer`; `MacroExecutor::GoTo` calls
it behind the **same MacroResult interface** so the swap is invisible to the model and to evals
(LocoGym, idea 4, is the eval).
**Why:** The RL stack's documented failure was a category error — it was asked to be a cortex.
As a cerebellum it has a clean dense-reward task, a regression suite, and a deployment seam that
already exists. Pitch-wise this is the strongest slide in the lens: *frozen Gemini cortex +
learned Source-engine cerebellum*, the exact hierarchy SIMA 2 gestures at, demonstrated in a real
engine with ground truth. Also the flywheel: every BLOCKED in every eval (idea 22) is a free
hard-negative for its curriculum.
**Effort:** L (honest: env-gen, training, the serving seam; months for one RE even with the
infra battle-hardened).
**Depends:** 4 (eval), 8 (obs), 22 (data); idea 1 is the baseline it must beat.
**Unlocks:** robustness past hand-coded A* (dynamic obstacles, moving platforms, momentum);
the headcount ask ("this workstream is a full-time RE").

### 15. Mine human `.hdem` for locomotion demonstrations
**Build:** Segment human playthroughs at grab/release/portal/button events into point-to-point
movement clips → (start pose, goal, action sequence, ticks) tuples. Use them to (a) BC-pretrain
idea 14's policy, (b) set per-chamber human tick baselines for LocoScore, (c) quantify the
human-vs-macro locomotion gap chamber-by-chamber.
**Why:** Human movement data is the scarce commodity (VPT lesson) and it's lying in the recorder
already. Locomotion is the *easiest* slice to mine because segmentation events are explicit
entity-state changes the snapshotter records.
**Effort:** M.
**Depends:** actions-in-hdem v2 (human-data lens's top item) for the input stream.
**Unlocks:** 14's warm start; honest "human-normalized" locomotion metrics for the pitch.

### 16. Momentum macros: expose the in-repo TAS tools (`strafe_to`, `abh`, `bhop`)
**Build:** Upstream SAR already ships tick-perfect movement tech: `StrafeTool` (vectorial/angular
autostrafer), `AutoJumpTool`, `DuckTool`. Wrap them as macros: `strafe_to(point, max_speed)`,
`abh(dist)` (accelerated back-hop), `bhop(n)`. The work is adapting tools that expect TAS-script
playback to drive the harness framebulk slot.
**Why:** The speedrun axis's "tick-perfect execution" half **already exists in this repo** and
is unused. A routing agent that proposes macro sequences gets execution-optimal movement for free;
route quality becomes measurable in ticks. Also: momentum is the part of Source locomotion no
A* grid will ever capture.
**Effort:** M.
**Depends:** 17 (tick accounting makes route quality legible); independent otherwise.
**Unlocks:** the routing/speedrun research axis; differentiates the platform from every
walk-speed game benchmark.

### 17. Tick-cost accounting + tick budgets
**Build:** Every `MacroResult` reports `ticks_used`; the eval budget switches (per cvar) from
"25 macros" to "N ticks." Trivial server-side counter.
**Why:** Macro-count budgets make fat macros free and thin macros expensive — exactly wrong for
comparing altitude levels (idea 13) and meaningless for the speedrun axis where the objective IS
ticks. One scalar fixes both.
**Effort:** S (a day).
**Depends:** none.
**Unlocks:** honest cross-altitude comparisons; speedrun scoring; better loop detection signals.

### 18. WEDGED vs BLOCKED vs STUCK + bounded auto-unstick
**Build:** Split the failure classes: BLOCKED (guard ray hit), STUCK (no progress under input —
exists), WEDGED (hull interpenetrating solid / velocity zero with clear guard). On STUCK/WEDGED,
attempt a bounded recovery script (back-step, jump, re-orient, retry once) *before* reporting,
and report what was tried in `detail`.
**Why:** A 32-bit Source engine generates physics jank for free; today all of it lands in two
overloaded codes. Recovery-before-report is the cheap version of "the engine owns local
actuation" — micro-unsticking is exactly the kind of thing a model should never burn a step on.
**Effort:** S.
**Depends:** none.
**Unlocks:** fewer dead runs; cleaner failure taxonomy for 22.

### 19. Visible-point aim: `aim_at` LoS refinement
**Build:** LoS-trace eye→OBB center; on occlusion, sample the 9 canonical OBB points and aim at
the nearest visible one; report `AIM_OCCLUDED` + the offset used when no point is visible.
**Why:** OBB-center aim fails on partially occluded targets (button behind glass, cube corner
past a frame) — silently today. Hard prerequisite for `place_portal` (idea 11) and for honest
interact failures. Also fixes the documented mark-label-through-walls inconsistency at the
*action* layer.
**Effort:** S.
**Depends:** none.
**Unlocks:** 11; trustworthy aim semantics everywhere.

### 20. Ride verbs: funnel / faith plate / lift
**Build:** `enter(funnel_mark)` / `ride(mark)`: the actuator's job is precise entry into the
element's effective volume plus a settle condition (zone arrival / velocity stabilization / tick
cap); the *element* does the locomotion. Faith plate = stand on plate + wait through flight +
landing validation; funnel = enter beam + optional exit trigger.
**Why:** Funnels and faith plates are stock PeTI **pure-locomotion** elements — they belong to
this lens, not the puzzle lens. Without ride semantics, every chamber containing one is an
automatic actuation failure misattributed to reasoning.
**Effort:** M.
**Depends:** element volumes in the snapshot (funnel/plate already annotated); 12's interruption
machinery for "in flight."
**Unlocks:** two whole element families in the chamber ladder.

### 21. `go_to point(bearing, dist)` — mark-less waypoint fallback
**Build:** A spatial (non-mark) go_to target: relative bearing+distance (or chamber-frame point;
the proto already reserves `Vector3 point`). Same march/guard machinery, no mark resolution.
**Why:** Cheap insurance *while* idea 1 lands (the model can self-route: "go to the gap left of
the glass"), and *after* it lands it's the escape hatch when A* says UNREACHABLE but the model
sees a path the grid missed. Also the natural target vocabulary for nav-compass headings (idea 8:
"open heading 120° → `go_to point(120, 200)`").
**Effort:** S.
**Depends:** none.
**Unlocks:** model-side pathfinding arm of the A/B (idea 8) becomes actually expressible.

### 22. Path telemetry: record the attempted path in every MacroResult
**Build:** `go_to`/`move` already read player position every 4-tick batch — keep the polyline
and guard events, attach them to `MacroResult`, render attempted-vs-achieved paths in the
trajectory viewer's top-down SVG.
**Why:** Three consumers: (a) the model can calibrate ("I traveled 312u along this arc"); (b) the
viewer makes actuation failures *legible* to humans reviewing runs; (c) every BLOCKED becomes a
labeled (state, goal, failed-path) hard-negative — the seed dataset for idea 14's curriculum and
for actuator regression archaeology. Almost free: the data is already sampled, it's just dropped.
**Effort:** S.
**Depends:** none.
**Unlocks:** 14's data flywheel; viewer-grade actuation debugging.

---

## Sequencing sketch (one RE + agentic coding)

- **Week 1:** 5 (nav recon, timeboxed) → 3 (auto-approach + stand points) → 17 + 18 + 22 (small
  result-surface wins) → start 1.
- **Weeks 2–3:** 1 (A* go_to) + 4 (LocoGym, lands with it) + 2 (failure surface v2). Rerun first
  light → expect SOLVED. That's the next milestone artifact.
- **Weeks 4–6:** 6 + 7 (held truth, carry mode), 9 (vertical), 19 + 11 (aim + place_portal),
  8 + 21 (the A/B control arm).
- **Months 2–3:** 10 (topology edges), 12 (interruptible), 13 (altitude ladder experiment),
  16 (momentum macros). 14 + 15 are the fundable workstream — that's the headcount ask, not a
  side quest.

## Spiciest take

**The "no global pathfinding" purity principle is already obsolete, and keeping it is
anti-science.** Navigation over *walkable-now* space is formally puzzle-free — Demaine's
PSPACE-hardness lives entirely in topology *changes* (buttons, doors, portals, state), so an A*
that treats a closed door as a wall and knows no portal edges **cannot smuggle a single bit of
puzzle-solving into the actuator**. Meanwhile every step the model burns micro-steering around
glass is a step where you measured nothing about reasoning. Straight-line `go_to` isn't the
instrument — it's the confound. The principle worth keeping is narrower and sharper: *the engine
never CHANGES topology, only traverses it.* The model opens the door; the engine walks through it.

## If I could only do ONE thing next week

Ship trace-grid A* `go_to` behind `sar_harness_goto_nav 1` (idea 1), with structured
UNREACHABLE/BLOCKED reporting (idea 2's minimal form), and rerun first light end-to-end.
Expected outcome: testchamber_000 flips BUDGET → SOLVED, the glass enclosure becomes LocoGym
test #1, and the project gains its second headline artifact: "same frozen model, fixed legs,
solved chamber."
