# Gap lens: Demand-side discovery — who is the second user?

*Gap-filler pass, 2026-06-12. The council produced ~230 ideas and every one of them is
supply-side: build the artifact, then pitch it. Not one idea is "talk to a potential user
before building." This lens makes demand discovery a first-class workstream with the same
standards as the rest: concrete, named, effort-rated, with pre-registered kill criteria.*

**Effort scale:** S = days, M = weeks, L = months, for one RE + agentic coding. Demand work
is unusual: RE-hours are small but **calendar time is real** (people take 1–3 weeks to reply).
Where they diverge, both are stated.

---

## The core argument

The VP meeting is not demand; it is a **financing event**. A VP funding "general agents via
Portal 2" is making a seed investment, and seed investors fund *traction*, not roadmaps. The
single strongest slide the pitch could contain is not the knowing-doing-gap scalar or the
sizzle video — it is: **"Three named external teams already ran their models/loaders on this
instrument, unpaid. Here is what they said. Here is what they asked for next."** No council
idea produces that slide. Everything in this lens exists to produce it — or to discover,
for roughly $0 of code, that it cannot be produced, *before* six months get spent on p2bench,
the hosted ladder, and the RLDS exporter for users who were never going to come.

The risk this lens retires is the classic solo-builder failure: 100% of time on supply
because code is comfortable and rejection is not. First light proves the instrument works.
The second user proves the instrument *matters*. They are different milestones, and only
one of them is on the ROADMAP today.

---

## The segment map (reference for all ideas below)

| # | Segment | Job-to-be-done | Minimum viable artifact (MVA) | Likely disqualifier to listen for |
|---|---|---|---|---|
| 1 | Frontier agent-eval teams (GDM, Anthropic, OpenAI, Meta evals) | "Unsaturated, legible, cheat-resistant agentic eval for the next model card / internal bar" | Hosted endpoint or container they never have to install Steam into; budget-capped, replayable | "Legal/infra won't let us run a Steam game"; "we only adopt suites with 100+ tasks" |
| 2 | Academic embodied-agent / LLM-agent labs (UCL DARK, CMU, AI2, NVIDIA GEAR…) | "A citable, novel env for the next paper; baselines that run on a student's machine" | pip-install p2bench + starter kit + 20 chambers + a free-tier model baseline | "No GPU/Steam license budget"; "reviewers want established benchmarks" |
| 3 | Classical planning / search community (ICAPS, IPC organizers) | "New grounded domains with real-world teeth for the competition / for papers" | **Engine-free** routing-as-planning export (PDDL/JSON) + a verifier endpoint | "If it needs a live game, it's dead to us" |
| 4 | World-model / video-prediction teams (Genie line, Dreamer/Danijar, 1X, Wayve) | "Ground-truth physical-consistency eval our rubric scorers can't give us" | Wormhole eval: 1k portal-transit clips + per-tick pose/velocity ground truth, one tfrecord | "Video benchmarks are dead internally"; "non-Euclidean is a curiosity, not a need" |
| 5 | Robotics-data buyers / VLA trainers (Gemini Robotics, LeRobot/HF, Physical Intelligence, academic VLA labs) | "Cheap, long-horizon, action-labeled first-person data that passes legal screening" | 1-hour RLDS/LeRobot shard + datasheet + consent/license provenance doc | "Game data never transfers"; "no consent chain, no ingestion — full stop" |
| 6 | Game-AI / competition researchers (ViZDoom, MineRL/BASALT, NetHack LE alumni, Kaggle Game Arena) | "A competition that doesn't bankrupt the organizers and doesn't saturate in year one" | Agent-protocol spec + starter kit + the n=20 suite + budget rules | "Competitions cost us a year and got 12 entrants; never again" (listen for *why*) |
| 7 | Speedrun + mapmaker community (p2sr, BEEmod/PeTI authors) — **segment zero** | Runners: better routing/TAS tools. Authors: playtest analytics for their chambers | SAR cvars/HUD they'd actually use; agent-playtest heatmaps for one author's map | "Don't touch our leaderboard integrity"; apathy (tools offered, nobody installs) |
| 8 | Eval-methodology orgs (METR-like, Epoch, scaling-eval academics) | "Instruments with per-step failure attribution, not just pass rates" | The .trajectory format + viewer + verdict-labeling spec, offline, no game needed | "We're text-only for the foreseeable future" |

Segments 1, 4, 5 are *pitch-load-bearing* (they map to the four budget lines in
pitch-strategy's slot memo). Segments 2, 3, 6 are *adoption-load-bearing* (citations,
longevity). Segment 7 is the only one with **existing pull** today.

---

## Ideas

### 1. The demand spec: segment map + JTBD canvas as a repo doc — [S]
**What:** Promote the table above into `brainstorm/demand/segments.md`: for each of the 8
segments, one page — job-to-be-done, the MVA, 3 named candidate humans, the network path to
each, the disqualifying answer, and the pre-commitment that would count as a win. Treat it
with ROADMAP discipline: it is the source of truth the interview sprint executes against,
updated after every conversation.
**Why:** Right now "who is this for?" has eight implicit answers smeared across ten council
docs. Until the segments are written down with names attached, demand work can't be
scheduled, delegated to agentic search (finding emails, papers, Discord handles), or
falsified.
**Effort:** S — 2–3 days, much of it agentic (literature/author/contact research).
**Unlocks:** Every other idea in this lens; the pre-read targeting in pitch-strategy;
honest scoping of p2bench/ladder/RLDS before they're built.

### 2. The MVA matrix + the build-on-pull rule — [S]
**What:** A one-page decision rule adopted into ROADMAP: *no segment-facing artifact gets
built past spike quality until at least one named person in that segment has asked for it in
an interview.* The matrix maps each planned artifact (p2bench, starter kit, hosted ladder,
RLDS shard, wormhole eval, routing export, playtest analytics) to its segment, its current
pull evidence (today: zero for all), and the smallest spike that could be shown in a call
(often: the existing viewer + a mock README, not working code).
**Why:** The council queued L-effort artifacts (hosted ladder, workshop pipeline, RLDS at
scale) on zero demand evidence. One RE cannot afford a single mis-built L. The matrix makes
"we built it because lens X liked it" impossible.
**Effort:** S — one day to write; ongoing discipline.
**Unlocks:** Kills or defers up to 2–3 council L-items; converts the backlog from
taste-ranked to demand-ranked.

### 3. Mom-Test interview kit: script, disqualifiers, ledger — [S]
**What:** A reusable interview protocol in `brainstorm/demand/interview_kit.md`. Rules: never
pitch first; ask about *past behavior and money*, not opinions about your demo. Core
questions: "Walk me through the last time your team adopted a new env/benchmark — who
decided, how long, what broke?" / "What did you evaluate and *reject* recently, and why?" /
"What does one eval sweep cost you today (GPU-hours, $, person-days)?" / "What was the last
external dataset you ingested, and what killed the ones you didn't?" / commitment close: "If
I gave you X in 30 days, what would have to be true for you to actually run it?" / referral
close: "Who else should I be talking to?" Plus a disqualifier list ("looks cool, keep me
posted" = polite no) and a notes template that feeds the ledger (idea 5).
**Why:** Without a script, researcher interviews collapse into demos and compliments —
maximally pleasant, zero information. The kit is what makes 10 interviews comparable and
the kill table (idea 6) executable.
**Effort:** S — 1–2 days.
**Unlocks:** Ideas 4, 6, 9–13; protects the sprint from producing feel-good noise.

### 4. The 10-interview discovery sprint: named targets, named paths — [S effort, M calendar]
**What:** Run ≥10 interviews across segments 1–8 in the 4–8 weeks *before* any VP meeting.
Concrete starting roster (refine in idea 1; ~30 min agentic work finds emails/handles for all):
- **Seg 2/6:** BALROG authors (Davide Paglieri, Tim Rocktäschel — UCL DARK); NetHack
  LE/MiniHack alumni (Eric Hambro, Mikayel Samvelyan); BASALT organizers (Stephanie Milani,
  CMU; Anssi Kanervisto, MSR); ViZDoom competition organizers (Marek Wydmuch); MineDojo/
  Voyager (Guanzhi Wang, Jim Fan — NVIDIA GEAR).
- **Seg 1:** one agent-evals IC at GDM (via the pre-read TL, idea 12), one at a second lab
  via NeurIPS/Twitter.
- **Seg 3:** IPC/ICAPS organizers (Malte Helmert, Jendrik Seipp orbit) — offer a domain,
  ask what a submission needs.
- **Seg 4:** Danijar Hafner (Crafter author *and* world models — double coverage, and at
  GDM); 1X world-model-challenge organizers (Eric Jang orbit).
- **Seg 5:** LeRobot (Rémi Cadène, HF); one Open X-Embodiment contributor; one academic VLA
  lab.
- **Seg 7:** 2–3 p2sr maintainers/TASers (warmest path: this repo forks their tool); 1–2
  prolific PeTI/BEEmod authors via their Discords.
- **Seg 8:** Farama maintainers (Mark Towers / Jordan Terry) — they've watched every env
  project live and die; 30 minutes with them is a decade of adoption post-mortems.
Channels, in order of conversion: warm intro > p2sr/BEEmod/Farama Discords > cold email
with the 90-second video + viewer permalink > X/Twitter DM. The first-light viewer link is
the door-opener; it costs nothing and no other env project has "read the model's mind" as
a cold-email attachment.
**Why:** This is the lens's centerpiece. 10 conversations re-rank the entire council
backlog and produce the demand-evidence slide (idea 15) — or the honest discovery that
there is no second user yet, which changes the pitch from "fund the platform" to "fund the
search for the platform's user."
**Effort:** S in RE-hours (~1.5h per interview incl. prep/notes); M in calendar. Start
outreach *this week* — latency dominates.
**Unlocks:** Ideas 5, 6, 8, 15; the pre-read content; the entire re-scoped backlog.

### 5. Pre-commitment ladder + the demand ledger — [S]
**What:** Define commitment as a graded currency and track it in one table
(`brainstorm/demand/ledger.md`): **L0** replied · **L1** took a 30-min call · **L2** ran an
artifact on *their* machine (starter kit boots, shard loads in their pipeline) · **L3**
spent *their* compute/model budget on it (their model through the eval; their loader over
the RLDS shard; a planning system on the routing export) · **L4** public attachment
(letter/LOI for the NeurIPS proposal, co-organizer, co-author, named design partner) ·
**L5** resources (compute credits, prize sponsorship, contractor hours). The project's
demand metric is **count of L3+**, reviewed monthly next to the supply milestones in
ROADMAP.
**Why:** "What counts as a pre-commitment" was asked nowhere in 230 ideas. Without the
ladder, "Jim Fan said it's cool on Twitter" inflates into traction. With it, the VP slide
can say "2× L3, 1× L4" and mean something auditable.
**Effort:** S — half a day; minutes per week thereafter.
**Unlocks:** Idea 15; honest go/no-go on the VP meeting timing; design-partner selection.

### 6. Pre-registered kill/accelerate table for planned artifacts — [S]
**What:** *Before* the interviews, write the decision table binding plausible findings to
backlog changes — same pre-registration discipline eval-science demands for stats. Seed
rows:
- ≥3 of 5 frontier/academic eval interviewees cite **install friction or legal** as the
  adoption killer → **accelerate** container + hosted-eval-lite; **demote** pip-p2bench
  from launch artifact to nice-to-have.
- ≥3 academics say "we only adopt 100+ task suites with baselines" → **accelerate**
  generator + probe-difficulty ladder; **re-scope** hand-authored 20-chamber suite to
  internal calibration only.
- ≥2 world-model interviewees say they'd run the wormhole eval next quarter → **accelerate**
  it from S-M to *now*; if both shrug → **kill** it as a pitch pillar (keep as a weekend
  artifact).
- Robotics-data interviews all open with license/consent screening → **freeze** RLDS-at-scale;
  **promote** dataset-governance + Valve packet to critical path (the exporter is worthless
  if the shard fails legal screening at every buyer).
- Zero L3 pre-commitments after 10 interviews → **postpone the VP meeting**; the pitch
  becomes idea 8's design-partner search, not a headcount ask.
- p2sr interviews show real appetite for routing/TAS tooling → **accelerate**
  routing-copilot + TAS-track co-organization (the moat thesis); apathy → speedrun axis
  stays research-only, drop "community workforce" from the pitch.
**Why:** Deciding *after* hearing feedback invites motivated reasoning ("they didn't mean
it like that"). Pre-registration converts interviews from vibes into a branch instruction.
**Effort:** S — one evening.
**Unlocks:** A backlog that updates mechanically; pitch credibility ("we pre-registered our
demand experiments" is a sentence no other env pitch can say).

### 7. The over-the-shoulder test: watch an outsider reproduce first light — [S]
**What:** Before any public kit ships, hand the starter kit + a chamber to 1–3 friendly
outsiders (a former colleague, a p2sr member, a Discord volunteer — anyone but you) and
*watch them install and run it on a screen-share, saying nothing*. Record time-to-first-frame,
every point of confusion, every README lie. Repeat after fixes.
**Why:** Malmo died of install friction; Crafter won on zero-friction — both lessons appear
in council docs, but no council idea actually *tests* friction with a human who isn't the
author. This is the cheapest usability instrument that exists, and the first run will be
humbling (32-bit multilib, gamescope, Steam licensing, /opt/p2-grpc32 — each a candidate
wall).
**Effort:** S — half a day per session plus fixes; needs the kit at spike quality first.
**Unlocks:** A starter kit that survives contact; a measured "time-to-first-light: 40 min"
number for the pitch; often recruits the outsider as user #2.

### 8. Design-partner program: three named slots, not a launch — [M calendar, S-M effort]
**What:** Instead of launching p2bench to the world, recruit **2–3 design partners** — target
mix: one academic lab (seg 2), one world-model or eval team (seg 1/4), one community partner
(seg 7). The deal, written in one page: they get white-glove support, influence over the verb
grammar/suite/format, and co-authorship on paper #1 or the competition proposal; you get
their friction reports as the backlog, their model/loader runs as L3 evidence, and their
names on the demand slide. Cap at 3 — more is a support burden one RE can't carry.
**Kill criterion (pre-registered):** if 8 weeks of trying can't fill 2 slots, the demand
hypothesis fails at current artifact strength — that finding goes in the pitch honestly.
**Why:** The second user you *support into existence* arrives months before the hundredth
user a polished public launch might attract. Co-authorship is the one currency a solo
unfunded RE has in abundance, and academics convert on it.
**Effort:** S-M spread over weeks (support is bursty); the slots are filled *from* the
interview sprint, not cold.
**Unlocks:** L3/L4 ledger entries; external bug discovery; the NeurIPS proposal's
co-organizer list (council's benchmark lens needs 3–5 — this is where they come from).

### 9. Planning-community probe: pre-sell the routing export at ICAPS — [S to probe, M to deliver]
**What:** The routing-as-planning export (speedrun-routing lens) is the *easiest L3 in the
whole portfolio*: the planning community chronically hungers for grounded domains, ships
its own solvers, and needs **no game, no GPU, no Steam** — an engine-free instance format
plus a verifier endpoint. Sequence: (1) two-page domain proposal — Portal 2 routing with
Demaine's PSPACE teeth, instance format sketch, verifier semantics; (2) email IPC/ICAPS
organizers and 2–3 planning groups asking *what a domain needs to be adoptable* (Mom Test,
not pitch); (3) build the export **only if** someone says they'd run a solver on it.
**Why:** A planning group adopting the export is exactly the assignment's example
pre-commitment, reachable in weeks because their MVA is the cheapest. One accepted IPC
domain = permanent academic citation pump from a community no other game-env project
courts.
**Effort:** S for proposal + outreach; M for the export if pulled.
**Unlocks:** First L3 on the ledger; "the classical planning community attacks Portal 2
routing" as a pitch line; the route-graph work gets a customer before it gets built.

### 10. World-model probe: pre-sell the wormhole eval before cutting a single clip — [S]
**What:** Write the wormhole eval as a **2-page spec + 5 hand-made teaser clips** (one
portal transit, ground-truth pose overlay, "what should frame t+1 look like?") — not the
1k-clip dataset. Put it in front of 2–3 world-model people (Danijar; Genie-orbit via
pre-read; 1X challenge organizers) with one question: "would your team run this next
quarter, and what would the report need to contain?" Build the full set only on a yes.
**Why:** The wormhole eval is the sharpest *differentiated* hook into the world-model org
(both moonshots and data-flywheel rank it highly) — but it's also fully speculative demand.
Five clips and a spec test the hypothesis for 2% of the build cost, and the teaser clips
double as pitch-deck material either way.
**Effort:** S — clips come from existing rollout/render tooling.
**Unlocks:** Either an accelerated, pre-sold benchmark with a named first user, or a clean
kill that saves weeks; calibrates how the Genie-line actually evaluates physics today
(intel the pitch needs anyway).

### 11. Data-buyer discovery: does anyone actually ingest game trajectories? — [S-M]
**What:** Before hardening exporters, interview 3–5 people who *consume* embodied data:
LeRobot/HF, an Open X-Embodiment contributor, one academic VLA lab, and (via pre-read
path) anyone adjacent to Gemini Robotics data ingestion. Questions: what was the last
third-party dataset you ingested; what format reality (RLDS? LeRobot? parquet?); what
fraction of candidate datasets die at *legal/consent* screening vs *quality* screening;
has game data ever moved a robot metric for you; what would a Portal 2 shard's datasheet
have to claim for you to even open it?
**Why:** "Trajectories for Gemini Robotics/Waymo" is the user's pitch axis with the
**least demand evidence and the highest wishful-thinking risk** — sim-to-real skeptics are
the default audience. If the answer is "game data never passes screening," better to learn
it in week 3 than in the VP room; if there's a real screening rubric, the datasheet gets
written against it.
**Effort:** S-M — interviews are S; a rubric-conformant datasheet +1-hour pilot shard is M.
**Unlocks:** Go/no-go on the entire data-product pillar; re-scopes .hdem-v2 urgency
(it stays load-bearing for *internal* BC regardless — that motivation is unaffected);
promotes or demotes the governance/Valve work.

### 12. Inside-DeepMind discovery: the pre-read as a two-way instrument — [S]
**What:** Pitch-strategy's pre-read memo treats TLs as a validation gate. Reframe it as
*discovery*: send the 2-page SIMA-gap memo to 3–5 ICs/TLs across SIMA, Genie, Gemini
Robotics evals, and Game Arena orbits with three explicit questions: (1) what would make
your team run a model on this within a quarter? (2) which of these four artifacts
(hosted eval / wormhole set / RLDS shard / failure-attribution instrument) is closest to
something you'd actually use? (3) who is the right second reader? Their answers are
collected into the demand ledger like any interview.
**Why:** The VP's first move after any pitch is to ask their own teams. If the teams have
*already* told you what they'd use — and the deck opens with "we asked your ICs; here's
the ranked list" — the diligence loop is pre-run in your favor. This converts the warmest
network path the project has into structured demand data instead of a one-way teaser.
**Effort:** S — the memo exists in pitch-strategy's plan; add the questions and the
follow-up discipline.
**Unlocks:** Pitch-slot ranking grounded in insider statements; named internal champions;
possibly the design partner from segment 1.

### 13. Segment zero first: p2sr runners and PeTI authors as live users — [S]
**What:** The only segment with existing pull gets the first interviews (they're also the
lowest-stakes practice rounds for the kit in idea 3). Runners/TASers: which of
{routing copilot, fling solver HUD, ghost tools, anchors-for-practice} would you install
this month; what would make you opt into .hdem recording; would a TAS-track co-organization
interest p2sr as an org? Mapmakers (BEEmod Discord): would agent-playtest heatmaps and
completion funnels change how you iterate on a chamber; would you trade a "may be used in
the benchmark" license flag for free playtests?
**Why:** Three council lenses (data-flywheel, speedrun-routing, benchmark) assume community
participation as a *given input* — recording drives, demo corpora, co-organizers. Nobody
scheduled the conversation that tests whether the community wants any of it. Community
goodwill is also the most destructible asset in the portfolio: shipping a scraper or an
opt-out recorder without asking first could poison segment zero permanently.
**Effort:** S — Discords are open; this fork's lineage (a SAR fork by an active user) is
the warm intro.
**Unlocks:** Tools-for-trajectories deal terms grounded in stated preferences; the TAS-track
feasibility answer; validated assumptions under ~6 council ideas before any of them get built.

### 14. The pull sensor: landing page + access form, instrumented — [S]
**What:** Ship the passive demand instrument: a one-page site (name TBD per pitch-strategy)
with the 90-second video, a viewer permalink to the first-light trajectory, a 3-sentence
"what this is," and a **request-access form** that asks one question: "what would you do
with it?" Post the first-light thread once (X + r/MachineLearning + p2sr Discord) and then
*measure*: unique visitors → form fills → form fills from people you've never heard of.
**Why:** Interviews measure solicited demand; the form measures **unsolicited pull** — the
only demand signal that arrives while you sleep, and the difference between "I convinced 10
people to care" and "strangers are asking." Council's landing-page idea exists for pitch
optics; this version exists as a sensor, and the form answers become interview leads.
**Effort:** S — static page + a form backend; the video is already a council item.
**Unlocks:** An inbound-leads queue; a "N unsolicited access requests" number for the
demand slide; early warning if the framing attracts the wrong segment entirely.

### 15. The demand-evidence slide: quotes and commitments replace claims — [S]
**What:** The integration point: one pitch slide and one repo doc generated from the ledger.
Format: segment → named team → ladder level → verbatim quote → what they asked for next.
Plus the honest negatives: "segment 5 (robotics data): 3 interviews, all blocked on consent
provenance — governance work scheduled, pillar demoted." The backlog section of ROADMAP
gains a `pull:` annotation per artifact, sourced only from the ledger.
**Why:** Every council pitch idea argues from artifact quality; this slide argues from
*other people's revealed behavior*, which is the only argument a skeptical VP can't attribute
to founder enthusiasm. Including the pre-registered negatives is what makes the positives
believable — and "we talked to users and killed two workstreams" signals exactly the
research taste a headcount ask is meant to demonstrate.
**Effort:** S — assembly, once ideas 4–13 produce content.
**Unlocks:** The strongest single slide in the deck; the demand-ranked roadmap that the
five-workstream headcount ask (pitch-strategy) inherits its ordering from.

---

## How this interacts with the council backlog (summary)

- **Accelerated if interviews go as predicted:** container/hosted-eval-lite (seg 1's install
  wall), routing-as-planning export (seg 3 pull is cheap), wormhole teaser (seg 4),
  dataset governance + Valve packet (seg 5's screening reality), p2sr tools-for-trajectories
  (seg 7).
- **At risk of demotion:** pip-p2bench as *launch* artifact (if seg 1 can't install and
  seg 2 needs task count first), hosted ladder at L scale (needs ≥2 L3s to justify),
  RLDS-at-scale (gated on idea 11), the 20-chamber hand-authored suite as a *public* artifact
  (internal calibration value is unaffected).
- **Unconditionally safe (internal customers exist regardless):** exit oracle, .hdem v2
  actions, locomotion fixes, trajectory tooling — demand discovery changes none of these,
  which is itself a useful sanity check: the critical path to M3 survives every interview
  outcome.

## Spiciest take

The council wrote ~230 ideas and the phrase "we asked a potential user" appears in zero of
them — which means the project is currently a supply-side monoculture betting six months of
one RE's life on "build it and they will come," the exact failure mode that killed Universe
and Malmo *after* they shipped. The VP meeting everyone is optimizing for is not demand; it
is a financing event, and the strongest artifact it can contain is not the knowing-doing-gap
scalar but two named external teams at L3 on a pre-commitment ladder. Run the 10 interviews
first: the most likely single finding — frontier labs *cannot* install Steam, for legal and
infra reasons no container fixes — inverts the council's pip-first consensus overnight and
makes hosted-eval-lite the launch artifact. And if 10 interviews yield zero L3s, the correct
move is to postpone the VP meeting, because the pitch would be a seed round with no letters
of intent — and discovering that costs days, not the months the supply-side backlog is about
to spend finding it out the hard way.
