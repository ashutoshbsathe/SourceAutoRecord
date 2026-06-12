# Lens: pitch-strategy — how to turn first light into headcount

*Diverge-mode brainstorm, 2026-06-12. One of ten council lenses. This lens owns the VP Research
narrative: hook, thesis, why-now, why-Portal-2, velocity story, the ask, milestones, demo, risks,
metrics, and how far to push "AGI via Portal 2."*

**Effort key:** S = days, M = weeks, L = months, XL = needs a team — all at one-RE-plus-agentic-coding staffing.

---

## The framing problem this lens has to solve

The pitch has a structural trap baked into its best result. "A frozen Gemini solved the puzzle in
7 steps" reads, to a VP, as *"the model already works — why do you need headcount?"* The honest
finding — reasoning solved, locomotion the wall — must be inverted in the narrative: **lead with
the wall, the generalization cliff, and the data engine; use the solve as proof the instrument
works, not as the deliverable.** Every idea below serves one of three pitch jobs:

1. **Harden the kernel** — turn a single-model, single-chamber anecdote into a cross-model finding before anyone senior sees it.
2. **Map to budget lines** — sell into slots the VP already funds (SIMA, Game Arena, Genie, Robotics), never into "RL benchmark," a category both OpenAI and DeepMind exited.
3. **Make the ask legible** — headcount mapped to de-risked workstreams, milestones with kill criteria, and a demo only this project can do.

---

## Ideas (ordered roughly by leverage)

### 1. Multi-model result table: the pre-pitch hardening that matters most
**What:** Run the existing first-light chamber with Claude and a GPT-class model alongside Gemini.
The agent seam is already one callable (`agent(obs)` in `run_eval.py`); `header.model` already
handles arbitrary names. Minimum version: 3 models × 1 chamber × 3 seeds (days). Full version:
3 models × ~10 tiered chambers once the suite exists (weeks, depends on exit oracle).
**Why it matters:** A single-model result is an anecdote about Gemini; the same reasoning-solved/
locomotion-blocked signature across three frontier models is a finding about *embodied reasoning*,
and the instrument becomes the story. Politically: if Gemini tops the table, you walk into DeepMind
showing their model winning on your instrument — the best possible valence.
**Effort:** S (minimum) → M (full table).
**Dependencies:** none for minimum; exit oracle + 5–10 chambers for full.
**Unlocks:** the headline slide; the BALROG-style credibility; idea #2's metric has something to average over.

### 2. The knowing-doing-gap scalar as THE headline number
**What:** Implement the BALROG-proposed-but-unbuilt metric offline over `.trajectory` files: per
step, classify (plan-correct ∧ actuation-failed) vs (plan-wrong) vs (plan-correct ∧ executed) —
the thinking text + result codes are all captured, so an LLM-judge pass over logged trajectories
computes it with zero game time. First light renders as: "from step 8 on, plan correctness ~100%,
actuation success ~0%."
**Why it matters:** VPs fund numbers, not vibes. One scalar that *is* the thesis ("the gap between
knowing and doing, measured") travels through review chains intact. It also pre-empts "isn't this
just a harness quirk?" — the metric explicitly conditions on the plan being right.
**Effort:** S–M (judge prompt + offline pipeline over existing trajectories).
**Dependencies:** more trajectories (idea #1) to make the number robust.
**Unlocks:** the abstract of the eventual paper; the per-step verdict sidecar the viewer design already reserved.

### 3. Three-tier live-demo plan, climaxing in "the VP authors a chamber"
**What:** A rehearsed demo ladder for the meeting. Tier 1 (zero risk): the archived first-light
viewer HTML — scroll the model's mind, step 7 and step 12. Tier 2 (low risk): a live run on a
chamber recorded that morning, projected via the trajectory viewer's live mode (~50-line FastAPI,
pre-designed). Tier 3 (the closer): **the VP opens the in-game Puzzle Maker, drags a cube, a
button, a door; five minutes later the agent attempts their chamber live.** No other benchmark on
earth offers authorship-to-eval inside one meeting — not Minecraft, not NetHack, not any custom sim.
**Why it matters:** Tier 3 demonstrates the entire flywheel (UGC → eval → trajectory) viscerally,
and it makes the held-out-test-set argument *physical*: "you just created a chamber that has never
existed; no training set contains it."
**Effort:** M — tier 3 needs pathfinding `go_to` (the #1 lever anyway), the exit oracle, and a
PeTI→harness load path hardened against demo-day failure. Tiers 1–2 are S.
**Dependencies:** pathfinding go_to, exit oracle (both already the roadmap's top items — the demo
and the science want the same two things).
**Unlocks:** the meeting's memorable moment; also the seed of the competition's onboarding flow.

### 4. The pitch-slot mapping memo: sell into four existing budget lines
**What:** One page mapping each project asset to a slot DeepMind already funds: (a) **SIMA** —
macro-grammar agent in a commercial 3D game, with the ground-truth entity state and causally-correct
auto-annotations SIMA paid humans for; (b) **Kaggle Game Arena** — spectator-legible, Hassabis-endorsed
games-as-eval, and Portal 2 adds embodied physics to a lineup of board games; (c) **Genie** — a real
deterministic engine with per-tick state as the *verifier/calibrator* their generated-world training
loop publicly lacks; (d) **Gemini Robotics** — first-person, physics-grounded, input-synchronized
trajectories at near-zero marginal cost vs dollars-per-minute teleop, exportable to RLDS.
**Why it matters:** "RL environment benchmark" is a dead procurement category; these four are live
ones. The memo also tells you *which VP* — the slots have different owners, and the pitch should be
tuned to whoever is in the room.
**Effort:** S.
**Dependencies:** none.
**Unlocks:** the deck skeleton; the pre-read (idea #16); clarity on the audience.

### 5. The cold open: read the model's mind for 90 seconds
**What:** Open the meeting with zero preamble: the trajectory viewer on screen, scrolling steps
0→7 — the model's actual thinking text beside the annotated frames — pausing on step 7 (button
pressed) and step 12 ("we bumped the cube off the button" → recovers). *Then* the title slide.
**Why it matters:** The artifact already exists and is the project's single most persuasive object.
Leading with evidence before claims flips the room from skeptical to curious. The step-12 recovery
is the moment that does the work: unprompted physics-side-effect tracking is what "embodied
reasoning" *looks like*, and no slide can say it better.
**Effort:** S (curation + rehearsal; the HTML is archived).
**Dependencies:** none.
**Unlocks:** the first two minutes; reusable in every future telling.

### 6. The 90-second sizzle video
**What:** A rendered cut from the first-light trajectory: annotated agent view, thinking stream as
subtitles, the derived top-down map alongside, covering steps 0–7 + step 12 + ten seconds of the
locomotion flail (honesty is part of the aesthetic). Everything needed is already in the
`.trajectory`; this is an offline render script.
**Why it matters:** VPs forward videos, not repos and not 9MB HTML files. This is the asset that
travels the corridor after the meeting, and the one that goes to the SIMA TL with the pre-read.
**Effort:** S.
**Dependencies:** none.
**Unlocks:** pre-meeting buzz, post-meeting forwarding, eventual blog/launch material.

### 7. "Why Portal 2" one-pager (the comparison-table slide)
**What:** A disciplined table vs the alternatives a VP will name: **Minecraft** (saturated; VPT got
diamonds; no formal difficulty grounding), **NetHack** (unsolved but symbolic — no physics, no
vision, no robotics story), **custom sim / Genie worlds** (no humans, no UGC, no ground truth /
hallucinatable physics), **Portal 2**: deterministic-steppable Source engine, tick-perfect state,
built-in Puzzle Maker, ~950k workshop items (third-largest on Steam — cite the Mercencium scrape,
mid-2024), PSPACE-complete stock elements (Demaine et al., FUN 2018), first-person physical
embodiment, and a plugin harness needing zero studio partnership (vs SIMA's negotiated access).
**Why it matters:** "Why not Minecraft?" is the first question. Having the answer as one visual
ends the digression in thirty seconds. Caveats stated proactively (semi-determinism of airborne
physics; 32-bit engine) buy credibility for the strong claims.
**Effort:** S.
**Dependencies:** none (verify the live workshop count before printing it).
**Unlocks:** the why-here slide; reusable in the paper's related-work framing.

### 8. The velocity exhibit: quantify one-RE-plus-agentic-coding
**What:** Mine the git history for the receipts: calendar span, commit cadence, LOC across C++
harness / proto / Python stack, and the catalogue of shipped subsystems (in-engine annotation,
macro executor, three data formats, two viewers, an RL stack, an eval loop). Present as "what one
research engineer plus agentic coding shipped in N weeks" — then do the headcount math out loud:
*each marginal hire at this leverage is worth a small traditional team.*
**Why it matters:** This is the meta-pitch. The VP isn't just buying Portal 2 — they're buying
evidence about what agentic-coding-leveraged headcount yields, which is itself a question every
research org is currently pricing. It also reframes the small team ask as conservative.
**Effort:** S.
**Dependencies:** none.
**Unlocks:** the ask slide's credibility; a second conversation ("how do you work?") that builds champions.

### 9. The headcount ask as five named workstreams, each de-risked by an artifact
**What:** Structure the ask as workstreams with profiles, each anchored to something already built:
(1) **Engine/harness RE** (C++, game internals) — extends MacroExecutor/snapshotter; de-risked by
the working harness. (2) **Eval scientist** — owns chamber suite, metrics, multi-model runs;
de-risked by first light + trajectory tooling. (3) **Locomotion/RL researcher** — pathfinding
verbs, RL-as-actuator; de-risked by the dormant-but-real PPO stack and the InferenceServer seam.
(4) **Data engineer** — .hdem v2, RLDS export, human corpus ops; de-risked by the working
recorder + render farm. (5) **0.5 community/competition ops** — Workshop ingestion, leaderboard,
Valve relationship. Total: 4.5 + the founder.
**Why it matters:** "Give me headcount" fails; "here are five named seats whose first quarter is
already specced and whose risk is already retired by a working artifact" is how asks get granted.
The structure also shows the founder has thought like a manager, which is part of what's evaluated.
**Effort:** S (writing; the de-risking artifacts exist).
**Dependencies:** ideas #1, #3 strengthen seats 2 and 3.
**Unlocks:** the ask slide; the org plan for month 1 after a yes.

### 10. 3/6/12-month milestone plan with explicit kill criteria
**What:** 3mo (founder alone, proves trajectory): pathfinding `go_to` + exit oracle + 30-chamber
tiered suite + 3-model table + knowing-doing-gap report. 6mo (+2 hires): public leaderboard,
human `.hdem` pilot corpus (50–100 hours via the speedrun community), cross-model paper submitted.
12mo (full team): NeurIPS competition proposal, RL-locomotion-as-actuator closing the first-light
wall, RLDS export pilot with a robotics team as internal customer. Each milestone carries a kill
criterion — e.g., "if frontier models saturate tier-3 chambers by month 4, pivot the headroom story
to the routing/speedrun axis"; "if Valve declines a research conversation, scope to owned+licensed
chambers only."
**Why it matters:** Kill criteria are the highest-signal credibility device available to a solo
pitcher: they prove the plan is falsifiable and the founder will not ride a dead thesis. VPs fund
de-risking schedules, not visions.
**Effort:** S.
**Dependencies:** consistency with ideas #9, #12.
**Unlocks:** the plan slide; the quarterly check-in structure after funding.

### 11. The Demaine-gadget chamber generator
**What:** Programmatic `.p2c` generation (the format is documented plain-text KeyValues; libraries
exist) of chambers built from the *exact gadget families in the PSPACE-completeness proof* —
cube/button/door gadgets composed into instances of scalable formal hardness. Ship a
`gen_chamber --tier N --seed S` CLI.
**Why it matters:** The pitch line writes itself: **"our test set is sampled from the same gadget
grammar the complexity proof uses — difficulty is a dial, contamination is impossible, and the
supply is infinite."** It upgrades the held-out story from "950k maps of unknown quality" (the
Universe trap: raw breadth) to a *generator* (the Procgen lesson: distributions beat lists). Also
the formal-CS aesthetic plays extremely well with a DeepMind research audience.
**Effort:** M (p2c emitter + gadget templates + compile/load/validate loop).
**Dependencies:** exit oracle to auto-validate generated chambers are solvable/scoreable.
**Unlocks:** the benchmark's difficulty ladder; competition test-set refresh; a standalone artifact
reviewers love.

### 12. The risk register slide — stated before they ask
**What:** Five risks with named mitigations: (a) **Valve/IP** — no research carve-out exists;
mitigations: OpenAI Five precedent, CSGO-dataset precedent, P2:CE license precedent, SAR's decade
of tolerated existence, and a planned direct Valve conversation (DeepMind's name helps; SIMA shows
labs do studio deals). (b) **Engine determinism** — airborne-physics desyncs are documented by the
speedrun community; mitigation: macro-level eval doesn't need tick-perfect replay; state-truth is
recorded per-tick. (c) **VPT-moment obsolescence** — a frontier lab one-shots the headline task;
mitigation: generator-refreshed held-out sets + the unbounded routing axis + "the instrument then
measures *their* result." (d) **Bus factor = 1** — that *is* the ask. (e) **32-bit hostile target** —
mitigated and documented (perf post-mortems); P2:CE (64-bit, Valve-licensed Strata engine) named as
the future substrate watch-item.
**Why it matters:** A solo pitcher with a real risk register reads as a future TL; one without
reads as a hobbyist. Naming Valve first denies the room its easiest derail.
**Effort:** S.
**Dependencies:** none.
**Unlocks:** trust; the legal/partnership follow-up thread with the right framing already set.

### 13. The claim ladder: calibrating "AGI via Portal 2"
**What:** A written discipline for how far each claim goes. Rung 1 (defensible, lead with it):
*a failure-attribution instrument for embodied reasoning in frozen frontier models.* Rung 2
(strong): *a contamination-resistant generalization benchmark with formally-grounded difficulty and
infinite UGC refresh.* Rung 3 (aggressive, say once): *an embodied-trajectory data engine relevant
to SIMA/Robotics.* Rung 4 (never as a claim): "AGI via Portal 2" — permitted exactly once, as the
self-aware title of the closing slide, delivered with a smile. Each rung gets its supporting
evidence and its known holes (e.g., rung 3 needs the .hdem-v2 action stream before it's real).
**Why it matters:** The fastest way to lose a VP Research is one overclaim early; the second
fastest is underclaiming into "nice hobby." A pre-committed ladder lets the pitch run hot without
ever being catchably wrong, and it keeps every future public artifact (blog, paper, tweet) consistent.
**Effort:** S.
**Dependencies:** none.
**Unlocks:** message discipline across deck, memo, paper, and demo narration.

### 14. The why-now slide: the environment procurement wave + Genie's missing verifier
**What:** Three dated facts on one slide: (a) environments became a procurement category in
2025–26 (reported >$1B/yr discussions at Anthropic; Prime Intellect's environment hub; Mechanize),
(b) DeepMind leadership publicly endorses games-as-evergreen-eval *right now* (Kaggle Game Arena,
Aug 2025 →), (c) the SIMA 2 + Genie 3 "infinite training loop" (Jun 2026 press) concedes no proven
transfer and evaluates in worlds with *hallucinatable physics* — a real deterministic engine with
ground-truth state is the missing verifier, and it costs them nothing to adopt.
**Why it matters:** Why-now is the difference between "interesting" and "urgent." The Genie-verifier
angle is the sharpest: it positions Portal 2 not as competing with DeepMind's world-model strategy
but as the *calibration instrument* that strategy publicly lacks.
**Effort:** S (verify each citation date before the meeting).
**Dependencies:** none.
**Unlocks:** the urgency beat; the bridge into the pitch-slot memo (#4).

### 15. The data-engine annex: .hdem v2 actions + RLDS export + pilot human corpus
**What:** Three concrete steps that make the training-data story real instead of gestural:
(a) add the per-tick input record to `.hdem` (format is versioned; the CUserCmd hook already
exists) — human play becomes engine-free (obs, action, state) extraction at I/O speed; (b) write
an `.rollout`→RLDS exporter so trajectories land in the exact interchange format Open X-Embodiment
uses; (c) record a pilot corpus — the p2sr speedrun community is reachable, instrumented (they
already run SAR!), and produces expert-level play; 50–100 hours with consent and attribution.
Pitch line: *"VPT needed 2,000 contractor hours to seed its inverse-dynamics model; every SAR user
is already running our recorder's host plugin."*
**Why it matters:** Rung 3 of the claim ladder is vapor without this; with it, the pitch carries a
sample RLDS shard a robotics team can load that afternoon. It is also the unique asset no frozen-eval
competitor has: the eval harness and the data recorder are the same instrument.
**Effort:** M for (a)+(b); L for the corpus ops (consent, curation, hosting).
**Dependencies:** none technical; community outreach for (c).
**Unlocks:** the Robotics/SIMA budget-line story; offline-RL/BC experiments; the VPT-style
pseudo-labeling pitch over 15 years of YouTube Portal 2 video.

### 16. The pre-read: a SIMA-2-gap technical memo, pre-wired through a TL
**What:** A two-page memo written for the SIMA/Genie technical leads (not the VP), enumerating
their published limitations — Gemini-rewarder rubric hallucination, qualitative-only Genie evals,
language-annotation cost, no ground-truth state — and showing, with proto snippets and a trajectory
excerpt, how the harness addresses each. Send it ahead through whatever network path exists; the
goal is that someone technical has *already validated it* before the VP meeting, because VPs
decide on the recommendation of a TL they trust.
**Why it matters:** Cold pitches to VPs die in the follow-up ("have my team look at it") unless the
team already has. Pre-wiring converts the meeting from evaluation to confirmation.
**Effort:** S.
**Dependencies:** #4 (slot mapping) supplies the structure; #6 (video) rides along.
**Unlocks:** an internal champion; technical questions surfaced before they can kill the meeting.

### 17. The routing axis as the anti-saturation slide (+ MCTS teaser)
**What:** One slide: chess saturated in a week of Game Arena; NetHack is unsolved after six years;
Portal 2 *routing* (find the fastest/glitch route, then execute tick-perfectly) is open-ended
optimization against a live human leaderboard — headroom is structurally unbounded and the human
baseline ladder already exists (speedrun.com). Technical teaser if time permits: world-freeze +
engine save/load anchors (~40 LOC, designed) makes tree search over macro sequences nearly free —
a best-of-N routing agent over a small chamber as a video clip.
**Why it matters:** "What happens when models saturate your benchmark?" is a guaranteed question;
this is the answer that converts saturation from a risk into a roadmap. It also recruits a second
researcher archetype (search/planning people) into the project's orbit.
**Effort:** S for the slide; M for the MCTS teaser demo (needs anchors C9).
**Dependencies:** save/load anchors for the teaser.
**Unlocks:** the headroom narrative; the speedrun community alliance; a planning-research workstream.

### 18. Competition positioned as marketing; platform as the deliverable
**What:** State the lesson of the graveyard explicitly in the pitch: every dead competition
(ViZDoom, MineRL, NetHack Challenge) left a living platform — so the *deliverable* is the harness,
suite, formats, and viewers; the NeurIPS competition is year-one marketing for it. Budget realism:
$10–20k prizes (the proven norm), Kaggle/AIcrowd hosting (near-zero organizer eval cost,
BALROG-style rolling leaderboard rather than annual event), co-authorship on the retrospective as
the cheap incentive that worked for Melting Pot, two tracks (frozen-VLM API-budget-capped;
open/learning) per the NetHack/MineRL splits, and a baseline agent so day-1 entrants reproduce
first light in an afternoon.
**Why it matters:** It shows the founder has metabolized why predecessors died, and it right-sizes
the competition ask (ops headcount + small prize budget) instead of letting it balloon into the
main cost line.
**Effort:** S to position; L–XL to actually run one (that's seat 5 in idea #9).
**Dependencies:** chamber suite, exit oracle, baseline-agent packaging.
**Unlocks:** the community flywheel; external legitimacy via NeurIPS; the test-set refresh ritual.

### 19. The alternative top-frame: "fund the instrument, not the result"
**What:** A second full framing of the deck, held in reserve: the product is **failure attribution
for embodied agents** — any agent team (SIMA, Robotics, Gemini agents) points their model at the
harness and gets back *which layer failed* (perception / reasoning / actuation), per step, with
receipts. The benchmark, the competition, and the data engine become *applications* of the
instrument rather than the headline.
**Why it matters:** Instruments get embedded in other teams' workflows and survive strategy
shifts; results get cited once. If the room signals "we have enough benchmarks," this reframe is
the live pivot — and it's truer to what was actually built (the macro boundary *is* the instrument).
**Effort:** S.
**Dependencies:** #2 (the metric) is the instrument's readout.
**Unlocks:** resilience in the room; a services-to-internal-teams adoption path that needs less headcount.

### 20. Pre-empting the scale objection: the "win either way" rebuttal
**What:** A prepared answer to "won't SIMA 3 / the next Gemini just solve this with scale?" —
(a) the generator + UGC refresh make the test set scale-resistant by construction; (b) the routing
axis has no known ceiling; (c) and if scale *does* solve it, the harness is precisely the
instrument that proves it, attributes it, and produces the trajectories — the instrument
appreciates in value when models improve. VPT vaporized MineRL's task but made Minecraft data
*more* valuable; same shape here.
**Why it matters:** This objection is otherwise fatal-by-default in a frontier lab, where the house
view is "scale wins." The rebuttal converts the house view into an argument *for* funding.
**Effort:** S.
**Dependencies:** #11 and #17 supply the substance.
**Unlocks:** survival of the hardest question in the room.

### 21. The numbers slide: instrument throughput, cost, reliability
**What:** Measure and publish the operational stats a platform pitch needs: steps/sec under
frozen-step deliberation, parallel instances per box (the launcher already does N), wall-clock and
token cost per eval run (first light: 686k in / 6.6k out — and the planned context-window fix's
projected savings), bytes per trajectory, time-to-add-a-verb (one VERB_SPECS entry + one C++ case),
time-to-add-a-chamber. Frame against the graveyard: Universe died of flaky, unsteppable breadth;
this is synchronous, deterministic, and cheap — say so with numbers.
**Why it matters:** Eval cost killed or constrained MineRL, BASALT, and Procgen; a VP who has
funded competitions will probe ops cost immediately. Numbers here also quietly demonstrate
engineering seriousness no adjective can.
**Effort:** S (instrumentation + a benchmark run).
**Dependencies:** none.
**Unlocks:** the ops-credibility beat; capacity planning for the competition ask.

### 22. The failure museum appendix
**What:** A curated gallery, mined from trajectories, of *named* failure classes with one viewer
permalink each: the locomotion loop (first light steps 8–24), dead-end fixation, plan-correct/
actuation-blocked, physics side-effect recovery (step 12 — a *success* class worth exhibiting),
and whatever multi-chamber runs surface. Each class annotated with which proposed workstream
addresses it.
**Why it matters:** Each failure class is a research agenda line item that justifies a seat in
idea #9 — the museum is the headcount ask, evidenced. It is also the embryo of the paper's
taxonomy section and the labeling schema for the verdict-sidecar dataset.
**Effort:** M (needs the additional runs from #1 first).
**Dependencies:** #1; the viewer's fails-only filter already exists.
**Unlocks:** the evidence base for the ask; the failure-taxonomy paper section; verdict labels.

### 23. The REPL moment: the VP drives one macro by hand
**What:** A 60-second demo beat inside tier 2 of #3: hand the VP the macro REPL (`macro_repl.py`,
already the same loop as the agent) and have them type `go_to 7` themselves, watching the engine
execute it. *Then* show the model doing the same thing with its reasoning visible.
**Why it matters:** Nothing communicates an action space like using it. It also demonstrates the
human/model symmetry that underlies the whole data story — humans and models act through the
identical interface, so human trajectories are in-distribution by construction.
**Effort:** S (rehearsal only; the tool exists).
**Dependencies:** a live instance in the room (tier-2 demo infra).
**Unlocks:** visceral understanding; the "same interface for humans and models" pitch point, embodied.

### 24. Name it and ship the landing page before the meeting
**What:** Pick a name that travels (BALROG, Crafter, ViZDoom all out-traveled their papers), and put
up a minimal page: the sizzle video, the first-light trajectory viewer, the comparison table, a
"benchmark coming" leaderboard skeleton, and a contact link. Also stake the GitHub org name.
**Why it matters:** Crafter's lesson: zero-friction adoption by individual researchers can
out-impact an organized competition at near-zero cost. For the pitch specifically, a live page
makes the project feel inevitable-with-or-without-funding — which is, paradoxically, the state in
which funding is easiest to get.
**Effort:** S.
**Dependencies:** #6 (video), #5 (viewer artifact).
**Unlocks:** discoverability; the perception of momentum; a URL on the closing slide.

---

## Spiciest take

**The project's best result is its biggest pitch risk.** "A frozen Gemini solved the chamber"
argues, on its face, for *zero* headcount — the model already works. The pitch must be built on the
wall, not the win: the knowing-doing gap, the 950k-chamber generalization cliff nobody has
measured, and the data engine. If the deck leads with the solve and not the gap, the meeting's most
likely good outcome is a smile and "keep us posted" — first light is the *credential*, the wall is
the *business*.

## If I could only do ONE thing next week

Run the first-light chamber with Claude and a GPT-class model (the agent is one callable — days,
not weeks), compute the knowing-doing-gap scalar across all three trajectories, and cut the
90-second sizzle from the best run. That single move converts a one-model anecdote into a
cross-model finding with a headline number and a forwardable artifact — the minimum kernel every
other pitch idea builds on.
