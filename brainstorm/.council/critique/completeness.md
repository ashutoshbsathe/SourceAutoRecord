# Completeness critique — what the 10 lenses missed

*Role: completeness critic over `brainstorm/.council/diverge/` (10 files, ~3,600 lines,
~230 ideas). Method: read the index + sampled files, then keyword-swept all ten for
candidate blind spots (audio, real-time, death/respawn, safety, Waymo, faithfulness, ToS,
PII, deprecation, education, grants, Demaine-as-person, Windows, DevRel). Claims of "zero
coverage" below are grep-verified, not vibes.*

The council is impressively complete on its own axes — success oracle, chamber suites,
generation, data products, routing, cost engineering, and pitch mechanics are each covered
3–5 times from different angles. The gaps are *orthogonal axes nobody owned*, a few
*stakeholders nobody named*, several *unglamorous landmines*, and a handful of *next-week
actions everyone assumed but nobody assigned*.

---

## 1. Missing axes (no lens covered these)

### 1.1 Audio is a literal zero — and the user's vision says "Gemini Omni"

Across ~230 ideas, every percept is pixels + symbols. Not one idea mentions game audio
(the only hit is "voice or chat" inside the co-op moonshot). Yet:

- Portal 2's audio is *semantically load-bearing*: turrets announce acquisition before
  firing ("There you are"), buttons/doors/fizzlers/droppers have distinct event sounds,
  excursion funnels hum directionally, GLaDOS narrates state. An agent that can't hear
  is blind to off-screen events — first light's step-12 cube bump would have been
  *audible* (the button's un-press sound fired).
- The Source engine has a **closed-caption system** (timed, localized labels keyed to
  sound events). Hooking the sound-emit / caption path gives **ground-truth symbolic
  audio events** ("turret_active fired at tick N, origin X") for near-zero engine cost —
  the audio analog of `EntitySnapshotter`, and a free enrichment for the data-flywheel's
  auto-annotation spans.
- Raw audio capture (PipeWire loopback per gamescope instance, an audio track or
  sound-event log in `.hdem`/`.rollout`) upgrades the entire data-product story:
  first-person video *with synchronized audio and ground-truth event labels* is the shape
  Gemini Omni training data actually takes. The user's seed vision names Gemini Omni
  explicitly; no lens picked it up. **Timing matters:** `.hdem` v2 is being specified
  right now by three lenses (inputs only) — if audio events aren't in the v2 sidecar, the
  human corpus is silently lossy for the Omni pitch forever, the exact "recordings
  without it are lossy forever" argument the council already accepted for CUserCmd.
- Science that exists nowhere else: audio-vision binding in embodied frozen models;
  chambers where the *answer is audible but not visible* (active perception's sibling
  axis). Gemini's API accepts audio today — a per-step audio clip is a frozen-track arm
  nobody proposed.

### 1.2 Real-time acting — every lens assumes the world waits

The pausable simulator is the platform's superpower and the council leaned in hard
(benchmark lens explicitly *sanctions* pause-while-thinking; model-frontier notes
"world-freeze makes deliberation free"). One parenthetical in model-frontier ("the shape
a real-time variant would need") is the entire coverage of the opposite arm. Missing:

- A **latency-bounded track**: per-step wall-clock budget, the game keeps ticking while
  the model thinks. This is the constraint Gemini Robotics actually lives under (robots
  can't pause the world) and the constraint SIMA was evaluated under. Without it, the
  robotics-transfer pitch has an asterisk a VP's TL will find in one question.
- **Think-while-acting concurrency**: macros take many ticks to execute; the session
  layer could let the model begin reasoning about step N+1 while macro N runs, with
  abort-on-surprise (locomotion's interruptible-macros idea is the missing half of this
  pairing — nobody connected them). The anytime version is a reflex/default policy
  holding the body while the planner deliberates — the literal two-system Gemini
  Robotics architecture, measurable here.
- Nuance worth recording: Demaine's NP-hardness gadgets are *timed-button chains*, and
  they still work under world-freeze because game-time advances only during macros — so
  "timed puzzles" and "wall-clock pressure" are two different arms; the council has
  neither explicitly.
- Cheap because the engine's native mode IS real-time — the harness *added* pause. The
  deliberation-time-vs-performance curve under wall-clock pressure is the natural
  complement to model-frontier's thinking-budget scaling curve, and no benchmark
  anywhere publishes both.

### 1.3 Death and irreversibility — no DIED terminal, and M3 adds the hazards

Eval-science's terminal taxonomy v2 is SOLVED/LOOP/STUCK/BUDGET/GAVE_UP/CRASH — **no
DIED, no IRRECOVERABLE**. Locomotion mentions goo only as a fall-safety check;
puzzle-gen lists human "deaths" as a difficulty signal with nowhere to put it. Yet the
roadmap's very next rung (M3) adds lasers, turrets, and goo:

- What happens when the agent dies? Auto-respawn (where? is that a teleport cheat?),
  does chamber state reset on respawn, is `DIED` terminal or a costed event,
  deaths-per-episode as a metric (human baselines have it natively)?
- **Irreversible non-death failure** is the nastier sibling: cube fizzled, cube in goo —
  the chamber is unsolvable, the agent is alive, and the episode limps to BUDGET,
  polluting every solve-curve and censoring-aware metric the council designed. Detecting
  unsolvability-by-irreversible-action is itself a percept/oracle design question
  (the snapshotter sees the cube despawn; nobody routes that to the terminal logic).
- This belongs in the terminal-taxonomy-v2 spec *before* it is implemented (it is on the
  immediate critical path per first_light §5e), not as a retrofit after the first turret
  chamber produces nonsense data.

### 1.4 Safety/alignment — one idea (GLaDOS prompt injection) where a whole axis fits

Moonshots #19 covers embodied prompt injection and even notes "safety teams are a second
audience." Nobody followed the thread:

- **Portal 2 is a specification-gaming laboratory with a human-codified rulebook.**
  Speedrun category rules (Inbounds, Glitchless, NoSLA) are a community-negotiated,
  decade-stable taxonomy of *what counts as cheating* — exactly what reward-hacking
  research lacks. The speedrun lens built the detector (OOB/anomaly telemetry) and the
  generator (glitch-mine novelty search) for *capabilities* reasons; reframed, the same
  two artifacts are a ground-truth reward-hacking benchmark: under-specify the reward and
  *measure* whether the agent exploits the physics, with engine-truth detection instead
  of human review. The glitch mine is dual-use and nobody said so.
- **Exploit adjudication policy** (the governance half): when a reasoning-track agent
  finds a glitch route, is that SOLVED, flagged, or a separate category? The
  route-novelty detector *detects*; no lens *rules*. Competition integrity needs the
  written rule before the first contested leaderboard row.
- **Constraint-following under temptation**: moonshots' "One Chamber, N Tasks" is one
  anchor-restore away from "the constrained route is slower — does the agent hold the
  constraint when nobody's watching?" — a clean embodied honesty eval.
- **Stakeholder**: GDM's AGI Safety & Alignment and dangerous-capability-evals teams are
  a *fifth budget line* missing from pitch-strategy's four-slot memo. Safety teams buy
  instruments, and the per-step attribution instrument is precisely their shape.

### 1.5 Interpretability / CoT faithfulness — the two ingredients exist, uncombined

Eval-science grades *outcomes* (verdict labels, judge labels); nobody asks whether the
stated reasoning *causes* the action. The repo uniquely has both ingredients for causal
faithfulness experiments:

- The **offline prompt-replay rig** (model-frontier): perturb the scratchpad/thought,
  hold the world constant, resample — does the action change when the stated reason is
  edited out?
- **Save/load anchors**: hold the thought, perturb the world — execute the counterfactual
  and observe whether the stated *expectation* (world-model probe, model-frontier #20)
  tracked reality.
- Free extras nobody claimed: **confabulation detection** (after BLOCKED, does the
  model's explanation match the engine-truth blocker that failure-surface-v2 will
  literally attach?) and **verbalized-belief calibration** (the model constantly asserts
  world state — "door 8 is open" — that the snapshotter can grade per step;
  done-calibration covers only the `done` verb).

Embodied CoT-faithfulness is a publishable result with near-zero new infrastructure, and
GDM's interpretability org is another unnamed internal customer. It also hardens the
headline: "reasoning is solved" currently means "the transcript *reads* coherently" —
faithfulness testing is what converts narrative coherence into a causal claim.

### 1.6 The Waymo angle — user-seeded, effectively unclaimed

The user's vision names Waymo; the council's total coverage is one clause in moonshots
("makes the Waymo/Robotics slide load-bearing"). The deep version isn't "game
trajectories train driving models" (weak) — it's that **the harness has independently
converged on the shape of an AV simulation stack**, the most expensive part of an AV
program: log replay (`.hdem` → re-sim), closed-loop resimulation with divergence
tolerance (the determinism envelope), counterfactual branching from a logged state
(anchors + branch data = "what-if resim"), scenario mining from event streams
(auto-annotation), behavior-prediction eval (route-novelty clustering, ghost deltas),
and long-tail scenario *generation* (mutation operators). A two-page memo mapping each
harness primitive to its Waymo-sim counterpart is a different — and for some audiences
stronger — internal-synergy pitch: "one RE rebuilt the resim stack on a $10 game; here
is what each piece costs at AV scale."

### 1.7 The human funnel: education, contributors, accessibility (people, not artifacts)

The council built adoption *artifacts* (starter kit, container, pip client, landing
page, stream) and a year-3 stewardship doc, but no *people pipeline*. "Course/student/
university" hits: zero.

- **The course kit (Berkeley-Pacman / Malmo-in-education model):** a macro-altitude
  agent assignment — students implement `next_action(obs)` in Python; the offline
  prompt-replay rig makes it gradeable with zero game installs and zero GPU. Berkeley's
  Pacman taught a generation of AI researchers and seeded a citation flywheel; this is
  the cheapest durable-adoption engine available. Valve's own "Teach with Portals" /
  Steam-for-Schools (2012) is precedent that Portal 2 is *education-sanctioned* — a
  goodwill sweetener for the Valve packet and a broader-impacts paragraph for any
  grant-shaped funder.
- **Contributor funnel before the pitch:** risk register says bus-factor=1 "IS the ask"
  — but nothing fills months 0–12: good-first-issues, a GSoC/MATS-style scholar, p2sr
  devs as bounty-paid plugin contributors. The pitch is strictly stronger if headcount
  *accelerates* an existing contributor base rather than *creating* one.
- **Accessibility (small, real, uncovered):** the macro grammar is accidentally a
  voice-control layer for motor-impaired players ("go to the door, pick up the cube") —
  a goodwill demo, a DevRel story, and a second human population for the baseline corpus.
- **Spectator accessibility of the *reasoning***: the Twitch/sizzle ideas show the agent
  playing; nobody proposed publishing the *trajectory viewer* itself as the artifact
  reviewers and students cite — "read 25 annotated agent-decisions in your browser" is
  the DevRel front door, and it already exists.

### 1.8 Minor axes worth one line each

- **Physics-parameter OOD arm**: puzzle-gen covers *cosmetic* augmentation (reskins);
  *physics* augmentation — `sv_gravity`, timescale, funnel-speed variants over the same
  logical chamber — is a world-model-robustness axis that is pure cvar work.
- **Interactive teachability**: model-frontier's injected hints are *system*-generated
  loop feedback. "Human gives one hint mid-run; measure solve-rate lift per hint" is the
  SIMA-2 "learns with you" eval, distinct and uncovered.
- **Human-human co-op dialogue corpus**: moonshots covers agent-agent and human-agent
  co-op; *human pairs* playing co-op with voice/chat recorded into `.hdem` is a grounded
  collaborative-dialogue dataset (referring expressions under asymmetric viewpoints)
  that the language-grounding community has no 3D source for.
- **Annotation-correctness QA**: eval-science perturbs marks and ablates annotations but
  never validates the annotator itself — golden frames with IoU checks that box N
  actually covers entity N. A mis-drawn box silently charges perception errors to the
  model.
- **Competitive-landscape scan**: "Why Portal 2" compares *environments*; nobody
  assigned the cheap due-diligence scan of competing *projects* (SIMA's commercial-game
  evals, Voyager/Pokemon scaffolds, any existing Portal-2-LLM hobby work) that a VP's
  staff will run before the meeting.

---

## 2. Stakeholders nobody named

| Who | Why they matter | Cost to engage |
|---|---|---|
| **Erik Demaine's group (MIT)** | Every lens cites the PSPACE paper as methodological cover; *nobody proposes emailing the authors.* A hardness-theory co-author on the benchmark paper is instant theory credibility, and the gadget-graph compiler (puzzle-gen) is literally their construction made executable. This group publishes on games-and-hardness for fun. | One email |
| **GDM Safety/Interp orgs** | Fifth and sixth budget lines (see §1.4, §1.5). | Folded into pre-read memo |
| **Gemini Omni / audio org** | Customer for §1.1; nobody can pitch audio percepts that don't exist. | Gated on audio spike |
| **TeamSpen210 / BEE2 maintainers** | Puzzle-gen leans on srctools/BEE2 *as code* but never *as people*. The headless `.p2c→.bsp` compile is flagged as the lens's single load-bearing unknown — it is these maintainers' daily bread; one conversation may collapse an [M] unknown to an afternoon. | One Discord message |
| **Course instructors / TAs** | The education flywheel (§1.7) needs one pilot course, not a platform. | One email after the kit exists |
| **Workshop map authors as a constituency** | Governance covers *player* consent and *Valve* licensing; map authors' credit/opt-out for benchmark inclusion is unaddressed (download-on-demand mitigates redistribution, not attribution). P2-Bench-100 ships 100 people's creative work. | A credit-and-opt-out policy paragraph |
| **Plan-B funders** | The entire pitch lens has exactly one customer: a GDM VP. No alternative path is even sketched — other labs, an academic consortium, compute grants, or the "no funder, community-sustained" floor. Single-customer risk in a pitch *about* risk registers. | One section in the pitch doc |

---

## 3. Governance/legal/ops landmines beyond the Valve conversation

The council treats "legal" as "Valve" and "ops" as "the fleet." Five other landmines,
all grep-confirmed zeros:

1. **Frontier-model ToS on distillation.** Model-frontier's data factory + distill-to-4B
   and data-flywheel's agent-trajectory distillation train open models on Gemini
   outputs; Gemini's API terms restrict using outputs to develop competing models (and
   Claude/GPT terms say similar things about the cross-model corpus). Fine inside a
   Google partnership; a landmine for the *open dataset release* and the *competition
   data track*. One-day legal read, must happen **before** any public artifact contains
   frontier-model outputs.
2. **Windows build of the recorder — the data flywheel's silent prerequisite.** The
   "SAR-is-on-every-leaderboard" coup (moonshots #5, data-flywheel #6, pitch annex)
   assumes the fork's `.hdem` recorder runs where runners actually play: **Windows**.
   This fork is developed and built Linux-only (32-bit gRPC runtime at
   `/opt/p2-grpc32`); whether HdemRecorder + MarkTable compile into `sar.dll` without
   the gRPC stack is unscoped. One lens says "record on a 4K Windows rig" in passing;
   nobody scoped making that true.
3. **PII in community demos.** `.dem` files carry player names/SteamIDs; the community
   corpus and leaderboard-mining ideas (thousands of files) need a scrubbing step and a
   stated policy — and the speedrun community includes minors, which touches consent
   design for the "play for science" drive.
4. **Model deprecation = leaderboard rot.** `gemini-3.5-flash` will be retired; every
   leaderboard row keyed to a closed model becomes unreproducible. No lens addressed
   eval-rot from model churn (the golden-decision suite handles *prompt* churn only).
   Fix is cheap and standard: a **pinned open-weights reference agent** as the permanent
   reproducible baseline row, plus a stated re-run protocol on model retirement. Also:
   the exact model snapshot/version string of the first-light run should be archived
   *this week* before it silently changes underneath the API alias.
5. **Steam licensing for fleets.** 32–128 instances per box, cloud fleets, hosted eval:
   how many Steam accounts/licenses is that, and what does the subscriber agreement say
   about headless concurrent instances? Belongs in the Valve packet, currently absent.

---

## 4. Next-week actions everyone assumed, nobody assigned

1. **Re-run first light, n≥5 seeds, 2–3 trivially-varied chambers.** Every lens builds
   on n=1. The cheapest possible hardening of the headline result is one day of API
   spend with zero new code. Eval-science specifies the stats *framework*; nobody
   scheduled the *run*.
2. **Spike the `chamber_complete` hook.** It appears as the keystone [S] in at least
   four lenses — all of them *assume* the PeTI level-end path is hookable. Nobody
   assigned the half-day recon of *which* engine event fires on owned `.bsp`s vs
   workshop maps. If it's ugly, every downstream plan changes; find out now.
3. **Add `DIED`/`IRRECOVERABLE` to the terminal-taxonomy-v2 spec** before it is
   implemented (§1.3) — it is on the immediate critical path.
4. **Audio first-light spike:** `pw-record` the gamescope sink of one instance for 10
   seconds; attach a clip to one Gemini step and ask "what did you hear?" (§1.1). One
   day.
5. **Windows build check:** does the MSVC solution even include the Harness recorder
   sources? (§3.2). One afternoon.
6. **Read the Gemini/Claude/GPT ToS** (§3.1) before the data factory produces anything
   public.
7. **Send the Demaine email and the BEE2 Discord message** (§2) — both are
   minutes-cheap and weeks-long in calendar time, same logic as the Valve packet.
8. **Archive the first-light model snapshot metadata** (§3.4).

---

## 5. Cross-lens combinations left on the table

- **Glitch mine × safety framing** → the spec-gaming lab (§1.4). Same artifact, second
  funder.
- **Interruptible macros (locomotion) × real-time track (§1.2)** → think-while-acting
  with abort-on-surprise; neither lens knew it was building half of the other's feature.
- **Prompt-replay rig (model-frontier) × anchors (harness)** → causal CoT faithfulness
  (§1.5).
- **Teleport control arm (eval-science) × data factory (model-frontier)** → the cheat
  arm produces *reasoning-only* trajectories with zero locomotion noise — the cleanest
  SFT corpus the data lens never claimed.
- **Auto-annotation spans (data-flywheel) × engine caption/sound hooks (§1.1)** →
  audio-enriched SIMA spans; the annotation engine gets a second ground-truth event
  stream for free.
- **CM category rules (speedrun) × constraint predicate DSL (moonshots #8)** → speedrun
  categories are machine-checkable constraint tasks; one implementation serves both.
- **Counterfactual branch data (data-flywheel) × Waymo resim memo (§1.6)** → the branch
  dataset *is* what-if resimulation; naming it in AV vocabulary is the pitch.
- **Teleport control arm (eval-science) × LocoGym (locomotion)**: the same
  reasoning-vs-actuation A/B measured at two layers — should be designed as one
  attribution experiment, not two artifacts that will drift.

---

## 6. What I checked and did NOT find missing (for the record)

Human baselines (covered ≥4×), contamination (≥3×), cost accounting (≥3×), memory/
context (≥3×), egocentric observability (2×), co-op (deep in moonshots), Valve (3×),
stewardship/succession (2×), reproducible packaging (≥3×), determinism honesty (≥3×),
crash recovery (2×), prompt caching (1×, thorough). The council does not need more ideas
on those axes; it needs the axes above and the eight assigned actions in §4.
