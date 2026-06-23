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
> **Everything below is GATED on the interception recon spike** (built:
> `sar_harness_laser_intercept_spike`). Do not write the verbs until Spike 1 passes.

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

## Open risks carried forward

- **Spike 1 is a bimodal bet on one unrun experiment** — the headline "LLM never touches geometry" is
  false until it passes. Run it first.
- **Multi-emitter disambiguation** — `interpose`/`power_with` take the field/target explicitly, which
  resolves it; the routed-lens math wants the >1-emitter airtightness check.
- **Relay vs catcher dwell timing** — a relay can toggle/retrigger; tune the dwell so it doesn't read
  `NOT_POWERED`.
- **Multi-hop beams** (chained cubes) — P1, out of v0; compose as repeated `power_with` calls.
