"""Eyeball-verify that the curated [dm] status fields flow over gRPC (Phase 1a).

Launches its own headless game instance (via game_launcher), loads a chamber,
and prints every entity of the status-bearing classes with its fields --
flagging whether the four curated datamap-only fields registered in
EntitySnapshotter (m_nCubeType, m_bActivated, m_bPowered, m_bDisabled) are
present in the gRPC payload. Tears the instance down on exit.

The snapshot is taken from Reset's `initial_state`, which is a full snapshot
captured after warmup -- so every registered field is emitted regardless of
value (a `False` bool still proves the field flows; that is what 1a verifies).

Usage:
    uv run python py/check_status_fields.py                  # launch, sp_a2_laser_chaining
    uv run python py/check_status_fields.py --map mp_coop_doors
    uv run python py/check_status_fields.py --attach --instance 0   # use a running instance

Notes:
    * Launch needs gamescope + Steam Portal 2 (same as the trainer).
    * --instance N -> gRPC port 50000+N. Pick a free N if others are running.
    * sp_a2_laser_chaining covers point_laser_target + trigger_catapult +
      reflective cubes (3 of 4). For m_bActivated=True, use a cube+button
      chamber and start with a cube on a button.
"""

import argparse
import time

import grpc

from game_launcher import (
    DEFAULT_GAME_ARGS,
    DEFAULT_GAMESCOPE_ARGS,
    GameInstance,
    get_instance_specific_args,
)
from p2harness.harness import P2Harness

# className -> the curated [dm] field(s) we expect to now see over gRPC.
EXPECTED = {
    'prop_weighted_cube': ['m_nCubeType', 'm_bActivated'],
    'point_laser_target': ['m_bPowered'],
    'trigger_catapult': ['m_bDisabled'],
}

DEFAULT_MAP = 'sp_a2_laser_chaining'


def field_value(f):
    """Read the populated arm of an EntityField's value oneof."""
    which = f.WhichOneof('value')
    if which is None:
        return None
    val = getattr(f, which)
    if which == 'vec3_val':
        return f'({val.x:.1f}, {val.y:.1f}, {val.z:.1f})'
    return val


def collect(snapshot):
    """Index a full entity snapshot by entity_index -> {class, name, fields}."""
    acc = {}
    for ent in snapshot.entities:
        if ent.deleted:
            continue
        acc[ent.entity_index] = {
            'class': ent.class_name,
            'name': ent.target_name,
            'fields': {f.name: field_value(f) for f in ent.fields},
        }
    return acc


def wait_for_handshake(harness, instance, timeout):
    """Retry handshake until the gRPC server answers, or the game dies/times out."""
    deadline = time.monotonic() + timeout
    attempt = 0
    while True:
        try:
            harness.handshake()
            return
        except grpc.RpcError as e:
            if instance is not None and not instance.is_alive():
                raise RuntimeError(
                    f'game process died during boot; see {instance.log_file_path}'
                ) from e
            if time.monotonic() >= deadline:
                raise RuntimeError(
                    f'gRPC server not reachable after {timeout:.0f}s'
                ) from e
            attempt += 1
            print(f'  waiting for harness... (attempt {attempt})')
            time.sleep(min(3.0, 1.0 + 0.5 * attempt))


def report(acc):
    """Print an [OK]/[MISSING] line per status entity; return True if all OK."""
    all_ok = True
    print(f'\n{len(acc)} live entities.\n')
    for cls, expected_fields in EXPECTED.items():
        ents = [r for r in acc.values() if r['class'] == cls]
        print(f'=== {cls} ===  ({len(ents)} present)')
        if not ents:
            print('  (none in this map -- load a chamber that contains it)\n')
            continue
        for r in ents:
            missing = [f for f in expected_fields if f not in r['fields']]
            present = {f: r['fields'][f] for f in expected_fields if f in r['fields']}
            tag = 'OK' if not missing else f'MISSING {missing}'
            if missing:
                all_ok = False
            print(f'  [{tag}] {r["name"] or "<no name>"}: {present}')
        print()
    return all_ok


def main():
    """Launch (or attach), load the map, dump the status fields, tear down."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--instance', type=int, default=0, help='instance N -> port 50000+N'
    )
    parser.add_argument('--map', default=DEFAULT_MAP, help='chamber to load')
    parser.add_argument(
        '--attach',
        action='store_true',
        help='connect to an already-running instance instead of launching one',
    )
    parser.add_argument('--timeout', type=float, default=180.0, help='boot wait (s)')
    args = parser.parse_args()

    address = f'localhost:{50000 + args.instance}'
    instance = None
    try:
        if not args.attach:
            print(f'Launching game instance {args.instance} ...')
            instance = GameInstance(
                instance_id=args.instance,
                gamescope_args=DEFAULT_GAMESCOPE_ARGS.copy(),
                game_args=DEFAULT_GAME_ARGS.copy()
                + get_instance_specific_args(args.instance),
            )
            instance.start()

        print(f'Connecting to {address} ...')
        harness = P2Harness(address=address)
        wait_for_handshake(harness, instance, args.timeout)

        print(f'Loading {args.map} (warmup may take a few seconds) ...')
        resp = harness.reset(map_name=args.map)
        acc = collect(resp.initial_state.entity_snapshot)
        ok = report(acc)

        harness.close()
        print(
            'All curated status fields present.'
            if ok
            else 'Some fields MISSING -- see above.'
        )
    finally:
        if instance is not None:
            print('Tearing down game instance ...')
            instance.stop()


if __name__ == '__main__':
    main()
