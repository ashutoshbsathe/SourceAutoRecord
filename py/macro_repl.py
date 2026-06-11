"""Interactive macro REPL: type closed-verb commands and drive the frozen game.

A human-driven front-end to the macro executor -- the manual analogue of the
eval driver, and a fast way to find a solve sequence by hand instead of
hardcoding one. Launches its own instance (or --attach), then takes commands:

    go_to 12          walk to mark 12
    pick_up 12        grab it
    interact 8        walk to + press mark 8
    release [12]      drop (look-down, or toward mark 12)
    move forward 20   hold a direction (forward|back|left|right) for N ticks
    look 30 -15       turn: yaw +30, pitch -15 (signed degrees)
    aim_at 12         point the view at a mark
    wait 50 / done
    obs               list the marked entities (mark, class, pos)
    reset [map]       full-reload the map (re-fires droppers), or switch chambers
    save [file]       write a replayable transcript of the session
    help / quit

Up-arrow recalls history (persisted across sessions). `#` starts a comment, so a
saved transcript pipes straight back in:  macro_repl.py --attach < solve.txt

Usage:
    uv run python py/macro_repl.py                       # launch a fresh instance
    uv run python py/macro_repl.py --map testchamber_000 # ... on a specific map
    uv run python py/macro_repl.py --attach              # drive a running instance

Launching needs gamescope + Steam Portal 2 (same as the trainer/smoke test); the
launched instance is torn down on exit.
"""

import argparse
import os
import sys

try:
    import readline  # noqa: F401 -- importing it gives input() up-arrow history
except ImportError:  # pragma: no cover -- not present on every platform
    readline = None

from p2harness import harness_pb2
from testchamber_session import TestChamberSession
from testchamber_session import launch_or_attach

VERBS = {
    'aim_at',
    'look',
    'go_to',
    'move',
    'pick_up',
    'release',
    'interact',
    'wait',
    'done',
}

HELP = """commands:
  go_to N / aim_at N / pick_up N / interact N   verbs taking a mark
  release [N]        drop: look-down, or toward mark N
  move DIR T         DIR in forward|back|left|right, hold T ticks
  look YAW [PITCH]   turn by signed degrees
  wait T / done
  obs                list marked entities (mark, class, pos)
  reset [map]        full-reload the current map (re-fires droppers), or switch
  save [file]        write the session as a replayable transcript
  help / quit"""

# Up-arrow history persists here across sessions (best-effort).
HISTFILE = os.path.expanduser('~/.macro_repl_history')


def fmt_pos(p):
    """Compact (x,y,z) string for an (x, y, z) sequence."""
    return f'({p[0]:.0f},{p[1]:.0f},{p[2]:.0f})'


def build_macro(verb, args):
    """Map a typed command + args to a MacroRequest. Raises on malformed args."""
    m = harness_pb2.MacroRequest(verb=verb)
    if verb in ('aim_at', 'go_to', 'pick_up', 'interact'):
        m.mark = int(args[0])
    elif verb == 'release':
        m.mark = int(args[0]) if args else 0
    elif verb == 'move':
        m.dir = args[0]
        m.ticks = int(args[1])
    elif verb == 'look':
        m.yaw = int(args[0])
        m.pitch = int(args[1]) if len(args) > 1 else 0
    elif verb == 'wait':
        m.ticks = int(args[0])
    elif verb == 'done':
        pass
    return m


def dump_marks(obs):
    """Print every marked entity (mark, class, position, name)."""
    if not obs.marks:
        print('  (no marked entities)')
        return
    for m in obs.marks:
        name = f' "{m["name"]}"' if m['name'] else ''
        print(f'  [{m["mark"]:>2}] {m["class"]:<22} {fmt_pos(m["pos"])}{name}')


def write_transcript(path, map_name, steps):
    """Write the session as a replayable transcript.

    Each step is one command on its own line with its result as a trailing `#`
    comment; the REPL strips `#` on input, so a saved transcript pipes straight
    back in (e.g. macro_repl.py --attach < solve.transcript).
    """
    with open(path, 'w') as f:
        f.write(f'# macro_repl transcript -- map {map_name}\n')
        for line, result in steps:
            f.write(f'{line:<22} # {result}\n')


def report(macro, obs):
    """Print the macro result plus the player and (if anchored) the mark's pos."""
    mr = obs.result
    extra = ''
    if macro.mark:
        for m in obs.marks:
            if m['mark'] == macro.mark:
                extra = f' mark{macro.mark}={fmt_pos(m["pos"])}'
                break
    print(f'  {mr.result_code:<13} {mr.detail}   player={fmt_pos(obs.player)}{extra}')


def run_repl(session):
    """The interactive command loop (the session must already be primed)."""
    if readline is not None:
        try:
            readline.read_history_file(HISTFILE)
        except OSError:
            pass
    transcript = []  # (command, result) per state-changing step, for `save`
    obs = session.last

    try:
        while True:
            try:
                raw = input('> ')
            except EOFError, KeyboardInterrupt:
                print()
                break
            line = raw.split('#', 1)[0].strip()  # strip `#` -> replayable saves
            if not line:
                continue
            tokens = line.split()
            cmd, rest = tokens[0], tokens[1:]

            if cmd in ('quit', 'exit', 'q'):
                break
            if cmd == 'help':
                print(HELP)
                continue
            if cmd == 'obs':
                dump_marks(obs)
                continue
            if cmd == 'save':
                path = rest[0] if rest else 'macro_repl.transcript'
                write_transcript(path, session.current_map, transcript)
                print(f'  saved {len(transcript)} steps to {path}')
                continue
            if cmd == 'reset':
                target = rest[0] if rest else session.current_map
                try:
                    obs = session.reset(target)
                except Exception as e:  # noqa: BLE001 -- REPL: surface, don't crash
                    print(f'  reset failed: {type(e).__name__}: {e}')
                    continue
                print(f'  reset ({session.current_map}).')
                transcript.append((line, f'-> {session.current_map}'))
                dump_marks(obs)
                continue
            if cmd not in VERBS:
                print(f'  ? unknown command "{cmd}" (try help)')
                continue

            try:
                macro = build_macro(cmd, rest)
            except ValueError, IndexError:
                print(f'  ? bad args for {cmd} (try help)')
                continue
            try:
                obs = session.step(macro)
            except Exception as e:  # noqa: BLE001 -- REPL: surface, don't crash
                print(f'  ! send failed: {type(e).__name__}: {e}')
                continue
            report(macro, obs)
            mr = obs.result
            transcript.append((line, f'{mr.result_code} {mr.detail}'.strip()))
    finally:
        if readline is not None:
            try:
                readline.write_history_file(HISTFILE)
            except OSError:
                pass


def main():
    """Launch (or attach to) an instance, run the command loop, then tear down."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--instance', type=int, default=0, help='instance N -> port 50000+N'
    )
    parser.add_argument(
        '--attach', action='store_true', help='drive a running instance'
    )
    parser.add_argument(
        '--map', default='', help='load this map at startup (default: the booted map)'
    )
    parser.add_argument('--timeout', type=float, default=180.0, help='boot wait (s)')
    args = parser.parse_args()

    try:
        harness, game, booted_map = launch_or_attach(
            args.instance, args.attach, args.timeout
        )
    except Exception as e:  # noqa: BLE001 -- boot failures are fatal, just report
        print(f'fatal: {e}')
        return 1

    try:
        session = TestChamberSession(harness, booted_map)
        if args.map and args.map != booted_map:
            print(f'loading map {args.map} ...')
            try:
                obs = session.reset(args.map)
            except Exception as e:  # noqa: BLE001 -- fall back to the booted map
                print(f'  load failed ({e}); staying on {booted_map}')
                obs = session.prime()
        else:
            obs = session.prime()
        print(
            f'attached {harness.address}  map={session.current_map or "<none>"}'
            '  -- type "help"'
        )
        if obs.result.result_code == 'NOT_READY':
            print('  (harness not ready -- still warming up? give it a moment)')
        try:
            run_repl(session)
        finally:
            session.close()
    finally:
        if game is not None:
            print('tearing down game instance ...')
            game.stop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
