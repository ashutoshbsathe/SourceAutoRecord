# `place_portal` + surface enumerator — implementation plan

> **Status (2026-06-29): R1 decided PURE SHIP-RIGHT** ([portal_verb_recon_design.md §5](portal_verb_recon_design.md)).
> Build the named-panel verb `place_portal(color, surface, where)` with a world-brush surface enumerator.
> **Two arcs, both committed:** ARC A = offline sidecar (hand-built for 2-3 maps) to prove verb + percept +
> labels + ergonomics; ARC B = runtime in-engine enumeration that eliminates the sidecar. A pluggable
> panel-source seam lets B swap in behind the same registry/verb/percept/labels.

Goal verb:
```
place_portal(color: blue|orange, surface: mark, where: CENTER|TOP|BOTTOM|LEFT|RIGHT|corner = CENTER)
  -> PLACED | NOT_PORTALABLE | CANT_FIT | OVERLAP | FIZZLED | NO_LOS
```
The model names a portalable **panel** by an integer mark; the engine places the portal at the chosen anchor.

---

## Cross-cutting (settle first / hold throughout)

**C0. Proto is the shared spine — one `make proto`.** Two edits to `harness.proto`, regenerated together:
- `MacroRequest` (reserved slot ~`:84`): add `string color`, `int32 surface_mark`, `string where`.
- New `SurfaceMark` message + `repeated SurfaceMark surface_marks` on **`GameState`** (chamber-wide, not on
  `EntitySnapshot` — no delta-stream/`WorldView` merge). Fields: `int32 mark`, `Vector3 plane_normal`,
  `Vector3 center`, `Vector3 mins`, `Vector3 maxs`, `int32 anchor_flags`. **No `confirmed_portalable`** —
  the verb re-traces at placement; a percept flag would be dead (and a leak risk, C3).

**C1. Separate surface-mark namespace.** A parallel `SurfaceMarkTable` (mirrors `MarkTable`: persistent
assignment, forward/reverse maps, mutex) numbers panels from 1 in their **own** namespace, distinct from
entity marks. Percept + on-screen labels render panel marks with a distinguishing token (lean: an `S` prefix
— `S1`, `S2`; glyph tunable) so the VLM never conflates a wall panel with a cube. `place_portal`'s surface
arg resolves against this namespace; movement/manip verbs take entity marks. No band offset — the namespaces
are genuinely separate (own proto field + own table + own verb arg).

**C2. Panel source is pluggable (`IPanelSource`).** One seam: `EnumeratePanels(...) -> vector<PanelDesc>`
(plane, center, mins/maxs, anchor_flags). ARC A = `SidecarPanelSource` (load JSON); ARC B =
`BspWalkPanelSource` (runtime). `SurfaceMarkTable`, the proto fill, the verb, and the labels consume
`PanelDesc`/`SurfaceMarkTable` only — never the source. Swap = one pointer at session start.

**C3. Leak-safe uniform labels.** Expose **every** clustered white-tile panel uniformly — same style, same
fields — regardless of whether it is *currently* portalable. An enumerator that hides non-portalable panels
hands the model the answer key. Portalability is decided at placement time by the verb's own trace, never
surfaced as a percept hint.

**C4. Sidecar keying.** ARC A: **name-key** the 2-3 hand-built sidecars (we choose those maps). ARC B / general:
**BSP-CRC key** (name collides across variants; a `.bsp` can be renamed). Panel `id` assigned
deterministically (sort by plane key then min-cell, like `MarkTable`'s index/origin sort) so marks are stable
across runs and match between sidecar and runtime walk where geometry agrees.

**C5. L0 verb-build lessons (bake into `PlacePortal`).** `TraceFirePortal` previews only; the
`portal_place <linkage> <isPortal2> x y z p y r` console command commits. Prime the gun first
(`server->FindPortal(linkage, secondary, true)`). Settle N ticks, THEN confirm via `m_bIsPortal2` +
`m_hLinkedPortal` (**never `m_bActivated`**). `placed_pos = pinfo.finalPos` (fresh `prop_portal` reads
`abs_origin`=0 until settled). Map `ePlacementResult` → verb codes; `USED_HELPER` is a `PLACED` (note the
helper-snap in `detail`). Wrap every main-thread touch in `RunOnMainThreadSync` with `shared_ptr` captures
(the `Interpose`/`RedirectTo` idiom).

**Comment discipline for all new code:** terse, self-contained, zero doc/section refs.

---

## ▶ Resume here (2026-06-30) — enumerator spine DONE; next = the verb + Track B recon

**Shipped (A1–A6, commits `18a97e16`→`1f0ae091`):** the full offline→runtime enumerator spine. Sidecars
emit (`cluster_panels.py --emit`), `SidecarPanelSource` loads them via json11, `SurfaceMarkTable` rebuilds at
session start, `sar_harness_panels_dump` lists them in-game — **verified on two eval maps (27 + 5 panels,
origin-junk filtered).** Sidecars live in `artifacts/panels/`; set `sar_harness_panel_dir` to its abs path
before loading a map. The `IPanelSource` seam is in, so Arc B is a source swap.

**Tomorrow — two parallel tracks:**
1. **Arc A: A7→A12** — flow panels into the percept, add the grammar, build the `PlacePortal` verb. End state:
   `place_portal blue S1` drops a portal on a named panel in `macro_repl`. (Detailed in A7–A12 below.)
2. **Track B: B0 recon (PRIORITIZED)** — the runtime-walk feasibility spike; start with
   `sar_harness_bsp_face_probe` (route (b) trace-based first). (Detailed in Arc B below.)

Loose end: the dual-role eval map (`17093866141393312246`) isn't in the BSP corpus, so its sidecar needs a
`dump_ents.py --geometry` run on its `.bsp` before A14.

---

## ARC A — offline sidecar; prove verb + percept + labels + ergonomics

SAR/C++ before Python within each cluster. Each phase = one focused edit + a 1-line verify.

- **A0 — eval maps (LOCKED):** the last 3 of `map_candidates.txt` — `workshop/17093866141393312246/1782237070`,
  `workshop/927053855830294446/1522623535`, `workshop/596996616964103777/1361778957`. Some have **no**
  portalable surface — deliberately, to exercise the empty-panel-set / `NOT_PORTALABLE` path. *(no code)*
- **A1 ✅** (`18a97e16`) proto: `SurfaceMark` + `surface_marks` on `GameState`; `color`/`surface_mark`/`where`
  on `MacroRequest`. `make proto` regenerated; both sides import + build.
- **A2/A3 ✅** (`1b0173a1`) `PanelDesc` + `IPanelSource` (`PanelSource.hpp`) + `SurfaceMarkTable.{hpp,cpp}`.
- **A4 ✅** (`8d24a8b4`, `1f0ae091`) `cluster_panels.py --emit <dir> [--only <ids>]` emits per-map panel
  sidecars (world geometry + deterministic 1..N marks); origin-junk filtered.
- **A5 ✅** (`66d6dd62`) `SidecarPanelSource` loads `sar_harness_panel_dir/<map>.json` via json11.
- **A6 ✅** (`35223ed4`) session-start rebuild + `sar_harness_panels_dump`. **Verified in-game: 27 + 5 panels.**

### A7–A12 — flow panels to the model + build the verb (tomorrow)

- **A7 — fill `SurfaceMark` protos.** In `Portal2HarnessImpl::InternalObserve` (~`:384`, after the
  `PopulateEntityStateProto` loop): `for (auto& p : surfaceMarkTable.Panels()) { auto* sm =
  state->add_surface_marks(); sm->set_mark(p.mark); <set plane_normal/center/mins/maxs from Vector>;
  sm->set_anchor_flags(p.anchorFlags); }`. Reuse the existing `Vector`→`Vector3` setter used for `position`.
  Full list every observe (panels static; O(#panels)). *Verify:* `agentloop_smoke` prints non-empty
  `state.surface_marks` on an eval map.
- **A8 — Python percept.** In `entities.py` `WorldView.observe` (~`:110`), after the entity marks: per
  `state.surface_marks` entry append `{ 'mark': f'S{sm.mark}', 'class': 'wall_panel', 'name': '',
  'pos': (c.x,c.y,c.z), 'dist': …, 'bearing': … }` (dist/bearing from the player, like entity marks). The
  `S`-prefixed token is the panel's own namespace (C1); merge into the sorted percept, uniform style (C3 — no
  portalability hint). *Verify:* `agentloop_smoke` percept lists `S1…SN` beside int entity marks.
- **A9 — grammar spec.** `macro_grammar.py` `VERB_SPECS`: `'place_portal': Verb(doc='Place a portal of the
  given color on a named wall panel.', example='place_portal blue S1', mark='required')` (confirm how the
  `mark` field validates an `S`-prefixed panel mark vs an int entity mark). *Verify:* `percept_grammar_smoke`
  validates the example.
- **A10 — grammar parse + validate.** `build_macro`: `elif verb=='place_portal': m.color=args[0];
  m.surface_mark=int(args[1].lstrip('S'))`. `_check_place_portal(req, by_mark)` (mirror `_check_redirect`):
  `color∈{blue,orange}`; the `S`-prefixed surface arg resolves to a `wall_panel` in the percept. Wire into
  `validate` + `_signature`. `where` deferred. *Verify:* bad color / unknown `S`-mark → structured reject,
  no game step.
- **A11 — dispatch.** `MacroExecutor::Execute` (~`:1099`): `if (verb=="place_portal") return PlacePortal(req);`.
  Add `MacroResult PlacePortal(const MacroRequest&);` to the private decls in `MacroExecutor.hpp` (after
  `RedirectTo`). *Verify:* builds; `place_portal` no longer `NOT_IMPLEMENTED`.
- **A12 — `PlacePortal` core (the meaty one).** Template off `Interpose` (`MacroExecutor.cpp:1160`); the
  actuator mirrors `sar_harness_portal_fire_spike` (`PuzzleAnnotate.cpp:780-850`). All engine touches inside
  one `RunOnMainThreadSync` with a `shared_ptr` out-capture:
  1. **Resolve:** `surfaceMarkTable.GetPanelFromMark(req.surface_mark(), &panel)` → `BAD_MARK` if missing.
     `req.color()`: `blue`→secondary=false, `orange`→secondary=true, else `BAD_ARG`. Anchor = `panel.center`.
  2. **Prime gun:** player `active_weapon` → `IsPortalGun`; `linkage = m_iPortalLinkageGroupID`;
     `FindPortal(linkage, secondary, true)` + seed `m_hPrimaryPortal`/`m_hSecondaryPortal` (fire-spike idiom);
     `NO_GUN` if absent.
  3. **Preview:** `origin = panel.center + panel.planeNormal*10`, `dir = -panel.planeNormal`;
     `TraceFirePortal(gun, origin, dir, secondary, 2, pinfo)`.
  4. **Result gate (no commit on fail):** `res=pinfo.ePlacementResult`. `res>BUMPED` →
     `INVALID_*`/`PASSTHROUGH`→`NOT_PORTALABLE`, `CANT_FIT`→`CANT_FIT`, `OVERLAP_*`→`OVERLAP`,
     `CLEANSER`→`FIZZLED`; `ret==0`/trace-miss → `NO_LOS`.
  5. **Commit:** `engine->ExecuteCommand("portal_place <linkage> <secondary?1:0> finalPos.xyz finalAngle.xyz")`.
  6. **Settle + confirm:** `AdvanceTicksBlocking(kSettle)`; `FindPortal(linkage, secondary, false)` → read
     `m_bIsPortal2` (color sanity) + `m_hLinkedPortal`. `placed_pos = pinfo.finalPos`.
  7. **Result:** `SUCCESS`/`USED_HELPER`/`BUMPED` → `PLACED` (`USED_HELPER` noted in `detail`).
  *Verify:* `macro_repl` `place_portal blue S1` → `PLACED` + a blue portal at the panel center; a panel on a
  non-portalable surface → `NOT_PORTALABLE`; bad mark → `BAD_MARK`.
- **A13 — panel labels** (reuse `PuzzleAnnotate` legibility): in `RENDER`, after the entity loop, per panel:
  LOS via `MarkVisible` (panel center, null entity — verify `SkipTwoEntities` tolerates null), `InFrame`,
  `OverlayRender::addText(clamp=true)`. Uniform style (C3). *Verify:* `sar_harness_annotate 1` → every panel
  shows its mark, none flagged by portalability.
- **A14 — sidecars for the A0 maps** via the A4 emitter. *Verify:* each loads, labels, round-trips in `macro_repl`.
- **A15 — `agentloop_smoke` round-trip** for `place_portal` (proto + percept + result codes). *Verify:* smoke passes.
- **A16 — ergonomics pass.** Drive a scripted episode per eval map; confirm codes, no mark collision
  (entities <100, panels ≥100), no oracle leak. Tune the verb/percept wording. *Verify:* clean episode.

---

## ARC B — runtime in-engine enumeration; eliminate the sidecar

Reuses the entire ARC A spine (same `IPanelSource`, `SurfaceMarkTable`, proto, verb, labels). Differs only in
the panel source. **B0 front-loads the recon and gates B1+; ARC A never waits on it.**

- **B0 — recon spike (read-only; ⭐ PRIORITIZED — start the next session here).** SAR is trace-based today;
  no BSP-face walk exists. Decide the route by spiking both, cheaper first:
  - **Route (b) trace-based — try FIRST (likely cheaper, no offset RE).** `CGameTrace` already carries
    `surface` (`csurface_t`: `name`=material, `flags`) and `worldSurfaceIndex` (`Trace.hpp:103-111`).
    **First task: write `sar_harness_bsp_face_probe`** (read-only, mirror the laser/portal recon cmds): trace
    at the crosshair, print `surface.name`, `surface.flags & SURF_NOPORTAL`, `worldSurfaceIndex`,
    `plane.normal`/`dist`. Then the open question — can we *enumerate*? Probe whether `worldSurfaceIndex` is a
    dense iterable range (walk it directly) or whether we discover faces via a **coarse one-time ray sweep at
    chamber load** + dedup by `worldSurfaceIndex`. If a load-time sweep recovers the sidecar's face set, (b) wins.
  - **Route (a) raw BSP-lump walk — fallback (only if (b) can't enumerate).** `entityList->GetEntityInfoByIndex(0)`
    (worldspawn) → `ICollideable::GetCollisionModel()` → `model_t` (`ICollideable.hpp:22-31,45`) →
    `model_t->fnHandle` → the brushmodel/`dmodel_t` header → `dface` + `texinfo` arrays. Needs per-build offset
    RE for the lump pointers + material-name resolution from `texinfo` (the fiddly part).
  **Deliverable:** the probe command + a written verdict (route + cost) appended here. **Gate:** if both
  exceed budget, ARC A ships standalone and ARC B reschedules — ARC A never waits on B0.
- **B1 — `SurfaceEnumerator`** (`.{hpp,cpp}`): the chosen B0 route → portalable white-tile faces → cluster
  (reuse `cluster_panels.py`'s plane-key/128u-cell/connected-components logic, ported to C++) → `vector<PanelDesc>`,
  identical shape to the sidecar. Replicate the emitter's **origin-junk filter** (drop panels whose center is
  at `(0,0,0)` — the PeTI puzzlemaker's origin-instance geometry). *Verify:* `sar_harness_panels_dump` matches the sidecar.
- **B2 — `BspWalkPanelSource`** behind `IPanelSource`: call `SurfaceEnumerator` at load; band marks with the
  same sort key as ARC A (C4). *Verify:* builds.
- **B3 — source swap behind a cvar** (`sar_harness_panel_source 0=sidecar 1=bspwalk`). Everything else unchanged.
  *Verify:* `sar_harness_panel_source 1; map <eval>` → same labels + same `place_portal` results as the sidecar.
- **B4 — cross-validate** walk vs sidecar on the A0 maps (panel count, planes, anchors, marks). Reconcile
  clustering edge cases (coplanar jitter; `func_brush` excluded — same caveat as the offline census). *Verify:*
  `agentloop_smoke` percept is mark-for-mark equivalent between sources.
- **B5 — default to bspwalk**, keep `SidecarPanelSource` compiled as fallback/oracle for new maps. *Verify:*
  eval maps run end-to-end with no sidecar files present.

---

## Sequencing
- **A1 (proto) blocks both arcs** — first, regen once.
- **A2 (`IPanelSource`) is the integration contract** — once A7/A8/A12/A13 consume `SurfaceMarkTable`+`PanelDesc`
  only, ARC B is a source swap (B3) with zero churn to verb/percept/labels.
- **B0 runs concurrently with A2-A16**; its gate only decides whether B1+ proceed now or later.

## Open / deferred
- **`where` enum — DEFERRED.** v0 places at panel center; the `string where` proto field stays reserved. Add
  the CENTER/TOP/… anchor-offset table + grammar arg when a chamber needs sub-panel precision.
- **Dynamic-portalability** (gel-flip / flip-panel mid-chamber) — out of v0 scope; the verb's placement-time
  trace already handles it per-call, so no percept change unless a chamber needs live hints.

*Provenance: 4-agent subsystem grounding (percept/proto flow, verb dispatch, label legibility, Opt2 BSP-access)
+ architect synthesis, 2026-06-29, refined against the L0 recon lessons and the M=0.678 census.*
