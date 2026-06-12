# Lens: eval-science — the science of the benchmark

*Divergent brainstorm, 2026-06-12. One of ten council lenses. This lens owns: what makes
first light a* finding *instead of a demo — oracles, terminals, metrics, ablations, controls,
contamination, statistics, and the paper(s). Volume over pruning; every idea is actionable.*

**Framing axiom for everything below:** first light is currently an N=1 chamber, N=1 model,
N=1 seed result on an author-built map, where steps 8–24 were *censored* by the locomotion
wall — meaning the model's reasoning was never stress-tested past step 7. "Reasoning is
solved" is a hypothesis the eval-science workstream exists to make falsifiable. Everything
here is in service of surviving Reviewer 2.

---

## Ideas, ordered by leverage

### 1. Engine-hooked `chamber_complete` — ground-truth success oracle
**Build:** Hook the PeTI level-end path (`@relay_pti_level_end`-style relay /
`trigger_changelevel` / `changelevel`/`disconnect` dispatch) using SAR's standard hooking
patterns (`docs/contributing.md`); emit a `chamber_complete` bool in `GameState` over gRPC.
Keep the exit-coords oracle as a cross-check and measure oracle-disagreement rate on the
first 10 chambers (a one-evening meta-eval that validates the oracle itself).
**Why:** Every metric, comparison, and claim downstream is garbage if SOLVED is a hand-set
radius. This is the single prerequisite the suite (idea 4), the stats spec (idea 6), and the
leaderboard all share. First-light's own config (`exit r=10` vs the 100u default, 2D-told /
3D-tested distance) shows how fragile hand-oracles already are.
**Effort:** S (2–4 days; squarely inside SAR's existing hook patterns; sidesteps the missing
entity-I/O wiring entirely).
**Depends on:** nothing. **Unlocks:** everything; first item on the critical path.

### 2. Terminal taxonomy v2 + loop detector
**Build:** Split `BUDGET` into `SOLVED / DONE_WRONG / LOOP / STUCK / BUDGET / GAVE_UP /
CRASH`. Loop detector runs client-side over the running `(percept-hash, action, result)`
history; on the k-th repetition of a failed (state,action) pair, first *inject feedback*
via the existing rejection/re-prompt channel ("you tried `go_to 8` twice, both BLOCKED —
do something else"; zero proto/engine changes), and after k+m repetitions terminate with
`LOOP`. Percept-hash = rounded player pos + held_mark + sorted (mark, state) tuples — all
already in `WorldView`.
**Why:** Steps 8–24 were a (state,action) cycle masked as BUDGET. Terminal vocabulary is
the *outcome variable* of the whole benchmark; an overloaded terminal is an unusable label.
Also the feedback-injection arm is itself a publishable ablation (does telling the model
it's looping fix the loop? — directly tests in-context self-correction).
**Effort:** S (the detector is ~100 lines of Python; the terminal enum touches
trajectory.proto + viewer).
**Depends on:** nothing. **Unlocks:** honest failure attribution, the failure taxonomy
(idea 10), loop-rate as a cross-model metric.

### 3. The teleport control arm (actuation-oracle A/B)
**Build:** A `--cheat-locomotion` eval mode where `go_to`/`pick_up` resolve by snap-teleport
(`setpos` + settle) instead of engine marching. Same model, same prompts, same chambers, same
budget. Report all results as a pair: (realistic actuation, oracle actuation).
**Why:** The design docs rejected teleport for the *benchmark* ("you cheated the movement") —
correct for the leaderboard, but as a **control arm** it is the missing instrument for the
headline claim. Oracle-actuation solve rate upper-bounds reasoning; the delta between arms
*is* the locomotion tax, measured per-chamber and per-model instead of asserted from one
trajectory. If a model fails *with* perfect legs, that failure is unambiguously
reasoning/perception. This converts "reasoning solved, locomotion is the wall" from a
narrative into a two-column table.
**Effort:** S (teleport is trivially easier than the marching we already built; flag in
MacroExecutor + eval config).
**Depends on:** nothing (stronger with ideas 1–2).
**Unlocks:** the headline figure of paper #1; principled prioritization of locomotion work
(if the delta is small on hard chambers, nav-mesh `go_to` is *not* the #1 lever).

### 4. Chamber suite v0 — a 20-chamber reasoning ladder grounded in Demaine gadgets
**Build:** 15–25 hand-authored PeTI chambers, tiered by *formal gadget class* (Demaine,
Lockhart, Lynch 2018): T0 locomotion-only; T1 cube+button+door (PSPACE-complete gadget set,
first-light tier); T2 multi-cube/multi-button ordering; T3 timed pedestal buttons (NP-hard
gadget); T4 laser+relay+platform (PSPACE set #2); T5 compositions. Each chamber ships a
**benchmark card**: elements used, gadget class, human par (macro count, from idea 16),
author solution `.trajectory`, oracle config. Suite manifest = one JSON the runner iterates.
**Why:** N=1 chamber supports no claim. The ladder *is* the benchmark; the complexity-theory
grounding is the scientific cover that lets a 20-chamber suite claim difficulty *structure*
rather than difficulty *vibes* — no predecessor (BALROG, VideoGameBench, FLE) has
per-task formal hardness classes.
**Effort:** M (2–3 weeks: PeTI authoring is fast, but each chamber needs validation play,
oracle wiring, par measurement; agentic coding helps with manifest/runner, not authoring).
**Depends on:** idea 1 (oracle), ideally idea 2.
**Unlocks:** every multi-chamber number; M3/M4; the paper's main table.

### 5. Contamination protocol — unseen chambers, hash-commit, memorization probes
**Build:** (a) Suite hygiene: eval chambers are never uploaded to Workshop/YouTube/git-public
before the eval window; publish SHA256 of the `.p2c`s at announce time, release files after.
(b) Memorization probes: before showing any frame, ask each model to describe "Portal 2
chamber X" by name; run stock campaign chambers (`sp_a2_*`) vs geometry-isomorphic PeTI
remakes and measure the solve-rate delta — that delta is the *measured contamination bonus*
of 15 years of walkthrough text/video in pretraining.
**Why:** Stock Portal 2 is among the most-walkthrough'd games in existence; any eval touching
campaign maps is measuring recall ∪ reasoning. The isomorphic-remake experiment is cheap,
novel, and quantifies what every LLM-game benchmark hand-waves. Procgen's private-env split
and GSM1k are the precedents; PeTI makes test-set refresh nearly free (the genuinely unique
asset vs all predecessor benchmarks).
**Effort:** S–M (protocol is a doc + hashes = days; the isomorphic-remake experiment = a week
of authoring + runs).
**Depends on:** idea 4 (or even 3 chambers suffice for the probe).
**Unlocks:** contamination-resistance as a *headline benchmark property* in the pitch;
defensible held-out evaluation forever (rotate chambers per season).

### 6. Pre-registered stats spec (write it BEFORE the big runs)
**Build:** A short `brainstorm/eval_stats_spec.md` fixing, in advance: seeds per
(model, chamber) cell (≥5; Source physics nondeterminism makes single-seed numbers noise);
primary metric = solve rate with Wilson 95% CIs; model comparisons via paired bootstrap over
chambers (chambers are the unit of analysis, not episodes); pass@k AND pass^k (all-k-succeed,
the reliability metric labs actually care about); a power sketch (with 20 chambers × 5 seeds,
detectable solve-rate gap ≈ 20–25pp at α=.05 — so don't claim 5pp differences); multiple-
comparison handling for the ablation battery; and a rule that every headline number states
observability mode + actuation arm.
**Why:** This is the cheapest possible insurance against the benchmark's numbers being
dismissed. Pre-registration (even informal, in-repo, dated by git) converts "we ran some
evals" into methodology. Nobody in the LLM-game-eval space does this; it's a differentiator
that costs days.
**Effort:** S (2–3 days, mostly thinking).
**Depends on:** nothing. **Unlocks:** credibility of every table; honest N for the headcount
pitch ("at N models × 20 chambers × 5 seeds × 2 arms, eval cost is X tokens ≈ $Y").

### 7. Cross-model harness — BALROG-for-Portal-2
**Build:** The agent is already one callable (`obs → AgentAction`); add `claude_agent.py`,
`gpt_agent.py`, and 1–2 open VLMs (Qwen2.5-VL, InternVL via vLLM/OpenRouter) behind the same
system prompt, grammar, budgets, and retry caps. Matrix runner: models × chambers × seeds ×
arms, writing `runs/<chamber>/<model>/<iso>.trajectory` (the pre-designed directory
convention). Publish a rolling table, BALROG-style, not an annual event.
**Why:** A benchmark with one model is a case study. Cross-model spread is also the fastest
sanity check of the instrument itself (if all models tie at 0 or 100, the ladder is
mis-tiered). The open-VLM rows future-proof against "this only works on Gemini" and give the
DeepMind pitch a competitive frame.
**Effort:** S plumbing (each agent class is a day — the seam was built for this) + M for
runs/cost management (rate limits, $$ tracking; free-tier reality already documented).
**Depends on:** ideas 1–2; idea 6 for run counts.
**Unlocks:** M4; the paper's model table; the leaderboard; "Gemini vs Claude vs GPT on
PSPACE-hard chambers" is the single most quotable artifact for a VP pitch.

### 8. Censoring-aware metrics: solve *curves*, not solve rates
**Build:** Report solve probability as a function of macro budget (anytime curve, AUC as the
scalar) and token budget, computed offline from `.trajectory` (the data is already there).
Treat non-SOLVED terminals as right-censored observations (survival analysis: Kaplan-Meier
over steps-to-solve), so "BUDGET at 25" doesn't pretend the model *couldn't* solve at 40.
Also: tokens-to-solve and macros-over-par as efficiency metrics.
**Why:** First light is the poster child — a fixed 25-step budget plus a locomotion tax means
solve-rate-at-25 conflates reasoning ability with actuation overhead. Curves decompose this;
the censoring framing is statistically correct and (to my knowledge) absent from every
LLM-game benchmark. Pure analysis code; zero engine work.
**Effort:** S (days; pandas over trajectories).
**Depends on:** a handful of runs to plot. **Unlocks:** honest cross-model comparison at
*any* budget; kills budget-choice as a reviewer attack surface.

### 9. Paper #1: the instrument + the separation finding (venue ladder)
**Build:** Target shape: "**Portal2Arena: attributable evaluation of frozen VLMs in a
PSPACE-hard physical puzzle environment**." Claims: (i) the macro-boundary instrument makes
failures attributable (perception/reasoning/actuation) by construction; (ii) the
teleport-arm × verdict-label data shows where frontier models actually fail; (iii) formal
difficulty ladder via Demaine gadgets; (iv) contamination-controlled. Ladder: agentic-AI /
games workshop (fast, 4-pager, deadline pressure) → NeurIPS Datasets & Benchmarks (the FLE/
BALROG venue) with the full suite + 4–6 models. Pre-write the related-work spine now:
BALROG (knowing-doing gap, vision-hurts), VideoGameBench Lite (pause-while-thinking),
FLE (spatial/long-horizon), Voyager (macro altitude), Demaine (hardness), SIMA 2 (command
grammar at scale).
**Why:** The paper IS the pitch artifact; a VP funds a result with a venue, not a repo.
Writing the claims list *first* back-propagates exactly which infra ideas (1–8) are
load-bearing and which are decoration.
**Effort:** M for the workshop paper given ideas 1–8 land; L to D&B (the suite + runs
dominate, not the writing).
**Depends on:** 1, 2, 3, 4, 6, 7 minimum.
**Unlocks:** legitimacy, citations, the VP meeting, competition announcement platform.

### 10. Per-step verdict labels + failure-taxonomy codebook
**Build:** The viewer's reserved verdict-writeback seam becomes real: one POST endpoint +
sidecar JSON (`ok / perception / grounding / reasoning / actuation / loop` per step). Author
a codebook with definitions and 2 examples each (e.g. *grounding* = right plan, wrong mark;
*actuation* = right verb+mark, engine result ≠ SUCCESS). Label first-light + the next ~20
runs by hand (~1–2h each); then bootstrap an LLM-assisted labeler over `.trajectory`
(thinking text + result codes are already captured — the labeler needs no game) and measure
human-LLM agreement (Cohen's κ) before trusting it at scale.
**Why:** "Reasoning vs locomotion" at headline scale requires *labeled* failures, not one
anecdote. The labeled set is triple-duty: paper evidence, regression suite for harness
changes (did nav-mesh `go_to` actually convert actuation failures?), and — per the SIMA 2
template — exactly the rubric-scored trajectory data DeepMind ingests.
**Effort:** M (seam + codebook = days; labeling discipline + agreement study = weeks,
amortized).
**Depends on:** ideas 2, 7 (runs to label). **Unlocks:** the attribution pie chart that is
the paper's figure 1; the curated-trajectory training-data story.

### 11. Knowing-doing gap via the oracle-plan arm
**Build:** Third eval arm: prepend the chamber's ground-truth macro plan (from the author
solution / idea 22's solver) to the system prompt — "a correct plan is: …" — and measure
execution. Four-cell design per chamber: {plan, no-plan} × {real, teleport actuation}.
BALROG named the knowing-doing gap but measured it weakly; this design isolates it cleanly:
no-plan failure that becomes plan success = planning gap; plan failure under teleport
actuation = pure grounding/execution gap.
**Why:** It's the cheapest *causal* decomposition available — pure prompt manipulation, zero
engine work — and it directly measures whether better prompting/planning (cheap) or better
models (expensive) is the binding constraint, which is precisely a funder's question.
**Effort:** S (a prompt flag + analysis), given idea 3.
**Depends on:** ideas 3, 4. **Unlocks:** the 2×2 table that anchors the paper's analysis
section.

### 12. Perception probe suite — VQA over logged percepts (no game required)
**Build:** From archived `.trajectory` frames + mark lists, auto-generate ground-truthed
perception questions (truth comes from the entity snapshot: "what class is mark 7?", "is the
button pressed?", "which mark is nearest the door?", "is the cube left or right of you?").
Run as stateless single-turn VQA across all models. Pure offline replay — the trajectories
already store everything.
**Why:** Completes the decomposition empirically: perception score (this), reasoning score
(teleport arm), actuation tax (arm delta). If a model can't read the annotated frame, its
agentic failures are over-determined and the agentic result uninterpretable. Also directly
tests the project's core bet that Set-of-Marks annotation makes perception ~free — that bet
deserves a number. BALROG's "vision hurts" finding makes this topical.
**Effort:** S–M (generator + runner ≈ a week; reuses trajectory_io).
**Depends on:** archived trajectories (exists). **Unlocks:** a standalone mini-benchmark
(publishable on its own), annotation-quality regression testing for every HarnessAnnotate
change.

### 13. Annotation ablation battery
**Build:** Factorial eval over the percept channel, every cell already a near-flag:
{full annotation, boxes-no-labels, labels-no-boxes, raw pixels, symbolic-only (no frame),
frame-every-step vs every-N, state fields on/off in the mark list}. Pick 4–5 informative
cells, not the full factorial (idea 6 governs N).
**Why:** Measures what the annotation layer *buys* — the project's central engineering bet.
"Annotated frames add +Xpp over raw pixels; symbolic-only loses Ypp (spatial imagination
confound confirmed)" is both a paper section and the empirical justification for the entire
HarnessAnnotate investment. Symbolic-only is the arm the grammar doc explicitly refused to
ship as default; as an *ablation* it's the right control.
**Effort:** M (each cell is cheap; the cost is runs + analysis discipline).
**Depends on:** ideas 4, 6, 7. **Unlocks:** annotation design becomes evidence-driven;
ammunition against "you over-scaffolded it" (the Pokémon-harness critique, which WILL come).

### 14. Recovery events as a first-class metric
**Build:** Auto-detector over `.trajectory`: a *divergence* = entity-state regression on a
previously-achieved subgoal (button unpressed, cube dropped, held_mark lost); a *recovery* =
subsequent macros re-targeting the diverged entity within k steps, confirmed by state
restoration. Report divergence rate, recovery rate, mean steps-to-recover per model.
**Why:** Step 12 ("we bumped the cube off the button… re-place it") is the most impressive
moment of first light and currently exists only as an anecdote. Recovery-under-physics-
side-effects is exactly what separates embodied agents from text agents, no existing
benchmark reports it, and it's computable offline from data already captured. Likely the
most *novel* single metric this benchmark can claim.
**Effort:** S–M (detector ≈ days; validating it against hand labels from idea 10 ≈ days).
**Depends on:** runs to mine; stronger with idea 10. **Unlocks:** a headline-grade novel
metric; a robustness axis for the leaderboard.

### 15. Global vs egocentric observability — the registered A/B (Track B)
**Build:** The ~70-LOC frustum + LoS filter behind a cvar (already designed); then run the
suite both ways and report the *information-leak delta*. Pre-register the hypothesis (global
mode inflates solve rate via through-wall marks) per idea 6. All headline numbers state mode.
**Why:** The docs already admit global mode leaks what a player couldn't see — an honesty
debt that compounds with every published number. The A/B is also an active-perception
research axis (does the model *look around* when it must?) that egocentric mode opens
for free.
**Effort:** S–M (engine filter S; runs M-ish shared with idea 7's matrix).
**Depends on:** ideas 1, 4. **Unlocks:** defensible headline claims; an entire
exploration/active-perception follow-on paper.

### 16. Human macro-altitude baseline ("par")
**Build:** Humans solve every suite chamber through the *same* macro REPL (same verbs, same
percepts — `macro_repl.py` already exists and writes the same `.trajectory`). 2–3 humans ×
20 chambers. Par = median human macro count; report models as human-normalized scores
(BASALT-style human reference) and as macros-over-par.
**Why:** "Solved in 7 steps" means nothing without par (human par on testchamber_000 is
maybe 5–6 — so the model was near-optimal! That's a stronger claim than "solved"). Humans
hitting locomotion walls in the same interface also calibrates how much of the actuation tax
is the *interface's* fault vs the model's. And the human trajectories are seed data for the
training-data flywheel — same format, zero extra tooling.
**Effort:** S–M (tooling exists; it's hours of human play + recruiting two friends).
**Depends on:** idea 4. **Unlocks:** human-normalized leaderboard; interface-fairness
evidence; demo corpus.

### 17. Macro-determinism audit
**Build:** Replay the accepted macro sequence from a stored `.trajectory` N=20 times on the
same chamber (script agent, no LLM); measure outcome divergence (terminal agreement,
final-state entity diff, position spread). Publish the number per chamber tier ("macro-level
replay agrees on terminal outcome in 98% of runs; physics divergence appears only with
airborne cubes").
**Why:** p2sr's own TAS wiki documents Source physics desync (dropper cubes diverge across
replays). Reproducibility is a *claim the benchmark must measure, not assert* — this number
decides the seeds-per-cell in idea 6 and pre-empts the "Source isn't deterministic" attack.
It's also a one-day scripted-agent job thanks to the action-source seam.
**Effort:** S (1–2 days).
**Depends on:** nothing. **Unlocks:** honest reproducibility section; correct statistical
design; informs whether save/load tree search (the routing axis) can trust its branches.

### 18. Mark-robustness perturbations
**Build:** Three cheap perturbation evals: (a) permute mark numbers between episodes (same
chamber, shuffled IDs), (b) recolor classes (swap the kClassColors palette), (c) jitter label
billboard positions. Solve rate should be invariant; any drop = the model is keying on
surface conventions rather than reading the scene.
**Why:** Set-of-Marks grounding is the percept's core mechanism; its robustness is currently
assumed. This is the adversarial-eval hygiene that distinguishes a benchmark from a demo,
and (a) doubles as a contamination probe for models that have seen *our own published
trajectories* in future training runs — a real concern once the viewer HTMLs circulate.
**Effort:** S (perturbations are config/palette flags + MarkTable shuffle).
**Depends on:** ideas 4, 7. **Unlocks:** robustness section; future-proofing against
benchmark-specific overfitting.

### 19. `done`-precision battery (false-completion testing)
**Build:** 3–4 trap chambers: door opens but exit is beyond it; a decoy second door; a
chamber where the visible "exit" is unreachable without a second mechanism. Metrics:
premature-`done` rate (declared done, oracle says no) and missed-`done` rate (oracle says
solved, model keeps acting). Requires idea 1 so `done` can be scored against ground truth.
**Why:** `done` has *never fired in a real run* — the verb that defines episode semantics is
untested. Calibrated stopping (knowing you've finished) is itself a capability metric, and
premature-done is the failure mode that silently corrupts solve rates if unmeasured.
**Effort:** S (chambers + analysis).
**Depends on:** idea 1. **Unlocks:** trustworthy terminal semantics; a self-knowledge
metric (ties to the knowing-doing axis).

### 20. Context-policy ablation as a science question
**Build:** Compare {full chat history, last-N frames (N=1,3), text-summary memory, no
history (Markov)} on the suite. The quadratic-cost fix is the engineering motivation, but
*report it as a memory ablation*: how much cross-step memory does chamber-solving need?
`.trajectory` keeps everything regardless, so live-context trimming loses no analysis.
**Why:** 686k tokens for 25 steps is the scaling wall AND an open science question — if
last-1-frame matches full history, chambers are near-Markov at macro altitude (itself a
finding, and it makes the benchmark 20× cheaper for everyone, which matters enormously for
a public leaderboard's accessibility).
**Effort:** S–M (context policies are agent-side code; runs shared with the matrix).
**Depends on:** ideas 4, 7. **Unlocks:** 10–20× cheaper evals (more seeds for the same $,
feeding idea 6); a memory-requirements finding.

### 21. Cost-normalized leaderboard semantics
**Build:** Define and report: tokens-per-solve, $-per-solve (priced per model), solves-per-
1M-tokens, and a budget-capped track (BALROG-style API budget cap) alongside the uncapped
track. Token accounting is already captured per-call in `.trajectory` — this is pure
reporting discipline plus a leaderboard column.
**Why:** Frontier-model evals that ignore cost reward whoever burns the most thinking
tokens; a cost axis keeps open VLMs on the same chart (they win $-per-solve long before
they win solve rate) and is what makes the competition accessible. Cheap, and it makes the
benchmark feel professionally run.
**Effort:** S (days).
**Depends on:** idea 7. **Unlocks:** fair multi-track comparison; competition design input.

### 22. Macro-MCTS oracle solver — machine par + chamber validation
**Build:** Save/load anchors (C9, ~40 LOC, engine save/load) + the frozen-world turn loop +
mark-stable IDs = best-first/MCTS search over macro sequences. Use it offline as an *oracle
solver*: validates every suite chamber is macro-solvable, computes machine par (min macro
count), and emits the ground-truth plans idea 11 consumes.
**Why:** A benchmark whose tasks aren't machine-verified solvable ships broken chambers
(every UGC benchmark's plague, and fatal at Workshop scale). Machine par beats human par for
optimality-gap metrics. And the solver is literally the embryo of the speedrun-routing agent
— the eval tool and the routing research axis are the same code.
**Effort:** M–L (anchors S; a competent pruned search over ~9 verbs × ~15 marks is real
work; restore-correctness is the admitted tar pit).
**Depends on:** ideas 1, 17 (branch trust). **Unlocks:** suite QA at scale, optimality-gap
metric, ground-truth plans, the routing-agent bridge.

### 23. Per-element competence matrix via generated minimal-chamber families
**Build:** Programmatic `.p2c` generation (format documented; Portal2.Puzzle/p2c_conv
exist) of one-factor-at-a-time micro-chambers: k variants each isolating one element
(fizzler, faith plate, laser relay, funnel, timed button…). Output: a model × element
heat-map of competence, and within-element difficulty scaling (1 cube vs 3 cubes).
**Why:** The difficulty ladder (idea 4) tells you *that* a model fails at tier 3; the matrix
tells you *which mechanism* breaks it. It's the Procgen "task distribution" property
realized through PeTI's unique programmatic authorship, the contamination-proof renewable
test set, and the natural seed of competition season content.
**Effort:** L (generator + per-element oracle/annotation coverage including category-B
elements like gels that need new sensors; this is the first idea that honestly wants a
second pair of hands — say so in the pitch).
**Depends on:** ideas 1, 4, 22 (solvability validation). **Unlocks:** scalable benchmark
generations, competition seasons, the "800k-workshop → curated tiers" pipeline story.

---

## Spiciest take

**"Reasoning is solved" is not yet a finding — it's an N=1, single-seed, author-built-chamber
anecdote whose final 17 steps were censored by the locomotion wall, which means the model's
reasoning was never tested past step 7.** The design principle "no teleport — that's
cheating the movement" is correct for the leaderboard and exactly backwards for the science:
until the teleport control arm exists, *every* failure on harder chambers will be charged to
locomotion by default, and the benchmark will keep telling its authors the story they
already believe. Build the cheat. The cheat is the control. I'd bet money the teleport arm
on a 20-chamber ladder reveals reasoning failures by tier 3 — and that result is *more*
fundable than "reasoning is solved," because it defines the gap a research program exists
to close.

## If I could only do ONE thing next week

Build the engine-hooked `chamber_complete` oracle + the terminal split (ideas 1+2, both S,
same week). Every metric, ablation, model comparison, and paper claim downstream is
uninterpretable until SOLVED is ground truth and LOOP stops masquerading as BUDGET —
it's the only item that is upstream of literally everything else in this lens.
