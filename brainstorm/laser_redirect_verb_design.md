# Lasers — redirect-cube verb design (multi-agent synthesis + phased plan)

> **Status (2026-06-23):** synthesis of a 12-agent brainstorm — 5 lens-diverse verb
> ideators + 5 code/recon scavengers + a synthesizer + an adversarial critic. This is the
> authoritative **verb design**; it supersedes the verb sketch in §B.2–B.6 of
> [laser_percept_and_aim_design.md](laser_percept_and_aim_design.md), which remains the record
> of the **L0 recon results (§B.1) and the redirect mechanics (§B.0)** — read it for the empirical
> facts this design rests on. Reuses the teleport spine of
> [release_place_on_button_design.md](release_place_on_button_design.md); the field findings live in
> [status_field_recon.md](status_field_recon.md).
>
> **Locked decisions:** (1) percept marks **`point_laser_target` only** — no catcher resolver;
> (2) actuator is **teleport-primary**, held-aim demoted to a documented oracle; (3) verb surface =
> **`interpose` + `power_with` + `redirect_to`** (all teleport-driven), **`aim_at` stays camera-only**;
> (4) **laser-first**, funnels generalize via `interpose` later.
> **Spike 1 PASSED** (2026-06-23) — computed-point teleport interception validated. See the handoff
> block below for current status + the independent-vs-frontier next directions.

---

## Status & next directions (handoff — 2026-06-26)

**SHIPPED end-to-end — a full PeTI laser chamber SOLVED verb-only** (`go_to`/`pick_up`/`release`/
`interpose`/`redirect_to`, no raw geometry from the driver). macro_repl + **agentloop_smoke 13/13**.
Commits `866ff50f` + `278c51cc` on yeeh (UNPUSHED). The verb surface (`interpose` / `redirect_to` /
`power_with`(=`target=`) / `release`) and the `go_to` nav it needs are built; the robustness chain lives in
`MacroExecutor.cpp` + `GoToPlanner.cpp`. Full per-fix detail: [[laser-l0-recon-and-held-aim]] memory + ROADMAP
2026-06-26.

**Built on the FLAT planner, NOT the stepped-floor extension.** The 2026-06-25 reachability recon
(`sar_harness_laser_reachability_test`) showed flat 2.5D `GoToPlanner` judges reachability correctly across
the tested chambers (no floorZ-seam blocker needed for that envelope). So "extend `GoToPlanner` (stepped-floor
+ portal edges)" below is **deferred, off the critical path** — revisit only for stacked floors / portal-
bridged islands / a sub-128u goo moat (the seam guard is still the right fix there).

**The hard-won robustness chain:** FreeGrab clear-air drop (a steep-down drop wedges the cube into the floor,
+use won't release) · interpose player-displacement **perpendicular to the beam, rejecting on-beam bearings**
(else the displaced player occludes the beam) · release proximity-displace + drift-confirm + honest
no-standoff · redirect_to reach gate (`OUT_OF_REACH`) · go_to footprint-overlap-skip (reach a cube-on-button)
+ pushable-target arrival standoff + velocity-zero-on-arrival.

**NEXT (highest-value first):**
1. **⭐ Run the FROZEN-LLM eval on the laser chamber** ("laser light") — verbs/percept are ready; this is the
   reasoning signal (M3 analogue of first/third light). Gates everything below (does the model compose
   `go_to`+`redirect_to` on `OUT_OF_REACH`, or one-shot `power_with`?).
2. **Increments as the eval exposes gaps:** dual-role `at=@button` placement · `IN_HAZARD` point-contents goo
   check (`NO_FLOOR` misses slime) · power_with-one-shot vs `interpose`+`redirect_to` decompose ablation ·
   multi-emitter incoming-independence.

The 2026-06-24 plan below ("Part B verbs depend on the reachability layer, build after") is **executed** —
kept for the design rationale (the fairness spec + verb synthesis remain the authoritative design).

### Earlier handoff (2026-06-24 — superseded by the above, kept for rationale)

**Done:** L0 recon closed; `sar_harness_laser_intercept_spike` PROVED computed-point teleport
interception (down-trace rest, ±24u capture radius, no drift, no freeze). Verb design + the
fairness/legality spec are brainstormed (this doc). **Decision: invest in fuller reachability** — extend
`GoToPlanner` (stepped-floor + portal edges) rather than scope lasers to flat/true-void chambers.
**Part A (percept) SHIPPED (2026-06-24)** + the spike mid-air-on-miss fix — see below.

**SHIPPED 2026-06-24 (Part A + spike fix; builds clean, percept smoke 6/6):**
- **Part A percept:** dropped `prop_laser_catcher`/`prop_laser_relay` from `kClassColors`; added the
  per-entity `IsHarnessMarkedEntity` gate (PuzzleAnnotate) — the **transform-sane discriminator**
  (reject `env_portal_laser` at origin (0,0,0)), **not** named-only per fairness F2, so an unnamed
  re-emitter mid-chain stays addressable. Wired into MarkTable (percept) + the RENDER loop (overlay).
  Python `_project_state` → `point_laser_target {powered}`; asserted in `percept_grammar_smoke.py`.
- **Spike mid-air-on-miss fix:** `sar_harness_laser_intercept_spike` now rejects with `NO_FLOOR` (pit/void)
  instead of placing the cube at beam height.
- **Pending in-engine visual check (next game session):** percept shows `point_laser_target {powered}`,
  no catcher/relay marks, no transient (0,0,0) segment marks, beam holds after a redirect.

**Independent — a fresh session can pick up ANY of these now (no dependency):**
- **2-emitter incoming-independence** rerun of the intercept spike — **now unblocked** (2-emitter map in
  hand). Confirms the +X re-emit is incoming-independent beyond L0's single-direction `dot=1.000`.
- **Reachability recon** `sar_harness_laser_reachability_test`: prove the gap produces BLOCKED cells at
  the player's *current* refZ + profile per-verb `Plan()` cost. Gates the fairness layer. (§ Fairness.)

**The chosen frontier — extend `GoToPlanner` (gates the verbs' fairness layer; also upgrades `go_to`):**
stepped-floor reachability (per-cell `floorZ`-seam, multi-refZ) + a **portal-teleport edge**
(`m_hLinkedPortal` is never consulted today); z-anchor `FindPlayerStandoff` at the cube floor. Needs its
own recon/validation on stacked + portal-bridged chambers. → § Fairness (load-bearing correction).

**The verbs (Part B)** depend on the reachability layer + the recon — build after.

---

## The problem & the principle

Redirecting a laser with a reflector cube is **two coupled continuous-geometry subproblems**:
**(1) position** the cube so it intercepts the incoming beam, and **(2) orient** it so its local +X
re-emit axis hits the target. If a verb only does (2) and makes the LLM walk the cube into the beam
(the "orient-in-place" v0), we re-introduce **the first-light confound**: the model fiddles with
continuous geometry (walk a bit → did it catch? → walk more → re-aim), which conflates "can't reason"
with "can't aim/move", burns tokens, and adds episode noise. Robustifying `go_to` (internal A*) and
`release` (internal teleport-seat + confirm) is exactly what made third light solve in 15 steps
(686k→286k tokens).

**The principle:** a verb **owns the continuous geometry**; the LLM supplies only **discrete intent**
(which payload, which target). This is the load-bearing constraint that ranks every candidate below.

---

## Multi-agent synthesis

### The five candidates

| # | Design | Verb surface | Critic |
|---|---|---|---|
| **P1** | `power_target` — single deterministic oracle | `power_target <cube> <target>` solves full SE(3) pose + teleports + confirm-retry | 7.5 keep |
| **P2** | position/orient **split** | `carry_into <field>` + `redirect_to <target>`; `aim_at` stays camera | **8 keep** |
| **P3** | `put` unified grammar | `put <object> on <target>` (class-dispatched position/orient/predicate triple) | 5 — defer |
| **P4** | `aim_at` polymorphic | **zero new verbs** — overload `aim_at` on held-state | 4.5 — kill |
| **P5** | primitive + goal verb | `interpose <object> <field>` + `power_with <cube> <target>` | **8 keep** |

### Critic ranking & verdicts — `P2 ≈ P5 > P1 > P3 > P4`

- **Kill P4.** Overloading `aim_at` on hidden held-state manufactures a **state-dependent-semantics
  confound** — a frozen LLM that loses track of "am I holding a reflector?" aims the camera when it
  meant the cube: the *same class* of reason-vs-control conflation the eval exists to eliminate. Plus
  the pending-orientation-across-tick-batches plumbing is the trickiest new mechanism in the set (the
  "view isn't held across ticks" lesson is a verified landmine).
- **Defer P3.** Fusing the **working, third-light-validated `pick_up`/`release` pair** into `put` is a
  long-term-investment move masquerading as a get-results phase. Its own safe staging ("ship `put` for
  catcher/funnel first, keep `pick_up`/`release`") just *is* P2/P5.
- **P1 (7.5).** Cleanest single verb, but concentrates every failure into one opaque
  `NOT_POWERED` — the model can't decompose a partial success (positioned-but-misaimed).
- **P2 ≈ P5 (8).** Near-duplicates: **P5's `interpose` ≡ P2's `carry_into`**. The only real difference
  is whether you *also* expose a convenience goal verb (`power_with`) on top of the positional
  primitive. The critic's single best ergonomic insight: **`interpose` as a standalone primitive gives
  the model a "reason-about-it" fallback** instead of an opaque failure — *and it is the funnel verb
  verbatim* ("get payload into field" is purely positional, zero laser-specific work to lift).

### The five blind spots (every proposal under-weighted these)

1. **The confirm-read is not a "two-field swap."** `release` reads its predicate off the *held cube*
   (`m_bActivated`); a laser confirm reads `m_bPowered` off a *different* entity.
   **→ Dissolved by our "mark the target" decision:** the agent names `point_laser_target` directly,
   so the verb reads `m_bPowered` off *that* named mark — no `m_hMoveParent` resolver, no catcher↔child
   join, no Python merge. (The brainstorm assumed we'd mark the catcher; we don't.)
2. **How `m_bPowered` reaches the percept.** Same dissolution — `_project_state` keys on the marked
   `point_laser_target`'s own field; no cross-mark join needed.
3. **The segment-mark bug bites immediately.** `env_portal_laser` is unconditionally marked, so the
   instant a beam redirects, transient nameless (0,0,0) segments spawn marks that **corrupt the percept
   the LLM reasons over — before any verb runs.** One-line `targetname` gate fixes it (Spike 2).
4. **`go_to` straight-marches.** If Spike 1 fails and we fall to orient-in-place, carrying the cube
   around an obstacle re-introduces the move-10/turn/move-5 fiddle. The orient-in-place fallback is
   *more* confounding than admitted.
5. **Lethal-beam vs the fairness reach gate.** `CheckFairness`'s reach gate requires the player be
   within arm's reach of the seat; a computed in-beam point may be unreachable, or reaching it walks
   the player into the killing beam. **Resolution:** gate *possession* at `pick_up` (its own reach
   check), then teleport-place with **no second reach gate** (mirrors `release`'s "must be holding"),
   and replace the reach check with geometric validity (in-line + out-line + rest traces).

### Resolved verb surface — all three, all teleport-driven

`aim_at` **stays strictly camera-only** (rejecting P4's overload). Built on one shared teleport spine:

- **`interpose <object> <field>`** — positional primitive. Teleport the object onto the field's ray
  (emitter for laser, tractor beam for funnel) at a reachable interception point; confirm it's on the
  ray. **This is the funnel verb verbatim** and the honest fallback ("interpose, then reason").
- **`power_with <cube> <target>`** — one-call convenience = `interpose` + closed-form aim-solve +
  `m_bPowered` confirm. Fewest tokens / lowest branching for the common case.
- **`redirect_to <target>`** — the orient atom: re-teleport the intercepting cube in place to
  +X→target, confirm. (Rotating about the center keeps it on the ray, so "stay in the beam" is
  automatic.)

Shipping **all three** (the user's call) gives the convenience path *and* a built-in mini-ablation:
we observe whether the model one-shots via `power_with` or decomposes via `interpose`+`redirect_to`.

---

## Recon-first (gating — run before writing any verb)

Launching the game is the expensive step, so one in-engine session answers everything:

- **Spike 1 — BLOCKING.** `sar_harness_laser_intercept_spike <emitter> <target> <t> [lateral]`
  (built, [PuzzleAnnotate.cpp](../src/Features/Harness/PuzzleAnnotate.cpp)): teleport a free reflector
  cube to a **computed** point on the emitter ray (not pre-walked), aimed +X at the target; read
  `m_bPowered`. **If it flips, interception = pure hull-overlaps-ray** (no contact/facing constraint) →
  the whole teleport surface (P1/P2/P5) is real. If not, everything degrades to position-leaking
  orient-in-place and we rethink. Sweep **`t`** (envelope along the beam) + **`lateral`** (perpendicular
  capture radius → the positional tolerance the solver must hit; complements L0's ~1–3° angular).
- **Spike 2 — cheap percept fix (regardless of verb choice).** One-line named-emitter-only mark gate so
  transient segments stop polluting the percept.
- **Multi-emitter / incoming-independence.** Rerun Spike 1 with a *second* emitter index (cube
  intercepts a beam from a different direction, +X aimed at the same target). If it still powers, the
  routed-lens (+X, incoming-independent) model is airtight, not just L0's single-direction `dot=1.000`.

### Recon RESULTS (2026-06-23) — Spike 1 PASSED, placement strategy LOCKED

Run on the single-emitter chamber via `sar_harness_laser_intercept_spike <emitter> <target> <cube> [t] [lateral]`:

- **Interception is pure hull-overlaps-ray.** A free reflector cube teleported to a *computed* point
  on the emitter ray (no pre-walk), aimed +X at the target, **powers `m_bPowered`** — across the whole
  mid-beam envelope (t=200/300/388). No contact/grab/facing precondition.
- **Placement = down-trace rest, NOT mid-air drop.** A cube dropped at beam height while *awake* falls
  and **tumbles its yaw ~3.6°** (past tolerance → misses); an *asleep* cube **levitates** (a vphysics
  sleep artifact, not reliable). The fix that works cleanly: **down-trace to the floor and rest the
  cube** (center z≈18) at the **closed-form yaw** — no fall, no drift. A floor-resting cube **powers a
  beam-height (z=32) catcher** because the redirect exits at the beam-interception height within the
  hull, not at the cube center. So the verb **never needs to elevate or freeze** the cube.
- **Positional tolerance ≈ ±24u** capture radius perpendicular to the beam (±24 powered, ±25 flicker,
  ±26+ dead) — generous; any "project onto the beam ray" placement lands well inside it.
- **Angular tolerance ~1–3°** (L0); rest placement holds the commanded yaw exactly, so it's a non-issue.
- **Still open (non-blocking):** the 2-emitter incoming-independence rerun (single-emitter map so far;
  L0 `dot=1.000` strongly implies routed-lens +X).

**→ The teleport verb surface is validated. Build Part A + Part B.** The shared spine is: resolve →
down-trace-rest the cube on the beam ray at closed-form yaw (within ±24u) → settle → read `m_bPowered`
off the named target mark → result.

---

## Phased implementation plan (post-Spike-1, many-small, C++-before-Python)

**Phase 0 — recon (gating).** Run Spike 1 + Spike 2 + the multi-emitter rerun. Record the interception
envelope, capture radius, and incoming-independence. **Decision gate:** if interception is pure
hull-overlap, proceed; else rethink positioning before any verb.

**Part A — percept (C++ → Python).** *(independent of the verbs; can land first)*
- **A1 (C++):** drop `prop_laser_catcher` + `prop_laser_relay` from `kClassColors`; keep
  `point_laser_target` (carries `m_bPowered` natively). Named-emitter-only filter for `env_portal_laser`
  (= Spike 2).
- **A2 (Python):** `_project_state` → `point_laser_target` returns `{'powered': bool(...)}`.

**Part B — verbs (C++ → Python).** *Shared spine first, then the three verbs.*
- **B1 (C++):** the shared teleport positioner — ray-intercept point solve (emitter ray, reachable,
  in-line + out-line clear), reusing `SeatEntity` + the `Release` settle→read→dwell→result skeleton.
  Fairness = possession-gated at `pick_up`, no seat-reach gate; geometric-validity traces instead.
- **B2 (C++):** `interpose <object> <field>` — position + on-ray confirm (`IN_FIELD` / `NOT_IN_FIELD`).
- **B3 (C++):** `redirect_to <target>` — closed-form +X→target rotation (`yaw=atan2` for horizontal),
  re-teleport in place, dwell-confirm `m_bPowered` read off the **named target mark** (no resolver),
  with a ±small confirm-nudge for the ~1–3° tolerance on a **predicted-exit surrogate** (never the
  binary bit, never the bogus segment). `POWERED` / `NOT_POWERED` / `NOT_INTERCEPTING`.
- **B4 (C++):** `power_with <cube> <target>` = `interpose` + B3's aim-solve + confirm, one call.
- **B5 (C++):** held-aim oracle (optional) — the eval-faithful cross-check, kept behind a flag.

**Part C — grammar + gate (Python).**
- **C1:** `VERB_SPECS` entries for `interpose` / `power_with` / `redirect_to` + `build_macro` +
  `_SLOW_VERBS`.
- **C2:** update [agentloop_smoke.py](../py/agentloop_smoke.py) for the new fields + verbs; documented
  manual visual check (target shows `powered`, no catcher/segment marks, beam holds after placement).

**Funnel-lift (deferred).** `interpose` is the funnel verb verbatim — `prop_tractor_beam` needs only
the confirm swapped to point-in-volume. **Zero tractor telemetry exists today**; recon it as a separate
pass (direction field, OBB-vs-trigger volume, on/off state, carry-through) when funnels come up.

---

## Fairness / legality of the placement (14-agent synthesis + adversarial corrections, 2026-06-23)

A brainstorm (5 fairness ideators + 5 reachability/code scavengers + synthesizer + 3 adversarial
critics) on *when is a teleport-placement legal?* **The philosophy survived; the feasibility story did
not** — the corrections below are folded in (the rosy "structurally impossible" synthesis is wrong).

### The contract (sound)

The legal placement set must equal **what the player could currently do by hand**: hold the cube
(gated at `pick_up`), walk to a spot reachable *from their current position*, set it down. So legality
= **possession + reachability**, not geometry alone. The verb samples points along the beam, keeps the
**reachable + physically-valid** ones, places at the cheapest, else fails gracefully. The LLM supplies
intent (which target); the verb owns the geometry. A cube can never land on an island the player can't
currently walk to — **iff reachability is judged correctly (see the load-bearing correction).**

### Conditions (corrected; status = HAVE / BUILD / RECON)

| id | condition | sev | status |
|----|-----------|-----|--------|
| F1 | possession-gated (holding the reflector); **no** seat-reach gate (`pick_up` owns reach) | blocking | HAVE |
| F2 | ray read only from a **transform-sane** emitter (reject origin==(0,0,0)/identity) — *not* "named-only" (a legit unnamed re-emitter in a chain must still address) | blocking | HAVE (fix discriminator) |
| F3 | beam on (`m_bLaserOn`) + in-line clear E→P; `t` bounded by an **open forward ray** E→firstWorldHit | blocking | **BUILD** (that forward ray doesn't exist; spike only does point-to-point) |
| F4 | down-trace rest: solid up-facing floor (normalZ>0.7), supported; **reject on miss** (pit/void) | blocking | HAVE (+ fix the spike leaving P mid-air on a miss) |
| F5 | **reachable**: a walkable cell *touching P's footprint* is A*-reachable from current feet | blocking | **BUILD + RECON** (the hardest gate; see below) |
| F6 | interception: the **settled** cube hull overlaps the ray (re-trace, not pre-placement math) | blocking | BUILD |
| F7 | out-line clear cube→target (hard gate for `power_with`/`redirect_to`; informational for `interpose`) | soft/gate | HAVE |
| F8 | seat clear / not in hazard / not in wall — but **flush-against-world is legal** (don't auto-reject startsolid-vs-brush; epsilon-deflate) | blocking | HAVE (+ rewire filter: no button to pass) |
| F9 | player not forced to **sustain** lethal-beam occupancy — gate the **transit**, not a frozen terminal standoff | soft | BUILD |
| F10 | dwell-confirm `m_bPowered` **read directly off the marked `point_laser_target`** (no `m_hMoveParent` follow — that's display-only) | blocking | HAVE |
| F11 | **cube-carry survives the route**: sweep the carried-cube hull along the A* path through fizzler/laserfield; reject if it crosses one (the planner models the *player*, never the carried cube) | blocking | BUILD (new) |

### Load-bearing correction — "2-island handled by construction" is FALSE as written

`GoToPlanner` is **single-refZ, flat 2.5D**: built from the player's *current feet*, it floor-probes
each cell in `[refZ+40 … refZ−128]` and marks BLOCKED only on a down-trace **miss** in that window. So
it's a *"floor >128u below my feet"* detector, **not a gap detector**:

- ✅ **Safe** when the gap is a true void >128u deep and the far island is near the player's z.
- ❌ **FALSE-LEGALIZES** a *shallow* gap (goo moat, lower walkway, ~100u chasm with floor beneath,
  light-bridge gap): those cells read WALKABLE, A* routes across, the cube lands on the "unreachable"
  island → **the exact trivialization the verb exists to prevent.**
- ❌ **FALSE-REJECTS** a legal near placement whose beam floor steps below/above the player (>40u up /
  >128u down) — both the planner *and* `FindPlayerStandoff` anchor to the player's feet z, not P's.

**Corrections promoted to BLOCKING (were "deferred"):**
1. **floorZ-seam check** — reject any A* leg whose two cells' `floorZ` differ by more than `kStepDownMax`,
   and gate the chosen stand-cell `floorZ` against the rested-point floor (refuses a shallow-gap cross).
2. **z-anchor the standoff search at P's floor**, not player feet (a rewrite of `FindPlayerStandoff`,
   not a rename) — and use the same z-window the planner uses so the two halves agree.
3. **Reachability frontier (CHOSEN investment, 2026-06-23):** rather than scope lasers to flat/true-void
   chambers, **extend `GoToPlanner`** — stepped-floor reachability (per-cell `floorZ`-seam, multi-refZ)
   + a **portal-teleport edge** (`m_hLinkedPortal` is never consulted today), and z-anchor
   `FindPlayerStandoff` at the cube floor. This is the gating workstream for the fairness layer and it
   *also upgrades `go_to`* (height changes + portals). Needs its own recon/validation on stacked +
   portal-bridged chambers. The `floorZ`-seam check is a blocking safety gate regardless (it's what
   refuses the shallow-gap false-legalize).

### Perf is the real blocker

~1000u beam / 16u ≈ 60 candidates; each survivor = a standoff ring (~32 traces) + an A* `Plan()`
(≤400 cells, each a hull-sweep), on the **main thread in one `PRE_TICK`**. The lazy grid caches the
grid, **not** the standoff rings or per-goal A*. Worst case = tens of thousands of traces/verb → stall.
**Mandatory:** one `RunOnMainThreadSync` that builds the planner and loops *all* candidates inside it
(cross the boundary once, not N×); and **profile** before trusting any reachability claim. If it doesn't
fit the budget → cap candidates and **document** that the cap can produce false `NOT_REACHABLE`.

### Recon gate — `sar_harness_laser_reachability_test` (run before writing the verb)

Per candidate far-P, print: (a) does `Probe` mark the gap cells BLOCKED **at the player's current
refZ**, and (b) **why** (down-trace miss vs hull-startsolid vs obstacle stamp) — *prove* the gap severs
reachability, don't assume it. Plus: reconcile MASK_OPAQUE (beam) vs MASK_PLAYERSOLID (floor / planner /
standoff) on glass/grate; confirm `env_portal_laser` is **mark-addressable in the percept path** (not
just recon-readable); the multi-emitter incoming-independence rerun; the goo/water class (CONTENTS_SLIME
is outside MASK_PLAYERSOLID → a cube can be seated submerged undetected); `kPlanMaxCells=400` horizon vs
real beam-walk distances.

### Failure reporting (so the LLM reasons, never fiddles)

Distinct codes the model can act on: `NOT_HOLDING`, `NO_BEAM` / `BEAM_BLOCKED`, `NO_FLOOR`,
`NOT_REACHABLE` (with the reachable beam fraction — "bridge to the far island first"), `NOT_INTERCEPTING`,
`OUT_LINE_BLOCKED` (blocker class), `PLAYER_IN_BEAM`, `NOT_POWERED`. `NOT_REACHABLE` (gap) must be
distinguishable from the `kPlanMaxCells` horizon (long-but-walkable) so the LLM doesn't misread it.

### Over-restrictions — fix vs accept

- **Fix:** flush-against-wall reject (F8); the F1↔F5 reach-anchor contradiction (require a route to a
  cell *touching* P's footprint — drop the 96u ring radius F5 silently re-imposed); F9 frozen-player
  (gate transit, not terminal standoff).
- **Accept + document (v0):** portal-bridged islands refused (under-permit — common on Portal 2, a real
  limitation); elevated / ledge / stacked placements refused (floor-only v0, `NOT_INTERCEPTING`);
  `kPlanMaxCells` horizon on very large chambers.

### Bottom line

Build order shifts: **Part A (percept) and the basic placement spine are still go**, but the
**reachability/fairness layer (F5 + F11 + the floorZ-seam + standoff z-anchor) is gated on the
`sar_harness_laser_reachability_test` recon + a perf profile**. Per the 2026-06-23 decision we **invest
in extending `GoToPlanner`** (stepped-floor + portal edges) so the verbs aren't limited to flat/true-void
chambers — that reachability work is the critical path for Part B, and it pays off for `go_to` too.

---

## Open risks carried forward

- **Spike 1 is a bimodal bet on one unrun experiment** — the headline "LLM never touches geometry" is
  false until it passes. Run it first.
- **Multi-emitter disambiguation** — `interpose`/`power_with` take the field/target explicitly, which
  resolves it; the routed-lens math wants the >1-emitter airtightness check.
- **Relay vs catcher dwell timing** — a relay can toggle/retrigger; tune the dwell so it doesn't read
  `NOT_POWERED`.
- **Multi-hop beams** (chained cubes) — P1, out of v0; compose as repeated `power_with` calls.
