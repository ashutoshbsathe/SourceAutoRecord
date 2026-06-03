# Annotated-Environment Percept/Act Grammar for Frozen-LLM Reasoning Eval

**Status:** brainstorm / pre-implementation. Nothing here is built yet. This doc is for red-penning before any code lands.

**Provenance:** Motivated by feedback that training a policy at the *raw* pixel+key level confounds two failure modes — "couldn't aim/move" (perception/motor) vs "couldn't reason about the chamber" (cognition). The proposal below disentangles them by giving an off-the-shelf, *untrained* LLM a clean symbolic interface and reliable actuators, and measuring whether it can still solve chambers.

---

## 1. The scientific question this is built to answer

> Is the bottleneck *reasoning* or *actuation/perception*?

The eval is a clean ablation:

- Give the agent **reliable actuators** (a portal that lands where intended, a walk that stops at the platform edge) and a **clean percept** (the chamber, visually annotated + a structured entity list).
- Run a **frozen, untrained** LLM (Claude / Gemini / GPT-class) in a ReAct loop.
- **If it still fails** → the bottleneck is genuinely reasoning. That is the "worth investigating deeply" outcome (and a paper).
- **If it solves chambers easily** → raw-level RL pain was actuation/perception, not cognition → "fun little project."

Either result is publishable signal. Crucially, at the raw level these two failure modes are *indistinguishable*; this harness pulls them apart.

---

## 2. Why this fork is unusually well-suited (the three enablers)

### 2.1 Stable anchors are free — no image segmentation needed

The hard part of any vision agent is "point at the same object across camera/player motion." Normally: detection + tracking + re-identification, and it drifts.

We sidestep it entirely. `EntitySnapshotter` walks the global server entity list each tick and tags every entity with a stable `(entity_index, serial_number)` pair that persists across ticks (`src/Features/Harness/Portal2HarnessImpl.cpp:98`). Every portal, cube, button, door, laser, catcher = an entity with a persistent handle, world position, OBB, classname, and editor `targetname`. **When the camera swings, mark `⑦` is still `⑦`** — same handle, recomputed pixels, zero drift. The "consistent anchor points" requirement is solved at the engine level. A from-pixels RL agent cannot do this; our privileged game-state access can.

### 2.2 Annotations bake into the framebuffer for free (`OverlayRender`)

We do **not** add projection/marker logic to the gRPC layer. Instead we annotate *in-engine* and let the existing screen-capture pick it up.

`src/Features/OverlayRender.{hpp,cpp}` is a **3D world-space mesh renderer** already in this fork. It is *not* a HUD system. HUD = 2D screen-space pixels (the separate `Surface`/`VGui` path). OverlayRender emits real triangles/lines **into the rendered 3D scene**, depth-tested against world geometry, by hooking `Client::DrawOpaqueRenderables` / `DrawTranslucentRenderables` (`src/Modules/Client.cpp:809-827`). Its `addText` is *billboarded 3D text* anchored at a world position (camera-facing, baked font atlas), not a HUD string.

- `createMesh(solid, wireframe)`, `addLine/addTriangle/addQuad`, `addBoxMesh(origin, mins, maxs, ang, solidCb, wfCb)`, `addText(pos, str, x_height, visibility_scale, no_depth, align, col, bg_col)` (`src/Features/OverlayRender.hpp:30-54`).
- **`addBoxMesh` draws an oriented bounding box around any world position** — verified: it takes the 8 corners from `mins/maxs`, rotates by `Math::AngleMatrix(ang)`, translates by `origin`, builds 6 filled quads + 12 wireframe lines (`OverlayRender.cpp:172-213`).
- `RenderCallback::constant(color, nodepth=true)` → `$ignorez` materials → **draw THROUGH walls** (`OverlayRender.cpp:240-326`). Depth-tested (default) vs `nodepth` maps onto the observability toggle (§6): depth-tested = only-visible entities get a box (egocentric), `nodepth` = mark occluded entities too (global).

**Key consequence:** the Harness SHM framebuffer is a copy of that rendered scene, so anything `OverlayRender` draws appears in the 224×224 pixels the ViT/VLM sees **with no extra plumbing**. Verified consumers that box a thing in the world: `StepSlopeBoostDebug.cpp:65-77` (translucent-fill + opaque-wireframe player-sized boxes at world positions) and `Camera.cpp:357-364` (red keyframe boxes drawn through walls via `addBoxMesh` + `nodepth`). And `EntitySnapshotter` already captures each entity's collision OBB (`mins/maxs/origin/angles`, `EntitySnapshotter.cpp:78-80,257-262`) — the precise inputs `addBoxMesh` wants. Drawing a labeled box around every puzzle element is ~100–150 LOC of glue, not a new subsystem.

> Two disambiguations (these tripped us up once): (1) `OverlayRender` ≠ HUD ≠ `PlayerTrace`. `PlayerTrace.cpp` is the unrelated SAR feature that draws the green movement-replay ghost line — **we never enable it**; it merely happens to also use `OverlayRender`. (2) A *true* stencil glow (`CGlowObjectManager`) does **not** exist in retail Portal 2 and is not in SAR. Don't chase it. Wireframe/translucent boxes via `OverlayRender` achieve the highlight look more simply (KISS).

### 2.3 The world freezes while the LLM thinks

After a 256-tick warmup the harness calls `engine->SetAdvancing(true)` and the game advances **only** during an `Act()` call; between actions `PRE_TICK` returns early and the sim is paused (`src/Features/Harness/Harness.cpp:264-278`). `Act()` blocks on `tickCV.wait()` with **no server-side deadline/watchdog** (`Portal2HarnessImpl.cpp:420-445`, `Harness.cpp:171-199`). The *only* timeout is client-side Python: `step_agent_loop()` defaults to `timeout=5.0s` (`py/p2harness/harness.py:149-173`), already overridden to `30.0s` for reset (`py/rl_challenge_env.py:204`).

**Consequence:** an LLM can deliberate for as long as we allow against a perfectly frozen world. Making think-time configurable is a one-line `max_think_seconds` param (set `None` to disable). Caveat: for multi-minute pauses, add gRPC keepalive / max-connection-age so a proxy/socket doesn't silently drop the long-held stream (no keepalive is configured today).

---

## 3. The percept (what the agent observes each step)

**Visual + symbolic, always together — never symbolic-only.** Rationale: a text-only entity list forces the LLM to hold the chamber's spatial layout *in its head* across a long horizon, which it's bad at — so a failure would confound "spatial *imagination* failed" with "spatial *reasoning* failed." The annotated frame externalizes the layout (offloads imagination), isolating reasoning. So every step delivers all three channels below. There is no symbolic-only eval condition.

Three coupled channels, all derived from one frozen game state:

1. **Annotated frame** — the rendered chamber with `OverlayRender` overlays:
   - a colored box around each *puzzle-relevant* entity (portals, cubes, buttons, doors, lasers, catchers, relays, the target),
   - a billboard number label `①②③…` at the entity's top, drawn `no_depth` so it's legible through walls (optional — ties to observability mode, §6),
   - colored by class family (portal=blue/orange, cube=gold, button=green, laser/catcher=red, target=magenta),
   - optional crude nav arrows to platform edges (§5.2): 🔴void / 🟡lower-floor / 🟢walkable-far.
2. **Player telemetry** — position, eye position, view angles, velocity, on-ground, holding-cube (JSON).
3. **Entity telemetry** — the structured list, *already on the gRPC wire* but currently discarded at `py/rl_challenge_env.py:125`. Each entry: `{mark, class, name, world_pos, dist, bearing, portalable_faces?, state{...}}`. State comes from auto-discovered SendTable fields (e.g. catcher `m_bPowered`, portal `m_bActivated`/`m_bIsPortal2`/`m_hLinkedPortal`, cube `m_nCubeType`).

**Mark IDs are the linchpin.** A mark is a small per-episode integer that maps deterministically to a stable `(entity_index, serial)`. The *same* mapping drives both the rendered labels and the JSON telemetry, so "mark ⑦ in the image" === "mark 7 in the entity list" === a fixed engine handle. This is what makes Set-of-Marks prompting work here: the marks are anchored to engine handles, so they never drift.

---

## 4. The act grammar (meaningful, not just minimal)

The verbs map onto the *semantic affordances of a Portal chamber* — the vocabulary a human uses to describe a solution. The LLM emits one JSON action per step; the harness executes it (advancing the frozen sim), then returns the new percept + a structured result.

| Verb | Args | Executes as | Returns |
|---|---|---|---|
| `look_at` | `target` (mark / point / direction) | `SetAngles(VectorAngles(target − eye))` | new view |
| `go_to` | `target` (mark / point) | straight march on current platform + edge-safety (§5) | `{reached, stuck, final_dist}` |
| `go_to_edge` | `direction` (compass / relative) | raycast platform probe + march (§5.2) | `{reached, edge_type: void\|lower\|wall, dist}` |
| `shoot_portal` | `color`, `target` (mark face / point) | aim → `TraceFirePortal` validate → fire | `{placed, result: SUCCESS\|INVALID_SURFACE\|CANT_FIT\|OVERLAP\|FIZZLED, final_pos}` |
| `pick_up_cube` | `target` (mark) | face target, `key_use` edge | `{holding, held_mark}` |
| `release_cube` | — | `key_use` edge | `{holding:false}` |
| `press` / `interact` | `target` (mark) | `go_to` within reach + `key_use` | `{ok}` |
| `wait` | `ticks` | advance N ticks, no input (let physics/lasers settle) | new state |
| `done` | — | end episode (env checks success) | `{success}` |

Design notes:

- **Structured failure is a feature.** `shoot_portal` leans on `TraceFirePortal` (`src/Modules/Server.hpp:19`), which *validates* placement and returns rich codes (`src/Utils/SDK/PortalPlacement.hpp`). `INVALID_SURFACE` → the LLM learns "that wall isn't portalable, try the white panel." The feedback *is* a reasoning signal and enables error recovery.
- **Macros are blocking.** Each verb runs a closed loop for up to `max_ticks` then returns. The LLM operates at the macro timescale (seconds of thought), never at 66 Hz. Simpler than interruptible and fine for an eval.
- **Cube grab is already wired.** `key_use → IN_USE` exists end-to-end (`harness.proto:113`, `Portal2HarnessImpl.cpp:414`, `TasController`). `pick_up_cube`/`release_cube` are just `key_use` edges; no new RPC. **No engine "is-held" signal** — the LLM remembers what mark it grabbed (SOTA models track this trivially). The only thing a signal would add is disambiguating *identical* cubes if the agent loses track; descoped for v1.
- **Reflective cube** = `prop_weighted_cube` with `m_nCubeType==2` (auto-discovered field), not a separate classname.

### 4.1 The loop (the sketch, fleshed out)

```
reset() → percept₀ = {annotated_frame, player, entities}
loop:
    action_json = LLM(system_prompt, history, percept)      # think arbitrarily long; world frozen
    result, percept = harness.execute(action_json)          # sim advances, re-annotated
    if result.success or step_budget_exceeded: break
```

Example trace (a "portal across a gap to a button" chamber):

```jsonc
// percept: frame shows ① blue-portal-surface (far wall), ② button, ③ cube; gap east
{"action":"go_to_edge","direction":"E"}              // → {reached:true, edge_type:"void", dist:3.1}
{"action":"shoot_portal","color":"blue","target":{"mark":1}}   // → {placed:true, result:"SUCCESS"}
{"action":"look_at","target":{"direction":"down"}}            // floor at my feet is portalable
{"action":"shoot_portal","color":"orange","target":{"point":"under_self"}} // → SUCCESS
{"action":"go_to","target":{"mark":2}}               // walk into orange → exit blue across gap
{"action":"pick_up_cube","target":{"mark":3}}        // → {holding:true, held_mark:3}
{"action":"press","target":{"mark":2}}               // place cube on button
{"action":"wait","ticks":33}                          // door opens
{"action":"done"}                                     // → {success:true}
```

---

## 5. Locomotion via raycasting (the one real gap — no navmesh exists)

There is **no nav mesh / pathfinding** anywhere (only ground-entity + `CheckStuck`). We do **not** build A*. We build *crude, honest* nav cues from raycasts — and crude is correct here, because perfect pathing would smuggle chamber-solving into the actuators and re-confound the thing we're measuring. The cues are the *substrate* for spatial reasoning, not a solver.

### 5.1 The trace primitive (confirmed)

`Engine::Trace(Vector &pos, QAngle &angle, float distMax, int mask, CTraceFilterSimple &filter, CGameTrace &tr)` (`src/Modules/Engine.cpp:266`) is a **pure raycast query that renders nothing** (it is *not* the `PlayerTrace` ghost-line feature). Start point + direction angle + max distance → `CGameTrace` with `endpos`, `plane.normal`, `fraction`, `surface.flags`, hit `m_pEnt`. `CTraceFilterSimple::SetPassEntity` ignores the player's own hull. `pitch=+90` = straight down. It runs on the main thread; macros execute there (like `Act()` does), so the calls are valid. Existing precedent: `PlacementScanner` already calls `engine->TraceRay` with `MASK_SHOT_PORTAL`.

### 5.2 Platform-edge probe ("go to edge of current platform")

From the player's foot origin `P`, for each of N radial bearings θ, march outward in ~12u steps:

```
Q = P + dist·dir(θ)
wall:  Trace(eye, {0, θ}, dist, MASK_PLAYERSOLID) hits closer than dist → terminator = WALL @ dist
floor: Trace(Q + ε·up, {90, 0}, 256, MASK_PLAYERSOLID)
         no hit                  → EDGE→VOID @ prev
         |endpos.z − P.z| > Δz   → EDGE→LOWER(Δz) @ prev
         hit m_pEnt is prop_portal → FLOOR_PORTAL @ dist
```

Output = a **nav compass** per bearing: `{bearing, walkable_dist, terminator, beyond}`. `go_to_edge(dir)` marches to `edge_dist − margin`; `go_to(target)` straight-marches with the same edge guard. Cost ≈ 8 bearings × ~16 steps × 2 traces ≈ 250 traces, **only when a nav verb fires** (not per-tick) — negligible vs the snapshotter's <200µs budget.

**Honest breakage:** routing *around* an on-platform obstacle fails → macro returns `blocked: WALL @ X`, LLM copes ("there's a wall → I need a portal"). Steep stairs read as edges (Δz tuning). Moving floors ignored v1. Player-isn't-a-point → inflate the down-probe by bbox half-width or fire 2 parallel rays.

---

## 6. Observability modes (toggle)

Build global-list first (nearly free; data already on the wire), add an egocentric filter as an opt-in:

- **Global** — every puzzle entity is marked + listed. Trivial perception, cleanest *pure-reasoning* isolation. Risk: leaks info a player wouldn't have (e.g. a button in an unseen room).
- **Egocentric / in-frame** — frustum test (pure math from eye + angles + FOV) + one `TraceRay` LoS check per candidate; only visible entities get marks/telemetry. Matches "no global list," forces active perception (`look_at`/`go_to_edge` to discover). More research-honest, harder.

A `sar_harness_obs_mode` cvar flips it; same annotation/telemetry path, different filter. Lets us A/B whether perception is even a factor.

---

## 7. Where the code lives (reuse vs new)

**C++ (Harness side)**
- *Reuse:* `OverlayRender` (annotations), `EntitySnapshotter` (entity list + OBB), `Engine::Trace`/`TraceFirePortal`/`SetAngles`, `key_use→IN_USE`. All present.
- *New, small:* an **annotation Feature** (`sar_harness_annotate`) that each frame iterates target classnames from the snapshot and emits `addBoxMesh`+`addText` — patterned on `Camera.cpp`/`PlayerTrace.cpp` (~100–150 LOC).
- *New, medium:* a **macro executor** in the Harness — the closed-loop controllers for `go_to`/`go_to_edge`/`shoot_portal`/aim, using the trace+aim primitives. **Decision (§8):** these *must* be C++-side because the raycast/aim primitives aren't exposed to Python and precise aiming over the relative-mouse-delta action space is painful.
- *New, small:* extend the `AgentLoop` message with a `macro` oneof + `MacroResult` (or a separate `ExecuteMacro` RPC). Requires `make proto` + rebuild.
- *New, trivial:* configurable `max_think_seconds` + gRPC keepalive.

**Python**
- *New, small:* parse `EntitySnapshot` (currently discarded at `rl_challenge_env.py:125`) into the entity telemetry + mark mapping.
- *New, medium:* the **LLM driver** — system prompt, ReAct loop, JSON tool schema mirroring §4, percept formatting (frame + telemetry), success check, transcript logging.

---

## 8. Resolved decisions

1. **Macro execution is C++-side. ✅** All macros run in the Harness (closed-loop control with the trace/aim primitives). Python *cannot* do the raycast nav or `TraceFirePortal` validation (engine calls) and per-tick decomposition is chatty. **Addition:** Python holds a thin **macro validator / "lexer"** — it parses the LLM's JSON against the grammar *and the current entity list* (verb known? args well-typed? `color` valid? does `mark` exist + resolve to a real handle? is the target a sane type for this verb?) **before** sending the gRPC message. Malformed/unresolvable calls are rejected locally with a structured error fed straight back to the LLM, never burning a round-trip or a game step. This also centralizes the grammar's spec in one place (shared with the prompt's tool schema).
2. **Visual + symbolic, always. ✅** No symbolic-only condition (see §3 rationale — avoids the spatial-*imagination*-vs-*reasoning* confound). The `OverlayRender` annotation Feature is therefore part of the MVP, not a later phase.
3. **`shoot_portal` supports both targeting modes. ✅** `target: {mark}` snaps to the entity's portalable face (reasoning-level); `target: {point}` / `{screen}` takes a free world/screen point (fine spatial control). Mark-face is the default; both are validated against `TraceFirePortal`.
4. **Chambers are user-provided. ✅** The user has a specific chamber + a set of simple-enough chambers. We use those. *Needed when we build:* the map names (or `.bsp`/`.vmf`) + per-chamber target position(s) and success criterion.
5. **Evaluation only, not training. ✅** No reward shaping. Per episode we log: success/fail, step count, the full action+feedback transcript, and the annotated frames. That transcript *is* the deliverable for the perception-vs-cognition question.

---

## 9. Phased plan (quick diagnostic first)

- **Phase 0 — MVP: first visual+symbolic eval on the simplest chamber.** This already answers "is reasoning the bottleneck?"
  - *C++:* extend the `AgentLoop` message with a `macro` oneof + `MacroResult`; macro executor for `look_at` (SetAngles), `shoot_portal` (aim → `TraceFirePortal`, both target modes), `go_to` (straight march + edge guard), `press`/`interact`, `wait`; the `OverlayRender` annotation Feature (boxes + Set-of-Marks labels from snapshot OBB); configurable `max_think_seconds` + gRPC keepalive.
  - *Python:* `EntitySnapshot` parser + mark mapping; the macro **validator/lexer** (§8.1); LLM ReAct driver; percept formatter (annotated frame + player/entity telemetry); transcript logger.
- **Phase 1 — crude nav:** raycast platform-edge probe + `go_to_edge` + nav-compass telemetry + nav arrows drawn on the frame. (For chambers where edge awareness matters.)
- **Phase 2 — richer verbs + state:** `pick_up_cube`/`release_cube` (held-object signal, §10), laser/relay/catcher field-name verification + their `m_bPowered`/`m_bEnabled` state surfaced in telemetry and mark coloring.
- **Phase 3 — harden / scale:** egocentric observability toggle (depth-tested marks), the rest of the user's chamber set, robustness + run aggregation across chambers.

## 10. Risks / unknowns to verify empirically

- **Held-object signal — descoped.** The LLM tracks what it picked up in its own context. Future nicety only: distinguishing identical cubes A vs B, via whitelisting a player handle field or hooking the already-reversed `CPortal_Player::PollForUseEntity` (`src/Cheats.cpp:390-400`). Not in v1.
- **Laser "active/connected" field name** — `m_bEnabled` (emitter) vs `m_bPowered`/`m_bIsPowered` (catcher/relay). Snapshotter captures whatever the real name is; confirm via a snapshot dump on `sp_a2_laser_chaining`.
- **Overlay-in-SHM** — high-confidence from the hook location (overlays draw inside the captured view) but **verify against an actual captured frame** early.
- **Long-pause networking** — add gRPC keepalive before relying on multi-minute think times.
- **The accessibility mod** — confirmed Discord-distributed, no public source. Nothing to mine; `OverlayRender` already covers the visual-annotation capability (the laser-latch / funnel-assist parts were gameplay nudges, out of scope).
