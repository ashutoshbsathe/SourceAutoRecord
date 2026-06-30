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

## ▶ Resume here (2026-06-30) — Track B (BSP-file enumerator) at B6; sidecar removal next

**Track B SHIPPED B0–B6** (commits `bee1ede8`→ this one). The runtime enumerator no longer needs the offline
sidecar: `BspFilePanelSource` parses the map's `.bsp` file in-engine (VBSP v21, uncompressed, zero new deps),
reconstructs portalable white-tile faces, and clusters them via the `cluster_panels` port — **mark-for-mark
identical to the sidecar on `1361778957` (27 panels).** `PanelSession` now wires it as the live source. Trace
enumeration (route b) was rejected; the full reasoning + B-series detail is below (§B-series, §B0 STATUS).

**Track B COMPLETE (B0–B8).** The runtime enumerator is fully self-contained: `BspFilePanelSource` parses the
map `.bsp` in-engine, no sidecar, no offline precompute. Verified 27 + 5 on the two eval maps. Sidecar deleted;
`artifacts/panels/*.json` kept (unused) per user.

**Next: Arc A — the actual `place_portal` verb (A7→A16),** now fed by the BSP source:
1. **A7** — fill `SurfaceMark` protos in `InternalObserve` from `surfaceMarkTable.Panels()`.
2. **A8** — Python percept: `S`-prefixed panel marks into `WorldView.observe`.
3. **A9/A10** — grammar spec + parse/validate for `place_portal blue S1`.
4. **A11/A12** — dispatch + the `PlacePortal` core (template off `Interpose` + the `fire_spike` actuator).
5. **A13–A16** — panel labels, smoke round-trip, ergonomics. End: `place_portal blue S1` drops a portal in `macro_repl`.

Loose end: the dual-role eval map (`17093866141393312246`) has no portals — skipped (no parse needed).

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

- **B0 — recon spike (read-only; ⭐ PRIORITIZED).** **STATUS 2026-06-30: probe + sweep BUILT, awaiting the
  in-game run.** `sar_harness_bsp_face_probe` (single crosshair, `MASK_SOLID` so grates/glass register) +
  `sar_harness_bsp_face_sweep` (coarse hemisphere of rays, `MASK_SHOT_PORTAL`, portalable world-brush hits →
  distinct `worldSurfaceIndex` / planes / 128u cells vs the sidecar panel count), both in `PuzzleAnnotate.cpp`;
  `sar.so` builds. **Route (a) STRUCK by the grounding pass:** a 4–6 per-build-offset RE project that only sees
  static worldspawn brushes, and `py/bsp_recon` already does the exact parse offline — if route (b) can't
  enumerate, the real fallback is *keep the shipped sidecar*, not RE `model_t`. Run protocol: set
  `sar_harness_panel_dir` → `artifacts/panels`, load the 27-panel eval map, `sar_harness_panels_dump` (sanity),
  `bsp_face_probe` on white wall / black wall / grate, then `bsp_face_sweep`. **Crux unknowns the run closes
  (→ verdict in B0.2):** is `worldSurfaceIndex` nonzero + stable-per-face + a small dense range (→ walk it
  directly)? does `SURF_NOPORTAL` cleanly split white vs black at runtime (→ usable sweep filter)? does a
  single-point sweep's distinct-cell count approach the 27 sidecar panels (→ coverage)?
  **PROBE RAN 2026-06-30 (1361778957) — route (b) signals GREEN:** `SURF_NOPORTAL` cleanly splits
  white(`0x0800`,noportal=0)/black(`0x0820`,noportal=1); `surface.name` is the real VMT (`tile/white_wall_tile003a`,
  `metal/black_wall_metal_002c`) so the offline `'white'+'tile'` material filter ports directly;
  `worldSurfaceIndex` populated + small (0,1,2,5). **Gotcha:** world-brush hits report `m_pEnt = worldspawn`
  (a real entity), NOT null — the first sweep filtered on `m_pEnt!=null` and got 0 world hits; fixed to match
  `classname=="worldspawn"` + added a `portalable-on-entity` counter (func_brush blind-spot check).
  **SWEEP RAN 2026-06-30 (3 vantage points) — trace-enumeration NO-GO:** `worldSurfaceIndex` is NOT a per-face
  id (only **3 distinct** across 88–1102 portalable hits, range wobbles 0..5 / 0..7 / 0..38 by position →
  idea (i) walk-the-index DEAD); single-point coverage poor + position-bound (**14/22/23 distinct cells** vs
  27 multi-tile panels → many-sweep + cluster + still occlusion-bound); **portalable-on-entity = 41/2/31**
  confirms real `func_brush` portalable surfaces a worldspawn-only enumerator (sidecar OR sweep) misses.
  (Sidecar read 0 only because `sar_harness_panel_dir` resets on restart and wasn't re-set before map load —
  procedure, not a bug.) The per-face oracle (`SURF_NOPORTAL` + material) stays GREEN for placement-time
  checks; it's enumeration-via-trace that fails.

  **→ B0.2 DECISION: trace-sweep enumerator REJECTED. Arc B, if built, = route (c) in-engine BSP _file_
  parse** (the file, not engine memory — so no offset RE, no per-build rot, unlike route (a)).
  `FileSystem::FindFileSomewhere("maps/<map>.bsp")` resolves the path (in-tree; EngineDemoPlayer uses it) →
  `std::ifstream` + a small v21 lump reader (PLANES/VERTEXES/EDGES/SURFEDGES/FACES/TEXINFO/TEXDATA/
  string-table/string-data) → feed the existing `cluster_panels` port (B1). Exact geometry, complete coverage
  (can read brush-entity submodels → fixes the func_brush gap), no precompute. **Only open cost: are the
  geometry lumps LZMA-compressed?** (SAR links zlib, not LZMA.) Price it with a ~60-LOC read-only
  `sar_harness_bsp_lump_probe` (header + per-lump fourCC/payload-magic) before committing. Until then the
  shipped sidecar is the v0 source; the `IPanelSource` seam swaps route (c) in with zero downstream churn.
  SAR is trace-based today; no BSP-face walk exists. The original two-route framing, for reference:
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
### B-series — `BspFilePanelSource` (the chosen build, decided 2026-06-30)

In-engine parse of the map's `.bsp` **file** (not engine memory). **Deps: zero new** — std `<fstream>` +
in-tree `FileSystem::FindFileSomewhere` + `Math.hpp`; LZMA confirmed not needed (`bsp_lump_probe`: all lumps
`fourCC 0`, uncompressed, VBSP v21). **Lives in** one new pair `src/Features/Harness/BspFilePanelSource.{hpp,cpp}`
(Makefile auto-globs `*.cpp`, no edit). **Reuse, not reinvent:** v21 struct layouts lifted from canonical
`public/bspfile.h` (sizes already cross-confirmed by the lump-probe: face 56 / texinfo 72 / texdata 32 /
plane 20 / edge 4, all exact divisors); clustering is a 1:1 port of the validated `cluster_panels.py`.
**`func_brush` submodels deferred** (optional, post-B8 — no regression, the sidecar misses them too).

- **B1 ✅ — v21 structs + lump loader** (`BspFilePanelSource.cpp`, anon ns): `Plane`/`Edge`/`Face`/`TexInfo`/
  `TexData` (`#pragma pack(1)`), `LoadBsp(path)` slurps the 9 lumps via `FindFileSomewhere`. *Verify:* `geo_dump`
  counts (8426 planes / 7183 verts / 3491 faces / 782 texinfos / 42 texdatas on `1361778957`).
- **B2 ✅ — face → geometry** (`ExtractFace`): plane from `planes[planenum]`; winding verts via
  `firstedge..→surfedges→edges→verts`; material+flags via `texinfo→texdata→stringtable→stringdata`. *Verify:*
  `geo_dump` prints real materials (`tile/white_wall_tile003a`) + axis-aligned normals.
- **B3 ✅ — filter:** portalable (`!(flags & SURF_NOPORTAL)`) + white-tile material (`IsWhiteTile`, mirrors
  `is_white_tile`). *Verify:* `geo_dump` portalable-white-tile face count is sane (> 27, multi-tile).
  *(B1–B3 land together in `sar_harness_bsp_geo_dump` for one in-game verify.)*
- **B4 ✅ — cluster → panels:** ported `cluster_panels` (plane-key bucket → `PlaneAxes` project → 128u cells →
  connected-components → center/mins/maxs → origin-junk drop → deterministic sort → marks 1..N) into
  `EnumeratePanels`. **VERIFIED `1361778957`: 38 portalable white-tile faces → 27 panels, MARK-FOR-MARK
  identical to the sidecar** (S1..S27, same centers + normals). `func_brush` washed out via the white-tile
  filter → exactly 27, no model-0 gate needed. The in-engine parser reproduces the offline pipeline exactly.
- **B5 ✅ — `BspFilePanelSource : IPanelSource`** wraps B1–B4 (`EnumeratePanels` = `LoadByMap` + `ClusterPanels`).
- **B6 ✅ — wire the source:** `PanelSession` news up `BspFilePanelSource` directly (no A/B cvar — geo_dump
  already proved parity; KISS). Sidecar files kept compiled one round as a safety net until map 2 is confirmed.
  *Verify (pending):* `panels_dump` → 27 on `1361778957` and 5 on `1522623535` through the wired path.
- **B7 ✅ — cross-validated** through the wired `SurfaceMarkTable` path: `panels_dump` → **27 on `1361778957`,
  5 on `1522623535`**, both correct. The parser generalizes past the one map.
- **B8 ✅ — sidecar REMOVED:** deleted `SidecarPanelSource.{hpp,cpp}` + the `sar_harness_panel_dir` cvar;
  `PanelSession` news up `BspFilePanelSource` directly (no source-select indirection). **`artifacts/panels/*.json`
  KEPT** (per user — may be useful later; `BspFilePanelSource` no longer reads them). The percept pipeline is now
  fully self-contained C++ — no offline precompute step. (Python `cluster_panels --emit`/census untouched.)

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
