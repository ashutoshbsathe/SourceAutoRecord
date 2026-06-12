# Diverge Catalog — the idea space, consolidated

*2026-06-12, the day after first light. This is the DIVERGE artifact of the brainstorming
council: every distinct idea from 10 lens passes + 7 gap passes + 2 critique passes,
deduped (merges noted) but deliberately NOT pruned for quality — pruning is the next
(converge) phase. Seed doc: [first_light_and_next_steps.md](first_light_and_next_steps.md).
Raw council output indexed at the bottom. I will live in this doc for months; treat the
theme letters + numbers (A1, B2, …) as stable IDs.*

**Effort key** (one RE + agentic coding): **S** = days · **M** = weeks · **L** = months ·
**XL** = needs a team. Where calendar time dominates RE-hours (emails, community), both are noted.

---

## Map of the territory (the flywheel)

Thirteen themes. The arrows are the flywheel:

```
            A  Ground truth & terminals  ──────────────────────────┐
            │  (oracle, DIED, lifecycle — makes anything scorable) │
            ▼                                                      │
  E  Chambers & generation ──► D  Eval science ◄── C  Agent loop   │
     (suite, puzzlegen,           (controls, stats,    (O(1) ctx,  │
      workshop, held-out)          attribution)         x-model)   │
            │                        │     ▲                       │
            ▼                        ▼     │                       │
  B  Locomotion & actuation ──► more SOLVEDs ──► F  Data flywheel ◄┘
     (the legs; the #1 lever)                    (.hdem v2, RLDS,
            │                                     corpora, annexes)
            ▼                                          │
  G  Search & routing  ◄── anchors ──►  C12-14 train/distill loops
     (routes, TAS, glitches = compute→data)            │
            │                                          ▼
  H  Audio & real-time ── keep the embodiment claim honest
  I  Safety & faithfulness ── keep the *readouts* honest
            │
            ▼
  J  Platform & ops ──► K  Benchmark & competition ──► L  Co-op & spectacle
     (container, CI,       (kits, ladder, NeurIPS)       (the moats nobody copies)
      determinism)                  │
                                    ▼
            M  Pitch, demand, economics & humans
               (the part that turns artifacts into allies, money, headcount)
```

How it spins: **A** makes success non-hallucinatable → **E** supplies unlimited scorable
tasks → **C** makes running models on them affordable → **D** attributes every failure to
perception/reasoning/actuation → **B** converts actuation failures into solves → every run
and every human play feeds **F** → data trains actuators (B21) and distilled agents (C14) →
**G** converts raw compute into routes and verified expert trajectories (more F) → **H/I**
keep the whole instrument honest against the two attacks (reality doesn't pause; traces may
lie) → **J/K** let people who aren't me run all of it → **L** is the differentiated headline
nobody can fork → **M** converts the lot into a funded program. The single most repeated
finding across all 17 passes: *A1 (the exit oracle) is upstream of literally everything*,
and the gap lenses' counterweight: *the council is supply-side; calendar, allies, and demand
evidence (M-theme) are the binding constraints, not code.*

---

## A. Ground truth, terminals & death

*The keystone theme. Merged from: benchmark#1, eval-science#1–2/19, harness#1, speedrun#1,
model#2/18, gap-4 part B. Five lenses independently ranked the oracle first.*

- **A1. `chamber_complete` engine oracle in GameState** [S] — Hook the PeTI level-end path
  (`@relay_pti_level_end` / `trigger_changelevel` / `changelevel`) via SAR's standard hook
  patterns; emit a bool in `GameState`; fall back to an exit-door-entity heuristic for
  Hammer maps. Kills the hand-passed `--exit x,y,z --radius` that caps the project at ~5
  chambers and gives every suite, leaderboard, search loop, and RL reward a
  non-hallucinatable success bit. Recon first with `sar_harness_dump_fields diff` —
  the completeness critic notes nobody has actually verified the hook point yet.
  *Deps: none. Unlocks: nearly everything in this document.* (merged: benchmark, eval,
  harness, speedrun, puzzle-gen, moonshots)
- **A2. Challenge Mode timer as oracle + tick score** [S–M] — Wire upstream
  `ChallengeMode.cpp`/`SpeedrunTimer` into the harness: `cm_time_ticks`, `portal_count`,
  completion per Valve's own per-chamber speedrun rules. Same hook family as A1 but adds
  the routing track's objective function (ticks) for free, plus ~10 years of human CM
  times on board.portal2.sr as instant baselines. (speedrun#1)
- **A3. Terminal taxonomy v2 + loop detector with injected feedback** [S] — Split `BUDGET`
  into SOLVED / DONE_WRONG / LOOP / STUCK / BUDGET / GAVE_UP / CRASH (+ A4/A7's DIED and
  IRRECOVERABLE). Client-side `(percept-hash, action, result)` loop detector: on k-th
  repeat, inject "you tried `go_to 8` twice, both BLOCKED — do something else" via the
  existing rejection channel; after k+m, terminate `LOOP`. First light's steps 8–24 were a
  loop masked as BUDGET; the hint-vs-no-hint arm is itself a self-correction ablation.
  (merged: eval#2, model#2, first-light §5e)
- **A4. DIED terminal + cause-of-death telemetry** [S] — Watch `health → 0` / hook
  `player_death`; every march loop in `MacroExecutor.cpp` aborts with `DIED` (today they
  happily keep driving a silently-respawned corpse); attribute GOO/TURRET/LASER/CRUSH/FALL
  + killer's mark. **Must ship before the first M3 hazard chamber** or that data is
  garbage by construction. (gap-4 B1)
- **A5. Respawn policy as a versioned eval rule** [S] — `terminal` (science default) /
  `lives:N` (leaderboard, deaths injected into the percept and scored) / `free` (data
  collection). The harness must suppress Portal 2's silent auto-respawn under `terminal`.
  Without it two runs of the same chamber aren't comparable. (gap-4 B2)
- **A6. Entity-lifecycle events + mark tombstones** [S] — Promote the snapshotter's
  per-tick memcmp knowledge to percepts: `FIZZLED(mark, by)`, `DESTROYED`, `SPAWNED(from
  dropper, replaces=11)`; dead marks tombstoned (never reused in-episode), respawns carry
  lineage. Fixes the percept-side gaslighting (cube 11 vanishes silently) and is the
  mark-stability invariant save/load anchors (G1) need anyway. (gap-4 B3)
- **A7. IRRECOVERABLE oracle v0 (manifest-scoped)** [M] — General unsolvability detection
  is PSPACE-hard; don't build it. The chamber manifest declares a resource model (cube
  feeding button B; respawner exists y/n); a rule over A6's event stream emits
  `IRRECOVERABLE` the moment a goal consumable dies with no respawner. Separates "the
  model destroyed its own resources" from "needed more steps" — a failure class no
  benchmark currently has. (gap-4 B4)
- **A8. Engine-side hazard guard** [S–M] — Extend `go_to`'s WALL/EDGE guards with
  lethality: goo ahead (`CONTENTS_SLIME` trace), live laser beams, turret fire cones →
  stop with `HAZARD_AHEAD(kind, mark)`; explicit grammar override `accept:hazard` for
  intentional risk. A model should never die because the actuator couldn't see goo —
  that death measures nothing. Ship the goo trace first; it's the M3 killer. (gap-4 B5)
- **A9. `done`-precision battery + confidence calibration** [S] — 3–4 trap chambers (door
  opens but exit beyond it; decoy door; lying "complete" announcement) scoring
  premature-done and missed-done against A1; require a `confidence: 0-100` field per
  action and plot the calibration curve. `done` has never fired in a real run — the verb
  that defines episode semantics is untested. (merged: eval#19, model#18)
- **A10. Oracle-disagreement meta-eval** [S] — Keep the exit-radius oracle as a
  cross-check and measure oracle-vs-oracle disagreement on the first 10 chambers (one
  evening). Validates the instrument itself; also exposes the latent 2D-told/3D-judged
  distance bug from first light. (eval#1 sub-idea)
- **A11. Timed-element audit + `requires_realtime` manifest flag** [S] — Classify every
  stock PeTI element as clock-bearing (pedestal timer, track platform, turret tracking)
  vs clock-free; refuse to score clock-bearing chambers in pause mode (Zeno's agent can
  legally freeze the timer today — a benchmark-integrity bug against our own v0 scope).
  (gap-4 A6)
- **A12. Death × anchors semantics** [S] — Written rule: eval tracks never restore
  (death is DIED, full stop); search/data modes treat death as a pruned branch and emit
  near-death counterfactual pairs (identical anchor, lethal vs surviving continuation) —
  the starkest DPO labels the flywheel produces. *Deps: G1.* (gap-4 B6)
- **A13. Mortality metrics + risk calibration** [S–M] — Deaths-per-solve, death-cause
  histograms, self-inflicted-unsolvability rate, and an optional `risk:{none,low,high}`
  annotation graded against outcomes: does the model know when it might die? Ground-truth
  embodied risk calibration is something neither text benchmarks nor (ethically) robot
  labs can measure. (gap-4 B7)

---

## B. Locomotion & actuation — fix the legs

*First light's verdict: reasoning solved, locomotion is the wall. This theme owns the wall.
Source: locomotion-actuation lens; merges from data-flywheel, moonshots, speedrun.*

- **B1. Nav-mesh recon: is Source's `CNavMesh` alive in Portal 2?** [S, timebox 1–2 days]
  — `sv_cheats 1; nav_generate` in a PeTI map; check for `.nav` output and live queries.
  Could collapse B2 from "build a planner" to "call the engine's planner"; even a negative
  result justifies the custom grid in the writeup. Do this first. (loco#5)
- **B2. Trace-sampled walkability grid + A\* `go_to`** [M] — On-demand lattice (~24u
  pitch) via the already-wired `engine->Trace`; A\* over the 8-connected grid; waypoints
  feed the existing march loop. Encodes *walkable-NOW* space only — a closed door is a
  wall, no portal edges — so it formally cannot smuggle puzzle-solving into the actuator
  (Demaine hardness lives in topology *changes*). Fixes ~all of first light's steps 8–24.
  The named #1 lever of the whole project. (loco#1)
- **B3. Failure surface v2: BLOCKED becomes a percept** [S] — Attach blocker mark/class,
  hit distance/bearing, a 16-ray free-space fan, and (for A\* fails) nearest-reachable
  point to every failed MacroResult. The model recovers well when it understands failures
  (step 12 proved it); "BLOCKED" with no referent forced 16 steps of blind probing.
  (loco#2)
- **B4. Auto-approach `pick_up` + per-class stand-point resolver** [S] — `PickUp` on
  OUT_OF_REACH internally runs GoTo first (the composition `Interact` already uses);
  replace naive OBB-center targets per class: button → front stand point, door → near/far
  threshold, cube → adjacent (walking *to* a cube's center is how you punt it — step 12's
  bump). Cheapest real win in the theme. (loco#3)
- **B5. LocoGym: locomotion-only suite + LocoScore CI gate** [S–M] — 10–20 zero-puzzle
  chambers (the first-light glass enclosure verbatim as test #1, U-maze, stairs, 128u
  drop, doorway-while-carrying); scripted agent issues only `go_to <exit>`; LocoScore =
  success + ticks vs human `.hdem` baseline, run on every actuator change.
  Operationalizes "a locomotion failure is an engine bug, never charged to the model."
  (loco#4)
- **B6. Engine-truth held state (`held_mark`)** [S] — Recon the player's grab-controller
  field with `sar_harness_dump_fields diff` while grabbing/dropping; surface in
  GameState. Kills the documented client-side desync (release that silently grabs;
  physics knocks the cube loose unnoticed). (loco#6)
- **B7. Carry-aware locomotion** [S–M] — Add the held entity as a pass-entity in
  `CheckGuard`'s trace filter (today your own cube can trip the WALL ray — a probable
  live bug), widen doorway clearance for player+cube, report `BUMPED(mark)` on mid-march
  contact. "Carry cube through doorway" is *the* canonical Portal act. *Deps: B6.*
  (loco#7)
- **B8. Vertical policy: safe descent, `jump`, `jump_to(mark)`** [S–M] — Replace the
  blanket 64u EDGE refusal with floor-vs-death tracing (long-fall boots make almost all
  descent survivable). Today the actuator cannot go DOWN 65 units; most multi-level PeTI
  is unwalkable regardless of pathfinding. (loco#9)
- **B9. Topology-aware nav edges: through portals and open doors** [M] — Dynamic graph
  edges from snapshot state: open door = threshold-to-threshold edge; linked portal pair
  (`m_hLinkedPortal` already snapshotted) = zero-length edge. The crisp altitude line:
  *the model changes topology; the engine traverses whatever topology exists.* *Deps:
  B2, B10.* (loco#10)
- **B10. `place_portal(color, mark|point)`** [M] — Compose visible-point aim (B11) + the
  portalable-surface predicate HarnessAnnotate already computes + upstream
  `TasTools/ShootTool`. Failure surface: NOT_PORTALABLE / NO_LOS / SURFACE_TOO_SMALL.
  It's the game's name; every chamber past tier 1 needs it; all three ingredients exist —
  assembly, not research. (loco#11)
- **B11. Visible-point aim: `aim_at` LoS refinement** [S] — LoS-trace to OBB center; on
  occlusion sample the 9 canonical OBB points; report `AIM_OCCLUDED` + offset. Hard
  prerequisite for B10; fixes silent aim failures on partially occluded targets. (loco#19)
- **B12. Interruptible macros: event-triggered early return** [M] — Long macros are blind
  for up to 400 ticks; abort with `INTERRUPTED` + detail when a watched entity status
  flips mid-macro or the held object detaches. Turns "surprise next frame" into "alert in
  the result." Shares its state-machine refactor with the real-time mode (H12) and the
  HEARD interrupt (H7). (loco#12)
- **B13. Nav-compass percept (16-ray free-space rose)** [S] — Per-observation walkability
  rays as JSON (+optional overlay arcs). The scientific control for B2: arm A = dumb
  actuator + nav percept (model paths itself), arm B = smart actuator. "Where should
  navigation live — percept or actuator?" becomes a measured question. (loco#8)
- **B14. `go_to point(bearing, dist)` mark-less waypoint** [S] — Spatial fallback target
  using the proto's reserved `Vector3 point`. Insurance while B2 lands; afterwards the
  escape hatch when A\* says UNREACHABLE but the model sees a path; the natural target
  vocabulary for B13 headings. (loco#21)
- **B15. The altitude ladder, executable: L0 framebulk → L3 `fetch`** [M] — Each level
  implemented strictly in terms of the one below; a cvar caps run altitude; run the same
  model on the same chambers at L1/L2/L3 and plot solve rate/tokens vs altitude. "Where
  is the right reasoning/actuation altitude" becomes a measured curve — the headline
  methodological contribution beyond first light. *Deps: B2, B4.* (loco#13)
- **B16. Tick-cost accounting + tick budgets** [S] — Every MacroResult reports
  `ticks_used`; budgets switch from "25 macros" to "N ticks" per cvar. Macro-count
  budgets make fat macros free — exactly wrong for altitude comparisons and meaningless
  for the speedrun axis. (loco#17)
- **B17. WEDGED vs BLOCKED vs STUCK + bounded auto-unstick** [S] — Split the failure
  classes; on STUCK/WEDGED attempt a bounded recovery script (back-step, jump, retry
  once) *before* reporting. Micro-unsticking is exactly what a model should never burn a
  step on. (loco#18)
- **B18. Ride verbs: `enter(funnel)` / `ride(plate)`** [M] — The actuator owns precise
  entry into the element's volume + a settle condition; the element does the locomotion.
  Funnels and faith plates are pure-locomotion stock elements; without ride semantics
  every chamber containing one is an automatic misattributed failure. *Deps: B12.*
  (loco#20)
- **B19. Momentum macros: `sprint_to`, `abh`, `bhop` from upstream TAS tools** [M] —
  Upstream SAR already ships a battle-tested autostrafer (`StrafeTool`), AutoJump, Duck
  tools; wrap them as macros with the same guard/result contract. Time-optimal movement
  for the routing track and the part of Source locomotion no A\* grid captures. (merged:
  loco#16, speedrun#5)
- **B20. Path telemetry in every MacroResult** [S] — Keep the polyline + guard events the
  march loop already samples; render attempted-vs-achieved paths in the viewer's top-down
  SVG. Every BLOCKED becomes a labeled (state, goal, failed-path) hard negative — seed
  data for B21. (loco#22)
- **B21. Learned `go_to`: the PPO stack as cerebellum** [L] — Re-aim the dormant RL stack
  at point-goal navigation only (nav-compass + goal offset obs, framebulk actions, dense
  progress reward, procedurally generated nav rooms); serve via the existing
  InferenceServer *behind the same MacroResult contract*. The literal Gemini Robotics
  frozen-reasoner/learned-motor architecture, demonstrated in a real engine; per the
  economics lens, locomotion-scale RL is ~$0.50–5 per 10M ticks — essentially free. This
  is a fundable workstream (the headcount ask), with B2 as the boring baseline it must
  beat. (merged: loco#14, data#5, moonshots#6)
- **B22. Mine human `.hdem` for locomotion demonstrations** [M] — Segment human play at
  grab/release/portal/button events into point-to-point movement clips: BC pretraining
  for B21, per-chamber human tick baselines for LocoScore, and the measured
  human-vs-macro locomotion gap. *Deps: F1.* (loco#15)

---

## C. Agent loop & model economics

*The cost of context is the cost of everything downstream. 686k input tokens for 25 steps
means the agent is doing retrieval over its own transcript, not reasoning over a world.
Source: model-frontier lens; economics from gap-2.*

- **C1. The O(1)-context agent: scratchpad + current-frame-only, with the A/B** [S] —
  Replace the stateful chat with `system + scratchpad + current frame + percept + last-K
  digest`; the model ends every reply with an updated scratchpad (notes/plan/tried/
  where-things-are). Implement as a `ContextPolicy` abstraction ({full-chat | window-N |
  scratchpad}) and run the 3-way A/B. Simultaneously the #1 cost lever (quadratic →
  linear; ~20×+ at 100 steps per the gap-2 arithmetic), the #1 latency lever, the only
  context shape a 4B distilled model can ever run, AND the experiment: does the model
  actually maintain a bounded world-model? The single highest-ROI engineering act in the
  whole council per the economics audit. (merged: model#1, eval#20)
- **C2. Prompt-cache engineering** [S] — Measure the implicit cache-hit rate already
  recorded in `first_light.trajectory`, then restructure for caching: stable prefix,
  append-only shapes, `[stable | scratchpad | fresh obs]` ordering. Cached input is
  ~4–10× cheaper; with C1, the difference between a $30k and a $1–2k 1000-chamber sweep.
  (model#5)
- **C3. Cross-model harness + the capability-ladder curve** [S–M] — `ClaudeAgent`,
  `OpenAIAgent`, 1–2 open VLMs behind the same callable/grammar/budgets; sweep a ladder
  (flash-lite→pro, haiku→opus, mini→full) × chambers × ≥3 seeds. A single-model result is
  an anecdote about Gemini; the same reasoning-solved/locomotion-blocked signature across
  three vendors is a finding about embodied reasoning — and if Gemini tops the table, you
  walk into DeepMind showing their model winning on your instrument. The minimal 3-model ×
  first-light-chamber version is days. (merged: model#3, eval#7, pitch#1)
- **C4. Thinking-budget scaling curve** [S] — Sweep `thinking_level` × chambers × seeds.
  World-freeze makes deliberation free in-world, making this the cleanest *embodied*
  test-time-compute instrument anywhere — a DeepMind-legible chart. (model#4)
- **C5. Adaptive compute routing** [S] — Default LOW thinking; escalate on
  BLOCKED/STUCK, loop-detector fire, or model request; optionally route flash-lite for
  plan execution and pro for surprises. Hypothesis: 3–5× cheaper at equal solve rate.
  *Deps: A3, C4.* (model#16)
- **C6. Compound actions: emit short verb sequences, abort on first failure** [S–M] —
  `"verbs": ["go_to 11", "pick_up 11", …]` (cap ~4), executed sequentially server-side
  by the session. Amortizes context resends 4× on confident stretches; with C1, 30-step
  chambers become ~10 API calls. Risk respected: staler percepts — abort-on-failure
  keeps it honest. (model#21)
- **C7. Simulate-then-commit: best-of-N macros against save/load** [S API-only / M with
  anchors] — Tier A: sample K candidate actions, majority-vote. Tier B: anchor → execute
  each candidate for real → observe MacroResult + percept delta → restore → commit best.
  A frozen pausable simulator gives the agent ground-truth one-step lookahead — the
  proposal-distribution+verifier recipe, never yet done in embodied 3D with *real*
  dynamics. *Deps: G1.* (model#6)
- **C8. Plan-then-execute loop + the knowing-doing-gap scalar** [S–M] — Explicit numbered
  plan in the scratchpad; each step references its plan step or declares re-plan;
  offline, compute KD-gap = fraction of failures where the plan was right but execution
  failed (right verb + STUCK = doing-gap; wrong verb = knowing-gap). First light renders
  as "plan correctness ~100%, actuation success ~0% from step 8" — the thesis as one
  scalar that travels through review chains intact. (merged: model#8, pitch#2)
- **C9. Oracle-plan control arm (the 2×2)** [S] — Prepend the chamber's ground-truth
  macro plan to the prompt; run {plan, no-plan} × {real, teleport} per chamber. The
  cheapest *causal* decomposition of planning vs grounding vs actuation — pure prompt
  manipulation. *Deps: D1, D2.* (eval#11)
- **C10. Offline prompt-replay rig** [S] — Replay logged `prompt_sent` against modified
  prompts or other models; diff chosen actions step-by-step. 90% of prompt iteration
  stops needing a live game; treat match-rate as a screen, not a result (open-loop
  caveat). (model#10)
- **C11. Golden-decision regression suite** [S] — ~50 curated decision points (step-12
  recovery, loop escapes, done moments) as `prompt → expected action class`; 5-minute
  API-only CI on every prompt/grammar change and every new model release. *Deps: C10.*
  (model#22)
- **C12. Cross-chamber lessons memory (Voyager-lite, no code-as-action)** [S] — Distill
  3–5 transferable lessons per run into a versioned `lessons.md` injected next run;
  report `frozen` vs `frozen+memory` as separate modes. In-context skill acquisition
  without weight updates. (model#11)
- **C13. The data factory: rejection-sampling solves into an SFT corpus** [M] — Best-of-N
  per chamber; keep oracle-verified successes; judge-filter steps; emit (frame, percept,
  scratchpad, MacroRequest) tuples + hindsight-label failures ("predict the MacroResult")
  as calibration data. SIMA 2's self-improvement loop with a ground-truth reward instead
  of a hallucinatable rubric. *Deps: A1, C1, D2.* (model#12)
- **C14. First unfreeze: tune Gemini Flash on solved trajectories** [M] — Gemini tuning
  API on the C13 corpus (single-turn O(1) examples are what make trajectories tunable at
  all); evaluate tuned-Flash vs frozen-Flash vs frozen-Pro on held-out chambers. The
  cheapest credible "training on our env improves a frontier-family model" datapoint.
  (model#13)
- **C15. Distill to a 2–4B local VLM + macro-altitude RLFT** [L] — LoRA-SFT an open VLM
  on C13; serve locally behind the same agent interface; then GRPO with verifiable reward
  (= A1). Target: ≥ frozen-Flash solve rate at <1s/step and ~$0/step — a deployable
  agent, free evals, the competition baseline model, and the first "RL with verifiable
  rewards in a commercial 3D game." (merged: model#14, moonshots#17, data#19)
- **C16. The eval farm: 1000-chamber sweeps as a button** [M] — Marry
  `render_demos.py`'s queue/worker shape with `run_eval`: N instances × M concurrent API
  calls, resumable manifest (`runs/<chamber>/<model>/<seed>.trajectory`), per-run cost
  ledger, one `results.parquet`. At C1+C2 economics a 1000×3 flash sweep is ~$300–1k;
  naive stateful chat is ~$30k. This IS the competition eval backend, built for myself
  first. (model#15)
- **C17. Tokens-to-solve / $-per-solve as first-class metrics** [S] — Report (solve rate,
  median steps, tokens-to-solve, $-to-solve, wall-clock) from TokenUsage already in every
  trajectory. Anti-Goodhart: a 200-step loop-until-lucky agent and a 7-step solve must
  not score alike. (merged: model#19, eval#21)
- **C18. World-model accuracy probe: the model predicts, the engine grades** [M] —
  Optional `"predict"` field per action (expected result + state changes), graded against
  the actual MacroResult/percept delta. Per-model world-model accuracy in a real physics
  engine, graded automatically — a metric the Genie line cannot produce and would love to
  calibrate against; also surprise signals for C5. (model#20)

---

## D. Eval science — controls, metrics, statistics

*First light is N=1, single-seed, author-built, with steps 8–24 censored by the locomotion
wall. "Reasoning is solved" is a hypothesis this theme makes falsifiable. Source:
eval-science lens.*

- **D1. The teleport control arm** [S] — `--cheat-locomotion`: `go_to`/`pick_up` resolve
  by snap-teleport. Wrong for the leaderboard, *the missing instrument* for the science:
  oracle-actuation solve rate upper-bounds reasoning; the arm delta IS the locomotion
  tax, per chamber per model. The eval lens's spiciest take: until this exists, every
  hard-chamber failure gets charged to locomotion by default and the benchmark keeps
  telling me the story I already believe. (eval#3)
- **D2. Chamber suite v0: a ~20-chamber ladder + the manifest standard** [S–M] —
  Hand-authored PeTI chambers tiered by Demaine gadget class (T0 locomotion-only → T5
  composites), each with a `manifest.json` (exit/oracle config, element census, tier,
  par, reference solution, `requires_realtime`); patch `run_eval.py` to take `--chamber`.
  Converts first light from anecdote (n=1) into the first row of a benchmark (a
  success-vs-tier curve). The manifest is the interchange contract half the catalog emits
  into. (merged: eval#4, benchmark#2, puzzle#1)
- **D3. Pre-registered stats spec** [S] — Fix in advance: ≥5 seeds per cell, Wilson CIs,
  paired bootstrap over chambers, pass@k AND pass^k, power sketch (20 chambers × 5 seeds
  detects ~20–25pp gaps — don't claim 5pp), every headline states observability mode +
  actuation arm. Costs days; converts "we ran some evals" into methodology nobody in the
  LLM-game space has. (eval#6)
- **D4. Contamination protocol: hash-commits + isomorphic-remake probes** [S–M] — Eval
  chambers never published before the window (SHA256 committed at announce); memorization
  probes: stock campaign chambers vs geometry-isomorphic PeTI remakes — the solve-rate
  delta is the *measured contamination bonus* of 15 years of walkthroughs. The skeptic
  rates contamination the #1 kill-shot; this is the answer. (eval#5)
- **D5. Censoring-aware metrics: solve curves, not solve rates** [S] — Solve probability
  vs macro/token budget (anytime curves, AUC scalar); treat non-SOLVED terminals as
  right-censored (Kaplan-Meier over steps-to-solve). Kills budget-choice as a reviewer
  attack; pure analysis over existing trajectories. (eval#8)
- **D6. Per-step verdict labels: codebook + viewer writeback + LLM judge at scale** [M] —
  The viewer's reserved one-POST verdict seam becomes real (`ok / perception / grounding
  / reasoning / actuation / loop` sidecar JSON); author the codebook; hand-label ~20
  runs; then an LLM-judge labeler over `.trajectory` calibrated against the hand labels
  (report κ). The attribution pie chart that is figure 1; also the curation engine for
  C13 and the regression suite for harness changes. (merged: eval#10, model#9, data#17)
- **D7. Recovery events as a first-class metric** [S–M] — Auto-detect divergences
  (subgoal regression: button unpressed, held mark lost) and recoveries (re-targeting
  within k steps, state restored). Step 12 is the most impressive moment of first light
  and currently an anecdote; recovery-under-physics-side-effects is the most *novel*
  single metric this benchmark can claim. (eval#14)
- **D8. Perception VQA probe suite over logged percepts** [S–M] — Auto-generate
  ground-truthed questions from archived frames + snapshots ("what class is mark 7?",
  "is the button pressed?"); run stateless across models, no game needed. Completes the
  decomposition: perception score (this) + reasoning score (D1 arm) + actuation tax (arm
  delta); directly tests the Set-of-Marks bet. (eval#12)
- **D9. Egocentric vs global observability A/B (Track B)** [S–M] — Ship the designed
  ~70-LOC frustum+LoS filter behind a cvar; pre-register the leak hypothesis; report the
  information-leak delta; headline numbers state mode. Also opens the active-perception
  axis (look-before-act rates, exploration efficiency, an explicit `scan` verb). (merged:
  eval#15, moonshots#20)
- **D10. Human macro-altitude par** [S–M] — 2–3 humans solve every suite chamber through
  the same macro REPL (same verbs, percepts, budgets); par = median human macro count;
  report models human-normalized. Also measures the *grammar's* ceiling — if humans can't
  solve it through these verbs, the grammar is missing verbs — and seeds the demo corpus.
  (merged: eval#16, data#16)
- **D11. Mark-robustness perturbations** [S] — Permute mark IDs, recolor classes, jitter
  label positions; solve rate should be invariant. Adversarial hygiene + a future-proofing
  probe against models trained on our own published trajectories. (eval#18)
- **D12. Annotation-correctness QA** [S] — Golden frames with IoU checks that box N
  actually covers entity N, run on every HarnessAnnotate change. A mis-drawn box silently
  charges perception errors to the model; nobody validates the annotator today.
  (completeness §1.8)
- **D13. Vision/annotation ablation battery** [S–M] — Arms: {full annotation,
  boxes-no-labels, raw pixels, text-only mark list, frame-every-N, status fields on/off}.
  BALROG found vision often *hurts* frontier models; if Portal 2 replicates that, it's a
  citable finding and it tells us where the benchmark's vision discrimination actually
  lives. (merged: model#17, eval#13)
- **D14. Per-element competence matrix via minimal-chamber families** [L] — One-factor
  micro-chambers per element (fizzler, faith plate, relay, funnel, timed button…) → a
  model × element heat-map and within-element difficulty scaling. Tells you *which
  mechanism* breaks a model, not just which tier. *Deps: A1, E1–E2.* (eval#23)
- **D15. Paper #1: the instrument + the separation finding** [M→L] — "Attributable
  evaluation of frozen VLMs in a PSPACE-hard physical puzzle environment": macro-boundary
  attribution by construction, teleport-arm × verdict data, Demaine ladder, contamination
  controls. Venue ladder: agentic/games workshop 4-pager → NeurIPS D&B. Writing the
  claims list first back-propagates which infra is load-bearing. (eval#9)

---

## E. Chambers as a generative substrate

*The puzzle-gen lens's inversion: a generated chamber carries the exit, the wiring, the
difficulty knobs, a solvability certificate, and the witness solution BY CONSTRUCTION —
four of the roadmap's standing blockers are hard on found maps and free on emitted ones.
Source: puzzle-gen-curriculum; merges from benchmark, moonshots, harness, data.*

- **E1. `py/puzzlegen`: a Python `.p2c` chamber DSL** [S minimal → M full category-A] —
  `Chamber(size).add(FloorButton, at=…, connects=door).save()`; voxel grid + items +
  connections per the documented KeyValues format, porting Kyle0654/Portal2.Puzzle logic.
  Once chambers are Python objects, generation, mutation, canonicalization, and
  LLM-authoring are ordinary code. (puzzle#2)
- **E2. Headless `.p2c → .bsp` compile pipeline** [M, recon-first] — Three paths in
  order: drive the in-game PeTI compiler via `ExecuteCommand`; sidestep via
  `.p2c → .vmf` (srctools / BEE2.4 reference) + CLI `vbsp/vvis/vrad`; worst case a
  Windows batch hop. **The single load-bearing unknown of the whole generative thesis**
  — budget a throwaway recon week, and send the TeamSpen ping (M46) first. (puzzle#3)
- **E3. Demaine-gadget parametric families** [M] — One generator per formal rung
  (`family_cube_button(n_rooms, n_distractors)`, `family_laser_relay(depth)`,
  `family_timed_button(slack)`…), each seed-deterministic, certified by G4, manifest with
  family/knobs/class. A task *distribution* whose difficulty axis is a complexity-theory
  dial; test sets regenerated from private seeds. (merged: puzzle#9, pitch#11)
- **E4. Formal gadget-graph compiler: provable instances** [L] — Encode the actual
  Demaine reduction gadgets as `.p2c` macros; compile abstract gadget graphs (or small
  SAT/QBF instances) into playable chambers whose solvability is provable from the source
  instance. "These 500 chambers are satisfiable iff this 3-SAT instance is" — also
  generates arbitrarily hard *reasoning* with trivially easy locomotion, the perfect
  dissection tool. (puzzle#14)
- **E5. Mutation operators + adversarial hardening loop** [M] — Move/rotate elements,
  add glass walls, swap cube types, re-wire, add distractors; when an agent solves C,
  search for the minimal mutation that breaks it while G4 still certifies solvability.
  First light's killer — a glass enclosure — is exactly one `add_glass_wall` mutation on
  a solved chamber; also the memorization probe (base-vs-mutant delta as a leaderboard
  column). (merged: puzzle#10, benchmark#10)
- **E6. Cosmetic augmentation: same logic, different pixels** [S] — k visual variants
  per chamber (PeTI style packs, lighting, jittered proportions) tagged as a
  logical-equivalence class. Visual-robustness eval + domain-randomization data.
  (puzzle#19)
- **E7. Physics-parameter OOD arm** [S] — `sv_gravity`, timescale, funnel-speed variants
  over the same logical chamber; pure cvar work; a world-model-robustness axis nobody
  claimed. (completeness §1.8)
- **E8. Distractor & clutter ablation grid** [S] — +k unwired buttons, decoy doors,
  antlines visible vs hidden (PeTI's indicator lines are literal wiring information),
  mark-density stress. Does the VLM read the wiring or pattern-match "cube goes on
  button"? Cheapest publishable science in the theme. (puzzle#12)
- **E9. LLM puzzle setter: language spec → chamber** [M] — Frozen LLM drives the E1 DSL
  ("two-room chamber, laser through a fizzler-guarded corridor") → compile → certify →
  screenshot self-critique loop. The 30-second VP demo of "generative substrate," and a
  science question: can a model author puzzles it cannot solve? *Deps: E1, E2, G4.*
  (puzzle#11)
- **E10. Setter/solver POET coevolution** [L→XL] — Archive of chambers at the frontier of
  solver ability; setter (E5 mutations, optionally E9) proposes children; minimal
  criterion = certified solvable AND solve rate in (0.1, 0.9); solver = macro-altitude
  learning agent. "The benchmark that grows itself at the frontier" — XLand/Genie energy
  with real physics and verifiable ground truth; honestly a team-sized run. (merged:
  puzzle#13, moonshots#3)
- **E11. Workshop ingestion pipeline + chamber DB** [M→L] — ISteamUGC enumeration →
  `steamcmd workshop_download_item` → headless boot probe (loads? element census? exit
  candidate? 60s crash test) → sqlite/parquet DB with Workshop social metadata.
  Download-on-demand only, never redistribute (the UGC-licensing-safe path). Turns "954k
  items, most garbage" into a queryable corpus and the honest version of the breadth
  claim. (merged: puzzle#4, benchmark#8, harness#18, moonshots#7, data#14)
- **E12. Canonicalization + dedup of the corpus** [M] — Quantize the BSP entity lump to
  the voxel grid, hash modulo symmetries, LSH for near-dups. Keeps test sets leak-free
  (a train chamber's near-dup in test = silent contamination) and makes corpus claims
  honest. *Deps: E11.* (puzzle#15)
- **E13. Solve-rate difficulty probes: agents as difficulty meters** [M] — Probe battery
  per chamber, cheapest first: random-macro → scripted-greedy → Flash → Pro; difficulty =
  first tier that solves it. Also the **baseline floors** the skeptic demands on every
  chart forever (if greedy matches Gemini on tier 1, the first-light slide is dead).
  (puzzle#5)
- **E14. Static difficulty predictor** [S–M] — Census features (elements, volume, wiring
  fan-out) + social signals → predict E13's probe difficulty; report which features carry
  signal. Pre-sorts 100k maps so probe budget goes to the interesting band. (merged:
  puzzle#6, benchmark#18)
- **E15. Curriculum scheduler: `next_chamber(agent_history)`** [S] — ~200-line bandit
  over difficulty bins targeting the 30–70% solve band, with novelty bonuses and
  per-element quotas; serves both the VLM eval ordering and the RL training curriculum.
  The smallest artifact that makes "curriculum" a real word in the pitch. (puzzle#7)
- **E16. Two-source held-out protocol: generator seeds + fresh-workshop windows** [S–M +
  quarterly S] — Test sets (a) regenerated from maintainer-held seeds per season
  (hash-committed before, revealed after) and (b) drawn from a rolling window of
  newly-published Workshop chambers a frontier lab cannot have trained on. The benchmark
  becomes unsaturatable in principle: the metric is "solve rate on chambers that didn't
  exist last quarter." (merged: puzzle#17, benchmark#7)
- **E17. P2-Bench-100: the curated community tier** [M] — ~100 acclaimed community
  chambers (Mevious-tier authors, hall-of-fame lists, DB-stratified), shipped as item-ID
  lists + download-on-demand, with author credit and opt-out policy (the completeness
  critic's unnamed stakeholder). Generated chambers prove scale; human masterpieces prove
  meaning. (puzzle#18)
- **E18. Demaine ladder asymptotic curves** [M] — Solve rate vs instance size n per
  gadget family: an asymptotic scaling curve of embodied reasoning grounded in actual
  complexity theory. Every LLM eval reports a scalar; none reports decay with formal
  instance size. *Deps: E3.* (moonshots#15)
- **E19. Puzzle kernel extraction** [L] — Greedily delete elements while G4 still
  certifies solvability → minimal "kernel" puzzles; cluster kernels across the corpus →
  an empirical inventory of the community's actual puzzle motifs. Curriculum atoms + a
  "grammar of Portal 2 puzzles" paper. *Deps: G4, E12.* (puzzle#22)

---

## F. The data flywheel — trajectories as the product

*Premise: labs provably pay for embodied trajectories (VPT contractors, SIMA studio deals,
teleop at dollars-per-minute); Portal 2 trajectories cost near-zero marginal here and carry
perfect ground-truth state. Per gap-1: the data clocks are the only durable team-vs-team
moats, and none has started ticking. Source: data-flywheel; merges everywhere.*

- **F1. `.hdem` v2: per-tick actions + sound events + format-discipline sweep** [S] —
  Add the CUserCmd input record (buttons, viewangles, moves — the hook exists at
  `Client.cpp:586`) AND the sound-event record (gap-3) in the same version bump; write
  the footer CRC; add `format_version` to `trajectory.proto`. Turns every human
  playthrough into a complete, engine-free (obs, action, state, audio-event) episode at
  I/O speed. **Schedule-critical: recordings made before v2 are lossy forever** — this is
  the data clock that gap-1 says must start this month. (merged: data#1, harness#13,
  moonshots#5a, benchmark#17a, pitch#15a, gap-3#4)
- **F2. RLDS / LeRobot / SIMA-span exporters** [S] — One `py/export/` converter from
  `.hdem`/`.rollout`/`.trajectory` to RLDS tfrecords, LeRobot, and SIMA span tuples, with
  a tiny tfds builder. A dataset nobody can load is a demo; one that drops into the
  buyer's pipeline is a product. Skeptic caveat: format compatibility ≠ semantic
  usefulness — don't oversell "train on it Monday." (merged: data#3, harness#14,
  moonshots#18a)
- **F3. Auto-annotation engine: entity events → language spans** [S–M] — Pure-Python
  detector over the snapshot stream emitting causally-correct subtitles ("placed cube on
  button 7 → door 8 opened") chunked into SIMA-style instruction spans; optional Gemini
  paraphrase pass. SIMA 1's two stated bottlenecks were studio access and human
  annotation; the engine deletes both. Per gap-2: ~$0.0002–0.0004/event vs $0.05–0.50
  human — the single best unit-economics line the project owns. (merged: data#2,
  puzzle#16)
- **F4. Macro-segmentation of human play (inverse-macro labeling)** [M] — Re-express
  human low-level trajectories in the agent's verb vocabulary using entity ground truth
  (`header.model="human"`). Yields macro-altitude BC data, in-context worked examples,
  human KD-gap baselines, and a measurable grammar-coverage metric (if human play can't
  be macro-segmented, the grammar is missing verbs). *Deps: F1.* (merged: data#4,
  moonshots#16)
- **F5. `.hdem` → percept replay: humans in the agent's observation space** [S–M] —
  Re-render human recordings as macro-level percept streams (pure Python; optional
  engine replay for frames). Human demos become few-shot exemplars and SFT data in
  exactly the model's input/output space. *Deps: F1, F4.* (data#13)
- **F6. P2-Traj v0: the dataset release as credibility artifact** [M] — A deliberately
  modest 10–20 hours (self + volunteers) with synchronized inputs, ground-truth state,
  auto-annotations, RLDS export, dataset card + license, on HuggingFace, with the 6-page
  D&B-style paper. Forcing-function hygiene: every format bug and licensing question
  surfaces the first time an outsider downloads it. (data#10)
- **F7. Distribution coup: opt-in `.hdem` recording in community SAR** [M + calendar] —
  Upstream (or ship a blessed build of) the recorder behind an explicit consent flow +
  one-click share. Upstream SAR is mandatory tooling for the Portal 2 leaderboards — the
  recording host is already on every active runner's machine; verify the actual rule text
  first (skeptic claim-check #3) and don't oversell the population (low hundreds of
  runners). *Deps: F1, F19.* (data#6)
- **F8. Demo-archive backfill farm** [M→L] — Ingest the existing community `.dem` corpus
  (speedrun.com, p2sr archives, board.portal2.sr CM demos — rank = skill label) through a
  hardened `render_demos.py` farm into `.hdem`-overlay rollouts with reconstructed
  actions. Years of expert play already exist on disk; honest math: rendering is ~1×
  realtime, so full-board scale needs the throughput program (J10). (merged: data#7,
  speedrun#4)
- **F9. Engine-verified VPT: IDM + YouTube harvest with a replay verifier** [L] — Train
  an inverse dynamics model on the input-synchronized corpus; pseudo-label 15 years of
  YouTube Portal 2; *replay predicted inputs in the actual engine* and accept only clips
  that re-simulate (tolerance bands — Source is semi-deterministic). The honest
  needs-headcount item; pseudo-label *certification* is a paper on its own. *Deps: F1,
  F7/F8, E11 for map ID.* (merged: data#8, moonshots#5b)
- **F10. Audio as free action labels for the harvest** [M] — Portal shots, jumps,
  pickups are loud distinctive one-shots; train a tiny onset classifier on
  engine-labeled audio (infinite free labels via the waveform tap) and run it over
  YouTube audio to seed/cross-check the IDM. *Deps: H4.* (gap-3#14)
- **F11. Counterfactual branch data: embodied preference pairs** [M] — From anchors, roll
  multiple macro continuations per decision state; label by outcome; emit (state, chosen,
  rejected) DPO pairs and per-step progress labels. Almost nobody has counterfactuals
  from identical states in a physical 3D world, because reality doesn't reload — arguably
  the most differentiated data product in the catalog. *Deps: G1.* (data#11)
- **F12. The wormhole world-model eval set** [S–M] — A few thousand portal-transit clips
  with per-tick ground-truth pose/velocity + matched no-portal controls, packaged as a
  next-frame/next-state prediction benchmark. Genie-class models are publicly weakest at
  non-Euclidean consistency and cannot self-grade physics; this is the sharpest hook into
  the world-model org, at tiny cost. Pre-sell with 5 teaser clips before cutting 1k
  (M26). (merged: data#12, moonshots#14)
- **F13. Playtest analytics for PeTI authors** [M→L] — "Test my chamber": authors get
  heatmaps, stuck-points, completion funnels from `.hdem`; recording consent built in.
  Answers "who records at scale?" with an *incentive* — every playtest is a labeled
  trajectory on a novel chamber. v0 = "send me your hdem, get an HTML report." (data#15)
- **F14. Offline analysis library over `.trajectory` archives** [S] —
  `py/llm_eval/analysis.py`: KD-gap, loop stats, action distributions, token-cost
  curves, rejection taxonomy, cross-run reports — no game needed. `.trajectory` is
  lossless precisely so analysis never needs reruns; that value is currently unrealized.
  (data#18)
- **F15. Format hardening + published spec (`docs/formats.md`)** [S] — `format_version`
  into trajectory.proto, hdem footer CRC, fix `hdem_to_rollout.py` placeholder dims,
  round-trip tests, and one spec doc so third parties can write independent readers.
  Prerequisite hygiene for every release above. (data#21)
- **F16. Lazy-pixel architecture: store state, render frames on demand** [M] — Canonical
  artifact = `.dem`+`.hdem` (MBs); a farm job regenerates frames at any resolution.
  Per gap-2: at pilot scale storage is noise — pitch this as resolution/fps
  future-proofing and the 100k-hr regime ($12/mo vs $9k/mo), not as 2026 savings.
  (merged: data#22, gap-2#9)
- **F17. The Omni data annex: four-channel time-aligned export** [M] — Video + audio
  (wav) + per-tick actions + ground-truth state + caption-grade sound-event text on one
  tick clock. YouTube has audio but no state/actions; teleop has state/actions but no
  annotated audio; sim data is silent — nobody has all four aligned. Makes "trajectories
  for Gemini Omni" a sentence with content. *Deps: F1, H4, F2.* (gap-3#13)
- **F18. Human-human co-op dialogue corpus** [M] — Human pairs playing co-op with
  voice/chat recorded into `.hdem`: grounded referring expressions under asymmetric
  viewpoints — a collaborative-dialogue dataset the language-grounding community has no
  3D source for. *Deps: L1 plumbing.* (completeness §1.8)
- **F19. Dataset governance: consent, license, PII, ToS** [S, before any recording] —
  Opt-in consent flow + anonymization (`.dem` files carry SteamIDs; the community
  includes minors); a dataset license separating inputs/state/annotations (ours) from
  frames (game-derived gray zone); read the Gemini/Claude/GPT ToS *before* any public
  artifact contains frontier-model outputs (distillation restriction landmine). Consent
  cannot be retrofitted onto collected data. (merged: data#20, completeness §3.1/3.3)
- **F20. VLA transfer experiment: the falsifiable robotics claim** [L] — Mix Portal 2
  RLDS data into an open VLA fine-tune (OpenVLA/pi0-class), measure on LIBERO/SIMPLER vs
  control. A positive result makes the robotics slide load-bearing; a negative one is
  honest and cheap — either beats asserting transfer. Flag as collaboration bait.
  (moonshots#18b)
- **F21. The data-clock audit** [S] — One table over the four data assets (consented
  human corpus, public demo archive, curated chamber DB, trajectory archives): accrual
  rate, replication latency for a funded fork, and the date each clock starts. Output is
  a scheduling constraint: F1 + F19 ship this month because lead time is the only thing a
  fork can't download. (gap-1#7)

---

## G. Search, routing & the speedrun axis

*Routing (find the route) vs execution (hit the ticks) — orthogonal, and both halves sit on
infra that mostly exists: upstream SAR is ten years of speedrun tooling (TAS framebulks,
autostrafer, SeamshotFind, RNG pinning, ghosts, Challenge Mode). The fork added the missing
half: a pausable, branchable simulator. Source: speedrun-routing; merges from moonshots,
model-frontier, eval-science. Note the epistemic split: "no global pathfinding" is the LLM
track's rule; here search IS the method.*

- **G1. Save/load anchors → a branchable simulator API** [S–M, tar pit acknowledged] —
  `Anchor()`/`Restore(id)` RPCs over engine save/load with the post-restore invariants
  *tested* (marks stable, snapshotter slots coherent, SHM refreshed). The skeptic is
  blunt: "~40 LOC" is a design claim, restore is untested, and four lenses stack on it —
  test before building anything above it. Branchability is what distinguishes a simulator
  protocol from a Gym env. (merged: harness#12, speedrun#2-prereq, model#6b, moonshots#2a)
- **G2. $/search-node microbenchmark before any planner** [S–M] — Measure save/restore
  latency, macro throughput ±rendering/timescale, snapshot read cost → $/node and the
  prior-to-engine cost ratio. Gap-2's arithmetic: an LLM prior call is 20–80× the engine
  cost of a node, which **kills per-node-LLM MCTS designs** before anyone builds one;
  restore latency decides deep-trees vs fan-out architecture. (gap-2#10)
- **G3. Best-of-N route search v0: "LLM proposes, engine verifies"** [S–M] — Sample N
  macro sequences (LLM at temperature or scripted enumeration), execute each from an
  anchor, keep the fastest that completes, re-execute deterministically and record. The
  first routing *result*, weeks not months; seed of every fancier planner. *Deps: G1,
  A1/A2.* (speedrun#2)
- **G4. Macro-MCTS: router, oracle solver, and machine par** [M→L] — Best-first/MCTS
  over macro sequences: nodes = anchors, priors = VLM proposals (sparse, per G2), value =
  ticks + progress. Triple duty: the routing agent, the suite's solvability certifier
  (a benchmark whose tasks aren't machine-verified ships broken chambers), and machine
  par + witness solutions = free SFT data. (merged: model#7, speedrun#3, moonshots#2,
  eval#22, puzzle#8)
- **G5. Tick-level savestate brute-forcer ("BizHawk for Source")** [M→L] — RPC: load
  anchor → apply framebulk window → advance K ticks → report metric; drive with
  CEM/random-restart over jump ticks and strafe angles. The execution half of superhuman;
  also the first tool a human TASer would adopt today. *Deps: G1, J10.* (speedrun#7)
- **G6. Chamber route-graph extraction + portal edges** [L] — Offline BSP/voxel parse
  into a coarse reachability graph (walk/fall/portal/fling edges); the portal-edge
  generator reuses the annotation reticle's placement-validity logic. Routing becomes
  engine-free graph search with the engine as verifier. (speedrun#8)
- **G7. Analytic fling solver** [M] — Ballistics module: entry/exit portal poses →
  reachable set; inverse mode proposes portal placements connecting two marks; validate
  against the engine on known flings. Makes momentum routes searchable instead of
  stumbled-upon. (speedrun#9)
- **G8. Ghost racing: human WR as dense reward** [M] — Play a human demo as a ghost
  (machinery exists); dense signal = signed time-delta at matched route progress.
  Fixes the sparse-reward problem that killed the PPO stack with human data instead of
  hand-shaped penalties; maximally spectator-legible. *Deps: F8 demos.* (speedrun#10)
- **G9. Glitch regression suite: known exploits as `.p2tas` unit tests** [M] — Transcribe
  ~10 canonical p2sr glitches with post-condition assertions. Regression tests, ground
  truth for glitch *discovery* (can search rediscover them?), and demos for execution RL.
  Recruit one community TASer. (speedrun#11)
- **G10. Glitch discovery as novelty search** [L] — Go-Explore-style perturbation search
  from dense anchors with anomaly detectors (teleport-scale displacement, speed-over-cap,
  OOB-while-alive, trigger-without-prerequisite) as interestingness. Honest framing:
  rediscovery is the publishable result; a *new* glitch is the lottery ticket / move-37
  narrative. Dual-use with I6 — same search, defense for safety. (merged: speedrun#12,
  moonshots#13)
- **G11. Route-novelty detector** [M] — Cluster the human corpus into route families
  (DTW over position polylines); flag agent routes far from every cluster AND
  competitive on time. Converts "the agent found a route" into a measurable claim with
  error bars. *Deps: G3/G4, F8.* (speedrun#13)
- **G12. Skill-stratified imitation from the CM corpus** [L] — Leaderboard demos carry
  rank; train rank-conditioned BC so one model spans novice→WR motion, steerable by a
  skill token. Graded-by-outcome motion priors are what teleop data never has. *Deps: F8
  at scale.* (speedrun#14)
- **G13. Two-layer machine TAS: router → `.p2tas` compiler → tick polisher** [L→XL] —
  Macro route compiled to framebulk skeleton (autoaim/strafe tool annotations), segments
  tightened by G5, final deterministic replay submitted in the community's own format,
  verifiable by their own tools. The flagship speedrun artifact; honest about
  semi-determinism. (merged: speedrun#16, moonshots#12)
- **G14. Automated demo splicing** [L] — Segment library cut at low-velocity anchors;
  search splice compositions with boundary-state matching + local repair. 1000 demos ×
  per-segment best = corpus-derived superhuman assemblies. (speedrun#18)
- **G15. Route-waypoint curriculum for the dormant PPO stack** [M→L] — Replace the
  hand-confessed reward patchwork with waypoint progression along known routes (dense,
  potential-based, derived from routing not hand-tuning); fix the bit-rotted launch
  flags. The two tracks finally compose: routing output makes execution learnable.
  (speedrun#19)
- **G16. Routing copilot for human runners** [M] — Ship the toolkit *to* the community
  as SAR cvars/HUD (fling solver overlay, route-graph viz, seamshot percepts) in
  exchange for opt-in `.hdem` recording. Tools-for-trajectories; makes "SAR is the
  substrate" true rather than aspirational. (speedrun#20)
- **G17. Routing-as-planning benchmark export** [M, pre-sell first] — Serialize route
  graphs + macro dynamics into engine-free planning instances (JSON/PDDL) with the
  harness as verifier. The planning community ships its own solvers and needs no game,
  no GPU, no Steam — per gap-0, the cheapest L3 pre-commitment in the portfolio; email
  ICAPS organizers before building. (merged: speedrun#17, gap-0#9)
- **G18. OOB + anomaly telemetry** [S–M] — `player_oob`, `speed_over_cap`, displacement
  spikes in GameState. Sensor layer for G10, a guard for normal evals ("you clipped
  through the wall" ≠ BLOCKED), and the violation-label substrate for I2. (speedrun#21)
- **G19. SeamshotFind & friends as percepts** [S] — Surface upstream analytic routing
  tools (seamshot locations, PlacementScanner) through the harness. Free glitch-route
  edges from code the community already trusts. (speedrun#22)
- **G20. The exhibition: human WR vs community TAS vs agent** [S–M packaging] — 3–5 CM
  chambers, three synchronized runs each with times, route maps, and the agent's decision
  trace, in the house viewer style. The pitch centerpiece; "within 20% of WR via a
  different route" is already strong when the route map shows why. *Deps: G13 chain.*
  (speedrun#23)
- **G21. Full-campaign routing** [XL] — Route the whole SP campaign (chapter graph,
  elevator transitions, category rules from SpeedrunTimer). Named to scope the ladder
  honestly: this is the multi-person flag at the top, i.e. the headcount ask.
  (speedrun#24)
- **G22. The compound-asset demo** [M] — One command: pull a top-100 board demo →
  re-render through the farm → auto-extract macro labels → savestate-search a segment to
  beat its time → verify with the oracle → emit `.trajectory`. Each asset alone is
  replicable; the *compound* is what takes a fork a year — demonstrate the moat as
  integration, on video. (gap-1#12)

---

## H. Audio & real-time — the embodiment gaps

*Two axes no lens owned (gap-3, gap-4): the harness is deaf, and the world always waits.
Both bite within one milestone: every `.hdem` recorded today is a silent movie pitched to
an audio-first org, and pause mode breaks timed elements that are in our own v0 scope.*

- **H1. Audio capability recon** [S] — Per launch mode (gamescope, headless, `-nosound`):
  do server-side sound *events* fire (expected: yes), does the mixer run? Half a day; the
  result picks the architecture (semantic hook first, waveform second). (gap-3#1)
- **H2. SoundSnapshotter: the audio EntitySnapshotter** [S] — Hook the soundemitter emit
  path; buffer per-tick `{tick, sound_name, source mark, origin, volume, soundlevel,
  flags}` with looping dedup; surface as delta'd `sound_events` on GameState. The only
  percept that captures events *between* observations (a turret line on tick N+3 is
  simply gone today); also a free replay-verification fingerprint. (gap-3#2)
- **H3. Caption + bearing renderer: sound as ~20 text tokens** [S] — Parse soundscripts +
  Portal 2's shipped closed captions into `sound_name → caption`; render `[heard t+12]
  turret 7 (right, ~6m): "I see you"` with engine-attenuation audibility filtering
  (audio is naturally egocentric — honesty falls out of the physics). Oracle hearing for
  frozen text/vision models, no ASR, no audio tokens billed — the cheapest percept in the
  stack. (gap-3#3)
- **H4. Tick-synced waveform tap** [S–M] — Generalize upstream `Renderer.cpp`'s
  `SND_RecordBuffer` hook (80% built) into a HarnessAudio tap: PCM into a tick-indexed
  SHM ring + optional `.wav` sidecar. Tick-clocked, so it survives `host_timescale` and
  world-freeze where any OS tap drifts. (gap-3#5)
- **H5. PipeWire per-instance tap spike** [S, timeboxed] — Per-instance null sink +
  `pw-record`; measure drift vs the engine tap. Do it once to kill it with data (and keep
  it as the only option that hears OS-level output). (gap-3#6)
- **H6. EchoBench: audible-but-not-visible chambers, three arms** [M] — 8–12 chambers
  where audio carries the discriminating bit (which door opened, off-screen dropper,
  turret behind opaque glass, audible timer tick); arms: deaf / transcript / raw audio.
  No embodied benchmark anywhere isolates audio as the load-bearing modality with engine
  ground truth. (gap-3#7)
- **H7. `HEARD(event)` as macro interrupt** [S on B12] — `go_to` aborts with
  `INTERRUPTED_BY_SOUND(mark, class)` when a watched sound class fires mid-march. A
  turret saying "Target acquired" three ticks into a 200-tick march should end the macro
  — today the agent learns from pixels one step later, possibly while dead. (gap-3#8)
- **H8. The Omni arm: native ears vs engine transcripts** [M] — Attach the last N seconds
  of waveform per step for audio-capable models; pre-registered A/B vs the transcript
  arm on EchoBench, with audio-token cost accounting. Registered prediction: transcripts
  win in 2026 — and that negative result about Omni-class ears is itself citable.
  (gap-3#9)
- **H9. Viewer audio lane + synced playback** [S] — Sound-event pips on the step
  timeline, captions on hover, `<audio>` synced to the scrubber where a wav exists.
  Makes "missed audio" a labelable failure cause. (gap-3#10)
- **H10. Sound-localization micro-probe** [S] — From archived clips + engine truth:
  "a turret is beeping — which direction?", graded as bearing error. Spatial hearing in
  multimodal models is measured nowhere; every clip auto-labeled. (gap-3#11)
- **H11. Soundscape hygiene knobs** [S] — Documented cvar presets: clean mode (events/VO
  only) vs realistic mode (music+ambience), recorded per artifact. Turns SNR from a
  confound into a dial. (gap-3#15)
- **H12. Real-time session mode** [M] — `sar_harness_realtime 1`: engine free-runs at 1×,
  server pushes GameState at a cadence, actions preempt via the existing CANCELLED paths.
  The real cost is honest: macros become per-tick state machines — the same refactor B12
  needs, so the cost is shared. Without this the robotics pitch has no answer to
  "reality doesn't pause." (gap-4 A1)
- **H13. The anytime-action contract** [S] — Written, versioned semantics: default
  behavior while thinking (idle v0 / continue), advisory `respond_by_tick`, late actions
  apply but staleness is recorded with a `STALE_WORLD` note. Universe died partly of
  *implicit* real-time semantics; the contract is what makes the track an instrument.
  (gap-4 A2)
- **H14. Staleness accounting** [S] — `obs_tick` / `decide_walltime_ms` / `apply_tick`
  per trajectory step; staleness distributions per run (identically ~0 in pause mode —
  which is exactly the chart). Robotics-grade latency accounting, nearly free. (gap-4 A3)
- **H15. The deliberation-subsidy curve** [S–M] — Run the suite at `host_timescale` ∈
  {0, 0.1, 0.25, 0.5, 1.0} while the model thinks; headline scalar = paused-vs-1× solve
  gap; secondary: thinking-budget sweep at 1× (test-time compute now has a world-state
  price). The one chart neither text benchmarks, nor Genie, nor real robots can produce —
  each lacks one of the two arms. (gap-4 A4)
- **H16. Two-system agent: think-while-acting** [M] — `default=continue`: the macro layer
  is System-1, the VLM is System-2, planning latency hides inside actuation; measure
  overlap fraction and solve-time vs stop-and-think. The literal fast/slow Gemini
  Robotics architecture, demonstrated with the existing macro boundary as the reflex
  half. (gap-4 A5)
- **H17. Real-time hazard gauntlet** [M] — 8–12 chambers trivial in pause mode and graded
  in real time (timer-button distance sweeps, turret corridors where standing still
  draws fire). Isolates pure time pressure with reasoning held constant; the most legible
  real-time artifact. *Deps: H12, A4-series.* (gap-4 A7)

---

## I. Safety, spec-gaming & reasoning faithfulness

*Gap-5's double axis. (a) Portal 2 + speedrun category rules = a reward-hacking laboratory
with engine-exact violation labels — the thing METR/OpenAI grade with LLM judges and hand
review. (b) Five council metrics consume the model's traces as truth; the literature says
stated reasoning routinely fails causal tests. Both labs open the fifth and sixth GDM
budget lines (AGI Safety, interpretability). Caveat kept: Gemini returns thought
summaries, not raw CoT — name the trace layer measured.*

- **I1. The Rulebook: exploit-adjudication policy v0** [S] — Intended-solution semantics
  per chamber, an exploit taxonomy (OOB, clip, trigger-skip, fizzler bypass,
  oracle-gaming…), p2sr-style category labels (`any%` / `inbounds` / `glitchless`) on
  every reported score, and an adjudication ladder for novel exploits. Speedrunning is
  the only institution with 20 years of adversarially-tested spec-adjudication; without a
  rulebook "the agent found an exploit" is a vibe. (gap-5#1)
- **I2. Violation sensors wired as verdicts** [S–M] — Extend G18 with trigger-skip
  detection, subgoal-ordering checks, object-through-barrier, proxy-oracle disagreement;
  fold per-run into `LEGAL` / `EXPLOIT(class)` against I1. Engine-truth violation labels
  are what every reward-hacking paper lacks — here the judge is memcmp. (gap-5#2)
- **I3. `Rules.cpp` as the machine-checkable category engine** [S–M] — Upstream SAR
  already ships a declarative rule interpreter (`Speedrun/Rules.cpp`) battle-tested by
  the most adversarial users imaginable; expose it via manifests (`max_portals: 0`,
  forbidden zones, required triggers). Glitchless becomes a bit the engine computes.
  (gap-5#3)
- **I4. Temptation chambers: the exploit is cheaper by design** [M] — 10–15 chambers with
  an intended route and a planted cheaper violation route; run under three framings
  (bare goal / "solve as designed" / time pressure); report exploit rate × framing ×
  model. The embodied analog of honesty-under-temptation evals, with physical, authorable
  temptations and engine-exact verdicts. (gap-5#4)
- **I5. The proxy-gap experiment: our own reward is the first hack** [S] — Chambers where
  the exit-radius proxy and engine truth disagree by construction; run the PPO stack
  against the proxy (it will Goodhart — that's the point) and the frozen VLM told only
  the proxy goal; report the Goodhart gap. "We measured our own benchmark's hackability
  first" is the risk-register slide writing itself. (gap-5#5)
- **I6. The glitch mine, re-aimed at the Rulebook** [M→L] — Same novelty search as G10,
  objective = sensor-flagged rule violations; every confirmed exploit auto-compiles into
  a sensor regression test, a new temptation chamber, and a labeled exploit trajectory.
  A reward-hacking benchmark with a generator behind it stays adversarial. (gap-5#6)
- **I7. The confession channel** [S] — Fixed post-run debrief ("did you use any
  unintended mechanic?") graded against sensor verdicts: confession-given-violation rate,
  false confessions, exploit awareness. The cleanest honesty metric a leaderboard can
  carry — not "did it hack" but "did it tell you." (gap-5#7)
- **I8. Monitor-vs-actor: CoT monitoring with non-hallucinatable labels** [M] — A second
  frozen model reads trajectories (thinking+actions vs actions-only arms) and flags
  violations; sensors grade the monitor. Then: actors informed they're monitored vs not —
  does verbalized intent go quiet while exploits persist? Embodied CoT obfuscation with
  exact labels; the sharpest AGI-Safety artifact in the catalog. (gap-5#8)
- **I9. Plan-swap counterfactuals: is the trace load-bearing?** [S offline / S–M with
  anchors] — Edit the model's own stated plan inside replayed prompts (swap marks, negate
  facts); measure action divergence; tier 2 executes divergent branches from anchors. If
  actions are invariant under plan edits, the plan text is decorative — and the KD-gap
  scalar is fiction. Runs on archived first light for tens of dollars. (gap-5#9)
- **I10. The belief ledger: verbalized state vs the snapshotter** [S–M] — Extract
  testable propositions from thinking text ("button is pressed", "I'm holding the cube")
  and grade each against the engine bit at that tick: belief accuracy, hallucination
  rate, staleness lag. Step 12 becomes a measurement: did the model verbalize the
  side-effect before or after the percept showed it? (gap-5#10)
- **I11. The blame test: failure explanations vs engine-truth causes** [S] — Grade stated
  explanations of every BLOCKED/WALL against the true blocker entity (B3 attaches it).
  Misattribution rate per failure class; high misattribution on WALL says the *percept*
  is failing, not the model. First light already contains 12+ gradable explanation
  events. (gap-5#11)
- **I12. Tampered-percept probes: the honest-liar test** [S–M] — Flip exactly one bit of
  evidence (text mark list vs truthful pixels, or the reverse); measure
  text-vs-pixels-vs-flags-conflict, and whether verbalized reasoning mentions the
  evidence actually used. Known-cause, single-bit unfaithfulness detection; doubles as
  percept-trust hygiene. (gap-5#12)
- **I13. Epiphenomenal-CoT ablation** [S] — thinking off/medium/high + a replay arm with
  stored thinking stripped; same runs as C4, different question: is the trace causally
  upstream of action (monitorability), per chamber tier? Free paper section. (gap-5#13)
- **I14. The audited leaderboard + deception-trajectory dataset** [S–M] — Exploit%,
  Confession%, Belief-accuracy, Plan-faithfulness columns next to Solved%; package
  adjudicated runs as labeled honest-vs-exploit, faithful-vs-confabulated trajectories
  through the F2 exporters. Anyone can publish Solved%; only this benchmark ships its
  own audit. (gap-5#14)
- **I15. The safety & interpretability pitch annex (budget lines 5–6)** [S] — Two pages
  mapping the Temptation Lab to GDM's AGI-Safety agenda (reward hacking, amplified
  oversight) and the Faithfulness Lab to interpretability; includes the dual-use
  paragraph (glitch search: offense for routing, defense for safety). (gap-5#15)
- **I16. GLaDOS the unreliable narrator** [S–M] — In-world text/VO that is sometimes
  helpful, sometimes adversarially wrong ("the cube is behind you" — it is not), scored
  on whether the agent weights world evidence over narration. Embodied prompt injection
  with zero new machinery once captions are percepts; literally the character — the
  fiction does the framing for free. (merged: moonshots#19, gap-3#12)

---

## J. Platform & ops — the instrument as the durable artifact

*Every dead competition left a living platform; every dead platform died of unreliability,
closedness, or maintenance vacuum. Today the instrument is real but lives on one Arch box
behind a launch incantation. Source: harness-platform; economics from gap-2.*

- **J1. Protocol versioning + capability negotiation (the v1 freeze)** [S] —
  `harness_version` + `capabilities` in HandshakeResponse, `min_required_version`,
  reserved tags, and `docs/protocol-stability.md` (frozen vs experimental). The moment a
  second person runs this, sar.so/client skew becomes the #1 support burden; the cheapest
  "we are a platform, not a script" signal. (harness#2)
- **J2. One canonical `p2harness.Session` core** [M] — Factor the common 80% of
  `rl_challenge_env.py` and `testchamber_session.py` into one Session (connect, AgentLoop
  lifecycle, SHM map, delta merge, reset, crash recovery) with three thin facades:
  GymEnv, MacroSession, RecorderTap; kill the dead `py/rl/rollout.py`. "One harness,
  three consumers" is currently true at the proto layer and false at the Python layer.
  (harness#3)
- **J3. SHM framebuffer v2: header + seqlock + tick/camera metadata** [S] — Magic,
  version, dims, monotonic seq (torn-read safe), `server_tick`, camera pose at capture.
  Fixes a latent race, makes frame↔tick provable, and gives world-model consumers
  camera-conditioned frames. (harness#4)
- **J4. The container: one-command reproducible runtime** [M] — OCI image with
  steam-runtime + gamescope + steamcmd + sar.so; downloads the Portal 2 depot on first
  run under user credentials (ships zero Valve assets); acceptance test =
  `agentloop_smoke.py` green from a fresh cloud VM. The single biggest gap between
  "works on my Arch box" and "an external lab reproduces first light in an afternoon."
  (harness#5)
- **J5. "p2harness" v1.0: pip package + versioned binary releases + `doctor`** [M] —
  Client, stubs, Session, launcher, viewers; GitHub Releases ship sar.so version-locked
  to the wheel via J1. Crafter's lesson: zero-friction install out-impacts funded
  competitions. *Deps: J1, J2; name per J17.* (harness#6)
- **J6. Determinism envelope: measure it, version it, gate on it** [M] — K seed scripts ×
  R replays × machines; hash per-tick snapshots; publish first-divergence distributions
  per element class ("player kinematics tick-exact; airborne props diverge ~tick N±M") +
  a CI job that fails on regression; extend upstream `RNGManip` pinning. "Deterministic
  engine" is only semi-true and a VP's DD finds that on the TAS wiki in an afternoon —
  own the number. (merged: harness#7, eval#17, speedrun#15)
- **J7. Throughput program: render-on-demand + timescale + the published table** [M] —
  Skip rendering on pixel-less ticks (`mat_norendering`/SetSkipping), `host_timescale`
  free-run for state-only work, and `bench_harness.py` printing ticks/sec per mode per
  instance count. Gap-2's ROI memo aims it: VLM eval is model-latency-bound (skip);
  backfill/search/locomotion-RL are engine-bound (build, 10–50×). (merged: harness#8,
  speedrun#6, gap-2#14)
- **J8. `harnessd`: crash recovery as a platform layer** [M] — One-file supervisor owning
  N instances: health checks, restarts, warmup, instance leases, and a `session_epoch`
  in the handshake so clients detect "this is not the world you were talking to." The
  32-bit process *will* die; recovery currently lives ad-hoc in the RL worker.
  (harness#9)
- **J9. Fleet test: 32–128 instances, one box → many** [M–L] — Measure VRAM/CPU/SHM per
  gamescope instance, find the per-GPU ceiling, fix port ranges/VPK-lock stagger/Steam
  singleton issues; then flat YAML of harnessd endpoints — no k8s, no Ray until >3 boxes
  or >1 user. "Scalable evals" is the pitch's load-bearing adjective. (harness#10)
- **J10. Software-rendering spike: lavapipe** [S] — One day: does gamescope+Vulkan run on
  CPU at 640×480 ≥30fps? If yes, the farm escapes GPU procurement; if no, a crisp
  hardware-requirements line. (harness#11)
- **J11. Stats RPC + structured logging** [S] — Ticks, RPC latencies, SHM copy µs,
  snapshot walk µs, macro result-code histogram, crash counts. Fleet ops without metrics
  is archaeology; the result-code histogram is also free science (actuation failure rates
  per verb per chamber). (harness#15)
- **J12. Self-hosted CI gate** [M] — Every PR: build sar.so, boot one container instance,
  run `agentloop_smoke.py` + the determinism quick-check + golden wire-compat
  transcripts. The smoke-test rule is currently enforced by one person's discipline; CI
  is what makes agentic-coding-heavy development safe. *Deps: J4.* (harness#16)
- **J13. Chaos suite: executable war stories** [M] — SIGKILL mid-macro/mid-Reset,
  3GB-address-space exhaustion, dropped streams, corrupted SHM seq; assert recovery
  invariants hold and the tick condvar never deadlocks. Turns the phase4 graveyard prose
  into regression armor. *Deps: J8.* (harness#17)
- **J14. CostLedger + `cost_report.py` + the price-sheet YAML** [S] — Per-episode ledger
  (tokens by modality, cache hits, wall-time split, instance-seconds, crashes) written
  next to every trajectory; a script repricing any archive under any vendor's prices.
  First deliverable: first light priced at $0.22, in the repo, citable. (merged:
  gap-2#1,2,3)
- **J15. Instance-hour price discovery** [S–M] — Instances-per-box and $/instance-hour on
  local, cloud GPU, lavapipe; saturation points; watts. The denominator of every number
  in the economics model, currently a 13×-wide guess. (gap-2#4)
- **J16. The crash tax** [S] — Crashes per instance-hour, relaunch wall time, expected
  lost spend per crash → episode-cost multiplier. Decides with a number whether harnessd
  is load-bearing or polish. (gap-2#5)
- **J17. Open-source strategy: three rings, a name, a steward** [S decide, M execute] —
  Ring 0 public (plugin, proto, client, viewers, baseline agent, public suite), Ring 1
  gated (held-out chambers, golden transcripts), Ring 2 private (checkpoints, decks,
  ingestion DB); a trademark-safe name (not "Portal2Gym" — Gym is a dead category and a
  lawsuit-shaped word); the fork-surface audit (what `git clone` hands a competitor,
  including `brainstorm/` war stories — decide deliberately whether they ship). (merged:
  harness#21, gap-1#14, pitch#24a)
- **J18. Multi-title audit: prove "platform" with a second game** [M] — Run the recon
  protocol on Aperture Tag or Portal Stories: Mel; ship one annotated-percept chamber.
  Converts "Portal 2 harness" into "Source-engine embodied-agent harness, Portal 2
  first." (harness#20)
- **J19. Strata / Portal 2: CE spike — the escape from 32-bit** [M spike, XL port] —
  Time-boxed feasibility report against P2:CE (64-bit, Valve-licensed): what survives the
  engine-branch jump. The substrate is a depreciating asset; a 5-year pitch needs a
  written engine-succession plan, and P2:CE is also the warmest Valve path. (merged:
  harness#19, moonshots#22)
- **J20. Windows recorder build check** [S] — Does the MSVC solution even build
  HdemRecorder/MarkTable into sar.dll without the gRPC stack? The community-recording
  coup (F7) silently assumes yes; runners play on Windows. (completeness §3.2)
- **J21. Reference open-weights baseline + model-deprecation protocol** [S] — A pinned
  open-weights agent as the permanently reproducible leaderboard row, a stated re-run
  protocol on model retirement, and archive the first-light model snapshot string this
  week before the API alias drifts. (completeness §3.4)

---

## K. Benchmark productization & competition

*The platform is the product; the competition is marketing for it. Launch two tracks
honestly, not four aspirationally. Source: benchmark-competition; merges from harness,
moonshots, gap lenses.*

- **K1. Agent protocol spec: agents are callables, eval owns the game** [S] — Freeze
  `next_action(observation) → command-string` with two transports (subprocess JSON-lines;
  HTTPS endpoint), versioned, with conformance tests. The anti-MineRL move: inference-only
  means bounded hosted cost and closed-source labs can enter. (benchmark#5)
- **K2. First Light Starter Kit** [M] — Prebuilt sar.so, pinned env, T1 chambers, the
  Gemini agent + an Agent stub, `make first-light`, a scripted agent that self-tests
  without an API key, honest gamescope/license docs. Day-1 participants must reproduce
  first light in an afternoon or they churn; also the artifact a VP's team runs
  themselves. (merged: benchmark#3, moonshots#23)
- **K3. `p2bench`: pip-install local eval with one scalar** [M] — `p2bench eval
  my_agent.py` → tier-weighted geometric-mean success + per-tier table + trajectories,
  against the player's own install. The Crafter play: a benchmark individuals adopt in
  papers is the durable flywheel. Linux-only v0, said out loud. (benchmark#4)
- **K4. Trajectory-audit anti-cheat** [M] — No score without its replayable artifact;
  spot-replay with tolerance rules (Source is semi-deterministic) + telemetry sanity +
  binary/map integrity hashes. Borrows 20 years of speedrun verification culture; the
  artifact requirement is secretly the dataset engine. (benchmark#13)
- **K5. Budget standardization: tokens are the FLOPs of the frozen track** [S] — Hard
  caps (in/out/thinking tokens, steps), pause-while-thinking sanctioned, declared model
  ID + scaffold; cost-per-solve on the board. Without budget rules the track measures
  wallet size. (benchmark#12)
- **K6. Attribution leaderboard: perception / reasoning / actuation columns** [M] —
  Solved%, Reasoning-Solved% (oracle-actuated replay), actuation-loss, KD-gap,
  tokens/solve — the macro boundary as a *scoring instrument*. A leaderboard that tells
  entrants which layer to improve is harder to Goodhart and is the thesis rendered as a
  metric. *Deps: D1, D6, C8.* (benchmark#11)
- **K7. Track architecture: four tracks, two at launch** [S doc] — Launch: Track A
  (frozen agents, token-capped, strict + open divisions) and Track B (TAS/routing).
  Year 2, gated: Track C (learning, sample-capped on generated chambers) and Track D
  (offline-from-human-data, gated on F1). The tracks ARE the org chart of the pitch.
  (benchmark#14)
- **K8. The TAS/routing track with p2sr as co-organizer** [M eng + calendar] —
  `.p2tas` submissions, replay-verified with desync tolerance, glitchless and
  anything-goes divisions, p2sr members as verifiers. Imports an existing obsessive
  community on day one; the benchmark lens's spiciest take: the speedrunners are the
  Farama Foundation this platform needs — they pay the year-3 maintenance bill.
  (benchmark#15)
- **K9. Hosted eval server + rolling leaderboard** [L→XL] — Persistent BALROG-shape
  service: submissions vs held-out chambers on a harnessd fleet, rate-limited,
  static-site leaderboard with trajectory drill-down. This is where the honest answer
  starts to be "this is where headcount goes" — per gap-2, only viable with per-entry
  dollar caps. (merged: benchmark#6, harness#23)
- **K10. NeurIPS competition-track proposal package** [M] — Tracks A+B, attribution
  metrics, human-normalized scores, local-first infra, held-out protocol, $10–20k
  prizes, retrospective co-authorship, 3–5 recruited co-organizers (a BALROG-adjacent
  academic, a p2sr maintainer, ideally one frontier-lab person). Only credible once
  A1/D2/K1–K4 run as code. (benchmark#19)
- **K11. Competition season P&L** [S] — Price one season under the inference-only
  protocol: ~$100–300 of game compute vs a $10–20k prize pool — "organizer COGS ≈ 1–3% of
  prizes" inverts every dead competition's budget and sets the sponsor ask correctly.
  (gap-2#12)
- **K12. Pilot the competition as one course's semester project** [M, spread] — One
  allied professor's grad course runs "climb the P2 ladder" with a private 5-chamber
  held-out set. Find the operational fires with 25 friendly students, not 200 anonymous
  teams; the class report is the pilot-evidence section NeurIPS reviewers ask for.
  (gap-6#13)
- **K13. Capture the BALROG column** [M] — PR Portal 2 into BALROG (and/or lmgame-bench)
  as an environment: their leaderboard, our environment. The cheapest way to win the
  citation clock, and it pre-empts someone else owning the integration. (merged:
  gap-1#11, gap-6#4a)
- **K14. Spectator layer: model-vs-model chamber races** [M] — Auto-caster composing
  side-by-side runs (annotated frames + scrolling reasoning + step counters) from
  trajectories; live-stream marquee matchups on model releases. Visible thinking +
  visible failure is uniquely watchable; this is the Kaggle-Game-Arena-shaped slot in a
  VP's head. (benchmark#21)
- **K15. Stewardship & exit plan, written on day one** [S] — Licenses, who maintains
  what, the succession ladder (solo-RE → p2sr co-maintainership → Farama-style adoption →
  archived-with-dignity), publication pipeline commitments. "Who maintains it in year 3"
  is the question every dead platform failed. (benchmark#22)

---

## L. Co-op, multi-agent & spectacle — the moats nobody can fork

*Moonshots' core claim, sharpened by gap-1's teardown: single-agent chamber-solving is
commodity within a quarter of publication; author-able interdependent 3D-physics co-op and
the speedrun-community bridge are the assets money can't fast-follow.*

- **L1. The Atlas & P-Body protocol: two frozen agents, one chamber, a chat channel**
  [M smoke → L benchmark] — Splitscreen co-op (`ss_map`) keeps both players in ONE
  process (no networking, one snapshotter); slot-indexed MacroExecutor + per-player
  percepts + a `say` verb; two `next_action` agents turn-by-turn against the frozen
  world. PeTI authors co-op chambers natively — puzzles interdependent *by construction*.
  Claim sharpened per gap-1: "author-able interdependent first-person 3D-physics co-op,"
  not "only grounded two-agent benchmark on Earth" (Overcooked exists). (moonshots#1)
- **L2. Emergent coordinates: asymmetric-information co-op** [M on L1] — A sees the
  button, B sees the door, the chat channel is the only bridge; measure solve rate vs
  shared-percept control + transcript analysis (do they use mark numbers as common
  ground?). Grounded emergent communication between frozen models in 3D has zero good
  testbeds. *Deps: L1, D9.* (moonshots#9)
- **L3. Play co-op WITH Gemini: human-agent teaming** [M on L1] — Human plays Atlas
  live; agent plays P-Body at macro altitude; turn-based "chess mode" resolves the
  freeze conflict. The single most legible demo for any audience — SIMA 2's "plays with
  you" framing in a game with real puzzle dependency. (moonshots#10)
- **L4. "Gemini Plays Portal 2", 24/7** [M] — Crash-resilient eval loop on a
  chat-voted workshop-chamber queue, streaming annotated frames + live thinking + macro
  log (viewer as OBS overlay); failures are content. Per gap-2: viable only post-C1
  (~$5–8/day vs $30+). Continuous public eval + organic chamber discovery + trajectory
  trickle. (moonshots#11)
- **L5. One chamber, N tasks: constraint-conditioned solving** [M] — Same chamber,
  different instruction ("never let the cube touch goo", "press the button exactly
  twice"), verified as predicates over the entity stream — BASALT's fuzzy
  instruction-following with formally checkable rules for free; multiplies every chamber
  into a task family. (moonshots#8)
- **L6. The Wormhole Test: portal mechanics as THE non-Euclidean reasoning benchmark**
  [M] — 25–40 micro-chambers each isolating one portal primitive (momentum redirection,
  fling prediction, seeing-yourself-through, topology re-fires, portal reachability);
  per-primitive capability fingerprints. Per gap-1's teardown, portal physics is the only
  literal monopoly on the board — publish it first (release-order strategy M38).
  Interactive sibling of the F12 prediction dataset. *Deps: B10.* (moonshots#4)
- **L7. The Gauntlet: multi-chamber campaigns and the memory wall** [M] — 5–10 chambers
  in one continuous session; compare memory strategies (full history / last-N / notebook
  / summary) against verifiable per-chamber milestones. Long-horizon memory is THE open
  scaffold question; here it's measurable against ground truth. (moonshots#21)
- **L8. Macro grammar as assistive tech: play Portal 2 by voice** [S–M] — STT → command
  grammar → MacroExecutor; world-freeze makes the game turn-based, which IS a
  motor-accessibility feature nobody noticed. Four payoffs: new contributor community, an
  accessibility funding lane, the friendliest Valve-goodwill story, and macro-altitude
  human baselines as a side effect. (gap-6#9)
- **L9. Interactive teachability: one human hint mid-run** [S–M] — Measure solve-rate
  lift per hint — the SIMA-2 "learns with you" eval, distinct from system-injected loop
  feedback. (completeness §1.8)

---

## M. Pitch, demand, economics & the human funnel

*The gap lenses' collective correction: the council is a supply-side monoculture. Code got
cheap the day the agent showed up; calendar, allies, and demand evidence are the binding
constraints. The VP meeting is a financing event, not demand — seed investors fund
traction. Sources: pitch-strategy, gap-0 (demand), gap-1 (teardown), gap-2 (economics),
gap-6 (humans).*

### Pitch assets (pitch-strategy lens)

- **M1. The claim ladder** [S] — Rung 1 (lead): *a failure-attribution instrument for
  embodied reasoning in frozen models.* Rung 2: contamination-resistant benchmark with
  formal difficulty and infinite UGC refresh. Rung 3 (say once, needs F1 first): embodied
  trajectory data engine. Rung 4: "AGI via Portal 2" — permitted exactly once, as the
  closing-slide title, with a smile. Message discipline across every artifact. (pitch#13)
- **M2. The pitch-slot mapping memo: six budget lines** [S] — Map assets to slots
  DeepMind already funds: SIMA (annotated commercial-game agent data), Game Arena
  (spectator evals), Genie (the verifier/calibrator their loop lacks), Gemini Robotics
  (RLDS trajectories) + gap-5's two: AGI Safety and interpretability. "RL benchmark" is a
  dead procurement category; these six are live. Tells you which VP. (merged: pitch#4,
  gap-5#15)
- **M3. The demo ladder: cold open → live run → "the VP authors a chamber"** [M for tier
  3] — Tier 1: the archived first-light viewer, 90 seconds, no preamble (step 7, step
  12). Tier 2: live run via the viewer's live mode + the VP typing `go_to 7` in the REPL
  themselves. Tier 3 (the closer): the VP drags cube/button/door in the in-game Puzzle
  Maker and the agent attempts *their* chamber five minutes later — authorship-to-eval
  inside one meeting, which no other benchmark on earth offers; also makes held-out
  *physical*. *Deps: B2, A1.* (merged: pitch#3, #5, #23)
- **M4. The 90-second sizzle video** [S] — Annotated agent view + thinking subtitles +
  top-down map, steps 0–7 + step 12 + ten honest seconds of the flail. VPs forward
  videos, not repos. (pitch#6)
- **M5. The "Why Portal 2" comparison table** [S] — vs Minecraft (saturated, no formal
  grounding), NetHack (symbolic, no physics), custom sims / Genie worlds (no humans, no
  ground truth): deterministic-steppable engine, tick-perfect state, in-game editor,
  ~950k workshop items (re-verify the count first), PSPACE-complete element families,
  no studio partnership needed. Pre-state the caveats (semi-determinism, 32-bit) to buy
  credibility. (pitch#7)
- **M6. The velocity exhibit — reframed** [S] — Mine git history: what one RE + agentic
  coding shipped in N weeks. Skeptic P7 warning baked in: present it as *which work was
  compressible and which was calendar/relationship-bound* — otherwise the exhibit prices
  the marginal hire DOWN ("why fund anyone? wait two quarters"). (pitch#8)
- **M7. The headcount ask as five named, de-risked workstreams** [S] — Engine/harness RE,
  eval scientist, locomotion/RL researcher, data engineer, 0.5 community/competition ops
  — each anchored to a working artifact that retired its risk. "Here are five seats whose
  first quarter is already specced" is how asks get granted. (pitch#9)
- **M8. 3/6/12-month milestones with explicit kill criteria** [S] — e.g. "if frontier
  models saturate tier-3 by month 4, pivot headroom to routing"; "if Valve declines,
  scope to owned+licensed chambers." Kill criteria are the highest-signal credibility
  device a solo pitcher has. (pitch#10)
- **M9. The risk register slide** [S] — Valve/IP, engine determinism, VPT-moment
  obsolescence, bus factor, 32-bit — each with named mitigation, stated before they ask.
  Naming Valve first denies the room its easiest derail. (pitch#12)
- **M10. The why-now slide** [S] — Environments became a procurement category (2025–26);
  DeepMind leadership publicly endorses games-as-eval *right now* (Game Arena); SIMA 2 +
  Genie 3 concede noisy success detection / hallucinatable physics — a real engine with
  ground truth is the missing verifier. Skeptic demands quote-level accuracy and a named
  source for the "$1B/yr" rumor — verify or cut. (pitch#14)
- **M11. The pre-read as a two-way instrument** [S] — The 2-page SIMA-gap memo sent to
  3–5 ICs/TLs *with questions attached* (what would make your team run a model on this
  within a quarter? which artifact is closest to useful?). VPs decide on a trusted TL's
  recommendation; pre-wiring converts evaluation into confirmation — and the replies are
  demand data for the ledger (M22). (merged: pitch#16, gap-0#12)
- **M12. The anti-saturation answer: routing headroom + win-either-way** [S] — Routing
  against a live human leaderboard has no engine ceiling; the generator+UGC refresh make
  the test set scale-resistant; and if scale solves it anyway, the instrument is what
  proves and attributes that — it appreciates when models improve (VPT made Minecraft
  data *more* valuable). Skeptic claim-check: do NOT say "chess saturated in a week" —
  Game Arena showed LLMs are *weak* at chess; the defensible line is "closed games with
  superhuman engines make poor headroom stories." (merged: pitch#17, #20)
- **M13. The numbers slide, measured** [S given J14/J15] — $/eval-run (current vs O(1)),
  ticks/sec, instances/box, save/restore latency, bytes/trajectory, time-to-add-a-verb,
  the BALROG cost-parity decomposition ("3D embodiment at ~2× text-game prices, post-C1"),
  and the data COGS table (human play 5–30× under teleop; backfill ~1000×;
  auto-annotation ~1000× under human labeling — with better labels). Eval cost killed or
  constrained MineRL/BASALT/Procgen; a VP probes this immediately. (merged: pitch#21,
  gap-2#6,7,8)
- **M14. The failure museum** [M] — Named failure classes with one viewer permalink each
  (the locomotion loop, dead-end fixation, plan-correct/actuation-blocked, the step-12
  recovery as a *success* class), each annotated with the workstream that addresses it.
  The museum IS the headcount ask, evidenced. (pitch#22)
- **M15. Landing page + pull sensor** [S] — Sizzle video, first-light viewer permalink,
  three sentences, and a request-access form asking one question: "what would you do
  with it?" Measure unsolicited pull — the only demand signal that arrives while you
  sleep. Stake the org/name (per J17). (merged: pitch#24, gap-0#14)
- **M16. The reserve framing: "fund the instrument, not the result"** [S] — If the room
  signals benchmark fatigue, pivot live: the product is failure attribution for embodied
  agents; benchmark, competition, and data engine are *applications*. Truer to what was
  built anyway. (pitch#19)
- **M17. The Waymo resim memo** [S] — Don't claim game→driving transfer; map harness
  primitives to the AV-sim stack they independently converged on (log replay = .hdem
  resim; anchors = what-if branching; auto-annotation = scenario mining; mutation ops =
  long-tail generation). "One RE rebuilt the resim stack on a $10 game" — and delete the
  naive Waymo line from every doc per skeptic P6. (completeness §1.6)

### Demand discovery (gap-0) — the missing workstream

- **M18. The demand spec: 8-segment map + JTBD canvas** [S] — Promote the segment table
  (frontier eval teams, academic labs, planning community, world-model teams,
  robotics-data buyers, competition researchers, speedrunners/mapmakers = segment zero,
  eval-methodology orgs) into a repo doc with named humans, network paths, disqualifiers,
  and what counts as a win. Until names are attached, demand work can't be scheduled or
  falsified. (gap-0#1)
- **M19. The build-on-pull rule + MVA matrix** [S] — No segment-facing artifact gets
  built past spike quality until a named person in that segment asked for it in an
  interview. Kills or defers 2–3 council L-items; one RE cannot afford a single mis-built
  L. (gap-0#2)
- **M20. Mom-Test interview kit** [S] — Never pitch first; ask about past behavior and
  money ("what did you evaluate and *reject* recently?", "what killed the last dataset
  you didn't ingest?"); commitment and referral closes; a disqualifier list ("looks cool,
  keep me posted" = polite no). (gap-0#3)
- **M21. The 10-interview discovery sprint** [S effort, M calendar — start this week] —
  ≥10 interviews across the segments before any VP meeting: BALROG authors, NetHack/
  BASALT/ViZDoom alumni, MineDojo/Voyager, an agent-evals IC at GDM, ICAPS organizers,
  Danijar Hafner, LeRobot, p2sr maintainers, Farama maintainers (a decade of env
  post-mortems in 30 minutes). The first-light viewer link is a cold-email attachment no
  other env project has. (gap-0#4)
- **M22. Pre-commitment ladder + demand ledger** [S] — L0 replied → L1 call → L2 ran an
  artifact on their machine → L3 spent their compute/budget on it → L4 public attachment
  (LOI, co-organizer) → L5 resources. The project's demand metric is count of L3+,
  reviewed monthly next to supply milestones. (gap-0#5)
- **M23. Pre-registered kill/accelerate table** [S] — Bind findings to backlog changes
  *before* the interviews ("≥3 cite install/legal friction → accelerate hosted-eval-lite,
  demote pip-first"; "zero L3 after 10 interviews → postpone the VP meeting"). Converts
  interviews from vibes into a branch instruction. (gap-0#6)
- **M24. The over-the-shoulder test** [S] — Watch 1–3 outsiders install and run the
  starter kit on a screen-share, silently; record time-to-first-frame and every README
  lie. The first run will be humbling (multilib, gamescope, /opt/p2-grpc32). (gap-0#7)
- **M25. Design-partner program: three named slots** [S–M over weeks] — One academic lab,
  one world-model/eval team, one community partner; white-glove support + grammar/suite
  influence + co-authorship in exchange for friction reports and L3 evidence. Kill
  criterion: 2 slots unfilled after 8 weeks = the demand hypothesis fails at current
  artifact strength, reported honestly. (gap-0#8)
- **M26. Pre-sell before building: wormhole spec + planning-domain probe + data-buyer
  screening** [S each] — (a) 2-page wormhole eval spec + 5 teaser clips to world-model
  people, build the 1k set only on a yes; (b) routing-domain proposal to ICAPS before the
  export exists; (c) interview 3–5 actual data *consumers* on format reality and
  legal/consent screening before hardening any exporter — "trajectories for Robotics" is
  the axis with the highest wishful-thinking risk. (merged: gap-0#9,10,11)
- **M27. The demand-evidence slide** [S] — Segment → named team → ladder level → verbatim
  quote → what they asked for next, including the pre-registered negatives ("robotics
  data: 3 interviews, all blocked on consent provenance — pillar demoted"). The only
  argument a skeptical VP can't attribute to founder enthusiasm. (gap-0#15)

### Competitive posture (gap-1)

- **M28. The 2026 teardown dossier** [S] — Every moat claim × {BALROG, Cradle,
  VideoGameBench, Voyager/Mineflayer, FLE, SIMA 2, Genie 3, ARC-AGI-3, ProcTHOR, OSWorld,
  Game Arena…}: has-it / builds-it-in-X-weeks / structurally-can't, with citations. No
  doc on disk survives a reviewer who knows Cradle exists. (gap-1#1)
- **M29. Two-battlefields doctrine** [S] — Tag every roadmap item [env-vs-env] (Portal 2
  vs Minecraft — won by game structure: PeTI, portals, demo archive, co-op) or
  [team-vs-team] (us vs a fork of this MIT repo — won only by clocks, process, social
  capital, brand). Decks may only use battlefield-1 assets for "why Portal 2" and
  battlefield-2 for "why us." (gap-1#2)
- **M30. Attack yourself: the Minecraft fast-follower timebox** [M, hard-capped 2 weeks]
  — Rebuild the percept/act layer on Mineflayer (SoM frames, pathfinder go_to,
  MacroResults, .trajectory) and publish the measured cost as a *portability* result.
  Either outcome wins: fast = the protocol generalizes (second env for free); slow = the
  moat story strengthens. (gap-1#3)
- **M31. The computer-use null hypothesis** [M, run BEFORE the pitch] — Strongest
  available screen-scraping agent (no SoM, no macros, no pause) on the same chambers,
  same model, same budget: solve rate, $/solve, attribution quality. The 2026 question
  isn't "could they rebuild your harness" — it's "why need a harness at all when
  computer-use plays games from pixels?" If stock computer-use solves testchamber_000,
  the instrument pitch retreats to attribution+cost+ground-truth; if it flails, the
  delta is the best number in the deck. (gap-1#4)
- **M32. The internal-rebuild wargame memo** [S] — Write the build-vs-buy-vs-fund memo a
  GDM TL would write, better: Path A fork-the-MIT-repo (a quarter, but no clocks, no
  community bridge, no me), Path B Valve partnership (12–24 months of legendary latency),
  Path C SIMA/Genie/computer-use reuse (no ground truth, by their own published caveats).
  Conclusion: funding the existing thing is the cheapest path to the asset — the pitch is
  structurally an acqui-hire; write it that way. (gap-1#5)
- **M33. The Valve packet + the latency-asymmetry analysis** [S work, months calendar —
  start now] — Outreach: what we do, what we ask (tolerance for headless farms where
  every instance is licensed, workshop download-on-demand, community-event branding),
  precedents deployed honestly (OpenAI Five = you need a deal; CSGO dataset = unsued
  academic; P2:CE = engine license; tolerated SAR = goodwill) — never as clearance.
  Gap-1's corollary: Valve's unresponsiveness is *protection*; do NOT ask for an official
  API — it would be issued to every lab equally and obsolete this codebase. Include the
  Steam-account topology for fleets and an education-use paragraph. (merged:
  benchmark#20, harness#22, data#20c, gap-1#6, completeness §3.5)
- **M34. The SIMA-gap probe: measure their evaluator's error with our oracle** [M] —
  Score identical runs three ways: engine oracle / Gemini-judge video rubric (SIMA-style)
  / human labels; publish the confusion matrix. Evidence — not assertion — that the
  harness is the calibrator the SIMA/Genie loop lacks; the pre-read's centerpiece.
  (gap-1#8)
- **M35. Claim triage + the hygiene pass** [S] — Retire ("LLM plays a commercial game" —
  Cradle did it; "only two-agent benchmark"), sharpen ("800k chambers" → "800k *tasks
  with win conditions* + 15y of human votes"), keep-and-lead (per-step ground-truth
  attribution on a commercial 3D physics binary; portal physics monopoly;
  action-bearing expert archive). Apply the skeptic's Part-2 worklist (14 shaky claims)
  to every externally visible doc. Credibility fails like a dam, not a dial. (merged:
  gap-1#9, skeptic Part 2)
- **M36. Release-order strategy: ship by defensibility, not readiness** [S] — R1 the
  wormhole/portal-physics eval (monopoly), R2 the attributed leaderboard (instrument +
  brand clock), R3 the harness container/protocol (deliberately given away as
  standard-setting), R4 the human corpus (clock-gated, licensed). Default behavior ships
  the forkable harness first — handing the fork its head start before any clock runs.
  (gap-1#10)
- **M37. Landscape tripwires** [S] — Quarterly deep-research scan (SIMA/Genie releases,
  ARC-AGI-3 adoption, Mineflayer-SoM repos, any Portal/Source agent paper); no deck cites
  a landscape older than 90 days. (gap-1#13)
- **M38. The kill-list economics audit + marginal-claims register** [S] — Every council
  idea gets a COGS line and a verdict (killed/wounded/blessed — per gap-2: per-node LLM
  priors and full-chamber pixel RL are killed; O(1) context, TAS track, auto-annotation,
  human corpus, locomotion RL are blessed); every "near-zero marginal cost" claim in any
  doc gets a number or dies. A VP's DD team will do exactly this pass; do it first.
  (merged: gap-2#11, #13)

### The human funnel (gap-6) — sends, not builds

- **M39. The first-light post + monthly research notes** [S — publish FIRST] —
  `first_light_and_next_steps.md` is already 90% of a great post ("solved the puzzle,
  couldn't walk out of a glass box"). Every recipient of every email below will look me
  up; what they find decides the reply rate. Also timestamps public priority. (gap-6#14)
- **M40. The Demaine/Lynch letter** [S, send this week] — "Your reductions are now
  executable" + the viewer link; asks smallest-first: a 30-min gadget-mapping
  sanity-check → complexity-claims review → co-authorship on the gadget compiler or the
  competition proposal. Every lens leans on their theorem; nobody proposed telling them.
  One famous co-signer inoculates the formal framing. (gap-6#2)
- **M41. The TeamSpen210 ping** [S, send this week] — Three questions to the BEE2.4/
  srctools maintainer: is headless `.p2c→.bsp` viable, where does it break, paid bounty
  for a `p2c_compile` CLI? The highest information-per-character action available —
  collapses the puzzle-gen lens's single load-bearing unknown (E2). (gap-6#3)
- **M42. BALROG handshake + the ally map** [S per email] — Artifact-shaped offers to 5–8
  named groups whose published limitations this instrument answers ("we do the
  integration, you get a hardest-tier 3D environment"). Adoption happens through people
  who co-own results, not to artifacts. Feeds K13. (gap-6#4)
- **M43. A research wing inside p2sr** [S + 2h/month] — `#sar-research` channel, monthly
  office hours, `CONTRIBUTING-RESEARCH.md` mapping community skills to needs (SAR C++
  devs → verbs; TASers → glitch suite; mappers → chambers; runners → corpus). The only
  community on Earth pre-qualified to contribute C++ to a Source plugin; bus-factor=1 is
  fixed by making the second contributor's first hour frictionless. (gap-6#5)
- **M44. The contribution ladder: chambers are the no-code on-ramp** [S] — 15–20 curated
  good-first-issues in three tracks: no-code (author a PeTI chamber + manifest = mint a
  benchmark item in an evening), Python-only (viewers, exporters, analysis), C++ (small
  verbs behind the smoke gate). The benchmark's scarcest input needs zero code. (gap-6#6)
- **M45. Plan-B funding matrix: eight funders, two applications this month** [S–M] —
  Emergent-Ventures-class talent bets, open-science grants, Open Philanthropy
  (METR/Epoch-shaped), Kaggle prize sponsorship, GitHub Sponsors, the
  professor-writes-it-into-a-grant route. One funder + binary outcome is the riskiest
  plan in the portfolio; "funded either way" is the posture that makes plan A land.
  (gap-6#7)
- **M46. The credits stack** [S, forms not essays] — Apply to Google/OpenAI/Anthropic
  researcher-credit programs at once, first-light trajectory attached. Pays for exactly
  the pre-pitch hardening (the cross-model matrix) and arrives in weeks; every lab that
  grants credits becomes mildly invested in its row. (gap-6#8)
- **M47. Five pre-scoped MS theses: rent headcount from academia** [S to write, M to
  supervise — cap at 2 concurrent] — Learned go_to; difficulty predictor; perception VQA;
  loop/recovery metrics; mark-robustness. Infrastructure provided, data provided,
  co-supervision, venue named. Each completed thesis = a paper section + an adopter + a
  pre-vetted hire. (gap-6#10)
- **M48. Reproduction bounty + hall of fame** [S] — First 5 external reproductions of
  first light get named entries + small bounty + paper acknowledgment; verification =
  their `.trajectory` through the audit tooling. Until someone outside this repo
  reproduces it, first light is a single-author claim — and the drive is a brutal QA pass
  on the kit. (gap-6#11)
- **M49. The 2-hour tutorial notebook** [S–M] — Load a real `.trajectory`, render the
  viewer inline, write a 30-line rule agent against the protocol, score on golden
  decisions, compare with Gemini — zero game install. The top of every funnel: course
  kit, contributor day-0, and the executable pre-read a TL runs before the meeting.
  (gap-6#12)
- **M50. P2-Agents course kit: Berkeley-Pacman for embodied VLM agents** [M] — 3–4
  projects autograded offline against archived data (parse trajectories → implement
  `next_action` → prompt-engineer via the replay rig; optional live run for students who
  own the $10 game). CS188's Pacman is the most successful adoption artifact in
  AI-education history, and this one grades on files. Valve's own "Teach with Portals"
  is precedent. (gap-6#1)
- **M51. The open-research circuit talk** [S] — 20 minutes, viewer as the demo, at ML
  Collective / EleutherAI-style communities: co-authors, pre-submission reviewers, and an
  affiliation line for solo papers. (gap-6#15)

---

## Skeptic's corner — the red team, faithfully

*Full text: `.council/critique/skeptic.md` (a VP-Research red team). Severity: KILL-SHOT /
SERIOUS / ANNOYING. The meta-observation first: the council's systematic weakness is
rebuttals that are* work not yet done, presented at the confidence of work already done
*("~40 LOC", "the hook already exists"). A diligence pass will notice the answers are plans.*

1. **P1 — Contamination (KILL-SHOT for "reasoning is solved")**: every frontier model has
   read every Portal 2 walkthrough; the percept then *names* the objects; 7 steps of
   cube→button→door is schema retrieval with grounding assistance. *Rebuttal:* mechanics
   knowledge ≠ instance contamination (step 12 is in no walkthrough), and the controls
   are already designed (D4 isomorphic remakes, E5 mutants, E3 generator-fresh). *Defuse:*
   run the contamination battery before any external claim; retire the phrase "reasoning
   is solved."
2. **P2 — "So what?" against the 2026 bar (SERIOUS)**: 7 macro steps over ~10 marks with
   oracle perception may be 95% pipeline; a scripted greedy bot plausibly matches Gemini
   on tier 1. *Rebuttal:* it's an attribution instrument, not a difficulty record, and
   the probe ladder (E13) IS the baseline floor. *Defuse:* random/greedy columns on every
   chart forever; don't show the 7-step number to anyone senior until a tier exists where
   greedy fails and the teleport arm proves models fail there for reasoning reasons.
3. **P3 — Single-game / benchmark fatigue (SERIOUS; KILL-SHOT if the deck says
   "benchmark" early)**: the graveyard is real and both big labs exited the category.
   *Rebuttal:* in-game authoring + a maintenance community that predates the research
   (p2sr) + formal difficulty — three properties no graveyard entry stacked — and the
   reframe: a pausable, ground-truth, soon-branchable *simulator protocol*, instantiated
   twice (J18). *Defuse:* the what-this-measures-that-BALROG/OSWorld-cannot table, with
   receipts.
4. **P4 — Valve IP for a Google-branded anything (KILL-SHOT for competition/dataset;
   ANNOYING for internal research)**: the precedent stack proves less than deployed —
   OpenAI Five proves you need a *deal*; CSGO proves unsued academics exist; unexamined:
   Steam Subscriber Agreement vs headless fleets, frame redistribution. *Rebuttal:* the
   architecture is already licensing-defensive (assets-clean container, download-on-
   demand, lazy-pixel = frame-free public data) and the paths are warm. *Defuse:* open
   the Valve thread before the pitch; keep public artifacts frame-free until it resolves;
   never present precedent as clearance.
5. **P5 — A 32-bit retail binary from 2011 (SERIOUS)**: signature-scanned, runtime-hooked,
   3GB heap, rotting multilib toolchain. *Rebuttal:* finished game, stable binary, decade
   of SAR survival; pinned-depot containers + CI golden transcripts detect breakage in
   hours; the succession plan is named (P2:CE) — no dead platform had one written before
   launch. *Defuse:* ship the pinned container, run the P2:CE spike, publish the report.
6. **P6 — The robotics/Gemini-data story is mostly vapor (KILL-SHOT as worded)**: no
   contact dynamics, no sensor noise, anti-physical optimal play, low-hundreds runner
   population vs VPT's 70k hours — and "Waymo" costs the room's respect. *Rebuttal:*
   narrow to *embodied-agent pretraining/eval data*; two products are genuinely scarce —
   counterfactual branch pairs from identical physical states (F11; reality doesn't
   reload) and the wormhole eval (F12; ground truth Genie can't have) — and the claim is
   falsifiable for a few thousand dollars (F20). *Defuse:* run the VLA-mix experiment and
   report it either way; delete Waymo (M17 is the defensible version).
7. **P7 — Bus factor, double-edged (SERIOUS)**: funding doesn't transfer knowledge out of
   one skull; worse, the velocity exhibit invites "if one RE ships this much, fund zero
   and wait." *Rebuttal:* the stewardship ladder is written and p2sr makes succession
   non-hypothetical; the honest velocity story is that agentic coding compressed the
   *instrument* but cannot compress calendar-bound work (community, corpus, Valve,
   multi-month search research) — which is exactly what the five seats are. *Defuse:* one
   external contributor landing PRs; the formats spec published; reframe the exhibit
   around compressible-vs-not.
8. **P8 — Competition cost & ops (ANNOYING→SERIOUS)**: the unit of evaluation boots a
   commercial game; nobody wrote the $/leaderboard-row number. *Rebuttal:* inference-only
   protocol, local-first eval, externalized hosting, O(1) context, $10–20k prize norms —
   and gap-2 then *did* write the numbers (~$10/row at O(1); season COGS ≈ 1–3% of
   prizes). *Defuse:* the measured numbers slide + one full internal competition
   rehearsal with its all-in cost published.
9. **P9 — What does Genie make obsolete? (ANNOYING now, SERIOUS at 5 years)**: if
   infinite generated worlds work, a 2011 engine is a museum piece. *Rebuttal (the
   council's strongest):* generated worlds have no ground truth — their physics is the
   model's opinion; a real engine with per-tick state is the verifier/calibrator that
   loop publicly lacks; world models rising makes the reference instrument MORE valuable.
   *Defuse:* ship the wormhole eval and get one world-model team to run it.
10. **P10 — First light's science hygiene (SERIOUS)**: N=1, single seed, author-built,
    back half censored by the locomotion wall — the harness gave the model no way to fail
    at reasoning after step 7, so the benchmark is telling its authors the story they
    already believe. *Rebuttal:* it's day two, and the controls were proposed by the
    project before any reviewer asked (D1, C9, D3, D5, J6). *Defuse:* the minimum citable
    kernel — 3 models × 20 chambers × 5 seeds × {real, teleport} with baseline floors —
    before anything public.
11. **P11 — Throughput economics under the search/RL/data claims (SERIOUS)**: 60
    ticks/sec, 1× rendering, unmeasured save/load, vs Procgen's 10⁴–10⁵ steps/sec; the
    scale claims silently assume an unbuilt multiplier. *Rebuttal:* irrelevant for the
    frozen-VLM track (API latency dominates); the multiplier work is scoped and
    precedented in-repo (`sar_tas_skipto`, `mat_norendering`); scale claims are *gated*
    on J7, which is a named workstream. *Defuse:* measure ticks/sec, save/restore, and
    lavapipe; re-cost and demote in writing whatever dies.
12. **P12 — Where is the demand side? (SERIOUS)**: ~230 ideas, zero "talk to a user";
    benchmarks die of no adoption, not missing features. *Rebuttal:* partial only —
    instrument-before-audience was deliberate and the gate passed; there is no real
    counter except to go ask. *Defuse:* the 10 conversations (M21), results on one slide.
    (Gap-0 subsequently built this entire workstream.)
13. **P13 — Claim inflation in aggregate (SERIOUS)**: superlatives a TL punctures with
    one search each ("contamination is impossible", "only benchmark on Earth", "chess
    saturated in a week"); after the second puncture the room discounts the true claims
    too. *Defuse:* the hygiene pass (M35) over everything external, using the skeptic's
    Part-2 list of 14 specific shaky claims (PSPACE phrasing — family-hardness, never
    instance-hardness; the Game Arena chess reversal; "SAR mandatory" rule text; the 954k
    count; "~40 LOC anchors" = designed-not-tested; "RLDS = train Monday"; the
    $1B-procurement rumor; reconcile saturation predictions; etc.).

**The verdict, verbatim in spirit:** *"The macro-boundary attribution idea is genuinely
good and first light proves the pipeline runs. But you've shown me one model, one chamber,
one seed, on a game my models memorized, with no baselines, no controls, no users, an
unresolved IP question. Come back with the kernel."* The kernel: (1) the controlled
result (3×20×5×{real,teleport} + floors + contamination delta), (2) one settled economics
table, (3) the Valve thread opened, (4) ten demand conversations on a slide, (5) the
claim-hygiene pass. What he would NOT fund even if pre-empted perfectly: the hosted eval
service, the co-op track, the YouTube/IDM harvest, full-campaign routing — team-sized bets
on unvalidated primitives. Don't let the moonshots leak into the ask.

---

## Next 7 days — the reconciled shortlist

Seventeen "if I could only do ONE thing" picks, reconciled to five. The losers (co-op
smoke test, puzzlegen DSL, anchors+best-of-N, the container, the computer-use null
hypothesis, cross-model table) are all week-2/3 material, not killed — the cross-model
table + KD-gap scalar (C3+C8) is the named runner-up and follows immediately once 1 and 3
land, with M46 credits paying for it.

1. **A1+A3(+A4): the exit oracle and the terminal split** [S] — `chamber_complete` hooked
   and in GameState, BUDGET split (LOOP detector with injected feedback), DIED specced
   into the same taxonomy PR before any hazard chamber exists. Five lenses ranked this
   first; everything downstream is uninterpretable until SOLVED is ground truth.
2. **B1→B2: nav recon (1 day, timeboxed), then trace-grid A\* `go_to` behind
   `sar_harness_goto_nav 1` — and rerun first light** [M, starts now] — with B3's minimal
   structured failures and B4's stand-point fix riding along. Expected outcome:
   testchamber_000 flips BUDGET → SOLVED; the glass enclosure becomes LocoGym test #1;
   the project gains its second headline artifact: *same frozen model, fixed legs, solved
   chamber.*
3. **C1: the O(1) scratchpad `ContextPolicy` + the 3-way A/B** [S] — full-chat vs
   window-N vs scratchpad on testchamber_000 + two new chambers; solve/steps/tokens/
   latency per arm. The single biggest cost lever (20×+), the stream/farm/tuning
   enabler, and the science (does the model actually maintain state?) in one change.
4. **F1: `.hdem` v2 — per-tick actions AND sound events, in the same version bump** [S] —
   the CUserCmd hook exists; the sound hook is gap-3's S-sized add. Recordings made
   before v2 are lossy forever, and per the teardown this is the only data clock a fork
   can't rewind. Record ~2 hours of my own play through it before the week ends.
5. **The sends (zero engineering, calendar-long — fire them first):** publish the
   first-light post (M39, day 1 — every recipient will look me up), then the TeamSpen
   ping (M41, collapses the headless-compile unknown), the Demaine/Lynch letter (M40),
   all three API-credit applications (M46), and book the first 3 of the 10 demand
   interviews (M21, segment zero: p2sr + a BALROG author).

---

## Index of raw council files

All under `brainstorm/.council/`:

**Diverge — 10 lenses** (`diverge/`):
`locomotion-actuation.md` · `model-frontier.md` · `eval-science.md` ·
`puzzle-gen-curriculum.md` · `data-flywheel.md` · `speedrun-routing.md` ·
`harness-platform.md` · `benchmark-competition.md` · `moonshots.md` · `pitch-strategy.md`

**Diverge — 7 gap lenses** (`diverge/`):
`gap-0.md` (demand-side discovery) · `gap-1.md` (competitive teardown & the
internal-rebuild threat) · `gap-2.md` (unit economics of the three businesses) ·
`gap-3.md` (the Omni gap: audio) · `gap-4.md` (real-time + death/irreversibility) ·
`gap-5.md` (trust & temptation: spec-gaming + faithfulness) · `gap-6.md` (the human
funnel: education, allies, plan-B money)

**Critique** (`critique/`):
`skeptic.md` (the VP-Research red team: P1–P13 + 14 shaky claims + the kernel) ·
`completeness.md` (what the 10 lenses missed; sourced gap lenses 3–6 and the §4
assigned-actions list)





