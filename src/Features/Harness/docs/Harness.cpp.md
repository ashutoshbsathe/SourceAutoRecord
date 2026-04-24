# Harness.cpp

This file implements the core logic defined in [Harness.hpp](Harness.hpp.md).

**Link to Source:** `//docs/Harness.cpp:StartServer>`

## Implementation Details

- **Event Handlers (`SESSION_START`, `PRE_TICK`)**: These handlers bridge SAR's synchronous, single-threaded engine loop with the asynchronous nature of the gRPC server. The `PRE_TICK` hook is responsible for managing the warmup ticks and waking up the condition variables (`tickCV`, `resetCV`) when `ticksRemaining` reaches zero.
- **TAS Injection**: Uses `BuildHarnessPlaybackInfo()` to construct a script that uses raw analog inputs (`forceRawPlayback = true`). This bypasses any of SAR's higher-level TAS tool processing, feeding direct inputs to the engine.
- **Async Thread Management**: `StartServer()` spins up a background thread. `StopServer()` handles safe teardown by terminating the `grpc::Server` before joining/detaching the thread.

### External Functions Used
- `engine->SetAdvancing(bool)`: Pauses or resumes the simulation.
- `tasPlayer->Activate(TasPlaybackInfo)`: Begins playing the fake TAS script.
- `tasPlayer->Stop(bool)`: Halts TAS playback.
- `Scheduler::OnMainThread(lambda)`: Schedules execution on the engine's main thread to prevent thread safety issues when interacting with the game memory.

---
**See Also:**
- [Harness.hpp documentation](Harness.hpp.md)