"""Render a `.trajectory` into one self-contained, aesthetic HTML solve report.

Usage:
    uv run python py/llm_eval/vis_trajectory.py <path/to/x.trajectory> [-o out.html] [--no-open]

Everything is inlined (frames base64, CSS, rendered markdown) so the output is a
single offline-forever file. The model's thinking trace is rendered with
markdown-it-py; JSON with pygments. One vertical scroll of decision cards: each
shows the frame, the percept, the reasoning + full thinking, the verb, the
result (failures in red), the token split, and any rejected retries.
"""

# ruff: noqa: E501  -- the inlined CSS/JS/HTML strings are long by nature

import argparse
import base64
import html
import json
import math
import sys
from pathlib import Path

from markdown_it import MarkdownIt
from mdit_py_plugins.tasklists import tasklists_plugin
from pygments import highlight
from pygments.formatters import HtmlFormatter
from pygments.lexers import JsonLexer

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from p2harness import harness_pb2  # noqa: E402

from llm_eval.trajectory_io import read_trajectory  # noqa: E402

_MD = (
    MarkdownIt('commonmark', {'html': False, 'typographer': True})
    .enable(['table', 'strikethrough'])
    .use(tasklists_plugin)
)
_FMT = HtmlFormatter(style='github-dark', cssclass='hl')
_PYGMENTS_CSS = _FMT.get_style_defs('.hl')


def _md(text):
    """Render markdown text to a styled HTML block."""
    return f'<div class="md">{_MD.render(text or "")}</div>'


def _json_block(text):
    """Syntax-highlight a JSON string (lenient -- bad JSON still lexes)."""
    return highlight(text or '', JsonLexer(), _FMT)


def _pre(text):
    """Escape plain text into a <pre> block."""
    return f'<pre class="plain">{html.escape(text or "")}</pre>'


def _action(call):
    """Decode a call's serialized MacroRequest."""
    m = harness_pb2.MacroRequest()
    m.ParseFromString(call.action)
    return m


def _result(call):
    """Decode a call's serialized MacroResult."""
    r = harness_pb2.MacroResult()
    r.ParseFromString(call.result)
    return r


def _accepted(step):
    """The step's accepted Call, or None if it gave up."""
    return next((c for c in step.calls if c.accepted), None)


def _verb(action):
    """A verb plus its non-default args, e.g. 'go_to mark=7' (empty verb = gave up)."""
    if not action.verb:
        return '(no valid action)'
    args = ' '.join(f'{f.name}={v}' for f, v in action.ListFields() if f.name != 'verb')
    return f'{action.verb} {args}'.strip()


def _result_extras(result):
    """Non-default result fields beyond ok/result_code/detail, as 'k=v · …'."""
    out = []
    for f, v in result.ListFields():
        if f.name in ('ok', 'result_code', 'detail'):
            continue
        out.append(f'{f.name}={v:.1f}' if isinstance(v, float) else f'{f.name}={v}')
    return ' · '.join(out)


def _tokens(u):
    """The 'in N (img M) · out N (think M) · cached N' token line."""
    s = f'in {u.input}' + (f' (img {u.image})' if u.image else '')
    s += f' · out {u.output}' + (f' (think {u.thinking})' if u.thinking else '')
    return s + (f' · cached {u.cached}' if u.cached else '')


def _frame(obs, idx):
    """The annotated frame as a clickable inline <img>, or a placeholder."""
    if not obs.frame_png:
        return '<div class="noframe">no frame captured</div>'
    uri = 'data:image/png;base64,' + base64.b64encode(obs.frame_png).decode('ascii')
    return (
        f'<img class="frame" src="{uri}" loading="lazy" alt="step {idx} frame" '
        'onclick="lightbox(this.src)">'
    )


def _is_target(mark, target):
    """True if a percept mark (int entity, or 'S1'/'Pb' panel/portal) is the
    action's target -- a percept label string, so compare as strings."""
    return bool(target) and str(mark) == str(target)


def _percept(marks, target):
    """One row per mark; the action's target mark is highlighted."""
    rows = []
    for m in marks:
        x, y, z = m.get('pos') or [0, 0, 0]
        state = m.get('state') or {}
        chips = ''.join(
            f'<span class="chip">{html.escape(f"{k}:{v}")}</span>'
            for k, v in state.items()
        )
        hot = ' hot' if _is_target(m.get('mark'), target) else ''
        rows.append(
            f'<div class="mark{hot}"><span class="mn">[{m.get("mark")}]</span>'
            f'<span class="mc">{html.escape(m.get("class", ""))}</span>'
            f'<span class="mp">({x:.0f},{y:.0f},{z:.0f}) · d={m.get("dist", 0):.0f} '
            f'b={m.get("bearing", 0):.0f}</span>'
            f'<span class="ms">{chips}</span></div>'
        )
    return ''.join(rows)


def _projector(header, steps):
    """Build a world->SVG projection covering every player/mark point."""
    xs, ys = [], []
    for s in steps:
        xs.append(s.obs.player.x)
        ys.append(s.obs.player.y)
        for m in json.loads(s.obs.percept_json or '[]'):
            p = m.get('pos') or [0, 0, 0]
            xs.append(p[0])
            ys.append(p[1])
    if not xs:  # no player/mark points to bound -- fall back to the origin
        xs, ys = [0.0], [0.0]
    w, h, pad = 300, 210, 22
    minx, miny = min(xs), min(ys)
    spanx, spany = max(max(xs) - minx, 1.0), max(max(ys) - miny, 1.0)
    scale = min((w - 2 * pad) / spanx, (h - 2 * pad) / spany)
    ox = pad + ((w - 2 * pad) - spanx * scale) / 2
    oy = pad + ((h - 2 * pad) - spany * scale) / 2

    def proj(x, y):
        return ox + (x - minx) * scale, h - (oy + (y - miny) * scale)

    return proj, (w, h)


def _map(header, steps, idx, ctx):
    """A top-down SVG: path so far, marks, player + facing arrow."""
    try:
        proj, (w, h) = ctx
        s = steps[idx]
        acc = _accepted(s)
        target = _action(acc).target if acc else ''
        out = [f'<svg class="map" viewBox="0 0 {w} {h}">']
        pts = ' '.join(
            '%.1f,%.1f' % proj(steps[i].obs.player.x, steps[i].obs.player.y)
            for i in range(idx + 1)
        )
        out.append(f'<polyline points="{pts}" class="path"/>')
        for m in json.loads(s.obs.percept_json or '[]'):
            p = m.get('pos') or [0, 0, 0]
            mx, my = proj(p[0], p[1])
            cls = 'mk hot' if _is_target(m.get('mark'), target) else 'mk'
            out.append(f'<circle cx="{mx:.1f}" cy="{my:.1f}" r="3.5" class="{cls}"/>')
            out.append(
                f'<text x="{mx + 5:.1f}" y="{my + 3:.1f}" class="mlbl">{m.get("mark")}</text>'
            )
        px, py = proj(s.obs.player.x, s.obs.player.y)
        yaw = math.radians(s.obs.eye_yaw)
        fx, fy = proj(
            s.obs.player.x + math.cos(yaw) * 45, s.obs.player.y + math.sin(yaw) * 45
        )
        out.append(
            f'<line x1="{px:.1f}" y1="{py:.1f}" x2="{fx:.1f}" y2="{fy:.1f}" class="facing"/>'
        )
        out.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="4.5" class="player"/>')
        out.append('</svg>')
        return ''.join(out)
    except Exception:
        return ''


def _render_call(call, j):
    """Render one LLM call -- accepted (green) or rejected (amber) -- uniformly."""
    acc = call.accepted
    r = _result(call) if acc else None
    good = bool(r.ok) if r else False
    if acc:
        label = f'✓ {html.escape(_verb(_action(call)))}'
        extras = _result_extras(r)
        outcome = (
            f'<div class="result {"ok" if good else "bad"}">'
            f'<span class="rc">{"✓" if good else "✗"} {html.escape(r.result_code)}</span> '
            f'<span class="detail">{html.escape(r.detail)}</span>'
            f'{(" · " + html.escape(extras)) if extras else ""}'
            f'<span class="tok">{_tokens(call.usage)}</span></div>'
        )
    else:
        label = f'try {j + 1} · ✗ rejected'
        outcome = (
            f'<div class="result bad"><span class="rc">✗ rejected</span> '
            f'<span class="reason">{html.escape(call.rejection_reason)}</span>'
            f'<span class="tok">{_tokens(call.usage)}</span></div>'
        )
    reasoning = (
        f'<div class="reasoning"><span class="rlbl">reasoning</span> '
        f'{html.escape(call.reasoning)}</div>'
        if call.reasoning
        else ''
    )
    open_attr = '' if (acc and good) else ' open'
    think = (
        f'<details class="think"{open_attr}>'
        f'<summary>🧠 thinking · {len(call.thinking)} chars</summary>{_md(call.thinking)}</details>'
        if call.thinking
        else ''
    )
    prompt = (
        f'<details class="io"><summary>prompt sent · {len(call.prompt_sent)} chars</summary>'
        f'{_pre(call.prompt_sent)}</details>'
        if call.prompt_sent
        else ''
    )
    response = (
        f'<details class="io"><summary>raw response · {len(call.raw_response)} chars</summary>'
        f'{_json_block(call.raw_response)}</details>'
        if call.raw_response
        else ''
    )
    return (
        f'<div class="call {"ok" if acc else "bad"}"><div class="call-h">{label}</div>'
        f'{reasoning}{think}{outcome}<div class="io-row">{prompt}{response}</div></div>'
    )


def _card(header, steps, idx, ctx):
    """Render one decision step (its observation + every LLM call) into a card."""
    s = steps[idx]
    obs = s.obs
    marks = json.loads(obs.percept_json or '[]')
    acc = _accepted(s)
    a = _action(acc) if acc else None
    r = _result(acc) if acc else None
    ok = bool(r.ok) if r else False
    target = (a.target if a else '') or None
    verb = html.escape(_verb(a)) if a else '(no valid action)'
    badge = (
        f'<span class="badge {"ok" if ok else "bad"}">'
        f'{"✓" if ok else "✗"} {html.escape(r.result_code)}</span>'
        if acc
        else '<span class="badge bad">✗ gave up</span>'
    )
    held = f' · held [{obs.held_mark}]' if obs.held_mark else ''
    term = (
        f'<span class="term {"ok" if s.terminal in ("SOLVED", "DONE") else "bad"}">'
        f'{s.terminal}</span>'
        if s.terminal
        else ''
    )
    n = len(s.calls)
    calls_h = (
        '' if n == 1 else f'<div class="calls-h">{n} calls · {n - 1} rejected</div>'
    )
    calls = ''.join(_render_call(c, j) for j, c in enumerate(s.calls))

    return f"""
<section class="card" data-ok="{str(ok).lower()}" id="step-{idx}">
  <header class="ch">
    <span class="idx">STEP {idx}</span>
    <span class="verb">{verb}</span>
    <span class="arrow">→</span>
    {badge}
    {term}
  </header>
  <div class="grid">
    <div class="frame-col">{_frame(obs, idx)}
      <div class="cap">player ({obs.player.x:.0f},{obs.player.y:.0f},{obs.player.z:.0f}) · yaw {obs.eye_yaw:.0f}°{held}</div>
    </div>
    <div class="percept-col"><div class="colh">PERCEPT · {len(marks)} marks</div>{_percept(marks, target)}</div>
    <div class="map-col">{_map(header, steps, idx, ctx)}</div>
  </div>
  {calls_h}
  {calls}
</section>"""


def build_html(header, steps):
    """Render the whole trajectory into one self-contained HTML string."""
    ctx = _projector(header, steps)
    cards = ''.join(_card(header, steps, i, ctx) for i in range(len(steps)))
    fails = sum(1 for s in steps if not (_accepted(s) and _result(_accepted(s)).ok))
    ti = sum(c.usage.input for s in steps for c in s.calls)
    to = sum(c.usage.output for s in steps for c in s.calls)
    tc = sum(c.usage.cached for s in steps for c in s.calls)
    term = steps[-1].terminal if steps else ''
    tcls = 'ok' if term in ('SOLVED', 'DONE') else ('bad' if term else 'neutral')
    hdr_extras = ''
    if header.system_prompt:
        hdr_extras += (
            f'<details class="hd"><summary>system prompt · {len(header.system_prompt)}c</summary>'
            f'{_pre(header.system_prompt)}</details>'
        )
    if header.grammar:
        hdr_extras += (
            f'<details class="hd"><summary>grammar · {len(header.grammar)}c</summary>'
            f'{_pre(header.grammar)}</details>'
        )

    head = (
        f'<header class="top"><div class="title">'
        f'<span class="map">{html.escape(header.map)}</span>'
        f'<span class="sep">·</span><span class="model">{html.escape(header.model or "scripted")}</span>'
        f'<span class="outcome {tcls}">{term or "in progress"}</span></div>'
        f'<div class="stats">{len(steps)} steps · '
        f'<span class="{"bad" if fails else "muted"}">{fails} failed</span> · '
        f'Σ in {ti:,} / out {to:,}' + (f' / cached {tc:,}' if tc else '') + '</div>'
        f'<div class="controls">'
        f'<button onclick="allThinking(true)">expand thinking</button>'
        f'<button onclick="allThinking(false)">collapse thinking</button>'
        f'<label class="chk"><input type="checkbox" onchange="failsOnly(this.checked)"> fails only</label>'
        f'{hdr_extras}</div></header>'
    )

    return (
        '<!doctype html><html lang="en"><head><meta charset="utf-8">'
        f'<title>{html.escape(header.map)} · trajectory</title>'
        '<meta name="viewport" content="width=device-width, initial-scale=1">'
        '<style>'
        + _CSS
        + _PYGMENTS_CSS
        + '</style></head><body>'
        + head
        + f'<main>{cards}</main>'
        + '<div id="lb" onclick="closeLb()"><img alt="frame full"></div>'
        + '<script>'
        + _JS
        + '</script></body></html>'
    )


def main():
    """CLI: read a .trajectory, write a self-contained .html, open it."""
    ap = argparse.ArgumentParser(
        description='Render a .trajectory to a static HTML report.'
    )
    ap.add_argument('trajectory', help='path to a .trajectory file')
    ap.add_argument('-o', '--out', help='output .html (default: <trajectory>.html)')
    ap.add_argument(
        '--no-open',
        action='store_true',
        help='write the file but do not open a browser',
    )
    args = ap.parse_args()

    header, steps = read_trajectory(args.trajectory)
    page = build_html(header, steps)
    out = args.out or args.trajectory + '.html'
    Path(out).write_text(page, encoding='utf-8')
    print(f'wrote {out}  ({len(page) / 1e6:.1f} MB · {len(steps)} steps)')
    if not args.no_open:
        import webbrowser

        webbrowser.open(Path(out).resolve().as_uri())


_CSS = """
:root{
  --bg:#0a0c10; --panel:#11151c; --panel2:#0d1117; --card:#12161f; --line:#1e2530;
  --ink:#d7dee8; --dim:#8b97a8; --faint:#5b6675;
  --accent:#5b9cff; --green:#3fb950; --red:#f85149; --amber:#d29922; --hot:#ffd24a;
  --radius:14px;
}
*{box-sizing:border-box}
html{scroll-behavior:smooth}
body{
  margin:0; background:radial-gradient(1200px 600px at 70% -10%,#121826 0,var(--bg) 60%);
  color:var(--ink); font:15px/1.55 system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;
  -webkit-font-smoothing:antialiased;
}
.mono,.cap,.tok,.cost,.mp,.detail,pre,.hl{
  font-family:ui-monospace,'JetBrains Mono','SF Mono',Menlo,Consolas,monospace;
}
a{color:var(--accent)}

.top{
  position:sticky; top:0; z-index:20; padding:14px 22px;
  background:rgba(10,12,16,.78); backdrop-filter:blur(12px) saturate(1.4);
  border-bottom:1px solid var(--line);
}
.title{display:flex; align-items:center; gap:10px; font-size:18px; font-weight:650}
.title .sep{color:var(--faint)}
.title .model{color:var(--dim); font-weight:500}
.outcome{margin-left:auto; padding:3px 12px; border-radius:999px; font-size:13px; font-weight:600;
  border:1px solid var(--line)}
.outcome.ok{color:var(--green); background:rgba(63,185,80,.12); border-color:rgba(63,185,80,.35)}
.outcome.bad{color:var(--red); background:rgba(248,81,73,.12); border-color:rgba(248,81,73,.35)}
.outcome.neutral{color:var(--dim)}
.stats{color:var(--dim); font-size:13px; margin-top:5px}
.stats .bad{color:var(--red); font-weight:600}
.controls{display:flex; align-items:center; gap:8px; margin-top:10px; flex-wrap:wrap}
.controls button,.chk{
  font:inherit; font-size:13px; color:var(--ink); background:var(--panel); cursor:pointer;
  border:1px solid var(--line); border-radius:8px; padding:5px 11px;
}
.controls button:hover{border-color:var(--accent); color:#fff}
.chk{display:inline-flex; align-items:center; gap:6px}
.hd{margin-left:4px}
.hd>summary{cursor:pointer; color:var(--dim); font-size:12.5px; padding:5px 8px;
  border:1px solid var(--line); border-radius:8px; list-style:none; user-select:none}
.hd[open]>summary{color:var(--ink)}
.hd>pre{margin:8px 0 0}

main{max-width:1180px; margin:24px auto 120px; padding:0 22px; display:flex; flex-direction:column; gap:20px}

.card{
  background:linear-gradient(180deg,var(--card),#0f131b); border:1px solid var(--line);
  border-radius:var(--radius); padding:18px 20px; box-shadow:0 1px 0 rgba(255,255,255,.02),0 14px 40px -28px #000;
  scroll-margin-top:120px;
}
.card[data-ok="false"]{border-left:4px solid var(--red); background:linear-gradient(180deg,#1a1216,#120f14)}
body.fails-only .card[data-ok="true"]{display:none}

.ch{display:flex; align-items:center; gap:12px; margin-bottom:14px}
.ch .idx{font-size:12px; letter-spacing:.12em; color:var(--faint); font-weight:700}
.ch .verb{font-family:ui-monospace,monospace; font-size:15px; font-weight:600; color:#eef3fb}
.ch .arrow{color:var(--faint)}
.badge{padding:2px 10px; border-radius:999px; font-size:12.5px; font-weight:700; font-family:ui-monospace,monospace}
.badge.ok{color:var(--green); background:rgba(63,185,80,.14)}
.badge.bad{color:var(--red); background:rgba(248,81,73,.16)}
.term{margin-left:auto; padding:2px 11px; border-radius:999px; font-size:12px; font-weight:700; letter-spacing:.05em}
.term.ok{color:var(--green); background:rgba(63,185,80,.14)}
.term.bad{color:var(--amber); background:rgba(210,153,34,.16)}

.grid{display:grid; grid-template-columns:minmax(0,1.3fr) minmax(0,1fr) auto; gap:18px; align-items:start}
@media(max-width:900px){.grid{grid-template-columns:1fr}}
.frame .cap{}
.frame{width:100%; border-radius:10px; border:1px solid var(--line); display:block; cursor:zoom-in}
.noframe{aspect-ratio:4/3; display:grid; place-items:center; color:var(--faint);
  border:1px dashed var(--line); border-radius:10px; font-size:13px}
.cap{color:var(--dim); font-size:12px; margin-top:7px}

.colh{font-size:11px; letter-spacing:.12em; color:var(--faint); font-weight:700; margin-bottom:8px}
.mark{display:grid; grid-template-columns:34px 1fr; grid-template-areas:"n c" "n p" "n s";
  gap:0 8px; padding:6px 8px; border-radius:8px; margin-bottom:4px; border:1px solid transparent}
.mark.hot{background:rgba(91,156,255,.10); border-color:rgba(91,156,255,.30)}
.mark .mn{grid-area:n; font-family:ui-monospace,monospace; color:var(--accent); font-weight:700}
.mark.hot .mn{color:var(--hot)}
.mark .mc{grid-area:c; color:var(--ink); font-size:13.5px}
.mark .mp{grid-area:p; color:var(--dim); font-size:11.5px}
.mark .ms{grid-area:s; display:flex; flex-wrap:wrap; gap:4px; margin-top:3px}
.chip{font-family:ui-monospace,monospace; font-size:10.5px; color:var(--dim);
  background:var(--panel2); border:1px solid var(--line); border-radius:6px; padding:1px 6px}

.map{width:300px; max-width:100%; background:var(--panel2); border:1px solid var(--line); border-radius:10px}
.map .path{fill:none; stroke:var(--accent); stroke-width:1.5; opacity:.6; stroke-linejoin:round}
.map .mk{fill:var(--faint)}
.map .mk.hot{fill:var(--hot)}
.map .mlbl{fill:var(--dim); font:9px ui-monospace,monospace}
.map .player{fill:#fff; stroke:var(--accent); stroke-width:2}
.map .facing{stroke:var(--accent); stroke-width:2}

.reasoning{margin:14px 0 4px; font-size:14.5px}
.reasoning .rlbl{font-size:11px; letter-spacing:.1em; color:var(--faint); font-weight:700; margin-right:8px;
  text-transform:uppercase}

details{margin:10px 0}
details>summary{cursor:pointer; list-style:none; user-select:none; color:var(--dim); font-size:13px;
  padding:7px 11px; border:1px solid var(--line); border-radius:9px; background:var(--panel2);
  transition:color .15s,border-color .15s}
details>summary::-webkit-details-marker{display:none}
details>summary::before{content:'▸'; color:var(--faint); margin-right:8px; display:inline-block; transition:transform .15s}
details[open]>summary::before{transform:rotate(90deg)}
details[open]>summary{color:var(--ink); border-color:var(--accent)}

.think>summary{color:#cdbbf2}
.think[open]>summary{color:#d9c8ff; border-color:rgba(165,131,255,.45)}
.think>.md{margin:10px 0 4px; padding:14px 18px; border-left:3px solid rgba(165,131,255,.5);
  background:rgba(124,92,196,.06); border-radius:0 10px 10px 0; max-width:78ch; color:#c9d2de}

.calls-h{font-size:11px; letter-spacing:.1em; color:var(--faint); font-weight:700; margin:16px 0 6px}
.call{margin:10px 0; padding:12px 14px; border:1px solid var(--line); border-left:3px solid var(--faint);
  border-radius:0 10px 10px 0}
.call.ok{border-left-color:var(--green); background:rgba(63,185,80,.04)}
.call.bad{border-left-color:var(--amber); border-color:rgba(210,153,34,.3); background:rgba(210,153,34,.05)}
.call-h{font-weight:700; font-size:13px; margin-bottom:8px; font-family:ui-monospace,monospace}
.call.ok>.call-h{color:var(--green)}
.call.bad>.call-h{color:var(--amber)}
.result .reason{color:var(--red); font-family:ui-monospace,monospace}

.result{display:flex; align-items:center; flex-wrap:wrap; gap:10px; margin-top:14px; padding:10px 14px;
  border-radius:10px; font-size:13px; background:var(--panel2); border:1px solid var(--line)}
.result.bad{border-color:rgba(248,81,73,.4); background:rgba(248,81,73,.07)}
.result .rc{font-family:ui-monospace,monospace; font-weight:700}
.result.ok .rc{color:var(--green)} .result.bad .rc{color:var(--red)}
.result .detail{color:var(--dim); font-family:ui-monospace,monospace}
.result .tok{margin-left:auto; color:var(--faint); font-size:12px}

.muted{color:var(--faint); font-size:12.5px; margin:10px 0; font-style:italic}
.io-row{display:flex; gap:10px; flex-wrap:wrap; margin-top:6px}
.io-row .io{flex:1; min-width:240px}
pre.plain{margin:8px 0 0; padding:12px 14px; background:var(--panel2); border:1px solid var(--line);
  border-radius:10px; overflow:auto; font-size:12px; color:var(--dim); white-space:pre-wrap; word-break:break-word}
.hl{margin:8px 0 0; padding:12px 14px; border:1px solid var(--line); border-radius:10px; overflow:auto; font-size:12px}
.hl pre{margin:0; background:transparent}

.md h1,.md h2,.md h3,.md h4{margin:14px 0 6px; line-height:1.3; color:#eaf0f8}
.md h1{font-size:1.25em} .md h2{font-size:1.15em} .md h3{font-size:1.05em} .md h4{font-size:1em}
.md p{margin:8px 0}
.md ul,.md ol{margin:8px 0; padding-left:22px}
.md li{margin:3px 0}
.md strong{color:#fff} .md em{color:#e6d8ff}
.md code{font-family:ui-monospace,monospace; font-size:.88em; background:var(--panel2);
  border:1px solid var(--line); border-radius:5px; padding:1px 5px}
.md pre{background:var(--panel2); border:1px solid var(--line); border-radius:8px; padding:12px; overflow:auto}
.md pre code{background:none; border:none; padding:0}
.md blockquote{margin:8px 0; padding:2px 14px; border-left:3px solid var(--line); color:var(--dim)}
.md hr{border:none; border-top:1px solid var(--line); margin:14px 0}
.md table{border-collapse:collapse; margin:8px 0}
.md th,.md td{border:1px solid var(--line); padding:5px 10px}
.md a{text-decoration:underline}

#lb{position:fixed; inset:0; z-index:50; display:none; place-items:center; padding:30px;
  background:rgba(4,6,10,.92); backdrop-filter:blur(6px); cursor:zoom-out}
#lb.on{display:grid}
#lb img{max-width:100%; max-height:100%; border-radius:8px; border:1px solid var(--line);
  box-shadow:0 30px 80px -20px #000}
"""

_JS = """
function allThinking(open){document.querySelectorAll('details.think').forEach(d=>d.open=open);}
function failsOnly(on){document.body.classList.toggle('fails-only',on);}
const lb=document.getElementById('lb');
function lightbox(src){lb.querySelector('img').src=src;lb.classList.add('on');}
function closeLb(){lb.classList.remove('on');}
document.addEventListener('keydown',e=>{if(e.key==='Escape')closeLb();});
"""


if __name__ == '__main__':
    main()
