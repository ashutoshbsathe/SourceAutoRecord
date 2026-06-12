# Lens: data-flywheel — trajectories as the product

*Diverge-mode council output, 2026-06-12. Premise: the benchmark is marketing; the dataset is
the product. Labs are provably paying for embodied trajectories (VPT contractors, SIMA studio
deals, Gemini Robotics teleop fleets at dollars-per-minute). Portal 2 trajectories — first-person,
physical, long-horizon, non-Euclidean, language-annotatable, with PERFECT ground-truth state —
cost near-zero marginal here. Every idea below is about turning the existing recorder/format
stack into a flywheel: more play → more data → better agents → more solved chambers → more data.*

Key existing assets this lens builds on:
- `.hdem` sidecar recorder (entity ground truth, delta-coded, versioned, ~MB/playthrough) — but **no actions** (v1 gap).
- The `CUserCmd` hook already exists (`src/Modules/Client.cpp:586` → `Harness::RecordDemoAction`).
- `EntitySnapshotter` = universal tap: anything added flows into live gRPC, `.hdem`, `.rollout` at once.
- `render_demos.py` = a working headless farm shape (queue + N gamescope instances).
- `.trajectory` stores every prompt/thought/action/result of every LLM run, losslessly.
- Upstream SAR is **mandatory on the Portal 2 speedrun.com leaderboards** — the recorder's host plugin is already installed on every active runner's machine.
- ~954k Workshop items (verified mid-2024 count), `.p2c` is parseable plain text.

---

## The ideas (ordered roughly by leverage)

### 1. `.hdem` v2: per-tick action records
**Build:** Add a per-tick input record to the versioned `.hdem` format: buttons bitfield, viewangles, forward/side move, weapon/portal fire — sourced from the existing `CUserCmd` hook that `RenderDemo` already uses. Bump `HDEM` version, extend `HdemFormat.hpp` + `HdemRecorder.cpp` + `py/hdem_reader.py`. While in there: write the footer CRC (currently 0) and add the planned-but-missing record-interval cvar.
**Why:** This is the single change that turns `.hdem` from "entity telemetry" into a **complete, engine-free BC dataset**: pure-Python demo → (obs, action) pairs at I/O speed, no game in the loop. It is the seed corpus for an inverse dynamics model (VPT's ~2k contractor hours were exactly this artifact). Every downstream idea (#4, #5, #6, #7, #9) consumes it.
**Effort:** S (days). The hook, the versioning, and the reader all exist; this is plumbing.
**Deps:** none. **Unlocks:** literally everything below.

### 2. Auto-annotation engine: entity-events → language spans
**Build:** A pure-Python event detector over the snapshot stream (works on `.hdem`, `.rollout`, live): emits causally-correct, timestamped event subtitles — "player picked up cube (mark 11)", "cube placed on button 7; button pressed", "door 8 opened", "player traversed portal blue→orange", "fizzler destroyed cube". Then a chunker that groups events into SIMA-style **instruction spans** ("carry the cube to the button") with start/end ticks. Optionally a Gemini pass that paraphrases event sequences into natural instructions (Gemini-synthesized annotation, the SIMA 2 recipe).
**Why:** Language annotation was THE expensive human bottleneck in SIMA 1 (the two-person Setter-Solver protocol existed only to make instructions causally precede actions). Here causality is free: we have the ground-truth event ordering from the engine. This converts every raw playthrough into instruction-conditioned training data at zero human cost — the exact thing DeepMind paid studios and annotators for.
**Effort:** S–M. The status fields (button state, cube activated, portal linkage) are already in the snapshot; door-open is the one known percept gap.
**Deps:** none (better with #1 for action-aligned spans). **Unlocks:** #8, #9, P2-Traj (#10), the SIMA-shaped pitch slide.

### 3. RLDS / SIMA-span exporter — speak the buyer's format
**Build:** One converter, `py/export/`, from `.hdem`+`.dem` / `.rollout` / `.trajectory` to (a) **RLDS** tfrecord episodes (the Open X-Embodiment interchange format: per-step image, instruction, action vector, episode metadata incl. success) and (b) the **SIMA span tuple** (frames, low-level inputs, per-span language instruction, rubric/verified score). Include a tiny `tfds` builder so `tfds.load('portal2_traj')` works.
**Why:** Gemini Robotics ingests RLDS; SIMA 2 ingests spans. A dataset nobody can load is a demo; a dataset that drops into their existing pipelines is a product. This is days of work that converts the entire pitch from "we have a cool format" to "here is a drive link your team can train on Monday."
**Effort:** S. **Deps:** #1 for action channels, #2 for instructions. **Unlocks:** the actual DeepMind conversation.

### 4. Macro-segmentation of human play (inverse-macro labeling)
**Build:** A segmenter that re-expresses a human low-level trajectory in the agent's own verb vocabulary: detect "this 180-tick input span = `go_to mark=7`", "this `+use` pulse near cube = `pick_up 11`", "viewangle sweep = `aim_at`", using entity ground truth + proximity + the MarkTable identities. Output: `.trajectory`-format files where the "agent" is a human — same Observation+Call schema, `header.model="human"`.
**Why:** Humans and the frozen VLM become directly comparable in one format and one action space. This yields (a) macro-altitude BC data, (b) few-shot exemplars to paste into the eval prompt (human solutions as in-context demos), (c) the BALROG knowing-doing-gap analysis with a human reference, and (d) the honest answer to "is the macro grammar expressive enough?" — if human play can't be losslessly macro-segmented, the grammar is missing verbs (a measurable grammar-coverage metric).
**Effort:** M. Segmentation heuristics will need iteration; ground truth makes it tractable.
**Deps:** #1. **Unlocks:** #5, #11, human-normalized benchmark scores.

### 5. The learned `go_to`: solve the locomotion wall with human data, not nav-mesh code
**Build:** Mine human demos for **locomotion micro-segments**: every span where the player moved from A to B is a training example (start pose, goal point, local geometry context / depth or trace probes, input sequence). Train a small policy (the dormant `py/rl/` stack is literally built for this: frozen ViT embeddings, inference server, swappable head) that *implements* the `go_to` macro — same MacroResult contract, BLOCKED/STUCK codes preserved. The macro boundary doesn't move; the actuator behind it gets legs.
**Why:** First light's finding is "reasoning solved, locomotion is the wall," and the roadmap's stated #1 lever is a nav-mesh pathfinder. This is the flywheel's counter-proposal: locomotion is the first *internal customer* of the dataset. If human Portal 2 data can train a robust point-goal navigator, that is the proof-of-value slide for the entire "trajectories fuel Gemini Robotics" pitch — demonstrated in our own stack before asking anyone to believe it. It also dodges the grammar doc's worry that engine pathfinding "smuggles chamber-solving into the actuators": a policy trained only on locomotion segments can't solve puzzles.
**Effort:** M–L. Data mining M; training/integration M; honest risk that a hybrid (learned policy + trace guards) is needed.
**Deps:** #1, #4 (segment mining), some recorded hours (#6/#10). **Unlocks:** SOLVED on testchamber_000, the whole difficulty ladder, and the pitch's credibility.

### 6. Distribution coup: opt-in `.hdem` recording in community SAR
**Build:** Upstream (or release as a blessed fork build) the `.hdem` recorder behind `sar_harness_record`, with an explicit opt-in consent flow (one-time prompt, dataset-terms URL, anonymized player ID) and a one-click "share my runs" uploader (or just "zip the sidecar next to the demo you already submit to speedrun.com"). Coordinate with p2sr maintainers; sidecars are zero-risk to vanilla playback by design.
**Why:** Portal 2 speedrun.com submissions **require demo files**, and SAR is mandatory tooling — the recording host is already deployed on every active runner's machine. This is distribution OpenAI paid contractors for and SIMA negotiated studio deals for, available via one PR and one Discord conversation. Speedrunners generate exactly the high-skill, long-horizon, glitch-rich play that makes the routing/execution data interesting.
**Effort:** M of engineering+packaging; calendar time dominated by community/political work. The recorder itself works today.
**Deps:** #1 (record actions from day one — re-recording is impossible), #20 (consent/license text). **Unlocks:** a continuous, growing, zero-marginal-cost human corpus; the "who records?" answer.

### 7. Demo-archive backfill farm (Approach E at scale)
**Build:** Ingest the *existing* community `.dem` corpus — speedrun.com submissions, p2sr archives, autorender queues — through a hardened `render_demos.py` farm: play each demo headless, record `.hdem`-overlay entity state (client-fidelity; build the designed-but-unbuilt metadata-overlay fallback), reconstruct actions via the existing `CUserCmd` hook + viewangle-diff mouse heuristic, emit `.rollout`/RLDS. Add a manifest DB (which demo, which map, success, divergence flags).
**Why:** Years of expert human play already exist on disk in the community; nobody has to record anything. Client-fidelity entity state is weaker than server truth but fine for BC/IDM purposes. This is the fastest path to "N hundred hours" on a pitch slide, and it exercises the farm that any future competition/eval cluster needs anyway.
**Effort:** M. The farm, hook, and playback all exist; the overlay fallback and divergence handling (Source demos can desync) are the work.
**Deps:** #1's format, archive access + author permission posture (#20). **Unlocks:** scale numbers, IDM training corpus (#8).

### 8. Engine-verified VPT: IDM + YouTube harvest with a replay verifier
**Build:** Train an inverse dynamics model on the input-synchronized corpus (#1/#6/#7): frames → inputs. Then harvest Portal 2 video at scale via yt-dlp (15 years of walkthroughs, speedrun VODs, Workshop playthroughs), pseudo-label with the IDM — and add the twist VPT never had: **replay the predicted inputs in the actual engine** on the identified map and score trajectory agreement (position-track distance, event-sequence match), accepting only clips that re-simulate. Physics desync makes this a soft verifier (tolerance bands, event-level matching), not exact — be honest about that.
**Why:** VPT turned 2k labeled hours into 70k usable hours; the same multiplier applied to YouTube Portal 2 is the only route to six-figure hours. The engine-in-the-loop verifier is a genuinely novel methodological contribution (pseudo-label *certification*) and a paper on its own. Map identification for stock + workshop chambers is feasible (visual fingerprint vs. the workshop index from #14).
**Effort:** L (months): IDM M, harvest+dedup pipeline M, verifier M. The honest "needs headcount" item — and therefore a headcount ask in the pitch.
**Deps:** #1, hours from #6/#7/#10, #14 for map ID. **Unlocks:** web-scale corpus; the strongest single dataset-size claim.

### 9. Search-generated expert trajectories: compute → data
**Build:** Implement save/load anchors (C9, ~40 LOC by the grammar doc's estimate), then a best-of-N / beam / MCTS driver over macro sequences (frozen world + pausable engine makes the ~100ms save cost irrelevant). Point it at workshop chambers with the exit oracle; every found solution is a **verified synthetic expert trajectory** in `.trajectory`/RLDS format. LLM-proposed actions can serve as the search prior (the first-light agent already emits sensible candidates).
**Why:** This is the SIMA 2 self-improvement loop with a better reward model: the chamber exit is ground truth, not a hallucinatable rubric. It converts GPU/CPU time into expert demonstrations on maps no human has played — contamination-free by construction — and it IS the speedrun-routing research axis wearing a data hat (the search tree over macro moves is the route finder).
**Effort:** M–L. Anchors S; search driver M; making it solve nontrivial chambers is research.
**Deps:** exit oracle (other lens), anchors, save/load-invariant marks (already designed). **Unlocks:** #11, unlimited macro-altitude SFT data, the routing-agent story.

### 10. P2-Traj v0: the dataset release as the credibility artifact
**Build:** Record/assemble a deliberately modest v0: ~10–20 hours of human play (self + a few community volunteers) across stock campaign + curated workshop chambers, with synchronized inputs (#1), ground-truth entity state, auto-annotations (#2), RLDS export (#3), dataset card, dataset license. Ship on HuggingFace; write the 6-page NeurIPS D&B-style paper. Precedent: the CSGO BC dataset (Pearce & Zhu, 4M frames scraped from public servers) shipped openly with no Valve trouble.
**Why:** A released dataset with a DOI is what makes everything else citable and fundable. It's also forcing-function hygiene: you discover every format bug, licensing question, and tooling gap the first time an outsider downloads it. Per-hour richness (perfect state + inputs + language + portals) is the differentiator, not raw hours — lead with that.
**Effort:** M, given #1–#3. Recording hours are real calendar time; volunteers help.
**Deps:** #1, #2, #3, #20. **Unlocks:** citations, community recorders (#6 marketing), the pitch attachment.

### 11. Counterfactual branch data: embodied preference pairs & process supervision
**Build:** Using anchors (#9), at each decision state roll *multiple* macro continuations (the model's chosen action, its rejected alternates, perturbations); label branches by outcome (progress, BLOCKED, solved). Emit (state, chosen, rejected) preference pairs and per-step value/progress labels.
**Why:** Everyone has expert trajectories; almost nobody has *counterfactuals from identical states* in a physical 3D world, because real-world and most game setups can't reset to a frozen state. The pausable engine + anchors makes this nearly free, and it is exactly the DPO / process-reward-model data shape frontier labs are buying for agent training. Arguably the most differentiated data product in this whole document.
**Effort:** M on top of #9. **Deps:** #9. **Unlocks:** a second, premium data product; PRM-style training for embodied reasoning.

### 12. Portal-transit world-model eval set ("the wormhole benchmark")
**Build:** Curate a few thousand short clips (from #7/#10) centered on portal traversals: frames + per-tick ground-truth pose/velocity/camera through the transit, plus paired "same scene, no portal" controls. Package as a next-frame / next-state prediction benchmark with exact-state scoring. One generator script + one eval script.
**Why:** Genie-class world models are trained on game video and are *publicly* weakest at consistency through non-Euclidean transitions — nothing in their data distribution looks like a portal. A small, sharp eval that frontier world models demonstrably fail, with ground truth no other source can provide, is the single most memorable artifact you can put in front of a DeepMind world-model team. Tiny effort, outsized pitch value.
**Effort:** S–M. **Deps:** some corpus (#10 suffices). **Unlocks:** a hook into the Genie org specifically; a workshop paper.

### 13. `.hdem` → percept replay: humans in the agent's observation space
**Build:** Re-render human `.hdem`s as *macro-level percept streams*: what marks existed, statuses, what the human did to them (with #4's segmentation) — pure Python, no game. Optionally engine-replay to also regenerate annotated frames for vision-aligned pairs.
**Why:** Produces human demonstrations in EXACTLY the frozen-VLM's input/output space — directly usable as in-context exemplars, SFT data for a macro-policy, and the apples-to-apples human baseline for every eval chamber. Bridges the human corpus to the LLM track without any new recording.
**Effort:** S–M (the percept projection already exists server-side; this is porting it over the hdem reader).
**Deps:** #1, #4. **Unlocks:** few-shot prompting experiments (does one human demo fix the locomotion flailing?), macro-BC.

### 14. Workshop metadata scrape: the curriculum is already crowdsourced
**Build:** ISteamUGC/steamcmd pipeline over the ~954k Portal 2 workshop items: titles, descriptions, ratings, subscriber/completion counts, author, timestamps; download `.p2c` where available and parse the element inventory (voxel grid + items — it's plain text). Output: a queryable index with difficulty/quality priors and element-composition labels.
**Why:** Fifteen years of human votes constitute a free curriculum/difficulty signal (Procgen's lesson: you need a level *distribution*, curated hard). The element-inventory parse lets you select chambers matching the ontology ladder (cube+button only → +lasers → +gels) automatically. Also de-risks the pitch's "~1M chambers" claim with real, current numbers.
**Effort:** S–M (rate limits, UGC API quirks). **Deps:** none. **Unlocks:** chamber-suite construction, #8 map ID, #9 target selection, honest pitch numbers.

### 15. Playtest analytics for PeTI authors: data in exchange for a product
**Build:** A "test my chamber" service for mapmakers: players run the map with the recorder on; authors get heatmaps, death/stuck locations, completion funnels, time-to-solve, derived from `.hdem` (the snapshot stream contains everything needed). One web page per map; recording consent built in.
**Why:** Answers "who records at scale?" with an *incentive*, not charity: PeTI authors desperately want playtest feedback (the Workshop has no analytics). Every playtest session is a labeled trajectory on a novel chamber — exactly the held-out-map data the benchmark and the dataset both need. This is the flywheel's self-sustaining loop: tool → data → better tool.
**Effort:** M–L (the analytics M; the service/web plumbing is the L tail — scope v0 to "send me your hdem, get an HTML report" which is M).
**Deps:** #1, #2. **Unlocks:** continuous novel-chamber human data; community goodwill; competition seeding.

### 16. Human baseline corpus on the eval suite
**Build:** Humans (you + a few others) play every eval chamber twice: once through `macro_repl.py` (macro-altitude, same budget/percept as the model) and once free-play with the recorder. Store both in `.trajectory`/`.hdem`. Report human scores alongside model scores.
**Why:** SIMA frames everything against "humans ~70%"; BALROG against human play; a benchmark without a human line isn't legible to a VP. The macro-REPL run also measures the *grammar's* ceiling (can a human solve it through these verbs at all?) — separating grammar inadequacy from model inadequacy, which protects the headline claim.
**Effort:** S (tooling exists; this is hours of play + a results table). **Deps:** eval suite chambers. **Unlocks:** human-normalized reporting; grammar-coverage validation; exemplars for #13.

### 17. Trajectory viewer → verdict labeling tool
**Build:** The already-designed one-POST verdict-writeback seam: per-step labels (`ok / perception / reasoning / actuation / grammar`) clickable in the HTML viewer, persisted as a sidecar JSON next to the `.trajectory`.
**Why:** The headline result ("reasoning solved, locomotion the wall") is currently one anecdote; labeled step-level verdicts over many runs make it a *measured distribution* — and the labels themselves are a dataset (failure-mode classification training data, and the ground truth for any future auto-triage model).
**Effort:** S. **Deps:** none. **Unlocks:** quantified headline claims; failure taxonomy (#18).

### 18. Offline analysis library over `.trajectory` archives
**Build:** `py/llm_eval/analysis.py`: BALROG-style knowing-doing gap (model states correct intent in reasoning text but acts wrong), loop detection post-hoc, action-distribution stats, token-cost curves, rejection-reason taxonomy, cross-run aggregation into one report. Runs purely on archived trajectories — no game.
**Why:** `.trajectory` is lossless precisely so analysis never needs reruns; right now that value is unrealized. Every eval run already paid the API cost — this extracts the science from sunk cost. The knowing-doing gap number is the single most quotable stat for the reasoning-vs-actuation thesis.
**Effort:** S. **Deps:** a handful of runs. **Unlocks:** paper figures from existing data; regression tracking as the stack evolves.

### 19. Agent-trajectory distillation: frozen-VLM runs → small macro-policy
**Build:** Convert successful `.trajectory` runs into SFT pairs ((annotated frame, percept text) → command string) and rejected/accepted Call pairs into DPO data; fine-tune a small open VLM (e.g. a Gemma-class model) as a cheap macro-policy; evaluate it on the same suite.
**Why:** Closes the SIMA 2-shaped loop end-to-end at miniature scale: frozen frontier model generates, verified environment filters, small model distills, benchmark measures the gap. Even a modest result ("distilled 4B model solves tier-1 chambers at 1/100th the cost") demonstrates the full train-side value of the harness — the difference between pitching an *eval* and pitching a *training environment*.
**Effort:** M (data conversion S; fine-tune + eval M). **Deps:** a corpus of successful runs (post-locomotion-fix, or via #9). **Unlocks:** the "this environment trains agents, not just grades them" slide.

### 20. Dataset governance: consent, license, and the Valve conversation
**Build:** (a) An opt-in consent flow + anonymization in every recording path; (b) a dataset license: inputs + entity state + annotations under a permissive research license (clearly ours), frames flagged as game-derived imagery (the gray zone); (c) a one-page Valve outreach citing the precedents that worked — OpenAI Five (cooperation), CSGO BC dataset (tolerance), P2:CE (actual engine license); (d) workshop-map handling = download-on-demand via the Workshop API, never redistribution.
**Why:** The first question any DeepMind lawyer asks. The CSGO precedent suggests tolerance, but a pitch that says "we asked, here's the posture" beats "we assume it's fine." Cheap to prepare, catastrophic to skip, and the consent flow must exist *before* community recording (#6) starts — consent can't be retrofitted onto collected data.
**Effort:** S of writing; calendar-L for any Valve response (don't block on it; document the attempt).
**Deps:** none. **Unlocks:** #6, #7, #10 shipping without a retraction risk.

### 21. Format hardening + published spec ("dataset as product" hygiene)
**Build:** Add the missing `format_version` to `trajectory.proto` (the design doc's loud-failure guard that never shipped); implement the `.hdem` footer CRC; write one `docs/formats.md` spec covering `.hdem`/`.rollout`/`.trajectory` framing so third parties can write independent readers; add round-trip tests.
**Why:** The moment data is the product, silent mis-parses of stale files are data corruption. A published spec is also what makes "platform outlives the contest" real — ViZDoom/NLE survived because outsiders could build on the formats. Boring, small, and a prerequisite for every release above.
**Effort:** S. **Deps:** none. **Unlocks:** safe format evolution (v2 actions, future fields) under external consumers.

### 22. Lazy-pixel architecture: store state, render frames on demand
**Build:** Make the canonical stored artifact `.dem` + `.hdem` (MBs), never pixel `.rollout`s (GBs); a farm job (render_demos.py shape) regenerates frames at any requested resolution on demand, exploiting `.hdem`'s rendering-independence (record on a 4K Windows rig, render 224px on headless Linux). Add the never-built fast-conversion tier (`mat_norendering` for state-only passes) where pixels aren't needed.
**Why:** Storage economics decide whether a 100k-hour corpus is feasible for one lab, let alone one RE: raw RGB rollouts ran 3.3GB per 2846 ticks. State-canonical storage with reproducible re-rendering is also a scientific feature (re-render yesterday's corpus at tomorrow's resolution/encoder) no video-scraped dataset can match. Caveat honestly: Source demo replay is only semi-deterministic (airborne-physics divergence), so frames are "faithful re-simulation," not bit-exact — fine for training data, must be stated.
**Effort:** M. **Deps:** #1; farm exists. **Unlocks:** corpus scale at single-RE storage budgets; resolution-future-proofing.

---

## Spiciest take

**Don't write the nav-mesh pathfinder. The locomotion wall — the project's #1 stated lever — should
be solved by the data flywheel, not by C++ geometry code.** Mine human locomotion micro-segments
from `.hdem` v2 demos and train a small policy that *implements* `go_to` behind the same MacroResult
contract. If the flywheel can't even produce a walking policy from human data, the entire
"Portal 2 trajectories will fuel Gemini Robotics" pitch is hollow — and if it can, your fix for
first light's failure IS the proof-of-value demo, generated in-house, before you ask a VP to
believe anything. Eat the dogfood before serving it.

(Runner-up spice: SAR being mandatory on the speedrun.com leaderboard means the recording host is
already deployed on every active runner's machine — distribution that OpenAI paid 2,000 contractor
hours for. One PR + one Discord conversation away.)

## If I could only do ONE thing next week

Ship `.hdem` v2 with per-tick `CUserCmd` action records (#1, days, the hook already exists), then
record ~2 hours of my own play across 20 chambers and run it through a minimal RLDS exporter (#3).
End the week holding the first input-synchronized, ground-truth-state Portal 2 dataset file — the
artifact every other idea in this lens, and the entire training-data pitch, hangs off.
