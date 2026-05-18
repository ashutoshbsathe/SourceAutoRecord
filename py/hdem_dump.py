import sys
import os
import struct
import argparse

HDEM_MAGIC = 0x4D454448

FIELD_TYPES = {
    0: "FLOAT",
    1: "INT32",
    2: "VEC3",
    3: "BOOL",
    4: "STRING",
    5: "HANDLE",
    6: "BYTE",
    7: "SHORT",
    8: "COLOR",
}


def read_cstring(f):
    res = bytearray()
    while True:
        b = f.read(1)
        if not b or b == b"\x00":
            break
        res.extend(b)
    return res.decode("utf-8", errors="replace")


def parse_hdem(path, max_ticks=None):
    filesize = os.path.getsize(path)
    with open(path, "rb") as f:
        magic, version, flags = struct.unpack("<IHH", f.read(8))
        if magic != HDEM_MAGIC:
            print(f"Error: Invalid magic 0x{magic:08X} (expected 0x{HDEM_MAGIC:08X})")
            return

        map_name = read_cstring(f)
        tickrate, timestamp = struct.unpack("<fQ", f.read(12))
        sar_version = read_cstring(f)
        game_dir = read_cstring(f)

        (schema_offset,) = struct.unpack("<Q", f.read(8))

        print("=" * 70)
        print(f"HDEM FILE HEADER: {path}")
        print("=" * 70)
        print(f"Version:      {version}")
        print(f"Map Name:     {map_name}")
        print(f"Tickrate:     {tickrate:.2f}")
        print(f"Timestamp:    {timestamp}")
        print(f"SAR Version:  {sar_version}")
        print(f"Game Dir:     {game_dir}")
        print(f"Schema Offset:{schema_offset}")
        print("-" * 70)

        # Save tick frames start position
        ticks_start_pos = f.tell()

        if schema_offset == 0 or schema_offset >= filesize:
            print(f"Error: Trailing schema offset ({schema_offset}) is invalid or unfinalized.")
            print("This indicates the recording session is either still active or was not cleanly stopped.")
            return

        # Load trailing schema tables
        f.seek(schema_offset)
        (num_classes,) = struct.unpack("<H", f.read(2))
        classes = {}
        for _ in range(num_classes):
            (cid,) = struct.unpack("<H", f.read(2))
            cname = read_cstring(f)
            classes[cid] = cname

        print(f"Classes ({num_classes}):")
        for cid, cname in sorted(classes.items()):
            print(f"  [{cid:4d}] {cname}")
        print("-" * 70)

        (num_fields,) = struct.unpack("<H", f.read(2))
        fields = {}
        for _ in range(num_fields):
            (fid,) = struct.unpack("<H", f.read(2))
            fname = read_cstring(f)
            (ftype,) = struct.unpack("<B", f.read(1))
            fields[fid] = (fname, ftype)

        print(f"Fields ({num_fields}):")
        for fid, (fname, ftype) in sorted(fields.items()):
            tstr = FIELD_TYPES.get(ftype, f"UNKNOWN({ftype})")
            print(f"  [{fid:4d}] {fname} ({tstr})")
        print("=" * 70)

        # Seek back to start of ticks
        f.seek(ticks_start_pos)

        ticks_read = 0
        while f.tell() < schema_offset:

            tb = f.read(10)
            if len(tb) < 10:
                break
            tick_number, num_ents, frame_size = struct.unpack("<iHI", tb)

            dump_this_tick = max_ticks is None or max_ticks <= 0 or ticks_read < max_ticks
            if dump_this_tick:
                print(
                    f"Tick {tick_number} | Entities Changed: {num_ents} | Payload Size: {frame_size} bytes"
                )
            elif ticks_read == max_ticks:
                print(f"\n... Omitting full entity trees for remaining frames (exceeded --ticks {max_ticks}) ...")
                print("Pass --ticks 0 to dump all frames.")

            payload = f.read(frame_size)
            if len(payload) < frame_size:
                print("Warning: Unexpected EOF in tick payload")
                break

            if dump_this_tick and num_ents > 0:
                ppos = 0
                for _ in range(num_ents):
                    ent_idx, serial, cid, eflags, num_f = struct.unpack_from(
                        "<HHHBB", payload, ppos
                    )
                    ppos += 8
                    cname = classes.get(cid, "UNKNOWN")

                    flag_strs = []
                    if eflags & 1:
                        flag_strs.append("ALIVE")
                    if eflags & 2:
                        flag_strs.append("DORMANT")
                    if eflags & 4:
                        flag_strs.append("DELETED")
                    if eflags & 8:
                        flag_strs.append("FULL_SNAP")
                    fstr = "|".join(flag_strs)

                    print(
                        f"  ├─ Entity [{ent_idx:4d}] (Serial: {serial:5d}) Class: {cname} Flags: {fstr}"
                    )

                    for _ in range(num_f):
                        (fid,) = struct.unpack_from("<H", payload, ppos)
                        ppos += 2
                        fname, ftype = fields.get(fid, ("UNKNOWN", 0))

                        val_str = ""
                        if ftype == 0:  # FLOAT
                            (val,) = struct.unpack_from("<f", payload, ppos)
                            val_str = f"{val:.4f}"
                            ppos += 4
                        elif ftype == 1:  # INT32
                            (val,) = struct.unpack_from("<i", payload, ppos)
                            val_str = f"{val}"
                            ppos += 4
                        elif ftype == 2:  # VEC3
                            vx, vy, vz = struct.unpack_from("<fff", payload, ppos)
                            val_str = f"({vx:.2f}, {vy:.2f}, {vz:.2f})"
                            ppos += 12
                        elif ftype == 3:  # BOOL
                            (val,) = struct.unpack_from("<B", payload, ppos)
                            val_str = "True" if val else "False"
                            ppos += 1
                        elif ftype == 4:  # STRING
                            # read until null byte
                            start = ppos
                            while ppos < len(payload) and payload[ppos] != 0:
                                ppos += 1
                            val_str = payload[start:ppos].decode("utf-8", errors="replace")
                            ppos += 1  # consume null byte
                        elif ftype == 5:  # HANDLE
                            (val,) = struct.unpack_from("<I", payload, ppos)
                            if val == 0xFFFFFFFF:
                                val_str = "INVALID"
                            else:
                                h_idx = val & 0x7FF
                                h_ser = val >> 16
                                val_str = f"Handle[idx={h_idx}, serial={h_ser}] (raw: 0x{val:08X})"
                            ppos += 4
                        elif ftype == 6:  # BYTE
                            (val,) = struct.unpack_from("<B", payload, ppos)
                            val_str = f"{val}"
                            ppos += 1
                        elif ftype == 7:  # SHORT
                            (val,) = struct.unpack_from("<h", payload, ppos)
                            val_str = f"{val}"
                            ppos += 2
                        elif ftype == 8:  # COLOR
                            r, g, b, a = struct.unpack_from("<BBBB", payload, ppos)
                            val_str = f"rgba({r},{g},{b},{a})"
                            ppos += 4
                        else:
                            val_str = f"UNSUPPORTED_TYPE({ftype})"

                        print(f"  │   ├─ {fname}: {val_str}")

            ticks_read += 1

        # Parse final Footer
        f.seek(filesize - 12)
        total_ticks, total_ents, checksum = struct.unpack("<III", f.read(12))
        print("=" * 70)
        print("FOOTER")
        print("=" * 70)
        print(f"Total Ticks:     {total_ticks}")
        print(f"Total Entities:  {total_ents}")
        print(f"Checksum:        0x{checksum:08X}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Dump .hdem sidecar file contents.")
    parser.add_argument("path", help="Path to the .hdem file")
    parser.add_argument(
        "--ticks",
        type=int,
        default=0,
        help="Maximum number of ticks to dump fully (default: 0 = dump all ticks)",
    )
    args = parser.parse_args()

    if not os.path.exists(args.path):
        print(f"Error: File not found: {args.path}")
        sys.exit(1)

    parse_hdem(args.path, max_ticks=args.ticks)
