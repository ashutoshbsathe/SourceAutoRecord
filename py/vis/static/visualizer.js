/**
 * Rollout Visualizer — Client JS
 *
 * Responsibilities:
 *   - Build entity grid from /meta (sorted by update count, injected as _META)
 *   - Playback loop: fetch /frame/{n} + /tick/{n} in parallel per tick
 *   - Draw game frame on <canvas>, then draw HUD overlay on top
 *   - Flash changed entity cells (CSS class toggle)
 *   - Show changed fields on hover in field panel
 *
 * Controls: Space=play/pause, Left/Right arrows=step, ,/.=same
 */

'use strict';

// _META is injected by visualize.py before this script loads
const META = _META;

// --- State ---
let currentTick   = 0;
let isPlaying     = false;
let playTimer     = null;
let hoveredIdx    = null;  // entity idx currently hovered
let lastTickData  = null;  // last fetched tick JSON

const cells = {};  // entityIdx -> DOM element

// --- Init ---
// NiceGUI mounts Vue components asynchronously — poll until all elements exist.
function init() {
    const ids = ['entity-grid', 'game-canvas', 'field-panel', 'seek-slider', 'btn-play', 'btn-prev', 'btn-next'];
    if (ids.some(id => !document.getElementById(id))) {
        setTimeout(init, 50);
        return;
    }
    buildEntityGrid();
    setupControls();
    clearFieldPanel();
    document.getElementById('seek-slider').max = META.total_ticks - 1;
    advanceTick(0);
}

document.addEventListener('DOMContentLoaded', init);

// --- Entity grid ---

function entityFamily(cls) {
    if (cls === 'player')              return 'fam-player';
    if (cls.includes('portal'))        return 'fam-portal';
    if (cls.includes('cube') || cls.startsWith('phys')) return 'fam-physics';
    if (cls === 'worldspawn' || cls.startsWith('func_') || cls.startsWith('trigger_')) return 'fam-static';
    return 'fam-default';
}

function buildEntityGrid() {
    const grid = document.getElementById('entity-grid');

    META.entities.forEach(ent => {
        const cell = document.createElement('div');
        cell.className = `entity-cell ${entityFamily(ent.class_name)}`;
        cell.title     = `${ent.class_name} [idx:${ent.idx}]\nTotal updates: ${ent.total_updates}  (#${ent.rank})`;
        cell.innerHTML = `<span class="cell-label">${ent.class_name.slice(0, 9)}</span>`
                       + `<span class="cell-rank">#${ent.rank}</span>`;

        cell.addEventListener('mouseenter', () => {
            hoveredIdx = ent.idx;
            refreshFieldPanel();
        });
        cell.addEventListener('mouseleave', () => {
            hoveredIdx = null;
            clearFieldPanel();
        });

        grid.appendChild(cell);
        cells[ent.idx] = cell;
    });
}

// --- Playback ---

function play() {
    if (isPlaying) return;
    isPlaying = true;
    document.getElementById('btn-play').textContent = '⏸';
    document.getElementById('btn-play').classList.add('playing');

    const frameMs = 1000 / META.tickrate;

    const step = async () => {
        if (!isPlaying) return;
        const next = currentTick + 1;
        if (next < META.total_ticks) {
            await advanceTick(next);
            playTimer = setTimeout(step, frameMs);
        } else {
            pause();
        }
    };
    step();
}

function pause() {
    isPlaying = false;
    clearTimeout(playTimer);
    document.getElementById('btn-play').textContent = '▶';
    document.getElementById('btn-play').classList.remove('playing');
}

function togglePlay() { isPlaying ? pause() : play(); }

function seekTo(n) {
    pause();
    advanceTick(Math.max(0, Math.min(n, META.total_ticks - 1)));
}


// --- Core tick advance ---


let latestRequestedTick = 0;

async function advanceTick(n) {
    latestRequestedTick = n;
    currentTick = n;

    try {
        // Parallel fetch: frame PNG + tick JSON
        const [frameResp, tickResp] = await Promise.all([
            fetch(`/frame/${n}`),
            fetch(`/tick/${n}`),
        ]);

        // Read both bodies in parallel so HUD gets current-tick data
        const [tickData, blob] = await Promise.all([
            tickResp.ok  ? tickResp.json()  : Promise.resolve(null),
            frameResp.ok ? frameResp.blob() : Promise.resolve(null),
        ]);

        // If a newer tick has been requested since this fetch started, discard this stale response.
        if (latestRequestedTick !== n) {
            return;
        }

        // Clear previous tick's flashes
        document.querySelectorAll('.entity-cell.flash').forEach(c => c.classList.remove('flash'));

        // Update entity state first (HUD needs it)
        if (tickData) {
            lastTickData = tickData;

            lastTickData.entities.forEach(ent => {
                const cell = cells[ent.idx];
                if (!cell) return;
                void cell.offsetWidth;  // force reflow so re-adding flash triggers transition
                cell.classList.add('flash');
            });

            if (hoveredIdx !== null) refreshFieldPanel();
        }

        // Draw frame + HUD with current tick's data
        if (blob) {
            const bitmap = await createImageBitmap(blob);
            const canvas = document.getElementById('game-canvas');
            canvas.width  = bitmap.width;
            canvas.height = bitmap.height;
            const ctx = canvas.getContext('2d');
            ctx.drawImage(bitmap, 0, 0);
            if (lastTickData) drawHUD(ctx, lastTickData, bitmap.width, bitmap.height);
        }

        // Update seek slider + tick label
        document.getElementById('seek-slider').value = n;
        document.getElementById('tick-label').textContent =
            `Tick ${lastTickData?.tick ?? n}  /  ${META.total_ticks - 1}`;

    } catch (err) {
        console.error(`advanceTick(${n}) failed:`, err);
    }
}

// --- Field panel ---

function clearFieldPanel() {
    document.getElementById('field-panel').innerHTML =
        '<div class="fp-empty">Hover an entity cell to inspect fields</div>';
}

function refreshFieldPanel() {
    if (!lastTickData || hoveredIdx === null) return;

    const panel = document.getElementById('field-panel');
    const ent   = lastTickData.entities.find(e => e.idx === hoveredIdx);

    // Find static info from META
    const meta_ent = META.entities.find(e => e.idx === hoveredIdx);
    const cls      = meta_ent?.class_name ?? 'unknown';

    if (!ent || Object.keys(ent.fields).length === 0) {
        panel.innerHTML =
            `<div class="fp-header">${cls} <span class="fp-sub">[idx:${hoveredIdx}]</span></div>`
            + '<div class="fp-empty">No field changes this tick</div>';
        return;
    }

    const rows = Object.entries(ent.fields).map(([name, val]) => {
        const str = Array.isArray(val)
            ? val.map(v => v.toFixed(3)).join(', ')
            : String(val);
        return `<div class="field-row">
                    <span class="field-name">${name}</span>
                    <span class="field-val">${str}</span>
                </div>`;
    }).join('');

    panel.innerHTML =
        `<div class="fp-header">${cls} <span class="fp-sub">[idx:${hoveredIdx}] — ${Object.keys(ent.fields).length} changed</span></div>`
        + rows;
}

// --- HUD overlay (replicates visualize_rollout.py's draw_hud) ---

function drawHUD(ctx, tick, W, H) {
    const act = tick.action;
    const pos = tick.pos;

    // Top bar
    ctx.fillStyle = 'rgba(15,18,22,0.82)';
    ctx.fillRect(0, 0, W, 46);
    ctx.fillStyle = '#d0e0f8';
    ctx.font = '11px monospace';
    ctx.fillText(
        `Tick ${tick.tick}   X ${pos.x.toFixed(1)}  Y ${pos.y.toFixed(1)}  Z ${pos.z.toFixed(1)}`,
        12, 18
    );

    // WASD layout
    const kw = 30, kh = 30, gap = 5;
    const bx = 15, by = H - 90;

    const keys = [
        { x: bx + kw + gap,          y: by,        w: kw,       h: kh, label: 'W',    on: act.forward   },
        { x: bx,                      y: by+kh+gap, w: kw,       h: kh, label: 'A',    on: act.left      },
        { x: bx + kw + gap,          y: by+kh+gap, w: kw,       h: kh, label: 'S',    on: act.backward  },
        { x: bx + 2*(kw+gap),        y: by+kh+gap, w: kw,       h: kh, label: 'D',    on: act.right     },
        { x: bx + 3.3*(kw+gap),      y: by,        w: kw*2.2,   h: kh, label: 'JUMP', on: act.jump      },
        { x: bx + 3.3*(kw+gap),      y: by+kh+gap, w: kw*2.2,   h: kh, label: 'DUCK', on: act.crouch    },
        { x: bx + 5.7*(kw+gap),      y: by,        w: kw*1.5,   h: kh, label: 'USE',  on: act.use       },
    ];

    ctx.textAlign = 'center';
    keys.forEach(k => {
        ctx.fillStyle   = k.on ? '#50f064' : '#1e2328';
        ctx.strokeStyle = k.on ? '#78ff8c' : '#3c4650';
        ctx.lineWidth   = 1;
        roundRect(ctx, k.x, k.y, k.w, k.h, 3, true, true);
        ctx.fillStyle = k.on ? '#0a140a' : '#909aa4';
        ctx.font      = `${k.on ? 'bold ' : ''}10px monospace`;
        ctx.fillText(k.label, k.x + k.w / 2, k.y + k.h / 2 + 4);
    });

    // Portal buttons
    const px = W - 180, py = by;
    const portals = [
        { x: px,    y: py, label: 'BLUE', on: act.portal_primary,   bg: '#1e70ff', off: '#191e28' },
        { x: px+50, y: py, label: 'ORNG', on: act.portal_secondary,  bg: '#f0a020', off: '#28201a' },
    ];
    portals.forEach(p => {
        ctx.fillStyle   = p.on ? p.bg : p.off;
        ctx.strokeStyle = p.on ? p.bg : '#3a3a44';
        ctx.lineWidth   = 1;
        roundRect(ctx, p.x, p.y, 44, 30, 3, true, true);
        ctx.fillStyle = '#ffffff';
        ctx.font      = '10px monospace';
        ctx.fillText(p.label, p.x + 22, p.y + 19);
    });

    ctx.textAlign = 'left';

    // Mouse vector pad
    const sq = 60, sx = W - sq - 15, sy = H - sq - 15;
    const cx = sx + sq / 2, cy = sy + sq / 2;
    ctx.fillStyle   = '#0e1014';
    ctx.strokeStyle = '#3a4455';
    ctx.lineWidth   = 1;
    roundRect(ctx, sx, sy, sq, sq, 4, true, true);

    // Crosshair
    ctx.strokeStyle = '#282e3c';
    ctx.beginPath(); ctx.moveTo(cx, sy+5); ctx.lineTo(cx, sy+sq-5); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(sx+5, cy); ctx.lineTo(sx+sq-5, cy); ctx.stroke();

    // Vector dot
    const dx = Math.max(-1, Math.min(1, act.mouse_dx));
    const dy = Math.max(-1, Math.min(1, act.mouse_dy));
    const dotX = cx + dx * (sq / 2 - 4);
    const dotY = cy + dy * (sq / 2 - 4);
    ctx.strokeStyle = '#28dcff';
    ctx.lineWidth   = 2;
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(dotX, dotY); ctx.stroke();
    ctx.fillStyle = '#00ffff';
    ctx.beginPath(); ctx.arc(dotX, dotY, 3, 0, Math.PI * 2); ctx.fill();
}

function roundRect(ctx, x, y, w, h, r, fill, stroke) {
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.lineTo(x + w - r, y);
    ctx.quadraticCurveTo(x + w, y, x + w, y + r);
    ctx.lineTo(x + w, y + h - r);
    ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
    ctx.lineTo(x + r, y + h);
    ctx.quadraticCurveTo(x, y + h, x, y + h - r);
    ctx.lineTo(x, y + r);
    ctx.quadraticCurveTo(x, y, x + r, y);
    ctx.closePath();
    if (fill)   ctx.fill();
    if (stroke) ctx.stroke();
}

// --- Keyboard controls ---

function setupControls() {
    document.getElementById('btn-play').addEventListener('click', togglePlay);
    document.getElementById('btn-reset').addEventListener('click', () => seekTo(0));
    document.getElementById('btn-prev').addEventListener('click', () => {
        pause();
        advanceTick(Math.max(currentTick - 1, 0));
    });
    document.getElementById('btn-next').addEventListener('click', () => {
        pause();
        advanceTick(Math.min(currentTick + 1, META.total_ticks - 1));
    });
    document.getElementById('seek-slider').addEventListener('input', e => {
        seekTo(parseInt(e.target.value, 10));
    });

    document.addEventListener('keydown', e => {
        if (e.target.tagName === 'INPUT') return;
        switch (e.code) {
            case 'Space':       e.preventDefault(); togglePlay(); break;
            case 'ArrowRight':
            case 'Period':      pause(); advanceTick(Math.min(currentTick + 1, META.total_ticks - 1)); break;
            case 'ArrowLeft':
            case 'Comma':       pause(); advanceTick(Math.max(currentTick - 1, 0)); break;
        }
    });
}


