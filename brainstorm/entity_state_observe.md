# Entity State Extraction for Observe() — Brainstorming Document

> **Goal**: Enrich the `Observe()` / `InternalObserve()` function in the Portal 2 Harness with per-tick entity state data, enabling training of a high-fidelity world model beyond raw pixels.

---

## Table of Contents

1. [Current State of Observe()](#1-current-state-of-observe)
2. [The Server vs Client Entity Problem](#2-the-server-vs-client-entity-problem)
3. [What Entity Data Can SAR Extract?](#3-what-entity-data-can-sar-extract)
4. [Portal 2 Entity Classes That Matter](#4-portal-2-entity-classes-that-matter)
5. [Delta-Encoding Strategy](#5-delta-encoding-strategy)
6. [Proposed Protobuf Schema](#6-proposed-protobuf-schema)
7. [Implementation Plan Sketch](#7-implementation-plan-sketch)
8. [Open Questions & Risks](#8-open-questions--risks)
9. [Impact on World Model Training](#9-impact-on-world-model-training)

---

## 1. Current State of Observe()

Today, `InternalObserve()` in [`Portal2HarnessImpl.cpp`](../src/Features/Harness/Portal2HarnessImpl.cpp) extracts **only the player**:

```cpp
// Current GameState proto:
message GameState {
  Vector3 position = 1;   // player position
  Vector3 velocity = 2;   // player velocity
  Vector3 camera = 3;     // pitch, yaw, roll
  int32 health = 4;
  bool is_crouching = 5;
  int32 server_tick = 6;
}
```

That's **6 floats + 2 scalars**. The entire world state of the Portal 2 map — portals, cubes, buttons, doors, turrets, lasers, light bridges, funnels — is invisible to the agent unless it can see it in the pixels.

The observation is also asymmetric:
- **Live play (RL training)**: reads from `server->GetPlayer(1)` via `ServerEnt` — full access to server-side entity list
- **Demo playback (rollout recording)**: reads from `client->GetPlayer(1)` via `ClientEnt` — limited to networked/predicted fields

---

## 2. The Server vs Client Entity Problem

This is the crux of the consistency challenge.

### During Live Play (RL Inference)
- The **server is running**. We have `server->m_EntPtrArray`, a contiguous array of `CEntInfo` structs for up to `NUM_ENT_ENTRIES` (2048) entities.
- We can iterate the full entity list, call `server->GetEntityClassName()`, `server->GetEntityName()`, and read arbitrary fields via `SE(ent)->field<T>("fieldname")`.
- We get **authoritative server state**: positions, angles, velocities, activation states, etc.
- DataMaps (via `GetDataDescMap()`) give us **rich type info** including embedded structs and fields not networked to clients.
- SendTables (via `GetServerClass()`) give us the **networkable subset**.

### During Demo Playback (Training Data)
- The server is **not active**. `server->GetPlayer()` returns null. `server->m_EntPtrArray` may be stale or empty.
- The client **does** maintain a `VClientEntityList003` interface (`client->s_EntityList`), accessible via `client->GetClientEntity(index)`.
- Client entities have `ClientEnt` with `ClientClass` (via `RecvTable`) and prediction datamaps (via `GetPredDescMap()`).
- **What's available**: All **networked properties** that were transmitted from the server during the original recording. This includes position, angles, and any field exposed via SendTable/RecvTable.
- **What's NOT available**: Server-only fields (things in the server DataMap that aren't in the SendTable), and anything the server decided not to transmit (PVS culling, LOD).

### Key Insight: RecvTables Mirror SendTables

The Source engine's networking model means that every field in a `SendTable` (server-side) has a corresponding `RecvTable` entry (client-side) with the **same name and offset structure**. When a demo is played back, the engine replays the network stream, so all fields that were originally networked are available on the client entity.

For Portal 2 specifically, most gameplay-critical fields **are networked**:
- `m_vecAbsOrigin`, `m_angAbsRotation`, `m_vecAbsVelocity` ✅
- `m_bActivated` (portals) ✅
- `m_bIsPortal2` (portal color) ✅
- `m_hFiredByPlayer` (who shot it) ✅
- `m_iClassname` (class name) — **server-only, not networked** ❌
- `m_iName` (targetname) — **server-only, not networked** ❌

### The `m_iClassname` Problem

The biggest issue: **class names and target names are server-side DataMap fields, not networked**. During demo playback, we can't call `server->GetEntityClassName()`.

**BUT**: We *can* get the `ClientClass::m_pNetworkName`, which is the network class name (e.g. `"CPropPortal"`, `"CWeightedCube"`, `"CPortalButton"`). This is available from the client entity's vtable and is valid during demo playback. The mapping from network class name to game entity class name is deterministic and well-known.

### Resolution Strategy

| Field | Live (Server) | Demo (Client) | Notes |
|-------|--------------|---------------|-------|
| Position | `SE(ent)->abs_origin()` | `CE(ent)->abs_origin()` | Both work ✅ |
| Angles | `SE(ent)->abs_angles()` | `CE(ent)->abs_angles()` | Both work ✅ |
| Velocity | `SE(ent)->abs_velocity()` | `CE(ent)->abs_velocity()` | Both work ✅ |
| Class name | `server->GetEntityClassName()` | `ClientClass->m_pNetworkName` | Different string, same semantics |
| Target name | `server->GetEntityName()` | ❌ Not available | Must skip or use index-based IDs |
| Custom fields | `SE(ent)->field<T>("x")` | `CE(ent)->field<T>("x")` | Works if field is in RecvTable |
| Flags | `SE(ent)->flags()` | Not defined on ClientEnt | Could add via `field<int>("m_fFlags")` |
| Collision | `SE(ent)->collision()` | ❌ Requires server | Best-effort from client data |

---

## 3. What Entity Data Can SAR Extract?

SAR has **deep access** to the Source engine entity system. Here's what's concretely available:

### 3.1 Entity Enumeration

```cpp
// From EntityList.cpp — iterate every entity in the map
for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
    CEntInfo* info = entityList->GetEntityInfoByIndex(i);
    if (info->m_pEntity == nullptr) continue;
    // info->m_pEntity is a void* to the entity
    // info->m_SerialNumber is the serial (for handle validation)
}
```

- **Server-side**: Up to 2048 entities, accessed via `server->m_EntPtrArray`
- **Client-side**: Accessed via `client->GetClientEntity(index)`, returns `ClientEnt*`

### 3.2 Arbitrary Field Access

SAR's `Entity.hpp` provides a powerful template-based field accessor:

```cpp
// Read any field by name and type:
ServerEnt* se = (ServerEnt*)entity;
Vector pos = se->field<Vector>("m_vecAbsOrigin");
bool activated = se->field<bool>("m_bActivated");
int health = se->field<int>("m_iHealth");
float speed = se->field<float>("m_flMaxspeed");
CBaseHandle handle = se->field<CBaseHandle>("m_hGroundEntity");
```

Under the hood, this uses `traverseSendTables()` and `traverseDataMap()` to build a name→offset cache per class, then reads directly from the entity's memory at that offset. **The same system works on ClientEnt via RecvTables.**

### 3.3 Field Discovery (Auto-Schema)

SAR already has `ClassDumper` and `DataMapDumper` that can export the full field schema:

- `sar_dump_server_classes` → `server_classes.json` (all SendTable props with offsets/types)
- `sar_dump_client_classes` → `client_classes.json` (all RecvTable props with offsets/types)
- `sar_dump_server_datamap` → `server_datamap.json` (all DataMap fields)
- `sar_dump_client_datamap` → `client_datamap.json`

This means we can **auto-discover** what fields exist on any entity class at runtime.

### 3.4 Collision Data

Via `ICollideable` (from `SE(ent)->collision()`):
- `GetCollisionOrigin()`, `GetCollisionAngles()` — world-space collision transform
- `OBBMins()`, `OBBMaxs()` — oriented bounding box
- `WorldSpaceSurroundingBounds()` — AABB in world space
- `GetSolid()` — solid type (BBOX, OBB, VPHYSICS, BSP, etc.)
- `GetSolidFlags()` — NOT_SOLID, NOT_STANDABLE, etc.
- `GetCollisionGroup()` — collision group ID
- `GetVPhysicsObject()` — physics simulation object

**Caveat**: `ICollideable` is only available on server entities. During demo playback, collision data is not directly accessible (the collision system isn't fully simulated client-side).

---

## 4. Portal 2 Entity Classes That Matter

Not all 2048 entity slots matter. For a world model, we care about entities that are **visible, interactive, or mechanically relevant**. Here's the taxonomy:

### Tier 1: Player State (already extracted)
- `player` / `CPortal_Player` — position, velocity, camera, crouch, health

### Tier 2: Core Puzzle Mechanics
| Entity Class | Key Fields | Importance |
|-------------|-----------|------------|
| `prop_portal` / `CPropPortal` | `m_vecAbsOrigin`, `m_angAbsRotation`, `m_bActivated`, `m_bIsPortal2`, `m_hFiredByPlayer`, `m_hLinkedPortal` | **Critical** — the defining mechanic |
| `prop_weighted_cube` / `CWeightedCube` | position, angles, velocity, `m_bNewSkins` | **Critical** — core puzzle element |
| `prop_monster_box` / `CFrankenTurret` | position, angles, velocity | **Critical** — Frankenturret |
| `prop_button` / `CPortalButton` | position, `m_bLocked` | **High** — activation triggers |
| `func_door` / `CDoor` | position, `m_toggle_state`, `m_bLocked` | **High** — gates progress |
| `npc_portal_turret_floor` | position, angles, `m_iHealth`, `m_bEnabled` | **High** — hazard avoidance |

### Tier 3: Environmental Mechanics
| Entity Class | Key Fields | Importance |
|-------------|-----------|------------|
| `projected_tractor_beam_entity` | position, angles, direction | **Medium** — funnel mechanics |
| `trigger_paint_cleanser` | AABB bounds | **Medium** — gel removal |
| `prop_paint_bomb` | position, velocity | **Medium** — gel projectiles |
| `trigger_portal_cleanser` (fizzler) | AABB bounds, `m_bEnabled` | **Medium** — portal destruction |
| `point_laser_target` | position, `m_bPowered` | **Medium** — laser puzzles |
| `env_portal_laser` | position, angles, `m_bEnabled` | **Medium** — laser emitters |
| `prop_laser_catcher` | position, `m_bPowered` | **Medium** — laser receivers |
| `func_weight_button` | position, `m_toggle_state` | **Medium** — weight-activated buttons |

### Tier 4: Map Structure (mostly static, send once)
| Entity Class | Key Fields | Notes |
|-------------|-----------|-------|
| `func_brush` | bounds, `m_bEnabled` | Walls, platforms |
| `prop_dynamic` | position, angles, `m_bEnabled` | Moving platforms |
| `func_movelinear` | position, `m_flMoveDistance` | Elevators, pistons |
| `trigger_multiple` | AABB bounds | Trigger zones |
| `npc_security_camera` | position, angles, `m_iHealth` | Breakable cameras |
| `info_placement_helper` | position, angles, radius | Portal placement hints |

### Tier 5: Low-Priority / Cosmetic
- `env_sprite`, `env_beam`, `info_particle_system`, `light`, `ambient_generic`
- These don't affect gameplay mechanics and can be ignored.

---

## 5. Delta-Encoding Strategy

> "only the entities for which the entity info changed need to be streamed"

### 5.1 What Changes?

On a typical Portal 2 tick:
- **Player**: position, velocity, angles change every tick → always send
- **Cubes**: position/angles change when moving (picked up, dropped, placed on button) → maybe every few ticks
- **Portals**: position changes only when placed (very rare) → only on change
- **Buttons**: toggle state changes on activation (rare) → only on change
- **Doors**: toggle state changes (rare) → only on change
- **Turrets**: angles change when tracking (semi-frequent) → poll periodically
- **Static entities**: brushes, triggers → send once at session start, never again

### 5.2 Implementation Approach

Maintain a per-entity hash of the "last sent" state. Each tick:

```
for each entity in entity_list:
    if entity is alive:
        compute hash(position, angles, key_fields)
        if hash != last_sent_hash[entity_index]:
            add to delta packet
            last_sent_hash[entity_index] = hash
```

For efficiency, we can use a **two-tier system**:
1. **Full snapshot** on session start / Reset() — send all entities once
2. **Delta updates** on each Observe() — only changed entities

### 5.3 Estimated Bandwidth

Rough per-entity delta message size: ~80 bytes (index + class_id + position + angles + velocity + 2-3 custom fields)

Typical Portal 2 map has ~50-200 entities that matter. On a given tick, maybe 5-20 entities change state.

**Per-tick payload**: 5-20 entities × 80 bytes = **400-1600 bytes** — negligible compared to pixel data (854×480×3 = 1.2 MB).

For rollout recording to `.rollout` files, this is a fraction of the image data cost.

---

## 6. Proposed Protobuf Schema

```protobuf
// New messages for entity state

message EntityState {
  int32 entity_index = 1;         // slot in entity list (0-2047)
  int32 serial_number = 2;        // handle serial, for identity tracking
  string class_name = 3;          // e.g. "prop_portal", "prop_weighted_cube"
  string target_name = 4;         // e.g. "cube_1" (server only, empty in demos)

  // Transform
  Vector3 position = 5;
  Vector3 angles = 6;
  Vector3 velocity = 7;

  // Common gameplay fields
  bool is_active = 8;             // activated, enabled, etc. (class-dependent)
  int32 health = 9;
  int32 flags = 10;

  // Portal-specific
  bool is_portal_2 = 11;         // blue=false, orange=true (only for prop_portal)
  int32 linked_portal_index = 12; // entity index of linked portal

  // Collision info (server-only)
  int32 solid_type = 13;
  Vector3 collision_mins = 14;
  Vector3 collision_maxs = 15;

  // Generic key-value pairs for custom fields
  map<string, float> float_fields = 20;
  map<string, int32> int_fields = 21;
  map<string, bool> bool_fields = 22;
}

message EntitySnapshot {
  repeated EntityState entities = 1;
  bool is_full_snapshot = 2;      // true on Reset(), false on delta
  int32 server_tick = 3;
}

// Updated GameState
message GameState {
  Vector3 position = 1;
  Vector3 velocity = 2;
  Vector3 camera = 3;
  int32 health = 4;
  bool is_crouching = 5;
  int32 server_tick = 6;

  // NEW: entity world state
  EntitySnapshot entity_snapshot = 7;
}
```

### Why `map<string, float>` for Custom Fields?

Different entity classes have different important fields. Rather than trying to pre-define every possible field in the proto, we use a flexible key-value map. The **entity extraction logic** in C++ decides which fields to include per class.

For the ML pipeline, the Python side can convert these into a fixed-size feature vector by defining a "schema" per class name that maps field names to tensor indices.

---

## 7. Implementation Plan Sketch

### Phase 1: Entity Extraction Core

**File**: New `EntityExtractor.cpp` / `EntityExtractor.hpp` in `src/Features/Harness/`

```cpp
class EntityExtractor {
public:
    // Extract all entities from the current map state
    // Uses server entities when available, client entities during demo playback
    void ExtractSnapshot(portal2_harness::EntitySnapshot* snapshot, bool fullSnapshot);

private:
    // Per-entity extraction based on class name
    void ExtractPortal(void* ent, bool isServer, portal2_harness::EntityState* state);
    void ExtractCube(void* ent, bool isServer, portal2_harness::EntityState* state);
    void ExtractButton(void* ent, bool isServer, portal2_harness::EntityState* state);
    void ExtractTurret(void* ent, bool isServer, portal2_harness::EntityState* state);
    void ExtractGeneric(void* ent, bool isServer, portal2_harness::EntityState* state);

    // Delta tracking
    struct EntityHash { uint64_t hash; int serial; };
    std::unordered_map<int, EntityHash> lastSentState;

    // Class name resolution
    const char* GetClassName(void* ent, bool isServer);

    // Entity class whitelist (what to extract)
    static const std::unordered_set<std::string> EXTRACT_CLASSES;
};
```

### Phase 2: Integration with InternalObserve()

```cpp
bool Portal2HarnessImpl::InternalObserve(portal2_harness::GameState* response) {
    // ... existing player state extraction ...

    // NEW: Entity state extraction
    bool isDemo = engine->demoplayer->IsPlaying();
    bool fullSnapshot = /* first observe after reset */;
    entityExtractor->ExtractSnapshot(
        response->mutable_entity_snapshot(),
        fullSnapshot
    );

    return true;
}
```

### Phase 3: Client-Side Entity Iteration (for demos)

Currently, `EntityList` only uses `server->m_EntPtrArray`. For demo playback, we need to iterate client entities:

```cpp
// New: Iterate client-side entity list
for (int i = 0; i < MAX_EDICTS; ++i) {
    ClientEnt* ent = client->GetClientEntity(client->s_EntityList->ThisPtr(), i);
    if (!ent) continue;

    // Get class name from ClientClass
    ClientClass* cc = /* vtable call to GetClientClass */;
    const char* className = cc->m_pNetworkName;

    // Read fields using CE(ent)->field<T>("fieldname")
    // ...
}
```

### Phase 4: Python Consumption

```python
def _get_obs(self, env_msg):
    state = env_msg.state

    # Existing: player kinematics
    kinematics = np.array([...])

    # NEW: Entity state as structured data
    entity_features = self._encode_entities(state.entity_snapshot)

    return {
        "image": resized_image,
        "kinematics": kinematics,
        "entities": entity_features,  # fixed-size tensor
    }

def _encode_entities(self, snapshot):
    """Convert variable-length entity list to fixed-size tensor.

    Strategy: For each entity class, maintain N slots (e.g. 4 portals,
    8 cubes, 4 buttons). Entities are assigned to slots by index.
    Unused slots are zero-filled.
    """
    # Example: [4 portals × 10 features, 8 cubes × 7 features, ...]
    features = np.zeros(ENTITY_FEATURE_DIM, dtype=np.float32)
    # ... fill from snapshot ...
    return features
```

---

## 8. Open Questions & Risks

### Q1: How to Handle ClientClass vs Server ClassName?

The mapping is deterministic but the strings differ:
- Server: `prop_portal` (from `m_iClassname` DataMap field)
- Client: `CPropPortal` (from `ClientClass::m_pNetworkName`)

**Proposed solution**: Maintain a static lookup table mapping `ClientClass` network names to the familiar server-side class names. This table is fixed per game version.

```cpp
static const std::unordered_map<std::string, std::string> NETWORK_TO_CLASS = {
    {"CPropPortal", "prop_portal"},
    {"CWeightedCube", "prop_weighted_cube"},
    {"CPortalButton", "prop_button"},
    // ...
};
```

### Q2: PVS Culling During Demo Playback

During demo recording, the server only transmits entities within the player's **Potentially Visible Set (PVS)**. Entities behind walls or far away may not be in the client entity list during demo playback.

**Impact**: Some entities might "pop in" as the player moves. For world model training, this means the entity state is incomplete during demo-based training.

**Mitigations**:
1. SAR already has `sar_always_transmit_heavy_ents` which forces certain entity classes to always be transmitted. We could enable this during demo recording for training runs.
2. For rollout recording, we could add a pre-pass that forces all entities into PVS.
3. Accept the limitation — the world model learns that entities outside PVS are "unknown" (zero-filled), which is actually the ground truth for what the agent can reason about.

### Q3: Will Client Entity Iteration Be Fast Enough?

`client->GetClientEntity(index)` for all 2048 slots each tick could be expensive.

**Analysis**: The EntityList operations are pointer-indexed array lookups. `sar_list_ents` already iterates all 2048 slots for the debug HUD. The `PlayerTrace::ConstructPortalLocations()` does this every tick when recording. Performance should be fine — we're talking microseconds.

### Q4: Demo Playback — Is the Full Entity State Available?

Source engine demos store the **network stream**, which includes:
- Entity creation/deletion events
- Property updates for entities within PVS
- Baseline updates for entity classes

When replaying, the client reconstructs entity state from this stream. **All networked fields are available during demo playback.** Non-networked fields (like internal physics simulation state) are not.

### Q5: Should Entity Data Go in the GameState Proto or Separately?

**Option A**: Embed in `GameState` (proposed above) — simpler, everything in one message.
**Option B**: Separate gRPC call `ObserveEntities()` — more flexible, can be called at different frequencies.
**Option C**: Separate field in `EnvironmentMessage` — only for AgentLoop streaming.

**Recommendation**: Option A. Keep it in `GameState` so that rollout recording (`RolloutStep`) automatically captures entity state alongside everything else. The overhead is minimal.

### Q6: Risk of Crashing the Game

Reading arbitrary fields from entity memory carries crash risk if offsets are wrong. SAR's `EntField` system has type-checking (`warnBadFieldType`) but doesn't prevent reading from invalid offsets.

**Mitigations**:
- Only read fields from the **whitelisted set** per entity class
- Add null checks before every entity access
- Wrap the entire extraction in try/catch (even though Source's error handling is weak)
- Test extensively with `sar_dump_server_classes` output to validate offsets

### Q7: What About Trigger Volumes and Invisible Entities?

Many important gameplay elements are invisible triggers (`trigger_multiple`, `trigger_portal_cleanser`, `trigger_paint_cleanser`). These have AABB bounds but no visual representation.

**For world model training**: Including trigger volumes gives the model knowledge of "no-go zones" and fizzler locations that would otherwise require the agent to learn from negative experiences (portals disappearing, cubes being cleaned, etc.).

**Access pattern**: Triggers are server entities with `ICollideable` providing `WorldSpaceTriggerBounds()`. During demo playback, we may not have collision data, but the entity position + extents from network data may suffice.

---

## 9. Impact on World Model Training

### What the World Model Currently Sees
- Raw pixels (854×480×3 per tick)
- Player position/velocity/camera (6 floats)
- Player actions (discrete + continuous)

### What It Would See With Entity State
- Everything above, PLUS:
- **Portal locations and orientations** — the model knows exactly where portals are, even if they're off-screen
- **Cube positions and velocities** — the model can predict cube physics without needing to "see" the cube
- **Button/door states** — the model knows which puzzle elements are activated
- **Turret positions and orientations** — the model can predict threat zones
- **Fizzler/cleanser boundaries** — the model knows where portals will be destroyed

### Expected Benefits

1. **Dramatically improved world model fidelity** — Entity state is the "ground truth" that pixels are a lossy projection of. The world model can learn physics rules from structured state much more efficiently than from pixels alone.

2. **Better long-horizon prediction** — Knowing portal locations means the model can predict teleportation outcomes. Knowing button states means it can predict door openings.

3. **Faster training convergence** — Entity state provides a strong supervisory signal. Even a simple MLP can learn to predict next-entity-state from current-entity-state + action, while pixel-based prediction requires massive compute.

4. **Consistent observation space** — With the server/client unification strategy, the observation space is identical during demo-based training and live inference. The only difference is PVS coverage, which we can mitigate.

5. **Enables auxiliary losses** — The world model can have auxiliary prediction heads for entity state (predict where the cube will land, predict portal placement success), providing strong gradient signal even when pixel prediction is hard.

### Potential Architecture

```
[Pixels] → ViT Encoder → Image Embedding (768d)
[Entity State] → Entity Encoder (per-entity MLP + attention) → Entity Embedding (256d)
[Player Kinematics] → Linear → Kinematic Embedding (64d)

Concatenate → Transformer Backbone → World Model Prediction
                                   → Policy Head
                                   → Value Head
```

The Entity Encoder could use a **Set Transformer** or **Graph Attention Network** architecture since entities form an unordered set with pairwise relationships (portal linkage, cube-on-button, player-holding-cube).

---

## Summary / Next Steps

| Priority | Task | Effort |
|----------|------|--------|
| 🔴 **P0** | Add client-side entity iteration for demo playback | 1-2 days |
| 🔴 **P0** | Define `EntityState` / `EntitySnapshot` protobuf messages | 1 day |
| 🔴 **P0** | Implement `EntityExtractor` with portal + cube + button support | 2-3 days |
| 🟡 **P1** | Integrate into `InternalObserve()` with server/client branching | 1 day |
| 🟡 **P1** | Implement delta-encoding with per-entity hash tracking | 1-2 days |
| 🟡 **P1** | Network-name-to-classname mapping table | 0.5 day |
| 🟢 **P2** | Python-side entity feature encoding in `rl_challenge_env.py` | 1-2 days |
| 🟢 **P2** | Update `RolloutStep` proto to include entity snapshots | 0.5 day |
| 🟢 **P2** | Add turret, laser, funnel, fizzler extraction | 2-3 days |
| ⚪ **P3** | Entity-aware world model architecture (Set Transformer) | Research phase |
| ⚪ **P3** | PVS forcing for demo recording quality | Investigation |

**Total estimated effort for P0+P1**: ~1 week of focused C++ + proto work.

The code infrastructure in SAR (Entity.hpp, EntityList, ClassDumper) already does 80% of the heavy lifting. We're essentially packaging what `sar_ent_info` and `PlayerTrace::ConstructPortalLocations()` already do into a structured, streamable format.
