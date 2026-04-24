# Portal2HarnessImpl.cpp

This file implements the gRPC service defined in `harness.proto`. It translates gRPC requests from the network into actionable commands and data lookups within the Portal 2 engine.

**Link to Source:** `//docs/Portal2HarnessImpl.cpp:AgentLoop>`

## Class: Portal2HarnessImpl

Inherits from `portal2_harness::Portal2Harness::Service` to fulfill the protobuf contract.

### RPC Implementations

#### `InitialHandshake`
Exchanges game versions and initializes the POSIX shared memory block via `shm.Init()`. Computes the required byte size for a typical Portal 2 resolution (e.g., 854x480x3).

#### `Observe`
Retrieves the player entity via `server->GetPlayer(1)` (casting to `ServerEnt*`). It reads position, velocity, and health directly from entity fields (`abs_origin()`, `abs_velocity()`, `field<int>("m_iHealth")`). It reads the camera pitch/yaw/roll from `engine->GetAngles(0)`.

#### `Act`
Translates `ActionRequest` keys (WASD, jump, mouse DX/DY) into a `TasFramebulk`.
- **Thread Safety:** Pushes the `TasFramebulk` update and `engine->AdvanceTick()` execution onto the main thread via `Scheduler::OnMainThread()`.
- **Synchronization:** Blocks the gRPC thread using `harness->tickCV` until the engine has advanced the requested number of ticks.

#### `ExecuteCommand`
Directly pushes a console command to `engine->ExecuteCommand(cmd, true)`. It also advances the tick by 1 to ensure the command takes effect immediately.

#### `Reset`
Restarts the map or switches to a new one. It yields harness control (`harnessControlActive = false`), unpauses the engine, and stops the `TasPlayer`. It then blocks on `resetCV` until the map loads and the warmup ticks are completed, after which it returns the first `GameState`.

#### `AgentLoop`
The high-performance streaming loop. It iteratively reads `AgentMessage`s from the stream, calls `Act()` and `Observe()` internally, and writes back `EnvironmentMessage`s. If `copy_pixels_to_shm` is requested, it calls `Offsets::ReadScreenPixels` via the game's VMT.

### External Functions Used
- `server->GetPlayer(int)`: Retrieves the `ServerEnt` pointer.
- `engine->GetAngles(int)`: Retrieves current camera vectors.
- `engine->ExecuteCommand(const char*, bool)`: Executes console commands.
- `Scheduler::OnMainThread(lambda)`: Pushes work to the game thread.
- `Memory::VMT<...>(...)`: Invokes the engine's internal pixel reading function.

---
**See Also:**
- [harness.proto documentation](harness.proto.md)