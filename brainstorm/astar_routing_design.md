# A\* global routing for `go_to` (P2) — design + the save/restore detour

Design record for the **global routing layer** that makes `go_to` *complete* (route around
concave pockets instead of oscillating), plus the evaluation of the "save/restore tree-search"
alternative. Fleshes out [locomotion_tech.md](locomotion_tech.md) §4 **P2** and §3 (global
planner + local controller compose). Read those first. Written 2026-06-20.

> Status: **design agreed in shape, decision-forks open** (§3). Not yet built. The cube-shove
> guard (P-VFH.2/.3) shipped as `79637dd0`; this is the next layer on top of it.

---

## 1. The problem (why VFH alone isn't enough)

`go_to` uses a greedy 360° VFH local controller. A cube-on-a-button **flush against a wall**
forms a *concave pocket*, and greedy "freest valley nearest goal" has no escape — it oscillates
in front of the mouth and never rounds it. Live repro (debug log, the chamber from this session):

```
goto t=0  dist=193 best=193 hdg=+131 clr=46   ← goal-ward blocked (cube + wall)
goto t=8  dist=203 best=193 hdg=-151 clr=96   ← only open valley points backward
goto t=20 dist=239 best=193 hdg=+118 clr=42   ← tries goal-ward, blocked, …
                                              ← best frozen, dist bounces, → BLOCKED → retry
```

The obstacle injection works (clearance collapses on the cube's bearings — it is **not** shoved);
the gap is purely **global routing**: greedy VFH provably can't escape a pocket. This is the
completeness layer the roadmap parked as *"deep concave-pocket escape → P1.2 Bug2 / A\* (P2)."*
A real chamber defeated VFH → the trigger to build P2 is met. We choose **A\*** (not Bug2):
Bug2 has its own local-min failure modes; A\* gives completeness over the explored frontier.

---

## 2. Recommended design — A\* over a lazy hull-probed grid

A\* sits **above** the shipped VFH executor as a *"which corridor"* oracle and is invisible to it.
The VFH march (`ChooseVfhHeading` + `InjectObstacles` + `CheckEdge` + wedge + body-frame strafe)
is the local layer and is touched **only by extract-method, never rewritten**.

**Control flow** (matches [locomotion_tech.md](locomotion_tech.md) §3):
1. `GoTo` resolves the mark (unchanged). Run the existing VFH march straight at the real target —
   the **fast path**; most `go_to`s are line-of-sight and spend *zero* grid traces.
2. Only if that march returns `BLOCKED` (global-stall fired in a pocket) →
   `PlanPath(feet, target, hull, targetKey, heldKey) → vector<Vector>` waypoints.
3. March legs: for each waypoint but the last, retarget the **same** march loop with a looser
   arrival radius (`kLegRadius ≈ 1 cell`); the final leg uses the real mark + `kReachRadius`.
   Camera stays on the **final** target (body-frame strafe decouples view from move dir).
4. A leg `BLOCKED` (dynamic block — door shut, cube moved into the corridor) → re-plan **once**
   from current feet; cap total replans at 2; then `BLOCKED`.
5. A\* exhausts / hits the expansion cap → empty path → `BLOCKED` (now with an honest
   "no route exists" meaning). No proto change.

**The only structural refactor** — extract the per-batch march for-loop into
`MarchTo(target, reachRadius, tickBudget, &feet, &dist) → code`. `GoTo` becomes: try
`MarchTo(realTarget, kReachRadius, full)`; if `BLOCKED`, plan; per waypoint `MarchTo(wp,
kLegRadius, remaining)`. All per-leg state (wedge ttl, `bestDist`, `lastYaw`, `stallBatches`) is
created **fresh per `MarchTo`** (correct — no cross-leg stall bleed); `kGoToMaxTicks`(400) is a
budget decremented across legs so a long path can't run unbounded.

**Geometry oracle** — re-add the reverted `Engine::TraceHull(start, end, mins, maxs, mask,
filter, tr)`: `Ray_t` with `m_StartOffset=(mins+maxs)/2`, `m_Extents=(maxs-mins)/2`,
`m_IsRay=false`. Blocked `= fraction<1 || startsolid || allsolid`. **Reading `startsolid`/
`allsolid` is load-bearing** — a hull flush inside a brush returns `fraction 1.0` with a garbage
normal; reading `fraction` alone is the named P-VFH.4 pitfall. Point-ray `Engine::Trace` stays
for `RayClearance`/`CheckEdge`. Player hull captured once per `go_to` from
`GetPlayer(1)->collision().OBBMins()/OBBMaxs()`.

**Grid** — value-type in a new `src/Features/Harness/GoToPlanner.{hpp,cpp}`, anchored to the
world voxel lattice. `Cell{ uint8 state; float floorZ; }`, `state ∈ {UNKNOWN, WALKABLE, BLOCKED}`,
keyed `cx<<16|cy` in an `unordered_map`. **Lazy**: A\* probes a cell only when popped as a
neighbour, then caches — only the explored frontier is traced, no reset-time sweep. `floorZ` is
stored now (the 2.5D-ready seam — costs 4 bytes + zero extra traces; M3 multi-level becomes "add a
step-height check to `Passable`", not a grid rewrite); connectivity stays 2D.
- `WalkableCell`: a down point-ray (the `CheckEdge` idiom) finds the floor / records `floorZ`
  (no floor → `BLOCKED`, a pit), then a zero-length player-hull test at `floorZ+lift`
  (`startsolid||allsolid` → `BLOCKED`). 1 point-ray + 1 hull-test per cell.
- `Passable(a,b)`: a hull **sweep** between cell centers (the test that makes diagonals honest —
  a wall lip refuses the hull even when both ends are walkable); diagonals also require both
  shared orthogonal cells walkable (no corner-cut through a brush corner).

**A\*** — textbook 8-connected, octile heuristic (admissible+consistent → optimal, no
re-expansion), `h*=1.001` tie-break to cut frontier fan-out, hard `kPlanMaxCells`(~400)
expansion cap → `NO_PATH` on overflow (the EntitySnapshotter "bound the worst case" lesson).
Start = feet cell (accepted even if it probes `BLOCKED` — you're standing there). Goal =
mark-center cell; if `BLOCKED` (target is itself a solid cube/button), spiral-snap to the nearest
walkable neighbour; the final VFH leg + `kReachRadius` close the gap.

**Obstacle stamp** — fold the `InjectObstacles` enumeration into `WalkableCell`: a cell is also
`BLOCKED` if its **center** lies inside any `IsGoToObstacleClass` prop's footprint-circle (skip
target + held key, same conservative stance as `InjectObstacles`). This is the *only* thing that
fixes the motivating pocket — world-only A\* would route straight back into it. ~10 LOC, zero
extra traces. VFH still owns "don't shove" via `InjectObstacles`, unchanged.

**Clearance cost-term** (*optional, prefer roomier routes — the "left-vs-right of an obstacle" gap*).
As specced, A\* picks on pure octile length, so given a 33u gap and a roomier 200u detour one cell
longer it **takes the gap every time** — and VFH's `RayClearance` is *local + per-heading* (it steers
the chosen corridor, it can't choose *between* corridors). To make A\* prefer the roomy side and give
a moved cube a *wide berth*, add a **clearance penalty to `g`** — not to the admissible `h` (clearance
is no goal-distance bound, so it belongs in cost, where A\* stays optimal under the *reweighted* cost):
`tentative_g = g + octile_step + β·penalty(nbr.clearance)`, `penalty = max(0, kSafeDist − clearance)`
(zero once roomy). The **cube-berth is free**: the obstacle stamp above already marks the cube
`BLOCKED` in the same `state` field, so "distance to nearest `BLOCKED`" repels from cubes and walls
uniformly — no cube-specific code. **Field source = a runtime *truncated/local* distance-transform,
NOT a global one and NOT BSP:** the grid is lazy (frontier-only), so a global DT would force eager
full-grid probing and kill the zero-trace fast path — instead compute clearance *at probe time* from
the analytic `dist−radius` to nearest stamped prop (reuse `InjectObstacles`, zero traces — nails the
cube case) plus a small bounded ring of wall probes; you only need to *deter tight cells*, not rank
roomy ones. BSP-exact (sub-cell) clearance is **dominated**: at 32u/36u-hull it rarely flips the
route, and **halving the cell 32u→16u is the cheaper precision lever** than threading wall-plane
distances (every risk-aware planner in the literature runs on occupancy grids, not CAD geometry).
**The one real tradeoff:** any `β>0` makes `h=1.001·octile` no longer a lower bound on the new cost →
you lose "optimal, no re-expansion." Keep `β·penalty` below one octile step (tie-break shaping, near-
optimal) or knowingly accept re-expansions (the cap still bounds the search). Carry `float clearance`
on `Cell` — the same 4-byte speculative move already made for `floorZ`. **Recon-gate (before building):**
once A\* ships, log how often it commits to a pinched corridor (min route clearance < 1–2 cells) *while
a within-budget roomier alternative existed* — rare ⇒ record-only; common ⇒ build it.

**Main-thread model** — the whole A\* burst runs in **one** `RunOnMainThreadSync` closure (it's
pure CPU over cached cells; its traces are synchronous engine reads), once per blocked `go_to`,
between tick batches — exactly where the existing per-batch closure runs. Rejected: a
gRPC-thread-expand + batched-probe-queue with a resumable open-list (speculative perf machinery
for a non-problem). **Measure** worst-case traces with `sar_harness_goto_debug` before trusting
the cap; chunk only if a real chamber hitches a tick.

---

## 3. Open decision-forks (confirm before building)

| # | Decision | Options | Recommendation | Tradeoff |
|---|---|---|---|---|
| 1 | Cell size | 16u / **32u** / 48u | **32u** | ≈ player-hull width → cell-walkable ⇒ hull-fits; exact 128/4 divisor. 16u = 4× traces, no gain. 48u aliases tight gaps. |
| 2 | A\* stamps cube/button footprints? | **stamp (this-plan-only)** / world-only | **Stamp** | Only thing that fixes the pocket; reuses `IsGoToObstacleClass` (~10 LOC). Cost: obstacle set lives in 2 places (small blur). |
| 3 | Plan-once vs replan-on-blocked-leg | once / **replan-once cap 2** | **Replan-once** | Handles dynamic block (door/cube) for ~5 LOC; grid warm so cheap. Cap guarantees termination. |
| 4 | Connectivity | 4-conn / **8-conn** | **8-conn (octile)** | Short paths, fewer legs (PeTI corners need diagonals); cost = corner-cut guard ~4 LOC. |
| 5 | Surface path in `MacroResult`? | **no proto change v0** / add `repeated Vec3 path` | **No proto change** | Condump verifies; adding the path is a later visualizer nicety (doc's P2.3 optional). |
| 6 | A\* execution | **one burst** / gRPC-expand + probe-queue | **One burst, measure, chunk if needed** | Matches the codebase; the split is liability for speculative perf. |
| 7 | Clearance cost-term? | none / **runtime-local penalty in g** / BSP-exact | **Runtime-local, recon-gated** | Makes A\* prefer roomy routes + berth cubes (the global "which corridor" choice VFH-local clearance can't make). Field = truncated local DT at probe time (cube-berth free off the stamp); drop global-DT (eager probing) + BSP (finer grid wins). Tradeoff: penalty in `g` breaks `h=1.001` optimality — keep small or accept re-expansions. |

---

## 4. Phased plan (many small, hand-verifiable; C++ before Python)

Each verified by hand via `py/macro_repl.py` on a live game (the user runs the game). **~270 LOC
C++ + ~30 Python**, one new file pair, no proto change.

- **P2.0** (~30 LOC, **zero behaviour change**) — extract the march loop into `MarchTo`; `GoTo`
  calls it once. *Verify:* every existing `go_to` (line-of-sight, side-door, around-a-cube, the
  rim repro) behaves **exactly** as before. **This is the safety net for everything after.**
- **P2.1** (~14 LOC) — `Engine::TraceHull`, blocked `= fraction<1||startsolid||allsolid`. No
  caller. *Verify:* a throwaway `sar_harness_probe_hull` condump — flush-wall reads `startsolid`,
  open floor clear, a 64u doorway reads blocked for a 36u hull.
- **P2.2** (~70 LOC, new `GoToPlanner.{hpp,cpp}`) — cell math + `Cell` + grid + `WalkableCell`
  (floor z + zero-length hull test). No A\*, no stamp. *Verify:* probe a row across a floor/wall
  boundary → matches visible geometry.
- **P2.3** (~15 LOC) — `Passable(a,b)` hull-sweep + corner-cut guard. *Verify:* refused through a
  sub-hull gap, passable down a real corridor.
- **P2.4** (~70 LOC) — the 8-connected A\* (octile, cap, start-accept, goal spiral-snap) →
  `vector<Vector>`. Not wired in. *Verify:* `go_to_plan <mark>` condumps a path *around* a
  hand-placed pocket; expansion count well under the cap.
- **P2.5** (~10 LOC) — fold the obstacle-prop stamp into `WalkableCell`. *Verify:* the plan now
  bends around the cube footprint.
- **P2.5b** (optional, ~20–30 LOC, **recon-gated — fork #7**) — `float clearance` on `Cell`, populated
  lazily at probe time (analytic `dist−radius` to stamped props + bounded wall ring), charged as
  `+β·penalty(clearance)` in g. *Verify:* the plan now **bows away** from the cube/wall, not just
  *around* it; a tuned `β` takes the roomy detour over a tight gap. *Gate:* build only if the §2
  pinched-corridor recon shows it's common. (BSP-exact clearance: a later precision drop-in for the
  same field, only if 32u quantization visibly mis-plans **and** the causal-graph BSP pipeline exists.)
- **P2.6** (~30 LOC) — wire into `GoTo` (plan-on-blocked, leg marching, replan-once, decremented
  budget). *Verify — **the money test**:* the `79637dd0` cube-pocket repro now rounds the pocket
  and reaches the mark; `on_button` stays True; a line-of-sight `go_to` still takes the fast path.
- **P2.7** (optional, ~20 LOC) — path simplification (drop waypoints whose skip is LoS-clear) so
  VFH gets a few long legs, not many 32u hops.
- **P2.8** (Python, ~30 LOC) — `macro_grammar.py` `go_to` doc rewrite ("routes around obstacles;
  `BLOCKED` only if no path exists") + `agentloop_smoke` assertion. No `make proto`.

**Biggest risks** (from the review): (1) `startsolid`/`allsolid` must be read on *every* probe
— the sharp edge. (2) `TraceHull` box-center math is easy to get subtly off → `Passable` too
permissive/strict; P2.1 probe against known-width gaps is the gate. (3) the `MarchTo` extract is
where a silent regression hides (the captured per-batch state is fiddly) — ship P2.0 verified
first. (4) over-stamping walls off a legit gap → spurious `NO_PATH` (stamp by cell-center only).
(5) goal spiral-snap can pick the wrong side of a thin wall (acceptable v0, log it). (6) the
expansion cap is load-bearing for the trace budget — **measure** before trusting it. (7) doors:
a door opening mid-march to reveal a shorter route is only caught via the one replan (v0 limit).

---

## 5. The "risqué" alternative — save/restore tree-search → **parked at the puzzle layer**

The idea: skip geometry, search **configuration space** by trying moves in the real simulator and
backtracking via the game's `save`/`load`. The engine is the oracle; handles *any* dynamics for
free. Genuinely elegant — but evaluated and **hard-no for locomotion**.

**Why not (the deciding numbers, all code-confirmed):**
- The only full-world snapshot is **Source disk `save`/`load`** (real save files here: median
  1.08 MB, max 1.77 MB, ~650 entities). `Teleporter::SaveLocal` is **player-position-only** — it
  *cannot* restore the cube on the button, which is the whole problem. No cheap in-memory
  savestate exists.
- **The killer:** every `load` fires `SESSION_START`, which unconditionally sets
  `harnessControlActive=false` + `warmupTicksRemaining=256`, then runs the game free for **256
  ticks (~4.3 s)** before re-pausing ([Harness.cpp:248-291](../src/Features/Harness/Harness.cpp#L248)).
  **Every restore is structurally a full episode Reset, not a cheap rewind** — disk I/O + ~650-entity
  reconstruct + ~4.3 s warmup = *seconds* per node, vs a hull trace at *µs* (5-6 orders of
  magnitude). One `go_to` is ~6 s of budget; one restore warmup ≈ 64% of it. A non-starter inside
  the inner loop.

**This ties back to the harness-robustness point** (the audit that motivated this section): a
mid-episode `load` desyncs harness-side caches — `g_heldEntityKey`, the `Observe`
delta-compression baseline (`observeLast*`), tick-sync atomics, the mark table. The *good* news is
`SESSION_START` already self-heals most of them (it clears `g_heldEntityKey` per the `79637dd0`
fix, rebuilds marks, re-establishes control). The *bad* news is **that very teardown is the
prohibitive cost** — robustness and the 256-tick warmup are the same coin. And it's an ongoing
tax: every future harness cache (laser/door/puzzle state) would have to be `SESSION_START`-resilient.
A\* needs none of this. (One gap to fix if save/restore is ever used: the `Observe` baseline needs
a forced full-snapshot after a same-stream load, or it ships garbage deltas.)

**Where it *does* belong — C9.** This is already scoped as
[**C9 — Save/load anchors (`anchor`/`restore`)**](llm_percept_act_phased_plan.md#L182), *"the
substrate for tree-search-over-moves."* Its real value is exactly where A\* is blind: **puzzle-level
action sequencing** (press button → door opens → path changes; cube-carry ordering; portal/laser
counterfactuals) in **turn-based eval**, where tens of restores *per puzzle* (not per path-step) is
affordable because the model pauses to think anyway. A\* and save/restore **complement**: A\* for
geometry/locomotion, save/restore for puzzle search.

> **Correction to C9:** its cost note says *"~100s of ms"* per restore — that **omits the 256-tick
> (~4.3 s) warmup** every `load` triggers. Real restore cost is *seconds*. C9 stays viable at
> turn/macro granularity, not as a tight search substrate. (Pointer added at C9.)

**De-risk before depending on C9** (two cheap `macro_repl` checks): (1) **cost** — time one
`save`+`load` round-trip wall-clock *until `harnessControlActive` flips back true* (through the full
warmup), to harden the seconds-per-restore number; (2) **fidelity** (currently *assumed, untested* —
flagged in [status_field_recon.md](status_field_recon.md#L238)) — carry a cube + place both portals,
`save`, move/drop, `load`, assert the cube is still held and both portals survive. If anyone ever
wants save/load as a real *inner* search substrate, the prerequisite is a true **in-memory savestate
that bypasses `SESSION_START` + the warmup** — it does not exist today.

---

## 6. Recommendation

**Ship the A\* design (§2-§4) for the cube-pocket; park save/restore at C9 for puzzle search.**
The cube-pocket is a *geometry* problem and A\*'s µs hull traces beat save/load by 5-6 orders of
magnitude per node while never touching the harness warmup state machine. The save/restore instinct
is right — just one layer up. Confirm the §3 forks (32u / stamp / replan-once / 8-conn / no-proto /
one-burst are the defaults) and I'll start at **P2.0** (the zero-behaviour-change `MarchTo` extract).

---

## 7. Postscript — "is A\* the only method? what about parsing the map offline?" (2026-06-20)

Raised right after this doc landed: instead of A\* *percepting* geometry at runtime, parse the
chamber's `.bsp`/`.vmf` **offline** (BSP parsers exist — `srctools` reads the entity lump as parsed
`Output` objects) and reuse a precomputed map — ground-truth geometry, what a button wires to, what
a funnel/bridge enables, even geometry that *changes* (stairs rising on a button press). Deep-dived
in **[offline_map_preprocessing.md](offline_map_preprocessing.md)**; the short version, because it
re-frames where A\* sits rather than replacing it:

- **It does *not* replace A\* for locomotion.** A\* already gets ground-truth geometry from µs hull
  traces *for the current world state* — including the dynamic cube-on-a-button that forms the
  motivating pocket. A static offline grid is blind to exactly that dynamic obstacle, and adds a
  permanent offline↔runtime sync liability. For "which corridor," runtime A\* **strictly dominates**
  offline geometry. (Nav meshes are worse: Source ships none for P2, and they carry no
  jump/portal/fling edges — the "navmesh-absent" premise this whole plan rests on, finally worth
  *verifying* via `nav_generate`, see recon below.) So §2–§4 stand unchanged.
- **"But geometry gives left-vs-right!" — yes, but that's a *percept*, not a planner.** The follow-up
  instinct: knowing surfaces + regions lets the model reason "left route blocked by a cube → take
  right." The gap is real — A\*'s waypoints are *nameless*, so the model has no vocabulary for "region
  L," and A\* already handles the *actuation* (it reroutes around the cube for free) but says nothing.
  The fix is **not** an offline region pipeline, though: a clean BSP room graph is dead for PeTI (areas
  collapse to one), a custom watershed breaks on Portal's portal/fling/goo connectivity, and a *wrong*
  region graph poisons the eval. And "take right" is the **answer** — the same confound that gates the
  causal graph. Resolution: **recon first** (does the model already route right from the annotated
  frame + entity list?), then at most a *cheap, hand-authored, facts-only* region label in the Python
  percept — never as an A\* oracle. Full treatment: offline_map_preprocessing.md payload (d) + fork #8.
- **The offline instinct is right — one layer up, same as save/restore (§5).** What runtime
  perception is *structurally* blind to, and what an offline parse hands you almost for free, is the
  **I/O causal graph** (`button → relay → door.Open`; `func_movelinear rises 128u when triggered` —
  the changing-geometry case, wiring *and* envelope both static-knowable) and the **affordance
  prior** (portalable surfaces from `%noportal` VMTs, catapult arcs, funnel rails). That's the
  button→door wiring the model otherwise guesses — *static structure*, so no C9 warmup and no A\*
  trace budget. A\* owns geometry; the offline parse owns wiring + affordances; C9 owns *realized*
  config-space. The static/dynamic line is sharp: offline gives *potential* affordances (where a
  portal *could* go, what a funnel *would* carry); the *realized* graph (where the player actually
  shot portals, which branch is live) stays runtime — never offline.
- **The catch — it's a research-eval decision, not just engineering.** The harness exists to measure
  *whether reasoning is the bottleneck*; handing the model the wiring as a percept risks solving the
  puzzle for it. So the offline graph's first home is the **experimenter's** side (ground-truth,
  annotation labels the classname-walk drops, a 0% / partial / full **hint-ablation** knob), and
  model-facing exposure is a deliberate dialled choice. See offline_map_preprocessing.md §1 + fork #7.
- **Recon before any of it** (one weekend, you run it): `pip install srctools`; dump `BSP.ents`
  outputs on the real training BSP (`…/maps/workshop/1858300862251329775/1644417521.bsp`) and check
  the I/O edges against a hand-traced chamber; grep its pakfile for `.nut`/`vscripts` (the
  static-analysis hard ceiling — VScript-wired chambers yield only a skeleton); and — orthogonal but
  cheap — run `nav_generate` in a live PeTI chamber (`sv_cheats` already forced) to finally settle
  the navmesh-absent assumption this locomotion plan asserts but never checked.

**Net — three proposed uses of BSP geometry, all chased down (2026-06-20).** The instinct "surely the
map geometry buys us more" was pressed on three concrete fronts, and the pattern is consistent: *every
spatial use of offline geometry is better sourced at runtime; offline BSP's irreplaceable contribution
is the non-spatial structure.*
1. **Geometry-as-path-grid** → *parked.* Runtime hull-traces give ground-truth, dynamics-aware
   occupancy for free; a static grid can't see the dynamic cube-pocket (offline_map_preprocessing.md §2a).
2. **Geometry-as-named-region-vocabulary** (your "left-route-vs-right-route" percept) → *recon-gated
   percept, not a planner.* Real gap (A\* waypoints are nameless), but BSP regions are dead-for-PeTI +
   a watershed tar-pit, and a wrong graph poisons the eval — check if the model even needs it first
   (offline_map_preprocessing.md payload (d), P0d).
3. **Geometry-as-clearance-cost-field** (your "tight gap vs roomy detour" routing) → *runtime-local
   distance-transform wins; BSP rides-along-only.* Real gap (global route choice ignores clearance),
   fixed by a `g`-penalty off the grid A\* already builds; BSP-exact clearance is dominated by simply
   halving the cell (§2 clearance cost-term, fork #7, P2.5b).
So BSP earns its keep for the **causal wiring + affordances** (non-spatial, no runtime equivalent), and
its one *in-principle* non-dominated spatial role (sub-cell clearance) stays a rides-along upgrade, never
built for itself. The maximal thing the wiring then unlocks — a brute-force chamber **solver** (engine
save/load × the offline causal-graph prior × A\* verb primitives) — is recorded as a forward-looking
**experimenter-side solvability/difficulty oracle**, walled off to shallow chambers by PSPACE-completeness
(Demaine, Lockhart & Lynch, FUN 2018 — the cube/button/door fragment; *not* "NP-complete") and the
~4.3 s/node save-load warmup (§5). See offline_map_preprocessing.md §7 + the ROADMAP "someday" line.
