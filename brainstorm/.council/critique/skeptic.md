# Skeptic — the VP Research red team

*Critique-mode council output, 2026-06-12. Role: the tough, fair VP Research at Google DeepMind
hearing this pitch. My job is to find every reason to say no, so the pitch can pre-empt it. I have
read the diverge docs, the ROADMAP, and the first-light writeup. I am not hostile to the project —
I am hostile to every claim in it that has not earned its weight, because in the actual room,
one punctured claim costs more than ten missing ones.*

A meta-observation up front: the council is unusually good at anticipating objections — risk
registers, kill criteria, claim ladders, contamination probes all appear unprompted. Its
systematic weakness is that nearly every rebuttal is **work not yet done, presented at the
confidence of work already done** ("~40 LOC", "one PR and one Discord conversation", "days, the
hook already exists"). A VP's diligence pass will notice that the answers are plans. Part of my
job below is marking which rebuttals are real *today*.

**Format per pushback:** severity (**KILL-SHOT** = ends the meeting or the workstream if
unanswered / **SERIOUS** = costs the ask unless pre-empted / **ANNOYING** = wastes five minutes,
survivable), the attack at full strength, the strongest *honest* rebuttal available, and the
evidence/work that defuses it.

---

## Part 1 — The pushbacks

### P1. Contamination: every frontier model has read every Portal 2 walkthrough ever written

**Severity: KILL-SHOT** — for the headline claim "reasoning is solved," as currently phrased.
Survivable for the benchmark as a whole, *if* re-framed.

**The attack.** Portal 2 is one of the most documented games in history: 15 years of
walkthroughs, wikis, Let's Plays, speedrun commentary, and the literal sentence "put the cube on
the button" saturating pretraining corpora. The first-light percept then *names the objects*:
the model receives a mark list with classes — weighted cube, floor button, door — plus status
fields. At that point "cube → button → door in 7 steps" is not reasoning; it is schema retrieval
with grounding assistance. My first question after the cold open: *"How do you know it didn't
just remember the game?"* — and "the chamber is hand-built" is not an answer, because the
*mechanics* are memorized even when the *instance* is novel, and a 3-element chamber has exactly
one schema to retrieve.

**Strongest honest rebuttal.** Two-part. (a) Mechanics knowledge is not instance contamination —
the same way knowing algebra doesn't contaminate a fresh algebra problem. The benchmark's claim
should be *multi-step grounded execution under novel instance topology*, which memorization does
not supply: step 12 (noticing it knocked the cube off the button and re-placing it) is in no
walkthrough. (b) The council has already designed the controls: mutation probes
(benchmark-competition #10), geometry-isomorphic remakes with a measured contamination delta
(eval-science #5), decoy-wiring/distractor ablations (puzzle-gen #12), generator-fresh instances
(puzzle-gen #9). The stock-vs-isomorphic delta is a number nobody in the world has measured, and
it is publishable either way it lands.

**What defuses it.** Run the contamination battery *before* any external claim: stock chamber vs
isomorphic remake vs decoy-wired mutant, 3 models, report the deltas. Retire the phrase
"reasoning is solved" entirely (eval-science already demanded this — listen to them). The
defensible phrasing: "frozen VLMs execute multi-step grounded plans on trivial instances; the
measured contamination bonus is X%; here is where the ladder breaks them."

---

### P2. "A frozen VLM solved a trivial chamber" — against the 2026 bar, so what?

**Severity: SERIOUS**, borderline KILL-SHOT for the result *as a result* (it survives as an
instrument-calibration story).

**The attack.** It is mid-2026. Frontier agents run multi-hour computer-use sessions, play
Pokemon on stream for days, and clear large fractions of OSWorld. Against that bar, "7 macro
steps over a ~10-mark discrete action space, with oracle perception and engine-executed
actuation" is below the line. Worse: nobody has shown me the floor. A scripted baseline — pick
up nearest cube, drop on nearest button, walk to door — plausibly solves tier-1 chambers with
zero model calls. If the greedy bot matches Gemini on tier 1, the first-light slide is dead on
arrival. The effective decision space per step is a handful of verbs times a handful of marks;
the very capable pipeline around the model may be doing 95% of the work.

**Strongest honest rebuttal.** Correct — and it is the design, not the embarrassment. The pitch
is an *attribution instrument*, not a difficulty record: the macro boundary exists precisely to
separate reasoning from actuation, and tier-1 was the calibration shot. The puzzle-gen lens
already specifies the probe ladder (random-macro / scripted-greedy / Flash / Pro) as the
difficulty meter — the council independently invented the baseline floor. And first light's real
content is the *failure*: a frontier model that cannot walk out of a glass box is a finding
about embodied actuation that OSWorld structurally cannot produce.

**What defuses it.** Baseline floors on every chart, forever: random-macro and scripted-greedy
columns beside every model. A tier where the greedy bot fails and frontier models also fail
*for reasoning reasons* — proven via the teleport control arm (eval-science #3), not asserted.
Until that exists, the 7-step number should not be shown to anyone senior.

---

### P3. Single-game risk and "yet another game benchmark" fatigue

**Severity: SERIOUS** (KILL-SHOT if the deck says "benchmark" in the first five slides).

**The attack.** The graveyard is the council's own favorite citation: Malmo, Universe, Gym
Retro, MineRL, Obstacle Tower, ViZDoom, DMLab. Every one launched with a "unique properties"
slide; both this lab and OpenAI *exited* the category. The field has fatigue specifically for
single commercial games: niche optics, platform risk, no licensing story, and a community that
has learned these die when the founding attention moves. And a single game is n=1 at the
*environment* level no matter how many chambers are generated inside it: one visual domain, one
physics, one mechanic family.

**Strongest honest rebuttal.** Three properties the graveyard entries did not stack, plus a
reframe. Properties: (a) in-game authoring (PeTI) so the task distribution grows without the
maintainers — the closest analog is Minecraft, the one game environment that did *not* die;
(b) a maintenance community that predates the research interest (p2sr has maintained this
repo's upstream for a decade, unpaid) — the stewardship answer every dead platform lacked;
(c) formal difficulty grounding (Demaine) that Minecraft and NetHack lack. The reframe is
harness-platform's: the product is not "Portal2Gym" but a pausable, ground-truth-instrumented,
soon-branchable *simulator protocol* around a commercial 3D engine — the thing Universe failed
to be — and the multi-title audit shows the pattern ports across Source games.

**What defuses it.** The "what this measures that BALROG/OSWorld/Crafter cannot" table with a
receipt per row (frozen-step deliberation, per-tick ground truth, attribution arms,
authorship-to-eval). A second Source title with one annotated chamber (harness-platform #20),
converting "Portal 2 benchmark" into "instrument, instantiated twice." External adoption proof
— see P12.

---

### P4. Valve IP and licensing, for a Google-branded anything

**Severity: KILL-SHOT** for the public competition, dataset redistribution, and anything with a
Google/DeepMind logo. **ANNOYING** for internal research use today.

**The attack.** The council's precedent stack is weaker than its deployment. OpenAI Five was a
*negotiated partnership* — it proves you need a deal, not that tolerance is the default. The
CSGO BC dataset shipping unchallenged proves an academic with no money is not worth suing;
Google's exposure and Google's legal review bar are categorically different. P2:CE proves Valve
licenses engine code to a community mod team — nothing about blessing a corporate eval farm or
a public dataset of rendered frames. "SAR tolerated for a decade" is community goodwill, not a
position on headless farms and frame redistribution. Two unexamined exposures: (a) Steam
Subscriber Agreement vs N headless instances — accounts, licenses, concurrent sessions; the
launcher's port math is engineering, not law; (b) redistributing rendered frames in a public
dataset — the gray zone data-flywheel #20 names and then routes around. Internal counsel will
not accept "the community never got sued."

**Strongest honest rebuttal.** The risk is real, known, bounded, and cheap to retire relative
to its severity — which is why three lenses independently proposed the Valve packet. The
architecture is already licensing-defensive: zero Valve assets shipped (container pulls via
steamcmd under user credentials), workshop content download-on-demand, lazy-pixel storage
(state canonical, frames regenerated locally, so the *distributed* data can be frame-free).
And the path is warm: p2sr and the Strata/P2:CE team are existing relationships, and SIMA
proves this lab does studio deals when it wants a substrate.

**What defuses it.** (1) Open the Valve thread *before* the pitch — "we have a conversation in
progress" beats any precedent list; (2) a one-page posture doc covering the Steam-account
topology for farms; (3) keep public artifacts frame-free until (1) resolves — entity-state +
action datasets are unambiguously yours; (4) P2:CE licensing as the endgame. Never present the
precedent stack as clearance; present it as the reason the conversation should go well.

---

### P5. A 32-bit retail binary from 2011 as the foundation

**Severity: SERIOUS** (longevity and ops), not kill-shot.

**The attack.** The substrate is a depreciating asset Valve can break with any depot update,
instrumented via signature scanning and runtime hooking (the repo's own phase4 docs read like a
war journal), inside a 3GB address space the chaos-suite proposal admits can be exhausted. The
32-bit multilib toolchain is actively rotting out of Linux distros. You are asking me to fund a
five-year program on an engine whose owner owes you nothing and whose binary you patch at
runtime; every hour the engine fights you is an hour not spent on science, and the repo
documents many such hours.

**Strongest honest rebuttal.** (a) Portal 2 is a finished game, not a live service; its binary
has been stable for years, and SAR has survived a decade of its updates with a volunteer team.
(b) Pinned-depot containers freeze the target, and a CI gate replaying golden transcripts
detects breakage in hours, not weeks. (c) The succession plan exists and is named — P2:CE,
64-bit, Valve-licensed, spike already scoped (harness-platform #19, moonshots #22). No dead
platform had a written engine-succession plan; this one has it before launch.

**What defuses it.** Ship the container with a pinned depot manifest; run the P2:CE feasibility
spike and publish the report (a negative result is still credibility); write the
update-response playbook. Put the toolchain rot in the risk register in your own words before
I say it in mine.

---

### P6. The robotics/Gemini-data story is mostly vapor (and never say "Waymo")

**Severity: KILL-SHOT** for the robotics-data pitch as worded; **SERIOUS** for the softer
SIMA-shaped version.

**The attack.** Walk through what a robotics data buyer pays for: contact-rich manipulation,
real sensor noise, real-world visual distribution, human-plausible kinematics. Portal 2
trajectories have none of these. The player is a velocity-controlled capsule; grabbing is a
binary attach with no gripper; the *optimal* human play style (air strafes, ABHs, save/load
abuse) is anti-physical; the visual domain is a stylized 2011 game. The sim-to-real literature
is a graveyard of "game data will transfer" hopes, and SIMA's own transfer claims are modest.
Volume kills the rest: the active P2 speedrun population is in the low hundreds; the pilot
corpus is 50–100 hours against VPT's 70k-hour bar. And "Waymo" in the vision is a non-sequitur
that costs the room's respect — first-person puzzle chambers teach a driving stack nothing.
The flywheel diagram is drawn; the buyer is hypothesized.

**Strongest honest rebuttal.** Narrow the claim and it becomes defensible: not "robot training
data" but *embodied-agent pretraining and eval data* — long-horizon, first-person,
language-annotatable, with perfect state labels and the cheapest counterfactuals in existence.
Two council products are genuinely scarce: (a) counterfactual branch data from identical
physical states (data-flywheel #11) — DPO/process-supervision pairs reality cannot produce at
any price, because reality doesn't reload; (b) the portal-transit world-model eval set
(data-flywheel #12) — ground truth Genie-class models lack by construction. And the council
already proposed the falsifiable test (moonshots #18: mix into an open VLA fine-tune, measure
LIBERO/SIMPLER deltas) — the data claim can be *settled for a few thousand dollars* rather than
asserted.

**What defuses it.** Run the RLDS-mix experiment early and report it whichever way it lands —
"we tested transfer, here's the number" beats the claim in either direction. Lead the data
story with counterfactuals and the wormhole eval, not with hours. Delete Waymo from every
document, today.

---

### P7. One-RE bus factor — and the velocity exhibit cuts against the ask

**Severity: SERIOUS**, and double-edged in a way the council has not noticed.

**The attack.** Two prongs. (a) Standard bus factor: one person holds the engine RE knowledge,
the eval design, the RL stack, the formats, and the community relationships; if they leave, the
asset rots in a quarter. "Bus factor = 1 — that IS the ask" is a quip, not a mitigation:
funding does not transfer knowledge out of one skull. (b) The subtler prong: pitch-strategy #8
proposes a velocity exhibit — look how much one RE plus agentic coding shipped in weeks. I can
draw the opposite conclusion from the intended one: *if agentic coding gives one engineer the
output of a small team, why fund 4.5 more heads? Fund zero, wait two quarters, see what one
person ships.* The exhibit prices the marginal hire DOWN.

**Strongest honest rebuttal.** (a) The stewardship ladder is written before launch
(benchmark-competition #22): solo-RE → p2sr → Farama-style adoption, and p2sr's decade of
unpaid SAR maintenance makes the succession non-hypothetical; the formats are specced for
independent readers precisely so the project survives its founder. (b) The velocity answer:
the exhibit demonstrates leverage, and leverage multiplies hires too — but the honest version
is that velocity bought the *instrument*, while the remaining bottlenecks (community ops,
corpus collection, the Valve relationship, multi-month search research, hosted eval) are
calendar- and relationship-bound, which agentic coding does not compress. That is why the five
seats are those five seats.

**What defuses it.** One external contributor landing PRs (proves delegability). The formats
spec published (proves survivability). Reframe the velocity exhibit explicitly around *which
work was compressible and which was not* — make the calendar-bound residue the justification
for headcount, or the exhibit will be used against the ask.

---

### P8. Cost and ops of a competition (and of the benchmark itself)

**Severity: ANNOYING** → **SERIOUS** if the answer is hand-waved.

**The attack.** MineRL's organizers ran themselves ragged on submission retraining; BASALT paid
MTurk; Universe died partly of ops. This benchmark's unit of evaluation *boots a commercial
game inside gamescope*. Hosted eval means per-instance licensing, GPU-or-lavapipe unknowns,
crash-recovery plumbing (harnessd is still a proposal), anti-cheat verification labor, and a
rolling ladder someone must operate — and there is no "someone" until the ask is granted.
Token costs: first light burned 686k input tokens on a *trivial* chamber; a 1000-chamber
multi-model sweep at stateful-chat shape is a five-figure bill per iteration. What does one
leaderboard row cost in dollars and human hours? Ten lenses, and nobody wrote the number.

**Strongest honest rebuttal.** The architecture choices are cost-aware by design:
agents-as-callables make hosted eval inference-only (the MineRL lesson, learned); local-first
p2bench means most usage costs the organizer nothing; Kaggle/AIcrowd hosting externalizes the
ladder; the O(1)-context agent (model-frontier #1) collapses the token bill an order of
magnitude; prize norms are $10–20k, not millions; and the competition is explicitly positioned
as marketing for a platform, not the deliverable.

**What defuses it.** The numbers slide (pitch-strategy #21) *with measured numbers*: $/eval-run
at current and O(1)-context shapes, instances/box, crash MTBF, projected ladder ops-hours/week.
Run one full internal competition rehearsal (3 models × 20 chambers) and publish its all-in
cost. A VP who has funded competitions will ask for exactly this table.

---

### P9. What do Genie-class world models make obsolete here?

**Severity: ANNOYING** today; **SERIOUS** over the five-year horizon the ask implies.

**The attack.** The house strategy is: generate infinite interactive worlds, train agents
inside them, difficulty dial included. If that works, a fixed 2011 engine with one mechanic
family is a museum piece — why fund the harness when Genie N+1 emits portal-like worlds on
demand, with no Valve, no 32-bit, no gamescope? The UGC argument thins too: 950k human
chambers vs unbounded generated ones.

**Strongest honest rebuttal.** This is the one pushback where the council's answer is close to
airtight, and it should be delivered with confidence: generated worlds have no ground truth —
their physics is the model's opinion. A real engine with per-tick state is the **verifier and
calibrator** the generative loop publicly lacks: the wormhole benchmark (data-flywheel #12) is
a concrete eval such models fail and cannot self-grade, and a SIMA-2-style
generate/filter/distill loop *requires* a non-hallucinatable reward — `chamber_complete` is
one, a rubric is not. World models rising makes the calibration instrument more valuable, the
way better telescopes need better reference stars. And Genie produces neither human data nor
complexity-grounded difficulty.

**What defuses it.** Ship the wormhole eval set and get one world-model team to run it. A
single "your model scores X on portal-transit consistency; here is the exact ground truth"
exchange converts this slide from defense into the sharpest hook in the deck.

---

### P10. The science hygiene of "first light" (N=1, censored, author-built)

**Severity: SERIOUS** — a self-inflicted wound if any external artifact ships before it's fixed.

**The attack.** One chamber, one model, one seed, author-built map — and steps 8–24 were
censored by the locomotion wall, so "reasoning is solved" was never tested past step 7. The
back half charged every failure to locomotion *by default*, because the harness gave the model
no way to fail at reasoning once go_to started returning BLOCKED. The eval-science lens said
this in its own spiciest take and is right: the benchmark is currently telling its authors the
story they already believe. Beyond the missing controls, the basics are missing: no seeds, no
CIs, no pre-registered metrics — and Source's semi-determinism means run-to-run variance is
real and unmeasured.

**Strongest honest rebuttal.** It is day two, the project knows it, and — unusually — the
controls were proposed *by the project itself* before any reviewer asked: the teleport
actuation-oracle arm, the {plan, no-plan} × {real, teleport} 2×2, the pre-registered stats
spec, censoring-aware solve curves, the macro-determinism audit. The instrument's purpose is
to make attribution measurable rather than asserted; first light is the existence proof of the
pipeline, not the finding.

**What defuses it.** Execute eval-science #1–#8 before anything public: oracle, terminal
taxonomy, teleport arm, 20-chamber ladder, contamination probes, stats spec, cross-model
matrix, solve curves. The minimum citable kernel: **3 models × 20 chambers × 5 seeds ×
{real, teleport}, with baseline floors.** Anything less is content marketing.

---

### P11. Throughput economics: a real-time engine under the search/RL/data claims

**Severity: SERIOUS** for the routing, RL, and data-scale stories; minor for the frozen-VLM track.

**The attack.** The engine runs ~60 ticks/sec; demo rendering is ~1× realtime; save/load is
~100ms *claimed*, unmeasured; the fast-forward mode is designed-but-unbuilt against an engine
whose own perf docs "punish optimism." On top of this substrate the council stacks MCTS over
save-states, full-leaderboard demo ingestion, 100k-hour corpora, and a PPO resurrection.
Procgen does 10k steps/sec; this does 60 on a good day. Every search idea's iteration count
and every data idea's scale number silently assumes a multiplier that does not exist. The
fleet answer (32–128 instances) is a procurement line, not a solution — instances of a *game*
with compositor and (maybe) GPU dependencies.

**Strongest honest rebuttal.** For the frozen-VLM track — the actual near-term product —
throughput is irrelevant: API latency dominates and world-freeze makes deliberation free in
both wall-clock and tokens. The multiplier work is scoped and precedented in-repo
(`sar_tas_skipto` already fast-forwards, `mat_norendering` exists, SHM copies are opt-in per
tick), and the lavapipe spike is a one-day test that settles the GPU question. The honest
position: search/data scale claims are *gated* on the throughput program (harness-platform
#8), which is why it is a named workstream rather than an assumption.

**What defuses it.** Measure before claiming: a published ticks/sec table (idle / macro /
search-mode / N instances), measured save/restore latency, the lavapipe verdict — then re-cost
the MCTS and corpus ideas against measured numbers and demote, in writing, whatever dies. A VP
forgives "this needs a 20× we haven't built yet"; they do not forgive discovering the
assumption themselves.

---

### P12. Where is the demand side? (the pushback nobody assigned themselves)

**Severity: SERIOUS.**

**The attack.** Ten lenses produced ~230 ideas and not one is "talk to a potential user." Who
is the *second* user of this harness? Which agent-eval team has said "we'd run our model on
this if X"? Which planning researcher wants the routing benchmark? Which robotics team would
load the RLDS shard? The council output is entirely supply-side: build, then pitch. The
graveyard says benchmarks die of *no adoption*, not missing features — and adoption is testable
this week with a repo link and ten conversations, at zero engineering cost. A VP funds things
their teams already want; the pre-wired TL memo (pitch-strategy #16) is the only demand-side
idea in 3,600 lines, and it is framed as pitch tactics, not product discovery.

**Strongest honest rebuttal.** Partial: the project deliberately sequenced
instrument-before-audience (first light was the de-risking gate, and it passed), and several
artifacts are designed as adoption probes — pip-install p2bench, the starter kit, the landing
page, all on the Crafter zero-friction model. But there is no real counter to "you haven't
asked anyone yet" except to go ask.

**What defuses it.** Ten structured conversations before the VP meeting: three agent-eval
people, three academic RL/planning people, two world-model people, two robotics-data people.
Each gets the sizzle video and two questions: would you use this, and what's missing? Bring
the answers as a slide. If nobody bites, that is the cheapest possible time to learn it.

---

### P13. Claim inflation across the council output (the meta-pushback)

**Severity: ANNOYING** per instance, **SERIOUS** in aggregate — credibility is a solo pitcher's
only asset, and it fails like a dam, not a dial.

**The attack.** The diverge docs contain superlatives a single skeptical TL can puncture with
one search each: "contamination is impossible," "the only grounded two-agent-communication
benchmark on Earth," "chess saturated in a week," "a move-37 moment," "the recording host on
every active runner's machine." After the second puncture, the room discounts everything —
including the true claims, of which this project has plenty.

**What defuses it.** A fact-check pass over every externally-visible artifact, with the claim
ladder (pitch-strategy #13) applied to *all* docs, not just the deck. Part 2 is the worklist.

---

## Part 2 — Factually shaky claims in the council output

"Shaky" = wrong, unverifiable as stated, or true-but-misleading as deployed.

1. **"Cube+button+door alone is PSPACE-complete" (ROADMAP; invoked everywhere).** Two problems
   as used. (a) *Check the theorem.* The Demaine–Lockhart–Lynch results attach to specific
   gadget constructions under specific mechanic sets; the per-mechanic attributions scattered
   through the council docs (puzzle-gen's "laser+relay = PSPACE," moonshots' "timed-button
   NP-hardness chains") need verification against the actual paper — some look extrapolated.
   (b) *Asymptotics vs instances.* PSPACE-hardness is a statement about scaling families; every
   actual chamber is O(1), and the eval chambers are tiny instances. "Our benchmark is
   PSPACE-complete" is rhetorically potent and technically vacuous as a difficulty claim for
   any finite suite — a complexity theorist in the room will say so. Safe usage: "the element
   set is rich enough that the *family* is PSPACE-hard, so the difficulty ladder has no
   structural ceiling" — and nothing stronger.
2. **"Chess saturated in a week of Game Arena" (pitch-strategy #17; echoed in puzzle-gen).**
   As far as I can tell this is backwards: the Game Arena chess exhibitions showed frontier
   *LLMs* were strikingly weak at chess — blunders, illegal-move scaffolding — the opposite of
   saturation. Chess is saturated by engines, not by LLMs. The anti-saturation slide is built
   on a premise the room can falsify from memory. The defensible version: "closed games with
   superhuman engines make poor headroom stories; routing against a live human leaderboard has
   no engine ceiling." Verify before any deck exists.
3. **"SAR is mandatory on the Portal 2 speedrun.com leaderboards" → "a standing VPT-style
   contractor workforce" (data-flywheel; moonshots).** Verify the actual rule text (SAR
   required for which categories; demos required where; board.portal2.sr's separate policy).
   Even if literally true, the inflation is population: active P2 runners number in the low
   hundreds, and their play is glitch-dense speedrun movement — a small, biased corpus, not a
   workforce. The framing invites a fatal per-hour comparison with VPT's 70k hours.
4. **"~954k Workshop items (verified mid-2024), third-largest on Steam."** "Verified" rests on
   one scrape that should be re-run live; "third-largest" is dubious against Wallpaper Engine /
   Garry's Mod / Dota 2 / CS workshops (the docs themselves hedge with "verify before
   printing"). More important: the usable fraction is unknown — how many are PeTI puzzles that
   load, are solvable, and aren't duplicates? Pitch "950k raw, N verified after triage" only
   after triage produces N. Today N could be anything from 2k to 200k.
5. **The Valve precedent stack deployed as implied clearance.** Each precedent proves something
   narrower: OpenAI Five = a negotiated partnership (you need a deal); CSGO dataset = an
   unsued academic (survivorship, not precedent); P2:CE = an engine license to a mod team;
   tolerated SAR = community goodwill about a plugin, silent on frame datasets and farms.
   Presenting the stack as "risk retired" rather than "risk retirable; conversation opened" is
   the overreach counsel will catch.
6. **"C9 save/load anchors ≈ 40 LOC" + "marks are save/load-invariant (already designed)"
   (speedrun-routing; model-frontier; data-flywheel).** Designed ≠ tested. Source save/load
   visibly perturbs physics state — the speedrun community *exploits* save/load glitches,
   direct evidence restores are not clean snapshots. Every MCTS / best-of-N / counterfactual
   idea in four lenses rests on this one untested primitive. The mark-stability and
   state-fidelity tests (harness-platform #12) are mandatory before any search result is
   claimed.
7. **"RLDS exporter: days" → "your team can train on it Monday" (data-flywheel #3).** Format
   compatibility is days; *semantic* compatibility (action-space mapping, embodiment mismatch,
   camera conventions) decides whether ingestion produces value. "Loads in tfds" is being
   conflated with "useful to Gemini Robotics" — see P6.
8. **".hdem recorder is invisible at ~55–120µs/tick" (benchmark-competition #16).** Measured on
   one machine and one chamber class, presumably. At a runner's 300fps loop, 55–120µs is 2–4%
   of frame budget — likely fine, not "invisible" — and speedrunners will benchmark it
   themselves before adopting. Ship the methodology with the upstreaming PR.
9. **"Frontier models will saturate stock chambers within a year" (speedrun-routing spiciest)
   vs "reasoning failures by tier 3" (eval-science spiciest).** Both deployed as load-bearing
   in different lenses; both cannot be. This is an empirical question the 20-chamber ladder
   answers in weeks — let it, and stop asserting either direction.
10. **"Only commercial game with author-able two-player puzzle dependency / only grounded
    two-agent-communication benchmark on Earth" (moonshots).** Overcooked is *the* standard
    grounded-coordination benchmark; Hanabi, co-op Minecraft maps, and the We Were Here series
    exist. The defensible claim is narrower: first-person 3D *physics* co-op with built-in
    authoring and ground-truth state. As written, it's a one-search puncture.
11. **"Reported >$1B/yr environment-procurement discussions" (pitch-strategy #14).** A press
    rumor deployed as a market size, in front of a VP who may know the real numbers. Quote a
    named, dated source or cut it.
12. **"SIMA 2 + Genie 3 press concedes no proven transfer / hallucinatable physics"
    (pitch-strategy #14, #16).** Paraphrasing the target lab's own publications *to that lab's
    VP* demands quote-level accuracy. The pre-read memo lives or dies on getting their
    limitations section verbatim right. Pull exact sentences before writing it.
13. **Demo-corpus ingestion scale (speedrun-routing #4; data-flywheel #7).** Rendering at
    ~1× realtime means thousands of leaderboard demos = months of machine time, plus
    documented .dem replay desync. The lens is honest locally ("M for a few chambers, L for
    full board") but upstream flywheel slides treat the corpus as nearly free. Reconcile.
14. **"686k tokens ⇒ quadratic context growth" (first_light §5d; model-frontier).** Fine as an
    internal observation, but before it anchors the cost slide: separate image vs text tokens
    and account for implicit prompt caching (the TokenUsage fields exist). Compute the real
    curve, not the narrative one.
15. **"In-game PeTI compile via ExecuteCommand" (puzzle-gen #3).** Correctly flagged by its own
    lens as the load-bearing unknown — but moonshots #3 and the LLM-setter demo build towers on
    it without inheriting the flag. Any plan that presumes headless .p2c→.bsp compilation
    should carry the dependency explicitly.

---

## Part 3 — The verdict, as the VP would give it

What I would actually say after a strong version of this pitch: *"The macro-boundary
attribution idea is genuinely good, and first light proves the pipeline runs. But you've shown
me one model, one chamber, one seed, on a game my models have memorized, with no baselines, no
controls, no users, an unresolved IP question, and decks containing claims my TL can puncture
from memory. Come back with the kernel."*

**The kernel that earns the second meeting** — all of it already in the council's own output;
the skeptic's contribution is sequencing and the one missing item:

1. **The controlled result:** 3 models × 20-chamber ladder × 5 seeds × {real, teleport} arms,
   with random/greedy baseline floors and the contamination delta measured (eval-science
   #1–#8, benchmark #2, pitch #1–#2).
2. **One settled economics table:** measured $/eval-run, ticks/sec, instances/box, save/restore
   latency (pitch #21, harness-platform #8).
3. **The Valve thread opened**, posture documented (three lenses, same idea — do it first).
4. **Ten demand conversations**, results on one slide (nobody's idea — see P12).
5. **The claim-hygiene pass** over everything external (Part 2 is the worklist).

What I would *not* fund even if pre-empted perfectly: the hosted eval service, the co-op
track, the YouTube/IDM harvest, and full-campaign routing — team-sized bets stacked on
unvalidated primitives (anchors, throughput, demand). The council's effort tags are honest
about this; the pitch must not let the moonshots leak into the ask.

The strongest sentence available to the pitch, in my voice: *"Every objection you just raised
is a measurement this instrument exists to make — and here are the first five, already
measured."* Get to where that sentence is true, then book the meeting.
