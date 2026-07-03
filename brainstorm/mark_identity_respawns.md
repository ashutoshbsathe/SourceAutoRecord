# Respawn-stable entity marks (droppers)

**Problem.** `MarkTable` keys marks on `(entindex, serial)`, so every respawn
mints a fresh mark. A dropper parked over goo (the "infinite dropper" trick,
`workshop/1805355826134795545/1615598981`) inflates the cube's mark without
bound; worse, catching a cube spawns a spare in the tube and the model then
sees TWO cube marks whose numbers shuffle across generations. Three harms:
plans reference marks that die mid-plan (`pick_up 14` → BAD_MARK), the percept
implies new objects where the puzzle means "the same cube", and numbers
inflate into noise.

## Recon facts (2026-07-03, all in-game / corpus-verified)

- **Respawned cubes reuse the exact template name** (`cdN-box`, no fixup
  suffix) at recycled entity indexes. Identity = `classname:targetname`
  (a trailing `&NNNN` is stripped anyway as a safeguard for
  preserve-names-off templates).
- **There is no dissolve state on a goo kill.** Per-tick lifecycle logging
  showed every field frozen from spawn to remove; the kill is a direct
  removal, and the dropper spawns the replacement **0–2 ticks before** the
  old cube is removed. (Fizzler kills do dissolve visibly for ~2 s — the
  corpse legally holds its mark meanwhile.) `FL_DISSOLVING` (SDK lore) never
  fired — second lore-instead-of-recon burn this arc after the BSP plane
  `side` bit.
- **The dropper broadcasts both lifecycle moments as entity inputs**, which
  SAR already intercepts via the `AcceptInput` hook:
  - spawn: `point_template OnEntitySpawned → cdN-box* FireUser4` — the
    template pings the newborn cube itself. Corpus: 226 instances across all
    223 dropper-bearing workshop maps; nothing else pings cubes with it.
  - release: the tube holds the cube on a clip brush and releases by
    `Disable`-ing it. Corpus: 376 such outputs.
- Same-name multiplicity is real for statics (one map has 7 live fizzler
  brushes sharing a name), so mark reuse must be vacancy-gated.

## Design (shipped 2026-07-03)

Three cooperating rules in `MarkTable`, all engine-event-driven:

1. **Dropper suppression.** A **never-marked** entity that receives
   `FireUser4` is tagged dropper-held and carries **no mark** — a cube inside
   the tube is not an affordance (can't be reached), so the percept omits it
   entirely (Python already filters `mark > 0`; annotate draws nothing for
   mark 0). The never-marked gate matters: the template pings a *name
   wildcard* (`cdN-box*`), so every same-name veteran in the chamber — the
   cube in the player's hands, a cube on a button — receives the same input
   and must be left alone; only the newborn can be unmarked at ping time
   (spawn and ping share one event-queue pass, no rebuild interleaves).
   **Release = the cube receiving `FireUser1`.** The dropper's release chain
   (`spawn_man OnChangeToAllTrue`) pings `cdN-box*` with FireUser1 at +0.01 s
   — corpus: 223 manager senders ≈ one per dropper — before the open
   animation (+0.1 s) and the clip disable (+1.1 s). For an out veteran the
   same ping means "dissolve yourself" (`done_trig` installs
   `OnUser1 → self:Dissolve` after exit), so only suppressed keys react.
   Direct-to-entity, zero geometry. Two earlier release designs failed and
   are recorded as warnings: clip-`Disable` proximity (the dropper Disables
   its fade brush beside the tube at settle → released at settle) and
   support-under-the-cube (the clip disable trails release by 1.1 s and its
   enable/disable timing interleaves with the next spawn). The dropper-open
   `SetAnimation item_dropper_open` (+0.1 s, 455 in corpus) is the documented
   fallback signal if some template ever lacks the FireUser1 ping.
   Backstop: a suppressed entity that moves **768u** (six voxels) from its
   tag origin is released regardless of wiring — stuck-insurance only.
   The threshold is deliberately huge: the newborn spawns at the template
   cube's Hammer origin ~150u ABOVE the tube seat, so its in-housing fall
   alone is ~134–156u (tracer-measured; a 128u "one voxel" backstop released
   every tube cube on arrival and stole the release from FireUser1 — the
   v3 bug). `sar_harness_mark_debug 1` traces every suppress/release/assign
   decision, including no-op FireUser1 arrivals.
2. **Name-keyed inheritance.** A newly marked entity whose
   `classname:targetname` previously held a mark inherits it **iff no live
   entity owns it**. The dropper's "same" cube keeps one number forever.
3. **Inherit grace.** If the identity-mark is still held (fizzler corpse
   dissolving ~2 s), the newcomer stays unmarked for up to 150 rebuilds
   (~2.5 s) — invisible, it's just a released cube falling — then falls back
   to a fresh mark (genuine same-name coexistence must stay distinct). The
   initial cohort never defers, so same-name statics keep their immediate
   fresh marks.

Invariant preserved throughout: **a mark never moves off a living entity.**

Net behavior: continuous goo loop → one mark forever. Catch → held cube
keeps its mark, the tube spare is invisible, and the eventual replacement
inherits the same number. Two simultaneously *free* same-name cubes (a map
that dispenses multiples) still get distinct marks after the grace.

Rejected: 48u spawn-displacement as the *primary* in-dropper detector (pure
heuristic; demoted to the never-lies backstop) · dropper-entity detection by
name/geometry (template naming already burned us: `angledPanelN` vs `apN` vs
`fpN`) · per-identity mark pools (bounded the numbers but kept the confusing
`{38,41}` shuffle the suppression removes outright) · marking dissolving
corpses out (no such state exists on the goo path).

## Status

**✅ VERIFIED in-game (2026-07-03, after four iterations, each user-caught or
tracer-diagnosed):** goo loop → one stable number; catch → the tube spare is
invisible (no box/label/mark) and the eventual release inherits the same
number; genuine multi-cube coexistence (the map's double-dispense glitch)
gets honest distinct marks. `sar_harness_dump_fields` still lists tube cubes
(recon set is mark-independent); `sar_harness_mark_debug 1` stays in as the
cvar-gated tracer.

**Smoke — WRITTEN (2026-07-03), in-game run pending.** `agentloop_smoke`
`check_respawn_marks` (pinned to the dropper map): arms the goo loop via
`ent_fire @relay_spawn_on_entrance trigger`, asserts one mark per cube
identity across ≥2 respawn cycles, then emulates the catch — `ent_setpos`
yanks a just-released cube to the player, and the tube spare must stay
mark-0 (raw snapshot) while the veteran keeps its number and the percept
shows exactly one cube for that identity. Run:
`uv run python py/agentloop_smoke.py --only dynamic_panels,respawn_marks`.
