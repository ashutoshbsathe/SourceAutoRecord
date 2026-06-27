# dual_role_cube trajectories: blameless adversarial post-mortem (good vs weird-teleport)

> **Source artifacts:** two frozen-LLM solves of the SAME chamber `workshop/17093866141393312246/1782237070`
> ("dual_role_cube"), same verb grammar, same model (gemini-3.5-flash):
> `noteworthy_trajectories/dual_role_cube.trajectory` (19 steps, terminal **SOLVED**, 0 rejected) and
> `noteworthy_trajectories/weird_teleport_dual_role_cube.trajectory` (50 steps, terminal **BUDGET**, did NOT solve).
> **Method:** both dumped to text + per-step annotated frames, then a multi-lens adversarial workflow — 4
> independent lenses (harness-mechanics / model-strategy / percept+held-flag-recon / visual-frames), each
> finding re-checked by a skeptic agent that re-opened the cited frames/code/steps and tried to refute it,
> then synthesized. 22 agents. The adversarial pass overturned three of the first-pass overclaims (logged inline).
> **Date:** 2026-06-27.

---

## 0. TL;DR

- **Same chamber, two outcomes.** GOOD solved in 19 steps / 530k input tok, 15 SUCCESS / 0 rejects.
  WEIRD burned 50 steps / 3.17M input tok and hit BUDGET without solving.
- **The chamber's trick = cube 14 is dual-role.** Seated on button 7 it both holds the button AND lies on
  emitter 8's beam line (button 7 and emitter 8 share `x=448`), so you fire `redirect_to 14` on the
  *already-seated* cube to power a target **without ever picking it back up**. GOOD derived this from
  coordinates (step 10 thinking: *"same x-coordinate! Bingo"*) and executed it: release→SEATED (s11),
  `redirect_to 14`→POWERED (s13), `interpose 9`→POWERED (s16), SOLVED (s18).
- **WEIRD made ONE strategy miss and the harness amplified it into an unrecoverable doom loop.** It seated
  cube 14 fine but never ran the `x=448` check, so it never considered `redirect_to` on the seated cube
  (`redirect_to` issued **zero** times in 50 steps). Needing a second redirector at s16, it reached for
  `pick_up 14` off the button — which tripped a **harness grab-confirm bug**: the positional heuristic
  false-negatived 3× (GRAB_FAILED s17/19/24). The cube ended up engine-attached anyway, but the harness
  believed hands were empty (`g_heldEntityKey=0`), so the next `go_to` **dragged the cube ~440u off the
  button** (s25→26), killing the press and the solve. Recovery fell into an `interpose` mis-drop onto the
  emitter housing and a dark navmesh pocket until budget death.
- **The single fix that pays for itself 3×:** read the engine's authoritative held flag
  (`m_hAttachedObject`, already read in `DropHeld`) → `markTable.GetMark` → expose `held_mark` on
  `GameState`. This **dumps the Python-side held guess** (`_update_held`), **fixes the grab-confirm
  false-negative**, and **removes the orphan-attachment drag** — all at once.
- **Net blame: model owns the strategy miss; the harness owns turning it terminal.** The model is strong
  (the GOOD run is proof); this chamber should be trivially solvable, and the harness is what made it not.

---

## 1. The two trajectory arcs

**GOOD (19 steps, SOLVED).** Spawn elevator → corridor (s0–6, in-band) → `go_to 14` → `pick_up 14` (s9,
`moved=13`→SUCCESS) → `go_to 7` → `release 7` → **SEATED** (s11) → `go_to 14` → **`redirect_to 14`** →
**POWERED** target 12 (s13) → `go_to 13` → `pick_up 13` → **`interpose 9`** → **POWERED** target 11 (s16)
→ `go_to 10` → `go_to 6` → **SOLVED** (s18). Clean, monotone, no backtracking.

**WEIRD (50 steps, BUDGET).** Started already inside the chamber (corridor pre-cleared; note start pos
`(192,-326)`). s0–7: enter, `pick_up 14`, one premature `release 7` NOT_FAIR (out of reach), re-grab,
`go_to 7`, `release 7`→**SEATED** (s7). s8–15: `pick_up 13`, `interpose 8`→target 11 ON (s10), then
`pick_up 13` again + `interpose 9`→target 12 ON but **target 11 flips OFF** (s14–15) — one cube can't sit
in two beams. s16: realizes it needs cube 14 to redirect → reaches for **`pick_up 14`** (the fork). s17–25:
GRAB_FAILED ×3 on the button-seated cube, interspersed with aim/move/go_to nudges. s26: cube 14 has
**teleported to the player's feet** (~440u off the button); model re-grabs it. s27–34: `interpose 8`
mis-drops the cube onto the **emitter housing** (z≈83), NOT_REACHABLE/NOT_INTERCEPTING spam. s35–49:
player wedged in a **dark navmesh pocket** behind the housing, `go_to`→ADVANCED+BLOCKED 60u, `move`→STUCK,
until **BUDGET**.

| | GOOD | WEIRD |
|---|---|---|
| steps / outcome | 19 / **SOLVED** | 50 / **BUDGET** |
| input tokens | 530k | 3.17M |
| `redirect_to` used | **1** (the key move) | **0** |
| failure codes | EDGE×1 | GRAB_FAILED×3, NOT_REACHABLE×4, STUCK×2, NOT_INTERCEPTING, OUT_OF_REACH, NOT_FAIR |

---

## 2. HARNESS — what it got right / wrong

### Right
- **Honest telemetry.** ADVANCED-vs-BLOCKED/STUCK on nav, and `dz/moved/dist` on GRAB_FAILED — that detail
  string is literally what made this post-mortem diagnosable.
- **The authoritative held-flag read already exists in-tree.** `DropHeld` reads
  `pl->field<CBaseHandle>("m_hAttachedObject")` at [MacroExecutor.cpp:586](../src/Features/Harness/MacroExecutor.cpp)
  (also used by `FreeGrab`). The correct primitive sits one function away from the bug.
- **`ConfirmInterception`** ([MacroExecutor.cpp:1046](../src/Features/Harness/MacroExecutor.cpp)) re-traces the
  *actual* beam against the cube's *actual* position, so it correctly reported NOT_INTERCEPTING for the
  housing-parked cube instead of faking ON_BEAM.
- **`MarkTable.GetMark`** is mutex-locked / gRPC-thread-safe ([MarkTable.hpp:22](../src/Features/Harness/MarkTable.hpp)),
  so the proposed `held_mark` read needs no new synchronization.

### Wrong — four mechanisms, in causal order

**(a) Grab-confirm false-negative on a button-seated cube [CONFIRMED, critical].**
`PickUp` confirms a grab *positionally*: `post->held = post->dist < kHeldDist(80) && post->moved >
kMinGrabMove(8)` ([MacroExecutor.cpp:1857](../src/Features/Harness/MacroExecutor.cpp); constants `:80-82`).
A cube laser-pinned on a button barely moves when grabbed: WEIRD s17 `moved=8`, s19 `moved=2`, s24
`moved=1` → three GRAB_FAILED, all `dist=64-66 < 80`. The heuristic **structurally cannot** tell a
laser-resisted real grab from a missed `+use`. The author's own comment at `:1832` ("No networked held
flag, so infer the grab") is **stale** — `:586` disproves it.
- *Skeptic correction (down-weighted from first pass):* the "`moved=8` is a strict-`>` boundary injustice"
  framing is **wrong** — the detail string is `%.0f`, so the true float could be ~7.6 (a genuine miss). s19/s24
  (`moved=2/1`) are unambiguous. The bug is positional-inference-vs-engine-truth, **not** a `>` vs `>=` tweak.

**(b) Secret-attachment drag = the "teleport fuckery" [phenomenon CONFIRMED; mechanism genuinely UNCERTAIN, critical].**
`g_heldEntityKey` is written **only on the SUCCESS path** ([MacroExecutor.cpp:1883](../src/Features/Harness/MacroExecutor.cpp));
a GRAB_FAILED return leaves it 0. Between s25 (`go_to 13`) and s26, cube 14 jumped from `(438,704)` on the
button to `(181,348)` at the player's feet (~440u), button 7 flipped `pressed:True→False`, and `step026.png`
shows the cube point-blank with button 7 green-and-empty behind it.
- *Skeptic correction — do NOT assert "the engine carried it" as fact.* The cube stayed **stationary through
  the `go_to` marches at s20 and s23** — a *continuously* engine-carried cube would have followed the player
  then. `PulseUse` holds `+use` only ~3 ticks of a ~20-tick settle ([:561-573](../src/Features/Harness/MacroExecutor.cpp)),
  and Portal carry needs *continuous* `+use`. So the culpable event is **s24** (not s17), and the 440u jump is
  **either** a brief residual engine attachment **or** the player's physics hull punting the un-skipped cube
  off the pedestal during the 525u march (`MarchTo`'s heldKey arg is only a histogram skip, not a carry tracker,
  [:1221](../src/Features/Harness/MacroExecutor.cpp)). We cannot distinguish these from offline data — see §6.2.
  **The fix is identical regardless of mechanism.**

**(c) Interpose mis-drop onto the emitter housing [CONFIRMED, major].**
`DownTraceRest` traces straight down from the beam point passing **only the cube, not the emitter**
([LaserGeometry.cpp:59](../src/Features/Harness/LaserGeometry.cpp)). For a point near emitter 8 `(448,-16,32)`
the down-trace lands on the **housing top brush**, seating the cube at `z≈83`. WEIRD s30 returned
NOT_INTERCEPTING "at (448 -8 83)"; s31 telemetry shows cube 14 at `(447.9,-25,83.6)` — above the beam,
unable to occlude it. The NOT_REACHABLE results (s27/28/33/34) are the same defect downstream (the flat
planner probes the elevated housing surface and calls the seat cell unreachable).
- *Frame correction:* the claim that `step034/step036` *show* the cube on the housing is **wrong** — by then
  it's back at floor `z≈31`. The z=83 state is evidenced by **s31 telemetry only**.

**(d) Stuck-in-pocket + blind percept [CONFIRMED as a navmesh dead-pocket, NOT a held-cube wedge, major].**
The interpose standoff teleport ([MacroExecutor.cpp:1251-1262](../src/Features/Harness/MacroExecutor.cpp)) runs
*before* the NOT_INTERCEPTING return, dropping the player into a narrow pocket at `(407,-48)` south of the
housing. From s31 on, `go_to`→ADVANCED+BLOCKED 60u, `move`→STUCK/EDGE, oscillating `y=-48↔-108` to BUDGET.
Held-cube wedge is **ruled out** (`held_mark=0` from s37, cube stationary at `(457,-77,22)` through s49).
- *Frames (opened):* `step041.png` near-pure black + reticle; `step049.png` point-blank dark-red wall +
  reticle, **no marks rendered**. `step031.png` is a dark red wall under the **A5 aim reticle** (blue/orange
  arcs = the aim cursor, *not* placed portals — which the model itself briefly hallucinated then corrected).
  The model was **visually blind but still received the 14-mark text telemetry**, so it kept issuing
  `go_to`/`aim_at` against marks it couldn't see.

---

## 3. MODEL — what it got right / wrong

### Right (credit — the model is strong)
- GOOD **unprompted derived the dual-role solution from coordinates** (s10 thinking compares emitter 8
  `(448,…)` to button 7 `(448,…)`), then recited and geometrically verified `redirect_to`'s "already on a
  beam" precondition before firing it (s13→POWERED). 19 steps, 0 rejected verbs, no backtracking.
- WEIRD **correctly diagnosed the one-cube-two-beams impossibility** (s11/s16).
- WEIRD **self-corrected its own portal hallucination** at s16 (*"I was confusing the crosshair for
  portals… there are NO portals"*) without help — the trap was the A5 center reticle.
- WEIRD **read the teleported cube at s26** and adapted — reasonable recovery against a percept that lied
  to it (`holding=nothing`).

### Wrong
- **Dual-role miss [CONFIRMED, critical, model].** Across 50 steps WEIRD considered `redirect_to` as an
  *action* exactly **zero** times. Two compounding misses: (i) at seating (s7-8) it had emitter-8 and cube-14
  coordinates but never ran the `x=448` check; (ii) at s16 it correctly identified cube 14 as the redirect
  agent but defaulted to `pick_up`+`interpose` instead of `redirect_to` on the seated cube.
- **`pick_up` was strictly worse even if it had worked** — a clean grab unseats the button anyway, so it's a
  regression without a subsequent re-seat + `redirect_to`. `redirect_to`-on-seated was the only
  non-regressing move and the model never reached for it.
- **Aggravating legibility gap (shared model+grammar, not pure model fault).** The percept emits no
  `on_beam` field on a cube ([gemini_agent.py:119-135](../py/llm_eval/gemini_agent.py)); the emitter's state is
  `{}`. The grammar repeats *"interpose it first"* ~4× and frames `redirect_to` as the tail of a fixed
  `pick_up→interpose→redirect_to` pipeline — anchoring the wrong prior. GOOD bridged this by pure geometry;
  WEIRD didn't. **Primarily a model reasoning failure, with grammar/percept as a real aggravator.**

---

## 4. PERCEPT & THE HELD-FLAG FIX (the headline)

### 4.1 The held-state desync timeline
1. s17/19/24 — `pick_up 14` → GRAB_FAILED (`moved=8/2/1`). `g_heldEntityKey` stays 0; `_update_held` sets
   `held_mark` only on `result.ok`, so it stays `None`.
2. s17–26 — the model prompt reads **`holding=nothing`** ([gemini_agent.py:123](../py/llm_eval/gemini_agent.py))
   the whole time.
3. s24 — the `+use` pulse attaches the cube engine-side (best-supported culpable event); harness still
   believes hands empty.
4. s25 `go_to 13` — cube 14 displaced ~440u off button 7; the press is lost.
5. s26 — model finally *sees* the displaced cube (frame + telemetry are honest), re-grabs cleanly
   (`moved=16`, off-button → SUCCESS).

The percept channel (frames + mark coordinates) was **honest throughout**. The single lie was the
synthesized `holding=` line.

### 4.2 Recon answer: the canonical "what am I holding" source
The authoritative flag is the **player field `m_hAttachedObject`** (a `CBaseHandle` on the player /
grab-controller), already read at [MacroExecutor.cpp:586](../src/Features/Harness/MacroExecutor.cpp).
`CBaseHandle::GetEntryIndex()`/`GetSerialNumber()` ([Handle.hpp:9-13](../src/Utils/SDK/Handle.hpp)) yield
exactly the `(index, serial)` that `MarkTable::GetMark` expects. **There is NO prop-side cube "held bool"** —
`EntitySnapshotter` walks no attach/held field (grep: zero matches), and in Source's grab model the *player*
owns the handle, not a networked bool on the cube. A prop-side flag would be redundant; use the player handle.

### 4.3 The surgical fix (one engine field = single source of truth for the model line AND grab-confirm)
1. **[harness.proto](../src/Features/Harness/harness.proto)** — add `int32 held_mark` to `GameState`;
   `make proto` (then `git checkout --` the generated `*.pb.cpp` per the format.sh churn gotcha).
2. **[Portal2HarnessImpl.cpp](../src/Features/Harness/Portal2HarnessImpl.cpp)** (live-play branch, `pl` already
   in hand) — populate it:
   ```cpp
   CBaseHandle h = pl->field<CBaseHandle>("m_hAttachedObject");
   response->set_held_mark(h ? markTable.GetMark(h.GetEntryIndex(), (uint16_t)h.GetSerialNumber()) : 0);
   ```
3. **[py/testchamber_session.py:131-145](../py/testchamber_session.py)** — **delete `_update_held`** and the
   `_INTERPOSE_KEPT_HELD` set; in `step()` set `held_mark = state.held_mark or None`.
   [gemini_agent.py:123](../py/llm_eval/gemini_agent.py) is unchanged — `obs.held_mark` is now engine truth.
4. **Grab-confirm** — in `PickUp`'s post-pulse closure replace the positional heuristic
   ([MacroExecutor.cpp:1857](../src/Features/Harness/MacroExecutor.cpp)) with the same handle read, compared to
   the target mark's entity via the **in-tree** pattern (`markTable.GetEntityFromMark(mark)` → `{hidx,hser}`,
   the exact pair already used at `:1883`):
   ```cpp
   auto [hidx, hser] = markTable.GetEntityFromMark(mark);
   ServerEnt* plr = server->GetPlayer(1);
   CBaseHandle h = plr ? plr->field<CBaseHandle>("m_hAttachedObject") : CBaseHandle();
   post->held = h && h.GetEntryIndex() == hidx && (uint16_t)h.GetSerialNumber() == (uint16_t)hser;
   ```
   Keep `moved/dist` for telemetry only.

This turns s17/19/24 into SUCCESS, sets `g_heldEntityKey` on the real grab, and **removes the orphan
attachment that caused the §2(b) drag** — the false-negative, the orphan drag, and the Python guess all die
together. *Open guard:* `GetMark` returns 0 for an unmarked entity (== empty hands); for v0 PeTI cubes are
always marked, but note it. Add a `held_mark` round-trip assert to [py/agentloop_smoke.py](../py/agentloop_smoke.py)
since this touches the gRPC surface.

---

## 5. "`pick_up` failed when the cube was on the button — HUH?"

Because `pick_up` confirms a grab by **displacement, not by the engine's held-flag**
([MacroExecutor.cpp:1857](../src/Features/Harness/MacroExecutor.cpp)). A cube resting on the **floor** lifts
~9-13u into the hold position (GOOD s9 `moved=13`→SUCCESS). A cube **seated on a button** already sits near
hold height *and is pinned under a live laser*, so the same grab nudges it only 1-8u — under the `moved>8`
threshold — and the harness declares GRAB_FAILED even though the engine grabbed it. The check looks at
displacement, not at `m_hAttachedObject` (which the same file already reads in `DropHeld`). The model's aim
was fine (`step019.png` shows the reticle dead on the lit cube); the harness's *grab detector* is blind to a
grab that doesn't move the cube far, and a button-seated, laser-pinned cube is exactly that case.

---

## 6. Next directions (cheap → structural)

1. **[~½ day, surgical] Ship the held-flag fix (§4.3).** One engine field kills the false-negative (a), the
   orphan drag (b), and the Python guess (§4.1) at once. **Highest ROI.**
2. **[~½ day] Recon-confirm the §2(b) mechanism honestly.** A `sar_harness_dump_fields`-style per-tick log of
   `m_hAttachedObject` during a scripted button-grab settles "engine-carried vs physics-punted" — the one
   genuine uncertainty in this report. Cheap.
3. **[~1 day] Fix the interpose down-trace (§2c).** Pass the **emitter** to `DownTraceRest`'s filter
   ([LaserGeometry.cpp:53](../src/Features/Harness/LaserGeometry.cpp)), or reject/clamp a seat whose `z` exceeds
   the beam-point `z` by more than a cube half-height. Removes the housing-snap and, transitively, the
   standoff-teleport into the pocket.
4. **[cheap, design call] Add `on_beam: True` to reflective-cube percept state** (and/or soften the grammar's
   "interpose it first" repetition to "a cube already on a beam — by interpose **or** by sitting on a button on
   the beam line"). Makes `redirect_to`-on-seated discoverable without the geometry inference. *Risk:*
   overfitting to this one chamber — validate on a second dual-role map before committing.
5. **[structural] Navmesh dead-pocket escape (§2d).** When `go_to`/`move` thrash one batch then re-guard with
   no net progress, the planner should detect the pocket and route out (or interpose's standoff should never
   seat the player in a sub-hull gap). Larger task — defer until 1-3 land, since fixing §2c removes the
   *specific* pocket this run hit.

*Down-weighted / not actioned:* the `moved=8` strict-`>` "boundary" tweak — superseded by the engine-flag
read (don't tune the threshold). Corridor-entry asymmetry between the two runs — noted, not the cause.
