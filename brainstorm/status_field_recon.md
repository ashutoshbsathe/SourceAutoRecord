# Status-Field Recon Protocol

**Goal:** for each puzzle element class, find the engine field that encodes its
semantic status (button pressed / door open / catcher powered / turret alive /
portal linked), and record whether that field is networked (`[net]` — the
`EntitySnapshotter` SendTable walk captures it) or datamap-only (`[dm ]` — the
snapshotter currently misses it).

Output of this recon = the input for writing the C++ status resolver and for
deciding whether the snapshotter needs datamap discovery.

This is a **one-time** sweep. Fill in the table at the bottom and send it back.

---

## Key idea

A status field is one that **changes across a state transition**. So the unit of
work is: capture a baseline → flip one (or several) elements → diff. The
`sar_harness_dump_fields` command does the diffing and tags each field net/dm.

Coverage is **per class, not per instance** — one `prop_button` result covers
every pedestal button in the game. You only need the classes that appear in
**your** chamber set; skip the rest.

---

## Command reference

| Command | What it does |
|---|---|
| `sar_harness_dump_fields` | Full dump of every puzzle entity's status-bearing fields + **stores a baseline**. Lists targetnames. |
| `sar_harness_dump_fields diff` | Prints only fields that changed since the last dump (then re-baselines). Run it after a transition. |
| `sar_harness_dump_fields reset` | Clears the baseline. |

A diff line reads: `m_bButtonState [net] : 0 -> 1` → field, source tag, old → new.

The cleanest way to force a transition without solving the chamber is
**`ent_fire <targetname> <Input>`** (sv_cheats is always on). Targetnames are
printed by the full dump.

---

## Prereqs

1. `make` and reload the plugin (the command ships in `sar.so`).
2. Load a chamber from your set.

---

## The loop (per chamber)

1. `sar_harness_dump_fields` — baseline. Skim the headers to see which classes
   this chamber contains.
2. Flip one or more elements. Either play through naturally, or:
   - `ent_fire <button> Press`
   - `ent_fire <door> Open`
   - `ent_fire <catcher> Power` (or route the laser in)
   - `ent_fire <turret> SelfDestruct` (or knock it over)
3. **Wait for animations to settle** (a door passes through transient states),
   then `sar_harness_dump_fields diff`.
4. Read off the changed field(s) per entity → fill the table.
5. Repeat 2–4 until every class in this chamber is covered.
6. `condump` to save the console to a file; keep it.

Static elements (cube, faith plate, often emitter/fizzler) need **no
transition** — read their values straight from the step-1 baseline.

**Portals** are a two-step: dump baseline → fire blue → `diff` (blue appears as
`(new)`) → fire orange → `diff` (watch `m_hLinkedPortal` on blue change from an
invalid handle to a real one when the pair links).

---

## Gotchas

- Dump at the **settled end-state**, not mid-animation.
- If the field you expect isn't in a diff, it may be named oddly — the heuristic
  sweep should still surface it; if not, run `sar_dump_server_datamap` and grep
  the class for likely names, then tell me.
- A `[dm ]` tag is important to flag — it means the snapshotter won't see that
  field as-is and we'll need to extend discovery.

---

## Results table (fill this in)

One row per class you actually use. `Status field` = the field that flipped;
`Meaning` = how to read it (e.g. `!= 0` means active).

| Class | How flipped | Status field | net/dm | Meaning | Notes |
|---|---|---|---|---|---|
| `prop_floor_button` | cube on it | `m_bButtonState` | net | `1`=pressed, `0`=not | ✅ C3 — **verified**: flips `0→1` when a cube lands on it. (C4: also carries `m_nSequence`=3 at rest, but `m_bButtonState` is the clean bool — use it.) |
| `prop_button` (pedestal) | `PressIn` | `m_nSequence` | net | `0→3` = pressed | ✅ C4 — **resolved**: no pressed-bool (`m_bButtonState`/`m_bPressed` absent); pressed = anim sequence `0→3` (on `[64]`; `[20]`/`[76]` didn't register — timing or non-actuating). `m_bLocked`[dm] = lock only. |
| `func_weight_button` | — | — | — | — | ⏳ absent in C1 |
| `prop_under_floor_button` | — | — | — | — | ⏳ absent in C1 |
| `prop_floor_cube_button` | — | — | — | — | ⏳ absent in C1 |
| `prop_floor_ball_button` | — | — | — | — | ⏳ absent in C1 |
| `prop_testchamber_door` | `Open`+`Close` fired | **`m_nSequence` RULED OUT** (constant =1 through both) | — | — | ⏳ C5 (deferred): door state is *not* `m_nSequence`. Either **client-side-animated** (server anim fields static) or the `@exit_door` is **relay/solve-gated** so `ent_fire Open` is a no-op. **Deferred to the observation-rework phase** — lowest-value field (visually obvious; success is trigger-based, not door-field-based). |
| `prop_portal` | fired a pair | `m_bActivated` / `m_hLinkedPortal` / `m_bIsPortal2` | net | activated `1`=placed; linked `0xFFFFFFFF`=unlinked else handle of partner; `m_bIsPortal2` `0`=blue/primary `1`=orange | ✅ C1 — also `m_bOldActivatedState` `[net]` |
| `prop_laser_catcher` | beam in/out | *(none on prop)* → child `point_laser_target.m_bPowered` | dm | catcher lit ⇔ its target powered | ✅ C1 — prop itself only has lifeState/health; **power lives on a child target** |
| `prop_laser_relay` | beam in/out | *(none on prop)* → child `point_laser_target.m_bPowered` | dm | relay lit ⇔ its target powered | ✅ C2 — **confirmed identical to the catcher**; prop itself exposes only `m_lifeState`/`m_iHealth` |
| `point_laser_target` | beam hit | `m_bPowered` | dm | `true`=beam striking | ✅ C1 — flips with the beam; **the real sensor behind catcher/relay** |
| `env_portal_laser` (emitter + beam segs) | toggled beam | `m_bLaserOn` | net | `true`=emitting | ✅ C1 — redirected beams spawn **new** `env_portal_laser` ents (segments) |
| `npc_portal_turret_floor` | static (read) | `m_lifeState` (alive); `m_bSelfDestructing`; `m_iHealth` | `m_lifeState` **net**, rest dm | `m_lifeState 0`=alive; `m_bSelfDestructing true`=blowing up | ✅ C2 at-rest — also `m_iHealth`=10, `m_bEnabled`=true, `m_bLaserOn`[net]=its aim beam; ⏳ no `m_bTipped`/tip captured (never knocked over) |
| `prop_weighted_cube` | on a button | `m_nCubeType` (type); `m_bActivated` (on-button) | dm | `m_nCubeType`: `0`=standard, `2`=reflective; `m_bActivated true`=cube is **pressing a button** | ✅ C3 — `0`=standard confirmed; **`m_bActivated` resolved**: flips `false→true` in lockstep with the button's `m_bButtonState` |
| `prop_monster_box` | static (read) | — | — | — | ⏳ absent in C1 |
| `trigger_portal_cleanser` (fizzler) | static (read) | `m_bDisabled` | net | `false`=fizzler active/on | ✅ C1 — also `m_toggle_state` `[dm]`; `m_bDisabled true`=off |
| `trigger_catapult` (faith plate) | static (read) | `m_bDisabled`; `m_toggle_state` | dm | `m_bDisabled false`=active | ✅ C2 — **caveat: MANY per map** (mostly unnamed safety-net catapults, not player faith plates) → annotation needs a name filter, else view-flood |
| `prop_tractor_beam` (funnel) | — | — | — | direction | ⏳ absent in C1 |

---

## Findings so far

### Chamber 1 — `sp_a2_triple_laser` (condump000)

Covered outright: **portals, laser emitters, catchers + their targets, fizzler, reflective cube.** Buttons, door open-state, relay, turret, faith plate, funnel, monster box are **not present / not transitioned** — see the ⏳ rows.

Two findings shape the status resolver:

1. **Catcher/relay power is not on the prop — it's on a child `point_laser_target`.** `prop_laser_catcher` exposes only `m_lifeState`/`m_iHealth`; the powered bool (`m_bPowered`) lives on a *separate* `point_laser_target` that flips `false→true` when the beam connects (C1 had 3 catchers + 3 targets, targets tracked the beams). The resolver must **associate each catcher/relay with its target** (by parent or proximity). *Open design question.*

2. **Most status fields are datamap-only `[dm]`, so the SendTable-only snapshotter currently misses them.**
   - `[net]` (snapshotter sees today): portal `m_bActivated`/`m_hLinkedPortal`/`m_bIsPortal2`/`m_bOldActivatedState`, emitter `m_bLaserOn`, fizzler `m_bDisabled`.
   - `[dm]` (snapshotter blind today): cube `m_nCubeType`/`m_bActivated`, target `m_bPowered`, fizzler `m_toggle_state`, and (expected) door open-state.
   - **→ before these are observable over gRPC, the snapshotter must *register* them.** Subtlety worth pinning down: the *read* path (`EntField::getServerOffset`, used in `Update()` and by this recon command) already resolves datamap **and** SendTable — a registered datamap field reads fine. The gap is *discovery*: Phase 4 ([phase4_sendtable_discovery.md](phase4_sendtable_discovery.md)) builds each class's field set by walking **SendTables only**, so datamap-only fields are never registered. The fix is **not** full datamap discovery (the [post-mortem](phase4_fixing_slowness_and_crashes.md) warns against that scope) but a small **curated per-class status set** seeded from this very table — register exactly the `[dm]` fields above, let `getServerOffset` read them. Follow-up scoped in [phase4_sendtable_discovery.md](phase4_sendtable_discovery.md).

---

### Chamber 2 — `sp_a2_laser_chaining` (condump001/002)

Targeted relays, turrets, faith plates, panels. Closes 4 pending rows and surfaces
one structural pattern.

1. **Relay == catcher, confirmed.** `prop_laser_relay` ("catcher_1"/"catcher_2")
   exposes only `m_lifeState`/`m_iHealth` on the prop; power lives on a child
   `point_laser_target.m_bPowered`, which flips `false→true` with the beam — the
   *same* mechanism as the catcher (which was also present, "catcher_3"). The map
   had 3 laser devices ↔ 3 `point_laser_target`s — the resolver's catcher/relay →
   target association holds for both classes.
2. **The structural lesson: catcher / relay / door are all `DT_BaseAnimating`
   props with no clean status bool on the prop itself.** Their meaning lives
   *elsewhere* — on a **child entity** (lasers → `point_laser_target`) or in
   **animation** (door). The status resolver can't just read a field off these
   props; it needs a per-class strategy.
3. **Door is the hard one.** `prop_testchamber_door` resolved *only*
   `m_lifeState`/`m_iHealth`; `m_toggle_state`/`m_bOpen`/`m_bLocked` don't exist on
   it. As a `DT_BaseAnimating`, open-state is animation-driven — likely
   `m_nSequence`/`m_flCycle`. Still unresolved; needs a dedicated probe or a real
   `Open` transition (we never opened a door in this chamber).
4. **Turret at-rest captured.** `m_lifeState` **[net]** (0=alive) is the clean
   aliveness signal; `m_bSelfDestructing`/`m_iHealth`/`m_bEnabled` are `[dm]`.
   `m_bLaserOn`[net] is the turret's *own aim beam*, not a puzzle laser. No tip/
   destruct transition captured.
5. **Faith plates flood.** `trigger_catapult` carries `m_bDisabled`/`m_toggle_state`
   `[dm]` (static launch config), but the map has **a dozen+** of them — mostly
   unnamed safety-net catapults (`get_box_saftey_catapult`, the "<no name>" ones),
   not the 2-3 player-facing faith plates (`catapult1`/`catapult2`). Annotation
   must filter to named/visible ones or it's a `func_brush`-style view-flood.
6. **Cubes:** all 3 are `m_nCubeType=2` (reflective — laser map); `m_bActivated`
   `false` at rest, never flipped. Standard cube (`m_nCubeType=0`) + `m_bActivated`
   meaning still need a non-laser chamber.
7. **Two notes for the entity/mark model (not status, but learned here):**
   redirected beams spawn **new** transient `env_portal_laser` segment entities
   (fresh indices each time) — don't assign marks to beam segments. And flip panels
   are `ramp_90deg_*` `prop_dynamic` driven by `makeramp_*` animation sequences —
   confirms category-B (geometry/animation, name-matched), not a status entity.

### Chamber 3 — Cube Momentum (condump003)

Cube-momentum chamber ([wiki.portal2.sr/Cube_Momentum](https://wiki.portal2.sr/Cube_Momentum)):
buttons + cubes + doors. Closes the button-verify and the cube questions; the door
stays the holdout.

1. **Floor button verified.** `prop_floor_button.m_bButtonState` is `[net]` and flips
   `0→1` when a cube lands on it. The pre-filled guess was right.
2. **Cube `m_bActivated` resolved — it's the *on-button* flag.** Every button
   `0→1` was paired, in the same diff, with a cube's `m_bActivated false→true`
   (`button_1`↔`cube_dropper_2` cube, `button_2`↔`cube_dropper_1` cube). So
   `m_bActivated` on `prop_weighted_cube` = "this cube is currently pressing a
   button." Useful corollary: cube-on-button is readable from **either** side.
3. **Standard cube confirmed:** `m_nCubeType=0` (vs `2`=reflective in C2).
4. **Pedestal button still half-open.** `prop_button` baseline shows only
   `m_bLocked`[dm]; its pressed-state field never surfaced (none was pressed). Needs
   an `ent_fire <prop_button> Press` diff.
5. **Door still opaque.** Two `prop_testchamber_door` present, never opened — still
   only `m_lifeState`/`m_iHealth`. Unchanged from C2; remains the white whale.
6. **Entity-model notes:** (a) map-placed *named* portals exist (`room_1_portal`):
   they spawn with `m_bActivated=false` and activate when linked, so not every
   `prop_portal` is player-created. (b) Cube droppers spawn cubes that **share a
   targetname** (`cube_dropper_1-…`) but get **fresh entity indices** per drop —
   confirms mark identity must key on `(index, serial)`, never targetname.

### condump004 — `m_nSequence` is the button/door state (the resolver pattern)

Added `m_nSequence` to the probe; it resolves the pedestal button and confirms the
door's mechanism, and in doing so settles the **status-resolver design**.

1. **Pedestal button resolved.** `prop_button` has no pressed-bool; `PressIn` flips
   `m_nSequence 0→3` (`[64]`). `m_bLocked` is lock only. (`[20]`/`[76]` didn't
   register — diff-timing or non-actuating; revisit only if needed.)
2. **Door field found, values pending.** With the probe on, `prop_testchamber_door`
   shows `m_nSequence` (=1 at capture) — confirms it's animation, not a bool. But no
   `Open`/`Close` was fired, so the open-vs-closed sequence values are still unknown.
   **One capture left** (see below).
3. **The resolver pattern is now settled.** Category-A buttons/doors split two ways:
   - **Clean bool (lone exception):** `prop_floor_button.m_bButtonState` [net] `0/1`.
   - **Animation-driven (`m_nSequence`):** pedestal `prop_button`,
     `prop_testchamber_door`, and (A2 taxonomy) flip panels.
   `m_nSequence` is **per-model**: pedestal "pressed"=3, floor button "at rest"=3 —
   same number, opposite meaning. So the resolver needs a **per-class
   sequence→state map**, not a universal rule. `m_nSequence` is `[net]`, so the
   snapshotter already captures it — only the value→meaning table is missing.

#### Door capture (condump005) — `m_nSequence` ruled out, door deferred

Fired `Close` then `Open` on `prop_testchamber_door`; `m_nSequence` stayed constant
at `1` through both (`recon diff: 0 changed`). So **the door's open-state is not
`m_nSequence`** — nor any field the probe sees. Two candidate explanations:

- **Client-side animation:** the *server* entity's anim fields don't move while the
  client plays the visual open/close → no server field we probe will ever reflect it.
- **Logic-gated `@exit_door`:** PeTI exit doors open on *puzzle solve* via a relay, so
  a direct `ent_fire Open` may be overridden and the door never actually moves.

**Decision: defer.** The door open-state is the lowest-value status field — visually
obvious in-frame, and success detection is trigger/reach-based, not door-field-based.
Resolving it (client-side read, or finding the controlling logic bool) is far cheaper
during the **observation-rework phase**, inside the snapshotter with the full per-class
schema, than via more blind `ent_fire` probes. Every *other* category-A element is
resolved, so this does not gate the rework.

*(If we do want it later: a "dump every live field for the entity under the crosshair"
recon mode would settle it in one shot — or check `m_bClientSideAnimation` on the door
to confirm the client-anim theory.)*

## Save/load recon (new — 2026-06-09)

Two quick checks while you're in a chamber, for the `anchor`/`restore` undo
mechanism (which reuses Source's engine `save`/`load` — see grammar doc §4):

1. **Does `load` preserve a held cube + placed portals?** Pick up a cube, fire a
   blue+orange pair, `save undo`, walk somewhere, `load undo` → is the cube still
   held, and are *both* portals still placed at the same spots? (Quicksave lore
   says yes — but this is the load-bearing assumption for the whole undo design,
   so verify it.)
2. **What events does `load` fire?** Watch for a `SESSION_START` / level-reload on
   `load`. This is only a sanity check — the **canonical mark numbering**
   (phased-plan A3 revision) makes marks load-invariant *regardless* — but confirm
   `load` doesn't trigger a surprise re-warmup or otherwise disturb the harness.

## What to send back

- This table, filled in for the classes you use.
- The `condump` file(s) (raw safety net in case a mapping needs a second look).

From that I lock the status resolver, flag any `[dm ]`-only fields for snapshotter
discovery, and judge whether a field-presence heuristic can retire the hardcoded
`kClassColors` set.
