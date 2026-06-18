"""Run the frozen-VLM eval on a chamber -> a `.trajectory`.

Launches (or attaches to) a game instance, then lets Gemini drive the chamber.
The engine auto-detects the exit and ends the run; Needs GEMINI_API_KEY in the
repo `.env` and a video-mode instance (for frames).

    uv run python py/run_eval.py --map testchamber_000
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
    parser.add_argument('--out', default='eval.trajectory', help='output path')
    parser.add_argument(
        '--instance', type=int, default=0, help='instance N -> port 50000+N'
    )
    parser.add_argument(
        '--attach', action='store_true', help='drive a running instance'
    )
    parser.add_argument('--timeout', type=float, default=180.0, help='boot wait (s)')
    parser.add_argument('--max-steps', type=int, default=30, help='step budget')
    args = parser.parse_args()

    cfg = {'map': args.map}

    try:
        harness, game, _ = launch_or_attach(args.instance, args.attach, args.timeout)
    except Exception as e:  # noqa: BLE001 -- boot failures are fatal, just report
        print(f'fatal: {e}')
        return 1
    try:
        session = TestChamberSession(harness, args.map)
        agent = GeminiAgent()
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
