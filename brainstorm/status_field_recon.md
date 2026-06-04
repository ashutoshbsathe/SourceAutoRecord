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
| `prop_floor_button` | stood on it | `m_bButtonState` | net | `1`=pressed, `0`=not | int |
| `prop_button` (pedestal) | | | | | |
| `func_weight_button` | | | | | |
| `prop_under_floor_button` | | | | | |
| `prop_floor_cube_button` | | | | | |
| `prop_floor_ball_button` | | | | | |
| `prop_testchamber_door` | | | | | |
| `prop_portal` | | | | | (m_hLinkedPortal, m_bActivated, m_bIsPortal2) |
| `prop_laser_catcher` | | | | | |
| `prop_laser_relay` | | | | | |
| `point_laser_target` | | | | | |
| `env_portal_laser` (emitter) | | | | | likely static |
| `npc_portal_turret_floor` | | | | | |
| `prop_weighted_cube` | static (read) | `m_nCubeType` | | | 0=std, 2=reflective? confirm |
| `prop_monster_box` | static (read) | | | | cube variant |
| `trigger_portal_cleanser` (fizzler) | | | | | likely static |
| `trigger_catapult` (faith plate) | static (read) | | | | launch vector config |
| `prop_tractor_beam` (funnel) | | | | | direction |

---

## What to send back

- This table, filled in for the classes you use.
- The `condump` file(s) (raw safety net in case a mapping needs a second look).

From that I lock the status resolver, flag any `[dm ]`-only fields for snapshotter
discovery, and judge whether a field-presence heuristic can retire the hardcoded
`kClassColors` set.
