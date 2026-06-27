# laser_and_button trajectory: adversarial post-mortem + corridor-fix brainstorm

> **Source artifact:** `noteworthy_trajectories/laser_and_button.trajectory` (gemini-3.5-flash, thinking=MEDIUM, 48 steps, terminal **SOLVED**, 0 rejected calls).
> **Method:** the trajectory was dumped to text + 48 annotated frames, then analysed by a multi-lens adversarial workflow — 6 independent lenses (harness-right / model-right / model-wrong / harness-wrong / percept / efficiency), each finding re-checked by a skeptic agent against the dump, frames, and source. 49 findings, 48 confirmed, 1 refuted. A second workflow ran a 5-proposal corridor-fix design panel scored by 3 independent judges. Full findings in **Appendix A**, full proposals + judge scores in **Appendix B**.
> **Date:** 2026-06-26.

> **RESOLVED — SHIPPED 2026-06-27 (branch `yeeh`, commit pending).** The corridor P0 fix landed, but
> as a *different and better* signal than §7.4 proposed, and the recon corrected two claims this doc makes.
> **What shipped:** a one-branch addition to `PuzzleExit::OnInput` latching the EXISTING `chamber_complete`
> on **`@exit_airlock_door.Open`** (`SIG_EXIT_AIRLOCK = 1<<5`, input `Open` only) — **no new proto field, no
> Python change** (the rescope-via-`objective_complete`-proto-field idea in §7.4 was dropped as unnecessary;
> the existing `if chamber_complete: SOLVED` picks it up). **Why this signal, not the §7.4 candidates:** a
> first cut hooked `@exit_door.Open` and the user correctly rejected it — that fires at *puzzle-solve* while
> the player is still at the button (0 navigation), and "navigation stays the puzzle." `@exit_airlock_door.Open`
> is fired by `relay_leaving_level`, gated on `@exit_door.OnFullyClosed` *while the player stands in the exit-
> corridor trigger* — so it requires solved **+ walked through the exit door + up the corridor**, but fires
> BEFORE the elevator. An agent that solves but doesn't walk out gets no latch → must navigate. The §7.4
> fallback (`m_bPowered`+`m_bButtonState` element predicate) was also rejected (chamber-specific, doesn't
> generalize). **Two corrections to this doc's own analysis:** (1) the exit "teleport" is NOT a
> `trigger_teleport` — it's a *seamless bidirectional `linked_portal_door` worldportal pair*
> (`@exit_portal_chamber_side`↔`@exit_portal_elevator_side`); its bidirectionality is what bounced the agent.
> (2) Because the new latch fires *before* the worldportal crossing, the entire §7.2/§7.4 "A\* can't path
> across the seam → annotate-the-elevator / reachability-test" thread is **moot** — we never require the ride.
> **Also shipped:** the ghost-`prop_portal` (0,0,0) origin-reject (§5 P1). **Deferred per user:** cube-dup
> filter, `TELEPORTED` signal, image-history cap. Generality: 108/134 in-scope PeTI maps have
> `@exit_airlock_door`, 96% open it via the player-gated `relay_leaving_level`; spawn-fire outliers + the 19%
> without the door fall back to the unchanged egress OR-set (zero regression). See ROADMAP top-of-mind.

---

## 0. TL;DR

- The **puzzle core** (laser redirect + cube-on-button, steps 5–16, ~11 steps) was solved **cleanly and impressively**. One-shot `interpose`, correct cube-role reasoning, conjunctive-goal decomposition. This is the harness working as designed.
- **~36 of 48 steps (75%) were corridor/elevator navigation**, not puzzle. The exit tail (steps 17–47) is a **31-step trainwreck**.
- **Root cause of the tail (P0):** the cylindrical exit elevator — the one object the agent must reach to win — **has no mark.** Its only marked handle is `@exit_airlock_door` (a door *frame* ~110u above + offset from the capsule). The agent can *see* the capsule but cannot *address* it.
- **The P0 hides two distinct problems:**
  - **(A) False-failure timeout.** Both objectives met by step 13 (POWERED@9, SEATED@13), but `chamber_complete` doesn't latch until step 47. Under the repo-default `max_steps=30` ([run_eval.py:33](../py/run_eval.py)), **this solve would be scored a FAILURE.** The 48-step budget masked it.
  - **(B) Wasted ReAct steps** — 30 steps oscillating on an unmarked target, anti-thesis to the harness's "spend steps on reasoning, not navigation" principle.
- **Recommended fix (panel consensus):** *recon first*, then **rescope the eval boundary** (end the run on objective-met, decoupled from the elevator ride — kills (A), most principled) **+ annotate the elevator** (the cheap KISS win that literally answers "can we mark the elevators", helps (B) and the entrance symmetrically). Reject the `leave`-verb and auto-skip-corridor angles.
- **The subtlety on annotation:** the elevator is trivially markable (a `func_tracktrain` named `*departure_elevator*`), BUT `GoToPlanner`'s flat A* **cannot path across the airlock `trigger_teleport` seam** — so a mark gives a *bearing*, not a guaranteed ride. One in-engine reachability test gates whether `go_to` alone is enough or a teleport-snap is also needed.

---

## 1. The trajectory arc (what actually happened)

| steps | phase | what happened |
|---|---|---|
| 0–4 | **entrance airlock** | spawn elevator → `go_to @entrance_airlock_door` → `move forward` into a *closed* door (EDGE) → a `look 90` 1-tick advance let the airlock auto-teleport fire → **~3935u jump** to the chamber-entrance hallway. |
| 5–16 | **THE PUZZLE (clean, ~11 steps)** | `pick_up` reflective cube[14] → `interpose 8 0.5 10` → **POWERED one-shot** → `pick_up` standard cube[13] → `release 7` → **SEATED**; objectives now met. `move forward 100` → "moved 4600 (COMPLETED)" → **~4600u teleport** to the exit-elevator pocket. |
| 17–47 | **THE EXIT TAIL (31 steps of flailing)** | the capsule is unmarked; agent anchors on `@exit_airlock_door[4]` (the frame), `wait`s facing blank walls, `go_to 4` *yanks it back up* to the frame whenever it nears the capsule, `move forward` re-enters the **bidirectional** teleport and warps it ~4700u back to the chamber, twice. SOLVED at step 47 by a `move right 20` recenter that incidentally fires the elevator I/O. |

Net displacement start→end: 4115u. Net *useful* work: 11 steps. Everything else was traversal.

---

## 2. What the HARNESS got right (do not lose this)

The verb layer is the reason the puzzle read as legible reasoning. Highlights (full list in **A.1**):

- **`interpose 8 0.5 10` → POWERED in one call.** The C++ ([MacroExecutor.cpp:1171-1341](../src/Features/Harness/MacroExecutor.cpp)) carries the held cube to the 50%-beam seat, steps the player *perpendicular* (beamYaw+90) so its hull doesn't occlude the beam, frees the +use grab, teleport-snaps the cube aimed +X at the target, and re-seat-retries until `ConfirmInterception` **and** `m_bPowered` both pass. ~6 fragile ops → one reasoning step. The player's step-9→10 displacement physically matches the perpendicular standoff — verified, not asserted.
- **`release 7` → SEATED via fairness gate + dwell-confirm** (not a naive drop). SEATED *means* seated.
- **Status fields drove every decision and stayed correct.** `cube_type` disambiguated the twin cubes; `powered`/`on_button` flipped at the right steps with real causality.
- **Stable per-entity marks** survived the cube-dropper spawn churn.
- **0 rejected calls / 48** — `validate()` never tripped; the grammar was teachable.
- **A\* `go_to` routing, targetname disambiguation, LOS/in-frame label cull** all held up in the puzzle region.

## 3. What the MODEL got right (gemini-3.5-flash genuinely reasoned)

Highlights (full list in **A.2**):

- **Conjunctive-goal decomposition with no hint** — at step 10 it noticed the exit door was *still closed* after the laser lit, and inferred a second condition (the button).
- **Cube role assignment from `cube_type`**, beating the opaque dropper names.
- **Momentary-vs-latched button reasoning** — it *seated* the cube so `pressed` stays True after it leaves, rather than just standing on the button.
- **Self-diagnosed the airlock teleport from coordinates alone** ("a look can't move coordinates → I passed through an airlock").
- **Robust world-model through the entire tail** — never re-touched the solved puzzle; its confusion was strictly "where's the exit", never "is the puzzle done."
- **Compute scaled to decision difficulty** — terse think blocks for grabs, long ones for real decisions.

## 4. What the MODEL got wrong (the tail is a vision-vs-telemetry failure)

Highlights (full list in **A.3**):

- **Telemetry overrode vision.** Step 22: declared *"I am standing inside the cylindrical exit elevator"* and fired `wait 250` — while the frame showed a **flat grey wall**. It fabricated an "elevator center [4]" and a "~50u radius" from a *door's* distance reading.
- **Conflated the door-frame mark with the capsule.** Step 32: standing *right at the capsule*, `go_to 4` hauled it 369u **back up to the frame**.
- **Misread a teleport as death** ("fell into the pit and respawned") and re-entered the corridor.
- **Confabulation under uncertainty** — confident, detailed, false narratives the frame never constrains.
- **Walked away from a visibly-open elevator** (step 17) by choosing an off-axis door mark.
- **The step-47 SOLVED was incidental**, a lucky recenter — not deliberate entry.

## 5. What the HARNESS got wrong (beyond the corridor)

A **percept-noise cluster**, most of it cheap and several in the *same file/function* as the elevator-annotation edit (full list in **A.4 / A.5**):

| sev | defect | fix |
|---|---|---|
| **P1** | **Silent teleport** — the airlock warp fires inside a move/look batch; `moved_dist` is straight-line, so `move forward 100` reports `moved 4600 (COMPLETED)` with no teleport flag. | emit a `TELEPORTED` detail on a single-tick position discontinuity. |
| **P1** | **Two ghost `prop_portal` marks pinned at (0,0,0)** ride every percept and render as the floating orange/blue rings in every exit frame. The (0,0,0) origin-reject in [`IsHarnessMarkedEntity`](../src/Features/Harness/PuzzleAnnotate.cpp) covers `env_portal_laser` only — **a one-line gap.** | extend origin-reject to `prop_portal` / any zero-origin entity. |
| **P2** | **Cubes are double-marked** — live cube + frozen dropper-box duplicate at z≈430 sharing one targetname (marks 15/16 = noise). | filter the in-dropper duplicate. |
| **P2** | **Doors expose `state={}`** — no open/closed bit (datamap-only `m_toggle_state`, snapshotter misses it). Agent burned ~6 steps reading door-open from dim pixels. | wire an `{open}` bit (double-duty — it's also the rescope signal). |
| **P2** | **Duplicate door targetnames** (`@entrance_airlock_door`×2, `@exit_airlock_door`×2) defeat the name disambiguation the prompt promises. | suffix-disambiguate by side. |
| **P2** | **`CheckEdge` calls a closed door a cliff** — `EDGE` conflates "blocked by door" with "floor drop." | add a forward wall/door probe; reserve EDGE for real drops. |
| **P1** | **O(steps²) token cost** — one stateful Gemini chat re-bills *all* prior frames every step (3.8k→122k input on the last step; 3.05M total; 41% accumulated images). A long tail is catastrophic, not linear. | cap image history to last-K frames (last-3 saves 88%, last-1 saves 96%). |

> **One finding was refuted** (kept for honesty, **A.7**): the claim that the 2D distance metric "collapses the vertical shaft" — actually the `dist=18` false-arrival on mark 4 is the door's default 48u `reachRadius`, not a Z-collapse.

---

## 6. Prioritized backlog (answering "what else should we prioritize?")

1. **P0 — Corridor/exit** (§7).
2. **P1 — Cap image history.** Highest-leverage *eval-infra* fix, independent of corridors: bounds the cost of any tail. ~5 lines in [gemini_agent.py](../py/llm_eval/gemini_agent.py).
3. **P1 — Percept-noise cluster**, batched into one `IsHarnessMarkedEntity` PR: ghost-portal origin-reject (1 line) + cube-dup filter + the silent-teleport `TELEPORTED` signal. Removes 2–4 junk marks from *every* percept.
4. **P2 — "steps-after-objective-met" metric** so a puzzle-solved-but-egress-flailed chamber is *flagged*, never silently scored clean-pass or false-fail.
5. **P2 — door open/closed bit + `CheckEdge` door-vs-cliff** (these also unlock the rescope fix).

---

## 7. Corridor deep-dive: can we annotate the elevators?

**Literal answer: yes, trivially — but annotation alone probably won't let `go_to` *ride* it, and that's the whole subtlety.**

### 7.1 Ground truth (BSP + source recon)

- The rideable capsule is a **`func_tracktrain`** whose targetname always contains `departure_elevator` (exit) / `arrival_elevator` (entrance) across PeTI exports; the visible glass tube is a sibling `prop_dynamic` `*elevator_model*` with no useful OBB.
- Its `abs_origin` coincides with the `*player_teleport*` ride spot, so **the capsule center IS the spot you must stand on** to fire the exit I/O.
- [`EntitySnapshotter`](../src/Features/Harness/EntitySnapshotter.cpp) **already auto-registers** `func_tracktrain` (it gets a position for free). The whole mark pipeline keys off one predicate (`kClassColors` membership). So annotating it is **one name-gated entry in `IsHarnessMarkedEntity` + a color** — no proto, no offset, no schema change.
- **Correction to our own mental model (and the prompt):** the exit latches on **ENTRY** into the capsule (`in_elevator.SetValue(1)` → `@relay_pti_level_end.Trigger` fires the instant you step in, *before* the descent) — **not on riding it out.** That's why step 47 SOLVED the moment it centered. The prompt's *"wait 250 for the elevator to finish"* advice ([gemini_agent.py:89-95](../py/llm_eval/gemini_agent.py)) is partly wrong and should change regardless.

### 7.2 The load-bearing gotcha

`GoToPlanner` is **flat, contiguous-world A\*** (16u grid, single refZ floor probe, kProbeDown=128, 400-cell cap). It **cannot path across the airlock `trigger_teleport` seam.** So a marked elevator gives the model a *named target + a stable bearing* — but not a *guaranteed traversal* onto the ride spot. **Annotation is necessary but possibly not sufficient.**

### 7.3 The five proposals (full detail + judge scores in Appendix B)

| proposal | fixes | one-line verdict |
|---|---|---|
| **mark-the-elevator** | (B) partial | add `func_tracktrain` to the marked set; bearing not guaranteed ride. |
| **waypoint-plus-telemetry** | (B) partial | the tightest version: one name-gated entry in `IsHarnessMarkedEntity`. Pragmatist's #1 (18). |
| **auto-skip-corridor** | (B) | harness auto-advances through the corridor; **rejected** — hides locomotion the agent can't reason about, most new C++. |
| **leave-verb** | (B) | one gated verb that rides the elevator; **rejected** — gates on a door open-state field recon already killed (doors return `{}`), and the wait-out-the-ride loop solves a non-problem (entry is the trigger). |
| **rescope-eval-boundary** | **(A) entirely** + makes A* moot | end the run on objective-met, not the ride. Eval-scientist's & adversary's #1 (16/16). |

**Scorecard (sum of principle / KISS / generality / robustness, max 20):**

| proposal | pragmatist | eval-sci | adversary |
|---|---|---|---|
| mark-the-elevator | 17 | 13 | 13 |
| waypoint-plus-telemetry | **18** | 15 | 14 |
| auto-skip-corridor | 11 | 13 | 13 |
| leave-verb | 11 | 11 | 12 |
| rescope-eval-boundary | 10 | **16** | **16** |

The split is informative: the **pragmatist** optimizes pure KISS (mark the elevator, one file). The **eval-scientist** and **adversary** both rank **rescope #1** because it's the only angle that kills the *false-failure* (A) and makes the unverified A*-reachability question *moot* by never requiring the elevator.

### 7.4 Recommended plan (sequenced, recon-gated)

**Step 0 — recon first, zero code** (decides everything; both gates cheap):
- **(a)** Run `py/bsp_recon/dump_ents.py` on `laser_and_button.bsp` + 2–3 other PeTI exports → grep output edges for the one that **opens `@exit_*_door` on solve** (lead: `door_wants_to_close_branch.SetValue` / the door's `Open` input). Confirm it fires *strictly before* `ReadyForTransition` and generalizes (match name-suffix/class — the `InstanceAuto` prefix varies). *Offline, safe to run from the agent terminal.*
- **(b)** Stand the player in the exit pocket post-airlock and run `sar_harness_laser_reachability_test` → does the capsule center read `.` reachable or `x`/severed across the seam? *Needs a game instance — user runs this.*

**Step 1 — primary fix: rescope** (kills (A) forever): latch a `g_objective` / `objective_complete` bit off the existing `PuzzleExit::OnInput` AcceptInput hook on the door-open edge — **or fall back to the element-status predicate that already exists and is read today** (`point_laser_target.m_bPowered` + `button.m_bButtonState`, more general). End `run_eval` on *that*, decouple from the ride, drop the elevator caveat from the prompt. **One proto field** (`make proto` + 32-bit rebuild + `git checkout` the reformatted `*.pb.cpp` per the format.sh footgun). **This changes what "solved" means** — the run ends at the puzzle objective, dropping the implicit "agent physically reached an exit" check. Arguably *more* correct for a reasoning eval, but it's a real semantic call to sign off.

**Step 2 — complementary KISS win: annotate the elevator** (answers the literal question; helps (B) + the entrance symmetrically): the one-file name-gated `func_tracktrain` mark + `@exit_elevator`/`@entrance_elevator` synthetic names. Gate on recon (b): if **reachable** → `go_to @exit_elevator` rides it; if **severed** → the mark is still a far better bearing than the door (which yanks backward), and you add a **teleport-snap onto the ride spot reusing the in-tree FCPS Teleport primitive** (`FCPS.cpp`, 0 new offsets).

**Reject:** `leave`-verb and auto-skip-corridor (see §7.3).

> **Net:** rescope removes the false-fail and stops measuring traversal (the principled fix); annotating the elevator is the cheap literal answer + entrance polish — but it's downstream of one A* reachability test, so run the two recon commands before touching C++.

---

## Appendix A — All verified findings

_49 findings across 6 adversarial lenses; each independently re-checked by a skeptic agent against the dump, frames, and source. 48 survived, 1 refuted. Claims/evidence trimmed for readability; full text in the workflow result JSON._


### A.1 What the HARNESS got right (11)

**`[info]` interpose with target= solved the whole laser sub-puzzle in one verb** — _confirmed/high_  
At step 9 the model issued `interpose 8 0.5 10` while holding the reflective cube[14] and got POWERED in a single call -- the harness internally carried the held cube to the 50%-beam seat, stepped the player perpendicular to the beam so its hull wouldn't occlude E->P, freed the +use grab, teleport-snapped the cube onto the beam with +X aimed at target[10], and re-seat-retried until ConfirmInterception passed AND m_bPowered latched. This collapses ~6 fragile low-level operations into one legible reasoning step, which is exactly the harness's stated goal.  
› _Evidence:_ step 9 ACTION interpose 8 -> RESULT POWERED 'interpose: cube on beam, target powered at (448 376 18)'; mechanism in MacroExecutor.cpp:1171-1341 (carry MarchTo 1221, FindPlayerStandoff perpendicular w/ beamYaw+90 at 1251-1264, FreeGrab 1240, re-seat loop + ConfirmInterception 1277-1315). Frame step_09.png shows cube[14]…  

**`[info]` POWERED vs ON_BEAM result code told the model the redirect already succeeded, suppressing a needless redirect_to** — _partial/high_  
interpose 8 0.5 10 honored the optional target and powered point_laser_target[10] in one shot, returning POWERED (not ON_BEAM); the model correctly read the laser objective as complete -- citing the redundant telemetry state {'powered': True} -- and moved to the floor-button sub-goal with no redirect_to. The POWERED/ON_BEAM result-code split (taught in the system prompt) is sound design, but in this trajectory the one-shot interpose made redirect_to moot rather than the result code suppressing an otherwise-tempting redundant call; the powered-state telemetry field is a second signal pointing the same way.  
› _Evidence:_ MacroExecutor.cpp:1335 sets result_code = targetMark ? POWERED : ON_BEAM; step 10 reasoning pivots immediately to 'the laser target [10] is now powered ... I likely need to press the floor button [7]' with no redirect_to. point_laser_target[10] state flips powered:False->True between step 9 and step 10 telemetry.  
› _Skeptic's correction:_ Mechanical facts all check out. MacroExecutor.cpp:1335 is verbatim `r.set_result_code(targetMark ? "POWERED" : "ON_BEAM")`. The model issued `interpose 8 0.5 10` (step 9, dump line 816); macro_grammar.py:165 parses arg[2] into target_mark=10, so the POWERED branch fired. Step 9 RESULT is `result_code:'POWERED', detail:'interpose: cube on…  

**`[info]` release-on-button SEATED the cube via a fairness-gated, dwell-confirmed place rather than a naive drop** — _confirmed/high_  
At step 13 `release 7` returned SEATED with 'm_bActivated 1/1 drift 0u'. The harness did NOT just drop the cube: ComputeSeat traced the real press plane, CheckFairness proved the place was something a hand-drop from the player's spot could have done, the player was stepped off the seat, the cube was teleported dead-centre with velocity zeroed, and SEATED was only reported after m_bActivated read pressed twice across a dwell gap AND the cube hadn't drifted off. This is the right rigor for a legible eval: SEATED means genuinely seated, so the model can trust it and leave.  
› _Evidence:_ step 13 ACTION release 7 -> RESULT SEATED 'release mark 7: m_bActivated 1/1 drift 0u'; mechanism ComputeSeat (MacroExecutor.cpp:229-282), CheckFairness 366-445, displace+seat 1980-2019, double-read dwell gate 2074-2107 (seated = act1 && fin->act && fin->onSeat). step_13.png shows the seated cube on the button with '[Do…  

**`[info]` Per-class status fields (pressed/powered/on_button) drove every puzzle decision and stayed correct through the solve** — _confirmed/high_  
Per-class status fields (powered off m_bPowered, pressed off m_bButtonState, on_button off m_bActivated, cube_type off m_nCubeType) were the load-bearing telemetry for this chamber and stayed correct through the solve. cube_type disambiguated the two identically-named cubes and steered interpose to reflective[14]; target powered flipped F->T at step 10 post-interpose; cube[13].on_button flipped F->T at step 14 post-seat. button.pressed tracks ANY weight: it first flipped True at step 7 (player weight via go_to 7), reverted to False steps 8-12 (player off button), and returned True at step 13 -- the seat-relevant transition the finding cites.…  
› _Evidence:_ powered read off point_laser_target.m_bPowered (PuzzleAnnotate.cpp:58-59 comment + RedirectConfirm reads m_bPowered MacroExecutor.cpp:1153); cube_type via m_nCubeType lets the model distinguish reflective[14] from standard[13]. Telemetry transitions: target powered True @ step10, button pressed True @ step13, cube on_b…  

**`[info]` Targetname disambiguation let the model tell otherwise-identical doors apart and pick the right one** — _partial/high_  
The harness surfaces each entity's in-game targetname (e.g. @entrance_door, @exit_door, @exit_airlock_door) and the system prompt instructs the model to read it; this let the model distinguish entrance-vs-exit among six otherwise-identical prop_testchamber_door marks and route to the exit (step 4 -> go_to @entrance_door[2]; step 14 -> @exit_door[9]; step 17 -> @exit_airlock_door[4]). Caveat: of the six door props only four targetnames are distinct (marks 1&3 both @entrance_airlock_door, 4&6 both @exit_airlock_door), so names alone are not a unique key -- the model also used position/distance to break the two degenerate pairs (e.g. step 17 cho…  
› _Evidence:_ prompt_sent at every step tags marks with names ([2] '@entrance_door', [9] '@exit_door', [6] '@exit_airlock_door'); gemini_agent.py:129-133 emits the targetname into the per-mark line. step 14 reasoning: 'walk to the exit door [9]'; step 15: 'exit door [9] is wide open ... exit airlock door [6]'. cube_type and these na…  
› _Skeptic's correction:_ Core mechanism + model behavior CONFIRMED, but two supporting facts are wrong. CONFIRMED: target_name is a real harness field end-to-end (harness.proto:117 target_name; Portal2HarnessImpl.cpp:102 set_target_name(slot.targetName); sourced from server->GetEntityName at PuzzleAnnotate.cpp:415,505). It is surfaced per-mark at gemini_agent.py:…  

**`[info]` Stable per-entity mark assignment kept identities pinned across the cube-dropper spawn churn** — _confirmed/high_  
Stable per-entity mark assignment kept the model's cube references valid across the dropper-spawn churn. At step 0 marks 13 (standard) and 14 (reflective) sit at the dropper origins (z~430); at step 7 the dropper fires and these two entities drop to the floor (z~18-21) keeping marks 13/14, while two freshly-spawned dropper-resident cubes are appended as marks 15/16 (frozen at z~430 for the rest of the run and never referenced). Because MarkTable (MarkTable.cpp:42-54) assigns a mark once per entity key and appends new entities after already-handed-out marks, marks 13/14 never reshuffled or swapped types across all 48 steps, so pick_up 14 / int…  
› _Evidence:_ MarkTable.cpp:42-54 (fresh marks appended after already-handed-out ones, existing entities keep their mark); step 0 has 14 marks, step 8 onward has 16 (the at-rest cubes 13/14 + their dropper-origin duplicates 15/16 at z=430). pick_up 14 @ step8 and interpose using cube[14] @ step9 both resolved to the same physical cu…  

**`[info]` Pre-flight grammar+percept validation yielded 0 rejected calls across 48 steps -- no wasted re-prompts** — _confirmed/high_  
Every action the model emitted passed macro_grammar.validate() on the first try (n_calls == n_steps, zero rejection entries). The validator checks verb/arg-type/range, mark existence, mark-class-for-verb (e.g. interpose's emitter must be env_portal_laser, pick_up's mark must be grabbable), and holding-state -- all locally, with no gRPC round-trip or game step. That the model never tripped any of these means the grammar was teachable enough that the model's intent always mapped to a legal command, so no step was burned on a malformed-verb correction.  
› _Evidence:_ macro_grammar.py validate() 241-281 + _check_interpose 199-216 / _check_redirect 219-238; gemini_agent.py:284-300 validates before stepping and re-prompts in-session on reject. Summary shows each step's calls list has exactly one accepted=true entry and no rejected ones (e.g. step 9 interpose 8 0.5 10 accepted first tr…  

**`[info]` go_to A* obstacle routing carried cubes and reached cube-on-button targets without bulldozing** — _confirmed/high_  
One nuance, not an error: the cube-on-button overlap-skip (MacroExecutor.cpp:726-731) is genuine code but is NOT exercised in steps 7/10/12 of THIS run — no go_to in this trajectory targets a cube already sitting on a button (at step 12 the standard cube is still carried, not yet placed). It's cited as a general capability, which is fair; just note it as latent-but-unused-here rather than demonstrated. The step-specific claims (7 standoff=40u, 10 routed-around-reflector, 12 held-cube-skipped close-approach to button) are all directly demonstrated and accurate.  
› _Evidence:_ GoTo standoff for grabbable targets (MacroExecutor.cpp:1554-1566 reachRadius = footprint+body+kApproachGap), InjectObstacles skips heldKey + target-overlapping props (707-745), end-of-march velocity zeroing (1610-1614). Steps 7/10/12 all RESULT SUCCESS 'dist=N after march' with reached=True; held cube[13] correctly ski…  

**`[info]` Annotation LOS-cull + in-frame gate kept the percept honest (no marks bleeding through walls)** — _partial/high_  
The annotation layer's CULL is applied to the rendered on-screen LABELS only: a label is drawn only if the entity is in-frame (InFrame, PuzzleAnnotate.cpp:214) and has LOS (EntityVisible center+8 corners, 220-221), and the OBB box is depth-tested so it occludes naturally; env_portal_laser at (0,0,0) is rejected (86-93). This keeps the rendered mark numbers matched to on-screen entities (frames step_05/08/09/13/16). HOWEVER the percept is a JOINT image+telemetry observation, and the telemetry mark list is NOT gated: markTable.RebuildFromWorld() (164) runs before the overlay cvar (166), and entities.py:119/gemini_agent.py:127 emit every mark>0…  
› _Evidence:_ PuzzleAnnotate.cpp:136-154 InFrame gate, 120-131 EntityVisible (center + 8 corners), 214/220-221 the cull is applied before drawing the label; env_portal_laser at (0,0,0) re-emit artifact rejected 86-93 so phantom laser segments never get a mark. Frames step_08/09/13/16 show labels only on on-screen entities.  
› _Skeptic's correction:_ The reviewer's CODE and FRAME evidence are all real, but the central THESIS is half-wrong. What I verified true: PuzzleAnnotate.cpp:136-154 InFrame gate, 120-131 EntityVisible (center+8 corners, MarkVisible TraceRay frac>0.97), and the cull applied before the label (line 214 InFrame-continue, 220-221 EntityVisible-continue) are exactly as…  

**`[info]` ADVANCED vs BLOCKED/STUCK move semantics gave the model actionable re-plan signal (used correctly at the corridor edge)** — _partial/high_  
The Move/GoTo result vocabulary's ADVANCED code (MacroExecutor.cpp 1764-1772 / 1640-1650) plus the moved_dist scalar gave the model a truthful, re-plannable signal: at steps 41 and 46 the model read the moved magnitude (4703 vs 117) to correctly distinguish 'I was relocated across the corridor' from 'I bumped a doorframe', and re-planned from the new spot (look -90 at step42 -> upper catwalk; move right 20 at step47 -> SOLVED), confirmed against the rendered frames. Caveat: the next verbs were wait/look/move (not 'look/go_to'), and at step 37 the model re-planned on a WRONG read of the EDGE (thought elevator back-wall, was a corridor edge), s…  
› _Evidence:_ MacroExecutor.cpp Move advanced-detection 1764-1772 (moved>kReachRadius && guard) and GoTo 1642-1650; detail strings 'advanced N units, M to go'. Steps 37/41/46 returned ADVANCED with nonzero moved_dist; the model re-read percept after each rather than repeating the exact verb.  
› _Skeptic's correction:_ CODE AND STEP-MAPPING ARE REAL AND CORRECT. MacroExecutor.cpp Move ADVANCED logic is exactly at 1764-1772: advanced = moved > kReachRadius(48) && (code==WALL||EDGE||STUCK); detail = "moved N units then CODE (ADVANCED)". GoTo ADVANCED at 1640-1650: advanced = !reached && moved>48 && code==BLOCKED; "advanced N units, M to go (CODE)". I conf…  

**`[info]` Annotation in the puzzle region is high-quality and demonstrably load-bearing** — _partial/high_  
In the puzzle (steps 5-16) the marks are clean and stable and provided the load-bearing per-entity INDICES the model fed to pick_up/interpose/release (one-shot POWERED at step 9, SEATED at step 13, 0 rejected calls). But the reflective-vs-standard cube discrimination that let the model choose cube [14] as reflector came from the cube_type telemetry state field (m_nCubeType, EntitySnapshotter.cpp:161-162), NOT the annotation box — both cube classes are colored identically gold (kClassColors:47-48), and the model's step-7 thinking explicitly relies on the cube_type field. Cited frames step_07/09/13 do show legible marks; the cited step_16 does…  
› _Evidence:_ frame step_09 (cube [14] crisply boxed gold with readable label on the reflective cube), step_13 (button/door/laser region marks legible), step_16 (exit door region). step_09 action `interpose 8 0.5 10` -> POWERED in one shot; step_13 `release 7` -> SEATED. kClassColors (PuzzleAnnotate.cpp:46-58) colors exactly these c…  
› _Skeptic's correction:_ Core claim holds but is over-attributed on one axis and cites one bad frame. CONFIRMED: kClassColors (PuzzleAnnotate.cpp:44-73) colors exactly the cited classes — gold cubes (47-48), green buttons (49-55), red emitter (57), magenta target (58), white doors (56); catcher/relay deliberately unmarked (59-61). Step 9 `interpose 8 0.5 10`->POW…  


### A.2 What the MODEL got right (8)

**`[info]` Correct conjunctive-goal decomposition (laser AND button)** — _confirmed/high_  
The model inferred this is a two-condition chamber (power laser target + press floor button) by reasoning over state, not by being told. At step 10, after the laser target went powered=True, it explicitly noted the exit door was still closed and hypothesized a second condition, pointing at the unpressed floor button[7].  
› _Evidence:_ step_10 thinking: 'the exit door ... is closed ... maybe both the laser target being powered AND the floor button being pressed are needed' -> chose go_to 13 to fetch the standard cube for the button. Confirmed correct at step_13 ('[Door Opening][Button Activated]' once both pressed=True and powered=True).  

**`[info]` Cube role assignment from cube_type field, beating misleading dropper names** — _partial/high_  
The model correctly assigned reflective cube[14]->beam reflector and standard cube[13]->floor button, citing the cube_type telemetry field (derived from m_nCubeType: entities.py:17, EntitySnapshotter.cpp:161-162) over the opaque dropper IDs. Choosing the wrong cube would have failed the laser objective, but via interpose's NOT_POWERED reject (MacroExecutor.cpp:1329) on the model's actual code path -- NOT the NOT_REFLECTOR gate at line 1365, which lives only in redirect_to (a verb the model never used). The reflective geometry is also visually evident in frame step_09, so the field is a confirmation rather than the sole disambiguator.  
› _Evidence:_ step_07 thinking: 'one's reflective, and the other is standard - I can tell from their cube types despite their potentially misleading names'. Telemetry cube_type derives from m_nCubeType 0/2 (EntitySnapshotter.cpp:161-162). Names cubedropper40319 (standard) vs cubedropper46201 (reflective) carry no type hint. Model ne…  
› _Skeptic's correction:_ The behavioral observation is real but the cited causal mechanism is on the WRONG verb. CONFIRMED parts: (a) the step-7 thinking quote is verbatim in the dump (laser_and_button.md:658 "one's reflective, and the other is standard - I can tell from their cube types despite their potentially misleading names"); (b) the model assigned reflect…  

**`[info]` One-shot interpose with target= — correct verb composition (partly harness-assisted)** — _confirmed/high_  
At step 9 the model issued `interpose 8 0.5 10` in one ACCEPTED call (holding reflective cube [14]) and got POWERED one-shot (n_rejected=0). The harness owns the geometry entirely: InterposeGate computes the beam seat (MacroExecutor.cpp:1012) and ComputeRedirectYaw aims the cube from seat->target (LaserGeometry.cpp:68), looping kRedirectTries=3 on m_bPowered (1286,1304) — the model solved no reflection angle. The model's genuine wins were assigning the reflective cube as reflector (it read cube_type:reflective) and picking a reachable target/mid-beam percent. Note: the interpose->redirect_to one-step collapse is explicitly documented in the s…  
› _Evidence:_ step_09 thinking: 'interpose <emitter> <percent 0-1> [target] ... try interpose 8 0.5 10 ... confirm that target [10] is both reachable and within our field of view'. RESULT POWERED in one shot. Mechanism: MacroExecutor.cpp:1270-1314 yaws cube +X at target and loops kRedirectTries until m_bPowered.  

**`[info]` Self-diagnosed the airlock teleport purely from the position jump** — _confirmed/high_  
After `look 90` the player position jumped ~3935u from (-1904,2874) to (192,-456) with no explicit teleport signal. The model correctly reasoned that a look command cannot move coordinates, concluded it had passed through the airlock/portal, and re-localized to the entrance hallway between marks [1] and [2] — recovering cleanly from the EDGE at the closed entrance door instead of repeating the blocked move.  
› _Evidence:_ step_03 thinking: 'The position change is too drastic to be a simple directional shift from the look action ... we're in the hallway between entrance airlock door [1] and entrance door [2]'. Prior step_02 hit EDGE (moved 4 units) at the closed door; model pivoted to look rather than re-issuing move.  

**`[info]` Name-based door disambiguation used to find the way OUT** — _confirmed/high_  
The model consistently read targetname suffixes to tell otherwise-identical prop_testchamber_door marks apart: it treated @entrance_door[2] as the way in (step 4-5) and @exit_door[9]/@exit_airlock_door[6] as the way out (steps 14-16), navigating the correct exit chain in order (button-seat -> @exit_door[9] -> @exit_airlock_door[6]) without confusing them with the entrance doors.  
› _Evidence:_ step_14 thinking: 'walk to the exit door [9]'; step_15 thinking: 'made it to Exit Door [9] ... leading directly to that exit airlock door, @exit_airlock_door [6]'; step_16 chose go_to 6. All four doors share class prop_testchamber_door; only the names distinguish them.  

**`[info]` Anticipated the button-release-and-leave failure mode (seat the cube)** — _confirmed/high_  
The model recognized that standing on a momentary floor button only holds it while occupied, so walking to the exit would drop the door. It chose `release 7` to SEAT the standard cube on the button (keeping pressed=True after the player leaves) BEFORE heading out — correct momentary-vs-latched reasoning.  
› _Evidence:_ step_13 thinking: 'If I just walk through the exit, I'll step off the button, and the door is going to close ... the only logical move is to place the cube [13] on the button [7]'. RESULT SEATED (m_bActivated 1/1 drift 0u); step_14 confirms button[7] pressed=True, cube[13] on_button=True after player moved off.  

**`[info]` Robust dual-cube world-model held even through the 31-step tail flailing** — _confirmed/high_  
Even while oscillating across the exit teleport corridor (steps 17-47), the model never re-touched the solved puzzle: it never tried to re-press the button, re-aim the laser, or pick the cubes back up. Its confusion was strictly 'where is the exit elevator', not 'is the puzzle done'. This is the correct prioritization — the wasted steps are navigation, not a corrupted puzzle model.  
› _Evidence:_ Step verb list (lines 1383-3559): tail is exclusively go_to 4/6, wait, look, move forward/back/right — zero puzzle-mutating verbs (no pick_up / interpose / redirect_to / release). Button[7].pressed stays True through step 17 telemetry onward.  

**`[info]` Zero rejected calls and dense, terse reasoning across the puzzle window** — _confirmed/high_  
The entire run had 0 rejected calls (terminal SOLVED), and the puzzle-solving window (steps 5-16) spent its tokens efficiently: short think blocks for the trivial pick_up steps (step 8 think=25 tok, step 11 think=24 tok) and longer ones only where a decision mattered (step 7 think=835, step 10 think=353, step 14 think=535). The model scaled its compute to decision difficulty rather than over-thinking grabs.  
› _Evidence:_ usage lines: step 8 'out 77 (think 25)', step 11 'out 75 (think 24)' for plain pick_ups vs step 7 'out 910 (think 835)' and step 10 'out 439 (think 353)' for the role/decomposition steps. No call was rejected (summary terminal SOLVED, factual arc states 0 rejected).  


### A.3 What the MODEL got wrong (9)

**`[P1]` Declares 'inside the elevator' while frame shows a flat wall — telemetry overrides vision** — _confirmed/high_  
At step 22 the model declares it has "entered the cylindrical exit elevator (distance to elevator center [4] is only 18 units, meaning I am standing inside it)" and fires wait 250, while frame step_22.png shows a flat grey corridor wall with the mark-4 ring on the wall (no capsule/door/interior). The error compounds a category mistake: mark [4] is prop_testchamber_door "@exit_airlock_door" (a door, not the elevator — the capsule is unmarked), so "elevator center [4]" and the "~50-60u radius" are fabricated. This is motivated reasoning, not pure telemetry-over-vision: at step 21 the agent examined the same wall, called it "the wall that the do…  
› _Evidence:_ step 22 reasoning ('distance to elevator center [4] is only 18 units, meaning I am standing inside it'); frame step_22.png (flat corridor wall, mark-4 ring on wall, no elevator); contrast frame step_17.png and step_46.png which DO show the glass capsule.  
› _Fix:_ Prompt should force a vision-grounded self-check before wait 250 ('does the screenshot show an open cylindrical glass capsule around you? if not, you are NOT inside'). dist-to-a-mark is not evidence of being inside an unmarked capsule.

**`[P1]` Anchors navigation on mark 4 (the airlock DOOR FRAME), conflating it with the elevator capsule** — _confirmed/high_  
Mark 4 (@exit_airlock_door, prop_testchamber_door at -2688,-2032,32) is the airlock door FRAME at the top of the exit catwalk, not the elevator capsule (which is unmarked, sits at the bottom ~Z=-80 to -112, i.e. ~110-140u lower and 370-660u away). The model conflates mark 4 with the capsule and repeatedly go_to 4 / aim_at 4 / wait-at-4. go_to 4 routes to the door-frame entity origin: at step 32 the player was at the bottom beside the real capsule (-2312,-2009,-80); go_to 4 hauled it 369u BACK UP to the frame (-2681,-2019,33), where it then wait-250'd fruitlessly (steps 33-34). Active mark-4 anchoring spans steps 17-35; the solve only came aft…  
› _Evidence:_ marks list step 0: [4] @exit_airlock_door pos=(-2688,-2032,32); step 32 player=(-2312,-2009,-80) -> step 33 player=(-2681,-2019,33) after 'go_to 4' (moved 369u BACK up); steps 33,34 both 'wait 250' at the frame; PuzzleAnnotate.cpp:56 (prop_testchamber_door is the only marked class here; capsule is unmarked).  
› _Fix:_ Model should treat a door-frame mark as a threshold to pass THROUGH, not a destination to camp on; once dist-to-mark stops changing across go_to attempts, stop targeting that mark. (Harness contributory: capsule is unmarked, so the model had no positive handle — see percept findi…

**`[P1]` Misreads the teleport-corridor jumps as 'fell into a pit and respawned'** — _confirmed/high_  
At step 38->39 the player jumps from (-2674,-2026,33) to (880,1125,1) during a `wait 100`; the model concludes it "fell into the pit and respawned" (step 39 only). It is actually the bidirectional PeTI airlock teleport corridor (same corridor traversed at step 16's "moved 4600") running it from the exit-elevator region back to the chamber-exit-door region. Because it believes it fell, it re-orients (look 180), go_to 6 (step 40), then move forward 100 (step 41) which teleports it BACK to the exit region (step 42, "moved 4703 then STUCK") — a pure round-trip oscillation. The model never forms a correct model of the corridor (it re-reads the sam…  
› _Evidence:_ step 38 player=(-2674,-2026,33), wait 100; step 39 player=(880,1125,1) with reasoning 'respawned... lethal fall'; step 41 go_to 6 + move forward 100 -> step 42 player=(-2624,-1984,36) 'moved 4703 units then STUCK'; MacroExecutor Move COMPLETED/ADVANCED with a 4600-4700u 'moved' is the teleport, not a fall.  
› _Fix:_ Teach the model the airlock-teleport pattern: a single move yielding moved>>budget with a ~3500u position delta = corridor teleport, not death. After such a jump, do NOT re-enter the door you just came from (you'll bounce back).

**`[P2]` Walks AWAY from a clearly-visible open elevator (frame 17) by choosing go_to 4** — _partial/high_  
At step 17 (player (-2417,-2055,-11), facing +X toward the exit pit) the model's frame shows the cylindrical exit elevator capsule visible ahead/below, and its own thinking identifies it — yet it issues go_to 4, walking to the @exit_airlock_door mark at (-2688,-2032,33), which is in the OPPOSITE (-X, uphill) direction (mark 4 was at bearing -175, behind the player), landing in a side corridor facing a wall (frame 18) with the elevator no longer in view. This visual-vs-telemetry mis-resolution started a ~28-step recovery. It is a real model error but is heavily induced by a harness gap: the elevator capsule is not a marked entity (PuzzleAnnota…  
› _Evidence:_ frame step_17.png (open glass capsule ahead, mark 4 in front of it); step 17 action go_to 4 -> player (-2676,-2018,33) frame_22 flat wall; step 45 player=(-2417,-2055,-10) ~identical to step 17, then move forward x3 -> SOLVED at step 47.  
› _Skeptic's correction:_ The finding's positional skeleton is REAL and I confirmed every coordinate from the dump. Step 17 player=(-2416.6,-2055.0,-10.7) eye_yaw=-10; go_to 4 routed to step 18 player=(-2675.8,-2018.1,33.0); step 45 player=(-2417.4,-2055.0,-10.2) is indeed ~identical to step 17. The frames confirm the core regression: frame_17 shows the cylindrica…  
› _Fix:_ When the screenshot shows an open elevator/opening directly ahead, prefer move forward over go_to to an off-axis door-frame mark. go_to optimizes 2D dist to an entity origin, which is the wrong objective for 'enter the thing in front of me.'

**`[P2]` Fabricates a coherent-sounding but false world-model and commits to it (confabulation under uncertainty)** — _confirmed/high_  
The model repeatedly invents detailed physical explanations that are wrong and then acts on them: step 21 'red oval is the portal gun reticle... glass window... door behind glass'; step 33 'we walked down the ramp directly INTO the elevator, glass door closed behind us' (it was at the door frame at z=33, never moved into the capsule); step 36 'I saw a glass elevator door on entry but now there's no glass, so the door is NOT closed.' Each is a confident narrative untethered from the frame, and it drives a wasted action. The long think traces (e.g. 1565 think-tokens at step 21, 1219 at step 22) are spent rationalizing the dist=18 number rather…  
› _Evidence:_ step 21 reasoning (red oval = reticle, door-behind-glass); step 33 reasoning ('walked directly into the cylindrical exit elevator... glass door just closed behind us') vs player only moved within z=33 frame region; step 36 reasoning (saw glass on entry, now none); usage think tokens step 21=1565, step 22=1219.  
› _Fix:_ Penalize unverifiable spatial claims; prompt the model to state, per step, one concrete thing it SEES in the frame (object + screen location) before reasoning about coordinates, so vision constrains the narrative.

**`[P2]` Never connects the move-forward 'moved 4600 units' signal to a teleport across ~20 turns** — _partial/high_  
The model verbally recognizes the move-forward 'moved 4600 units' = a corridor teleport at step 22 but never operationalizes it: it re-crosses the airlock corridor twice more (the unintended `wait 100` B->A warp at step 38->39, misread as a pit-death respawn; and the forward A->B re-crossing at step 41), instead of treating mark 4 and mark 6 as the two ends of ONE @exit_airlock_door corridor. Note: only step 41 of the cited 41/44/46 is a teleport re-crossing; steps 44 (218u stair descent) and 46 (117u elevator approach) are correct short moves WITHIN the exit-elevator region that DID advance it to the SOLVED state.  
› _Evidence:_ step 16 RESULT 'moved 4600 units (COMPLETED)'; step 22 reasoning acknowledges 'transition or portal'; yet step 41 move forward 100 -> 'moved 4703 then STUCK' (re-teleport), step 42 player back at (-2624,-1984,36).  
› _Skeptic's correction:_ CORE OBSERVATION CONFIRMED, SPECIFIC EVIDENCE PARTLY WRONG.

Confirmed: step 16 `move forward 100 -> moved 4600 units (COMPLETED)` is a real teleport (dump line 1319: moved_dist=4599.8; I computed the actual 3D delta (823,1211)->(-2417,-2055) = 4599.9u). At step 22 (dump line 1742-1744) the model DID explicitly compute the displacement ("…  
› _Fix:_ After identifying a move as a teleport once, the model should tag the @exit_airlock_door threshold as 'crossing this warps me' and switch to small (move 20-50) probes on the post-warp side rather than re-crossing.

**`[P3]` Repeats identical failing verbs expecting different results (go_to 4 x3, wait 250 x4)** — _partial/medium_  
In the exit tail the model fails to break a wait/micro-poke loop near @exit_airlock_door [4]: it issues 7 waits across the exit region (steps 20,22,23,33,34,35,38) — including three identical wait250 from the byte-identical pose at steps 33/34/35 — and a literal no-op go_to 4 at step 24 (moved_dist=0.0, reached:True) that it rationalizes as "need to be closer to center." This is genuine non-progress, but it is co-caused by the harness: mark [4] is the door-entity origin offset from the rideable elevator capsule (frames 17/22/32), go_to returns reached:True on a 0-unit march, and there is no positional exit signal (PuzzleExit.cpp). The cited g…  
› _Evidence:_ step 17/24/32 all 'go_to 4'; step 24 RESULT moved_dist=0.0 (no-op) yet model re-plans toward 4; waits at the frame: step 22 wait250, step 33 wait250, step 34 wait250, step 35 wait250, step 38 wait100; grammar note line 52 'don't blindly repeat the same command'.  
› _Skeptic's correction:_ Cited facts all check out, but the framing is off and the severity is inflated. CONFIRMED: go_to 4 at step 17 (dump L1388), step 24 (L1898), step 32 (L2482); step 24's RESULT is the literal no-op `moved_dist=0.0, reached:True, dist=18 after march` (L1903), and the model's own step-25 thinking notes "the position hasn't changed!" (L1956) y…  
› _Fix:_ A moved_dist==0 'SUCCESS dist=X after march' means the destination mark is as-close-as-reachable; the model must abandon that mark rather than re-issue go_to to it.

**`[P3]` Burns a turn walking into the closed entrance airlock door (entrance waste)** — _partial/high_  
At step 1 the model issued `move forward 100` into the closed entrance airlock door [3] (dist=10) and got EDGE (moved ~4u, CheckEdge reads a closed door as a floor-edge). The model did not anticipate that the airlock auto-teleports; the teleport then fired during step 2's `look 90` 1-tick advance, jumping the player to (192,-456). Unlike the exit tail, this was a single non-oscillating step, and the EDGE move was semi-productive: it pressed the player from the go_to standoff (dist=10, outside the trigger) up against the door (dist=6, inside the airlock trigger volume), which is what enabled the next-tick teleport. P3 entrance-friction, not a…  
› _Evidence:_ step 1 player=(-1904,2870,0) dist=10 to [3], action move forward 100 -> RESULT EDGE moved 4; step 2 look 90 -> step 3 player jumps to (192,-456,0).  
› _Skeptic's correction:_ All cited facts verified in the dump. Step 1 (laser_and_button.md:261): player=(-1904,2870,0), dist=10 to airlock door [3], action `move forward 100` -> RESULT `EDGE moved 4 units (EDGE)`. Step 2 (:326): `look 90`. Step 3 (:333): player jumps to (192,-456,0). The EDGE is mechanically real: CheckEdge (MacroExecutor.cpp:632-646) casts a for…  
› _Fix:_ Recognize @entrance/@exit_airlock_door marks as auto-airlocks: approach to ~dist<48 then look/step; an EDGE at dist~10 from an airlock door is expected, not a dead end.

**`[info]` Solve at step 47 was incidental, not a deliberate elevator-entry** — _partial/high_  
The model fumbled the final elevator approach: at step 46 it correctly identified the open exit capsule directly ahead (frame_46) but its `move forward 100` only advanced 117u before STUCK on the capsule's left doorframe. At step 47 it correctly diagnosed the wedge and chose to recenter, planning `move right 20` THEN `move forward 50`; the one-verb-per-step protocol executed only the `right 20`, which moved 36u and centered the agent enough to trigger the elevator's level-end I/O (SOLVED). So the model did under-estimate how close it already was (it expected to still need a forward step), but it correctly recognized and was deliberately enter…  
› _Evidence:_ step 47 action move right 20 -> terminal SOLVED; step 47 reasoning ('move right 20 to shift over, THEN move forward 50 to get us fully inside'); frame step_47.png (agent wedged on grey door frame, capsule only partly in view on right).  
› _Skeptic's correction:_ The mechanism part of the claim is REAL and well-grounded; the interpretive conclusion OVERREACHES.

VERIFIED TRUE: Step 47 (md:3489-3556) action was `move right 20` -> RESULT COMPLETED moved 36u -> terminal=SOLVED. The model's reasoning verbatim (md:3544): "Let's try `move right 20` to shift over, then `move forward 50` to hopefully get…  
› _Fix:_ Don't read the 48-step SOLVED as exit-navigation success; the tail is a near-miss recovered by luck. A robustness fix to elevator entry is still needed.


### A.4 What the HARNESS got wrong (beyond the corridor) (15)

**`[P0]` Exit elevator capsule is unmarked — the goal object the agent must enter has no mark** — _confirmed/high_  
The chamber exit requires physically riding the cylindrical elevator, but the elevator capsule is not in kClassColors, so it never gets a mark or a box. The agent has NO addressable handle for the one object it must reach; it can only guess via screenshot + the nearby door mark [4]. This is the structural cause of the 31-step tail: the model repeatedly treats door mark [4] as 'the elevator' and waits/aims at it.  
› _Evidence:_ frame step_46.png clearly shows the open cylindrical elevator capsule with NO mark number on it; PuzzleAnnotate.cpp:44-73 kClassColors has no elevator class (prop_dynamic/func_brush elevator not listed); step 17 thinking 'cylindrical exit elevator right there... that white capsule' but the only mark it can name is [4]…  
› _Fix:_ Mark the exit elevator (resolve it by name/model — PeTI names it deterministically) so the agent has a go_to-able target for the actual exit. Without it the harness asks the model to navigate to an unnamed object, which is the anti-thesis of a legible percept eval.

**`[P0]` Exit elevator capsule is never annotated — the win target has no mark** — _confirmed/high_  
The cylindrical exit-elevator capsule — the object the agent must ride to trigger the I/O-latched SOLVED — is never annotated (no class for it in PuzzleAnnotate.cpp kClassColors, gated by IsHarnessMarkedClass:77-79), and PuzzleExit.cpp gives no positional exit signal. The agent does have proxy marks in the exit region (@exit_airlock_door [4]/[6], @exit_door [9]) and successfully go_to mark 4 repeatedly, but those land it at the airlock door frame ~660u short of the capsule interior; the final door->inside-capsule leg has no addressable target. As a result the agent burns 31 of 48 steps (17-47, all pure-navigation verbs, 0 rejected) hand-navig…  
› _Evidence:_ frame step_17 (capsule dead-center on teleport-in, no mark), step_30 (lit white cylinder behind door+stairs, unmarked), step_45/step_46 (agent standing INSIDE the capsule, still no mark anywhere on it). PuzzleAnnotate.cpp:44-73 kClassColors has no elevator/`prop_indicator`/capsule class, and IsHarnessMarkedClass (line…  
› _Fix:_ Give the exit-elevator trigger/capsule a synthetic mark (or annotate the @*_elevator brush/trigger_catapult/point_teleport that owns the ride) so the agent can `go_to` it like any other element. Without an addressable mark the whole 31-step tail is forced low-level nav, which is…

**`[P1]` Teleport happens inside a move/look batch with only 'moved N units' as the sole signal** — _confirmed/high_  
The PeTI airlock teleport fires inside AdvanceTicksBlocking during a move; the harness computes moved_dist as straight-line start->final 2D displacement, so a `move forward 100` that crosses the teleport reports 'moved 4600 units (COMPLETED)' with no teleport flag. The model has to back-infer the teleport from the implausible magnitude (step 22: 'move forward 100 resulted in 4600 units... we didn't walk that, must be a transition'). A `look 90` even teleported with zero move feedback (step 2->3, ~3935u jump reported as a plain look SUCCESS).  
› _Evidence:_ Step 16 move forward 100 -> 'moved 4600 units (COMPLETED)' md:1319; MacroExecutor.cpp:1753 moved = (fin-start).Length2D() with code staying COMPLETED (no per-tick teleport guard); step 2 look 90 (md:326) then step 3 player jumped (-1904,2874)->(192,-456) (md:333) with last_result a plain look SUCCESS.  
› _Fix:_ Detect a single-tick position discontinuity (delta >> max walk speed * ticks) inside the move/look loop and surface it as a distinct result detail (e.g. 'TELEPORTED through airlock'), so the model gets an explicit corridor-transition signal instead of inferring it from a suspicio…

**`[P1]` (0,0,0) prop_portals render as floating ghost portal-rings — frame contradicts telemetry** — _partial/high_  
Two prop_portal entities (marks 11/12) persistently report abs_origin=(0,0,0), so the harness draws their OBB box + mark number at world origin (off-screen) and emits bogus but live-updating dist/bearing telemetry. PuzzleAnnotate.cpp origin-filters this exact artifact for the sibling class env_portal_laser (line 88-90) but NOT for prop_portal -- a one-line gap. Meanwhile the chamber's native pre-placed portal surfaces render as numberless blue/orange rings on the walls in many frames (step_03/11/17/22/30/33/40/45), giving the model an unexplained portal-looking artifact with no addressable mark. The model demonstrably wastes reasoning on it a…  
› _Evidence:_ Ghost rings visible in frames step_03, step_09, step_11, step_17, step_22, step_30, step_33, step_39, step_40, step_42, step_45 (blue/orange dotted-reticle rings, no mark number beside them). Telemetry marks [11]/[12] pos=(0,0,0) at every step (e.g. step_00 dist=2832, step_16 dist=1464). PuzzleAnnotate.cpp:86-92 IsHarn…  
› _Skeptic's correction:_ Mechanism is REAL and code-grounded. PuzzleAnnotate.cpp:86-92 origin-filters ONLY env_portal_laser at (0,0,0); prop_portal is not filtered (confirmed). Annotation (line 199) and telemetry (EntitySnapshotter.cpp:339) both read se->abs_origin(), the SAME source, so a (0,0,0) entity gets its mark box AND number drawn at world origin (off-scr…  
› _Fix:_ Extend the (0,0,0) origin-reject in IsHarnessMarkedEntity to prop_portal too (or to any class whose abs_origin is the world origin), OR resolve the real portal placement origin so the mark/telemetry pos matches what is drawn. Right now two of 14-16 marks are pure noise that the m…

**`[P1]` Exit elevator capsule has no mark — agent's one required nav has no target** — _confirmed/high_  
The whole eval premise is 'spend steps on puzzle, not navigation', yet the single navigation that decides success — riding the exit elevator — has no addressable mark. kClassColors (the only marked set) contains no elevator class, so the agent can only go_to mark [4] @exit_airlock_door, a flat DOOR panel ~at (-2688,-2032). The cylindrical elevator the agent must enter is visibly elsewhere down a walkway (clearly in frame at step_17/step_30) but unmarked. With no target, the agent can only blind look/move/wait.  
› _Evidence:_ PuzzleAnnotate.cpp:44-73 kClassColors has prop_testchamber_door, cubes, buttons, lasers — no elevator/lift class. Frames: step_17.png (glass cylinder elevator dead-center, no number), step_30.png (elevator at end of walkway behind railings, no number), step_24.png (mark [4] is a flat door, elevator not in view). summar…  
› _Fix:_ Add the exit-elevator capsule entity (find its classname/targetname, likely a prop_door/func_ linked to @exit, or synthesize a mark at the elevator trigger center) to the marked set so the agent can go_to it. Alternatively annotate a synthetic '@exit_elevator' waypoint at the tri…

**`[P2]` prop_portal (0,0,0) marks [11][12] pollute every percept, never origin-filtered** — _confirmed/high_  
Two distinct prop_portal entities at the world origin (0,0,0) are marked [11] and [12] and ride along in every one of the 48 percepts (both the on-screen-style and prompt renderings) as never-drawn ghost marks. The origin re-emitter filter in IsHarnessMarkedEntity (PuzzleAnnotate.cpp:88-91) is hard-coded to env_portal_laser only; the prop_portal special-case (lines 194-197) only recolors, never filters; and MarkTable.cpp:32 shares the same gate, so the percept list isn't cleaned either. The two marks are mutually indistinguishable (identical pos/dist/bearing). Beyond passive noise, they actively misled a reasoning step at md:1664 where the ag…  
› _Evidence:_ Step 0-47 marks [11][12] always read `prop_portal pos=(0,0,0)` (e.g. md:137-138, md:1289-1290); PuzzleAnnotate.cpp:86-93 IsHarnessMarkedEntity only origin-filters env_portal_laser, not prop_portal; frame step_05.png shows no portal mark numbers drawn in the chamber (they're off-screen at origin).  
› _Fix:_ Extend the (0,0,0) origin-reject in IsHarnessMarkedEntity to cover prop_portal too (or any class), or filter zero-origin entities generically in MarkTable::RebuildFromWorld. Stock PeTI chambers with no placed portals shouldn't surface unplaceable portal marks at all.

**`[P2]` Cube + dropper-box marks share one targetname, defeating name disambiguation** — _partial/high_  
Live cubes and their dropper boxes are both emitted as `prop_weighted_cube` marks sharing one targetname per dropper (13&15 = cubedropper40319, 14&16 = cubedropper46201; appears at step 7, not 9), because the annotation path uses GetEntityName with no z/dropper-box filter. The shared name defeats the system-prompt's name-based disambiguation, leaving cube_type + z (floor vs ~430) + on_button as the only distinguishers. This produced persistent clutter (marks 15/16 never acted on) but did NOT mislead the model: at step 7 it chose go_to 14 over the NEARER dropper box 16 (dist 723 vs 693) by reasoning explicitly on cube_type, not distance — so t…  
› _Evidence:_ Step 9 md:793-796: [13]&[15] both `cubedropper40319-cube_dropper_box`, [14]&[16] both `cubedropper46201-cube_dropper_box`; marks were 14 (count 14) for steps 0-8 then jumped to 16 at step 9 per summary; the box marks 15/16 persist unused through step 47.  
› _Skeptic's correction:_ The MECHANISM is real and verified: marks 13&15 share targetname `cubedropper40319-cube_dropper_box` and 14&16 share `cubedropper46201-cube_dropper_box` (dump md:793-796, verbatim). The name comes straight from server->GetEntityName (EntitySnapshotter.cpp:283, PuzzleAnnotate.cpp:505) and there is NO z-filter/dropper-box exclusion for prop…  
› _Fix:_ Either don't mark the empty dropper box once a cube has spawned from it (it's not an interactable target), or annotate the spawned cube with a distinct synthesized name (e.g. append the cube_type or a #suffix) so the targetname genuinely disambiguates. The dropper box is PeTI sca…

**`[P2]` Doors expose no open/closed state — exit readability depends entirely on pixels** — _partial/high_  
Doors expose no server open-state field (state={} forever; entities.py:45-46), and this is a genuine, deliberately-deferred gap (status_field_recon.md: m_nSequence ruled out, open-state is client-animated). The agent does waste ~6 steps (20-25) trying and failing to read door-open from pixels. But this is a secondary contributor to the exit-corridor flailing, not the primary cause: the dominant tail blockers are the UNMARKED exit elevator capsule and a silent corridor teleport seam (step 41's 4703u jump), neither of which a door-open boolean would resolve. Adding a door-state field would marginally help; marking the elevator capsule / signali…  
› _Evidence:_ Every door mark across all steps shows `state={}` (e.g. md:1287 [9] @exit_door state={} even at step 16 after it opened); contrast [7] floor_button state={'pressed': True} at step 13 md:1069; entities.py:45 comment 'no server open-state field -- visual only'. Steps 20-25 the model repeatedly asks 'is the door open?' an…  
› _Skeptic's correction:_ Every factual sub-claim checks out, but the causal weight ("exit readability depends entirely on pixels" → drives the tail flailing) is over-stated.

CONFIRMED (code): py/p2harness/entities.py:45-46 — `if 'door' in class_name: return {}` with comment "no server open-state field -- visual only". (The finding's path `entities.py:45-46` actu…  
› _Fix:_ The status-field recon (sar_harness_dump_fields) already targets door m_toggle_state, a datamap field the snapshotter misses. Wiring an {open} bit onto door state would let the model read exit-path progress instead of guessing from dim pixels — directly relevant to the M2 exit wo…

**`[P2]` Duplicate door targetnames (@entrance_airlock_door x2, @exit_airlock_door x2) are ambiguous** — _confirmed/high_  
Two marks share "@entrance_airlock_door" (1,3) and two share "@exit_airlock_door" (4,6) because two SEPARATE PeTI airlock instances (~4700-5400u apart, not an inner+outer pair) reuse the same instance-global targetname. This defeats the disambiguation the prompt explicitly promises (gemini_agent.py:53-55), causing brief, self-resolved confusion at steps 0 and 21. It is a real annotation-legibility defect but a SECONDARY contributor; the dominant tail driver is the separate, unmarked exit-elevator capsule.  
› _Evidence:_ Step 0 md:151,152 [1][3] both @entrance_airlock_door; md:153? no — md:152 [3] and md:150 [1]; [4] and [6] both @exit_airlock_door (md:152-onwards, e.g. step 17 md:1354 [4] / md:1356 [6]); step 21 thinking explicitly confused about the two @exit_airlock_door marks.  
› _Fix:_ Disambiguate same-name marks with a positional/ordinal suffix (e.g. @exit_airlock_door#inner / #outer or append the z or a count) so the targetname stays a unique key. Or suppress the inner airlock door pair the player can't address.

**`[P2]` CheckEdge reports a closed door / teleport seam as EDGE, conflating 'blocked' with 'cliff'** — _confirmed/high_  
Move's only guard is CheckEdge, a 24u-ahead floor down-trace (MacroExecutor.cpp:632-644). At a closed airlock door or a teleport-trigger threshold the floor probe finds no floor and returns EDGE, so the model is told 'a gap/edge stopped you' when really a door was shut. Step 1 'move forward' into the closed entrance airlock returned EDGE moved 4u; step 18 into the closed elevator door returned EDGE moved 0. The model reads EDGE as 'cliff', then look/move-hunts for an opening that doesn't exist.  
› _Evidence:_ Step 1 md:261 EDGE moved 4 at a closed door; step 18 md:1461 EDGE moved 0 in front of elevator door [4]; MacroExecutor.cpp:642-644 returns 'EDGE' purely on a missing-floor down-trace, no wall/door distinction; step 19 thinking 'EDGE moved 0 units... has to mean the door is closed' — the model has to override the EDGE l…  
› _Fix:_ Add a forward wall/door probe to Move so a head-height obstruction reports WALL/CLOSED_DOOR rather than EDGE; reserve EDGE for an actual floor drop. Mislabeling a shut door as a cliff sends the model hunting for a non-existent detour.

**`[P2]` Every cube is double-marked: dropped cube + dropper-box duplicate at z=430** — _confirmed/high_  
Each weighted cube produces TWO permanent marks: the live dropped cube (low mark, e.g. 13/14) and a duplicate frozen inside the dropper at z~430 (high mark, 15/16), both sharing the cube_dropper_box targetname — verified frozen across steps 7-47 (step_10.png visibly shows mark 15 floating high beside floor mark 13). Root cause: MarkTable.cpp:28-54 marks every prop_weighted_cube with no in-dropper filter and never despawns the template. In this run the duplicate caused NO realized confusion — the model never referenced mark 15/16 and correctly grabbed the live low-numbered cubes (POWERED in one shot, button SEATED) — so it is a latent percept-…  
› _Evidence:_ step_06 has 14 marks; step_07 shows 16 marks: [13] std pos z=18.3 vs [15] std pos z=429.7, [14] refl z=21.1 vs [16] refl z=430.0 — same names cubedropper40319/46201. Frame step_10 visually shows mark [15] floating high (the in-dropper duplicate) next to floor mark [13]. MarkTable.cpp:28-54 marks every IsHarnessMarkedEn…  
› _Fix:_ Filter the in-dropper/parented cube (it sits at z~430 inside cube_dropper_box) or dedupe by (class,cube_type) keeping the reachable one. The duplicate adds two confusable marks; the model only avoided picking the wrong one here because z was obviously high.

**`[P2]` Name collisions across entrance/exit doors AND both ends of the teleport corridor** — _partial/high_  
Both ends of the entrance and exit airlock-teleport corridors carry duplicate targetnames (@entrance_airlock_door on marks [1],[3]; @exit_airlock_door on marks [4] at exit-elevator side (-2688,-2032) and [6] at chamber side (832,1221)) — verbatim PeTI map data faithfully reported by the harness (server->GetEntityName), not harness-injected. The pos/dist fields still disambiguate the twins (the model correctly picked near [6] over far [4] at step 40), so this is a lost SEMANTIC cue, not a lost disambiguation cue: the agent reasonably reads "go_to [6] @exit_airlock_door" as "reach the exit" when [6] is actually the chamber-side mouth of the tel…  
› _Evidence:_ step_00 telemetry: [1]&[3] both @entrance_airlock_door; [4]@exit_airlock_door pos=(-2688,-2032), [6]@exit_airlock_door pos=(832,1221). At step_40 (player teleported back to chamber side, pos 880,1124) the round portal-ring airlock door in frame is mark [6] @exit_airlock_door — identical name to [4] which the agent left…  
› _Skeptic's correction:_ FACTS CONFIRMED. step_00 prompt_sent (lab dump lines 149-162): [1]&[3] both "@entrance_airlock_door" at (192,-461) and (-1904,2880); [4] "@exit_airlock_door" at (-2688,-2032), [6] "@exit_airlock_door" at (832,1221). Exactly as cited. step_40.png: I see a round portal-ring airlock door directly ahead with a "6" label box above it — mark [6…  
› _Fix:_ The names are PeTI-authored and not unique; the harness should suffix-disambiguate same-named doors (e.g. by which side of the corridor / nearest player-reachable region) or expose the door's open/closed + 'leads-toward-exit' bit so the agent isn't relying on a non-unique targetn…

**`[P2]` `wait` inside the airlock teleports the agent back — the prompt's own 'wait near exit' advice sabotages it** — _partial/high_  
Letting the world run (`wait`) while standing inside the unmarked airlock-corridor trigger volume fires a bidirectional teleport that warps the agent ~4750u back to the puzzle region (step 38 wait 100 -> step 39, verified 4749.7u; step 41 move forward -> step 42, 4703u). This is real but conditional: identical `wait 250` calls ~9u away (steps 33-35) did NOT teleport, and the actual exit elevator is at Z=-112 down a ramp, not at the @exit_airlock_door[4] (Z=32) the agent waited at. The root cause is the unmarked airlock corridor/elevator capsule (already-known P0 corridor problem) plus the agent misidentifying the airlock door as the exit elev…  
› _Evidence:_ Per-step position deltas: step38 wait 100 -> jump 4750 to (880,1125); step41 move forward 100 -> jump 4703 back to (-2624,-1984); step42 confirmed at exit region. System prompt CAVEAT: 'wait 250 near the exit (200-300 ticks) to let the elevator finish'. PuzzleExit.cpp:17-46 latches purely on engine I/O (no positional n…  
› _Skeptic's correction:_ The raw evidence is REAL and the numbers are essentially exact. Step 38 (dump line 2855): player=(-2674.4,-2025.9,33), 15u from @exit_airlock_door[4]; action `wait 100` (line 2920) -> step 39 player=(880.0,1124.7,1). I computed that jump = 4749.7u (claim "4750" ✓). The destination IS the puzzle region (step 39 marks: @exit_airlock_door[6]…  
› _Fix:_ Make airlock trigger volumes a no-wait / no-teleport-while-harness-controlled zone, or detect that the agent is inside an airlock and surface it in telemetry. At minimum, the prompt should not advise 'wait near exit' until the agent is provably inside the elevator capsule (needs…

**`[P2]` No positional exit signal — `from_start_2d` is flat at ~4186 for the whole tail, agent flies blind** — _partial/high_  
The exit elevator capsule has no annotation mark (PuzzleAnnotate kClassColors omits its class), so the agent cannot navigate to it with a verb — only the prop_testchamber_door airlock decoys [4] are marked, and go_to 4 walks the agent into a dead-end corridor (step 18 EDGE, frame step_18). With no addressable exit and exit-completion latching purely on engine I/O (PuzzleExit.cpp), the agent must blunder through freelook + strafing to physically enter the elevator, burning the entire tail (steps 17-47). The solve was NOT luck: at step 47 the model correctly read the frame (jammed on the left doorframe, elevator visible right) and deliberately…  
› _Evidence:_ summary from_start_2d=4186 for steps 17-23 (identical), player pos identical (-2676,-2018) across steps 18-29. step47 reasoning: 'I walked forward but hit the left door frame ... I will step slightly to the right first' — verb move right 20 -> terminal SOLVED. PuzzleExit.cpp has no elevator position.  
› _Skeptic's correction:_ The finding's premise (no positional exit signal) is TRUE but its causal story (flies blind, lucky solve) is REFUTED by the frames and the step-47 reasoning.

VERIFIED TRUE: PuzzleExit.cpp:27-52,59 latches chamber_complete purely on engine I/O, no positional notion. from_start_2d IS flat at 4185.9 for steps 18-23 (dump lines 1399-1839); i…  
› _Fix:_ Surface a 'distance to exit trigger' or 'inside_exit_volume' boolean in telemetry once the objective is met (the engine knows the @exit/elevator trigger geometry). This converts the exit from an unobservable I/O latch into a gradient the agent can climb.

**`[info]` @exit_elevator_cleanser fizzler trigger [5] is marked noise at the exit** — _partial/high_  
trigger_portal_cleanser mark [5] rides in the text telemetry list every step (no LOS cull in MarkTable.cpp:32), but it is NOT harmful noise: it renders on a visible fizzler field (frames step_14/16), sits in the chamber-internal exit-door region ~4500u from the exit elevator (LOS-culled from the frame during the steps 17-38 flail, dist=4503), is never acted on by any verb, and is used by the model as a correct orientation landmark (md:1230, 2756, 2982). It is at most a cosmetic always-on text-mark entry, not an exit-tail confound.  
› _Evidence:_ Step 0 md:153 [5] trigger_portal_cleanser '...@exit_elevator_cleanser'; PuzzleAnnotate.cpp:68 marks trigger_portal_cleanser; PuzzleAnnotate.cpp:64-66 comment notes trigger volumes read as slabs; mark [5] present and unused all 48 steps.  
› _Skeptic's correction:_ The reviewer's literal facts check out but the harm mechanism and severity are wrong. CONFIRMED: trigger_portal_cleanser IS in kClassColors (PuzzleAnnotate.cpp:67), mark [5] "doorexit2-...-@exit_elevator_cleanser" at (830,808,64) IS present in the text-telemetry percept all 48 steps (MarkTable.cpp:32 applies class-only filtering, no LOS c…  
› _Fix:_ For v0, only mark fizzlers the model can act on (a portal cleanser mid-puzzle), not the cosmetic exit-airlock cleanser. Or de-prioritize/omit trigger volumes with no associated verb, since their slab OBB and invisibility make them low-value, high-clutter marks.


### A.5 Percept / annotation quality (2)

**`[P1]` Mark [4] @exit_airlock_door is a positional decoy: dist=18 says 'arrived', but it's the wrong spot** — _confirmed/high_  
Mark [4] @exit_airlock_door is a false-arrival trap, not a true positional decoy: go_to 4 stops at dist=18 because a door keeps the default 48u reachRadius (MacroExecutor.cpp:850,1556-1565) and reports reached=True/"arrived", but the cube-shaped airlock-door panel is ~144u above and offset from the actual UNMARKED cylindrical elevator (the only exit-trigger structure, which kClassColors never marks). The agent reads dist=18 as "inside the elevator" (step 22) and burns 5 wait-250s (22,23,33,34,35) plus a cluster of zero-displacement look/aim steps (25-29) before the SCREENSHOT — not telemetry — lets it recover and ride the real elevator to SOL…  
› _Evidence:_ summary prompts: step22 mark[4] dist=18, agent reasoning 'I have successfully entered the cylindrical exit elevator (distance ... 18)'. Steps 22,23,33,34,35 verb=wait 250 with jump=0 each; steps 25-29 all look/aim with jump=0. Mark[4] pos=(-2688,-2032,32), player parked at (-2676,-2018).  
› _Fix:_ Tie to the prior finding: a real elevator mark removes the decoy pull. Separately, the door's state={} is empty — expose an open/closed status field so the agent can tell the airlock door apart from the elevator and know whether to step through.

**`[P2]` LOS/InFrame cull leaves the ghost portal-rings labelless, making them un-actionable but still visually distracting** — _partial/high_  
prop_portal marks [11]/[12] are emitted with telemetry/origin (0,0,0) because abs_origin() reads m_vecAbsOrigin, which the engine leaves zeroed for prop_portal (true position lives in m_ptOrigin/render path). PuzzleAnnotate draws BOTH the SAR box and the SAR label at that (0,0,0) origin (not divorced), so the InFrame cull (L214) merely drops a label that would have been at world origin anyway — it is a symptom, not the cause. The visible wall ring is the engine's native portal effect, not a SAR overlay. The model can see a portal it cannot map to a mark number (confirmed model confusion at ~step 21, L1664). Correct fix: read the portal's true…  
› _Evidence:_ frames step_03, step_11, step_17, step_39 all show the blue/orange ring with NO adjacent integer, whereas real marks (e.g. [4],[6],[13]) always carry a clamped number. PuzzleAnnotate.cpp:214 `if (!InFrame(origin,...)) continue;` skips the label when the (0,0,0) box projects off-screen; lines 220-221 LOS-cull it too. So…  
› _Skeptic's correction:_ SYMPTOM REAL, MECHANISM WRONG. Verified: prop_portal marks [11]/[12] carry telemetry pos=(0,0,0) at every step (laser_and_button.md L137-138/159-160/207-208/...), and the rings ARE visible+unlabeled in the cited frames (read step_03/11/17/39: a blue+orange swirling oval on the near wall, no adjacent integer). The confusion is also REAL an…  
› _Fix:_ Fixing finding #2 (origin-reject prop_portal) removes both the stray decal-vs-label split and the (0,0,0) telemetry. If portals are ever genuinely in-scope, resolve their real origin so box, label, and telemetry pos coincide.


### A.6 Eval efficiency (3)

**`[P1]` Stateful single chat re-bills full history every step -> quadratic token cost** — _confirmed/high_  
gemini_agent uses one chat.send_message stream, so each call re-sends and re-bills ALL prior frames+telemetry+responses. Per-call prompt grows monotonically 3.8k -> 122.2k; the LAST step alone billed 122,193 input tokens (51,072 of them the 48 accumulated images at 1064 each). totals.tokens_in is the SUM of these growing prompts, so cost is ~O(steps^2). This is why a long tail is catastrophic and not merely linear.  
› _Evidence:_ gemini_agent.py:234 chat = client.chats.create(...); :257 self.chat.send_message; :260 self.tokens_in += usage.input. laser_and_button.md usage lines: in 3814 (img 1064) at step0 -> in 122193 (img 51072) at step47, monotonic; sum of per-call img = totals.tokens_image=1251264.  
› _Fix:_ Cap image history: drop image bytes from all but the last K turns (keep text telemetry). Last-3-frames saves 88% of image tokens; last-1 saves 96%. Either trim the chat history before each send_message or switch to a stateless re-prompt that re-attaches only the latest frame + a…

**`[P2]` Step budget was raised to 48 to mask the tail; default max_steps=30 would have failed this chamber** — _confirmed/high_  
Under the codebase default max_steps=30 (run_eval.py:33, gemini_agent.py:311), this run would report BUDGET/failure: both puzzle objectives are met by step 13 (POWERED@9, SEATED@13) but chamber_complete (the ONLY SOLVED trigger, gemini_agent.py:349) never latches until index 47, and the player is frozen at the exit airlock (-2675.8,-2018.1, jump=0) for all of steps 18-30 — so at index 29 the loop sets terminal='BUDGET'. This run only 'passes' because it was launched with an explicit --max-steps 48 (48 is not a default anywhere in py/). The tail wastes 18 no-op look/aim/wait calls (steps 17-47), not 11; '0 rejected' means every verb was gramma…  
› _Evidence:_ run_eval.py:33 default --max-steps=30; gemini_agent.py:311 def run_eval(..., max_steps=30). This run n_steps=48 (summary). Steps 18-29 all jump=0 (player pinned at the decoy). result_code_counts: 35 SUCCESS but those include 11 no-op look/aim/wait in the tail.  
› _Fix:_ Don't paper over the tail with a bigger budget; fix exit navigation so the chamber solves within the default 30. Track a 'steps after objective-met' metric so a chamber that solves the puzzle but flails on egress is flagged, not counted as a clean pass.

**`[P2]` Even the TEXT portion of context grows quadratically — telemetry+reasoning history is not pruned** — _partial/high_  
Context grows unbounded because the agent uses a single stateful Gemini chat (gemini_agent.py:234,257) with NO pruning of any modality. Per-call input grows ~linearly (~1064 image tokens + ~1455 text tokens added per step, all prior turns retained), making total run cost O(N²): 3.05M input tokens over 48 steps. Both halves are uncapped — image is 41% (1.25M, exactly 1064/step) and text 59% (1.79M). By the tail (steps 17-47, puzzle already solved at step 16), every call re-sends the full 16-mark puzzle telemetry (emitter/target/button at 3700-4900u, all irrelevant) plus all prior reasoning; frames 27/47 confirm the agent is in the exit corrido…  
› _Evidence:_ laser_and_button.md usage: step0 text(in-img)=2750, step16=25528, step30=47780, step47=71121. Telemetry block is ~16 marks * a line each every step (summary marks n=16), all retained in the stateful chat.  
› _Skeptic's correction:_ Core mechanism + cited numbers VERIFIED, but two framings are wrong. (1) Cited text-input figures match exactly: I parsed the dump's "usage: in X / out Y (think Z, img W)" lines and computed text=in-img → step0=2750, step16=25528, step30=47780, step47=71121 — all four match. text() in gemini_agent.py is prompt_token_count minus IMAGE-moda…  
› _Fix:_ Switch to stateless re-prompting with a compact rolling state: latest frame + current telemetry + a 1-2 line summary of objective progress ('laser powered, button seated, now reach exit'), instead of replaying the entire transcript. Combined with image capping this collapses per-…


### A.7 Refuted (kept for honesty)

- **2D distance metric collapses the vertical elevator shaft — mark 4 reads 18u while a floor away** — The cited code is real but the headline mechanism is false. dist=sqrt(dx^2+dy^2) with z dropped IS in py/p2harness/entities.py:58-59 (finding mis-cited path/lines as entities.py:50-61). The docstring (51-56) openly states z is dropped and justifies it as matching go_to's reach; I confirmed go_to truly uses 2D reach (GoToPlanner.cpp:68,85, Length2D with z=0), so the 2D metric is a deliberate, inter…
---

## Appendix B — Corridor-fix design panel

_5 independent proposals from distinct angles, each grounded in fresh source/BSP recon, then scored 1–5 on four axes by 3 independent judges (a Carmack/Torvalds-taste pragmatist, an ML eval scientist, an adversary hunting for breakage)._


### B.1 Scorecard (sum of principle / KISS / generality / robustness, max 20)

| proposal | pragmatist | eval-sci | adversary |
|---|---|---|---|
| mark-the-elevator | 17 | 13 | 13 |
| auto-skip-corridor | 11 | 13 | 13 |
| leave-verb | 11 | 11 | 12 |
| rescope-eval-boundary | 10 | 16 | 16 |
| waypoint-plus-telemetry | 18 | 15 | 14 |

### B.2 The five proposals

#### mark-the-elevator
_effort=M · annotates_elevator=True · new_offset=False · proto_change=False_

**Idea.** Add func_tracktrain to the harness-marked class set so the entrance/exit elevator capsules become numbered, named, go_to-able marks like the doors — the agent navigates to the capsule directly instead of luck-walking the corridor.

**Mechanism.** Recon ground truth (offline BSP dump, py/bsp_recon, ~60 PeTI maps): the rideable capsule is a static func_tracktrain. Across maps its targetname always contains "departure_elevator" (exit) or "arrival_elevator" (entrance), prefix varies (InstanceAuto4- / elev_exit-), class is ALWAYS func_tracktrain. OBB ~128x128x227, and its abs_origin coincides with the trigger_teleport "*player_teleport*" ride spot (e.g. exit -2032,-2032,-56), so the capsule center IS the spot you must stand on to fire ReadyForTransition. The visible glass tube mesh is a sibling prop_dynamic "*elevator_model*" but it has no useful OBB and isn't the rideable volume — the func_tracktrain is the right handle.

The whole percept-mark pipeline keys off ONE predicate. markTable.GetMark (Portal2HarnessImpl.cpp:107) and the annotation box/label (PuzzleAnnotate.cpp RENDER loop) both gate on IsHarnessMarkedEntity -> IsHarnessMarkedClass -> kClassColors membership. EntitySnapshotter auto-registers any class it sees in the world (EntitySnapshotter.cpp:115 RegisterClassSchema(className) in the per-entity loop), so func_tracktrain ALREADY gets a snapshot schema row and a position — no schema edit needed. The percept's name/class/mark come straight from slot.targetName / slot.className / markTable (Portal2HarnessImpl.cpp:101-107) and surface in py/p2harness/entities.py as the marks list the agent reads.

So the surgical change is: (1) add {"func_tracktrain", <color, e.g. cyan {0,255,255}>} to kClassColors in PuzzleAnnotate.cpp. That alone gives the capsule a box, a stable mark (MarkTable assigns/holds it deterministically by index+origin), and a percept row with class=func_tracktrain, name=<raw ugly targetname>. (2) F…

**Risks.** RISK 1 (the load-bearing one): A* reachability into the elevator pocket. The exit elevator sits in a SEPARATE prefab pocket (~-2032,-2032,z-56) reached ONLY by teleporting through the exit-airlock corridor (~4600u warp). GoToPlanner is contiguous world-space A* (16u grid, hull+floor probe, 400-cell cap) — it CANNOT path across a trigger_teleport warp. So go_to(@exit_elevator) works ONLY once the player is already inside the elevator pocket (post-airlock). Before the warp, the planner sees an unreachable island and returns BLOCKED. Recon-open: is the pocket floor itself A*-contiguous from the elevator-side @exit_airlock_door (~-2688,-2032,z32) to the capsule (z-56, an ~88u drop into the tube…

**Open recon.** 1. CONFIRM IN-ENGINE: stand the player in the exit elevator pocket (post-airlock) and run sar_harness_laser_reachability_test — does the capsule center read '.' reachable from the pocket floor, or is the ~88u tube-well drop a severed 'x'/pit '_'? This decides whether go_to walks fully in or stops at the lip. 2. CONFIRM the live engine classname is exactly "func_tracktrain" (BSP says yes for static ones; verify the runtime InstanceAuto-spawned exit tracktrain on maps where it's not static also re…

**Files.** /home/absathe/MachineLearning/SourceAutoRecord/src/Features/Harness/PuzzleAnnotate.cpp, /home/absathe/MachineLearning/SourceAutoRecord/src/Features/Harness/EntitySnapshotter.cpp, /home/absathe/MachineLearning/SourceAutoRecord/py/llm_eval/gemini_agent.py


#### auto-skip-corridor
_effort=M · annotates_elevator=False · new_offset=False · proto_change=False_

**Idea.** Make corridor + elevator traversal FREE by latching off the airlock-teleport AcceptInput edge: when the player crosses an airlock door's teleport (the engine input that fires on the warp), the harness auto-advances the player to the elevator-side landing and, in the exit case, auto-rides to the ReadyForTransition latch -- so the agent never spends ReAct steps in a corridor or capsule.

**Mechanism.** Ground truth from recon: the AcceptInput chokepoint at src/Modules/Server.cpp:657 already feeds PuzzleExit::OnInput, and the PeTI exit elevator fires `InstanceAuto*-departure_elevator-in_elevator.SetValue(1)` (logic_branch) the instant the player physically enters the capsule, then cascades `...elevator_1_player_teleport.RunScriptCode(ReadyForTransition())` at each path_track node (brainstorm/exit_detection_brainstorm.md §2.4/§2.7). PuzzleExit's SIG_READY ALREADY latches chamber_complete on ReadyForTransition (PuzzleExit.cpp:40-42). So the engine already KNOWS "agent entered exit" the moment it matters -- the only waste is the agent flailing to physically reach the unmarked capsule across the bidirectional airlock teleport. Concrete plan, surgical, no new offsets: (1) Add a sibling to PuzzleExit -- call it from the SAME Server.cpp:657 callout -- that watches the airlock-corridor edges by INPUT NAME, not author name: a prop_testchamber_door named `@exit_airlock_door`/`@entrance_airlock_door` firing its teleport, or the `in_elevator.SetValue(1)` branch. Match by substring `airlock_door` on entName + the door's open/teleport input, mirroring PuzzleExit's case-insensitive substring style (it's PeTI-template-stable, like `departure_elevator`). (2) On that edge, set an atomic `g_autoAdvanceArmed`. (3) In the GoTo/Move executor (MacroExecutor.cpp), after a move/go_to COMPLETES, if g_autoAdvanceArmed is set, the harness silently runs the corridor-clear loop ITSELF (hold +forward in kGoToTickBatch slices, edge-guarded, until either chamber_complete latches OR the player stops gaining ground) -- exactly the existing MarchTo/Move batch machinery, just looped internally and NOT count…

**Risks.** RISK 1 (premature ride / false exit): if the auto-advance loop fires when the player is NOT actually in the capsule (e.g. armed by an entrance edge but applied at the exit), the player walks into a wall and stalls -- harmless (loop terminates on position-stall, agent gets an honest 'crossed' result), but it must NOT claim chamber_complete it didn't earn. Mitigation: the loop NEVER sets chamber_complete itself; it only reads PuzzleExit::Get(), which is the existing I/O-latched oracle. The march is pure locomotion; correctness of 'solved' stays 100% on the existing oracle. RISK 2 (ping-pong, the explicit ask): the airlock teleport is bidirectional, so a naive 'auto-walk forward' could warp bac…

**Open recon.** (1) Confirm the exact entName/inputName of the 'player is inside the capsule' edge on a bare load -- §2.7 shows `InstanceAuto4-departure_elevator-in_elevator.SetValue(1)` but the `InstanceAuto*` prefix varies; need a fresh sar_show_entinp capture on laser_and_button specifically to lock the substring (likely `in_elevator` + SetValue param `1`, or just reuse the already-latching ReadyForTransition as the arm and skip a new signal entirely). (2) Does the player remain physics-controllable while th…

**Files.** src/Features/Harness/PuzzleExit.cpp, src/Features/Harness/PuzzleExit.hpp, src/Modules/Server.cpp, src/Features/Harness/MacroExecutor.cpp, py/llm_eval/gemini_agent.py


#### leave-verb
_effort=M · annotates_elevator=False · new_offset=False · proto_change=False_

**Idea.** Add a gated `leave` verb that, once the exit airlock door reads open, walks the player onto the PeTI departure_elevator ride spot (found by targetname, not marked) and waits out the I/O-latched chamber_complete, collapsing ~30 corridor/elevator steps into one act.

**Mechanism.** New MacroExecutor::Leave (dispatched in Execute() like the other verbs; one VERB_SPEC entry `leave` with no args). Steps, all reusing existing primitives: (1) GATE on main thread — walk the server entity list (Offsets::NUM_ENT_ENTRIES loop, the same pattern in PuzzleAnnotate/MarkTable) for a prop_testchamber_door whose server->GetEntityName contains "exit" and read its open-state field (m_toggle_state / m_bOpen — already in PuzzleAnnotate's kStatusCandidates and snapshotted); if not open, reject NOT_SOLVED with guidance ("solve the puzzle to open the exit door first"). This is the real "solved" proxy because chamber_complete itself only latches AFTER the ride (circular), whereas §2.7/§2.8 recon confirms @exit*_door.Open() fires on solve. (2) FIND the ride spot — same entity walk for a func_tracktrain (or its child) whose name contains "departure_elevator"/"player_teleport"; take its abs_origin as the target Vector. Reject NOT_FOUND if absent (non-PeTI map). (3) DRIVE — call MarchTo()/RouteAround() (the exact go_to backend) to that Vector, skipping marks (targetKey=0). (4) WAIT — AdvanceTicksBlocking in a loop (e.g. up to ~600 ticks in kGoToTickBatch slices), polling PuzzleExit::Get() each batch; the elevator descends and its player_teleport fires ReadyForTransition() purely from the player riding it (position-independent I/O, per PuzzleExit.cpp's SIG_READY). (5) REPORT — SUCCESS the tick PuzzleExit::Get() latches; else TIMED_OUT with the final distance/guidance. The agent sees a new verb in the prompt and one SUCCESS step instead of a 30-step oscillation; the exit elevator stays UNMARKED (no kClassColors / RegisterClassSchema change), so the percept's legibility budget is…

**Risks.** (1) GATE robustness: the "solved" proxy is the exit-door-open field. Risk: the door open-state field name varies (m_toggle_state datamap-only vs m_bOpen) and a chamber with no airlock door (custom Hammer) would never gate true -> agent can't leave. Mitigation: read via the same getServerOffset path PuzzleAnnotate recon uses (resolves datamap+sendtable), and if NO exit-named door exists, fall back to gating on "any go_to to the elevator ride spot is reachable" or just allow leave unconditionally with a softer warning — needs the recon below. (2) DRIVE reachability: GoToPlanner is flat-chamber A* (single refZ floor probe, kProbeDown=128); the ride spot may be down stairs / through the teleport…

**Open recon.** RECON STILL NEEDED before building: (1) Confirm the exit-door open-state field + name on a real PeTI chamber via sar_harness_dump_fields (diff before/after solve) — is it m_toggle_state (datamap) or m_bOpen, and does a @exit*_door always exist? (2) CRITICAL: run sar_harness_goto_plan / sar_harness_probe_cells from a solved-chamber position to the departure_elevator/player_teleport origin — does the FLAT GoToPlanner actually produce a route onto the capsule, or is the ride spot past stairs/a Z-se…

**Files.** src/Features/Harness/MacroExecutor.cpp, src/Features/Harness/MacroExecutor.hpp, py/p2harness/macro_grammar.py, py/llm_eval/gemini_agent.py


#### rescope-eval-boundary
_effort=M · annotates_elevator=False · new_offset=False · proto_change=True_

**Idea.** Keep the natural entrance spawn but END the run on a "puzzle solved" predicate that fires the instant the exit door is unlocked -- decoupled from the elevator ride -- by latching the exact engine output PeTI uses to open the exit door, so the corridor/elevator tail can no longer turn a solved chamber into a timeout.

**Mechanism.** Today the only terminal is `obs.state.chamber_complete`, latched by PuzzleExit::OnInput on `ReadyForTransition()`/`@relay_pti_level_end.Trigger` -- signals that fire only WHILE the player rides the departure_elevator (recon: brainstorm/exit_detection_brainstorm.md:184-188, "elevator started its departure"). So the agent must physically reach + ride the unmarked glass capsule, which is the 30-step tail. The fix: latch a SECOND, earlier bit -- "exit door unlocked / objective met" -- and make the eval terminal on THAT. The clean signal already exists in PeTI's own I/O graph and is recoverable offline: the exit airlock/door is opened by a logic edge (exit_detection_brainstorm.md:266 documents `@exit_door-door_wants_to_close_branch.SetValue(0)` + the door's `Open` input firing on solve; bsp_recon/dump_ents.py already dumps every output edge, so we can confirm the exact (entname,input) per chamber offline without the engine). Concretely: (1) C++ -- in PuzzleExit add a `g_objective` latch fed by the SAME AcceptInput hook (Server.cpp:657 already routes every input through PuzzleExit::OnInput); match the door-open edge (e.g. inputName `Open`/`Unlock` on an entity whose name contains `exit` and class `prop_testchamber_door`, OR the `@..._level_end`-feeding relay's `Trigger`), set a new `objective_complete` bool on GameState (proto field 10). (2) Python -- py/run_eval.py / gemini_agent.py run-loop: terminal = SOLVED when `obs.state.objective_complete` (not chamber_complete); drop the elevator caveat from the _SYSTEM prompt and the TODO at gemini_agent.py:40. The agent now never sees -- and never needs -- the exit elevator: the run ends at the puzzle boundary. The spawn side is LEFT…

**Risks.** RISK 1 (biggest, unresolved): the exact door-open input edge is NOT yet pinned down. status_field_recon.md never opened a real PeTI exit door in-engine; exit_detection_brainstorm.md:266 gives a LEAD (`door_wants_to_close_branch.SetValue` + the door `Open` input) but flags it unverified. Mitigation: run bsp_recon/dump_ents.py on laser_and_button.bsp + 2-3 other PeTI exports and read the output edges feeding @exit_*_door / the level-end relay to find the constant signal BEFORE writing the matcher (this is the open recon below). If no single early edge generalizes, fall back to matching the relay that ALSO triggers ReadyForTransition but fires on solve, not ride. RISK 2: false-positive -- if th…

**Open recon.** PRIMARY: which exact (entityName, inputName) edge fires when a PeTI exit door UNLOCKS on solve, and is it constant across chambers? Resolve by running py/bsp_recon/dump_ents.py on laser_and_button.bsp + 2-3 other PeTI maps and grepping the dumped `outputs` for edges targeting @exit_*_door / the level-end relay (the InstanceAuto-prefix varies per export -- exit_detection_brainstorm.md:189 -- so match on suffix/class, not full name). SECONDARY: does that edge fire strictly BEFORE ReadyForTransitio…

**Files.** src/Features/Harness/PuzzleExit.cpp, src/Features/Harness/PuzzleExit.hpp, src/Features/Harness/Portal2HarnessImpl.cpp, src/Features/Harness/harness.proto, py/run_eval.py, py/llm_eval/gemini_agent.py, py/bsp_recon/dump_ents.py


#### waypoint-plus-telemetry
_effort=S · annotates_elevator=True · new_offset=False · proto_change=False_

**Idea.** Mark the func_tracktrain named *departure_elevator* (and the symmetric arrival capsule) as a harness entity via a per-entity name gate in IsHarnessMarkedEntity, so the exit elevator gets a stable mark + annotation box and the agent can simply go_to it instead of luck-walking the corridor.

**Mechanism.** The percept already streams the elevator: the snapshot loop (Portal2HarnessImpl.cpp:342) emits every alive entity class-agnostically; PopulateEntityStateProto stamps mark = markTable.GetMark(...). The ONLY reason the capsule is invisible is that its mark is 0 -- MarkTable.RebuildFromWorld and PuzzleAnnotate both gate on IsHarnessMarkedEntity, which is pure kClassColors (classname) membership, and func_tracktrain isn't in it. Fix: extend IsHarnessMarkedEntity(ent, className) (PuzzleAnnotate.cpp:86) so that for className=="func_tracktrain" it also matches when server->GetEntityName(ent) contains "departure_elevator" or "arrival"/the entrance instance token (GetEntityName is already used in this file at line 415/505, so no new primitive). Give that class an entry in kClassColors (e.g. a cyan {0,200,255}) so the annotate RENDER loop boxes+labels it and colorIt resolves. Result, with ZERO other changes: (a) the capsule gets a stable integer mark from the existing MarkTable, appears in obs.marks via WorldView (rec[mark]>0), and is drawn on the frame with its number; (b) its target_name (@..._departure_elevator) flows through the existing set_target_name path so the model reads it as the way OUT; (c) go_to <elevator_mark> resolves to EntityCenter and A*-marches the player into the capsule -- the same resolve+march already used for every other mark -- firing in_elevator.SetValue(1) -> ReadyForTransition -> chamber_complete. The agent spends ONE puzzle-legible step (go_to the exit) instead of ~30 corridor steps. Entrance gets marked too for symmetry/orientation but the exit is what kills the budget tail. No distance_to_exit / in_exit_elevator telemetry bits are needed in v0: go_to…

**Risks.** RISK 1 (primary, needs live recon): func_tracktrain is a moving brush; its OBB center may sit mid-capsule and its abs_origin may be the model's brush-origin, not floor-center. go_to resolves to EntityCenter (OBB center) and A* SnapGoal+reachRadius closes the gap to the nearest walkable cell -- the same path that already lands the agent 'beside' cubes/buttons -- but whether that lands the player INSIDE the capsule (so in_elevator fires) vs beside its glass wall is the one thing I could not confirm from source alone. Mitigation/recon: load a PeTI chamber, sar_harness_annotate 1, confirm the capsule gets a box+mark, then macro_repl `go_to <mark>` and check chamber_complete latches. If center mi…

**Open recon.** 1) Live confirm that go_to onto the func_tracktrain center actually lands the player INSIDE the capsule and fires in_elevator -> chamber_complete (macro_repl on a stock PeTI chamber); if not, decide between targeting OBB-bottom-center vs the *...player_teleport* child position. 2) Confirm the exact entrance/arrival instance targetname token (the brainstorm names departure_elevator precisely; the symmetric arrival token should be grabbed from a live sar_harness_dump_fields / GetEntityName dump or…

**Files.** /home/absathe/MachineLearning/SourceAutoRecord/src/Features/Harness/PuzzleAnnotate.cpp



### B.3 Judge recommendations (verbatim)

**pragmatist** — ranking: waypoint-plus-telemetry > mark-the-elevator > auto-skip-corridor > leave-verb > rescope-eval-boundary

Ship waypoint-plus-telemetry as the core, with the one good idea from mark-the-elevator folded in (friendly-name synthesis). Rationale grounded in source: the latch is NOT ride-duration -- @relay_pti_level_end.Trigger is already SIG_PTI_LEVEL_END in PuzzleExit.cpp:34-36 and the recon (exit_detection_brainstorm.md:300-303) shows it firing the instant the player enters the capsule (in_elevator.SetValue(1) -> relay at tick 6687, before the descent). So chamber_complete latches on ENTRY; the entire 30-step tail is purely 'the capsule is unmarked, so the agent has no navigable handle.' The fix is to make the capsule a first-class marked target. The percept pipeline keys off ONE predicate (IsHarnessMarkedClass -> kClassColors, consumed by both MarkTable.cpp:32 and PuzzleAnnotate.cpp:188), and EntitySnapshotter already auto-registers func_tracktrain (EntitySnapshotter.cpp:115) -- so this is genuinely a one-predicate change with no proto/offset/schema cost. Reject leave-verb and rescope: both gate on a door open-state server field that recon already KILLED (status_field_recon.md:99/147; entities.py returns {} for doors), and leave-verb's wait-out-the-ride loop solves a non-problem. Reject auto-skip-corridor: it hides locomotion the agent can't reason about, which fights the LEGIBLE-eval principle, for the most new C++. FIRST SURGICAL STEP (before any code): resolve the single load-bearing unknown shared by both top angles -- does GoToPlanner's flat A* actually reach INTO the capsule, or is the exit pocket severed across the airlock trigger_teleport warp? Stand the player in the exit pocket post-airlock and run sar_harness_laser_reachability_test; read whether the capsule center is '.' reachable vs 'x' severed / '_' pit. If reachable: the code change is exactly the gated-name edit -- in PuzzleAnnotate.cpp add {\"func_tracktrain\", {0,200,255}} to kClassColors AND extend IsHarnessMarkedEntity so func_tracktrain only marks when GetEntityName contains \"departure_elevator\"/\"arrival_elevator\" (mirrors the existing env_portal_laser per-entity guard at line 88, prevents marking the custom-map 7-8-tracktrain movers); optionally synthesize @exit_elevator/@entrance_elevator names in EntitySnapshotter::InitSlot for legibility; then drop the stale gemini_agent.py TODO + the hand-holding 'walk toward the CYLINDRICAL elevator' caveat and add an agentloop_smoke.py assertion that the exit mark appears. If the reachability test shows severed: the marked capsule center is still a far better go_to target + stable bearing than the door (which yanks backward), so ship it anyway and let go_to's SnapGoal close the final lip -- strictly better than today regardless. Either way, gate-test FIRST, then make the ~one-predicate edit."}

**eval-scientist** — ranking: rescope-eval-boundary > waypoint-plus-telemetry > auto-skip-corridor > mark-the-elevator > leave-verb

Ship a COMBINATION, sequenced by recon, because the root cause is two distinct problems the user conflated: (A) the false-FAILURE timeout (puzzle solved ~step 15 but the corridor tail overflows the 30-step budget), and (B) the wasted ReAct steps themselves. Attack (A) first -- it's the P0 -- with rescope-eval-boundary, which is also the most principled because it ends the run at the puzzle's OWN objective and stops measuring traversal entirely (the harness's stated point). The annotation angles (waypoint/mark-the-elevator) are seductively KISS but I rank them below rescope because the flat GoToPlanner provably CANNOT path a go_to across the airlock trigger_teleport warp (Probe uses straight-down ray traces, kPlanMaxCells=400, single refZ) -- a marked elevator gives the model a bearing, not a guaranteed traversal, so it's a partial fix to (B) and does nothing for (A). The user's literal question ('can we annotate the elevators') has the honest answer: yes, trivially (one name-gate in IsHarnessMarkedEntity + a kClassColors entry), but it will NOT reliably let go_to ride the elevator, so don't lead with it. FIRST SURGICAL STEP (pure recon, zero code, decides the whole plan): run `py/bsp_recon/dump_ents.py` on laser_and_button.bsp + 2-3 other PeTI workshop exports and grep the dumped `outputs` for the edge that OPENS @exit_*_door on solve (the brainstorm's lead is door_wants_to_close_branch.SetValue / the door's Open input, recon'd only on campaign so far) -- confirm a single (inputName, name-pattern) edge fires strictly BEFORE ReadyForTransition and generalizes across exports. If it does: add a g_objective latch fed by the existing PuzzleExit::OnInput AcceptInput hook + an objective_complete proto field, make run_eval terminal on it, drop the elevator caveat from the prompt -- done, corridor can never false-fail again. If that edge does NOT generalize, fall back to an element-status objective predicate (the laser target's m_bPowered + button m_bButtonState already exist and are read today) before reaching for the elevator-ride bundling. Park the annotation angles as a P1 polish only if, after rescope lands, you still want the entrance/short-tail legibility -- and even then prefer the cheap one-file waypoint-plus-telemetry over mark-the-elevator, gated on the departure_elevator/arrival name substring so custom-Hammer maps don't get spurious marks."

**adversary** — ranking: rescope-eval-boundary > waypoint-plus-telemetry > auto-skip-corridor > mark-the-elevator > leave-verb

Ship rescope-eval-boundary as the real fix, but DON'T write any C++ until one recon gate is closed -- because every other angle is downstream of the same unverified fact (can the flat A* even reach the capsule across the teleport seam?), and rescope is the only one that makes that question MOOT by never requiring the elevator at all. FIRST SURGICAL STEP (pure recon, no engine, no risk): run py/bsp_recon/dump_ents.py on laser_and_button.bsp + 2-3 other PeTI exports and grep the dumped output edges for what fires on the exit door / @relay_pti_level_end when the chamber is solved. You are looking to confirm a single (entityName-suffix, inputName) edge -- e.g. an Open/Unlock on a name containing 'exit'+class prop_testchamber_door, OR the relay's Trigger -- that fires strictly BEFORE ReadyForTransition and is constant across exports (the InstanceAuto prefix varies, so match suffix/class, never full name). GATE: if such an early edge exists and generalizes -> implement rescope: add one g_objective latch in PuzzleExit::OnInput fed by the SAME Server.cpp AcceptInput callout (verified live), expose it as a new GameState bool (this is the one proto change -> make proto + 32-bit rebuild + agentloop_smoke assertion + git-checkout the reformatted *.pb.cpp per the format.sh footgun), and flip py/run_eval.py's terminal to it. Be explicit to the user that this CHANGES what 'solved' means: the run ends at the puzzle objective (door unlocked), dropping the implicit end-to-end 'agent physically reached a valid exit' check -- which is arguably MORE correct for a reasoning eval, but is a real semantic shift to sign off on. FALLBACK: if no early door edge generalizes, the objective latch instead reads the element-status predicate that already exists (point_laser_target m_bPowered, button m_bButtonState) per-chamber -- a more general 'objective met' signal worth pricing. Only if BOTH fail do you fall back to a navigation angle, and at that point ship waypoint-plus-telemetry (the one-file name-gated mark) as the minimal handle AND first run sar_harness_laser_reachability_test standing inside the exit pocket to confirm the capsule center reads '.' reachable -- if it reads 'x'/severed/pit, the mark is only a bearing and you additionally need a teleport-snap (reuse the in-tree FCPS Teleport primitive) onto the ride spot, not a march. Net: recon first, rescope if the edge generalizes, mark+teleport-snap only as the navigation fallback.
