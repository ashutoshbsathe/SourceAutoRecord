# Portal Unified Grammar — OPTIONS DOSSIER (you are the judge)

Status: **GRAMMAR LOCKED — building the spine.** The design fan-out below is the record of *how* we got here;
the section immediately following is the DEFINITIVE, LOCKED decision log. If the two disagree, the log wins.

---

# ★ DECISIONS — LOCKED (2026-07-01) ★

**How we got here:** R5 portal-traversal recon ran (SEVERED + engine blunders body through on mouth-contact) →
`go_to`-portal-edge (fork-B) **DROPPED** by user (routing a path *through* a portal is pointless; portals must
interlock with flings + lasers) → 28-agent unified-grammar brainstorm (this doc) → user rulings below. Every
axis shares one transform `M = prop_portal.m_matrixThisToLinked` (body / beam / momentum through a portal).

## The LOCKED spine verb surface (this session, zero new offsets)

| Verb | Signature | Ships now? |
|---|---|---|
| `pass_through` | `pass_through(portal: blue\|orange)` → `TRANSITED(cell) \| NO_SUCH_PORTAL \| UNLINKED \| NOT_AT_MOUTH \| BLOCKED` | ✅ walk-transit (fling-landing deferred) |
| `drop_into` | `drop_into(portal: blue\|orange, object: mark = None)` → drop self (None) or held object INTO a ground portal | ✅ object + gentle self-drop (fling-landing deferred) |
| `place_portal` | `place_portal(color, surface: S-mark, where: enum \| uv(u,v))` — declarative `(u,v)` on **all panels** | ✅ |
| `redirect_to` / `interpose` / `aim_at` | accept `@blue` / `@orange` as a target **verbatim, no new code** | ✅ free once portals are named |

**Portal naming = by COLOR.** Two fixed percept slots on `GameState`: `blue_portal`, `orange_portal` (there is
only ever one of each per linkage). Each publishes `{present/active, mouth_center, mouth_normal, linked}`.
Verbs reference `@blue` / `@orange`.

## The rulings (each is LOCKED; rationale in the axis sections below)

1. **Traversal/drop family — INTO-a-portal vs FROM-a-mark.** `pass_through` = walk self into a foot-reachable
   mouth (momentum-implicit: arrive fast → fling out). `drop_into(portal, object=None)` = drop self OR a held
   object into a GROUND portal (`object=None` ⇒ player; a distinct local controller — route to the ledge above
   the mouth + step off). **No `fall_into` verb** (folded into `drop_into`). **`launch(via=@plate|@gel)`** =
   FROM-the-mark throw, **deferred**. **Throw-into-wall-portal OUT OF SCOPE** (never an intended workshop
   solution — user, 15-min corpus review).
2. **Laser × portal — KEEP THE VERB DUMB.** `redirect_to`/`interpose`/`aim_at` aim at the portal **mouth**; the
   model reasons the exit itself. **No pull-back solver, no new laser code** — once portals are named marks, the
   existing verbs accept `@portal` and the engine-native `m_bPowered` confirms through the pair FREE. Only the
   through-portal *seat* (poly-line `interpose` on the exited leg) is gated on the P0 recon + deferred.
3. **Fractional `(u,v)` — ALL PANELS** (walls + floors/ceilings), gravity-anchored canonical frame; accept the
   near-vertical-normal degeneracy as a *measured* risk (define the fallback frame, measure VLM legibility).
4. **Portal marking — EXCLUDE, don't un-exclude.** Live settled portals are NOT hidden by the `(0,0,0)` reject
   (they have real origins) — today they'd get a meaningless generic integer mark. So: **exclude `prop_portal`
   from the generic integer-mark path entirely** (portals are color-referenced, never integer-marked, no
   double-representation) and **add a dedicated 2-slot read gated on `m_bActivated==true`** (that gate — not the
   origin heuristic — is what filters the gun's inactive ghost portals). Read pose via `server->GetAbsOrigin`
   (real face even when `abs_origin==0` on the settle frame), normal via `abs_angles`, linkage via
   `m_hLinkedPortal`. The `(0,0,0)` reject stays untouched.
5. **Session scope — SPINE ONLY.** Defer all momentum (`launch`, player-fling landing-confirm) behind the DIED
   terminal + confirm infra; defer the entire gel/paint percept.

## Honestly DEFERRED (designed, NOT cut — each names its blocker)

- `launch(via)` + player-fling **landing-confirm** — blocked on the DIED/respawn terminal (a fling into goo
  silently respawns → the verb would gaslight the model) + `FL_ONGROUND` (unplumbed) + settle-tolerance bisect.
- **Gel / paint percept** — no `info_paint_sprayer`/paint read exists; blocks `paint()`, repulsion-gel `launch`,
  propulsion, conversion-gel-into-`place_portal`. Largest debt.
- **Through-portal `interpose` seat** (poly-line on the exited beam leg) — gated on the P0 recon (does a bare
  `TraceRay` report a `prop_portal` hit at the plane?); fallback = user's manual line-intersection.
- **Non-coplanar (floor↔ceiling) laser aim**, **through-portal visibility labels** — deferred, not removed.

## Build order (small phases, C++ before Python, verify in-game each)

**B** portal naming + percept (excl. from generic marks + 2 color slots) → **C** `pass_through(color)` →
**D** `drop_into(portal, object=None)` → **A** fractional `(u,v)`. Laser falls out free after B.

---


Thesis under evaluation: hand the frozen LLM/VLM a clean percept + reliable macro verbs; the macro draws the line at **LOCAL** (engine-simulated actuation) vs **GLOBAL** (model reasoning). A verb is **free** if it *consumes* an affordance and a **puzzle** verb if it *creates/changes* one. Must be palatable to a frozen LLM AND a human baseline (the control arm).

Indexes: `verb_grammar_rethink.md` (backbone), `portal_verb_recon_design.md` (place_portal recon, R3/R5), `laser_redirect_verb_design.md` (laser family), `portal_place_verb_plan.md` (C3 leak discipline). Read those first.

---

## 1. Framing — the 6 axes, and why portals can't be designed in isolation

Six axes, all coupled through the same substrate primitives:

1. **PASS_THROUGH** — deterministic WALK transit of a placed portal pair.
2. **FLINGS** — momentum transit (portal-fling ∪ faith-plate ∪ repulsion-gel bounce).
3. **FRACTIONAL SURFACE** — sub-tile `(u,v)` placement on huge (10×12-tile) panels.
4. **LASER × PORTAL** — beam routed cube → portal A → (engine transit) → portal B → target.
5. **PORTAL NAMING** — how a placed `prop_portal` is named/marked so the other verbs can reference it.
6. **PERCEPT** — what the model must SEE to drive all of the above.

**Why the axes interlock (they share primitives, not just a theme):**

- **A portal is one affine transform `M` = `prop_portal.m_matrixThisToLinked`** (read forward+backward in-tree at `EngineDemoPlayer.cpp:435-436`; applied via `VMatrix::PointTransform/VectorTransform`, `Camera.hpp:108`). BODY-through-portal (axis 1/2), BEAM-through-portal (axis 4), and MOMENTUM-through-portal (axis 2) are the **same** `M` applied to a point / a ray / a velocity. Design axis 4's laser math and you have already designed the geometry axis 1/2 needs, and vice-versa.
- **Naming (axis 5) gates axes 1/2/4.** `pass_through`, `launch(via=@portal)`, and `redirect_to`-through-portal all need to *name* the portal the body/beam enters. No naming scheme → none of the three can be expressed. Naming is the keystone.
- **Percept (axis 6) gates ALL FIVE.** Every verb argument references something the percept must publish: the portal color/linkage, the surface `(u,v)` frame, the fling affordance (drop + launch vector), the through-portal reachability. The substrate is unanimous: *every one of the six asks is blocked on a percept addition before its verb is expressive.* You cannot design the verbs against today's blind (frustum + direct-LOS) percept.
- **Flings ∩ lasers ∩ pass_through share the confirm story.** The engine owns the local actuation (transit / arc / beam re-emit / powered-latch); the model owns the global choice (which portal / which surface / which fraction / which entry-aim). That single cut is what makes the whole grammar honest — or leaky.

Consequence: this session designs the **spine** (naming + percept + pass_through + fractional-uv + laser-through-portal), which is cheap and mostly zero-new-offset. The **momentum half** (launch) is designed here but is honestly gated on unbuilt substrate (see §5, §6).

---

## 2. Per-axis options

Each axis: 2–4 concrete options, tradeoffs, marked lean. **You may override any lean.**

### Axis 1 — PASS_THROUGH (deterministic walk transit)

Substrate fact (R5, `portal_verb_recon_design.md:236`): the engine **physically transits the body on mouth-contact** — `getpos` before/after shows same z/pitch/roll, **yaw flipped ~180°**, mirrored standoff. The flat A* planner reads the far mouth as a **SEVERED disconnected component**, and `go_to`'s local path can **accidentally graze** an open mouth (blunder-through).

- **Option 1A — `navigate` A*-portal-edge (fold traversal into the planner).**
  Tradeoff: zero new verb, but the user **rejects** this ("finding a path through a portal is pointless") and R5 kills it on substrate: the planner sees SEVERED, so the edge is dishonest, and a blunder-through would be relabeled as an intended traverse. ❌
- **Option 1B — `pass_through(portal: blue|orange)` explicit verb.** March body to the named mouth → engine transits → settle → **verify the yaw-flip + mirrored-standoff signature** before returning `TRANSITED(cell)`.
  Tradeoff: one new verb, but makes the *decision to traverse* a first-class model act (the puzzle bit), and the signature check distinguishes intended transit from blunder-graze. Naming is by **color** (matches `place_portal(color)` + engine `m_bIsPortal2`; the model just placed blue, it references blue). ✅
- **Option 1C — `pass_through <P-mark>` by entity mark instead of color.**
  Tradeoff: future-proofs multi-pair chambers, but adds mark-namespace churn and is un-needed for PeTI v0 (≤1 pair). See axis 5.

**Our lean: 1B, name by color.** Codes: `TRANSITED(cell) | NO_SUCH_PORTAL | UNLINKED | NOT_AT_MOUTH | BLOCKED | DIED`.

**Open critique (you decide):** the success predicate. "yaw flipped ~180°" is the signature of ONE antiparallel wall↔wall transit; a floor↔wall pair won't produce a clean 180° flip. The honest predicate must generalize to **"pose consistent with the pair transform `M`"** (position-side: emerged in front of the linked mouth), not a hard-coded 180°. Also: if `MarchTo` VFH keep-out stops the body *short* of the mouth disc, `NOT_AT_MOUTH` false-fires and a mouth-approach special-case reintroduces some motor-tax — this is a real seam, not fully closed.

### Axis 2 — FLINGS (momentum traversal)

Mechanism ground-truth (`p2-mechanics`): a Source fling **conserves speed magnitude, rotates direction** through the exit mouth. The two DOFs are **entry portal** (sets `|v| = sqrt(2gh)` from drop height; engine caps exit ~1000–1100 u/s) and **exit portal orientation** (sets the launch vector: vertical wall = flat/far, angled = up-and-out). There is **no in-flight steering primitive**. Faith-plate `trigger_catapult` *overrides* velocity with a map-authored vector; repulsion gel *reflects* approach velocity; propulsion gel is a *speed source* (not a launch). All three momentum sources are the same event class ("become a ballistic projectile").

- **Option 2A — `launch(via: mark)`, NO `toward` arg, one verb for all three sources.**
  Tradeoff: mechanism-correct (a `toward` would be a lie — ignored — or a teleport that deletes momentum-as-currency, the single most Portal-specific reasoning act). Expressivity lives upstream in `place_portal`/`paint`/approach-speed. `via` names the entry affordance (`@blue_portal` | `@faith_plate` | `@repulsion_patch`). Bare landing cell on miss, no hill-climb. ✅ **(all four finalists agree on this)**
- **Option 2B — `launch(via)` + a separate `jump_through(portal, via)` for portal-flings.**
  Tradeoff: more "player-natural" (a self-authored portal fling *feels* different from stepping on a plate), and it cleanly owns the floor-portal→wall-portal case. BUT: identical projectile physics under two verbs → the verb NAME encodes which mechanic solves the room (**puzzle-smuggling**), and `jump_through(color, via=@strip)` **overlaps** `launch`'s domain → same physical fling has two legal encodings → non-deterministic scoring. The prior design already carried the source distinction via `via` **without** the second verb.
- **Option 2C — `launch(via, toward: mark)` directional.**
  Tradeoff: ❌ mechanism-false. Rejected by every mechanics read; there is no steering primitive to expose.

**Our lean: 2A.** The REAL fix for `launch`'s under-specification is **not** a `toward` arg — it's the **percept** (axis 6): today the model is asked to reason about momentum vectors (drop height, plate launch dir, gel bounce normal) it **cannot see**. Publish those and `launch(via)` is expressive enough.

**Honest deferral flag (unbuilt-terminal):** `launch` is **NOT groundable this session**. Its `LANDED`-vs-`DIED` confirm predicate depends on three unbuilt pieces + one unmeasured:
1. **`FL_ONGROUND` read** — not plumbed.
2. **DIED/respawn terminal** — `GameState.health` exists but the terminal logic doesn't; **a fling into goo silently respawns and the verb gaslights the model. HARD BLOCKER.**
3. **Settle-tolerance constant** — undefined; needs bisection (like the `release` dwell constant).
4. **Body-jank re-roll budget** — you cannot cheaply re-drop the player like a re-teleported cube, so a correct plan landing jank-short 1-in-N mislabels `LANDED_SHORT`; unmeasured.

Terminal maturity is **three different levels**: portal-fling (substrate ready, needs the confirm predicate) · faith-plate (percept exists, nearest-buildable) · **gel-bounce (BLOCKED — the paint/gel percept is entirely absent, see axis 6).** The `launch` unification is sound; only ~1/3 ships near-term.

### Axis 3 — FRACTIONAL SURFACE PLACEMENT (sub-tile `(u,v)`)

The prior fan-out **rejected** fractional `(x,y)` for two reasons; the user's override splits them:
- **Objection A — "hand-aim in a costume" (motor-tax).** DIES once `(u,v)` is **declarative** — a number the model *computes from published extents*, not a ray it *steers* via a look-loop. A number reasoned out ≠ a ray converged. The override is correct on this axis.
- **Objection B — "illegible frame."** REAL, and it is the whole job. `PlaneAxes(n)` (`BspFilePanelSource.cpp:209`) returns a deterministic-but-**arbitrary** in-plane basis with **no canonical origin** — a VLM can't know which corner is (0,0) or which way `u` runs on a foreshortened white plane.

Options for the legible frame (this is what you're choosing between):

- **Option 3A — coarse `where` enum only** `{CENTER,TOP,BOTTOM,LEFT,RIGHT,4 corners}` (the recon-DECIDED status quo).
  Tradeoff: crisp, zero legibility risk, but **inadequate for 10×12-tile walls** — exactly the case the mandate calls out. ❌ as the sole mechanism.
- **Option 3B — declarative `(u,v)` off a GRAVITY-ANCHORED canonical frame, `where` kept as sugar.** ✅ **(all four finalists converge here)**
  - `v̂` = in-plane direction closest to world **+Z** ("up the wall"); floors/ceilings fall back to +X-projected.
  - `û` = `v̂ × n̂` oriented so `u` points to the **viewer's right** facing the panel (kills "which way is +x").
  - **Origin** = the (low-v, low-u) corner = "bottom-left as you face the wall", published **explicitly** (no dead-reckoning).
  - Publish `u_extent, v_extent` in world units → `(u,v)∈[0,1]²` is a pure normalize.
  - `world = origin + u·u_extent·û + v·v_extent·v̂ + standoff·n̂` (exactly `PlanePoint`, `BspFilePanelSource.cpp:215`).
  - `where` resolves internally to a canonical `(u,v)` (single-tile → CENTER).
- **Option 3C — `(u,v)` off the raw `PlaneAxes` basis with only the world AABB published.**
  Tradeoff: ❌ the AABB `mins/maxs` is a **world-space** box; on a non-axis-aligned wall `(maxs−mins)·û` ≠ the true u-extent. Illegible exactly where it matters.

**Our lean: 3B.** Leak discipline (C3, `portal_place_verb_plan.md:41`): publish the frame **uniformly on EVERY panel** (identical neutral style, no per-cell portalability mask, no salience) — the frame publishes the SIZE, never the answer. `TraceFirePortal` decides portalability at placement (`NOT_PORTALABLE`/`CANT_FIT`), same discipline as the `where` enum. Echo `placed_pos = finalPos` EXACTLY (an attractor's yank stays visible; the requested `(u,v)` is NOT echoed as truth).

**Two critiques you must weigh:**
1. **Extent-source plumbing gap.** The per-panel in-plane `umin/vmin/umax/vmax` **is** computed at `BspFilePanelSource.cpp:308-309` but **thrown away** (only cell indices `mincu/mincv` survive for sorting); only the world AABB is stored. So 3B needs the true extents *plumbed through clustering* — it is **not** a projection of the already-published AABB. "Ingredients already present" is an overstatement; the load-bearing scalar is currently discarded. (~40 LOC to keep it.)
2. **Floor/ceiling & oblique degeneracy.** `v̂` = "closest to +Z" is unstable when the normal is near-vertical (floors/ceilings — where *huge* panels most need `(u,v)`), and a VLM reading a fraction off a foreshortened/45°/seam-split plane is weakest exactly there. The published origin makes the **arithmetic** exact but the **perception** still eyeballed. This is a residual motor-tax-in-costume risk relocated from the verb into the VLM's vision — **unmeasured against any VLM.** If it doesn't beat the enum, `(u,v)` is dead weight on those panels.

### Axis 4 — LASER × PORTAL

**The load-bearing substrate answer: there are TWO beams. Do not conflate them.**
- **(A) The engine's `env_portal_laser` IS portal-native.** It transits the beam and re-emits from the linked mouth (proof: redirected beams spawn transient `env_portal_laser` segments at `(0,0,0)` each tick, `MacroExecutor.cpp:1063`). So `point_laser_target.m_bPowered` **already latches through the pair for free** — **the CONFIRM is free, zero new code.**
- **(B) Our harness `ComputeBeamSegment` (`LaserGeometry.cpp:28`) is portal-BLIND** — a single straight `IEngineTrace::TraceRay(MASK_OPAQUE)` that stops dead at the portal surface (a `prop_portal` is a solid brush to a bare trace; no `UTIL_Portal_TraceRay` in-harness). All our seat/reachability tooling inherits this blindness.

Design job is **entirely on side B**, and it's cheaper than it looks. Options:

- **Option 4A — entry-aim only (redirect_to-through-portal), no poly-line.** Pull the target BACK through the portal: `T' = A.backward_matrix.PointTransform(T)` (= `m_matrixThisToLinked` inverse-pairing, `EngineDemoPlayer.cpp:436`), then reuse **`ComputeRedirectYaw(seat, T')` VERBATIM** — the through-portal aim IS the flat aim at the pre-image of the target. Confirm via `m_bPowered` (free).
  Tradeoff: closed-form, ZERO new solver, does NOT need our trace to follow the portal. **Cannot** seat a cube on a *post-portal* leg (interpose on the far leg dies). ✅ ships regardless of the recon gate.
- **Option 4B — poly-line interpose (4A + `ComputeBeamSegmentThroughPortals`).** Straight-trace; on a `prop_portal` (`m_bActivated`) disc hit, `E' = M·hit`, `F' = M·F`, nudge off the exit plane by ε, recurse (~2–4 hops); return the leg poly-line. Reparameterize `interpose` `percent` from "along one segment" to **"arc-length along the whole poly-line."** ~40 LOC reusing the snapshotted `m_hLinkedPortal` + the VMatrix. This formalizes the user's "line from mark to visible portal ∩ laser line" idea: pull the target back → it lives in the incoming leg's frame → the seat is the on-ray closest-approach point.
  Tradeoff: full through-portal `interpose`, but **GATED on one unverified in-engine fact** (below).
- **Option 4C — user's manual line-intersection as the primary construction (no auto poly-line).**
  Tradeoff: explicit, less automatic, no dependence on the trace gate — the **fallback** if 4B's gate fails.

**Our lean: ship 4A now (free confirm + closed-form aim), gate 4B behind the recon, keep 4C as the fallback.**

**THE ONE RECON GATE (must close in-engine before building 4B):** does a bare `IEngineTrace::TraceRay` **REPORT a `prop_portal` hit** (in `tr.m_pEnt`, within the portalable disc not the frame) at the portal plane — or pass through invisibly? The engine RE-EMIT is proven; **what our low-level trace SEES at the plane is not.** If invisible, `ComputeBeamSegmentThroughPortals` can't detect the transit and 4B collapses to 4C.

**Second caveat:** `ComputeRedirectYaw` (`LaserGeometry.cpp:82`) zeros **roll** (`ang.z=0`) and produces yaw+pitch (note: the proposals repeatedly misstate this as "zeros roll/pitch" — it's roll only). Either way the cube-`+X` reflector cannot realize arbitrary pre-image directions for **non-coplanar** portal pairs (floor↔wall, floor↔ceiling). **The closed-form reuse is wall↔wall only**; floor/ceiling needs the full-rotation aim path (deferred). Also undesigned: the cube→A entry-leg obstruction + portalable-disc-hit check (the beam must hit A's ~32×56u oval, not the frame).

**Leak/smuggle to weigh:** 4A/4B do the through-portal aim *inside the verb* — the model names `(cube, target)` and the actuator solves the routing. Whether that's the intended abstraction altitude or a **too-smart verb that solves the fun part** (the "aim into the portal, it comes out over there" insight) is a genuine design question. Guardrail: keep `m_bPowered` as a **post-act** result only (don't surface it before the model commits), and hold that the model chose the portals.

### Axis 5 — PORTAL NAMING / MARKS

Today placed portals are effectively invisible: `prop_portal` **is** in `kClassColors` (`PuzzleAnnotate.cpp:47`, blue / orange-if-`m_bIsPortal2`) but the **(0,0,0) origin-reject** (`PuzzleAnnotate.cpp:85-89`) drops it — which **correctly** kills the 2 inactive gun-slot ghost portals the gun pre-creates at origin. A *settled* placed portal has a real `abs_origin` and would get a generic integer entity mark today, with no color/linkage legibility.

- **Option 5A — name by COLOR** `blue|orange`, resolved via `server->FindPortal(linkage, color, active)` (the `PlacePortal` readback idiom, `MacroExecutor.cpp:1437`). Percept draws a **P-namespace overlay** (`Pb`/`Po`, or reuse the existing blue/orange box) so the model *sees* which mouth is which + linkage; the **verb argument stays the color.**
  Tradeoff: matches how `place_portal` places + how the engine links; ≤2 mouths per color; sidesteps entity-mark-namespace churn entirely. **No story for >2 linkages / multi-pair chambers** (rare in PeTI v0). ✅
- **Option 5B — name by generic entity mark (integer).**
  Tradeoff: future-proofs multi-pair, but churns the entity-mark namespace and reads unnaturally ("`pass_through(37)`"). ❌ for v0.
- **Option 5C — dedicated P-mark integer namespace (`P1..Pn`), separate table.**
  Tradeoff: multi-pair-safe AND legible, but adds a whole third table/lifecycle for a case v0 doesn't have.

**Our lean: 5A.** The fix to make live portals visible: **keep** the (0,0,0) reject (kills ghosts), **gate portal-naming on `m_bActivated==true`** (the deliberate `prop_portal` carve-out from the blanket "never `m_bActivated`" rule — for portals it reliably means "live placed"), read pose via `server->GetAbsOrigin` (real face even when `abs_origin==0`) confirmed on a settle tick, color via `m_bIsPortal2`, linkage via `m_hLinkedPortal` (→ partner via `ReconReadHandleIndex`, `-1` until the pair settles — tolerate the one-portal transient; **use `m_hLinkedPortal != -1`, never `m_bActivated`, as the link signal**).

**Coexistence with S-surface-marks is orthogonal by construction:** S-marks name *unplaced* portalable **surfaces** (`place_portal` targets, `SurfaceMarkTable`, static per map); P/color names name *placed* portal **mouths** (`pass_through` / `launch(via)` / `redirect_to`-through targets, entity path). Different tables, lifecycles, render loops — no collision.

**Critique to weigh:** color handles are **mutable** — "re-placing a color moves it", so `@blue` rebinds silently. A plan that places blue, then re-places blue, invalidates prior `pass_through`/`launch`/`redirect_to` references with **no staleness signal**. Consider a versioning/"portal moved" percept flag.

### Axis 6 — PERCEPT (portals cannot be designed apart from it)

Every one of the six asks is blocked on a percept addition. Design the verbs against these additions, not the blind percept.

- **(1) Portal marks + linkage** — un-exclude live portals (5A), publish per portal `{color, mouth_center, mouth_normal, linked_partner}`. `mouth_normal` IS the launch-vector info axis 1/2 need.
- **(2) Through-portal visibility** — three rungs, cheapest first:
  - **(a) Declarative linkage text** — "`Pb (blue) ↔ Po (orange); Po opens onto <region>`". Pure GLOBAL reasoning, leak-clean-*ish*, no render pass. ✅ v0.
  - **(b) Through-portal LOS-cull** — if the straight eye→target ray is blocked but passes through a live mouth, re-origin at the linked portal via `M` and continue the trace; tag survivors "(through Pb)". ~1 extra trace/portal/mark. Add only when a chamber needs "mark X reachable only through a portal".
  - **(c) Real through-portal label render** — DEFER. A VLM given SHM pixels already sees *through* the portal (Source composites the recursive view); only the symbolic labels are missing from the sub-view. A genuine render-hook project, out of scope for a text percept.
- **(3) Fling affordances** — currently INVISIBLE: publish a **DROP tag** per floor panel (cheap DownTrace found a fall > threshold → launch source), and **launch direction + magnitude** for faith-plates + gel bounce normals. **This is the real fix for axis-2's `launch(via)` under-specification.**
- **(4) Canonical `(u,v)` frame fields** — `origin, u_axis, v_axis, u_extent, v_extent` on `SurfaceMark`, uniform across all panels (leak-safe).
- **(5) Gel / paint map** — **TOTALLY ABSENT** (no `info_paint_sprayer`/paint read anywhere in `src/Features/Harness/`). Blocks `paint()` confirm AND the repulsion-gel `launch` terminal AND propulsion-navigate-speed AND conversion-gel-into-`place_portal`. **The single largest percept debt.**

**Two leak risks to weigh:**
- **(2a)/(2b) can leak the answer.** "`Po opens onto <region>`" where `<region>` names the goal, or the "(through Pb)" tag, hands the model the **routing insight** a through-portal puzzle is meant to test. Same C3 class the `(u,v)` design carefully avoids. **Constrain what `<region>` may say** (geometry, not goal-adjacency) before shipping (2a).
- **(3) DROP tag salience.** "This floor is a launch source" is a salience hint if only *some* floors carry it. C3-safe only if **every** floor panel carries its true drop distance uniformly (then it's geometry, not answer-key).

---

## 3. The finalist grammars — top 3 side by side

Ranked by summed judge score (frozen-LLM legibility · human palatability · local/global cut · coverage · feasibility):

| Rank | Grammar | Σ | Optimizes for |
|---|---|---|---|
| 1 | **minimal-orthogonal** | 34 | Fewest primitives; both user rejections derived from one principle; laser = flat-solver on a pulled-back target |
| 2t | **explicit-mechanic** ("one verb per named mechanic") | 31 | Max coverage + max human-nameability (1:1 verb↔noun-phrase) |
| 2t | **Percept-First (PFP)** | 31 | "No verb arg may name what the percept doesn't publish" — drivability invariant |

(For reference, the tail: GETTO mode-fence 30 · UTAG typed-args 27 · geometry-primitive-unified 26 · human-baseline-speedrunner 14.)

### Verb tables (the axis-relevant deltas; shipped verbs `navigate/pick_up/release/press/ride/aim_at/look/wait/done` unchanged in all three)

**Finalist 1 — minimal-orthogonal** (adds 2 verbs: `pass_through`; `launch`; `(u,v)` is a zero-new-verb `where`-variant; laser = zero new solver)

| Verb | Kind | Signature → returns |
|---|---|---|
| `pass_through` | puzzle-adjacent | `pass_through(portal: blue\|orange)` → `TRANSITED(cell) \| NO_SUCH_PORTAL \| UNLINKED \| NOT_AT_MOUTH \| BLOCKED \| DIED` |
| `launch` | both | `launch(via: mark)` → `LANDED(cell) \| LANDED_SHORT(cell) \| NO_MOMENTUM \| DIED` — no `toward` |
| `place_portal` | puzzle | `place_portal(color, surface: S-mark, where: CENTER\|…\|corner \| uv(u,v) = CENTER)` → `{result, placed_pos, placed_angle, placed_surface, used_helper}` |
| `redirect_to` | puzzle | `redirect_to(object, target)` → `POWERED \| NOT_POWERED \| NOT_SEATED` — target may be through-portal (pull-back T', reuse `ComputeRedirectYaw`) |
| `interpose` | puzzle | `interpose(object, emitter, percent)` → `ON_BEAM \| NO_REACH \| NO_BEAM \| OCCUPIED` — `percent` = poly-line arc-length |
| `power_with` | puzzle | `power_with(object, target)` → union (pure composition) |
| `paint` | puzzle | `paint(gel, surface: S-mark)` → `PAINTED \| NOT_PAINTABLE \| NO_SPRAYER` — **gated on unbuilt paint percept** |

**Finalist 2 — explicit-mechanic** (adds 3 momentum/transit verbs; splits portal-fling from plate/gel)

| Verb | Kind | Signature → returns |
|---|---|---|
| `pass_through` | — | `pass_through(portal: blue\|orange)` → `TRANSITED(cell) \| NO_PORTAL(color) \| NO_REACH \| BLOCKED_MOUTH \| DIED` |
| `jump_through` | both | `jump_through(portal: blue\|orange, via: mark=none)` → `LANDED(cell) \| LANDED_SHORT(cell) \| NO_MOMENTUM \| NO_PORTAL(color) \| DIED` — portal-fling |
| `launch` | both | `launch(via: mark)` → `LANDED(cell) \| LANDED_SHORT(cell) \| NO_MOMENTUM \| NO_LAUNCHER \| DIED` — plate/gel only |
| `place_portal` | puzzle | `place_portal(color, surface, where: enum \| uv(u,v) = CENTER)` → `PLACED(P-mark,pos,angle) \| NOT_PORTALABLE \| CANT_FIT \| OVERLAP \| NO_LOS \| FIZZLED` |
| `redirect_to` | puzzle | `redirect_to(object, target: mark\|portal)` → `POWERED \| NOT_POWERED \| NOT_SEATED \| NO_PORTAL_PATH` |
| `interpose` | puzzle | `interpose(object, emitter, at: percent\|mark)` → `ON_BEAM \| NO_REACH \| NO_BEAM \| OCCUPIED` — poly-line |
| `power_with` | puzzle | `power_with(object, target: mark\|portal)` → union |
| `paint` | puzzle | `paint(gel, surface)` → `PAINTED \| NOT_PAINTABLE \| NO_SPRAYER` — **gated** |

**Finalist 3 — Percept-First (PFP)** (verb set ≈ minimal-orthogonal; organizing rule = the percept invariant)

Same verb surface as Finalist 1 (single `launch(via)`, `pass_through(color)`, `place_portal` with `uv`, poly-line `interpose`, pull-back `redirect_to`), **plus one hard rule:** *no verb argument may reference anything the percept does not already publish.* This forces every axis to declare its percept dependency; `interpose(at: percent|mark)` additionally accepts a `mark` (beam point nearest a mark) alongside the arc-length scalar.

### What each optimizes · judge scores · key critiques

**Finalist 1 — minimal-orthogonal.** *Optimizes:* fewest wrong choices for a VLM; both user rejections fall out of one principle (the puzzle bit must be a first-class model act); the elegant collapse — laser-through-portal is the flat solver on a pulled-back virtual target, +40 LOC, **zero new solver**. *Scores:* legibility 7 · palatability 5 · cut **9** (highest) · coverage 6 · feasibility 7. *Key critiques:* (a) `power_with` kept as pure composition **contradicts its own minimalism thesis** — should be cut. (b) `(u,v)` echo (`placed_pos=finalPos`) enables a **retry hill-climb** the grammar bans for `launch` — motor-tax through the honest-echo channel. (c) the redirect pull-back **does the through-portal aim geometry inside the verb** (too-smart-verb, unconfronted). (d) mouth-hunting march reintroduces some motor-tax. (e) least-fun for a human (auto-solves the "aim into the portal" insight).

**Finalist 2 — explicit-mechanic.** *Optimizes:* the most COMPLETE axis coverage (only proposal giving flings **first-class** treatment — `jump_through` owns the floor-portal fall-through case that Finalist 1 leaves homeless) and the most human-nameable surface (1:1 verb↔"what a player says out loud"). *Scores:* legibility 5 · palatability **8** (tied-highest) · cut **4** (lowest) · coverage **8** (highest) · feasibility 6. *Key critiques:* the `jump_through` vs `launch` split is the crux liability — **identical projectile physics under two verbs**, so (a) the verb NAME encodes which mechanic solves the room (**puzzle-smuggling**), (b) `jump_through(color, via=@strip)` **overlaps** `launch` → same solution, two encodings → **non-deterministic scoring**. Humans genuinely experience a self-authored fling differently from a plate (the palatability defense), but a frozen VLM can't ground the choice from percept. Everything else (naming, laser, uv) is substrate-honest.

**Finalist 3 — PFP.** *Optimizes:* frozen-VLM **drivability-by-construction** — the "no arg names an unpublished percept field" rule is literally the drivability invariant, and it does the most actual WORK on the `(u,v)` legibility (treats the origin-less-basis as the deliverable). *Scores:* **legibility 8** (highest) · palatability 5 · cut 6 · coverage 7 · feasibility 5. *Key critiques:* (a) `interpose(at: percent)` reintroduces a **continuous steer-by-number scalar on the beam** — motor-tax-in-costume WITHOUT the declarative-frame justification it demanded for placement (self-inconsistent). (b) `redirect_to`/`power_with` **smuggle** the through-portal aim, and the "(through Pb)" LOS tag **leaks the routing answer** — the two leaks must be closed before it's an eval. (c) mutable color handles rebind silently. (d) by its own honest accounting `launch`/`paint`/half-of-laser are ungroundable this session.

---

## 4. Implementation sketch per finalist

Shared substrate reuse (all three, **zero new engine offsets** — every field name-resolved via `SE(ent)->field<T>()`, all already read in-tree): `m_matrixThisToLinked` (`EngineDemoPlayer.cpp:435-436`), `m_bActivated`/`m_bIsPortal2`/`m_hLinkedPortal` (`PuzzleAnnotate.cpp:190,488`), `m_vecVelocity` (`MacroExecutor.cpp:1718`), `PlaneAxes`/`PlanePoint` (`BspFilePanelSource.cpp:209,215`), `ComputeRedirectYaw` (`LaserGeometry.cpp:82`), `ComputeBeamSegment`/`DownTraceRest` (`LaserGeometry.cpp:28,67`), `FindPortal` (`MacroExecutor.cpp:1437`), `TraceFirePortal` (`MacroExecutor.cpp:1433`).

**The hardest piece is the same for all three:** the laser-portal **recon gate** (does a bare `TraceRay` report a `prop_portal` hit at the plane?). It's a ~0-LOC-ship recon command that MUST pass before the poly-line `interpose` is buildable, with a clean fallback (entry-aim-only `redirect_to` + free `m_bPowered` confirm) that ships real value even if it fails.

### Finalist 1 — minimal-orthogonal (phase order = honest ship order)

- **P0 RECON** (~30 LOC throwaway): laser-portal trace gate. *Blocks P7.*
- **P1** (C++): canonical `(u,v)` frame in `BspFilePanelSource::ClusterPanels` — keep the discarded `umin/vmin`, derive gravity frame. ~40 LOC.
- **P2** (proto+C+++py): publish `origin/u_axis/v_axis/u_extent/v_extent` on `SurfaceMark`; `where`↔`uv` resolver; mirror in `_panel_dict`; update `agentloop_smoke.py`. ~60 LOC.
- **P3** (C++): `place_portal` `where=uv(u,v)` variant through the P2 resolver → unchanged `TraceFirePortal`. ~40 LOC.
- **P4** (C++): live portal marking + linkage percept (`m_bActivated` gate, `GetAbsOrigin`, P-mark overlay). ~90 LOC. *Manual visual check.*
- **P5** (C++): `pass_through(color)` — `FindPortal` → `MarchTo` → engine transit → verify signature. ~90 LOC.
- **P6** (C++): `redirect_to`-through-portal (Option 4A) — pull-back T', reuse `ComputeRedirectYaw`, free `m_bPowered` confirm. Wall↔wall only. ~50 LOC. *Gated on P0 for the transit-detect; 4A itself needs only the confirm.*
- **P7** (C++): poly-line `interpose` (Option 4B) — `ComputeBeamSegmentThroughPortals`. ~80 LOC. **Gated on P0**; fallback = user's line-intersection (4C).
- **P8** (C++): fling-affordance percept (drop tags + launch vectors). ~70 LOC.
- **P9** (C++): `launch` confirm predicate (FL_ONGROUND + DIED terminal + settle bisection) + faith-plate terminal ONLY. ~120 LOC. **Multi-session.**
- **P10** (py): grammar surface + percept text + prompts.

**LOC:** ~450–600 C++ net + ~150–250 py. **Honestly groundable this session ≈ P0–P7 (~400 LOC).** P8/P9 (launch) + gel are multi-session (confirm predicate + paint percept unbuilt). **Reuse:** heaviest — laser = zero new solver, transit = zero harness code.

### Finalist 2 — explicit-mechanic

Same P0/P1/P2/P4/P5/P6/P7 spine as Finalist 1. **Extra:** the momentum half is bigger because it's **two** verbs:
- **P8** (C++): momentum confirm *infra* (on-ground, DIED terminal, settle bisection, jank budget) — prerequisite, not a verb. ~150 LOC.
- **P9** (C++): `jump_through(color, via)` **+** `launch(via)` after P8. ~180 LOC. Portal-fling first; faith-plate needs only P8; gel BLOCKED.

**LOC:** ~600–800 C++ + ~150 py. Shippable-this-session (P0–P7) ≈ 450 C++ / 120 py; momentum half (~330 C++) designed-but-ungroundable. **Hardest piece adds:** disambiguating `jump_through` vs `launch` at the grammar/scoring layer (the overlap is a spec problem, not just LOC).

### Finalist 3 — PFP

Same spine + LOC profile as Finalist 1 (single `launch`). **Extra work is the two leak-closures, not code volume:** (1) constrain the through-portal visibility text/tag so it can't name goal-adjacency (axis 6 leak); (2) re-justify or replace `interpose(at:percent)` so it isn't a bare steer-by-number (make it declarative-from-published-beam-geometry, or restrict to `at:mark`). Plus a portal-handle staleness signal for the mutable-color-rebind hazard. **Hardest piece:** same recon gate; the differentiator is percept-discipline enforcement, which is process not LOC.

---

## 5. The crux decisions you must make (enumerated)

1. **`launch` — one verb or split?**
   Options: 2A single `launch(via)` (Finalists 1, 3) · 2B `launch` + `jump_through` (Finalist 2). **Our lean: 2A** — the split smuggles the mechanic into the verb name and creates a two-encoding scoring ambiguity; the `via` arg already carries the source distinction. *You may override if human-baseline naturalness (a self-authored fling ≠ a passive plate) outweighs VLM scoring determinism.*

2. **Fractional `(u,v)` frame — ship it, and on which panels?**
   Options: 3A enum-only · **3B gravity-anchored declarative `(u,v)` + enum-as-sugar** (all finalists). **Our lean: 3B**, but **honestly flag** the floor/ceiling/oblique degeneracy is unmeasured against a VLM — decide whether to ship `(u,v)` on *all* panels or **walls-only** for v0 and keep the enum on floors. Also decide: accept the retry-hill-climb risk the honest echo enables, or forbid re-fire on the same `(u,v)`.

3. **Laser-through-portal — how far this session?**
   Options: 4A entry-aim-only (ships now, free confirm, no far-leg seat) · 4B + poly-line `interpose` (gated on the recon) · 4C manual line-intersection (fallback). **Our lean: ship 4A now, gate 4B on P0, keep 4C as fallback.** Decide up front: **is the through-portal aim-inside-the-verb the right altitude, or a too-smart verb** that solves the puzzle's fun? (Affects whether `redirect_to(@portal)` should instead just aim at the *mouth* and let the model reason the exit.)

4. **Portal naming — color, or a mark namespace?**
   Options: **5A by color** (all finalists) · 5B entity mark · 5C dedicated P-mark integers. **Our lean: 5A** for v0. Decide whether the **>2-pair / multi-linkage ceiling** matters for your chamber corpus; if it might, pre-reserve 5C. Decide whether to add a **portal-moved staleness flag** for the mutable-handle hazard.

5. **Through-portal visibility — which rung, and how constrained?**
   Options: (a) linkage text · (b) LOS-cull tag · (c) render labels. **Our lean: (a) v0, (b) on demand, (c) deferred.** **Crux sub-decision: constrain `<region>` text + the "(through Pb)" tag so they don't leak the routing answer** (the puzzle) — decide the exact vocabulary allowed (pure geometry vs goal-adjacency).

6. **Ship order / scope of THIS session.**
   Options: (i) spine-only this session (naming + percept + `pass_through` + `uv` + laser-4A/4B) and defer all momentum · (ii) spine + faith-plate `launch`. **Our lean: (i)** — `pass_through` → laser → fractional-uv → naming are cheap, zero-offset, reuse shipped code; **`launch` (any terminal) waits on the DIED terminal + confirm predicate** because shipping it before those exist means it **gaslights the model**. Decide if faith-plate `launch` is worth the P8 confirm-infra investment now.

7. **Close the recon gate first (non-negotiable prerequisite, but you time it).**
   The single unverified in-engine fact (bare `TraceRay` reports a `prop_portal` hit at the plane) blocks the poly-line `interpose` in ALL three finalists. Decide: run P0 before committing to 4B, or ship 4A-only and defer 4B entirely.

---

## 6. What we are NOT cutting (and what is HONESTLY deferred as unbuilt-terminal)

**Nothing is scoped down.** All six axes are designed and covered by the finalists. Per the mandate, *the user decides when to cut, not us.* But the following are **honestly flagged as designed-but-not-groundable this session** — they are on the table, not removed, and each names its blocker:

- **`launch` confirm predicate (FLINGS, axis 2) — UNBUILT terminal.** Blockers: `FL_ONGROUND` read (not plumbed) · **DIED/respawn terminal (HARD BLOCKER — silent goo respawn gaslights the model)** · settle-tolerance constant (needs bisection) · body-jank re-roll budget (unmeasured). The `launch` *verb and unification are designed*; only the faith-plate terminal is near-buildable (needs the confirm infra), portal-fling waits on the confirm predicate + jank budget, **gel-bounce waits on the paint percept.**
- **Gel / paint percept (axes 2, 6) — UNBUILT SUBSYSTEM, the largest debt.** No `info_paint_sprayer`/paint-map read exists anywhere in `src/Features/Harness/`. This blocks `paint()` confirm, repulsion-gel `launch`, propulsion-navigate-speed, and conversion-gel-into-`place_portal`. The whole gel family is **designed but ungroundable** until a new percept subsystem ships. **Flagged: the most speculative slice; do not commit gel this session.**
- **"Faith/gel percept" and fling-affordance percept (axis 6) — UNBUILT.** Drop tags + launch vectors + gel bounce normals are the *real* fix for `launch(via)`'s under-specification (not a `toward` arg). Designed (~70 LOC), not yet built. Until it lands, `launch` asks the model to reason about momentum it cannot see.
- **Laser-through-portal poly-line `interpose` (axis 4) — GATED, not cut.** Ships as Option 4A (entry-aim + free confirm) unconditionally; the far-leg seat (4B) is gated on the one recon fact, with 4C as the explicit fallback. **Nothing about axis 4 is removed** — the confirm is free and always works; only the far-leg *seat computation* is contingent.
- **Non-coplanar (floor/ceiling) portal laser aim (axis 4) — DEFERRED.** The closed-form `ComputeRedirectYaw` reuse is **wall↔wall only**; floor/ceiling needs the full-rotation aim path. Designed as a follow-on, not cut.
- **Through-portal render-labels (axis 6, rung c) — DEFERRED.** A VLM's SHM pixels already show through the portal; only the *symbolic labels* in the recursive sub-view are missing. Text-percept agents don't need it; deferred, not removed. **Flag: this creates a pixel-vs-text percept mismatch** (a pixel VLM sees through the portal without labels; a text agent sees the linkage text) — an eval-fairness wrinkle to note if any arm is pixel-native.
- **`ride` (funnels) — GATED on `prop_tractor_beam` recon** (unchanged from prior; not part of this mandate but composes with the portal verbs).

**Two seams named but NOT closed (you should know they exist):** (1) the `pass_through` success predicate must generalize from "yaw ~180° flip" to "pose consistent with `M`" for non-antiparallel pairs; (2) the cube→portal-A entry-leg obstruction + portalable-disc-hit check is undesigned in the `interpose`-through-portal path.
