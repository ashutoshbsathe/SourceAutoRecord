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
import itertools
import os
import sys

try:
    import readline  # noqa: F401 -- importing it gives input() up-arrow history
except ImportError:  # pragma: no cover -- not present on every platform
    readline = None

from p2harness.macro_grammar import VERBS
from p2harness.macro_grammar import build_macro
from testchamber_session import TestChamberSession
from testchamber_session import launch_or_attach
from testchamber_session import reached_exit

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
    if obs.state.chamber_complete:
        print(f'  *** chamber_complete  exit_signal_mask={obs.state.exit_signal_mask}')


def run_repl(session, record_step=None):
    """The interactive command loop (the session must already be primed).

    If `record_step(obs, macro, line)` is given, each verb step captures a frame
    and is handed to it (the binary-trajectory recorder).
    """
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
                obs = session.step(macro, capture_frame=record_step is not None)
            except Exception as e:  # noqa: BLE001 -- REPL: surface, don't crash
                print(f'  ! send failed: {type(e).__name__}: {e}')
                continue
            report(macro, obs)
            if record_step is not None:
                record_step(obs, macro, line)
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
    parser.add_argument(
        '--record', help='also write a binary .trajectory (frames + telemetry)'
    )
    parser.add_argument(
        '--exit', help='exit position "x,y,z" (marks a step SOLVED on reaching it)'
    )
    parser.add_argument(
        '--radius', type=float, default=64.0, help='exit success radius (units)'
    )
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

        record_step = None
        recorder = None
        if args.record and session.harness.shm is None:
            print('  --record needs a video-mode instance (no SHM); not recording')
        elif args.record:
            from llm_eval import trajectory_pb2
            from llm_eval.trajectory_io import TrajectoryWriter
            from llm_eval.trajectory_io import make_call
            from llm_eval.trajectory_io import make_step
            from p2harness import macro_grammar

            exit_pos = (
                tuple(float(v) for v in args.exit.split(',')) if args.exit else None
            )
            header = trajectory_pb2.TrajectoryHeader(
                map=session.current_map,
                success_radius=args.radius,
                grammar='\n'.join(macro_grammar.verb_signatures()),
            )
            if exit_pos is not None:
                header.exit_pos.x = exit_pos[0]
                header.exit_pos.y = exit_pos[1]
                header.exit_pos.z = exit_pos[2]
            recorder = TrajectoryWriter(args.record, header)
            counter = itertools.count()
            # The human is the agent here, so the model fields are mocked.
            mock_usage = trajectory_pb2.TokenUsage(input=-1, output=-1, cached=-1)

            def record_step(obs, macro, line):
                """Append the current verb step (human agent; model fields mocked)."""
                solved = exit_pos is not None and reached_exit(
                    obs, exit_pos, args.radius
                )
                call = make_call(
                    '',
                    '',
                    '',
                    line,
                    mock_usage,
                    accepted=True,
                    macro=macro,
                    result=obs.result,
                )
                recorder.write_step(
                    make_step(next(counter), obs, [call], 'SOLVED' if solved else '')
                )

        try:
            run_repl(session, record_step)
        finally:
            if recorder is not None:
                recorder.close()
                print(f'  recorded -> {args.record}')
            session.close()
    finally:
        if game is not None:
            print('tearing down game instance ...')
            game.stop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
