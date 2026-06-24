# Lasers — the dual-role cube (presses a button AND redirects the beam)

> **Status (2026-06-24):** design from a 12-agent brainstorm (5 code/recon scavengers + 5 lens-diverse
> ideators + synthesizer + adversarial critic), recon-grounded in the real release/button/laser code.
> **P0 recon PASSED** (`sar_harness_dual_seat_spike`) — the seat presses the button and the redirect
> powers the target on a real dual-role chamber; cleared to build (see "P0 RESULTS" below).
> Extends [laser_redirect_verb_design.md](laser_redirect_verb_design.md) (the teleport verb surface) and
> reuses the button-seat from [release_place_on_button_design.md](release_place_on_button_design.md).
>
> **Lead steer (load-bearing):** do **not** reveal the dual purpose to the LLM, and do **not** build a
> named "dual-role" abstraction. The puzzle *is* the LLM realizing one cube serves two goals. The job is
> narrow: make laser-verb placement physically **faithful**, so a cube that lands on a button actually
> depresses it. Everything below serves that, nothing more.

---

## The corner case

One cube must (a) depress a floor button that physically sits on the un-bent emitter ray, **and** (b)
redirect that same beam into a relay/catcher. Observed on a 2-emitter map: `power_with`-style placement
projects a beam point, then down-traces to "rest on whatever's below"
([PuzzleAnnotate.cpp](../src/Features/Harness/PuzzleAnnotate.cpp) intercept-spike, `P.z = endpos.z +
halfH`, no sink). Over a button that trace lands on the button's collision housing and rests the cube
*on top* of it — the cube floats above the plunger, the button never latches, and the cube fails its
second job even though the beam connects.

## Design philosophy

- **No reveal.** No `on_beam` percept field, no "dual-role" concept in the percept or the verb names. The
  LLM sees the ordinary laser verbs plus the ordinary button `pressed` / target `powered` state, and must
  reason that a single cube can do both. Surfacing the insight trivializes the puzzle.
- **No grand abstraction.** No named `SeatDualRole(...)` framework. The fix is faithful physics: when a
  laser verb's placement lands the cube on a cube-accepting button, it depresses it — emergent, unnamed.
- **Faithful placement is the whole job.** A cube placed by any verb on a legal button must depress it
  (no float); a cube re-aimed in place must not lift off the seat.

## Mechanism (minimal, internal)

The position-vs-orient authority split is already structural in the engine's `Seat{origin, angles}`
([MacroExecutor.cpp `ComputeSeat`](../src/Features/Harness/MacroExecutor.cpp)): origin is solved
independently of orientation, `angles = {0, yaw, 0}`.

- **Position authority = the button-seat, *not* the laser down-trace.** When the projected placement
  point overlaps a legal cube-accepting button, position comes from `ComputeSeat`, which sinks the cube
  `kSeatBias=1u` *into* the trigger so bbox-overlap latches the press → **no float**. Otherwise: plain
  floor rest. The laser down-trace is demoted from "owns position" to "classifier: what is under the
  point?".
- **Orient authority = laser, yaw-only.** `yaw = atan2(target − seat.center)`; pitch stays `0` always.
  A yaw rotate about the cube's own axis keeps the footprint over the same X/Y, so the press survives.
  Never tilt pitch to chase a non-coplanar target — yaw-only or fail (`NOT_POWERED`). This is the lead's
  "if pitch would lift the seat, don't change pitch at all," held by construction (no code path writes
  pitch for a seated cube).
- **Reject / NO-OP.** Reject a rest on a dynamic prop (`REST_ON_PROP`). Cleanly NO-OP the press on a
  receptor that won't accept the cube (ball receptor + non-sphere cube) — fall through to plain rest, the
  button is simply not part of the outcome. Acceptance is a tiny `LegalPress(receptorClass, cubeType)`
  table beside the existing button-class helper.

**The faithful signal, not a revealed one.** The button half is read off `cube.m_bActivated` (proves
*this* cube is the presser — immune to a second cube false-positiving the button); the laser half off the
named `point_laser_target.m_bPowered`. These confirm the placement is *honest*; they are not surfaced as
a "you solved the dual role" hint.

**Emergent LLM solve path** (not scripted, not announced): `pick_up cube` → `release cube <button>`
(existing button-seat; the button sits on the beam, so the cube now intercepts) → `redirect_to relay`
(yaw-only in-place re-aim, stays on button + beam, powers the relay). The laser verbs `interpose` /
`power_with` are *also* made button-faithful so they don't float if used over a button, but
release→redirect_to is the clean path. The LLM is given no clue which to use — that reasoning is the eval.

## Verb surface

Three teleport-driven verbs after `release` ([MacroExecutor.cpp dispatch](../src/Features/Harness/MacroExecutor.cpp));
`aim_at` stays camera-only.

- **`interpose <object> <field>`** — positional primitive; place the object on the field's ray, on-ray
  confirm.
- **`redirect_to <target | yaw>`** — yaw-only orient atom on an already-seated cube; never touches origin
  or pitch. Arg is a target mark (yaw = atan2 to it) or a raw world yaw. **Gated to an already-seated
  cube** (raw-yaw on a free cube has no defined position — KISS reject).
- **`power_with <cube> <target>`** — one call = interpose + redirect_to + confirm.

The second argument needs a **dedicated proto field** (`field_mark` / `target_mark`) — `make proto` +
checked-in stub regen, not a reuse of scratch fields.

## Constraint resolutions

1. **Button-aware across the surface, clean NO-OP.** `interpose`/`power_with` use the button-seat when
   their point overlaps a legal button, plain rest otherwise; NO-OP on an illegal receptor via
   `LegalPress`. Not a decomposition-only property.
2. **Pitch never changes.** `angles = {0, yaw, 0}`; no path writes pitch for a seated cube.
3. **Honest dual confirm.** `cube.m_bActivated` AND `point_laser_target.m_bPowered`, both dwell-confirmed
   at the two existing read points (`kSeatSettle`, then `kSeatDwellGap`). Re-read both *after* the yaw
   re-aim so "rotate kept the button pressed" is verified, not assumed.
4. **No floating.** Position via `ComputeSeat`'s sink-into-trigger; the spike's `endpos.z + halfH` (rest
   on top, no sink) is never used over a button. A floated cube never latches `m_bActivated`, so a float
   reports failure instead of false success.
5. **Generalizes structurally, but we are not building the framework.** The `Seat{origin, angles}` split
   means button+funnel would later be a confirm-fn + orient-source swap. We ship button+laser only;
   funnel telemetry is **zero** today, so funnel is aspirational, not near.
6. **Position authority is the button-seat, and the wart is fixed.** Add the missing
   reject-rest-on-dynamic-prop / wrong-normal gate to the laser placement path (it does not exist today —
   the spike trusts the trace blindly).

## Critic's must-fixes (folded in)

- **`CheckFairness` would silently kill *legal* seats.** Its reach / corridor / eye-line gates are
  measured from the **player's** position, but a dual-role cube must seat wherever the beam crosses the
  button — possibly far from the player. Wiring it in verbatim NO-OPs a perfect placement and the LLM
  misreads it as "button rejects cube." **For teleport-seated verbs, drop the player-relative gates;**
  keep only `pressNormal > 0.7` + `seatClear`. Possession is already gated at `pick_up`. Document as a
  deliberate divergence from `release`.
- **`ComputeSeat`'s own down-trace can float too** — it also passes only the player, so it can stop on
  the button housing. Verify with a `seat_check` spike before trusting it for no-float; if it floats,
  switch its filter to pass player + button, and confirm that doesn't then skip through to the floor
  under a raised button (a too-low seat is a different float).
- **Harden the prop-reject classifier against housing-as-world.** If the down-trace endpoint resolves to
  world/null *but* lands within the known button's OBB Z-extent, treat it as the button (button-seat
  authority), not a plain rest — else a housing reported as world brush silently floats the cube.
- **`redirect_to` gets an explicit re-latch hop**, not a "maybe": if the post-rotate read shows the press
  dropped (Teleport relink flicker), do one `kButtonRiseTicks`-style re-seat-and-re-dwell before failing.

## P0 recon — HARD GATE (run before any spine code)

A single in-engine session on a **real dual-role chamber** (button physically on the un-bent beam +
downstream catcher — the 2-emitter map plus a button on the ray). Two must-clears; if either fails the
design as written does not ship:

1. **Button-center vs the ±24u beam envelope.** Measure the perpendicular distance from the button
   trigger midpoint to the un-bent emitter ray. `ComputeSeat` centers the cube on the button; if that is
   >±24u off the ray, seating on the button puts the cube *outside* the beam capture radius and the two
   goals are unsatisfiable by one rigid seat. **If buttons commonly sit off-ray, the premise is dead.**
2. **Yaw-only re-seat keeps the press.** Read `cube.m_bActivated` immediately after a yaw-only
   `SeatEntity` (same origin, new `angles.y`) and again after the dwell, to confirm the Teleport relink
   does not drop `EndTouch→StartTouch` for a tick.

Plus the cheaper reads in the same session: does `ComputeSeat` itself float on a real `prop_floor_button`
(seat-check); does `point_laser_target.m_bPowered` latch on the seat dwell timeline or need its own
settle; does `HeldCube()->field<int>("m_nCubeType")` return sane values (it is datamap-only and unread
anywhere today); is the un-bent emitter→cube segment the correct line to test the button against.

## P0 RESULTS (2026-06-24) — PASSED on a real dual-role chamber

Run via `sar_harness_dual_seat_spike <emitter> <target> <cube> <button>` (MacroExecutor.cpp recon command:
ComputeSeat-seat + yaw-only aim + geometry dump) on a 2-emitter workshop chamber whose floor button sits
on emitter 8's un-bent ray.

- **Premise holds.** Button trigger centre is `lateral 0.0u` from the valid emitter's ray; the off-ray
  emitter is correctly flagged (`384u OFF-RAY`). One rigid seat satisfies both goals.
- **Seat presses AND redirect powers — both targets** (catcher and relay): `m_bButtonState` /
  `m_bActivated` and `point_laser_target.m_bPowered` all latch. Seat-Z was correct all along
  (`surface 21.68` is the real press plane, not the housing — no `TraceSkip2` fix needed for this button).
- **NEVER FREEZE THE CUBE.** A `MOVETYPE_NONE` pin (added to "stop drift") turned the cube into a static
  prop: the button trigger stopped counting it (no press) *and* it became un-grabbable (`+use` did
  nothing). The pin, not the seat, caused the apparent "floating, won't press" failure. The cube must stay
  a live moveable.
- **Two read gotchas (both real, both handled by the verb path):** the press is a sim-tick `StartTouch` —
  *not* readable on the teleport tick; a console command can't `AdvanceTicks`, so the spike reads at t=0
  and you must re-read via `dump_fields` / `laser_probe` after. The real verb advances ticks, so it reads
  the latched state directly.
- **~2% settling jank.** A free awake cube occasionally rotates a few degrees extra after landing,
  throwing the +X aim past the ~1–3° tolerance so the target goes dark (button still pressed). Handled by
  the confirm-and-correct loop in P5 — *not* by freezing.

→ **Cleared to build P1–P8.**

## Phased plan (C++ first, many small steps — after P0 clears)

- **P1 — held `m_nCubeType` read.** Confirm the datamap read works for a carried cube; log it from an
  existing verb. (Built *before* `LegalPress` — its acceptance side depends on this read.)
- **P2 — `LegalPress(receptorClass, cubeType)`** table beside the button-class helper; pure function.
- **P3 — placement classifier.** Demote the down-trace to "what is under the point?": legal button →
  `ComputeSeat`; illegal button / dynamic prop → reject code; world floor (good normal) → plain rest.
  Add the housing-as-world hardening. Cover with a temporary console command echoing the decision.
- **P4 — placement spine.** Reuse `release`'s `DropHeld → RunOnMainThreadSync{seat; fairness(divergent);
  SeatEntity} → two-read dwell`, with a `angles.y` override and both confirm predicates. Leave `release`
  on its existing single-field path (minimize blast radius).
- **P5 — `redirect_to`** (yaw-only atom) with the **confirm-and-correct loop** (P0 found ~2% settling
  jank): `for attempt in 1..N (~3): seat(button_center, yaw=atan2(target−seat_center)); settle; if
  m_bActivated && m_bPowered -> POWERED; if not intercepting -> NOT_INTERCEPTING`. After the loop ->
  NOT_POWERED (on the ray but dark after N = a real aim miss, not jank). The correction *is re-applying
  the known-good seat* — the jank is random vphysics settling, so each re-seat re-rolls it (2% → ~1e-5
  after 3); no hill-climb needed, and re-seating re-centres any xy drift and keeps the press. Re-read both
  predicates every attempt (a re-seat flickers `EndTouch→StartTouch`; read after the settle). This
  subsumes the re-latch hop. The cap keeps a genuinely impossible target failing fast instead of fiddling.
  Distinct codes: POWERED / NOT_POWERED / NOT_INTERCEPTING / NO_OBJECT. (Optional later: zero *angular*
  velocity post-seat to kill the jank at source — SeatEntity only zeros linear — but needs physics-object
  access, and the loop is robust without it. Do NOT freeze/`MOVETYPE_NONE` — P0 proved that breaks the
  trigger and the grab.)
- **P6 — `interpose`**; **P7 — `power_with`** (= interpose + redirect_to, one result).
- **P8 — proto + smoke.** Dedicated second mark field, `make proto`, rebuild, extend
  [agentloop_smoke.py](../py/agentloop_smoke.py) to assert the result codes end-to-end.

**No percept change.** The dual-role conflict stays invisible in the percept by design; the only feedback
is the ordinary button `pressed` / target `powered` state and the verb result codes. Discovering the dual
purpose is the eval.
