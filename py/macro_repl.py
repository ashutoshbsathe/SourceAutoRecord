"""Interactive macro REPL: type closed-verb commands and drive the frozen game.

A human-driven front-end to the macro executor -- the manual analogue of the
eventual LLM ReAct driver, and a fast way to find a solve sequence by hand
instead of hardcoding one. Launches its own instance (or --attach), then takes
commands:

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
import time

try:
    import readline  # noqa: F401 -- importing it gives input() up-arrow history
except ImportError:  # pragma: no cover -- not present on every platform
    readline = None

import grpc
from game_launcher import DEFAULT_GAME_ARGS
from game_launcher import DEFAULT_GAMESCOPE_ARGS
from game_launcher import GameInstance
from game_launcher import get_instance_specific_args
from p2harness import harness_pb2
from p2harness.harness import P2Harness

# Verbs whose march can run many ticks need a generous per-step timeout.
SLOW_VERBS = {'go_to', 'interact'}
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
    """Compact (x,y,z) string for a Vector3-like position."""
    return f'({p.x:.0f},{p.y:.0f},{p.z:.0f})'


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


def dump_marks(state):
    """Print every marked entity (mark, class, position, name)."""
    rows = sorted(
        (e for e in state.entity_snapshot.entities if e.mark > 0),
        key=lambda e: e.mark,
    )
    if not rows:
        print('  (no marked entities)')
        return
    for e in rows:
        name = f' "{e.target_name}"' if e.target_name else ''
        print(f'  [{e.mark:>2}] {e.class_name:<22} {fmt_pos(e.position)}{name}')


def send(harness, macro):
    """Send one macro and return its EnvironmentMessage."""
    timeout = 90.0 if macro.verb in SLOW_VERBS else 30.0
    return harness.step_agent_loop(
        harness_pb2.AgentMessage(macro=macro), timeout=timeout
    )


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


def report(macro, env):
    """Print the macro result plus the player and (if anchored) the mark's pos."""
    mr = env.macro_result
    extra = ''
    if macro.mark:
        for e in env.state.entity_snapshot.entities:
            if e.mark == macro.mark:
                extra = f' mark{macro.mark}={fmt_pos(e.position)}'
                break
    print(
        f'  {mr.result_code:<13} {mr.detail}'
        f'   player={fmt_pos(env.state.position)}{extra}'
    )


def run_repl(harness, current_map):
    """The interactive command loop (the agent-loop stream must be started)."""
    if readline is not None:
        try:
            readline.read_history_file(HISTFILE)
        except OSError:
            pass
    transcript = []  # (command, result) per state-changing step, for `save`

    # Prime the stream so the first observe is a full snapshot.
    env = send(harness, harness_pb2.MacroRequest(verb='wait', ticks=1))
    if env.macro_result.result_code == 'NOT_READY':
        print('  (harness not ready -- still warming up? give it a moment)')

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
                dump_marks(env.state)
                continue
            if cmd == 'save':
                path = rest[0] if rest else 'macro_repl.transcript'
                write_transcript(path, current_map, transcript)
                print(f'  saved {len(transcript)} steps to {path}')
                continue
            if cmd == 'reset':
                target = rest[0] if rest else current_map
                resp = harness.reset(map_name=target)
                if not resp.success:
                    print(f'  reset failed: {resp.error_message}')
                    continue
                current_map = target
                harness.start_agent_loop()
                env = send(harness, harness_pb2.MacroRequest(verb='wait', ticks=1))
                print(f'  reset ({current_map}).')
                transcript.append((line, f'-> {current_map}'))
                dump_marks(env.state)
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
                env = send(harness, macro)
            except Exception as e:  # noqa: BLE001 -- REPL: surface, don't crash
                print(f'  ! send failed: {type(e).__name__}: {e}')
                continue
            report(macro, env)
            mr = env.macro_result
            transcript.append((line, f'{mr.result_code} {mr.detail}'.strip()))
    finally:
        if readline is not None:
            try:
                readline.write_history_file(HISTFILE)
            except OSError:
                pass


def wait_for_handshake(harness, instance, timeout):
    """Retry the handshake until the server answers, or the game dies / times out."""
    deadline = time.monotonic() + timeout
    attempt = 0
    while True:
        try:
            return harness.handshake()
        except grpc.RpcError as e:
            if instance is not None and not instance.is_alive():
                raise RuntimeError(
                    f'game process died during boot; see {instance.log_file_path}'
                ) from e
            if time.monotonic() >= deadline:
                raise RuntimeError(
                    f'no harness at {harness.address} after {timeout:.0f}s'
                ) from e
            attempt += 1
            print(f'  waiting for harness... (attempt {attempt})')
            time.sleep(min(3.0, 1.0 + 0.5 * attempt))


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

    address = f'localhost:{50000 + args.instance}'
    instance = None
    try:
        if not args.attach:
            print(f'launching game instance {args.instance} ...')
            instance = GameInstance(
                instance_id=args.instance,
                gamescope_args=DEFAULT_GAMESCOPE_ARGS.copy(),
                game_args=DEFAULT_GAME_ARGS.copy()
                + get_instance_specific_args(args.instance),
            )
            instance.start()

        harness = P2Harness(address=address)
        try:
            hs = wait_for_handshake(harness, instance, args.timeout)
        except RuntimeError as e:
            print(f'fatal: {e}')
            return 1
        current_map = args.map or hs.map_name
        if args.map and args.map != hs.map_name:
            print(f'loading map {args.map} ...')
            resp = harness.reset(map_name=args.map)
            if not resp.success:
                print(f'  load failed ({resp.error_message}); staying on {hs.map_name}')
                current_map = hs.map_name
        print(f'attached {address}  map={current_map or "<none>"}  -- type "help"')
        harness.start_agent_loop()
        try:
            run_repl(harness, current_map)
        finally:
            harness.stop_agent_loop()
            harness.close()
    finally:
        if instance is not None:
            print('tearing down game instance ...')
            instance.stop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
