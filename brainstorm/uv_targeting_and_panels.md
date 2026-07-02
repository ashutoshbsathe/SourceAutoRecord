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

## Open follow-ups (tomorrow)

1. **`(u,v)` grid overlay on panels (temp vis).** Draw a labeled `(u,v)` grid on
   each panel (debug cvar, off by default) so the axis orientation and corner
   origin are legible in-game. Pure annotation; no gameplay change. Makes `(u,v)`
   actually usable by a human (and later legible to the model in screenshots).
2. **Angled/inclined-panel enumeration recon.** Identify what PeTI angled panels
   compile to (`dump_ents.py`), then decide: entity/prop-geometry reconstruction
   vs runtime-trace enumeration. Its own design pass.
3. **Ledge fling end-to-end.** With `(u,v)`: place a floor portal near an edge
   below you (`place_portal blue S_floor@…`) → `jump_into Pb` → confirm the money
   fling (long fall → big momentum) freezes mid-air. jump_into's ledge path is
   still unverified because you couldn't aim the portal there until now.
4. **`drop_into`** — the gentle sibling (step through / drop a held cube).
