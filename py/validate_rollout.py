import struct
import sys
import os
from p2harness.harness_pb2 import RolloutHeader, RolloutStep

def read_delimited_proto(file_path):
    if not os.path.exists(file_path):
        print(f"Error: File {file_path} not found.")
        return

    with open(file_path, "rb") as f:
        # Read Header first
        size_data = f.read(4)
        if not size_data:
            print("Error: Empty file.")
            return
        
        size = struct.unpack("<I", size_data)[0]
        header = RolloutHeader()
        header.ParseFromString(f.read(size))
        
        print("=== Rollout Header ===")
        print(f"Map:      {header.map_name}")
        print(f"SHM Res:  {header.shm_width}x{header.shm_height}")
        print(f"Tickrate: {header.tickrate:.2f}")
        print("======================\n")

        count = 0
        while True:
            size_data = f.read(4)
            if not size_data:
                break
            
            size = struct.unpack("<I", size_data)[0]
            step = RolloutStep()
            step.ParseFromString(f.read(size))
            
            if count < 10:
                print(f"Step {count:03d} | Tick: {step.state.server_tick:6d} | "
                      f"Pos: ({step.state.position.x:8.2f}, {step.state.position.y:8.2f}, {step.state.position.z:8.2f}) | "
                      f"Buttons: 0x{step.action.key_forward:d}{step.action.key_backward:d}{step.action.key_left:d}{step.action.key_right:d}...")
            
            if step.image_data and count == 0:
                print(f"\n[!] Found image data in first step ({len(step.image_data)} bytes). Saving to first_frame.raw...")
                with open("first_frame.raw", "wb") as img_file:
                    img_file.write(step.image_data)
                print("Tip: Use 'ffplay -f rawvideo -pixel_format rgb24 -video_size 854x480 first_frame.raw' to view.\n")

            count += 1
        
        print(f"\nFinished. Total steps: {count}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python validate_rollout.py <path_to_rollout>")
    else:
        # Ensure we can find the p2harness package
        sys.path.append(os.path.dirname(os.path.abspath(__file__)))
        read_delimited_proto(sys.argv[1])
