# Lens: harness-platform — the GENERAL ROBUST harness as the durable artifact

Diverge-mode brainstorm, 2026-06-12. Premise of this lens: every dead competition left a
living platform (ViZDoom, NLE, Procgen); every dead platform died of unreliability
(Universe), closedness (XLand), or maintenance abandonment (Gym/Retro). The eval results
and PPO runs are *consumers*; the thing a VP funds for five years is the instrument.
Today the instrument is real but exists only on one Arch box behind a launch incantation.
Everything below is about making "one harness, three consumers" true for someone who is
not you.

Grounding facts checked against the repo today: `HandshakeResponse` has **no harness/proto
version field**; `GameState` has **no chamber_complete, no seed, no session epoch**; SHM is
headerless raw RGB888; two parallel Python session layers exist (`rl_challenge_env.py` vs
`testchamber_session.py`); `rl_challenge_1.sh` is bit-rotted against `py/rl/config.py`
flags; `.trajectory` shipped without its designed `format_version`.

Effort key: **S** = days, **M** = weeks, **L** = months, **XL** = needs a team. All for one
RE + agentic coding.

---

## 1. `chamber_complete` ground-truth oracle in GameState

**Build:** Hook the engine level-end / `transition_fade` / `@relay_pti_level_end` firing
path (SAR's hooking patterns make this routine — same shape as the `EngineDemoRecorder`
detours) and add `bool chamber_complete = 8` to `GameState`. Falls back to an
exit-door-entity heuristic where the hook misses (Hammer maps). Add the case to
`py/agentloop_smoke.py` per the repo's smoke-test rule.

**Why:** Every suite, leaderboard, auto-scorer, and self-improvement loop needs a
non-hallucinatable success bit. The hand-passed `--exit x,y,z --radius` oracle caps the
project at ~5 chambers. SIMA 2's rubric-scored rewards are guessable; a ground-truth
engine bit is the pitch differentiator ("our reward cannot be gamed by the rater").

**Effort:** S. **Depends on:** nothing. **Unlocks:** chamber suite, workshop-scale eval,
competition scoring, RL terminal reward — nearly everything downstream of this lens.

## 2. Protocol versioning + capability negotiation (the v1 freeze)

**Build:** Add `harness_version` (semver string) + `repeated string capabilities` to
`HandshakeResponse`; `min_required_version` to `HandshakeRequest`; adopt proto hygiene
(reserved tags, no field reuse, `format_version` actually added to `trajectory.proto` —
it was designed as the loud-failure guard and silently dropped). Write
`docs/protocol-stability.md`: what is frozen (v1: handshake, GameState, ActionRequest,
AgentLoop framing), what is experimental (macros, percept modes). Client raises a clear
error on mismatch instead of mis-parsing.

**Why:** The instant a second person runs this, skew between their sar.so and their pip
client becomes the #1 support burden. Capability flags ("has_macros", "has_anchors",
"has_chamber_complete") let old clients degrade gracefully and let you ship the .so and
the client on independent cadences. This is the cheapest possible "we are a platform, not
a script" signal in the pitch.

**Effort:** S. **Depends on:** nothing. **Unlocks:** binary releases (#6), conformance
testing (#16), external users at all.

## 3. One canonical `Session` core — make "one harness, three consumers" true in code

**Build:** Factor the common 80% of `py/rl_challenge_env.py` and
`py/testchamber_session.py` into one `p2harness.Session`: connect/handshake, AgentLoop
lifecycle, SHM mapping, delta-snapshot merge into `WorldView`, reset semantics, crash
recovery. Three thin facades on top: `GymEnv` (raw actions, Gymnasium API), `MacroSession`
(verbs, percept text), `RecorderTap` (read-only, for human-play tooling). Kill
`py/rl/rollout.py` (admitted dead code) in the same pass.

**Why:** Today the flagship claim ("one harness, three consumers") is true at the proto
layer and false at the Python layer — two session implementations already drift (held-mark
tracking, reset behavior, frame cadence). Every future consumer (Claude agent, scripted
baseline, BC data loader, competition submission kit) multiplies the drift. This is also
the KISS/taste move: less code, one truth.

**Effort:** M. **Depends on:** nothing, but do before #6 (you don't want to ship two APIs).
**Unlocks:** pip package with a sane surface, competition starter kit, cheap new consumers.

## 4. SHM framebuffer v2: header + seqlock + tick/camera metadata

**Build:** Prefix the SHM region with a small header: magic, version, width/height/format
enum, monotonic frame seq (seqlock-style: odd while writing), `server_tick`, and camera
pose (pos + pitch/yaw + FOV) at capture time. Client retries on torn reads. ~50 lines C++,
~30 lines Python.

**Why:** Three birds: (a) the current headerless buffer is a latent torn-frame race the
moment anything reads asynchronously; (b) frame↔tick correspondence becomes provable
(today it's "trust the call ordering"); (c) camera pose per frame is exactly what the
trajectory viewer needs for frame↔mark hover correspondence and what SfM/world-model
consumers (Genie-style) need for camera-conditioned training. Resolution/format become
negotiable later (RGB888 → optional NV12 for cheap video encoding) behind the version
field.

**Effort:** S. **Depends on:** #2 conventions. **Unlocks:** async readers, video export,
viewer upgrades, world-model data credibility.

## 5. The container: one-command reproducible runtime

**Build:** An OCI image (`ghcr.io/.../p2harness`) containing steam-runtime, gamescope,
steamcmd, the sar.so release, and an entrypoint that (a) logs into Steam with
user-supplied credentials / depot cache mount, (b) downloads the Portal 2 depot on first
run (so the image ships **zero Valve assets** — licensing-clean), (c) boots N instances
with the per-instance port/SHM math, (d) exposes gRPC ports. Acceptance test = the image
runs `py/agentloop_smoke.py` green from a fresh cloud VM. Document GPU and (pending #11)
CPU-only paths. Compose file for N instances.

**Why:** This is the single biggest gap between "works on my Arch box" and "an external
lab reproduces first light in an afternoon." Every platform-survival lesson (MineRL's
pip-install, Malmo's death-by-install) says onboarding friction is existential. It is
also the unit of scaling (#10) and the unit of competition submission (#23). The launch
incantation in `game_launcher.py` is institutional knowledge that must become
infrastructure.

**Effort:** M (the steam-runtime-inside-container + gamescope-virtual-display dance is
fiddly; the rest is packaging). **Depends on:** none hard; #2 makes it sane.
**Unlocks:** external users, cloud scaling, competition, CI (#16).

## 6. "Portal2Gym" v1.0: pip package + versioned binary releases

**Build:** `pip install p2harness` (name TBD — see #21): the client, generated stubs,
`Session` + facades (#3), `game_launcher`, viewers, and a `p2harness doctor` command that
checks Steam install / sar.so version / gamescope and prints exactly what's missing.
GitHub Releases ship `sar.so`/`sar.dll` built reproducibly, version-locked to the wheel
via the #2 handshake. Quickstart README: 10 lines from zero to first annotated frame.

**Why:** Crafter's lesson — zero-friction install + one scalar metric out-impacts funded
competitions. The wheel is the front door; the container (#5) is the back room. Also
forces the repo split decision (#21).

**Effort:** M. **Depends on:** #2, #3; #5 recommended. **Unlocks:** adoption, baselines
by strangers, the benchmark-of-record story.

## 7. Determinism envelope: measure it, version it, gate on it

**Build:** An empirical determinism harness: for K seed scripts (raw framebulk sequences,
fixed warmup, `sv_alternateticks 0`), run R replays each across instances/machines, hash
per-tick `EntitySnapshotter` payloads, and report first-divergence-tick distributions per
element class (p2sr's wiki already warns airborne dropper cubes diverge). Output: a
`determinism.md` table ("player kinematics: tick-exact; settled cubes: exact; airborne
props: diverge ~tick N±M") + a CI job that fails when the envelope regresses. Add
`int32 rng_note`/seed plumbing where the engine exposes any.

**Why:** "Deterministic engine" is in the pitch and is *only semi-true*; a VP's tech DD
will find that in one afternoon on the TASing wiki. Owning the number — "replayable
within a documented envelope, divergence quantified per mechanic" — converts a weakness
into instrument calibration. Tree search (#12), seeded benchmark episodes, and
tick-perfect-execution claims all rest on this.

**Effort:** M. **Depends on:** #1 helps (auto end detection). **Unlocks:** honest
benchmark seeds, search, the speedrunning-execution axis, reviewer-proof claims.

## 8. Throughput program: render-on-demand + timescale + a published table

**Build:** The designed-but-unbuilt fast path: (a) skip rendering entirely on ticks where
no `copy_pixels_to_shm` (try `mat_norendering 1` toggled per-step, or the `SetSkipping`
hook from the conversion design); (b) `host_timescale`/`fps_max 0` free-run mode for
state-only workloads (hdem→rollout conversion, recon, non-visual RL); (c) a
`bench_harness.py` that prints ticks/sec and frames/sec for: tick-gated+pixels,
tick-gated state-only, free-run, per instance count 1/4/16. Publish the table in README
and the deck.

**Why:** The survival brief is explicit: "throughput numbers in the deck — DO." Cost per
experiment is a first-order VP question, and today nobody knows the number. State-only
speedup also directly multiplies RL iteration speed and demo-ingestion (#18) capacity.
~1× real-time per instance with parallelism-only scaling is a weak story; 10× free-run
per instance is a strong one.

**Effort:** M (the engine will fight `mat_norendering`; budget for one dead end).
**Depends on:** none. **Unlocks:** RL viability, ingestion scale, deck credibility.

## 9. `harnessd`: crash recovery promoted from client hack to platform layer

**Build:** A small supervisor (Python, one file, no Ray) that owns N `GameInstance`s:
health-checks via handshake ping, restarts crashed instances (the 32-bit process *will*
die — phase4 doc treats it as weather), re-runs warmup, and exposes a stable
instance-lease API (`acquire(tags) -> endpoint`, `release`, `report_crash`). Session
epoch (`int64 session_epoch` in handshake, bumped on every game restart) lets clients
detect "this is not the world you were talking to" instead of silently observing a fresh
map. Today's recovery logic in `async_worker.py` becomes a client of this instead of
bespoke.

**Why:** Crash recovery currently lives in the RL worker, duplicated in eval scripts,
absent in the REPL. A fleet (#10) and a hosted eval service (#23) are unbuildable without
a supervisor; the session-epoch bit is the difference between "flaky" and "fault-tolerant
by contract." This is the Universe lesson inverted: reliability is the product.

**Effort:** M. **Depends on:** #2 (epoch field). **Unlocks:** #10, #23, honest multi-hour
unattended runs (overnight eval sweeps, big renders).

## 10. Fleet test: 32–128 instances, one box → many boxes

**Build:** Stress the per-instance math past its comfort zone: measure VRAM/CPU/SHM per
gamescope instance at 640×480, find the per-GPU ceiling, fix what breaks (port ranges,
VPK lock stagger — currently a hardcoded 20s sleep — Steam client singleton issues,
`/dev/shm` sizing). Then the trivial distribution step: `harnessd` (#9) on each box, a
flat YAML of endpoints, client-side lease across boxes. No Kubernetes, no Ray, until a
named trigger (>3 boxes or >1 user).

**Why:** "Scalable evals" is the pitch's load-bearing adjective. ViZDoom gave each bot a
dedicated i7+GTX960; you should be able to say "one 4090 box runs N instances at M
ticks/sec; the suite evaluates in X minutes." Parallel instances are also the only
working speedup today (#8 may change that), so the fleet IS the throughput story for
launch.

**Effort:** M–L (mostly empirical debugging of weird shared-state failures).
**Depends on:** #5, #9. **Unlocks:** workshop-scale ingestion (#18), competition backend
(#23), RL at >4 envs.

## 11. Software-rendering spike: does it run on lavapipe?

**Build:** One-day spike: run the gamescope+Vulkan stack on CPU (lavapipe/llvmpipe) at
640×480, measure FPS. If ≥30, document the flag; if not, try smaller res / `-low` /
state-only mode (#8) on CPU.

**Why:** If instances run CPU-only, the farm escapes GPU procurement entirely —
competitions can run on spot CPU fleets, and "every grad student's laptop" becomes a
target. Even a negative result is worth having written down before someone asks in a
pitch meeting.

**Effort:** S. **Depends on:** #5 makes it repeatable. **Unlocks:** (if yes) order-of-
magnitude cheaper scaling; (if no) a crisp hardware-requirements line.

## 12. Save/load anchors → a *branchable* simulator API

**Build:** `Anchor()`/`Restore(anchor_id)` RPCs over engine save/load (C9 in the
roadmap), with the post-restore invariants *tested*: marks stable (the A3 deterministic
renumbering was designed for exactly this), EntitySnapshotter slots coherent, SHM frame
refreshed, session epoch unchanged. Expose in `Session` as `fork()`-like semantics:
`with session.anchor() as a: ... session.restore(a)`.

**Why:** World-freeze + anchors + stable marks = MCTS/best-of-N over macro sequences for
~free — the bridge from "eval harness" to "search-based routing agent" (the speedrunning
axis) and to counterfactual data generation ("same state, 5 different next actions" — a
data product no video corpus has). It also gives human recorders and labelers undo.
Platform-lens point: *branchability* is what distinguishes a simulator protocol from a
Gym env, and nobody else's commercial-game harness has it.

**Effort:** S–M (the 40-LOC claim is optimistic; restore is the admitted tar pit —
budget the M). **Depends on:** determinism audit (#7) to know what restore actually
guarantees. **Unlocks:** tree search, routing experiments, labeling UX, dense
counterfactual datasets.

## 13. `.hdem` v2: per-tick input records + format-discipline sweep

**Build:** Bump HDEM to v2: append a per-tick input record (buttons, viewangles, mouse
dx/dy from the existing `Client.cpp:586` CUserCmd hook) so a `.hdem` alone yields
(obs, action) pairs at I/O speed via `py/hdem_reader.py` — no engine replay, no
viewangle-diff heuristic. Same PR: the format-version sweep — `format_version` into
`trajectory.proto`, fix `hdem_to_rollout.py`'s placeholder header dims and sign-extension
hacks, write the footer CRC.

**Why:** The VPT lesson verbatim: input-synchronized human play is the scarce commodity
that pseudo-labels the abundant YouTube corpus. This single small change makes every
human playthrough a complete BC sample and makes the "trajectories for Gemini
Robotics/Omni" pitch concrete. From this lens it's also the proof that the formats can
*evolve* — versioned formats that have never been bumped are versioned in name only.

**Effort:** S. **Depends on:** nothing. **Unlocks:** offline RL/BC pipeline, IDM
training, the human-data flywheel.

## 14. Standard-format exporters: RLDS, LeRobot, SIMA-span

**Build:** `py/export/`: `.rollout`→RLDS episodes (tfrecord; per-step image, state,
action, `chamber_complete` as terminal), `.rollout`→LeRobot dataset,
`.trajectory`→SIMA-style spans (frames + instruction + score tuple). Round-trip tests on
first-light artifacts.

**Why:** Open X-Embodiment standardized on RLDS; SIMA 2 ingests (720p frames, low-level
inputs, span instruction, rubric score). If the pitch is "our data feeds your training
pipelines," the demo is a file their loader opens *today*, not a custom proto. Cheap,
high pitch-leverage, zero engine work.

**Effort:** S. **Depends on:** #13 makes the exports action-complete; #1 makes terminals
real. **Unlocks:** the data-product story, instant credibility with robotics/agents
teams.

## 15. Harness metrics + structured logging endpoint

**Build:** A `Stats` RPC (or `/metrics` side-channel): ticks advanced, RPC counts and
latencies, SHM copy µs, snapshot walk µs, macro result-code histogram, crash/restart
counts, session epoch. `harnessd` (#9) scrapes and aggregates; `bench_harness.py` (#8)
consumes the same numbers.

**Why:** Fleet ops without metrics is archaeology in per-instance log files (the current
state). The result-code histogram is also a free *science* instrument: actuation-failure
rates per verb per chamber, across thousands of runs, with no trajectory parsing.

**Effort:** S. **Depends on:** #2. **Unlocks:** #9/#10 operability, deck numbers, regression
detection.

## 16. Self-hosted CI gate: the smoke test becomes a wall

**Build:** A self-hosted GitHub Actions runner on the dev box (later a dedicated mini-PC
with a Steam install): every PR builds sar.so, boots one headless instance in the
container (#5), runs `agentloop_smoke.py` + the determinism quick-check (#7) + golden
conformance transcripts (recorded RPC request/response pairs replayed against the new
binary; assert wire compatibility). Release tags publish the binary (#6).

**Why:** The repo rule "every gRPC-surface PR updates the smoke test" is currently
enforced by one person's discipline. Platforms die when reliability regressions ship;
external users turn every regression into a support fire. CI is also what makes
*agentic-coding-heavy* development safe — the agent can iterate boldly against a hard
gate.

**Effort:** M (first 80% in days; flaky-game-in-CI tax is the rest). **Depends on:** #5.
**Unlocks:** confident releases, contributor PRs, the maintenance story (#21).

## 17. Chaos suite: executable institutional knowledge for a hostile 32-bit target

**Build:** Fault-injection tests over the client+supervisor: SIGKILL the game mid-macro,
mid-`Reset`, mid-SHM-copy; saturate the 3GB address space until malloc fails; drop the
AgentLoop stream mid-step; corrupt SHM seq; assert the recovery invariants (#9's epoch
contract) hold and no consumer deadlocks on the tick condvar.

**Why:** `phase4_fixing_slowness_and_crashes.md` is a graveyard of hard-won lessons that
currently live in prose. The condvar tick-gating (`ticksRemaining`, `tickCV`) is exactly
the kind of machinery that deadlocks in rare interleavings and burns a week of a
multi-day unattended run. Tests turn the war stories into regression armor before
external users find the interleavings for you.

**Effort:** M. **Depends on:** #9. **Unlocks:** multi-day unattended runs, trustworthy
fleet, fewer 3am pages when this has users.

## 18. Workshop ingestion pipeline: from 950k items to a curated task distribution

**Build:** `py/ingest/`: steamcmd `workshop_download_item` fan-out → BSP sanity checks →
boot-in-harness probe (loads? entities resolvable against kClassColors + curated status
fields? exit detectable via #1? spawn reachable?) → metadata DB (elements present, voxel
size, author, ratings) → tiered emit: `verified` / `loads-but-unscored` / `rejected`.
Run on the fleet (#10); target a first batch of ~1k verified chambers.

**Why:** The Universe corpse says never pitch raw breadth; Procgen says the *distribution*
with train/held-out splits is the asset. "950k items" is a slide; "12,000 verified
chambers with element metadata and ground-truth completion, refreshed monthly, held-out
split private" is a benchmark no frontier lab can contaminate. Licensing note for the
design: chambers download-on-demand via Workshop API per user — the pipeline ships
*recipes and metadata*, never map files (#22).

**Effort:** M–L for the pipeline; curation is ongoing. **Depends on:** #1 (hard), #9/#10
(scale), #5. **Unlocks:** the contamination-resistant benchmark, competition test sets,
generalization science.

## 19. Strata Source / Portal 2: CE spike — the planned escape from 32-bit

**Build:** Time-boxed port spike against P2:CE open beta (~Apr 2026, 64-bit, DX11,
Valve-licensed): does the SAR Feature pattern + interface lookup survive? What of
signature scanning, SendTables, the TAS framebulk path? Deliverable is a *report* (port
cost estimate, capability diff, what breaks in .hdem/marks), not a port.

**Why:** The 32-bit retail binary is a depreciating asset: hand-built 32-bit gRPC stack,
3GB heap that SIGABRTs under fragmentation, no official server, broken -textmode. DM Lab
died partly of old-engine ossification. A platform pitched for a 5-year horizon needs a
stated engine-succession plan; P2:CE is also the proof-point that Valve licenses engine
source to community teams — pitch ammunition even if the port waits a year. Honest risk
to record: speedrun ecosystem and existing offsets stay on retail, so retail remains the
v1 substrate.

**Effort:** M for the spike; the port itself L. **Depends on:** P2:CE beta access.
**Unlocks:** 64-bit headroom, the longevity slide, possibly a real licensed relationship
with Valve.

## 20. Multi-title audit: prove "platform" with a second game

**Build:** Pick the cheapest second title already in `src/Games/` (Aperture Tag or
Portal Stories: Mel share the engine branch and most offsets) and audit what the Harness
needs: entity ontology delta (kClassColors append + one recon session per the
`sar_harness_dump_fields` protocol), map sourcing, oracle (#1) variance. Ship one
working annotated-percept chamber in the second title as the existence proof.

**Why:** "Portal 2 harness" is a project; "Source-engine embodied-agent harness, Portal 2
first" is a platform. The recon-command protocol was explicitly designed as a repeatable
empirical method — demonstrating it on a second title converts the ontology from
hand-curated artifact to documented process. Low urgency, high pitch-shape value.

**Effort:** M. **Depends on:** none (the recon protocol exists). **Unlocks:** the
generality claim, INFRA/HL2 as future eval diversity.

## 21. Open-source strategy: three rings, a name, and a steward

**Build:** Decide and write down: **Ring 0 public** (harness plugin, proto, pip client,
viewers, baseline agent, ~20-chamber public suite; MIT/Apache-2.0) — consider offering
the percept/mark layer upstream to p2sr/SAR to share maintenance; **Ring 1 gated**
(held-out eval chambers + golden transcripts — Procgen's private-envs precedent; access
on request); **Ring 2 private** (trained checkpoints, pitch decks, ingestion DB). Pick a
name that isn't Valve's trademark ("Portal2Gym" is a lawsuit-shaped name; the harness
deserves its own — and per the survival brief, don't brand it "Gym"/"RL benchmark" at
all). Write the stewardship paragraph the Farama lesson demands: who maintains year 3 —
the honest answer is "this is part of the headcount ask."

**Why:** XLand (closed) produced one paper and no ecosystem; Gym (unstewarded) had to be
rescued by volunteers. The rings resolve the tension between openness-for-adoption and
held-out-for-integrity. Doing this *before* v1.0 (#6) avoids a repo-history scrub later.

**Effort:** S (decision + docs; repo split M when executed). **Depends on:** none —
sequence before #6. **Unlocks:** external contributions, competition integrity, clean
pitch answer to "what's open?"

## 22. The Valve packet: licensing from gray to green

**Build:** A two-page brief + email to Valve (via p2sr contacts / P2:CE team as warm
intro): what we do (research harness, headless instances, human-gameplay datasets), what
we ask (blessing for headless farms where every instance has a license; workshop chamber
*download-on-demand* distribution for benchmarks; a competition where participants own
Portal 2), precedents cited (OpenAI Five cooperation, tolerated decade of SAR, CSGO BC
dataset shipped unchallenged, P2:CE engine license). Decision tree for each answer
including silence (= today's tolerated-gray status quo, documented as accepted risk).

**Why:** It's the platform's only existential legal risk, and it's cheap to retire. A VP
*will* ask "what does Valve think?"; "we have a thread with them and here's the
precedent stack" beats "we assume it's fine." Worst realistic case is silence, which
costs nothing.

**Effort:** S. **Depends on:** none; stronger after #5 shows assets-clean distribution.
**Unlocks:** competition (#23), dataset releases, the partnership slide.

## 23. Hosted eval service: the competition backend (the headcount ask)

**Build:** The BALROG-style rolling-leaderboard architecture on top of everything above:
participants' agents connect over the public gRPC surface (or submit containers) to a
hosted fleet (#10); server enforces budgets (macro count, wall-clock, API tokens),
assigns held-out chambers (#18, #21 Ring 1), records `.trajectory` for every run,
auto-scores via #1, publishes TrueSkill/score ladder. Start as
submission-runs-locally-against-hosted-eval-server (cheapest credible mode per the
competition brief), graduate to hosted-execution only with sponsor compute.

**Why:** This is the marketing layer for the platform and the concrete thing headcount
buys. Every input is an idea above — which is exactly the pitch structure: "one RE built
items 1–17; items 18–23 at scale are the team."

**Effort:** L for a minimal trusted-friends ladder; XL for public competition ops
(eval-cost is the documented killer of MineRL/BASALT-scale events). **Depends on:** #1,
#5, #9, #10, #18, #21, #22. **Unlocks:** community, citations, the NeurIPS competition
track, the flywheel.

---

## Spiciest take

**Stop building "Portal2Gym" — the Gym framing is a dead category that both OpenAI and
DeepMind exited, and a Gym wrapper is 30 lines anyone can write.** What you have
accidentally built is rarer: a *pausable, soon-branchable, ground-truth-instrumented
simulator protocol* wrapped around a hostile commercial binary — the thing Universe
failed to be and SIMA's studio partners never expose. Productize the protocol (versioned
handshake, container, determinism envelope, anchors, metrics) and plan its escape from
the 32-bit retail binary (P2:CE) from day one — the LLM eval results will be reproduced
by others within months of publication; the instrument is the moat, and only the
instrument justifies headcount.

## If I could only do ONE thing next week

Build the container (#5) with the protocol-version handshake (#2) folded in, and make
`agentloop_smoke.py` green from a *fresh cloud VM* the acceptance test.
Until a machine that isn't yours can boot this, there is no platform to pitch — and every
other idea on this list (fleet, CI, ingestion, competition) is blocked behind exactly
this artifact.
