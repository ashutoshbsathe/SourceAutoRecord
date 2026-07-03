# Fractional `(u,v)` panel targeting + the inclined-panel gap

Two problems surfaced together (2026-07-02) and turned out to be one layer — the
**panel surface representation**. This doc records what shipped, the angled-panel
gap that did *not* get fixed, and the open follow-ups for tomorrow.

## What shipped — `place_portal Sn@u,v`

`place_portal <color> Sn@u,v` aims at a fractional point on the panel instead of
its center. `(u,v)` are in `[0,1]`, corner-relative: `(0,0)` and `(1,1)` are
opposite corners, `(0.5,0.5)` is the center. Omit `@u,v` ⇒ center ⇒ old
behavior (back-compatible). Rides the existing `target` string — **zero proto
change**. Also flows through `ResolveTarget`, so `aim_at Sn@u,v` / `go_to Sn@u,v`
point at the same spot.

Mechanic:
- `PanelDesc` now carries `corners[4]` — the true in-plane rect the enumerator
  *already computed at build time and was throwing away* (it degraded to a
  world-space AABB `mins/maxs`). Keeping the corners makes `(u,v)` exact for
  **any** tilt, not just axis-aligned panels.
- `ResolvePanelPoint(p,u,v)` = bilinear interp over the four corners. No trig, no
  basis recompute at resolve time.
- `ClassifyTarget` parses `Sn@u,v` (clamps to `[0,1]`; a malformed `@` with no
  comma → NONE); `PlacePortal` aims the fair-fire trace at the resolved point, so
  an edge-graze still fails honestly (`NO_LOS`) rather than magically placing.
- Grammar: `_panel_base` validates the optional `@u,v` (two floats in `[0,1]`).

**Status: kinda-sorta works.** Arbitrary off-center portals place correctly. But
the in-game orientation of the `(u,v)` axes is *not legible* to a human — which
`(u,v)` maps to which direction depends on `PlaneAxes(normal)` (the deterministic
in-plane basis) crossed with the corner ordering, and you can't see it. **A temp
`(u,v)` grid overlay on panels is the fix (follow-up 1 below).**

`(u,v)` axis definition (for reference / the overlay): corners are ordered
`(umin,vmin) (umax,vmin) (umax,vmax) (umin,vmax)`; `u` runs along the panel's
`PlaneAxes` first basis vector, `v` along the second. For a floor (`n=0,0,1`)
`PlaneAxes` yields `u=+Y, v=-X` — hence the confusion; a grid makes it obvious.

## The inclined-panel gap — NOT fixed, and it's structural

Inclined/angled portalable surfaces get **no S-mark at all**. `sar_harness_bsp_geo_dump`
on the test map: 38 white-tile faces → **27 panels, every one cardinal** (`±1 0 0`,
`0 ±1 0`, `0 0 ±1`); the two angled panels visible in-game (one with `Po` on it)
are simply absent.

Root cause (verified by reading the enumerator, *not* the first guess):
- The workflow's initial theory (a missing plane offset at `BspFilePanelSource.cpp:285`)
  is **wrong** — the projection is origin-relative but self-consistent, `PlanePoint`
  reconstructs the exact world point, and the span is preserved. Inclined *geometry*
  clusters fine. `IsPortalable` has **no** orientation gate; `PlaneAxes` handles
  steep normals.
- The real reason: `BspFilePanelSource` parses **static world faces in world
  coordinates** (`LUMP_FACES`). PeTI angled panels are not static world brushes —
  the editor places them as an **entity/prop the item rotates into its tilt**, so
  their portalable surface never exists as a tilted face in the static lump.

So this is a **separate feature**, not a filter tweak: reconstruct entity/prop
panel geometry (read the entity lump + brush-model transforms, or the static-prop
lump), or enumerate panels at runtime by tracing (in-game the angled panel is a
real collidable portalable surface — `Po` proves it). Decision deferred; needs its
own recon (`py/bsp_recon/dump_ents.py` can identify what the angled-panel entity
actually is on this map — brush vs prop — which sets the fix shape).

Crucially, this does **not** block the `(u,v)` win or the ledge fling: floors are
cardinal (`S15 = 0 0 1 @ z=0`), so `(u,v)` on a floor panel is exact today.

## `(u,v)` grid overlay — SHIPPED (2026-07-03)

`sar_harness_annotate_uv 1` (with `sar_harness_annotate 1`) draws a faint
quarter-fraction grid (plus edges) on every panel, **shaded white at `(0,0)`
blending to red at `(1,1)`** — the corner origin reads from color alone, no
labels (v2 after in-game review: corner text labels were clutter). Segments
pool into 8 constant-color shade meshes shared across panels. The grid points
go through the same `ResolvePanelPoint` bilerp the verb aims with (now declared
in `PanelSource.hpp`, defined with the rest of the panel geometry in
`BspFilePanelSource.cpp`), so the overlay cannot disagree with where a portal
actually lands. Known tradeoff: the `(u+v)/2` shade is symmetric in u/v, so
`(1,0)` vs `(0,1)` share a shade — if the mirror ambiguity ever bites, give v
its own hue. Line width is `sar_harness_annotate_uv_width` (world units,
default 1; 0 = hairline) — thickness renders as in-plane quads because the
engine's wireframe lines are fixed 1px.

## Angled-panel recon — DONE (2026-07-03): they're func_brush slabs at the BSP origin

`dump_ents.py --geometry` on the test map (`workshop/596996616964103777/1361778957`)
closed every open question. Anatomy of a PeTI angled panel:

- **Portalable surface = `func_brush` named `angledPanelNN_panel_top`** — a
  128×128×2 brush-model slab (5 on this map). Its white-tile face **is in
  `LUMP_FACES`, portalability baked, `IsPortalable` passes** — but brush-entity
  models compile in **origin-relative local coords**, so all five slabs sit at
  `(0,0,0)`. That is precisely the cluster our "PeTI origin-junk" filter drops:
  the panels were never absent from the BSP, they're entity-local geometry.
  Confirmed: five 128×128 white-tile portalable faces centered at the origin,
  one floor-oriented + four wall-oriented, matching the five arms' mounts.
- **Pose = parent `prop_dynamic` `angledPanelNN-model_arms`**
  (`models/props_ingame/arm_4panel.mdl`, shared by every PeTI angled panel).
  The deploy angle lives in the **animation name** (`ramp_30_deg_open`,
  `ramp_45_deg_open`); mount orientation in `angles`; toggled panels via
  `logic_branch → angledPanelNN-ramp_open`. So the surface set is **dynamic** —
  panels can start deployed or deploy mid-episode.
- No double-mark risk: behind a deployed panel the map compiles a SQUAREBEAMS
  recess, not white tile, and the retracted rest pose only exists origin-local.
  The 27 static world panels stay correct; the new source is purely additive.

### Recon phase 2 (2026-07-03) — offline read path VERIFIED end-to-end

srctools walk of the same map, per `*_panel_top` bmodel (`*2 *3 *4 *12 *13`,
carried by the raw entity lump's `model` kv — `dump_ents.py` silently drops it
because srctools pops `model` when linking `bsp.bmodels`):

- exactly **6 faces** each: 1 white-tile + 1 backpanel + 4 frame bevels;
- the tile face has **no `SURF_NOPORTAL`** while the backpanel does (0x20) —
  the existing `IsPortalable` filter selects the correct face unchanged;
- tile corners are **exact origin-local quads** (±64, thickness 2, cardinal at
  rest): floor-mount for panel 11, wall-mounts for the rest, matching the arms.

So the entire file-side pipeline is proven: entity lump `targetname` → `*N` →
`LUMP_MODELS` firstface/numfaces → face extraction → `IsPortalable` → 4 local
corners. What's left is the **runtime half**, and a probe for it is built:
`sar_harness_angled_panel_probe` dumps each `*_panel_top`'s live
`abs_origin`/`abs_angles`/OBB plus the parent arm's angles + `m_nSequence` +
`m_flCycle`. **Protocol: run once retracted → deploy the panel → run again.**
That answers the two open runtime questions: (a) does the func_brush abs
transform carry the tilt (local corners × abs transform = the deployed quad),
and (b) which arm field is the deploy/retract status signal.

**Fix shape (decided by the recon): runtime pose × BSP bmodel faces.**
Enumerate live `func_brush` entities named `*_panel_top` → read their `*N`
brush-model index → pull that model's white-tile face from the already-parsed
`.bsp` (`LUMP_MODELS` partitions `LUMP_FACES` by firstface/numfaces — one new
lump read) → transform the local-coord corners by the entity's **current** abs
transform and emit a `PanelDesc`. The engine has already done all hinge +
animation math; `(u,v)` bilerp already handles tilt. Zero new offsets, no
`.mdl` parsing. Rejected: static reconstruction (anim-name angle + hinge RE —
strictly worse) and trace enumeration (already rejected for world panels).
The real design cost is **dynamism**: `SurfaceMarkTable` is session-static
today, so deploy/retract needs a refresh path + name-keyed stable marks.
**The design pass is written →
[dynamic_panel_enumeration.md](dynamic_panel_enumeration.md)** (broadened by a
corpus census: two angled-panel templates + flip panels → class-agnostic
brush-entity enumeration, phases P1–P5, probe gates G1/G2).

## Open follow-ups

1. **Ledge fling end-to-end.** With `(u,v)`: place a floor portal near an edge
   below you (`place_portal blue S_floor@…`) → `jump_into Pb` → confirm the money
   fling (long fall → big momentum) freezes mid-air. jump_into's ledge path is
   still unverified because you couldn't aim the portal there until now.
2. **`drop_into`** — the gentle sibling (step through / drop a held cube).
3. **Angled-panel verb/percept build** — the design pass above (dynamic
   SurfaceMarkTable), then the implementation.
