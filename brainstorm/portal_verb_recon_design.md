# Portal verb — L0 recon + surface-designation design (`place_portal`)

> **Scope (2026-06-27): RECON-ONLY this session.** Close the unknowns, lock the
> surface-designation design, write the recon commands for the user to run. **No verb
> code yet.** Mirrors the laser L0 cadence (recon → spike → percept → verb). `place_portal`
> is **build-order step 4** of [verb_grammar_rethink.md](verb_grammar_rethink.md) §Build order
> (:300); this doc is its L0. Read [ROADMAP.md](ROADMAP.md) first.

---

## 0. TL;DR

> **UPDATE 2026-06-29 — RECON RAN, mechanical verb GREEN.** R0/R2/R3/R4 closed: `TraceFirePortal`
> preview → `portal_place` commit places a portal that settles, activates, and auto-links, with ZERO
> requested↔placed drift. Only **R1** (the world-brush surface enumerator) gates the design fork. Full
> results + verb-build lessons in **§7**.

- **The actuator + fields + portalability oracle already exist in-tree** — `place_portal` is
  not a from-scratch build, it's a wiring job over `TraceFirePortal`. Validated this session.
- **One unknown gates the entire verb:** does `TraceFirePortal(…, ePlacedBy=2)` *commit* a
  portal or only *compute a preview*? Every in-tree caller treats it as compute-only and there
  is **no `FirePortal` symbol** in this fork. If it's preview-only, the commit path is the
  `portal_place` console command ([Teleporter.cpp:144](../src/Features/Teleporter.cpp#L144)).
  This is the Spike-1 go/no-go.
- **Surface-designation fan-out verdict (6 schemes → critique → 3 judges):** the user's
  *honesty instinct* (return the **effective** placed location) and *enumerator instinct* are
  vindicated; the *fractional-grid addressing* (`place_portal color surf x y`, fractions) is
  **rejected by all three judges** as "hand-aim in a costume" + illegible off a 2D frame. The
  target design is **`surface_center` + a coarse discrete `where` enum**; the ship-now
  alternative is **`aim_ray_reticle`**. They split on *timing* (ship-right vs ship-now), and
  **recon decides between them** — specifically whether the world-brush *surface enumerator* is
  feasible/leak-safe and whether v0 chambers even need sub-panel placement.

---

## 1. Settled — validate, don't rebuild

| Thing | Status | Anchor |
|---|---|---|
| **Verb spec** (do NOT redesign) | `place_portal(color: blue\|orange, surface) → PLACED \| NOT_PORTALABLE \| CANT_FIT \| OVERLAP \| FIZZLED \| NO_LOS`. Pair auto-links; re-placing a color *moves* it. | [verb_grammar_rethink.md:165](verb_grammar_rethink.md#L165) |
| **Actuator** | `int TraceFirePortal(pgun, &origin, &dir, portalToPlace, ePlacedBy=2, &info)` — computes placement, returns `info.ePlacementResult` + `finalPos` + `finalAngle`. `portalToPlace`: false=blue/primary, true=orange/secondary. Sig-bound across builds; **null-check** (can miss on a version). | [Server.hpp:19,45](../src/Modules/Server.hpp#L19); [Server.cpp:926](../src/Modules/Server.cpp#L926) |
| **Result taxonomy** | `SUCCESS/USED_HELPER/BUMPED` = placeable (`result<=2`); `CLEANSER→FIZZLED`, `OVERLAP_*→OVERLAP`, `INVALID_SURFACE/PASSTHROUGH/INVALID_VOLUME→NOT_PORTALABLE`, `CANT_FIT→CANT_FIT`, trace-miss→`NO_LOS`. | [PortalPlacement.hpp:6-28](../src/Utils/SDK/PortalPlacement.hpp#L6) |
| **Caller precedent** | `testPoint` (origin=`pt+normal*d`, dir=`-normal`) + `initScan` (gun acquire + `FindPortal(linkage, secondary, create)` priming) — copy-pasteable. `Hud/PortalPlacement.cpp` does the camera-ray variant. | [PlacementScanner.cpp:157-204](../src/Features/PlacementScanner.cpp#L157) |
| **Confirm fields** | `m_hLinkedPortal` / `m_bIsPortal2` (color: 0=blue, 1=orange) — name-resolved via `SE(p)->field<T>("…")`, no offsets. Already in the `prop_portal` snapshotter schema. **Never `m_bActivated`** (overloaded button/cube field). | [EntitySnapshotter.cpp:68-70](../src/Features/Harness/EntitySnapshotter.cpp#L68); [Entity.hpp:82-91](../src/Entity.hpp#L82) |
| **Portalability oracle** | Runtime `TraceFirePortal` IS the verdict (sees gel-flip / flip-panel / fizzler live). Static BSP `SURF_NOPORTAL` map is a **~89% false-positive** candidate-surface *prior* only. | [dump_ents.py:233](../py/bsp_recon/dump_ents.py#L233); [bsp_corpus_harness_improvements.md:187](bsp_corpus_harness_improvements.md#L187) |
| **Mandatory two-step** | `FindPortal(linkage, secondary, create=true)` + seed `m_hPrimaryPortal`/`m_hSecondaryPortal` on the gun **before** firing, else the placed portal has no backing entity. | [PlacementScanner.cpp:179-204](../src/Features/PlacementScanner.cpp#L179) |

---

## 2. The crux unknowns recon must close

- **🔴 R0 — does `TraceFirePortal` COMMIT or only COMPUTE?** Every in-tree caller (HUD preview,
  scanner) reads `info` and never relies on a side-effect placement; the scanner calls it
  thousands of times with no portals flying around → strong evidence it's **preview-only**.
  There is **no `FirePortal` symbol** in this fork. Fallback commit path = the `portal_place`
  console command ([Teleporter.cpp:144](../src/Features/Teleporter.cpp#L144)). **This decides
  whether `place_portal` is one call or compute-then-commit.**
- **🟠 R1 — surface designation feasibility.** `MarkTable` is **entity-only** (`index<<16|serial`,
  walks the entity list — [MarkTable.cpp:28](../src/Features/Harness/MarkTable.cpp#L28)); a
  white-tile wall face is **world brush geometry → no entity → unmarkable today**. So *any*
  "name the surface" scheme needs a **new surface enumerator** percept (the single hardest
  deferred percept). Recon: are v0-target PeTI portalable surfaces ever entities? how many
  candidate panels per chamber? mostly single-tile or multi-tile? does the offline BSP white-tile
  map agree with the runtime `TraceFirePortal` verdict on the same face?
- **🟡 R2 — placement code map + attractor behavior.** Confirm the enum→verb-code mapping live,
  and measure how far `info.finalPos` drifts from the requested point when a mapmaker **attractor /
  placement-helper** fires (`USED_HELPER`/`BUMPED`). The portal analogue of the laser ±24u radius.
- **🟡 R3 — confirm round-trip + the percept gap.** After a real place (blue then orange): does
  `m_hLinkedPortal` auto-populate (on which tick)? does `m_bIsPortal2` discriminate color? **Does
  a freshly-placed `prop_portal` read `abs_origin (0,0,0)`** (it currently hits the origin-reject
  at [PuzzleAnnotate.cpp:88](../src/Features/Harness/PuzzleAnnotate.cpp#L88) → no on-screen mark)?
  which field carries the real face position?
- **🟡 R4 — portalgun availability.** Does the harness TAS player have a fireable gun
  (`active_weapon` = portalgun, `m_bCanFirePortal{1,2}`) on a stock PeTI chamber, or must
  `place_portal` prime via `FindPortal`?
- **🟢 R5 — (the hard parked fork B) traversal distinguishability.** The engine *already
  physically walks the player through* an open portal mouth on a naive march
  ([llm_percept_act_grammar.md:103](llm_percept_act_grammar.md#L103)). Probe whether the planner
  can tell *intended traverse* from *blunder-through* — **decides** whether `navigate` ever ships a
  portal edge or portal traversal stays `launch`-only ([verb_grammar_rethink.md:263](verb_grammar_rethink.md#L263)).
  Cheap probe only this session; the `GoToPlanner` portal-edge build is its own workstream.

---

## 3. Recon plan (read-only → spike → probe) — mirror the laser L0

Two new `CON_COMMAND`s, byte-for-byte the shape of the laser recon at
[PuzzleAnnotate.cpp:482](../src/Features/Harness/PuzzleAnnotate.cpp#L482) (`sar_harness_laser_probe`)
and [:566](../src/Features/Harness/PuzzleAnnotate.cpp#L566) (`sar_harness_laser_intercept_spike`).
Reuse `ReconReadField` / `ReconReadHandleIndex` ([:466-480](../src/Features/Harness/PuzzleAnnotate.cpp#L466)).
**User runs them in-game** (per the no-launch rule); each must `list selectable indices` on too-few-args.

### Phase R — `sar_harness_portal_probe` (read-only) → closes R1·R3·R4
Walk the entity list; for every `prop_portal` and the player's portalgun, dump origin/abs_origin
+ **every candidate position field** (`m_ptOrigin`, `m_vecAbsOrigin`, networked portal-pos prop) +
`m_hLinkedPortal`→partner index + `m_bIsPortal2` + `m_bActivated`; for the gun dump `active_weapon`
classname + `m_bCanFirePortal{1,2}` + `m_iPortalLinkageGroupID`. **Answers:** which field carries a
placed portal's real face position (the (0,0,0) gap), does linkage resolve, does the agent have a gun.

### Phase S — `sar_harness_portal_fire_spike <color> <pos|mark> [dir]` (mutating) → closes R0·R2
Full two-step: prime via `FindPortal` → call `TraceFirePortal` from a synthesized `(origin,dir)` (the
`testPoint` idiom) → **if R0 shows preview-only, also shell `portal_place`** → settle N ticks → read
back `ePlacementResult` + `finalPos`/`finalAngle` + `m_hLinkedPortal`/`m_bIsPortal2`. Sweep the
requested point across a white wall / black wall / fizzler-cross / existing-portal overlap and **log
raw `ePlacementResult` + the requested↔placed drift**. This is the laser-intercept-spike analogue: it
**proves a portal places + auto-links programmatically before any verb code.** Go/no-go.

### Phase R5-probe — reuse `sar_harness_laser_reachability_test` ([GoToPlanner.cpp:305](../src/Features/Harness/GoToPlanner.cpp#L305))
Stand across an open linked pair; run the reachability test at a mark behind the far portal — does the
far island read **severed** (needs an explicit edge) or already reachable? Then `navigate` toward it
and **look at the rendered frames** — does the body teleport through (blunder) or stall? Records the
data the halt-vs-traverse decision needs; **builds nothing.**

> **Lessons baked into the commands** (from [dual_role_cube_postmortem.md §7](dual_role_cube_postmortem.md#L254)):
> read the engine's own field (never infer); **skip player + held cube** in any placement trace
> (self-block class of bug); a reachability pre-gate must **not** be stricter than the carry;
> re-assert the view each tick-advancing batch (`harness-view-not-held-across-ticks`); after
> `make proto`, `git checkout --` the regenerated `*.pb.cpp` (`format.sh` reindents them).

---

## 4. Surface-designation brainstorm — the design fork

> A 6-scheme competing-design fan-out (design → adversarial critique → 3 independent judges:
> philosophy-purist, VLM-legibility, shipping-pragmatist), mirroring the verb-grammar rethink.

### 4.1 The scoreboard

| Scheme | Purist | Legibility | Pragmatist | One-line verdict |
|---|---|---|---|---|
| **`surface_center`** (name a panel, engine centers) | **8.2 ★** | **7.1 ★** | 5.9 | Cleanest no-hand-aim + local/global cut; amputates the *within-wall* DOF (a reasoning gap, additively fixable). |
| **`aim_ray_reticle`** (fire along view; reticle bit) | 4.3 | 5.8 | **7.4 ★** | Only scheme that **ships this session** on existing infra — but relocates motor-tax into a `look`-loop + the reticle already draws the answer disc. |
| `coarse_region` (panel + 3×3 anchor enum) | 6.6 | 5.0 | 4.5 | Right *intra-surface vocabulary* (high/low/corner, engine-resolved) but front-loads the enumerator + a labelled lattice. |
| `tile_quantum` (one mark per portal-sized tile) | 4.5 | 6.5 | 5.3 | Clean "tile is the unit" story, but the confirm-scan is a **rendered portalability oracle** (draws the answer set). |
| **`surface_grid`** (user's: panel + `x y` cells, fractional) | 3.8 | 3.7 | 3.6 | **Rejected:** fractional `(x,y)` IS hand-aim renamed; bbox/up-cross origin unreadable off one eye-height frame. |
| `surface_uv_units` (continuous world-unit offset) | 2.3 | 2.6 | 2.7 | Worst: world-unit floats are pitch-degrees in costume; published extents leak the answer wall. |

### 4.2 What the user's grid idea got right (keep) and wrong (drop)

**KEEP — both instincts are vindicated:**
- **"return the effective placed index."** This is the *most-praised* honesty mechanism across the
  whole fan-out. The verb returns `placed_pos`/`placed_angle`/`placed_surface`/`used_helper` straight
  from `info.finalPos` — **never the request** — so a requested≠placed attractor snap is visible at a
  glance. Mirrors the scanner's existing `liesInMatchArea(info.finalPos)` discipline
  ([PlacementScanner.cpp:176](../src/Features/PlacementScanner.cpp#L176)). Every surviving scheme adopts it.
- **"enumerate portalable surfaces."** Right direction — *but* it is exactly the hard deferred percept
  (world-brush faces aren't markable) **and** it carries an **oracle-leak risk**: enumerate only the
  *currently-portalable* / *answer-relevant* panels and the percept hands the model the answer key;
  the clusterer's split/merge threshold becomes an author-held difficulty dial. Mitigation is load-bearing:
  **enumerate ALL portalable panels uniformly, identical styling, no salience.**

**DROP — the philosophy rejects it (unanimous across judges):**
- **Fractional `(x,y)` cell addressing.** A continuous sub-tile offset is "hand-aim in a costume" —
  it reintroduces the motor-tax altitude `aim_at`/macros exist to abolish. And the per-surface
  coordinate frame is **illegible**: `getAxesForPlane` returns an *arbitrary* in-plane basis with no
  canonical origin ([PlacementScanner.cpp:48](../src/Features/PlacementScanner.cpp#L48)), so "(0,0)
  bottom-left, +x right, +y up" is crisp on an axis-aligned wall but ambiguous on angled / seam-split /
  edge-on faces — and a VLM cannot dead-reckon a cell off a foreshortened blank white plane.

### 4.3 The target design (ship-right) — `surface_center` + coarse `where`

```
place_portal(color: blue|orange, surface: mark, where: CENTER|TOP|BOTTOM|LEFT|RIGHT|corner = CENTER)
  -> { result, placed_pos, placed_angle, placed_surface, used_helper }
```
- **`surface`** = a single integer mark on a portalable panel (maximally legible: one integer on the
  thing you choose, **no coordinate to read**). `where` = an optional **closed, engine-resolved** coarse
  anchor that restores the within-wall reasoning DOF (fling-apex height, corner-align) **without** any
  continuous hand-aim — strictly coarser than `look`'s 15° snaps. On the single-tile PeTI majority
  `where` is omitted/`CENTER` and the verb collapses to "name the panel."
- **Why `where` and not pure center:** the purist + pragmatist judges both flagged that pure
  `surface_center` *amputates a reasoning DOF* on multi-tile walls (where-on-the-wall is a genuine puzzle
  choice that changes the downstream graph edge) and lets the attractor *smuggle in* the spatial choice
  on exactly the discriminative chambers. `where` keeps the choice in the model's head as a discrete
  enum — restoring eval-validity while honoring no-hand-aim + KISS.
- **Gated on:** the **world-brush surface enumerator** (R1) — discover portalable panels (runtime
  `TraceFirePortal` probes, BSP white-tile map as a seed prior), cluster coplanar 128u-adjacent hits into
  panel regions, assign a stable surface-mark, project a label per panel (reuse the
  [PuzzleAnnotate](../src/Features/Harness/PuzzleAnnotate.cpp) legibility path: clamp on-screen + LOS-cull
  + declutter). **This is the single hardest deferred percept on the portal track.**

### 4.4 The ship-now alternative — `aim_ray_reticle`, and why it's risky

```
place_portal(color: blue|orange)   # fires along the current view ray; model orients with look()
```
- **Pro:** ~40–50 LOC over `TraceFirePortal`, **no enumerator** — rides the camera-aim dry-trace the
  annotate path already runs ([Hud/PortalPlacement.cpp](../src/Features/Hud/PortalPlacement.cpp)). It is
  the only scheme whose correctness floor doesn't depend on solving world-brush marking.
- **Three real wounds** (why the purist scored it 4.3): (a) it **relocates motor-tax into a `look`-loop**
  — converging a ray by hand with 15° snaps over a ~single-digit-degree target is the exact
  reasoning-vs-actuation confound the harness exists to abolish; a "solve" measures look-loop persistence.
  (b) **The existing reticle already draws the post-attractor disc at `finalPos`** (per recon of
  `Hud/PortalPlacement.cpp`) — i.e. the actuator already renders "here's where a legal portal ends up" on
  the annotated frame: an anti-oracle leak that's *already shipped*. (c) Naively dropping the
  `liesInMatchArea` gate to build the legality bit makes that bit **lie** (reports portalable when the
  aim is on bare wall but an attractor would yank `finalPos` to a distant tile).

### 4.5 How recon decides between them

The 2-1 split is about *timing*, not the ideal. Recon resolves it:
- **R1 chamber census** — if v0-target chambers are **mostly single-tile panels**, `surface_center`
  with **no `where`** suffices and the enumerator is "one label per panel" (still needs world-brush
  marking, but no sub-panel vocabulary). If v0 needs multi-tile spatial reasoning, you need `where`.
- **R1 enumerator feasibility + leak-safety** — can we enumerate *all* portalable panels uniformly
  (no answer-set salience), affordably, with stable marks? If yes → ship-right is on the table.
- **The reticle leak** — confirm what the annotate path actually draws today; if it renders the
  post-attractor disc, that must be addressed before `aim_ray_reticle` is anti-oracle-clean either way.

---

## 5. Decision (2026-06-29) — PURE SHIP-RIGHT

**DECIDED: build the named-panel verb `place_portal(color, surface, where)` with a world-brush
surface enumerator. No `aim_ray_reticle`, no corner-cutting.** R1 is closed: **M = 0.678** (68% of
portalable panels are multi-tile, 270-map census) + helpers too sparse to enumerate (~5.5/map vs ~21
panels/map) + the enumerator does not pre-exist (`PlacementScanner` enumerates placements not
surfaces; `MarkTable` is entity-only). Multi-tile dominance plus the eval-validity cost of a hand-aim
look-loop (the reasoning-vs-actuation confound this harness exists to remove) settle it.

1. **Designation — RESOLVED: `surface_center` + `where`.** Build the enumerator. `aim_ray_reticle`
   rejected: it relocates motor-tax into a `look`-loop and the reticle already leaks `finalPos`.
2. **`where` vocabulary — keep the full set** `{CENTER,TOP,BOTTOM,LEFT,RIGHT,4 corners}`. M=0.678 says
   panels are multi-tile-dominant, so the sub-panel DOF is real; don't trim it pre-emptively. `where`
   defaults to `CENTER`, so single-tile panels collapse to "just name the panel".
3. **R5 / fork B — `launch`-only for now.** `navigate` stays portal-blind; portal traversal routes
   through `launch` until the `GoToPlanner` portal-edge workstream. Unchanged.

---

## 6. Build order + provenance

`place_portal` is **step 4** of [verb_grammar_rethink.md](verb_grammar_rethink.md#L300) (after the
`navigate` rename, the `GoToPlanner` reachability frontier, and the shipped laser family); it **unblocks
`launch`**. When it lands, the wiring template is `Interpose`/`RedirectTo` (NOT the `press`
`NOT_IMPLEMENTED` stub): grammar in [macro_grammar.py](../py/p2harness/macro_grammar.py) `VERB_SPECS` +
`build_macro` + `validate`; proto `string color` on `MacroRequest`
([harness.proto:84](../src/Features/Harness/harness.proto#L84) reserves `// later (portals/nav)…`);
dispatch at [MacroExecutor.cpp:1099](../src/Features/Harness/MacroExecutor.cpp#L1099); `agentloop_smoke`
round-trip. **None of that this session — recon + this doc first.**

*Provenance: a 12-agent recon read (design docs + engine foundations + portalability) and a 15-agent
surface-designation fan-out (6 competing schemes → adversarial critique → 3-lens judge panel),
2026-06-27, grounded against the shipped laser L0 cadence, `TraceFirePortal`/`PlacementScanner` in-tree,
and the dual-role-cube post-mortem lessons.*

---

## 7. Recon results (2026-06-29) — mechanical verb GREEN

Ran `sar_harness_portal_probe` + `sar_harness_portal_fire_spike <blue|orange>` on
`sp_a2_triple_laser`. **The compute→commit spine is proven end-to-end: `TraceFirePortal` preview →
`portal_place` commit places a portal that settles, activates, and auto-links.** R0/R2/R3/R4 closed;
only the R1 surface enumerator + chamber census remains, and R5 was not run.

| Unknown | Verdict |
|---|---|
| **R0 — commit?** | ✅ Preview-then-commit works. White wall → `BUMPED(2)` → `portal_place` → live linked portal. (Can't isolate whether `TraceFirePortal` alone commits, but the two-step is what we ship and it works.) |
| **R4 — gun** | ✅ `sp_a2_triple_laser` = DUAL device: `m_bCanFirePortal1/2=true`, `linkage=0`. The gun pre-creates its 2 `prop_portal` entities (`m_hPrimaryPortal→[272]` blue, `m_hSecondaryPortal→[574]` orange) at spawn, inactive at `(0,0,0)`. |
| **R2 — drift** | ✅ **ZERO** — committed origin == `finalPos` exactly, twice (`8008.3,-5504,56`; `7424,-5312,56`). Black wall → `INVALID_SURFACE(8)`, rejected before commit. ⚠️ the `placementHelper` bool is ALWAYS `yes` (even on rejects) → useless; honest `used_helper` = `ePlacementResult==USED_HELPER(1)`. |
| **R3 — round-trip** | ✅ `m_hLinkedPortal` cross-links both ways a few frames AFTER the 2nd placement (single portal reads `-1`). `m_bIsPortal2` = color (blue/primary=false, orange/secondary=true). **The "abs zeroes / server carries real" hypothesis was WRONG:** once settled `abs_origin==server_origin==finalPos`. The `(0,0,0)` is a transient pre-settle/unplaced state. |
| **R1 — surface** | ⛔ **OPEN** — target walls are world brush (not entities); only the placed portal is markable. Unchanged by this run. The white walls fired `BUMPED`+`helper=yes` → they carry `info_placement_helper` attractors, a candidate enumerable unit. |
| **R5 — traversal** | ⛔ Not run. Lean stays launch-only. |

**Verb-build lessons (bank for `place_portal`):**
1. **Confirm on a LATER tick** — the same-frame readback after `portal_place` always shows
   `(0,0,0)`/inactive; settle N ticks (mirror the laser settle), then read
   `finalPos`/`m_bActivated`/`m_hLinkedPortal`.
2. **Percept gate = `m_bActivated==true`** — drops the 2 inactive gun-slot ghosts AND unsettled
   placements in one field. For `prop_portal`, `m_bActivated` reliably means "live placed portal" —
   a carve-out to §1's blanket "never `m_bActivated`" (that was a cross-class caution; on portals it
   is the MVP field). Retires the "ghost (0,0,0) portals" percept-noise from the laser post-mortem.
3. **`placed_pos = finalPos`** (exact), **`used_helper = (ePlacementResult==1)`** (not the bool).
4. **Cleanup:** the `TraceFirePortal`-unbound guard was removed (`ret=0/1` proved it binds on build
   9568); §1's "null-check (can miss on a version)" now stands only as future-porting guidance.

**Next:** R1 — census the v0-target portal chambers (single- vs multi-tile panels) + probe the
`info_placement_helper` route to a world-brush surface enumerator. That resolves §5.1 (ship-right
`surface_center`+`where` vs ship-now `aim_ray_reticle`).

---

## 8. R1 recon plan — agreed 2026-06-29 (BOTH tracks, cross-validated)

A 5-agent R1 fan-out (G1–G4 grounding + synthesis) established the stakes: **`PlacementScanner` is a
*placement* enumerator, not a *surface* one** — its `testPoint`/`initScan` spine is reusable, but panel
auto-discovery + coplanar clustering + non-entity marking are all NEW (`MarkTable` is entity-only, world
brush has no entity). `info_placement_helper` has no surface-ID field and unproven coverage (leak-risky
prior, never the sole enumerator). `dump_ents.py --geometry` already emits per-face
`portalable`/`plane`/`verts`. So "name the surface" is a **MEDIUM-lift** build *if* we go ship-right — which
is exactly why R1 measures whether v0 needs it before building anything.

**This is recon to DECIDE, not the enumerator build.** User chose BOTH tracks so the runtime side
ground-truths the BSP's ~89% portalable-flag false-positive the offline census would otherwise inherit.

### Track C — in-engine `sar_harness_portal_surface_census` (read-only, console-only, no file writes)
Mirror `sar_harness_portal_probe` ([:690](../src/Features/Harness/PuzzleAnnotate.cpp#L690)); reuse
`ReconReadField`/`ReconReadHandleIndex` + the `PlacementScanner` `camTrace`/`getAxesForPlane`/`testPoint`/
`initScan` idiom ([PlacementScanner.cpp:21-204](../src/Features/PlacementScanner.cpp#L21)).
- **C1** classname gate + command skeleton (banner, null-guard, no-op). Verify: appears in-game, mutates nothing.
- **C2** helper census loop — walk entity list, gate `info_placement_helper`, print index + `abs_origin` +
  `m_flRadius` + `m_bForcePlacement`/`m_bSnapToHelperAngles`/`m_bDisabled`/`m_bDeferringToPortal`
  ([PlacementHelperHud.cpp:77-88](../src/Features/Hud/PlacementHelperHud.cpp#L77)). Verify: lists white-wall helpers.
- **C3** camera-aimed wall pick — `camTrace` + `getAxesForPlane`; print hit normal/origin/basis. Verify: prints the aimed wall.
- **C4** coarse portalability strip — prime gun (`initScan`), `TraceFirePortal` at **64–128u** spacing across
  the aimed wall plane; print per-probe `ePlacementResult` + `finalPos` + `used_helper (==result 1)`. Verify:
  white→run of `SUCCESS/BUMPED`, black→`INVALID_SURFACE`, helper drift visible in `finalPos`.
- **C5** build (`make`); user runs on 3–5 v0-target chambers, captures console.

**Track C — RESULTS (2026-06-29, `sp_a2_triple_laser`).** Command validated: black wall → `0/81`,
white panels → `#` clusters, floor/ceiling → large fields (72–73/81). (a) **Helpers are SPARSE** — 5
`info_placement_helper` in the whole chamber (radii 16–24u, all flags 0), nowhere near one-per-panel →
the **helper-only enumerator is DEAD** (confirms g2: mapmaker attractors, partial coverage). (b)
`used_helper` is always 0 here **by design** — perpendicular probes place directly and never need a
helper-assist; helper coverage is read from the Part-1 census instead. (c) **Panels are a size MIX**: a
1×4-cell strip (~1 tile wide), a 4×4-cell wall panel (~2×2 tiles), and large portalable floors/ceilings
→ multi-tile panels common → *preliminary* lean toward `where`. (d) Runtime cleanly ground-truths
portalable/not, so the BSP-vs-runtime cross-check is viable. **Caveat:** one Valve SP map, a spot-check
— the PeTI corpus census (Track P) is the prevalence authority.

### Track P — offline BSP census (after Track C, per SAR-before-Python)
- **P1** run existing `uv run python py/bsp_recon/dump_ents.py --corpus workshop --geometry --out artifacts/bsp_recon`
  (no code) → confirm `*.geo.json` carries `faces[].portalable` + `info_placement_helper` entities.
- **P2** `cluster_panels.py` (new, <200 LOC): load `*.geo.json`, **material-filter to white tile** (dodges the
  89% FP), union-find faces by coplanarity (normal+dist ε) + 128u adjacency → panel cells; emit per-map
  **single-vs-multi-tile histogram** + a **helper-coverage tally** (spatial-join `info_placement_helper`
  onto panels) → `panel_census.json`. Verify on one map: ~29 panels, sane sizes.
- **P3** cross-validate P2 vs the C5 runtime capture on the same chambers — flag BSP false-positive discrepancies.

### Decision rule → resolves §5.1
Let **M = fraction of portalable panels that are multi-tile** (≥2 contiguous 128u cells), corpus-wide.
- **M low (< ~20%) + runtime confirms single-tile-dominant** → `surface_center`, **no `where`** (one label/panel).
- **M high + look-loop is the bottleneck** → `surface_center` **+ `where`** (the sub-panel DOF is a real puzzle choice).
- **clustering not uniform/stable/leak-safe OR runtime confirm too costly at load** → ship-now **`aim_ray_reticle`**,
  earn the enumerator later (fix the `Hud/PortalPlacement.cpp` reticle leak first — §4.4).

**RESULT: M = 0.678 — far above the threshold → ship-right `surface_center` + `where` (build the
enumerator). DECIDED 2026-06-29, see §5.** Full-corpus M; the in-scope v0 subset is unconfirmed (the
chamber-set reader failed), so re-census M on the in-scope chambers when sizing the `where` vocabulary.
