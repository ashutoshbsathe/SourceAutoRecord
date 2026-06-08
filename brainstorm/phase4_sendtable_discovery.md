# Phase 4: Full Entity Dump via SendTable Traversal

Replace hardcoded per-class field lists with runtime SendTable discovery so that **every** entity field on **every** entity class is automatically captured.

## Background

Phases 1-3 work correctly. They hardcode ~16 "well-known" fields and a handful of class-specific fields (portal activated, button locked, etc). Phase 4's goal is to auto-discover **all** SendTable props for every `ServerClass` and record them.

## Lessons from Prior Failed Attempts

The [failure post-mortem](file:///home/absathe/MachineLearning/SourceAutoRecord/brainstorm/phase4_fixing_slowness_and_crashes.md) documents cascading problems from over-engineering:

> [!CAUTION]
> The previous attempts introduced: RCU shared_ptr schema, POD TrackedEntity with 1024-byte flat buffers, flat 8192-element cache arrays, custom collision vtable validation, and dozens of other "performance" changes — all at once. The result was a tower of complexity that crashed in multiple novel ways (SIGSEGV, SIGABRT, OOB writes, data races, heap exhaustion).

**The lesson: do ONE thing — add SendTable discovery — and change as little else as possible.**

## Proposed Changes

The change is **surgically contained** to [EntitySnapshotter.cpp](file:///home/absathe/MachineLearning/SourceAutoRecord/src/Features/Harness/EntitySnapshotter.cpp) and [EntitySnapshotter.hpp](file:///home/absathe/MachineLearning/SourceAutoRecord/src/Features/Harness/EntitySnapshotter.hpp). No new files. No architectural changes.

---

### Core Design

#### How it works today (Phase 3)

[DiscoverSchema()](file:///home/absathe/MachineLearning/SourceAutoRecord/src/Features/Harness/EntitySnapshotter.cpp#L38-L104) does:
1. Pre-registers 16 well-known fields at fixed IDs
2. Hard-registers ~10 class names with hardcoded field lists
3. Iterates entity list, calls `RegisterClassSchema()` which adds the same universal fields to any new class

[Update()](file:///home/absathe/MachineLearning/SourceAutoRecord/src/Features/Harness/EntitySnapshotter.cpp#L142-L236) reads field values using `EntField::getServerOffset(ent, field.name.c_str())` — this already does SendTable + DataMap traversal internally and caches results per class. So the offset lookup is already efficient.

#### What changes (Phase 4)

**In `DiscoverSchema()`**: After registering well-known fields, walk `server->GetAllServerClasses()` to discover every `ServerClass` and its `SendTable` props. For each prop, register it as an `HdemFieldDef` with the appropriate type mapping. Then associate each class with its discovered fields.

**Key insight**: We don't need to change `Update()` at all. It already uses `EntField::getServerOffset(ent, field.name.c_str())` which does its own offset resolution per entity. We just need to populate `classes[cid].fields` with the right field definitions.

**The `Entity.cpp` infrastructure already does SendTable traversal** — see [traverseSendTables()](file:///home/absathe/MachineLearning/SourceAutoRecord/src/Entity.cpp#L48-L69). We follow the exact same pattern: walk `SendTable`, accumulate props with name/type/offset, recurse into `DPT_DataTable` children.

---

### [MODIFY] [EntitySnapshotter.hpp](file:///home/absathe/MachineLearning/SourceAutoRecord/src/Features/Harness/EntitySnapshotter.hpp)

Add one private method declaration:

```cpp
void DiscoverSendTableFields(ServerClass* sclass);
```

#### [MODIFY] [EntitySnapshotter.cpp](file:///home/absathe/MachineLearning/SourceAutoRecord/src/Features/Harness/EntitySnapshotter.cpp)

1. **Add include**: `#include "Utils/SDK/Class.hpp"` (for `ServerClass`, `SendTable`, `SendProp`, `SendPropType`)

2. **New function** `DiscoverSendTableFields(ServerClass* sclass)`:
   - Get or register the class via `GetOrAddClass(sclass->m_pNetworkName)`
   - If fields already populated for this class (the well-known ones from `RegisterClassSchema`), skip re-discovery
   - Recursively walk `sclass->m_pTable`, and for each leaf `SendProp`:
     - Map `DPT_Int` → `HDEM_INT32`, `DPT_Float` → `HDEM_FLOAT`, `DPT_Vector` → `HDEM_VEC3`, `DPT_String` → `HDEM_STRING`
     - Skip `DPT_Array`, `DPT_DataTable` (non-leaf), `DPT_Int64`, and props with names starting with `"baseclass"` or `"m_flSimulationTime"` (engine internal)
     - Call `GetOrAddField(propName, type)` to register globally
     - If this field is not already in the class's field list, add it
   - **Cap fields per class**: If a class already has more than e.g. 64 fields, stop adding. This bounds the per-entity work in `Update()`.

3. **Modify `DiscoverSchema()`**: After the existing well-known field registration and `RegisterClassSchema()` calls, add:
   ```cpp
   // Phase 4: Auto-discover all SendTable fields for every server class
   for (auto sclass = server->GetAllServerClasses(); sclass; sclass = sclass->m_pNext) {
       if (!sclass->m_pNetworkName || !sclass->m_pTable) continue;
       DiscoverSendTableFields(sclass);
   }
   ```

4. **No changes to `Update()`**: It already handles arbitrary field names via `EntField::getServerOffset()`.

5. **No changes to `RegisterClassSchema()`**: It still handles the well-known fields. The SendTable discovery adds additional fields on top.

---

### Type Mapping

| `SendPropType` | `HdemFieldType` | Size | Notes |
|---|---|---|---|
| `DPT_Int` | `HDEM_INT32` | 4 | Covers bools, shorts, ints (Source packs them all as DPT_Int in SendTables) |
| `DPT_Float` | `HDEM_FLOAT` | 4 | |
| `DPT_Vector` | `HDEM_VEC3` | 12 | |
| `DPT_VectorXY` | `HDEM_VEC3` | 12 | Treat as VEC3, z will just be 0 |
| `DPT_String` | `HDEM_STRING` | variable | Handled specially in Update() — already works |
| `DPT_Array` | — | — | **Skip** |
| `DPT_DataTable` | — | — | **Recurse**, not a leaf prop |
| `DPT_Int64` | — | — | **Skip** (rare, no HdemFieldType for it) |

---

### Safety Considerations

> [!IMPORTANT]
> **Why this won't repeat prior crashes:**

1. **No architectural changes**: `TrackedEntity`, delta tracking, `HdemRecorder`, `Update()` — all unchanged. The only moving part is which fields get registered at schema discovery time.

2. **`EntField::getServerOffset()` is already battle-tested**: The entire SAR codebase uses it. It handles missing fields gracefully (returns offset=0, type=NONE). The existing `Update()` code already checks `val.first != 0 && val.second != EntField::Type::NONE` before reading.

3. **No threading changes**: Schema discovery runs once at `SESSION_START` on the main thread. No mutex/RCU needed.

4. **No memory layout changes**: Same `std::unordered_map<uint16_t, std::vector<uint8_t>>` field storage. Same `std::unordered_map<int, TrackedEntity>` per tick. Yes it's more fields per entity now, but the existing code handles that — it's just more entries in the same maps.

5. **Field cap**: We cap at 64 fields per class to bound worst-case work. This prevents pathological classes with 200+ props from tanking performance.

### Performance Expectation

With the field cap of 64 and ~600 entities, worst case is 600 × 64 = 38,400 field reads per tick. Each `EntField::getServerOffset` call is a cached hash lookup. At ~50ns each, that's ~2ms per tick — well within the 15ms budget. In practice, most entities will have 15-30 fields due to the cap and SendTable sizes.

> [!NOTE]
> If profiling shows this is too slow (unlikely), we can reduce the cap or add a class whitelist cvar. But let's not pre-optimize.

---

## Decisions

1. **Field cap**: 64 fields per class. <!-- TODO: explore raising this if we find classes with important fields being truncated -->

2. **Skip filter cvar**: Deferred to a follow-up.

3. **Props to skip**: Only `baseclass` (engine pseudo-prop). Don't skip array-like or area props for now.

> [!WARNING]
> **Known bug**: `.hdem` recording stops on map transitions (`sar_autorecord`). This is a pre-existing issue — not caused by Phase 4. Should be investigated separately.

## Verification Plan

### Automated Tests
- Build and load a map with `sar_harness_record 1; record test_sendtable`
- Play for 5 seconds, stop
- Use existing `py/hdem_dump.py` to verify the `.hdem` now contains many more fields per entity (should see things like `m_bActivated`, `m_nModelIndex`, `m_clrRender`, etc. that weren't in the hardcoded list)
- Verify no crashes on map transitions (`sar_autorecord`)
- Verify the gRPC `Observe`/`AgentLoop` still works (it reads from the same snapshotter)

### Manual Verification
- Compare field counts: Phase 3 had ~9-12 fields per class. Phase 4 should have ~20-60.
- Check `.hdem` file size stays reasonable (should be somewhat larger but delta encoding keeps it bounded)

---

## Follow-up: datamap-only status fields are not discovered here (recon, 2026-06)

The status-field recon ([status_field_recon.md](status_field_recon.md)) ran `sar_harness_dump_fields` across real chambers and found that **several key puzzle status fields are datamap-only**, so the Phase 4 SendTable walk never registers them:

| Field | Class(es) | tag |
|---|---|---|
| `m_nCubeType`, `m_bActivated` | `prop_weighted_cube` | `[dm]` |
| `m_bPowered` | `point_laser_target` (the sensor behind catcher/relay) | `[dm]` |
| `m_toggle_state` | `trigger_portal_cleanser`, (expected) `prop_testchamber_door` | `[dm]` |

Networked status fields (portal `m_bActivated`/`m_hLinkedPortal`/`m_bIsPortal2`, emitter `m_bLaserOn`, fizzler `m_bDisabled`) **are** found by the SendTable walk and need nothing extra.

**Why they're missed:** discovery (this doc) walks `SendTable`s only; the *read* path (`EntField::getServerOffset` in `Update()`) already resolves datamap **and** SendTable. So the fix is purely at registration time — and per this doc's own "do ONE thing" lesson it must stay surgical:

> **Do not** add a symmetric full-datamap walk — datamaps are huge; that's exactly the over-discovery the field cap exists to bound. **Do** register a small **curated per-class status set** — the handful of `[dm]` fields the recon table identifies — and let `getServerOffset` read them. The recon output *is* that curated list.

Caveat for whatever consumes these: catcher/relay power is **not** on the prop — it lives on a child `point_laser_target`. The resolver must associate catcher/relay → target (parent or proximity). Detail in the recon doc.
