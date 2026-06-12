# Model-frontier lens — divergent brainstorm

*Council lens: the model side. Context/token scaling, memory, when to stop being frozen
(SFT/RLFT/distillation), latency/cost for 1000-chamber evals, multi-sample/tree-search over
macros, and the agent-loop design space. Diverge mode: 22 ideas, ordered roughly by leverage.
Effort = ONE research engineer + agentic coding: S=days, M=weeks, L=months, XL=team.*

Grounding facts this lens leans on (from first light, 2026-06-12):
- 686k input / 6.6k output tokens for 25 steps. **The cost is context, not generation** — the
  stateful chat resends full history + every frame each turn (quadratic-ish in steps).
- `TokenUsage` already records `input/output/cached/image/thinking` per call; the viewer rolls
  them up. Instrumentation for every cost experiment below already exists.
- The agent seam is one callable: `run_eval` touches only `agent(obs)`, `agent.system`, and token
  counters. New agents and new context policies are one class each.
- The rejection channel (`macro_grammar.validate` → re-prompt, zero game ticks) is a general
  feedback-injection slot.
- World-freeze means deliberation is free in-world: no other 3D embodied benchmark gives you that.

---

## 1. The O(1)-context agent: scratchpad + current-frame-only (and the A/B that justifies it)

**What:** Replace the stateful chat with a stateless per-step call: `system + scratchpad +
current annotated frame + current percept + last-K action/result digest`. The model *ends every
reply* with an updated scratchpad block (`notes`, `plan`, `tried-and-failed`, `where-things-are`)
that the agent carries forward verbatim. Implement as a `ContextPolicy` abstraction inside
`py/llm_eval/gemini_agent.py` (policy = {full-chat | window-N | scratchpad}) so the three modes
share everything else, then run the A/B on testchamber_000 + 2–3 new chambers: solve rate, steps,
tokens, latency per mode.

**Why it matters:** This is simultaneously the #1 cost lever (linear → constant per-step input;
~686k → ~10–15k/step regardless of episode length), the #1 latency lever, and — the underrated
part — *the science*. At 686k tokens the model is partly doing retrieval over its own transcript;
forcing it to externalize a bounded world-model tests whether it actually *has* one. The
scratchpad artifact is also legible in the viewer: you can watch the model's map of the chamber
evolve. Claude/Gemini-Plays-Pokémon harnesses converged on exactly this shape.

**Effort:** S (2–4 days incl. the A/B runs). **Depends on:** nothing. **Unlocks:** affordable
long chambers, the 1000-chamber eval farm (#15), cache-friendly shapes (#5), and the
"plan-then-execute" loop (#8) — the scratchpad's `plan` field is the natural seed.

## 2. Loop detection as injected feedback + a LOOP terminal

**What:** Hash `(percept-projection, verb-string)` per step in the session layer; on a repeat
(or a 2-cycle / 3-cycle), inject via the existing rejection channel: *"You have tried `go_to 8`
3 times; all BLOCKED. That approach is exhausted — do something different (move around the
obstacle, or reconsider the plan)."* After N injected warnings with no novel state, terminate
with a new `LOOP` terminal (splitting the overloaded `BUDGET`). Zero proto/engine changes; pure
Python in `testchamber_session.py`/`gemini_agent.py`.

**Why:** Steps 13–24 of first light were a masked loop. Every complex chamber will loop harder.
This both saves budget/tokens and produces honest terminal vocabulary for the benchmark (a
solve-rate table where BUDGET conflates "slow" and "stuck" is unpublishable). The injected-hint
arm vs no-hint arm is itself a small result about frozen-model self-recovery.

**Effort:** S (1–2 days). **Depends on:** nothing. **Unlocks:** clean terminals for the suite,
fairer budgets, and a measurable "recovers-after-hint rate" per model.

## 3. Cross-model harness + the model-ladder scaling curve

**What:** `ClaudeAgent` and `OpenAIAgent` classes (same `__call__(obs) -> AgentAction` contract,
same command-string grammar — it's provider-agnostic by design), plus a sweep script that runs
the same chamber set across a capability ladder: gemini flash-lite / flash / pro; claude haiku /
sonnet / opus; gpt-5-mini / gpt-5. One chart: solve rate (and steps-to-solve, tokens-to-solve)
vs model. Three seeds each.

**Why:** A single-model result is an anecdote; a ladder is a benchmark. "Solve rate scales with
model capability and saturates nowhere" is the single most pitchable artifact for a VP — it shows
headroom (the NetHack lesson) and positions the harness as model-agnostic infrastructure, not a
Gemini demo. BALROG's credibility came precisely from the multi-model table.

**Effort:** S (each agent is ~150 LOC; the harness seam was built for this). API spend is the
real cost — bounded by #1. **Depends on:** ideally #1 first (or pro-tier runs get expensive).
**Unlocks:** the headline pitch chart; competition baselines; distillation teacher selection.

## 4. The thinking-budget scaling curve: Portal 2 as a test-time-compute instrument

**What:** Sweep `thinking_level` (NONE/LOW/MEDIUM/HIGH) × chambers × seeds, holding everything
else fixed. Plot solve rate / steps / knowing-doing incidents vs thinking tokens. Add a second
arm: best-of-N sampling at fixed thinking (see #6's API-only variant) so you get *two* test-time
compute axes (deeper vs wider) on the same env.

**Why:** The world-freeze makes deliberation literally free in-world — no real-time pressure, no
penalty for thinking. That makes this harness arguably the cleanest *embodied* test-time-compute
instrument anywhere (VideoGameBench had to invent "Lite mode" to fake this; you have it natively,
per-tick). "Reasoning-compute scaling curves in a 3D physical environment" is a paper section and
a DeepMind-legible pitch slide (they are the test-time-compute company right now).

**Effort:** S (the config knob exists; it's runs + a notebook). **Depends on:** #1 for cost
sanity, 2–3 more chambers for signal. **Unlocks:** principled per-step compute routing (#16),
and a defensible answer to "why games for reasoning research?"

## 5. Prompt-cache engineering: make every token after the first step cached

**What:** Measure first (the `cached` field is already recorded — first job: pull the implicit-
cache hit rate out of `first_light.trajectory`). Then restructure for caching: stable system
prompt; append-only message shapes; images only at stable positions; explicit cached-content
for the system+grammar prefix; for the scratchpad agent (#1), order the message as
`[stable prefix | scratchpad | fresh obs]` so the prefix caches across steps and across *runs*.
Report effective $/step before/after.

**Why:** Cached input is ~4–10× cheaper depending on provider/tier. Combined with #1, this is the
difference between a $30k and a $1–2k 1000-chamber sweep. Also a competition design input: the
API-budget-capped track (BALROG-style) needs honest cost accounting.

**Effort:** S (2–3 days). **Depends on:** #1 (the shapes interact). **Unlocks:** #15 farm
economics; per-call cost numbers for the deck.

## 6. Simulate-then-commit: best-of-N macro sampling against engine save/load

**What:** Two tiers. **Tier A (API-only, no engine work):** sample K=5 candidate actions at
temperature, majority-vote on the verb string (self-consistency), validator breaks ties. **Tier B
(the real thing):** with save/load anchors (C9, ~40 LOC C++), snapshot the world, *execute* each
candidate macro, observe the real `MacroResult` + percept delta, restore, then either pick by a
scripted scorer (progress, new-state-reached) or show the model the K outcomes and let it choose
("here is what each action actually does — commit one"). Record branches in the trajectory as a
sidecar (keep the main file linear).

**Why:** Tier B is unique to this stack: a frozen pausable simulator means the agent gets a
*ground-truth* world model for one step of lookahead, at ~100ms per probe. It converts the LLM
from a policy into a proposal distribution + verifier loop — the standard recipe everywhere else
in 2026 reasoning, never yet done in an embodied 3D env with *real* (not learned) dynamics. Also
the on-ramp to #7.

**Effort:** Tier A: S. Tier B: M (C9 + branch plumbing + scorer). **Depends on:** C9 save/load;
marks already save/load-invariant by design. **Unlocks:** tree search (#7), high-quality
trajectories for the data factory (#12), and a "compute → solve rate" curve with real lookahead.

## 7. Macro-MCTS: the routing agent (speedrunning axis, model edition)

**What:** Best-first / MCTS search over macro sequences: nodes = engine save states, expansion =
K sampled verbs from the VLM (prior), rollout value = scripted progress heuristic or a cheap
flash-lite value query, backup standard. Budgeted (e.g. 200 node expansions/chamber). Output:
the best action *sequence* — i.e., a route — plus the search tree as an artifact.

**Why:** This is the bridge from "puzzle eval" to the user's speedrunning/routing axis: route
*finding* as search over semantic actions, with tick-perfect *execution* delegated to the engine
macros — exactly the orthogonal split the user described. A found-route artifact (replayable,
viewable) is spectacular demo material. Science: LLM-as-prior-in-MCTS in a real 3D engine, where
AlphaZero-style methods never go because they lack a steppable, saveable sim.

**Effort:** M–L (search loop M; making it *good* — value function, dedup states — L).
**Depends on:** #6 Tier B, exit oracle (terminal reward), parallel instances help. **Unlocks:**
the routing workstream, glitch discovery later (search at lower altitude), competition "routing
track."

## 8. Plan-then-execute loop + the knowing-doing gap metric (BALROG's, implemented)

**What:** Variant agent loop: at step 0 (and after any "surprise" — failed precondition,
unexpected state change), the model writes an explicit numbered plan into the scratchpad; each
subsequent step must reference the plan step it is executing or declare a re-plan. Offline,
compute the knowing-doing gap: fraction of failures where the *plan was correct* (judged by #9
or by ground truth, e.g. first light: plan right by step 7, execution starved after) but
execution failed. Compare ReAct vs plan-then-execute vs plan+reflection on the same chambers.

**Why:** First light IS a knowing-doing gap datapoint — formalizing the metric turns the anecdote
into the benchmark's signature number. BALROG named the phenomenon but measures it weakly;
the macro boundary + result codes measure it *sharply* (right verb + STUCK = doing-gap, wrong
verb = knowing-gap). Also the agent-loop A/B is a real open question: nobody knows which loop
shape wins in embodied settings.

**Effort:** S–M (loop variant S; gap metric + judge integration M total). **Depends on:** #1
(scratchpad carries the plan), #9 helps. **Unlocks:** the headline metric; failure-attribution at
suite scale.

## 9. LLM-judge verdict labeler over `.trajectory` (failure taxonomy at scale)

**What:** An offline judge (strong model, e.g. Pro/Opus) that reads each trajectory step
(frame, percept, reasoning, action, result) and emits the per-step verdict the viewer design
already reserved a writeback seam for: `ok | perception | reasoning | actuation | termination`.
Calibrate against ~100 hand-labeled steps; report agreement; then run over every trajectory ever
recorded. Aggregate into THE chart: failure-mode distribution per model per chamber tier.

**Why:** "Reasoning solved, locomotion is the wall" generalized from n=1 to n=everything, with
the human-verified judge as methodology. This is also the curation engine for the data factory
(#12): verdicts select which steps become SFT data vs hard negatives. Cheap because
`.trajectory` is lossless — no reruns, ever.

**Effort:** S (the format has everything; it's a prompt + a loop + a calibration pass).
**Depends on:** nothing. **Unlocks:** the headline aggregate chart, dataset curation, viewer
verdict chips, regression testing for harness changes ("did the new go_to move failures out of
the actuation bucket?").

## 10. Offline prompt-replay rig: counterfactual evals without a game

**What:** A small tool that replays logged observations from `.trajectory` (every call stores the
EXACT prompt sent) against (a) modified prompts — new system prompt, new grammar phrasing, percept
format ablations — or (b) other models, and diffs the chosen actions step-by-step against the
logged run. Metrics: action-match rate, divergence-step distribution.

**Why:** 90% of prompt-engineering iteration stops needing a live game. Cross-model screening
before paying for live runs (#3). Percept-format ablations (does bearing matter? does state
chip phrasing matter?) become an afternoon. This is the cheapest experimentation multiplier in
the whole program — the open-loop nature is a known limitation (divergence after step k makes
later steps off-policy), so treat match-rate as a screen, not a result.

**Effort:** S (1–2 days). **Depends on:** nothing. **Unlocks:** fast iteration on everything in
this file; "replay percepts to other models" was already called out as a designed-for property.

## 11. Cross-chamber lessons memory (Voyager-lite, no code): the frozen+memory track

**What:** After each run, the model (or a judge) distills 3–5 transferable lessons into a
persistent `lessons.md` ("glass walls block straight `go_to` — back out with `move back` first";
"confirm the button stayed pressed after walking near it"). Future runs inject it into the system
prompt. Strictly versioned and tracked as a *separate eval mode*: `frozen` vs `frozen+memory`,
both reported (the eval-purity rule from the design docs, kept).

**Why:** In-context skill acquisition without weight updates — Voyager's result, minus the
code-as-action confound the grammar docs explicitly rejected, so it slots into the existing
methodology. If `frozen+memory` solve rate climbs across a chamber curriculum with zero training,
that's a strong "agents that learn the way humans read a guidebook" result. Also nearly free.

**Effort:** S (2–3 days). **Depends on:** a few chambers to transfer *between*. **Unlocks:** the
curriculum/transfer storyline; a third leaderboard column for the competition.

## 12. The data factory: rejection-sampling solves into an SFT corpus (STaR for chambers)

**What:** Run best-of-N (N seeds × temperature, plus #6 Tier B when available) per chamber; keep
trajectories that reach ground-truth success (needs exit oracle); judge-filter steps (#9); emit
(annotated frame, percept text, scratchpad, accepted MacroRequest) tuples. Format converter to
standard SFT chat format + RLDS-ish export. Hindsight-label *failed* runs too: every `go_to`
attempt with its result code is calibration data ("predict the MacroResult") even when the
episode failed.

**Why:** This is the "when to stop being frozen" on-ramp and the training-data pitch made real:
the env is verifiable, so every API dollar spent on eval can be converted into supervised tokens
(SIMA 2's self-improvement loop is exactly this shape, with a hallucinatable rubric where you
have a ground-truth exit). The corpus feeds #13 (Gemini tuning), #14 (local SFT/RLFT), and the
Gemini-Robotics-style "structured-text actions over 3D percepts" data story.

**Effort:** M (the pieces exist; gluing + filtering + format export + actually generating volume).
**Depends on:** exit oracle (hard prerequisite for auto-filtering), chamber suite for diversity,
#1 for cost. **Unlocks:** #13, #14, and the trajectory-as-product pitch axis.

## 13. First unfreeze: supervised tuning of Gemini Flash on solved trajectories

**What:** Use the Gemini tuning API (supervised fine-tuning for Flash-class models) on the #12
corpus, formatted as single-turn (scratchpad+obs → action) examples — note the O(1) context
shape (#1) is what makes trajectories *tunable* at all (full-chat 686k-token examples are not).
Evaluate tuned-Flash vs frozen-Flash vs frozen-Pro on held-out chambers. Report: does tuning buy
the gap to Pro at Flash cost? Does it overfit to seen element combos (held-out PeTI elements as
the generalization probe)?

**Why:** The cheapest credible "training on our env improves a frontier-family model" datapoint —
the exact sentence the VP pitch needs ("your model + our env = measurably better embodied agent").
Methodologically clean because eval stays on held-out chambers.

**Effort:** M (mostly #12 plumbing reuse + tuning-API integration + eval sweeps). **Depends on:**
#12, suite with held-out split. **Unlocks:** the train-on-env headline; baseline for #14.

## 14. Distill to a small local VLM (2–4B): the deployable chamber-solver

**What:** LoRA-SFT an open VLM (Gemma 3 4B / Qwen3-VL-class) on the #12 corpus; serve it locally
(vLLM) behind the same agent interface; then RLFT (GRPO, verifiable reward = exit oracle) at
macro altitude — episodes are slow but macro-steps are few (~25/episode) and N game instances
parallelize rollouts; the world-freeze means no latency pressure during generation. Target
artifact: a 4B model with ≥ frozen-Flash solve rate at <1s/step and ~$0/step.

**Why:** "A 4B model solves Portal chambers on a single GPU" is a stronger pitch than any
frontier-API result — it's a *deployable agent*, it makes 1000-chamber evals and the competition
baseline free, and it's the existence proof for the headcount ask ("this is what one RE built;
imagine the scaled version"). It is also the only path to an agent the RL track can call at
interactive rates. RLFT here is the first real "RL with verifiable rewards in a commercial 3D
game" result.

**Effort:** L (SFT M; serving S; GRPO loop + stability another M; honest total: 2–3 months
part-time). **Depends on:** #12 (data volume is the gate), exit oracle, multi-instance farm.
**Unlocks:** free evals, competition baseline model (the BASALT/VPT "provide the pretrained
base" lesson), the RL-track bridge, the strongest demo.

## 15. The eval farm: 1000-chamber sweeps as a budgeted, parallel, resumable job

**What:** Marry `render_demos.py`'s queue/worker/instance-farm shape with `run_eval`: N game
instances × M concurrent API calls, per-run timeout/retry, resumable manifest
(`runs/<chamber>/<model>/<seed>.trajectory`, the directory convention already designed), cost
ledger per run (from TokenUsage), and a single `results.parquet`. Budget math up front: at #1+#5
economics (~10k cached-heavy tokens/step, ~30 steps) a 1000-chamber × 3-seed flash sweep is
~$300–1k and ~1–2 days on 8 instances — feasible; the naive stateful-chat version is ~$30k+ and
weeks — not.

**Why:** Everything upstream (ladders #3, curves #4, ablations #17, factory #12) is gated on
sweeps being a button, not a week of babysitting. Also this IS the competition eval
infrastructure (submission-runs-against-hosted-eval-server) — building it for yourself first is
the platform play.

**Effort:** M (the hard parts — instance lifecycle, crash recovery — exist in `render_demos.py`
and the RL stack's restart machinery; gluing + manifest + ledger is the work). **Depends on:**
#1/#5 for economics, exit oracle for unattended scoring. **Unlocks:** every at-scale claim in
the pitch; the competition backend.

## 16. Adaptive compute routing: think hard only when the world pushes back

**What:** Default `thinking_level=LOW` (or NONE); escalate to HIGH for the next call when (a)
last result ∈ {BLOCKED, STUCK, GRAB_FAILED, ...}, (b) loop detector fired, or (c) the model
itself requests it ("escalate" field in the output JSON). Optionally route *models*: flash-lite
executes the plan's next step; flash/pro is consulted on surprises (the
Pokémon-harness planner/navigator split, done with API tiers instead of two prompts).

**Why:** Most steps of a correct plan are trivial ("go_to 7, then release") — paying HIGH
thinking on every step is pure waste; first light's 6.6k output tokens across 25 steps shows
generation is already cheap, but thinking tokens and latency are not at HIGH. Gives a
cost-quality frontier curve (pairs with #4) and is the shape a real-time variant would need.

**Effort:** S (2–3 days + sweeps). **Depends on:** #2 (loop signal), #4 for baselines.
**Unlocks:** 3–5× cheaper runs at equal solve rate (hypothesis to verify); two-tier agent
architecture experience for the competition's budget-capped track.

## 17. The vision ablation: does the annotated frame actually help? (BALROG says maybe not)

**What:** Three arms on identical chambers: (a) frame+text (current), (b) text-only (mark list +
positions, no image), (c) frame-only (no mark-list text). The design docs mandate visual+symbolic
for the *headline* eval (correctly — it isolates reasoning); this is the *ablation* that tests
that design decision empirically. Measure solve rate, steps, perception-bucket failures (#9).

**Why:** BALROG found several frontier models do WORSE with vision (GPT-4o 32.3%→22.6%); if
Portal 2 replicates that, it's a punchy, citable finding about VLM percept fusion in 3D — and it
de-risks the percept design (if text-only matches frame+text on simple chambers, the symbolic
percept is carrying everything, and the interesting vision signal only appears on
geometry-heavy chambers — which tells you where the benchmark's vision discrimination lives).

**Effort:** S (config flag + runs). **Depends on:** a few chambers beyond 000. **Unlocks:** a
paper subsection; percept-design confidence; informs what the distilled model (#14) actually
needs to ingest.

## 18. Done-calibration: termination as a first-class measured skill

**What:** Once the exit oracle (`chamber_complete` in GameState) lands, study the `done` verb
properly: require a `"confidence": 0-100` field on every emitted action; compare stated
confidence + done-emission against ground truth. Metrics: premature-done rate, missed-done rate
(model walks through exit and keeps acting — would have shown up in first light's steps 8–24 had
it escaped), calibration curve.

**Why:** Termination is a known, under-measured LLM-agent weakness ("did I finish?" is a
world-model question). `done` has literally never fired in a real run — battle-testing it *is*
benchmark hygiene, and the calibration curve is a cheap, novel metric no game benchmark reports.

**Effort:** S. **Depends on:** exit oracle. **Unlocks:** trustworthy SOLVED/DONE terminals at
suite scale; a calibration column in the leaderboard.

## 19. Tokens-to-solve and dollars-per-chamber as first-class benchmark metrics

**What:** Define the benchmark's reported tuple as (solve rate, median steps, median
tokens-to-solve, $-to-solve at list price, wall-clock) — computed from TokenUsage already in
every trajectory; add to viewer rollups and the results table. Publish the metric definitions in
the benchmark doc (cached tokens counted at cached rate; thinking tokens counted).

**Why:** Anti-Goodhart for everything in this file (a 200-step loop-until-lucky agent and a
7-step solve must not score alike); makes efficiency a leaderboard axis (the BALROG-style
budget-capped track needs it defined precisely); and it's free — the data is already recorded.

**Effort:** S (1 day). **Depends on:** nothing. **Unlocks:** honest cross-model and
cross-agent-loop comparisons; competition scoring rules.

## 20. World-model accuracy probe: the model predicts, the engine grades

**What:** Add an optional `"predict"` field to the action JSON: before each macro, the model
states expected outcome ("result=SUCCESS, button 7 becomes pressed, dist to 8 ≈ 0"). The
session layer parses it and grades against the actual MacroResult + percept delta — ground truth
the engine provides for free. Report per-model world-model accuracy and its correlation with
solve rate; break out by verb and by element class.

**Why:** Step 12 of first light ("we bumped the cube off the button") showed real world-model
tracking — this measures it. "World-model accuracy in a real physics engine, graded
automatically" is a metric Genie-line work (learned world models, no ground truth) cannot
produce and would love to calibrate against — a sharp DeepMind pitch hook. Also directly useful:
prediction-conditioned training data (#12) and surprise-triggered compute routing (#16c).

**Effort:** M (parsing/grading S; making grading robust across verbs/percepts is the work).
**Depends on:** nothing hard. **Unlocks:** a novel headline metric; the Genie-calibration pitch
angle; surprise signals for routing and re-planning.

## 21. Compound actions: emit short macro sequences, abort on first failure

**What:** Let the model optionally emit `"verbs": ["go_to 11", "pick_up 11", "go_to 7",
"release 7"]` (cap ~4). The session executes sequentially, aborts on first non-SUCCESS, returns
per-verb results + one fresh observation. One grammar extension + a session loop; no engine
changes (each verb is still one MacroRequest). The frozen-loop design doc's reason to reject
composition ("worth ~zero in a no-deadline loop") was about *capability*; this is about *cost* —
it amortizes context resends 4× on confident stretches.

**Why:** With #1 it compounds: 30-step chambers become ~10 API calls. It also generates the data
that answers whether in-step composition is a real bottleneck — the named gate for the
code-as-action P1 arm in `llm_act_grammar_altitude.md`. Risk to respect: longer commitment =
staler percepts; abort-on-failure keeps it honest.

**Effort:** S–M. **Depends on:** #19 (so efficiency gains are measured, not vibes).
**Unlocks:** cheaper long chambers; the empirical gate for the altitude A/B.

## 22. Hosted "replay-to-any-model" regression suite: trajectories as unit tests for prompts

**What:** Curate ~50 golden decision points from accumulated trajectories (step 12's recovery,
correct done-moments, loop-escape moments, hard perception steps); package as a static eval set
(`prompt → expected action class`); run on every prompt/grammar/percept change and on every new
model release as a 5-minute CI job (pure API, no game).

**Why:** The prompt+grammar surface will churn constantly (every idea above touches it); without
regression anchoring you will silently un-fix old failures. Also the fastest "new model dropped,
is it better?" signal — first-light-grade evidence in minutes. This is the Crafter lesson
miniaturized: zero-friction, one scalar, runs anywhere.

**Effort:** S (builds on #10's replay rig). **Depends on:** #10. **Unlocks:** safe iteration
velocity for the whole model track; instant new-model triage.

---

## Spiciest take

**686k tokens for 25 steps means the agent isn't reasoning over a world — it's doing retrieval
over its own chat transcript. Kill the stateful chat.** The O(1) scratchpad agent is not a cost
optimization; forcing the model to externalize a bounded world-model *is the experiment* (does
it actually maintain state, or does it re-derive it from a 686k-token transcript every step?).
And everything strategic falls out of that one change for free: cacheable prefixes, affordable
1000-chamber sweeps, tunable single-turn training examples (#13 is impossible with 686k-token
chat examples), a legible mind-state artifact in the viewer, and the only context shape a 4B
distilled model can ever run. The stateful chat was the right first hack; it is now the single
biggest confound and cost center in the stack, and it should not survive the month.

## If I could only do ONE thing next week

Build the `ContextPolicy` abstraction and the scratchpad agent (#1), and run the three-way A/B
(full-chat vs window-2 vs scratchpad) on testchamber_000 plus two new chambers, reporting solve
rate, steps, tokens, and latency per arm. It is days of work, it de-confounds the science, and
every other idea in this file — ladders, curves, the farm, the data factory, distillation —
inherits its economics.
