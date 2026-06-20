# Offline BSP preprocessing — static structure, the I/O causal graph, and affordance priors

Design record for an **offline map-preprocessing layer**: parse each chamber's `.bsp` (or
`.p2c`/`.vmf` when authored locally) **once** into a per-map JSON sidecar carrying the
**button→relay→door→platform I/O causal graph** and a typed **affordance prior**
(funnels/bridges/lasers/plates/portalable surfaces) — and, *parked*, a static collision grid that
runtime A\* already dominates (§2a). It **complements**, does not replace, the runtime
[A\* locomotion layer](astar_routing_design.md) (geometry-only, current *dynamic* state) and the
[C9 save/restore](llm_percept_act_phased_plan.md#L182) puzzle-search layer.
Fleshes out the idea-staged offline precedents — `exit_detection_brainstorm.md` §X6,
`diverge_catalog.md` §G6/§B1, [exit_criteria_structure.md](exit_criteria_structure.md). Read
[astar_routing_design.md](astar_routing_design.md) §2/§5 first. Written 2026-06-20.

> Status: **recon-first, nothing committed.** The whole thing is gated on a one-evening Python
> spike (P0) that the user runs against the real training BSP. Build only if P0's causal graph
> matches a hand-traced chamber. Sits next to A\* (`79637dd0` lineage), not on top of it.

---

## 1. The problem (what runtime perception structurally can't cheaply get)

The runtime harness — by design — sees **a flat bag of dynamic entities + their current OBBs +
each one's current scalar status + one binary `chamber_complete` latch**
([Portal2HarnessImpl.cpp:94-180](../src/Features/Harness/Portal2HarnessImpl.cpp#L94),
`harness.proto:109-139`). The recon audit (`codebase:runtime-percept-surface`) makes the gap
explicit and it is *large*. Three classes of knowledge are **structurally invisible** to a per-tick
entity walk, and no amount of A\* hull-tracing recovers them:

1. **The I/O causal graph — "button X opens door Y."** The harness reads each entity's *current*
   status field independently; it has **zero** knowledge of the wiring. The
   `OnPressed → door,Open,delay` connection that *encodes the causality* lives in the BSP entity
   lump as plaintext keyvalues and is **never captured at runtime** — the snapshotter sees the
   door's state bit, never *what controls it*. This is exactly what L2 defers
   ([locomotion_tech.md](locomotion_tech.md) §5) and what the model "currently has to guess"
   ([exit_criteria_structure.md](exit_criteria_structure.md)).
2. **The affordance prior — typed movement envelopes + portalable surfaces.** A\* searches *static
   world geometry only* ([astar_routing_design.md](astar_routing_design.md) §2; "blind to causality
   by construction" §5). It has no concept of "this `trigger_catapult` flings ~Xu in direction D,"
   "this funnel rail goes here," "this wall is white/portalable." Portalability isn't even in the
   geometry — it's a `%noportal` flag in the surface's VMT (`web:bsp-format-parsers` §4). The
   harness has **zero** portalable-surface knowledge (confirmed: no `SURF_`/`TraceFirePortal` hits).
3. **Ground-truth annotation labels.** Track A annotation (`PuzzleAnnotate.cpp`) is *pure
   classname-keyed entity boxing* — it **cannot** annotate folding panels/stairs (`func_brush` keyed
   by targetname, classname can't match them) or gels/bridges (surfaces, not box-able entities),
   per its own comment. A static parse keys those by targetname + brush model and resolves the
   exact thing the live walk drops.

The honest framing (`web:dynamic-geometry-navmesh-affordances`): offline gets you the **set of
*potential* affordances** (where a portal *could* go, what arc a plate *would* fling, who a button
*could* affect) and the **static structure**. It does **not** get the **realized** graph (where the
player actually placed portals, which funnel branch is live, where the cube ended up). That's the
clean complementarity line — and it's the same line A\* (geometry) vs C9 (config-space search)
already draw, one layer up.

**The research-design catch — who is this *for*?** The harness exists to measure *whether reasoning
is the bottleneck* (`ROADMAP.md:9-17`), and the grammar doc explicitly refuses to "smuggle
chamber-solving into the actuators" ([llm_percept_act_grammar.md](llm_percept_act_grammar.md) §5).
Handing the model the button→door wiring *as a percept* risks doing exactly that — solving the
puzzle for it and **confounding the very thing we measure**. So the causal graph's safest,
highest-value consumer is **us, the experimenters**, not the model: ground-truth for building +
scoring evals, validating the harness/oracles, richer annotation — and the genuinely interesting
use, a **hint-ablation knob** (feed the model 0% / partial / full wiring and measure the
reasoning-difficulty curve). Model-facing exposure becomes a *deliberate, dialled* ablation rather
than a baked-in assist — which flips the confound from a liability into a feature, and is the
framing that gates any percept-facing use (fork #7).

---

## 2. Recommended design — a per-map JSON sidecar, keyed by map name

One offline Python tool over the map corpus emits **`<mapname>.mapinfo.json`** next to each BSP
(content-addressed by the BSP's own CRC so a recompiled chamber invalidates cleanly). The harness
(or the Python percept) loads it by map name at `reset`. **The engine stays the source of truth for
all dynamic state** — the JSON is a static *prior*, never a runtime authority. Three payloads:

**(a) Static nav/geometry layer — _parked, dominated by A\*._** You *could* reconstruct a coarse
occupancy/floor grid from the brush lumps (`LUMP_BRUSHES`(18) + `LUMP_BRUSHSIDES`(19) +
`LUMP_PLANES`(1), convex ∩-of-half-spaces — **avoid** `LUMP_PHYSCOLLIDE`(29), compiled VCollide
blobs) on A\*'s 32u lattice to warm-start its lazy grid. **Don't, for v0.** Runtime A\* already gets
ground-truth geometry from µs hull traces *including the current dynamic blockers* — and the
motivating cube-on-a-button pocket is a *dynamic* obstacle a static grid can't see, so the offline
grid is useless for the one case A\* was built to fix, while adding a permanent offline↔runtime sync
liability. A nav mesh is worse still (Source ships none for P2; combat-cover metadata, no
jump/portal/fling edges — `web:dynamic-geometry-navmesh-affordances` §3). Revisit only if profiling
ever shows A\*'s per-`go_to` trace budget is a real hotspot (the astar doc says **measure first**).
This is the §G6 "coarse reachability graph" idea — kept on ice.

> *The one in-principle non-dominated spatial role:* **sub-cell clearance** for A\*'s cost-term
> (sub-cell-exact distance to wall planes, which the lazy runtime grid can't give globally). But even
> there a runtime *truncated* distance-transform + simply **halving the cell 32u→16u** beats threading
> BSP-plane distances, so it stays a *rides-along-if-this-pipeline-exists* precision upgrade, never
> built for itself — see [astar_routing_design.md](astar_routing_design.md) §2 (clearance cost-term) +
> fork #7 + §7. Pattern: every *spatial* use of offline geometry is dominated by runtime; BSP earns its
> keep on the *non-spatial* payloads (b)/(c).

**(b) The I/O causal graph** — *the unique, load-bearing payload.* Walk every entity's outputs,
build a directed multigraph: node = entity (`targetname`/`classname`/`origin`/`mark-able`), edge =
`(output, target, input, param, delay)`. Transitively chase `logic_relay`/`logic_branch`/
`logic_auto`/`math_counter` hops so `button → relay → door.Open` collapses to a causal chain. Tag it
a **possible-effects (over-approximation) graph**, not guaranteed edges (branch/counter logic gates
on runtime state — you get topology, not which path fires). This is the thing nothing else gets
cheaply.

**(c) The affordance prior** — per movement entity, its *typed, parameterized envelope* (static
from keyvalues): `func_movelinear` (movedir/movedistance/speed — fully static), `func_door`
(direction+speed static; travel = brush-size-along-axis − lip, so needs the brush model),
`trigger_catapult` (computable ballistic arc from launch dir + speed + `sv_gravity`),
`prop_tractor_beam` + the `env_portal_path_track` rail chain, `prop_wall_projector` ray,
`env_portal_laser` first segment. Plus a **portalable-surface map** (texinfo→material name →
`%noportal` VMT lookup + `func_noportal_volume` brushes), with flip-panel faces flagged
*conditionally* portalable. Every edge is marked `conditional: <what-runtime-state-gates-it>` so the
consumer knows it needs runtime confirmation.

**(d) Surface + region topology — the "left-route-vs-right-route" spatial-vocabulary idea
(_recon-gated; the BSP part mostly deferred_).** The sharp form of "geometry buys more than a path
grid": *you're on surface S88 near button 1; two routes to the goal, left via region L and right via
region R; a cube now sits in L's chokepoint → only R is open.* This is geometry-as-**semantic
structure**, not geometry-as-path-grid (a) — and the gap is real: A\*'s output is **nameless
waypoints**, so the model has no token "region L" to reason over, and the flat entity bag has no
topology. But the honest resolution is **not** an offline region pipeline:
- **A clean BSP-native room graph is dead for PeTI.** Areas/areaportals are author-placed render
  optimizations the Puzzle Maker never emits → the whole chamber is one area (1-line check:
  `len(BSP.areas) <= 1`). Visleafs are render-shaped + over-fine (one room = many leaves; PVS
  clustering merges rooms joined by a sightline — PeTI chambers are very open). So a region graph
  means a **custom watershed over a walkable grid** (~100 LOC) that **breaks on Portal's defining
  connectivity** — portals/flings/goo connect space that isn't spatially adjacent, so a 2D watershed
  silently mis-segments exactly the chambers that matter.
- **A wrong region graph poisons the eval.** A mis-segmented graph emits a confidently-wrong "left
  blocked" — label noise in the one thing the harness measures. Worse than no graph.
- **The confound (fork #7, again).** "→ take right" *is the answer*. The percept may carry **facts**
  (`exit L: blocked_by cube 5`), never the **conclusion** (`recommended: right`) — the SiT-Bench
  keyword-match line. And "one of two exits blocked ⇒ take the other" is a *single trivial step*, so
  even the facts-only version sits right on the confound boundary here.

**What's clean, and what to actually do.** Surface→panel IDs *are* the one solid BSP-native win
(PeTI's 128u grid makes faces ≈ panels; key by geometry-hash with their own namespace `S88`, since
surfaces aren't entities and **can't reuse `MarkTable`**) — but nothing in v0 *consumes* them; they
serve a future portal-placement-reasoning feature (not this doc), so defer. For the left/right gap itself the minimal honest
path skips BSP entirely and lives **Python-side in the percept**, never the planner (region-graph-
as-A\*-oracle is redundant + dominated — the §2a / astar §7 verdict; never attach it there): recon
first (P0d), then — only if the model needs it — a *cheap, possibly hand-authored* region **label**
(2–3 world-coord AABBs/chamber, point-in-AABB on live position) emitting facts-only, as a decorator
at `entities._mark_dict` / `gemini_agent._percept_text`. Zero BSP, zero proto. The runtime
"which surface is E on" primitive already exists (the `CheckEdge` down-trace, `MacroExecutor.cpp`),
~15 LOC to expose — no offline parse needed for it.

**The complementarity, stated once** — *offline = static structure + the causal wiring + affordance
envelopes (the prior); runtime = current dynamic state (door open?, catcher lit?, portals where?,
cube where?, funnel direction?).* The agent reads the offline graph to know "button 3 *can* open
door 7," then reads live telemetry to know "door 7 *is* open now." Neither replaces the other:

| Layer | Owns | Source | Cost per use |
|---|---|---|---|
| **Offline sidecar** (this) | I/O wiring, affordance envelopes, GT labels (_static geometry parked — A\* owns it_) | `.bsp`/`.p2c` parse, once | parse once per map (offline) |
| **Runtime A\*** ([astar](astar_routing_design.md)) | which corridor *right now* | live hull traces | µs/cell, per blocked `go_to` |
| **Runtime telemetry** (snapshotter) | current per-entity status | live entity walk | per tick |
| **C9 save/restore** ([C9](llm_percept_act_phased_plan.md#L182)) | config-space search (realized effects) | engine save/load | ~4.3 s/node (puzzle layer only) |

---

## 3. Open decision-forks (confirm before building)

| # | Decision | Options | Recommendation | Tradeoff |
|---|---|---|---|---|
| 1 | Parser | **srctools** / bsp_tool / Rust `vbsp` | **srctools** | Only one that hands `entity.outputs_` as parsed `Output` objects (the causal-graph payload) + reads `%noportal` VMTs from `BSP.pakfile`. bsp_tool = raw-lump cross-check only; you'd re-parse output strings yourself. |
| 2 | Source artifact | **prefer `.p2c`/`.vmf` if present, else `.bsp`** | **bsp-primary** | We have **only** loose `.bsp` v21 for the eval/training chambers (`codebase:map-assets-on-disk`); no user `.p2c`/`.vmf` exist. `.p2c` is a literal voxel-grid + I/O graph (ideal) but only for chambers *you author*. Build bsp-first; treat p2c as a future fast-path for hand-made chambers. |
| 3 | Where the sidecar plugs in | annotation (Track A) / percept (Python) / **both, Python-first** | **Python-first** | A pure-Python prior consumed by the percept formatter is zero-C++-risk and ships independently; piping GT labels back into `PuzzleAnnotate` (panels/stairs by targetname) is a later C++ phase once the graph is trusted. |
| 4 | Graph edge semantics | guaranteed / **possible-effects (over-approx)** | **Over-approx + flag** | VScript / runtime `AddOutput` / branch logic are statically invisible; a superset ("button *could* affect these") is exactly what a planner/percept wants. **Flag any map with `vscripts`/`.nut` in pakfile as low-confidence.** |
| 5 | Static geometry grid? | seed A\*'s lazy grid / standalone advisory / **park it** | **Park (out of v0)** | A\* already has ground-truth, dynamics-aware geometry for free; a static grid can't see the dynamic cube-pocket A\* exists to fix, and is a permanent sync liability. Build only if A\*'s trace budget profiles hot. If ever built: advisory only — **never override a live probe.** |
| 6 | Map keying | by name / **by BSP CRC** / by workshop id | **CRC-addressed, name-indexed** | `testchamber_000` is a runtime *alias* with no BSP on disk — you preprocess the workshop BSP it aliases (`codebase:map-assets-on-disk`). CRC keying survives the alias + invalidates on recompile; a name→CRC index handles the lookup. |
| 7 | Who consumes the causal graph | **experimenter-only (GT / eval / ablation)** / model-facing percept | **Experimenter-first; model-facing only as a dialled ablation** | Showing the model the wiring may *solve the puzzle for it* and confound the reasoning eval (`llm_percept_act_grammar.md` §5). Ground-truth / annotation / hint-ablation use is pure upside; a raw percept is a measurement decision, not a default. |
| 8 | Surface/region "spatial vocabulary" (payload d) | offline region pipeline / **recon-then-cheap-label** / nothing | **Recon first (P0d); hand-authored facts-only label if the gap is real** | A\* waypoints are nameless — real gap. But offline regions are dead-for-PeTI + a watershed tar-pit that breaks on portal/fling connectivity, and a wrong graph poisons the eval. Check the model even needs it before building; surfaces wait for portal-reasoning. |

---

## 4. Phased plan — recon first, then Python-only, C++ last (all hand-verifiable)

Each phase the **user runs** (a Python spike against the real BSP / `macro_repl` on a live game),
not the agent. **v0 is ~200 LOC Python + one dep (`srctools`); zero C++, zero proto change** until
the optional annotation phase — and the static-geometry grid (P4) is *parked*, not built (fork #5).
Many small, the cheapest recon first. **The `P0*` recon stubs below are consolidated, in full, into the
§8 recon battery — run that first; the phases here (P1+) are the build, gated on §8 going green.**

- **P0** (~40 LOC Python, **the weekend recon — start here**) — `pip install srctools`; open the
  training BSP `…/maps/workshop/1858300862251329775/1644417521.bsp`; iterate `BSP.ents`, dump every
  entity's `classname`/`targetname`/`outputs_`. *Verify:* the printed I/O edges match a chamber you
  hand-trace in-game (button's `OnPressed` points at the door/relay you expect). **This is the money
  test** — if the graph is right here, the whole layer is real; if VScript/`AddOutput` hides the
  wiring, you learn it in one evening for free.
- **P0b** (~10 LOC, same spike) — check the BSP for `vscripts` keyvalues + `.nut` files in
  `BSP.pakfile`. *Verify:* the training/eval chambers are PeTI-vanilla (no VScript) → static graph is
  complete, not just a skeleton. Settles fork #4's confidence for *our* corpus.
- **P0c** (~5 LOC, recon-only, **closes a standing A\* assumption**) — on a live game, run
  `nav_generate` in a PeTI chamber (`sv_cheats` already forced on). *Verify:* does P2 produce a
  `.nav`, or no-op? Locomotion currently *asserts* navmesh-absent from the entity list, never via
  `nav_generate` (`diverge_catalog.md` §B1 — "do this first"). Either result is decisive: a usable
  `.nav` could collapse part of (a); confirmed-absent hardens the A\* doc's premise.
- **P0d** (~0 LOC, **the recon that gates payload (d)** — the spatial-vocabulary idea) — on a chamber
  where a cube blocks the left route, feed the frozen model the *existing* percept (annotated frame +
  entity list, no region graph) and watch whether it routes right. *Verify:* if it already does, the
  region/surface layer solves a **non-problem** — ship nothing. If it doesn't, re-run *once* with a
  hand-authored facts-only region label (5 min of JSON, no BSP) and see if *that* gap is what was
  missing. Two prompts settle whether the layer earns its keep, for zero new subsystems — do this
  before writing any region code.
- **P1** (~60 LOC) — turn the P0 dump into the **causal graph**: resolve targetnames (already
  fixup-baked in the compiled BSP), transitively chase `logic_relay`/`logic_branch`/`logic_auto`,
  emit `{nodes, edges, conditional_flags}`. *Verify:* `button → relay → door.Open` collapses to one
  chain; a `logic_branch`-gated edge is present but flagged conditional.
- **P2** (~50 LOC) — the **affordance prior**: per movement entity, emit its typed parameterized
  envelope (catapult arc, movelinear distance, funnel rail, projector ray). *Verify:* a faith plate's
  computed arc apex lands where the player actually flies (eyeball in-game).
- **P3** (~40 LOC) — **portalable-surface map**: texinfo→material → `%noportal` VMT lookup (from
  `BSP.pakfile` or game VPKs) + `func_noportal_volume` brushes; flag flip-panel faces conditional.
  *Verify:* white walls in a chamber read portalable, black walls don't.
- **P4** (~50 LOC, **parked — fork #5**) — the static nav grid (brush ∩-of-half-spaces → occupancy +
  floorZ on the 32u lattice). *Not in v0:* dominated by runtime A\*, blind to the dynamic cube-pocket.
  Listed only so the seam is documented; build solely if A\*'s trace budget ever profiles hot.
- **P5** (~30 LOC) — assemble `<mapname>.mapinfo.json` (CRC-keyed) + a tiny loader in the percept
  formatter that maps the graph's entity origins → live **marks** (deterministic index/origin order
  makes this key cleanly — `codebase:puzzle-causal-layer-roadmap`). *Verify:* the percept can now
  say "mark 3 (button) → mark 7 (door)" and the marks match the live annotation labels.
- **P6** (optional, C++, ~30 LOC) — feed GT labels the live walk drops (panels/stairs by targetname,
  via the sidecar) back into `PuzzleAnnotate`. *Verify:* a folding-stair panel gets a box + label it
  never had before. **Only after the graph is trusted (P0–P5 green).**

**Biggest risks:** (1) **VScript / runtime `AddOutput` is the hard ceiling** — statically invisible;
P0b's flag is the only honest mitigation, and a VScript-heavy chamber silently yields an incomplete
graph. (2) **Static-prop sprp version is unreliable** — infer layout from per-prop byte size, not
the version field (the #1 ad-hoc-parser killer); srctools handles it, *don't* roll your own. (3)
**Portalability needs a VMT pass, not a lump read** — material-name → VMT `%noportal` is a second
hop through the pakfile/VPKs; budget for it (P3 is its own phase for this reason). (4) **A static
offline grid goes stale the instant the world changes** (moved cube, opened door) — which is *why*
the geometry grid is parked (fork #5); if ever un-parked it stays advisory and **must never override
a live A\* trace.** (5) **`testchamber_000` has no BSP on disk** — you preprocess the workshop BSP it aliases;
get the alias→file mapping right (CRC-key it). (6) **Branch/counter edges are possible-not-realized**
— the graph is a sound over-approximation; a consumer that treats an edge as guaranteed will
mis-plan. (7) **per-map pipeline cost** — every new chamber needs a parse pass; cheap (seconds, fully
offline) but it's a build step someone has to remember to run on map churn (CRC-invalidation makes it
self-triggering if wired into `reset`). (8) **The causal graph can confound the eval** — feeding the
wiring to the model risks solving the puzzle for it; keep it experimenter-side by default and expose
it only as a dialled hint-ablation (fork #7). This is a *measurement* footgun, not a parser bug, and
the easiest one to trip without noticing.

---

## 5. The "risqué" alternative — offline entity-lump *injection* (§X6) → rejected for v0

`exit_detection_brainstorm.md` §X6 proposes the *write* sibling: patch a SAR-recognizable output
into the BSP entity lump offline so the engine fires it at runtime. Genuinely robust for the
*exit-detection* use case (rated Robustness 4) — but **out of scope here and rejected for v0**:

- **It mutates ship artifacts.** Rewriting the entity lump risks CRC/signing rejection on load (an
  open recon Q in §X6 itself) and means every chamber needs a *modified* BSP on the engine's search
  path — operational liability for a *read-only* preprocessing goal.
- **We don't need to write to read.** The causal graph, affordances, and GT labels are all
  *recoverable by parsing* — no engine round-trip, no mutated map. The exit oracle is already shipped
  + parked (`PuzzleExit`, P1–P4 verified); this layer doesn't need to re-solve completion detection.

Keep X6 filed where it is (an exit-detection fallback). This doc's tool is **read-only**; if a
chamber's wiring ever genuinely needs injection, that's a separate, scoped decision.

---

## 6. Recommendation

**Run P0 this weekend; build the rest only if the graph matches a hand-trace.** The defaults:
**srctools** (fork #1), **bsp-primary** (#2, we have no `.p2c`/`.vmf`), **Python-first consumer**
(#3), **over-approx + VScript-flag** graph (#4), **static geometry grid parked** (#5, A\* dominates),
**CRC-keyed/name-indexed** (#6), **experimenter-first — model-facing only as a dialled ablation**
(#7), **spatial-vocabulary recon-gated, BSP regions not built** (#8). The layer's unique, load-bearing
buy is the **I/O causal graph** — the button→door wiring L2 defers and the model otherwise guesses —
with the affordance prior + GT annotation labels as the secondary wins. The **surface/region
"left-vs-right" idea (payload d) is a separate, recon-first percept question** — run P0d (does the
model already route right from pixels + entities?) before writing any region code; the offline-BSP
part of it (surfaces) waits for a portal-reasoning feature that needs it. **Annotation gains** (panels/stairs the classname-walk drops; "mark 3 → mark 7"
wiring on labels); **hdem/rollout gain ≈ nothing** — those are *runtime* captures, so offline can
label entities in post-processing but doesn't change the recorder. It **sidesteps both cost
centers**: no per-node 4.3 s warmup (vs C9), no runtime trace budget / "blind to causality"
concession (vs A\*) — it's a *static prior*, computed once, that the always-correct runtime layers
instantiate. The literal next step is **P0**: `pip install srctools`, dump `BSP.ents` on the training
map, and check the printed I/O edges against a chamber you trace by hand.

---

## 7. Capstone (forward-looking, NOT a priority) — the causal graph as a brute-force solver's prior

The maximal thing this layer unlocks, recorded so it isn't lost: an **in-engine brute-force chamber
solver**. State = a full engine `save`; actions = the verb grammar (`go_to`/`press`/portal-fire — each
a macro bundling thousands of ticks into one transition); goal-test = the exit oracle fires; backtrack =
engine `load` (C9 `anchor`/`restore`). The engine *is* the transition oracle — standard **black-box /
savestate planning** (the ALE line): `s' = sim(s, a)`, no declarative model, just "what happens if I do
`a`." This **unifies all three layers**: the offline **causal graph + affordance prior prune the search**
(goal-regression — only expand verbs whose effects lie on the causal path to the exit; the annotation
atoms `button_pressed`/`cube_on_button`/`door_open` are the **novelty features for IW(1)/width pruning**
*and* the state-hash that makes the continuous engine state revisit-detectable); **A\* `go_to` is the
reliable move primitive**; **C9 save/load is the backtrack**.

**Its honest role — experimenter-side, never a runtime agent** (same discipline as fork #7): a **non-LLM
reference solver / solvability + difficulty oracle**. Per chamber, offline: a *solvability certificate*
(ground-truth a solution exists, so a model's miss is unambiguously a reasoning failure, not a broken
chamber) and a *difficulty label* (search depth to first solution = a principled stratification axis for
the eval set). It only really pays off if **automated eval-chamber generation** ever lands — a human can
verify a hand-authored chamber by playing it.

**Two hard walls keep it a capstone, not a tool:** (1) **PSPACE-complete** — the cube/weighted-button/door
fragment (the canonical core-Portal vocabulary, and exactly the v0 PeTI scope) is PSPACE-complete; lasers
+relays and funnels+cubes+buttons independently also reach it; turrets/timed-doors/HEP are NP-*hard*;
movement-only is in P; **portals-alone is open** (Demaine, Lockhart & Lynch, *The Computational Complexity
of Portal and Other 3D Video Games*, FUN 2018 / arXiv:1611.10319 — note: PSPACE-complete, *not* the
folklore "NP-complete," which understates it). (2) the **~4.3 s/node** save-load `SESSION_START` warmup
([astar_routing_design.md](astar_routing_design.md) §5) → seconds/node → **shallow chambers only**. So:
record it, don't build it — a [ROADMAP](ROADMAP.md) "someday" line + the C9 pointer. De-risk first via the
two cheap save/load checks (cost + cube/portal fidelity) — now consolidated in the §8 battery (R5).

---

## 8. Recon battery — run this FIRST (the whole BSP feasibility gate)

Consolidates **every** recon the BSP layers (payloads b/c/d) and the §7 solver depend on, into one ordered
battery a fresh session can execute top-to-bottom. Principle: *cheapest offline checks before any build;
each item gates a specific payload and has a hard pass/fail.* The `P0*` stubs in §4 and the save/load
checks at astar §5 are the scattered summaries — **this is the single source of truth.** Three environment
tiers (do all of a tier's offline work before booting the game):

- **Tier 0 — offline Python, no game** (parse `.bsp` files; runs anywhere). Most of the battery.
- **Tier 1 — one live game instance** (`macro_repl`; *you* launch it, per the no-long-runs rule).
- **Tier 2 — live game + frozen model** (the eval loop).

**What the battery outputs:** a **go/no-go per payload**, a corpus catalog (which maps parse; which are
VScript-tainted), the name→CRC→file index, and the real save/load cost+fidelity numbers. Nothing here
builds — it decides *what's worth building*.

### R1 — Parser & corpus sanity (Tier 0) — *gates everything BSP*
- **R1.1 install.** `uv add srctools`; confirm it imports under `requires-python >=3.14` (srctools targets
  3.8+, but verify on 3.14 — if it breaks, `bsp_tool` is the raw-lump fallback). *Pass:* `from srctools.bsp
  import BSP` works in the uv env.
- **R1.2 load the corpus.** Iterate the in-scope maps — the training BSP
  `…/maps/workshop/1858300862251329775/1644417521.bsp`, the eval chamber, + whatever workshop/stock set you
  scope (R1.3). `BSP(path)`; confirm no parse crash; log header version (expect **21**) + any LZMA-lump or
  sprp-version warnings. *Pass:* every in-scope map loads; catalog any that don't.
- **R1.3 resolve the alias + key by CRC.** `testchamber_000` has **no BSP on disk** — find what file the live
  engine actually loads it as (it aliases the workshop chamber). Compute + record each map's CRC. *Produces:*
  the name→CRC→file index the sidecar keys on. *Your decision:* corpus scope = training+eval only, or the
  whole ~277-map workshop set?

### R2 — Causal graph (Tier 0) — *gates payload (b), the headline buy*
- **R2.1 dump outputs — THE money test.** `for e in bsp.ents: for o in e.outputs_: print(e['classname'],
  e['targetname'], o.output_, o.target_, o.input_, o.delay_)` on the training BSP. Hand-trace one chamber
  in-game (which button → which door). *Pass:* printed edges match the hand-trace.
- **R2.2 instance-fixup.** Confirm targetnames are concrete (no `$`/instance placeholders) and every output
  target resolves to a real entity — i.e. VBSP baked the fixups. *Pass:* targets match real targetnames
  (modulo `!activator`/wildcards).
- **R2.3 transitive chase.** On a chamber with indirection, follow `button → logic_relay / logic_branch /
  logic_auto / math_counter → door`. *Pass:* the chain collapses; branch/counter-gated edges are *flagged
  conditional*, not dropped.
- **R2.4 VScript ceiling (the hard limit).** Grep each pakfile for `.nut`; scan entities for
  `vscripts`/`logic_script` keyvalues. Tally the VScript-touched fraction. *Pass for us:* the training + eval
  chambers are PeTI-vanilla (no VScript) → complete graph, not a skeleton. *Produces:* the low-confidence map list.
- **R2.5 element-scope coverage.** Histogram distinct classnames + output names across the corpus → the exact
  set the parser must handle; reconcile against [puzzlemaker_elements.md](puzzlemaker_elements.md) v0 scope.
  *Produces:* the "known elements" table; flags out-of-scope (BEEmod/custom → P1).

### R3 — Affordance prior (Tier 0 + one Tier-1 eyeball) — *gates payload (c)*
- **R3.1 portalability.** texinfo → material name → VMT `%noportal`. Check whether the VMTs live in
  `BSP.pakfile` (srctools reads it) or the game VPKs (needs a VPK reader — extra dep, flag it). *Pass:* white
  walls read portalable, black don't, on a known chamber (eyeball in-game, Tier 1).
- **R3.2 movement envelopes.** Dump `func_movelinear`/`func_door`/`prop_door_rotating`/`trigger_catapult`/
  `prop_tractor_beam` keyvalues on chambers that have them; confirm move-dir/distance/speed present. For
  `func_door`, confirm brush bounds via `BSP.bmodels` (travel = size-along-axis − `lip`). *Pass:* a faith-plate
  arc / a platform rise computes from keyvalues alone.

### R4 — Surfaces / regions (Tier 0 surfaces; Tier 2 the real gate) — *gates payload (d)*
- **R4.1 areaportals dead? (1-line).** `len(bsp.areas) <= 1` + empty areaportals lump across the corpus →
  confirms the BSP-native room graph is moot for PeTI (any region graph is a custom watershed, not a free
  lump). *Pass:* areas ≤ 1 as predicted.
- **R4.2 surface→panel IDs.** faces → coplanar+material clustering; confirm PeTI faces are panel-sized on the
  128u grid; test geometry-hash stability (parse the same map twice → identical IDs; if you can recompile,
  confirm the hash survives face-reorder). *Pass:* stable per-panel IDs.
- **R4.3 P0d — the percept A/B (Tier 2, the DECISION gate).** *Not* a BSP check — it decides whether payload
  (d) is worth **any** build. On a cube-blocks-left chamber, feed the frozen model the *existing* percept
  (annotated frame + entity list, no region graph) → does it route right? Then once more with a hand-authored
  facts-only region label. *Pass→ship nothing* if it already routes right; *fail→* the label is justified.
  Run this **before** any surface/region code.

### R5 — Solver de-risk (Tier 1 + Tier 2) — *gates the §7 capstone*
- **R5.1 save/load cost.** Time one `save`+`load` round-trip *until `harnessControlActive` flips true* (through
  the full 256-tick warmup) → hardens the ~4.3 s/node number. *Produces:* the real per-node cost.
- **R5.2 save/load fidelity (currently ASSUMED, untested — load-bearing).** Carry a cube + place both portals;
  `save`; perturb (move/drop/re-portal); `load`; assert the cube is still held and both portals are back.
  *Pass:* full state survives. **If this fails, tree-search-over-moves is dead** until a real in-memory
  savestate exists — so run it early.
- **R5.3 state-hash readout.** Confirm the annotation atoms (`button_pressed`/`cube_on_button`/`door_open`/
  `catcher_powered`) are reliably readable at runtime (cross-ref [status_field_recon.md](status_field_recon.md))
  — they're the solver's state-hash *and* its IW(1) novelty features. *Pass:* the atom set is complete + stable per tick.

### Out of scope of this battery (tracked elsewhere)
- **Clearance recon (P2.5b)** — the runtime "pinched-corridor counter" — needs A\* *shipped* first; lives at
  [astar_routing_design.md](astar_routing_design.md) §2 + fork #7. Not a BSP check.
- **`nav_generate`** (does P2 emit a navmesh) — a Tier-1 one-liner (§4 P0c), orthogonal to BSP parsing; fold
  it into the same live-game session as R3.1/R5.
