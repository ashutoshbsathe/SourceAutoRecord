#!/usr/bin/env python3

import grpc
import sys
import os
import time

# Add the generated proto path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src/Features/Harness'))

import harness_pb2
import harness_pb2_grpc

def main():
    # Connect to the harness server
    channel = grpc.insecure_channel('localhost:50051')
    stub = harness_pb2_grpc.Portal2HarnessStub(channel)
    
    try:
        # Initial handshake
        print("=" * 60)
        print("HANDSHAKE")
        print("=" * 60)
        handshake_request = harness_pb2.HandshakeRequest(
            client_version="test_v1.0",
            client_id="python_test_client"
        )
        handshake_response = stub.InitialHandshake(handshake_request)
        print(f"✓ Connected to {handshake_response.game_version}")
        print(f"✓ Current map: {handshake_response.map_name}")
        print()
        
        # Wait for warmup ticks (256 ticks at 60fps = ~4.3 seconds)
        print("Waiting 6 seconds for warmup ticks to complete...")
        time.sleep(6)
        
        # Observe initial state
        print("=" * 60)
        print("INITIAL OBSERVATION")
        print("=" * 60)
        state = stub.Observe(harness_pb2.Empty())
        init_pos = (state.position.x, state.position.y, state.position.z)
        print(f"Position: ({init_pos[0]:.2f}, {init_pos[1]:.2f}, {init_pos[2]:.2f})")
        print(f"Velocity: ({state.velocity.x:.2f}, {state.velocity.y:.2f}, {state.velocity.z:.2f})")
        print(f"Camera: ({state.camera.x:.2f}, {state.camera.y:.2f}, {state.camera.z:.2f})")
        print(f"Health: {state.health}")
        print(f"Server tick: {state.server_tick}")
        print()
        
        # Test 1: Move forward for 5 ticks (small to start)
        print("=" * 60)
        print("TEST 1: Move forward for 5 ticks")
        print("=" * 60)
        act_request = harness_pb2.ActionRequest(
            num_ticks=5,
            key_forward=True
        )
        act_response = stub.Act(act_request)
        print(f"  success={act_response.success}, error={act_response.error_message}")
        
        state = stub.Observe(harness_pb2.Empty())
        new_pos = (state.position.x, state.position.y, state.position.z)
        delta = tuple(n - o for n, o in zip(new_pos, init_pos))
        print(f"New position: ({new_pos[0]:.2f}, {new_pos[1]:.2f}, {new_pos[2]:.2f})")
        print(f"Delta:        ({delta[0]:.2f}, {delta[1]:.2f}, {delta[2]:.2f})")
        print(f"Velocity: ({state.velocity.x:.2f}, {state.velocity.y:.2f}, {state.velocity.z:.2f})")
        if abs(delta[0]) > 0.01 or abs(delta[1]) > 0.01 or abs(delta[2]) > 0.01:
            print("✓ PLAYER MOVED!")
        else:
            print("✗ Player did NOT move — something is wrong")
        print()
        
        # Test 2: Move forward + left for 5 ticks
        print("=" * 60)
        print("TEST 2: Move forward + left for 5 ticks")
        print("=" * 60)
        prev_pos = new_pos
        act_request = harness_pb2.ActionRequest(
            num_ticks=5,
            key_forward=True,
            key_left=True
        )
        act_response = stub.Act(act_request)
        print(f"  success={act_response.success}, error={act_response.error_message}")
        
        state = stub.Observe(harness_pb2.Empty())
        new_pos = (state.position.x, state.position.y, state.position.z)
        delta = tuple(n - o for n, o in zip(new_pos, prev_pos))
        print(f"New position: ({new_pos[0]:.2f}, {new_pos[1]:.2f}, {new_pos[2]:.2f})")
        print(f"Delta:        ({delta[0]:.2f}, {delta[1]:.2f}, {delta[2]:.2f})")
        print()
        
        # Test 3: Look around (mouse movement)
        print("=" * 60)
        print("TEST 3: Look around (rotate camera)")
        print("=" * 60)
        prev_cam = (state.camera.x, state.camera.y, state.camera.z)
        act_request = harness_pb2.ActionRequest(
            num_ticks=3,
            mouse_dx=10.0,
            mouse_dy=5.0
        )
        act_response = stub.Act(act_request)
        print(f"  success={act_response.success}, error={act_response.error_message}")
        
        state = stub.Observe(harness_pb2.Empty())
        new_cam = (state.camera.x, state.camera.y, state.camera.z)
        cam_delta = tuple(n - o for n, o in zip(new_cam, prev_cam))
        print(f"Camera: ({new_cam[0]:.2f}, {new_cam[1]:.2f}, {new_cam[2]:.2f})")
        print(f"Camera delta: ({cam_delta[0]:.2f}, {cam_delta[1]:.2f}, {cam_delta[2]:.2f})")
        if abs(cam_delta[0]) > 0.01 or abs(cam_delta[1]) > 0.01:
            print("✓ CAMERA MOVED!")
        else:
            print("✗ Camera did NOT move")
        print()
        
        # Test 4: Jump
        print("=" * 60)
        print("TEST 4: Jump + forward for 5 ticks")
        print("=" * 60)
        prev_pos = (state.position.x, state.position.y, state.position.z)
        act_request = harness_pb2.ActionRequest(
            num_ticks=5,
            key_forward=True,
            key_jump=True
        )
        act_response = stub.Act(act_request)
        print(f"  success={act_response.success}, error={act_response.error_message}")
        
        state = stub.Observe(harness_pb2.Empty())
        new_pos = (state.position.x, state.position.y, state.position.z)
        delta = tuple(n - o for n, o in zip(new_pos, prev_pos))
        print(f"New position: ({new_pos[0]:.2f}, {new_pos[1]:.2f}, {new_pos[2]:.2f})")
        print(f"Delta:        ({delta[0]:.2f}, {delta[1]:.2f}, {delta[2]:.2f})")
        print(f"Server tick: {state.server_tick}")
        print()
        
        print("=" * 60)
        print("ALL TESTS COMPLETED")
        print("=" * 60)
        
    except grpc.RpcError as e:
        print(f"RPC Error: {e.code()}: {e.details()}")
        return
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return

if __name__ == '__main__':
    main()
