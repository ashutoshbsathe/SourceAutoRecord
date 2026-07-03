# Dynamic panel enumeration — brush-entity portalable surfaces

Angled panels, flip panels, and any other **brush-entity** portalable surface are
invisible to the static enumerator: their white-tile faces live in `LUMP_FACES`
but in **origin-relative local coords** (brush models compile about the entity
origin), so they land in the origin-junk cluster `ClusterPanels` drops. Their
world pose exists only at runtime (arm animation / door rotation). This doc is
the design + phased plan for enumerating them. Recon trail:
[uv_targeting_and_panels.md](uv_targeting_and_panels.md).

## Recon facts (2026-07-03, all verified)

- **Test map** (`workshop/596996616964103777/1361778957`): 5 angled panels =
  `func_brush angledPanelNN_panel_top` (128×128×2 slab, model `*N`) parented to
  `prop_dynamic angledPanelNN-model_arms` (`arm_4panel.mdl`; deploy angle in the
  anim name `ramp_30/45_deg_open`; toggled by `logic_branch → ramp_open/close`).
- **Per slab bmodel: exactly one white-tile face, no `SURF_NOPORTAL`** (frame
  bevels + backpanel carry it / fail the material test) → the existing
  `IsPortalable` selects the correct face unchanged. Corners are exact
  origin-local quads (±64, cardinal at rest).
- **Corpus (278 workshop maps, entity-lump sweep):** 217 `angledPanelN_panel_top`
  across 52 maps **+ 76 `apN-brush`** (a second angled-panel template, same
  anatomy, different naming) **+ ~60 flip panels as `func_door_rotating`
  `fpN-flipping_panel`/`flipPanelN-flipping_panel`** (pose via door rotation,
  not parenting). Glass variants (`angledClearPanelN-*`) are not white tile →
  auto-excluded. Every PeTI angled panel ships its own `info_placement_helper`,
  so the engine already snap-assists portals onto deployed panels.
- **Naming is NOT a reliable gate** (two angled templates + a different class
  for flip panels). The reliable invariant is *structural*: a non-worldspawn
  brush model whose bmodel carries a portalable white-tile face.

## Design

**One rule: enumerate ALL brush-entity models with a portalable white-tile
face; pose each from its live entity every refresh.** Class-agnostic — covers
both angled-panel templates and flip panels with the same code path.

1. **Session start (file side).** Extend `BspFile` with `LUMP_ENTITIES` (0,
   text) and `LUMP_MODELS` (14, `dmodel_t`: mins/maxs/origin float3 + headnode
   + firstface/numfaces). For each entity-lump entry with `model "*N"` (N>0):
   run the bmodel's face range through the existing `ExtractFace` +
   `IsPortalable`; if a face passes, emit a rest descriptor
   `{ targetname, localCorners[4], localNormal }` (local = as stored, origin-
   relative). No clustering — a bmodel face IS the panel.
2. **Refresh (runtime side).** `SurfaceMarkTable` holds the rest descriptors
   alongside the static panels. A per-frame `RefreshDynamic()` (RENDER, main
   thread — same cadence/precedent as `markTable.RebuildFromWorld()`) finds
   each live entity by targetname and computes the posed `PanelDesc`:
   `world = abs_origin + R(abs_angles) · local` for corners and normal
   (`Math::AngleMatrix`, same as the annotate OBB path). Corner ordering is
   re-derived at refresh from `PlaneAxes(worldNormal)` so the `(u,v)`
   convention matches static panels exactly; the grid overlay makes whatever
   frame results legible.
3. **Marks.** Dynamic panels are numbered **after** the static set, ordered by
   targetname — deterministic across sessions and stable across deploy state
   (identity is the name, not the pose). A retracted angled panel is a real
   flush portalable surface, so it keeps its mark at its flush pose — **no
   deployed/retracted gating needed for v0**; the pose IS the status.
4. **Everything downstream rides free.** Percept (`GameState.surface_marks`),
   annotate outline + `(u,v)` grid, `place_portal Sn@u,v` bilerp, `aim_at` /
   `go_to` — all consume `surfaceMarkTable.Panels()` / `GetPanelFromMark`,
   which now return the current pose. Zero proto change, zero Python change.

### Gates — BOTH CLOSED (2026-07-03, in-game probe on the test map)

- **G1 ✓ — the func_brush abs transform carries the full pose.** Deployed
  panel 11: `abs_origin (1984, 1975.4, 288)`, roll 30° (matches its
  `ramp_30_deg_open` sequence; the 45° panels show roll ±45). The hinge math
  checks numerically: `abs_origin + R(angles)·local` puts one slab edge at
  exactly `(y=1920, z=256)` — the stationary hinge line — and the other at
  `(1920+128·cos30°, 256+128·sin30°)`. The composition P2 uses is exact.
- **G2 ✓ — retract reads back through the same fields.** `ent_fire
  <name>-ramp_close Trigger` swings it flush and the probe returns the
  **exact entity-lump rest origin with identity angles** (`1984 1984 256`,
  angles 0) — the transform model is verified in both poses. `-ramp_open`
  restores it. Note: `m_nSequence` stays at the open-sequence id when closed
  (`m_flCycle` flips 1.0 → 0.0), so sequence id alone is NOT an open/closed
  signal — irrelevant for us since the pose is the status.
- Also verified live: **P1 extraction is correct** — 5 slabs, exact ±64 local
  quads, outward normals right after deriving them from the face winding
  (Source faces wind clockwise seen from the front). The shared-plane normal
  + `side` bit misoriented three of five faces — winding is the only
  trustworthy orientation source for brush-model faces.

### Edge cases / notes

- The panel **outline box is gone** (user review, 2026-07-03): the AABB box
  read as a fat diagonal slab on tilted panels and was redundant next to the
  `(u,v)` grid, which is now the panel's visual extent (plus the S-label).
  Note the grid is `sar_harness_annotate_uv`-gated (default 0) — with it off,
  panels show only their S-label.
- A retracted slab sits coplanar-flush with neighboring static white tile: two
  adjacent marks that visually read as one surface. Honest (one of them can
  tilt), not a bug.
- A portal placed on a panel that then moves: engine fizzles it; percept
  already tracks portals live via `ReadPortal`. Assert during P4.
- Flip panels: `func_door_rotating` rotates its own transform — same read, no
  parent hop. Both faces white? If the bmodel carries two portalable faces,
  emit both (they're distinct surfaces back-to-back); verify on a flip-panel
  map in P5.

## Phases (each small + hand-verifiable; C++ only, no proto/Python)

- **P1 — file side. ✅ SHIPPED + VERIFIED in-game (2026-07-03).** `BspFile` +
  entity-lump text parse + `LUMP_MODELS`; rest descriptors printed from
  `sar_harness_bsp_geo_dump`. Verified: 5 slabs, ±64 local quads, outward
  normals correct (winding-derived — see gates).
- **P2 — table + refresh. ✅ SHIPPED + VERIFIED in-game (2026-07-03).** Rest
  descriptors → `SurfaceMarkTable` dynamic slice; per-frame posing in
  `PanelSession` (RENDER). Verified: 32 panels; the S28 deploy/retract
  round-trip moves between flush (`0 0 1`, z=256) and 30° (`0 -0.5 0.87`,
  hinge numbers exact) live in `sar_harness_panels_dump`.
- **P3 — percept + annotate. ✅ folded into P2's verify.** S-marks + `(u,v)`
  grid drape tilted panels correctly (screenshot-verified on a 45° panel);
  percept rides `Panels()` unchanged. The planned corner-quad outline was
  dropped instead — see edge cases.
- **P4 — the verb. ✅ VERIFIED in-game (2026-07-03).** `place_portal` with
  fractional `(u,v)` lands on a deployed angled panel — zero verb changes;
  the bilerp over posed corners was sufficient.
- **P5 — coverage.** A flip-panel workshop map through the same pipeline;
  `agentloop_smoke` gains a dynamic-panel assertion (panel count + a
  non-cardinal normal on the test map).

Deferred: gravity-anchored `(u,v)` frame (dossier ruling — separate,
whole-surface change) · a `deployed` percept bit (pose already tells) ·
BEEmod/Hammer exotic panels.
