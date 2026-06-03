import sys
import os
import argparse
from hdem_reader import HdemReader, FIELD_TYPES, SOLID_TYPES


def parse_hdem(path, max_ticks=None):
    try:
        reader = HdemReader(path)
    except Exception as e:
        print(f'Error opening/parsing HDEM file: {e}')
        return

    print('=' * 70)
    print(f'HDEM FILE HEADER: {path}')
    print('=' * 70)
    print(f'Version:      {reader.version}')
    print(f'Map Name:     {reader.map_name}')
    print(f'Tickrate:     {reader.tickrate:.2f}')
    print(f'Timestamp:    {reader.timestamp}')
    print(f'SAR Version:  {reader.sar_version}')
    print(f'Game Dir:     {reader.game_dir}')
    print(f'Schema Offset:{reader.schema_offset}')
    print('-' * 70)

    print(f'Classes ({len(reader.classes)}):')
    for cid, cname in sorted(reader.classes.items()):
        print(f'  [{cid:4d}] {cname}')
    print('-' * 70)

    print(f'Fields ({len(reader.fields)}):')
    for fid, (fname, ftype) in sorted(reader.fields.items()):
        tstr = FIELD_TYPES.get(ftype, f'UNKNOWN({ftype})')
        print(f'  [{fid:4d}] {fname} ({tstr})')
    print('=' * 70)

    ticks_read = 0
    # Read tick sequentially using the reader
    while True:
        tick_res = reader.read_next_tick()
        if tick_res is None:
            break

        tick_number, entities = tick_res

        dump_this_tick = max_ticks is None or max_ticks <= 0 or ticks_read < max_ticks

        # Calculate how many entities actually changed/were written this tick
        # Wait, the reader reconstructs the state, but we want to know what changed.
        # However, for dump purposes, we can just print the active entities reconstructed.
        if dump_this_tick:
            print(f'Tick {tick_number} | Reconstructed Entities: {len(entities)}')
            for ent_idx, ent in sorted(entities.items()):
                print(
                    f'  ├─ Entity [{ent_idx:4d}] (Serial: {ent["serial_number"]:5d}) Class: {ent["class_name"]} Target: {ent["target_name"]}'
                )
                # Print origin/angles/velocity if present
                print(
                    f'  │   ├─ position: ({ent["position"][0]:.2f}, {ent["position"][1]:.2f}, {ent["position"][2]:.2f})'
                )
                print(
                    f'  │   ├─ angles: ({ent["angles"][0]:.2f}, {ent["angles"][1]:.2f}, {ent["angles"][2]:.2f})'
                )
                print(
                    f'  │   ├─ velocity: ({ent["velocity"][0]:.2f}, {ent["velocity"][1]:.2f}, {ent["velocity"][2]:.2f})'
                )
                for fname, (ftype, fval) in sorted(ent['fields'].items()):
                    val_str = ''
                    if ftype == 2:  # VEC3
                        val_str = f'({fval[0]:.2f}, {fval[1]:.2f}, {fval[2]:.2f})'
                    elif ftype == 6 and fname == 'm_nSolidType' and fval in SOLID_TYPES:
                        val_str = f'{fval} ({SOLID_TYPES[fval]})'
                    else:
                        val_str = str(fval)
                    print(f'  │   ├─ {fname}: {val_str}')
        elif ticks_read == max_ticks:
            print(
                f'\n... Omitting full entity trees for remaining frames (exceeded --ticks {max_ticks}) ...'
            )
            print('Pass --ticks 0 to dump all frames.')
            break

        ticks_read += 1

    reader.close()

    # Read final footer if file size permits
    try:
        with open(path, 'rb') as f:
            f.seek(os.path.getsize(path) - 12)
            total_ticks, total_ents, checksum = struct.unpack('<III', f.read(12))
            print('=' * 70)
            print('FOOTER')
            print('=' * 70)
            print(f'Total Ticks:     {total_ticks}')
            print(f'Total Entities:  {total_ents}')
            print(f'Checksum:        0x{checksum:08X}')
    except Exception:
        pass


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Dump .hdem sidecar file contents.')
    parser.add_argument('path', help='Path to the .hdem file')
    parser.add_argument(
        '--ticks',
        type=int,
        default=0,
        help='Maximum number of ticks to dump fully (default: 0 = dump all ticks)',
    )
    args = parser.parse_args()

    if not os.path.exists(args.path):
        print(f'Error: File not found: {args.path}')
        sys.exit(1)

    parse_hdem(args.path, max_ticks=args.ticks)
