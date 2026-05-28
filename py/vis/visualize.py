"""
Rollout Visualizer — NiceGUI + FastAPI entry point.

Usage:
    python py/vis/visualize.py <path/to/demo.rollout>

Controls (in browser):
    Space       : Play / Pause
    Arrow Left  : Step backward
    Arrow Right : Step forward
    , / .       : Same as arrows
"""

import json
import sys
import os
from pathlib import Path

from fastapi.responses import Response, JSONResponse
from fastapi.staticfiles import StaticFiles
from nicegui import app, ui

# Resolve paths before any relative imports
VIS_DIR    = Path(__file__).parent
STATIC_DIR = VIS_DIR / 'static'

sys.path.insert(0, str(VIS_DIR.parent))  # so p2harness is importable
from rollout_loader import load, frame_to_png


def main():
    if len(sys.argv) < 2:
        print('Usage: python py/vis/visualize.py <path/to/demo.rollout>')
        sys.exit(1)

    rollout_path = sys.argv[1]
    meta, frames_raw, ticks_json, (w, h) = load(rollout_path)

    # --- FastAPI data endpoints ---

    app.mount('/static', StaticFiles(directory=str(STATIC_DIR)), name='static')

    @app.get('/meta')
    def get_meta():
        return JSONResponse(meta)

    @app.get('/frame/{n}')
    def get_frame(n: int):
        if 0 <= n < len(frames_raw) and frames_raw[n]:
            return Response(
                content=frame_to_png(frames_raw[n], w, h),
                media_type='image/png',
            )
        return Response(status_code=404)

    @app.get('/tick/{n}')
    def get_tick(n: int):
        if 0 <= n < len(ticks_json):
            return Response(content=ticks_json[n], media_type='application/json')
        return Response(status_code=404)

    # --- NiceGUI page ---

    @ui.page('/')
    def page():
        ui.add_head_html('<link rel="stylesheet" href="/static/vis.css">')

        # Top control bar
        with ui.row().classes('control-bar'):
            ui.html('''
                <button class="ctrl-btn" id="btn-reset">⏮</button>
                <button class="ctrl-btn" id="btn-play">▶</button>
                <button class="ctrl-btn" id="btn-prev">&lt;</button>
                <input  class="seek-slider" id="seek-slider" type="range" min="0" value="0">
                <button class="ctrl-btn" id="btn-next">&gt;</button>
                <span   class="tick-label" id="tick-label">Tick 0</span>
            ''')
            ui.label(f'{meta["map"]}  ·  {meta["total_ticks"]} ticks @ {meta["tickrate"]:.0f} Hz').classes('meta-label')

        # Main container: game panel (fixed width) + entity panel (flex-grow)
        ui.html(f'''
            <div class="main-container">
                <div class="game-panel" style="width: {w}px;">
                    <canvas id="game-canvas" width="{w}" height="{h}"></canvas>
                    <div id="field-panel"></div>
                </div>
                <div class="entity-panel">
                    <div id="entity-grid"></div>
                </div>
            </div>
        ''')

        # Bootstrap JS with meta already embedded (avoids an extra fetch)
        ui.add_body_html(
            f'<script>const _META = {json.dumps(meta)};</script>'
            '<script src="/static/visualizer.js"></script>'
        )

    ui.run(
        title=f'Rollout — {meta["map"]}',
        port=8080,
        show=True,
        reload=False,
        favicon='🎮',
    )


if __name__ == '__main__':
    main()
