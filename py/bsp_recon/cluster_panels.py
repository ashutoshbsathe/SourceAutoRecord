"""Census portalable white-tile panels: single- vs multi-tile prevalence across the BSP corpus.

Reads the ``*.geo.json`` emitted by ``dump_ents.py --geometry``, material-filters portalable faces
to white tile (the bare ``portalable`` flag is ~89% false-positive -- squarebeams, tools, grates,
glass, goo all read portalable), quantizes each face to 128u cells on its plane, connected-components
the cells into panels, and reports the per-map and corpus single-vs-multi-tile histogram.

A high multi-tile fraction means most portalable panels span several tiles, so naming a panel does
not pin a portal position; single-tile-dominant means naming the panel suffices. Cross-checked
against the in-engine ``sar_harness_portal_surface_census`` command.

Caveat: worldspawn geometry misses func_brush / brush-entity panels, so func_brush-heavy maps
undercount -- the runtime command is the ground-truth on those.
"""

import argparse
import collections
import glob
import json
import math
import os

TILE = 128.0


def load(path):
    with open(path) as fh:
        return json.load(fh)


def is_white_tile(material):
    m = material.lower()
    return 'white' in m and 'tile' in m


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def normalize(a):
    n = math.sqrt(dot(a, a)) or 1.0
    return (a[0] / n, a[1] / n, a[2] / n)


def plane_axes(normal):
    up = (0.0, 0.0, 1.0) if abs(normal[2]) < 0.9 else (1.0, 0.0, 0.0)
    u = normalize(cross(normal, up))
    v = normalize(cross(normal, u))
    return u, v


def plane_key(normal, dist):
    # PeTI planes are axis-aligned; round to fold float jitter and the upper/lower-case dup.
    return (round(normal[0], 2), round(normal[1], 2), round(normal[2], 2), round(dist))


def face_cells(verts, u, v):
    us = [dot(p, u) for p in verts]
    vs = [dot(p, v) for p in verts]
    cu0, cu1 = math.floor(min(us) / TILE), math.floor((max(us) - 1e-3) / TILE)
    cv0, cv1 = math.floor(min(vs) / TILE), math.floor((max(vs) - 1e-3) / TILE)
    return {(cu, cv) for cu in range(cu0, cu1 + 1) for cv in range(cv0, cv1 + 1)}


def connected_components(cells):
    seen = set()
    panels = []
    for start in cells:
        if start in seen:
            continue
        stack = [start]
        blob = set()
        while stack:
            cell = stack.pop()
            if cell in seen:
                continue
            seen.add(cell)
            blob.add(cell)
            cu, cv = cell
            for nb in ((cu + 1, cv), (cu - 1, cv), (cu, cv + 1), (cu, cv - 1)):
                if nb in cells and nb not in seen:
                    stack.append(nb)
        panels.append(blob)
    return panels


def census_map(geo_path):
    """Return the list of panel sizes (in 128u cells) for one map's white-tile surfaces."""
    by_plane = collections.defaultdict(list)
    for fa in load(geo_path).get('faces', []):
        if not fa.get('portalable'):
            continue
        if not is_white_tile(fa.get('material', '')):
            continue
        n = tuple(fa['plane']['normal'])
        by_plane[plane_key(n, fa['plane']['dist'])].append((n, fa['verts']))

    sizes = []
    for faces_on_plane in by_plane.values():
        u, v = plane_axes(faces_on_plane[0][0])
        cells = set()
        for _, verts in faces_on_plane:
            cells |= face_cells(verts, u, v)
        sizes.extend(len(blob) for blob in connected_components(cells))
    return sizes


def plane_point(uc, vc, u, v, dist, n):
    return [uc * u[i] + vc * v[i] + dist * n[i] for i in range(3)]


def enumerate_panels(geo_path):
    """Per-map portalable panels with world geometry and deterministic marks (1..N)."""
    by_plane = collections.defaultdict(list)
    for fa in load(geo_path).get('faces', []):
        if not fa.get('portalable') or not is_white_tile(fa.get('material', '')):
            continue
        n = tuple(fa['plane']['normal'])
        d = fa['plane']['dist']
        by_plane[plane_key(n, d)].append((n, d, fa['verts']))

    raw = []
    for key, faces in by_plane.items():
        n, dist = faces[0][0], faces[0][1]
        u, v = plane_axes(n)
        cells = set()
        for _, _, verts in faces:
            cells |= face_cells(verts, u, v)
        for blob in connected_components(cells):
            cus = [c[0] for c in blob]
            cvs = [c[1] for c in blob]
            umin, umax = min(cus) * TILE, (max(cus) + 1) * TILE
            vmin, vmax = min(cvs) * TILE, (max(cvs) + 1) * TILE
            corners = [
                plane_point(umin, vmin, u, v, dist, n),
                plane_point(umax, vmin, u, v, dist, n),
                plane_point(umax, vmax, u, v, dist, n),
                plane_point(umin, vmax, u, v, dist, n),
            ]
            raw.append(
                (
                    key + (min(cus), min(cvs)),
                    {
                        'plane_normal': list(n),
                        'center': plane_point(
                            (umin + umax) / 2, (vmin + vmax) / 2, u, v, dist, n
                        ),
                        'mins': [min(c[i] for c in corners) for i in range(3)],
                        'maxs': [max(c[i] for c in corners) for i in range(3)],
                        'anchor_flags': 0,
                    },
                )
            )
    raw.sort(key=lambda r: r[0])
    panels = []
    for i, (_, p) in enumerate(raw):
        p['mark'] = i + 1
        panels.append(p)
    return panels


def helper_count(geo_path):
    ent_path = geo_path[: -len('.geo.json')] + '.json'
    try:
        return (
            load(ent_path).get('classname_counts', {}).get('info_placement_helper', 0)
        )
    except OSError:
        return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        '--recon', default='artifacts/bsp_recon', help='dir of <id>/<map>.geo.json'
    )
    ap.add_argument('--out', default='artifacts/panel_census.json')
    ap.add_argument('--limit', type=int, default=0, help='cap maps processed (0 = all)')
    ap.add_argument(
        '--emit',
        default='',
        help='dir to write per-map panel sidecars (skips the census)',
    )
    ap.add_argument(
        '--only', default='', help='comma-separated path substrings selecting maps'
    )
    args = ap.parse_args()

    geos = sorted(glob.glob(os.path.join(args.recon, '*', '*.geo.json')))
    if args.only:
        keys = args.only.split(',')
        geos = [g for g in geos if any(k in g for k in keys)]
    if args.limit:
        geos = geos[: args.limit]

    if args.emit:
        os.makedirs(args.emit, exist_ok=True)
        for geo in geos:
            name = os.path.basename(geo)[: -len('.geo.json')]
            mapid = os.path.relpath(geo, args.recon)[: -len('.geo.json')]
            with open(os.path.join(args.emit, name + '.json'), 'w') as fh:
                json.dump({'map': mapid, 'panels': enumerate_panels(geo)}, fh, indent=1)
        print(f'wrote {len(geos)} sidecar(s) to {args.emit}')
        return

    per_map = []
    size_hist = collections.Counter()
    single = multi = helpers = skipped = 0
    for geo in geos:
        try:
            sizes = census_map(geo)
        except Exception:
            skipped += 1
            continue
        if not sizes:
            continue
        s1 = sum(1 for s in sizes if s == 1)
        s2 = len(sizes) - s1
        h = helper_count(geo)
        single += s1
        multi += s2
        helpers += h
        for s in sizes:
            size_hist[min(s, 5)] += 1  # 5 == "5+ cells"
        per_map.append(
            {
                'map': os.path.basename(geo)[: -len('.geo.json')],
                'panels': len(sizes),
                'single_tile': s1,
                'multi_tile': s2,
                'helpers': h,
            }
        )

    panels = single + multi
    summary = {
        'maps_scanned': len(geos),
        'maps_skipped': skipped,
        'maps_with_white_tile': len(per_map),
        'total_panels': panels,
        'single_tile': single,
        'multi_tile': multi,
        'multi_tile_fraction_M': round(multi / panels, 3) if panels else 0.0,
        'total_helpers': helpers,
        'panel_size_hist_cells': {str(k): size_hist[k] for k in sorted(size_hist)},
    }
    with open(args.out, 'w') as fh:
        json.dump({'summary': summary, 'per_map': per_map}, fh, indent=1)

    print('=== portal panel census ===')
    for k, val in summary.items():
        print(f'{k:>24}: {val}')
    print(f'\nwrote {args.out}')


if __name__ == '__main__':
    main()
