# Harness.hpp / Harness.cpp

These files implement the `Harness` class, which registers as a SourceAutoRecord (SAR) `Feature`. It is responsible for bridging the main-thread event loops of the game with the asynchronous gRPC threads.

**Link to Source:** 
- `//docs/Harness.hpp:Harness>`
- `//docs/Harness.cpp:StartServer>`

## Class: Harness

The `Harness` class manages the lifecycle of the gRPC server and provides the required state variables and concurrency primitives (`std::mutex`, `std::condition_variable`) to synchronize the game engine's tick loop with gRPC requests.

### Core Responsibilities
- **Server Lifecycle:** Provides `StartServer` and `StopServer` to manage the background gRPC `std::thread`.
- **Concurrency primitives:**
  - `tickMutex` / `tickCV`: Blocks the gRPC `Act` call until the game engine has advanced the requested number of ticks.
  - `resetMutex` / `resetCV`: Blocks the gRPC `Reset` call until the game engine has finished loading and completed its warmup phase.
- **State tracking:** `harnessControlActive` tracks whether the harness currently has authority to pause the engine. `warmupTicksRemaining` ensures `TasPlayer` initializes safely.

### Key Functions (Harness.cpp)

- **`StartServer()`**: Spawns a background thread that starts a `grpc::Server` listening on `0.0.0.0:50051`.
- **`StopServer()`**: Cleanly shuts down the server, notifying all condition variables to prevent deadlocks in any pending RPCs, and detaches the thread.
- **`BuildHarnessPlaybackInfo()`**: (Local helper) Constructs a fake TAS script containing a default framebulk at tick 0 and a sentinel framebulk at `INT_MAX/2`.
- **`ActivateHarnessTasPlayer()`**: (Local helper) Passes the built playback info into `tasPlayer->Activate()`.

### Event Handlers (SAR Hooks)
- **`SESSION_START`**: Activated when a map loads. It triggers `ActivateHarnessTasPlayer()` and resets `warmupTicksRemaining` to `HARNESS_WARMUP_TICKS` (256).
- **`PRE_TICK`**: Executes every engine tick.
  - If in warmup (`warmupTicksRemaining > 0`), it decrements. At zero, it issues `engine->SetAdvancing(true)` to pause the game, activates `harnessControlActive`, and notifies `resetCV`.
  - If active, it counts down `ticksRemaining` (set by `Act`), notifying `tickCV` when the agent's requested action duration is met.

### External Functions Used (SAR / Engine)
- `console->Print()` / `console->Warning()`: Logging.
- `tasPlayer->Activate()`: Reusing TAS to initialize our sentinel playback script.
- `tasPlayer->IsActive()` / `tasPlayer->Stop()`: Managing TAS state.
- `engine->SetAdvancing()`: Crucial function used to pause and unpause the game simulation, giving the gRPC server control over tick progression.
- `session->isRunning`: Checking valid session state.
- `Scheduler::OnMainThread()`: Used occasionally in shutdown sequences to safely access TAS on the main thread.