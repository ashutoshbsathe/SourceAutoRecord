<!-- Generated 2026-07-04 via a 7-investigator multi-agent postmortem (each finding adversarially
verified by an independent skeptic; all 7 held). Crown-jewel claims (laser mutual-exclusion, the
vphysics-sleep freeze mechanism, the ghost-door list) were re-verified by hand against the raw
trajectory state before this was written. Run artifact:
noteworthy_trajectories/azorae_stride_remake_by_gigo_20260704.trajectory -->

# Postmortem: azorae's stride remake — 100-step BUDGET failure (frozen gemini-3.5-flash)

Blameless. Every claim carries a `file:line` or a `step N`. Verdicts fold in the adversarial-verify passes — where an investigator overclaimed, the corrected version is what's written here.

---

## 1. TL;DR

- A frozen gemini-3.5-flash drove "azorae's stride remake" for 100 steps and **never solved it** (terminal = BUDGET, 0 net progress for the last ~45 steps). Input 14.5M tokens, 5.37M of them images, **zero validator rejections** — the model spoke the grammar fluently and still lost.
- **The one-line why:** this is a *locomotion + percept* failure wearing a reasoning-failure costume — but with a real reasoning trap underneath (below) that means the "the plan was correct" story is only half true.
- **~60%+ of the budget was locomotion and orientation churn.** 24 of the first 26 steps were spent trying to walk *down* to a dropped cube, because `go_to` and `move` are both structurally single-Z-plane and **no verb in the surface can descend a plain ledge** (GoToPlanner.cpp:107-166; MacroExecutor.cpp:747-760).
- **The laser "solves" were physically fake.** `redirect_to`/`interpose` freeze the reflector cube via a vphysics **sleep artifact** (zero-velocity teleport, no gravity flag anywhere) and read POWERED off a ~10.7°-pitched cube that a free cube could not hold on flat floor (MacroExecutor.cpp:486-504; LaserGeometry.cpp:82-90). Every POWERED (steps 39/44/66/92) is suspect.
- **The exit was in the percept but drowned:** 6 door marks including two ghost airlock doors 3200-3900u outside the chamber (summary.json step 0), the real exit `mark 12 @exit_door` never once targeted by an action, and the entrance door `mark 5` wears a running-man EXIT glyph on frame 0 (step_000.png). But — correction to ground truth #3 — the model *did* name mark 12 as the exit from step 38 on; it failed to **commit** a nav action to it, not to recognize it.
- **The model barely looks at the image**, but not primarily because of darkness (median frame brightness 44, and the step-99 frame it ignores is *bright*, median 193). It reasons in coordinate space because the prompt never once forces a visual observation and sends the image *before* the telemetry every turn (gemini_agent.py:271).
- **There is no loop-breaker.** `place_portal S1 → NO_LOS` four times (steps 80/82/86/87), `go_to 15 → ADVANCED/BLOCKED` ≥8 times, with no behavioral change and no harness no-progress signal (gemini_agent.py:254-277).
- **THE BIGGEST MISS in both ground truth and the first-pass findings:** the two laser targets are **mutually exclusive with one cube** — target 14 (opens exit) and target 13 (kills fizzler) are perfectly anti-correlated in the per-step powered map. The chamber needs the fizzler *down* to cross AND the exit door *open* to leave, and a single reflector cannot power both at once. The "flip-flop redirect 13↔14" was not flailing — it was a genuine 1-actuator/2-constraint contradiction the model never diagnosed. **This makes it a reasoning datapoint after all.**
- **Single highest-leverage fix:** land a **descend/height-seam model in `GoToPlanner` + relax `CheckEdge`** (the planner *and* the march must agree, or you get a plan the body refuses to walk). Everything else is percept hygiene and verb-correctness that this run also needs, but locomotion is what drowned a quarter of the budget before the puzzle even started.

---

## 2. What actually happened (the arc, with budget %)

Displacement start→end was 1762 units; the model spent 100 steps producing net ~1 room of motion.

**Budget classification of all 100 timeline steps** (grouped; a step can only belong to one bucket):

| Bucket | ~Steps | % | What it was |
|---|---|---|---|
| Genuine progress | ~15 | 15% | button/drop (0-2), portal + interpose/redirect that advanced the puzzle (30,33-39,44,46) |
| **Descent flailing** | ~24 | **24%** | steps 3-26: trying to walk DOWN to the dropped cube — pure locomotion gap |
| Failed nav (`go_to` ADVANCED/BLOCKED + `move` EDGE) | 13 | 13% | 3,5,11,16,24,53,54,58,72,73,75,88,90 |
| Orientation-only (`look`/`aim_at`, no state change) | ~30 | 30% | ~9 of these `aim_at Sn` immediately before `place_portal Sn` — redundant (place_portal self-aims) |
| Portal `NO_LOS` refails | 8 | 8% | 40,41,43,56,80,82,86,87 |
| Redundant re-place / re-POWER | ~11 | 11% | 49,60,62,63,65,66,68,71,92,94,97 |

**>60% of the budget is locomotion (descent, height-seam `go_to`, portal-hop shuttling) + orientation churn. Reasoning-bearing steps are ~15.**

The arc:
- **0-2:** `go_to` corridor → button 11 → `interact` 11 drops the reflector cube. Clean.
- **3-26:** ~24 steps, a full quarter of the budget, trying to get *down* to the cube (mark 15, now on a lower floor). `go_to 15 → ADVANCED`, `move forward → EDGE moved 0`. Only progresses by lucking onto the sub-64u staircase risers and hand-crawling `move forward` (steps 9,14,20-23 COMPLETED).
- **28-39:** places blue S1 + orange S4, drops to the low level, picks up cube, `interpose` on laser emitter → `redirect_to 15 14 → POWERED`. Target 14 lit = exit door opens. **Puzzle causal core apparently done.** (Physically fake — §3.2.)
- **40-46:** `place_portal S3/S2/S5 → NO_LOS ×3` (portaling across a fizzler it can't see past); `redirect_to 15 13 → POWERED` deactivates the fizzler; `place_portal S3 → PLACED`.
- **47-99:** **death spiral.** But not random: it is the model oscillating `redirect_to 13↔14` because it cannot hold both constraints at once (§3.7 — the mutual-exclusion trap), interleaved with `go_to 15 → ADVANCED/BLOCKED ≥8×`, `place_portal S1 → NO_LOS ×4`, and `pass_through` mouth-approach failures. Terminal: step 99 `pass_through Pb → NOT_AT_MOUTH`, fizzler down but **exit door SHUT** (target 14 unpowered). BUDGET.

---

## 3. Findings

### 3.1 Locomotion — `go_to` can't route across height; no descend verb exists

**Verdict on user thoughts #1 (go_to couldn't walk around/down the stairs) and #4 (a jump_down verb?): CONFIRMED, and the verified re-read strengthened it.**

**Mechanism — three independent single-Z gates compound:**

1. **The A\* grid is anchored to one reference plane.** `GoToPlanner` ctor takes a single `refZ` = player feet at call time (MacroExecutor.cpp:1078; GoToPlanner.hpp:30,64). `Probe` casts a straight-down ray from `refZ + kProbeUp(40)` reaching only `kProbeDown(128)` below (GoToPlanner.cpp:24-25,107-116). A cell's floor is only detectable in **z ∈ [refZ−128, refZ+40]** — you can descend ≤128u and step up ≤40u from wherever `go_to` started, and nothing else exists.

2. **Even when both cells probe walkable, `Passable` hull-sweeps horizontally** at `z = max(floorZ_a, floorZ_b) + kHullLift(2)` between cell centers (GoToPlanner.cpp:159-166). Across a step-down the high sweep slams into the riser → the edge is non-Passable. A\* connectivity is pure 2D (GoToPlanner.hpp; astar_routing_design.md:76-77 "connectivity stays 2D" — the floorZ field was designed-in as a *future* 2.5D seam and never wired). A severed grid → empty route → `RouteAround` returns BLOCKED → `go_to` maps a moved>48 BLOCKED march to **ADVANCED** (MacroExecutor.cpp:2544-2546).

3. **`move` cannot intentionally walk off a ledge.** `CheckEdge` casts a down-ray reaching only `kProbeHeight(18) + kStepDownMax(64)` below the feet (MacroExecutor.cpp:747-760, constants :89-91); any deeper drop → `EDGE`. **And the `go_to` march is itself edge-guarded** — `ChooseVfhHeading` drops any EDGE bin and re-picks (MacroExecutor.cpp:902-903). *This is the verified strengthening:* even if `Passable` learned a descending edge, the VFH march would still refuse to walk off it. **You must relax BOTH the planner (Passable) AND the executor (CheckEdge/VFH) or the planner produces a plan the body won't take → STUCK.**

No `descend`/`jump_down`/`drop_off` verb exists (grep of Harness + macro_grammar.py = empty). The parked `jump_into`/`drop_into` are **floor-PORTAL only** — they gate on `normal.z >= 0.7` and need a placed portal to funnel into (jump_into_design.md:46-47,127); they do nothing for a plain geometric ledge.

**Run evidence:** cube floorZ 481→149 after the button press while the player stayed z=256 — a 107u pit drop inside the 128u window, so the destination *probes* but the seam severs it (summary.json steps 2→3). `move forward → EDGE moved 0` at player z=249/242 with cube floor 149 (~100u/93u drop > 64) — steps 5, 24, verbatim. `go_to 15/14/Po → ADVANCED/BLOCKED` at steps 3,11,16,53,54,58,72,73,75,88,90 (e.g. 72: "advanced 332, 731 to go"). frame_004.png confirms it's genuinely multi-level, not a percept artifact.

**Extra mechanisms found (beyond ground truth):**
- **The step-UP ceiling (40u) is tighter than step-down and equally broken** — a platform floor >40u above the `go_to` start is *below* the probe ray's origin → NO_FLOOR. Descend and ascend are the same bug mirrored, which is why late-run `go_to` also failed to climb *back* onto platforms.
- **`ADVANCED` is actively harmful here.** It was designed to make the model "re-plan from the new spot" (MacroExecutor.cpp:2542-2543), but across a permanent geometric seam the new spot is on the same severed level → re-planning re-severs identically → ADVANCED invites an *infinite retry of a call that can never succeed*. The model dutifully retried `go_to 15` ~8×. **But** `ADVANCED` is also genuinely ambiguous: a march that merely ran out of ticks also returns BLOCKED (MacroExecutor.cpp:911), so ADVANCED means *either* "severed by height" *or* "slow, ran out of ticks" — reinforcing the case for a distinct terminal code.
- **The subsystems disagree on "walkable down":** grid reaches 128u down, `CheckEdge` only 64u. In the 64-128u band the grid *thinks* a lower cell has floor but `move` aborts at the first descending leg → low moved distance → sometimes not even ADVANCED, just bare BLOCKED (step 73).

**Fixes:**

| Fix | Kind | Effort | Recommend | Tradeoff / Risk |
|---|---|---|---|---|
| **Minimal step-height patch:** probe cell floor from a high fixed z (find up-steps + deep drops); in `Passable`, for a step-down run the sweep at a **two-segment** test (horizontal at upper floor to the lip, then the drop) — *not* a single lowered horizontal sweep (that starts inside the upper riser → false startsolid); bump `kStepDownMax` to ~72 so a stair riser isn't an EDGE. | code | **M** | **YES** | ~40-60 LOC, no grid-key change. Fixes this chamber's stairs (risers <64u). Doesn't handle a >128u sheer pit (fine — that wants a portal). Risk: bound the descend to ≤1 grid cell horizontally so it's a step not a cliff. |
| **Layered-Z GoToPlanner:** key cells by (cx,cy,layer), add explicit step-up / step-down edges in `Passable`, widen the probe to span chamber height. The only fix that makes `go_to` *complete* on arbitrary multi-level chambers with zero new verbs. | code | **L** | YES (after the M patch proves the geometry is traversable) | ~150-250 LOC; must re-verify the A\* money-test invariants. Risk: a too-permissive descend edge routes onto a lethal drop — gate on a landing-hull-fits check. |
| **Distinct terminal code (`HEIGHT_SEVERED`)** when the march stalled and the goal's z differs from the reachable frontier by > the step ceiling — stops the infinite retry. **Cheaper than proposed:** thread the *existing* per-cell `BlockReason` enum (NO_FLOOR/IN_WALL/OBSTACLE, GoToPlanner.hpp:21, already surfaced by the recon commands) up through `RouteAround`, rather than inventing a new enum. | code | **S** | **YES** — ship even if the planner fix is deferred | ~15 LOC + threading the reason. Pure legibility; converts an infinite loop into a one-shot "switch strategy" signal. |
| A standalone `descend`/`step_down` verb (walk off ledge, disable CheckEdge for a bounded push, report LANDED/DIED). | verb-design | M | **NO** | Redundant with layered-Z `go_to` for reachable drops; adds surface area + goo-death safety burden. Only earns its keep for a *deliberate* drop the planner would route around. |

**Open question for you:** are the risers actually <64u (the model got COMPLETED walking them)? If yes, the **M patch alone** may unblock this chamber without the L rewrite — worth a `sar_harness_probe_cells` / BSP check on 1522623535 first.

---

### 3.2 Laser freeze — `redirect_to`/`interpose` hold the cube in an impossible pose; POWERED is fake

**Verdict on user thought #2 (redirect "paused" the cube → crazy angles in the air) and the emphatic follow-up (the laser solve is FAKE): CONFIRMED as a real correctness bug — but the first-pass fixes are wrong for this chamber, and the corrected diagnosis is subtler.**

**Mechanism:** `SeatEntity` (MacroExecutor.cpp:486-504) `Teleport`s the cube to an absolute origin+angles then zeroes **both** linear and angular vphysics velocity (`SetVelocity(0,0)`, :496-503). There is **no gravity/movetype flag anywhere** — grep for `EnableMotion`/`MOVETYPE_NONE`/`SetGravity`/`EnableGravity`/`Wake` across all Harness sources = **zero hits**. A zero-velocity teleported body **sleeps**, and a sleeping body gets no gravity integration and no drift — the "gravity disabled, held permanently" the user saw is an *implicit vphysics sleep artifact*, exactly as the design doc already warned: "an *asleep* cube **levitates** (a vphysics sleep artifact, not reliable)" (laser_redirect_verb_design.md:196-201). It's permanent because Source only wakes a sleeper on contact/impulse, and `wait`/`AdvanceTicksBlocking` issues no impulse — no `Wake` call exists in the seat path.

**The angle is the load-bearing illegitimacy.** `ComputeRedirectYaw` (LaserGeometry.cpp:82-90) calls `Math::VectorAngles` which sets `pitch = atan2(-fwd.z, xyDist)` (Math.cpp:100-104) and only zeroes **roll** (`ang.z=0`), **not pitch**. For step 39 (cube (830,634,146.6) → target 14 (448,1280,288.1)) that bakes in a **~10.7° upward pitch** (computed from verified coords — *larger* than the "several degrees" first claimed). A weighted cube resting flat on floor is axis-aligned (pitch 0); a 10.7°-pitched cube can only hold that tilt because it's asleep.

**The asymmetry the user suspected is real:** `interpose` has a floor gate (`DownTraceRest` + NO_FLOOR, LaserGeometry.cpp:67-80, MacroExecutor.cpp:1147); `redirect_to` has **none** — `RedirectConfirm` captures `*seat = cube->abs_origin()` (the already-frozen position, MacroExecutor.cpp:1243-1246) and re-seats with a fresh pitched yaw, never calling `DownTraceRest`. POWERED is read straight off the frozen pose (MacroExecutor.cpp:1432-1447, :1271) with **zero plausibility test**.

**Run evidence:** step 38 `interpose 10 14 → NOT_POWERED` (11.3° off) seated a floor-rest cube that *didn't* power; step 39 `redirect_to 15 14 → POWERED` only *rotated* that same cube (added the up-pitch) and flipped it to POWERED. So **the pitch is what "made it work."** Cube sat at z≈146.5 over a z=128 floor across steps 39/44/66/92 with no support re-check between (held=0). frame_067.png shows the magenta beam angling *up* from the low cube to the higher catcher.

**Corrections that change the fix (adversarial verify — these are important):**
- **Dropping the pitch (yaw-only) is WRONG here and would produce false NOT_POWERED, not "honest failures."** The beam re-emits along the cube's local +X (design doc line 90). Targets 13/14 are at z=288, cube at z=146 → the beam **must** rise ~142u over ~750u horizontal; the ~10.7° pitch is **geometrically required** to reach the catcher. A flat cube emits horizontally at z≈146 and misses. The doc's "a flat cube still powers a catcher" spike (lines 199-201) only holds when the catcher is at *beam height* (z=32), which this chamber is not.
- **Adding a floor gate to `redirect_to` would NOT catch this trajectory** — the cube is genuinely floor-supported (z=146.5 over z=128), so a down-trace passes. That fix guards a mid-air void float that didn't occur here.
- **So the real bug is narrower:** the harness freezes the cube at a pitch a free reflector on *flat floor* could never hold. A real cube needs an **angled panel / ledge / held pose** to aim a beam upward. The correct correctness bar is **"seat orientation must be physically realizable given the support surface"** — which correctly exposes that this puzzle legitimately needs an *elevated or angled-panel* reflector seat the teleport verb doesn't model.
- **Goo blind spot (missed):** frame_067.png shows mark 15 in what looks like a recessed goo pit; design doc lines 148-149 warn a seat in goo *wrongly passes* the down-trace (it lands on the goo-bottom brush). Any floor-gate fix inherits this.
- **Sleep nuance:** vphysics sleeps only after several sub-threshold frames, so during `kSeatSettle` gravity *is* integrated until sleep latches — which is why a *supported* seat freezes flat-but-pitched while a *void* seat would fall. The `kRedirectTries=3` re-roll loop re-reads `m_bPowered` every attempt with no re-down-trace, **laundering a borderline pose into a stable POWERED across 3 tries.**

**Fixes (revised per corrections):**

| Fix | Effort | Recommend | Note |
|---|---|---|---|
| **Wake + settle + re-check:** after seating, `Wake` the vphysics object, advance a settle window, read `m_bPowered` off the *settled* pose; POWERED only if a real cube still rests there and holds the beam. Gate the re-check on **"cube unsupported"** rather than a blanket free-fall (a blanket drop would fail *legitimate* floor-rest redirects too). | **M** | **YES — this is the actual fix** | Needs the `Wake` vfunc offset; more ticks per redirect; re-run the agentloop_smoke laser check. Removes the 2%-jank re-roll motivation. |
| Add `redirect_to`'s missing floor/support gate (mirror `interpose`). | S | Partial — good hygiene, but does NOT catch *this* trajectory (cube was supported). Inherits the goo blind spot. |
| Drop the pitch (yaw-only). | S | **NO — geometrically wrong here**, would break the required upward beam. |

**The honest conclusion:** treat every POWERED (39/44/66/92) as an illegitimate solve. The benchmark **cannot currently certify a legitimate laser solve on any laser-hold chamber** — this is a *benchmark validity* hole, not just a verb bug.

---

### 3.3 Vision — does the model attend to the image?

**Verdict on user thought #3 (the model doesn't pay ANY attention to the image): NUANCED — directionally right but too strong.**

The model *does* make ~25 image-referencing statements over 100 steps and a handful (~7) of genuinely visual judgments (step 8 grey floor "looks uncooperative," step 30 reading the u,v color grid to confirm S4 is portalable — *correct and useful*, step 40 fizzler position, step 80 "S4 is up on the vertical wall but there's no walkway," step 83 "solid dark grey wall with the portal icon"). But its decisions are overwhelmingly **coordinate-space**: step 99 thinking is pure telemetry ("coordinate readout confirms our position (1008,1045,128) spot on") while the frame shows a huge white portalable wall at point-blank — completely unremarked — and `pass_through` then fails NOT_AT_MOUTH. Step 5 justifies walking off a ledge purely by "the Z-difference is 100... a completely safe drop" → EDGE moved 0.

**Two compounding mechanisms:**
1. **Stateful single chat re-attends the whole history.** `gemini_agent.py:254` opens one `self.chat`; :270-277 appends `[image, percept]` every step. **Verified O(N²):** per-step image tokens grow 1,064 (step 0) → 106,400 (step 99) = exactly 100×, summing to 5,373,200, with **zero cached tokens** — the current frame is literally 1/100th of the image tokens at step 99, buried among ~100 near-identical dark interiors with no "this is NOW" delimiter.
2. **Ordering + prompt lean.** The image is sent **before** the percept text (gemini_agent.py:271), so telemetry is always freshest before the answer. The system prompt (gemini_agent.py:46-136) is ~90 lines of telemetry/verb semantics and **never forces** a visual observation (it has soft gestures — "look around," "look for an opening" — so the ground-truth "NEVER once" is an overstatement, but there is no *required* visual output).

**Corrections (adversarial verify):**
- **Darkness is over-generalized.** Measured all 100 frames: median-of-medians brightness = 44, only **14% are truly dark (<40)**, min 27 / max 193. The centerpiece step-99 frame is **bright (median 193)** and *still* ignored. Darkness can't explain the step-99 neglect — the brighten fix (below) is lower-leverage than first presented.
- **The burial hypothesis is not cleanly proven.** Genuinely-visual reasoning appears at steps 4, 8, 30, 40, 53, 80, 83 — *deep* in the run. If growth-burial were THE cause, visual attention should decay with history; it doesn't. **So the higher-confidence lever is the prompt (force a visual field), not the burial rewrite.**
- **Brighten must NOT go in `encode_png`** — that function is shared by the model-input path AND the `.trajectory` record path (trajectory_io.py:61). Brightening there permanently corrupts every recorded frame the postmortem/visualizer depends on. Apply it only on the model-input path.
- **Annotation as a confound (missed):** step_000 annotates entrance-door mark 5 with a running-man EXIT icon — the on-frame glyph *actively mis-cues* the model. "Make the model look more" could backfire without also fixing exit-mark legibility (§3.4). Evaluate the two jointly.

**Fixes:**

| Fix | Kind | Effort | Recommend |
|---|---|---|---|
| **Force a per-step visual field:** require `"seen": "<walls/panels/drops/doors/beams visibly in front>"` before the verb; add a note that telemetry can be stale/ghosted and the frame is ground truth for reachability. | prompt | **S** | **YES — highest-confidence lever.** Activates the model's already-demonstrated visual competence (step 30). Worst case: a no-op. |
| Send **only the current frame** with a `CURRENT VIEW (what you see now)` label, image placed **after** the percept (recency slot); carry a compact rolling text summary instead of prior frames. | code | M | YES, but more speculative than the prompt fix — the retry loop (gemini_agent.py:276-321) must then manually re-include the current frame in the correction request (the first-pass "nearly the same work" undersells this). |
| Brighten (~1.5× gamma) **on the model-input path only**. | percept | S | Marginal — only 14% of frames are dark; do it, but don't expect much. |
| Upscale client-side. | percept | S | **NO** — a dark 640×480 upscale adds no detail, may cost extra tiles. |

---

### 3.4 Percept legibility & entity-list hygiene (dropper cube, ghost doors, exit identity, fizzler)

**Verdict on user thought #5 (dropper cube STILL annotated) + the exit-illegibility finding: CONFIRMED (all four defects reproduce), but the headline "exit beacon is nearly free" fix rests on a FALSE code claim, and the "model never recognized the exit" framing is contradicted by the run.**

The shared root: **neither the C++ mark layer nor the Python percept applies any spatial/PVS/reachability/exit-distinction filter** — the percept is a flat, unranked dump of every entity of a marked class in the whole BSP.

**(A/B) Dropper cube not suppressed.** Suppression fires *only* on the PeTI-stock `FireUser4` template ping (MarkTable.cpp:171-190), releases on `FireUser1` (:192-208). This map's dropper is custom Hammer — `cubedropper19-cube_dropper_box`, not stock `cdN-box` — and never sends the ping, so suppression **never arms**. Verified: step 0 cube = mark 15 @z=558 (in-tube, unsuppressed); button press → 15 travels tube→floor (z=558→149); a **new phantom mark 16 respawns at z=558 and stays annotated as a grabbable reflective cube through step 99** (summary.json). The 768u backstop (MarkTable.cpp:86) can't help — the phantom never moves. **This is a silent coverage cliff:** the suppression was verified on 223 *stock-dropper* workshop maps (mark_identity_respawns.md); this is exactly the out-of-corpus custom-Hammer case, and it fails invisibly (`sar_harness_mark_debug` off by default).

**(C) Ghost doors / exit illegibility.** `EntitySnapshotter::Update` walks all `NUM_ENT_ENTRIES` with no spatial filter (EntitySnapshotter.cpp:399-428); `RebuildFromWorld` marks every entity of a marked class map-wide (MarkTable.cpp:58-69; `prop_testchamber_door` ∈ kClassColors, PuzzleAnnotate.cpp:66); the Python percept lists every mark>0 with no cull (entities.py:159-176). Step 0: 6 door marks incl. **mark 6 @entrance_airlock dist=3896, mark 7 @exit_airlock dist=3196** — departure/arrival elevator frames baked thousands of units outside the chamber. Real exit = mark 12 @exit_door dist=1758. Doors carry state `{}` (entities.py:47-48) so open/closed isn't even legible. The eval author already flagged this in a TODO (gemini_agent.py:40-44).

**(D) Fizzler multi-mark.** `barrierhazard13_brush` is 3 stacked `trigger_portal_cleanser` brushes sharing a targetname → marks 1/2/3 at z=192/320/448 (128u apart). State legibility *works* (entities.py:41-42 emits `{'active': ...}`, annotate hides the box when off, PuzzleAnnotate.cpp:208-210) — only the 3× duplication is clutter. Lowest priority.

**Corrections (adversarial verify — these matter a lot):**
- **FALSE CODE CLAIM (the headline fix):** the first pass said "PuzzleExit already name-matches `@exit_door` — promoting it to a beacon is nearly free." **Wrong.** `grep '@exit_door'` over all of `src/` = **zero hits.** PuzzleExit.cpp (68 lines) matches `@relay_pti_level_end.Trigger`, `@transition_from_map.Trigger`, `OnLevelEnd`, `ChangeLevel`, and `@exit_airlock_door.Open` — it **never references `@exit_door`**, has no entity origin/mark knowledge (it's two atomics latched from Server.cpp:658), and its only door signal (`@exit_airlock_door.Open`) fires *after* the puzzle is solved. So an exit beacon needs **NEW** name-matching (still cheap in Python off the mark's name) and must target **`@exit_door` specifically**, not the airlock names PuzzleExit trusts (those point at a partly-ghost frame, mark 8 dist=2198).
- **OVERSTATED behavioral claim:** ground truth says the model "NEVER ONCE targeted mark 12 ... treated mark 5 as its goal the whole run." The run refutes the strong version: step 38 "target 14 has a dotted blue line connecting to @exit_door (mark 12)! Bingo!"; step 54 "the exit door (mark 12) is at (192,1293,256)"; step 92 "we can just walk through the exit door!" **The exit was NOT illegible to the model.** What's literally true: **no *action* ever targeted mark 12** (go_to histogram: {5:4, 11:4, 15:13, Po:4, S1:1, 14:1, Pb:2} — zero go_to 12). The real mechanism is a **navigation-commit failure** across a height seam (§3.1), not exit-identity. **This reassigns Defect C's causal weight away from percept illegibility toward the nav planner** — the first-pass "Defect C most plausibly cost the solve" is not supported.
- **The "dotted blue line" may be a hallucination:** I could not find a laser-target→door link renderer in PuzzleAnnotate.cpp (the only linkage at :881-891 is portal↔portal). If the model *inferred* the causal link from proximity, exit legibility "worked" by luck — which strengthens the case for an explicit beacon but means the beacon can't rely on a link the harness doesn't draw.
- **Door open-state is not blocked on a new proto field:** `m_toggle_state` exists in the snapshotter schema (EntitySnapshotter.cpp:74) and `prop_testchamber_door` is registered (:98); the only unknown is whether it's *networked* at runtime. So surfacing exit open-state is a recon question, not a proto change.

**Fixes:**

| Fix | Kind | Effort | Recommend |
|---|---|---|---|
| **Surface `chamber_complete`/exit progress in the percept** — 1 line in `_percept_text` from `obs.state.chamber_complete` (already proto fields 8/9, harness.proto:157-158, set in Portal2HarnessImpl.cpp:213-214; today only read for termination at gemini_agent.py:373). Free negative signal ("not at exit yet") the model currently never gets. | prompt | **S** | **YES** |
| **Exit beacon** — NEW Python name-match on `@exit_door` (not the airlock names) → render mark 12 as `<<< EXIT`. Reuses the mark's `name`, no proto change. | percept | S | YES — but re-scoped: it is NOT "free reuse of PuzzleExit," and given the model already named the exit, its causal value is **mark-namespace hygiene**, not "this would have solved it." |
| **Distance/in-bounds cull** for ghost doors (3196/3896u). | percept | M | YES, but the margin is narrow: real exit 1758 vs airlock-duplicate 2198 vs ghost 3196 — only ~1000u. A magic radius is fragile; an AABB/name cull is the principled path (harness computes no chamber bounds today). |
| **Robust dropper backstop** — not solely the FireUser4 ping. Cheaper than the proposed z-height heuristic: the phantom [16] is **byte-identical in name** to [15] and coexists; the name-inheritance path (MarkTable.cpp:123-135) already tracks same-name cohorts, so a *same-name cube spawned at a prior cube's origin* test catches the respawn with no floor geometry and less false-positive risk. | code | M | YES |
| Fizzler same-name coplanar dedup. | code | M | **NO** — cosmetic; state is already legible; risks merging genuinely distinct same-name fizzlers (mark_identity_respawns.md:14). |

---

### 3.5 `pass_through` — no pause on the transit tick

**Verdict on user thought #6 (pass_through didn't pause immediately after passing through; listen for a portal-cross event): NUANCED — it genuinely does NOT stop on the transit tick (detects up to a 2-tick batch late, then runs 20 more settle ticks), but the observed overshoot in THIS run was tiny (~0.3-0.5u) because both transits were low-speed grounded emergences and the reported position is post-settle (which hides drift).**

**Mechanism:** the push loop advances `kPassBatch=2` ticks/iter (MacroExecutor.cpp:1755) and detects transit only by per-batch feet displacement > `kPassJump=48u` (:1737-1740) — **post-hoc, up to a full batch late**. Then unconditionally: clear the input but **keep `m_vecVelocity`** (`SetMoveFramebulk(0,0)` only zeroes buttons/analog, def :659-665) and `AdvanceTicksBlocking(kSettle=20)` (:1758-1762, :109). The reported position is re-read **after** the settle (:1777) and the detail carries **position only, no velocity** — contrast `drop_into` (:2226-2230) and `jump_into` (:1960-1964) which report `vel=`.

This settle was correct *before* the drop_into work, when the same block also **zeroed velocity**. Commit `b299bd52` removed `pl->field<Vector>("m_vecVelocity") = {0,0,0}` to satisfy the new family invariant "never destroy emerged momentum." **Post-removal, `pass_through` is the only traversal verb that both preserves momentum AND runs a settle** — an airborne emergence coasts for 20 ticks instead of freezing mid-flight the way the flings do. There is **no engine portal-teleport hook** to latch (PortalRead.cpp:11-32 is snapshot-only); the position-jump heuristic is the only cross-event, and `drop_into`/`jump_into` already run it at **1-tick** granularity (:2195) — the reference implementation for what the user asked.

**Run evidence:** step 57 TRANSITED (1504,274,128) vs step-58 start (1503.6,274.4,128) = ~0.57u; step 63 TRANSITED (1096,473,244) vs step-64 (1096,473,243.7) = ~0.3u. Step 63 *is* airborne (z=244 is 76u below its z=320 wall-portal). Steps 50 (BLOCKED mouth) and 99 (NOT_AT_MOUTH) are reachability failures, **not** timing bugs — they bound the scope to the TRANSITED path.

**Corrections:** the "airborne coast can carry the body an arbitrary distance" claim is only true for a *high-speed* exit (portal-boost / flung entry), which `pass_through`'s walk-in approach doesn't produce — step 63 was airborne yet drifted 0.3u. It's a **latent timing+legibility defect**, not a demonstrated overshoot. And fix #1 changes behavior for the **common grounded case** (100% of this run's transits) — every grounded `pass_through` would then report a player still carrying ~walk-speed velocity (arguably more legible, but a real semantic change to the normal path, not just a rare edge fix).

**Missed:** the transit-tick position is *already captured* at `*emerged = *feet` (:1751) before the settle — so the legibility variant needs only a velocity read, nearly zero cost. And `kEmergeRadius=128u` (:62) is checked on the **post-settle** position (:1780), so drift is silently absorbed into the success tolerance; freezing on transit (fix #1) makes that gate meaningful again as a side effect.

**Fixes:**

| Fix | Effort | Recommend |
|---|---|---|
| **1-tick detect + freeze ON transit** (near-copy of drop_into's tail, MacroExecutor.cpp:2214-2225): clear input, leave `m_vecVelocity`, advance nothing; report the frozen transit position **+ velocity**. Answers the user's ask exactly; unifies the family invariant. | **S** | **YES** — note it changes the common grounded path; document "pass_through leaves you paused mid-step, `wait N` resumes." |
| `kPassBatch` 2→1 as a standalone one-liner (:59). Halves detection lag. | S | **YES** — pair with fix #1. |
| Do NOT add an engine portal-teleport hook (none exists; L-effort, 32-bit-fragile; the heuristic already works). | L | **NO** |

---

### 3.6 `place_portal` NO_LOS spam + orientation burn (aim_at 18 + look 14 = 32 steps)

**Verdict on findings E (8/19 place_portal NO_LOS) + F (32/100 orientation-only): CONFIRMED — both share one mechanism: the text percept lists ALL panels with zero visibility signal while the frame LOS-culls their labels, so the model must spend a step on `place_portal` (or a false-positive `aim_at`) to discover LOS it can't otherwise perceive.**

**Mechanism:** `surface_marks` is the full chamber-static panel list every observe, unfiltered (Portal2HarnessImpl.cpp:223-231; SurfaceMark proto harness.proto:133-140 has **no visibility field**); `_panel_dict` projects each into an actionable `S{n}` mark with empty state (entities.py:80-96) — shape-identical to a real entity mark, so the model can't tell a visible panel from an occluded one. But the frame draws the `Sn` label **only if** `MarkVisible(eye→anchor)` passes (a MASK_OPAQUE traceray, PuzzleAnnotate.cpp:107-124,321-334) **and** it's on-screen. So an occluded panel is fully targetable in text but has no on-frame label → the model targets it by bearing → the shot hits the occluder → `NO_LOS` (MacroExecutor.cpp:1607-1614). **`place_portal` is the only channel that reveals per-panel LOS.**

**`aim_at` compounds it:** `AimAt` does **zero tracing** — resolves the mark, applies view, always returns SUCCESS (MacroExecutor.cpp:2319-2364). It's a false-positive "facing" signal, not a visibility probe. And `place_portal` **already auto-aims** at the panel (:1532-1560), so `aim_at Sn` before `place_portal Sn` is dead weight — I counted **9 such redundant pairs** (steps 29,45,55,61,67,70,85,93,96).

**Run evidence:** `place_portal S1 → NO_LOS ×4` (steps 80,82,86,87) — at step 80, S1 is at (1536,256,192) targeted from (174,8,256), **1384u away through walls**, because the percept lists all panels regardless of dist/LOS. Step 85 `aim_at S1 → SUCCESS` ("now that we can see it around the corner") → step 86 `place_portal S1 → NO_LOS` — a genuine false-positive the model trusted. frame_080 shows S1 has no on-frame label; frame_086 shows the crosshair on a bare dark-red non-portalable wall with no S-label anywhere.

**Corrections (adversarial verify):**
- **The proposed `visible`-flag fix is NOT S-effort and the "observe already hops to the main thread" claim is FALSE.** `InternalObserve` runs on the gRPC thread and does **zero tracerays** today (grep of Portal2HarnessImpl.cpp: no TraceRay/engine trace); all LOS logic lives on the main thread. Adding per-panel `MarkVisible` in observe forces a main-thread hop on the hot `AgentLoop` step path (treated as expensive, like the opt-in pixel copy).
  → **The tasteful KISS fix instead:** compute panel visibility **during the annotation RENDER pass** (which already calls `MarkVisible` per panel, PuzzleAnnotate.cpp:322), cache a per-panel `visible` bool into the mark table, and have observe read it **lock-free**. Reuses the exact traceray already run, zero added trace cost.
- **The "document that place_portal auto-aims" prompt fix is a near no-op** — macro_grammar.py:127 **already** says "Aims at the panel center by default." The model was told and aimed redundantly 9× anyway. The redundant `aim_at` is **not a docs gap** — it's the model defensively probing for the LOS signal it lacks. The visible flag is the real lever.
- **The fizzler cases (40-43,56) are a DIFFERENT mechanism.** A fizzler isn't opaque, so MASK_OPAQUE `MarkVisible` returns TRUE there — the visible flag would wrongly say "visible" and NOT have prevented steps 40/41/43. Label the flag **"visible" not "placeable"** (the annotation uses MASK_OPAQUE, the place-gate uses MASK_SHOT_PORTAL, MacroExecutor.cpp:1607 — they differ on fizzlers/glass).
- **Missed:** `MarkVisible` traces to a **single point** (PuzzleAnnotate.cpp:321-322), unlike `EntityVisible` which samples 8 corners — a large panel visible only at an edge is flagged not-visible. Sample corners if the flag must be trustworthy. And `place_portal` has **no range cap** (TraceFirePortal is unbounded, :1607) — a 1384u target is "legal"; a cheap pre-gate (reject beyond some dist / not floor-reachable, distinct result code) kills the spam at the verb layer.

**Fixes:**

| Fix | Effort | Recommend |
|---|---|---|
| **Cache per-panel `visible` in the RENDER pass, read lock-free in observe** (not an inline observe traceray); surface as `(occluded)` in the percept. Sample corners like `EntityVisible`. Label "visible" not "placeable." | **S-M** | **YES — the real lever** |
| Richer NO_LOS detail (what the shot hit: occluder classname / "off-panel by N" / "behind fizzler") — data already in hand at :1609/1614. | S | **YES** — turns a dead-end into a reposition hint. |
| Range / reachability pre-gate on `place_portal` with a distinct code. | S | YES — complementary, kills the 1384u spam at the verb layer. |
| Make `aim_at` return NO_LOS when the eye→target ray is blocked. | M | **NO** — overloads `aim_at`'s "turn toward a thing I can't see yet" use; the `visible` flag informs without breaking it. |

---

### 3.7 The reasoning trap that both ground truth and the first pass missed: 1 cube, 2 mutually-exclusive targets

**This reframes the whole run. Verdict on the meta-thesis ("locomotion + illegibility, plan was correct"): CONFIRMED that locomotion dominates the budget — but the plan was NOT correct; it was under-constrained.**

**Verified per-step powered map** (re-checked by hand against raw `state.powered`/`state.active` for every step, not just the agent's word):

| signal | powered/active windows (step ranges) |
|---|---|
| target 14 (opens `@exit_door`) | **40-44, 67-92** |
| target 13 (kills fizzler) | **45-66, 93-99** |
| fizzler `active` | **0-44, 67-92** (default-on ∪ target-14-on; the exact complement of target-13) |

target 13 and 14 are **never simultaneously True** — one reflector cube, one beam, one target at a time. They are perfectly anti-correlated, and each `redirect_to` flips the pair (steps 39→14, 44→13, 66→14, 92→13).

The chamber needs the **fizzler down** to cross to the exit side **AND** the **exit door open** to leave — but a single cube can't power both simultaneously. At terminal (step 99): fizzler down (13 lit), target 14 unpowered → **exit door SHUT**. The "flip-flop redirect_to 13↔14" that ground truth dismissed as flailing is the model repeatedly hitting a genuine **2-constraint / 1-actuator contradiction it never diagnosed.** The real solve routes the beam **through a portal** (so both stay lit), or portals across *during* the fizzler-down window, or uses a second cube. **The model never realized this. It IS a reasoning datapoint.**

**Corrections to the meta-narrative (adversarial verify):**
- **"Learned helplessness — named go_to 12 then refused it" is a misread.** The step-54 choice of `go_to 14` over `go_to 12` was arguably **correct**: target 14 was **unpowered** at step 54 (per the powered map), so the exit door was physically shut — walking to mark 12 would have been useless; the model needed the cube to re-power 14 first. Correct sub-goal ordering, not timidity.
- **"Fixated on mark 5 as the exit for most of the run" is overstated.** In the second half, `go_to 5` is a **navigation waypoint** to reach the stairs/top floor (step 69: "walking back up the stairs to the entrance door (mark 5) so we can reach the portal on S4"), not the goal-exit. frame_099 shows the model on the *exit side* doing its final `pass_through`. Illegibility poisoned the *early* goal signal, not the endgame.
- **The endgame blocker is `pass_through`, not `go_to`.** Terminal step 99 is a `pass_through` mouth-approach failure — a verb-robustness gap distinct from the `go_to` height seam. **A descend-fix in GoToPlanner alone would NOT have unblocked the endgame** — by then the model was teleporting via portals and hit the mutual-exclusion trap + `pass_through` alignment.
- **The fake laser solve (§3.2) is what masked the trap:** because POWERED latched off a frozen cube, the model *believed* it had "solved" each target and moved on, never confronting that it never held both. **Surfacing "this cube currently powers target N" in the percept** would let the model reason about the one-cube constraint — a percept-completeness gap that directly feeds the reasoning failure.

**No loop-breaker (confirmed):** one stateful chat, no recent-action summary, no repeated-failure signal, no no-progress detector (gemini_agent.py:254-277). `place_portal S1 → NO_LOS ×4` and `go_to 15 → ADVANCED/BLOCKED ≥8×` with no behavioral change; ~45 dead steps after the run was effectively over, no early terminal.

---

## 4. User's raw thoughts → verdict → root cause → fix

| # | User thought | Verdict | Root cause (file:line) | Recommended fix |
|---|---|---|---|---|
| 1 | `go_to` couldn't walk around/down the stairs | **CONFIRMED** | Single-Z grid + horizontal Passable sweep sever any height seam; ADVANCED invites infinite retry (GoToPlanner.cpp:107-166; MacroExecutor.cpp:2544) | Step-height patch (M) → layered-Z (L); `HEIGHT_SEVERED` code (S) |
| 2 | `redirect_to` "paused" the cube → crazy angles in the air; **fake solve** | **CONFIRMED (correctness bug)** | Zero-velocity teleport → vphysics **sleep artifact** (no gravity flag); kept ~10.7° pitch a flat-floor cube can't hold; POWERED read off frozen pose (MacroExecutor.cpp:486-504; LaserGeometry.cpp:82-90) | **Wake+settle+re-check gated on "unsupported"** (M); NOT yaw-only (pitch is geometrically required) |
| 3 | Model pays no attention to the image | **NUANCED (mostly right)** | Coordinate-space reasoning; image sent before text, no forced visual field, O(N²) burial (gemini_agent.py:46-136,254-277). Darkness over-blamed (median 44). | **Force a `"seen"` field (S)**; per-step CURRENT VIEW frame (M); brighten on input path only |
| 4 | A `jump_down`/descend-ledge verb? | **PARTLY — a descend model is needed, but as a `go_to` capability, not (necessarily) a new verb** | No descend primitive exists; `jump_into`/`drop_into` are floor-PORTAL only (jump_into_design.md:46-47,127) | Fold descent into GoToPlanner+CheckEdge (§3.1); standalone verb NOT recommended (redundant) |
| 5 | Dropper-tube cube STILL annotated | **CONFIRMED** | Suppression keyed on stock `FireUser4` ping this custom-Hammer map never sends; phantom mark 16 @z=558 all run (MarkTable.cpp:171-208) | Same-name-cohort respawn backstop (M) + surface `in_dropper` (S) |
| 6 | `pass_through` didn't pause on transit; listen for the cross event? | **NUANCED (real timing+legibility defect)** | 2-tick post-hoc detection + 20-tick momentum-preserving settle; no engine hook exists (MacroExecutor.cpp:1730-1762; PortalRead.cpp:11-32) | 1-tick detect + freeze-on-transit (S) + `kPassBatch` 2→1 (S) |

---

## 5. Additional problems the user did NOT list (NEW)

- **[NEW — the actual reasoning failure] 1 cube / 2 mutually-exclusive targets (§3.7).** Targets 13 and 14 are perfectly anti-correlated; the chamber needs both simultaneously; the model never diagnosed the contradiction. This is the single most important *new* finding — it means the run *is* a reasoning datapoint, not a pure locomotion write-off.
- **[NEW] The fake laser solve makes the benchmark unable to certify ANY legitimate laser-hold solve** (§3.2) — a benchmark-validity hole, and the direct enabler of the mutual-exclusion blindness (the model thought each POWERED was permanent).
- **[NEW] Redundant re-work tax (~11 steps).** The percept never says "this portal is already on this panel" or "target 14 is already powered (latched vs momentary)," so the model re-places/re-powers already-done things (steps 49,60,62,63,65,66,68,71,92,94,97). Surfacing **portal occupancy per S-panel** and **"this cube currently powers target N"** closes both this and the mutual-exclusion gap.
- **[NEW] No loop-breaker / no no-progress terminal** — ~45 dead steps burned after the run was over (§3.7).
- **[NEW] `aim_at` manufactures false confidence** — always SUCCESS, no trace; corrupts the *next* frame too (step 85 aimed the crosshair onto the occluding wall → frame_086 lost all S-panel anchors) (§3.6).
- **[NEW] Step-UP is as broken as step-down** — a platform floor >40u above the go_to start is invisible to the probe (§3.1); late-run failures to climb *back* onto platforms.
- **[NEW] Per-entity-not-per-object marking** inflates the mark namespace (fizzler 1/2/3); will bite any multi-brush object (folding-panel stairs, tall glass) (§3.4).
- **[NEW] Goo blind spot in the down-trace** — any floor-gate fix for the cube inherits it (design doc lines 148-149) (§3.2).

---

## 6. Prioritized, sequenced recommendations

Separated by track. Rationale: **locomotion drowned the budget before the puzzle started, so it gates every multi-level chamber; but the correctness bugs (laser freeze) gate benchmark validity, and the reasoning trap is the thing that makes this run *interesting*.** Do them in this order.

### Track EVAL-SCOPE (do FIRST — it's free and it's the ROADMAP contract)
1. **[S] Declare this run a non-datapoint for *clean* reasoning, and gate v0 chamber selection on locomotion-solvability.** Before a chamber enters the suite, require a macro_repl/human solve using only the shipped verbs. If a human can't traverse it, it tests locomotion, not reasoning — out of v0 (ROADMAP.md:12-17 separability thesis). **BUT** — given §3.7 — re-run this exact chamber *after* the fixes below as a controlled ablation, because it *does* contain a real reasoning trap once locomotion and the fake solve are removed.
2. **[S] Confirm this map's scope:** the multi-level stairs + mid-air laser-hold + fizzler-behind-glass feel beyond stock-PeTI "average researcher" scope (brainstorm/puzzlemaker_elements.md). If it's BEEmod/custom, it's out of v0 by the ROADMAP's own rule.

### Track CORRECTNESS (do SECOND — benchmark validity)
3. **[M] Fix the fake laser solve** (§3.2): Wake+settle+re-check gated on "cube unsupported." Until this lands, no laser-hold chamber can be certified. This is also what unmasks the mutual-exclusion trap. Validate on the dual-role + laser_and_button chambers first.
4. **[S] `pass_through` freeze-on-transit + `kPassBatch` 2→1** (§3.5) — the endgame blocker for THIS run's terminal.

### Track LOCOMOTION (do THIRD — the single highest-leverage unblock for the *class*)
5. **[M] Step-height patch** in GoToPlanner (two-segment step-down test, not a lowered horizontal sweep) + bump `kStepDownMax`~72 + relax the VFH edge-guard for planned descend legs. **Both planner and march must agree** or you ship a plan the body refuses. Verify the risers are <64u first.
6. **[S] Thread the existing `BlockReason` enum out of `RouteAround` → `HEIGHT_SEVERED`** result code (§3.1) — stops the infinite `go_to` retry loop. Ship even if #5 slips.
7. **[L, later] Layered-Z GoToPlanner** — for arbitrary multi-level chambers, after #5 proves the geometry is traversable.

### Track PERCEPT (do FOURTH — hygiene + closing the reasoning loop)
8. **[S] Surface `chamber_complete` + "cube currently powers target N" + portal occupancy per S-panel** in `_percept_text`. The powered-per-cube line is what would let the model diagnose the one-cube/two-target contradiction (§3.7). This is the highest-value percept change.
9. **[S-M] Cache per-panel `visible` in the RENDER pass, read lock-free in observe** (§3.6) — NOT an inline observe traceray. Kills NO_LOS spam. Add a NO_LOS reposition hint.
10. **[S] Force a `"seen"` visual field in the prompt** (§3.3) — highest-confidence vision lever. Evaluate *jointly* with the exit-mark fix (the entrance-door EXIT glyph mis-cues visual attention).
11. **[M] Ghost-door cull + exit beacon on `@exit_door` specifically** (§3.4) + same-name-cohort dropper backstop. Re-scoped: the model already *named* the exit, so this is namespace hygiene, not the thing that would have solved it.
12. **[S] No-progress signal** in gemini_agent.py: after N identical failing (verb,target,code) tuples, inject a factual "you've tried X 4× → NO_LOS" line (report the repetition, don't prescribe the fix — keep the frozen-model purity), plus an optional early terminal to stop burning dead steps.

**The one thing to do if you do nothing else:** #5+#6 (locomotion descent + a distinct severed-height code). It converts a whole *class* of chambers from "conflated locomotion+reasoning failure" back into a clean reasoning signal — which is the entire point of the benchmark.