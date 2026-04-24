# harness.proto

This file defines the Protobuf messages and gRPC services that form the communication layer between the external agent and the Portal 2 engine.

**Link to Source:** `//docs/harness.proto:Portal2Harness>`

## Service: Portal2Harness

The core gRPC service exposing control over the game.

### RPCs
- **`InitialHandshake(HandshakeRequest) -> HandshakeResponse`**: Verifies connection, exchanges version info, and provides the client with the dimensions and size of the POSIX shared memory buffer initialized by the engine.
- **`Observe(Empty) -> GameState`**: Returns the current observation of the world (player position, velocity, view angles, health, etc.).
- **`Act(ActionRequest) -> ActionResponse`**: Sends input states (keyboard/mouse) to the engine to be held for `num_ticks`. Blocks until the ticks have elapsed.
- **`ExecuteCommand(CommandRequest) -> CommandResponse`**: Dispatches a raw console command to the engine.
- **`Reset(ResetRequest) -> ResetResponse`**: Resets the current episode by optionally changing the map, handling warmup logic, and returning the first valid `GameState`.
- **`AgentLoop(stream AgentMessage) -> stream EnvironmentMessage`**: A bidirectional stream optimized for RL training. It combines `Act` and `Observe` continuously.

## Streaming Messages
- **`AgentMessage`**: Contains the `ActionRequest` and a boolean `copy_pixels_to_shm`. Requesting pixels is computationally expensive (forces `ReadScreenPixels` on the main thread) and should be requested sparsely.
- **`EnvironmentMessage`**: Returns the `GameState`, along with success/error status flags.

## Observation Messages
- **`GameState`**: Contains `Vector3` properties for `position`, `velocity`, and `camera` (pitch/yaw/roll), plus primitives for `health`, `is_crouching`, and the `server_tick`.

## Action Messages
- **`ActionRequest`**: Specifies `num_ticks` to execute, boolean states for WASD, jump, crouch, use, zoom, and portals. `mouse_dx` and `mouse_dy` are provided as analog floating point values for view changes.
- **`ActionResponse`**: Simple success boolean and error string.

## Shared Types
- **`Vector3`**: Represents 3D space vectors for engine coordinates and angles.

---
**See Also:**
- [overall.md](overall.md)
- [Portal2HarnessImpl.cpp documentation](Portal2HarnessImpl.cpp.md)