# Macro Executor — Code-Grounded PR Plan (to First Light)

**Status (2026-06-11):** PR0–PR4 **built and validated against a live game**; Gate 2 passed — `testchamber_000`
(an auto-dropper cube→button→door chamber) solved by hand through the real executor via `py/macro_repl.py`.
**PR5 (entity parser + grammar + macro send) and PR6 (gRPC keepalive) have since landed; PR7 (the frozen-VLM
ReAct agent) remains — now split into 7a/7b/7c, with Gemini 3.5 Flash as the model (see §PR7).** Read
**§As-built carry-forward** below before PR7 — the
build diverged from this plan in a few load-bearing ways. The rest is the *detailed, code-grounded* build order for the
C++ macro executor + Python driver — the concrete version of Track C / Track D in
[`llm_percept_act_phased_plan.md`](llm_percept_act_phased_plan.md). Where this disagrees with the sketch
there, **this wins** for the executor; the phased plan stays the index. Decisions/altitude:
[`llm_act_grammar_altitude.md`](llm_act_grammar_altitude.md) (closed semantic verbs). First-light contract:
[`llm_percept_act_grammar.md`](llm_percept_act_grammar.md) §11.

**Target = first light (M2):** a frozen VLM solves a cube→button→door chamber (no portals, no lasers). The
critical-path verbs are `aim_at`, `go_to`, `pick_up_cube`, `release_cube`, `press`/`interact`, `wait`, `done`,
plus the exploratory pair `look` / `move` (folded into PR2/PR3 — cheap, and required for the egocentric A/B
though not to *solve* chamber 1 under global observability). **`shoot_portal` + nav-compass + save/load anchors
are explicitly post-first-light** (§Follow-on).

Each section: **Goal · Files · Changes (concrete) · Verify · Size · Deps · maps-to**. Eight PRs, PR0→PR7
(PR7 split into 7a/7b/7c — see §PR7).

---

## As-built carry-forward (2026-06-11)

PR0–PR4 are **done and validated end-to-end** (Gate 2: `testchamber_000`, an auto-dropper cube→button→door
chamber, solved by hand via `py/macro_repl.py`). Where the build diverged from the PR sections below — read
these before PR5–PR7:

- **Stable marks (MarkTable, was PR0's dense 1..N).** Dense `1..N` numbering shifted whenever the dropper spawned
  the cube mid-episode, so a mark-anchored verb would resolve to a *different* entity (a `pick_up 12` would chase
  what was the button). Replaced with **persistent per-entity marks** — assign once on first sight, append new
  entities, never renumber (the "stable identity hash" escape hatch PR0 anticipated). PR5's entity parser keys on
  these. Determinism for transcripts holds because new entities are appended in sorted (index, origin) order.
- **🔴 Reset must be a full `map` reload, not `restart_level`.** A PeTI dropper drops its cube via a **one-shot
  `OnMapSpawn`**; the soft `restart_level` (what `Reset` with an empty `map_name` runs,
  [Portal2HarnessImpl.cpp:514](src/Features/Harness/Portal2HarnessImpl.cpp#L514)) does **not** re-fire it, so the
  cube stays stuck in the dispenser at spawn height (~z=430) and is ungrabbable. **Any reset on a dropper chamber
  must pass the map name** (full reload). The clean fix for PR5/PR7 is a `full_reload` flag on `ResetRequest` so RL
  keeps fast `restart_level` while the eval forces a reload. `py/macro_repl.py` and `agentloop_smoke.py` already
  reset with the map name.
- **`+use` must be held >1 tick.** A 1-tick programmatic `+use` press is lost to a tick-advance/release race
  (exacerbated under `sv_alternateticks`), so `pick_up`/`release`/`interact` registered *nothing* (cube `moved=0`).
  `PulseUse` now holds `+use` for `kUseHoldTicks=3`. Also empirically settled: `kGrabRange=80` (engine
  `PLAYER_USE_RADIUS`), grab-confirm requires the object to actually move (`moved>kMinGrabMove`), `go_to` bleeds
  walk velocity at the end so the player doesn't coast onto the cube, `pick_up` gates on a grabbable class
  (cube/monster-box/turret), `release` auto-orients (look-down, or at a mark).
- **Verb renames:** `pick_up_cube`/`release_cube` → **`pick_up`/`release`** (generic — works on any grabbable prop);
  `press` folds into `interact`. The proto `verb` string and PR4/PR5/PR7 text should use the new names.
- **`py/macro_repl.py` = a human-driven PR7.** Interactive REPL over the executor (stdin instead of an LLM): typed
  verbs, `obs`/`reset [map]`/`save` (replayable transcript), up-arrow history. Its `build_macro` + verb table are a
  working prototype of PR5's `macro_grammar` single source; its loop is the shape PR7's `D3` reuses.
- **Gate 2 ✅** (was "between PR4 and PR7"): the canonical solve runs by hand. `agentloop_smoke.py::check_solve` is
  the automated version (presently red on cube *placement* on the plate — a solve-sequence tuning detail, not a
  verb bug; bake the hand-found sequence into it).
- **PR6 (keepalive) landed client-driven, not symmetric.** The frozen world idles for minutes between macros while
  the LLM thinks; the client (`harness.py` `_CHANNEL_OPTIONS`) pings to hold the stream open, the server
  (`Harness.cpp`) only *tolerates* those pings (`MIN_RECV_PING_INTERVAL=10s`) and disables idle/age close — no
  server-initiated pings, so no `too_many_pings` GOAWAY. Survival actually rests on the open AgentLoop stream being
  an outstanding RPC the server never closes; the pings are warmth + dead-peer detection. **`max_think_seconds` was
  NOT added** — it would be unused state here, so it moves to **PR7's driver**, where it bounds the real LLM call.

---

## 0. How the engine actually works (the three facts every PR depends on)

Verified by reading the code — these pin every design choice below.

1. **Input = the TAS framebulk, not raw keys.** `Act()` writes the action into
   `tasPlayer->playbackInfo.slots[0].framebulks[0]` — `moveAnalog{x,y}`, `viewAnalog{dx,dy}`,
   `buttonStates[TasControllerInput]` ([Portal2HarnessImpl.cpp:434-439](src/Features/Harness/Portal2HarnessImpl.cpp#L434-L439)).
   Each tick, `SteamControllerMove`→`TasController::ControllerMove`→`TasPlayer::FetchInputs` reads framebulk[0]
   and hydrates the `CUserCmd` (`moveAnalog`→`forwardmove/sidemove` + `IN_FORWARD…`; `buttonStates[Use]`→`IN_USE`;
   `buttonStates[FireBlue]`→`IN_ATTACK`; `viewAnalog`→`mousedx/dy` **and** `engine->SetAngles(slot, viewangles)`).
   Buttons: `Jump,Crouch,Use,Zoom,FireBlue,FireOrange,…` ([TasController.hpp:11-21](src/Features/Tas/TasController.hpp#L11-L21)).
2. **Aiming must be absolute via `engine->SetAngles`, never the analog delta.** `viewAnalog` is a *relative*
   mouse delta — useless for precise aim. The macro sets the view with
   `engine->SetAngles(slot, Math::VectorAngles(target − eyePos, {0,0,1}))`
   ([Engine.cpp:151](src/Modules/Engine.cpp#L151), [Math.cpp:94](src/Utils/Math.cpp#L94)) and **zeroes
   `framebulk.viewAnalog`** so `ControllerMove`'s `viewangles -= viewAnalog` (Δ=0) preserves it. *(Verify this
   preservation empirically in PR2 — it's the one aim assumption.)*
3. **The world is frozen; ticks step one at a time off the main thread.** After a 256-tick warmup
   `PRE_TICK` calls `engine->SetAdvancing(true)` (pause) and sets `harnessControlActive`
   ([Harness.cpp:264-279](src/Features/Harness/Harness.cpp#L264-L279)). `Act()` sets `ticksRemaining=N`,
   dispatches `engine->AdvanceTick()` ×N via `Scheduler::OnMainThread`, and **blocks on `tickCV`**;
   `PRE_TICK` decrements `ticksRemaining` and notifies at 0
   ([Portal2HarnessImpl.cpp:427-451](src/Features/Harness/Portal2HarnessImpl.cpp#L427-L451),
   [Harness.cpp:283-290](src/Features/Harness/Harness.cpp#L283-L290)).
   **Crucial consequence:** ticks run *after* the dispatched lambda returns, so a closed-loop macro (re-aim /
   re-check each tick) **cannot** observe results inside one lambda — it must iterate on the gRPC thread:
   `read+decide+set-framebulk (main thread)` → `advance B ticks + wait (tickCV)` → repeat.

**Threading rule (non-negotiable):** every engine read/write — `abs_origin`, `collision()`, `Trace`,
`TraceFirePortal`, `SetAngles`, framebulk writes, `AdvanceTick` — runs on the **main thread** via
`Scheduler::OnMainThread`. The macro *loop* lives on the gRPC thread (like `Act`) and dispatches into it.

---

## PR0 — Infra: tick helpers + canonical MarkTable (do this first)

> The foundation. The two helpers are pure refactors; the MarkTable change is an intentional, **benchmark-critical**
> behavior change pulled in here because PR0 already touches `MarkTable` and *everything downstream must build on
> stable, reproducible marks.*

- **Goal:** factor the two patterns the executor needs out of existing code (no behavior change), **and** make mark
  numbering deterministic + add reverse lookup.
- **Files:** `Portal2HarnessImpl.{hpp,cpp}`, `MarkTable.{hpp,cpp}`.
- **Changes:**
  - `void AdvanceTicksBlocking(int n)` — extract [Portal2HarnessImpl.cpp:427-451](src/Features/Harness/Portal2HarnessImpl.cpp#L427-L451)
    (`ticksRemaining=n` → `OnMainThread{ for n: AdvanceTick() }` → `tickCV.wait`). Refactor `Act()` to call it.
  - `template<class F> auto RunOnMainThreadSync(F&& fn)` — extract the atomic-flag + spin-wait +
    `context->IsCancelled()` pattern from the pixels read
    ([Portal2HarnessImpl.cpp:594-611](src/Features/Harness/Portal2HarnessImpl.cpp#L594-L611)) so macros can do
    **synchronous main-thread reads** (player/entity origin, traces) and bail on a dropped stream.
  - **🔴 Canonical mark numbering (the A3-revision fix — was deferred, now critical).** Today `GetMark` assigns
    `nextMark++` *on first sight* ([MarkTable.cpp:11](src/Features/Harness/MarkTable.cpp#L11)) — **observation-order-dependent**,
    so the *same chamber* can produce *different* numbering across runs, breaking transcript comparability/replay (a
    benchmark requirement, not just a save/load nicety). Replace with a deterministic recompute: a `RebuildFromWorld()`
    that each observe gathers the category-A entities, **sorts by spawn `entity_index`** (rounded spawn-origin as
    tiebreak for dropper-spawned cubes), numbers `1..N`, and fills **both** the forward (entity→mark) and reverse
    (mark→entity) maps in one pass. `GetMark` / the new `GetEntityFromMark(mark) -> {index,serial}` (returns `{-1,0}`
    if absent) both read from it. *Caveat to note in code:* dense `1..N` shifts if an entity spawns/despawns
    mid-episode — fine for static hand-authored chambers (first light + early suite); escape hatch later = mark as a
    stable identity hash if dynamic spawning becomes common.
- **Verify:** builds; existing `Act`/`AgentLoop` round-trip still works (run the RL env smoke path, or a manual
  `Act` from a script) — identical behavior. **Marks reproducible:** load the same chamber twice → identical
  mark→entity assignment (compare two `Observe` dumps); marks stay fixed across a save/load cycle.
- **Size:** ~120 LOC. **Deps:** none. **maps-to:** new infra + the A3-revision MarkTable fix (prereq for C-track).

---

## PR1 — Proto: `MacroRequest`/`MacroResult` + `mark` in telemetry + dispatch stub

- **Goal:** the wire format + routing exist; mark is in telemetry; executor is a stub. (Folds C1 + C7.)
- **Files:** `harness.proto`, `Portal2HarnessImpl.cpp`, then `make proto` (commit regenerated C++ **and** Python stubs).
- **Changes (proto — flat verb for v0; KISS):**
  ```proto
  message MacroRequest {            // one closed semantic verb per step
    string verb  = 1;              // aim_at|look|go_to|move|pick_up_cube|release_cube|press|interact|wait|done
    int32  mark  = 2;              // anchored verbs: target mark (0 = unset)
    int32  ticks = 3;             // wait() AND move() — hold duration in ticks
    string dir   = 4;             // move(): forward|back|left|right (relative to facing)
    int32  yaw   = 5;             // look(): signed degrees, lexer-snapped to 15°
    int32  pitch = 6;             // look(): signed degrees, lexer-snapped to 15°, engine-clamped
    // P1 (portals/nav): string color; Vector3 point; string direction;
  }
  message MacroResult {
    bool   ok          = 1;
    string result_code = 2;       // SUCCESS|STUCK|BLOCKED|UNREACHABLE|BAD_MARK|EDGE|WALL|COMPLETED|NOT_IMPLEMENTED|…
    string detail      = 3;
    bool   reached     = 4;       // go_to
    float  final_dist  = 5;       // go_to
    float  moved_dist  = 6;       // move (so the model calibrates ticks→distance from feedback)
  }
  ```
  - `AgentMessage`: add `MacroRequest macro = 3;` (alongside `action=1`; message-field presence → check
    `req.has_macro()`, **no oneof needed**, no break to existing `action` path).
  - `EnvironmentMessage`: add `MacroResult macro_result = 4;`. The **refreshed percept rides on the existing
    `GameState state`** (AgentLoop already calls `Observe`) — no duplication.
  - **Pixel copy on macro completion + on step 0:** the macro's final `Observe` must copy the (annotated)
    framebuffer to SHM so the driver gets a fresh visual percept each step. Today the copy is gated on
    `AgentMessage.copy_pixels_to_shm` ([Portal2HarnessImpl.cpp:591](src/Features/Harness/Portal2HarnessImpl.cpp#L591));
    honor that flag on the macro branch too (model sets it once per macro), reusing the existing main-thread
    `ReadScreenPixels` path. **Also** trigger the copy on `Reset`'s `initial_state` so step 0 has an image.
  - `EntityState`: add `int32 mark = 10;` (1-9 taken; `deleted=9`). Populate in `PopulateEntityStateProto`
    after `set_target_name` ([Portal2HarnessImpl.cpp:98](src/Features/Harness/Portal2HarnessImpl.cpp#L98)):
    `protoState->set_mark(markTable.GetMark(entityIndex, slot.serial));` — this is the frame↔telemetry bridge.
  - `AgentLoop`: after the `req.action()` branch ([Portal2HarnessImpl.cpp:566](src/Features/Harness/Portal2HarnessImpl.cpp#L566)),
    add `else if (req.has_macro()) { ExecuteMacro(&req.macro(), env_msg.mutable_macro_result()); }` →
    stub returns `{ok:false, result_code:"NOT_IMPLEMENTED"}`, then `Observe` fills `state`, then `Write`.
- **Verify:** a hand-crafted macro round-trips → `NOT_IMPLEMENTED`; the `mark` in telemetry == the on-screen
  Set-of-Marks label for the same entity (visual check on the chamber).
- **Size:** ~90 LOC + proto. **Deps:** PR0. **maps-to:** C1 + C7.

---

## PR2 — Executor skeleton + `aim_at` + `look` + `wait` + `done`

> **⚠ Spike first (5 min, any map):** confirm `engine->SetAngles` from the macro **survives** `ControllerMove`'s
> per-tick re-apply (it does `viewangles -= viewAnalog; SetAngles(viewangles)` every tick). Set angles → advance
> 1 tick → read angles back. If they hold → `aim_at`/`look` work as written. If `ControllerMove` clobbers them →
> aim must be driven through a computed `framebulk.viewAnalog` delta instead. This is the highest-uncertainty
> assumption in the plan (fact §0.2); resolve it before writing the aim verbs. (`aim_at` and `look` share this
> path, so this spike de-risks both.)

- **Goal:** the executor exists; the look/wait/done verbs work; mark→entity resolution is proven.
- **Files:** new `src/Features/Harness/MacroExecutor.{hpp,cpp}`; `Portal2HarnessImpl.cpp` (wire `ExecuteMacro`).
- **Changes:**
  - `MacroResult MacroExecutor::Execute(const MacroRequest&)` on the gRPC thread.
  - **Mark→entity resolver** (`RunOnMainThreadSync`): `GetEntityFromMark(mark)` → `(index,serial)` →
    `GetEntityInfoByIndex(index)`, **verify `m_SerialNumber==serial`** (reuse safety), cast `m_pEntity`→`ServerEnt*`,
    read `se->abs_origin()` + OBB center (`abs_origin + ½(OBBMins+OBBMaxs)` rotated by `abs_angles`, per
    [EntitySnapshotter.cpp:334-371](src/Features/Harness/EntitySnapshotter.cpp#L334-L371)). Bad/stale mark →
    `{ok:false, result_code:"BAD_MARK"}` (never crash).
  - **`aim_at(mark)`** (anchored, precise — *was* `look_at`)**:** main-thread: `eye = se_player->abs_origin() +
    GetViewOffset(player)`; `QAngle a = Math::VectorAngles(targetCenter − eye, {0,0,1})`;
    `engine->SetAngles(GET_SLOT(), a)`; zero `framebulk.viewAnalog`; then `AdvanceTicksBlocking(1)`. Return new view.
  - **`look(yaw, pitch)`** (exploratory, coarse)**:** main-thread: read current view angles; `a = current +
    {pitch, yaw, 0}` (degrees already 15°-snapped by the Python lexer); **clamp pitch** to engine limits
    (`±~85°`, report the clamp in `detail`); `SetAngles`; zero `viewAnalog`; `AdvanceTicksBlocking(1)`. Same
    SetAngles path as `aim_at` — ~15 incremental LOC. Returns new view (egocentric mode refreshes visible marks).
  - **`wait(ticks)`:** clamp to `[1, kMaxWaitTicks]`; `AdvanceTicksBlocking(ticks)`.
  - **`done`:** return `{ok:true, result_code:"DONE"}` immediately (real success = exit-proximity, decided
    Python-side per §11.4).
  - Precondition (mirror `Act`): require `harnessControlActive` ([Portal2HarnessImpl.cpp:390](src/Features/Harness/Portal2HarnessImpl.cpp#L390)).
- **Verify:** `aim_at mark=N` points the crosshair at N (box centered in the annotated frame); `look yaw=30`
  rotates the view 30°; `wait` advances; `done` returns. **Confirm SetAngles survives the tick** (fact §0.2).
- **Size:** ~135 LOC. **Deps:** PR1. **maps-to:** C2 (+ `look`, wait/done from C6).

---

## PR3 — `go_to` (anchored) + `move` (exploratory), on a shared march

> `go_to` and `move` are the **same march loop + same edge/wall guard**; they differ only in what they aim at and
> when they stop. Factor a shared `MarchStep(forward, edgeGuard)` helper; `go_to` drives it toward a mark until
> reached, `move` drives it for a fixed number of ticks. One implementation, two verbs (the locomotion analogue of
> PR4's one-Use-pulse-three-verbs).

- **Goal:** anchored walk-to-mark and free directional move, both edge-safe. The local/global line in code.
- **Files:** `MacroExecutor.cpp`.
- **Changes:**
  - **`go_to(mark)`** — closed loop, `kGoToTickBatch`(≈4) ticks/iter, ≤ `kGoToMaxTicks`(≈400):
    1. main-thread read: player origin, target origin, `dist = horizontal‖target−player‖`.
    2. **terminate** if `dist ≤ kReachRadius` (`reached`), or stuck (origin moved `< kStuckEps` over 2 iters →
       `STUCK`), or **edge-guard** trip → `BLOCKED`.
    3. else main-thread set: yaw-only `SetAngles` toward target (level pitch), `framebulk.moveAnalog={0,1}`
       (forward; analog is unit-normalized so just `1`), zero `viewAnalog`; then `AdvanceTicksBlocking(kGoToTickBatch)`.
    - Return `{reached, final_dist, result_code}`.
  - **`move(dir, ticks)`** — hold a move key for `ticks` (the model's chosen duration), edge/wall-guarded:
    main-thread set `framebulk.moveAnalog` for `dir` (`forward={0,1}`, `back={0,-1}`, `left={-1,0}`,
    `right={1,0}`, **relative to current facing**; camera unchanged → strafes keep eyes fixed), `viewAnalog=0`;
    advance in `kGoToTickBatch` slices up to `ticks`, checking the edge/wall guard each slice; stop early on the
    guard. Return `{moved_dist = ‖final−start‖, stop_reason: COMPLETED|EDGE|WALL|STUCK}` so the model
    **calibrates ticks→distance from feedback** (no client-side speed math).
  - **Edge/wall guard (shared, minimal for flat first-light chamber):** before each slice, `Engine::Trace` a
    short ray forward at foot height → blocked = `WALL`; and down from a point `kStepAhead` ahead → no floor
    within `kStepDownMax` = `EDGE` (`MASK_PLAYERSOLID`, [Engine.cpp:266](src/Modules/Engine.cpp#L266)). Full
    fan-of-rays nav-compass is C5/§Follow-on.
  - **Local-only** (grammar §4): real `CUserCmd` sim; **no global routing** — a real blocker just stops and
    reports (reasoning signal). No portal traversal yet (no portals in first light).
- **Verify:** `go_to mark=cube` → `go_to mark=button` walks and stops within `kReachRadius`; `move forward ticks=20`
  advances ~proportionally and returns `moved_dist`; neither marches into a pit (returns `EDGE`).
- **Size:** ~150 LOC. **Deps:** PR2. **maps-to:** C4 (+ `move`).

---

## PR4 — `pick_up_cube` / `release_cube` / `press` / `interact`

- **Goal:** the interaction verbs — completes the cube→button→door canonical solve (grammar §11.1).
- **Files:** `MacroExecutor.cpp`.
> **One primitive, three semantic verbs.** `pick_up_cube` / `release_cube` / `press` are *all* "face a mark, pulse
> `IN_USE`" — the engine's `+use` toggles grab/drop/activate by world state. So they share **one** Use-pulse helper
> (KISS at the executor layer). They stay distinct **verbs** only because intent + affordance-checks + result-shapes
> differ (the reasoning signal). Because there's **no engine "is-held" field** (grammar §10), each must
> **affordance-check before** and **confirm after** — else a grab that missed silently "succeeds."

- **Changes:**
  - **Use-pulse helper** (buttons are held for *all* advanced ticks, so pulse explicitly): main-thread set
    `buttonStates[Use]=true` → `AdvanceTicksBlocking(1)` → main-thread set `false` → `AdvanceTicksBlocking(kSettle)`.
  - **`pick_up_cube(mark)`:** affordance precheck → reject `ALREADY_HOLDING` (model-tracked held-state),
    `NOT_A_CUBE` (class ≠ `prop_weighted_cube`), `OUT_OF_REACH` (cube dist > `kGrabRange`). Then `aim_at(mark)` →
    Use-pulse → **confirm** the grab: over the next ~`kSettle` ticks the cube should track the player (its
    position holds ≈constant in front of the eye); if not → `GRAB_FAILED`. Success → `{ok, result_code:"SUCCESS",
    held_mark: mark}` (held-state tracked model/Python-side, grammar §10). *(Uses `aim_at(mark)` to face the cube.)*
  - **`release_cube`:** reject `NOT_HOLDING` if nothing held; else Use-pulse → `{ok, holding:false}`.
  - **`press` / `interact`(mark):** `go_to` within `kReachRadius` → `aim_at(mark)` → Use-pulse → `{ok}`.
    *(Not used by the recommended weighted-floor-button first-light chamber, §11.6 — only for a pedestal button.)*
- **Verify:** grab the cube → `go_to` button (carrying it) → `release_cube` on button → `wait 66` → door opens →
  exit reachable (the §11.1 trace runs end-to-end **manually from a script**, no LLM yet).
- **Size:** ~90 LOC. **Deps:** PR3. **maps-to:** C6.

---

## PR5 — Python: entity parser (D1) + macro send + validator (D2)

- **Goal:** the thin client — parse the percept, send macros, reject malformed verbs locally before a round-trip.
- **Files:** new `py/p2harness/entities.py`, new `py/p2harness/macro_grammar.py`, `py/p2harness/harness.py`.
- **Changes:**
  - **D1 `entities.py`:** `parse_snapshot(GameState) -> list[dict]` → `{mark, class, name, pos, dist, bearing,
    state{...}}`, **keyed by the `mark` field** (PR1), `dist`/`bearing` derived from player pos. (Replaces the
    discard at [rl_challenge_env.py:125](py/rl_challenge_env.py#L125).) *Marks are integers — no string aliases.*
  - **D2 `macro_grammar.py`:** **one schema = single source of truth** for both the LLM tool spec and local
    validation. `validate(macro_json, entity_list) -> MacroRequest | error_str`: verb known? args well-typed?
    `mark` exists in the live entity list and is a sane type for the verb (e.g. `pick_up_cube` ⇒ a cube)?
    `ticks` in range? Reject → structured error string fed back to the LLM, **no gRPC, no game step** (grammar §8.1).
  - **`harness.py`:** `step_macro(macro_req, timeout) -> (MacroResult, GameState)` — build `AgentMessage(macro=…)`,
    reuse `step_agent_loop` ([harness.py:149](py/p2harness/harness.py#L149)). **Per-verb timeout** must exceed the
    server tick budget (a `go_to` of ~400 ticks ≫ the default 5.0s) — pass generous timeouts or disable.
  - **Also lands here** (detail → §Other first-light needs): the **semantic state projection** table (raw
    `EntityField`s → `{cube_type, on_button, pressed, open:null}`) inside D1; and **driver-side held-state**
    threaded into `validate` so `ALREADY_HOLDING`/`NOT_HOLDING` can be checked client-side.
- **Verify:** from a Python script, send each verb on the chamber; percept parses (incl. projected `state`);
  validator rejects a bad/absent mark **and** an `ALREADY_HOLDING` grab locally with a structured message.
- **Size:** ~200 LOC. **Deps:** PR4. **maps-to:** D1 + D2.

---

## PR6 — gRPC keepalive + configurable think-time

- **Goal:** the world can stay frozen for minutes (long LLM deliberation) without the stream dropping.
- **Files:** `Harness.cpp` (`ServerBuilder` keepalive / max-connection-age), `harness.py` (channel args).
- **Changes:** set keepalive + large/disabled max-connection-age both ends (none configured today,
  [Harness.cpp:171-199](src/Features/Harness/Harness.cpp#L171-L199)); make client `max_think_seconds` a param.
- **Verify:** submit a macro, idle 5+ min, response still arrives.
- **Size:** ~30 LOC. **Deps:** PR1. **maps-to:** C8. *(Independent — can land any time after PR1.)*

---

## PR7 — frozen-VLM ReAct agent on a chamber-solve session → ⭐ FIRST LIGHT (7a → 7b → 7c)

PR7 is split into three small PRs. The driver, the REPL (`macro_repl.py`), and the smoke test all sit on **one
shared chamber-solve session**, so the human-driven and model-driven loops share a core — only the *action source*
differs (typed stdin vs the model). The model is **Gemini 3.5 Flash**.

**Decisions pinned (apply across 7a–7c):**

- **Model = `gemini-3.5-flash`**, chosen for best-in-class multimodality — the perception channel won't be the
  bottleneck, so a failure reads as reasoning, not "couldn't see the marks" (the *fairer eval*). ~$1/session with
  caching; free tier for early iteration (the daily request cap, not cost, is the real limit — a paid key removes
  it and still totals ~$300 over a full dev cycle). The action source is abstracted (one callable) so swapping in
  Opus/GPT for a cross-check on headline runs is a config change, not a rewrite.
- **Stateful single session** (`client.chats.create`), full history kept. The chat API is stateless, so a naive
  resend is quadratic in steps; keeping one byte-stable session triggers **implicit context caching** (the repeat
  bills at ~0.1×). On the free tier only rate/quota grows, not a bill. A **bounded last-K-frames window** is a
  backstop, added only if rate limits bite — not pre-built.
- **Send a bounded context to the model, serialize everything to disk losslessly** — two separate concerns. This
  is why the cost shape and the trajectory format are decoupled.
- **No `temperature`** — Gemini 3.5 Flash controls depth via `thinking_level`; temp-0 reproducibility isn't
  available (also gone on Opus 4.8). Transcripts are best-effort deterministic.
- **The "box" (verb extraction) = structured output**, not prose parsing: a `response_schema` derived from
  `macro_grammar.tool_schema()` (single source — already built, with `verb_signatures()` for the prompt) plus a
  `reasoning` field. `validate()` stays the semantic gate (mark exists / grabbable / held-state); a rejection is
  re-prompted *in-session*, capped.
- **The action source is an `agent`** — a callable `next_action(observation) -> action`. A *scripted agent* (7b)
  replays a verb list; the *Gemini agent* (7c) calls the model. This is the only seam; no class hierarchy. (The
  session is action-agnostic, so a raw-input agent is a future drop-in.)
- **Serialization = a single binary `.trajectory` per run** — a Python-only protobuf mirroring `.rollout`: an
  `LlmTrajectoryHeader` + length-delimited `LlmStep`s, with **frames embedded as PNG bytes (no loose files)** and
  every field captured (reasoning, raw model response, action, `MacroResult`, marks, token usage). `--dump-json`
  is an optional text lens; the binary stays primary.

### PR7a — chamber-solve session core + REPL on top

- **Goal:** factor the launch/handshake/reset/step/observe loop shared by the REPL, smoke test, and future driver
  into one action-agnostic session.
- **Files:** new `py/testchamber_session.py`; rewrite `py/macro_repl.py`.
- **Changes:**
  - `TestChamberSession` — wraps `P2Harness` + `WorldView` + held-mark + current map. `reset(map=None)` (full
    reload + prime), `step(macro, capture_frame=False) -> Observation`, `observe(capture_frame=False)`, `close()`.
    Owns held-state updates (`pick_up`/`release` SUCCESS) and the `copy_pixels` toggle. `step` is shaped to take a
    raw `ActionRequest` later without a rename — nothing in the type names is macro-specific.
  - `Observation` — `result`, `marks`, `state`, `frame` (ndarray|None), `held_mark`, `player`, `tick`.
  - `launch_or_attach(...)` helper (dedups the boot/handshake boilerplate copy-pasted across `macro_repl.py` and
    `agentloop_smoke.py`). The `reached_exit` success check lands in 7b, with the driver that uses it.
  - `macro_repl.py` → thin stdin loop over the session; `build_macro`, the verb table, `dump_marks`/`report`,
    `save`/`obs`/`reset`, and up-arrow history are all preserved. The old `send`/boot/handshake/reset boilerplate
    moves into the session.
- **Verify:** `macro_repl.py --attach < success.txt` still solves the canonical chamber. Pure refactor.
- **Size:** ~180 LOC. **Deps:** PR4/PR5.

### PR7b — binary `.trajectory` format + the REPL as the manual recorder

- **Goal:** the lossless trajectory format, plus a way to produce trajectories **by hand** — the human is the
  agent, no model and no transcript replay. A trajectory captures action + observation (+ frame) per step; the
  model-only fields are mocked.
- **Files:** new `py/llm_eval/trajectory.proto`, `py/llm_eval/trajectory_io.py`; `--record`/`--exit`/`--radius` on
  `macro_repl.py`; `reached_exit` on `testchamber_session.py`.
- **Changes:**
  - `trajectory.proto` (**Python-only** — never crosses gRPC, so NOT in `harness.proto`): `TrajectoryHeader`
    (map, exit_pos, success_radius, model, system_prompt, grammar) + `Step` (index, `frame_png` bytes,
    percept_json, player, reasoning, raw_response, action+result as serialized harness protos, held_mark,
    `TokenUsage`, terminal). Compile via `make proto_py`; the generated stub is out of the lint gate.
  - `trajectory_io.py`: `TrajectoryWriter` (length-delimited append, per-record flush), `read_trajectory`,
    `make_step(obs, macro, ...)`. `reached_exit(obs, exit_pos, radius)` lives with `Observation`.
  - `macro_repl.py --record FILE [--exit x,y,z --radius R]`: each verb step captures a frame and writes a `Step`;
    the typed command is the `reasoning`, the model fields (`raw_response`, `TokenUsage`) are mocked (usage = -1),
    and a step is marked `SOLVED` when it reaches the exit. Frame capture needs a video-mode instance (SHM); if
    absent, `--record` disables itself with a warning.
- **Verify:** hand-solve a chamber in the REPL with `--record`, read the `.trajectory` back — frames embedded,
  action + observation per step, mocked model fields, `SOLVED` on the exit step.
- **Size:** ~160 LOC + proto. **Deps:** PR7a.

**As-built (7b):**

- **No scripted replay, no `run_trajectory.py`, no `driver.py`.** Execution isn't tick-perfect, so replaying a saved
  transcript has no value — the REPL (human agent) records the trajectory directly, and the model agent loop is
  built in 7c where it's actually exercised. `make_step` / `TrajectoryWriter` / `reached_exit` are the shared core
  both the REPL and 7c use.
- **`VERBS` + `build_macro` moved to `macro_grammar`** (`VERBS = frozenset(VERB_SPECS)`) — the eval code no longer
  imports the REPL script.
- **bagz rejected:** no `cp314` wheel (only cp310–313), unusable on `requires-python >=3.14`. Kept the repo's
  `.rollout`-style length-delimited framing (`struct '<I'`) — zero new deps, crash-safe (per-record flush →
  readable prefix), reader tolerates a truncated tail.

### PR7c — Gemini 3.5 Flash agent → ⭐ FIRST LIGHT

- **Goal:** the frozen VLM drives a chamber to a `.trajectory`; the first perception-vs-reasoning signal.
- **Files:** new `py/llm_eval/gemini_agent.py` + the model agent loop (built here, reusing `trajectory_io` +
  `reached_exit` + `make_step`). Adds `google-genai`; loads `GEMINI_API_KEY` from the repo `.env`.
- **Changes:**
  - `GeminiAgent` — one `client.chats.create(model="gemini-3.5-flash", config=...)` per trajectory,
    `thinking_level=HIGH`, system instruction from `verb_signatures()` + task + percept-format. `next_action(obs)`
    sends `[frame_png, percept_text]`, extracts the action via structured output (`response_schema` from
    `tool_schema()` + `reasoning`), and captures the thinking trace + raw response + token usage. A `validate()`
    rejection is re-sent in the same chat (cap retries).
  - Per-chamber config (`map`, `exit_pos`, `success_radius`, step budget) — a tiny dict the driver loads.
  - Confirm at the top of this PR against current `google-genai` docs: structured-output ↔ `thinking_level` ↔
    `chats` composition; inline image part; how the thinking trace surfaces. The only SDK unknown — and 7b is built
    so it can't block the harness.
- **🚦 Gate 1 (before running the model):** eyeball the embedded step-0 annotated frame for boxes+marks. If the
  overlay isn't in the captured frame, fix the capture/annotation path first — the visual channel depends on it.
  (Gate 2 is PR7b's scripted solve.)
- **Verify:** point at the cube→button→door chamber, run the frozen VLM, read the `.trajectory`.
- **Size:** ~150 LOC. **Deps:** PR7b + your chamber config. **maps-to:** D3 + D4.

**As-built (7c)** — implemented + offline-verified (mocked chat); the real run is the user's:

- **Files:** `py/llm_eval/gemini_agent.py` (`GeminiAgent` + the `run_eval` loop + `AgentAction`) and the runner
  `py/run_eval.py`. google-genai 2.8.0 in pyproject; `GEMINI_API_KEY` from `.env` (and `.env.example`). The SDK
  matched the pseudocode (`ThinkingLevel.HIGH`; `response_schema` accepts the grammar's `tool_schema()` dict;
  `Part.from_bytes`; `usage_metadata`).
- **The "box" = JSON mode + `validate()` + in-session re-prompt** (capped). `response_schema` is best-effort
  structure; `validate()` against the live percept is the real gate, so a partially-honored schema is harmless.
- **The model is given the exit** (dist + bearing in the percept) — a tunable choice that keeps the eval about
  navigation+reasoning rather than blind exit-finding. Drop the exit line from `_percept_text` to test the latter.
- **Each Step records the observation the model SAW + its action + that action's result** (not the post-action
  frame), so reasoning lines up with the frame it concerned. Outcome stamped on the last step.
- **Cost shape (flagged, not optimized):** the stateful chat re-sends every prior frame each turn (quadratic) and
  `thinking_level=HIGH` drives output tokens. Fine for a one-off first-light run; on the free tier a ~25-step run
  can hit rate limits. Bounded frame window / lower thinking are the levers when scaling.
- **Run:** `uv run python py/run_eval.py --map M --exit x,y,z --radius R` (needs a video-mode instance + the key).
  Gate 1 (eyeball the step-0 annotated frame for boxes+marks) still applies before trusting a result.

---

## Dependency graph

```
PR0 ─► PR1 ─► PR2 ─► PR3 ─► PR4 ─► PR5 ─► PR7a ─► PR7b ─► PR7c  ★ first light
                 └► PR6 (any time after PR1) ──────────────────┘
```
PRs 0-4 are **C++ only** (SAR-first, per the repo rule); 5 and 7a-7c are Python; 6 is infra. Everything 0-4 and
the 7a/7b scripted path is demonstrable **without any LLM** — only 7c calls the model.

## Cross-cutting decisions (pin once, apply everywhere)

- **Marks are integers**, bridged by `EntityState.mark` (PR1); Python keys the entity list by it.
- **Aim = `SetAngles` absolute + zero `viewAnalog`**; never the analog delta (fact §0.2).
- **Buttons pulse** (1 tick on, then off) — they're held for the whole advance otherwise.
- **Held-cube state is model-tracked**, not an engine field (grammar §10).
- **Tunables (`kReachRadius`, tick batches/budgets, `kSettle`, edge thresholds) = C++ consts for v0** (KISS);
  promote to cvars only if a chamber needs it.
- **Cancellation:** the macro loop checks `context->IsCancelled()` each iter (via `RunOnMainThreadSync`) so a
  client timeout / dropped stream aborts cleanly mid-macro — return a partial `MacroResult`, never wedge `tickCV`.
- **`MacroResult.result_code` is the reasoning signal** (`STUCK`/`BLOCKED`/`BAD_MARK`/… ; later `INVALID_SURFACE`)
  — keep it informative and machine-parseable (grammar design principle).

## Other first-light needs (beyond the verbs + the chamber)

These are *not* verbs and easy to forget, but first light fails without them. Each lands in an existing PR
(noted) — surfaced here so none slips.

**Visual percept must actually reach the VLM (the riskiest, least-obvious half):**
- **🔴 Overlay-in-SHM verification — pre-flight gate (grammar §10 risk).** The whole visual percept assumes
  `OverlayRender`'s boxes + Set-of-Marks labels land *inside* the captured SHM framebuffer. High-confidence from
  the hook location but **unverified against a real captured frame.** Capture one annotated frame from SHM and eyeball
  it **before PR7** — if overlays aren't in the copy, the visual channel is broken and nothing downstream matters.
- **Annotation must be ON during the eval.** `sar_harness_annotate 1` set via the harness autoexec (or the driver)
  so every captured frame is annotated. Trivial, but a silent killer if missed. → PR7 setup / autoexec.
- **Frame resolution + label legibility.** SHM read is at `GetScreenSize` (~854×480, [Portal2HarnessImpl.cpp:596-604](src/Features/Harness/Portal2HarnessImpl.cpp#L596-L604));
  the VLM percept is nominally 224×224. **Check the marks are still readable after downscale** — if not, either
  feed the VLM a larger frame or enlarge the label `x_height`. → verify in PR7; cheap to fix in the annotation feature.
- **Pixels on every macro's final frame _and_ on the initial post-reset observation.** The model needs a fresh
  annotated frame each step *including step 0*. Macro-completion copy is in PR1; **also ensure `Reset`'s
  `initial_state` triggers a pixel copy** (today the copy is AgentLoop/flag-gated) or step 0 has no image. → PR1.

**Driver / percept plumbing (Python):**
- **Semantic state projection** (raw `EntityField`s → `{cube_type, on_button, pressed, open:null}`): a small
  per-class projection table (`m_nCubeType`→cube_type, `m_bButtonState`→pressed, …, grammar §11.2). The entity
  telemetry is *half* the percept — this is a real D1 deliverable, not a one-liner. → PR5.
- **Driver-side held-state tracking.** The validator needs "am I holding?" to reject `ALREADY_HOLDING`/`NOT_HOLDING`.
  The driver holds a `held_mark`, updated from `pick_up_cube`/`release_cube` results, passed into `validate`. → PR5.
- **Invalid-output re-prompt loop with a cap.** Model emits non-JSON / unknown verb / unresolved mark → validator
  rejects locally (no game step) → re-prompt with the structured error → **cap retries** (e.g. 3) so a model that
  can't produce valid output fails the episode instead of looping forever. → PR7.
- **System prompt + tool schema.** Both single-sourced from `macro_grammar` (PR5): `tool_schema()` →
  `response_schema`, `verb_signatures()` → the prompt's verb list. Built; only the task/percept-format system
  prompt remains to author. For a *frozen-model* eval, prompt quality is a first-class variable. → PR7c.
- **Gemini 3.5 Flash client.** Stateful `client.chats.create` (implicit caching), `thinking_level=HIGH`, image+text
  turn, structured output via `response_schema` for the verb, thinking trace + raw response captured. **No
  `temperature`** (Gemini uses `thinking_level`; temp-0 reproducibility isn't available, so transcripts are
  best-effort deterministic). `GEMINI_API_KEY` from the repo `.env`. → PR7c.
- **Per-chamber config** (`map_name`, `exit_pos`, `success_radius`, macro-step budget): a tiny dict/YAML the driver
  loads. The *only* inputs you still owe (map + exit) live here. → PR7.

**Gate (do before trusting an LLM result):**
- **Scripted canonical-solve dry run.** Run the §11.1 cube→button→door sequence as a *hardcoded* macro list through
  the real executor + success check, **before** wiring the LLM. If the script solves, the executor + success path
  are sound, so any later LLM failure is cleanly reasoning/perception — not an executor bug. This is the single
  best confound-remover. → PR7b (the scripted agent path, before 7c wires the model).

## Pre-flight checklist (resolve before/inside the relevant PR)

- **🔴 MarkTable canonical numbering → done in PR0** (was deferred; promoted because transcript reproducibility is
  benchmark-critical, not a save/load nicety). See PR0.
- **⚠ SetAngles-survives-the-tick spike → before PR2** (the one high-uncertainty assumption; see PR2 callout).
- **Grab confirmation has no engine signal → built into PR4** (affordance precheck + post-pulse tracking confirm).
- **`sv_alternateticks`:** `AdvanceTick` bumps `g_advance` by 2 when enabled ([Engine.cpp:685](src/Modules/Engine.cpp#L685)),
  so a macro's "advance N ticks" may not equal N *server* ticks. Verify the harness autoexec's setting and that
  tick budgets count server ticks (cross-check against `GameState.server_tick`). Minor, but pin it in PR0/PR2.

## Follow-on (post first light — out of this plan)

- **`shoot_portal`** (C3): aim → `TraceFirePortal` (needs the portal-gun entity ptr;
  [PortalPlacement.cpp:104-152](src/Features/Hud/PortalPlacement.cpp#L104-L152) is the reuse template) →
  return `PortalPlacementResult_t` (`SUCCESS`=0-2, `INVALID_SURFACE`=8, `CANT_FIT`=3, … ,
  [PortalPlacement.hpp:6](src/Utils/SDK/PortalPlacement.hpp#L6)) → pulse `FireBlue`/`FireOrange`.
- **`go_to_edge` + nav-compass** (C5): the fan-of-rays platform probe (grammar §5.2).
- **`anchor`/`restore`** (C9): engine `save`/`load` slots.
- **Egocentric observability** (Track B), portal-traversal-during-`go_to` semantics (grammar §4 checkpoint).
