# Lens: benchmark-competition — "The Portal 2 Challenge"

*Diverge-mode brainstorm, 2026-06-12. One of ten council lenses. This lens owns: competition
design, tracks, chamber curation at workshop scale, difficulty grading, held-out/anti-cheat,
eval infra, NeurIPS proposal shape, starter kits, leaderboards, prizes, community bootstrap,
and — above all — durability beyond year one.*

**Framing axiom (from the graveyard study): the platform is the product; the competition is
marketing for it.** ViZDoom, NLE, Procgen, Melting Pot — every dead competition left a living
platform; every platform without a maintenance story died when the sponsoring lab pivoted.
Every idea below is scored against "does this make the platform more durable" as much as
"does this make a splashier contest."

Effort key: **S** = days, **M** = weeks, **L** = months, **XL** = needs a team — all for ONE
research engineer + agentic coding.

---

## Ideas, ordered by leverage

### 1. Ground-truth exit oracle: `chamber_complete` in GameState
**What:** Hook the PeTI level-end path (exit-door logic / `@relay_pti_level_end`-style relay /
`changelevel`) using SAR's standard hooking patterns (`docs/contributing.md`), emit a
`chamber_complete` bool in `GameState`, and make `run_eval.py` terminate on it instead of the
hand-passed `--exit x,y,z --radius R`. Recon first with `sar_harness_dump_fields diff` on
`testchamber_000` to find what actually fires. Fallback tier: exit-door-entity heuristic from
the snapshotter (marks for `prop_testchamber_door` already exist).
**Why:** This is the single prerequisite for *everything else in this lens*. No suite, no
leaderboard, no auto-grading of workshop chambers, no anti-cheat is possible while success is
a hand-set sphere per chamber. It also fixes the latent bug where the model is told 2D distance
but judged on a 3D sphere, and unsplits `SOLVED` from `BUDGET` honestly.
**Effort:** S (the hook itself is routine by SAR standards; recon + proto field + smoke test).
**Deps:** none. Do it first.
**Unlocks:** ideas 2, 4, 6, 7, 8, 9, 13 — literally the rest of this document.

### 2. P2-Bench-v0: a 20-chamber tiered public suite + the first leaderboard row
**What:** Hand-author (PeTI) ~20 chambers in 4–5 difficulty tiers explicitly keyed to the
Demaine et al. complexity ladder: T1 cube+button+door (the PSPACE-complete core, first-light
tier), T2 +portals on white surfaces, T3 +lasers/relays, T4 +timed buttons/turrets, T5
composite. Ship as `.p2c` sources + compiled `.bsp` + a manifest (`chambers.json`: map name,
tier, element census, par steps). Run the *existing* gemini-3.5-flash agent across all 20 and
publish per-tier success/steps/tokens as leaderboard row #1.
**Why:** Converts first light (n=1 anecdote) into a benchmark (n=20 curve). The "success
decays with tier X" plot is the headline figure of the M4/M5 pitch, the spine of the NeurIPS
proposal, and the thing BALROG-style frozen-model evals can adopt. Crucially the suite is
*authored by us*, so redistribution is unencumbered (no Workshop-UGC licensing question).
**Effort:** M (chamber authoring is days with PeTI; the manifest/runner plumbing is days;
locomotion will eat some chambers — that's data, not a blocker, per the macro lens).
**Deps:** idea 1. Benefits from loop-detection and context-window fixes (other lenses) but
does not need them.
**Unlocks:** multi-model eval (M4), the NeurIPS proposal (idea 19), the starter kit (idea 3).

### 3. First Light Starter Kit: reproduce the baseline in an afternoon
**What:** One repo-or-release artifact: prebuilt `sar.so`, `uv`-pinned Python env, the T1
chambers, the Gemini agent + a stub `Agent` base class (the "agent is one callable" seam in
`run_eval.py`), a `make first-light` target that launches the game, runs the eval, and opens
the trajectory HTML. Include a scripted agent that solves T1 so the kit self-tests without an
API key. Document the Portal-2-license + gamescope prerequisites honestly.
**Why:** The BASALT/VPT lesson: providing the pretrained base + scaffold is what broadened
participation; warm-up friction is what killed ViZDoom rounds. Day-1 participants must
reproduce first light in an afternoon or they churn. This is also the artifact a DeepMind VP's
team can run *themselves* — the strongest possible pitch appendix.
**Effort:** M (mostly packaging, docs, and the painful part: making the gamescope/steam-runtime
launch work on a machine that isn't yours; budget real time for that).
**Deps:** idea 2 (needs chambers worth running). Sequencing: before any public announcement.
**Unlocks:** community bootstrap, cross-model baselines contributed by outsiders, idea 6.

### 4. `p2bench`: pip-install local eval with one scalar (the Crafter play)
**What:** `pip install p2bench; p2bench eval my_agent.py` → runs the public suite locally
against the player's own Portal 2 install, prints one number (geometric-mean tier-weighted
success, Crafter-style) + a per-tier table, writes `.trajectory` files. Auto-detects the Steam
install, manages instance launch via the existing `game_launcher.py`.
**Why:** Crafter out-impacted whole competitions at near-zero org cost purely via zero-friction
install + one scalar. A benchmark individuals adopt in papers is the durable flywheel; the
competition then formalizes a leaderboard that already has organic users. Also de-risks the
"eval cost is the killer" lesson: local-first means the organizer pays nothing for the long tail.
**Effort:** M (the eval loop exists; the work is packaging, install detection, and Windows —
decide explicitly to ship Linux-only v0 and say so).
**Deps:** ideas 1–3.
**Unlocks:** citations, organic leaderboard submissions (idea 6 accepts `p2bench`-produced
trajectory artifacts), the "platform first" durability story.

### 5. Agent protocol spec: agents are callables, eval owns the game
**What:** Freeze a tiny submission interface: an agent is a process/endpoint implementing
`next_action(observation) -> command-string` (exactly the seam `run_eval.py` already has),
where observation = annotated PNG + percept JSON. Two transports: local subprocess (stdin/stdout
JSON lines) for the local kit, HTTPS endpoint for hosted eval. Version it. Publish conformance
tests.
**Why:** This is the anti-MineRL move: MineRL's organizers *re-trained submissions* for
reproducibility — sponsor-compute-dependent and staff-hungry. An inference-only protocol means
hosted eval cost is bounded per-episode, agents can be closed-source (industry labs can enter
without code disclosure), and the human REPL / scripted / RL-policy agents are conformant for
free. The RL track needs a different compliance story (idea 14) — that's a feature of splitting
tracks, not a bug of the protocol.
**Effort:** S (the seam exists; this is specification + a conformance script).
**Deps:** none hard; co-design with idea 3.
**Unlocks:** ideas 6, 13, 21; closed-lab participation.

### 6. Hosted eval server + rolling leaderboard (BALROG shape, not annual-event shape)
**What:** A persistent eval service: submission (API endpoint or container) + held-out chamber
set + N headless game instances drained by a queue — which is structurally `render_demos.py`
with `RenderDemo` swapped for the eval RPC, already noted as a cheap swap. Rolling leaderboard
website (static, regenerated per eval; the trajectory-HTML machinery is reusable for per-run
drill-down). Rate-limit submissions (Lux AI: 5/day) instead of gating on events.
**Why:** Always-on ladders (Lux AI, Kaggle) are the longest-lived competition format and the
cheapest to operate; annual events concentrate org cost and die after edition 3 (ViZDoom).
A rolling leaderboard also gives the pitch a *live* URL, which lands harder than a paper plot.
**Effort:** L (the farm exists in embryo; hardening multi-tenant eval, sandboxing, the website,
and ops is real months for one RE — this is the first idea where the honest answer starts to be
"this is where headcount goes").
**Deps:** ideas 1, 2, 5; idea 13 for integrity.
**Unlocks:** the competition itself; continuous multi-model tracking (frontier-model releases
get evaluated the week they ship — free press every time).

### 7. Self-refreshing held-out sets: the fresh-workshop-stream protocol
**What:** Define the private test set as *a rolling window of newly published Workshop chambers*
(e.g., published in the last 60 days, auto-filtered by idea 8's pipeline, validated by idea 16's
human play), refreshed quarterly. Scores are reported per-window. Old windows graduate to the
public dev set.
**Why:** This is the anti-contamination and anti-VPT-moment design in one move, and it is the
genuinely novel asset vs. every predecessor: Procgen needed private *code* envs, we get a free
firehose of human-authored, never-before-seen tasks because the level editor ships inside the
game and the Workshop adds items continuously (954k as of mid-2024). A frontier lab cannot have
trained on chambers published after its cutoff. It also makes the benchmark *unsaturatable in
principle* — the metric is "solve rate on chambers that didn't exist last quarter."
**Effort:** M for the protocol + first refresh, *given* idea 8; ongoing S per quarter (the
recurring cost must be stated honestly in the proposal — this is a curation commitment).
**Deps:** ideas 1, 8; idea 16 strengthens it (human-solvability validation).
**Unlocks:** contamination-resistant headline metric; the strongest single slide in the VP pitch.

### 8. Workshop ingestion + curation pipeline (950k → a graded few thousand)
**What:** A batch pipeline: enumerate via Workshop API → `steamcmd workshop_download_item`
on demand → parse `.p2c` where available (documented KeyValues format) for an element census →
compile/load-check headlessly via the instance farm → dedup (voxel-grid hash) → filter
(SP-only, stock-PeTI-elements-only for v0, loads without errors, exit exists) → score difficulty
features (idea 18) → emit a tiered candidate catalog. Never redistribute map files; ship IDs +
download-on-demand (this is the UGC-licensing-safe path).
**Why:** "800k chambers" pitched raw is the Universe mistake (breadth without reliability);
curated-and-graded thousands is the Procgen task-*distribution* asset. This pipeline is also
the substrate for ideas 7 and 16, and its by-product — a structured census of what humans build
with PeTI — is a publishable dataset on its own.
**Effort:** L (each stage is simple; the long tail of Workshop garbage, .p2c absence on
Hammer-made maps, and farm-throughput engineering make it months solo).
**Deps:** idea 1 (load-check needs the oracle), the instance farm. Solvability validation needs
idea 16 or a strong heuristic — be honest that "loads + exit exists" ≠ "solvable."
**Unlocks:** ideas 7, 18; train/test splits at scale for the RL/offline tracks; the "task
distribution, not task list" claim.

### 9. Procedural chamber generator from `.p2c` gadget grammars
**What:** A Python generator that composes Demaine-style gadgets (cube/button/door interlocks,
laser-relay chains, timed-button bridges) into valid `.p2c` voxel grids — building on the
documented format and prior art (Kyle0654/Portal2.Puzzle) — with difficulty knobs: gadget count,
dependency-graph depth, decoy elements, chamber volume. Compile via the in-game compiler on the
farm. Generated chambers are solvable *by construction* (the generator emits the witness
solution as a macro script — which also gives every chamber a free scripted-agent par).
**Why:** Procgen's lesson is that procedural task distributions are what measure generalization
(500–1000 levels to generalize) and resist contamination. Solvable-by-construction kills the
hardest curation problem (idea 8's solvability gap). The emitted witness solution doubles as
BC data and as the routing track's baseline. And per Demaine, `.p2c`'s pseudopolynomial size
bound means generated instances are *formally* bounded — a cute line for the paper.
**Effort:** L (the format is documented but the in-game compiler round-trip, gadget library
design, and placement constraints are genuinely fiddly; the witness-solution guarantee is the
hard part — start with 3 gadget types, not 30).
**Deps:** idea 1; farm for compile-checking. Independent of idea 8 (deliberately: two
uncorrelated sources of test chambers).
**Unlocks:** infinite train sets for the RL track, difficulty-controlled science (idea 18
calibration), unsaturatable eval supply that doesn't depend on Workshop goodwill.

### 10. Mutation probes: same puzzle, different skin
**What:** Given any `.p2c`, emit semantics-preserving mutants: mirrored layout, translated/
rotated element placement, PeTI style/theme swap, decoy-element insertion, re-ordered marks
(mark IDs are append-on-first-sight, so renumbering is free variation). Leaderboard reports
base-vs-mutant success delta as a *memorization score*.
**Why:** The cheapest anti-overfitting instrument that exists for this benchmark, and a clean
science axis nobody else reports: a model that solves `testchamber_000` but not its mirror is
pattern-matching, not reasoning. Directly answers the reviewer question "how do you know the
VLM hasn't seen Portal 2 walkthroughs?" — it has, that's *why* mutants matter.
**Effort:** M (pure `.p2c` transformation given idea 8's parser; mirroring with portals is the
subtle case — chirality changes nothing logically but tests spatial reasoning hard).
**Deps:** `.p2c` parsing (subset of 8 or 9).
**Unlocks:** memorization-vs-generalization headline plot; reviewer-proofing for the D&B paper.

### 11. Attribution leaderboard: perception / reasoning / actuation scored separately
**What:** Operationalize the macro boundary as a *scoring* instrument, not just a design
principle. Per episode, compute from the `.trajectory`: (a) plan-validity — did the accepted
verb sequence, executed by an oracle actuator (or replayed with a pathfinding `go_to`),
solve the chamber? (b) actuation loss — steps lost to BLOCKED/STUCK on correct plans; (c)
knowing-doing gap (BALROG's metric, already proposed in-repo, unimplemented) — model states the
right subgoal in its reasoning text but emits a wrong verb. Leaderboard columns: Solved%,
Reasoning-Solved% (oracle-actuated), KD-gap, tokens/solve.
**Why:** This is the benchmark's differentiator. Every other game eval reports a success scalar;
first light proved this stack can *attribute* failure (reasoning solved by step 7, locomotion
ate 8–24). A leaderboard that tells entrants *which layer* to improve is more scientifically
useful and harder to Goodhart. It is also the pitch's thesis rendered as a metric.
**Effort:** M (the `.trajectory` already stores everything needed; the oracle-actuator replay
needs the pathfinding `go_to` from the macro lens, or a human-piloted replay as v0; KD-gap
needs an LLM-judge pass over reasoning text — fine offline).
**Deps:** `.trajectory` (exists); pathfinding `go_to` (other lens) for the clean version.
**Unlocks:** the headline result format for M5; per-step verdict sidecars = the labeled failure
dataset the viewer's writeback seam reserved.

### 12. Budget standardization: tokens are the FLOPs of the frozen-model track
**What:** Define the frozen-VLM track's resource rules: hard cap on input+output tokens per
episode (first light: 686k in / 6.6k out — so caps like 1M in / 50k thinking are realistic),
fixed step budget, pause-while-thinking sanctioned (VideoGameBench-Lite precedent; our
tick-gating already implements it), declared model ID + any scaffold. Leaderboard shows
cost-per-solve next to success.
**Why:** Without budget rules the track measures wallet size; with them it measures
token-efficiency of reasoning — a dimension labs actually compete on now. Sample-efficiency
caps are exactly what made MineRL academically accessible. Also forces the context-scaling
problem (other lens) into the open as a *scored* engineering axis rather than a hidden tax.
**Effort:** S (counters already in the trajectory; this is rules-writing + enforcement in the
eval runner).
**Deps:** none.
**Unlocks:** fair cross-model comparison; an efficiency sub-leaderboard; defensible rules
section in the NeurIPS proposal.

### 13. Trajectory-audit anti-cheat: every score ships a replayable artifact
**What:** A leaderboard entry is invalid without its `.trajectory` (frozen track) or
`.dem`+`.hdem` (RL/TAS tracks). The eval server spot-replays a random sample: re-execute the
accepted macro sequence / demo and check `chamber_complete` fires, with explicit tolerance
rules because Source is only semi-deterministic (p2sr's own TAS wiki: airborne physics objects
desync) — verification = re-run-with-tolerance + telemetry sanity (no teleports, no
`sv_cheats`-class commands; the harness can log every `ExecuteCommand`), not bit-exactness.
Add an integrity hash of `sar.so` + map to the header.
**Why:** Competitions die of their first cheating scandal; speedrunning has 20 years of
verification culture to borrow (this is literally what SAR upstream exists for — demo
verification for leaderboards). The artifact requirement is also secretly the dataset engine:
every submission donates a trajectory to the corpus (training-data lens synergy).
**Effort:** M (replay machinery mostly exists via `RenderDemo`; the rules, tolerance calibration,
and `format_version` fix — currently missing from trajectory.proto, a known latent bug — are
the work).
**Deps:** ideas 1, 5.
**Unlocks:** trustworthy public leaderboard; submission-as-dataset flywheel; speedrun-community
credibility (they will not respect an unverified board).

### 14. Track architecture: four tracks, two at launch
**What:** Write the tracks doc and launch deliberately understaffed-honest:
**Launch (year 1):** Track A — Frozen agent (VLM/LLM, agent-protocol, token-capped, the
first-light lineage). Track B — TAS/Routing (see idea 15).
**Year 2, gated on data/infra:** Track C — Learning track (RL or any training, sample-capped
on generated chambers from idea 9, evaluated on held-out). Track D — Offline-from-human-data
(gated on `.hdem` v2 with actions, idea 17).
Per-track leaderboards, MineRL-Intro-style "open" division in Track A (any scaffold allowed)
beside the "strict" division (standard percept/grammar only).
**Why:** Two-track minimum is the proven participation-broadener (NetHack symbolic/neural,
MineRL research/intro); strict-vs-open splits keep the science clean while letting hackers
play. Launching only A+B is the honest one-RE scope — announcing four and shipping two kills
credibility, shipping two well and announcing the ladder builds it.
**Effort:** S for the design doc; the execution cost lives in the other ideas.
**Deps:** A needs 1–5; B needs 15; C needs 9; D needs 17.
**Unlocks:** the NeurIPS proposal structure; a headcount story per track (this IS the org
chart of the pitch: one workstream per track).

### 15. The TAS/routing track, with p2sr as co-organizer
**What:** A routing competition on fixed chambers: minimize game ticks, submissions as
`.p2tas` scripts (SAR's existing TAS format + tooling + VS Code extension), verified by
replay on the farm with desync tolerance. Two divisions borrowed from speedrun category
culture: glitchless and anything-goes. Sub-challenge for *agents*: produce the route
automatically (search over macro/save-load anchors — the tree-search extension point the
harness architecture makes nearly free). Recruit p2sr members as co-organizers and route
verifiers; their existing leaderboard norms are the rules template.
**Why:** Three compounding reasons. (a) Community bootstrap cheat code: this track imports an
existing, obsessive, tooling-fluent community on day one — no other track has a pre-existing
audience. (b) Scientific: routing = long-horizon planning over a continuous physics space with
glitch-shortcuts; an agent that finds a faster route than humans is a *general planner* result
(the speedrunning axis of the vision). (c) Durability: p2sr has maintained SAR for a decade,
unpaid. They are the Farama Foundation this platform needs (see spiciest take).
**Effort:** M for the engineering (TAS verify harness, leaderboard glue — most pieces exist
upstream); the real cost is community relations time, which is cheap in hours and priceless
in outcome.
**Deps:** idea 13's verification machinery; idea 1.
**Unlocks:** year-3 maintenance story; spectator content (TAS races are watchable); the
routing-agent research axis; instant legitimacy with the game's actual experts.

### 16. Human baseline corpus + human-normalized scoring
**What:** A "play for science" drive: distribute the recording-enabled build (`sar_harness_record 1`
— the `.hdem` recorder is invisible at ~55–120µs/tick) to community volunteers + the p2sr
Discord; collect N≥5 human playthroughs per benchmark chamber (suite + held-out windows).
Publish median human steps/time per chamber; leaderboards report agents as %-of-human.
Consent + license (CC-BY) baked into the recruitment form. Trivially, this corpus *is also*
the solvability validator for ideas 7/8 and the seed dataset for idea 17.
**Why:** Every benchmark that mattered had human reference (MineRL's 60M frames, BASALT's
human judging, SIMA's human-level framing). Human-normalized scores make tiers comparable and
give the VP pitch its "humans: 95%, best frozen model: 40%" gap chart. The dual-use (baseline
+ validation + training data) makes this the highest-leverage *data* idea in this lens.
**Effort:** M (recorder exists; the work is packaging the recording build, a submission
inbox, dedup/QC, and community wrangling).
**Deps:** `.hdem` pipeline (exists); ideas 2/7 for what to play. `.hdem` v2 with actions
(idea 17) makes the corpus dramatically more valuable — sequence them together.
**Unlocks:** ideas 7, 8, 17; the training-data pitch (trajectories with ground-truth state);
human-gap headline metrics.

### 17. Offline-RL/BC track, gated on `.hdem` v2 (actions in the sidecar)
**What:** First ship `.hdem` v2: add the per-tick `CUserCmd` input record (the hook already
exists at `Client.cpp:586`; the format is versioned) so a human recording becomes a complete
(pixels-optional, state+action) episode without engine replay. Then define Track D: train any
policy on the released human corpus (idea 16), evaluate online on held-out chambers via the
agent protocol. Release the corpus in `.rollout` + an RLDS export (the robotics interchange
format) so it doubles as a Gemini-Robotics-shaped dataset.
**Why:** The MineRL formula (human data + sample-capped competition) is the proven engagement
engine, and the VPT formula (small action-labeled corpus → IDM → pseudo-label YouTube's 15
years of Portal 2 video) is the scale story the training-data lens needs — *this track's
dataset is that seed corpus*. The competition and the DeepMind data pitch are the same
artifact viewed from two sides.
**Effort:** M for `.hdem` v2 + RLDS export; L for the full track (baseline BC agent, rules,
eval integration). Honest sequencing: v2 now (it's small and the data is irreversible —
recordings made before v2 lack actions forever), track later.
**Deps:** idea 16 for the corpus; the dormant RL stack's data-loader extension point makes
the baseline cheap.
**Unlocks:** the training-data flywheel; a track industry labs care about; justification for
the "record everything generously" design bet.

### 18. Difficulty grading model: a calibrated hardness score per chamber
**What:** Features from the `.p2c`/entity census (element counts per class, dependency-graph
depth from connections, chamber voxel volume, portal-surface density, decoy count) + observed
outcomes (human median steps from idea 16, scripted-baseline steps, frozen-model success) →
a simple regression/ordinal model producing a 1–10 difficulty score. Validate against the
hand-tiered suite. Use it to auto-tier ideas 7/8/9 output.
**Why:** Tiering by hand stops at ~50 chambers; the rolling held-out stream (idea 7) needs
automatic grading to keep windows comparable across quarters (otherwise score drift is just
sampling drift). Also a nice standalone artifact: "what makes a Portal 2 chamber hard for
humans vs. machines" is a paper section that writes itself.
**Effort:** M (feature extraction is the bulk; the model is deliberately boring).
**Deps:** ideas 8 (features), 16 (labels), 2 (validation set).
**Unlocks:** fair window-over-window comparison; difficulty-controlled curricula for the RL
track; the hardness-frontier plot.

### 19. NeurIPS competition-track proposal package
**What:** Write the proposal to the official template: task + tracks (A+B), metrics
(success, attribution columns, human-normalized), baselines (first-light agent, scripted,
human), infra plan (local-first eval + hosted verification — explicitly NOT organizer-side
retraining), held-out protocol (idea 7), prizes (modest: $10–20k total was the norm;
co-authorship on the retrospective as a stated prize, the Melting Pot trick), timeline,
organizer list. Recruit 3–5 co-organizers before submission: a BALROG-adjacent academic, a
p2sr maintainer, ideally one frontier-lab person. Target the next cycle's deadline.
**Why:** NeurIPS acceptance supplies legitimacy, a deadline, and free marketing — but the
proposal is only credible if ideas 1–5 + 13 exist as running code, so this is deliberately
mid-list despite being the lens's namesake. A rejected-but-circulated proposal still
functions as the pitch deck's skeleton.
**Effort:** M (writing is days; co-organizer recruitment and infra-evidence gathering is the
real time).
**Deps:** ideas 1–5, 13 demonstrable; idea 15's community partner named.
**Unlocks:** the event; the retrospective paper; the headcount conversation ("competitions
need an org — here is the org chart" = the tracks).

### 20. The Valve conversation (risk-retirement workstream)
**What:** A short dossier + direct outreach to Valve: what we do (research harness, binary
plugin like the decade-tolerated SAR), what we want in writing (blessing for headless eval
farms, research-use trajectory datasets from owned recordings, Workshop chamber use via
download-on-demand, competition branding "community event, not Valve-endorsed"). Cite
precedents: OpenAI Five's Dota cooperation, the CSGO BC dataset (shipped unchallenged),
Portal 2: Community Edition's actual engine license. Fallbacks if silent: ship anyway under
the SAR-tolerance status quo, keep chamber redistribution download-on-demand-only, every
participant owns a license (it's frequently ~$1–2 on sale).
**Why:** The single existential legal risk to the competition and the dataset release. SIMA
shows labs license studio access; a VP *will* ask "what does Valve think." A written answer —
even a soft one — converts the pitch's biggest unknown into a slide. Cheap in engineering
time; long in calendar time, so start the clock early.
**Effort:** S of work, months of calendar. Run it in parallel with everything.
**Deps:** none. Strengthened by having ideas 2/3 to show.
**Unlocks:** dataset releases at scale (idea 17), hosted farms (idea 6), sponsor comfort.

### 21. Spectator layer: model-vs-model chamber races (the Game Arena slot)
**What:** An auto-caster: run two-plus agents on the same chamber in parallel instances,
compose a side-by-side video (annotated frames + scrolling reasoning text + step counters)
from the `.trajectory` files, auto-publish per leaderboard refresh. Optionally live-stream
marquee matchups (new frontier model release vs. incumbent).
**Why:** Claude/Gemini Plays Pokémon proved long-horizon game evals have enormous public
reach, and Kaggle Game Arena proves DeepMind leadership *currently* wants spectator-legible
evals — this is the exact slot in a VP's head the pitch should occupy. Visible thinking +
visible failure (an agent pacing a glass box, reasoning aloud) is uniquely watchable content
that doubles as failure-taxonomy outreach. Marketing is what competitions are *for*; this is
the marketing engine.
**Effort:** M (everything needed is in the trajectory files; this is rendering/compositing +
publishing glue).
**Deps:** ideas 2, 6 (something worth racing on).
**Unlocks:** public attention, sponsor interest, the cultural proof point for the pitch.

### 22. Stewardship & exit plan, written down on day one
**What:** A one-page governance doc published with the benchmark: code license (keep SAR's),
data licenses, who maintains what, and the explicit succession ladder — solo-RE → p2sr
co-maintainership (idea 15) → Farama-style adoption or a foundation/lab sponsor → archived-
with-dignity (formats documented, eval reproducible offline). Plus the publication pipeline
commitment: D&B paper for the benchmark, retrospective with top entrants as co-authors,
dataset DOIs.
**Why:** "Who maintains it in year 3" is the question every dead platform failed (Gym,
Retro, DMLab) and the question a funder asks fourth. Having the answer *written* before
launch is rare and credible. Costs almost nothing.
**Effort:** S.
**Deps:** none; names idea 15's partnership.
**Unlocks:** funder confidence; community trust; the difference between a project and an
institution.

### 23. Co-op track: two-agent cooperative chambers (flagged honestly: not now)
**What:** Portal 2's co-op campaign + PeTI co-op chambers as a two-agent cooperation
benchmark: shared chamber, two embodied agents, a ping/signal grammar (the in-game gesture
system is a natural emote vocabulary), joint success metric. Melting Pot's cooperation
framing in a first-person physics world.
**Why:** No embodied first-person *cooperation* benchmark exists at this fidelity; it is the
single most differentiated future track and a MARL-community magnet. But: Portal 2 co-op is
listen-server-only (no srcds), the multiplayer mod stack (P2:MM) is Proton-fragile, and the
harness is single-instance-single-player throughout. This is genuinely XL.
**Effort:** XL (needs a team; for one RE it would consume the year).
**Deps:** everything stable in single-player first; possibly the Strata/P2:CE substrate
(64-bit, actively developed) rather than fighting the 32-bit listen server.
**Unlocks:** a year-3 flagship; a clean headcount line-item in the pitch ("co-op track = 2
engineers, here is why").

---

## Sequencing sketch (the one-RE critical path through this lens)

```
now ──► 1 (oracle, S) ──► 2 (suite, M) ──► 3 (starter kit, M) ──► 4 (p2bench, M)
                  │                                    │
                  ├──► 16 (.hdem corpus, M) ──► 17 (hdem v2 + RLDS, M)
                  ├──► 8 (workshop pipeline, L) ──► 7 (rolling held-out, M) ──► 18 (grading, M)
                  └──► 5 (protocol, S) ──► 13 (anti-cheat, M) ──► 6 (hosted board, L)
parallel, calendar-long: 20 (Valve), 15 (p2sr courtship), 22 (governance, S)
then: 19 (NeurIPS proposal) when 1–5+13 are demoable; 21 (spectator) opportunistically;
9/10 (generator/mutants) as the science deepens; 14 formalizes; 23 is the headcount ask.
```

The honest headcount story this lens supports: one RE gets you ideas 1–5 + 13 + 16 (the
*benchmark*); the *competition* (6, 7, 8, 15, 19, 21 run concurrently and continuously) is
where the second and third hires go; the co-op track (23) and generator-at-scale (9) are the
team-sized line items. That is the org chart of the pitch.

---

## Spiciest take

**The track everyone will rank last — TAS/routing — is the one that makes this platform
immortal, and the stewardship plan already exists: it's the speedrunners.** Every dead
platform (Gym, Retro, DMLab, Universe) died of lab pivot + maintenance vacuum; the only
communities that maintain 15-year-old game tooling indefinitely, for free, with verification
culture already built in, are speedrunning communities — and this project is *literally a
fork of their tool*. p2sr has maintained SAR for a decade without a single grant. Design the
competition so speedrunners win something real (a routing track, co-organizer credits,
verification authority), and you get the Farama Foundation outcome without founding a
foundation. The frozen-VLM track gets the citations; the TAS track pays the maintenance bill.

## If I could only do ONE thing next week

Build the `chamber_complete` engine hook (idea 1, days), then author 10 tiered PeTI chambers
and run the existing gemini-3.5-flash agent across all of them (idea 2). That converts first
light from an anecdote (n=1) into the first row of a benchmark (a success-vs-tier curve) —
the seed artifact for the starter kit, the NeurIPS proposal, and the VP pitch alike.
