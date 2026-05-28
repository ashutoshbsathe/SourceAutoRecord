# Rollout Visualizer — Design & Implementation

## Context

The `.rollout` format stores 2846 ticks × 557 entities × full pixel frames (3.3GB).
The existing `py/visualize_rollout.py` uses OpenCV to play back frames with a HUD overlay
(position, WASD, portal buttons, mouse vector pad). This document captures the design
brainstorm, framework selection, and final implementation plan for the new browser-based
visualizer that adds entity visualization.

---

## What the Current OpenCV Visualizer Does

- Plays `.rollout` pixel frames at tickrate in an OpenCV window
- HUD overlay: position, tick, WASD key layout, portal buttons, mouse vector pad
- Space = pause/resume, arrow keys = step frame-by-frame
- All in `py/visualize_rollout.py`, ~345 lines

This is kept as `py/vis/legacy_cv_visualizer.py` (unchanged) for quick inspection without a browser.

---

## The Core Problem: 557 Entities at 2846 Ticks

Each tick, each entity either:
- Changed (1+ fields updated) → delta is in the rollout's EntitySnapshot
- Didn't change → absent from that tick's snapshot entirely

The challenge: **how do you show the change pattern of 557 entities across ~2800 ticks usefully?**

---

## Design Options Considered

### Option A: Entity Grid (CHOSEN — user's idea)

A hoverable N×M grid of colored cells, one per entity. On each tick:
- Cell flashes red/orange briefly when that entity updated
- Sorted descending by total update count (most-active at top-left)
- Hover → tooltip with fields that changed this tick, also flashing

**Pros**: High density, sorting surfaces interesting entities, flash gives temporal pulse feel  
**Cons**: In OpenCV, hover detection + animations = ~100 lines of manual boilerplate

### Option B: Entity Timeline / Heatmap

2D heatmap: X = tick, Y = entity. Color intensity = fields changed that tick. Cursor line follows playback.

**Pros**: Shows full temporal pattern at a glance, spikes/idle periods obvious  
**Cons**: Static analysis view, not live playback. Needs 557 scrollable rows. Better as a secondary "analysis" tab.

### Option C: 3D Top-Down Minimap

Entity positions projected top-down. Player centered, portals as blue/orange dots, cubes as squares.

**Pros**: Spatially intuitive  
**Cons**: Source maps are multi-floor, top-down is lossy without BSP parsing. Too much infrastructure.

### Option D: Hybrid Web App (CHOSEN)

Python HTTP server + single HTML page. Left: game frame canvas. Right: entity grid.

**Why web over extending OpenCV for the entity panel:**
CSS `transition: background-color 0.3s ease` = the entire animation system.
OpenCV equivalent = 100+ lines of manual color lerping + hit testing + timer tracking.

---

## Framework: NiceGUI

**Comparison:**
| | Streamlit | NiceGUI | Dash |
|--|--|--|--|
| Execution model | Reruns entire script on each interaction | Event-driven, WebSocket | Callback-based |
| Good for | Prototyping | Custom tools/dashboards | Enterprise dashboards |
| Latency | High (rerun) | Low (WS diff) | Medium (~100ms callbacks) |

**Verdict:** NiceGUI.
- Streamlit: reruns whole script on every interaction → breaks frame-by-frame playback
- Dash: callback round-trip ~100ms → can't drive 60fps flash animations from Python
- NiceGUI: FastAPI under the hood → clean `@app.get()` data endpoints; `ui.splitter()` for layout; Python drives *which tick*, JS drives *everything visual* (one int over WS per tick, not 557 DOM mutations)

---

## Architecture

```
startup (preparse everything, startup time doesn't matter):
  rollout_loader.py reads entire .rollout → memory
    ├─ frames_raw[]: list of raw RGB bytes (width*height*3) per tick
    ├─ ticks_json[]: list of JSON bytes per tick (entity deltas + action)
    └─ meta: {map, tickrate, total_ticks, width, height, entities (sorted by update count)}

runtime:
  FastAPI endpoints:
    GET /meta       → meta JSON (with sorted entity list for building grid)
    GET /frame/{n}  → PNG (encoded on-demand from frames_raw[n], ~5ms each, fine)
    GET /tick/{n}   → JSON: {tick, pos, action, entities: [{idx, class_name, fields}]}

  browser JS:
    - On init: fetch /meta → build entity grid (CSS Grid, sorted by update count)
    - Playback loop: setInterval/async step at 1000/tickrate ms
    - advanceTick(n): Promise.all([fetch /frame/n, fetch /tick/n])
        ├─ draw frame on <canvas>, draw HUD on top
        ├─ for each changed entity: cell.classList.add('flash') → CSS transition
        └─ if entity hovered: refresh field panel with changed fields
```

**Key insight**: Only `currentTick` (one integer) travels from Python to browser via NiceGUI.
The flash animations, canvas drawing, field panel — all local to the browser.

---

## Decisions (from user review)

| Question | Decision |
|----------|----------|
| Frame format | PNG (lossless) |
| Preload strategy | Preparse everything at startup, don't care about startup time |
| Cell size | 20-column grid, 24×24px cells with 2px gap |
| Flash duration | Proportional to playback speed: `1000 / tickrate` ms |
| Field detail panel | Show only fields that changed this tick |
| Update rank on cell | Show as faint number (optional) |

---

## File Structure

```
py/vis/
├── visualize.py            entry point: python py/vis/visualize.py demo.rollout
├── rollout_loader.py       pre-parse .rollout → frames_raw, ticks_json, meta
└── static/
    ├── visualizer.js       entity grid, canvas HUD, playback loop
    └── vis.css             dark theme, flash transitions, family color coding

py/vis/legacy_cv_visualizer.py   (moved from py/visualize_rollout.py, unchanged)
```

---

## Entity Grid Specifics

- **Layout**: CSS Grid, 20 columns × ~30 rows for 557 entities. 24×24px cells, 2px gap.
- **Sort**: By total update count descending (computed at load). Rank shown as faint `#N` in cell corner.
- **Class family color coding** (set at grid-build time):
  - `player` → green tint
  - `*portal*` → blue tint
  - `*cube*` / `phys*` → yellow/gold tint
  - `worldspawn` / `func_*` / `trigger_*` → near-black (rarely update)
  - everything else → neutral dark gray
- **Flash**: `cell.classList.add('flash')` → CSS `transition: background-color` handles fade-out automatically
- **Hover**: field detail panel shows only changed fields for that entity on current tick
- **Label**: first 8 chars of class name + `[idx]` as `title` attribute tooltip

---

## Implementation Notes

### PNG encoding on demand

`frames_raw[n]` stores raw RGB bytes (width × height × 3). On `GET /frame/{n}`:
```python
img = Image.frombytes('RGB', (w, h), frames_raw[n])
buf = io.BytesIO()
img.save(buf, format='PNG', compress_level=1)  # fast, not max compression
return Response(content=buf.getvalue(), media_type='image/png')
```
~5ms per frame. At 16ms/tick budget, fine.

### Entity update count computation

Single pass over all ticks: `Counter(ent['idx'] for tick in all_ticks for ent in tick['entities'])`.
Then sort entity registry by count descending.

### Flash CSS

```css
.entity-cell { transition: background-color 400ms ease; background: #1a1d24; }
.entity-cell.flash { background: #cc2222; }
```
Remove `.flash` class after `flashDuration` ms → CSS handles the interpolation back.
Force reflow between remove+add to restart animation: `void cell.offsetWidth`.

### HUD on canvas

Replicate `visualize_rollout.py`'s `draw_hud()` using Canvas 2D API:
- `fillRect` for key backgrounds, `strokeRect` for borders, `fillText` for labels
- Mouse vector: `ctx.arc()` dot + `ctx.lineTo()` line

---

## Dependencies

New: `nicegui`, `pillow` (likely already present)
Existing: `protobuf` (already used by validate_rollout.py)

Install: `pip install nicegui pillow`
