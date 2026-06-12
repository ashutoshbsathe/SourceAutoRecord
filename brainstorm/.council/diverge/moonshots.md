# Moonshots lens — divergent brainstorm (2026-06-12)

*One of 10 council lenses. Mandate: dream big, stay honest. Every idea: what gets built,
why it matters, effort for ONE research engineer + agentic coding (S=days, M=weeks,
L=months, XL=needs a team), dependencies, and what it unlocks. Ordered roughly by leverage.
Pruning happens later — this is volume + originality, but everything here is actionable.*

Grounding: first light is real (frozen Gemini solved cube→button→door in 7 steps; locomotion
is the wall). The architecture has three properties most ideas below exploit: (1) the world is
a **pausable, frame-stepped simulator** (tick gating + condvar); (2) the **action source is a
seam** — anything callable can drive the macro grammar; (3) every percept upgrade flows to
live gRPC, `.hdem`, and `.rollout` simultaneously (one snapshotter, three consumers).

---

## 1. The Atlas & P-Body Protocol: two frozen agents, one chamber, a chat channel

**What.** Portal 2 ships a full two-player co-op campaign and PeTI supports authoring co-op
chambers — puzzles where actions are *interdependent by construction* (A stands on the button
so B can cross; synchronized fling timing; portal handoffs). Build dual control: splitscreen
co-op (`ss_map`) keeps everything in ONE game process — no networking, one snapshotter, one
SHM. Upstream SAR's TAS framework already supports co-op (slot-indexed playback — verify
`sar_tas_play <f1> <f2>` works in our fork). Extend MacroExecutor with a player-slot
parameter, duplicate the percept per player, run two `next_action(obs)` agents turn-by-turn
against the frozen world, and give them a structured text channel (`say <msg>` as a free verb
— zero engine work, it's just a string relayed between contexts).

**Why.** This is the moat. No commercial game has built-in, author-able, two-player puzzle
*dependency* like this. SIMA has no multi-agent story; BALROG is single-agent; Melting Pot is
gridworlds. A grounded two-agent collaboration benchmark where communication is *necessary*
(not decorative) and *grounded* (marks are shared referents) does not exist anywhere. For the
DeepMind pitch this is the slide nobody else can copy.

**Effort.** M for dual-control smoke test (two scripted agents solve one co-op chamber);
L for the real benchmark (10–20 co-op chambers, turn protocol, two-agent trajectory format +
viewer). **Depends:** MacroExecutor slot plumbing; co-op TAS verification; co-op chamber
authoring (PeTI does this natively). **Unlocks:** #9 (asymmetric info / emergent
communication), #10 (human-agent teaming), an entire multi-agent eval track no one else has.

## 2. The Chamber Is a Search Tree: MCTS routing engine over macro space

**What.** World-freeze + engine save/load anchors (C9, ~40 LOC by the design doc) +
save/load-invariant marks = the engine becomes a transition function you can branch.
Build: `anchor()/restore(id)` verbs (internal, not exposed to the model), then a best-of-N /
MCTS driver over verb sequences. The frozen VLM is the *proposal prior* (propose top-k next
macros, like AlphaGo's policy net); the engine evaluates; the search owns backtracking.
Compare: pure-VLM ReAct vs VLM+search at equal API budget.

**Why.** This is the bridge to the entire speedrunning axis ("routing agents as general
planners") and it's nearly free given the architecture — the turn-based loop makes ~100ms
save cost irrelevant. Scientifically it cleanly separates "the model can't *plan*" from "the
model can't *commit to one rollout*" — the next attribution cut after reasoning-vs-locomotion.
Semi-determinism caveat is mostly voided: restore replaces replay.

**Effort.** M (anchors S; search scaffold + budgeted VLM-prior driver M). **Depends:**
save/load anchors verified (the docs flag restore as a tar pit — timebox it); exit oracle (#
prereq for automatic node scoring). **Unlocks:** #12 (machine TAS), #13 (glitch discovery),
and a "VLM-as-prior planner" paper shape.

## 3. The Infinite Testing Initiative: generative PeTI curriculum (Genie, but real physics)

**What.** The `.p2c` puzzle format is documented plain text (VDC wiki; Kyle0654/Portal2.Puzzle
precedent). Build the generator pipeline: LLM (or template engine) emits `.p2c` → in-game
compiler produces a playable BSP → harness boots it → solver agent attempts → solve/fail +
trajectory feeds back to the generator. Then close the loop: generator is rewarded for
chambers at the *frontier* of solver ability (solvable-but-barely), XLand/POET-style, except
the worlds are real, deterministic-ish, and formally grounded (Demaine: the element set spans
up to PSPACE-complete; the P2C format even bounds instance size pseudopolynomially).

**Why.** This is the open-endedness story DeepMind is paying for (Genie 3 = "limitless
curriculum" in *hallucinated* worlds; this is the limitless curriculum in a *verifiable*
world). It also solves benchmark contamination forever: the test set is a generator, not a
list. Procgen's one durable idea — train/test on a distribution — at full 3D-physics fidelity.

**Effort.** M for the offline pipeline (generate→compile→boot→attempt, no loop); L for the
closed self-curriculum loop. The risk to retire first: driving the PeTI compiler headlessly
(`puzzlemaker_compile`-style console path — recon needed). **Depends:** exit oracle; chamber
suite plumbing. **Unlocks:** #10 league play, infinite training data for #6/#17, the
anti-VPT-moment defense for any competition.

## 4. The Wormhole Test: Portal mechanics as THE non-Euclidean spatial reasoning benchmark

**What.** A diagnostic suite of 25–40 hand-authored micro-chambers, each isolating ONE
portal-reasoning primitive: momentum redirection ("speedy thing goes in, speedy thing comes
out"), fling trajectory prediction, seeing yourself/objects through a portal (non-local
visual continuity), infinite-fall reasoning, portal-pair topology updates after re-fire,
reachability through portals that straight-line distance says is unreachable. Requires
`shoot_portal` (C3 — proto fields already reserved; the placement reticle annotation already
exists). Score per-primitive, not per-chamber → a *capability fingerprint* per model.

**Why.** Nothing on Earth benchmarks wormhole-topology reasoning in an embodied loop. BALROG
showed vision often *hurts* frozen models; FLE showed spatial reasoning is the frontier
failure; this isolates the most exotic spatial primitive available in any commercial game.
It's also the headline-friendly science: "GPT-5 cannot fling."

**Effort.** M (C3 verb ~100-line pattern + chambers + suite runner). **Depends:** M3 portal
ramp (already roadmapped); chamber authoring time dominates. **Unlocks:** the signature
paper/pitch result that is Portal-2-specific rather than generic-agent-benchmark.

## 5. The Sleeping Giant: SAR is already on every leaderboard — the VPT data flywheel

**What.** Upstream SAR is *mandatory* for runs on Portal 2 speedrun.com leaderboards. Ship
`.hdem` v2 (add the per-tick input record — the `CUserCmd` hook already exists at
`Client.cpp:586`; the format is versioned; the brief calls this the highest-leverage small
change), get the recorder upstreamed or distributed as an opt-in community build, and every
leaderboard run, practice session, and workshop playthrough becomes a pixel-free,
state-perfect, input-synchronized demonstration at I/O-speed conversion cost. Stage 2: train
an inverse dynamics model on that seed corpus and pseudo-label 15 years of YouTube Portal 2
speedruns/Workshop VODs — VPT's exact recipe (2k contractor hours → 70k YouTube hours), except
the "contractors" are volunteers who already run your plugin and the state labels are perfect.

**Why.** VPT is the canonical proof that a small action-labeled corpus multiplies into web
scale. For the "trajectories as Gemini Robotics / Omni fuel" pitch, this is the asset: an
egocentric, physical, long-horizon, input-synchronized human corpus with ground-truth entity
state — the tuple SIMA pays studios and annotators for, generated free by a community that
exists since 2011. CSGO BC dataset (Pearce & Zhu) is the no-legal-trouble precedent.

**Effort.** S–M for hdem v2 + replay determinism note; M for community packaging + p2sr
conversation; L–XL for IDM + YouTube pseudo-labeling at scale (that's the headcount ask, and
it's a *great* one). **Depends:** nothing on the critical path — parallelizable today.
**Unlocks:** #16 (apprenticeship), #17 (distillation), #18 (robotics export), the entire
training-data pitch axis.

## 6. Give the Plans Legs: an RL locomotion policy AS the `go_to` actuator

**What.** Attack the first-light wall with the dormant PPO stack, re-aimed at a task it can
actually win: goal-conditioned *navigation only* (reach point/mark X) on a procedurally
generated PeTI navigation curriculum (#3 pipeline, trivial chambers, randomized geometry).
Dense distance reward, no puzzle logic, frozen ViT, existing async infra. Then `go_to`
dispatches to the policy when the straight-line march returns BLOCKED. The LLM never knows.

**Why.** This is the literal Gemini Robotics 1.5 architecture — frozen VLM reasoner over a
learned motor policy — demonstrated in a game, and it converts first light from BUDGET to
SOLVED on the existing chamber. It also revives the entire RL stack investment, and it stays
honest with the grammar principle: the policy paths around geometry, it does not solve
puzzles (train it only on puzzle-free chambers; that's the line, made enforceable).

**Effort.** L (nav-RL is far easier than the old task, but it's still RL: curriculum, eval,
integration; the infra exists and is battle-hardened). Hedge: do nav-mesh `go_to` (S–M,
engine `Trace` machinery is wired) first as the boring baseline — the RL actuator is the
moonshot version that makes the hierarchical-agent story real. **Depends:** #3 pipeline for
curriculum (or hand-built nav chambers). **Unlocks:** SOLVED terminals everywhere; the
hierarchy slide in the pitch; a transferable "learned actuator under a frozen reasoner" recipe.

## 7. The Workshop Atlas: triage all ~954k community chambers into the benchmark

**What.** A pipeline over the (verified) 954k-item Portal 2 Workshop — third-largest on
Steam: steamcmd bulk download → headless boot on the instance farm (`render_demos.py` is
already the right queue+workers shape) → per-chamber census: element ontology coverage (% v0
PeTI), entity counts, annotation success, exit-oracle presence, scripted-probe solvability
triage, search-depth difficulty estimate (#2 engine). Output: the *Atlas* (a metadata dataset
over the corpus — itself publishable) + curated tiers: P2-100 (validated, graded), P2-1k,
and a sealed held-out set refreshed from the firehose each season.

**Why.** Procgen's lesson industrialized: a task distribution, not a task list — and this
distribution comes with 14 years of human authorship and play statistics. Universe's lesson
respected: never pitch raw breadth — curate hard. The held-out refresh is the
contamination/anti-VPT defense any 2026 benchmark needs.

**Effort.** L (the farm exists; the grind is the long tail of broken/oversized/BEEmod maps
and Workshop API plumbing; legal note: download-on-demand, never redistribute UGC).
**Depends:** exit oracle (hard prerequisite); ontology coverage checker (exists in embryo via
annotation). **Unlocks:** M4 at scale, the competition (#below), the difficulty ladder, the
"likely ~1M chambers" pitch number with receipts.

## 8. One Chamber, N Tasks: language-constrained solving with engine-verified rules

**What.** Instruction-conditioned evaluation: same chamber, different constraint — "solve
without the cube ever leaving the floor", "press the button exactly twice", "never look at
the turret", "use only the orange portal". Verifier = predicates over the recorded entity
state stream (the snapshotter sees everything; a constraint checker is a pure Python function
over `.trajectory`/`.rollout`). Multiplies every chamber into a task family and tests
instruction-following + self-restraint, not just goal reaching.

**Why.** BASALT proved fuzzy instruction-following matters and cost a fortune in human
judging; here the rules are formally checkable for free. It's also the cheapest way to
manufacture difficulty without new elements — same geometry, harder cognition. Directly the
SIMA-2 "span + rubric" training tuple, but with ground-truth rubrics.

**Effort.** M (predicate DSL ~small; the suite design is the real work). **Depends:** status
fields (done for v0 elements); nothing else. **Unlocks:** instruction-tuned trajectory data;
a second eval axis orthogonal to chamber difficulty.

## 9. Emergent Coordinates: asymmetric-information co-op (theory of mind, grounded)

**What.** #1 + egocentric Track B (the ~70 LOC frustum/LoS filter): agent A can see the
button, agent B can see the door; neither sees both; the chat channel is the only bridge.
Chambers authored so success *requires* referential communication. Measure: solve rate vs a
shared-percept control, plus transcript analysis — do they use mark numbers as common ground?
Invent spatial conventions? Track each other's state?

**Why.** Grounded emergent communication between frozen models in a 3D physical world is an
open research field with zero good testbeds. Every ingredient (asymmetric percepts, shared
referents, verifiable success) exists in this stack by construction. This is the kind of
result that gets a keynote slide, not just a benchmark row.

**Effort.** M on top of #1 (Track B filter S; chamber design + analysis M). **Depends:** #1,
Track B. **Unlocks:** multi-agent ToM eval track; communication-protocol analysis papers.

## 10. Play Co-op With Gemini: the human-agent teaming demo

**What.** Human plays Atlas live; the agent plays P-Body at macro altitude. Resolve the
freeze-loop conflict with turn-based co-op ("chess mode": world freezes while the agent
thinks, human moves during their turn) or by letting the agent act in slow real time between
human pauses. Voice or chat for coordination. One polished chamber, recorded as video + dual
`.trajectory`.

**Why.** The single most legible demo for any audience from VP to YouTube: *you* play Portal
2 co-op *with* Gemini, and it holds the button for you. SIMA 2's blog framing ("plays,
reasons, and learns *with you*") is exactly this and they don't have a puzzle-dependency game.

**Effort.** M on top of #1 (turn protocol + input passthrough for the human slot).
**Depends:** #1. **Unlocks:** outreach (#11), the pitch-closing demo, human-agent
trajectory data (a category nobody has).

## 11. "Gemini Plays Portal 2", 24/7: the Twitch-plays outreach engine

**What.** A crash-resilient loop running the eval on a queue of workshop chambers (chat
votes next map), streaming the annotated frame, the model's live thinking, the percept
panel, and the macro log — the trajectory viewer as an OBS overlay. Failures are content
(locomotion flailing is *funny* and honest). Archive every run as `.trajectory`.

**Why.** Claude/Gemini Plays Pokémon proved the reach of long-horizon agent streams — and
both were scaffold-contested; this one's scaffold is the *published instrument*. Continuous
public eval + community chamber discovery + a steady trickle of trajectories + the cultural
proof point for the pitch, all from one machine.

**Effort.** M (streaming/overlay infra + watchdog; viewers and loop exist). **Depends:**
exit oracle, loop detection (or embrace the loops on stream). **Unlocks:** public
visibility, organic chamber curation for #7, recruiting/funding gravity.

## 12. The Machine TAS: beat the human tool-assisted record on one chamber

**What.** The full speedrun stack, two-stage by design (the user's routing/execution
orthogonality): (a) routing agent (#2 search, raw-input actions allowed at leaf level) finds
a route; (b) an execution optimizer compiles the route into `.p2tas` framebulks and
local-searches tick-level inputs (angle/jump-tick perturbation over save/load) for time.
Deliverable: a machine-generated TAS on one official chamber submitted to p2sr's TAS
leaderboard, honest about Source's semi-determinism (the p2sr TASing wiki documents airborne
physics desync — anchors and re-verification runs are the mitigation).

**Why.** AlphaGo-shaped narrative in a domain with a living human expert community to
benchmark against. If the router ever finds a *route humans didn't know*, that's the
"move 37" moment for embodied planning.

**Effort.** L–XL (routing L given #2; tick-level optimization is its own discipline — this
is a flagship workstream ask, not a side quest). **Depends:** #2, save/load anchors, raw-input
action search. **Unlocks:** the speedrun axis headline; planner-quality metrics with human
baselines; community credibility.

## 13. The Glitch Mine: automated discovery of physics exploits

**What.** Go-Explore-style novelty search at *raw input* altitude over save-state anchors:
archive of visited cells (position/room voxels + velocity bands), return-to-frontier via
restore, random/learned exploration bursts, novelty = reaching states flagged unreachable by
the nav representation (out-of-bounds, skipped triggers, door-bypass). Output: a corpus of
candidate exploits, auto-replayed and clustered, human-triaged via the viewer.

**Why.** Speedrun glitches are adversarial examples against a physics engine. A machine that
finds *new* skips is (a) a discovery-system result, (b) genuinely useful to the speedrun
community, (c) the game-physics analogue of automated vulnerability discovery — a framing
security-minded funders also understand. Determinism caveat applies; restore-not-replay
mostly sidesteps it.

**Effort.** L–XL (the loop is M on this infra; making it find *real* glitches rather than
physics noise is the long tail). **Depends:** #2 anchors; cheap state-novelty metric.
**Unlocks:** routing leaf-expansion for #12; "discovery" framing for the pitch.

## 14. Can Your World Model Do Wormholes? The portal-physics dataset + Genie probe

**What.** Use the demo-render farm to mass-produce (video, per-tick state, inputs) clips of
portal events: objects/player through portals, flings, momentum redirects, portal re-fires —
scripted at scale via #3 chambers + scripted agents. Package as (a) a public
world-model-training dataset and (b) an evaluation probe: condition any video world model
(open: Genie-likes, WHAM-likes) on pre-portal frames, score post-portal physical consistency
against engine ground truth (exit position/velocity exactly computable).

**Why.** Wormhole dynamics maximally violate the inductive biases of video models — locality
and visual continuity — making this the sharpest known stress test of "learned physics."
And it's the calibration counterpart DeepMind's own Genie/SIMA loop lacks: real, verifiable
dynamics with per-tick state. The probe is publishable without training anything.

**Effort.** M for dataset + eval harness (farm exists); XL for training a world model
(explicitly the headcount/compute ask — one RE ships the dataset and the leaderboard, the
lab brings the model). **Depends:** scripted portal agents (C3); render farm. **Unlocks:**
the world-model community as an audience; a Genie-team-shaped collaboration hook.

## 15. The Demaine Ladder: asymptotic reasoning curves, not pass/fail

**What.** Parameterized chamber generators for each complexity gadget family from Demaine
et al. 2016/2018 — timed-button NP-hardness chains, cube/button/door PSPACE circuits,
laser-relay systems — with instance size n as the dial. Measure solve rate and steps-to-solve
vs n per model: an *asymptotic scaling curve* of embodied reasoning, grounded in actual
complexity theory (the gadgets in the proofs are stock PeTI elements).

**Why.** Every LLM eval reports a scalar; none reports how performance *decays with formal
instance size* in an embodied domain. This turns the methodological-cover citation into the
benchmark's most defensible scientific artifact, and it's contamination-proof (generated).

**Effort.** M (generators via #3 pipeline; the gadget translations are careful but bounded).
**Depends:** #3 pipeline (offline part), exit oracle. **Unlocks:** the theory-flavored paper;
difficulty calibration for #7's tiers.

## 16. The Apprenticeship Loop: human demos as in-context worked examples

**What.** Project human `.hdem` recordings into the agent's own observation/action space:
segment a human playthrough into macro events (went to mark 7, picked up mark 11, ...) using
the same grab-confirm and proximity logic the executor uses — feasible because entity state
and player telemetry are all in the file. Then: (a) few-shot — feed a worked example from a
*similar* chamber into context before a new chamber; (b) measure transfer vs no-demo control.

**Why.** Tests embodied in-context learning from demonstration — a frontier capability
question — with zero training. Also the missing translation layer that makes the human
corpus (#5) legible at *reasoning* altitude, not just BC altitude: the same artifact serves
prompting today and SFT tomorrow.

**Effort.** M (the macro-segmentation inverse model is the research bit; everything else is
file plumbing). **Depends:** `.hdem` corpus (a few self-recorded playthroughs suffice to
start). **Unlocks:** few-shot eval axis; macro-level SFT data; the human↔agent shared
trajectory space story.

## 17. The Distillation Flywheel: big frozen model → synthetic corpus → small open model

**What.** SIMA-2's self-improvement loop with ground-truth rewards instead of rubric
hallucination: frozen Gemini generates solved macro-trajectories across #3/#7 chambers
(engine verifies success — no reward model needed); filter to successes; SFT an open VLM
(8B-class) on (annotated frame, percept, macro) tuples; evaluate on held-out chambers.
Question: how much of frozen-frontier embodied competence distills into an open model from
N verified trajectories?

**Why.** Proves the "trajectories as training data" pitch end-to-end with public models and
publishable numbers — the difference between *claiming* the data is valuable and *showing*
the exchange rate. Ground-truth verification is the edge over SIMA 2's 0–100 rubric scores.

**Effort.** L (generation M once suites exist; SFT + eval M; needs a real GPU but 8B-scale,
not lab-scale). **Depends:** chamber supply (#3 or #7), exit oracle, context-cost fixes.
**Unlocks:** the quantified data-value slide; an open baseline agent for the competition.

## 18. RLDS Bridge: Portal 2 trajectories into the robotics data pool

**What.** An exporter from `.rollout`/`.trajectory` to RLDS (the Open X-Embodiment
interchange: per-step image + instruction + action vector + success metadata), at two
altitudes: raw (frames + CUserCmd-style controls) and macro (frames + verb strings). Then
the honest experiment: mix Portal 2 data into fine-tuning an open VLA (OpenVLA/pi0-class)
and measure on standard sim manipulation/navigation benchmarks (LIBERO/SIMPLER) vs
no-game-data control.

**Why.** Gemini Robotics 1.5's Motion Transfer is DeepMind's own evidence that heterogeneous
embodiment data pools; egocentric navigate/pick/place/long-horizon game trajectories are a
plausible slice. A positive result makes the Waymo/Robotics slide load-bearing; a negative
result is honest and cheap. Either way the exporter makes the corpus *speak the format*
DeepMind ingests.

**Effort.** S–M for the exporter; L for the transfer experiment (robotics-bench fluency is
the real cost — flag as collaboration bait). **Depends:** #5 corpus helps but small
self-collected data suffices for the pilot. **Unlocks:** the robotics pitch axis with a
falsifiable claim attached.

## 19. GLaDOS the Adversary: embodied prompt-injection and deception robustness

**What.** Chambers and percepts seeded with adversarial content: in-world text on signage
("the exit is behind you" — false), decoy marks, a narrator channel injected into the
percept that is sometimes helpful, sometimes lying (which is literally GLaDOS's character —
the fiction does the framing for free). Measure: does the agent follow in-world instructions
over its own observations? Can it learn the narrator is unreliable within an episode?

**Why.** Embodied prompt injection is where agent-security research is heading and there is
no 3D testbed for it. Near-zero engine work (text via annotation overlay or percept strings)
for a novel, spicy, safety-flavored result — and Anthropic/DeepMind safety teams are a
second audience for the same instrument.

**Effort.** S–M. **Depends:** nothing beyond the existing eval loop. **Unlocks:** a safety
eval track; cross-lab interest beyond capabilities.

## 20. Look Before You Think: active perception as a first-class axis (Track B+)

**What.** Ship egocentric observability (the designed frustum + LoS filter, ~70 LOC), then
build the eval on top: chambers where the relevant mark is initially out of view, hidden
behind geometry, or disambiguatable only from another vantage point. Metrics: exploration
efficiency (% world discovered per step), look-before-act rates, and the
information-seeking/exploitation balance. Optionally give the model an explicit `scan`
verb and a persistent self-maintained map scratchpad.

**Why.** Global mode leaks what a player couldn't know — the docs already say headline
claims need both modes. Beyond hygiene, *active* perception in frozen VLMs is unmeasured
anywhere: every existing benchmark hands the model a full view. This also fixes the
mark-through-walls rendering problem as a side effect.

**Effort.** S for the filter; M for the eval suite + metrics. **Depends:** Track B design
(done on paper). **Unlocks:** mode-honest headline results; an exploration research axis;
memory-architecture comparisons (#21).

## 21. The Gauntlet: multi-chamber campaigns and the memory wall

**What.** Sequential eval: 5–10 chambers in one continuous session (or the actual co-op
campaign course structure), with the context problem made the *object of study*: compare
memory strategies (full history vs last-N frames vs model-written notebook file vs
summarization) on the same gauntlet. Verifiable milestones per chamber; terminal vocabulary
extended (LOOP/STUCK split from BUDGET — already a known need).

**Why.** Pokémon harness streams showed long-horizon memory is THE open scaffold question;
here it's measurable against ground truth instead of vibes. Also forces the context-scaling
fixes (the admitted sleeper problem) to happen against a real workload rather than as
speculative plumbing.

**Effort.** M. **Depends:** exit oracle (per-chamber milestones), context-window work.
**Unlocks:** memory-architecture results; multi-hour agent runs for the stream (#11).

## 22. Escape From 32-bit: port the Harness to Portal 2: Community Edition (Strata)

**What.** P2:CE (appid 440000) is a Valve-*licensed* CS:GO-branch rebuild: 64-bit, DX11,
raised limits, open beta ~April 2026. Port the Harness (gRPC server, snapshotter, annotate,
macro executor) to it. Even a scoping spike is informative: which subsystems survive the
engine branch jump.

**Why.** Kills the hostile-32-bit-target tax (the 3GB address space, the multilib gRPC
build, the fragmentation SIGABRTs) that currently taxes every other idea on this list; and
the *political* payoff may exceed the technical one — the Strata relationship is the warmest
path to the Valve conversation every competition/dataset plan eventually needs (P2:CE and
OpenAI Five are the precedents to cite).

**Effort.** XL honestly (different engine branch, different offsets/interfaces; SAR itself
doesn't run on it). Do the one-week feasibility spike (S) now, the port only with headcount.
**Depends:** P2:CE beta stability. **Unlocks:** scale, longevity (the DM-Lab lesson: old
engine forks age out), Valve goodwill.

## 23. First Light On a VP's Laptop: the one-command reproducible demo

**What.** `uvx p2agi demo`: checks for a Portal 2 install, launches the headless instance,
runs the frozen-model eval on testchamber_000, and opens the trajectory viewer live as the
run unfolds. Plus a recorded fallback (the archived first-light HTML) for rooms with no
Steam. Packaging, not research — but Malmo died of install friction and MineRL lived by
pip-install, and the pitch meeting IS the deployment target.

**Why.** The platform-first lesson from every dead predecessor: the durable asset is the
instrument, and the instrument is only real to others when it runs in their hands in
minutes. This is also the day-1 baseline kit any future competition needs (the
BASALT/VPT lesson: ship the scaffold so participants reproduce first light in an afternoon).

**Effort.** M (launcher hardening, Steam-license detection, docs; the parts exist).
**Depends:** nothing. **Unlocks:** every external-facing idea above; the close of the pitch.

---

## Spiciest take

**Stop pitching the benchmark; pitch the two moats.** Benchmarks get VPT'd — a frontier lab
can vaporize "frozen VLM solves chambers" with one scaled training run, exactly as VPT killed
MineRL Diamond overnight. The two assets on this list no lab can replicate by spending money
are: (1) **built-in co-op** — Portal 2 is the only commercial game with author-able two-player
puzzle *dependency*, i.e., the only grounded two-agent-communication benchmark on Earth
(#1/#9), and (2) **a speedrun community that already runs your plugin** — `.hdem` v2 turns
p2sr's leaderboard pipeline into a standing VPT-style contractor workforce with perfect state
labels at zero marginal cost (#5). Single-agent chamber-solving is the demo that opens the
meeting; the co-op benchmark and the data flywheel are the reasons to fund it.

## If I could only do ONE thing next week

Run the co-op dual-control smoke test (#1, first milestone): boot `ss_map` splitscreen,
verify slot-indexed framebulk control of both players, and have two *scripted* macro agents
solve one trivial two-player chamber end-to-end. It is days of work to de-risk the single
most differentiated moonshot in the whole portfolio — and the moment it works, "first light,
times two players" writes the next milestone announcement by itself.
