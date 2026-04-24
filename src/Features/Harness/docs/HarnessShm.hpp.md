# HarnessShm.hpp / HarnessShm.cpp

These files implement a lightweight C++ wrapper over POSIX shared memory, providing a high-performance channel for transferring rendered framebuffers from the Portal 2 engine to external agents.

**Link to Source:** 
- `//docs/HarnessShm.hpp:HarnessShm>`
- `//docs/HarnessShm.cpp:Init>`

## Class: HarnessShm

This class handles the creation, sizing, and cleanup of the shared memory file descriptor and memory map.

### Key Functions

- **`Init(const std::string& name, size_t size)`**: 
  - Calls `shm_open` with `O_CREAT | O_RDWR` to open or create a shared memory object under the `/dev/shm/` namespace (e.g., `/portal2_harness_framebuffer`).
  - Calls `ftruncate` to size the memory block accurately (usually `854 * 480 * 3` bytes).
  - Uses `mmap` to map the memory into the Portal 2 process address space, storing the pointer in `mapped_ptr_`.
- **`Cleanup()`**: 
  - Unmaps the memory (`munmap`), closes the file descriptor, and calls `shm_unlink` to release the memory back to the OS.

### Synchronization Strategy
As explicitly noted in `HarnessShm.cpp`:
```cpp
// SYNC: We are relying on the gRPC HTTP/2 stream as the synchronization barrier.
```
There are no `pthread_mutex` or other SHM locks utilized. The C++ engine writes pixels to `mapped_ptr_` on the main thread. Only *after* the write is complete does the gRPC server send the `EnvironmentMessage` back to the Python client. The Python client waits for this message before reading from the SHM block, ensuring safety without complex IPC mutexes.

### External Functions Used
- POSIX primitives: `shm_open`, `ftruncate`, `mmap`, `munmap`, `close`, `shm_unlink`.
- `console->Print()`: For SAR logging.