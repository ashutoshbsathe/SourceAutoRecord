# `.hdem` Implementation Plan — Phased Task List

> **Reference**: Read [hdem_implementation.md](./hdem_implementation.md) for full design rationale.
> **Codebase**: All C++ in `src/Features/Harness/`, proto in `harness.proto`, Python in `py/`.

---

## Phase 1: Infrastructure + Portal Entity Recording

**Goal**: Create `HdemRecorder` that writes a `.hdem` sidecar file containing `prop_portal` entity snapshots every tick during demo recording.

### Task 1.1: Define `.hdem` binary format structs

**New file**: `src/Features/Harness/HdemFormat.hpp`

Define C++ structs/constants for the binary format:

```cpp
// Magic: "HDEM", version, flags
constexpr uint32_t HDEM_MAGIC = 0x4D454448; // "HEDM" in LE
constexpr uint16_t HDEM_VERSION = 1;

enum HdemFieldType : uint8_t {
    HDEM_FLOAT = 0, HDEM_INT32 = 1, HDEM_VEC3 = 2,
    HDEM_BOOL = 3, HDEM_STRING = 4, HDEM_HANDLE = 5,
    HDEM_BYTE = 6, HDEM_SHORT = 7, HDEM_COLOR = 8,
};

enum HdemEntityFlags : uint8_t {
    HDEM_ENT_ALIVE = 0x01, HDEM_ENT_DORMANT = 0x02,
    HDEM_ENT_DELETED = 0x04, HDEM_ENT_FULL_SNAPSHOT = 0x08,
};

// Size lookup by type
inline size_t HdemFieldSize(HdemFieldType t) {
    switch(t) {
        case HDEM_FLOAT: case HDEM_INT32: case HDEM_HANDLE: return 4;
        case HDEM_VEC3: return 12;
        case HDEM_BOOL: case HDEM_BYTE: return 1;
        case HDEM_SHORT: return 2;
        case HDEM_COLOR: return 4;
        default: return 0; // STRING is variable
    }
}
```

### Task 1.2: Create `HdemRecorder` class

**New files**: `src/Features/Harness/HdemRecorder.hpp`, `src/Features/Harness/HdemRecorder.cpp`

**Header** — minimal interface:
```cpp
#pragma once
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include "HdemFormat.hpp"
#include "Utils/SDK.hpp"

struct HdemFieldDef {
    uint16_t fieldId;
    std::string name;
    HdemFieldType type;
};

struct HdemClassDef {
    uint16_t classId;
    std::string name;
    std::vector<HdemFieldDef> fields;
};

class HdemRecorder {
public:
    bool Start(const std::string& path, const std::string& mapName, float tickrate);
    void Stop();
    bool IsActive() const { return isActive; }
    void RecordTick(int tickNumber);

private:
    void WriteHeader(const std::string& mapName, float tickrate);
    void WriteClassTable();
    void WriteFieldTable();
    void DiscoverEntities(); // Scan entity list, build class/field tables
    void WriteEntityData(int entIndex, void* entity, const char* className);

    std::ofstream file;
    bool isActive = false;
    bool headerWritten = false;

    // Schema
    std::vector<HdemClassDef> classes;
    std::vector<HdemFieldDef> allFields;
    std::unordered_map<std::string, uint16_t> classNameToId;
    std::unordered_map<std::string, uint16_t> fieldNameToId;

    // Delta tracking: entityIndex -> last written field values (raw bytes)
    std::unordered_map<int, std::vector<uint8_t>> lastEntityState;
    // Track which entities existed last tick
    std::unordered_map<int, int> lastEntitySerial;

    // Write buffer (reused per tick to avoid allocs)
    std::vector<uint8_t> tickBuffer;

    size_t totalBytes = 0;
    size_t totalTicks = 0;
};
```

**Implementation** — Phase 1 hardcodes portal fields only:

In `DiscoverEntities()`, iterate `server->m_EntPtrArray[0..NUM_ENT_ENTRIES]`. For each alive entity, call `server->GetEntityClassName()`. Register classes and define fields per class. For Phase 1, only register:
- **All entities**: `m_vecAbsOrigin` (VEC3), `m_angAbsRotation` (VEC3), `m_vecAbsVelocity` (VEC3)
- **prop_portal**: `m_bActivated` (BOOL), `m_bIsPortal2` (BOOL), `m_hLinkedPortal` (HANDLE)

In `RecordTick()`:
1. Iterate entity list
2. For each alive entity: read fields via `SE(ent)->field<T>("fieldname")`
3. Compare against `lastEntityState[index]` — if changed, write to `tickBuffer`
4. Write tick header + entity data to file

**Key patterns to follow from existing code:**
- Entity iteration: see [`Session::Ended()`](../src/Features/Session.cpp#L148-L155) — iterates `entityList->GetEntityInfoByIndex(i)`
- Field access: see [`Portal2HarnessImpl::InternalObserve()`](../src/Features/Harness/Portal2HarnessImpl.cpp#L100-L106) — uses `SE(ent)->field<int>("m_iHealth")`
- File I/O: see [`RolloutRecorder::WriteMessage()`](../src/Features/Harness/RolloutRecorder.cpp#L70-L77) — length-prefixed binary writes

### Task 1.3: Add `sar_harness_record` cvar and hook into demo recording

**Modified file**: `src/Features/Harness/Harness.hpp`
- Add `class HdemRecorder* hdemRecorder` member to `Harness`
- Add `Variable harnessRecord` cvar member

**Modified file**: `src/Features/Harness/Harness.cpp`
- Include `HdemRecorder.hpp`
- Initialize `hdemRecorder = new HdemRecorder()` in constructor
- Add `POST_TICK` event handler:
  ```cpp
  ON_EVENT(POST_TICK) {
      if (!harness || !harness->hdemRecorder->IsActive()) return;
      if (!engine->hoststate->m_activeGame) return; // server must be running
      harness->hdemRecorder->RecordTick(server->gpGlobals->tickcount);
  }
  ```
- Add `SESSION_START` handler to call `DiscoverEntities()` + `Start()`
- Add `SESSION_END` / demo stop handler to call `Stop()`

**Modified file**: `src/Modules/EngineDemoRecorder.cpp`
- In `StartRecording` detour: if `sar_harness_record` is enabled, derive `.hdem` path from demo filename, call `harness->hdemRecorder->Start()`
- In `StopRecording` detour: if recorder is active, call `harness->hdemRecorder->Stop()`

**Existing cvars to reuse** (don't redefine):
- `sar_record_prefix` (line 282 of EngineDemoRecorder.cpp)
- `sar_record_mkdir` (line 283)
- `sar_autorecord` (Cheats.cpp line 28)

### Task 1.4: Verify with a test recording

**Steps:**
1. Build SAR with the new code
2. Launch Portal 2, load `sp_a2_laser_chaining` (has portals)
3. `sar_harness_record 1; record test_portals`
4. Play for ~5 seconds, shoot some portals, then `stop`
5. Verify `test_portals.hdem` exists alongside `test_portals.dem`
6. Write a small Python script `py/hdem_dump.py` that reads the `.hdem` and prints entity info per tick
7. Confirm portals appear with correct positions/activation states

---

## Phase 2: Extend to Cubes, Buttons, Doors

**Goal**: Add `prop_weighted_cube`, `prop_button` / `func_weight_button`, and `prop_testchamber_door` entity support.

### Task 2.1: Extend field definitions per class

**Modified file**: `src/Features/Harness/HdemRecorder.cpp`

In `DiscoverEntities()`, add field registrations for:

| Class | Extra Fields | Type |
|-------|-------------|------|
| `prop_weighted_cube` | `m_hOwnerEntity` | HANDLE |
| `prop_button` | `m_bLocked` | BOOL |
| `func_weight_button` | `m_toggle_state` | INT32 |
| `prop_testchamber_door` | `m_toggle_state` | INT32 |
| `prop_testchamber_door` | `m_bLocked` | BOOL |

All entities already get position/angles/velocity from Phase 1.

### Task 2.2: Add entity health and flags universally

Add to the "all entities" universal fields:
- `m_iHealth` (INT32)
- `m_fFlags` (INT32)
- `m_hOwnerEntity` (HANDLE)

### Task 2.3: Add classname/targetname as string fields (tick 0 only)

On the **first tick** an entity appears, write:
- `m_iClassname` via `server->GetEntityClassName(ent)` → HDEM_STRING
- `m_iName` via `server->GetEntityName(ent)` → HDEM_STRING (may be empty)

These are written as `HDEM_ENT_FULL_SNAPSHOT` flag and NOT delta-tracked (they don't change).

### Task 2.4: Test on a puzzle map

Test on `sp_a2_laser_chaining` or `sp_a4_laser_platform`:
- Verify cubes appear with positions
- Verify buttons toggle `m_toggle_state` when pressed
- Verify doors open/close
- Check `hdem_dump.py` output shows correct class names

---

## Phase 3: Collision Data + ICollideable

**Goal**: Record bounding box and solid type for entities.

### Task 3.1: Add collision fields

For each entity where `SE(ent)->collision()` is accessible:
- `m_nSolidType` → BYTE (from `ICollideable::GetSolid()`)
- `m_vecMins` → VEC3 (from `OBBMins()`)
- `m_vecMaxs` → VEC3 (from `OBBMaxs()`)

**Reference**: See [ICollideable.hpp](../src/Utils/SDK/ICollideable.hpp) for the interface.

**Important**: Wrap in try/catch or null-check. `ICollideable` access can crash if the entity doesn't support it.

### Task 3.2: Record collision once (static)

Collision bounds don't change for most entities. Record them on first appearance (`HDEM_ENT_FULL_SNAPSHOT`) and don't delta-track.

---

## Phase 4: Full Entity Dump via SendTable Traversal

**Goal**: Replace hardcoded per-class field lists with runtime SendTable discovery.

### Task 4.1: Implement SendTable field auto-discovery

**Reference**: [`ClassDumper.cpp`](../src/Features/ClassDumper.cpp) shows how to traverse `SendTable` and `RecvTable`.

In `DiscoverEntities()`:
1. For each entity, get `CServerClass` via the entity's vtable
2. Traverse `serverClass->sendTable` recursively
3. For each `SendProp`, register a field with appropriate `HdemFieldType`:
   - `DPT_Int` → `HDEM_INT32`
   - `DPT_Float` → `HDEM_FLOAT`
   - `DPT_Vector` → `HDEM_VEC3`
   - `DPT_String` → `HDEM_STRING` (record on first tick only)
   - `DPT_Array` → skip for now
4. Build the field table dynamically

### Task 4.2: Add a whitelist/blacklist filter

**New cvar**: `sar_harness_record_filter` — comma-separated list of class prefixes to include. Empty = all. This lets users limit recording to specific entity types for debugging.

### Task 4.3: Stress test on complex maps

- Test on `sp_a4_finale4` (lots of entities, complex I/O)
- Test on a workshop map with custom entities
- Profile recording overhead — target < 200 µs per tick
- Verify `.hdem` file size stays reasonable (~5-15 MB for a full playthrough)

---

## Phase 5: Python Reader + Rollout Integration

**Goal**: Read `.hdem` files in Python and integrate into `.rollout` conversion.

### Task 5.1: Python `.hdem` reader

**New file**: `py/hdem_reader.py`

A pure-Python reader that parses the binary format:
```python
class HdemReader:
    def __init__(self, path: str): ...
    def read_header(self) -> dict: ...
    def read_tick(self) -> dict[int, EntityState]: ...
    def __iter__(self): ...  # yield ticks
```

### Task 5.2: Upgrade `hdem_dump.py` to use the reader

Replace any ad-hoc parsing with the `HdemReader` class.

### Task 5.3: Add `EntitySnapshot` to `harness.proto`

**Modified file**: `src/Features/Harness/harness.proto`

```protobuf
message EntityField {
    string name = 1;
    oneof value {
        float float_val = 2;
        int32 int_val = 3;
        Vector3 vec3_val = 4;
        bool bool_val = 5;
        string string_val = 6;
        int32 handle_val = 7;
    }
}

message EntityState {
    int32 entity_index = 1;
    int32 serial_number = 2;
    string class_name = 3;
    string target_name = 4;
    Vector3 position = 5;
    Vector3 angles = 6;
    Vector3 velocity = 7;
    repeated EntityField fields = 8;
}

message EntitySnapshot {
    repeated EntityState entities = 1;
    bool is_full_snapshot = 2;
    int32 tick = 3;
}
```

Add `EntitySnapshot entity_snapshot = 7;` to `GameState`.
Add `EntitySnapshot entity_snapshot = 4;` to `RolloutStep`.

### Task 5.4: Wire `.hdem` data into rollout conversion

**Modified file**: `src/Features/Harness/Harness.cpp`

In the `POST_TICK` handler that records rollouts during demo playback (line 269-307), add:
- If an `.hdem` sidecar exists for the current demo, open an `HdemReader`
- Read the entity snapshot for the current tick
- Pass it to `RolloutRecorder::RecordTick()` alongside `GameState` and pixels

---

## Phase 6: Fast Conversion Command

**Goal**: Add `sar_harness_convert` for high-speed `.hdem` → `.rollout`.

### Task 6.1: Entity-only conversion (no pixels, no engine)

**New file**: `py/hdem_to_rollout.py`

Standalone Python script that reads `.hdem` + optionally merges with `.dem` header info to produce `.rollout` files without launching Portal 2. **100-1000x faster than real-time.**

### Task 6.2: In-engine fast conversion

**Modified file**: `src/Features/Harness/Harness.cpp`

New command `sar_harness_convert <demo> [output] [pixels:0|1]`:
- If `pixels=0`: read `.hdem` directly, write `.rollout` with entity data only. Use `mat_norendering 1; fps_max 0`.
- If `pixels=1`: play demo with `mat_norendering 0; fps_max 0` for fast pixel capture, overlay `.hdem` entity data.

Use `engine->SetSkipping(true)` (see [TasPlayer.cpp L97](../src/Features/Tas/TasPlayer.cpp#L97)) to suppress rendering during non-pixel ticks.

---

## File Summary

### New Files (in order of creation)
| File | Phase | Purpose |
|------|-------|---------|
| `src/Features/Harness/HdemFormat.hpp` | 1 | Binary format constants and enums |
| `src/Features/Harness/HdemRecorder.hpp` | 1 | Recorder class header |
| `src/Features/Harness/HdemRecorder.cpp` | 1 | Recorder implementation |
| `py/hdem_dump.py` | 1 | Debug tool: print `.hdem` contents |
| `py/hdem_reader.py` | 5 | Reusable Python `.hdem` parser |
| `py/hdem_to_rollout.py` | 6 | Standalone converter |

### Modified Files
| File | Phase | Changes |
|------|-------|---------|
| `src/Features/Harness/Harness.hpp` | 1 | Add `HdemRecorder*` member, `harnessRecord` cvar |
| `src/Features/Harness/Harness.cpp` | 1,5,6 | POST_TICK handler, SESSION_START/END hooks, convert command |
| `src/Modules/EngineDemoRecorder.cpp` | 1 | Hook Start/StopRecording for `.hdem` sidecar |
| `src/Features/Harness/harness.proto` | 5 | Add EntityState, EntitySnapshot messages |
| `src/Features/Harness/RolloutRecorder.hpp` | 5 | Accept EntitySnapshot in RecordTick |
| `src/Features/Harness/RolloutRecorder.cpp` | 5 | Serialize entity data into rollout steps |

---

## Key Patterns & Gotchas

### Entity iteration pattern (copy this exactly)
```cpp
#include "Features/EntityList.hpp"
#include "Modules/Server.hpp"
#include "Offsets.hpp"

for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    auto info = entityList->GetEntityInfoByIndex(i);
    if (!info || !info->m_pEntity) continue;
    
    auto ent = info->m_pEntity;
    const char* className = server->GetEntityClassName(ent);
    if (!className) continue;
    
    // Read fields:
    ServerEnt* se = SE(ent);
    Vector pos = se->abs_origin();
    QAngle ang = se->abs_angles();
    Vector vel = se->abs_velocity();
}
```

### Guard: only record when server is active
```cpp
if (!engine->hoststate->m_activeGame) return; // NO server during demo playback
if (!session->isRunning) return;
```

### Guard: don't conflict with rollout recording
```cpp
if (harness && harness->isRecordingRollout) return; // rollout mode, not hdem mode
```

### Demo filename derivation
The demo base name is at `engine->demorecorder->m_szDemoBaseName`. The full path is:
```cpp
std::string(engine->GetGameDirectory()) + "/" + engine->demorecorder->m_szDemoBaseName + ".hdem"
```
Handle `sar_record_prefix` by hooking AFTER the prefix is applied (in the `StartRecording` detour, the filename already includes the prefix).

### Delta encoding tip
For each entity, concatenate all field values into a `std::vector<uint8_t>`. Compare with `lastEntityState[entIndex]` using `memcmp`. If identical, skip. If different, write the changed fields and update the cache. This is O(field_bytes) per entity but avoids per-field hashing.
