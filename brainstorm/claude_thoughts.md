# VP pitch narrative — the instrument, the wall, and the data engine

*Drafted 2026-06-12 from the council output (`.council/diverge/`, `.council/critique/`) and
`first_light_and_next_steps.md`. This is the narrative I will adapt into the actual deck. Voice:
first person. Discipline: every claim is either evidenced in-repo, cited, or explicitly flagged.
Target: readable in ten minutes.*

---

## 1. The hook: first light, told honestly

Yesterday a frozen, untrained `gemini-3.5-flash` — no fine-tuning, no reward, no examples —
solved a Portal 2 test chamber from pixels. I handed it an annotated screenshot, a list of
numbered entity marks, and nine semantic verbs (`go_to`, `pick_up`, `aim_at`, `release`, …)
executed by the game engine itself. In **7 steps, with zero invalid actions and zero retries**,
it walked in, found the cube, grabbed it, carried it to the button, and pressed it. At step 12 it
noticed something nobody told it to watch for: *"we bumped into the cube and pushed it off the
button"* — a physics side-effect of its own body — and it re-placed the cube. You can read its
mind doing this, step by step, in the trajectory viewer; I will open the meeting with that
artifact, not a slide.

Then it spent the remaining 18 steps unable to **walk out of a glass enclosure**. My `go_to` is a
straight-line march with no pathfinding — deliberately, because perfect pathing would smuggle
puzzle-solving into the actuators — and the model's correct plan died at the legs, every step
returning `BLOCKED`.

I want to be precise about what this is and isn't. It is **N=1**: one model, one author-built
chamber, one seed, on a game whose walkthroughs saturate every pretraining corpus. "Reasoning is
solved" is not a claim I will make; it is a hypothesis the instrument now lets me falsify. What
first light *does* prove is the thing I actually built: **a harness that attributes failure to a
layer**. Clean percept + wrong verb = reasoning gap. Right verb + `BLOCKED` = actuation gap. The
first real run produced exactly that separation — plan correct, legs broken — on contact with
reality. The solve is the credential. The wall, and the instrument that isolated it, is the
business.

## 2. The thesis

**One sentence:** a pausable, ground-truth-instrumented commercial 3D game with a level editor
built into the game and roughly a million user-authored puzzles is simultaneously the most
contamination-resistant embodied-reasoning benchmark available and the cheapest source of
perfectly-labeled embodied trajectories a frontier lab can buy — and one engineer has already
built the instrument and gotten first light through it.

Why Portal 2, specifically, and not Minecraft / NetHack / a custom sim:

- **Authorship is in-game.** The Puzzle Maker (PeTI) ships inside retail Portal 2; the `.p2c`
  level format is documented plain text. The Steam Workshop held **954,393 Portal 2 items as of a
  mid-2024 scrape** (third-largest workshop on Steam) — *flag: the live 2026 count and the usable
  puzzle fraction are unverified; I will pitch "950k raw, N verified" only after my triage
  pipeline produces N.* The deeper asset isn't the count — it's the **firehose**: chambers
  published last week cannot be in any training set. The held-out test set refreshes itself.
- **Formal difficulty grounding.** Demaine, Lockhart & Lynch (FUN 2018) prove that stock PeTI
  element *families* — cubes+buttons+doors, lasers+relays — are PSPACE-complete. Carefully
  stated: any finite chamber is O(1); the claim is that **the difficulty ladder has no structural
  ceiling**, because the gadget family scales. No other game benchmark has per-tier complexity
  classes.
- **Non-Euclidean spatial reasoning.** Portals — momentum-preserving wormholes you place
  yourself — test spatial and physical reasoning that no existing benchmark touches, and that
  generative world models are publicly weakest at (nothing in their video distribution looks like
  a portal transit). A ground-truth portal-transit prediction set is an eval Genie-class models
  cannot self-grade.
- **Ground truth, per tick.** The harness reads server entity state directly: button pressed,
  cube activated, door open are engine facts, not rubric scores. A SIMA-2-style
  generate/filter/distill loop requires a non-hallucinatable reward; `chamber_complete` is one,
  a Gemini-graded rubric is not.
- **The data engine is the same instrument.** The `.hdem` recorder captures human play with
  per-tick entity ground truth at ~MB per playthrough; auto-annotation from engine events
  ("player placed cube on button; door opened") produces the causally-correct language spans SIMA
  paid human annotators for. Humans and models act through the *same* macro interface, so human
  trajectories are in-distribution by construction. And because the engine is pausable and
  save/load-branchable, it can emit **counterfactual branches from identical physical states** —
  DPO/process-supervision data reality cannot produce at any price, because reality doesn't
  reload.

The framing I am *not* using: "an RL benchmark." That category is dead — both our lab and OpenAI
exited it. What I built maps onto four budget lines DeepMind funds today: **SIMA** (commercial-game
agents, minus the studio negotiation — this needs no studio deal, it's a plugin), **Kaggle Game
Arena** (spectator-legible games-as-eval, plus physics), **Genie** (a real deterministic engine as
the verifier/calibrator the generated-world loop publicly lacks), and **Gemini Robotics**
(first-person, input-synchronized, state-labeled trajectories at cents-per-hour against
teleoperation at dollars-per-minute).

## 3. Why now, why us

**Why now.** Environments became a procurement category in 2025–26 (the startup wave around RL
environments; our own leadership publicly endorsing games-as-evergreen-eval via Game Arena since
Aug 2025). SIMA 2 (Dec 2025) is architecturally *this system one level lower* — Gemini emitting a
parsed command grammar into commercial games — trained on licensed human demos and
Gemini-synthesized annotations, evaluated partly in Genie 3 worlds with no ground-truth physics.
Every published limitation of that stack (rubric hallucination, annotation cost, no engine state,
qualitative-only world-model evals) is a thing this harness provides natively. The fit is not
hypothetical; it is line-by-line.

**Why us.** We are possibly THE only company that CAN execute this faithfully given our history.

## 4. The lay of the land: seven workstreams

**4.1 Benchmark & competition.** P2-Bench: a ~20-chamber suite tiered by Demaine gadget class,
each with a benchmark card, human par, and baseline floors (random-macro and scripted-greedy
columns on every chart, forever); then a rolling BALROG-style leaderboard, not an annual event.
The competition is *marketing for the platform* — the graveyard lesson (ViZDoom, MineRL, NetHack
Challenge) is that every dead competition left a living platform, so the platform is the
deliverable. Two launch tracks: frozen-VLM (token-capped, pause-while-thinking sanctioned) and
TAS/routing with the speedrun community as co-organizers. **6–12 months delivers:** public suite,
starter kit reproducing first light in an afternoon, NeurIPS competition proposal, first
cross-model leaderboard. **Profile:** the eval-scientist seat plus 0.5 community/ops.

**4.2 Harness platform.** The durable artifact: protocol versioning, a container that boots from
a fresh cloud VM (ships zero Valve assets; depot pulled under user credentials), crash-supervised
instance fleets, a measured determinism envelope ("player kinematics tick-exact; airborne props
diverge at tick N±M" — owned, not hidden), throughput work (render-skip, timescale), save/load
anchors making the engine a *branchable* simulator, and the P2:CE (64-bit, Valve-licensed engine)
succession spike. Universe died of unreliability; this is the anti-Universe. **6–12 months
delivers:** `pip install` + container + CI gate + published ticks/sec and $/episode tables + the
anchors API. **Profile:** an engine/harness RE (C++, game internals, systems).

**4.3 Locomotion & macros.** The wall itself. A trace-sampled walkability grid + A\* behind
`go_to` — encoding *walkable-now* space only (a closed door is a wall), so it formally cannot
solve puzzles, preserving the attribution boundary. Plus structured failure percepts (`BLOCKED`
names its blocker), per-class stand-points, auto-approach `pick_up`, and a locomotion-only
chamber suite (LocoGym) as a CI gate so actuation quality is measured, never vibes. The
research-grade follow-on: a *learned* `go_to` trained on human locomotion segments — the data
flywheel's first internal customer, and the proof-of-value for the whole data story. **6–12
months delivers:** SOLVED on first light's chamber, the full ladder unblocked, LocoScore
regression suite, learned-`go_to` prototype. **Profile:** shared engine-RE + RL-researcher work.

**4.4 Speedrun routing & search.** The anti-saturation axis. World-freeze + anchors + stable
marks = tree search over macro sequences nearly free: "LLM proposes, engine verifies" routing,
macro-MCTS, segment-level input optimization using upstream SAR's own TAS tools (this repo *is* a
fork of the speedrunners' tool). Routing against a live human leaderboard has no engine ceiling —
when models saturate stock chambers, this is where headroom lives. Every found route is also a
verified synthetic expert trajectory: the search tree is the data factory wearing a different
hat. **6–12 months delivers:** anchors validated (restore-fidelity tested, not assumed),
best-of-N router on small chambers, one search-found route faster than the scripted par.
**Profile:** an RL/search researcher.

**4.5 Data flywheel (SIMA / Robotics / Omni).** Sequence: `.hdem` v2 with per-tick action records
(the hook exists; days of work; **recordings made before v2 lack actions forever, so this is
first**), auto-annotation of engine events into instruction spans, RLDS/SIMA-span exporters so
the data drops into existing lab pipelines, a pilot human corpus (tens of hours, consented,
community-sourced — the speedrun community already runs SAR), then the premium products:
counterfactual branch data and the portal-transit world-model eval set. Claim discipline: this is
*embodied-agent pretraining and eval data*, not robot training data — and the transfer question
gets settled empirically (mix into an open VLA fine-tune, report the delta, whichever way it
lands). **6–12 months delivers:** v2 format, exporters, pilot corpus with dataset card, the
transfer experiment, wormhole eval v0. **Profile:** a data engineer.

**4.6 Eval science.** What makes first light a *finding*: the engine-hooked `chamber_complete`
oracle; terminal taxonomy v2 (LOOP/STUCK/DIED split out of BUDGET); the **teleport control arm**
(oracle actuation as a control — the delta between real and teleported legs *is* the locomotion
tax, measured per chamber per model, replacing the narrative with a two-column table);
contamination probes (stock chambers vs geometry-isomorphic remakes — that delta is the measured
memorization bonus, publishable either way); a pre-registered stats spec (≥5 seeds/cell, Wilson
CIs, chambers as the unit of analysis); cross-model runs (Claude, GPT-class — the agent seam is
one callable; days each); and the knowing-doing-gap scalar computed offline from trajectories.
The minimum citable kernel: **3 models × 20 chambers × 5 seeds × {real, teleport}, with baseline
floors.** **6–12 months delivers:** that kernel, plus the workshop paper. **Profile:** the
eval-scientist seat (shared with 4.1) — and me.

**4.7 Open-endedness & puzzle generation.** Chambers as *emitted*, not given: a `.p2c` generator
composing Demaine-style gadgets into solvable-by-construction instances (the generator knows the
exit, the wiring, and the witness solution — which doubles as scripted par and BC data), plus the
Workshop ingestion pipeline (download-on-demand, never redistribution) turning 950k raw items
into a graded few thousand with element metadata. Procgen's lesson: a task *distribution*, not a
task list, is what measures generalization and resists contamination. **6–12 months delivers:**
generator v0 (3 gadget types), first 1k verified workshop chambers, difficulty-grading model.
**Profile:** research engineer, shareable with 4.1; the headless `.p2c→.bsp` compile is the named
load-bearing unknown (one conversation with the BEEmod maintainers may collapse it).

## 5. Milestones

**Next 3 months — me alone (this happens regardless of funding; it is the pre-pitch hardening):**
exit oracle; pathfinding `go_to` + teleport arm; 20-chamber Demaine-tiered suite; the citable
kernel (3 models × 20 × 5 seeds × 2 arms, baseline floors, contamination delta); `.hdem` v2
before any more recording; the measured numbers table ($/episode — first light cost **$0.22
measured**; a full leaderboard row is ~$10–85 depending on context policy — instances/box,
ticks/sec); ten demand conversations with agent-eval, planning, world-model, and robotics-data
people, results on one slide; the Valve thread opened. **Kill criteria:** if the contamination
delta explains most of tier-1 performance, the headline reframes around mutation-robustness; if
frontier models saturate tier 3+ immediately, headroom shifts to the routing axis; if Valve
declines, scope to owned+licensed chambers.

**6 months — solo vs funded.** Solo: workshop paper submitted, starter kit + container public,
pilot corpus started. Funded (+2: engine RE, eval scientist): all of that plus the rolling
leaderboard live, 50–100 hour consented human corpus, RLDS transfer experiment reported,
generator v0.

**12 months — funded team:** NeurIPS competition proposal with p2sr as co-organizers; learned
`go_to` closing the first-light wall end-to-end; 1k verified workshop chambers with quarterly
held-out refresh; counterfactual branch dataset + wormhole eval in front of a world-model team;
P2:CE port report; D&B paper.

**The live demo (rehearsed, tiered):** Tier 1 — the archived first-light viewer: read the model's
mind, steps 0–7, pause on step 12. Tier 2 — a live chamber run; the VP types `go_to 7` in the
macro REPL themselves, then watches the model do the same with its reasoning visible. Tier 3 (the
closer, gated on pathfinding + oracle landing first): **the VP opens the Puzzle Maker, drags a
cube, a button, and a door; five minutes later the agent attempts their chamber live.** No other
benchmark on earth offers authorship-to-eval inside one meeting — and it makes the held-out
argument physical: *that chamber did not exist ten minutes ago.*

## 6. Risks, stated plainly

- **Contamination.** Every frontier model has read every Portal 2 walkthrough ever written; a
  3-element chamber has one schema to retrieve. *Mitigation:* I treat mechanics-knowledge vs
  instance-contamination as measurable, not arguable — isomorphic-remake deltas, mutation probes,
  and post-cutoff workshop windows, run *before* any external claim. The phrase "reasoning is
  solved" is retired.
- **"So what" / triviality.** Against the 2026 agent bar, 7 macro steps over ~10 marks is below
  the line, and a scripted greedy bot may match it. *Mitigation:* baseline floors on every chart;
  the pitch is the attribution instrument, not the difficulty record; the 7-step number never
  appears without its controls.
- **Valve IP.** No research carve-out exists. OpenAI Five was a negotiated deal, the CSGO dataset
  an unsued academic, P2:CE an engine license to modders, SAR a tolerated plugin — precedents
  that a conversation should go well, **not** clearance. *Mitigation:* thread opened before this
  meeting; zero Valve assets distributed (depot pulls under user credentials, workshop maps
  download-on-demand); public artifacts stay frame-free until resolved.
- **Single-game risk / benchmark fatigue.** The graveyard is real. *Mitigation:* three properties
  no dead platform stacked — in-game authoring, a maintenance community that predates the
  research (p2sr has maintained upstream SAR unpaid for a decade: the stewardship answer), formal
  difficulty grounding — plus a planned second-Source-title existence proof.
- **32-bit retail binary.** A depreciating, hostile substrate (the perf post-mortems in this repo
  read like a war journal). *Mitigation:* pinned-depot containers, CI replay gates, and a written
  engine-succession plan (P2:CE, 64-bit, Valve-licensed) — a succession plan no dead platform
  ever had before launch.
- **Bus factor = 1.** Real, and double-edged: the velocity exhibit can be read as "fund zero,
  wait." *Mitigation:* the formats are specced for independent readers; the succession ladder
  (me → p2sr co-maintainership → foundation-style adoption) is written; and the ask is built
  from the calendar-bound work agentic coding demonstrably did not compress.
- **The data story is the least-proven claim.** Volume is small, the buyer hypothesized.
  *Mitigation:* the transfer experiment is cheap and scheduled; until it reports, the data pitch
  leads with the two genuinely scarce products (counterfactual branches, portal-transit ground
  truth), not with hours.

## 7. The ask

**4.5 FTE alongside me — five named seats, each de-risked by a working artifact:**

1. **Engine/harness RE** (C++, game internals, systems) — owns workstreams 4.2 + the engine half
   of 4.3. De-risked by the working harness. Unblocks: container/fleet/CI, anchors, throughput,
   P2:CE — everything external users touch.
2. **Eval scientist** — owns 4.1 + 4.6 with me. De-risked by first light and the trajectory
   tooling. Unblocks: the citable kernel, the suite, the leaderboard, the paper.
3. **RL/search researcher** — owns 4.4 + learned-`go_to` in 4.3. De-risked by the existing PPO
   stack, the inference-server seam, and the TAS tooling. Unblocks: the routing axis, search-
   generated data, the headroom story.
4. **Data engineer** — owns 4.5 + format stewardship. De-risked by the working recorder, formats,
   and render farm. Unblocks: the corpus, the exporters, the transfer result, the Omni/Robotics
   conversation.
5. **Community & competition ops (0.5)** — owns Workshop ingestion ops, the leaderboard, the
   p2sr partnership, and the Valve relationship. De-risked by a decade of community
   infrastructure this repo is literally forked from. Unblocks: the competition, the corpus
   pipeline, year-3 stewardship.

Plus modest non-headcount lines: ~$10–20k competition prizes (the proven norm), one GPU box for
the fleet, API budget for the eval matrix (a full 10-model leaderboard refresh costs under $500
at the efficient context policy — eval COGS is not the ask; credibility infrastructure is).

---

One closing calibration. The working title on my whiteboard is "AGI via Portal 2." I don't mean
it literally, and I'll only say it once — with a smile. What I mean is: embodied reasoning,
long-horizon planning, spatial understanding through non-Euclidean space, grounded language,
search, and human-data flywheels, all measurable in one instrument, with ground truth, for the
price of a $10 game. A frozen model already solved its first chamber and was failed only by its
legs. Every objection in section 6 is a measurement this instrument exists to make. Fund the
seats, and we make them.
