"""One chamber-solve session over the harness: reset, step a macro, observe.

Wraps the gRPC client and the delta-snapshot percept (WorldView) into a single
object the REPL and the eval driver both drive -- only the action source
differs. The session is action-agnostic; today it steps closed-verb macros.
"""

import time
from dataclasses import dataclass

import grpc
import numpy as np
from game_launcher import DEFAULT_GAME_ARGS
from game_launcher import DEFAULT_GAMESCOPE_ARGS
from game_launcher import GameInstance
from game_launcher import get_instance_specific_args
from p2harness import harness_pb2
from p2harness.entities import WorldView
from p2harness.harness import P2Harness


@dataclass
class Observation:
    """One step's percept: the macro result, the marked world, and the frame."""

    state: harness_pb2.GameState
    marks: list[dict]
    result: harness_pb2.MacroResult
    frame: np.ndarray | None
    held_mark: int | None

    @property
    def player(self):
        """Player origin as an (x, y, z) tuple."""
        p = self.state.position
        return (p.x, p.y, p.z)

    @property
    def tick(self):
        """The server tick this observation was taken at."""
        return self.state.server_tick


def reached_exit(obs, exit_pos, radius):
    """True when the player is within `radius` of `exit_pos` (x, y, z)."""
    px, py, pz = obs.player
    ex, ey, ez = exit_pos
    return ((px - ex) ** 2 + (py - ey) ** 2 + (pz - ez) ** 2) ** 0.5 <= radius


class TestChamberSession:
    """Drive one chamber to a solve: reset, step macros, read the percept.

    Owns the harness, the running merged percept (WorldView), the carried-mark
    state, and the current map. step() blocks for as many engine ticks as the
    verb needs; capture_frame copies the annotated framebuffer for that step.
    """

    def __init__(self, harness, current_map=''):
        """Bind a connected harness and the booted/loaded chamber name."""
        self.harness = harness
        self.current_map = current_map
        self.world = WorldView()
        self.held_mark = None
        self.last = None

    def prime(self, capture_frame=False):
        """Start the action stream and fetch the first (full) observation."""
        self.harness.start_agent_loop()
        return self.step(harness_pb2.MacroRequest(verb='wait', ticks=1), capture_frame)

    def reset(self, map_name=None, capture_frame=False):
        """Full map reload (re-fires droppers), then re-prime. Raises on failure."""
        target = self.current_map if map_name is None else map_name
        resp = self.harness.reset(map_name=target)
        if not resp.success:
            raise RuntimeError(resp.error_message or 'reset failed')
        self.current_map = target
        self.world = WorldView()
        self.held_mark = None
        return self.prime(capture_frame)

    def step(self, macro, capture_frame=False):
        """Send one macro; merge the percept and return the Observation."""
        result, state = self.harness.step_macro(macro, copy_pixels=capture_frame)
        marks = self.world.observe(state)
        self._update_held(macro, result)
        frame = self.harness.get_shm_pixels() if capture_frame else None
        self.last = Observation(
            state=state,
            marks=marks,
            result=result,
            frame=frame,
            held_mark=self.held_mark,
        )
        return self.last

    def _update_held(self, macro, result):
        """Track the carried mark -- there is no engine held-flag."""
        if not result.ok:
            return
        if macro.verb == 'pick_up':
            self.held_mark = macro.mark
        elif macro.verb == 'release':
            self.held_mark = None

    def close(self):
        """Tear down the harness connection."""
        self.harness.close()


def _wait_for_handshake(harness, game, timeout):
    """Retry the handshake until the server answers, the game dies, or timeout."""
    deadline = time.monotonic() + timeout
    attempt = 0
    while True:
        try:
            return harness.handshake()
        except grpc.RpcError as e:
            if game is not None and not game.is_alive():
                raise RuntimeError(
                    f'game process died during boot; see {game.log_file_path}'
                ) from e
            if time.monotonic() >= deadline:
                raise RuntimeError(
                    f'no harness at {harness.address} after {timeout:.0f}s'
                ) from e
            attempt += 1
            print(f'  waiting for harness... (attempt {attempt})')
            time.sleep(min(3.0, 1.0 + 0.5 * attempt))


def launch_or_attach(instance=0, attach=False, timeout=180.0):
    """Launch a fresh game instance (or attach), handshake, return the harness.

    Returns (harness, game, booted_map); `game` is None when attaching, and is
    torn down here if the boot fails. On success the caller owns teardown.
    """
    address = f'localhost:{50000 + instance}'
    game = None
    harness = None
    try:
        if not attach:
            print(f'launching game instance {instance} ...')
            game = GameInstance(
                instance_id=instance,
                gamescope_args=DEFAULT_GAMESCOPE_ARGS.copy(),
                game_args=DEFAULT_GAME_ARGS.copy()
                + get_instance_specific_args(instance),
            )
            game.start()
        harness = P2Harness(address=address)
        hs = _wait_for_handshake(harness, game, timeout)
        return harness, game, hs.map_name
    except Exception:
        if harness is not None:
            harness.close()
        if game is not None:
            game.stop()
        raise
