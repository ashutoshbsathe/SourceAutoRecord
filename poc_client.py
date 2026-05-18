#!/usr/bin/env python3
"""
Portal 2 AgentLoop benchmark & RL training loop demo.

Demonstrates the AgentLoop streaming RPC with episode resets.
When the player dies (health <= 0) or a random reset is triggered,
the stream is cancelled and a new episode starts via the Reset RPC.

Usage:
    # Basic benchmark (10 seconds, no rendering)
    python3 poc_client.py --duration 10

    # With OpenCV rendering
    python3 poc_client.py --duration 10 --render

    # RL-style: random resets with map switching
    python3 poc_client.py --duration 30 --reset-prob 0.005

    # All flags
    python3 poc_client.py --duration 60 --num-ticks 1 --reset-prob 0.01 --render

Prerequisites: Portal 2 running with sar_harness 1, map loaded, warmup complete.
"""

import argparse
import grpc
import random
import time
import threading
import harness_pb2
import harness_pb2_grpc

MAPS = ['sp_a2_triple_laser', 'sp_a2_laser_chaining']


def parse_args():
    parser = argparse.ArgumentParser(
        description='Portal 2 AgentLoop benchmark & RL demo'
    )
    parser.add_argument(
        '--render',
        action='store_true',
        help='Enable OpenCV rendering of SHM framebuffer',
    )
    parser.add_argument(
        '--duration',
        type=float,
        default=10.0,
        help='Total benchmark duration in seconds (default: 10)',
    )
    parser.add_argument(
        '--num-ticks',
        type=int,
        default=1,
        help='num_ticks per AgentMessage (default: 1)',
    )
    parser.add_argument(
        '--reset-prob',
        type=float,
        default=0.0,
        help='Per-step probability of triggering a random episode reset (default: 0, disabled)',
    )
    parser.add_argument(
        '--address',
        type=str,
        default='localhost:50051',
        help='gRPC server address (default: localhost:50051)',
    )
    return parser.parse_args()


# docs/poc_client.py:run_episode>
def run_episode(
    stub, args, episode_num, stats, stop_event, shm=None, shm_width=0, shm_height=0
):
    """Run a single AgentLoop episode. Returns the reason it ended."""
    if args.render:
        import cv2
        import numpy as np

    send_times = []
    recv_times = []
    step_count = 0
    end_reason = 'duration'

    def action_generator():
        nonlocal step_count
        step = 0
        while not stop_event.is_set():
            action = harness_pb2.ActionRequest(
                num_ticks=args.num_ticks,
                key_forward=True,
                mouse_dx=2.0 if step % 120 < 60 else -2.0,
                key_jump=True if step % 30 == 0 else False,
            )
            send_times.append(time.perf_counter())
            yield harness_pb2.AgentMessage(
                action=action,
                copy_pixels_to_shm=args.render,
            )
            step += 1

    ep_start = time.perf_counter()

    try:
        responses = stub.AgentLoop(action_generator())

        for i, env_msg in enumerate(responses):
            recv_times.append(time.perf_counter())

            # Wall-clock duration check
            if time.perf_counter() - stats['bench_start'] >= args.duration:
                stop_event.set()
                responses.cancel()
                end_reason = 'duration'
                break

            if not env_msg.success:
                print(
                    f'  [Ep {episode_num}] Error at step {i}: {env_msg.error_message}'
                )
                end_reason = 'error'
                break

            # Death detection — client decides episode boundary
            if env_msg.state.health <= 0:
                print(f'  [Ep {episode_num}] Player died at step {i}')
                stop_event.set()
                responses.cancel()
                end_reason = 'death'
                break

            # Random reset (simulates RL episode truncation)
            if args.reset_prob > 0 and random.random() < args.reset_prob:
                print(f'  [Ep {episode_num}] Random reset triggered at step {i}')
                stop_event.set()
                responses.cancel()
                end_reason = 'random_reset'
                break

            # Render if requested
            if args.render and shm is not None:
                frame = np.ndarray(
                    (shm_height, shm_width, 3), dtype=np.uint8, buffer=shm.buf
                )
                bgr_frame = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                cv2.imshow('Portal 2 RL Demo', bgr_frame)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    stop_event.set()
                    responses.cancel()
                    end_reason = 'user_quit'
                    break

            step_count = i + 1

            if i % 100 == 0:
                pos = env_msg.state.position
                print(
                    f'  [Ep {episode_num}] Step {i:>5d}  '
                    f'pos=({pos.x:.1f}, {pos.y:.1f}, {pos.z:.1f})  '
                    f'hp={env_msg.state.health}'
                )

    except grpc.RpcError as e:
        if e.code() != grpc.StatusCode.CANCELLED:
            print(f'  [Ep {episode_num}] RPC Error: {e.code()}: {e.details()}')
            end_reason = 'rpc_error'

    ep_duration = time.perf_counter() - ep_start

    # Record per-episode stats
    total_steps = min(len(send_times), len(recv_times))
    latencies = [(recv_times[i] - send_times[i]) * 1000 for i in range(total_steps)]

    stats['total_steps'] += total_steps
    stats['all_latencies'].extend(latencies)
    stats['episode_durations'].append(ep_duration)
    stats['episode_steps'].append(total_steps)
    stats['end_reasons'].append(end_reason)

    return end_reason


def main():
    args = parse_args()

    # Conditional imports for rendering
    shm = None
    shm_width, shm_height = 0, 0

    print(f'Connecting to Portal 2 Harness at {args.address}...')
    channel = grpc.insecure_channel(args.address)
    stub = harness_pb2_grpc.Portal2HarnessStub(channel)

    # Handshake
    print('Performing Handshake...')
    resp = stub.InitialHandshake(
        harness_pb2.HandshakeRequest(
            client_version='poc_client rl_demo', client_id='rl_demo'
        )
    )
    print(f'Connected to {resp.game_version}, map: {resp.map_name}')
    shm_width = resp.shm_width
    shm_height = resp.shm_height
    print(f'SHM: {shm_width}x{shm_height} ({resp.shm_size} bytes)')

    if args.render:
        from multiprocessing import shared_memory

        if resp.shm_size == 0:
            print('Error: Handshake returned 0 size for SHM. Cannot render.')
            return
        try:
            shm = shared_memory.SharedMemory(name='portal2_harness_framebuffer')
        except FileNotFoundError:
            print('Failed to open shared memory. Ensure SAR is running.')
            return
        print('SHM mapped successfully!')

    time.sleep(2)

    print(f'\n{"=" * 50}')
    print(f'  AgentLoop RL Demo')
    print(f'  Duration:      {args.duration}s')
    print(f'  Render:        {"ON" if args.render else "OFF"}')
    print(f'  Ticks/action:  {args.num_ticks}')
    print(f'  Reset prob:    {args.reset_prob}')
    print(f'{"=" * 50}\n')

    # Aggregate stats across all episodes
    stats = {
        'bench_start': time.perf_counter(),
        'total_steps': 0,
        'all_latencies': [],
        'episode_durations': [],
        'episode_steps': [],
        'end_reasons': [],
    }

    episode = 0
    map_idx = 0

    while True:
        elapsed = time.perf_counter() - stats['bench_start']
        if elapsed >= args.duration:
            break

        episode += 1
        stop_event = threading.Event()

        print(f'\n--- Episode {episode} ---')
        end_reason = run_episode(
            stub,
            args,
            episode,
            stats,
            stop_event,
            shm=shm,
            shm_width=shm_width,
            shm_height=shm_height,
        )

        if end_reason in ('duration', 'user_quit', 'rpc_error'):
            break

        if end_reason in ('death', 'random_reset'):
            # Switch map for variety
            map_idx = (map_idx + 1) % len(MAPS)
            next_map = MAPS[map_idx]
            print(f'  Resetting to {next_map}...')

            try:
                reset_resp = stub.Reset(harness_pb2.ResetRequest(map_name=next_map))
                if reset_resp.success:
                    pos = reset_resp.initial_state.position
                    print(
                        f'  Reset OK — pos=({pos.x:.1f}, {pos.y:.1f}, {pos.z:.1f}), '
                        f'hp={reset_resp.initial_state.health}'
                    )
                else:
                    print(f'  Reset failed: {reset_resp.error_message}')
                    break
            except grpc.RpcError as e:
                print(f'  Reset RPC error: {e.code()}: {e.details()}')
                break

    # Print stats
    wall_duration = time.perf_counter() - stats['bench_start']
    total_steps = stats['total_steps']
    all_latencies = stats['all_latencies']
    num_episodes = len(stats['episode_durations'])

    print(f'\n{"=" * 50}')
    print(f'  BENCHMARK RESULTS')
    print(f'{"=" * 50}')
    print(f'  Wall duration:       {wall_duration:.2f}s')
    print(f'  Episodes:            {num_episodes}')
    print(f'  Total steps:         {total_steps}')

    if wall_duration > 0:
        print(f'  Actions/sec:         {total_steps / wall_duration:.2f}')
        print(
            f'  Game ticks/sec:      {(total_steps * args.num_ticks) / wall_duration:.2f}'
        )

    if all_latencies:
        sorted_lat = sorted(all_latencies)
        avg = sum(sorted_lat) / len(sorted_lat)
        p50 = sorted_lat[int(len(sorted_lat) * 0.50)]
        p95 = sorted_lat[int(min(len(sorted_lat) * 0.95, len(sorted_lat) - 1))]
        p99 = sorted_lat[int(min(len(sorted_lat) * 0.99, len(sorted_lat) - 1))]
        print(f'  Avg step latency:    {avg:.2f}ms')
        print(f'  Min step latency:    {sorted_lat[0]:.2f}ms')
        print(f'  Max step latency:    {sorted_lat[-1]:.2f}ms')
        print(f'  P50 latency:         {p50:.2f}ms')
        print(f'  P95 latency:         {p95:.2f}ms')
        print(f'  P99 latency:         {p99:.2f}ms')

    print(f'  Render:              {"ON" if args.render else "OFF"}')
    print(f'  Ticks/action:        {args.num_ticks}')
    print(f'  Reset prob:          {args.reset_prob}')

    if num_episodes > 1:
        print(f'\n  Per-episode breakdown:')
        for i, (steps, dur, reason) in enumerate(
            zip(
                stats['episode_steps'], stats['episode_durations'], stats['end_reasons']
            )
        ):
            print(f'    Ep {i + 1}: {steps:>5d} steps, {dur:.2f}s, ended: {reason}')

    print(f'{"=" * 50}')

    # Cleanup
    if args.render:
        import cv2

        if shm:
            shm.close()
        cv2.destroyAllWindows()

    print('Done.')


if __name__ == '__main__':
    main()
