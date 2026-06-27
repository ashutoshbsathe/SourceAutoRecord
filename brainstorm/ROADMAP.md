# ROADMAP — START HERE

**The single source of truth for project state, milestones, and decisions.** The other
docs hold the detailed design; this file is the index + tracker over them. If something
here disagrees with a detail doc, fix one of them — don't let them drift.

---

## What this is

A **frozen-LLM/VLM reasoning benchmark on Portal 2**. We hand an *untrained* model a clean
percept (the chamber, visually annotated + a structured entity list) and reliable actuators
(macro verbs like `go_to mark=N`), and measure whether it can solve chambers. The thesis:
**when perception + actuation are handed to the model for free, is the bottleneck reasoning
or perception — and when it fails, which one?** Macros draw the line at *local (engine) vs
global (model)*, so a reasoning gap is cleanly separable from a locomotion gap. Methodological
cover: Demaine et al. 2018 (cube+button+door alone is PSPACE-complete).

---

## Top of mind (2026-06-27) — DUAL-ROLE CUBE EVAL RAN: model is strong, a harness HELD-FLAG P0 surfaced

The M3 **dual-role cube eval** ran on `workshop/17093866141393312246/1782237070` and gave us a clean
controlled pair on the SAME chamber + grammar + model (gemini-3.5-flash): a **19-step SOLVE**
(`dual_role_cube.trajectory`) and a **50-step BUDGET non-solve** (`weird_teleport_dual_role_cube.trajectory`).
The good run is proof the model is strong — it derived the dual-role trick from coordinates (*"same
x-coordinate! Bingo"*) and ran `release→SEATED`, `redirect_to`→POWERED, `interpose`→POWERED, SOLVED, 0
rejects. A 22-agent blameless adversarial post-mortem (4 lenses + per-finding skeptic refutation; overturned
3 first-pass overclaims) → **[dual_role_cube_postmortem.md](dual_role_cube_postmortem.md)**.

**Root cause of the non-solve = ONE model strategy miss, amplified by a harness bug into a doom loop.** The
model never used `redirect_to` on the seated cube (0× in 50 steps) and reached for `pick_up 14` off the
button instead. That tripped the **P0**: `pick_up` confirms a grab **positionally** (`moved>8u`,
[MacroExecutor.cpp:1857](../src/Features/Harness/MacroExecutor.cpp)), and a button-seated laser-pinned cube
barely moves when grabbed → **GRAB_FAILED ×3 even though the engine attached it** ("pick_up failing when the
cube was on the button"). The harness then thinks hands are empty (`g_heldEntityKey=0`) while the cube is
engine-held → the next `go_to` **drags it ~440u off the button** ("teleport fuckery"), killing the press.

**The fix (P0, highest ROI, ~½ day) — read the engine held flag, DUMP the Python guess.** The authoritative
`m_hAttachedObject` is already read in `DropHeld` ([:586](../src/Features/Harness/MacroExecutor.cpp)); there is
**no prop-side cube held-bool** (the player owns the handle). Plumb it once: `m_hAttachedObject` →
`markTable.GetMark` → `int32 held_mark` on `GameState` → Python reads `state.held_mark` and **`_update_held`
([testchamber_session.py:131](../py/testchamber_session.py)) gets deleted**. The SAME read replaces the
`moved>8` grab-confirm. One field kills (a) the false-negative, (b) the orphan-attachment drag, and (c) the
Python-side held guess simultaneously.

**NEXT (post-mortem §6, cheap→structural):** ① ship the held-flag fix (SAR proto+C++ first, then Python;
add a `held_mark` round-trip to `agentloop_smoke`). ② recon-confirm the drag mechanism (per-tick
`m_hAttachedObject` dump — the one honest uncertainty: engine-carried vs physics-punted). ③ interpose
down-trace fix (pass the emitter to `DownTraceRest` so a near-emitter seat stops snapping onto the housing at
z≈83). ④ design call: add `on_beam` to cube percept / soften the "interpose first" grammar prior (validate on
a 2nd dual-role map — overfit risk). ⑤ structural: navmesh dead-pocket escape (defer behind ③).

**SHIPPED + the chamber now SOLVES CLEAN (2026-06-27).** All harness P0s fixed and verified in macro_repl,
then end-to-end: `robust_dual_role_cube.trajectory` is now a **16-step SOLVED, 0 rejected, ZERO failure codes**
run (down from 30→50 steps and 3.2M→0.34M tokens) — and at step 13 the model fires **`redirect_to 14` on the
cube while it sits on the button** (the dual-role move it kept missing). Commits on `yeeh`:
- ① held-flag fix — `3ce28193` (expose `GameState.held_mark`), `3359240b` (grab-confirm via `m_hAttachedObject`),
  `a990cd5f` (delete the Python `_update_held` guess), `bba2be85` (`agentloop_smoke` round-trip).
- Bug A release flakiness — `0e3fc7ae` (clear-air `FreeGrab` fallback + honest `STILL_HELD` + restore `g_heldEntityKey`).
- ③ interpose reachability — `162fa231`, but as TWO different root causes than ③ predicted: **(a) beam self-block**
  (the player standing in their own beam shortened the trace → seat snapped onto the housing; skip player+cube in
  the beam trace) and **(b) a capped-A\* reachability gate stricter than the uncapped VFH carry** (`go_to` reaches
  what the gate rejected — dropped the pre-gate, the carry is now the authority). interpose no longer returns
  NOT_REACHABLE (truly-unreachable → `BLOCKED`).
- ② recon-confirm drag mechanism: **moot** (the held-flag fix removed the drag entirely). ⑤ dead-pocket: **mostly
  moot** (it was caused by interpose's bad standoff, now fixed). Resolution writeup: postmortem §7.

**④ on_beam / grammar tweak — CONFIRMED UNNECESSARY (3/3 solves post-fix).** The model solves dual-role
*reliably* without any percept/grammar change, so the earlier weird/old-robust strategy misses were
harness-derailment + stochasticity, not a legibility wall. Not actioning ④ — it would overfit to one chamber.

**NEXT — dual-role chamber is DONE: harness P0s shipped, solve rate measured 3/3 verb-only. Onto the M3
frontiers (from 06-24, now unblocked):** **portals** (highest ceiling, the one missing core mechanic — STARTING
HERE) · the measured benchmark suite (157 in-scope PeTI chambers censused) · the I/O causal graph.

---

## Top of mind (2026-06-26) — LASER VERBS SHIPPED; a chamber SOLVED verb-only

The laser verb family is **shipped end-to-end and a full PeTI laser chamber was SOLVED verb-only**
(`go_to`/`pick_up`/`release`/`interpose`/`redirect_to` — no raw geometry from the driver), confirmed via
**macro_repl + agentloop_smoke 13/13**. This closes workstream **A** ("build the laser verbs") from 06-24.

**What shipped** (commits `866ff50f` + `278c51cc` on yeeh, UNPUSHED) — a robustness chain that took the
verbs from "intermittently broken" to "solves a chamber":
- **`interpose`** (held cube → beam): carry (go_to backend) → free the +use grab via a clear-air view sweep
  (`FreeGrab` — a steep-down drop wedges the cube into the floor, +use won't release) → displace the player
  off the seat scanning **perpendicular to the beam, rejecting on-beam bearings** (else the displaced player
  occludes the beam) → re-seat + confirm. `ON_BEAM`/`NOT_INTERCEPTING`/`NO_FLOOR`/`NOT_REACHABLE`.
- **`redirect_to`** (orient atom): **reach-gated** (`OUT_OF_REACH` if not next to the cube — no
  across-the-room teleport-rotate); rotate +X→target in place, confirm `m_bPowered`.
- **`release`** (place-on-button) hardened: displace on **proximity** (not just dead-on-seat); honest
  `NOT_SEATED` when no standoff fits; `SEATED` requires the cube still on-seat at dwell end (drift gate).
- **`go_to`** nav upgrade (enables the dual-role cube-on-button): skip obstacles whose footprint OVERLAPS
  the target (reach a cube seated on a button) + a **pushable-target arrival standoff** (stop beside a cube,
  don't bulldoze it; grabbable-gated so a button keeps a close approach for `release`) + zero player velocity
  on arrival (no coast-wedge).

The **dual-role cube** (one cube presses a button on the beam AND redirects it) solves as an EMERGENT
composition (`go_to cube → redirect_to relay`), no named dual-role verb — exactly the intended design.

**NEXT — the verbs + percept are ready, so the bottleneck is no longer SAR.** Highest-value first:
1. **⭐ "Laser light" — run the FROZEN-LLM eval on the laser chamber.** macro_repl proved the human
   baseline; the actual science is whether an *untrained* model reasons through laser redirection. Wire the
   laser chamber into the ReAct driver and run — the **M3 analogue of first/third light**. This is where the
   reasoning-vs-perception signal for lasers comes from; everything below is gated on what it reveals (e.g.
   does the model compose `go_to`+`redirect_to` on `OUT_OF_REACH`, or one-shot `power_with`?).
2. **Laser increments (as the eval exposes gaps):** dual-role `at=@button` placement (interpose a cube
   directly onto a button on the beam) · `IN_HAZARD` (point-contents goo check — `NO_FLOOR` misses slime) ·
   `power_with` one-shot vs `interpose`+`redirect_to` decompose ablation · multi-emitter incoming-independence.
3. **The bigger M3 frontiers (independent of lasers, per 06-24):** portals (highest ceiling, the missing
   mechanic) · the measured benchmark suite · the I/O causal graph.

Docs: [laser_redirect_verb_design.md](laser_redirect_verb_design.md) (handoff updated). Robustness lives in
`src/Features/Harness/MacroExecutor.cpp` + `GoToPlanner.cpp`; smoke in `py/agentloop_smoke.py`.

**UPDATE (2026-06-26) — laser-light eval RAN & SOLVED, but surfaced a P0.** `laser_and_button` solved
verb-only by gemini-3.5-flash (48 steps); the puzzle core (steps 5–16) is clean and impressive. BUT ~75%
of steps were entrance/exit **corridor** nav, and the **31-step exit-elevator tail would FALSE-FAIL under
the default `max_steps=30`** (objectives met by step 13, `chamber_complete` latches only at step 47). Root
cause: the cylindrical exit elevator (a `func_tracktrain` named `*departure_elevator*`) has **no mark** —
its only handle is the `@exit_airlock_door` frame. Deep adversarial post-mortem (48 verified findings) +
corridor-fix design panel (5 proposals, 3 judges) → **[laser_and_button_postmortem.md](laser_and_button_postmortem.md)**.
Recommended: recon-gate first, then **rescope the eval boundary** (end on objective-met, decoupled from the
ride — kills the false-fail) **+ annotate the elevator** (one name-gated `func_tracktrain` entry). Also P1:
cap the O(steps²) image-history token cost; batch the percept-noise cluster (ghost (0,0,0) portals, cube
double-marks, silent-teleport signal).

**RESOLVED (2026-06-27) — corridor rescope SHIPPED, signal corrected mid-session.** First cut hooked
`@exit_door.Open` — WRONG (fires at puzzle-solve, player still at the button, 0 navigation). User pushback:
"navigation stays the puzzle" — exit-door-unlocked ≠ agent reached the exit. I/O recon of the Q4 dump +
134-map corpus found the right signal: **`@exit_airlock_door.Open`** (`SIG_EXIT_AIRLOCK = 1<<5`). It's fired
by `relay_leaving_level`, gated on `@exit_door.OnFullyClosed` WHILE the player stands in a corridor trigger —
so it requires solved **+ walked through the exit door + up the corridor**, but fires BEFORE the bidirectional
`linked_portal_door` worldportal ride to the far elevator (the "~4600u teleport" is a *seamless worldportal
pair*, not a trigger_teleport; its bidirectionality caused the 31-step bounce). An agent that solves but
doesn't walk out gets NO early latch → must navigate → traversal stays part of the test. Shipped **proto-free,
Python-free**: `PuzzleExit::OnInput` latches the EXISTING `chamber_complete`, additive + backstopped by the
unchanged egress OR-set (zero regression). Generality: 108/134 in-scope PeTI maps have the door, 96% open it
via player-gated `relay_leaving_level`; 4 spawn-fire outliers backstopped. Also shipped: ghost-portal fix
(origin-reject extended to `prop_portal` in `IsHarnessMarkedEntity`). Deferred per user: cube-dup filter,
TELEPORTED signal, image-history cap. `sar.so` rebuilt + compiles. **NEXT (user verifies in macro_repl, THEN
runs eval):** macro_repl on `workshop/17093866141393312246/1782237070` — solve + walk into the exit corridor →
expect `chamber_complete exit_signal_mask=32` (bit 5 = SIG_EXIT_AIRLOCK), fired BEFORE reaching the elevator;
then the M3 **dual-role cube eval**. Corpus census also ran: **157 in-scope PeTI chambers**, 58
laser+cube+button candidates, benchmark-suite selection is the open M4 follow-up.

---

## Top of mind (2026-06-24) — lasers work mechanically; the VERB SURFACE needs a rethink

Lasers are **mechanically solved** at the SAR level. Part A percept shipped (transform-sane mark gate,
`point_laser_target {powered}`), the redirect spike PROVED computed-point teleport interception, and the
**dual-role cube P0 PASSED** — one cube presses a button sitting on the beam *and* redirects that beam to a
target, both catcher and relay, confirmed in-engine. SAR can clearly do a *lot* of powerful laser
manipulation (teleport-seat, closed-form +X aim, faithful button press, confirm-and-correct against ~2%
settling jank). → [laser_dual_role_cube_design.md](laser_dual_role_cube_design.md) (P0 RESULTS).

**The open problem is no longer "can SAR do it" — it's "what verbs expose it."** Both remaining laser
workstreams force a **verb-surface rethink before more code**:

- **A — build the laser verbs** (`interpose`/`redirect_to`/`power_with`, dual-role doc P1–P8). The sketch
  predates seeing how powerful *and* fiddly the mechanics are (dual-role, the jank loop, pitch-lock, the
  opportunistic-press NO-OP). Is this the right surface, or are we accreting verbs?
- **B — reachability/fairness layer** (`GoToPlanner` stepped-floor + portal frontier). Where a cube may
  *legally* be placed is its own can of worms and interacts with the verb semantics.

**Under-served bar: palatability to BOTH a frozen LLM AND a human baseline.** The human player is our
control arm — if a verb is awkward for a *person* to drive, it's a bad abstraction, not just a bad prompt.
Rethink the laser verb surface against *both* audiences before committing to P1–P8.

**RESOLVED (2026-06-24) — verb-surface rethink.** A 26-agent fan-out (7 competing grammars → critique
→ judge → synth → verify) plus a recovery-lens follow-up both selected **navigate/act-split**: one
free, capability-*fenced* `navigate()` over existing topology; every graph-*edit* (`place_portal` /
`paint` / `press` / the laser family) is its own explicit verb; ballistic *player* traversal is
SIMULATED (teleport reserved for inert objects); `fling` + faith-plate + gel-bounce collapse into one
`launch(via)`. The line: *free if it consumes an affordance, puzzle if it creates or changes one.* This
supersedes the "is this the right surface" question for **both** workstreams — A's laser verbs slot
into the table unchanged; B (`GoToPlanner` stepped-floor + portal edges) is build-order step 2. The
one open caveat: `redirect_to` absorbs beam-geometry feasibility, so crossfire-style chambers test
gate-identity + reflector choice but under-test spatial-feasibility (lever: make the model supply the
interposition point). → [verb_grammar_rethink.md](verb_grammar_rethink.md) (design + verb table + build
order), [verb_grammar_transcripts.md](verb_grammar_transcripts.md) (per-grammar try→fail→realign).

Docs: [laser_dual_role_cube_design.md](laser_dual_role_cube_design.md) (P0 results + verb sketch),
[laser_redirect_verb_design.md](laser_redirect_verb_design.md) (redirect surface + fairness),
[laser_percept_and_aim_design.md](laser_percept_and_aim_design.md) (L0 recon facts). Commits `68cd668e`
(Part A) + `271578a8` (dual-role recon + design).

---

## Top of mind (2026-06-21) — next steps

### ⭐ FIRST LIGHT ACHIEVED — third light SOLVED + what the robust verbs bought

A frozen VLM (gemini-3.5-flash) **SOLVED** the cube→button→door chamber end-to-end in **15 steps** —
`noteworthy_trajectories/third_light.trajectory`. The unlock was the enhanced verbs (A\* `go_to` +
closed-loop place-on-button `release`), both shipped this week. **M2 is done.** Items 1–3 in the
priority list below are now complete.

**The ablation writes itself** — same model, same chamber, only the verbs changed:

| | first light (06-15) | third light (06-21) |
|---|---|---|
| outcome | **BUDGET** (gave up, 25 steps) | **SOLVED** (15 steps) |
| `release` on the button | `SUCCESS released toward mark 7` ×2 (open-loop drop — cube fell off) | `SEATED m_bActivated 1/1` ×1 |
| cube re-grabs | 3 (kept re-fetching the cube that wouldn't stay) | 1 |
| `go_to` failures | BLOCKED ×4, WALL ×2 (old straight-line march) | 0 (A\* routes around) |
| tokens in | 686k | **286k** |

The agent's *plan* was correct in both runs (grab → carry → place → exit). First light failed because the
verbs **lied** — `release` said SUCCESS while the cube transiently seated and fell off, so the agent
re-grabbed and thrashed into walls until BUDGET. Third light's verbs told the truth (`SEATED` via the
`m_bActivated` dwell; A\* `BLOCKED` only when truly walled) and it solved in one clean pass. **Lesson: for
a frozen LLM, action-interface honesty + reliability dominates raw reasoning** — this *is* the
reasoning-gap-vs-locomotion-gap result (M5), now with a clean before/after.

**Place-on-button shipped (C1→P8, deferred bits noted):** `release <button>` = confirm-retry drop
(`m_hAttachedObject`) → displace the player off the seat if it stands there → flat dead-centre
`CBaseEntity::Teleport` → fairness gate (reach / press-normal / corridor / sightline / occupancy) →
`m_bActivated` dwell → `SEATED`/`NOT_FAIR`/`NOT_SEATED`. → [release_place_on_button_design.md](release_place_on_button_design.md).
*Deferred:* bisect the settle/dwell tick constants · R7 recon (past `prop_floor_button`) · strip-or-keep
the dormant `sar_harness_seat_*` commands.

**New frontiers to brainstorm next** (pick one for a fresh session):

1. **Portals — the missing mechanic, highest ceiling.** The agent can't place portals; every chamber so
   far is portal-free. A robust `shoot_portal` verb (aim at a portalable surface → fire; portalability is
   baked + statically recoverable per the BSP recon) + `go_to` that traverses portals (parked,
   `llm_percept_act_grammar.md` §4). Turns the eval into *actual Portal 2*.
2. **The benchmark + the result.** Turn the ad-hoc "lights" into a measured suite: N annotated chambers ×
   difficulty tiers × multi-model, scored on solve-rate / steps / tokens. The third-vs-first ablation is
   the template metric. Capitalises on the win now; feeds M4 + the M5 talk.
3. **Lasers (M3) — recon DONE; verb design + fairness brainstormed.** `sar_harness_laser_intercept_spike`
   PROVED computed-point teleport interception (down-trace rest, ±24u capture radius, no drift). Verbs =
   `interpose`/`power_with`/`redirect_to` (teleport-driven; held-aim demoted to oracle); mark
   `point_laser_target` only. **Next directions — independent (Part A percept, reachability recon) vs the
   chosen frontier (extend `GoToPlanner`: stepped-floor + portal edges, also upgrades `go_to`) — are in
   the handoff block at the top of** → [laser_redirect_verb_design.md](laser_redirect_verb_design.md).
4. **Percept enrichment — the I/O causal graph.** The button→door wiring is statically recoverable
   (`bsp_corpus_harness_improvements.md`). Feeding it to the agent unlocks multi-element, sequenced
   puzzles (which button opens which door) — the reasoning ladder past a single cube→button→door.

---

**`go_to` A\* routing — SHIPPED ✅ (P2.0–P2.6, money test passed).** VFH local control + A\*
global routing over a lazy hull-probed 16u grid: the cube-on-button pocket that oscillated →
BLOCKED now rounds the pocket and reaches the mark, `on_button` holds, and a line-of-sight `go_to`
keeps the zero-plan fast path. → [astar_routing_design.md](astar_routing_design.md),
[locomotion_tech.md](locomotion_tech.md). Verb-time camera view-hold also shipped (release/pick_up
no longer flip the view to the ceiling).

Next, roughly priority order (user to choose):

1. **P2.8 — grammar/prompt sync** *(A\* closeout, ~30 LOC Py)*. The model's `go_to` doc still says
   *straight-line*; rewrite it ("routes around obstacles; `BLOCKED` only if no path exists"), drop
   the caveat, add an `agentloop_smoke` assertion — else the LLM under-uses the new routing.
   (P2.7 path-simplification = optional smoothing if the legs look choppy.)
2. **P-manip — reliable place-on-button** *(design + phased plan LOCKED, 2026-06-21)*. The *other*
   first_light last-mile failure: `release` is open-loop, cubes only transiently seat (`on_button`
   flips back). **Direction:** a range-gated **central teleport** — static-trace fairness check
   (reach + press-normal + drop-corridor; the "sim N ticks" idea was killed) → snap the cube
   dead-center via FCPS's `CBaseEntity::Teleport`, orientation preserved → dwell-verify the held cube's
   `m_bActivated`. Net ≈ reuse, **0 new offsets**. Prototype spine for the laser `aim_laser` verb.
   → [release_place_on_button_design.md](release_place_on_button_design.md) (plan §10);
   [locomotion_tech.md](locomotion_tech.md) §4.
3. **⭐ First light (M2)** — the frozen-VLM ReAct driver (PR5–PR7) on `testchamber_000`. The core
   science; de-risked once go_to + place-on-button are both trustworthy. → [macro_executor_impl_plan.md](macro_executor_impl_plan.md).
4. **Lasers (M3)** — the next big element frontier, gated on the L0 recon spike. → [locomotion_tech.md](locomotion_tech.md) §5.
5. **Annotation improvements** — sharpen the in-engine percept (was priority #1; brainstorm pending).

**Camera free-run drift — parked.** The harness keeps the TasPlayer asserting the view every tick
even when `harnessControlActive == false` (warmup/idle "player control"), so the camera isn't truly
yours in free-run. Isolated (Harness.cpp PRE_TICK + TasController per-tick `SetAngles`); a fix
(stop/restart the TAS, or gate the assert) was prototyped and reverted — **not worth it right now.**

**Exit detection — parked as "good enough."** The PuzzleExit oracle's C++ core (P1–P4) is shipped + verified: it latches `chamber_complete` from the AcceptInput OR-set, reads out over gRPC, and re-arms per episode. The remaining phases (P5+: Python terminate-on-bit, smoke gate, prevention, radius-oracle deletion) are **not critical** and can wait. → [exit_detector_impl_plan.md](exit_detector_impl_plan.md)

**Someday / capstone (recorded, not scheduled).** A non-LLM **brute-force chamber solver** — engine save/load × the offline causal-graph prior × A\* verb primitives (black-box savestate planning; IW(1)/novelty-pruning over annotation atoms = the cheap instantiation). Real use = an experimenter-side **solvability + difficulty oracle** for eval construction (pays off only if automated chamber generation ever lands). Walled to shallow chambers: **PSPACE-complete** core fragment (Demaine, Lockhart & Lynch, FUN 2018 — *not* the folklore "NP-complete") + the ~4.3 s/node save-load warmup. Record-only, don't build. → [offline_map_preprocessing.md](offline_map_preprocessing.md) §7 + C9.

---

## Current status — 2026-06-11

- ✅ **Annotation (Track A, A1–A5):** colored boxes + Set-of-Marks labels + portal reticle, in-engine.
- ✅ **Recon mechanism:** every category-A status encoding understood. Schema locked in `status_field_recon.md`.
- ✅ **Scope:** decided — full stock-PeTI surface set is the v0 target, **phased after first light**.
- ✅ **Phase 1a — status into the snapshotter:** curated `[dm]` fields (cube type/activated, laser-target powered, faith-plate disabled) now flow over gRPC. Verified in-engine.
- ✅ **Macro executor (Track C, PR0–PR4):** all verbs built + validated against a live game — `aim_at`/`look`/`go_to`/`move`/`pick_up`/`release`/`interact`/`wait`/`done`. **Gate 2 passed: `testchamber_000` (an auto-dropper cube→button→door chamber) solved by hand through the real executor.** As-built deviations are in `macro_executor_impl_plan.md` §As-built carry-forward.
- ✅ **First-light chamber + manual driver:** `testchamber_000` provided and solvable; `py/macro_repl.py` is the human-driven macro REPL (the manual analogue of the ReAct driver).
- 🔜 **Next (critical path to first light):** the Python layer — `PR5` (entity parser + macro validator + `step_macro`), `PR6` (gRPC keepalive), then `PR7` ReAct driver (= ⭐ first light).

---

## Milestones

- [x] **M0 — Lock the ontology.** Annotation built; recon mechanism done; status schema + scope locked.
- [ ] **M1 — Status-aware percept** (Phase 1): curated category-A status flows over gRPC. *(1a ✅; 1b/1c remaining)*
- [x] **M2 — ⭐ FIRST LIGHT:** frozen VLM solves one cube→button→door chamber (no portals). *(Achieved 2026-06-21 — gemini-3.5-flash SOLVED it in 15 steps once the verbs were robust; `third_light.trajectory`. The robust-verbs-vs-reasoning ablation is the perception-vs-reasoning signal.)*
- [ ] **M3 — Ramp complexity:** add portals → lasers → panels; grow the chamber suite into difficulty tiers. *(Lasers: verb surface SHIPPED + a chamber solved verb-only, 2026-06-26 — next is the frozen-LLM "laser light" eval. Portals + panels remain.)*
- [ ] **M4 — Public benchmark:** multi-model eval (Claude/Gemini/GPT-class), scoring, reproducible packaging.
- [ ] **M5 — VP talk, with data:** the reasoning-gap-vs-locomotion-gap result.

---

## Phase plan — critical path

**To first light (M2):**
1. **Phase 1 — status into the snapshotter** *(the observation rework)* — **effectively done for first light**: `1a` ✅ + buttons are networked (SendTable already captures them); door deferred but non-blocking (visual + trigger-based success).
   - ✅ `1a` register curated per-class status (the `[dm]` fields the SendTable walk drops) — *done, verified in-engine*
   - `1b` catcher/relay → child `point_laser_target` association — **deferred → M3** (laser-only; not on the first-light path)
   - `1c` faith-plate name filter (avoid safety-net flood) — **deferred → ~M4** (stock-chamber hygiene; a hand-designed chamber has no safety-net flood to filter)
2. **First-light macro executor — ✅ done (PR0–PR4), validated live + Gate 2 solved by hand.** `C1` proto, `C2` aim_at+look, `C4` go_to+move, `C6` pick_up/release/interact/wait, `C7` mark-in-telemetry. (`C8` keepalive = PR6, still open.) Skipped `C3`/`C5` (no portals). As-built deviations: stable per-entity marks (not dense 1..N), full-`map`-reload reset for droppers, `+use` held ≥3 ticks, grabbable-class gate + movement-based grab-confirm — see `macro_executor_impl_plan.md` §As-built carry-forward.
3. **Python ReAct driver (PR5–PR7, remaining)** — `D1` entity parser, `D2` macro validator, `D3` driver + transcript logger, `D4` run on the chamber. (`py/macro_repl.py` is the human-driven prototype: `build_macro` ≈ D2, the loop ≈ D3.)

**Widen (after first light), by mechanism family:**
4. **Animation family** — `m_nSequence` state reader + flip-panel targetname matcher → brings panels **and** the door into the percept (one reader). Adds `C3` shoot_portal + `C5` nav-probe for portal chambers.
5. **Surface family** — gels (paint-map read), then light bridges (projector/volume read). Independent phases.
6. **Coverage cleanup (anytime):** recon a chamber with funnel + monster box + turret-tip.

---

## Code hygiene / tech debt (anytime, low-stakes)

Not on the critical path, but worth a sweep when touching a file: small *taste* debt
has crept into `py/` (and likely `src/`). Fix in passing, don't make a project of it.

- [ ] **Dict-access taste sweep** — kill pointless `.get(k)` guards on dicts whose
      schema *guarantees* `k` (e.g. mark dicts from `entities._mark_dict` always carry
      all keys), and the inconsistency of mixing `m['x']` and `m.get('x')` in one
      expression. Hoist the value into a local instead of burying a quoted subscript in
      an f-string (`f' "{m["name"]}"'` → `name = m['name']; tag = f' "{name}"'`). *Keep
      the legitimate `.get`s*: sparse delta field-maps and untrusted LLM JSON genuinely
      may miss keys. (First pass done in `gemini_agent._percept_text` + `macro_repl.dump_marks`, 2026-06-18.)
- [ ] **General readability pass** — convoluted inline f-strings / nested conditionals,
      when you're already in the file. Minimal, surgical, no churn-for-churn's-sake.

---

## Key decisions (log)

| Date | Decision | Detail |
|---|---|---|
| 2026-06-08 | Deep-narrow **fixed ontology** (stock PeTI), not wide-shallow | `fixed_ontology_scope.md` |
| 2026-06-08 | `.hdem` recorder = **generous** substrate; percept/rollout project to curated subset | `fixed_ontology_scope.md` #1 |
| 2026-06-09 | **v0 scope = full PeTI surface set, sequenced after first light** (panels = animation family; gels/bridges = surface family) | `fixed_ontology_scope.md` #3 |
| 2026-06-09 | Locomotion **engine-simulated**, split at **local (engine) / global (model)** | `llm_percept_act_grammar.md` §4 |
| 2026-06-09 | **Save/load anchors** reuse engine `save`/`load` (no custom restore) — *checkpoint-deferred grammar* | `llm_percept_act_grammar.md` §4 |
| 2026-06-09 | Portal-traversal during `go_to` — *checkpoint-deferred* | `llm_percept_act_grammar.md` §4 |
| 2026-06-09 | **Canonical mark numbering** (pure fn of world state, save/load-invariant) | `llm_percept_act_phased_plan.md` A3 |
| 2026-06-09 | **Category-A status schema locked** (bool vs `m_nSequence` vs child-target); door deferred | `status_field_recon.md` |
| 2026-06-09 | **First-light-first; element breadth is decoration for the core science** | Demaine: cube+button+door alone is PSPACE-complete → the reasoning-difficulty ladder needs no new elements. So `1b` (lasers) → M3, `1c` (safety-net filter) deferred (~M4 stock-chamber hygiene; a hand-designed chamber sidesteps the flood). First light is the de-risking gateway; in every VP/headcount scenario the *result* is the currency and breadth is downstream. Next = macro executor (C) + ReAct driver (D) + chamber. |
| 2026-06-10 | **Act-grammar altitude = closed semantic verbs (v0)**; code-as-action + skill library is a **P1 A/B arm**, gated on a demonstrated *in-step composition* bottleneck (not built for v0) | `llm_act_grammar_altitude.md` |
| — | **Eval only, no training**; **visual+symbolic always**; **macros C++-side** | `llm_percept_act_grammar.md` §8 |

---

## Doc index — what to read for what

| Doc | Read it for |
|---|---|
| **ROADMAP.md** (this) | state, milestones, decisions, where to look |
| `laser_and_button_postmortem.md` | **⭐ the laser-light eval post-mortem** — adversarial analysis of the first SOLVED laser-chamber trajectory (harness/model right+wrong, 48 verified findings) + the **corridor P0** brainstorm (2 problems: false-fail timeout + wasted nav; rescope-eval-boundary vs annotate-the-elevator, 5 proposals + 3-judge panel). Read for what to fix next on the eval/exit. |
| `fixed_ontology_scope.md` | *why* deep-narrow + the v0 scope decision (read first for scope) |
| `puzzlemaker_elements.md` | the element list (categories A/B/C, P1) |
| `status_field_recon.md` | per-class status fields (the locked schema) + recon protocol + I/O-edge vocabulary (PeTI vs BEEmod) |
| `exit_detection_brainstorm.md` | the map-completion oracle (exit detector): signal recon across 8 maps, the OR-set, the §9 shippable-v0 spec + corpus sweep |
| `exit_detector_impl_plan.md` | the PR-by-PR exit-detector build plan — **P1–P4 done + verified; P5+ deferred** (not critical, see Top of mind) |
| `exit_criteria_structure.md` | (parked, post-oracle) exposing the exit's dependency graph as structured hints / dense reward |
| `llm_percept_act_grammar.md` | the percept/act **design** (the macro verbs, the thesis) |
| `llm_act_grammar_altitude.md` | **why closed verbs over Voyager-style code-as-action** (industry sweep + decision) |
| `llm_percept_act_phased_plan.md` | the **build order** (tracks A–D, phase-by-phase) |
| `macro_executor_impl_plan.md` | **code-grounded PR plan** for the macro executor + driver (Track C/D detail, PR0–PR7 to first light) |
| `verb_grammar_rethink.md` | **the chosen v0→M3 verb grammar** — navigate/act-split backbone, the locomotion-vs-puzzle cut, the full verb table (`navigate`/`ride`/`launch`/`place_portal`/`paint`/`press` + laser family), crux decisions (simulate-body/teleport-object), and the SAR-first build order. Read before building any traversal/element verb. |
| `verb_grammar_transcripts.md` | **per-grammar ReAct transcripts** (third_light + laser + the baited "crossfire" try→fail→realign chamber) for all 7 candidate grammars + the recovery-lens comparison. Read for *why* navigate/act-split wins on failure-legibility + realign-cost. |
| `locomotion_tech.md` | **`go_to` pathfinding (local controller + A*) + reliable place-on-button + the laser-routing frontier** — phased plan, substrate recon, ROADMAP #2 |
| `astar_routing_design.md` | **A\* global routing for `go_to`** (lazy hull-probed grid) + why save/restore tree-search is parked at the puzzle layer (C9), not locomotion |
| `release_place_on_button_design.md` | **gated-fair central-teleport `release` onto a button** (the P-manip place-on-button design + phased plan C1–D9) — button taxonomy, static-trace fairness check, FCPS `Teleport` reuse, dwell-verify; orientation = preserve-only |
| `laser_percept_and_aim_design.md` | **lasers — catcher/relay `powered` percept (the catcher↔child-target "child thing") + reflector-cube aiming verb** — proximity-vs-parent-handle association, the teleport-place-and-orient aim (sidesteps the held-view-coupling unknown, reuses the place-on-button spine), the L0 redirect-axis spike, forks |
| `offline_map_preprocessing.md` | **offline BSP→JSON prior** — I/O causal graph + affordance prior (static geometry parked; A\* dominates) + the recon-gated surface/region "spatial-vocabulary" idea; complements runtime A\*/telemetry, gated on a srctools spike (P0) |
| `bsp_corpus_harness_improvements.md` | **the §8 recon battery executed on all 277 workshop maps** (+ adversarially verified) — causal graph statically recoverable (proxy `OnProxyRelayN` baked at compile); `causal_confidence` 3-tier gate + edge-list sidecar + exit-relay resolver = build-first wins; corrects the "PeTI = no VScript" premise |
| `rollout_visualizer.md` | the `.rollout` browser viewer |
| `trajectory_visualizer.md` | the `.trajectory` (LLM eval) self-contained HTML viewer design |
| `trajectory_retry_capture.md` | the Observation+Call `.trajectory` data model (per-call retries) |
| `first_light_and_next_steps.md` | **⭐ post-first-light: the result, the reasoning-vs-locomotion finding, and next steps** |
| `diverge_catalog.md` | **the full idea space** — 259 consolidated ideas (A–M themes, stable IDs), skeptic's corner, next-7-days shortlist; raw council output in `.council/` |
| `hdem_implementation.md` / `_plan.md` | the `.hdem` sidecar recorder design |
| `entity_snapshotter_redesign.md` | snapshotter hot-path / perf design |
| `phase4_sendtable_discovery.md` | snapshotter field discovery + curated-status follow-up |
| `phase4_fixing_slowness_and_crashes.md` | snapshotter perf/crash post-mortem (lessons) |
| `entity_state_observe.md` / `_server_during_demo.md` | early entity-state extraction brainstorms |
