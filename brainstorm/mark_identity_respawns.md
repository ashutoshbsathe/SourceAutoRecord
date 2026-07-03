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

## Recon results (2026-07-03, in-game dumps across a full respawn cycle)

- **R1 — NO fixup suffix.** The respawned cube's name is verbatim `cdN-box`
  every cycle (at a recycled low entity index — `[769]→[87]`). Canonical
  identity is simply `classname:targetname`; the `&NNNN` strip stays as a
  zero-cost safeguard for preserve-names-off templates elsewhere.
- **R2 — inconclusive and possibly unnecessary.** No dump caught a dissolving
  intermediate: the goo kill removes the old entity before the replacement
  registers at human dump speed (`m_lifeState` 0 throughout). Dissolve
  exclusion (B2) is therefore gated on actually observing the two-mark
  oscillation after B1, not built preemptively.
- **Bonus:** same-name multiplicity is real and common (`rfiz318-fiz` ×7 live
  fizzler brushes, doors duplicated) — the "inherit only if the mark has no
  live owner" gate is load-bearing.

## Build

- **B1 ✅ SHIPPED (2026-07-03)** — `CanonicalName` (classname:targetname,
  fixup-stripped) + inherit-if-vacant branch in `MarkTable::RebuildFromWorld`;
  `nameMark` records each identity's first mark for the episode.
- **B2 ✅ SHIPPED (2026-07-03) — spawn-grace, after the recon overturned the
  dissolve theory.** Post-B1 the mark alternated `38 → 42 → 38 → 44`. First
  fix attempt (reject `FL_DISSOLVING`, bit 28 from SDK lore) **did not fire**
  — second SDK-lore assumption burned this arc after the plane `side` bit.
  The per-tick lifecycle logger (`sar_harness_dissolve_recon <on|off>`) then
  settled it: **there is no dissolve state at all.** Every field is frozen
  from spawn to remove (`m_fFlags` constant `FL_OBJECT`, `m_lifeState` 0,
  `m_flDissolveStartTime` 0, `m_takedamage` 1); the goo kill is a direct
  removal. The real overlap is a **spawn-before-remove race of 0–2 ticks**:
  the dropper spawns the replacement, the old cube is removed ~2 ticks later
  (`SPAWN t1224 → REMOVE t1226`, every cycle). Any frame's mark rebuild that
  lands in that ~33 ms window sees both cubes and permanently mints a fresh
  mark — landing about every other cycle, hence the alternation.
  **Fix: inherit-grace.** A newcomer whose identity-mark is still held by a
  live predecessor stays *unmarked* for up to 10 rebuilds (~166 ms, 5× the
  race) instead of minting a fresh mark; genuine same-name coexistence times
  out to fresh, and the initial cohort never defers (same-name statics keep
  today's behavior). The bit-28 guard is deleted (dead code). Cost: a fresh
  respawn is markless for ≲166 ms — invisible at macro-step timescales.
- **B3** — `agentloop_smoke`: respawn-stability assertion on a dropper map
  (owed together with the dynamic-panel assertion).

Side benefit: name-keyed inheritance also makes marks stable across
save/load (serials churn there too), which the checkpoint-deferred grammar
will eventually want.
