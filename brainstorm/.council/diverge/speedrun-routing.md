# Lens: speedrun-routing

Routing (find the route) vs execution (hit the ticks) — orthogonal axes, both sitting on infrastructure that mostly already exists in this repo. Key asymmetry nobody else's lens will state: **upstream SAR is not neutral substrate, it is ten years of speedrun-community tooling** — a TAS player with framebulks and an autostrafer (`src/Features/Tas/`, `TasTools/StrafeTool.cpp` with `AutoStrafeType`), analytic glitch finders (`src/Features/Routing/SeamshotFind.cpp`), RNG pinning (`src/Features/RNGManip.cpp` saves/restores view punches + random seeds), demo parsing and ghost playback (`src/Features/Demo/`), Challenge Mode timing + leaderboard auto-submit (`ChallengeMode.cpp`, `AutoSubmit.cpp`), and a routing visualizer (`PlayerTrace.cpp`). The fork added the missing half: a pausable, frame-stepped, instrumented simulator. Routing-as-search is the cheapest "wow" left on the table.

One design note up front: the LLM-eval track's "no global pathfinding" principle is correct **for that track** (failure attribution). The speedrun-routing track inverts it: here finding paths IS the task, so search, pathfinding, and savestates are not cheating — they're the method. Same harness, different epistemic contract. Keep the two tracks' rules separate and both stay clean.

---

## Ideas (ordered roughly by leverage)

### 1. Challenge Mode timer as the universal oracle + score
**What:** Wire SAR's existing `ChallengeMode.cpp`/`SpeedrunTimer` into the harness: expose `cm_time_ticks`, `portal_count`, and `chamber_complete` in `GameState`. CM is Valve's built-in per-chamber speedrun mode with its own start/finish rules — the engine already knows when a chamber is done.
**Why:** Kills the #1 admitted limitation (hand-passed exit coords don't scale) AND gives every routing/search idea below its objective function (ticks-to-complete) for free. One hook, two workstreams unblocked. Bonus: board.portal2.sr holds ~10 years of human CM times per chamber — instant human baselines with zero eval design.
**Effort:** S–M (SAR's hooking patterns make the level-end/CM-timer hook routine; plumbing to proto is mechanical).
**Depends on:** nothing. **Unlocks:** chamber suites, all search ideas, leaderboard comparisons.

### 2. Engine-verified route search v0: anchors + best-of-N macro rollouts
**What:** Build C9 save/load anchors (~40 LOC per the roadmap), then the dumbest possible router: sample N macro-sequences (LLM at temperature, or scripted enumeration over marks), execute each from the anchor in the frozen-stepped engine, keep the fastest that completes. Re-execute the winner deterministically and record it.
**Why:** "LLM proposes, engine verifies" — the first routing *result*, weeks not months. The world-freeze + condvar tick gating means the engine is already a pausable simulator; this is the cheapest way to demonstrate that as a search substrate, and it's the seed of every fancier planner.
**Effort:** S–M.
**Depends on:** C9 anchors; idea 1 for the score. **Unlocks:** ideas 3, 7, 12, 16, 20.

### 3. Macro-level MCTS/beam router
**What:** Upgrade idea 2 to real tree search: nodes = engine save anchors, edges = macro verbs over live marks, rollout policy = LLM or heuristic, value = ticks + distance-to-exit. Marks are save/load-invariant (keyed on entity index+serial), so the action space survives restores. Budget-aware: ~100ms per save/restore is irrelevant in a turn-based loop.
**Why:** This is the "routing agents as generally-useful planners" pitch made concrete: a planner whose simulator is a AAA physics engine and whose action space is semantic. Also directly produces the route library that execution polishing (idea 16) consumes.
**Effort:** M (the search loop is plain Python; the engine side is idea 2's anchors + existing AgentLoop).
**Depends on:** ideas 1, 2. **Unlocks:** route libraries, novelty detection, the two-layer speedrun agent.

### 4. Mine board.portal2.sr: an action-labeled human route corpus
**What:** The community CM leaderboard requires demo upload for top times — thousands of `.dem` files across ~100 chambers, spanning a decade, sorted by skill (rank = label). Batch-ingest via the existing `render_demos.py` farm + `RenderDemo` RPC (which already reconstructs per-tick actions from demo `CUserCmd`s via the `Client.cpp` hook) → `.rollout`s with pixels, entity state, and inferred actions.
**Why:** This is the AlphaStar-replays / VPT-seed move: the highest-skill embodied first-person data that exists for this game, already public, already action-bearing (unlike YouTube). Route clusters per chamber = human route priors; rank stratification = skill curriculum; it's also the strongest "trajectories for Gemini Robotics" exhibit because expert motion is the scarce commodity.
**Effort:** M for a few chambers end-to-end (scrape→render→cluster); L for full-board scale (rendering is ~1× realtime, parallelized across instances). Legal note: demos are user uploads on a community board — get p2sr's blessing (they maintain SAR upstream; relationship already exists).
**Depends on:** nothing in-repo (farm exists). **Unlocks:** ideas 5, 13, 14, 19; the data-flywheel pitch.

### 5. `sprint_to`: time-optimal locomotion macro from the existing autostrafer
**What:** Wrap upstream `StrafeTool` (vectorial autostrafing — the community's own optimal-air-movement synthesizer) + `AutoJumpTool` as a harness macro: engine-executed, time-optimal point-to-point movement including bhop/airstrafe physics, with the same guard/result-code contract as `go_to`.
**Why:** Attacks the first-light locomotion wall with speedrun tech instead of nav-meshes, and doubles as the execution layer for routing (a route compiled to `sprint_to` segments is already half a TAS). Philosophically clean for the eval track too: the engine owns *local* optimality, the model still owns *where*.
**Effort:** M (the hard math is already written and battle-tested by TASers; wiring + guards + result codes is the work).
**Depends on:** none. **Unlocks:** ideas 9, 16; faster evals everywhere.

### 6. Fast-forward simulation mode (the throughput multiplier)
**What:** A harness "search mode": `mat_norendering 1`, uncapped `fps_max`/`host_framerate`, skip SHM pixel copies, batch tick advances — the designed-but-never-built 3-tier fast conversion, repurposed for search rollouts. Upstream `sar_tas_skipto` already fast-forwards TAS playback the same way; steal its mechanism.
**Why:** Every search idea's iteration count is gated on sim speed; 10–50× here multiplies ideas 2, 3, 7, 12, 19 directly. Also revives the dormant PPO stack's wall-clock story and the demo-ingestion farm (idea 4).
**Effort:** M (the engine fights you on some of this; budget for weirdness — the perf-history doc proves this codebase punishes optimism).
**Depends on:** none. **Unlocks:** everything iterative.

### 7. Tick-level savestate brute-forcer ("BizHawk for Source")
**What:** A search server RPC: `load anchor → apply input window (framebulk deltas) → advance K ticks → report metric (speed, position, trigger-touch time)`. Drive it from Python with coordinate descent / CEM / random restarts over small input windows (segment boundaries, jump ticks, strafe angles). This is exactly how console TAS communities optimize segments, mechanized.
**Why:** This is the *execution* half of superhuman: routing picks the segment sequence, this polishes each segment to tick-perfection. It's also the first tool in this list a human TASer would actually adopt today (they currently iterate by hand with `sar_tas_skipto`).
**Effort:** M–L (RPC + optimizer is M; making save/load fast and honest is the L tail — see idea 15).
**Depends on:** ideas 1, 6, 15. **Unlocks:** ideas 12, 16, 18, 23.

### 8. Chamber route graph extraction (+ portal edges)
**What:** Offline: parse the chamber's BSP/PeTI voxel grid into a coarse reachability graph — nodes = walkable regions + marks, edges = {walk, fall, portal-pair teleport, fling}. The novel piece is the portal edge generator: enumerate portalable surface pairs (the annotation reticle logic already classifies placement validity) and add teleport edges with momentum transforms.
**Why:** Turns routing into explicit graph search a planner can attack without the engine in the loop (engine = verifier of candidate routes). For PeTI chambers the .p2c voxel format makes extraction nearly trivial — this is the scalable-to-Workshop version of routing.
**Effort:** L (geometry grind; PeTI-only scope keeps it sane — Hammer maps are P1).
**Depends on:** none (better with idea 9). **Unlocks:** ideas 16, 17; workshop-scale routing.

### 9. Analytic fling solver
**What:** A small ballistics module: given entry-portal pose, exit-portal pose, gravity, and the engine's speed behavior, compute the reachable set / required entry speed to land at a target. Inverse mode: given two marks, propose portal placements + approach that connect them. Validate predictions against the engine (idea 7's RPC) on a battery of known flings.
**Why:** Flings are *the* signature Portal 2 route move; an analytic edge generator makes momentum routes searchable instead of stumbled-upon, and "physics calculator validated against a real engine" is independently publishable/pitchable (world-model calibration story).
**Effort:** M.
**Depends on:** none; consumed by ideas 3, 8. **Unlocks:** momentum routing, half of the interesting glitch space.

### 10. Ghost racing: human WR as dense reward and legible milestone
**What:** Use existing `DemoGhostPlayer` to play a human demo as a ghost in the agent's session; expose ghost position in telemetry. Dense signal = signed time-delta to ghost at matched route-progress; milestone = "beat the ghost."
**Why:** Solves the sparse-reward problem that killed the PPO stack (its reward function is described in-repo as a confession), using human data instead of hand-shaped penalty patches. "Agent racing the world record live" is also the single most spectator-legible artifact this project can produce — Game Arena energy.
**Effort:** M (ghost machinery exists; progress-matching metric is the design work).
**Depends on:** idea 4 for demos (or any owned demo). **Unlocks:** execution RL that learns, demo-day material.

### 11. Glitch regression suite: known exploits as `.p2tas` unit tests
**What:** Transcribe ~10 canonical glitches from the p2sr wiki (crouch-fling, edge glitch, seamshot, reportal, save/load abuses…) into `.p2tas` scripts + post-condition assertions (position/speed after N ticks), runnable headless via the harness.
**Why:** Three birds: (a) regression tests for the harness/TAS layer; (b) ground-truth validation set for glitch *discovery* (idea 12 — can search rediscover these?); (c) demonstration data for execution RL. Also forces the determinism question (idea 15) into the open early.
**Effort:** M (mostly speedrunner-knowledge transcription; recruit one community TASer — they already use SAR).
**Depends on:** none. **Unlocks:** ideas 12, 15, 19.

### 12. Glitch discovery as novelty search
**What:** From dense anchors along known routes, run perturbation search (random + CMA-ES + novelty bonus) over short input windows, with anomaly detectors as the interestingness signal: teleport-scale displacement, speed above cap, OOB-while-alive (idea 21), trigger fired without prerequisite, height gained without surface. Triage hits into a human-reviewable gallery (trajectory viewer already exists).
**Why:** Glitch discovery = exploration research with an economically legible cover story ("automated exploit discovery in a shipped binary"). One genuinely new glitch found by search would be a community-news event and a pitch slide that writes itself. Honest framing: rediscovery of idea 11's suite is the publishable result; *new* glitches are the lottery ticket.
**Effort:** L.
**Depends on:** ideas 6, 7, 11, 21. **Unlocks:** the "move 37" narrative.

### 13. Route-novelty detector
**What:** Embed per-chamber trajectories (position polylines → DTW distance or learned embedding), cluster the human corpus (idea 4) into route families, and flag agent routes that are (a) far from every human cluster and (b) competitive on time.
**Why:** Converts "the agent found a route" into a measurable claim ("outside all known human route families, 2.3s faster than cluster-best"). This is the difference between an anecdote and a result.
**Effort:** M.
**Depends on:** ideas 3/2 (agent routes), 4 (human routes). **Unlocks:** headline claims with error bars.

### 14. Skill-stratified imitation: rank-conditioned BC from the CM corpus
**What:** `.hdem`/rollout-ified leaderboard demos carry their rank/time; train rank-conditioned behavioral cloning (VPT-style architecture, or fine-tune at macro altitude from re-projected percepts) so one model spans novice→WR motion, steerable by a skill token.
**Why:** Skill-conditioned motion priors are exactly what SIMA-style agents lack and what robotics transfer wants (the corpus is *graded* by outcome, which teleop data never is). Also gives the routing search a learned rollout policy.
**Effort:** L.
**Depends on:** idea 4 at scale; `.hdem` v2 actions help. **Unlocks:** execution policies, the data-product pitch.

### 15. Determinism contract + RNG audit
**What:** Characterize, empirically, what survives save/load and replay: extend `RNGManip` (already pins view punches + random seeds) with a battery — N replays of fixed input scripts across dropper spawns, prop physics, paint blobs — and publish a table: *pinnable / bounded-divergence / chaotic*. Add a harness "pinned mode" enabling every available pin.
**Why:** The p2sr TAS wiki admits airborne props desync across replays; every search idea above silently assumes replayability. This doc is the honesty layer that keeps "tick-perfect" claims defensible — and the divergence table is itself interesting (it quantifies how non-deterministic a shipped AAA engine really is, relevant to the world-model-calibration pitch).
**Effort:** S–M.
**Depends on:** none. **Unlocks:** trustworthy versions of ideas 2, 3, 7, 12, 18.

### 16. Two-layer speedrun agent: router → `.p2tas` compiler → polisher
**What:** The full pipeline: macro route (ideas 2/3) → compile to a `.p2tas` skeleton (segments using `autoaim`/`strafe` tool annotations — the TAS script format already supports them) → segment-wise optimizer (idea 7) tightens tick counts and angles → final deterministic replay + demo recording.
**Why:** This is the artifact: *an agent-authored TAS of a real chamber, in the community's own format, verifiable by their own tools*. It cleanly demonstrates the routing/execution orthogonality the lens is named for, end to end.
**Effort:** L (each stage exists or is M above; integration and the compiler are the work).
**Depends on:** ideas 1, 2/3, 5, 7, 15. **Unlocks:** idea 23, leaderboard submissions (TAS category), the pitch centerpiece.

### 17. Routing-as-planning benchmark export
**What:** Serialize chamber route graphs (idea 8) + macro-level dynamics into engine-free planning instances (JSON graph + transition rules + tick-cost estimates), with the harness as the ground-truth verifier for submitted plans. Publish as a benchmark slice.
**Why:** Lets the classical-planning / search community attack Portal 2 routing without owning the game — widens the funnel beyond RL/LLM people, and the Demaine PSPACE-completeness result gives the instances formal teeth. Verifier-backed benchmarks are contamination-resistant by construction.
**Effort:** M (given idea 8).
**Depends on:** idea 8. **Unlocks:** external contributors, competition track C ("routing").

### 18. Automated demo splicing
**What:** Segment library from demos/TAS runs cut at low-velocity anchor points; search over splice compositions whose boundary states match within tolerance; repair small mismatches with local optimization (idea 7). Output: composite runs faster than any source run.
**Why:** Splicing is what human TASers do by hand and what the demo corpus makes combinatorial: 1000 demos × per-segment best = a "theoretical best" assembly. It is also a real (mildly spicy) contribution to TAS methodology.
**Effort:** L (state-matching across Source's partial determinism is the hard part).
**Depends on:** ideas 4, 7, 15. **Unlocks:** corpus-derived superhuman segment times.

### 19. Route-waypoint curriculum for the dormant PPO stack
**What:** Resurrect `py/rl/` with one change: replace the hand-confessed reward patchwork with waypoint progression along a known route (from ideas 3/4), i.e., dense potential-based shaping derived from routing, not hand-tuning. Fix the bit-rotted launch flags while there.
**Why:** Cheapest test of "routing output makes execution learnable" — and it gives the RL stack a reason to exist again as the *execution polisher* (and eventually the neural `sprint_to`). The two tracks finally compose instead of competing.
**Effort:** M–L (stack is dormant but real; known algorithmic nits are documented).
**Depends on:** routes from 2/3/4. **Unlocks:** learned execution, hybrid neural/TAS polishing.

### 20. Routing copilot for human runners
**What:** Ship the routing toolkit *to* the community: fling solver overlay (idea 9), route-graph visualization, seamshot percept (idea 22), "what connects to what" queries — as SAR cvars/HUD, the surface they already live in. Quid pro quo: an opt-in `sar_harness_record 1` so routing sessions produce `.hdem`.
**Why:** The community is the data flywheel and the QA department: tools-for-trajectories is a trade they'll take, and adoption makes the platform claim ("SAR is the substrate") true rather than aspirational. ViZDoom's lesson — the platform outlives everything — argues for investing here early.
**Effort:** M.
**Depends on:** ideas 8/9 partially. **Unlocks:** organic `.hdem` corpus, community goodwill, testers.

### 21. Out-of-bounds + anomaly telemetry
**What:** Expose `player_oob` (outside playable space via engine leaf/vis or trace checks), `speed_over_cap`, and per-tick displacement spikes in `GameState`.
**Why:** Tiny, but it's the sensor layer for glitch search (idea 12), a guard for normal evals (BLOCKED vs "you clipped through the wall" are very different failures), and a labeling signal for the demo corpus (runs containing OOB segments = glitch-route exemplars).
**Effort:** S–M.
**Depends on:** none. **Unlocks:** ideas 12, 13 labeling.

### 22. SeamshotFind (and friends) as percepts
**What:** Surface upstream analytic routing tools through the harness: seamshot locations as marked percepts/graph edges, `PlacementScanner` results, `StepSlopeBoostDebug` hints.
**Why:** Free glitch-route edges computed analytically by code the community already trusts; also a model for the general pattern "ten years of speedrun tooling → percepts."
**Effort:** S.
**Depends on:** none. **Unlocks:** glitch edges in ideas 3/8.

### 23. The exhibition: Human WR vs community TAS vs Agent
**What:** Pick 3–5 CM chambers with deep leaderboard history. Produce a single artifact (web page in the house style of the trajectory viewer): three synchronized runs per chamber — human WR demo, community TAS, agent run (idea 16) — with times, route maps (idea 13 clusters), and the agent's decision trace.
**Why:** This is the pitch deck's centerpiece slide and the public moment. Even "agent within 20% of human WR, via a different route" is a strong result when the route map shows *why*; "agent beats the TAS" is the moonshot framing that gets headcount.
**Effort:** S–M as packaging once 16 exists; the dependency chain is the real cost.
**Depends on:** ideas 1, 4, 13, 16. **Unlocks:** the VP meeting.

### 24. Full-campaign routing (horizon marker)
**What:** Route the entire SP campaign: chamber-graph + elevator transitions, per-segment anchors, category rules from `SpeedrunTimer` (it already encodes them). Long-horizon sequential planning over ~9 chapters.
**Why:** Named mostly to scope the ladder honestly: per-chamber routing (ideas 2–16) is the fundable unit; full-game is the multi-person, multi-month flag at the top — i.e., the headcount ask.
**Effort:** XL (genuinely needs the team the pitch is asking for).
**Depends on:** everything above. **Unlocks:** the "speedrun the whole game" claim.

---

## Spiciest take

The LLM percept/act track is the *demo*; speedrun-routing is the *moat*. BALROG-style frozen-VLM evals will be commoditized within a year — every lab can run one, and frontier models will saturate stock chambers the way they saturated text mazes. What no lab can casually replicate is what's already sitting in this repo: a deterministic-enough AAA physics engine with savestates, a tick-perfect actuation layer, a decade of action-bearing expert demos behind a community leaderboard, and analytic glitch oracles — i.e., everything needed for an agent to *find a route humans missed and execute it tick-perfectly*. That's not a benchmark result, that's a move-37 moment in embodied 3D, and Portal 2 is the only game where the verifier, the human baseline archive, and the search substrate already live in one codebase. The eval track gets you the meeting; the routing track gets you the headcount.

## If I could only do ONE thing next week

Build C9 save/load anchors + hook the Challenge Mode timer into `GameState`, then run best-of-N macro-sequence search on testchamber_000 and one real CM chamber (ideas 1+2).
It's days of work on existing seams, it kills the exit-oracle limitation as a side effect, and it produces the first *engine-verified, time-scored, agent-found route* — the seed artifact every other idea in this lens (MCTS, polishing, novelty detection, the exhibition) grows from.
