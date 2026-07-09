# Azorae's Stride remake — 2026-07-08 run postmortem

Blameless postmortem + adversarial review of
`noteworthy_trajectories/azorae_stride_remake_by_gigo_20260708.trajectory`
(gemini-3.5-flash, 100 steps, terminal=BUDGET, no solve). Second run on this map;
the first (2026-07-04) is analyzed in `azorae_stride_postmortem.md`. Between the
runs the NavSkeleton flood rewrite (F1→P5), the flat-seat/honest-POWERED laser
fixes (7c0cea66, 7be6ce54, 955ef368), the dropper birth-origin backstop
(1fff52c1), and two prompt updates (0bee4d84, 59003b77) all shipped — this run
is the acid test of those fixes.

Method: 6 independent trajectory reviewers (per-step attribution ledger, thinking
traces as primary evidence, frames consulted), BSP ground-truth reconstruction,
run-over-run comparison, 4 code audits (interpose / place_portal / laser percept /
verb grammar), adversarial verification of every finding (13 CONFIRMED,
2 REFUTED), an attribution judge, a completeness critic, and a web-verified
check of fizzler physics and the Portal complexity literature.

---

## 1. Verdict

**Reasoning-dominant failure, high confidence.** Per-step attribution over all
100 steps: **55 ok / 40 reasoning / 3 percept / 2 harness / 0 grammar**. The
"model cannot exert its will on the environment" hypothesis is falsified: every
physical effect the model correctly specified, the harness delivered — go_to
descended the height seam in one verb (step 2 vs step 37 in the 07-04 run),
interpose seated the cube at the exact requested fraction every time, target 13
was genuinely powered on 5 separate occasions (steps 25-28, 43-46, 49, 52, 71)
with the fizzler dropping in lockstep.

The sharper finding — CORRECTED after researcher input: **the chamber contains a
designed trap, and the model spent the second half of the run inside it.** The
entrance-side portal-tuning line the model pursued (beam → cube → Pb near-side →
Po far-side → target 14) is the tempting local-progress basin the chamber is
built around, and it is globally unconvertible: with target 13 unpowered the
fizzler is ON, so the far portal cannot be re-placed from the entrance side
(shots across are eaten) and walking through the fizzler to fix it CLEARS the
placed portals — un-powering 14 and shutting the door on the far side of the
grill you just crossed. Powering 14 from the entrance side is progress you
cannot cash. The one question the chamber poses — "suppose this beam config
works: can I still reach the door?" — the model never asked until step 99,
whose final plan (power 13 → cross during the window → rebuild BOTH portals
from the exit side using the beam's natural S3 terminus) contains most of the
actual trap-break, formed with zero budget left. It had FIVE fizzler-off
windows (25-28, 43-52, 71) in which to be across and rebuilding; it crossed
during one (step 25) and walked back.

An earlier judge pass framed steps 65-75 as "abandonment of a near-solve"
(z=334 derivation, interpose-0.24 x-y bullseye on 14, `redirect_to` cancelled at
step 75). The arithmetic is right, the framing is wrong: what was abandoned was
a trap-conformant configuration, not a solve. The step-75 self-talk and the
75-92 blind fraction sweep remain genuine verification-discipline failures —
they just happened *inside* a basin that could never pay out.

Honesty caveats the verdict carries:

- The CONFIRMED harness bugs (§4) cost ~8-12 steps as aggravators; interpose's
  under-delivering `[aim]` shaped the step-75 behavior. None of them touch the
  trap diagnosis — no percept lie was needed to miss it, and no fix would have
  made it for the model.
- "Reasoning failure" here means global plan verification and trap diagnosis —
  not local physics. The model's portal/laser reasoning was repeatedly locally
  correct and self-correcting (the z=334 eureka is real geometry); it optimized
  the local gradient the chamber deliberately dangles and never priced the
  conversion step.

## 2. Ground truth (BSP I/O graph — fully stock, authoritative)

BSP: `sourcemods/p2agent/maps/workshop/927053855830294446/1522623535.bsp`
(704 ents, 516 I/O edges, zero custom VScript).

- **Emitter** mark 10 `laseremit12-laser` (832,-16,160), fires +Y, beam z≈146.
- **Target 13** = sensor of `lasercatch16` (1088,0,288). Powered → fizzler
  `barrierhazard13_brush` (marks 1/2/3, plane y=704) disables. **Not latched.**
  Wired to NOTHING else — a traversal side-gate only.
- **Target 14** = sensor of `lasercatch42` (448,1280,288). Powered →
  `@exit_door.Open`. **The ONLY edge that opens the exit.** Not latched.
- **Button 11** `button17` → re-drops the reflective cube (mark 15). Pure cube
  reset; the model pressed it zero times in 100 steps (`interact` usage: 0).
- Marks 6/7: off-map elevator doors at 3000-4600u — distractors.

The crux: **the beam is born at z≈146; both catchers sit at z=288.** A ~142u
vertical lift through a portal pair is forced. Genuinely multi-level (floors at
z≈0/128/256, cube ledge ~z≈520). Exit needs a *second, different* beam rig than
the fizzler rig; the model oscillated between the two (Po flip-flopping S4↔S2)
instead of sequencing them.

**The trap (researcher-confirmed).** Powering 14 from the entrance side needs a
portal pair straddling the fizzler plane. That configuration is unconvertible:
the far portal is locked (portal shots across an active fizzler are eaten; the
model's far-side NO_LOS/FIZZLED wall was this lock announcing itself), and
crossing the fizzler on foot clears your placed portals — killing the beam
route and closing the door you opened. The chamber deliberately rewards
entrance-side portal-angle tuning with visible progress (beams move, captions
fire, 13 lights) that can never be cashed into a walkable exit. The break:
use a fizzler-off window (13 powered) to get yourself across, then rebuild both
portals from the exit side, where the beam's natural S3 terminus (lasers pass
through fizzlers) is capturable and both portals stay in reach.

Physics fact the run turned on (web + engine verified): **lasers pass through
active fizzlers** (trigger brushes are invisible to the beam's MASK_OPAQUE
trace); **portal shots are blocked** (engine `PORTAL_PLACEMENT_CLEANSER` →
harness `FIZZLED`); **players walk through unharmed** (the prompt's "destroys
you" is false — see A1). The model held contradictory beliefs about all three at
different steps and was never corrected.

## 3. What happened, run-over-run (07-04 → 07-08)

The shipped fixes worked — with causal fingerprints, not just vibes:

| 07-04 failure mode | 07-08 status |
|---|---|
| GoToPlanner single-Z flailing (24 steps to descend; ADVANCED×10) | **FIXED** — cube in hand at step 3; zero ADVANCED/EDGE/BLOCKED all run |
| Fake laser POWERED (vphysics-freeze pitched cube) | **FIXED at verb level** — POWERED gone from results; target 14 never fake-powered (07-04 faked it steps 40-44, 67-92) |
| Dropper phantom mark 16 | **FIXED** (birth-origin backstop) |
| aim_at/look orientation churn (32 steps) | **GONE** — aim_at used 0 times |
| place_portal NO_LOS spam, bare detail | **RECURRED** (8×, detail still just repeats the code) |
| ~50-step dead tail with no loop-breaker | **RECURRED in new costume** — steps 53-99 parameter-search loop (not blind verb repetition, but 0 progress) |

Milestones: cube pickup step 37→3, first fizzler kill 45→25, player on exit side
~55→26. Tokens: 14.5M→15.6M in. Both runs die at BUDGET — but for different
reasons. **The harness fixes moved the failure from actuation to reasoning.**
(Also the run's central irony: the exit-critical target 14 was never powered
once, while the side-gate target 13 was powered five times.)

## 4. Adversarial review — verified findings

13 CONFIRMED, 2 REFUTED (each independently attacked with code + frames).

**Prompt/percept lies and leaks (poison attribution):**

1. Prompt claims fizzlers "destroy you" — false; model planned a suicide-reset
   detour (35-37), then walked through alive and out-corrected its own prompt.
2. Every panel percept hardcodes `class: wall_panel`, `state: {}` — ceiling
   panel S5 mislabeled; no normal/axes despite `plane_normal` being on the wire.
3. Post-interpose frame renders ~180° off the reported eye_yaw (SHM one render
   behind) — a visual lie at exactly the verify-the-beam steps.
4. Game closed captions baked into frames ("[Laser Node Activated]", no entity
   id) — model mis-attributed the event to target 14; false belief seeded.
5. No beam-path/endpoint telemetry anywhere: a target's binary `powered` is the
   only laser feedback (the enabling condition for the 75-92 blind sweep).
6. Panel u/v basis unknowable from telemetry (color grid readable only on-frame).

**Verb-surface bugs (silently alter or under-deliver):**

7. interpose's optional `[aim]` reports "aimed" but does not refresh the
   downstream powering an identical follow-up `redirect_to` achieves (clean
   control at steps 68-71). Direct enabler of the step-75 abandonment.
8. place_portal silently clamps `@u,v` to fit — a clamped placement is
   byte-identical to an exact one (the hidden y=1248 cap on S2 spawned two
   fabricated obstruction theories).
9. go_to never re-aims the camera — post-move frames are point-blank wall shots
   (frames 008/016/026), each costing look-steps and image tokens.
10. interpose silently relocates the player (FollowTo walk unreported,
    `moved_dist`=0).
11. redirect_to's "N deg off" fuses irreducible pitch with correctable yaw — the
    constant 3.7° (= atan(46/704), flat cube vs elevated Pb) read as a mystery
    quantization instead of a derivable geometric fact.

**Verb friction (honest but expensive):**

12. Testing one beam fraction costs a ~4-step loop (go_to+pick_up+interpose+wait);
    a seated cube can't be nudged.
13. place_portal fires from the current eye only; no "go where you can see Sn"
    primitive.

**REFUTED — do not act:**

- "Active fizzler returns NO_LOS instead of FIZZLED": FIZZLED is live and
  correctly mapped (`MacroExecutor.cpp:1251`); the 8 NO_LOS steps were proven
  geometry occlusions by an A/B (steps 41 vs 43: fizzler OFF, still NO_LOS).
  Residual open question (FLAG-1, §7 P2): `TraceFirePortal`'s `ret==0` semantics
  when the shot *crosses* a fizzler curtain mid-flight were never probed.
- "Missing portal-park verb forces the S4↔S2 dance": the dance is intrinsic
  puzzle mechanics (one orange portal, two mutually exclusive jobs).

## 5. Fix plan (pre-paper A / fairness B / defer C)

**A — MUST-FIX before the paper eval (~1 day total, ordered by value):**

- **A1** Fix the fizzler-lethality sentence in the gemini prompt (2 min). Add
  one line: fizzlers do NOT block laser beams; they do block portal shots.
- **A5** Delete interpose's optional `[aim]` + "aimed" suffix — report the seat
  only, force the canonical interpose→redirect_to (~1-2h). (The heavier
  alternative — wake vphysics so aim actually refreshes power — is deferred.)
- **A2** Panel percept: derive wall/floor/ceiling from `plane_normal`, surface
  the normal in `state` (~10 LOC Python).
- **A6** GATE (inspection, ~1h): confirm the solve signal is the exit-door I/O
  event (map-conditional laser certifiability — flat-floor seats are now honest;
  slope/goo/retracting-support are not).
- **A3** Advance one render tick before post-interpose frame capture; verify
  against real frames (~1-2h; needs a game boot).
- **A4** `cc_subtitles 0` + `closecaption 0` in the harness autoexec (15 min).
  Judgment call: removes information, but the percept surface must be designed,
  not leaked. Trivially reversible.
- **A7** Report requested→actual + delta (or CLAMPED) when place_portal bumps
  the u,v point (~10 LOC).

**B — SHOULD-FIX for solve-rate fairness:**

- **B1** go_to re-aims eye at the resolved target on arrival (~5 LOC).
  PROMOTED TO A by the researcher (2026-07-09) along with redirect_to camera +
  verb mobility — full plan in §10.
- **B2** Grammar vocabulary drift: delete phantom `NOT_REACHABLE`, document real
  codes (`BLOCKED`, `NO_BEAM`, `NOT_HOLDING`, `NOT_REFLECTOR`) in prompt +
  grammar + smoke.
- **B3** Client validator wrongly rejects `Sn@u,v` for go_to/aim_at (the server
  accepts it) — one shared `classify_point()` mirroring `ClassifyTarget`.
- **B4** interpose reports its walk (`moved_dist`), 2 LOC.
- **B5** Split yaw/pitch in redirect_to's residual detail.

**C — DEFER until after the paper:** beam-endpoint telemetry as a percept field
(honest sparsity, an *enablement* that changes what the eval measures — a scope
decision, not a soundness fix; but see §6, the grammar gives it a cheap
address-only form), u/v axes in text percept, seated-cube nudge, LOS-vantage
primitive, full grammar unification (§6), vphysics wake+support-recheck.

- **C7 — TODO (researcher, 2026-07-09): delta percept.** Advertise per-step
  mark DELTAS instead of the full 22-line list every step (this run: ~290k
  input tokens/step by the endgame, mostly unchanged lines; the signal the
  model kept missing — `powered` flips, fizzler toggles — is exactly what a
  delta would foreground). Needs a staleness escape hatch: full list every N
  steps or an on-demand refresh, else absent-but-relevant entities fall out of
  the model's working set (this model already forgot derived facts it could
  re-read; a delta percept removes the re-read). Interacts with the vision
  ablations in §8 — decide there, not casually.

## 6. Verb grammar — unified point algebra (design, unimplemented)

Full audit + proposal in the workflow artifacts; the shape:

**One reference grammar, two slots** (`<verb> target [at]`). An entity mark is
simultaneously object and point; the point algebra is closed:

| form | syntax | note |
|---|---|---|
| mark | `N` | entity OBB center |
| panel | `Sn` = `Sn@0.5,0.5` | sugar |
| panel point | `Sn@u,v` | exists |
| **beam point** | `N@f` | NEW — fraction along emitter N's beam; **`N@1.0` = beam end** |
| portal | `Pb`/`Po` | live mouth |
| crosshair | `X` | live, fenced (see below) |
| held | `held` | explicit token for the carried entity |

`N@f` and `Sn@u,v` are the same `@`-parameterization on 1-D vs 2-D elements —
that symmetry is the design. Every slot resolves through the single existing
`ResolveTarget` choke point, so `aim_at 10@1.0`, `go_to 10@0.5`,
`redirect_to 15 10@1.0` come for free (~20 LOC per parser);
`place_portal at the beam end` additionally needs the world-point → (panel,u,v)
inverse (a loop of the existing `PortalOnPanel` test + inverse bilerp).
Proto diff is net-negative: `percent`, `color`, `where` all retired; `aim`
renamed `at` (it was object-overloaded by drop_into). Verbs: interpose becomes
`interpose held 10@0.24` (seat is a first-class point, per the "interpose's
point should be portalable-style" requirement); release widens to any point;
drop_into's aim-as-object arm deleted; reject codes collapse to
`BAD_TARGET`/`WRONG_KIND`. No compound/solver verbs ("place portal so the laser
hits the receiver" stays out — the grammar edits graph edges, never computes
the puzzle). Future elements slot in without new forms: funnels are 1-D (`N@f`),
bridges/gel are 2-D (`Sn@u,v`), turrets are objects.

Crosshair `X`: mechanically trivial (the executor never fires the real weapon —
it computes placement via the gun's own `TraceFirePortal` and commits via
`portal_place`; the recon spike `sar_harness_portal_fire_spike` is already
exactly aim-free fire). Design-wise it is the one **view-state-dependent** form
— the same confound that killed `use()` in the earlier grammar rethink. Options:
park it (grammar audit's call), or ship it fenced to place_portal/go_to/pick_up
where "where I'm looking" is the entire intent, never the interpose seat
(proposal's call). Either is defensible; nothing in the current eval needs it.

Interpose at the beam end, code truth (answering the design question directly):
**`interpose <emitter> 1.0` does NOT reliably work out of the box.** The seat
drops the beam's Z (floor projection), places the cube's XY exactly on the
impact plane (half-embedded in the wall — no back-off margin exists), and if the
impact point sits >~39u above floor+halfHeight the interception check is
*guaranteed* to fail — reported honestly as NOT_INTERCEPTING, but only after the
full carry. Non-horizontal beams: the beam math is 3D; straight-down ceiling
beams work by accident (seat lands under the beam, top face catches); steep
diagonal beams fail honestly at most fractions. A beam-end seat needs a small
back-off along the incoming ray + the existing floor drop — fold into the `N@f`
work, not a special case.

## 7. In-game verification playbook (user-run; each is minutes)

- **P1 — Document the trap (paper-figure material).** Scripted replay via
  macro_repl: cube `interpose 10 0.24 Pb`; `place_portal blue S1`;
  `place_portal orange S2@0.98,0.20`; trailing `redirect_to 15 Pb`; read target
  14 `powered` (with/without the redirect also settles whether A5's bug
  swallowed the step-74 attempt). If 14 lights: walk through the fizzler toward
  the exit and record the trap springing — portals cleared, 14 un-powered, door
  shut on the far side. One replay yields the chamber's thesis as a figure.
- **P2 — Fizzler-cross portal shot.** Stand south of the y=704 fizzler, aim at
  clean far wall (S3), fire; read `sar_pp_hud` / log `ret`+`ePlacementResult`.
  Resolves FLAG-1 (NO_LOS-vs-FIZZLED on cross-curtain shots). Only a paper
  blocker if eval maps require shooting across fizzlers.
- **P3 — interpose envelope.** Tiny PeTI recon chamber: wall-impact beam
  (`interpose 1.0`), ceiling emitter, 45° emitter. Closes the critic's "Q2/Q3
  are static code reading" gap; natural home = `agentloop_smoke.py`.
- **P4 — Beam visibility.** From the step-75 pose, `aim_at 14` / `look` at the
  beam terminus; confirm the rendered frame actually shows where the beam lands
  (tests the judge's "cheap in-grammar remedy existed" counterfactual).
- **P5 — Cube state at powered ticks.** During P1, `sar_harness_dump_fields`
  velocity/support of the cube while 13/14 is lit (closes the frozen-cube
  fake-success audit for this map).
- **P6 — After A3 ships:** eyeball post-interpose frames match telemetry yaw.

## 8. Paper plan (verified against Demaine/Lockhart/Lynch, FUN 2018)

**Complexity positioning — corrected in both directions.** The folklore claim
"buttons+lasers+portals is weakly NP-hard, probably PSPACE-complete" is wrong
twice: (a) it undersells — **cubes + weighted buttons + doors alone is strongly
PSPACE-complete** (Thm 13), and the paper states the gadgets are constructible
in the stock Puzzle Maker; no portals, no lasers, no long falls needed; (b)
"buttons+lasers+portals" is not a cited theorem combination (lasers appear in a
separate PSPACE-completeness result, Thm 14, paired with relays + moving
platforms). Membership: the *entire* v0 element set is in PSPACE (Thm 12).
Paper phrasing: cite Thm 13 + Thm 12; never call individual maps PSPACE-hard
(worst-case asymptotic statements say nothing about one finite instance —
methodological cover, not per-map difficulty).

**Claims.**
- **Claim B (headline, data in hand):** action-interface honesty and
  observability gate LLM performance on a PSPACE-complete puzzle domain.
  Evidence: robust-verbs ablation (third_light BUDGET→SOLVED, 686k→286k tokens);
  honest-laser+NavSkeleton reruns moving the failure mode off the harness; the
  inverse — missing beam telemetry converting a solvable sub-puzzle into a
  30-step blind sweep.
- **Claim A (second contribution, gated):** with perception + locomotion
  supplied as reliable macro-verbs, the dominant residual failure is reasoning —
  specifically verification timing and plan abandonment, quantified by a
  per-step attribution taxonomy. Gates: scripted-oracle solver certifying every
  map solvable through the interface, human baseline through the SAME verbs,
  ≥3 model families, frozen+versioned harness. Without those, "reasoning
  failure" is unfalsifiable.

**Curation pipeline** over `py/bsp_recon/dump_ents.py` output: element-whitelist
gate (no turrets/faithplates/funnels/gels/bridges), **VScript-purity gate**
(no custom .nut → I/O graph authoritative — the azorae ground-truth
precondition), exit-oracle-compatibility gate (standard exit choreography;
108/134 in-scope maps qualify), human-solvability-through-verbs gate (doubles as
the baseline), size sanity. Stratify by io_depth, gate count, element families,
fan-in/out coupling (the NCL AND/OR signature — the actual hardness driver),
multi-level flag. **Core N = 24-36, 3 tiers × 8-12** (floor N=20). Funnel figure:
277 local → 157 in-scope PeTI → ~40-80 survivors → sample.

**Laser caveat:** solve *detection* on laser maps is fine (exit oracle is
door-latched), but reasoning-vs-percept *attribution* on laser failures is
confounded until beam-endpoint telemetry ships. First paper's clean-attribution
core = cube+button+door(+portal+fizzler) — which is exactly the Thm-13
PSPACE-complete fragment; lasers as a marked preliminary tier, or gate on the
telemetry.

**Metrics:** solve rate, steps/tokens-to-solve, budget-exhaustion, loop rate,
verb-reject rate, plus the taxonomy as contribution: per-step blame
{ok, reasoning, percept, harness, grammar} × failure class {never-formed,
formed-then-abandoned, formed-couldn't-execute, percept-blind,
**trap-captured** (optimizing an uncashable subgoal — see §9.6 trap-basin
dwell time)} + ever-had-the-solution rate. Judge validity: human-annotate ≥3 episodes/tier,
report Cohen's κ, blind the judge to model identity.

**Ablations reviewers will demand:** robust-verbs (done), scripted oracle,
human baseline, ≥3 models, annotated-vs-raw frames, vision-vs-telemetry-only,
±I/O-graph-in-percept, ±beam-endpoint telemetry (the laser smoking gun),
random-verb floor, budget sensitivity (30/60/100), prompt-hygiene rerun after A1.

**Timeline:** W1 freeze+tag harness, A-fixes, corpus sweep + funnel; W2 human
baseline + oracle solver, finalize N≈30; W3 main eval (≥3 models × N × ≥3
seeds); W4 attribution + ablations + κ; W5 figures + draft; W6 buffer. Long
poles (W2-3) are user-gated game runs. If they slip: Claim B ships alone,
Claim A demoted to preliminary taxonomy.

**Threats:** harness non-stationarity (freeze+version; quarantine maps where a
known gap plausibly caused failure), LLM-judges-LLM circularity (κ + blinding),
single-model generalization, reasoning-vs-legibility confound (percept
ablations + prompt-hygiene rerun), construction bias + complexity over-read
(pre-registered filters, published funnel, §complexity disclaimer).

## 9. Open questions

1. RESOLVED (researcher): the step-74-family configs are the chamber's designed
   trap, not a near-solve — entrance-side portal tuning toward 14 can never be
   converted into a walkable exit. P1 now documents the trap rather than
   certifying a solve; A5's swallowed-aim sub-question still rides along.
2. RESOLVED by (1): the GT-agent-vs-judge disagreement over which target the
   late loop chased is moot — both sub-goals lived inside the trap basin.
3. Beam-endpoint telemetry: percept field (C, changes what the eval measures)
   vs address-only `N@1.0` (grammar, lets the model *look* at the end but not
   read it numerically). Which side of the line does the paper eval want?
   Note the trap sharpens this: endpoint telemetry would have made the basin
   *more* seductive (faster convergence on an uncashable subgoal) — evidence
   that observability fixes and reasoning demands are separable axes.
4. Crosshair `X`: park vs fenced-ship (§6). Nothing current needs it.
5. Attribution-ledger methodology for the paper: rubric + second annotator + κ
   (critic #10) — needs writing before W4.
6. Trap chambers as a first-class benchmark tier: curation can surface
   candidates from the I/O graph (mutually exclusive subgoals sharing one
   resource — here 1 cube / 2 unlatched catchers — plus a portal-clearing
   barrier between the goal region and the resource region). Proposed metric:
   **trap-basin dwell time** (steps spent optimizing a subgoal whose conversion
   precondition is provably violated), and whether the agent ever poses the
   conversion question at all. This run: ~44 steps dwell, question first posed
   at step 99/100.

## 10. Next session: verb mobility + camera honesty (planned 2026-07-09, user-approved)

Trigger: this trajectory pairs `go_to X` before every `pick_up X` (11×) and
before every `redirect_to X` (8×) — the mobility split is pure step tax. And
the camera-after-verb desync is promoted to the A-list: fix before publishing
the harness. Scope decision (user): **pick_up + redirect_to walk; release stays
proximity-gated** (walking-while-holding is a carry — interpose's machinery,
fizzler-crossing physics — deserves its own pass).

Code receipts (read 2026-07-09, so tomorrow needs no re-derivation):

- `Interact` is already the composition to copy: `GoTo(target)` →
  `AimAt` → `PulseUse`, with nav failures reframed
  (`MacroExecutor.cpp:2745-2778`). `GoTo`'s own comment says its reach is
  "tight so a follow-up pick_up/interact lands in grab range" (:2207).
- `PickUp` (:2372): one main-thread Pre block does resolve + grabbable-class +
  reach; `OUT_OF_REACH` at `kGrabRange` (:2407). `LookBackAt` (:722-731) is the
  existing re-aim helper (entity-keyed, 1 settle tick).
- `RedirectTo` (:1983-2067): reach gate at :2027-2031 ("go_to it first" is
  baked into its failure detail); **never touches the camera** — after AimCube
  the view is whatever the previous verb left.
- `GoTo` (:2175-2240): FollowTo then telemetry only; no terminal re-aim.
- AgentLoop ordering (`Portal2HarnessImpl.cpp:627-668`): ExecuteMacro →
  Observe (telemetry) → `CopyPixelsToShm` (:663). `CopyPixelsToShm` (:587)
  reads the CURRENT framebuffer via `ReadScreenPixels`; the world is frozen
  between macros, so nothing re-renders after the verb's final view command —
  the copied frame shows the pre-command pose. A3's evidence says
  `LookBackAt`'s 1 settle tick is not enough (SHM "one render behind").

Phases (tiny, hand-verifiable, C++ then Python text, smoke last):

- **P1 — `go_to` terminal re-aim** (~8 LOC). Point-variant of `LookBackAt`
  (`LookToward(context, Vector)`: `ApplyAbsoluteView(AimAnglesTo(eye, point))`
  + 1 tick) called at GoTo's tail on ok — both SUCCESS and REACHED_PROJECTION
  (facing the unreachable target is exactly what the model wants to see).
  Verify: macro_repl `go_to` → frame shows the target, not a wall.
- **P2 — `pick_up` walks** (~20 LOC + text). Hoist resolve + grabbable-class
  gate to a pre-check (don't walk to a button to fail NOT_GRABBABLE), then
  `GoTo(target)` (interact-style: require `nav.reached()`, reframe failures as
  "pick_up: not in reach (…)"), then the existing Pre (reach re-check, honest
  OUT_OF_REACH if the walk parked short — e.g. cube on a high ledge), AimAt,
  PulseUse, Post confirm unchanged. Prompt sheds the whole "Does NOT walk you
  there / go_to it FIRST" paragraph (macro_grammar.py verb doc +
  gemini_agent.py notes) — the prompt gets SHORTER.
- **P3 — `redirect_to` walks + ends facing the cube** (~10 LOC + text).
  `GoTo(cube)` before the gate (unconditional — a no-op walk when already in
  reach), keep the reach gate as the honest backstop; after AimCube, LookBackAt
  the cube (mirrors interpose :1229) so the frame shows the beam leaving it.
  Prompt drops "Stand next to the cube -- else OUT_OF_REACH, so go_to it
  first".
- **P4 — capture-side frame sync (the A3 root fix, all verbs at once).** In
  AgentLoop, when `copy_pixels_to_shm` is requested, advance 1 tick BEFORE
  InternalObserve + copy, so the renderer catches up with the final commanded
  view and telemetry + frame describe the same post-tick state. Cost: one
  world tick per pixel-observe (softens the frozen-world contract by ~16ms of
  sim per step — acceptable; telemetry/frame stay mutually consistent). If
  frames are STILL one behind in-game, bump to 2 ticks — the render-pipeline
  depth can't be certified from code (FLAG-4: needs the visual check).
- **P5 — smoke + docs.** agentloop_smoke.py: pick_up-from-across-the-room,
  redirect-from-across-the-room, and (manual visual) post-verb frame yaw ==
  telemetry eye_yaw. Update §5's A-list: B1 promoted here.

Verification is user-run in-game (per house rule); each phase lands separately.
