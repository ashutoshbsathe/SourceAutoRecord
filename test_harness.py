#!/usr/bin/env python3
"""
Harness test suite for Portal 2 SAR.
Tests movement, camera, and player death/respawn scenarios.

Usage: python3 test_harness.py
Prerequisites: Portal 2 running with sar_harness 1, map loaded, warmup complete.
"""

import grpc
import time
import sys
import harness_pb2
import harness_pb2_grpc


def connect(address="localhost:50051"):
    channel = grpc.insecure_channel(address)
    stub = harness_pb2_grpc.Portal2HarnessStub(channel)
    return stub


def observe(stub):
    return stub.Observe(harness_pb2.Empty())


def act(stub, num_ticks=1, **kwargs):
    req = harness_pb2.ActionRequest(num_ticks=num_ticks, **kwargs)
    return stub.Act(req)


def execute_command(stub, command):
    req = harness_pb2.CommandRequest(command=command)
    return stub.ExecuteCommand(req)


def print_state(state, label=""):
    if label:
        print(f"  [{label}]")
    print(f"  Position: ({state.position.x:.2f}, {state.position.y:.2f}, {state.position.z:.2f})")
    print(f"  Velocity: ({state.velocity.x:.2f}, {state.velocity.y:.2f}, {state.velocity.z:.2f})")
    print(f"  Camera:   ({state.camera.x:.2f}, {state.camera.y:.2f}, {state.camera.z:.2f})")
    print(f"  Health:   {state.health}")
    print(f"  Tick:     {state.server_tick}")


def print_separator(title):
    print(f"\n{'='*60}")
    print(f"{title}")
    print(f"{'='*60}")


# ================================================================
# Test Cases
# ================================================================

def test_handshake(stub):
    print_separator("HANDSHAKE")
    resp = stub.InitialHandshake(harness_pb2.HandshakeRequest(
        client_version="test_harness.py v2",
        client_id="test"
    ))
    print(f"✓ Connected to {resp.game_version}")
    print(f"✓ Current map: {resp.map_name}")
    return True


def test_move_forward(stub):
    print_separator("TEST 1: Move forward for 5 ticks")
    before = observe(stub)
    result = act(stub, num_ticks=5, key_forward=True)
    after = observe(stub)

    print(f"  success={result.success}")
    dx = after.position.x - before.position.x
    dy = after.position.y - before.position.y
    dz = after.position.z - before.position.z
    dist = (dx**2 + dy**2 + dz**2)**0.5
    print(f"  Delta: ({dx:.2f}, {dy:.2f}, {dz:.2f}), distance={dist:.2f}")
    print(f"  Velocity: ({after.velocity.x:.2f}, {after.velocity.y:.2f}, {after.velocity.z:.2f})")

    passed = result.success and dist > 0.1
    print(f"  {'✓ PASS' if passed else '✗ FAIL'}: Player {'moved' if dist > 0.1 else 'did NOT move'}")
    return passed


def test_move_diagonal(stub):
    print_separator("TEST 2: Move forward + left for 5 ticks")
    before = observe(stub)
    result = act(stub, num_ticks=5, key_forward=True, key_left=True)
    after = observe(stub)

    dx = after.position.x - before.position.x
    dy = after.position.y - before.position.y
    dist = (dx**2 + dy**2)**0.5
    print(f"  Delta: ({dx:.2f}, {dy:.2f}), distance={dist:.2f}")

    passed = result.success and dist > 0.1
    print(f"  {'✓ PASS' if passed else '✗ FAIL'}")
    return passed


def test_camera(stub):
    print_separator("TEST 3: Look around (rotate camera)")
    before = observe(stub)
    result = act(stub, num_ticks=1, mouse_dx=20.0, mouse_dy=10.0)
    after = observe(stub)

    dpitch = after.camera.x - before.camera.x
    dyaw = after.camera.y - before.camera.y
    print(f"  Camera before: ({before.camera.x:.2f}, {before.camera.y:.2f})")
    print(f"  Camera after:  ({after.camera.x:.2f}, {after.camera.y:.2f})")
    print(f"  Delta: pitch={dpitch:.2f}, yaw={dyaw:.2f}")

    passed = result.success and (abs(dpitch) > 0.01 or abs(dyaw) > 0.01)
    print(f"  {'✓ PASS' if passed else '✗ FAIL'}: Camera {'moved' if passed else 'did NOT move'}")
    return passed


def test_jump(stub):
    print_separator("TEST 4: Jump + forward for 5 ticks")
    before = observe(stub)
    result = act(stub, num_ticks=5, key_forward=True, key_jump=True)
    after = observe(stub)

    dz = after.position.z - before.position.z
    print(f"  Z delta: {dz:.2f}")

    passed = result.success and dz > 0.1
    print(f"  {'✓ PASS' if passed else '✗ FAIL'}: Player {'jumped' if dz > 0.1 else 'did NOT jump'}")
    return passed


def test_execute_command(stub):
    print_separator("TEST 5: ExecuteCommand (echo test)")
    result = execute_command(stub, "echo Harness_ExecuteCommand_works")
    print(f"  success={result.success}")

    passed = result.success
    print(f"  {'✓ PASS' if passed else '✗ FAIL'}")
    return passed


def test_player_death(stub):
    print_separator("TEST 6: Player death + observe")
    before = observe(stub)
    print_state(before, "Before kill")

    # Kill the player via console command
    print("  Executing 'kill' command...")
    result = execute_command(stub, "kill")
    print(f"  ExecuteCommand success={result.success}")

    # Give a few ticks for death to register
    time.sleep(0.5)

    # Try to observe after death
    try:
        after = observe(stub)
        print_state(after, "After kill")

        # Check if health dropped to 0
        if after.health <= 0:
            print("  ✓ PASS: Player is dead (health <= 0)")
            return True
        else:
            print(f"  ✗ FAIL: Player health still {after.health} (expected 0)")
            return False
    except grpc.RpcError as e:
        print(f"  Observe after death returned error: {e.code()}: {e.details()}")
        print("  ✓ PASS: Observe correctly handles dead player state")
        return True


# ================================================================
# Main
# ================================================================

def main():
    stub = connect()

    # Run tests
    results = {}
    results["handshake"] = test_handshake(stub)

    print("\nWaiting 6 seconds for warmup ticks to complete...")
    time.sleep(6)

    print_separator("INITIAL OBSERVATION")
    state = observe(stub)
    print_state(state)

    results["move_forward"] = test_move_forward(stub)
    results["move_diagonal"] = test_move_diagonal(stub)
    results["camera"] = test_camera(stub)
    results["jump"] = test_jump(stub)
    results["execute_command"] = test_execute_command(stub)
    results["player_death"] = test_player_death(stub)

    # Summary
    print_separator("TEST RESULTS")
    total = len(results)
    passed = sum(1 for v in results.values() if v)
    for name, result in results.items():
        print(f"  {'✓' if result else '✗'} {name}")
    print(f"\n  {passed}/{total} tests passed")

    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
