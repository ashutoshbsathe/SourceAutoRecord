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
        
        # Wait a bit for warmup ticks (256 ticks at 60fps = ~4.3 seconds)
        print("Waiting 5 seconds for warmup ticks to complete...")
        time.sleep(5)
        
        # Observe initial state
        print("=" * 60)
        print("INITIAL OBSERVATION")
        print("=" * 60)
        state = stub.Observe(harness_pb2.Empty())
        print(f"Position: ({state.position.x:.2f}, {state.position.y:.2f}, {state.position.z:.2f})")
        print(f"Velocity: ({state.velocity.x:.2f}, {state.velocity.y:.2f}, {state.velocity.z:.2f})")
        print(f"Camera: ({state.camera.x:.2f}, {state.camera.y:.2f}, {state.camera.z:.2f})")
        print(f"Health: {state.health}")
        print(f"Crouching: {state.is_crouching}")
        print(f"Server tick: {state.server_tick}")
        print()
        
        # Test 1: Move forward for 10 ticks
        print("=" * 60)
        print("TEST 1: Move forward for 10 ticks")
        print("=" * 60)
        act_request = harness_pb2.ActionRequest(
            num_ticks=10,
            key_forward=True
        )
        act_response = stub.Act(act_request)
        if act_response.success:
            print("✓ Act successful")
        else:
            print(f"✗ Act failed: {act_response.error_message}")
        
        # Observe after movement
        state = stub.Observe(harness_pb2.Empty())
        print(f"New position: ({state.position.x:.2f}, {state.position.y:.2f}, {state.position.z:.2f})")
        print(f"Velocity: ({state.velocity.x:.2f}, {state.velocity.y:.2f}, {state.velocity.z:.2f})")
        print()
        
        # Test 2: Jump and move right for 20 ticks
        print("=" * 60)
        print("TEST 2: Jump + move right for 20 ticks")
        print("=" * 60)
        act_request = harness_pb2.ActionRequest(
            num_ticks=20,
            key_jump=True,
            key_right=True
        )
        act_response = stub.Act(act_request)
        if act_response.success:
            print("✓ Act successful")
        else:
            print(f"✗ Act failed: {act_response.error_message}")
        
        state = stub.Observe(harness_pb2.Empty())
        print(f"New position: ({state.position.x:.2f}, {state.position.y:.2f}, {state.position.z:.2f})")
        print(f"Velocity: ({state.velocity.x:.2f}, {state.velocity.y:.2f}, {state.velocity.z:.2f})")
        print()
        
        # Test 3: Look around (mouse movement)
        print("=" * 60)
        print("TEST 3: Look around (rotate camera)")
        print("=" * 60)
        act_request = harness_pb2.ActionRequest(
            num_ticks=5,
            mouse_dx=10.0,
            mouse_dy=5.0
        )
        act_response = stub.Act(act_request)
        if act_response.success:
            print("✓ Act successful")
        else:
            print(f"✗ Act failed: {act_response.error_message}")
        
        state = stub.Observe(harness_pb2.Empty())
        print(f"Camera: ({state.camera.x:.2f}, {state.camera.y:.2f}, {state.camera.z:.2f})")
        print()
        
        # Test 4: Crouch for 15 ticks
        print("=" * 60)
        print("TEST 4: Crouch for 15 ticks")
        print("=" * 60)
        act_request = harness_pb2.ActionRequest(
            num_ticks=15,
            key_crouch=True
        )
        act_response = stub.Act(act_request)
        if act_response.success:
            print("✓ Act successful")
        else:
            print(f"✗ Act failed: {act_response.error_message}")
        
        state = stub.Observe(harness_pb2.Empty())
        print(f"Crouching: {state.is_crouching}")
        print(f"Position: ({state.position.x:.2f}, {state.position.y:.2f}, {state.position.z:.2f})")
        print()
        
        # Test 5: Long run forward (100 ticks)
        print("=" * 60)
        print("TEST 5: Long forward movement (100 ticks)")
        print("=" * 60)
        act_request = harness_pb2.ActionRequest(
            num_ticks=100,
            key_forward=True
        )
        act_response = stub.Act(act_request)
        if act_response.success:
            print("✓ Act successful")
        else:
            print(f"✗ Act failed: {act_response.error_message}")
        
        state = stub.Observe(harness_pb2.Empty())
        print(f"Final position: ({state.position.x:.2f}, {state.position.y:.2f}, {state.position.z:.2f})")
        print(f"Health: {state.health}")
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
