# `.hdem` Implementation — Approach D Deep Dive

> **Goal**: Design and implement a **Harness Demo** (`.hdem`) format that embeds full server entity snapshots into demo recordings at every tick, enabling perfect-fidelity `.rollout` conversion on any machine.

---

## 1. Architecture Overview

```
RECORDING PC (player plays normally)                    CONVERSION PC (batch processing)
┌─────────────────────────────────────┐                ┌──────────────────────────────────┐
│ Portal 2 + SAR                      │                │ Portal 2 + SAR (headless)        │
│                                     │                │                                  │
│ sar_harness_record myrun            │                │ sar_harness_convert myrun.hdem   │
│   ├─ Engine records .dem normally   │   ──copy──►    │   ├─ Reads .hdem sidecar         │
│   └─ SAR writes .hdem sidecar      │                │   ├─ playdemo myrun.dem (fast)    │
│       (entity snapshots per tick)   │                │   ├─ Merges client pixels +       │
│                                     │                │   │  .hdem entity data             │
│                                     │                │   └─ Writes .rollout              │
└─────────────────────────────────────┘                └──────────────────────────────────┘
```

### Key Design Decision: Sidecar File, Not Embedded

**Don't embed entity data inside the `.dem` file.** Instead, write a **sidecar** `.hdem` file alongside it.

**Why sidecar over embedding via `RecordCustomData`:**

| | Embedded (custom demo data) | Sidecar (.hdem) |
|---|---|---|
| **File coupling** | Tight — data inside .dem | Loose — separate file |
| **Demo compatibility** | Risk of breaking playback on vanilla clients | Zero risk — .dem is untouched |
| **Size control** | Limited by demo packet size constraints | Unlimited — direct file I/O |
| **Seek/random access** | Must replay entire demo to reach tick N | Can seek directly in .hdem |
| **Recording overhead** | Goes through engine's demo recording pipeline | Direct `fwrite()` — minimal overhead |
| **Existing demo reuse** | Community .dem files lack data | Community .dem files just lack sidecar |

**Naming convention**: `myrun.dem` + `myrun.hdem` (same base name, different extension).

---

## 2. The `.hdem` Binary Format

### Design Goals
- **Streaming-friendly**: Can be written tick-by-tick with no backpatching
- **Seekable**: Fixed-size index enables O(1) tick lookup
- **Self-describing**: Header contains schema version, map name, entity class table
- **Compact**: Entity data is delta-encoded; class names stored once in header

### File Layout

```
┌──────────────────────────────────────────────┐
│ MAGIC: "HDEM" (4 bytes)                      │
│ VERSION: uint16 (2 bytes)                    │
│ FLAGS: uint16 (2 bytes)                      │
├──────────────────────────────────────────────┤
│ HEADER SECTION (variable length)             │
│   ├─ map_name (null-terminated string)       │
│   ├─ tickrate (float32)                      │
│   ├─ recording_timestamp (uint64, unix)      │
│   ├─ sar_version (null-terminated string)    │
│   ├─ game_dir (null-terminated string)       │
│   ├─ num_class_entries (uint16)              │
│   └─ CLASS TABLE                             │
│       ├─ class_id=0: "player" \0             │
│       ├─ class_id=1: "prop_portal" \0        │
│       ├─ class_id=2: "prop_weighted_cube" \0 │
│       └─ ... (auto-discovered at runtime)    │
├──────────────────────────────────────────────┤
│ ENTITY SCHEMA SECTION                        │
│   ├─ num_field_entries (uint16)              │
│   └─ FIELD TABLE                             │
│       ├─ field_id=0: "m_vecAbsOrigin" \0     │
│       │   type=VEC3, size=12                 │
│       ├─ field_id=1: "m_angAbsRotation" \0   │
│       │   type=VEC3, size=12                 │
│       ├─ field_id=2: "m_vecAbsVelocity" \0   │
│       │   type=VEC3, size=12                 │
│       ├─ field_id=3: "m_iHealth" \0          │
│       │   type=INT32, size=4                 │
│       └─ ... (every field we read)           │
├──────────────────────────────────────────────┤
│ TICK DATA (repeating)                        │
│                                              │
│ ┌─ TICK HEADER ────────────────────────────┐ │
│ │ tick_number: int32                       │ │
│ │ num_entities: uint16                     │ │
│ │ frame_byte_size: uint32                  │ │
│ └──────────────────────────────────────────┘ │
│ ┌─ ENTITY 0 ──────────────────────────────┐ │
│ │ entity_index: uint16 (0-2047)           │ │
│ │ serial_number: uint16                   │ │
│ │ class_id: uint16 (index into class tbl) │ │
│ │ flags: uint8 (ALIVE|DORMANT|DELETED)    │ │
│ │ num_fields: uint8                       │ │
│ │ ┌─ FIELD 0 ───────────────────────────┐ │ │
│ │ │ field_id: uint16                    │ │ │
│ │ │ value: [size bytes from field tbl]  │ │ │
│ │ └─────────────────────────────────────┘ │ │
│ │ ┌─ FIELD 1 ───────────────────────────┐ │ │
│ │ │ ...                                 │ │ │
│ │ └─────────────────────────────────────┘ │ │
│ └──────────────────────────────────────────┘ │
│ ┌─ ENTITY 1 ──────────────────────────────┐ │
│ │ ...                                      │ │
│ └──────────────────────────────────────────┘ │
│                                              │
│ ... (next tick)                              │
├──────────────────────────────────────────────┤
│ FOOTER                                       │
│   total_ticks: uint32                        │
│   total_entities_seen: uint32                │
│   file_checksum: uint32 (CRC32)              │
└──────────────────────────────────────────────┘
```

### Field Types

```cpp
enum HdemFieldType : uint8_t {
    HDEM_FLOAT   = 0,  // 4 bytes
    HDEM_INT32   = 1,  // 4 bytes
    HDEM_VEC3    = 2,  // 12 bytes (3x float)
    HDEM_BOOL    = 3,  // 1 byte
    HDEM_STRING  = 4,  // uint16 length + chars (used for targetname at tick 0)
    HDEM_HANDLE  = 5,  // 4 bytes (CBaseHandle — entity index + serial)
    HDEM_BYTE    = 6,  // 1 byte
    HDEM_SHORT   = 7,  // 2 bytes
    HDEM_COLOR   = 8,  // 4 bytes (RGBA)
};
```

### Delta Encoding Strategy

On **tick 0** (or when an entity first appears): write ALL fields → **full snapshot**.

On subsequent ticks: write ONLY fields whose value changed since last write → **delta**.

The `flags` byte on each entity entry encodes:
- `0x01 ALIVE` — entity exists, fields follow
- `0x02 DORMANT` — entity exists but is dormant (no field data)
- `0x04 DELETED` — entity was destroyed this tick (no fields, just the header)
- `0x08 FULL_SNAPSHOT` — all fields present (not delta)

If an entity hasn't changed at all since last tick: **skip it entirely** (don't write an entry).

### Estimated Size Per Tick

Worst case (first tick, all 200 entities, ~10 fields each):
- 200 × (6 byte header + 10 × 14 byte avg field) = **~29 KB**

Typical delta tick (5-20 entities changed, 2-3 fields each):
- 15 × (6 + 3 × 14) = **~720 bytes**

For a 10,000-tick demo: ~1 MB header tick + 9,999 × 720 bytes ≈ **~7.5 MB**. Trivial.

---

## 3. What Entities and Fields to Dump

### The Rule: **DUMP EVERYTHING**

> "disk space is cheap and the algorithm designers can always choose to ignore certain entities later"

For every entity in `server->m_EntPtrArray[0..2047]` where `m_pEntity != nullptr`:

#### Universal Fields (every entity gets these)

| Field | Source | Type |
|-------|--------|------|
| `m_vecAbsOrigin` | `SE(ent)->abs_origin()` | VEC3 |
| `m_angAbsRotation` | `SE(ent)->abs_angles()` | VEC3 |
| `m_vecAbsVelocity` | `SE(ent)->abs_velocity()` | VEC3 |
| `m_iClassname` | `server->GetEntityClassName()` | STRING (tick 0 only) |
| `m_iName` | `server->GetEntityName()` | STRING (tick 0 only) |
| `m_iHealth` | `field<int>` | INT32 |
| `m_fFlags` | `SE(ent)->flags()` | INT32 |
| `m_nModelIndex` | `field<int>` | INT32 |
| `m_hOwnerEntity` | `field<CBaseHandle>` | HANDLE |
| `m_hGroundEntity` | `field<CBaseHandle>` | HANDLE |
| `m_nSolidType` | via ICollideable | BYTE |
| `m_vecMins` | OBB mins | VEC3 |
| `m_vecMaxs` | OBB maxs | VEC3 |

#### Class-Specific Fields (auto-discovered via SendTable)

Rather than hardcoding per-class fields, use **SAR's existing SendTable traversal** to discover fields at session start:

```cpp
void DiscoverEntityFields(void* entity) {
    auto serverClass = server->GetServerClass(entity);
    // Traverse serverClass->sendTable recursively
    // For each prop with type DPT_Int, DPT_Float, DPT_Vector:
    //   Register in the field table with its offset
}
```

This automatically handles:
- `m_bActivated` on portals
- `m_bIsPortal2` on portals
- `m_hLinkedPortal` on portals
- `m_bLocked` on buttons
- `m_toggle_state` on doors
- `m_bEnabled` on fizzlers
- `m_bPowered` on laser catchers
- **ANY field on ANY custom map entity** (sendificate machines, custom portals, etc.)

### Custom Map Compatibility

Because we use **runtime SendTable discovery**, any entity class compiled into the game (including custom entities from mods that use the standard Source entity system) automatically gets its fields dumped. The class table in the `.hdem` header will contain whatever classnames exist in the map.

The only entities that WON'T be captured are VScript-only entities that don't have C++ backing classes — but those are rare and their state would be in the VScript VM, not the entity system.

---

## 4. Recording: `sar_harness_record`

### Console Variables (reuse existing config)

```cpp
// Reuse sar_record_prefix and sar_record_mkdir from EngineDemoRecorder
extern Variable sar_record_prefix;  // already exists
extern Variable sar_record_mkdir;   // already exists

// New: harness-specific
Variable sar_harness_record("sar_harness_record", "0",
    "Enable harness demo recording alongside normal demo recording.\n"
    "When enabled, any 'record' command will also produce a .hdem sidecar.\n"
    "0 = off, 1 = record .hdem alongside .dem\n");

Variable sar_harness_record_interval("sar_harness_record_interval", "1", 1, 66,
    "Record entity snapshots every N ticks. 1 = every tick (default).\n"
    "Higher values reduce .hdem file size at the cost of temporal resolution.\n");
```

### How It Integrates with Existing `record` Command

**No new recording command needed.** We piggyback on the existing `record` / `sar_autorecord` flow:

```cpp
// Hook: When StartRecording fires, also start .hdem recording
DETOUR(EngineDemoRecorder::StartRecording, const char* filename, ...) {
    auto result = EngineDemoRecorder::StartRecording(thisptr, filename, ...);
    
    if (result && sar_harness_record.GetBool()) {
        std::string hdemPath = std::string(engine->GetGameDirectory())
            + "/" + filename + ".hdem";
        g_hdemRecorder.Start(hdemPath);
    }
    return result;
}

// Hook: When StopRecording fires, also stop .hdem recording
DETOUR(EngineDemoRecorder::StopRecording) {
    if (g_hdemRecorder.IsActive()) {
        g_hdemRecorder.Stop();
    }
    return EngineDemoRecorder::StopRecording(thisptr);
}
```

### Per-Tick Recording (in `POST_TICK` event handler)

```cpp
ON_EVENT(POST_TICK) {
    if (!g_hdemRecorder.IsActive()) return;
    if (!engine->hoststate->m_activeGame) return;  // Server must be running
    
    static int tickCounter = 0;
    if (++tickCounter % sar_harness_record_interval.GetInt() != 0) return;
    
    g_hdemRecorder.RecordTick(server->gpGlobals->tickcount);
}
```

### Player Overhead Analysis

The recording hook runs in `POST_TICK`, which fires once per server tick (66.67 Hz in Portal 2).

**Per-tick work:**
1. Iterate `m_EntPtrArray[0..2047]` — ~2048 pointer null-checks → **~5 µs**
2. For each alive entity (~50-200), read ~15 fields via cached offsets → **~30-80 µs**
3. Delta-compare against last-sent values → **~10-20 µs**
4. Serialize changed fields to binary buffer → **~5-10 µs**
5. `fwrite()` the buffer → **~5 µs** (OS-buffered)

**Total: ~55-120 µs per tick.** At 66.67 Hz, that's 0.4-0.8% of the 15ms tick budget. **Imperceptible to the player.**

For comparison, the existing `AcceptInput` hook + entity input recording already adds similar overhead.

### Schema Discovery at Session Start

When recording starts (SESSION_START), discover all entity classes and their fields:

```cpp
void HdemRecorder::DiscoverSchema() {
    classTable.clear();
    fieldTable.clear();
    
    for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
        auto info = entityList->GetEntityInfoByIndex(i);
        if (!info->m_pEntity) continue;
        
        const char* className = server->GetEntityClassName(info->m_pEntity);
        if (!className) continue;
        
        // Register class if new
        uint16_t classId = GetOrAddClass(className);
        
        // Discover fields from SendTable (if not already known for this class)
        if (!classFieldsDiscovered[classId]) {
            auto serverClass = /* get CServerClass from entity */;
            TraverseSendTable(serverClass->sendTable, classId);
            classFieldsDiscovered[classId] = true;
        }
    }
}
```

This happens **once** per session (map load). Field offsets are cached for the entire session.

---

## 5. Recording → Consuming PC Consistency

### What Differs Between PCs

| Property | Recording PC | Consuming PC | Impact |
|----------|-------------|-------------|--------|
| Resolution | Player's choice (e.g. 1920×1080) | Configured (e.g. 854×480) | **Pixels differ** — but `.hdem` has no pixels |
| Refresh rate / fps_max | 60/120/144 Hz | Uncapped | **Tick count identical** — tickrate is fixed at 66.67 |
| Visual quality settings | High/Ultra | Low/mat_norendering | **Pixels differ** — `.hdem` unaffected |
| sv_cheats | Off during play | On during conversion | No impact on entity state |
| OS / GPU | Various | Linux headless | `.hdem` is pure data, fully portable |

### The Brilliance of the Sidecar Approach

The `.hdem` file contains **ZERO rendering data**. It's pure game state:
- Entity positions, angles, velocities
- Entity class names, target names
- Entity field values (activation states, health, handles)

This data is **100% deterministic and resolution-independent**. A `.hdem` recorded at 4K ultra settings on Windows is byte-identical to one recorded at 640×480 low on Linux, because the server simulation is tick-deterministic.

### The `.rollout` Conversion Pipeline

When converting `.hdem` → `.rollout`, the consuming PC plays back the `.dem` file (for pixels/audio if needed) while reading entity state from the `.hdem` sidecar:

```
.dem file  → playdemo → client entities → pixels (ReadScreenPixels)
                                         ↓
.hdem file → read tick data → entity snapshots ─────────┐
                                                        ↓
                                              ┌─────────────────┐
                                              │   .rollout file  │
                                              │  ├─ RolloutHeader│
                                              │  ├─ RolloutStep 0│
                                              │  │  ├─ GameState │
                                              │  │  ├─ Action    │
                                              │  │  ├─ EntitySnap│
                                              │  │  └─ image_data│
                                              │  ├─ RolloutStep 1│
                                              │  └─ ...          │
                                              └─────────────────┘
```

**If pixels aren't needed** (entity-only rollout), the `.dem` file isn't even required. The `.hdem` alone contains the complete world state.

---

## 6. Fast `.hdem` → `.rollout` Conversion

### The Speed Problem

Currently, `sar_harness_playdemo` plays the demo at ~1x speed because demo playback runs in real-time within the engine's frame loop. For a 5-minute demo, that's 5 minutes of wall-clock time per conversion.

### Solution 1: `mat_norendering 1` + `fps_max 0`

If we don't need pixel data in the rollout (entity-only mode):
```
mat_norendering 1    → Skip all GPU rendering
fps_max 0            → Remove frame rate cap
host_timescale 100   → Run simulation at 100x speed
```

The engine processes ticks as fast as the CPU can go. Entity state comes entirely from the `.hdem` sidecar. **Expected speedup: 10-50x** (a 5-minute demo processes in 6-30 seconds).

### Solution 2: Skip Demo Playback Entirely

If we don't need pixels OR client-side interpolation, we can **read the `.hdem` file directly** without running the engine at all:

```python
# Pure Python .hdem reader — no Portal 2 needed!
def hdem_to_rollout(hdem_path, output_path):
    with open(hdem_path, 'rb') as f:
        header = read_hdem_header(f)
        class_table = read_class_table(f)
        field_table = read_field_table(f)
        
        with open(output_path, 'wb') as out:
            write_rollout_header(out, header)
            
            while not eof(f):
                tick = read_tick(f, field_table)
                step = make_rollout_step(tick, class_table)
                write_rollout_step(out, step)
```

**Speedup: 100-1000x.** Reading a 7 MB binary file and writing a protobuf stream is milliseconds. No game engine needed.

This is the ultimate payoff of the `.hdem` format: **training data extraction becomes a pure data transformation**, not a game simulation.

### Solution 3: Pixel Capture at Max Speed

If pixels ARE needed, use SAR's existing `SetSkipping(true)` mechanism (from TAS player):

```cpp
// In the conversion command handler:
engine->SetSkipping(true);   // Suppresses rendering but ticks still fire
mat_norendering.SetValue(1);  // Belt and suspenders
fps_max.SetValue(0);          // Uncap

// In POST_TICK during conversion:
if (needPixelsThisTick) {
    engine->SetSkipping(false);
    mat_norendering.SetValue(0);
    // Render ONE frame
    ReadScreenPixels(...);
    engine->SetSkipping(true);
    mat_norendering.SetValue(1);
}
```

This renders only the specific frames we need for the rollout. **Expected speedup: 5-20x** depending on how many frames need rendering.

### New Command: `sar_harness_convert`

```cpp
CON_COMMAND(sar_harness_convert,
    "sar_harness_convert <demo> [output] [pixels:0|1] [speed:fast|max] - "
    "Convert .dem+.hdem to .rollout\n") {
    
    // Parse args...
    // If pixels=0 and .hdem exists: pure Python-style fast conversion
    // If pixels=1: playdemo with speed optimizations + .hdem entity overlay
}
```

---

## 7. Custom Map / Mod Compatibility

### How Custom Maps Add Entities

Portal 2 custom maps (via Hammer editor) use the standard Source entity system. Custom entities like:
- **Sendificate machines** — typically `prop_dynamic` or custom point entities
- **Custom portal types** — derived from `CPropPortal` or custom classes
- **Time portals** — usually custom `trigger_*` entities
- **Gel dispensers** — `info_paint_sprayer` with custom parameters

These all appear in the server's entity list with their class names and have SendTable-defined properties.

### What Our System Captures

Since we use **runtime SendTable traversal**:

1. **Any entity with a C++ class** → Fully captured. All SendTable props are discovered and recorded.

2. **VScript-created entities** → The entity itself is captured (position, classname = `point_script`), but VScript-internal variables are NOT in the entity's SendTable and won't be captured. This is a fundamental limitation — VScript state lives in the Squirrel VM, not the entity system.

3. **Custom entity classes from map .bsp** → If the mapper compiled custom entity code (via FGD/C++ plugin), those entities and their SendTable props are fully captured.

4. **Entity I/O logic** → SAR already records `AcceptInput` events as custom demo data (type `0x03`). The `.hdem` records the state *result* of those I/O events. Combined, you get both the cause (input event) and the effect (state change).

### Forward Compatibility

The `.hdem` format is self-describing:
- Class table is written per-file (adapts to whatever entities exist in the map)
- Field table is written per-file (adapts to whatever fields exist on those entities)
- A new entity class in a mod just adds entries to these tables

A consumer reading the `.hdem` doesn't need to know about Portal 2 specifically — it reads the tables and decodes the data generically.

---

## 8. Implementation Roadmap

### Phase 1: HdemRecorder Core (2-3 days)

**New files:**
- `src/Features/Harness/HdemRecorder.hpp`
- `src/Features/Harness/HdemRecorder.cpp`

**Contents:**
- `HdemRecorder::Start(path)` — write magic, version, header
- `HdemRecorder::DiscoverSchema()` — traverse entity list, build class/field tables
- `HdemRecorder::RecordTick(tickNum)` — iterate entities, delta-encode, write tick frame
- `HdemRecorder::Stop()` — write footer, close file
- Delta-tracking: `std::unordered_map<int, std::vector<uint8_t>> lastSentState`

### Phase 2: Hook into Demo Recording (1 day)

**Modified files:**
- `src/Modules/EngineDemoRecorder.cpp` — add hooks to Start/StopRecording
- `src/Features/Harness/Harness.cpp` — add `POST_TICK` handler for `.hdem` recording

**New cvars:**
- `sar_harness_record` (0/1)
- `sar_harness_record_interval` (1-66)

### Phase 3: HdemReader + Rollout Integration (2 days)

**New files:**
- `src/Features/Harness/HdemReader.hpp`
- `src/Features/Harness/HdemReader.cpp`

**Contents:**
- `HdemReader::Open(path)` — parse header, class table, field table
- `HdemReader::ReadTick(tickNum)` — read/reconstruct full entity state for tick
- Integration with `RolloutRecorder` to embed entity snapshots in `.rollout`

### Phase 4: Python Reader (1 day)

**New files:**
- `py/hdem_reader.py` — Pure Python `.hdem` parser
- `py/hdem_to_rollout.py` — Standalone converter (no game engine needed)

### Phase 5: Fast Conversion Command (1 day)

**Modified files:**
- `src/Features/Harness/Harness.cpp` — add `sar_harness_convert` command
- Speed optimizations: `mat_norendering`, `fps_max 0`, selective rendering

### Phase 6: Proto Schema Updates (0.5 day)

**Modified files:**
- `src/Features/Harness/harness.proto` — add `EntityState`, `EntitySnapshot` to `GameState` and `RolloutStep`

**Total: ~7-8 days of focused work.**

---

## 9. Summary of Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **File format** | Binary sidecar `.hdem` | Doesn't touch `.dem`, zero compatibility risk |
| **Entity scope** | ALL entities, ALL fields | Disk is cheap; algorithm designers filter later |
| **Field discovery** | Runtime SendTable traversal | Auto-adapts to custom maps and mods |
| **Delta encoding** | Per-entity hash, skip unchanged | ~720 bytes/tick avg vs ~29 KB full |
| **Recording trigger** | Piggyback on `record` + cvar | Zero UX friction, reuse `sar_record_prefix` etc. |
| **Fast conversion** | 3 tiers: Python-only, no-render, selective-render | 100-1000x speedup without pixels |
| **Cross-PC consistency** | Entity data is tick-deterministic, resolution-independent | Same `.hdem` on any machine |
| **Custom map support** | Automatic via SendTable discovery | No hardcoded entity lists |
