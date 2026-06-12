# Gap lens: Competitive teardown & the internal-rebuild threat

*Gap-filler pass, 2026-06-12. The council asserted moats in ten spiciest-takes; nobody
checked them against the 2026 landscape or priced what a fast follower pays. This lens
does the teardown honestly — and derives build/publish priorities from true defensibility,
not asserted defensibility.*

**One fact that frames everything: this repo is MIT-licensed** (inherited from upstream SAR,
`LICENSE`, (c) 2021 The SAR authors). The day the harness is public, DeepMind, OpenAI, or a
grad student can fork *all of it* — the protocol, the macro executor, the annotation layer,
the war stories in `brainstorm/`. Any moat analysis that doesn't start from "the code is
free to everyone, including competitors" is fantasy.

---

## A. The landscape map (claims vs. 2026 reality)

Claimed unique capabilities (aggregated from the ten lenses) vs. who already has them or
how fast they could. Knowledge as of early 2026; idea #1 below operationalizes keeping
this current.

| Claimed capability | Closest existing systems | Honest status |
|---|---|---|
| "Frozen VLM plays a commercial game via screenshots" | **Cradle** (GPT-4V on RDR2/Stardew/Cities:Skylines, pure screen+kbd/mouse, 2024), **VideoGameBench** (VLMs on GB/DOS games, 2025), **lmgame-bench**, Claude/Gemini-Plays-Pokémon scaffolds | **Commodity.** Not a claim; a baseline. |
| "Agentic LLM game eval with knowing-doing gap, vision ablations" | **BALROG** (NLE, MiniHack, Crafter, Baba Is AI, TextWorld, MineRL), TextQuests | Occupied. The *metrics vocabulary* is theirs; we can only add a sharper env, not a new genre. |
| "Set-of-marks + entity ground truth + macro verbs in a 3D game" | **Minecraft/Mineflayer**: entity list, inventory, and a *pathfinding* `goto` macro exist today (mineflayer-pathfinder — Voyager's action space was literally code over these APIs). SoM overlay over entity AABBs ≈ 1–2 weeks of work | **Replicable in Minecraft in ~2–4 weeks.** The percept/act scaffold is not a moat. |
| "Verifiable long-horizon agent eval in a commercial game w/ first-party-ish API" | **Factorio Learning Environment** (2025): deterministic, real mod API, programmatic verification, UGC blueprints | Occupied for *logistics/planning*. Open for *embodied 3D physics*. |
| "Interactive reasoning benchmark with novelty by construction" | **ARC-AGI-3** (interactive grid games, agent API, 2026) | Competes directly for the "interactive reasoning eval" funding slot — without embodiment. The Demaine ladder must lean on *physics*, or ARC eats the story. |
| "Procedural 3D environments with ground truth at scale" | **ProcTHOR** (10k+ generated houses), Habitat 3.0, Holodeck (LLM-generated scenes) | They own *scenes*. PeTI's edge is **tasks**: every chamber has a win condition by construction. "800k chambers" must always be said as "800k *tasks with win conditions + 15y of human votes*", or it loses to "infinite houses". |
| "Agents in commercial 3D games at a frontier lab" | **SIMA 1/2** (≈10 studio-partnership games, pixels→kbd/mouse, *no engine instrumentation*, success judged by rubric/Gemini — SIMA 2's own report concedes noisy success detection); **Genie 3** (generated worlds, no physics ground truth) | The instrumentation depth (oracle, tick-truth, savestates) is precisely what SIMA-style infra *lacks* — and what Genie *cannot have*. This is the real wedge. |
| "Human expert demos with actions, at scale, for free" | **VPT** paid ~2k contractor-hours for labels; **CSGO dataset** scraped public demos; MineRL/BASALT paid MTurk | board.portal2.sr demos are action-bearing and **public — also to competitors**. The moat is the replay/render farm + lead time, not exclusivity. |
| "Only grounded two-agent communication benchmark" | Overcooked-AI, Melting Pot, **TeamCraft / Minecraft multiplayer** (trivial to wire two agents + chat today) | Overclaim. Sharpen to: *author-able interdependent 3D-physics co-op with an in-game editor* — that survives. |
| "Pausable, branchable, ground-truth simulator protocol on a AAA physics binary" | Nothing public. SC2LE/Dota APIs were partnership-built and neither is branchable; emulator savestates (BizHawk) exist only for retro games | **Genuinely rare today** — but it's *in the MIT repo*, so it's rare, not defensible. Rare-and-forkable = first-mover advantage measured in months. |
| "Non-Euclidean (portal) physics tasks" | Nothing. No commercial or research env expresses portal-traversal momentum tasks at all | **The only literal monopoly on the board.** |

## B. The two battlefields (the council's category error)

The ten lenses conflate two different competitive games:

1. **Env-vs-env** (Portal 2 vs Minecraft/Factorio/Habitat/ARC): won by *game-structural
   properties* — PeTI authoring, win-conditions-by-construction, portals, the demo archive,
   co-op authoring, PSPACE results. These are real and strong.
2. **Team-vs-team** (this project vs a fork of this project, or vs DeepMind+Valve): the
   game-structural properties are **equally available to whoever forks the MIT repo**. On
   this battlefield the only assets that work are **clocks** (data that accrues with time),
   **process** (held-out refresh, verification), **social capital** (p2sr trust, Valve's
   tolerated-plugin equilibrium), and **brand** (citation primacy).

Almost every "moat" asserted by the council is a battlefield-1 asset being sold as a
battlefield-2 defense. The teardown's job is to fix the mapping and re-derive build order.

### The internal-rebuild paths, priced honestly
A DeepMind TL asked "why fund instead of build?" proposes one of three paths:

- **Path A — fork this repo + 2 engineers.** Gets: everything public, in a quarter (the
  designs and war stories are *in the repo*). Does NOT get: accrued human data, the
  community bridge, the velocity of the person who built it, held-out sets.
- **Path B — Valve partnership for a first-party API** (SC2LE/Dota-Five precedent). Gets:
  legitimacy + 100% of the Workshop + 64-bit engine access. Costs: **Valve's legendary
  organizational latency** — flat structure, no BD pipeline; partnership-grade asks
  historically take 12–24 months *if ever*. Meanwhile the community-plugin path needs no
  permission. Valve's unresponsiveness is asymmetric protection **for** this project.
- **Path C — "just use SIMA infra / Genie worlds / computer-use".** Gets: scale and zero
  RE. Does NOT get: ground truth, determinism, verifiable success — the things SIMA 2's
  own evaluation section concedes it lacks.

Conclusion to engineer into the pitch: **funding the existing thing is the cheapest path
to the asset** — and the part of the asset that can't be forked is the person + community
bridge + running clocks. The pitch is structurally an acqui-hire; write it that way.

## C. Defensibility ranking (what's actually worth deepening)

- **Tier 0 — commodity (assume replicated the week you publish):** Gym wrapper, SoM
  annotation, macro verbs, ReAct scaffold, trajectory viewer, "VLM solved a chamber".
  *Ship freely as marketing; never pitch as moat.*
- **Tier 1 — quarter-for-a-funded-team:** the full harness (forkable), workshop ingestion,
  .p2c generator, exit oracle, savestate protocol. *Rare today; perishable. Value = months
  of lead, spent wisely or wasted.*
- **Tier 2 — slow/social/process (real team-vs-team moats):** consented human .hdem corpus
  (accrues only after the consent flow ships), curated chamber DB + human difficulty
  calibration (accrues with probe spend), fresh-workshop held-out *process*, p2sr
  relationship + mandatory-SAR distribution channel, benchmark citation primacy.
  **None of these clocks has started.**
- **Tier 3 — structural monopolies (env-vs-env only):** portal/non-Euclidean physics
  tasks, PeTI tasks-with-win-conditions UGC, action-bearing public demo archive behind a
  leaderboard, author-able physics co-op, Valve's benign-neglect equilibrium.

---

## D. Ideas

### 1. The 2026 teardown dossier: claims × competitors matrix, with receipts
**What:** Turn section A into a maintained doc (`brainstorm/competitive_teardown.md`):
rows = every moat claim made anywhere in `brainstorm/` (grep the spiciest-takes), columns
= BALROG, Cradle, VideoGameBench, lmgame-bench, Voyager/Mineflayer, FLE, SIMA 2, Genie 3,
ARC-AGI-3, ProcTHOR/Habitat, OSWorld, Game Arena, Pokémon scaffolds, CSGO-dataset,
Overcooked/MeltingPot/TeamCraft. Each cell: *has-it / builds-it-in-X-weeks /
structurally-can't*, with a citation. Run one deep-research pass to refresh against
post-cutoff releases.
**Why:** Every lens asserts uniqueness; no doc on disk can survive a reviewer who knows
Cradle exists. The matrix is also the raw input for ideas 2–10.
**Effort:** S. **Unlocks:** claim triage (#9), release order (#10), the wargame memo (#5),
the why-Portal-2 one-pager the pitch lens wants.

### 2. Two-battlefields doctrine: re-tag every roadmap item by which war it wins
**What:** One page codifying section B, then a pass over ROADMAP + council backlogs
tagging each item **[env-vs-env]** or **[team-vs-team]** (or neither). Rule: pitch decks
may only present battlefield-1 assets as "why Portal 2" and battlefield-2 assets as "why
us"; mixing them is the error a sharp TL will catch.
**Why:** The council's moat claims fail exactly at this seam — "PeTI UGC" defends against
Minecraft, not against a fork. The doctrine prevents an embarrassing pitch moment and
reprioritizes: it shows most current work strengthens the (forkable) instrument while zero
work has started any clock.
**Effort:** S. **Unlocks:** honest pitch framing; the data-clock audit (#7); makes the
acqui-hire framing legible.

### 3. Attack yourself: the Minecraft fast-follower time-box
**What:** Two-week hard-capped attempt (one RE + Claude) to rebuild the percept/act layer
on Mineflayer: SoM-annotated frames (project entity AABBs through the camera), `go_to`
(mineflayer-pathfinder), `pick_up`/`place`, MacroResult codes, `.trajectory` output, one
cube-on-pressure-plate-opens-door task. Record actual hours. Publish the number in the
benchmark paper as a *portability* result ("the protocol generalizes; here is the cost"),
and keep the artifact as a cross-game control arm.
**Why:** The single most load-bearing unknown in the whole moat story is "could a team add
set-of-marks + macro verbs to Minecraft in a month?" Stop asserting; measure. If it takes
2 weeks, the scaffold is officially commodity and the pitch must rest on Tier 2/3 assets —
better to learn that in-house for $0 than from a NeurIPS reviewer. Either way you win:
fast = portability claim + second env; slow = the scaffold is harder than it looks and the
moat story strengthens.
**Effort:** M (hard-capped). **Unlocks:** cross-env generalization arm for the eval-science
lens; kills or confirms a moat claim; "protocol, not Portal2Gym" platform story.

### 4. The computer-use null hypothesis: Cradle-style agent vs the harness
**What:** Run the strongest available screen-scraping agent (Gemini computer-use API, or a
Cradle-style scaffold: raw screenshots, synthesized kbd/mouse, no SoM, no macros, no
oracle) on the same chambers as the macro agent, same model, same budgets. Report solve
rate, $/solve, wall-clock, and — critically — attribution quality (can you tell WHY it
failed?).
**Why:** This is the sharpest unexamined threat in the entire council corpus. The 2026
default question is not "could DeepMind rebuild your harness?" — it's **"why do you need a
harness at all when our computer-use agent plays games from pixels?"** If a stock
computer-use agent solves testchamber_000, the instrument pitch is dead as stated and must
retreat to attribution + cost + ground truth. If it flails (likely: real-time, no pause,
no marks), the delta is the single number that justifies the harness's existence. Run this
*before* the pitch, because a TL will run it after.
**Effort:** M. **Unlocks:** the "why instrumentation" slide with a measured delta; the
honest fallback positioning if the answer is bad.

### 5. The internal-rebuild wargame: a build-vs-buy-vs-fund memo
**What:** Write the memo a DeepMind TL would write, better than they would: Path A (fork
the MIT repo, 2 engineers × 1 quarter — itemize what the repo hands them, including the
brainstorm/ war stories), Path B (Valve partnership: precedent timeline from SC2LE/Dota
Five; estimate 12–24mo calendar), Path C (SIMA/Genie/computer-use reuse: itemize the
ground-truth gap from SIMA 2's own published evaluation caveats). For each: cost, calendar,
and the asset list it does NOT acquire. End with the funding-is-cheapest conclusion and
the acqui-hire framing.
**Why:** "Could they rebuild it internally?" is the pitch's existential question and
currently has no written answer. Pre-empting it with *their* memo, costed honestly, is the
strongest credibility move a solo pitcher can make — and it forces this project to confirm
which assets are actually unforkable (spoiler: the clocks and the person).
**Effort:** S. **Unlocks:** the risk-register slide; pitch-strategy's "headcount as
workstreams" gains a defensible "why us" spine.

### 6. The Valve-latency asymmetry: measure the moat, and beware the partnership
**What:** Document the asymmetry as analysis (companion to the council's "Valve packet"
outreach): community plugins have shipped against Valve binaries for a decade with zero
permission (SAR itself); lab-grade partnerships took years and political capital (Dota
API, CSGO dataset releases). Estimate fast-follower-via-partnership calendar at 12–24mo.
Then the contrarian corollary: **a first-party Valve API would dissolve the team-vs-team
moat** — it would be available to every lab equally, and instantly obsolete the RE-heavy
harness. Decide what to actually *want* from Valve: tolerance + UGC blessing, not an API.
**Why:** Every lens treats Valve as pure risk to retire. The teardown shows Valve's
organizational latency is *protection*, and that the obvious ask (an official API) is
anti-moat. This changes what the outreach packet should request.
**Effort:** S. **Unlocks:** corrected Valve strategy; a defensibility argument for the
wargame memo (#5).

### 7. The data-clock audit: rank datasets by accrual rate, start every clock this month
**What:** One table over the four data assets: (a) consented human .hdem-v2 recordings —
accrues only after the consent flow + recorder ship; every month of delay is a month a
forker can erase; (b) board.portal2.sr demo archive — **public, zero exclusivity**; value
= ingestion farm + first-mover backfill, so backfill is urgent-but-not-moat; (c) curated
chamber DB + difficulty labels — accrues with probe spend; (d) eval .trajectory archives —
accrue automatically. For each: replication latency for a funded follower, and the date
its clock starts. Action item: the consent flow + .hdem v2 ship this month, even in v0
form, *because the clock matters more than the polish*.
**Why:** Tier 2 is the only durable team-vs-team tier and **none of its clocks has
started**. The council has .hdem v2 ideas everywhere as a data-product play; this lens
adds the competitive deadline: lead time is the only thing a fork can't download.
**Effort:** S for the audit; the audit's output is a scheduling constraint on existing
M-sized council items. **Unlocks:** turns the data-flywheel lens from "valuable someday"
into "depreciating if not started".

### 8. The SIMA-gap probe: measure their evaluator's error with your oracle
**What:** On 5–10 chambers, score identical agent runs three ways: (i) ground-truth
engine oracle, (ii) a Gemini-judge rubric over the video (SIMA-style success detection),
(iii) human labels. Publish the confusion matrix — judge-vs-oracle false-success and
false-failure rates.
**Why:** SIMA 2's published weakness is noisy, rubric-based success detection. The
strongest complement-positioning (not competitor-positioning) artifact possible is a
measured error bar on *their* methodology, produced by an instrument they can't have
without engine truth. This is the centerpiece of the pre-read memo the pitch lens wants —
evidence, not assertion, that the harness is the calibrator the SIMA/Genie loop lacks.
**Effort:** M (needs oracle + a handful of chambers; judge harness is API-only).
**Unlocks:** the DeepMind wedge; doubles as the LLM-judge validation the eval-science lens
needs anyway.

### 9. Claim triage: retire, sharpen, or keep every uniqueness claim
**What:** From the matrix (#1), a one-page verdict per claim. Retire: "LLM plays a
commercial game" (Cradle), "only two-agent grounded benchmark" (as stated). Sharpen:
"800k chambers" → "800k *tasks with win conditions* + 15y of human difficulty votes"
(survives ProcTHOR); co-op → "author-able interdependent 3D-physics co-op" (survives
TeamCraft/Overcooked). Keep & lead: ground-truth per-step attribution on a commercial 3D
physics binary; portal physics (monopoly); action-bearing expert archive + speedrun
verification culture. Patch the ROADMAP thesis paragraph and every council doc header
accordingly.
**Why:** A pitch that overclaims once loses the room; reviewers in 2026 know BALROG,
Cradle, and FLE. Self-inflicted triage is cheap; reviewer-inflicted triage is fatal.
**Effort:** S. **Unlocks:** every public artifact (paper, landing page, pre-read) inherits
defensible language.

### 10. Release-order strategy: ship by defensibility, not by readiness
**What:** A 4-release plan derived from the tiers, inverting the natural ship-what's-done
order: **R1** the portal-physics/wormhole eval set (Tier 3 monopoly — zero substitutes,
uncommoditizable by a scaffold port); **R2** the attributed leaderboard with
oracle/teleport control arms (Tier 1 instrument + Tier 2 brand clock); **R3** the harness
container + protocol (Tier 0/1 — *deliberately* given away as marketing and standard-
setting); **R4** the human corpus under a data license (Tier 2, clock-gated). Held-out
seeds, curation DB, and eval-server internals never ship.
**Why:** Default behavior ships the harness first because it exists — handing the fork
its quarter head start before any clock runs. Competitive logic says publish the monopoly
first (nobody can fast-follow a capability their environment cannot express), and time the
commodity release for when the brand clock is already running.
**Effort:** S (it's a sequencing decision over existing council items). **Unlocks:**
turns ten lenses' worth of build ideas into an ordered, moat-aware shipping plan.

### 11. Capture the BALROG column: integrate instead of compete
**What:** Package Portal 2 as an environment *inside* BALROG (and/or lmgame-bench): a PR
to their repo wrapping the container + agent protocol, with 3 starter chambers and a
maintained results column. Their leaderboard, your environment.
**Why:** The citation race is a Tier 2 clock, and the cheapest way to win it is to occupy
a column in the eval suite frozen-agent papers already use — every future BALROG paper
then reports your benchmark for free. It also pre-empts the most likely fast-follower
*distribution* move (someone else wrapping your public harness for BALROG and owning the
integration).
**Effort:** M (depends on the container; their env API is thin). **Unlocks:** citation
primacy, external users stress-testing the harness, the "standard, not repo" position.

### 12. The compound-asset demo: one chain no one else can run this year
**What:** A single scripted demo: (a) pull a top-100 demo from board.portal2.sr, (b)
re-render it through the farm into a rollout, (c) auto-extract macro-level labels from
entity events, (d) savestate-search a segment to beat its time, (e) verify the improved
run with the oracle and emit a `.trajectory`. Five steps, one repo, one command, recorded
as a 3-minute video.
**Why:** The teardown's honest conclusion is that each asset alone is replicable; the
*compound* — archive + farm + ground truth + savestate search + verification — is what
takes a follower a year to assemble even with the fork in hand. A moat that only exists as
integration must be *demonstrated* as integration; this is the artifact that makes
"the instrument is the moat" a video instead of a sentence.
**Effort:** M (assembles council items that are individually planned: oracle, anchors,
backfill farm). **Unlocks:** the pitch centerpiece; smoke-tests four subsystems against
each other; the speedrun-routing lens gets its first end-to-end rep.

### 13. Landscape tripwires: a standing quarterly scan
**What:** A cron'd deep-research run + alert list (SIMA/Genie releases, Game Arena game
additions, ARC-AGI-3 launch and adoption, FLE versions, any Portal/Source agent paper or
repo, Mineflayer SoM repos, computer-use game demos), appending a dated delta page to the
dossier (#1). Rule: no pitch deck cites a landscape older than 90 days.
**Why:** Every uniqueness claim in this corpus has a shelf life measured in months —
first light's "frozen VLM solves a 3D puzzle" headline could be matched by a SIMA 2 blog
post any Tuesday. A solo RE can't afford to discover that mid-pitch.
**Effort:** S (one evening; reuses the deep-research harness). **Unlocks:** dossier
freshness; early warning to re-trigger claim triage (#9).

### 14. The fork-surface audit: what a hostile fork gets on day 1
**What:** Complementing the council's "three rings" policy idea with the adversarial
inventory that should *drive* it: enumerate exactly what `git clone` hands a competitor
today (harness, protocol, MacroExecutor, annotation, the full design rationale in
brainstorm/ — which is itself a replication accelerant), what MIT obliges you to keep
open, and what is legally separable: held-out chamber seeds, curation DB, eval-service
code (can be closed — MIT is not copyleft), the human corpus (data ≠ code; needs its own
license + consent terms drafted *before* the first community recording), trademark-safe
name. Decide deliberately whether brainstorm/ ships in the public repo.
**Why:** The repo currently mixes the giveaway (code) with the lead-time assets (designs,
recon results, war stories, future held-out plans) in one history. Separating them is
cheap now and impossible after the first public push.
**Effort:** S. **Unlocks:** the rings policy gets teeth; data licensing unblocks the
consent flow (#7); prevents the irreversible mistake.

---

## Spiciest take

Nearly every moat the council asserted is either a property of Portal 2 — equally
available to anyone who forks this MIT-licensed repo, brainstorm war stories included —
or a commodity scaffold a Mineflayer team rebuilds in two weeks. The only moats this
*project* can own are clocks: human-data accrual, held-out refresh, citation primacy,
community trust — and as of today **not one clock has started ticking**, because the work
keeps deepening the forkable instrument instead. Worse, the council spent ten lenses
red-teaming "could DeepMind rebuild the harness?" and zero lenses on the question a TL
will actually ask: *"why does this need a harness at all when our computer-use agent
plays games from raw pixels?"* Run that control arm before anyone pitches anything —
if stock computer-use solves testchamber_000, the instrument story is dead as stated;
if it flails, that delta is the best number in the deck. And quietly drop the dream of an
official Valve API: Valve's unresponsiveness is the moat — a first-party API would be
issued to every lab equally and would obsolete this codebase overnight.
