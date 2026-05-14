# Alternative: Keeping Server Entities Alive During Demo Playback

> **Goal**: Instead of maintaining two code paths (server vs client) for entity extraction, find a way to make `server->m_EntPtrArray` accessible during demo playback so `InternalObserve()` uses a single, unified path.

---

## Table of Contents

1. [Why Two Code Paths is Painful](#1-why-two-code-paths-is-painful)
2. [How the Source Engine Actually Works During Demo Playback](#2-how-the-source-engine-actually-works-during-demo-playback)
3. [Approach A: `map` + `playdemo` — The Dual-Load Trick](#3-approach-a-map--playdemo--the-dual-load-trick)
4. [Approach B: Shadow Server Entity Array](#4-approach-b-shadow-server-entity-array)
5. [Approach C: Piggyback on Client Entities with a Unified Abstraction](#5-approach-c-piggyback-on-client-entities-with-a-unified-abstraction)
6. [Approach D: Record Entity Snapshots During Live Play, Embed in Demo](#6-approach-d-record-entity-snapshots-during-live-play-embed-in-demo)
7. [Approach E: Hybrid — `map` First, Then `playdemo` Overlay](#5-approach-e-hybrid--map-first-then-playdemo-overlay)
8. [Recommendation & Decision Matrix](#6-recommendation--decision-matrix)

---

## 1. Why Two Code Paths is Painful

The first brainstorm doc identified the core problem: `InternalObserve()` currently has:

```cpp
if (isDemo) {
    player = client->GetPlayer(1);   // ClientEnt*
} else {
    player = server->GetPlayer(1);   // ServerEnt*
}
```

Extending this to N entity types means every extraction function needs `if (isDemo) { ... } else { ... }` branching. And the data available differs:
- Server has `m_iClassname`, `m_iName` (targetname), collision data, physics state
- Client has `ClientClass::m_pNetworkName` instead of classname, no targetnames, limited collision
- Field offsets might differ between ServerEnt and ClientEnt for the same "logical" field
- The entity iteration mechanism is different (`server->m_EntPtrArray[i].m_pEntity` vs `client->GetClientEntity(i)`)

If we could make the server entity system available during demos, **all of this goes away**.

---

## 2. How the Source Engine Actually Works During Demo Playback

Let's be very precise about what's happening inside the engine when you run `playdemo`:

### Normal Gameplay (Listen Server)
```
Host Machine:
├── Server (CServerGameDLL)
│   ├── m_EntPtrArray[0..2047]  ← AUTHORITATIVE entity state
│   ├── GameFrame() fires → PRE_TICK / POST_TICK events
│   ├── gpGlobals (server-side globals)
│   └── Physics simulation runs
├── Client (CHLClient)
│   ├── s_EntityList (VClientEntityList003) ← PREDICTED/INTERPOLATED
│   ├── CreateMove() fires
│   └── Rendering pipeline
└── Engine (CEngine)
    ├── hoststate->m_activeGame = TRUE
    └── Frame() loop
```

### Demo Playback
```
Host Machine:
├── Server → DOES NOT RUN. GameFrame() NEVER fires.
│   ├── m_EntPtrArray → STALE/EMPTY (no entities spawned)
│   ├── gpGlobals → may have residual data but no updates
│   └── server->GetPlayer(1) → returns nullptr
├── Client (CHLClient)
│   ├── s_EntityList → ACTIVE (populated from demo network stream)
│   ├── ClientEnt objects are created/updated from demo packets
│   └── Rendering works normally
└── Engine (CEngine)
    ├── hoststate->m_activeGame = FALSE ← KEY DIFFERENCE
    ├── demoplayer->IsPlaying() = TRUE
    └── Frame() fires PRE_TICK/POST_TICK with simulating=false
```

### Critical Observations from SAR's Code

1. **`hoststate->m_activeGame` is FALSE during demos** — This is how SAR and the engine itself distinguish live play from demo playback. [`Engine::isRunning()`](../src/Modules/Engine.cpp#L181-L184) checks `m_activeGame && m_currentState == HS_RUN`.

2. **`GameFrame()` still fires during demos** — But with `simulating = false`. The `PRE_TICK`/`POST_TICK` events are triggered from [`Engine::Frame()`](../src/Modules/Engine.cpp#L435-L438) when demoplayer is active. This means hooks on the server side **still run**, they just don't do anything meaningful because there are no server entities.

3. **`server->m_EntPtrArray` exists but is empty** — The array is allocated when the server module loads, but entities are only spawned into it during `HS_NEW_GAME` / `HS_LOAD_GAME` / `HS_CHANGE_LEVEL_*`. During demo playback, none of these transitions happen for the server.

4. **Client entities are fully populated** — The engine's `CDemoPlayer` replays network packets which create client entities. `client->GetPlayer(1)` works. `client->GetClientEntity(i)` works for any entity that was within PVS during recording.

5. **`server->gpGlobals->pEdicts`** — This pointer to the edict array is set up during server initialization. During demos, it might be null or stale.

---

## 3. Approach A: `map` + `playdemo` — The Dual-Load Trick

### The Idea

**What if we load the map first (which starts the server), and THEN play the demo?**

```
Step 1: map sp_a2_laser_chaining    → Server starts, entities spawn into m_EntPtrArray
Step 2: playdemo my_demo.dem        → Client entities update from demo stream
```

After step 2, we'd have:
- Server entities: present (from the `map` command) — **BUT FROZEN** since `GameFrame()` runs with `simulating=false`
- Client entities: present (from demo playback) — actively updating

### The Problem: `playdemo` Kills the Server

When you run `playdemo`, the engine goes through this sequence:
1. `CDemoPlayer::StartPlayback()` is called
2. Engine transitions to `HS_GAME_SHUTDOWN` (which calls `Session::Ended()`)
3. Then it transitions to demo playback mode with `m_activeGame = false`
4. The server is effectively shut down

This is by design — the engine treats "playing a demo" and "running a game" as mutually exclusive states.

### Could SAR Prevent the Server Shutdown?

**Theoretically, yes.** SAR hooks `SetSignonState` and `Session::Changed()`. We could:

1. Hook the host state transition that shuts down the server during demo loading
2. Prevent `HS_GAME_SHUTDOWN` from destroying server entities
3. Keep `m_EntPtrArray` populated while the demo plays

**But this is EXTREMELY dangerous.** The server's entity system expects to own its entities. If we prevent shutdown:
- Entity memory could be freed by other systems
- Physics simulation references would be dangling
- Server-side think functions might fire on stale data
- The server's tick counter would be desynchronized from the demo's tick counter

### Verdict: ❌ Too fragile. The engine's server lifecycle is deeply intertwined with shutdown semantics.

---

## 4. Approach B: Shadow Server Entity Array

### The Idea

What if SAR maintains its **own** entity array that mirrors the server's `m_EntPtrArray` format, but is populated from client entity data during demo playback?

```cpp
struct ShadowEntity {
    int index;
    std::string className;     // From ClientClass or mapped from demo
    std::string targetName;    // Empty during demo (not networked)
    Vector origin;
    QAngle angles;
    Vector velocity;
    int flags;
    int health;
    bool isActive;
    SolidType_t solidType;
    Vector collMins, collMaxs;
    // ... per-class custom fields ...
};

class ShadowEntityList {
    std::array<ShadowEntity, 2048> entities;
    
    // Populate from server (live play) or client (demo)
    void Update();
};
```

### How It Works

- During **live play**: `Update()` iterates `server->m_EntPtrArray`, reads all fields, populates shadow entities. This is redundant (we could read server entities directly) but gives us a single abstraction.

- During **demo playback**: `Update()` iterates client entities via `client->GetClientEntity(i)`, reads the same fields via `CE(ent)->field<T>("fieldname")`, populates the same shadow struct.

- `InternalObserve()` always reads from `ShadowEntityList` — **zero branching**.

### Pros
- Clean abstraction — single code path for observation
- Shadow entities can be extended with computed fields
- Delta-encoding is trivially applied to the shadow array

### Cons
- We're essentially building a **third entity system** on top of the engine's server and client ones
- Still need the name-mapping table (ClientClass → server classname)
- Still can't get targetnames or collision data during demos
- Performance overhead of copying data into shadow structs every tick

### Verdict: ✅ **This is the cleanest approach.** It doesn't fight the engine; it builds an abstraction above it. The overhead is negligible (we're talking about reading ~50-200 entity positions per tick, microseconds of work).

---

## 5. Approach C: Piggyback on Client Entities with a Unified Abstraction

### The Idea

Instead of creating a shadow array, create a **thin wrapper** that presents a unified interface over both ServerEnt and ClientEnt, using compile-time or runtime dispatch.

```cpp
class UnifiedEntity {
public:
    UnifiedEntity(void* ent, bool isServer)
        : entity(ent), server(isServer) {}
    
    Vector origin() const {
        return server
            ? ((ServerEnt*)entity)->abs_origin()
            : ((ClientEnt*)entity)->abs_origin();
    }
    
    QAngle angles() const {
        return server
            ? ((ServerEnt*)entity)->abs_angles()
            : ((ClientEnt*)entity)->abs_angles();
    }
    
    template<typename T>
    T field(const char* name) const {
        return server
            ? ((ServerEnt*)entity)->field<T>(name)
            : ((ClientEnt*)entity)->field<T>(name);
    }
    
    const char* className() const {
        if (server) return server_mod->GetEntityClassName(entity);
        // For client: get from ClientClass
        return getClientClassName((ClientEnt*)entity);
    }
    
private:
    void* entity;
    bool server;
};
```

### How Entity Iteration Works

```cpp
void ExtractEntities(EntitySnapshot* snapshot) {
    bool isServer = !engine->demoplayer->IsPlaying();
    
    for (int i = 0; i < NUM_ENT_ENTRIES; ++i) {
        void* ent = nullptr;
        
        if (isServer) {
            ent = server->m_EntPtrArray[i].m_pEntity;
        } else {
            ent = client->GetClientEntity(client->s_EntityList->ThisPtr(), i);
        }
        
        if (!ent) continue;
        
        UnifiedEntity ue(ent, isServer);
        // Now everything goes through the same interface
        ExtractEntity(ue, i, snapshot);
    }
}
```

### Key Insight: ServerEnt and ClientEnt Share the Same `field<T>()` Implementation

Looking at [`Entity.hpp`](../src/Entity.hpp), both `ServerEnt` and `ClientEnt` use the `EntField` system. The `field<T>()` template:
1. Looks up the field name in the class's SendTable (server) or RecvTable (client)
2. Falls back to the DataMap if not found in the network table
3. Returns the value at the computed offset

**For all networked fields, the offset computation produces the same result** — because SendTable and RecvTable are mirrors of each other. The engine ensures this during network setup.

This means `SE(ent)->field<int>("m_fFlags")` and `CE(ent)->field<int>("m_fFlags")` will read from the same relative offset within the entity, just from different base pointers (server entity memory vs client entity memory).

### Pros
- No data copying — reads directly from engine memory
- Very thin abstraction, minimal overhead
- Still leverages SAR's battle-tested `field<T>()` system

### Cons
- The `if (isServer)` check is still there, just hidden inside the wrapper
- Class name resolution still needs the mapping table
- Can't get server-only data (targetname, full collision) during demos

### Verdict: ✅ **Good approach.** Slightly less clean than the Shadow approach but more efficient since it avoids copying.

---

## 6. Approach D: Record Entity Snapshots During Live Play, Embed in Demo

### The Idea

**What if we solve the problem at recording time, not playback time?**

SAR already records custom data into demos via `engine->demorecorder->RecordData()`. Entity input events (AcceptInput) are recorded as custom demo data with type bytes `0x03`/`0x04`. We could add a new custom data type for entity snapshots.

During **live recording** (when the server IS running):
```cpp
ON_EVENT(POST_TICK) {
    if (!engine->demorecorder->isRecordingDemo) return;
    
    // Build entity snapshot from server entities
    EntitySnapshot snapshot;
    ExtractAllServerEntities(&snapshot);
    
    // Serialize and embed into the demo file
    std::string data = snapshot.SerializeAsString();
    char header = 0x20; // new custom data type for entity snapshots
    // ... write to demo ...
    engine->demorecorder->RecordData(data);
}
```

During **demo playback**:
```cpp
void EngineDemoPlayer::CustomDemoData(char* data, size_t length) {
    if (data[0] == 0x20) {
        // Entity snapshot!
        EntitySnapshot snapshot;
        snapshot.ParseFromString(std::string(data+1, length-1));
        g_currentEntitySnapshot = snapshot;
    }
}
```

Then `InternalObserve()` during demo playback just reads `g_currentEntitySnapshot` — which was captured from the **server** at recording time.

### Pros
- **Perfect fidelity**: The entity data is exactly what the server had, including targetnames, collision, physics state — everything
- **No client/server abstraction needed**: During demos, we're reading pre-captured server data
- **No PVS issues**: Server sees all entities, not just PVS-visible ones
- **Delta encoding is free**: We can record only changes, just like the network protocol
- **Elegant**: Extends SAR's existing custom demo data mechanism

### Cons
- **Only works for demos WE record**: Existing community demos don't have this data. We'd need to re-record demos with the new SAR version to get entity snapshots.
- **Increases demo file size**: Each tick adds ~400-1600 bytes of entity data. For a 10,000-tick demo at 1KB/tick, that's ~10 MB extra. Not terrible given demos are already on the order of tens of MB.
- **Requires recorder changes**: Need to hook the demo recording pipeline, not just playback.
- **Timing sync**: Custom demo data is associated with specific demo ticks, so the entity snapshot playback tick must exactly match the frame we're observing. SAR already handles this for entity input events.

### Verdict: ✅ **Extremely compelling for OUR use case.** We control the recording pipeline. We're mass-processing community demos, but we could run a "re-recording" pass that loads each demo, enables the new snapshot recording, and re-records the demo with embedded entity data. Or just accept that for community demos, we fall back to client entities.

---

## 7. Approach E: Hybrid — `map` First, Then `playdemo` Overlay

### The Idea

This is the closest to the user's original intuition: "can we keep the server active?"

Instead of fighting the engine's lifecycle, we work with it:

1. **Load the map**: `map sp_a2_laser_chaining` — this starts the server, spawns all entities
2. **Snapshot the initial entity state**: Before playing the demo, capture the full entity list from the server. This gives us classnames, targetnames, collision data, and the initial state of every entity.
3. **Play the demo**: `playdemo my_demo.dem` — server shuts down, but we have the initial snapshot
4. **During playback**: Use client entities for position/angle/velocity updates, but overlay with our initial snapshot for classnames, targetnames, collision bounds, etc.

### Why This Is Actually Clever

The "static" properties of entities — their classnames, targetnames, model names, collision bounds, solid types — **don't change during gameplay**. A `prop_weighted_cube` is always a `prop_weighted_cube`. A fizzler's bounds are set at map load and don't move.

What changes per-tick is:
- Positions, angles, velocities (available from client entities)
- Activation states like `m_bActivated` (networked, available from client)
- Toggle states (networked)

So the approach is:
```
Initial snapshot (from server at map load):
  entity[42] = {class: "prop_portal", name: "@blue_portal", solidType: OBB, ...}

Per-tick client update:
  entity[42] = {pos: (100, 200, 300), ang: (0, 90, 0), m_bActivated: true}

Combined:
  entity[42] = {class: "prop_portal", name: "@blue_portal", pos: (100, 200, 300), ...}
```

### Implementation

```cpp
struct EntityMetadata {
    std::string className;
    std::string targetName;
    std::string modelName;
    SolidType_t solidType;
    Vector collMins, collMaxs;
    int collisionGroup;
};

class EntityRegistry {
    std::unordered_map<int, EntityMetadata> metadata;
    
    // Call this after map load, before playdemo
    void CaptureFromServer() {
        for (int i = 0; i < NUM_ENT_ENTRIES; ++i) {
            void* ent = server->m_EntPtrArray[i].m_pEntity;
            if (!ent) continue;
            
            metadata[i] = {
                server->GetEntityClassName(ent),
                server->GetEntityName(ent) ?: "",
                // ... capture static properties ...
            };
        }
    }
    
    // During demo playback, look up metadata by index
    const EntityMetadata* GetMetadata(int index) const {
        auto it = metadata.find(index);
        return it != metadata.end() ? &it->second : nullptr;
    }
};
```

### The Key Problem: Entity Index Stability

**Do entity indices stay the same between the `map` load and the demo playback?**

**YES, for the most part.** Source engine entity indices are deterministic for a given map load. The BSP's entity lump is parsed in order, entities are created in order, and they get sequential indices. As long as the map version is the same, the same entity will always be at the same index.

**Caveat**: Entities created dynamically during gameplay (spawned cubes, portals placed by the player) will have indices that depend on gameplay events. For these, the index in our initial snapshot won't match the demo. But for all **map-spawned entities** (buttons, doors, fizzlers, laser emitters, turrets, triggers), the indices are stable.

For dynamically spawned entities, we'd fall back to client entity data + `ClientClass::m_pNetworkName`.

### The Workflow

The `sar_harness_playdemo` command would be modified:

```
1. Parse demo header → extract map name
2. Execute "map <mapname>"  → wait for SESSION_START
3. CaptureFromServer()       → snapshot all entity metadata
4. Execute "playdemo <demo>" → server shuts down, demo plays
5. During playback: merge client entity state + cached metadata
```

### Pros
- Gets us **everything**: classnames, targetnames, collision, model info
- Entity indices are stable for map-spawned entities
- Single `InternalObserve()` path: always read from the merged view
- Works for ANY demo, not just ones we recorded

### Cons
- **Slower startup**: Loading the map first adds several seconds
- **Entity index mismatches for dynamic entities**: Portals, spawned cubes
- **Map version sensitivity**: If the demo was recorded on a different map version, entity indices could differ (unlikely for Portal 2 which hasn't been updated in years)
- **Requires careful lifecycle management**: Must capture before `playdemo` kills the server

### Verdict: ✅ **Very promising!** The startup cost is acceptable for batch processing. The entity index stability guarantee for static entities covers 80%+ of the important cases.

---

## 8. Recommendation & Decision Matrix

| Approach | Effort | Fidelity | Works for any demo? | Single code path? | Risk |
|----------|--------|----------|--------------------|--------------------|------|
| **A: Prevent server shutdown** | High | Perfect | ✅ | ✅ | 🔴 Engine crash risk |
| **B: Shadow Entity Array** | Medium | Good | ✅ | ✅ | 🟢 Safe |
| **C: Unified Wrapper** | Low | Good | ✅ | ⚠️ Hidden branching | 🟢 Safe |
| **D: Embed in Demo** | Medium | Perfect | ❌ Only our demos | ✅ | 🟢 Safe |
| **E: Map-then-Demo Hybrid** | Medium | Excellent | ✅ | ✅ | 🟡 Index stability |

### My Top Recommendations

#### For immediate implementation: **Approach C (Unified Wrapper)**

This is the lowest-effort, safest option. Create a `UnifiedEntity` wrapper that dispatches server/client reads through the existing `field<T>()` system. Add a static mapping table for `ClientClass → classname`. Accept that targetnames and collision are unavailable during demos.

**Why**: Ship fast, iterate later. The entity *positions and states* are the most important training signal. Classnames and collision bounds are nice-to-have.

#### For maximum quality: **Approach E (Map-then-Demo) + D (Embed in Demo)**

These are complementary:
- **Approach E** gives us rich metadata for ANY demo by loading the map first
- **Approach D** gives us *perfect* per-tick server state for demos we record ourselves

The workflow becomes:
1. For **mass-processing community demos**: Use Approach E — load map, snapshot metadata, play demo, merge
2. For **our own RL training demos**: Use Approach D — embed full entity snapshots at recording time

Together, they cover all use cases with maximum fidelity.

#### For the world model training pipeline specifically:

Since we're building training data for a world model, **Approach D is the clear winner**. We control the recording pipeline end-to-end. We can:
1. Re-record all community demos through our enhanced SAR that embeds entity snapshots
2. For new RL training runs, entity snapshots are automatically embedded
3. The world model gets *perfect* entity state — positions, classnames, targetnames, collision, physics — at every tick

---

## Deep Dive: Why Approach D + E is the Right Architecture

### For Training Data (demos → rollouts):
```
Community Demo (.dem)
         │
         ▼
[SAR: map <mapname>]  ← Load map, get metadata
[SAR: playdemo <demo>] ← Play demo
         │
         ├─ Per-tick: Read client entities (positions, states)
         ├─ Overlay: Cached server metadata (classnames, collision)
         └─ Output: Rich .rollout file with full EntitySnapshot
```

### For Live RL Inference:
```
[SAR: map <mapname>]  ← Server running
[Agent acts via gRPC]
         │
         ├─ InternalObserve() reads server entities directly
         └─ Output: GameState with EntitySnapshot (server-sourced)
```

### Both produce the SAME EntitySnapshot format. 

The world model receives identical observation structures whether trained on demo data or live data. The only difference is the data source — but the *output schema* is identical.

---

## Concrete Next Steps (if we go with D+E)

### Step 1: `UnifiedEntity` wrapper (1 day)
Create the thin abstraction in `EntityExtractor.hpp`. Add `ClientClass → classname` mapping.

### Step 2: `EntityRegistry` for Approach E (1 day)
Implement metadata capture from server entities. Hook into `sar_harness_playdemo` to do map-load-then-demo.

### Step 3: Custom demo data for Approach D (2 days)
Add a new custom data type (`0x20`) for entity snapshots. Hook `POST_TICK` during demo recording to serialize and embed snapshots.

### Step 4: `EntityExtractor::ExtractSnapshot()` (2 days)
Implement the per-class extraction logic. Use the `UnifiedEntity` wrapper for live play and client entity data during demos, with `EntityRegistry` overlay for metadata.

### Step 5: Protobuf + InternalObserve integration (1 day)
Wire everything into `GameState` and `RolloutStep`.

**Total: ~7 days of focused work.**
