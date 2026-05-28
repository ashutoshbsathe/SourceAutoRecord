"""
Pre-parses a .rollout file into memory at startup.

Returns:
    meta       - dict with map info + sorted entity registry
    frames_raw - list of raw RGB bytes (or None) per tick
    ticks_json - list of JSON bytes per tick (action + entity deltas)
"""

import io
import json
import struct
import sys
import os
from collections import Counter

from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from p2harness.harness_pb2 import RolloutHeader, RolloutStep


def load(path: str):
    frames_raw = []       # raw RGB bytes or None per tick
    all_tick_dicts = []   # intermediate dicts, converted to JSON after sorting

    entity_registry = {}   # idx -> class_name (first seen)
    update_counter = Counter()
    prev_state: dict[int, dict] = {}  # idx -> {field: value} from last tick

    print(f'Parsing {path}...')

    with open(path, 'rb') as f:
        size = struct.unpack('<I', f.read(4))[0]
        header = RolloutHeader()
        header.ParseFromString(f.read(size))

        w, h = header.shm_width, header.shm_height

        meta = {
            'map':         header.map_name,
            'tickrate':    header.tickrate,
            'width':       w,
            'height':      h,
        }

        while True:
            size_data = f.read(4)
            if not size_data:
                break
            size = struct.unpack('<I', size_data)[0]
            step = RolloutStep()
            step.ParseFromString(f.read(size))

            frames_raw.append(bytes(step.image_data) if step.image_data else None)

            tick_dict = _extract_tick(step, entity_registry, update_counter, prev_state)
            all_tick_dicts.append(tick_dict)

    # Sort entity registry by update count descending
    sorted_entities = sorted(
        entity_registry.items(),
        key=lambda kv: -update_counter[kv[0]]
    )
    meta['entities'] = [
        {
            'idx':           idx,
            'class_name':    cls,
            'total_updates': update_counter[idx],
            'rank':          rank + 1,
        }
        for rank, (idx, cls) in enumerate(sorted_entities)
    ]
    meta['total_ticks'] = len(all_tick_dicts)

    # Build rank lookup for JS-side use, then JSON-encode all ticks
    ticks_json = [json.dumps(d).encode() for d in all_tick_dicts]

    print(f'Loaded {len(all_tick_dicts)} ticks, {len(meta["entities"])} entities.')
    return meta, frames_raw, ticks_json, (w, h)


def _extract_tick(step, entity_registry: dict, update_counter: Counter,
                  prev_state: dict) -> dict:
    act   = step.action
    state = step.state

    entities = []
    if state.HasField('entity_snapshot'):
        for ent in state.entity_snapshot.entities:
            if ent.deleted:
                prev_state.pop(ent.entity_index, None)
                continue
            idx = ent.entity_index
            cls = ent.class_name or 'unknown'

            if idx not in entity_registry:
                entity_registry[idx] = cls

            all_fields = _extract_fields(ent)
            prev = prev_state.get(idx, {})

            # Only include fields that changed since last tick
            changed = {k: v for k, v in all_fields.items() if prev.get(k) != v}
            prev_state[idx] = all_fields  # update baseline

            if changed:
                update_counter[idx] += 1
                entities.append({'idx': idx, 'class_name': cls, 'fields': changed})

    return {
        'tick': state.server_tick,
        'pos':  {'x': state.position.x, 'y': state.position.y, 'z': state.position.z},
        'action': {
            'forward':          act.key_forward,
            'backward':         act.key_backward,
            'left':             act.key_left,
            'right':            act.key_right,
            'jump':             act.key_jump,
            'crouch':           act.key_crouch,
            'use':              act.key_use,
            'portal_primary':   act.portal_primary,
            'portal_secondary': act.portal_secondary,
            'mouse_dx':         act.mouse_dx,
            'mouse_dy':         act.mouse_dy,
        },
        'entities': entities,
    }


def _extract_fields(ent) -> dict:
    fields = {}
    for f in ent.fields:
        which = f.WhichOneof('value')
        if which == 'float_val':
            fields[f.name] = round(f.float_val, 4)
        elif which == 'int_val':
            fields[f.name] = f.int_val
        elif which == 'vec3_val':
            v = f.vec3_val
            fields[f.name] = [round(v.x, 3), round(v.y, 3), round(v.z, 3)]
        elif which == 'bool_val':
            fields[f.name] = f.bool_val
        elif which == 'string_val':
            fields[f.name] = f.string_val
        elif which == 'handle_val':
            fields[f.name] = f.handle_val
    return fields


def frame_to_png(raw_rgb: bytes, w: int, h: int) -> bytes:
    """Encode raw RGB bytes to PNG (compress_level=1 for speed)."""
    img = Image.frombytes('RGB', (w, h), raw_rgb)
    buf = io.BytesIO()
    img.save(buf, format='PNG', compress_level=1)
    return buf.getvalue()
