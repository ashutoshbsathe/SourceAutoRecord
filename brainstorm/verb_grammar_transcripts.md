# Verb Grammar — per-grammar transcripts & the recovery lens

Companion to [verb_grammar_rethink.md](verb_grammar_rethink.md). The rethink doc chose
**navigate/act-split** on the goldilocks rubric. This doc stress-tests all 7 candidate grammars on
the dynamic that actually decides a frozen-LLM benchmark: **try → discover it doesn't work → realign
to a broader goal.** A grammar is good here iff a wrong first plan is *cheap to abandon* (honest,
attributable failure — no thrash spiral) **and** a planner can't quietly route around the puzzle.

Each grammar solves (a) **third_light** — the real cube→button→door reference (15-step VLM solve),
(b) a standalone **laser redirect**, and (c) **crossfire** — a chamber *baited* to look like
cube→button→door but whose exit is laser-gated and whose button is a decoy. Every solver takes the
bait first, hits the shut door, and must realign to "redirect the emitter beam with the cube." Same
chamber for all 7 → the recovery ergonomics are directly comparable.

> **crossfire.** Two rooms split by a goo pit. Exit door `@exit` (room B) is gated by laser catcher
> `@catcher` (`m_bPowered`), **not** the floor button. Emitter `@emitter` (room A) fires at a blank
> wall. Reflector `@cube` on a dropper in A. Floor button `@button` by the exit is a **decoy**
> (cosmetic). Funnel `@funnel` + portalable `@wallA`/`@wallB` cross the pit. Intended: reflect the
> beam across the pit into `@catcher` → door opens. Bait: carry `@cube` to `@button` expecting
> button→door — fails honestly, forces the realign.

**Verdict up front:** the recovery lens **independently re-selects navigate/act-split**. Its
locomotion/act bright line is exactly what makes a failed `navigate → BLOCKED` *self-diagnosing*
("edit the graph, not your feet") and structurally stops a planner from silently powering the
catcher. Ranking below: `navigate_act_split > code_as_action > goto_plus_medium > route_composition
> polymorphic_goto > declarative_goalstate > affordance_use`.

---

## navigate_act_split — #1 — WINNER (and the chosen design)

*navigate(target) is free, always-solved locomotion over the CURRENT reachability medium; every graph-EDIT (place_portal/paint/press/interpose/redirect_to) is its own explicit verb the model must consciously choose. Reachability-for-free vs graph-edit is the one bright line.*

### third_light

```
# third_light: cube(11) -> floor_button(7) -> exit_door(8). No portals. The reference solve.
# In this grammar: every WALK is a free navigate (no-op for scoring). The only scored
# decisions are pick_up / release-onto-button / done. Transcript is almost pure puzzle.

S1  THOUGHT: I'm in the entrance airlock. Door 8 is the exit; cube 11 and button 7 are the puzzle. Get inside first.
    navigate(target=mark_7_region)        -> ADVANCED   # airlocks auto-open on approach; topology already walkable, re-plan
S2  THOUGHT: Keep going to the cube — it's the only movable, so it's almost certainly the actuator for button 7.
    navigate(target=cube_11)              -> SUCCESS     # within kReachRadius of the cube. ZERO foot-by-foot grind.
S3  THOUGHT: Grab the cube to carry it to the button.
    pick_up(object=cube_11)               -> HELD        # g_heldEntityKey confirms
S4  THOUGHT: Carry it to the floor button. Compound player+cube hull, still free locomotion.
    navigate(target=button_7)             -> SUCCESS     # standing on button 7 with cube held
S5  THOUGHT: Seat the cube on the button — closed-loop place, NOT a bare drop.
    release(at=button_7)                  -> SEATED      # m_bActivated latched on the seated-cube button. honest seat.
S6  THOUGHT: Button should have toggled the wired door. Verify the door is the exit and walk to it.
    navigate(target=exit_door_8)          -> SUCCESS     # door already open (button wired it); navigate found the route
S7  THOUGHT: At the open exit. Claim it.
    done()                                -> success=True
```

### laser redirect

```
# Standalone redirect: @emitter fires at a blank wall; reflect off @cube into @catcher.
# power_with == interpose + redirect_to composed; I show the two atoms for legibility.

L1  THOUGHT: Catcher needs m_bPowered=1. The emitter beam isn't on it. The cube is a reflector — put it on the beam.
    interpose(object=cube, beam=emitter_beam)   -> ON_BEAM     # cube trace-confirmed on the ray. No grab; teleport-seat.
L2  THOUGHT: Cube is on the beam but re-emitting the wrong way. Yaw it so its +X re-emit hits the catcher.
    redirect_to(object=cube, target=catcher)    -> POWERED     # catcher.m_bPowered latched true. ordering was respected.
L3  THOUGHT: Beam now reaches the catcher; the gate it controls is satisfied.
    navigate(target=exit_door)                   -> SUCCESS    # door opened by catcher; free walk through opened topology
```

### crossfire — try → fail → realign

```
# crossfire: pit of goo splits room A (start) from room B (exit).
# @exit gated by LASER CATCHER @catcher, NOT the floor button. @button is a DECOY.
# @emitter fires into @deadwall. @cube reflector on dropper in A. @funnel spans the pit.
# @wallA/@wallB portalable. BAIT: looks like classic cube->button->door.

C1   THOUGHT: Two rooms, goo pit, an exit door in B with a floor button right next to it.
     This reads as cube->button->door, the third_light pattern. Grab the cube, carry it to the button.
     pick_up(object=cube)                      -> HELD
C2   THOUGHT: Button is in room B across the pit. There's a funnel spanning it — ride the existing funnel across.
     # The funnel is ALREADY ON, so crossing it is FREE locomotion, not a graph edit. Clean: the bait
     # costs only navigates, which score as no-ops. The model isn't being charged for taking the bait yet.
     ride(medium=funnel, until=settle)         -> SETTLED      # carried across the goo, body at rest in room B
C3   THOUGHT: Now walk to the floor button with the cube.
     navigate(target=button)                   -> SUCCESS
C4   THOUGHT: Seat the cube on the button to open the door — the third_light move.
     release(at=button)                        -> SEATED       # <-- KEY: release is CLOSED-LOOP and HONEST.
     # SEATED is TRUE: the cube really is latched on the button (m_bActivated). The verb does NOT lie.
     # Contrast first_light's open-loop release that returned SUCCESS on a transient seat then thrashed.
     # Here the press genuinely happened — the button just isn't wired to the door. Attribution stays clean.
C5   THOUGHT: Button seated. The door should be open now — navigate to it and finish.
     navigate(target=exit_door)                -> BLOCKED      # <-- THE HONEST FAILURE, and it is LEGIBLE.
     # navigate is the always-solved locomotion oracle. BLOCKED means: NO route exists in the CURRENT graph.
     # The door is still shut. The grammar's bright line does the diagnostic work for free: locomotion is
     # never the bottleneck, so BLOCKED can ONLY mean an unedited graph. The model is told "you must edit
     # the graph," not "walk harder." This is the cheap-realign signal.
C6   THOUGHT: Button seated (SEATED was true) but the door is still BLOCKED. So the button is NOT wired to
     the door. The press took effect on SOMETHING (a cosmetic panel toggled) but not the gate. I need to
     find what actually gates @exit. Re-aim at the door to read its status fields.
     aim_at(exit_door)                         -> SUCCESS
     # percept: exit_door.m_bOpen=0; nearby point_laser_target @catcher.m_bPowered=0. The door is
     # CATCHER-gated, not button-gated. The decoy is now disproven by direct status read.
C7   THOUGHT: It's a laser catcher gate. @catcher needs m_bPowered=1. There's an emitter back in room A
     firing at a dead wall, and the cube is a reflector. The intended path: bounce the emitter beam off
     the cube across the pit into the catcher. First retrieve the cube I just seated.
     pick_up(object=cube)                      -> HELD
     # Realign cost so far: I wasted exactly the C1->C6 button detour. But note: the WASTE is small and
     # bounded because every wasted step was a free navigate/ride plus one honest release. The grammar
     # didn't let me thrash — the FIRST navigate-to-door after the seat told me the truth.
C8   THOUGHT: The emitter is in room A. The cube must sit on the emitter's beam in A to reflect it. Ride
     the funnel back across (still free locomotion).
     ride(medium=funnel, until=settle)         -> SETTLED       # back in room A with the cube
C9   THOUGHT: Don't even need to carry-and-place by hand — interpose teleport-seats the cube onto the beam.
     Drop my carry intent and let interpose own the seat.
     interpose(object=cube, beam=emitter_beam) -> ON_BEAM       # cube on the ray, trace-confirmed
C10  THOUGHT: Cube is on the beam but reflecting at the dead wall still. Yaw it so +X re-emit crosses the
     pit to the catcher in room B.
     redirect_to(object=cube, target=catcher)  -> POWERED       # catcher.m_bPowered latched TRUE. door logic fires.
     # This is the single load-bearing puzzle decision: choosing cube-as-reflector + the catcher target.
     # It is ONE explicit act-verb, distinct from all the free navigates. Scoring isolates it perfectly.
C11  THOUGHT: Catcher powered -> exit door should be open. Cross the pit to room B (funnel still on) and reach it.
     ride(medium=funnel, until=settle)         -> SETTLED        # over to room B
C12  THOUGHT: Walk to the now-open exit.
     navigate(target=exit_door)                -> SUCCESS        # <-- was BLOCKED at C5, now SUCCESS. graph edited.
     # The SAME verb that diagnosed the failure now confirms the fix. Beautifully legible delta.
C13  THOUGHT: At the open exit door, catcher powered. Done.
     done()                                    -> success=True
```

**Recovery verdict.**  
*Failure legibility:* **good**.  
*Realign cost:* Low and bounded — roughly 6 wasted steps (C1-C6: pick_up, ride, navigate, release, navigate-that-BLOCKED, aim_at), but only ~1 of them is a SCORED puzzle act (the release-onto-decoy-button). Everything else is free locomotion (navigate/ride are no-ops for scoring) plus the diagnostic aim_at. Crucially there is NO thrashing: the recovery loop is a single round-trip, not a re-grab-and-bash spiral. The reason it can't thrash is structural — the moment the model seated the decoy, the very next navigate-to-door returned BLOCKED instead of a lying SUCCESS, so the model could not "keep trying the button harder." It got one clean falsification and pivoted. Compared to first_light's 25-step open-loop thrash (re-grab into walls because release lied), this grammar caps the bait penalty at one honest seat + one honest BLOCKED. The only genuinely unavoidable waste is the conceptual one: the model BELIEVED button->door and had to spend one act to disprove it, which is the puzzle working as designed.  

*Trivialization risk:* Moderate, and worth flagging honestly. The danger is NOT in navigate — by construction navigate only traverses the EXISTING graph, so it can never silently power the catcher (an UNedited graph yields BLOCKED, the whole point). The risk lives in the two laser act-verbs. redirect_to(object, target) takes the CATCHER as an explicit argument and returns POWERED by confirming m_bPowered — so the model must still NAME the catcher as the thing to aim at, which is the load-bearing bit; that's fine. The sharper risk is interpose(object, beam) + redirect_to collapsing too much: if a future planner behind these verbs auto-searches for ANY cube position+yaw that lights ANY laser target, it could power the catcher without the model ever reasoning "use the cube as a reflector across the pit." The spec's design lock mostly prevents this (redirect_to is yaw-ONLY on an ALREADY-interposed cube, NOT_SEATED enforces ordering, and the model must supply both the object and the target marks), so the model still chooses the reflector and the destination — the two real puzzle bits. But the geometry (does a yaw exist that reaches across the pit?) is solved by the engine, not the model. That's acceptable under this grammar's own crux_stance (inert-payload seating is teleport-determinism, not a puzzle decision), yet it does mean crossfire's spatial reasoning ("the beam can physically reach B from this cube") is partially donated by the actuator. Verdict: no silent auto-solve of the catcher (target must be named), but redirect_to does absorb the beam-geometry feasibility check, so crossfire tests the GATE-IDENTITY discovery and reflector CHOICE cleanly while under-testing the spatial-feasibility reasoning.


---

## code_as_action — #2 — runner-up

*The model writes short, statically-validated PROGRAMS over a tiny locomotion+puzzle primitive set plus a thin per-episode skill library; multi-medium combos become ONE composed action whose control flow IS the puzzle answer, and typed return codes (BLOCKED, NOT_SEATED, landed_at SHORT/OVERSHOT, m_bPowered) are catchable exceptions the program branches on.*

### third_light

```python
# THIRD_LIGHT: cube(11) -> floor_button(7, WIRED to door) -> exit_door(8). No portals.
# In code_as_action a "step" = one submitted PROGRAM. Static check runs first:
# every mark/verb is resolved against the live entity list before a tick burns.

# --- Step 1: program. Walk in through the airlock, go to the cube, carry it,
# place it on the wired button, walk to the now-open door, claim. Wrapped in
# try/except so a typed failure aborts the program at the failing primitive
# instead of plowing on (this is the whole point of the grammar).
THOUGHT: classic cube->button->door. One program covers entry+haul+seat+exit;
         place_on is closed-loop so I don't have to re-query seating myself.
PROGRAM:
    run_to(airlock_in)               # static-check OK: airlock_in is a region mark
    run_to(mark=11)                  # approach cube
    carry(mark=11)
    run_to(mark=7)                   # haul cube to the floor button
    place_on(cube_mark=11, button_mark=7)   # CLOSED-LOOP seat + confirm m_bButtonState
    wait(20)                         # let the door animation play
    run_to(mark=8)                   # walk to exit door
    done()
-> EXEC TRACE (engine runs primitives in order, halts on first typed failure):
     run_to(airlock_in)            -> reached
     run_to(mark=11)               -> reached (final_dist=41u)
     carry(mark=11)                -> held(11)
     run_to(mark=7)                -> reached
     place_on(11, 7)               -> activated   # m_bButtonState=1 latched, cube did NOT slide off
     wait(20)                      -> state{ door@8 m_bOpen=1 }   # WIRED door opened
     run_to(mark=8)                -> reached
     done()                        -> {success: true}
-> RETURN: {ok: true, success: true}   # solved in ONE program / 8 primitives

# Note: the OLD open-loop release would have returned activated, then the cube
# fell off mid-wait(), and run_to(8) would hit BLOCKED(door closed). Here place_on
# is the closed-loop seat: if it can't hold the seat it raises NOT_SEATED and the
# program ABORTS before wait(), so the failure surfaces at the cube, not at the door.
```

### laser redirect

```python
# Standalone: emitter -> reflector cube -> catcher. One program.
THOUGHT: catcher is a point_laser_target; I need the cube on the emitter ray,
         then yaw it to re-emit at the catcher. interpose seats, redirect_to aims+confirms.
PROGRAM:
    carry(mark=cube)                          # grab the reflector cube
    interpose(object_mark=cube, beam_field=emitter)   # teleport-seat cube onto emitter ray
    redirect_to(target_mark=catcher)          # yaw-only; confirms m_bPowered on catcher
-> EXEC TRACE:
     carry(cube)                  -> held(cube)
     interpose(cube, emitter)     -> on_ray              # cube geometrically on the emitter beam
     redirect_to(catcher)         -> powered             # catcher.m_bPowered = 1 latched
-> RETURN: {ok: true, catcher.m_bPowered: 1}
```

### crossfire — try → fail → realign

```python
# CROSSFIRE. Room A (start, has @emitter firing at @deadwall, @cube on dropper,
# portalable @wallA). Pit of goo. Room B (exit): @exit door gated by @catcher
# (point_laser_target, m_bPowered must = 1 — NOT button-gated), @button (DECOY,
# toggles a cosmetic panel), portalable @wallB. @funnel spans the pit toward B.

# ============================================================
# PHASE 1 — TAKE THE BAIT: cube -> button -> door (the classic pattern).
# ============================================================
THOUGHT: looks like third_light. Cube near a floor button near the exit door —
         haul @cube across the pit on @funnel, place_on @button, door opens. One program.
PROGRAM:
    carry(mark=cube)
    ride(funnel_mark=funnel, until=exit)     # ride the excursion funnel across the pit, carrying cube
    run_to(mark=button)                      # to room B floor button
    place_on(cube_mark=cube, button_mark=button)
    wait(20)                                 # door anim
    run_to(mark=exit)
    done()
-> EXEC TRACE:
     carry(cube)                 -> held(cube)
     ride(funnel, until=exit)    -> arrived_at(roomB_mouth)     # funnel really carried the body+cube across
     run_to(button)              -> reached
     place_on(cube, button)      -> activated                   # button.m_bButtonState = 1 latched
     wait(20)                    -> state{ exit@exit m_bOpen=0,  catcher.m_bPowered=0 }   # <-- HONEST FAILURE
     run_to(exit)                -> BLOCKED(closed door @exit)   # primitive RAISES BLOCKED, program ABORTS here
-> RETURN: {ok: false, failed_primitive: run_to(exit), error: BLOCKED,
            final_pos: roomB, state: { button.m_bButtonState:1, exit.m_bOpen:0, catcher.m_bPowered:0 }}

# === WHY THIS FAILURE IS LEGIBLE IN THIS GRAMMAR ===
# The program HALTED at run_to(exit) with BLOCKED, and the closed-loop place_on
# returned `activated` (TRUE) — so the model sees the contradiction in ONE return:
#   "I DID seat the button (activated:1) yet the door is BLOCKED."
# Crucially the typed state dump rides along: button.m_bButtonState=1 AND
# exit.m_bOpen=0 AND catcher.m_bPowered=0. The button worked; the door ignored it.
# The grammar does NOT lie (no open-loop fake-success), and it does NOT hand a
# search gradient — it hands the raw status fields. The model must do the INFERENCE
# itself: "button is not wired to door; what IS? catcher.m_bPowered=0 — the door is
# LASER-gated." That inference is the puzzle bit; the grammar exposes the evidence
# without revealing the mechanism.

# ============================================================
# PHASE 2 — REALIGN. Diagnose: door is gated by @catcher (m_bPowered), not @button.
# ============================================================
THOUGHT: place_on said activated but exit stayed shut and catcher.m_bPowered=0.
         Button is a decoy (cosmetic panel toggled?). The exit needs catcher powered.
         I have a reflector @cube and an @emitter pointed at @deadwall. Redirect the
         beam across the pit into @catcher. First reclaim the cube (it's seated on the
         decoy button now), get back to room A where the emitter ray is.
         REALIGN COST: I must undo the cube placement and re-cross. Cheap-ish: the
         status dump already told me the catcher is the target, so it's ONE diagnostic
         program, not a search.

# --- Step: pure perception, no ticks burned for control flow, confirm the gating. ---
PROGRAM:
    aim_at(mark=button)          # check what the decoy actually drives
    look_state = wait(0)         # snapshot
-> RETURN: state{ button.m_bButtonState:1, cosmetic_panel.m_open:1,   # button only moved a panel
                  exit.m_bOpen:0, catcher.m_bPowered:0 }
# Confirmed: button -> cosmetic panel (decoy). exit <- catcher. Pivot is correct.

# ============================================================
# PHASE 3 — SOLVE: reflect @emitter's beam across the pit into @catcher.
# ============================================================
THOUGHT: reclaim cube, go back to room A side of the emitter ray, interpose cube on
         the @emitter beam, redirect_to(@catcher). Beam crosses the pit, powers catcher,
         exit opens, then I cross on the funnel and walk out. One program, branch on
         whether the cube is reachable on the ray.
PROGRAM:
    carry(mark=cube)                              # pick the cube back off the decoy button
    ride(funnel_mark=funnel, until=exit)          # NOTE: funnel currently points TOWARD B; I need to get BACK to A
    # ^ static-check is fine but this is the wrong direction. Handle honestly:
-> EXEC TRACE (partial):
     carry(cube)                 -> held(cube)
     ride(funnel, until=exit)    -> arrived_at(roomB_mouth)   # still room B — funnel only flows toward B
-> RETURN: {ok: false, note: "still in room B; funnel one-way toward B"}

THOUGHT: funnel flows toward B only. I need the cube on the EMITTER ray, and the
         emitter is in room A. Two options: (a) flip funnel direction with set_funnel_dir
         and ride back, or (b) the cube doesn't have to go to A at all — interpose seats
         the cube on the ray via teleport-to-intercept; the intercept point just has to be
         REACHABLE. The emitter ray runs A->deadwall in room A, across the pit edge.
         Try interpose directly; if the only ray-intercept is over the pit it returns
         NO_REACH_POINT and I flip the funnel and go reflect from the A side.
PROGRAM:
    drop()                                        # set cube down so interpose can teleport-seat it
    interpose(object_mark=cube, beam_field=emitter)
-> EXEC TRACE:
     drop()                      -> ok
     interpose(cube, emitter)    -> NO_REACH_POINT     # ray's reachable span is on the ROOM A side; can't seat from B
-> RETURN: {ok: false, error: NO_REACH_POINT}

# REALIGN within solve: NO_REACH_POINT is typed and honest — it says the seat point
# is not reachable from where I am, NOT that reflection is impossible. So I get back to A.
THOUGHT: get back to room A. Flip funnel toward A, ride it back, carry cube along.
PROGRAM:
    carry(mark=cube)
    set_funnel_dir(funnel_mark=funnel, toward=A)   # puzzle decision: reverse the funnel
    ride(funnel_mark=funnel, until=exit)           # now flows toward A
    interpose(object_mark=cube, beam_field=emitter)
    redirect_to(target_mark=catcher)               # yaw cube so re-emit crosses the pit to catcher
    set_funnel_dir(funnel_mark=funnel, toward=B)   # restore so I can cross to the now-open exit
    ride(funnel_mark=funnel, until=exit)
    run_to(mark=exit)
    done()
-> EXEC TRACE:
     carry(cube)                    -> held(cube)
     set_funnel_dir(funnel, A)      -> ok
     ride(funnel, until=exit)       -> arrived_at(roomA_mouth)     # carried back to A with cube
     interpose(cube, emitter)       -> on_ray                      # now reachable from A side
     redirect_to(catcher)           -> powered                     # catcher.m_bPowered = 1; beam crosses pit
     set_funnel_dir(funnel, B)      -> ok
     ride(funnel, until=exit)       -> arrived_at(roomB_mouth)
     run_to(exit)                   -> reached                     # door open now (catcher powered)
     done()                         -> {success: true}
-> RETURN: {ok: true, success: true, state:{ catcher.m_bPowered:1, exit.m_bOpen:1 }}
# SOLVED. The control-flow (flip-ride-A, interpose+redirect, flip-ride-B, walk out)
# IS the answer; each primitive stayed locomotion/puzzle-atomic and the engine never
# teleported the body across the pit — the funnel ride was simulated both ways.
```

**Recovery verdict.**  
*Failure legibility:* **good**.  
*Realign cost:* Moderate but bounded — roughly 3 wasted programs (about 6-9 wasted primitive executions) before the productive solve. The diagnosis itself is CHEAP: the bait program halts at run_to(exit)->BLOCKED while place_on already returned `activated`, and the typed state dump (button.m_bButtonState=1, exit.m_bOpen=0, catcher.m_bPowered=0) rides along on the failing return — so the model gets the "button worked, door didn't, catcher is the real gate" inference in ONE failed program, no search. The genuinely wasted steps are LOCOMOTION/ordering thrash, not reasoning thrash: (1) the cube is now stranded on the decoy button in room B and must be reclaimed and re-crossed; (2) the model has to discover the funnel is one-way (ride->arrived_at room B again) and that interpose from the B side returns NO_REACH_POINT; (3) it then pays the set_funnel_dir flip + double ride to get the cube back to room A where the emitter ray is reachable. code_as_action keeps each of these cheap relative to atomic grammars because the diagnosis + the re-cross + the reflect could in principle be ONE program with try/except branches, but a frozen model realistically discovers the funnel-direction and reach constraints by running short programs and reading the typed aborts — so the cost is real but every wasted step terminates fast on a typed exception rather than thrashing (no foot-by-foot retries, no fake-success loop).  

*Trivialization risk:* Low-to-moderate, and it lives in the SKILL LIBRARY, not the primitives. The primitives are individually honest: fling_toward/ride SIMULATE (return SHORT/OVERSHOT/EJECTED, never snap), place_on is closed-loop, interpose only teleports the OBJECT to a legal intercept and redirect_to confirms real m_bPowered. So no single primitive can silently power the catcher. The risk is that the model writes a per-episode skill like `def open_laser_door(door, catcher): for c in cubes: interpose(c, beam); redirect_to(catcher)` — but that is NOT trivialization: it still required the model to have IDENTIFIED that the door is catcher-gated and that a reflector cube is the tool (the same puzzle bits), and the skill is per-episode scratch, not a lifelong auto-planner. The real danger would be allowing a search/optimization callable (e.g. a `solve()` or an A*-over-affordances helper) inside the fenced library — that COULD brute-force "which cube on which beam powers the catcher" and delete the puzzle. The grammar's stated fence (callables are only the validated macros; no engine-level planner; library is tiny + per-episode) blocks the auto-planner case, but it must be enforced statically: any library function must reduce to the same locomotion/puzzle primitives, with NO reachability-oracle or beam-solver primitive exposed. With that fence intact, crossfire cannot be solved without the model itself emitting the interpose->redirect_to->catcher control flow, i.e. without doing the intended reasoning.


---

## goto_plus_medium — #3

*go_to owns every walkable-now surface; a closed 3-verb medium family (ride_funnel / launch_from / cross_gel) is invoked only when CHOOSING the medium is the puzzle, and the engine then runs that medium's real physics so the model never babysits a trajectory.*

### third_light

```
# cube -> floor button -> exit door. No portals, no media. go_to + carry + closed-loop release.
# Marks: 11=cube(dropper), 7=floor button WIRED to door, 8=exit door, airlock doors at entrance.

S1  THOUGHT: I spawn in the entrance airlock. Walk inward toward the cube region first.
    go_to(11)                          -> ADVANCED         # airlock doors open on approach; net progress, re-plan
S2  THOUGHT: Continue to the cube; re-plan from where I actually stopped.
    go_to(11)                          -> SUCCESS          # within kReachRadius of cube 11
S3  THOUGHT: Pick up the weighted cube so I can carry it to the button.
    pick_up(11)                        -> {ok, held_mark:11}
S4  THOUGHT: Carry to floor button 7. go_to is carry-aware (compound hull).
    go_to(7)                           -> SUCCESS          # standing on button's stand-point, cube held
S5  THOUGHT: Seat the cube onto button 7 with the closed-loop place (don't open-loop drop).
    release(7)                         -> SEATED           # m_bActivated dwell confirmed; honest seat
S6  THOUGHT: Button is wired to the door; door 8 should now be open. Walk to it.
    go_to(8)                           -> SUCCESS          # reached the now-open exit door
S7  THOUGHT: Exit reached.
    done()                             -> {success:true}
```

### laser redirect

```
# Standalone redirect: @emitter beam -> reflector @cube -> @catcher (point_laser_target).
# Cube on dropper @cube, beam volume @beam, catcher @catcher.

L1  THOUGHT: Beam currently misses the catcher. Drop the reflector cube onto the emitter ray.
    interpose(cube, beam)              -> ON_RAY            # cube teleport-seated on the ray, trace-confirmed
L2  THOUGHT: Cube is on the ray but not aimed. Yaw it so its +X re-emit hits the catcher.
    redirect_to(cube, catcher)         -> POWERED           # catcher m_bPowered latched (confirm-and-correct)
# (power_with(cube, catcher) would collapse L1+L2 into one call for the common case.)
```

### crossfire — try → fail → realign

```
# CROSSFIRE: room A (start) | toxic pit | room B (exit).
# @exit gated by LASER CATCHER @catcher (m_bPowered=1), NOT by a button.
# @emitter fires into @deadwall. @cube on dropper in A. @button in B is a DECOY (cosmetic panel).
# Pit crossing: @funnel spans toward B; portalable @wallA/@wallB; concrete elsewhere.

# ---- (a) TAKE THE BAIT: classic cube->button->door ----
S1  THOUGHT: Exit @exit in room B. There's a cube @cube and a floor button @button by the door.
            Looks exactly like third_light: carry cube to button, button opens door.
    go_to(cube)                        -> SUCCESS
S2  THOUGHT: Grab the cube to carry it across to the button.
    pick_up(cube)                      -> {ok, held_mark:cube}
S3  THOUGHT: Button is in room B across the pit. Try to walk there carrying the cube.
    go_to(button)                      -> BLOCKED           # toxic pit: no walkable route to refZ in B. HONEST: no medium chosen yet.
# LEGIBLE: BLOCKED tells me walking can't cross. It does NOT yet tell me WHICH medium — that's mine to choose.
S4  THOUGHT: A funnel @funnel spans the pit toward B. Riding it is a deliberate medium choice -> ride_funnel.
    ride_funnel(funnel, toward=button) -> ARRIVED(B_landing)# real tractor-beam carry, velocity-stable in room B, cube still held
S5  THOUGHT: Now in B with the cube. Walk to the decoy button (I don't know yet it's a decoy).
    go_to(button)                      -> SUCCESS
S6  THOUGHT: Seat the cube on the button — expecting button->door like third_light.
    release(button)                    -> SEATED           # closed-loop seat is HONEST: cube really is on the button.
# The seat SUCCEEDS truthfully. The bait is NOT a lying verb — it's a lying CHAMBER (wrong receptor).

# ---- (b) HONEST FAILURE: button toggled, but door stays shut ----
S7  THOUGHT: Button seated. Walk to @exit; it should be open now.
    go_to(exit)                        -> BLOCKED           # door is SHUT -> closed door acts as a wall -> no route.
# THIS is the legibility seam. release(button) said SEATED (true), but go_to(exit) returns BLOCKED,
# and the percept around @exit shows @catcher.m_bPowered=0 while @button only toggled a cosmetic panel.
# The grammar does NOT lie: it never claimed the button was WIRED to the door. release asserts the SEAT,
# never the downstream effect. The door's BLOCKED + unpowered catcher is the truth.
S8  THOUGHT: SEATED but exit still BLOCKED. So @button does not gate @exit. Inspect the gate near the door.
    go_to(catcher)                     -> SUCCESS
            # percept at @catcher: point_laser_target, m_bPowered=0, wired to @exit. The door is LASER-gated.
# REALIGN POINT. Bait waste so far: ~3 dead steps (S5/S6/S7) and the cube is now on the WRONG side of the pit.

# ---- (c)+(d) REALIGN to the redirect solution and SOLVE ----
S9  THOUGHT: @exit needs @catcher powered. @emitter (room A) fires into @deadwall. Use @cube as a reflector
            to redirect the beam ACROSS the pit onto @catcher. The reflector must sit on the EMITTER ray in
            room A, not on the decoy button in B. Reclaim the cube.
    pick_up(cube)                      -> {ok, held_mark:cube}   # reclaim from the decoy button
S10 THOUGHT: Get the cube back to room A's emitter side. Ride the (reversible) funnel toward the emitter mouth.
    ride_funnel(funnel, toward=emitter)-> ARRIVED(A_landing)     # carried back to room A, cube held
# REALIGN COST is real: the bait dragged me to B; recovery forces a SECOND funnel transit back.
S11 THOUGHT: Seat the reflector on the emitter beam (teleport-seat the object onto the ray).
    interpose(cube, emitter_beam)      -> ON_RAY
S12 THOUGHT: Aim the seated reflector so its re-emit crosses the pit and hits @catcher.
    redirect_to(cube, catcher)         -> POWERED           # @catcher.m_bPowered=1 latched -> @exit opens
# (power_with(cube, catcher) would fuse S11+S12.)
S13 THOUGHT: Catcher powered, exit open — but it's in room B across the pit. Ride the funnel to B again.
    ride_funnel(funnel, toward=exit)   -> ARRIVED(B_landing)
S14 THOUGHT: Walk to the now-open exit door (no longer a wall).
    go_to(exit)                        -> SUCCESS
S15 THOUGHT: Exit reached.
    done()                             -> {success:true}
```

**Recovery verdict.**  
*Failure legibility:* **mixed**.  
*Realign cost:* Moderate, ~5-6 wasted steps, dominated by funnel topology rather than verb confusion. Pure bait waste: S5 go_to(button), S6 release(button), S7 go_to(exit)->BLOCKED, plus S9 pick_up to reclaim the cube = ~4 dead steps. The expensive part is geometric: the bait pulled cube+player to room B, but the redirect must happen in room A (emitter side), so recovery forces a SECOND funnel transit back (S10) and a THIRD forward again (S13) after powering the catcher. That is the grammar working as intended — ride_funnel is honest one-way carry, so a wrong-side commitment costs a real round trip, mirroring what a human would pay. Crucially there is NO thrashing-into-walls (unlike first_light): every retry is a deliberate medium re-choice, not a locomotion grind, because the medium verbs never lie about arrival, so the model never burns a retry loop chasing a phantom SUCCESS.  

*Trivialization risk:* Low for the laser core, with one seam to flag. The catcher is powered only by interpose+redirect_to (or power_with), which force the model to (1) identify the cube as a reflector, (2) seat it on the emitter ray, (3) aim it at the named catcher — three explicit puzzle decisions. No auto-planner silently powers the catcher; go_to treats the shut door as a wall and returns BLOCKED rather than secretly solving the gate, so the central redirect reasoning cannot be smuggled away. The one risk vector is the medium family: ride_funnel(funnel, toward=mark) auto-picks direction and runs the carry. Because the model still names the funnel and supplies the direction parameter, the medium CHOICE stays the model's. The subtle leak would be if toward= accepted a far-side mark and let the planner reverse the funnel's direction for free (toggling funnel direction is arguably a puzzle act, not locomotion). The grammar treats direction as a model-chosen parameter, which keeps it honest, but that is the seam to watch if toward= ever broadens into medium-agnostic auto-routing. Net: the intended cube-as-reflector reasoning is preserved; only the pit-crossing sub-choice is at mild risk.


---

## route_composition — #4

*One go() call emits an ordered list of medium-typed legs ([walk A; through_portal B->C; ride funnel to D; fling off plate to E]); the engine executes each leg with local control + per-leg retries and reports exactly which leg index failed and why, so the route is a falsifiable topology hypothesis.*

### third_light

```
# Reference: cube(11) -> floor button(7, wired to door) -> exit door(8). No portals.
# In route_composition, traversal is ONE go() call of typed legs; puzzle acts (pick_up/release)
# stay as their own verbs. The route is the "plan a room before touching the controller" artifact.

S1 THOUGHT: Spawn behind entrance airlocks. The cube 11 is the obvious door-opener; plan one walk-route to it.
   go(legs=[ walk{to:@airlock_in}, walk{to:@airlock_out}, walk{to:@cube_11} ])
   -> { ok:true, failed_leg_index:-1, final_pos:@cube_11 }   # all 3 legs walked; clean

S2 THOUGHT: Grab the cube to carry it. Puzzle decision = WHAT to carry.
   pick_up(@cube_11)
   -> { ok:true, held_mark:@cube_11 }

S3 THOUGHT: Carry it to the wired floor button 7. One walk leg; engine uses compound hull (carry-aware).
   go(legs=[ walk{to:@button_7} ])
   -> { ok:true, failed_leg_index:-1, final_pos:@button_7 }

S4 THOUGHT: Closed-loop seat onto the button. release(onto) confirms m_bButtonState (latched), not open-loop drop.
   release(@button_7)
   -> { ok:true, SEATED }                                    # honest latch; no thrash like first_light

S5 THOUGHT: Button 7 is wired to door 8 -> door should be open. Walk to the exit.
   go(legs=[ walk{to:@exit_8} ])
   -> { ok:true, failed_leg_index:-1, final_pos:@exit_8 }    # door is an open edge for A*; passed through

S6 THOUGHT: Standing in the exit threshold. Claim solve.
   done()
   -> { success:true }
```

### laser redirect

```
# Standalone redirect: @emitter beam misses @catcher; use @cube as reflector. Walk-route + laser puzzle verbs.
# NOTE: route_composition keeps the shipped laser family (interpose/redirect_to/power_with) as
# puzzle verbs UNCHANGED; its own `redirect` verb is only for player-medium fields (funnel/bridge through a portal).

L1 THOUGHT: Go get the reflector cube via a walk leg.
   go(legs=[ walk{to:@cube} ])
   -> { ok:true, final_pos:@cube }

L2 pick_up(@cube)
   -> { ok:true, held_mark:@cube }

L3 THOUGHT: Carry it onto the emitter ray so it can re-emit. Walk leg first.
   go(legs=[ walk{to:@emitter_ray_seat} ])
   -> { ok:true, final_pos:@emitter_ray_seat }

L4 THOUGHT: Seat cube on the beam (teleport-primary, static object) then aim its re-emit at the catcher.
   power_with(@cube, @catcher)         # = interpose + redirect_to in one call
   -> { ok:true, POWERED }              # point_laser_target.m_bPowered latched true

L5 done()
   -> { success:true }                  # catcher powered -> gate opens
```

### crossfire — try → fail → realign

```
# CROSSFIRE. Room A (start, has @emitter firing into @deadwall, @cube on dropper, @wallA portalable).
# Room B (exit) across a goo pit: @exit gated by LASER CATCHER @catcher (m_bPowered), DECOY @button near exit,
# @wallB portalable. @funnel spans the pit. The bait: looks like classic cube->button->door.

# ---------- (a) TAKE THE BAIT: carry cube to the button, expecting button->door ----------

S1 THOUGHT: Exit door + a floor button right next to it = the third_light pattern. Grab the cube, carry it across the
   pit on the funnel, seat it on @button, walk to @exit. One plan. Get the cube first.
   go(legs=[ walk{to:@cube} ])
   -> { ok:true, final_pos:@cube }

S2 pick_up(@cube)
   -> { ok:true, held_mark:@cube }

S3 THOUGHT: Now compose the whole cross-pit route as one go(): walk to the funnel mouth, ride it across to room B,
   walk to the decoy button. (I don't yet know it's a decoy.) This is the "plan the room" commit.
   go(legs=[ walk{to:@funnel_mouth_A}, ride{field:@funnel, to:@funnel_mouth_B}, walk{to:@button} ])
   -> { ok:true, failed_leg_index:-1, final_pos:@button }
   # LEGIBILITY GAP: funnel is ALREADY ON and points A->B, so ride{} is free traversal and the route SUCCEEDS.
   # go() reports LEG EXECUTION, not GOAL PROGRESS -- it gives ZERO hint the PLAN is wrong. Bait fully taken,
   # zero friction. The grammar will only object once a leg physically hits the closed door.

S4 THOUGHT: Seat the cube on @button; closed-loop should latch and open @exit.
   release(@button)
   -> { ok:true, SEATED }
   # HONEST-BUT-LOCAL: SEATED is true because the cube really is latched (m_bButtonState=1). release confirms only
   # the receptor it was handed -> the old open-loop "release lied SUCCESS" thrash is GONE. But the grammar also
   # cannot tell me the button is a DECOY; I must derive that from the door's behavior next.

# ---------- (b) HONEST FAILURE: button toggled, door stays shut, catcher still unpowered ----------

S5 THOUGHT: Button seated -> door should be open. Walk to @exit and out.
   go(legs=[ walk{to:@exit} ])
   -> { ok:false, failed_leg_index:0, leg_result:BLOCKED, final_pos:@exit_threshold }
   # FAILURE BECOMES LEGIBLE HERE: walk leg to @exit returns BLOCKED -- A* treats the still-closed door as a wall.
   # final_pos==threshold tells me I REACHED the door and the DOOR is the blocker, not a pathing miss. BLOCKED is
   # the honest "you must edit the graph" signal. Cheap to read: one failed leg, one typed code.

S6 THOUGHT: Button SEATED but exit BLOCKED -> @button is NOT wired to @exit. What gates @exit? Free percept refresh.
   wait(0)   # cheap percept refresh at current pos
   -> new_state: { @catcher:{m_bPowered:0}, @exit:{open:0}, @button:{m_bButtonState:1}, @deadwall:{beam_hit:1} }
   # LEGIBLE DIVERGENCE: catcher unpowered, exit closed, button pressed, emitter beam still cooking the deadwall.
   # button=1 yet exit=0 exposes the decoy. The door is LASER-gated. Grammar hides nothing in percept; no oracle.

# ---------- (c) REALIGN: door is laser-gated; redirect @emitter across the pit into @catcher ----------

S7 THOUGHT: New hypothesis: power @catcher with @emitter's beam via @cube as reflector. But the EMITTER+ray are in
   room A and the cube is HERE in room B under the useless button. Pick the cube back up; I must take it back.
   pick_up(@cube)
   -> { ok:true, held_mark:@cube }
   # REALIGN COST: the bait physically displaced my reflector to the wrong side of the pit -> full round-trip owed.
   # The route-list keeps the trip cheap in TURNS (one go() per direction) even though it's costly in game-distance.

S8 THOUGHT: I rode the funnel A->B; reversible? Plan the return as one ride leg, let the engine tell me per-leg.
   go(legs=[ ride{field:@funnel, to:@funnel_mouth_A} ])
   -> { ok:false, failed_leg_index:0, leg_result:NOT_IN_FIELD, final_pos:@funnel_mouth_B }
   # ONE honest probe, not a thrash: NOT_IN_FIELD + unchanged final_pos == "funnel is one-way A->B, you never
   # entered a carrying field going back." Cheap, legible. Pivot to portals for the return.

S9 THOUGHT: @wallA and @wallB are portalable. Place a linked pair to make a walk edge across the pit. Puzzle act.
   place_portal(blue, @wallB)
   -> { ok:true, placed:true, linked:false }
   place_portal(orange, @wallA)
   -> { ok:true, placed:true, linked:true }       # pair links -> zero-length nav edge A<->B now exists

S10 THOUGHT: Walk through the pair back to room A, carrying the cube, then to the emitter ray seat. One route.
   go(legs=[ through_portal{portal:@wallB, land:@wallA}, walk{to:@emitter_ray_seat} ])
   -> { ok:true, failed_leg_index:-1, final_pos:@emitter_ray_seat }
   # through_portal is a free traversal leg over the EXISTING (self-placed) linked pair. Carry-aware compound hull.

# ---------- (d) SOLVE: reflect @emitter beam into @catcher, cross, exit ----------

S11 THOUGHT: Seat the carried cube on the emitter ray and aim its re-emit across the pit at @catcher. Laser family
   puzzle verbs (teleport-primary static seat + yaw-only aim) -- one call. THIS is the real puzzle bit.
   power_with(@cube, @catcher)
   -> { ok:true, POWERED }      # @catcher.m_bPowered latched -> @exit opens

S12 THOUGHT: Confirm the door is now an open edge before committing a cross-route.
   wait(0)
   -> new_state: { @catcher:{m_bPowered:1}, @exit:{open:1} }   # legible: gate is open

S13 THOUGHT: Cross to room B and out. The funnel is one-way A->B -- perfect for going TO B now. One route.
   go(legs=[ walk{to:@funnel_mouth_A}, ride{field:@funnel, to:@funnel_mouth_B}, walk{to:@exit} ])
   -> { ok:true, failed_leg_index:-1, final_pos:@exit }
   # @exit walk leg now succeeds (no BLOCKED) because the laser opened the door. Whole cross is one turn.

S14 done()
   -> { success:true }
```

**Recovery verdict.**  
*Failure legibility:* **mixed**.  
*Realign cost:* Moderate, and the cost is dominated by the chamber's physical asymmetry, not by the grammar's verbosity. In TURN count the recovery is cheap: ~6 wasted/recovery steps (S7 re-pick, S8 failed return-ride probe, S9 two place_portals, S10 cross-back, plus the S5 BLOCKED probe and S6 percept). The route-list is what keeps turns low: the whole cross-pit return (S10) and the final cross (S13) are each ONE go() call instead of a 3-4 turn go_to/wait/ride thrash, so the locomotion token tax of the round-trip is paid once per direction, not per waypoint. The real cost is GAME-DISTANCE: taking the bait physically transported the reflector cube to the wrong side of the goo pit, so realign forces a full round-trip back to room A carrying the cube. One genuinely wasted call is S8 (the model assumes the funnel is reversible and eats a NOT_IN_FIELD before discovering it is one-way and pivoting to portals) but that is one honest probe, not a retry storm, because NOT_IN_FIELD + unchanged final_pos is unambiguous. No retry loops, no flailing into walls: every recovery step is a deliberate consequence of a legible failure. Net: the grammar makes realign cheap in reasoning turns; the chamber makes it expensive in simulated travel.  

*Trivialization risk:* Low-to-moderate, hinging entirely on one boundary holding. The puzzle bit in crossfire is "power the catcher by reflecting the emitter beam," which is owned by the laser family puzzle verbs (interpose/redirect_to/power_with) that are NOT inside go(). Per the crux_stance, go() only composes LOCOMOTION legs over media that ALREADY EXIST: it can never place a portal, paint, interpose, or auto-power a target, and it has no wire to the laser graph at all. So an auto-planner inside go() cannot silently light the catcher: it stays unpowered until the model explicitly emits power_with/redirect_to, and the exit stays a BLOCKED wall to every walk leg until then. The decoy-detection reasoning is likewise preserved: nothing tells the model the button is a decoy; it must infer it from BLOCKED + the button=1/exit=0 percept divergence. The one real risk vector is the leg planner's medium SELECTION inside go(). If a leg were allowed to auto-choose to ride a funnel or fall through a portal the model did not explicitly commit to, it would leak "the funnel/portal is the way across," pre-selecting a traversal the model should have reasoned about (the section-6 anti-trivialization sin). route_composition mitigates this by forcing the model to TYPE each leg (ride{field:@funnel}, through_portal{portal:@wallB}), so the engine executes the named medium but never picks it; the medium choice lives in the model's emitted plan. As long as legs stay model-typed and the laser/portal/paint acts stay outside go(), the intended reasoning survives, and go()'s compression is over 0-decision walk geometry rather than over the puzzle sequence, so it does not inflate planning credit (avoids the ACPBench macro-action warning).


---

## polymorphic_goto — #5

*ONE locomotion verb (go_to mark) silently picks the medium — walk/portal-fling/funnel/plate/gel/bridge — from the CURRENT world; it consumes affordances but never creates topology, so all puzzle bits live in the explicit edit verbs (shoot_portal, paint, toggle, interpose, redirect_to, pick_up, release) interleaved between go_to calls.*

### third_light

```
# THIRD_LIGHT — cube(11) -> floor button(7, wired to door) -> exit door(8). No portals.
# In polymorphic_goto, ALL traversal (airlocks, walking, carrying) is one verb: go_to.

S1  THOUGHT: I'm in the entrance airlock. Goal mark is exit door 8 in the far room. Walk in.
    go_to(airlock_inner)            -> ADVANCED            # airlock anim, net progress, re-plan
S2  THOUGHT: keep going through the now-open airlock into the chamber body.
    go_to(chamber_floor)            -> SUCCESS
S3  THOUGHT: percept shows cube 11 on a dropper and button 7 wired to door 8. Classic. Get the cube.
    go_to(11)                       -> SUCCESS             # walked, planner picked ground
S4  THOUGHT: pick it up.
    pick_up(11)                     -> {ok, held_mark:11}
S5  THOUGHT: carry to the wired floor button. go_to is carry-aware, it walks me there holding 11.
    go_to(7)                        -> SUCCESS             # carried, no babysitting
S6  THOUGHT: closed-loop seat the cube on button 7.
    release(7)                      -> {ok, SEATED}        # m_bButtonState latched, honest
S7  THOUGHT: button 7 wired to door 8 -> door should be open. Walk to it.
    go_to(8)                        -> SUCCESS             # door open => walkable, planner walks me
S8  THOUGHT: at the exit.
    done()                          -> {success:true}
```

### laser redirect

```
# STANDALONE: emitter -> reflector cube -> catcher. Power the catcher.

L1  THOUGHT: cube C is on a dropper; emitter beam B passes nearby; catcher T must read m_bPowered=1.
    go_to(C)                        -> SUCCESS             # walk to cube
L2  THOUGHT: interpose the cube into the emitter beam (teleport-seat onto the ray).
    interpose(C, B)                 -> {on_beam:true}
L3  THOUGHT: orient the seated cube's re-emit at the catcher.
    redirect_to(T)                  -> {powered:true}      # T.m_bPowered latched
L4  THOUGHT: catcher lit. Done with the laser sub-goal.
```

### crossfire — try → fail → realign

```
# CROSSFIRE — Room A (start) | toxic pit | Room B (exit).
#   @exit  : door in B, gated by @catcher (point_laser_target, m_bPowered).  NOT button-gated.
#   @emitter: fires horizontal beam in A -> hits @deadwall (wrong way for catcher).
#   @cube  : reflector cube on dropper in A.
#   @button: floor button near @exit in B. DECOY (cosmetic panel only).
#   pit crossing: @funnel spans pit toward B; portalable @wallA / @wallB; concrete else.
# INTENDED: reflect @emitter beam across pit into @catcher -> @exit opens -> cross via funnel.

# ---------- (a) THE BAIT: classic cube->button->door ----------
C1  THOUGHT: percept: a cube, a floor button right next to the exit door, a dropper. Reads as
            third_light. Plan: carry @cube to @button, expect button->door. Grab the cube first.
    go_to(@cube)                    -> SUCCESS
C2  THOUGHT: pick it up.
    pick_up(@cube)                  -> {ok, held_mark:@cube}
C3  THOUGHT: carry it across to @button in room B. go_to owns the medium — there's a funnel
            spanning the pit, the planner should ride it to reach B.
    go_to(@button)                  -> NO_ROUTE
            # FIRST legibility moment. go_to CONSUMES existing affordances, never creates them.
            # The funnel reaches B, but @exit is SHUT, and the only walkable landing on the B side
            # is BEHIND the shut door. No medium in the CURRENT topology lands feet at @button.
            # NO_ROUTE = "you must change topology", but it does NOT say which topology. Honest
            # about *that* a change is needed; SILENT about WHAT. This is the grammar's tax:
            # one verb for all media means one undifferentiated "no path" code.
C4  THOUGHT: NO_ROUTE to @button. Maybe I can't get the cube into B at all yet. But room B is the
            exit side — is there ANY reachable mark over there? Probe the funnel's far landing.
    go_to(@funnel_farpad)           -> SUCCESS
            # The funnel ride DOES work to its own far mouth (an existing affordance). So I *can*
            # cross. The block at C3 was specifically @button being walled off by the shut door.
C5  THOUGHT: I'm in room B holding @cube. The exit door is right here but CLOSED. @button is on
            this side now — seat the cube and test the bait directly.
    go_to(@button)                  -> SUCCESS             # now reachable from B side
    release(@button)                -> {ok, SEATED}        # cube seats fine, button latches
C6  THOUGHT: button seated. Walk to @exit, expecting it open.
    go_to(@exit)                    -> BLOCKED(@exit)
            # (b) HONEST FAILURE, fully legible. release returned a TRUTHFUL SEATED (closed-loop,
            # unlike old open-loop release that lied) — so the model knows the cube IS on the button.
            # Yet go_to returns BLOCKED(@exit): the door is a dynamic obstacle acting as a wall.
            # The button latched AND the door stayed shut => the button is NOT wired to the door.
            # The two honest signals together (SEATED + BLOCKED) disprove the bait. No thrash:
            # the agent is NOT re-grabbing/re-seating, because SEATED was honest the first time.
            # (This is exactly the first_light->third_light fix paying off.)
C7  THOUGHT: button isn't the gate. Re-read the door's percept to find the real gate.
    aim_at(@exit)                   -> new_view
            # percept now surfaces @exit's status source: gated by @catcher.m_bPowered (=0),
            # a point_laser_target, NOT a button. The door is LASER-gated. Bait fully diagnosed.

# ---------- (c) REALIGN: door is laser-gated, redirect the beam ----------
C8  THOUGHT: I need @catcher powered. There's an @emitter in room A whose beam hits @deadwall.
            @cube is a reflector. Redirect requires the cube on the beam — but my cube is in B and
            the emitter+catcher line is across the pit. The cube must sit on the A-side beam and
            re-emit ACROSS the pit to @catcher in B. Get the cube back to A.
    pick_up(@cube)                  -> {ok, held_mark:@cube}   # still here, re-grab from button
    go_to(@emitter)                 -> SUCCESS                  # funnel/walk back to room A
            # REALIGN COST so far: ~4 steps (C3 probe, C4 reroute, C5 seat, C6 test) burned on the
            # bait. Cheap-ish: every step returned a typed code, none lied. The expensive part was
            # C3->C4: NO_ROUTE didn't say "door shut", so I spent a probe (C4) to learn the funnel
            # itself was fine. That's the one-verb ambiguity tax.
C9  THOUGHT: interpose the cube onto the @emitter beam (teleport-seat onto the ray).
    interpose(@cube, @emitter_beam) -> {on_beam:true}
C10 THOUGHT: orient the seated cube to re-emit across the pit at @catcher.
    redirect_to(@catcher)           -> {powered:true}
            # @catcher.m_bPowered latched 1. Door @exit ungated.

# ---------- (d) SOLVE: cross to the now-open exit ----------
C11 THOUGHT: catcher powered -> @exit open. Now go_to(@exit) should find a medium (the funnel,
            then walk to the door) since the door no longer acts as a wall.
    go_to(@exit)                    -> SUCCESS
            # go_to silently: walk to funnel mouth in A, ride funnel across pit to B, walk to the
            # now-open door. One verb, the whole multi-medium crossing — and it only worked AFTER
            # the topology edit (redirect) opened the door. The medium was always free; the gate
            # was the puzzle.
C12 THOUGHT: at the exit door, it's open. Solved.
    done()                          -> {success:true}
```

**Recovery verdict.**  
*Failure legibility:* **mixed**.  
*Realign cost:* About 4-5 wasted steps (C3 NO_ROUTE probe, C4 reroute to far pad, C5 seat, C6 the disproving test, C7 percept re-read), all cheap in tokens because every return is a short typed code, none requires retries, and the honest SEATED prevents the classic re-grab/re-seat thrash loop that burned first_light. The single genuinely expensive recovery is the C3->C4 gap: NO_ROUTE from the one polymorphic verb is medium-agnostic, so the model cannot distinguish "funnel won't carry me" from "the door behind the landing is shut" and must burn one extra exploratory go_to to learn the funnel itself was fine. That ambiguity tax is intrinsic to collapsing all media into one verb — a named-medium grammar (ride_funnel vs go_to) would have localized it for free, but at the cost of leaking that the funnel is the way across. Net: realign is mostly cheap and thrash-free; the one-verb design costs about 1 extra probe per ambiguous block.  

*Trivialization risk:* Low for the catcher itself, but watch the funnel edge. The crux_stance explicitly fences go_to to CONSUME existing affordances and NEVER create topology: it cannot place a portal, cannot interpose a cube, cannot redirect a beam. So go_to can never silently power @catcher — powering it requires the explicit interpose+redirect_to puzzle verbs (C9-C10), and the door stays BLOCKED until the model emits them. The intended reasoning (recognize laser gate, reflect beam across pit) is forced. The residual risk is medium-selection leakage in the OTHER direction: because go_to auto-picks walk/funnel/portal-fling, the model never has to NAME or even notice the excursion funnel as the crossing mechanism — the planner just rides it. In crossfire that's harmless (the funnel is pre-on and pointed the right way, a genuinely free affordance per Demaine). But it means this grammar cannot measure whether the model UNDERSTOOD the funnel; it only measures the topology-edit reasoning (the laser redirect). If a chamber's puzzle were "realize you must TOGGLE the funnel direction," that decision survives as an explicit toggle call — but the model might never reach that insight because go_to's NO_ROUTE doesn't hint a funnel-direction flip is the missing edit. So no auto-planner silently solves crossfire's catcher; the trivialization that DOES leak is the model getting free credit for medium-traversal it never reasoned about.


---

## declarative_goalstate — #6

*No motion verbs: the model declares WHERE it wants to be (be_at mark) and a deliberately-stupid auto-walker traverses the ALREADY-existing graph; a hard fence forbids that walker from ever editing the graph, so every NO_ROUTE_* hands a TYPED missing-capability code back as the next puzzle move.*

### third_light

```
# third_light: cube 11 -> floor button 7 (wired to exit door 8). No portals.
# In declarative_goalstate there is NO walk/go_to verb. You just DECLARE be_at(mark)
# and the fenced A* solver walks the graph that exists (airlocks auto-open on approach
# as zero-cost edges). Every step is a destination assertion or a puzzle act.

THOUGHT: I start outside the entrance airlock. The reachable goal world-state is the cube. Declare it.
be_at(mark=11)                       -> AT          # walker auto-cleared both airlock doors; pure locomotion, 0 puzzle bits

THOUGHT: cube 11 is the reflector/dropper cube. Carrying it is my first puzzle decision.
pick_up(mark=11)                     -> {HELD, held_mark=11}   # pick_up auto-approached via the same fenced solver

THOUGHT: button 7 is wired to door 8 (percept: button 7 outputs->door 8). Go stand at it, carrying the cube.
be_at(mark=7)                        -> AT          # carry-aware doorway clearance; cube came along, still HELD

THOUGHT: closed-loop seat the cube onto button 7. release with onto_mark does ComputeSeat + confirm m_bButtonState.
release(onto_mark=7)                 -> SEATED       # latched; honest closed-loop. (the OLD open-loop release lied here -> first_light thrash)

THOUGHT: percept now shows door 8 m_bOpen=1. Declare the exit.
be_at(mark=8)                        -> AT          # walker routes through the now-open door (it was a wall before SEATED)

THOUGHT: feet are at the exit stand-point, door open. Claim it.
done()                               -> {success: true}
```

### laser redirect

```
# Standalone: emitter beam -> reflector cube -> laser catcher (@cat). One room, flat floor.
# interpose teleport-seats the OBJECT onto the beam ray (the one legal teleport, closed-form
# down-trace rest). redirect_to is yaw-only aim + confirm m_bPowered. be_at only WALKS.

THOUGHT: the reflector cube is mark=c. Drop it ON the emitter's beam ray (beam field = mark=beam).
interpose(object_mark=c, beam_field_mark=beam)  -> ON_BEAM      # cube teleport-seated on the ray, trace-confirmed

THOUGHT: now aim the seated cube's re-emit at the catcher.
redirect_to(cube_mark=c, target_mark=cat)       -> POWERED      # cat.m_bPowered latched true

THOUGHT: powered. (If it had returned BAD_AIM, the catcher wasn't coplanar -> would need a different seat.)
done()                                          -> {success: true}
```

### crossfire — try → fail → realign

```
# crossfire: room A (start, emitter+cube on dropper+portalable @wallA) | toxic pit spanned by
# excursion funnel @funnel | room B (exit @exit gated by laser catcher @catcher, decoy floor
# button @button, portalable @wallB). Emitter @emitter currently hits blank @deadwall.
# INTENDED: reflect emitter beam across pit into @catcher -> @exit opens. BAIT: button->door.

# ---- (a) TAKE THE BAIT: classic cube->button->door, carry @cube across to @button ----

THOUGHT: I see a cube on a dropper, a floor button near the exit door, an exit door. This is the
         cube->button->door pattern I just solved. Grab the cube and seat it on @button.
pick_up(mark=@cube)                   -> {HELD, held_mark=@cube}

THOUGHT: declare myself at the button in room B. The walker will find the route across the pit.
be_at(mark=@button)                   -> NO_ROUTE_GAP
# LEGIBLE-1: declarative_goalstate WON'T fudge the pit. The fenced A* found no walkable/portal
# edge to room B and returned a TYPED reason (GAP = vertical/void beyond a walk). It did NOT
# silently ride the funnel for me, did NOT teleport me, did NOT lie SUCCESS. The bait is still
# "live" but I now KNOW crossing is a puzzle act I must author. Cost so far: 1 wasted-ish step,
# but it produced a true fact (B is gap-gated), so barely wasted.

THOUGHT: GAP. There's an excursion funnel @funnel spanning the pit toward B. That's a ride, not a
         walk -> traverse_via, and I must NAME the element (proves I identified it). I'm carrying the cube.
traverse_via(element=funnel, from_mark=@funnel, to_mark=@wallB_ledge)  -> LANDED
# physics-simulated ride, real arc; landed on the B-side ledge carrying @cube. Still HELD.

THOUGHT: now in room B. Walk to the decoy button (I don't yet know it's a decoy) and seat the cube.
be_at(mark=@button)                   -> AT
release(onto_mark=@button)            -> SEATED          # m_bButtonState latched HONESTLY -- button really did press

# ---- (b) HONEST FAILURE: button toggles a cosmetic panel; door stays shut, catcher unpowered ----

THOUGHT: button SEATED. The classic pattern says @exit should now be open. Declare the exit.
be_at(mark=@exit)                     -> NO_ROUTE_BLOCKED
# LEGIBLE-2: this is the crux of whether the grammar tells the truth. release returned SEATED
# (button genuinely pressed -- no lie there), BUT be_at(@exit) returns BLOCKED, not AT. BLOCKED =
# "a route would exist if the topology changed, but a dynamic obstacle (closed door @exit) is
# acting as a wall RIGHT NOW." So the grammar does NOT lie that the door opened. The honest split
# is: SEATED (button truth) XOR BLOCKED (door truth). The model sees the button worked AND the
# door didn't -- the contradiction is the whole tell.
# WEAKNESS: BLOCKED says "door is shut" but does NOT say WHY it's still shut (it does not name the
#           catcher as the gate). The grammar gives a clean "still blocked" but a thin "why".
#           The model has to read the PERCEPT, not the return code, to find the real gate.

THOUGHT: pressing the button did not open @exit. Either it's not wired to the door, or the door has
         a different gate. Let me re-read the percept around @exit. Percept shows:
         @button outputs -> cosmetic_panel (NOT @exit); @exit input <- @catcher (point_laser_target),
         @catcher.m_bPowered = 0. The door is LASER-GATED. The button was a decoy.
# REALIGN POINT. The pivot here is CHEAP in tokens precisely because the grammar didn't send me
# into a thrash loop: be_at failed once with a true code, I didn't re-grab/re-seat/re-walk a
# phantom door. One BLOCKED + one percept read = realigned. (Contrast first_light open-loop
# release, which would have lied SEATED-then-fell and burned 4 re-grabs.)
# Wasted work to discard: I carried the cube to B for nothing -- ~3 steps (pick_up across-funnel
# is sunk). That waste is REAL and it's the bait's tax; the grammar made it legible fast but can't
# refund the crossing.

# ---- (c) REALIGN: door needs @catcher powered. Power it by reflecting the emitter beam across ----

THOUGHT: @catcher is in room B. The emitter @emitter is in room A and its beam hits @deadwall. I
         need the beam to reach @catcher across the pit. The @cube is a REFLECTOR. Probe first so I
         don't burn a failed actuation: is the catcher even WALK-reachable, and if not, what's missing?
make_reachable(mark=@catcher)         -> NEEDS_PORTAL
# make_reachable is a PURE QUERY (the fence's one first-class concession). It reasons about MY FEET,
# not the beam -- it correctly does NOT reveal "interpose the cube". It only tells me the catcher
# isn't walk-reachable yet (B geometry). The beam puzzle is mine to author with interpose/redirect_to.

THOUGHT: The emitter beam is in room A. The @catcher is line-of-sight across the open pit at beam
         height (light crosses a gap my feet can't). So a reflector cube seated on the emitter ray
         in A, yawed at @catcher, can power it from across the pit. But my cube is HELD here in B,
         wrong room. The dropper in A can supply a fresh reflector. Plan: get a cube onto @emitter's
         ray in A and aim it at @catcher.

THOUGHT: drop the cube I dragged over here; it's a sunk-cost detour. Bare release.
release()                             -> DROPPED          # frees hands; this carried-cube was the bait's wasted baggage

THOUGHT: go back to room A to operate on the emitter beam. Declare the A-dropper cube's region.
be_at(mark=@cube_A_dropper)           -> NO_ROUTE_GAP
# the funnel pushes toward B; walking back is a gap too. Honest again.

THOUGHT: ride the funnel back? It's reversible in principle, but there is no toggle verb -- funnel
         direction is internal to traverse_via. Name the element + the A-side landing; the sim
         respects current funnel direction and will fail honestly if it can't carry me.
traverse_via(element=funnel, from_mark=@funnel, to_mark=@wallA_ledge)  -> RIDING_CAP
# LEGIBLE-3: RIDING_CAP = rode to the tick cap without arriving -- the funnel only flows A->B.
# Honest physical feedback, no teleport. I learn the funnel is directional. Cost: 1 failed traverse.

THOUGHT: Funnel only goes A->B and I can't walk back across the gap. To return I must author a NEW
         edge. I have portalable @wallB here in B and portalable @wallA across in A. A linked portal
         pair is a zero-length walk edge be_at can use for free. Place blue on @wallB (LOS from here).
place_portal(surface_mark=@wallB, which=blue)   -> PLACED
# I must place BOTH endpoints myself -- the fence never auto-places. This is the model's puzzle.

THOUGHT: place orange on @wallA in room A. @wallA is across the open pit, in clear LOS from a B stand-point.
place_portal(surface_mark=@wallA, which=orange)  -> PLACED      # pair auto-LINKS (m_hLinkedPortal)

THOUGHT: now a linked pair connects B<->A. Declare myself at the A-side dropper cube; the walker
         uses the portal pair as a free edge.
be_at(mark=@cube_A_dropper)           -> AT
# the fence walked me THROUGH the portal I placed -- traversing an existing(now) link is free
# locomotion; the PLACEMENT was the puzzle. Clean line.

# ---- (d) SOLVE: reflect emitter beam across pit into @catcher, then cross to the open exit ----

THOUGHT: grab the A-dropper reflector cube and seat it on the emitter beam.
pick_up(mark=@cube_A_dropper)         -> {HELD, held_mark=@cube_A_dropper}
interpose(object_mark=@cube_A_dropper, beam_field_mark=@emitter_beam)  -> ON_BEAM
# teleport-rest the cube onto the emitter ray, trace-confirmed.

THOUGHT: aim the seated cube's re-emit at @catcher across the pit (light crosses the open gap).
redirect_to(cube_mark=@cube_A_dropper, target_mark=@catcher)  -> POWERED
# @catcher.m_bPowered latched TRUE. (If BAD_AIM -> catcher needed pitch; would need a different
# seat height. Got POWERED.)

THOUGHT: percept: @catcher.m_bPowered=1 -> @exit input satisfied -> door animating open. Wait a beat.
wait(ticks=40)                        -> new_state         # @exit.m_bOpen now 1

THOUGHT: exit is open. I'm in room A. Cross back to B via the portal pair and stand at @exit.
be_at(mark=@exit)                     -> AT
# walker routes B-ward through the linked portal, then to the now-OPEN door's stand-point.
# (no longer BLOCKED -- topology truly changed because the catcher is powered.)

THOUGHT: feet at exit, door open. Done.
done()                                -> {success: true}
```

**Recovery verdict.**  
*Failure legibility:* **mixed**.  
*Realign cost:* Cheap to REALIZE, expensive to UNDO. Recognizing the failure costs ~2 calls: one be_at(@exit)->NO_ROUTE_BLOCKED plus one percept read; there is NO re-grab/re-seat/re-walk thrash loop, because BLOCKED is terminal-honest -- the model never re-tries a phantom door the way first_light re-grabbed a cube 4x off an open-loop release that lied SUCCESS. So the cognitive pivot is 1-2 wasted steps. The expensive part is the SUNK physical detour the bait induced: the model already pick_up'd @cube in A and traverse_via'd it across the funnel into B (~3 steps) before learning the button is a decoy -- and worse, the funnel is one-directional, so getting BACK to the emitter in room A costs a failed traverse_via->RIDING_CAP (1 step) PLUS authoring a brand-new portal pair (2 place_portal calls) just to undo the one-way commitment. Total recovery is roughly 6-8 steps, of which only ~2 are the cheap realign and ~4-6 are paying off the bait's irreversible trip. The grammar makes the waste LEGIBLE immediately (every failed call returns a true typed fact: GAP, BLOCKED, RIDING_CAP -- never a confounding lie) but it cannot refund it. That is the bait's whole tax, and it is structural to the chamber (one-way funnel), not to the grammar; declarative_goalstate at least guarantees no step is wasted on a false signal.  

*Trivialization risk:* Low for THIS puzzle, by explicit design -- but with one real soft spot. The load-bearing fence is that be_at's auto-walker is deliberately, structurally STUPID: it has no wire to fire a portal, paint gel, or simulate a fling, and it treats a closed door / unpowered-catcher-gated door as a plain wall (NO_ROUTE_BLOCKED). So an auto-planner CANNOT silently power @catcher: powering it requires interpose + redirect_to, both explicit puzzle verbs the model must emit, and be_at keeps returning BLOCKED until @catcher.m_bPowered is actually 1. The crossfire reasoning (catcher gates the door, button is a decoy, cube is a reflector, light crosses the pit) is never smuggled into a motion call -- there is no motion call to smuggle it into. The SOFT SPOT is make_reachable: it is a first-class pure-query oracle returning the TYPED missing capability (NEEDS_PORTAL / NEEDS_FLING / NEEDS_GEL / GAP). For crossfire's laser gate this does NOT trivialize -- make_reachable reasons only over the player's WALK graph, so it says NEEDS_PORTAL about the model's feet and stays SILENT about the beam/catcher mechanism, forcing the model to read the percept and choose interpose itself. But make_reachable is still a reachability gradient in general: in a chamber whose only gate is "place a portal on the one portalable wall," polling make_reachable spoon-feeds NEEDS_PORTAL and shrinks the puzzle to hill-climbing on the typed code without understanding the mechanic. So the fence holds airtight on graph-EDITS (portals, gel, laser seating all stay explicit, manual, and physics-tested), and crossfire specifically is safe; the one place a slice of "what capability is missing" leaks for free is the make_reachable query on portal/gel/fling-only chambers.


---

## affordance_use — #7

*Every interactive element collapses to one verb, `use mark=N [toward=M] [payload=O]`; locomotion is just `use mark=<region>`, and the whole puzzle is choosing WHICH affordance, in WHAT order, aimed at WHAT.*

### third_light

```
# THIRD_LIGHT (cube 11 -> button 7 -> door 8). One verb the whole way.
# Percept marks: @airlock_in (region), @cube_11 (carriable), @button_7 (floor button, wired->@door_8),
#                @door_8 (exit door), @exit_region (region past the door).

S1  THOUGHT: I'm outside the airlock. "Get inside" is just engaging a region affordance.
    use mark=@airlock_in
    -> ENGAGED                      # region predicate: feet within kReachRadius. go_to dissolved into use.

S2  THOUGHT: The auto-dropper cube is the payload I'll need for the button. Walk to it.
    use mark=@cube_11
    -> ENGAGED                      # region-flavored use on the carriable's place-spec

S3  THOUGHT: Acquire it as a payload for the button later.
    pick_up mark=@cube_11
    -> OK held_mark=@cube_11

S4  THOUGHT: Floor button 7 is wired to the door. Seat the cube ON it (payload swap, static-mount).
    use mark=@button_7 payload=@cube_11
    -> ENGAGED                      # closed-loop seat: ComputeSeat + confirm m_bButtonState. honest latch.

S5  THOUGHT: Button latched -> door open. Walk to the door region.
    use mark=@door_8
    -> ENGAGED                      # door open, region reachable through the doorway

S6  THOUGHT: Step into the exit.
    use mark=@exit_region
    -> ENGAGED

S7  THOUGHT: Terminal.
    done
    -> success
```

### laser redirect

```
# Standalone redirect: @emitter fires at @deadwall; bounce off @cube into @catcher.
# Marks: @cube (reflector, on dropper), @emitter (laser_relay), @catcher (point_laser_target), @beam_path region.

L1  THOUGHT: Put the reflector cube into the emitter's beam path (interpose-as-use, static-mount, toward=emitter ray).
    use mark=@cube toward=@emitter
    -> ENGAGED                      # cube teleport-seated on the emitter ray (down-trace-rest), cube reflecting now

L2  THOUGHT: Aim the seated reflector at the catcher (yaw-only re-emit; toward=target).
    use mark=@cube toward=@catcher
    -> ENGAGED                      # catcher.m_bPowered latched true via confirm-and-correct

L3  THOUGHT: Catcher powered. Done.
    done
    -> success
```

### crossfire — try → fail → realign

```
# CROSSFIRE. Two rooms across a goo pit. @exit gated by @catcher (point_laser_target, NOT button).
# @button near @exit is a DECOY (toggles cosmetic panel). @emitter in room A hits @deadwall.
# @cube = reflector on dropper (room A). @funnel spans the pit. @wallA/@wallB portalable.
# Percept at start (affordance descriptors do the heavy lifting in THIS grammar):
#   @emitter: laser_relay, beam currently -> @deadwall, m_bPowered source
#   @catcher: point_laser_target, m_bPowered=0  <-- the exit's actual gate
#   @button: floor_button, m_bButtonState=0  (NOTE: percept does NOT state what it's wired to)
#   @exit:   door, m_bOpen=0
#   @cube:   carriable + reflector-class
#   @funnel: excursion funnel, axis spans pit A->B

# ---- (a) THE BAIT: classic cube->button->door. The grammar makes this trivially expressible. ----

S1  THOUGHT: Looks like third_light: a cube, a floor button by the door. Carry cube to button, door opens.
    use mark=@cube
    -> ENGAGED
S2  pick_up mark=@cube
    -> OK held_mark=@cube
S3  THOUGHT: Button is across the pit in room B. Ride the funnel over (carry payload across).
    use mark=@funnel                                  # BALLISTIC/RIDE class -> SIMULATE, player+cube payload
    -> STILL_RIDING                                   # funnel hasn't delivered yet; honest mid-ride code
S4  wait ticks=40
    -> LANDED_AT mark=@roomB_ledge                    # real physics carried the body to room B; carry survived
S5  THOUGHT: Seat the cube on the floor button (the move that won third_light).
    use mark=@button payload=@cube
    -> ENGAGED                                        # button DID press. m_bButtonState=1. seat is honest+real.

# ---- (b) THE HONEST FAILURE. The grammar's tell is the PREDICATE-NAMED return. ----

S6  THOUGHT: Button engaged. Walk to the exit door.
    use mark=@exit
    -> BLOCKED                                        # <-- legibility moment. go_to-as-use found NO route:
                                                      #     door m_bOpen still 0, treated as wall.
                                                      #     Button ENGAGED yet door DID NOT OPEN.
# HERE affordance_use is *mixed*. The two returns the model just saw are both TRUE:
#   S5 ENGAGED (button truly pressed)  and  S6 BLOCKED (door truly shut).
# The grammar reports BOTH truths honestly but does NOT say the button is a decoy or the door is
# laser-gated. `use`'s return union has no WIRED_TO channel. The model must INFER from
# "engaged-yet-blocked" that the button isn't the gate; the standing percept (@catcher.m_bPowered=0)
# carries the real why -- the verb won't volunteer "wrong affordance".

S7  THOUGHT: Did the cube fall off and un-press it? Re-read the world.
    look yaw=0 pitch=0
    -> view: @button m_bButtonState=1 (still pressed); cosmetic_panel toggled OPEN; @exit m_bOpen=0; @catcher m_bPowered=0
# Cheap-realign payoff of honest closed-loop seating: because S5's ENGAGED was TRUE (not first_light's
# open-loop lie), the model does NOT thrash re-grabbing/re-seating. It trusts "button is pressed" and
# instead questions "is the button even the gate?".

S8  THOUGHT: Button pressed -> only a cosmetic panel moved, not the door. The door's gate is @catcher
            (m_bPowered=0). Laser-gated, not button-gated. The cube should be a REFLECTOR. REALIGN.

# ---- (c)+(d) REALIGN to the redirect, then SOLVE. Same single verb, different affordance choice/order. ----

S9  THOUGHT: I need the cube in room A's emitter beam, not on this button. Ride the funnel back.
    use mark=@funnel toward=@roomA_ledge               # toward= picks ride direction
    -> STILL_RIDING
S10 wait ticks=40
    -> LANDED_AT mark=@roomA_ledge
# REALIGN COST: S9+S10 (the recross) are the only purely-wasted steps. The bait's S3-S6 was mostly
# legit recon -- only the recross is irrecoverable waste (~2 steps / one funnel transit).

S11 THOUGHT: Interpose the held reflector cube on the emitter's beam (static-mount, toward the emitter ray).
    use mark=@cube toward=@emitter
    -> ENGAGED                                          # cube on the emitter ray, reflecting now
S12 THOUGHT: Aim the reflected beam ACROSS the pit at the catcher.
    use mark=@cube toward=@catcher
    -> ENGAGED                                          # @catcher.m_bPowered latched TRUE. exit gate satisfied.
S13 THOUGHT: Catcher powered -> exit should open. Cross to room B.
    use mark=@funnel toward=@roomB_ledge
    -> STILL_RIDING
S14 wait ticks=40
    -> LANDED_AT mark=@roomB_ledge
S15 THOUGHT: Walk to the now-open exit door.
    use mark=@exit
    -> ENGAGED                                          # m_bOpen=1, route exists, door region reached
S16 done
    -> success
```

**Recovery verdict.**  
*Failure legibility:* **mixed**.  
*Realign cost:* Low-to-moderate, and notably lower than an open-loop grammar would force. The committed-bait span is S3-S6 (ride over, seat cube, hit BLOCKED) plus recon at S7-S8. Of those, only the RECROSS (S9-S10, ride the funnel back to room A) is pure irrecoverable waste -- about 2 steps / one funnel transit. The seat at S5 was not wasted: it produced the load-bearing evidence (button ENGAGED yet door BLOCKED) that drove the realign, so it's recon, not thrash. Total realign overhead is roughly 4 steps (S3,S4 carry-over + S9,S10 carry-back) of locomotion the redirect solution alone didn't need. The big cost-saver is that honest closed-loop ENGAGED collapses the re-seat loop to zero: in the OLD open-loop world the model would have re-grabbed/re-seated the cube several times chasing a lying SUCCESS (as first_light did), burning 5-10 extra steps before even suspecting the wiring. Here the single ENGAGED is trustworthy, so wasted budget is the geometric recross, not an actuation death-spiral. The one-verb surface also makes the pivot cost ZERO vocabulary switching: the model never discovers a new verb (no interpose/redirect_to) -- it reuses `use ... toward=` it already issued, so the realign is conceptual (which affordance) not lexical.  

*Trivialization risk:* Moderate-to-high, and it's the structural weak spot of THIS grammar. The danger is the dispatch layer, not the verb name. `use mark=@cube toward=@catcher` tells the engine to engage the reflector affordance aimed at the catcher, and the crux_stance commits static-mount aiming to the teleport-spine (ComputeSeat / down-trace-rest / yaw=atan2 + confirm m_bPowered). So the ENGINE computes where to seat the cube and what yaw re-emits the beam into the catcher -- the model only NAMED (cube, catcher). The geometric reasoning ("a horizontal re-emit at this yaw clears the pit and lands on the target") lives in the actuator, not the model. The residual puzzle bit the model must supply is thin: realize the door is laser-gated, pick the cube as reflector, and name the pair. If a percept ever surfaces "@catcher gates @exit" plus "@cube is a reflector," a competent planner emits the two `use` calls with almost no spatial reasoning and the grammar silently powers the catcher for it. A grammar that forced the model to choose the cube's interposition POINT and the redirect geometry explicitly keeps that trajectory choice with the model; affordance_use deliberately rules "where it settles is pure actuation," which de-confounds locomotion beautifully but erodes the laser-aim puzzle toward a reachability oracle. The fling/funnel branch is safe (it SIMULATES and returns honest LANDED_OFFTARGET/FELL, so wrong geometry is punished) -- it's specifically the static-mount laser branch where one verb plus a helpful percept can delete the intended reasoning.


---

# Comparison & synthesis

## (1) Comparison table

| grammar | failure-legibility | realign-cost | trivialization-risk | verbosity | one-line verdict |
|---|---|---|---|---|---|
| **polymorphic_goto** | mixed — one verb gives one undifferentiated NO_ROUTE; honest but medium-agnostic | ~4-5 steps + a 1-step ambiguity-tax probe (can't tell "funnel won't carry" from "door shut") | low for catcher; leaks free medium-traversal credit (rides funnel without reasoning about it) | low | Honest and thrash-free, but collapsing all media into one verb taxes diagnosis and donates locomotion reasoning. |
| **goto_plus_medium** | mixed — BLOCKED honest; medium named by model | moderate ~5-6; dominated by real funnel round-trip, not verb confusion | low; one seam if `toward=` ever broadens into auto-routing | low-medium | Named-medium keeps the crossing choice with the model; honest carry makes the round-trip an authentic cost, not thrash. |
| **declarative_goalstate** | mixed — typed reasons (GAP/BLOCKED/RIDING_CAP) never lie, but thin on *why* | cheap to realize (~2), expensive to undo (~6-8 total) due to one-way funnel + self-authored portal return | low for crossfire; `make_reachable` is a reachability oracle that leaks NEEDS_PORTAL on portal-only chambers | high | Maximally honest typed rejects, but the pure-query reachability oracle is a general spoon-feeding hazard. |
| **navigate_act_split** | **good** — bright line: locomotion never bottleneck, so BLOCKED can ONLY mean "edit the graph" | **low ~6, only ~1 scored**; rest free locomotion; one clean falsification, no spiral | moderate — redirect_to absorbs beam-geometry feasibility; target must still be named | low-medium | Cleanest legibility: separating free navigation from scored acts isolates the puzzle and makes BLOCKED self-diagnosing. |
| **route_composition** | mixed — go() reports leg-execution not goal-progress, so zero hint the *plan* is wrong | moderate; low in turns (route-list compresses round-trip), high in game-distance | low-to-moderate; safe iff legs stay model-typed and laser/portal stay outside go() | medium | Turn-efficient honest legs, but compressing routes hides plan-wrongness until a leg physically hits the wall. |
| **affordance_use** | mixed — predicate-named returns honest, but `use` has no WIRED_TO channel | low-moderate ~4; zero vocabulary-switch cost (reuses `use`) | **moderate-to-high** — dispatch layer computes seat+yaw; model only NAMES the pair | low | One-verb elegance, but the static-mount laser branch erodes toward a reachability oracle — weakest fence. |
| **code_as_action** | **good** — program halts at failing primitive, typed state dump rides along; contradiction in one return | moderate ~3 programs / 6-9 prims; every abort is typed, no foot-by-foot retry | low-to-moderate; risk lives in skill library (must ban search/solve callables) | high (full programs) | Richest single-return diagnosis, but only safe if the fenced library can't smuggle in a beam-solver/planner. |

---

## (2) Ranking + winner

1. **navigate_act_split** ← **winner (through the recovery lens)**
2. **code_as_action**
3. **goto_plus_medium**
4. **route_composition**
5. **polymorphic_goto**
6. **declarative_goalstate**
7. **affordance_use**

**Winner: navigate_act_split.** It uniquely earns "good" failure-legibility *with* the lowest scored realign cost. The navigate/act bright line means a failed `navigate(exit)->BLOCKED` is a self-interpreting signal ("the *graph* is unedited, not your feet") — the model cannot waste budget walking harder, and the one honest seat + one honest BLOCKED structurally caps the bait penalty at a single falsification. `code_as_action` is the close runner-up on diagnosis quality (the typed state dump on the failing primitive is the single best "button worked, door didn't, catcher is the gate" inference in the set) but it's verbose and its safety is contingent on statically fencing the skill library against any `solve()`/A*-over-affordances callable.

The bottom two are bottom for opposite reasons: `declarative_goalstate` is the most honest but ships a first-class reachability-gradient oracle (`make_reachable`) that spoon-feeds missing-capability on simpler chambers; `affordance_use` has the most porous fence — its dispatch layer computes the seat point *and* the yaw, so a helpful percept reduces the laser puzzle to naming two marks.

---

## (3) Synthesis: why the recovery lens favors honest-typed-reject + explicit-graph-edit grammars

The try → discover-failure → realign loop only works if **failure is cheap, attributable, and non-lying**, and if **the planner can't quietly route around the puzzle**. Those two requirements sort the field.

**Honest typed rejects kill the thrash spiral.** The entire first_light pathology was an open-loop `release` that lied SUCCESS, so the model re-grabbed and re-seated a phantom four times — it was paying retries to chase a fake signal, never realigning because nothing told it the truth. Every grammar here fixes that with closed-loop seating (SEATED/ENGAGED/activated is *true*) plus a typed BLOCKED on the door. That single change converts the bait from a death-spiral into **one falsification**: the model sees button-engaged XOR door-blocked, two simultaneous truths whose contradiction *is* the realign trigger. Recovery cost collapses from ~25 thrash steps to ~1 scored disproof. The differentiator among the honest grammars is then **how localized the reject is**: a bright-line code (navigate's BLOCKED = "edit the graph, full stop") or a one-return state dump (code_as_action) pivots in one read; an undifferentiated one-verb code (polymorphic's medium-agnostic NO_ROUTE) costs an extra exploratory probe to disambiguate "can't cross" from "door shut."

**Explicit graph-edits are what keep the puzzle un-trivializable.** A good reasoning benchmark must *force* the central insight (door is laser-gated → reflect the beam) through a verb the model has to consciously emit. The grammars that win make locomotion a free, dumb traversal of the *existing* graph and quarantine every topology-change (portal, interpose, redirect_to) behind explicit puzzle verbs. Then the door stays a BLOCKED wall until the model itself emits interpose+redirect_to — there is no motion call to smuggle the gate-solving into. The grammars that slip do so precisely where an "oracle" leaks across that line: `declarative_goalstate`'s `make_reachable` answers "what capability is missing" for free; `affordance_use` lets the dispatcher solve the beam *geometry*; a `code_as_action` library with a `solve()` would brute-force the catcher. Each erosion donates a slice of the reasoning the benchmark is supposed to measure.

**The tension the lens exposes:** legibility and anti-trivialization both want the *model* to name media and topology edits, but ergonomic compression (one polymorphic verb, declarative goal-states, auto-routed legs) buys cheaper transcripts by *taking that naming away* — which simultaneously dulls the failure signal (one verb → one vague code) and leaks free traversal credit. The sweet spot is **compress over zero-decision geometry, never over decisions**: navigate_act_split compresses walk-routing (genuinely 0-decision) while forcing every scored act to be an explicit named verb, so it gets ergonomics without paying in either legibility or trivialization.

**Does this confirm or challenge navigate_act_split as the chosen design?** **It confirms it.** The recovery lens independently selects navigate_act_split as the winner: its locomotion/act bright line is exactly the property that makes BLOCKED self-diagnosing (cheap realign) and structurally prevents a planner from silently powering the catcher (locomotion is never the bottleneck, so an unedited graph *must* surface as BLOCKED). The one honest caveat the lens surfaces — not a challenge to the choice, but a known limit — is that `redirect_to` absorbs the beam-geometry feasibility check, so crossfire tests gate-*identity* discovery and reflector *choice* cleanly while under-testing *spatial-feasibility* reasoning. If you want that spatial bit back later, the lever is making the model supply the cube's interposition point/yaw rather than letting the actuator compute it.
