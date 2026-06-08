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
| `prop_floor_button` | stood on it | `m_bButtonState` | net | `1`=pressed, `0`=not | int — pre-filled, **verify** in a button chamber (absent in C1) |
| `prop_button` (pedestal) | — | — | — | — | ⏳ absent in C1 — need `Press` transition |
| `func_weight_button` | — | — | — | — | ⏳ absent in C1 |
| `prop_under_floor_button` | — | — | — | — | ⏳ absent in C1 |
| `prop_floor_cube_button` | — | — | — | — | ⏳ absent in C1 |
| `prop_floor_ball_button` | — | — | — | — | ⏳ absent in C1 |
| `prop_testchamber_door` | not opened | **unknown** | — | only `m_lifeState`/`m_iHealth` exposed at rest | ⏳ present in C1 but never opened — need an `Open` diff; expect `m_toggle_state` `[dm]` |
| `prop_portal` | fired a pair | `m_bActivated` / `m_hLinkedPortal` / `m_bIsPortal2` | net | activated `1`=placed; linked `0xFFFFFFFF`=unlinked else handle of partner; `m_bIsPortal2` `0`=blue/primary `1`=orange | ✅ C1 — also `m_bOldActivatedState` `[net]` |
| `prop_laser_catcher` | beam in/out | *(none on prop)* → child `point_laser_target.m_bPowered` | dm | catcher lit ⇔ its target powered | ✅ C1 — prop itself only has lifeState/health; **power lives on a child target** |
| `prop_laser_relay` | — | *(expected: child target, like catcher)* | — | — | ⏳ absent in C1 — confirm same `point_laser_target` pattern |
| `point_laser_target` | beam hit | `m_bPowered` | dm | `true`=beam striking | ✅ C1 — flips with the beam; **the real sensor behind catcher/relay** |
| `env_portal_laser` (emitter + beam segs) | toggled beam | `m_bLaserOn` | net | `true`=emitting | ✅ C1 — redirected beams spawn **new** `env_portal_laser` ents (segments) |
| `npc_portal_turret_floor` | — | — | — | — | ⏳ absent in C1 — need a knock-over / `SelfDestruct` diff (expect `m_lifeState`/`m_bTipped`) |
| `prop_weighted_cube` | static (read) | `m_nCubeType` (type); `m_bActivated` (?) | dm | `m_nCubeType` `2`=reflective (confirmed, laser map); `m_bActivated` `false` at rest — candidate "beam passing through", unconfirmed | ✅ type; ⏳ confirm `0`=standard on a non-laser cube + `m_bActivated` meaning |
| `prop_monster_box` | static (read) | — | — | — | ⏳ absent in C1 |
| `trigger_portal_cleanser` (fizzler) | static (read) | `m_bDisabled` | net | `false`=fizzler active/on | ✅ C1 — also `m_toggle_state` `[dm]`; `m_bDisabled true`=off |
| `trigger_catapult` (faith plate) | static (read) | — | — | launch vector config | ⏳ absent in C1 |
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

## What to send back

- This table, filled in for the classes you use.
- The `condump` file(s) (raw safety net in case a mapping needs a second look).

From that I lock the status resolver, flag any `[dm ]`-only fields for snapshotter
discovery, and judge whether a field-presence heuristic can retire the hardcoded
`kClassColors` set.
