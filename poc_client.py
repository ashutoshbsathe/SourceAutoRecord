#!/usr/bin/env python3
import grpc
import time
import cv2
import numpy as np
from multiprocessing import shared_memory
import harness_pb2
import harness_pb2_grpc

def main():
    print("Connecting to Portal 2 Harness...")
    channel = grpc.insecure_channel('localhost:50051')
    stub = harness_pb2_grpc.Portal2HarnessStub(channel)

    # 1. Handshake to get SHM geometry
    print("Performing Handshake...")
    resp = stub.InitialHandshake(harness_pb2.HandshakeRequest(
        client_version="poc_client 1.0",
        client_id="poc"
    ))
    
    print(f"Connected to {resp.game_version}")
    shm_width = resp.shm_width
    shm_height = resp.shm_height
    shm_size = resp.shm_size
    print(f"SHM Configured for {shm_width}x{shm_height} ({shm_size} bytes)")

    if shm_size == 0:
        print("Error: Handshake returned 0 size for SHM. Is the backend updated?")
        return

    # Let the game warmup if needed
    time.sleep(2)

    # 2. Setup Shared Memory
    print("Mapping POSIX Shared Memory...")
    # NOTE: The name MUST match the one exported by C++ HarnessShm.
    # POSIX shm_open names often have a leading slash (e.g., "/portal2_harness_framebuffer")
    shm_name = "portal2_harness_framebuffer"
    try:
        shm = shared_memory.SharedMemory(name=shm_name)
    except FileNotFoundError:
        print(f"Failed to open shared memory '{shm_name}'. Ensure SAR is running and Handshake was successful.")
        return

    print("SHM mapped successfully!")

    # Yield actions for the bidirectional stream
    def action_generator():
        for i in range(120): # Run for 120 steps
            action = harness_pb2.ActionRequest(
                num_ticks=1,
                mouse_dx=2.0 if i < 60 else -2.0, # sweep left and right
                key_forward=True if i % 10 < 5 else False,
                key_jump=True if i % 30 == 0 else False
            )
            # We explicitly request render
            yield harness_pb2.AgentMessage(action=action, request_render=True)
            time.sleep(1/60.0) # Rate limit the inputs for realistic POV replay

    try:
        responses = stub.AgentLoop(action_generator())

        for i, env_msg in enumerate(responses):
            if not env_msg.success:
                print(f"Error from server: {env_msg.error_message}")
                break
            
            # Read from SHM directly (IMAGE_FORMAT_RGB888)
            frame = np.ndarray((shm_height, shm_width, 3), dtype=np.uint8, buffer=shm.buf)
            
            # OpenCV expects BGR format
            bgr_frame = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)

            cv2.imshow("Portal 2 POC", bgr_frame)
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

            if i % 10 == 0:
                pos = env_msg.state.position
                print(f"Processed frame {i}, Pos Z: ({pos.x:.2f}, {pos.y:.2f}, {pos.z:.2f})")

    except grpc.RpcError as e:
        print(f"RPC Error: {e.code()}: {e.details()}")

    print("Closing SHM...")
    shm.close()
    cv2.destroyAllWindows()
    print("Done")

if __name__ == "__main__":
    main()
