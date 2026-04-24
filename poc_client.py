#!/usr/bin/env python3
"""
Portal 2 AgentLoop benchmark client.

Measures the throughput and latency of the AgentLoop bidirectional streaming RPC
with and without OpenCV rendering.

Usage:
    # Throughput without rendering (headless)
    python3 poc_client.py --duration 10

    # Throughput with rendering
    python3 poc_client.py --duration 10 --render

    # Multiple ticks per action
    python3 poc_client.py --duration 10 --num-ticks 4

Prerequisites: Portal 2 running with sar_harness 1, map loaded, warmup complete.
"""

import argparse
import grpc
import time
import threading
import harness_pb2
import harness_pb2_grpc


def parse_args():
    parser = argparse.ArgumentParser(description="Portal 2 AgentLoop benchmark")
    parser.add_argument("--render", action="store_true",
                        help="Enable OpenCV rendering of SHM framebuffer")
    parser.add_argument("--duration", type=float, default=10.0,
                        help="Total benchmark duration in seconds (default: 10)")
    parser.add_argument("--num-ticks", type=int, default=1,
                        help="num_ticks per AgentMessage (default: 1)")
    parser.add_argument("--address", type=str, default="localhost:50051",
                        help="gRPC server address (default: localhost:50051)")
    return parser.parse_args()


def main():
    args = parse_args()

    # Conditional imports for rendering
    if args.render:
        import cv2
        import numpy as np
        from multiprocessing import shared_memory

    print(f"Connecting to Portal 2 Harness at {args.address}...")
    channel = grpc.insecure_channel(args.address)
    stub = harness_pb2_grpc.Portal2HarnessStub(channel)

    # Handshake
    print("Performing Handshake...")
    resp = stub.InitialHandshake(harness_pb2.HandshakeRequest(
        client_version="poc_client benchmark",
        client_id="benchmark"
    ))
    print(f"Connected to {resp.game_version}")
    shm_width = resp.shm_width
    shm_height = resp.shm_height
    shm_size = resp.shm_size
    print(f"SHM: {shm_width}x{shm_height} ({shm_size} bytes)")

    if args.render and shm_size == 0:
        print("Error: Handshake returned 0 size for SHM. Cannot render.")
        return

    # Let the game warmup if needed
    time.sleep(2)

    # Setup SHM for rendering
    shm = None
    if args.render:
        shm_name = "portal2_harness_framebuffer"
        try:
            shm = shared_memory.SharedMemory(name=shm_name)
        except FileNotFoundError:
            print(f"Failed to open shared memory '{shm_name}'.")
            return
        print("SHM mapped successfully!")

    # Shared stop signal between generator thread and response consumer
    stop_event = threading.Event()

    # Track send timestamps for per-step latency
    send_times = []
    recv_times = []

    def action_generator():
        """Yield AgentMessages until duration elapses."""
        step = 0
        start = time.perf_counter()
        while not stop_event.is_set():
            elapsed = time.perf_counter() - start
            if elapsed >= args.duration:
                stop_event.set()
                return

            action = harness_pb2.ActionRequest(
                num_ticks=args.num_ticks,
                key_forward=True,
                mouse_dx=2.0 if step % 120 < 60 else -2.0,
                key_jump=True if step % 30 == 0 else False,
            )
            send_times.append(time.perf_counter())
            yield harness_pb2.AgentMessage(
                action=action,
                request_render=args.render,
            )
            step += 1

    print(f"\n--- Benchmarking AgentLoop ---")
    print(f"  Duration:    {args.duration}s")
    print(f"  Render:      {'ON' if args.render else 'OFF'}")
    print(f"  Ticks/action: {args.num_ticks}")
    print()

    bench_start = time.perf_counter()

    try:
        responses = stub.AgentLoop(action_generator())

        for i, env_msg in enumerate(responses):
            recv_times.append(time.perf_counter())

            # Stop consuming after duration (generator may have buffered extra messages)
            if time.perf_counter() - bench_start >= args.duration:
                stop_event.set()
                responses.cancel()
                break

            if not env_msg.success:
                print(f"Error at step {i}: {env_msg.error_message}")
                stop_event.set()
                break

            if args.render:
                frame = np.ndarray(
                    (shm_height, shm_width, 3), dtype=np.uint8, buffer=shm.buf
                )
                bgr_frame = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                cv2.imshow("Portal 2 Benchmark", bgr_frame)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    stop_event.set()
                    break

            if i % 50 == 0:
                pos = env_msg.state.position
                print(f"  Step {i:>5d}  pos=({pos.x:.1f}, {pos.y:.1f}, {pos.z:.1f})")

    except grpc.RpcError as e:
        # CANCELLED is expected when we call responses.cancel()
        if e.code() != grpc.StatusCode.CANCELLED:
            print(f"RPC Error: {e.code()}: {e.details()}")

    # Compute and print stats
    total_steps = min(len(send_times), len(recv_times))
    if total_steps > 0:
        wall_start = send_times[0]
        wall_end = recv_times[-1]
        wall_duration = wall_end - wall_start

        step_latencies = [
            (recv_times[i] - send_times[i]) * 1000  # ms
            for i in range(total_steps)
        ]
        avg_latency = sum(step_latencies) / len(step_latencies)
        min_latency = min(step_latencies)
        max_latency = max(step_latencies)

        # Percentiles
        sorted_lat = sorted(step_latencies)
        p50 = sorted_lat[int(len(sorted_lat) * 0.50)]
        p95 = sorted_lat[int(min(len(sorted_lat) * 0.95, len(sorted_lat) - 1))]
        p99 = sorted_lat[int(min(len(sorted_lat) * 0.99, len(sorted_lat) - 1))]

        actions_per_sec = total_steps / wall_duration if wall_duration > 0 else 0
        ticks_per_sec = (total_steps * args.num_ticks) / wall_duration if wall_duration > 0 else 0

        print(f"\n{'=' * 45}")
        print(f"  BENCHMARK RESULTS")
        print(f"{'=' * 45}")
        print(f"  Wall duration:       {wall_duration:.2f}s")
        print(f"  Total steps:         {total_steps}")
        print(f"  Actions/sec:         {actions_per_sec:.2f}")
        print(f"  Game ticks/sec:      {ticks_per_sec:.2f}")
        print(f"  Avg step latency:    {avg_latency:.2f}ms")
        print(f"  Min step latency:    {min_latency:.2f}ms")
        print(f"  Max step latency:    {max_latency:.2f}ms")
        print(f"  P50 latency:         {p50:.2f}ms")
        print(f"  P95 latency:         {p95:.2f}ms")
        print(f"  P99 latency:         {p99:.2f}ms")
        print(f"  Render:              {'ON' if args.render else 'OFF'}")
        print(f"  Ticks/action:        {args.num_ticks}")
        print(f"{'=' * 45}")
    else:
        print("No steps completed.")

    # Cleanup
    if args.render:
        if shm:
            shm.close()
        cv2.destroyAllWindows()

    print("Done.")


if __name__ == "__main__":
    main()
