"""Dump a .trajectory to analyzable text (no PNG bytes), decoding macro protos.

Usage: uv run python -m py.llm_eval.dump_trajectory <in.trajectory> <out_dir>
Writes <out_dir>/<stem>.md (full human dump) and <stem>.summary.json (compact).
"""

import json
import math
import os
import sys

from p2harness import harness_pb2

from . import trajectory_pb2
from .trajectory_io import read_trajectory


def _vec(v):
    return (round(v.x, 1), round(v.y, 1), round(v.z, 1))


def _dist2d(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _decode_action(b):
    if not b:
        return None
    m = harness_pb2.MacroRequest()
    m.ParseFromString(b)
    d = {'verb': m.verb}
    if m.target:
        d['target'] = m.target
    if m.aim:
        d['aim'] = m.aim
    if m.ticks:
        d['ticks'] = m.ticks
    if m.dir:
        d['dir'] = m.dir
    if m.yaw:
        d['yaw'] = m.yaw
    if m.pitch:
        d['pitch'] = m.pitch
    return d


def _decode_result(b):
    if not b:
        return None
    r = harness_pb2.MacroResult()
    r.ParseFromString(b)
    return {
        'ok': r.ok,
        'result_code': r.result_code,
        'detail': r.detail,
        'reached': r.reached,
        'final_dist': round(r.final_dist, 1),
        'moved_dist': round(r.moved_dist, 1),
        'aim_yaw': round(r.aim_yaw, 1),
        'aim_pitch': round(r.aim_pitch, 1),
    }


def main():
    in_path, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(in_path))[0]
    header, steps = read_trajectory(in_path)

    md = []
    summ = {
        'file': in_path,
        'map': header.map,
        'model': header.model,
        'grammar': header.grammar,
        'system_prompt': header.system_prompt,
        'n_steps': len(steps),
        'steps': [],
    }

    md.append(f'# {stem}\n')
    md.append(f'- map: `{header.map}`')
    md.append(f'- model: `{header.model}`')
    md.append(f'- n_steps: {len(steps)}')
    md.append(f'- terminal: `{steps[-1].terminal if steps else "?"}`')
    md.append('\n## System prompt\n')
    md.append('```')
    md.append(header.system_prompt)
    md.append('```')
    md.append('\n## Grammar\n')
    md.append('```')
    md.append(header.grammar)
    md.append('```\n')

    start = None
    tot_in = tot_out = tot_think = tot_img = 0
    n_calls = n_rejected = 0
    verb_counts = {}
    code_counts = {}

    for st in steps:
        p = _vec(st.obs.player)
        if start is None:
            start = p
        from_start = round(_dist2d(p, start), 1)
        sj = {
            'index': st.index,
            'player': p,
            'eye_yaw': round(st.obs.eye_yaw, 1),
            'held_mark': st.obs.held_mark,
            'from_start_2d': from_start,
            'terminal': st.terminal,
            'has_frame': bool(st.obs.frame_png),
            'frame_bytes': len(st.obs.frame_png),
            'calls': [],
        }
        try:
            marks = json.loads(st.obs.percept_json)
        except Exception:
            marks = []
        sj['n_marks'] = len(marks)
        sj['marks'] = marks

        md.append(f'\n---\n\n## Step {st.index}  (terminal=`{st.terminal}`)\n')
        md.append(
            f'- player={p}  eye_yaw={sj["eye_yaw"]}  held_mark={st.obs.held_mark}  '
            f'from_start_2d={from_start}  frame={"yes" if st.obs.frame_png else "NO"}'
        )
        md.append(f'- marks seen ({len(marks)}):')
        for m in marks:
            mp = tuple(round(c, 1) for c in m.get('pos', []))
            md.append(
                f'    [{m.get("mark")}] {m.get("class")} pos={mp} '
                f'dist={round(m.get("dist", 0), 1)} bearing={round(m.get("bearing", 0), 1)} '
                f'state={m.get("state")}'
            )

        for ci, c in enumerate(st.calls):
            n_calls += 1
            if not c.accepted:
                n_rejected += 1
            tot_in += c.usage.input
            tot_out += c.usage.output
            tot_think += c.usage.thinking
            tot_img += c.usage.image
            action = _decode_action(c.action)
            result = _decode_result(c.result)
            if c.accepted and action:
                verb_counts[action['verb']] = verb_counts.get(action['verb'], 0) + 1
            if result and result['result_code']:
                code_counts[result['result_code']] = (
                    code_counts.get(result['result_code'], 0) + 1
                )
            cj = {
                'i': ci,
                'accepted': c.accepted,
                'rejection_reason': c.rejection_reason,
                'reasoning': c.reasoning,
                'thinking': c.thinking,
                'raw_response': c.raw_response,
                'prompt_sent': c.prompt_sent,
                'usage': {
                    'in': c.usage.input,
                    'out': c.usage.output,
                    'think': c.usage.thinking,
                    'img': c.usage.image,
                },
                'action': action,
                'result': result,
            }
            sj['calls'].append(cj)

            tag = 'ACCEPTED' if c.accepted else f'REJECTED({c.rejection_reason})'
            md.append(f'\n### Step {st.index} · call {ci} — {tag}')
            md.append(
                f'- usage: in {c.usage.input} / out {c.usage.output} '
                f'(think {c.usage.thinking}, img {c.usage.image})'
            )
            if c.prompt_sent:
                md.append(f'- prompt_sent:\n```\n{c.prompt_sent}\n```')
            if c.thinking:
                md.append(f'- thinking:\n```\n{c.thinking}\n```')
            if c.raw_response:
                md.append(f'- raw_response:\n```\n{c.raw_response}\n```')
            if action:
                md.append(f'- ACTION: `{action}`')
            if result:
                md.append(f'- RESULT: `{result}`')

        summ['steps'].append(sj)

    end = _vec(steps[-1].obs.player) if steps else None
    summ['totals'] = {
        'tokens_in': tot_in,
        'tokens_out': tot_out,
        'tokens_thinking': tot_think,
        'tokens_image': tot_img,
        'n_calls': n_calls,
        'n_rejected': n_rejected,
        'start_player': start,
        'end_player': end,
        'net_displacement_2d': round(_dist2d(start, end), 1) if start and end else None,
        'verb_counts': verb_counts,
        'result_code_counts': code_counts,
    }

    with open(os.path.join(out_dir, f'{stem}.md'), 'w') as f:
        f.write('\n'.join(md))
    with open(os.path.join(out_dir, f'{stem}.summary.json'), 'w') as f:
        json.dump(summ, f, indent=1)

    print(f'wrote {out_dir}/{stem}.md and {stem}.summary.json')
    print(f'  steps={len(steps)} calls={n_calls} rejected={n_rejected}')
    print(f'  tokens in={tot_in} out={tot_out} think={tot_think}')
    print(f'  verbs={verb_counts}')
    print(f'  codes={code_counts}')
    print(f'  start={start} end={end} net2d={summ["totals"]["net_displacement_2d"]}')


if __name__ == '__main__':
    main()
