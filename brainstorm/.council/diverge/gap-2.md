# Gap lens: Unit economics of the three businesses (eval, data, search)

*Gap-filler pass, 2026-06-12. The council produced a "numbers slide" idea and ~20 claims of
"near-zero marginal cost" — and zero arithmetic. This lens builds the cost model, plugs in every
number the repo already measured, marks the rest TBM (to-be-measured, with the instrument that
measures it), and runs the council's own ideas through the result. Every pitch claim of "cheap"
gets a number or dies.*

---

## 0. Price sheet (the parameters — everything downstream is a function of these)

All prices are **plug-in parameters**, kept in one place so the model reprices when list prices
move. Defaults below are Flash-tier / Pro-tier class estimates; the repo's `TokenUsage` records
make every historical run repriceable forever.

| Parameter | Symbol | Default | Status |
|---|---|---|---|
| Flash-class input | `p_in` | $0.30 /M tok | plug in current list price |
| Flash-class output (incl. thinking) | `p_out` | $2.50 /M tok | plug in |
| Flash-class cached input | `p_cache` | ~0.1 × `p_in` | plug in |
| Pro-class multiplier | `k_pro` | ~6–8× Flash | plug in |
| Cloud GPU VM (g4dn.xlarge-class) | — | $0.53 /hr on-demand, ~÷3 spot | public |
| Cloud CPU VM (16 vCPU) | — | $0.70 /hr | public (lavapipe path) |
| Local box power | — | 400 W × $0.15/kWh = $0.06 /hr | estimate |
| Object storage | — | $0.023 /GB-mo | public S3 |
| Instances per GPU box | `N_inst` | **TBM** (idea 4) — assume 4–8 | unknown |
| Game crash MTBF | — | **TBM** (idea 5) | unknown |
| Save/restore (anchor) latency | — | **TBM** (idea 10) — assume 0.5–3 s | unknown |

**Derived: $/instance-hour** — the number every business consumes:
local ≈ **$0.01**, cloud GPU on-demand ≈ **$0.09–0.13**, cloud spot ≈ **$0.03–0.05**.

## 1. Measured inputs the repo already has (free — just read them out)

| Quantity | Value | Source |
|---|---|---|
| First-light episode tokens | **686k in / 6.6k out**, 25 steps | `first_light.trajectory` |
| Per-step input growth (stateful chat) | **≈ +1.7k tok/step** (img 1,064 + text ~630) | transcript: step0=1,925 → step6=12,402 |
| Image tokens per 640×480 frame | **1,064** | transcript `(img 1064)` |
| Output per step | ~100–310 tok (mostly thinking) | transcript |
| Trajectory artifact size | **8.9 MB / 25 steps** (~356 KB/step, frames inline) | `first_light/` |
| Instance shape | gamescope 640×480, 1 game proc | `game_launcher.py` |
| SHM frame copy | ~1 ms, opt-in per tick | CLAUDE.md / Harness |
| Server tick rate | 60 tps; render farm ≈ 1× realtime today | `render_demos.py` |
| Raw pixel rollout scale | ~3.3 GB/run class (council figure) | data-flywheel lens |

The cumulative-input formula (stateful chat): `In(S) ≈ 1.9k·S + 1.7k·S(S−1)/2` — linear per
step, **quadratic cumulative**. It fits first light within retry noise.

---

## 2. Business #1: frozen-VLM eval — $/episode, $/leaderboard-row

### Episode cost model (Flash defaults)

| Context policy | 25 steps | 50 steps | 100 steps | shape |
|---|---|---|---|---|
| Stateful chat (today) | **$0.22 (measured)** | ~$0.83 | ~$3.25 | quadratic |
| Stateful + 90% cache hits | ~$0.06 | ~$0.20 | ~$0.66 | quadratic ÷ ~5 |
| O(1) scratchpad (~3k in/step) | ~$0.04 | ~$0.08 | ~$0.17 | **linear** |
| Pro-class, O(1) | ~$0.3 | ~$0.6 | ~$1.3 | linear |

Working: 100-step stateful = 1.9k·100 + 1.7k·4,950 ≈ **10.3M input tokens** → $3.10 + output.
O(1) = 100 × 3k = 300k → $0.09 + output ~$0.08.

**Game-instance cost per episode:** wall time is model-latency-dominated (world freezes while
thinking). At ~10–20 s/step → 25-step episode ≈ 8–15 min ≈ **$0.015–0.03** on cloud GPU,
**~$0.002 local**. Under stateful chat that's 5–10% of episode cost (noise); **under O(1) context
the game becomes 20–40% of episode cost** — that is the regime where throughput work starts to
pay (idea 14).

### $/leaderboard-row (P2-Bench-v0: 20 chambers × 5 seeds = 100 episodes, mean 50 steps)

| Configuration | $/row |
|---|---|
| Flash, stateful chat | ~$85 |
| Flash, O(1) | **~$10** |
| Pro-class, O(1) | ~$60–80 |
| 10-model full refresh, O(1) | **< $500** |

### Versus BALROG-class text-game evals

A NetHack/TextWorld step is ~1–3k text tokens with a ~free env step. Portal 2's per-step premium
under O(1) context is: **+1,064 image tokens** (~35–50% of the step's input) **+ ~$0.0002–0.0004
instance time**. Net: an embodied 3D episode costs **~1.5–2.5× a text-game episode** — not 50×.
That is a *selling point* ("3D embodiment at text-game prices"), but only after O(1) lands; under
today's stateful chat the multiple is 10–40× and growing with horizon.

### What this kills / blesses (eval)

- **The "we need funding to run evals" framing is dead on arrival.** A full leaderboard refresh
  is three figures. Do not pitch eval COGS; pitch eval *credibility* (held-out sets, verification).
- **model-frontier's "$30k → $1–2k for 1000-chamber sweeps"** is directionally right but the fix
  is O(1) context first, caching second: caching gives ~5×, O(1) gives 20×+ at long horizons *and*
  makes prompts single-turn (trainable, replayable). Caching only rescues the stateful-chat *A/B
  control arm*, which must stay for science.
- **24/7 Twitch stream:** stateful ≈ $30+/day ($900+/mo — wounded); O(1) ≈ **$5–8/day** (blessed).
  The stream's go/no-go is literally the context policy.
- **Hosted rolling ladder:** blessed *only* with per-entry token budgets ($-capped), since
  Pro-class stateful entries at 100 steps are ~$10+/episode and the organizer must never
  underwrite an entrant's context policy.

---

## 3. Business #2: data — $/hour, $/labeled-event, storage

### $/hour of trajectory data, by acquisition channel

| Channel | $/hour | Notes |
|---|---|---|
| Robot teleop (the comparator) | **$60–300/hr** | operator + hardware + QA; industry range |
| Paid human Portal 2 play | $15–25/hr | gamers at market rate, .hdem records at I/O speed |
| Community prize-drive | **~$10/hr** | e.g. $2k pool / ~200 hr collected |
| Opt-in p2sr recorder (post-distribution coup) | **≈ $0 marginal** | recruitment already paid by speedrunning itself |
| Demo-archive backfill (render farm, 1× realtime) | **$0.01–0.13/hr** | 1 data-hr = 1 instance-hr |
| Backfill with fast-forward (10–50×) | $0.001–0.013/hr | gated on idea 14's throughput work |
| Search-generated synthetic expert (see §4) | **< $1 per verified solve-trajectory** | compute-to-data |

Honest headline: **human Portal 2 data is 5–30× cheaper than teleop, and backfilled archive data
is 3–4 orders of magnitude cheaper** — *and* both carry perfect state labels (tick-exact pose,
entity states, events) that teleop pays extra to approximate. This is the data annex's table.

### $/labeled-event

Auto-annotation (entity-event → language span) costs only the replay that produced the stream:
at ~300–600 ground-truth events/hr and $0.13/instance-hr → **~$0.0002–0.0004/event**, vs human
annotation at $0.05–0.50/label: **~200–2,000× cheaper**, with zero label noise on state facts.
This is the single best unit-economics line the project owns; it should be ON the numbers slide.

### Storage, and what lazy-pixel actually buys

| Format | ~bytes/hr | 10k hr | $/mo @ S3 |
|---|---|---|---|
| .dem + .hdem (lazy-pixel canonical) | ~50 MB/hr (TBM) | 0.5 TB | **~$12** |
| 224×224 @ 4 Hz JPEG rollouts | ~0.5–1 GB/hr | 5–10 TB | $115–230 |
| 640×480 @ 60 Hz | 20–40 GB/hr | 200–400 TB | $4.6–9.2k |

Honesty check: **at pilot scale (≤10k hr, ≤4 Hz) storage is noise** — do not pitch lazy-pixel as
a cost story there. The crossover: re-rendering an hour costs $0.01–0.13; storing its 4 Hz pixels
costs ~$0.02/mo — so cached pixels pay for themselves after ~1–6 expected re-reads. Lazy-pixel's
real value is **resolution/fps future-proofing and the 100k-hr/60 Hz regime**, where it is the
difference between $12/mo and $9k/mo. Pitch it as architecture, not 2026 savings.

---

## 4. Business #3: search — $/node, and who actually dominates node cost

A search node = restore anchor + execute macro + read snapshot.

- Restore: **TBM**, assume 0.5–3 s (idea 10 measures it; in-memory restore is the upside case).
- Macro: 1–5 s game time → 0.1–0.5 s at 10× timescale + norender (gated on idea 14).
- → node ≈ 0.6–3.5 s → **1,000–6,000 nodes/instance-hr** → at $0.03–0.13/instance-hr:
  **$/node ≈ $2e-5 – $1e-4.** A 10k-node chamber search: **$0.2–1.0** of engine compute.

**The LLM prior, not the engine, dominates node cost.** One Flash prior call (~3k in + 300 out)
≈ $0.0017 ≈ **20–80× the engine cost of a node**. Design rule that falls straight out of the
arithmetic: priors at the root / every k ≥ 20 nodes / batched — never per-node. Any MCTS design
review starts with this ratio.

**Versus Procgen-class simulators:** Procgen does 10⁴–10⁵ env-steps/s/GPU; Portal 2 does ~0.3–1
macro-node/s/instance — a **~10⁵× gap in transitions per dollar**. Consequences:

- **Killed:** from-scratch sparse-reward pixel RL on full chambers (sample complexity is
  Procgen-shaped; the budget is not).
- **Survives, cheaply:** tick-level locomotion RL — at 60 tps × 10× timescale = 600 steps/s per
  instance, 10M ticks ≈ 4.6 instance-hr ≈ **$0.50–5**. The locomotion-actuation lens's learned
  `go_to` is economically trivial; the *full-chamber* PPO resurrection is not. The economics
  draws the exact line the macro boundary drew.
- **Blessed:** compute-to-data. A search-verified solve trajectory costs **< $1** (engine) +
  prior tokens; vs $10–25/hr human. Synthetic expert data is the cheapest data product in §3's
  table — *if* anchors + fast-forward exist, which prices those two infra items precisely.

---

## 5. Council audit: killed, wounded, blessed by the arithmetic

| Verdict | Idea (lens) | Why |
|---|---|---|
| **Killed** | Per-node LLM priors in MCTS (model-frontier, moonshots) | priors are 20–80× node cost; sparse-prior redesign required |
| **Killed** | Full-chamber from-scratch pixel RL (implicit in py/rl resurrection) | 10⁵× transition-cost gap vs its sample complexity |
| **Killed** | "Pitch eval funding as compute" (any lens) | a leaderboard row is $10–85; the ask embarrasses itself |
| **Wounded** | 24/7 stream (moonshots) | viable only post-O(1); $900/mo → $200/mo |
| **Wounded** | Hosted ladder w/o $-caps (benchmark, harness) | organizer exposure to entrants' context policy is unbounded |
| **Wounded** | Lazy-pixel *as cost pitch* (data-flywheel) | storage is noise at pilot scale; reframe as future-proofing |
| **Wounded** | YouTube IDM harvest (moonshots, data) | compute/storage of 15 yr of video is the real bill, not the IDM |
| **Blessed** | O(1) scratchpad agent (model-frontier) | the single biggest $ lever in the eval business: 20×+ |
| **Blessed** | TAS/routing track (benchmark, speedrun) | zero token COGS; pure engine compute at $1e-4/node |
| **Blessed** | Auto-annotation (data, puzzle-gen) | 200–2,000× under human labeling — best unit-econ line owned |
| **Blessed** | .hdem human corpus + backfill (data) | 5–30× and 1,000×+ under teleop respectively, labels included |
| **Blessed** | Learned go_to as locomotion-only RL (locomotion) | $0.50–5 per 10M-tick training run; essentially free |
| **Blessed** | Agent-protocol/inference-only competition (benchmark) | organizer season COGS < prize pool by ~10× (idea 12) |

---

## 6. Ideas

### 1. CostLedger: instrument every dollar at the source — [S]
**What:** One small module threaded through `run_eval.py` + a harness `Stats` read: per-episode
JSON ledger — tokens by modality (already in `TokenUsage`), cache-hit fraction, wall time split
(model latency vs macro execution vs resets), instance-seconds, crash/retry counts, macro ticks.
Written next to the `.trajectory`.
**Why:** Every estimate in §§2–4 marked TBM becomes measured the next time anyone runs anything.
No new runs needed — the instrument rides along.
**Unlocks:** ideas 2, 3, 5, 7; the pitch's numbers slide stops being estimates.

### 2. Reprice the archive: `cost_report.py` over .trajectory + a price-sheet YAML — [S]
**What:** A script that walks any `.trajectory` archive and emits $/episode, $/step, $/solve under
a pluggable price sheet (Flash/Pro/Claude/GPT rows). First deliverable: the first-light run priced
at **$0.22**, in the repo, citable.
**Why:** `TokenUsage` is already stored per call — every historical and future run is repriceable
forever, under any vendor's price changes, with zero re-running. Eval dollars already spent become
economics data for free.
**Unlocks:** ideas 3 and 7; the offline-analysis library (data-flywheel) gains a cost axis.

### 3. The episode cost model: closed-form + crossover curves — [S]
**What:** Write down `In(S)` for each context policy (stateful, cached-stateful, last-N, O(1)),
calibrate constants on the measured transcript (+1.7k/step, 1,064 img tok/frame), and publish the
crossover chart: $/episode vs steps vs policy vs model tier. One notebook, one figure.
**Why:** It converts the context-policy debate from taste to arithmetic: O(1) wins ≥20× at 100
steps *even against perfect caching*; caching alone is worth only ~5×. The figure is the
quantitative backbone for the O(1)-agent decision and the budget-capped competition track.
**Unlocks:** model-frontier's O(1) agent gets its business case; cost-normalized leaderboard
semantics get their formula.

### 4. Instance-hour price discovery: the table everything else consumes — [S-M]
**What:** Measure instances-per-box and per-instance CPU/RAM/GPU on three shapes: the local box
(via `launch_multiple.sh`, scale N until tick-rate degrades), one cloud GPU VM, and the lavapipe
CPU path (piggybacks the harness-platform spike). Output: a 3-row $/instance-hour table with
saturation points, plus watts on the local box.
**Why:** $/instance-hour is the denominator of *every* number in §§2–4 and is currently a guess
spanning 13×. It is also the honest answer to "what does your farm cost?" in any pitch.
**Unlocks:** prices the fleet test, the hosted ladder, the backfill farm, and the search business
with one measurement campaign.

### 5. The crash tax: MTBF, relaunch cost, and the lost-token multiplier — [S]
**What:** From the CostLedger (idea 1) + a soak run: crashes per instance-hour, relaunch wall time
(transcript shows ~6 wait-cycles today), and the *expected lost spend* per crash (an episode dying
at step 20 of stateful chat burns ~$0.18 of context with no result). Output: episode-cost
multiplier `1/(1−p_crash)` per chamber tier.
**Why:** Decides with a number whether harnessd-supervisor (harness-platform) is economically
load-bearing infrastructure or polish — and whether O(1) context (cheap dead episodes) quietly
buys crash *tolerance* too.
**Unlocks:** honest reliability row on the numbers slide; prioritization of the chaos suite.

### 6. BALROG parity sheet: price the embodiment premium — [S]
**What:** Run (or paper-price) one BALROG-style NetHack eval episode with the same model and
compare $/episode and $/row against Portal 2 under O(1) context. Decompose the delta into image
tokens (+1,064/frame), instance time, and crash tax.
**Why:** "3D embodiment at ~2× text-game prices" is a slide; "we never compared" is a hole. If the
multiple is 10×+ instead, better to know before a reviewer computes it for us.
**Unlocks:** cross-model harness positioning; the why-Portal-2 one-pager gets a cost column.

### 7. $/leaderboard-row calculator + a published eval price list — [S]
**What:** A function (suite size, seeds, mean steps, model tier, context policy, instance $) →
$/row, shipped as both a repo utility and a public table in the benchmark docs: "what it costs to
be evaluated." Pair with the budget-capped track definition: caps stated in dollars *and* tokens.
**Why:** Competitions die of organizer-side cost surprises (MineRL retraining). Publishing the
price list *is* the anti-surprise mechanism, and a two-figure $/row is itself marketing: anyone
with a hobby budget can self-evaluate.
**Unlocks:** competition design (idea 12); budget-standardization (benchmark lens) gets numbers.

### 8. Data-product COGS sheet: the teleop comparison table — [S]
**What:** Build §3's table for real: measured .hdem bytes/hr and events/hr from existing
recordings, the three recruitment models priced (volunteer drive / prize pool / paid), backfill
$/hr at measured farm throughput, auto-annotation $/event — beside the public teleop ($60–300/hr)
and contractor-labeling ($0.05–0.50/label) comparators.
**Why:** The Gemini-Robotics data pitch currently says "cheap trajectories" with no number. The
defensible claims are spectacular: 5–30× under teleop for fresh human data, ~1,000× for backfill,
~1,000× for labels — *with better labels*. This table is the data annex's centerfold.
**Unlocks:** P2-Traj v0 release docs; the pilot speedrunner-corpus budget request.

### 9. Lazy-pixel break-even: store vs re-render, by the numbers — [S]
**What:** Measure actual bytes/hr for .dem+.hdem vs rendered rollouts at the resolutions/fps the
consumers use; compute the crossover (cached pixels pay after ~1–6 re-reads at current prices) and
the scale threshold where lazy-pixel becomes load-bearing (~100k hr or 60 Hz).
**Why:** Keeps the architecture decision honest: at pilot scale storage is noise, and pitching
lazy-pixel as 2026 savings invites a reviewer to do this arithmetic against us. As future-proofing
at the 100k-hr regime it's a $9k/mo vs $12/mo argument — a real one.
**Unlocks:** corpus architecture decision made by arithmetic; data-flywheel's canonical-format
choice de-risked.

### 10. $/search-node microbenchmark: price the anchor before building the planner — [M]
**What:** Minimal anchor loop (engine `save`/`load` console path is enough for v0): measure save
latency, restore latency, macro-execution throughput with and without rendering/timescale, and
snapshot read cost. Output: measured $/node, nodes/instance-hr, and the prior-to-engine cost ratio
(estimated 20–80×) as a design constant.
**Why:** Three lenses propose MCTS/beam/best-of-N on unmeasured save/restore latency. If restore
is 3 s the planner's shape is fan-out-across-instances; if 100 ms it's deep trees on one instance.
A two-day measurement determines the architecture of a months-scale workstream — and the
prior-domination ratio kills per-node-LLM designs before anyone builds one.
**Unlocks:** every search idea (speedrun-routing, model-frontier, eval-science MCTS-oracle);
prices compute-to-data (<$1/verified trajectory) for the data business.

### 11. The kill-list audit: run every council idea through the model — [S]
**What:** Formalize §5 into a maintained one-pager: each council idea gets a COGS line, a verdict
(killed / wounded / blessed), and the parameter its verdict is most sensitive to. Re-run when the
price sheet or a TBM measurement changes.
**Why:** This is the converge-phase's economic input — without it, prioritization re-litigates
taste. With it, "should we build per-node priors?" is answered by a ratio, not a meeting.
**Unlocks:** the roadmap's next prioritization pass; the pitch's risk register gets cost-grounded.

### 12. Competition season P&L: prove the trophies cost more than the eval — [S]
**What:** Price one NeurIPS-track season under the agent-protocol (inference-only, entrants pay
their own tokens): 50 entries × 100 hosted episodes ≈ ~850 instance-hr ≈ **~$100–300** of game
compute + held-out authoring + human verification hours, vs a $10–20k prize pool. One-page P&L
with sponsor ask.
**Why:** "Organizer COGS ≈ 1–3% of prize pool" is the inversion of every dead competition's
budget, and is only credible written down. It also sets the sponsorship ask at the right altitude:
fund prizes and authoring, not servers.
**Unlocks:** NeurIPS proposal package (benchmark lens) gets its budget section pre-written.

### 13. The marginal-claims register: every "near-zero" gets a number or dies — [S]
**What:** Grep the pitch and council docs for every cost claim ("near-zero marginal", "free",
"cheap at scale"); register each with: the number, the measurement that produced it (or TBM +
owning idea), and the hidden cost it elides (e.g. "human data ≈ $0 marginal" elides recruitment;
"infinite chambers free" elides compile+probe $/chamber — price that too: ~1 instance-min ≈
$0.002/chamber probed).
**Why:** A VP's DD team will do exactly this pass. Doing it first converts the pitch's weakest
sentence class into its most-audited one. KISS: it's a markdown table, not a system.
**Unlocks:** pitch hardening; doubles as the acceptance checklist for ideas 1–10.

### 14. Fast-forward ROI: spend throughput engineering only where wall-clock binds — [S]
**What:** A half-page decision memo from the measured numbers: VLM eval wall time is
model-latency-bound (fast-forward buys <2×, skip), backfill farm is engine-bound (buys 10–50×,
build), search is engine-bound (buys 10–50×, build), locomotion RL is engine-bound (buys 10×,
build). Sequence the harness-platform throughput program accordingly.
**Why:** The throughput program is M-weeks of work; this memo aims it at the two businesses where
it changes $/unit by an order of magnitude instead of the one where it changes nothing. Economics
as a prioritization function, not a slide.
**Unlocks:** correctly-ordered throughput backlog; backfill and search business cases activate.

---

## Spiciest take

The eval business — the thing first light just made famous — is the *worst* business of the
three, economically: a full leaderboard row costs $10 and a 10-model refresh costs less than a
conference registration, which means eval is a credibility product, not a compute product, and
"fund our evals" is dead on arrival. The arithmetic says the moats are inverted from the demo:
**auto-annotated human trajectories at ~1,000× under teleop-and-labeling, and engine search nodes
at $1e-4 where the LLM prior — not the physics engine — is 20–80× the node cost.** The pitch
should sell the two businesses where the unit economics are obscene (data, search) and give the
eval away as marketing — and the single highest-ROI engineering act in the entire council is the
O(1) context change, which is simultaneously the eval's 20× cost lever, the stream's go/no-go,
and the thing that makes every trajectory a single-turn training example. The numbers don't
support "fund the benchmark." They support "the benchmark is the free sample; the factory is
data and search."
