# Gap 4 — The world doesn't wait: a real-time track plus death and irreversibility semantics

*Gap-filler lens, 2026-06-12. The council treated pause-while-thinking as pure strength and
hazards as "more elements." Both readings are wrong in ways that bite within one milestone.*

## Framing: why this gap is real and why it is urgent

Two distinct holes, one root cause — the harness currently has **no model of time pressure and
no model of permanence**:

1. **The clock.** The whole stack is built on world-freeze: `ticksRemaining`/`tickCV` in
   `src/Features/Harness/Harness.hpp` gate `PRE_TICK`, and every macro drives the world via
   `AdvanceTicksBlocking` (`MacroExecutor.cpp`). Between actions, time stops. Every council lens
   celebrates this (free deliberation, branchable search, thinking-budget curves) and **none
   designed the arm where it's turned off**. But the robotics/embodiment pitch — Gemini
   Robotics trajectories, SIMA substrate, "agents like a human" — walks straight into the
   one-line objection any robotics reviewer will raise: *reality doesn't pause*. Today there is
   no answer, not even a slide.

2. **Permanence.** M3 adds lasers, turrets, and goo. Today: `MacroExecutor.cpp` never reads
   player health (verified — zero health checks in any march loop); `harness.proto` carries
   `health = 4; // 0 if dead` that nothing consumes; the terminal vocabulary
   (`py/llm_eval/trajectory.proto:62`, and eval-science's about-to-be-implemented taxonomy v2:
   SOLVED/LOOP/STUCK/BUDGET/GAVE_UP/CRASH) has **no DIED and no IRRECOVERABLE**. There is no
   respawn policy. Nothing detects that a fizzled or goo'd cube made the chamber unsolvable.

The urgency is concrete and mechanical. Portal 2 SP **auto-respawns** the player a few seconds
after death. So the first goo chamber, with today's code, produces this: agent issues
`go_to 7`, walks into goo mid-march, dies, silently respawns at the chamber entry, the march
loop keeps driving a teleported body, the macro returns `BLOCKED` or `STUCK`, the held cube has
fizzled, and the trajectory records a 25-step BUDGET run in which the agent was — unrecorded —
killed and teleported, possibly several times, in a chamber that may have become unsolvable at
step 3. That is not a hard chamber; that is **garbage data**, and the model is being gaslit
(its world jumped and nobody told it). And note: hazards act **during macro execution even in
pause mode** — turrets shoot you while `go_to` marches. Death semantics are needed *now*,
independent of any real-time track.

There's a second silent bug on the clock side: **stock PeTI contains wall-clock elements**
(the pedestal timer button is the canonical one; track platforms and turret aim also evolve in
world time). Pause-while-thinking changes their semantics — a timed-button puzzle stops testing
*decision-making under time pressure* and becomes *open-loop tick-efficient planning* (think
forever while the timer is frozen, then execute). The v0 scope says "full stock PeTI surface
set"; the benchmark's own scope already requires the real-time semantics nobody designed.

**Build order:** Part B (death/irreversibility) ships **before** the first M3 hazard chamber —
it's days of work and it slots directly into eval-science's terminal-taxonomy-v2 PR. Part A
(real-time) is the strategic half: one M-sized substrate change plus a family of cheap arms
that convert the robotics objection into this platform's most differentiated instrument.

---

## Part A — The real-time track

### A1. Real-time session mode: free-running engine, streamed observations, preemptive actions
**What.** A per-session mode (`sar_harness_realtime 1` / session flag in the `AgentLoop`
handshake) where the engine never blocks on `tickCV`: the world runs at 1× continuously. The
server **pushes** `GameState` on the stream at a fixed cadence (every N ticks, pixels on the
existing opt-in flag) instead of replying per-request; actions arrive whenever the client sends
them and apply at the next `PRE_TICK`. A new action **preempts** the currently executing macro
through the already-existing `CANCELLED` paths in `MacroExecutor.cpp` (a dozen of them — the
machinery is built, nothing triggers it externally yet). The real engineering cost is honest:
macros today are blocking loops around `AdvanceTicksBlocking`; in real-time mode they must
become per-tick state machines driven *by* `PRE_TICK` rather than driving it. That refactor is
the heart of this idea — and it's the same refactor locomotion's "interruptible macros" idea
needs, so the cost is shared, not added.
**Why.** This is the substrate for every other idea in Part A. Without it the robotics
objection has no answer; with it, the same chambers, grammar, and trajectory format run in both
clock regimes — which is precisely what makes the comparison publishable.
**Effort.** M (the macro state-machine refactor dominates; protocol delta is small).
**Unlocks.** A2–A7; locomotion's interruptible macros (shared refactor); the only credible
robotics slide in the deck.

### A2. The anytime-action contract: default behavior, deadlines, late-action policy
**What.** Real-time mode forces three questions pause mode never asked; answer them as a
written, versioned contract in the session config, not as emergent behavior. (1) **Default
behavior** while the model thinks: `idle` (stand still) v0; `continue` (current macro keeps
executing — see A5) as the second arm. Do **not** build a learned reflex policy first. (2)
**Deadline field**: each pushed observation carries `obs_tick` and an optional advisory
`respond_by_tick`; nothing enforces it — it exists so lateness is *defined*. (3) **Late-action
policy**: an action conditioned on a stale observation still applies (the world has moved; that
is the point), but staleness is recorded (A3) and a `STALE_WORLD` note rides the MacroResult
when the target mark's state changed since `obs_tick` (the snapshotter already memcmps every
tick, so "changed since tick T" is a lookup).
**Why.** Universe (OpenAI) died partly because real-time semantics were *implicit* (VNC lag was
unmeasured confound, not contract). Writing the contract first is what makes the real-time
track an instrument instead of a noise source.
**Effort.** S (on top of A1).
**Unlocks.** Comparable real-time results across models; the spec section of any paper that
uses the track.

### A3. Staleness accounting: obs_tick → decide → apply in every trajectory step
**What.** Three fields per step in `.trajectory`: `obs_tick` (tick of the observation the model
conditioned on), `decide_walltime_ms` (API latency, already implicitly known from the existing
TokenUsage timestamps), `apply_tick` (when the action took effect). Derived metric:
**staleness** = `apply_tick − obs_tick`, reported per step and as a run distribution. In pause
mode staleness is identically ~0 — which is exactly the chart: the same agent's staleness
histogram in both modes.
**Why.** "The model acted on a 2.3-second-old world" is robotics-grade latency accounting, and
it's nearly free — every quantity is already observable. It also future-proofs the dataset:
offline analysis can ask "did failures correlate with staleness?" only if staleness was
recorded.
**Effort.** S.
**Unlocks.** A4's x-axis; the data-flywheel corpus gains the field robotics consumers
(RT-Trajectory-style) actually filter on.

### A4. The deliberation-subsidy curve: solve rate vs clock pressure (host_timescale as the dial)
**What.** The science instrument of the lens. Between "paused" and "real-time" lies a
continuum: run the world at `host_timescale` ∈ {0 (= pause arm), 0.1, 0.25, 0.5, 1.0} while
the model thinks, same chamber suite, same models. Plot solve rate (and steps, deaths, staleness)
against clock pressure. The headline scalar: the **deliberation subsidy** — the solve-rate gap
between paused and 1× for a given model. Secondary curve: at 1×, sweep thinking budget — in
real time, test-time compute has a *world-state price* (think longer → better plan → staler
world), and this is the only benchmark anywhere that can measure that tradeoff cleanly,
because the paused control arm exists on the identical task.
**Why.** Model-frontier's thinking-budget curve and eval-science's teleport control are both
"make the confound an arm" moves; this is the third one, and the one with a robotics audience.
Genie/world-model people cannot produce it (no ground truth); real robots cannot produce it
(no paused control). A VP who has seen test-time-compute scaling charts will immediately
recognize this as the embodied version.
**Effort.** S–M (given A1; `host_timescale` manipulation is engine-trivial and SAR-adjacent
tooling already exists — the cost is runs, not code).
**Unlocks.** THE real-time pitch chart; pre-registers beautifully (eval-science stats spec).

### A5. Two-system agent: macro-as-reflex, think-while-acting
**What.** The architecture arm that real-time makes meaningful: while the VLM is thinking, the
**current macro keeps executing** (`default=continue` from A2) — the engine-side macro layer
*is* the System-1 cerebellum, the VLM is the System-2 planner, and planning latency hides
inside actuation time. Concretely: agent issues `go_to 8`; while the body marches, the model is
already reasoning over the last pushed frame about the *next* verb; if the macro completes
first, the body idles; if the plan arrives first, it queues (or preempts on an explicit
`now:` prefix in the grammar). Measure **overlap fraction** (share of wall-clock where body and
planner were both busy) and compare solve-time vs the stop-and-think baseline.
**Why.** "Reflex layer holds while the planner thinks" is the literal architecture of every
serious robotics stack (and of Gemini Robotics' fast/slow split). Demonstrating that the
*existing* macro boundary already implements the reflex half — no learned policy needed — is
the cheapest possible version of the fundable cortex/cerebellum story locomotion's lens tells,
and it reuses their interruptible-macros work rather than competing with it.
**Effort.** M (session-layer scheduling + the A1 substrate; no new engine verbs).
**Unlocks.** The robotics-architecture slide; the natural home for locomotion's learned `go_to`
later (swap the reflex implementation, keep the contract).

### A6. The timed-element audit: which stock PeTI elements have wall-clock semantics
**What.** A one-document audit + a manifest flag. Enumerate the stock PeTI set
(`brainstorm/puzzlemaker_elements.md`) and classify each element's time semantics:
**clock-bearing** (pedestal timer button; track platform; turret tracking/fire) vs
**clock-free** (cube, floor button, door, fizzler, funnel-as-state, gel surfaces). Then encode
the consequence in the chamber manifest (puzzle-gen's `manifest.json`): `requires_realtime:
bool` per chamber, set if any clock-bearing element is *load-bearing* for the solution. Eval
runners refuse to score a `requires_realtime` chamber in pause mode (or score it into a
separate, explicitly-labeled column).
**Why.** This is a benchmark-integrity bug today, not a future concern: the moment M3's suite
includes a timer-button chamber, pause mode lets the agent legally freeze the timer —
Zeno's agent — and the leaderboard number means nothing. The audit costs a day and prevents
the class of result that gets a benchmark dismissed in peer review.
**Effort.** S.
**Unlocks.** Honest M3/M4 suite design; the crispest one-paragraph justification for why the
real-time track must exist (it's required by the benchmark's own v0 scope).

### A7. Real-time hazard gauntlet: the chambers where thinking slowly hurts
**What.** A 8–12 chamber mini-suite, authored after A1+B1, where world dynamics punish
deliberation: a timer-button door at increasing distances (sweep the timer), a turret corridor
where standing still to "think" draws fire, a track-platform timing hop, a two-button timed
relay. Each chamber is trivial in pause mode (that's the control) and graded in real time —
the pair (pause solve rate, real-time solve rate) per chamber isolates pure time pressure with
reasoning held constant.
**Why.** A4 needs content where the clock actually binds; locomotion's LocoGym deliberately
excludes hazards and timing, so nothing in the council's suites covers this. Also the most
*legible* real-time artifact: a clip of an agent beating a timer reads instantly to any
audience.
**Effort.** M (chambers are S; the runs and the death-handling dependency are the rest).
**Unlocks.** The real-time leaderboard column; demo material; stress tests for B-series
semantics under fire.

---

## Part B — Death and irreversibility semantics (ship before the first hazard chamber)

### B1. DIED terminal + cause-of-death telemetry
**What.** (1) Detect death engine-side: hook the player damage/kill path (SAR's hooking
patterns; `Event_Killed` / the `player_death` game event) or, minimally, watch
`health → 0` server-side — the `health` field already exists in `harness.proto:133` and
nothing consumes it. (2) Mid-macro: every march loop in `MacroExecutor.cpp` checks alive-ness
per batch and aborts with a new result code `DIED` (today the loop happily keeps driving a
respawned body). (3) Terminal taxonomy gains `DIED` alongside eval-science's
SOLVED/LOOP/STUCK/BUDGET/GAVE_UP/CRASH. (4) Cause attribution from the damage type:
`GOO | TURRET | LASER | CRUSH | FALL | OTHER`, plus the killer's mark when it has one
(turret 9 killed you). Rides the MacroResult and the terminal record.
**Why.** This is the single highest-urgency item in both parts: without it, M3's first goo
chamber records deaths as BUDGET/STUCK and the dataset is poisoned in a way no offline pass can
repair (the death was never observed). Cause-of-death is what makes the failure museum,
per-element competence matrix, and hazard tiers chargeable to the right element.
**Effort.** S.
**Unlocks.** Every M3 chamber; B2–B7; honest terminal taxonomy; mortality metrics (B7).

### B2. Respawn policy as a versioned eval rule, not engine default
**What.** Decide — in writing, in the session config — what death *means*, per track, and make
the harness enforce it rather than inheriting Portal 2's silent auto-respawn. Three sanctioned
policies: (a) **`terminal`** (science default): episode ends at death with `DIED`; (b)
**`lives:N`** (leaderboard): auto-respawn up to N times, each death injected into the percept
stream as an explicit event ("you died: GOO; respawned at entry; cube 11 fizzled") and counted
in scoring; (c) **`free`** (data collection / human play): unlimited respawns, all recorded.
The harness must *suppress or intercept* the engine's own respawn under policy (a) — the one
real engineering nubbin here.
**Why.** Without a declared policy, two runs of the same chamber aren't comparable (one died
twice, silently). With one, death becomes a measured, prompt-visible event the model can
reason about — which is itself science: does the model *change behavior* after being told it
died? Every game benchmark with hazards (NetHack!) defines this; we currently don't.
**Effort.** S.
**Unlocks.** Comparable hazard-chamber results; the `lives` column on any leaderboard; B7.

### B3. Entity-lifecycle events as percepts: fizzle, destruction, respawn, tombstones
**What.** The snapshotter (`EntitySnapshotter.cpp`) memcmps every entity every tick — it
already *knows* when an entity vanishes. Promote that to first-class: (1) an `events` list on
`GameState`/Observation — `FIZZLED(mark=11, by=grill 4)`, `DESTROYED(mark=9)`,
`SPAWNED(mark=14, class=cube, from=dropper 6)` — covering everything since the last
observation; (2) **mark lifecycle rules** in `MarkTable.cpp`: a dead entity's mark is
*tombstoned* (never reused within an episode; the mark list shows it struck through with cause)
and a respawned cube gets a **new** mark with `replaces=11` lineage. (3) The annotation layer
renders tombstones for one frame so the visual and symbolic percepts agree.
**Why.** Marks silently vanishing is the percept-side half of the gaslighting problem: the
model placed cube 11 on the button, looked away, and mark 11 no longer exists. First light
showed the model *can* track physics side-effects when they're visible (step 12); give
destruction the same visibility. Tombstone lineage is also what keeps trajectories analyzable
(the cube on the button at step 20 is provably the respawn of the one fizzled at step 9).
**Why now:** mark-stability invariants are also load-bearing for save/load anchors
(harness-platform C9) — define lifecycle once, both consumers inherit it.
**Effort.** S.
**Unlocks.** B4 (its sensor layer); honest hazard percepts; anchor mark-stability tests.

### B4. IRRECOVERABLE oracle v0: manifest-scoped dead-state detection
**What.** Detecting unsolvability in general is PSPACE-hard — do not build that. Build the
chamber-scoped version the manifest makes trivial: the chamber manifest (puzzle-gen's
`manifest.json`; free-by-construction for generated chambers) declares a **resource model** —
for each goal-relevant consumable (cube of type T feeding button B), whether a respawner
exists (`dropper 6, auto-respawn: true`). The oracle is then an event rule over B3's stream:
*consumable destroyed AND no live respawner bound → emit `IRRECOVERABLE` terminal* (with the
causing event attached). For found/Workshop maps without manifests, a conservative heuristic
flags `SUSPECT_IRRECOVERABLE` (cube fizzled, no dropper of that cube type in the entity census)
for offline triage, never auto-termination. Note the comforting recon fact: stock PeTI droppers
auto-respawn fizzled cubes by default, so true dead states are *rare and declarable* — exactly
why a manifest-scoped oracle covers ~all of v0.
**Why.** Without it, an unsolvable-since-step-3 run burns 22 steps and $2 of tokens, then
records BUDGET — indistinguishable from "needed more steps," which corrupts solve curves,
censoring-aware metrics, and any difficulty calibration. With it, the episode ends the moment
the world became unsolvable, with the *cause* attached: a new, clean failure class ("the model
destroyed its own resources") that no existing benchmark separates from timeout.
**Effort.** M (the rule engine is S; manifest schema + heuristic census is the rest).
**Unlocks.** Honest BUDGET semantics; "self-inflicted unsolvability rate" as a model metric;
chamber-suite QA (a chamber whose *intended* solution risks IRRECOVERABLE is a design bug).

### B5. Engine-side hazard guard: macros refuse lethal primitives unless overridden
**What.** `go_to`'s march loop already traces for WALL/EDGE; extend the guard set with
*lethality*: goo ahead (trace contents `CONTENTS_SLIME` — geometrically knowable), an active
laser beam crossing the path (beam endpoints are in the snapshot), a turret's live fire cone.
On detection: stop, return `HAZARD_AHEAD(kind, mark)` — a percept, not a death. The grammar
gains an explicit override (`go_to 7 accept:hazard`) for routes that *must* cross danger
(running a laser gauntlet, sacrificial turret bump). Default-safe, override-explicit.
**Why.** Mirrors locomotion's "the engine never burns model steps on micro-jank" principle, at
the survival layer: a model should never die because the *actuator* couldn't see goo two steps
ahead — that death measures nothing about reasoning. The explicit override keeps the science
clean: when the agent crosses a hazard, intent is in the transcript, so DIED terminals split
into "chose the risk" vs "blindsided," which is exactly the calibration data B7 wants. Also the
robotics-legible safety story: planner may be slow and wrong; the actuator has a safety
envelope.
**Effort.** S–M (goo trace is S; laser/turret cones are the M tail — ship goo first, it's the
M3 killer).
**Unlocks.** Hazard chambers that measure reasoning instead of actuator blindness; risk-intent
labels for free; the safety-envelope paragraph in the robotics pitch.

### B6. Death × anchors: restore-on-death for search, terminal-on-death for eval
**What.** A one-page semantic rule plus a small data product, gated on save/load anchors (C9).
Rule: **eval** tracks never restore (death is `DIED`, full stop — leaderboard integrity);
**search/data** modes (MCTS router, best-of-N, counterfactual generation) treat death as a
pruned branch — restore the pre-action anchor, mark the edge lethal. Data product: every
search-mode death yields a **near-death counterfactual pair** — identical anchor state, lethal
continuation vs surviving continuation — which is the embodied preference-pair shape
(data-flywheel's counterfactual idea) with the starkest possible label, plus dense "lethal
edge" annotations for the route graph (a route through the laser is fast *and* fatal — the
router must know both).
**Why.** Without this rule, the search lenses will each invent their own death handling and
the eval/search results stop being comparable; with it, death becomes *useful* — the cheapest
ground-truth negative label the flywheel produces.
**Effort.** S (given C9 anchors; it's mostly a contract plus a restore-on-DIED branch in the
search driver).
**Unlocks.** Safe MCTS over hazard chambers; lethality-labeled route graphs; DPO pairs with
unambiguous labels.

### B7. Mortality metrics + risk calibration: deaths as first-class science
**What.** The metrics layer over B1–B5, computed offline from `.trajectory`: **deaths-per-solve**
and **death-cause histogram** per model per tier; **self-inflicted unsolvability rate** (B4
terminals); **hazard competence matrix** (model × hazard class — the goo column vs the turret
column); and the calibration piece: in hazard chambers the action grammar accepts an optional
`risk:{none,low,high}` annotation the model states before each macro, graded against actual
outcomes (died / HAZARD_AHEAD / clean). Premature-done calibration already exists in the
council (model-frontier); this is its mortal twin: *does the model know when it might die?*
**Why.** "Solve rate" on hazard chambers without mortality metrics hides the most interesting
variance — two models with equal solve rates, one of which dies 4× more, are not equal agents,
and no current metric separates them. Risk calibration in a world with real, ground-truth
death is something neither text benchmarks nor (ethically) robotics can measure; it's a
genuinely novel chart and it costs almost nothing once B1/B2 exist.
**Effort.** S–M (S for the offline metrics; M if the risk-annotation arm runs a real sweep).
**Unlocks.** The hazard rows of every leaderboard; a publishable "embodied risk calibration"
section; the safety-flavored hook for audiences that don't care about puzzles.

---

## Dependencies and sequencing (one RE + agentic coding)

```
NOW (days, before first M3 hazard chamber):
  B1 DIED terminal + cause      ──┐
  B2 respawn policy              ─┤── slot into eval-science's terminal-taxonomy-v2 PR
  B3 lifecycle events/tombstones ─┘
  A6 timed-element audit         (one day; informs M3 suite design)
THEN (weeks):
  B5 hazard guard (goo first) · B4 IRRECOVERABLE oracle (with puzzle-gen manifest)
  A1 real-time mode (shared refactor with locomotion's interruptible macros)
  A2 anytime contract · A3 staleness telemetry
LATER (with content + runs):
  A4 deliberation-subsidy curve · A5 two-system agent · A7 hazard gauntlet
  B6 death × anchors (gated on C9) · B7 mortality metrics + risk arm
```

## Spiciest take

Pause-while-thinking — the platform's celebrated superpower, the thing every other lens builds
on — is *already* a correctness bug against the project's own v0 scope: stock PeTI ships a
timer button, and in pause mode the agent can legally freeze time, so the first timed chamber
M3 adds will produce a leaderboard number that means nothing (Zeno's agent). Meanwhile the
first goo chamber, with today's code, records a death as `BLOCKED` and a silent teleport,
because no march loop in `MacroExecutor.cpp` reads health and Portal 2 quietly respawns the
corpse. The council designed twelve ways to exploit frozen time and zero ways to survive
running time. Invert the framing: the paused mode is the *control arm*, not the product — and
the deliberation-subsidy curve (same chamber, same model, clock on vs off) is the one chart
this platform can produce that no text benchmark, no Genie-style world model, and no physical
robot lab can, because they each lack one of the two arms. Ship DIED before M3 ships goo, or
the benchmark's first hazard data is garbage by construction.
