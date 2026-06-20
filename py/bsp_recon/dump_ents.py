"""Dump a Portal 2 .bsp's entity wiring.

Prints who-triggers-what (each entity's I/O outputs), flags any VScript, and
tallies the classnames/output names in the map. Read-only — run it against a
chamber and compare the printed edges to what you see in-game.

    uv add srctools
    uv run python py/bsp_recon/dump_ents.py                      # default map
    uv run python py/bsp_recon/dump_ents.py workshop/<id>/<id>   # any map
    uv run python py/bsp_recon/dump_ents.py --corpus workshop    # sweep a dir
"""

import argparse
import collections
import os
import sys
from pathlib import Path

from dotenv import load_dotenv

# Maps live under the Steam Portal 2 install; STEAM_ROOT comes from the repo .env
# (default ~/.steam/root).
load_dotenv(Path(__file__).resolve().parents[2] / '.env')
STEAM_ROOT = os.environ.get('STEAM_ROOT', '~/.steam/root')
_P2 = f'{STEAM_ROOT}/steamapps/common/Portal 2/portal2/maps'
MAPS_DIR = Path(os.path.expanduser(_P2))

# Map dumped when none is given on the command line.
DEFAULT_MAP = 'workshop/14115283150118884095/1781795990'

# Logic entities that relay or gate a signal between others. An edge into one is
# tagged so an indirect button -> ... -> door chain is visible in the dump.
RELAY_CLASSES = frozenset({
    'logic_relay',
    'logic_branch',
    'logic_auto',
    'math_counter',
    'logic_collision_pair',
})


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
            ent_hits.append((cls, ent['targetname'], scripts))
    nut_files = []
    try:
        nut_files = [n for n in bsp.pakfile.namelist() if n.lower().endswith('.nut')]
    except Exception as exc:  # noqa: BLE001 -- a pak read failure is just informational here
        print(f'  (pakfile read failed: {exc})')
    return ent_hits, nut_files


def dump_one(path):
    """Print one map's I/O edges, VScript status, and classname/output tallies."""
    if not path.exists():
        sys.exit(f'no such map: {path}')
    bsp, vmf = load_bsp(path)
    ents = list(vmf.entities)
    print(f'=== {path}')
    print(f'    version={bsp.version!r}  entities={len(ents)}')

    relay_names = {
        e['targetname']
        for e in ents
        if e['classname'] in RELAY_CLASSES and e['targetname']
    }
    print('\n--- I/O edges (entities that emit outputs) ---')
    edge_count = 0
    cls_hist = collections.Counter()
    out_hist = collections.Counter()
    for ent in ents:
        cls = ent['classname']
        cls_hist[cls] += 1
        if not ent.outputs:
            continue
        name = ent['targetname'] or '<noname>'
        origin = ent['origin'] or '?'
        print(f'\n{cls} "{name}" @({origin})')
        for out in ent.outputs:
            edge_count += 1
            out_hist[out.output] += 1
            via = ' [via logic]' if out.target in relay_names else ''
            param = f' "{out.params}"' if out.params else ''
            print(
                f'    {out.output} -> "{out.target}".{out.input}{param}'
                f'  delay={out.delay}{via}'
            )
    print(f'\n--- {edge_count} edges total ---')

    ent_hits, nut_files = vscript_taint(bsp, vmf)
    if ent_hits or nut_files:
        print('\n!!! VScript present -- some wiring may be hidden from this dump:')
        for cls, name, scripts in ent_hits:
            print(f'    {cls} "{name}" vscripts={scripts}')
        for n in nut_files:
            print(f'    pak: {n}')
    else:
        print('\n--- no VScript found ---')

    print('\n--- classname counts ---')
    for cls, n in cls_hist.most_common():
        print(f'    {n:4d}  {cls}')
    print('\n--- output-name counts ---')
    for out, n in out_hist.most_common():
        print(f'    {n:4d}  {out}')


def sweep_corpus(root):
    """Load every .bsp under a dir; report which parse, which use VScript, the vocab."""
    root = Path(root)
    if not root.is_absolute():
        root = MAPS_DIR / root
    bsps = sorted(root.rglob('*.bsp'))
    print(f'=== sweeping {len(bsps)} maps under {root}\n')
    cls_hist = collections.Counter()
    failed, tainted = [], []
    for path in bsps:
        try:
            bsp, vmf = load_bsp(path)
            ents = list(vmf.entities)
            for ent in ents:
                cls_hist[ent['classname']] += 1
            ent_hits, nut_files = vscript_taint(bsp, vmf)
            tag = ''
            if ent_hits or nut_files:
                tainted.append(path)
                tag = '  [VScript]'
            print(f'  ok  v{bsp.version!r}  ents={len(ents):4d}  {path.name}{tag}')
        except Exception as exc:  # noqa: BLE001 -- catalog the failure and keep going
            failed.append((path, exc))
            print(f'  FAIL  {path.name}: {exc}')
    print(
        f'\n--- {len(bsps)} maps: {len(failed)} failed, {len(tainted)} with VScript ---'
    )
    print('\n--- classname counts (top 40) ---')
    for cls, n in cls_hist.most_common(40):
        print(f'    {n:5d}  {cls}')


def main():
    """Dump a single map, or sweep a directory with --corpus."""
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        'map', nargs='?', default=DEFAULT_MAP, help='map name, e.g. workshop/<id>/<id>'
    )
    ap.add_argument(
        '--corpus', metavar='DIR', help='sweep all .bsp under DIR (relative to maps/)'
    )
    args = ap.parse_args()
    if args.corpus:
        sweep_corpus(args.corpus)
    else:
        dump_one(resolve_map(args.map))


if __name__ == '__main__':
    main()
