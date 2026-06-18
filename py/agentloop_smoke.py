"""Harness integration smoke test: exercise every gRPC endpoint against a live game.

Expensive -- it boots a real Portal 2 instance -- so run it by hand after any
change to the harness to confirm the gRPC surface still works end to end. Each
endpoint is a self-contained check that passes or fails on its own (handshake,
Reset, Observe, Act, AgentLoop, macro verbs (aim_at/move/go_to), the Python
client (percept parse + local validation + step_macro), pixels-over-shared-memory,
ExecuteCommand); the process exits non-zero if any fail. Observations and a few
captured framebuffers are written under --out so you can eyeball them.

Usage:
    uv run python py/agentloop_smoke.py                 # launch a fresh instance
    uv run python py/agentloop_smoke.py --map sp_a1_intro5
    uv run python py/agentloop_smoke.py --attach --instance 0

Notes:
    * Launching needs gamescope + Steam Portal 2 (same as the trainer).
    * --instance N -> gRPC port 50000+N. Pick a free N if others are running.
"""

import argparse
import json
import math
import os
import sys
import time

import cv2
import grpc
import numpy as np

from game_launcher import (
    DEFAULT_GAME_ARGS,
    DEFAULT_GAMESCOPE_ARGS,
    GameInstance,
    get_instance_specific_args,
)
from p2harness import harness_pb2
from p2harness import macro_grammar
from p2harness.entities import WorldView
from p2harness.harness import P2Harness


class CheckError(Exception):
    """A check's expectation was not met."""


def require(condition, message):
    """Fail the current check with `message` unless `condition` holds."""
    if not condition:
        raise CheckError(message)


class Context:
    """Shared state handed to every check: the client, args, and an artifact sink."""

    def __init__(self, harness, args, out_dir):
        self.harness = harness
        self.args = args
        self.out_dir = out_dir
        self.observations = []  # records dumped to observations.json


def gamestate_dict(gs, label):
    """Flatten a GameState into a JSON-friendly record for the observations dump."""
    return {
        'label': label,
        'server_tick': gs.server_tick,
        'position': [gs.position.x, gs.position.y, gs.position.z],
        'velocity': [gs.velocity.x, gs.velocity.y, gs.velocity.z],
        'camera': [gs.camera.x, gs.camera.y, gs.camera.z],
        'health': gs.health,
        'is_crouching': gs.is_crouching,
        'chamber_complete': gs.chamber_complete,
        'exit_signal_mask': gs.exit_signal_mask,
        'entities': len(gs.entity_snapshot.entities),
        'is_full_snapshot': gs.entity_snapshot.is_full_snapshot,
    }


def save_frame(path, frame):
    """Write an HxWx3 RGB frame to `path` as a PNG."""
    cv2.imwrite(path, frame[:, :, ::-1])  # OpenCV expects BGR


def action(num_ticks, mouse_dx=3.0):
    """Build an ActionRequest that walks forward and turns for `num_ticks` ticks."""
    return harness_pb2.ActionRequest(
        num_ticks=num_ticks, key_forward=True, mouse_dx=mouse_dx
    )


def reset_to_spawn(ctx):
    """Reload the map so a movement-dependent check starts from a known spawn --
    prior checks leave the player displaced, which derails go_to/move."""
    resp = ctx.harness.reset(map_name=ctx.args.map)
    require(resp.success, f'reset failed: {resp.error_message}')


# --- Checks. Each takes the Context, returns a one-line detail, raises on failure. ---


def check_reset(ctx):
    """Reset restarts the level and returns a full initial snapshot."""
    resp = ctx.harness.reset(map_name=ctx.args.map)
    require(resp.success, f'reset failed: {resp.error_message}')
    gs = resp.initial_state
    require(gs.entity_snapshot.is_full_snapshot, 'initial snapshot is not full')
    require(len(gs.entity_snapshot.entities) > 0, 'initial snapshot has no entities')
    ctx.observations.append(gamestate_dict(gs, 'reset.initial_state'))
    return f'{len(gs.entity_snapshot.entities)} entities, tick={gs.server_tick}'


def check_observe(ctx):
    """A standalone Observe returns a sane GameState."""
    gs = ctx.harness.observe()
    require(math.isfinite(gs.position.x), 'position is not finite')
    require(0 <= gs.health <= 200, f'health out of range: {gs.health}')
    require(gs.server_tick >= 0, f'negative server_tick: {gs.server_tick}')
    ctx.observations.append(gamestate_dict(gs, 'observe'))
    p = gs.position
    return f'pos=({p.x:.0f},{p.y:.0f},{p.z:.0f}) health={gs.health}'


def check_act(ctx):
    """A synchronous Act advances the simulation by the requested ticks."""
    before = ctx.harness.observe().server_tick
    resp = ctx.harness.act(action(ctx.args.num_ticks))
    require(resp.success, f'act failed: {resp.error_message}')
    after = ctx.harness.observe().server_tick
    require(after > before, f'server_tick did not advance ({before} -> {after})')
    return f'+{after - before} server ticks for {ctx.args.num_ticks} requested'


def check_agentloop(ctx):
    """A multi-step AgentLoop streams one state per action, ticks strictly rising."""
    n = ctx.args.steps
    ticks = []
    first_full = None
    ctx.harness.start_agent_loop()
    try:
        for i in range(n):
            env = ctx.harness.step_agent_loop(
                harness_pb2.AgentMessage(action=action(ctx.args.num_ticks)),
                timeout=30.0,
            )
            require(env.success, f'step {i} failed: {env.error_message}')
            ticks.append(env.state.server_tick)
            if i == 0:
                first_full = env.state.entity_snapshot.is_full_snapshot
            ctx.observations.append(gamestate_dict(env.state, f'agentloop[{i}]'))
    finally:
        ctx.harness.stop_agent_loop()
    require(len(ticks) == n, f'expected {n} responses, got {len(ticks)}')
    require(
        all(b > a for a, b in zip(ticks, ticks[1:])),
        f'ticks not strictly increasing: {ticks}',
    )
    require(first_full, 'first stream observation was not a full snapshot')
    return f'{n} steps, ticks {ticks[0]} -> {ticks[-1]}, first snapshot full'


def _norm_deg(d):
    """Wrap an angle (delta) into [-180, 180]."""
    return (d + 180.0) % 360.0 - 180.0


def check_macro(ctx):
    """Macro executor + the SetAngles aim check. aim_at points the view at a mark;
    the verb reports the angle it *commanded* (MacroResult.aim_yaw) and the
    refreshed percept carries the *actual* post-tick view (state.camera). They
    agree iff SetAngles survived the TAS per-tick view re-apply -- the
    load-bearing assumption for every later verb.

    Asserting commanded-yaw == actual-yaw is exact and immune to eye height and
    OBB-centre-vs-origin (the verb already aimed at the OBB centre). We pick the
    mark needing the biggest turn and require that turn be large, so a clobbered
    or ignored SetAngles can't pass by the camera already facing the mark. The
    mark<->on-screen-label match stays a manual visual check."""
    ctx.harness.start_agent_loop()
    try:
        # First stream step is a full snapshot; a no-op wait fetches it + the
        # pre-aim view cheaply.
        env = ctx.harness.step_agent_loop(
            harness_pb2.AgentMessage(
                macro=harness_pb2.MacroRequest(verb='wait', ticks=1)
            ),
            timeout=30.0,
        )
        require(env.HasField('macro_result'), 'no macro_result on wait (stale sar.so?)')
        require(env.macro_result.ok, f'wait failed: {env.macro_result.result_code}')
        pre_yaw = env.state.camera.y
        px, py = env.state.position.x, env.state.position.y

        # Pick the marked entity that demands the biggest turn (and is far enough
        # horizontally that its bearing is stable). This origin-based bearing is
        # only for *selection*; the pass/fail assertion uses the verb's own
        # commanded yaw, so OBB-centre-vs-origin never enters the verdict.
        best, best_turn = None, -1.0
        for e in env.state.entity_snapshot.entities:
            if e.mark <= 0:
                continue
            dx, dy = e.position.x - px, e.position.y - py
            if math.hypot(dx, dy) <= 64:
                continue
            turn = abs(_norm_deg(math.degrees(math.atan2(dy, dx)) - pre_yaw))
            if turn > best_turn:
                best, best_turn = e, turn
        require(best is not None, 'no marked entity with usable horizontal offset')
        require(
            best_turn > 20.0,
            f'no marked entity needs a >20 deg turn (max {best_turn:.1f}); '
            f'aim spike would be inconclusive',
        )

        env = ctx.harness.step_agent_loop(
            harness_pb2.AgentMessage(
                macro=harness_pb2.MacroRequest(verb='aim_at', mark=best.mark)
            ),
            timeout=30.0,
        )
    finally:
        ctx.harness.stop_agent_loop()

    require(env.success, f'aim_at step failed at RPC level: {env.error_message}')
    require(env.HasField('macro_result'), 'no macro_result on aim_at')
    mr = env.macro_result
    require(
        mr.ok and mr.result_code == 'SUCCESS',
        f'aim_at not SUCCESS: {mr.result_code} ({mr.detail})',
    )

    # SetAngles survived iff the actual post-tick view == the commanded one.
    yaw_err = abs(_norm_deg(env.state.camera.y - mr.aim_yaw))
    ctx.observations.append(gamestate_dict(env.state, f'macro.aim_at[{best.mark}]'))
    require(
        yaw_err < 3.0,
        f'aim_at mark={best.mark}: camera yaw {env.state.camera.y:.1f} != '
        f'commanded {mr.aim_yaw:.1f} (err {yaw_err:.1f} deg, turn was '
        f'{best_turn:.0f}) -- SetAngles did NOT survive the tick',
    )
    return (
        f'aim_at mark={best.mark}: turned {best_turn:.0f} deg, '
        f'camera==commanded within {yaw_err:.2f} deg -- SetAngles survives'
    )


def check_move(ctx):
    """move drives the player: forward for N ticks either advances (moved_dist)
    or legitimately stops at a wall/edge (the guard fired) -- both are correct.
    Resets first so it starts from a clean spawn."""
    reset_to_spawn(ctx)
    ctx.harness.start_agent_loop()
    try:
        env = ctx.harness.step_agent_loop(
            harness_pb2.AgentMessage(
                macro=harness_pb2.MacroRequest(verb='move', dir='forward', ticks=20)
            ),
            timeout=30.0,
        )
    finally:
        ctx.harness.stop_agent_loop()
    require(env.success, f'move step RPC failed: {env.error_message}')
    require(env.HasField('macro_result'), 'no macro_result on move')
    mr = env.macro_result
    require(
        mr.result_code in ('COMPLETED', 'ADVANCED', 'WALL', 'EDGE', 'STUCK'),
        f'unexpected move result_code {mr.result_code!r}',
    )
    # Pass if it advanced, or the verb correctly aborted (guard/stuck). A
    # COMPLETED with ~no movement is the broken case and must fail. (Whether a
    # WALL/EDGE/STUCK *should* have fired here is locomotion quality -- covered
    # by check_go_to's progress assertion, not this wiring check.)
    require(
        mr.moved_dist > 1.0 or mr.result_code in ('WALL', 'EDGE', 'STUCK'),
        f'move forward ran but neither advanced nor aborted '
        f'(moved {mr.moved_dist:.1f}, {mr.result_code})',
    )
    return f'move forward 20t -> moved {mr.moved_dist:.0f}u ({mr.result_code})'


def check_go_to(ctx):
    """go_to walks toward the farthest mark and must make real progress (or
    reach it). An immediate BLOCKED/STUCK with no headway is treated as a
    FAILURE -- it usually means the guard false-fired on open ground or the
    march is broken (the regression this check exists to catch). Resets first for
    a clean spawn; needs a chamber whose farthest mark is roughly walk-reachable."""
    reset_to_spawn(ctx)
    ctx.harness.start_agent_loop()
    try:
        env = ctx.harness.step_agent_loop(
            harness_pb2.AgentMessage(
                macro=harness_pb2.MacroRequest(verb='wait', ticks=1)
            ),
            timeout=30.0,
        )
        px, py = env.state.position.x, env.state.position.y
        best, best_d = None, -1.0
        for e in env.state.entity_snapshot.entities:
            if e.mark <= 0:
                continue
            d = math.hypot(e.position.x - px, e.position.y - py)
            if d > best_d:
                best, best_d = e, d
        require(best is not None, 'no marked entity to go_to')
        require(best_d > 64, f'farthest mark too close to test go_to ({best_d:.0f}u)')

        env = ctx.harness.step_agent_loop(
            harness_pb2.AgentMessage(
                macro=harness_pb2.MacroRequest(verb='go_to', mark=best.mark)
            ),
            timeout=90.0,  # go_to may run up to kGoToMaxTicks (400) ticks
        )
    finally:
        ctx.harness.stop_agent_loop()
    require(env.success, f'go_to step RPC failed: {env.error_message}')
    mr = env.macro_result
    require(
        mr.result_code in ('SUCCESS', 'ADVANCED', 'STUCK', 'BLOCKED', 'UNREACHABLE'),
        f'unexpected go_to result_code {mr.result_code!r}',
    )
    ctx.observations.append(gamestate_dict(env.state, f'macro.go_to[{best.mark}]'))
    # Measure progress client-side as the player's actual closing distance to the
    # target, both to the entity ORIGIN -- so best_d and final_d are commensurable
    # (no server-OBB-centre vs client-origin mismatch). Require real headway, not
    # just a stop-reason: an immediate BLOCKED/STUCK with ~0 progress is the
    # false-firing-guard / broken-march bug. A partial march that then blocks
    # (progress>16) is fine, so this stays robust on non-flat chambers.
    fpx, fpy = env.state.position.x, env.state.position.y
    final_d = math.hypot(best.position.x - fpx, best.position.y - fpy)
    require(
        mr.reached or best_d - final_d > 16.0,
        f'go_to made no progress toward mark={best.mark}: '
        f'{best_d:.0f}->{final_d:.0f}u away (code={mr.result_code}) -- '
        f'guard false-fire, broken march, or target not walk-reachable here?',
    )
    return (
        f'go_to mark={best.mark}: {best_d:.0f}->{final_d:.0f}u away, '
        f'reached={mr.reached} ({mr.result_code})'
    )


def check_client(ctx):
    """The Python macro client end to end: merge the streamed percept into
    marked-entity dicts (WorldView), validate an action locally against that
    percept (macro_grammar), then send the validated macro via step_macro and get
    its structured result back. Resets first for a clean spawn."""
    reset_to_spawn(ctx)
    ctx.harness.start_agent_loop()
    try:
        world = WorldView()
        # The first stream step is a full snapshot; later ones are deltas that
        # WorldView merges. A no-op wait fetches it.
        _, state = ctx.harness.step_macro(
            harness_pb2.MacroRequest(verb='wait', ticks=1)
        )
        marks = world.observe(state)
        require(marks, 'percept parsed no marked entities')
        keys = {'mark', 'class', 'name', 'pos', 'dist', 'bearing', 'state'}
        bad = next((m for m in marks if not keys <= set(m)), None)
        require(bad is None, f'percept dict missing fields: {bad}')

        # Local validation: a present mark passes; an absent one is rejected as a
        # structured string with no gRPC round-trip.
        target = marks[0]['mark']
        good = macro_grammar.validate(f'aim_at {target}', marks)
        require(
            isinstance(good, harness_pb2.MacroRequest), f'good aim_at rejected: {good}'
        )
        reject = macro_grammar.validate('aim_at 999999', marks)
        require(isinstance(reject, str), 'validate accepted an absent mark')

        # Send the validated macro; the structured result must come back.
        mr, _ = ctx.harness.step_macro(good)
        require(mr.result_code, 'step_macro returned an empty macro_result')
    finally:
        ctx.harness.stop_agent_loop()
    classes = sorted({m['class'] for m in marks})
    return (
        f'{len(marks)} marks parsed; validate accept+reject + step_macro '
        f'({mr.result_code}) ok; classes={classes}'
    )


def check_pixels(ctx):
    """copy_pixels_to_shm fills shared memory with a non-blank frame that changes."""
    require(ctx.harness.shm is not None, 'no shared memory mapped (no video mode?)')
    frames = []
    ctx.harness.start_agent_loop()
    try:
        for i in range(3):
            ctx.harness.step_agent_loop(
                harness_pb2.AgentMessage(
                    action=action(ctx.args.num_ticks, mouse_dx=8.0),
                    copy_pixels_to_shm=True,
                ),
                timeout=30.0,
            )
            frame = ctx.harness.get_shm_pixels()
            save_frame(os.path.join(ctx.out_dir, f'frame_{i}.png'), frame)
            frames.append(frame)
    finally:
        ctx.harness.stop_agent_loop()
    nonzero = max(float((f != 0).mean()) for f in frames)
    require(nonzero > 0.01, f'frames look blank (max nonzero fraction {nonzero:.3f})')
    require(
        any(not np.array_equal(frames[0], f) for f in frames[1:]),
        'frames did not change across movement',
    )
    return f'{len(frames)} frames saved, nonzero≈{nonzero:.2f}, varying'


def check_execute_command(ctx):
    """ExecuteCommand runs a console command and reports success."""
    resp = ctx.harness.execute_command('echo harness_smoke', timeout=30.0)
    require(resp.success, f'command failed: {resp.error_message}')
    return 'echo ran'


CHECKS = [
    ('reset', check_reset),
    ('observe', check_observe),
    ('act', check_act),
    ('agentloop', check_agentloop),
    ('macro', check_macro),
    ('move', check_move),
    ('go_to', check_go_to),
    ('client', check_client),
    ('pixels', check_pixels),
    ('execute_command', check_execute_command),
]


def wait_for_handshake(harness, instance, timeout):
    """Retry handshake until the server answers, or the game dies / times out."""
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
                    f'gRPC server not reachable after {timeout:.0f}s'
                ) from e
            attempt += 1
            print(f'  waiting for harness... (attempt {attempt})')
            time.sleep(min(3.0, 1.0 + 0.5 * attempt))


def run_checks(ctx):
    """Run every check, print a PASS/FAIL line each, and return the failure count."""
    print(f'\n=== harness smoke: {len(CHECKS)} checks ===')
    failures = 0
    for name, fn in CHECKS:
        start = time.perf_counter()
        try:
            detail = fn(ctx)
            ok = True
        except Exception as e:
            detail = f'{type(e).__name__}: {e}'
            ok = False
            failures += 1
        elapsed = time.perf_counter() - start
        print(f'  [{"PASS" if ok else "FAIL"}] {name} ({elapsed:.1f}s) -- {detail}')
    return failures


def main():
    """Launch or attach, run the checks, dump artifacts, and set the exit code."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--instance', type=int, default=0, help='instance N -> port 50000+N'
    )
    parser.add_argument(
        '--map', default='', help='map to reset to (default: current level)'
    )
    parser.add_argument('--attach', action='store_true', help='use a running instance')
    parser.add_argument('--steps', type=int, default=10, help='AgentLoop steps')
    parser.add_argument('--num-ticks', type=int, default=4, help='ticks per action')
    parser.add_argument(
        '--out', default='agentloop_smoke_out', help='artifact directory'
    )
    parser.add_argument('--timeout', type=float, default=180.0, help='boot wait (s)')
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    address = f'localhost:{50000 + args.instance}'
    instance = None
    exit_code = 2  # stays 2 if we never reach the checks (fatal setup failure)
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
        hs = wait_for_handshake(harness, instance, args.timeout)
        print(f'  handshake ok: {hs.shm_width}x{hs.shm_height} shm, map={hs.map_name}')

        ctx = Context(harness, args, args.out)
        failures = run_checks(ctx)

        with open(os.path.join(args.out, 'observations.json'), 'w') as f:
            json.dump(ctx.observations, f, indent=2)

        passed = len(CHECKS) - failures
        print(f'\n{passed}/{len(CHECKS)} passed. artifacts in {args.out}/')
        harness.close()
        exit_code = 1 if failures else 0
    except RuntimeError as e:
        print(f'FATAL: {e}')
    finally:
        if instance is not None:
            print('Tearing down game instance ...')
            instance.stop()
    sys.exit(exit_code)


if __name__ == '__main__':
    main()
