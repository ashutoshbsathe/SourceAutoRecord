# EntitySnapshotter Redesign Brainstorm

## What's wrong with the current design

### Per-tick allocation (the main problem)

`Update()` rebuilds `activeEntities` from scratch every tick:

```cpp
std::unordered_map<int, TrackedEntity> activeEntities;  // cleared + rebuilt
```

`TrackedEntity` itself is a fat struct:
```cpp
struct TrackedEntity {
    std::string className;       // heap allocation
    std::string targetName;      // heap allocation
    std::unordered_map<uint16_t, std::vector<uint8_t>> fieldValues;  // heap × N fields
};
```

For 200 alive entities × ~20 fields each, that's **~4000 vector constructions per tick**,
each of which may allocate. At 67 Hz this is genuinely bad.

### The `getServerOffset` try/catch at schema time

`getServerOffset` returns a **reference into a map** — if the field doesn't exist, it
returns a default-constructed `{0, NONE}` pair (it never throws). The try/catch is
cargo-culted. The actual check is just `rf.offset != 0`.

Worse: `getServerOffset` does the full SendTable+datamap traversal once per class and
caches it in `g_server_offsets`. Our `ResolvedClass` is a redundant re-derivation of
data already cached there. We're double-caching.

### The 6-way special-case dispatch in the hot path

Every field read in `Update()` runs a branch chain per field per entity per tick.
The `ICollideable` try/catch blocks are dead weight — `se->collision()` is a field
access, not a throwing operation.

### Lazy resolution inside the tick loop

`if (!rcls.resolved)` runs inside `Update()`. Schema resolution should be front-loaded.

### 2048 slot iteration regardless of alive count

On a typical Portal 2 SP map ~100–300 entities are alive. We check 2048 null pointers.

### GetSnapshot copies everything out

Called from `InternalObserve` and `HdemRecorder::RecordTick`. The caller then
immediately does its own delta. We copy everything just to compare it.

---

## Does Source have its own change tracking?

Yes. `CBaseEntity::NetworkStateChanged()` is a virtual method called whenever any
sendprop value changes. The engine accumulates dirty entities into a per-frame list
used by the demo recorder and networking code.

**Can SAR hook it?** It's a virtual method with variable vtable index across classes
(multiple inheritance, MSVC layout). We'd need to patch vtables per spawned entity.
`IServerGameDLL::GetAllChangedEntities()` is not accessible from SAR's game DLL tier.

**Verdict**: Too fragile. Our flat-buffer + memcmp approach costs one `memcmp` per
entity per tick (~100–300 memcmps) and is completely safe.

---

## Proposed design: flat slot array + compiled ClassLayout + changeVersion

### Core idea

Maintain a **persistent array of 2048 `EntitySlot` objects** indexed by entity slot.
Each slot owns a pre-allocated flat `uint8_t[]` buffer for its class's fields, computed
once at schema time from `EntField::g_server_offsets`. Field reads are a series of
`memcpy` from cached offsets — zero allocation, zero hash lookup, zero branching
beyond a small `ReadMode` switch that the branch predictor nails after the first entity
of each class.

Change detection: a **`uint32_t changeVersion`** per slot, bumped when `memcmp` of
the field buffer changes. Consumers track `uint32_t lastSeenVersion[2048]` — delta =
slots where versions differ. That's 2048 integer comparisons: essentially free.

### ClassLayout (compiled once per class at schema time)

```cpp
struct ClassLayout {
    uint16_t classId;
    uint16_t fieldBufSize;      // total bytes for all fields

    enum class ReadMode : uint8_t {
        DirectOffset,           // memcpy(dst, (char*)se + srcOffset, size)
        AbsOrigin,              // memcpy(dst, &se->abs_origin(), 12)
        AbsAngles,              // memcpy(dst, &se->abs_angles(), 12)
        AbsVelocity,            // memcpy(dst, &se->abs_velocity(), 12)
        CollisionSolid,         // *dst = se->collision().GetSolid()
        CollisionMins,          // memcpy(dst, &se->collision().OBBMins(), 12)
        CollisionMaxs,          // memcpy(dst, &se->collision().OBBMaxs(), 12)
    };

    struct FieldSlot {
        uint16_t fieldId;
        uint16_t dstOffset;     // byte offset within entitySlot.fieldBuf
        uint16_t srcOffset;     // byte offset within entity memory (DirectOffset only)
        uint8_t  size;
        ReadMode mode;
    };
    std::vector<FieldSlot> fields;
};
```

Building a `ClassLayout` from `EntField::g_server_offsets[className]` is O(fields) and
happens at most once per class per session.

### EntitySlot (2048, pre-allocated, persistent)

```cpp
struct EntitySlot {
    bool     alive         = false;
    uint16_t serial        = 0;
    uint16_t classId       = 0;
    uint32_t changeVersion = 0;

    // Allocated once at InitSlot. Never reallocated unless class changes.
    std::unique_ptr<uint8_t[]> fieldBuf;
    uint16_t fieldBufSize = 0;

    // Stored once at InitSlot, not every tick.
    std::string className;
    std::string targetName;
};
```

### ReadFields (the only hot-path function)

```cpp
void ReadFields(EntitySlot& slot, ServerEnt* se, const ClassLayout& layout) {
    uint8_t* buf = slot.fieldBuf.get();
    for (const auto& fs : layout.fields) {
        uint8_t* dst = buf + fs.dstOffset;
        switch (fs.mode) {
        case ReadMode::AbsOrigin:   { auto v = se->abs_origin();   memcpy(dst,&v,12); } break;
        case ReadMode::AbsAngles:   { auto v = se->abs_angles();   memcpy(dst,&v,12); } break;
        case ReadMode::AbsVelocity: { auto v = se->abs_velocity(); memcpy(dst,&v,12); } break;
        case ReadMode::CollisionSolid: { uint8_t s=(uint8_t)se->collision().GetSolid(); *dst=s; } break;
        case ReadMode::CollisionMins: { auto v=se->collision().OBBMins(); memcpy(dst,&v,12); } break;
        case ReadMode::CollisionMaxs: { auto v=se->collision().OBBMaxs(); memcpy(dst,&v,12); } break;
        case ReadMode::DirectOffset:
            memcpy(dst, (const char*)se + fs.srcOffset, fs.size); break;
        }
    }
}
```

No allocation. No hash lookup. No try/catch. The 7-way switch is branch-predictor
friendly because entities of the same class always take the same paths.

### Update() — new hot path

```cpp
void EntitySnapshotter::Update() {
    std::lock_guard lock(mutex);
    currentTick = server->gpGlobals->tickcount;

    // Stack scratch buffer for prev-state comparison.
    // Max field buf is bounded by MAX_FIELDS_PER_CLASS × max_field_size (~4 KB).
    alignas(16) uint8_t prev[4096];

    for (int i = 0; i < NUM_ENT_ENTRIES; i++) {
        auto* info = entityList->GetEntityInfoByIndex(i);
        auto& slot = slots[i];

        if (!info->m_pEntity) {
            slot.alive = false;
            continue;
        }

        uint16_t serial = static_cast<uint16_t>(info->m_SerialNumber);
        if (!slot.alive || slot.serial != serial) {
            InitSlot(i, info, info->m_pEntity);  // once per entity lifetime
        }

        const auto& layout = classLayouts[slot.classId];
        memcpy(prev, slot.fieldBuf.get(), slot.fieldBufSize);
        ReadFields(slot, SE(info->m_pEntity), layout);

        if (memcmp(prev, slot.fieldBuf.get(), slot.fieldBufSize) != 0) {
            slot.changeVersion++;
        }
    }
}
```

**Per-tick allocations: ZERO** after map load.

### How consumers use this

#### HdemRecorder

```cpp
// Member: uint32_t lastSeenVersion[NUM_ENT_ENTRIES] = {};

for (int i = 0; i < NUM_ENT_ENTRIES; i++) {
    const auto& slot = snapshotter->GetSlot(i);

    bool wasSeen = (lastSeenVersion[i] != 0);
    bool isAlive = slot.alive;

    if (!isAlive && !wasSeen) continue;                         // never seen, skip
    if (isAlive && slot.changeVersion == lastSeenVersion[i]) continue;  // unchanged

    if (!isAlive) {
        EmitDeleted(i, slot.serial);
        lastSeenVersion[i] = 0;
        continue;
    }

    // Emit full or delta record from slot.fieldBuf + classLayouts[slot.classId]
    EmitEntityRecord(i, slot, layout, isNew=(lastSeenVersion[i]==0));
    lastSeenVersion[i] = slot.changeVersion;
}
```

**No GetSnapshot call. No copying. No per-entity allocation.**

#### InternalObserve / AgentLoop delta

```cpp
// Member: uint32_t observeLastVersion[NUM_ENT_ENTRIES] = {};

for (int i = 0; i < NUM_ENT_ENTRIES; i++) {
    const auto& slot = snapshotter->GetSlot(i);
    if (slot.changeVersion == observeLastVersion[i]) continue;
    // Populate EntityState proto from slot directly
    observeLastVersion[i] = slot.changeVersion;
}
```

The `observeLastState` map with `unordered_map<uint16_t, vector<uint8_t>> fieldValues`
per entity is **gone**. The `LastSentState` struct is gone. Delta is 2048 integer
comparisons.

---

## Memory budget

| Structure | Size |
|-----------|------|
| `EntitySlot[2048]` struct overhead | ~160 KB |
| `fieldBuf` allocations (~200 alive × ~300 B avg) | ~60 KB |
| `ClassLayout` objects (~20 classes × ~20 fields × 12 B) | ~5 KB |
| `lastSeenVersion[2048]` per consumer (×2) | ~16 KB |
| Total | **~240 KB** |

Well under the user's stated budget. No need for 500 MB.

---

## What gets removed

| Removed | Why |
|---------|-----|
| `TrackedEntity` struct | Replaced by `EntitySlot` with flat fieldBuf |
| `unordered_map<uint16_t, vector<uint8_t>> fieldValues` | Replaced by flat byte buffer |
| `activeEntities` map | Replaced by `slots[2048]` array |
| `ResolvedClass` + lazy-resolve check inside Update() | Replaced by `ClassLayout`, built at schema time |
| All try/catch blocks | `collision()` is not a throwing call; offset validity checked at schema time |
| 6-way if-chain in field reading | Replaced by `ReadMode` enum switch |
| `GetSnapshot`/`GetSnapshotAndSchema` | Consumers read `GetSlot(i)` directly |
| `LastSentState` / `observeLastState` map in Portal2HarnessImpl | Replaced by `observeLastVersion[2048]` |
| `sessionLastState` in AgentLoop | Long gone; now via InternalObserve |

---

## Files to change

| File | Change |
|------|--------|
| `EntitySnapshotter.hpp` | New public API: `GetSlot(i)`, `GetClasses()`, `GetFields()`. New internals: `EntitySlot[2048]`, `ClassLayout[]`. Remove `TrackedEntity`, `ResolvedClass`, `GetSnapshot`, `GetSnapshotAndSchema`. |
| `EntitySnapshotter.cpp` | Rewrite `DiscoverSchema()`, `Update()`. Add `InitSlot()`, `ReadFields()`, `BuildClassLayout()`. |
| `HdemRecorder.hpp/.cpp` | Replace `lastEntityState`/`lastEntitySerial` with `lastSeenVersion[NUM_ENT_ENTRIES]`. Read fields directly from `EntitySlot` + `ClassLayout`. |
| `Harness.hpp` | Remove `LastSentState` struct + `observeLastState` map. Add `observeLastVersion[NUM_ENT_ENTRIES]`. |
| `Portal2HarnessImpl.cpp` | Rewrite entity delta section of `InternalObserve` to use version comparison + direct slot reads. |

---

## Open questions before implementation

1. **Stack scratch buffer**: fixed `uint8_t prev[4096]` on the stack vs a member-level
   scratch buffer? Stack is fine for 4 KB; just needs `alignas(16)` for potential SIMD
   memcmp acceleration.

2. **`GetSlot(i)` locking**: `HdemRecorder::RecordTick` and `InternalObserve` both run
   on the main thread, same as `Update()`. If `Update()` is also called from the main
   thread (`POST_TICK`), the mutex isn't needed at all for these callers. We keep it
   for safety (gRPC `Observe` is called from the gRPC thread).

3. **`aliveIndices` optimization**: maintain a `vector<uint16_t>` of alive slot indices
   to avoid the 2048 null-pointer scan? Worth it for very sparse maps. Can be added
   as a follow-up.

4. **Exposing `ClassLayout` for proto population**: `InternalObserve` needs to
   translate `fieldBuf` bytes into proto `EntityField` values. It needs the `FieldSlot`
   list (fieldId, dstOffset, size, type). Expose `GetClassLayout(classId)` from
   EntitySnapshotter.
