"""Run the frozen-VLM eval on a chamber -> a `.trajectory`.

Launches (or attaches to) a game instance, then lets Gemini drive the chamber.
Needs GEMINI_API_KEY in the repo `.env` and a video-mode instance (for frames).

    uv run python py/run_eval.py --map testchamber_000 --exit 256,128,64 --radius 64
"""

import argparse
import sys

from dotenv import load_dotenv
from llm_eval.gemini_agent import GeminiAgent
from llm_eval.gemini_agent import run_eval
from testchamber_session import TestChamberSession
from testchamber_session import launch_or_attach


def main():
    """Launch (or attach), run the eval, write the trajectory, tear down."""
    load_dotenv()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--map', required=True, help='chamber map name')
    parser.add_argument('--exit', required=True, help='exit position "x,y,z"')
    parser.add_argument(
        '--radius', type=float, default=64.0, help='exit success radius (units)'
    )
    parser.add_argument('--out', default='eval.trajectory', help='output path')
    parser.add_argument(
        '--instance', type=int, default=0, help='instance N -> port 50000+N'
    )
    parser.add_argument(
        '--attach', action='store_true', help='drive a running instance'
    )
    parser.add_argument('--timeout', type=float, default=180.0, help='boot wait (s)')
    parser.add_argument('--max-steps', type=int, default=25, help='step budget')
    args = parser.parse_args()

    exit_pos = tuple(float(v) for v in args.exit.split(','))
    if len(exit_pos) != 3:
        parser.error('--exit must be "x,y,z"')
    cfg = {'map': args.map, 'exit_pos': exit_pos, 'success_radius': args.radius}

    try:
        harness, game, _ = launch_or_attach(args.instance, args.attach, args.timeout)
    except Exception as e:  # noqa: BLE001 -- boot failures are fatal, just report
        print(f'fatal: {e}')
        return 1
    try:
        session = TestChamberSession(harness, args.map)
        agent = GeminiAgent(cfg['exit_pos'])
        terminal = run_eval(session, agent, cfg, args.out, args.max_steps)
        print(f'{terminal}  -> {args.out}')
        session.close()
        return 0 if terminal == 'SOLVED' else 1
    except Exception as e:  # noqa: BLE001 -- report and exit non-zero
        print(f'fatal: {e}')
        return 1
    finally:
        if game is not None:
            print('tearing down game instance ...')
            game.stop()


if __name__ == '__main__':
    sys.exit(main())
