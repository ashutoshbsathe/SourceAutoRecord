# Lens: human-funnel — education kit, academic allies, contributors, plan-B funding

*Gap-filler brainstorm, 2026-06-12. The critique pass found the council built an adoption
machine with no people in it: starter kits, containers, and leaderboards, but no course kit,
no outreach to the theorists every doc cites, no conversation with the modders who already
solved the headless-compile unknown, no contributor on-ramp to fix bus-factor=1, no
accessibility story, and exactly one funder in the entire pitch strategy. This lens designs
the **adoption flywheel** as deliberately as the data flywheel — including the two emails and
one Discord message that should go out next week.*

**Effort key:** S = days, M = weeks, L = months, XL = needs a team — all at one-RE-plus-agentic-coding staffing.

---

## The framing problem this lens has to solve

For a solo RE with agentic coding, **code is no longer the binding constraint — calendar and
allies are.** Every council idea is a build; almost none is a *send*. But the artifacts only
compound if humans pick them up, and humans arrive through funnels that take months of calendar
to warm up and hours of effort to start. Three sub-funnels, one rule:

1. **Education** — students are the decade-scale adoption channel (Berkeley's Pacman built a generation of AI researchers). The offline prompt-replay rig makes this funnel uniquely cheap here: gradeable with **zero game install**.
2. **Allies & contributors** — the theorists (Demaine/Lynch), the eval researchers (BALROG/UCL DARK), the modders (BEE2/srctools), the speedrunners (p2sr). Each has already done years of work this project depends on; none has been contacted.
3. **Plan-B money** — pitch-strategy has one funder and a binary outcome. Diversified small money (credit grants, fast grants, accessibility lane) buys the calendar to wait for the *right* VP meeting instead of the first one.

The rule: **every send must attach a working artifact** (the first-light viewer, a notebook, a
trajectory). The council built the artifacts; this lens mails them.

---

## Ideas (ordered roughly by leverage)

### 1. P2-Agents course kit: Berkeley-Pacman for embodied VLM agents, zero game install
**What:** A 3–4 project course module, autograded entirely offline against archived data.
*Project 0:* parse `.trajectory`, compute metrics (loop detection, knowing-doing gap) — pure
Python over the offline-analysis library. *Project 1:* implement `next_action(obs) → command`
against the frozen agent-protocol spec, scored on golden decision points harvested from real
runs. *Project 2:* prompt-engineer the frozen VLM, graded via the prompt-replay rig against
logged observations (capped credit budget, or fully-cached responses for $0). *Project 3
(optional, extra credit):* a live run for students who own Portal 2 — at ~$10 (often $1 on
sale) the cheapest lab fee in CS. Ship as a repo + autograder + instructor solutions, licensing
posture documented (trajectories + annotated frames only; no Valve assets redistributed —
fold an education-use paragraph into the council's Valve packet).
**Why:** CS188's Pacman projects are the most successful adoption artifact in AI-education
history; every other game benchmark (Minecraft, NetHack) has an install wall that makes course
adoption a sysadmin project. This one grades on files. Each adopting instructor is an academic
ally with skin in the game; each cohort produces dozens of diverse student agents — free
baselines and failure data for the benchmark itself.
**Effort:** M (kit + autograder + writeups; depends on council's offline-analysis lib + golden-decision suite).
**Unlocks:** the decade-scale funnel; instructor allies; idea #13's pilot course; "who will use this?" answered with enrollments.

### 2. The Demaine letter: make the theorems' authors co-conspirators
**What:** One well-written email to Erik Demaine and Jayson Lynch (FUN 2018, *The Computational
Complexity of Portal and Other 3D Video Games* — cited in ROADMAP.md and half of `brainstorm/`):
"your reductions are now executable — we built a harness that can compile gadget families into
playable chambers and measure frozen-VLM solve-rate decay along your complexity ladder; here is
a frozen model solving a cube→button→door instance from pixels" + the first-light viewer link.
Asks, smallest first: (a) a 30-minute call to sanity-check the Demaine-ladder generator's
gadget→chamber mapping, (b) review of the complexity claims in paper #1, (c) co-authorship on
the formal-gadget-compiler section or the NeurIPS competition proposal.
**Why:** every council lens leans on the PSPACE result for methodological cover, and nobody
proposed telling the people who proved it. Theory groups — Demaine's especially, given his
recreational/hands-on computation track record — love seeing their gadgets physically realized.
One famous co-signer transforms the competition proposal's credibility and inoculates the
complexity framing against reviewer attack. Hours-cheap, calendar-long: send it now.
**Effort:** S (an afternoon to write well).
**Unlocks:** theory co-authors; correctness review for the gadget compiler; academic legitimacy a solo RE cannot buy any other way.

### 3. The TeamSpen ping: one Discord message collapses the headless-compile unknown
**What:** Message TeamSpen210 — maintainer of BEE2.4 and author of `srctools`, the de facto
authority on `.p2c`→VMF — via the BEE2 Discord/GitHub discussions, with three questions: is
fully-headless `.p2c`→`.bsp` viable, where does it break, and would they take a paid bounty for
a minimal `p2c_compile` CLI. The puzzle-gen lens calls headless compile "the single load-bearing
unknown of the lens"; this person has spent years inside exactly that format.
**Why:** the highest information-per-character action available to the project. Worst case: a
polite "hard because X" that saves weeks of recon. Best case: the most qualified possible
external contributor joins for the price of a bounty. Also the prototype for a general pattern —
the modding community has already solved half the platform's unknowns; ask before building.
**Effort:** S (the message). M if it becomes a funded mini-contract — still cheaper than solo recon.
**Unlocks:** `py/puzzlegen`, the headless compile pipeline, the entire generated-curriculum lens; first deep-domain external contributor.

### 4. BALROG handshake: get Portal 2 adopted as *someone else's* environment
**What:** Email the BALROG team (UCL DARK lab) — whose frozen-agent eval philosophy and
knowing-doing-gap framing this project already borrows — with an artifact-shaped offer: the
agent-protocol spec + container + 5 chambers; "add Portal 2 as a BALROG environment — we do the
integration, you get a new hardest-tier 3D-physics environment with ground-truth failure
attribution." Generalize into an **ally map**: 5–8 named groups whose published limitations
sections this instrument answers (embodied-eval, language-agents, games-and-AI), each with a
specific offer ("we run your agent, you get a results section"), worked top to bottom.
**Why:** distribution through an existing benchmark's audience beats a standalone leaderboard
nobody visits, and NeurIPS competition proposals need named co-organizers — this is where they
come from. The council assumed adoption happens *to artifacts*; in practice it happens *through
people who co-own results*.
**Effort:** S per email; M to support one real integration (needs council's container + protocol spec).
**Unlocks:** external groups building the cross-model table for you; competition co-organizers; citations independent of your own papers.

### 5. A research wing inside p2sr: channel, office hours, CONTRIBUTING-RESEARCH.md
**What:** Ask p2sr's mods for a `#sar-research` channel; hold monthly "harness office hours";
write `CONTRIBUTING-RESEARCH.md` mapping community skills to project needs — SAR C++ devs →
harness verbs; TASers → the glitch regression suite; mappers → the chamber suite; runners →
the `.hdem` corpus. The council *uses* p2sr everywhere (TAS co-organizer, demo corpus, human
baselines) but never built the door they walk in through.
**Why:** p2sr has maintained the upstream of this very repo, unpaid, for a decade — the only
community on Earth pre-qualified to contribute C++ to a Source-engine plugin. Bus-factor=1 is
not fixed at pitch time; it is fixed by making the second contributor's first hour frictionless.
**Effort:** S (a message + two docs); ~2h/month ongoing.
**Unlocks:** the contributor pipeline; `.hdem` volunteers; TAS-track co-organizers; the stewardship story becoming observably true instead of aspirational.

### 6. The contribution ladder: chambers are the no-code on-ramp
**What:** Curate 15–20 genuinely self-contained good-first-issues in three tracks: **(a)
no-code** — author a PeTI chamber + `manifest.json` + reference solution (anyone who owns
Portal 2 can mint a benchmark item in an evening; the manifest standard is the quality gate);
**(b) Python-only** — viewer features, exporters, trajectory analysis (no game, no C++, no
32-bit toolchain); **(c) C++** — small macro verbs and telemetry fields behind the smoke-test
gate. Each issue gets acceptance criteria and a link to the design doc it serves; chamber
authors get named credits in benchmark cards.
**Why:** today the contribution surface reads "understand a 32-bit game-hacking plugin plus a
JAX RL stack" — a wall. The benchmark's scarcest input (good chambers) needs zero code; the
suite should be community-authored the way Workshop puzzles already are. The ladder is also the
funnel's plumbing: chamber author → Python contributor → C++ maintainer.
**Effort:** S to set up; ongoing review time bounded by the acceptance criteria.
**Unlocks:** the 20-chamber suite without hand-authoring all 20; a graded path to a second maintainer; somewhere to send everyone ideas #1–#5 attract.

### 7. Plan-B funding matrix: eight funders, two applications submitted this month
**What:** A one-page matrix — funder, check size, decision latency, what it buys, which
first-light artifact to attach — then actually submit the two cheapest. Candidates: **Emergent
Ventures** (fast, talent-bet-sized, built for exactly "one person, absurd leverage" — the
application is an essay); indie **AI-grant programs** (AI Grant-style, if still running in
2026); **Open Philanthropy** (has funded eval/benchmark orgs of the METR/Epoch shape);
open-science programs (Mozilla-style); **Kaggle prize sponsorship** for the eventual
competition; **GitHub Sponsors** on the fork (small money, strong signal); and the academic
indirect route — an allied professor (ideas #2/#4/#10) writes the harness into a grant as
funded infrastructure.
**Why:** pitch-strategy has exactly one funder and a binary outcome. A solo RE pitching from
"funded either way" negotiates from strength; "this dies if you pass" invites the pass.
Diversified small money buys calendar — the one resource the pitch timeline actually runs on.
**Effort:** S–M (matrix is a day; each application a day; EV-class decisions land in weeks).
**Unlocks:** survival independent of plan A; the council's "inevitable with or without you" landing-page posture, made financially true.

### 8. The credits stack: fund the eval matrix with API-credit grants
**What:** The dominant cash cost is inference — 686k input tokens for ONE 25-step run; the
multi-model × chambers × seeds matrix is both the most pitchable chart and the biggest bill.
Apply to all the researcher-credit programs at once: Google (Gemini academic/research credits +
Cloud research credits for the eval farm), OpenAI Researcher Access, and Anthropic's
academic/external-researcher programs. Each application is a form plus an abstract; attach the
first-light trajectory.
**Why:** the rare funding that needs no meeting, arrives in weeks, and pays for exactly the
pre-pitch hardening (the cross-model table) the pitch lens says must exist *before* the VP
meeting. Asymmetric: an afternoon of forms versus a $5–30k sweep bill. Bonus valence: every lab
that grants credits becomes mildly invested in its model's row on the leaderboard.
**Effort:** S.
**Unlocks:** the multi-model table, the thinking-budget scaling curve, and cross-model runs — unblocked from personal cash.

### 9. Macro grammar as assistive tech: "play Portal 2 by voice"
**What:** A thin speech/text front-end on the existing macro REPL: STT → command grammar →
MacroExecutor — and note that **world-freeze turns the game turn-based**, so a player can take
arbitrary time composing each command. The harness's pause-while-thinking design *is* a
motor-accessibility feature; nobody on the council noticed that the abstraction serving frozen
VLMs equally serves humans who can't drive WASD+mouse at 60Hz. Ship as a SAR cvar + a 3-minute
demo video; show it to accessibility-gaming communities (AbleGamers, the adaptive-controller
ecosystem).
**Why:** four payoffs from one S–M build: a contributor community with non-overlapping skills;
an accessibility-grant funding lane with a completely different gatekeeper than plan A; the
friendliest possible press frame for the Valve conversation ("research tool lets disabled
players finish Portal 2" beats "we farm your game for AGI data"); and macro-altitude human
baselines as a side effect of people actually playing this way.
**Effort:** S–M (macro_repl + Whisper-class STT is assembly; outreach is calendar).
**Unlocks:** second funding lane; Valve-goodwill narrative; a use case that survives even if the research agenda dies.

### 10. Five theses, pre-scoped: rent headcount from academia
**What:** Write five one-page MS-thesis proposals from council ideas that are genuinely
self-contained, each stating *infrastructure provided, data provided, co-supervision offered,
target venue named*: (1) learned `go_to` from human `.hdem` segments; (2) the static difficulty
predictor over the chamber DB; (3) the perception-probe VQA battery; (4) loop/recovery metrics
over `.trajectory` archives; (5) the mark-robustness perturbation study. Shop them to 3–5
professors — the ally map from idea #4 is the natural list.
**Why:** a master's student is the cheapest headcount in existence and the standard way
under-resourced infrastructure scales. Honest cost: supervision is real (2–4 h/week per
student) — cap at two concurrent. Each completed thesis is a paper section, an adopter, and a
pre-vetted hire for the eventually-funded team.
**Effort:** S to write; M in ongoing calendar (supervision) — budget it honestly.
**Unlocks:** parallel workstreams without funding; professors with stakes; the "three institutions already build on this" slide.

### 11. Reproduction bounty + hall of fame
**What:** `REPRODUCTIONS.md` plus a public offer: the first 5 external reproductions of first
light (any frontier model, own hardware, via the starter kit) get a named entry, a small bounty,
and acknowledgment in paper #1. Verification is free: require the `.trajectory` artifact and
run it through the council's trajectory-audit tooling.
**Why:** until someone outside this repo reproduces first light, it is an N=1 single-author
claim — reviewers and VPs both know it. The drive triples as external validation, a brutal QA
pass on the starter kit/container under genuinely adversarial conditions, and a contributor
filter: anyone who completes a reproduction has proven they can run the whole stack — exactly
who to hand a good-first-issue from idea #6.
**Effort:** S (gated on the council's starter kit existing).
**Unlocks:** "independently reproduced N times" in the paper and the pitch; pre-qualified contributors; starter-kit hardening.

### 12. The 2-hour tutorial: one notebook, no game required
**What:** A single self-contained notebook against archived data: load a real `.trajectory`,
render the viewer inline, write a 30-line rule-based agent against the protocol spec, score it
on golden decisions, compare with Gemini's run. Record one talk-through video; submit as a
conference tutorial; offer it as a guest lecture in allied professors' courses.
**Why:** the top of every other funnel in this lens. The course kit is a semester commitment;
the tutorial is an evening — and nobody adopts a platform they haven't touched in anger for two
hours. It doubles as contributor onboarding ("day 0" for any new collaborator) and as the
executable pre-read a VP's tech lead can run on a laptop before the meeting.
**Effort:** S–M.
**Unlocks:** course-kit funnel; contributor onboarding doc; the pre-wired-TL pitch motion with an artifact instead of a memo.

### 13. Pilot the competition as one course's semester project
**What:** Before any NeurIPS proposal, run the whole competition machinery once, small: one
allied professor's grad course adopts "climb the P2 ladder" as its semester project. Students
submit agents against the frozen protocol; offline grading plus a private 5-chamber held-out
set; end-of-semester class leaderboard and writeup.
**Why:** every competition's first run finds operational fires — submission formats, budget
enforcement, cheating vectors. Find them with 25 friendly students, not 200 anonymous teams and
a NeurIPS deadline. The professor becomes a *tested* co-organizer, and the class report becomes
the pilot-evidence section NeurIPS competition reviewers explicitly ask for.
**Effort:** M, spread thin across a semester (support + grading infra).
**Unlocks:** de-risked NeurIPS proposal; a co-organizer who has actually run it; the first 25 external agents; course-kit validation — one move, four payoffs.

### 14. Build-in-public: the first-light post and a monthly research note
**What:** `brainstorm/first_light_and_next_steps.md` is already 90% of a great blog post — the
steps 0–7 vs 8–24 split, the step-12 cube-bump recovery, the glass-box punchline. Publish it
(personal site / HN / X) with the viewer link, then keep a monthly research-note cadence drawn
from the `brainstorm/` docs that already exist. Post **before** the emails go out — Demaine,
TeamSpen, and BALROG will all look you up, and what they find decides the reply rate.
**Why:** the council's landing-page idea has no content engine behind it; a funnel needs a top.
The first-light story is legible to lay audiences ("solved the puzzle, couldn't walk out of a
glass box") *and* researchers (the macro-boundary separation). Every plan-B application in idea
#7 asks "show us what you've done" — this is the evidence stream. And it timestamps public
priority on the idea space, which matters for a solo researcher who will be scooped on pieces
of it regardless.
**Effort:** S per post (the writing is substantially done).
**Unlocks:** inbound contributors and funders; EV/grant application evidence; the audience that the sizzle video and Twitch stream ideas later inherit.

### 15. Affiliation: take first light to the open-research circuit
**What:** A 20-minute "frozen VLMs in a PSPACE-hard physics world" talk with the trajectory
viewer as the demo, given at open-research communities built for unaffiliated researchers — ML
Collective's reading group, EleutherAI's Discord, Cohere-style open-science programs. Goal:
collaborators, pre-submission reviewers, and a community affiliation line for solo-author papers.
**Why:** independent-researcher papers pay a quiet credibility tax; these communities exist
precisely to pool affiliation, feedback, and co-authors. One good talk historically yields
collaborators — the cheapest academic on-ramp while the higher-value, slower Demaine/BALROG
emails are in flight.
**Effort:** S.
**Unlocks:** co-authors; paper feedback before submission; an affiliation; warm intros into labs adjacent to the pitch target.

---

## Next week, literally (the calendar-long, hours-cheap sequence)

Ordering matters: the post goes first because every recipient will look you up.

1. **Day 1–2:** publish the first-light post (#14) — the source doc is already written; add viewer screenshots and ship.
2. **Day 2:** send the TeamSpen210 message (#3) — fastest reply loop, gates the most build-work.
3. **Day 3:** send the Demaine/Lynch email (#2) with the post + viewer links attached.
4. **Day 3:** submit all three API-credit applications (#8) — forms, not essays.
5. **Day 4:** open the p2sr `#sar-research` conversation (#5).
6. **Day 5:** draft the BALROG email (#4); send when the container/protocol artifacts it promises are honest, or send now with "shipping in N weeks" dates.

Total engineering cost: ~zero. Total expected calendar saved: months.

---

## Spiciest take

The council produced ~150 *build* ideas and zero *send* ideas — and for this team, that's
optimizing the unconstrained resource. One RE with agentic coding is not code-bound; code got
cheap the day the agent showed up. The binding constraints are **calendar and allies**, and the
only moves that compound while you sleep are the ones that recruit other humans. The three
highest-leverage artifacts shippable next week are two emails and a Discord message: Demaine's
group turns the methodological cover into co-authorship, TeamSpen collapses the puzzle-gen
lens's single load-bearing unknown for the price of a bounty, and the first-light post decides
whether either of them replies. Meanwhile, treat the DeepMind meeting as plan A and the
*riskiest* plan in the portfolio — one funder, one meeting, binary outcome. The human funnel is
not the warm-up for the pitch; it's the hedge that lets you walk into the pitch able to say the
sentence that actually gets things funded: "this happens with or without you."
