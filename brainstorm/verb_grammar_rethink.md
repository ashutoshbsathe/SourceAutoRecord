# Verb Grammar Rethink — locomotion + the full-element act surface

**Thesis.** Locomotion is a confound, not a puzzle. Demaine 2018 puts Portal's PSPACE-hardness
entirely in topology *changes* — buttons, portals, gel, funnel direction — never in traversing
whatever topology currently exists. `first_light` burned 686k tokens thrashing on foot-by-foot
motion; `third_light` solved the same chamber in 286k once `go_to` internalized A\* and `release`
internalized the seat. So the whole grammar collapses to one rule:

> **Free if the act *consumes* an affordance that already exists; a puzzle verb if it *creates or
> changes* one.**

One free, always-solved locomotion verb for "traverse existing topology," and a small closed set
of explicit verbs for every act that edits the puzzle graph. This is the altitude-doc tenet
("macros at the locomotion/aim line; `state{}` describes what exists, never what to do") and the
laser design's "verb owns continuous geometry, model supplies discrete intent," applied uniformly
to portals, gels, funnels, faith plates, and light bridges.

---

## Recommendation (backbone + grafts)

Build on **`navigate_act_split`** — the only proposal whose verb table is ~1:1 with the proven
surface, minimal by construction, and natural for a 500-hour human ("I go there; I place a portal;
I press the button"). Three grafts, each from a losing proposal:

1. **A small CLOSED momentum family — `ride` and `launch` — does NOT fold into `navigate`**
   (from `goto_plus_medium`). Choosing a momentum medium *and its direction* is a real puzzle bit
   a player consciously decides ("ride the funnel UP", "fling off the floor portal"). It must be
   transcript-visible, not auto-selected inside a polymorphic planner. This is the fix for
   `polymorphic_goto`'s fatal hole: a planner that silently elects the funnel edge and returns
   `SUCCESS` proves nothing about whether the model understood the mechanic.
2. **A hard CAPABILITY FENCE** (from `declarative_goalstate`). `navigate`'s planner has *no code
   path* to fire a portal, paint, toggle, or simulate a ballistic arc — it is structurally
   A\*-over-walkable-now, not "told not to." `BLOCKED` becomes a structural guarantee ("no route in
   the current graph; you must edit it"), not a prose promise.
3. **Spine unification at the *implementation* layer, not the verb surface** (from `affordance_use`).
   `interpose` IS the funnel-path interception verb, verbatim; one shared
   `ComputeSeat`/`FindStandoff`/teleport-confirm-retry core backs `interpose` / `release(at=)` /
   future reflector placement. Vocabulary economy in the C++, not a polymorphic `use()` on the
   surface (that `use()` is the state-dependent-semantics confound — the same one the laser design
   already killed).

Two corrections the judging pass demanded, both fixable on this backbone:

- **Do NOT fold momentum portal traversal into `navigate`.** The shipped grammar leaves
  halt-vs-traverse a checkpoint-deferred fork, and the fling-in-disguise problem is real. A *static*
  linked pair walked at walking speed may become a `navigate` edge **only** after the `GoToPlanner`
  portal-edge recon lands and **only** when no momentum is converted. Until then, all portal
  traversal routes through `launch`.
- **Defang the typed-reject hill-climbing oracle.** Honest typed rejects stay, but precondition
  failures cost a step (no free dry-run probe — `make_reachable` is cut), and `ride`/`launch` return
  the *bare* landing cell on a miss, never a corrective gradient ("undershot by N, aim higher"). The
  model must reason *why* it missed from the visible chamber, not binary-search portal geometry off
  engine feedback.

---

## The cut: free locomotion vs puzzle moves

| FREE — engine-owned, zero puzzle bits, never scored | PUZZLE — model-owned, one verb per discrete decision |
|---|---|
| foot-by-foot steering (VFH + A\*) | which surfaces get a portal, and at what orientation |
| the continuous geometry of seating an inert payload | which gel on which surface |
| the per-tick physics of *executing* a chosen ride / arc / bounce | whether / which-direction to ride a funnel |
| carrying a held cube through a leg (compound hull) | whether / where to launch |
| settle / confirm-retry loops | which cube intercepts which beam, aimed at which target |
| crossing an already-projected bridge, running an already-painted strip | which button; what to carry; **the ORDER of all of it** |

The subtlety the panel kept tripping on: **choosing the medium and its direction is a puzzle bit**
(`ride`/`launch` are explicit, named, directioned verbs), but **executing the chosen medium's fixed
physics is free.**

---

## Crux decisions

### 1. Simulate vs teleport for ballistic player traversal

**Simulate the player, always.** The engine drives real Source physics tick-by-tick
(`AdvanceTicksBlocking` + framebulk) and returns the honest landing cell. We never teleport the body
to a computed endpoint. **Teleport stays reserved for inert OBJECT placement** (the laser cube seat,
a future funnel reflector) where the rest pose is closed-form geometry with no trajectory to reason
about.

The dividing test is **"is it the player's body?"** — body ⇒ simulate (fidelity, anti-cheat, the
model must get the geometry right); inert payload ⇒ teleport (determinism, safety, the player is
never inside a beam). This resolves the apparent fling-vs-laser-cube inconsistency: same principle,
opposite sides of the body/object line. It is also locked by the shipped grammar — the snap-move
idea was already rejected because it "invites *you cheated the movement*." The trajectory IS the
puzzle for momentum chambers; teleporting the body deletes momentum-as-currency, the single most
Portal-specific reasoning act.

### 2. Does `go_to` survive?

**Renamed to `navigate`; semantics widened; backend kept byte-identical.** It stays one verb and
absorbs walking on ground + crossing an already-projected light bridge + running an already-painted
gel strip (all "traverse existing topology"). It does **not** absorb momentum portal traversal
(that is `launch`) or funnel riding (that is `ride`). The VFH + A\* stack is unchanged; the rename
signals the widened medium scope and the new capability fence. Renaming over a fresh verb keeps the
table at ~15 and the human mental model ("go there") intact. The medium family (`ride`/`launch`) is
the *deliberate non-absorption* that keeps medium-choice transcript-visible — the fix for
`polymorphic_goto`'s silent auto-selection.

### 3. One ballistic verb: `launch` (portal fling = faith plate = gel bounce)

This is the one place the final design overrides the panel. The panel left the faith-plate as
"`navigate`-onto + auto-arc" and left the repulsion-gel bounce (the user's "jump at the end from the
blue patch") **homeless** — owned by no verb. Both are wrong: `navigate` cannot honestly carry a
ballistic arc (it traverses *walkable* topology, and an arc is not walkable), and a momentum event
with no verb cannot be reasoned about or reported honestly.

**Unify them.** A *launch* is any event that turns the player into a ballistic projectile —
fall-through a floor portal, step onto a faith plate, drop onto a repulsion (blue) patch. In all
three: the model commits to it; the trajectory is determined by entry conditions + fixed geometry
that the model shaped by *upstream* choices (portal placement, paint placement, approach speed); the
engine **simulates** the arc and returns the honest bare landing cell; the model reasons the landing
from chamber geometry, no gradient.

`launch(via: mark)` covers all three with one honest verb — **net-zero verb count** (it replaces
`fling` and removes the faith-plate special-case debate). "Fling" survives as the colloquial name
for the portal case in prompt text. `ride` stays separate because *continuous weightless carry*
(funnel) is a genuinely different mental category from *one-shot ballistic launch* — that split is
for human-mental-model reasons, not expressivity (they share a contract shape and could collapse to
one verb, but a player thinks "I ride the funnel" vs "I get launched", so we keep both).

### 4. Anti-oracle discipline

Honest typed rejects (`NOT_PORTALABLE`, `NOT_SEATED`, `BLOCKED`) stay — they are the highest-ROI
honesty component (the `first_light` lesson). But: (a) every reject costs a full step (no free
`make_reachable` probe); (b) `ride`/`launch` return the *bare* cell on a miss, never a corrective
gradient; (c) `BLOCKED` names only "no route in the current graph," not which edit would fix it.
Honest enough to know you missed, thin enough that you must reason *why* from the visible chamber.

### 5. Funnel as laser — partly verified, gated on recon

`interpose` is reusable **verbatim** for placing a reflector/cube on a funnel *path* (object-on-ray,
the laser teleport spine) — sound, already the laser doc's plan. But `ride(funnel)` and the
direction-confirm predicate need recon: `prop_tractor_beam` direction/on-state fields are **untested
and absent from recon chambers**. Ship the object-interception half now on the existing spine; gate
the player-ride half behind an L0-style `sar_harness_dump_fields` pass, exactly as the laser verbs
gated on laser L0.

### 6. Success honesty (the `first_light` bug class)

A `ride`/`launch` success code (`SETTLED`/`LANDED`) is returned **only** when the body is
velocity-stable **AND** on-ground/in-zone **AND** within `kReachRadius` of the named target.
Settling-but-short, mid-air at the far mouth, or drifting all return the typed miss with the *real*
cell, never a success word. Same discipline as the `release` `m_bButtonState` dwell. A momentum verb
that lies "you arrived" is worse than `first_light`'s transient-seat bug, because the model builds
its next portal plan on a false position.

---

## Verb table

Three genuinely new verbs (`navigate` rename, `ride`, `launch`) + three new puzzle verbs
(`place_portal`, `paint`, `press`); the laser family and carry/aim/wait/done are shipped. No
polymorphic `use()`, no leg-list control flow, no `make_reachable`, no faith-plate verb.

| Verb | Kind | Signature → returns | Notes |
|---|---|---|---|
| `navigate` | loco | `navigate(mark)` → `SUCCESS \| ADVANCED \| BLOCKED \| DIED` | Renamed `go_to`. Traverses ground / projected bridge / painted strip / (post-recon, walk-speed) static portal pair. **Hard fence: no edit code path.** Existing VFH+A\* backend. |
| `ride` | both | `ride(medium, toward: UP\|DOWN\|mark)` → `SETTLED(cell) \| EJECTED(cell) \| NOT_IN_MEDIUM \| DIED` | Excursion funnel (GATED on `prop_tractor_beam` recon). Direction = puzzle, weightless carry = free. Replaces the "travel for 30 ticks" anti-pattern. **`FAR` enum dropped** (reference frame flips on a reversed funnel — a footgun); axis-sign preferred (also less leaky than naming the destination), `mark` allowed. |
| `launch` | both | `launch(via: mark)` → `LANDED(cell) \| LANDED_SHORT(cell) \| NO_MOMENTUM \| DIED` | One verb for **portal-fling, faith-plate, and repulsion-gel bounce**. Simulates the real arc. Trajectory is owned by upstream choices (the two `place_portal`s / `paint` / approach speed). **No `toward` arg** — you cannot aim a fling at a mark, you aim it by where you put the portals; bare cell on miss, no hill-climb. |
| `place_portal` | puzzle | `place_portal(color: blue\|orange, surface: mark)` → `PLACED \| NOT_PORTALABLE \| CANT_FIT \| OVERLAP \| FIZZLED \| NO_LOS` | The single portal graph-edit; **supersedes the designed `shoot_portal`**, over `TraceFirePortal`. Confirm via TraceFirePortal's own result + `m_hLinkedPortal`/`m_bIsPortal2` (**not** `m_bActivated` — that's the button/cube field). Pair auto-links. Conversion-paint flips the portalability predicate (live re-read). **Also relocates a light bridge** when fired at the projector face. Re-placing a color moves it. |
| `paint` | puzzle | `paint(gel: conversion\|repulsion\|propulsion\|cleansing, surface: mark)` → `PAINTED \| NOT_PAINTABLE \| NO_SPRAYER` | GATED on paint-map percept. conversion = flips portalability; repulsion = bounce; propulsion = speed; **cleansing = erase paint**. Last-coat-wins, surfaced in percept. After paint, `navigate`/`ride`/`launch` consume the surface for free. |
| `press` | both | `press(button: mark)` → `PRESSED \| NOT_PRESSED \| NO_REACH` | Pedestal button = walk + use-pulse; toggles door/funnel-direction/bridge state. **Cube-on-floor-button is owned by `release(at=button)`, not `press`** — `press` on a floor button needing a cube returns `NOT_PRESSED` with hint "use release(at=button)." (Kills the state-dependent-semantics confound.) |
| `interpose` | puzzle | `interpose(object, beam)` → `ON_BEAM \| NO_REACH \| NO_BEAM \| OCCUPIED` | Shipped laser family, teleport-primary down-trace-rest. Reusable verbatim for funnel-path interception. Gated on the `GoToPlanner` reachability/fairness layer. |
| `redirect_to` | puzzle | `redirect_to(object, target)` → `POWERED \| NOT_POWERED \| NOT_SEATED` | Shipped yaw-only orient atom. Gated to an already-seated cube (`NOT_SEATED` enforces `interpose`-first ordering). |
| `power_with` | puzzle | `power_with(object, target)` → union of the two | Pure composition (`interpose` + `redirect_to`); fewest tokens for the common case. **Accretion-risk entry** — earns its place by ergonomics + the one-shot-vs-decompose observable, not expressivity. Re-justify if the table grows. |
| `pick_up` | both | `pick_up(object)` → `HELD \| NO_REACH \| NOT_HOLDABLE` | Shipped. Engine-truth `g_heldEntityKey`. WHAT to carry = puzzle; carrying-while-walking = free. |
| `release` | both | `release(at: mark = none)` → `DROPPED \| SEATED \| NOT_SEATED \| NOT_FAIR \| SEAT_OCCUPIED \| FIZZLER_BETWEEN` | Shipped (full reject set per `release_place_on_button_design.md`; `NOT_FAIR` reports the first failing gate). `at=button` = the `third_light` closed-loop place. WHERE = puzzle; seating geometry = free. |
| `aim_at` | loco | `aim_at(target: mark)` → `SUCCESS \| BAD_MARK` | Shipped, strictly **camera-only** (laser lock — never an actuator). Feeds LOS before `place_portal`/`paint`. |
| `look` | loco | `look(yaw, pitch)` → view | Shipped exploratory free-look (15° snaps, pitch clamped/clamp reported). No body move. |
| `wait` | both | `wait(ticks)` → state | Shipped. **World-clock events only** (elevator, door anim, beam re-propagate). NOT funnel transit (that is `ride`). |
| `done` | both | `done()` → `success: bool` | Shipped terminal claim, checked vs the exit oracle. |

**Fizzlers** (`trigger_portal_cleanser`) are first-class hazards, not a verb: they surface in the
percept; `place_portal` across one returns `FIZZLED`; a carry/release plan through one returns
`FIZZLER_BETWEEN`. **Object transport on a funnel** (a cube riding to a destination) is **not** its
own verb — it composes as `pick_up` + `ride` (carry the held cube while riding); whether the
compound hull survives a funnel is an open recon question (below).

---

## Worked examples

### Funnel chamber
Funnel spans a goo pit but points the *wrong* way; a pedestal reverse-button is on the entrance ledge.
```
navigate(@reverse_button)          -> SUCCESS          # free ground walk
press(@reverse_button)             -> PRESSED          # PUZZLE: model reasoned the beam is backwards
ride(@funnel, toward=UP)           -> SETTLED(@exit)   # engine carries the weightless body; model named only the direction
navigate(@exit_door)               -> SUCCESS
done()                             -> true
```
Only `press` is a reasoning step. Skip it and `ride` returns `EJECTED(cell)` at the wrong end —
honest, with no gradient telling the model to reverse.

### Gel + portal chamber
Concrete walls (non-portalable); a conversion-gel sprayer can coat the far high wall; a cube + exit
floor-button gate the door.
```
navigate(@gel_blob_under_far_wall) -> SUCCESS
paint(conversion, @far_wall)       -> PAINTED          # PUZZLE: edits portalability
aim_at(@far_wall);  place_portal(blue, @far_wall)   -> PLACED   # NOT_PORTALABLE pre-paint = the honest "paint first"
aim_at(@near_floor); place_portal(orange, @near_floor) -> PLACED # pair auto-links
pick_up(@cube)                     -> HELD
navigate(@orange_portal)           -> SUCCESS          # walk-speed traversal of the now-existing static edge, no momentum
release(at=@exit_button)           -> SEATED           # PUZZLE: closed-loop place latches m_bActivated
navigate(@exit_door)               -> SUCCESS
done()                             -> true
```

### Portal-fling chamber
Deep pit, portalable floor at the bottom, portalable wall under the far exit balcony; only a fling
clears it.
```
aim_at(@pit_floor); place_portal(blue, @pit_floor)  -> PLACED   # PUZZLE: where speed comes IN
aim_at(@far_wall);  place_portal(orange, @far_wall) -> PLACED   # PUZZLE: the exit VECTOR (steeper wall = flatter/farther)
navigate(@drop_edge)                                -> SUCCESS  # free walk to the lip
launch(via=@blue_portal)                            -> LANDED(@exit_balcony)  # SIMULATED arc, momentum redirected by engine
navigate(@exit_door)                                -> SUCCESS
done()                                              -> true
```
Momentum reasoning is *entirely* the two portal-surface choices. Bad wall portal →
`LANDED_SHORT(@pit_floor)` with the bare splat cell, no "aim higher" — re-reason the geometry, don't
hill-climb. Teleporting to `@exit_balcony` would have made the portal-angle choice free.

### Named combos (verify-pass coverage)
- **"Run along the orange gel, jump off the blue patch."** `paint(propulsion, @floor_strip)` +
  `paint(repulsion, @end_patch)` (puzzle) → `navigate(@strip_start)` builds speed for free over the
  strip → `launch(via=@end_patch)` → `LANDED(cell)`. The bounce is a launch like any other; the
  model owns it via the paint placements + approach.
- **Light-bridge relocation.** A bridge is moved by firing a portal at the projector face:
  `place_portal(blue, @projector)` relocates the beam — a puzzle decision identical in spirit to
  redirecting a laser/funnel, owned by `place_portal`, no new verb.

---

## What the panel got right and wrong

| Proposal | Keep | Cut |
|---|---|---|
| `navigate_act_split` ⭐ | the backbone: free-traverse vs graph-edit; ~shipped verb table | linked-portal-as-free (overclaim); gradient-rich rejects |
| `goto_plus_medium` | name-the-medium-and-direction as a puzzle bit | `cross_gel(to=mark)` (answers the path + momentum sub-puzzles) |
| `declarative_goalstate` | the hard capability fence | `be_at`-only chassis; `make_reachable` wart; be_at-fling-in-disguise |
| `polymorphic_goto` | "navigate consumes affordances, never creates them" framing | medium-selection-is-free (buries the aha inside C++) |
| `route_composition` | momentum = leg boundary; one atomic launch event | ordered-leg-list + `failed_leg_index` (a sequencing search oracle) |
| `affordance_use` | `interpose` IS the funnel verb; spine unification at the *impl* layer | one `use()` verb (state-dependent-semantics confound) |
| `code_as_action` | simulate-body / teleport-object as the governing principle | the whole arm (re-fuses reasoning + codegen; parked to gated P1) |

---

## Open questions

- **`prop_tractor_beam` recon** — direction/on fields untested. `ride(funnel)` gated on an L0-style
  pass, exactly as the laser verbs gated on laser L0.
- **Paint-map percept unbuilt** — `paint()` can't honestly confirm `PAINTED`; gel-strip `navigate`
  and repulsion `launch` can't validate their surfaces; conversion-flips-portalability depends on it.
- **Light-bridge surface read unbuilt** — `navigate` treating a bridge as walkable assumes it
  registers in the hull-trace grid. Untested; bridge-walking is aspirational until verified.
- **Static-linked-portal `navigate` edge** depends on the `GoToPlanner` portal-edge recon
  (`sar_harness_laser_reachability_test`, the chosen frontier). Until it lands, ALL portal traversal
  routes through `launch` and the halt-vs-traverse fork stays deferred. Do not bank walk-through-portal
  as free before recon. (Lean: keep it `launch`-only until recon proves the planner can distinguish
  intended-traverse from blunder-through.)
- **DIED / IRRECOVERABLE terminals (gap-4) unbuilt** — `launch`/`ride` into goo respawns silently
  and `navigate` then drives a corpse. These terminals MUST ship before any momentum verb, or the
  verbs gaslight the model.
- **Player-body jank re-roll** — the laser confirm-and-correct loop re-rolls a free cube cheaply; you
  cannot cheaply re-drop the player. A correct plan that lands jank-short 1-in-50 (vphysics tumble,
  `sv_alternateticks` pairing) mislabels as `LANDED_SHORT`. Need a settle re-attempt budget + honest
  hard-cap + a *measured* body-jank rate before trusting the codes.
- **Settle tolerance** for `SETTLED`/`LANDED` vs the short codes is undefined — exactly where a
  dishonest success hides. Needs a concrete stable-velocity + stable-ground-contact predicate,
  bisected like the `release` dwell constant.
- **`place_portal` `NO_LOS`** re-couples a puzzle act to body position (a "correct" portal choice can
  fail for a vantage reason). Decide: generous LOS check, or an explicit model-driven navigate-to-
  vantage — but NOT an auto-vantage step (that leaks which surface is the answer).
- **`ride(toward=mark)` vs `toward=UP|DOWN`** — axis-sign is more human-natural AND less leaky.
  Allow both; confirm the default in a human pilot.
- **Object-on-funnel compound hull** — does `pick_up` + `ride` (carry a held cube through a funnel)
  survive the tractor carry? Undefined; gate on the same recon as `ride`.

---

## Build order (SAR/C++ before Python; small phases)

1. **C++:** rename `go_to` → `navigate` (lexer + dispatch + prompt); backend byte-identical. Add the
   capability-fence assertion (planner has no portal/paint/toggle code path). Update
   `agentloop_smoke.py`. *Surface-only, no behavior change.*
2. **C++:** land the `GoToPlanner` reachability frontier — stepped-floor (per-cell `floorZ`-seam,
   multi-refZ) + portal-teleport edge, gated by `sar_harness_laser_reachability_test`. The
   already-chosen investment; upgrades `navigate`, unblocks the laser fairness layer + the static
   portal edge. Profile before trusting any reachability claim.
3. **C++:** ship the laser family (`interpose`/`redirect_to`/`power_with`) on the validated
   reachability layer (dual-role P1–P8). Designed + P0-passed; just needs the fairness gate. No new
   ontology.
4. **C++:** ship `place_portal` over `TraceFirePortal` (rich `INVALID_SURFACE`/`CANT_FIT`/`OVERLAP`/
   `FIZZLED` codes already exist). Auto-link via `m_hLinkedPortal`. Cheapest real puzzle verb;
   unblocks `launch`.
5. **C++:** ship DIED / IRRECOVERABLE terminals (gap-4) — read player health + death-cause telemetry
   + respawn policy. MUST precede any `launch`/`ride`.
6. **C++:** ship `launch(via)` — simulate the arc via `AdvanceTicksBlocking` to velocity-stable
   on-ground; `LANDED`/`LANDED_SHORT` with the bare cell. Bisect the settle predicate + jank
   re-attempt budget. The first momentum verb; validate on a hand-built fling chamber, then a
   faith-plate and a gel-bounce chamber (same verb).
7. **C++ recon:** `sar_harness_dump_fields` on `prop_tractor_beam`. Gates `ride(funnel)`.
8. **C++:** ship `ride(funnel)` + funnel-path `interpose` reuse. Validate the direction codes are
   honest (no dishonest `SETTLED` at a mid-air far mouth).
9. **C++ recon + percept:** paint-map read (gels) + projector/volume read (light bridge). Gates
   `paint()` + gel/bridge `navigate` + repulsion `launch`.
10. **C++:** ship `paint`; wire conversion-gel into the `place_portal` portalability predicate (live
    re-read); wire fizzler volumes into the percept + the `FIZZLED`/`FIZZLER_BETWEEN` codes.
11. **Python:** extend the lexer/affordance gate for every new verb, validated pre-execution against
    the live entity list; per-verb typed return-code handling; rejects cost a step (no free probe).
12. **Python:** render `ride`/`launch` arcs + the new puzzle-verb events in the visualizer; add the
    new verbs to the `agentloop_smoke.py` end-to-end gate.
13. **Python:** human-baseline pilot on a funnel + gel+portal + fling chamber. Confirm `ride`/
    `launch`/`place_portal` are natural to drive for a 500h player; tune `ride` direction arg
    (mark vs axis-sign) from the pilot.

---

*Provenance: synthesized from a 26-agent fan-out (7 competing grammar philosophies → adversarial
critique → independent ranking → synthesis → completeness verify), grounded against the shipped
`go_to`/A\*, the laser verb family, the `first_light`/`third_light` trajectories, the element/status
recon, a harness capability inventory, and web recon on Portal-2 traversal mechanics + LLM
action-grammar altitude. Winner backbone `navigate_act_split` (7.5/10); the `launch` unification and
the return-code-honesty fixes are editorial folds of the verify punch-list.*
