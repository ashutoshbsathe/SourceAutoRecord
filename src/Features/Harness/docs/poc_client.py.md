# poc_client.py

This Python script is the reference client demonstrating how to connect to the Harness gRPC server, utilize the `AgentLoop` stream, and read framebuffer pixels from the POSIX shared memory.

**Link to Source:** `# docs/poc_client.py:run_episode>`

## Architecture & Flow

### 1. Connection & Handshake
The script establishes an insecure gRPC channel to `localhost:50051`. It calls `InitialHandshake` to retrieve the engine version and, critically, the shared memory dimensions (`shm_width`, `shm_height`, `shm_size`).

### 2. Shared Memory Mapping
If `--render` is enabled, the script uses the Python standard library's `multiprocessing.shared_memory` to map the `/portal2_harness_framebuffer` block into the Python process's address space.

### 3. The AgentLoop (Streaming)
The `run_episode()` function demonstrates bidirectional streaming:
- **Action Generator:** It yields `AgentMessage` objects containing `ActionRequest`s (e.g., holding forward, jumping intermittently).
- **Environment Processing:** It iterates over the incoming stream of `EnvironmentMessage`s.
- **Rendering:** It wraps the `shm.buf` in a NumPy array (`np.ndarray`) and renders it via OpenCV (`cv2.imshow()`). Because synchronization is implicit (gRPC acts as the barrier), it safely reads the memory block.

### 4. Episode Resets
The client logic handles episode boundaries.
- **Death Detection:** The client monitors `env_msg.state.health <= 0`.
- **Random Truncation:** Demonstrates RL-style random resets via `--reset-prob`.
- **Resetting:** When a boundary is hit, the stream is cancelled (`responses.cancel()`), and `stub.Reset` is called to seamlessly switch maps and begin a new episode without dropping the connection.

### External Python Libraries Used
- `grpc` / `grpc_tools`: Core communication layer.
- `multiprocessing.shared_memory`: POSIX IPC access.
- `numpy` / `cv2` (OpenCV): Pixel array manipulation and display.

---
**See Also:**
- [overall.md](overall.md)