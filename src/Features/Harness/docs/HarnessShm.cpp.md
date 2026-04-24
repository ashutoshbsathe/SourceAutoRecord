# HarnessShm.cpp

This file implements the shared memory initialization and cleanup defined in [HarnessShm.hpp](HarnessShm.hpp.md).

**Link to Source:** `//docs/HarnessShm.cpp:Init>`

## Implementation Details

- **Memory Mapping**: The `Init` function utilizes `shm_open`, `ftruncate`, and `mmap` to allocate a file-backed shared memory region.
- **Error Handling**: Checks against `-1` and `MAP_FAILED` are strictly implemented to prevent the game engine from crashing upon failed memory allocation, outputting to `console->Print()` when `errno` triggers.
- **Cleanup Routine**: Ensures that `munmap` and `close` are only called if the region was successfully initialized. The `shm_unlink` call deletes the named memory block from the OS `/dev/shm` space.

### External Functions Used
- `shm_open`, `ftruncate`, `mmap`, `munmap`, `close`, `shm_unlink`
- `console->Print(fmt, ...)`

---
**See Also:**
- [HarnessShm.hpp documentation](HarnessShm.hpp.md)