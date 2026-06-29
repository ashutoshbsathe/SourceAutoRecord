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

## ARC A — offline sidecar; prove verb + percept + labels + ergonomics

SAR/C++ before Python within each cluster. Each phase = one focused edit + a 1-line verify.

- **A0 — eval maps (LOCKED):** the last 3 of `map_candidates.txt` — `workshop/17093866141393312246/1782237070`,
  `workshop/927053855830294446/1522623535`, `workshop/596996616964103777/1361778957`. Some have **no**
  portalable surface — deliberately, to exercise the empty-panel-set / `NOT_PORTALABLE` path. *(no code)*
- **A1 — proto (C0).** Edit `harness.proto`, `make proto`. *Verify:* regen clean; `harness_pb2.SurfaceMark()` +
  `MacroRequest(color='blue')` import.
- **A2 — `PanelDesc` + `IPanelSource`** (`PanelSource.hpp`). *Verify:* builds.
- **A3 — `SurfaceMarkTable`** (`SurfaceMarkTable.{hpp,cpp}`, mirrors `MarkTable`): hold `vector<PanelDesc>` +
  band marks, `RebuildFromSource`, `GetPanelFromMark`, `Clear`-on-session-start. *Verify:* builds.
- **A4 — Python sidecar emitter.** Extend `py/bsp_recon/cluster_panels.py` (reuse its clustering verbatim) to
  emit, per map, `{ map, panels:[{id, plane_normal, center, mins, maxs, anchor_flags}] }`. *Verify:* emits a
  sane panel JSON for one eval map.
- **A5 — `SidecarPanelSource`** (`.{hpp,cpp}`): load `<map>.json` by name (C4), parse → `PanelDesc`, assign
  band marks deterministically. *Verify:* a temp `sar_harness_panels_dump` prints the panel count.
- **A6 — wire source + table at session start** (next to `markTable.Clear()`); `g_panelSource` defaults to
  `SidecarPanelSource`. *Verify:* `map <eval>` → `sar_harness_panels_dump` shows the panel set.
- **A7 — fill `SurfaceMark` protos** in `Portal2HarnessImpl::InternalObserve` (parallel to the entity loop):
  one `add_surface_marks()` per panel. Full list every observe (cheap, O(#panels)). *Verify:* `agentloop_smoke`
  sees non-empty `state.surface_marks`.
- **A8 — Python percept** (`entities.py` `observe`): append one dict per `surface_marks` entry
  (`class='wall_panel'`, `pos=center`, `dist`, `bearing`) with the panel mark in its own `S`-prefixed
  namespace (C1), listed alongside entity marks but visually distinct, uniform style (C3). *Verify:*
  `agentloop_smoke` percept lists `S`-prefixed panel marks beside entity marks.
- **A9 — grammar spec** (`macro_grammar.py` `VERB_SPECS`): `place_portal` verb, `example='place_portal blue S1'`.
  *Verify:* `percept_grammar_smoke` validates the example.
- **A10 — grammar parse + validate**: extend `build_macro` (`place_portal <color> <surface>`; parse the
  `S`-prefixed surface arg → `surface_mark` int) + a `_check_place_portal` (color∈{blue,orange}; surface
  resolves to a `wall_panel`); wire into `validate` + `_signature`. `where` is deferred (proto field stays
  reserved). *Verify:* bad color / unknown surface → structured error.
- **A11 — dispatch**: `if (verb=="place_portal") return PlacePortal(req);` + `.hpp` decl. *Verify:* builds;
  no longer `NOT_IMPLEMENTED`.
- **A12 — `PlacePortal` core** (template off `Interpose`): resolve `surface_mark`→panel; anchor = panel
  `center` (`where` deferred — always CENTER for v0); prime gun; `TraceFirePortal` preview; `portal_place`
  commit; settle; confirm `m_bIsPortal2`/`m_hLinkedPortal`; map `ePlacementResult`→code; `placed_pos=finalPos`.
  *Verify:* `macro_repl` `place_portal blue S1` → `PLACED` + a blue portal at the panel center; non-portalable
  panel → `NOT_PORTALABLE`.
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

- **B0 — recon spike (read-only; runs during ARC A).** SAR is **trace-based today** — no BSP-face walk exists,
  and `model_t` as wrapped lacks the face/texinfo lumps (`ICollideable::GetCollisionModel()` reaches the world
  model, but the lump offsets are unwrapped). Spike **two** routes and pick the cheaper:
  - **(a) raw BSP-lump walk** — `worldspawn` model → `fnHandle`/`dmodel_t` header → `dface` + `texinfo` arrays
    → plane + material + `SURF_NOPORTAL`. Per-build offset RE (the honest cost: the synthesis' "2-3 days" is
    optimistic — material-name resolution from cached refs is the fiddly part).
  - **(b) trace-based face discovery** — `CGameTrace` already returns `csurface_t` (flags + material name) and
    `worldSurfaceIndex`; cast a coarse one-time grid of rays at chamber load, read flags+material per hit,
    dedup by `worldSurfaceIndex`, cluster. No lump RE; cost is one-time trace volume (NOT per-tick), far below
    the rejected per-frame grid.
  *Verify:* a temp `sar_harness_bsp_face_probe` prints face/surface count + a sample's plane+material matching
  the sidecar. **Gate:** whichever route is cheaper proceeds; if both exceed budget, ARC A ships standalone and
  ARC B reschedules.
- **B1 — `SurfaceEnumerator`** (`.{hpp,cpp}`): the chosen B0 route → portalable white-tile faces → cluster
  (reuse `cluster_panels.py`'s plane-key/128u-cell/connected-components logic, ported to C++) → `vector<PanelDesc>`,
  identical shape to the sidecar. *Verify:* `sar_harness_panels_dump` shows live-walk panels.
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
