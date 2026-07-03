# Respawn-stable entity marks (the infinite-dropper churn)

**Problem (2026-07-03, found on `workshop/1805355826134795545/1615598981`).**
A dropper parked over goo respawns its cube forever. `MarkTable` keys marks on
`(entindex << 16 | serial)` and a respawn is a new serial **by design** ("marks
only grow, never move"), so the respawning cube's mark increments without
bound. For the frozen-LLM percept this is three separate harms: the model's
plan references a mark that dies mid-plan (`pick_up 14` → BAD_MARK), the
percept implies a *new object* appeared when it's semantically the same cube,
and mark numbers inflate into noise over a long episode.

## The identity that survives respawns (recon facts)

PeTI cube droppers are `cdN-*` instances: `prop_weighted_cube "cdN-box"` lives
inside `point_template "cdN-cube_template"` with **spawnflags=0 → name fixup
ON**, and the map's own I/O targets `cdN-box*` (wildcard). So every respawned
cube is named `cdN-box&0000`, `cdN-box&0001`, … — same base name, rotating
fixup suffix. (Templates with spawnflags=2, e.g. `@portalgun`, preserve names
verbatim — no suffix. Both cases normalize the same way.)

**Canonical identity = `classname + targetname` with a trailing `&<digits>`
stripped.** This is exactly "tied to the dropper" — the base name IS the
dropper instance's cube slot — without any dropper-specific code, and it
covers turret droppers and any other templated respawner for free.

## Design — name-keyed mark inheritance in MarkTable

1. Alongside `assigned`, keep `nameMark: canonicalName -> mark`, recorded when
   a named entity first gets a mark.
2. When an unseen `(index, serial)` appears: if its canonical name has a
   recorded mark AND no live entity currently holds that mark, **inherit it**
   instead of `nextMark++`. Otherwise a fresh mark (two live same-name cubes
   stay distinct). Unnamed entities keep pure serial behavior.
3. The existing invariant is preserved: a mark never moves off a *live*
   entity; inheritance only recycles marks whose owner is gone.

**Overlap window.** The old cube dissolves for ~1–2 s while the replacement
drops, so at inherit time the old mark can still be live. Without further
work the marks *oscillate between two values* (14 ↔ 15) — already bounded,
churn killed. The polish that makes it a single stable mark: **stop marking
dissolving cubes** (percept-honest — a dissolving cube is no longer an
affordance; it can't be grabbed). Needs one recon to find the dissolve
signal (R2 below); then the fizzled cube unmarks instantly and the
replacement inherits cleanly.

Rejected alternatives: mark the dropper and expose a "current cube" link
(more percept surface, breaks when the cube is carried away, doesn't cover
non-dropper respawns) · position/state-canonical renumbering (the original
A3 idea — violates "marks never move mid-episode", long since decided).

## Recon before build (2 min in-game, on the infinite-dropper map)

- **R1 — fixup format.** `sar_harness_dump_fields` prints `[i] classname
  "name"` headers; run it across two respawn cycles and read the cube's name
  both times. Expected `cdN-box&0000` → `cdN-box&0001`; confirms the strip
  rule.
- **R2 — dissolve signal.** `sar_harness_dump_fields` (baseline), let the
  cube hit the goo, `sar_harness_dump_fields diff` mid-dissolve. Whatever
  field flips (m_lifeState / dissolve-related) becomes the "don't mark"
  filter.

## Build (after R1/R2, ~40 LOC)

- **B1** — canonical-name map + inherit branch in `MarkTable::RebuildFromWorld`
  (strip rule from R1). *Verify:* infinite dropper cycles, `panels`/marks dump
  shows the cube mark bounded (oscillating at worst).
- **B2** — dissolve exclusion in `IsHarnessMarkedEntity` (field from R2).
  *Verify:* the cube keeps ONE mark across arbitrary many respawns; the
  dissolving corpse is unmarked the tick it fizzles.
- **B3** — `agentloop_smoke`: respawn-stability assertion on a dropper map.

Side benefit: name-keyed inheritance also makes marks stable across
save/load (serials churn there too), which the checkpoint-deferred grammar
will eventually want.
