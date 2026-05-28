# Brainstorming: Fixing Phase 4 Slowness and Crashes

This document outlines the performance optimizations (Data-Oriented Design) and thread-safety fixes implemented in Phase 4 of the `.hdem` Sidecar Recorder.

---

## 1. Multi-Map Auto-Recording Fix
### Problem:
The `.hdem` recorder was detoured only in `CDemoRecorder::StartRecording`. On transition to subsequent maps during `sar_autorecord`, the engine does not call `StartRecording` again; it internally continues recording. Thus, the sidecar `.hdem` recorder was stopped at map end but never restarted on the next map.

### Solution:
Hook `CDemoRecorder::SetSignonState` for `state == SIGNONSTATE_FULL`. When a new map is loaded and the engine is actively recording a demo, check if the `.hdem` recorder is active. If not, start it using `engine->demorecorder->currentDemo` (which contains the correct numbered filename for the current segment, e.g. `basename_2.hdem`).

---

## 2. Resolving the 0.5 FPS Slowness
To achieve high-performance tick updates, we transition the snapshotter and recorder away from hash maps and dynamic allocations toward a Data-Oriented Design (DOD).

### A. Flat Contiguous Payload Memory
- **Old Way**: `TrackedEntity::fieldValues` was a `std::unordered_map<uint16_t, std::vector<uint8_t>>`. For 600 entities with 30 fields, this resulted in 54,000 map queries/allocations per tick.
- **New Way**: Calculate the exact byte offset (`payloadOffset`) of each field in the serialized buffer during schema registration. Store all dynamic fields inside a single flat `std::vector<uint8_t> fieldBytes` per entity.
- **Tick Update**: Copy memory directly using `std::memcpy` at the pre-calculated `payloadOffset`.
- **Serialisation**: Write `fieldBytes` directly to disk/protobuf without rebuilding arrays.

### B. Static Entity Metadata Caching
- **Old Way**: On every tick for all 2048 entity slots, query the engine for classname (`GetEntityClassName`), targetname (`GetEntityName`), check filters (split/slice string), and do map lookups.
- **New Way**: Cache static metadata (class ID, class name, target name, and collision offsets) by entity index. When iterating, check if the entity's `serialNumber` matches the cache. If it does, reuse all properties instantly, reducing string/filter overhead to zero.

---

## 3. Resolving the Data Race Crashes
- **Old Way**: The main thread modifies `classes` and `allFields` vectors while the gRPC thread reads references to them, causing segmentation faults on reallocation. Copying them on every tick under mutex control resolves the crash but tanks performance.
- **New Way (RCU Pattern)**:
  Store the entire schema (classes, fields, maps) in a single immutable structure wrapped in a `std::shared_ptr<const HdemSchema>`. 
  - **Reads**: gRPC and recorder threads retrieve the `shared_ptr` atomically. This is extremely fast (just a refcount increment) and locks are bypassed during vector iteration.
  - **Writes (Rare)**: When registering a new class schema (usually only during the first few ticks of map load), we create a new schema copy, append to it, and atomically swap the `schema` pointer.

### Level Transition Cache Clearing (Stale Cache Crash Fix)
- **Problem**: On level/map transitions, the schema's classes and fields are preserved, but the mapping of entity indices to entity objects changes. Because serial numbers reset and count upwards from 1 on every map start, slot indices and serial numbers collide. If the entity cache is not cleared, a slot on the new map will hit the cache, think it is the old map's class, retrieve a stale/garbage `collisionOffset`, dereference it, and crash on a virtual function call.
- **Solution**: Clear `entityCache` completely inside `ClearCache()` on every `SESSION_START` level load, forcing a clean discovery of slot-to-class mappings on map change.

---

## 4. Elimination of Hot Path Heap Allocations (SIGABRT / Memory Exhaustion Fix)
### Problem:
Although the initial Phase 4 changes implemented flat byte vectors (`tracked.fieldBytes`) and static metadata caching, `TrackedEntity` and various caches still utilized dynamic standard containers (`std::string`, `std::vector`, `std::unordered_map`). 
Iterating over ~600 entities at 100 ticks per second resulted in hundreds of thousands of heap allocations and deallocations per second. Since Portal 2 is a 32-bit process (`portal2_linux`), virtual address space is capped at ~3-4GB. The extreme allocation frequency led to massive heap fragmentation and eventual out-of-memory (`std::bad_alloc`), triggering uncaught exception thread crashes resulting in a `SIGABRT` (Signal 6) dump after a relatively fixed duration.

### Solution:
To achieve a completely allocation-free execution model in the hot loop, we transition to a strictly Plain Old Data (POD) and flat array architecture:
1. **POD TrackedEntity**: Redefined `TrackedEntity` to be a pure POD struct. Dynamic strings and vectors are replaced with fixed-size arrays (`fieldBytes[1024]` and `targetName[128]`). The entity class name is fetched directly from the immutable RCU schema using the `classId` instead of storing/copying a dynamic classname string in every entity snapshot.
2. **Intelligent Payload Truncation**: Enforce a `MAX_PAYLOAD_SIZE = 1024` byte limit per class. During schema registration, if the sum of field sizes exceeds 1024 bytes, we gracefully truncate the registered class fields and emit a console warning. This bounds all copy operations and guarantees memory safety without buffer overflows.
3. **Flat 2048-Element Cache & State Arrays**: 
   - Transition `entityCache` from a `std::unordered_map` to a flat array `EntityCacheEntry entityCache[2048]`, clearing it via `isValid = false`.
   - Transition `HdemRecorder::lastEntityState` and `lastEntitySerial` to flat static arrays (`SavedState lastEntityState[2048]` and `int lastEntitySerial[2048]`).
   - Transition gRPC stream delta tracking `sessionLastState` to `std::vector<LastSentState> sessionLastState(2048)`.
4. **Reused Buffer Vectors**: Pre-allocate and reuse vectors (like `currentEntities` and `snapshotBuffer`) across ticks, ensuring that once the capacity is reached, memory allocation frequency in the hot path drops to **absolute zero**.

---

## 5. Portal 2 Max Entities Cache Size Fix (OOB Buffer Overflow Crash)
### Problem:
In the initial allocation-free refactoring, the entity snapshot caches, delta-tracking arrays, and session states were hardcoded to a size of `2048` entries based on an assumption about maximum entity capacity. However, in Portal 2, the server entity list (`NUM_ENT_ENTRIES`) actually scales up to `8192` entries (`0x2000`). Consequently, any level load or physics update loop that iterated beyond slot 2047 wrote properties (like `cache.isValid = true` or delta states) way past the array bounds, immediately causing heap/stack corruption and an instant segfault/crash.

### Solution:
1. **Sized Caches to 8192**: Defined a compile-time constant `MAX_ENTITY_ENTRIES = 8192` and scaled the cache arrays `entityCache`, `lastEntityState`, `lastEntitySerial`, and `sessionLastState` to `8192` entries.
2. **Explicit Bounds Checks**: Added a strict check `if (i < 0 || i >= MAX_ENTITY_ENTRIES) continue;` to the entity list iteration loop in `EntitySnapshotter::Update` and `HdemRecorder::RecordTick` to ensure out-of-bounds slots can never cause memory corruption even if the game's entity counts dynamically scale.
3. **Heap-Allocated Tracking Arrays**: Moved larger per-frame arrays like `currentSerialsActive` and `currentSerials` to `HdemRecorder` class members rather than stack allocation to avoid potential stack overflow issues.

---

## 6. Map Load SIGSEGV Crash (ICollideable Vtable + Lifecycle Race)
### Problem:
After the previous fixes, the game would SIGSEGV (Signal 11) during map load. The crash occurred inside `EntitySnapshotter::Update()` called from the `POST_TICK` event handler. Root cause analysis revealed two contributing factors:

1. **ICollideable vtable deref on partially-constructed entities**: During map load, entities exist in the server entity list (`m_EntPtrArray`) before they are fully constructed. Their embedded `CCollisionProperty` sub-object (resolved via `m_Collision` offset) may have an uninitialized vtable pointer. Calling virtual methods like `GetSolid()`, `OBBMins()`, `OBBMaxs()` through a garbage vtable is an immediate, uncatchable SIGSEGV — `try {} catch (...) {}` only catches C++ exceptions, not hardware signals.

2. **`POST_TICK` → `Update()` timing during transitions**: `GameFrame` fires `POST_TICK` as soon as the new server starts ticking. This can happen *before* `SESSION_START` has cleared the entity cache and re-discovered the schema, leading to stale `classId` values indexing past `schema->classes.size()`.

### Solution:
Three surgical, defense-in-depth changes:

1. **Session lifecycle guard** (`Harness.cpp`): Added `if (!session || !session->isRunning) return;` at the top of the `POST_TICK` recording handler. Since `session->isRunning` only becomes `true` **after** `SESSION_START` completes (which runs `ClearCache()` + `DiscoverSchema()`), this completely prevents `Update()` from running with stale cache/schema state during level transitions.

2. **`classId` bounds check** (`EntitySnapshotter.cpp`): Added `if (classId >= schema->classes.size()) continue;` before accessing `schema->classes[classId]`. Defensive guard against stale cache entries that reference class IDs from a previous map's schema.

3. **ICollideable vtable validation** (`EntitySnapshotter.cpp`): Before calling any virtual methods on the resolved `ICollideable*`, read its vtable pointer (`*(uintptr_t*)candidate`) and verify it's a plausible address (`>= 0x10000`). Entities with uninitialized collision objects (vtable = 0 or garbage low address) are safely skipped, defaulting to `SOLID_NONE` / zero vectors.

4. **Schema re-discovery per map** (`EntitySnapshotter.cpp`): `ClearCache()` now resets `schemaDiscovered = false`, ensuring `DiscoverSchema()` runs a full pass on each new map rather than silently reusing stale schema from the previous level.

---

## 7. SendTable Refactoring, Real-Time Optimization, and Python Crash Fix
Following a regression cleanup back to a Phase 3 "clean slate", the simplified Phase 4 auto-discovery was re-implemented. During initial playtests, three critical issues were uncovered and resolved:

### A. Classname Mismatch (ServerClass C++ Network Names vs Map Names)
* **Problem**: Walking `server->GetAllServerClasses()` registered discovered properties under C++ network names (e.g. `CPortal_Player` or `CPhysicsProp`). However, live entities are tracked under map-level classnames (e.g. `player` or `prop_physics`). Because of this mismatch, live entities never matched the SendTable class names, causing delta updates to omit all discovered properties and only output the base universal fields.
* **Solution**: In `DiscoverSchema()`, we replaced the static `GetAllServerClasses()` iteration with an iteration over live entities. We retrieve each active entity's `ServerClass` directly using its virtual method table (`Offsets::GetServerClass`) and register the discovered properties directly under its map-level classname.

### B. Tick-by-Tick CPU Lag (getServerOffset Lookup Overhead)
* **Problem**: In the C++ `Update()` loop, the snapshotter queried `EntField::getServerOffset` for every property on every entity on every tick. Since `getServerOffset` performs a string key hash map lookup (requiring string construction, heap allocation, and hashing), querying this 32,000+ times per frame led to a massive CPU bottleneck on the game's main thread, causing severe lag.
* **Solution**: Implemented lazy field-offset caching. During `Update()`, when an entity of a given class is first encountered, all field offsets and types are resolved and cached in a lightweight vector (`resolvedClasses`). On all subsequent ticks, the snapshotter accesses the fields directly via fast pointer additions (`se + rf.offset`), reducing string/hash lookup overhead to absolute zero.

### C. Python Deserialization Crash (struct.error in hdem_dump.py)
* **Problem**: `SendPropTypeToHdem` mapped `DPT_String` properties to `HDEM_STRING`. Since strings are variable-length and `HdemFieldSize` returned `0` for `HDEM_STRING`, the C++ recorder wrote `0` bytes (omitting any null terminator) to the tick payload. When `hdem_dump.py` parsed this, it treated `HDEM_STRING` as a null-terminated string and scanned forward indefinitely, parsing subsequent field IDs/values as characters and eventually running out of payload buffer bytes, resulting in a crash.
* **Solution**: String properties (aside from the static class name and target name written on the first tick) do not change over time and are not needed for training or physics snapshotting. We skipped `DPT_String` in `SendPropTypeToHdem` to prevent them from being registered as generic properties, completely resolving the deserialization crash.

