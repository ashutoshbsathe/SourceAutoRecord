# Verb Grammar — Genie Harness + Relational Point Algebra (BRAINSTORM SEED)

> **Seed for the next brainstorm session** (planned: let ALL agents brainstorm the verb-grammar
> refactor). Crystallized from the 2026-07-21 design conversation. It **extends** postmortem §6
> (unified point algebra), the portal grammar dossier, and `verb_grammar_rethink.md` — read those
> for substrate, but where this doc touches them, this supersedes.

## Context — why this exists

The run-2 azorae postmortem flipped the failure mode from actuation → reasoning (55 ok /
40 reasoning / 3 percept / 2 harness / 0 grammar). The open thread the user stalled on
(pre-Elden-Ring) was the verb-grammar *rethink* — postmortem §6's "unified point algebra,"
which was marked design/unimplemented. This session **resolved the altitude dilemma** and
extended §6 into a full philosophy. Tomorrow = design the concrete grammar on top of it.
**Framing (user): the machinery is already right — tomorrow is WRAP-UP + cleanup of the sharp
edges / remaining clutter, NOT a greenfield rebuild.**

## The one-line direction

Refactor into **(1) a small, dumb VERB surface** over **(2) a rich, compositional RELATIONAL
POINT ALGEBRA**, executed by **(3) a "literal genie" harness** that grants the exact *spatial*
wish, never the *intent* behind it.

## The claim this whole thing serves

> **SOTA models are BAD at spatial reasoning EVEN WITH perfect actuation; more investment is
> needed to make them good (excellent = a distant goal).**

The harness's entire job is to make actuation so perfect that the residual failure is
*unambiguously* reasoning. Every design call below is judged against "does this keep the
residual attributable to reasoning?"

---

## ★ THE decision of the session (resolves the pre-Elden-Ring dilemma) ★

Earlier framing was "pick an altitude and *restrict* the algebra so it can't name the goal
(else it's a puzzle-solver)." **That is thrown out.** Replaced with:

> **Let the algebra name anything. The harness just NEVER closes the wish→intent loop.**

The harness is a **literal genie / faithful servant**. It grants the exact *spatial* wish
(best-effort), reports honestly what it did, and then **stops**. It does **not** verify
goal-achievement, does **not** diagnose why a wish failed, does **not** forward-simulate
consequences, does **not** protect the model from legal-but-dumb wishes.

This dissolves the "solver-smuggle" worry: `collinear(receiver 16, Pb, cube 15)` is **not**
a solver, because the harness satisfies the *literal spatial constraint* but never checks the
beam actually reaches 16, never compensates the z-shift through the portal, never says "this
won't work." The model still must: form the right wish → observe the result → diagnose the
gap → re-wish. The gap between "I got exactly what I asked for" and "the door didn't open" **is
the eval.** Maximally expressive algebra AND airtight reasoning claim, simultaneously.

---

## LOCKED this session (build ON these; don't re-litigate)

**The factoring (resolves "simpler" vs "richer").**
- Simple *verb* surface (`interpose`, `redirect_to`, `place_portal`, …) — addresses the
  "worried about COMPLEX verbs" fear.
- Rich *target language* (the relational point algebra) — but resolved in ONE shared
  `ResolveTarget` choke. Torvalds "delete the special case": special-case verbs dissolve into
  one point expression. Complexity concentrated + factored, not smeared.

**The relational point algebra ("PROJECT" family).** Address a point by geometric
*relationship*, not raw coordinate: projection (foot of P onto a laser), angle (`∠pPx = θ`),
collinearity, collinearity-**through-a-portal**. References may be any addressable point
(surface point, cube, portal, beam). **Elements are addressable LOCI**: laser = 1-D ray/polyline,
panel = 2-D patch, portal = oriented disc/point, cube = OBB, funnel = 1-D axis, button = point.
The algebra composes points over loci via constraint constructors — the general form of
§6's `N@f` / `Sn@u,v`. Not laser-specific; ALL elements.

**Expressive about EXECUTION, silent about DIAGNOSIS.** The genie's honest outputs:
1. *"Here's what I did"* — achieved pose, with any clamp/tolerance/**RESIDUAL** made visible.
2. *"Here's what stopped me"* — rich EXECUTION-failure taxonomy only: unreachable,
   fizzler-in-the-ray/path, no-floor-under-point, out-of-bounds, **no-point-satisfies-constraint
   (0-solution)**, **constraint-ambiguous (>1 solution)**.
3. *raw observable world-state* — is 16 powered? button pressed? cube where? (neutral facts).
4. **HARD SILENCE** — never "why your wish won't achieve your goal."

**The execution-report vs strategy-diagnosis line (the fizzler sits ON it).** "Fizzler in the
ray I traced" = fact about the *execution attempt* → report. "Fizzler between you and the button
so your plan fails" = verdict on the *strategy* → silence. Audit EVERY error code:
*fact-about-the-attempt* (ships) vs *verdict-on-the-strategy* (never).

**Cautious-parent vs faithful-servant, disambiguated (else they contradict).**
- Cautious about **executability** — never fakes reaching/pressing; honest about what it
  mechanically can't do. (can't-reach-button → honest "couldn't")
- Literal about **consequences** — executes legal-but-doomed wishes without warning.
  (walk-cube-through-fizzler → do it, cube dissolves, model observes)
- Parent about *"can I do this?"*, servant about *"should you want this?"*.

**Legibility (design-time) vs Foresight (runtime) — the distinction that resolves the "let it
shoot blind" pushback.**
- **Legibility = experimenter's design-time job.** The fizzler must be *represented somewhere*
  in the observation ("don't hide the ball"). If it's invisible, failure is a perception
  confound and "bad at spatial reasoning" degrades to "fails when you withhold info" (trivial).
- **Foresight = model's runtime job.** Attend + forward-simulate. The genie NEVER warns or
  forward-sims. **Let it shoot blind and get smacked** — that IS the reasoning test.
- Shoot blind because it didn't look = fair smack. Shoot blind because the ball was hidden =
  rigged test. Don't stop before the fizzler; DO make the fizzler legible.
- **USER RULING: do NOT add "stop before the hazard" rails.** Parenting launders blindness into
  the harness and deletes the signal.

**But the smack must be OBSERVABLE, not silent.** Report the terminal (cube dissolved → empty
hands in percept; player died → `DIED` terminal, not silent respawn + teleported position).
Silent smack = gaslighting (the postmortem's hard blocker). Let it die; TELL it it died; let it
reason. Recoverable smacks (re-droppable cube) = the best mid-episode learning signal.

**Best-effort MUST report the residual.** "Best effort then STOP" → "best effort, report the
RESIDUAL, then STOP." Snapping to closest-point on an unsatisfiable constraint *without*
reporting the deviation = the `first_light` transient-seat lie reborn (model reasons on the
false premise "I got collinearity"). Residual-reporting is execution-honesty, NOT diagnosis —
stays on the right side of the line.

## Prior locked rulings this refactor must respect (backdrop)

- **Backbone (06-24, `verb_grammar_rethink.md`):** free-if-consumes / puzzle-if-creates;
  simulate-body / teleport-object; hard capability fence.
- **Portal grammar (07-01, `portal_unified_grammar_options_dossier.md`):** laser stays DUMB
  (aim at the mouth, model reasons the exit — no pull-back solver); `(u,v)` on all panels;
  spine-only; fork-B (A\*-portal-edge) dropped.
- **Reconciliation to settle tomorrow:** the PROJECT algebra *extends* what "dumb" means (the
  harness now computes constraint-satisfying POINTS, more than "aim at the mouth"). Under the
  genie principle this is STILL dumb — it satisfies literal geometry without verifying the beam
  hits. So the "dumb laser" ruling is *subsumed* by the genie principle. Confirm this framing.

## The claim's control: human baseline (necessary, deferred)

Perfect actuation makes "spatial reasoning" *clean* but not *falsifiable* by itself — it's an
unfalsifiable residual without a control. The **human baseline driving the same setup** converts
"models fail" → "models fail at what humans do easily," isolating reasoning-gap from
task-difficulty.
- **USER's open question (MUCH-later):** humans probably won't want the genie-grammar — they'd
  use native keyboard+mouse. Flag: genie-grammar-baseline and KB+M-baseline control for
  *different* things (is the GRAMMAR a fair interface, vs is the CHAMBER human-solvable). Decide
  which control the paper needs. Deferred, noted.

## The three reasoning modes (user confirmed; blurred for now)

(a) atomic spatial inference ("where does the beam exit the portal?") · (b) long-horizon
planning (the sequence, trap-avoidance) · (c) recovery (got smacked, now what?). "Let it get
smacked" weights **recovery** heavily; azorae's trap-basin failure was a recovery/verification
failure, not atomic-geometry.
- **USER RULING: OK to blur all three into one for now** (full-chamber solve-rate; don't build
  micro-probes yet). Keep the taxonomy for later measurement.
- Later altitude: **micro-probes** (isolate one spatial inference as an almost-VQA atomic task)
  de-conflate (a) from (b)+(c) — a stronger claim if models fail the *atomic* probe. The genie's
  exact-setup power makes micro-probes trivial to construct. Parked.

---

## OPEN for tomorrow's brainstorm (focus the agents HERE)

1. **Constraint syntax / predicate language.** "align cube 15 on laser 11 via Pb such that
   {16, Pb, 15} collinear" is a mouthful. Shape: verb takes a target-point *expression*;
   expressions are constraint constructors (`collinear`, `angle`, `project`, `intersect`,
   `through`) over addressable loci — e.g. `interpose(15, on=laser 11, satisfying=collinear(16, Pb))`.
   Keep it FEW composable primitives, not a big vocabulary. Clean syntax = real work.
2. **Unsatisfiable / multi-solution semantics.** 0 solutions → honest "no point on laser 11
   satisfies …" (execution code). >1 → documented tie-break (closest-to-current? report
   ambiguity?). Part of "expressive error codes."
3. **The percept dual + the open recon gate.** Every constructor the model can EXPRESS forces a
   matching percept field it can PERCEIVE the inputs of. Beam-endpoint telemetry doesn't exist;
   `ComputeBeamSegment` is portal-blind; the portal-geometry recon gate is OPEN (does a bare
   `TraceRay` report a `prop_portal` hit at the plane?). Genie philosophy TIGHTENS legibility.
4. **Crosshair `X`** (§6 open) — view-state-dependent form; re-pokes the kill-view-dependent-verbs
   rule. Park vs fenced-ship to place_portal/go_to/pick_up.
5. **Beam-endpoint: percept-field vs address-only `N@1.0`** (§6 open). Under the genie the model
   must OBSERVE the outcome (it's held responsible), so endpoint *legibility* is likely required;
   reading the exact number vs just pointing at it is the sub-question.
6. **DSL-fluency confound.** Richer language → "failed to express the wish" masquerades as
   "failed to reason" (already tasted: phantom `NOT_REACHABLE`, client rejecting valid `Sn@u,v`).
   Mitigation: few-composable-primitives + measure syntax-fumble rate as its own confound-check.

## ★ Strategic framing — RESOLVED (user, 2026-07-21) ★

**This is the next investment, AND the paper is delayed until it lands.** But the load-bearing
reframe: **we already have the right machinery — this is WRAP-UP + CLEANUP of the sharp edges /
remaining clutter to make the grammar sensible, NOT a greenfield rebuild.** The genie principle
+ relational point algebra are mostly a *consolidation* of what's already built (`place_portal`,
`interpose`/`redirect_to`, the `Sn@u,v` / `N@f` addressing, the honest reject codes) into one
coherent, factored surface — plus closing the open edges (percept dual, unsatisfiable-constraint
codes, residual reporting, syntax).

**Implication for tomorrow's brainstorm:** agents CONSOLIDATE and clean — they do NOT invent from
scratch. Bias hard toward "what existing machinery does this map onto / reuse" over "new
subsystem." Minimal, surgical, tasteful — dissolve the clutter, don't accrete. **Note:** §10
verb-mobility + §5 A-fixes fold *into* this wrap-up (no longer a separate pre-paper track — they
are part of the cleanup).

## Pointers

- `azorae_stride_remake_postmortem.md` §6 (unified point algebra), §9 (open Qs), §10
  (verb mobility, already approved), §5 (A-fixes), §8 (paper plan).
- `portal_unified_grammar_options_dossier.md` (07-01 locked rulings, 6 axes).
- `verb_grammar_rethink.md` (06-24 backbone, verb table).
- `ROADMAP.md` (doc index).
