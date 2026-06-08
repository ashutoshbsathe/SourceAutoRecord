# Phased Implementation Plan — Annotated-Env Percept/Act Harness

Companion to [llm_percept_act_grammar.md](llm_percept_act_grammar.md). That doc is the *design*; this is the *build order*.

## How to read this

Each phase is sized to be one small, self-contained PR implementable by a low-thinking-budget model. Every phase lists: **Goal · Files · Steps · Verify · ~Size · Deps**. Phases are grouped into tracks; **do tracks in order, but phases within a track are mostly sequential**.

Guiding constraints (from the user):
- **SAR (C++) functionality first; Python is the *last* thing we touch.** The whole percept/act capability must be demonstrable in-engine before any RL/LLM client exists.
- **Track A is hand-viewable.** The user loads a chamber, flips a cvar, and *looks* at the annotated frame to finalize the grammar. No gRPC, no Python needed for that.
- **KISS.** Reuse `OverlayRender` / `Engine::Trace` / `TraceFirePortal` / existing placement-preview code. New code is glue, not subsystems.
- Per repo rule: **the agent does not launch the game.** "Verify" steps describe what *the user* runs.

Naming convention for new cvars: `sar_harness_annotate*` (matches existing `sar_*` family, `src/Variable.hpp`).

---

## Track A — Visual annotation (in-engine, hand-viewable)

Pure client-side rendering via `OverlayRender`. Independent of the harness/gRPC/Python — works by loading a map and toggling a cvar. This is what the user inspects to design the grammar.

> Entity source: iterate the **server entity list** directly (`entityList->GetEntityInfoByIndex(i)`, the same loop `EntitySnapshotter.cpp:350` uses) and read `abs_origin` / `abs_angles` / `collision()->OBBMins/Maxs` via the `Entity.hpp` SDK helpers. This keeps annotation independent of whether a harness session is active, and keeps entity indices consistent with the snapshot/telemetry used later.

### A1 — Annotation feature skeleton + box around cubes
- **Goal:** prove the pipeline: one cvar draws a wireframe box around every `prop_weighted_cube`.
- **Files:** new `src/Features/Harness/HarnessAnnotate.cpp` (+ register like other Features); `src/SAR.cpp` if explicit registration is needed.
- **Steps:** declare `Variable sar_harness_annotate("sar_harness_annotate", "0", ...)`. Add `ON_EVENT(RENDER)` that early-returns unless the cvar is set. Iterate the server entity list; for entities whose classname == `prop_weighted_cube`, call `OverlayRender::addBoxMesh(origin, mins, maxs, angles, RenderCallback::none, RenderCallback::constant({255,215,0}, /*nodepth*/true))`. Pattern to copy: `src/Features/Routing/StepSlopeBoostDebug.cpp:65-87`.
- **Verify (user):** load a chamber with a cube, `sar_harness_annotate 1` → gold wireframe box hugs the cube, visible through walls.
- **~Size:** ~80 LOC. **Deps:** none.

### A2 — Box around all puzzle classnames
- **Goal:** extend A1 to the full target set.
- **Files:** `HarnessAnnotate.cpp`.
- **Steps:** define a `static const` classname set, drawing a box for any entity whose classname is in it. Two groups:
  - **Core puzzle objects (design doc v1):** `prop_portal`, `prop_weighted_cube`, `prop_monster_box`, `prop_button` (pedestal push button), `func_weight_button`, the floor-button family (`prop_floor_button` big red 1500kg button, `prop_under_floor_button`, `prop_floor_cube_button`, `prop_floor_ball_button`), `prop_testchamber_door`, `env_portal_laser`, `prop_laser_catcher`, `prop_laser_relay`, `point_laser_target`, plus the player.
  - **Hazards + brush-trigger volumes (added at A2, final keep/drop decided at the A5 checkpoint):** `npc_portal_turret_floor` (turret), `trigger_portal_cleanser` (emancipation grill / fizzler — already snapshot-tracked at `EntitySnapshotter.cpp:93`), `trigger_catapult` (faith plate), `prop_tractor_beam` (excursion funnel emitter).
- **Note — invisible trigger volumes:** the fizzler / faith-plate / funnel classes are brush triggers; their OBB reads as a slab spanning the volume (boxing invisible playspace is *useful*, not a bug). Confirm `m_Collision` OBB actually populates for brush ents via a snapshot dump — physics props are the happy path.
- **Verify (user):** all puzzle elements in a chamber get boxes.
- **~Size:** ~45 LOC. **Deps:** A1.

> **A2 status — complete for classname-matchable elements.** The classname-set mechanism boxes every puzzle element that is a *discrete entity with a stable engine classname*. What remains (below) is a categorically different matching problem, deliberately parked for the checkpoint — so **A2 is done, not partial**.

#### What A2 taught us — the element taxonomy
Building A2 against real chambers surfaced that Portal puzzle elements split into two kinds. Only the first fits A2's classname-set mechanism:

1. **Discrete objects — stable engine classname, often a clean state flag.** Cubes, the button family (pedestal `prop_button`, weight `func_weight_button`, floor `prop_floor_button`/variants), doors, turrets, lasers/catchers/relays/target. Identification is guaranteed by `server->GetEntityClassName`. **This is what A2 boxes.**
2. **Chamber-mutating geometry — no stable classname; identified by map convention or animation/IO state.** Needs a *different matching mechanism*; **deferred to the A5 checkpoint** and tuned against the user's actual chamber set:
   - **Folding panels / stairs** (arm-mounted flip panels). Discovered via `sar_ent_info` (aim crosshair → prints class/name/model). Real example: a `func_brush` named `robo_rampa_03_panel`, solid type 6 (`SOLID_VPHYSICS`). Its OBB *would* box correctly, **but** the class is `func_brush` (generic: glass, clips, cover, scenery), so it cannot go in the classname set without flooding the view — same trap as `prop_dynamic`. The only discriminator is the **targetname** (`*panel*`), which is per-mapper convention, not an engine guarantee: a substring filter catches maps that name panels `*panel*` and silently misses `flip_`/`stairs`/`angled_`/unnamed ones. → would need an opt-in **targetname-pattern filter** (a second match path), seeded + tuned at the checkpoint.
   - **Gels** (propulsion=orange, repulsion=blue, conversion=white) and **light bridges** — paint/projector **surfaces**, not box-able entities at all. Different approach entirely (read the paint map / projector volume), stage TBD.

   *Decision recorded:* defer all of category 2 to the checkpoint rather than bolt a fragile, map-specific heuristic into A2.

### A3 — Stable mark numbers (Set-of-Marks labels)
- **Goal:** each boxed entity gets a persistent number label `①②③…`.
- **Files:** `HarnessAnnotate.cpp`; new small `src/Features/Harness/MarkTable.{hpp,cpp}` (shared mark-assignment, reused by telemetry later).
- **Steps:** `MarkTable` maps `(entity_index, serial_number) → int mark`, assigning the next free small int on first sight, cleared on `SESSION_START`. In the render hook, for each boxed entity call `OverlayRender::addText(origin + {0,0,topZ}, std::to_string(mark), x_height, /*visibility_scale*/true, /*no_depth*/true, TextAlign::BOTTOM, {255,255,255})`.
- **Verify (user):** each box shows a stable number that doesn't flicker/renumber as the camera moves.
- **~Size:** ~60 LOC. **Deps:** A2.

### A4 — Color-by-class
- **Goal:** legible color coding so the user can read the chamber at a glance.
- **Files:** `HarnessAnnotate.cpp`.
- **Steps:** map classname → color (portal: read `m_bIsPortal2` → blue/orange; cube: gold; button: green; door: white; laser/emitter/catcher/relay/target: red; player: cyan). Use the color for both box wireframe and label.
- **Verify (user):** colors match the legend; portals show correct blue/orange.
- **~Size:** ~40 LOC. **Deps:** A3.

### A5 — Portalable-surface aim indicator (where would my portal land)
- **Goal:** show, for the current crosshair, where a portal would land and whether it's valid.
- **Files:** `HarnessAnnotate.cpp` (or enable/adapt existing).
- **Steps:** **first investigate reuse** — `src/Features/PlacementScanner.cpp:20-44` already does `camTrace()` with `MASK_SHOT_PORTAL`, and `src/Features/Hud/PortalPlacement.cpp:146-188` already draws placement info via `OverlayRender`. Prefer wiring those to an always-on cvar over new code. Fallback: trace from eye along view dir with `MASK_SHOT_PORTAL`, call `TraceFirePortal` at the hit, draw a quad at `finalPos` colored green (SUCCESS) / red (INVALID_SURFACE etc.).
- **DONE — pure reuse, ~10 LOC, no new cvar, no fallback needed.** `PortalPlacement.cpp` already computes `g_bluePlacementInfo` via `TraceFirePortal` each `PRE_TICK` and draws a portal-shaped preview at `finalPos`/`finalAngle` in `RENDER`, **already red-on-invalid**. A5 = OR the **master `sar_harness_annotate`** cvar (extern'd into `PortalPlacement.cpp`) into both gates; when on, draw the blue placement preview tinted **green** (the existing `drawPortal` swaps green→red itself on an invalid `ePlacementResult > 2`, giving the green/red affordance for free). Aim draw is guarded by `g_hasPortalGun` so stale info can't draw a stray disc at the world origin.
  - *Why a gate edit, not "just manage `sar_pp_hud`":* (a) `sar_pp_hud` is **hard-gated on `sv_cheats`** (`PortalPlacement.cpp:46,:153`), so toggling it from outside draws nothing unless we *also force `sv_cheats`* — sticky, achievement-breaking, intrusive. (b) `sar_pp_hud` draws blue/orange-when-valid; **green** needs touching the draw. (c) Puppeteering a shared cvar needs save/restore so we don't stomp the user's own value. The 10-line gate edit is the cleaner reuse than runtime cvar-puppeteering.
  - *Decisions:* (1) **Rides on the master `sar_harness_annotate`** — one knob, no separate `_aim` cvar. The per-tick `TraceFirePortal` cost is negligible and the coop partner-fizzle side effect is irrelevant for single-player checkpoint inspection; split out a sub-cvar later only if boxes-without-aim is ever wanted. (2) The aim path is independent of `sar_pp_hud`'s `sv_cheats` gate — but moot in practice: the harness autoexec (`src/Features/Harness/autoexec.cfg`) forces `sv_cheats 1` whenever the plugin loads, so cheats are always on and the cheat-less path is never exercised. (This fork doesn't compete officially, so always-on cheats is fine and assumed throughout.) (3) Draws the **blue/primary** preview only (blue & orange land coincident for the same crosshair).
- **Verify (user):** `sar_harness_annotate 1`; aiming at a white panel shows a green disc, at black/metal shows red.
- **~Size:** ~10 LOC (reuse). **Deps:** A1.

### A6 — (Optional, harder) Portalable-surface region highlight
- **Goal:** highlight *all* portalable surface regions currently in view, not just the aim point.
- **Files:** `HarnessAnnotate.cpp`.
- **Steps:** cast a coarse grid of rays through the view frustum (e.g. 16×9), `TraceFirePortal`/`SURF_NOPORTAL` at each hit, draw a small translucent quad at portalable hits. Throttle (every Nth frame) — this is many traces. **Split if complex.** May be unnecessary: in Track A4 the agent can already read white vs black panels off the image, and A5 confirms validity on aim.
- **Verify (user):** white panels in view get a faint highlight; black surfaces don't.
- **~Size:** ~80 LOC. **Deps:** A5. **Status:** build only if the user wants it after the checkpoint.

---

## ✅ CHECKPOINT — user reviews the annotated frame, finalizes the grammar

User inspects A1–A5 output on the real chambers and confirms/edits the §4 verb list and the targeting modes in the design doc *before* any macro code is written. Grammar changes are cheap here, expensive later.

---

## Track B — LOS filtering / EntitySnapshotter (optional, measured)

Enables egocentric observation and *may* reduce per-tick cost. **Do not assume a perf win** — the LoS raycast has its own cost; frustum-cull first, then measure. Non-destructive: hdem/rollout recording keeps full-state capture.

### B1 — Visibility predicate + egocentric annotation
- **Goal:** a reusable "is this entity visible to the player" test, applied to annotation.
- **Files:** new `src/Features/Harness/HarnessVisibility.{hpp,cpp}`; `HarnessAnnotate.cpp`.
- **Steps:** `IsVisible(entityOrigin, eyePos, viewAngles, fov, aspect)` = frustum test (cheap dot-product math) **then** one `Engine::Trace(eye → entityOrigin, MASK_OPAQUE, passEnt=player)` LoS check. Add cvar `sar_harness_annotate_los`; when set, annotate only visible entities. (Boxes are already depth-tested as of A1.)
- **Note — this is also the real fix for the A3 mark-label depth problem.** A Set-of-Marks number is a flat world-space text quad, so *neither* depth flag is clean on its own: depth-tested (`no_depth=false`, what A3 ships) gets **sliced** by a wall the label sits against; on-top (`no_depth=true`) **x-rays** every mark through walls (tried, looked awful). There is no good middle ground at the depth-flag layer. The fix is *this* predicate: when LOS filtering is on, **cull marks (and boxes) for occluded entities entirely** rather than relying on depth — apply `IsVisible` to the label too, not just the box. That removes both the slicing and the x-ray in one move.
- **Verify (user):** with the cvar on, only entities you can actually see are boxed *and* labelled.
- **~Size:** ~70 LOC. **Deps:** A2.

### B2 — (Optional, measured) LOS filter on the harness-observe telemetry path
- **Goal:** egocentric *observation* — the entity telemetry the agent receives is LOS-only — and reduce snapshot/serialize cost.
- **Files:** `src/Features/Harness/EntitySnapshotter.cpp` / `Portal2HarnessImpl.cpp` (serialize path only).
- **Steps:** add a mode that, when filtering is on, skips emitting non-visible entities in `Observe`/`AgentLoop` GameState (use B1's predicate). **Leave `.hdem`/rollout full capture untouched.** Profile before/after (the snapshotter is the documented hot path — `brainstorm/entity_snapshotter_redesign.md`); report whether LoS actually helps or just moves cost.
- **Verify:** snapshot dump in egocentric mode contains only visible entities; timing logged.
- **~Size:** ~60 LOC + measurement. **Deps:** B1. **Status:** build only if perf bites or egocentric obs is wanted; surface the tradeoff to the user first.

---

## Track C — Macro grammar (C++ executor)

After the grammar is locked. All macros run server-side on the main thread (where `Act()` dispatches ticks), using the trace/aim/portal primitives. Reuses the `MarkTable` from A3 to resolve `mark → entity`.

### C1 — Proto: macro message + result, routed to a stub
- **Goal:** the wire format and routing exist; executor is a stub.
- **Files:** `src/Features/Harness/harness.proto`; `Portal2HarnessImpl.cpp`; then `make proto` + rebuild.
- **Steps:** add `Macro { string verb; ... oneof target {int mark; Vector3 point; string direction;} ... }` and `MacroResult { bool ok; string result_code; ... }`. Add a `macro` field to the `AgentLoop` action message (oneof with the existing low-level keys). Server routes a macro to `ExecuteMacro()` which returns `{ok:false, result_code:"NOT_IMPLEMENTED"}`.
- **Verify:** a hand-crafted macro request round-trips and returns the stub result.
- **~Size:** ~80 LOC + proto. **Deps:** checkpoint.

### C2 — `look_at`
- **Goal:** aim the camera at a target.
- **Files:** `Portal2HarnessImpl.cpp` (ExecuteMacro).
- **Steps:** resolve target (mark→entity origin via `MarkTable`; or world point; or named direction). Compute `QAngle = Math::VectorAngles(target - eyePos)`; `engine->SetAngles(slot, ang)` (`Engine.cpp:139`); advance the configured ticks; return new view angles.
- **Verify:** macro aims the crosshair at the named entity.
- **~Size:** ~50 LOC. **Deps:** C1.

### C3 — `shoot_portal` (both target modes)
- **Goal:** place a portal at a mark-face or a free point, with validation.
- **Files:** `Portal2HarnessImpl.cpp`.
- **Steps:** aim at target (reuse C2). Call `TraceFirePortal` (`Server.hpp:19`) to validate; if valid, fire (`portal_primary`/`secondary` for the firing tick); return the `PortalPlacementResult_t` code (`PortalPlacement.hpp`) + `finalPos`. `color` selects blue/orange.
- **Verify:** `shoot_portal blue mark=N` lands a blue portal on N's face; targeting black surface returns `INVALID_SURFACE` without firing.
- **~Size:** ~80 LOC. **Deps:** C2.

### C4 — `go_to` (straight march + edge guard)
- **Goal:** walk to a target on the current platform without walking off it.
- **Files:** `Portal2HarnessImpl.cpp`.
- **Steps:** loop: face target (yaw only), set forward move, advance a few ticks, re-check distance; stop on within-threshold, `CheckStuck`, or edge-guard (a short down-trace ahead returns no floor). Return `{reached, stuck, final_dist}`.
- **Verify:** `go_to mark=button` walks to the button on a flat chamber and stops; won't walk into a pit.
- **~Size:** ~100 LOC. **Deps:** C2.

### C5 — Platform-edge probe + `go_to_edge` + nav telemetry
- **Goal:** crude nav cues + "go to edge in direction".
- **Files:** new `src/Features/Harness/NavProbe.{hpp,cpp}`; `Portal2HarnessImpl.cpp`.
- **Steps:** implement the fan-of-rays probe (design doc §5.2): N bearings × march × wall-trace + down-trace, classify `WALL / EDGE→VOID / EDGE→LOWER / FLOOR_PORTAL`. `go_to_edge(dir)` marches to `edge_dist − margin`. Include the nav-compass in the macro result / GameState telemetry.
- **Verify:** nav compass reports correct edges on a test platform; `go_to_edge E` stops at the eastern lip.
- **~Size:** ~120 LOC. **Deps:** C4.

### C6 — `pick_up_cube` / `release_cube` / `press` / `interact` / `wait`
- **Goal:** the interaction + settle verbs.
- **Files:** `Portal2HarnessImpl.cpp`.
- **Steps:** `pick_up_cube`/`press`/`interact` = face target (and `go_to` within reach for `press`), pulse `key_use` (`IN_USE`); `release_cube` = pulse `key_use`; `wait(ticks)` = advance N ticks with no input. No held-object signal (LLM tracks it).
- **Verify:** can grab a cube, carry it (via `go_to`), drop it on a button; door opens after `wait`.
- **~Size:** ~70 LOC. **Deps:** C4.

### C7 — Emit `mark` in telemetry (frame ↔ telemetry agreement)
- **Goal:** the number drawn on the frame == the `mark` in the entity telemetry.
- **Files:** `harness.proto` (add `int32 mark` to `EntityState`); `Portal2HarnessImpl.cpp` (populate from `MarkTable`); `make proto`.
- **Verify:** snapshot dump's `mark` for an entity matches its on-screen label.
- **~Size:** ~25 LOC + proto. **Deps:** A3, C1.

### C8 — Configurable think-time + gRPC keepalive
- **Goal:** the world can stay frozen for minutes without the connection dropping.
- **Files:** `Harness.cpp` (`ServerBuilder` keepalive/max-age); Python client channel args (small, even though Python is "last" this is infra).
- **Steps:** set gRPC keepalive + large/disabled max-connection-age on server and client so long pauses survive proxies/sockets (none configured today — `Harness.cpp:171-199`).
- **Verify:** submit a macro, idle 5+ min, response still arrives.
- **~Size:** ~30 LOC. **Deps:** C1.

---

## Track D — Python client (last)

Thin client over the now-complete SAR functionality.

### D1 — EntitySnapshot parser → Python entity list
- **Goal:** turn the proto entity snapshot (already on the wire, discarded at `py/rl_challenge_env.py:125`) into a clean Python list: `{mark, class, name, pos, dist, bearing, state{...}}`.
- **Files:** new `py/p2harness/entities.py`.
- **~Size:** ~80 LOC. **Deps:** C7.

### D2 — Macro validator / "lexer"
- **Goal:** validate the LLM's JSON locally before spending a gRPC message (design doc §8.1).
- **Files:** new `py/p2harness/macro_grammar.py`.
- **Steps:** one schema = source of truth for both validation and the LLM tool spec. Check verb known, args well-typed, `color` valid, `mark` exists in the current entity list and is a sane target type for the verb. Return a validated `Macro` proto or a structured error string (fed back to the LLM, no round-trip).
- **~Size:** ~120 LOC. **Deps:** D1.

### D3 — Percept formatter + ReAct driver + transcript logger
- **Goal:** the eval loop.
- **Files:** new `py/llm_eval/driver.py`.
- **Steps:** each step: read annotated frame from SHM + format player/entity telemetry → build the multimodal prompt → call the LLM (configurable `max_think_seconds`) → validate (D2) → send macro → record `MacroResult` → repeat until success or step budget. Log full transcript (actions, results, frames, success/step-count).
- **~Size:** ~200 LOC. **Deps:** D2, C2–C6, C8.

### D4 — Run on the simplest chamber + iterate
- **Goal:** first end-to-end perception-vs-cognition signal.
- **Steps:** point the driver at the user's simplest chamber (map + target + success criterion — *user to provide*); run a frozen LLM; read the transcript. Iterate prompt/grammar.
- **Deps:** D3, user's chamber set.

---

## Status fields (recon-driven) — feeds C7 / Track D

The §4 grammar gives the agent verbs; the **status fields** give it state ("is the button pressed, the catcher lit, the door open"). These come from the `sar_harness_dump_fields` recon sweep — protocol + live results in [status_field_recon.md](status_field_recon.md). Two findings from the first chamber (`sp_a2_triple_laser`) reshape the work here:

1. **Snapshotter registration gap.** Most status fields are *datamap-only* (`m_bPowered`, `m_nCubeType`, `m_toggle_state`, …); the snapshotter's field *discovery* is SendTable-only, so it never records them today — even though its *read* path could. Before status reaches gRPC telemetry (the C7 / D1 entity-state path), the snapshotter needs a small **curated per-class status registration**, *not* full datamap discovery. Mechanism + scope in [phase4_sendtable_discovery.md](phase4_sendtable_discovery.md).
2. **Catcher/relay power lives on a child `point_laser_target`, not the prop.** The status resolver must associate catcher/relay → its target (parent or proximity).

→ Net new work item, slots before C7 / D1: **"register curated datamap status fields in the snapshotter."** It only blocks the *status* portion of telemetry, not the macro executor — so it can land in parallel with C2–C6, after the recon table is filled across the chamber set.

---

## Dependency summary

```
A1→A2→A3→A4   A1→A5→(A6?)        ← Track A (hand-viewable)  ░ CHECKPOINT ░
A2→B1→(B2?)                       ← Track B (optional/measured)
[checkpoint]→C1→C2→{C3, C4→{C5,C6}}   C1→C8   A3+C1→C7   ← Track C (C++ macros)
C7→D1→D2→D3(+C2..C6,C8)→D4         ← Track D (Python, last)
```

## Open inputs needed from the user
- **Chamber set:** map names (or `.bsp`/`.vmf`) + per-chamber target position(s) + success criterion. Needed for C4/C5 testing and all of Track D.
- After the **checkpoint:** final grammar edits + whether A6 (region highlight) and B2 (telemetry LOS filter) are wanted.
