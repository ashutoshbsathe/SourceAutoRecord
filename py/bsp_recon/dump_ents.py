"""Dump a Portal 2 .bsp to JSON: entities, keyvalues, props, geometry.

Writes <out>/<map>.json per map -- every entity with its keyvalues, outputs and
brush AABB, plus worldspawn, materials, static props, lump counts and a VScript
flag. --corpus sweeps a directory in parallel and adds an index.json. --geometry
also emits <map>.geo.json (faces + brushes; portalable read from the baked
surface flag). Read-only.

    uv add srctools
    uv run python py/bsp_recon/dump_ents.py                      # default map -> JSON
    uv run python py/bsp_recon/dump_ents.py workshop/<id>/<id>   # any map
    uv run python py/bsp_recon/dump_ents.py --corpus workshop    # parallel sweep
    uv run python py/bsp_recon/dump_ents.py --corpus workshop --geometry
"""

import argparse
import collections
import json
import os
import sys
from concurrent.futures import ThreadPoolExecutor
from concurrent.futures import as_completed
from pathlib import Path

from dotenv import load_dotenv

# Maps live under the Steam Portal 2 install; STEAM_ROOT comes from the repo .env
# (default ~/.steam/root).
_REPO = Path(__file__).resolve().parents[2]
load_dotenv(_REPO / '.env')
STEAM_ROOT = os.environ.get('STEAM_ROOT', '~/.steam/root')
_P2 = f'{STEAM_ROOT}/steamapps/common/Portal 2/portal2/maps'
MAPS_DIR = Path(os.path.expanduser(_P2))

# Map dumped when none is given on the command line.
DEFAULT_MAP = 'workshop/14115283150118884095/1781795990'
# Corpus JSON lands here (artifacts/ is gitignored).
DEFAULT_OUT = _REPO / 'artifacts' / 'bsp_recon'

# Logic entities that relay or gate a signal between others. An edge into one is
# tagged so an indirect button -> ... -> door chain is visible.
RELAY_CLASSES = frozenset({
    'logic_relay',
    'logic_branch',
    'logic_auto',
    'logic_branch_listener',
    'logic_case',
    'math_counter',
    'logic_collision_pair',
    'func_instance_io_proxy',
})

# Identity fields kept at top level, so the per-entity keyvalues dict is params.
_IDENTITY = frozenset({'classname', 'targetname', 'origin'})


def resolve_map(name):
    """Turn a 'workshop/<id>/<id>' map name into its .bsp path under MAPS_DIR."""
    return MAPS_DIR / f'{name.removesuffix(".bsp")}.bsp'


def load_bsp(path):
    """Open a .bsp, returning (bsp, entity_vmf); exit cleanly if srctools is missing."""
    try:
        from srctools.bsp import BSP
    except ImportError as exc:
        sys.exit(f'srctools not installed (run `uv add srctools`): {exc}')
    bsp = BSP(str(path))
    return bsp, bsp.ents


def _version(bsp):
    """The BSP format version as a plain int (or string if unrecognized)."""
    v = getattr(bsp.version, 'value', bsp.version)
    return v if isinstance(v, int) else str(v)


def _vec(v):
    """A srctools Vec/Angle as [x, y, z] floats, or None."""
    try:
        return [float(v.x), float(v.y), float(v.z)]
    except Exception:  # noqa: BLE001 -- not every value is a vector
        return None


def _plane(pl):
    """A plane as {normal: [x, y, z], dist}."""
    return {'normal': _vec(pl.normal), 'dist': pl.dist}


def _geometry_counts(bsp):
    """Element count per bulk lump (the geometry itself stays in the .bsp)."""
    counts = {}
    lumps = ('brushes', 'planes', 'faces', 'vertexes', 'props', 'cubemaps', 'overlays')
    for n in lumps:
        try:
            counts[n] = len(getattr(bsp, n))
        except Exception:  # noqa: BLE001 -- absent lumps just report null
            counts[n] = None
    return counts


def _static_props(bsp):
    """Static props as {model, origin, angles, skin}."""
    out = []
    try:
        for p in bsp.props:
            out.append({
                'model': p.model,
                'origin': _vec(p.origin),
                'angles': _vec(p.angles),
                'skin': p.skin,
            })
    except Exception as exc:  # noqa: BLE001 -- prop lump variants differ across maps
        print(f'  (static props read failed: {exc})')
    return out


def vscript_taint(bsp, vmf):
    """Find VScript use, which can wire entities at runtime invisibly to a static parse.

    Returns:
        (ent_hits, nut_files): entities with a vscripts keyvalue or script
        classname, and any .nut files embedded in the map's pakfile.
    """
    ent_hits = []
    for ent in vmf.entities:
        scripts = ent['vscripts']
        cls = ent['classname']
        if scripts or cls in ('logic_script', 'point_script'):
            ent_hits.append({
                'classname': cls,
                'targetname': ent['targetname'],
                'vscripts': scripts,
            })
    nut_files = []
    try:
        nut_files = [n for n in bsp.pakfile.namelist() if n.lower().endswith('.nut')]
    except Exception as exc:  # noqa: BLE001 -- a pak read failure is just informational here
        print(f'  (pakfile read failed: {exc})')
    return ent_hits, nut_files


def extract(path):
    """Parse one .bsp into a JSON-friendly record (entities, geometry, props)."""
    bsp, vmf = load_bsp(path)
    ents = list(vmf.entities)
    bmodels = bsp.bmodels  # Entity -> brush model (its world-space AABB)
    relay_names = {
        e['targetname']
        for e in ents
        if e['classname'] in RELAY_CLASSES and e['targetname']
    }
    cls_hist = collections.Counter()
    out_hist = collections.Counter()
    edge_count = 0
    entities = []
    for ent in ents:
        cls = ent['classname']
        cls_hist[cls] += 1
        outputs = []
        for o in ent.outputs:
            edge_count += 1
            out_hist[o.output] += 1
            outputs.append({
                'output': o.output,
                'target': o.target,
                'input': o.input,
                'params': o.params,
                'delay': o.delay,
                'times': o.times,
                'via_logic': bool(o.target) and o.target in relay_names,
            })
        bm = bmodels.get(ent)
        entities.append({
            'classname': cls,
            'targetname': ent['targetname'],
            'origin': ent['origin'],
            'aabb': [_vec(bm.mins), _vec(bm.maxes)] if bm is not None else None,
            'keyvalues': {k: v for k, v in ent.items() if k not in _IDENTITY},
            'outputs': outputs,
        })
    ent_hits, nut_files = vscript_taint(bsp, vmf)
    spawn = getattr(vmf, 'spawn', None)
    try:
        pakfile = sorted(bsp.pakfile.namelist())
    except Exception:  # noqa: BLE001 -- absent/corrupt pak just reports empty
        pakfile = []
    return {
        'path': str(path),
        'version': _version(bsp),
        'map_revision': bsp.map_revision,
        'entity_count': len(ents),
        'edge_count': edge_count,
        'worldspawn': dict(spawn.items()) if spawn is not None else {},
        'materials': list(bsp.textures),
        'pakfile': pakfile,
        'geometry_counts': _geometry_counts(bsp),
        'static_props': _static_props(bsp),
        'vscript': {'entities': ent_hits, 'nut_files': nut_files},
        'classname_counts': dict(cls_hist.most_common()),
        'output_counts': dict(out_hist.most_common()),
        'entities': entities,
    }


def extract_geometry(path):
    """Raw geometry: faces (winding + material + portalable) and brushes (plane sets).

    Portalable is read straight from the baked SURF_NOPORTAL surface flag, so no
    VMT lookup is needed for static surfaces.
    """
    from srctools.bsp import SurfFlags

    bsp, _ = load_bsp(path)
    faces = []
    for f in bsp.faces:
        ti = f.texinfo
        faces.append({
            'material': ti.mat,
            'flags': ti.flags.value,
            'portalable': not (ti.flags & SurfFlags.NOPORTAL),
            'plane': _plane(f.plane),
            'verts': [_vec(e.a) for e in f.edges],
        })
    brushes = [
        {
            'contents': str(br.contents),
            'sides': [
                {'material': s.texinfo.mat, 'flags': s.texinfo.flags.value,
                 'plane': _plane(s.plane)}
                for s in br.sides
            ],
        }
        for br in bsp.brushes
    ]
    return {'faces': faces, 'brushes': brushes}


def _progress(items, total):
    """Yield items behind a rich progress bar, or a stderr counter without rich."""
    try:
        from rich.progress import track
    except ImportError:
        track = None
    if track is not None:
        yield from track(items, total=total, description='dumping')
        return
    for i, item in enumerate(items, 1):
        print(f'\r  dumping {i}/{total}', end='', file=sys.stderr, flush=True)
        yield item
    print(file=sys.stderr)


def dump_one(path, outdir, map_name, geometry):
    """Parse one .bsp -> <map_name>.json under outdir; return its index entry."""
    try:
        rec = extract(path)
    except Exception as exc:  # noqa: BLE001 -- catalog the failure, keep the sweep going
        return {'map': map_name, 'status': 'fail', 'error': str(exc)}
    rec['map'] = map_name
    out = outdir / f'{map_name}.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(rec, indent=1))
    if geometry:
        (outdir / f'{map_name}.geo.json').write_text(
            json.dumps(extract_geometry(path), indent=1)
        )
    vs = rec['vscript']
    return {
        'map': map_name,
        'json': f'{map_name}.json',
        'version': rec['version'],
        'entity_count': rec['entity_count'],
        'edge_count': rec['edge_count'],
        'vscript': bool(vs['entities'] or vs['nut_files']),
        'nut_files': vs['nut_files'],
        'status': 'ok',
    }


def sweep_corpus(root, outdir, geometry, jobs):
    """Dump every .bsp under root in parallel; write index.json alongside."""
    root = Path(root)
    if not root.is_absolute():
        root = MAPS_DIR / root
    bsps = sorted(root.rglob('*.bsp'))
    work = [(p, str(p.relative_to(root).with_suffix(''))) for p in bsps]
    print(f'{len(bsps)} maps under {root} -> {outdir}  ({jobs} workers)')

    # Threads, not processes: the parse is partly GIL-bound, but the disk reads
    # and lump decompression overlap, and it avoids pickling records across procs.
    entries = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(dump_one, p, outdir, name, geometry) for p, name in work]
        for fut in _progress(as_completed(futures), len(futures)):
            entries.append(fut.result())

    entries.sort(key=lambda e: e['map'])
    failed = [e for e in entries if e['status'] == 'fail']
    outdir.mkdir(parents=True, exist_ok=True)
    summary = {
        'root': str(root),
        'map_count': len(bsps),
        'ok': len(entries) - len(failed),
        'failed': failed,
        'maps': entries,
    }
    (outdir / 'index.json').write_text(json.dumps(summary, indent=2))
    print(f'wrote {summary["ok"]}/{len(bsps)} maps + index.json to {outdir} '
          f'({len(failed)} failed)')


def main():
    """Dump one map, or sweep a directory in parallel with --corpus."""
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        'map', nargs='?', default=DEFAULT_MAP, help='map name, e.g. workshop/<id>/<id>'
    )
    ap.add_argument(
        '--corpus', metavar='DIR', help='sweep .bsp under DIR (under maps/) to JSON'
    )
    ap.add_argument(
        '--out', metavar='DIR', default=str(DEFAULT_OUT), help='JSON output dir'
    )
    ap.add_argument(
        '--geometry', action='store_true',
        help='also emit <map>.geo.json (faces+brushes)',
    )
    ap.add_argument(
        '--jobs', type=int, default=min(8, (os.cpu_count() or 4)),
        help='parallel workers for --corpus',
    )
    args = ap.parse_args()
    outdir = Path(args.out)
    if args.corpus:
        sweep_corpus(args.corpus, outdir, args.geometry, args.jobs)
        return
    path = resolve_map(args.map)
    if not path.exists():
        sys.exit(f'no such map: {path}')
    entry = dump_one(path, outdir, args.map, args.geometry)
    if entry['status'] == 'fail':
        sys.exit(f'parse failed: {entry["error"]}')
    vs = '  [VScript]' if entry['vscript'] else ''
    print(f'wrote {outdir / entry["json"]}  '
          f'({entry["entity_count"]} entities, {entry["edge_count"]} edges){vs}')


if __name__ == '__main__':
    main()
