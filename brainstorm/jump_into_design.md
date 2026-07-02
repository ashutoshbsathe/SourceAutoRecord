# `jump_into` — portal fling verb (design + recon plan)

Part of the portal traversal/drop family (see `portal_unified_grammar_options_dossier.md`).
`pass_through` (walk through a wall portal, stop) shipped; this is its momentum-preserving
sibling for **floor portals**.

## What it is

`jump_into <Pb|Po>` — **jump into a floor portal for a fling.** The player jumps, the game's
portal **funnel** (Source's aim-correction) aligns them to the mouth as they fall in, they
transit, and the harness **freezes at the transit tick with all momentum preserved** — leaving
the agent mid-flight out the linked portal. `wait N` resumes gravity/flight. This is what lets a
frozen model reason about mid-air / fling outcomes.

Contrast:

| | `pass_through` (shipped) | `jump_into` (this) |
|---|---|---|
| portal | any (walks in horizontally) | **floor only** (`normal.z` up) |
| entry | horizontal push into the mouth | **jump + funnel-fall** into the mouth |
| on transit | kill velocity + settle → **stop** | **preserve `m_vecVelocity`, freeze** (mid-flight) |

## The mechanic

1. **Jump** — pulse `key_jump` to get airborne. The engine treats a *jumped* entry differently
   from a walked one (the jump's vertical velocity survives the portal redirect → different exit
   height), so this is load-bearing, not cosmetic.
2. **Hold the down-aim** — continuously `aim_at(portal center)`. Once airborne above the portal
   that *is* a down-look, which arms the **funnel** that pulls the player to the mouth center for a
   clean entry. Must be **re-asserted every tick** (the harness view doesn't hold across ticks), or
   the pitch drifts off the portal and funneling dies.
3. **Run free** — zero movement input, hold the down-aim, advance ticks; gravity + funnel carry the
   player in.
4. **Freeze at the transit tick** — detect the position-jump (emergence at the partner), stop
   advancing, **leave `m_vecVelocity` untouched**. Paused mid-flight for the model.

Two spatial cases, one sequence (geometry differs, verb doesn't):
- **portal at the player's z** — jump *up*, funnel-fall back down into it; the jump buys the height.
- **portal below (ledge)** — jump *off* toward it, funnel in; the long fall is the speed (big fling).

## Decisions (locked)

1. **Separate verb `jump_into`**, not a `jump` flag on `drop_into` — keeps `MacroRequest` clean (it
   reuses `target`, **zero proto change**). Gentle self step-through and object-drop stay `drop_into`,
   a later sibling.
2. **Floor gate = `normal.z`** above a threshold (default ~`0.7`, within 45° of up). Non-floor →
   `NOT_GROUND` (tells the model to use `pass_through` for a wall portal).
3. **The verb does the aligning** — given "reasonable" `(x,y,z)` positioning by the model, the verb
   nudges/aligns, jumps, and lets the funnel finish. The *tolerance* for "reasonable" is what the
   recon measures.
4. **DIED / OOB out of scope** — the verb's only job is the fling. Dying during/after it is not its
   concern; the maps in use have no goo. Revisit if that changes.
5. **Recon first** — characterize funneling empirically before writing the verb (below).

## P0 — the fling recon logger

Funneling is a feel/physics mechanic, so we **let a human do the fling and observe**, rather than
script it (scripting the continuous mid-air down-look is exactly the awkward part). Driven by the
user's instinct to test in a naturally player-controlled env.

- **`sar_harness_fling_recon on|off`** — arms a `POST_TICK` telemetry logger. Run it in a **plain,
  fully-playable Portal 2 + SAR session** (no harness instance flag, so the game isn't paused for
  `Act`). The hook fires every server tick regardless of harness state.
- **What it logs, per tick, to a CSV** (in the artifacts dir): `tick, px,py,pz, vx,vy,vz,
  view_pitch, view_yaw, on_ground, horiz_dist, vert_dist` to the floor portal, and a `transit`
  flag on the emergence position-jump. Floor portal = the placed portal with `normal.z` up.
- **How it's used:** shoot a floor portal + a linked wall portal with the gun, `fling_recon on`,
  run/jump into the floor portal looking down as you fall, `fling_recon off`. Repeat for a few
  variations (walk vs jump, aimed-down vs not, near vs far offset) — each is one clean trajectory.

### What the recon settles (the five unknowns)

1. **Does the funnel engage?** — does `horiz_dist` shrink as the player falls (funnel pulling in).
2. **Alignment envelope** — the largest starting horizontal offset that still transits = how
   precisely the verb must align the player before it jumps.
3. **Jump vs walk** — the exit-velocity difference, measured side by side.
4. **Freeze tick** — the tick the position-jump fires + its magnitude → the verb's freeze precision.
5. **The fling** — emergence pos + velocity → sanity that a fling comes out + what the model sees.

## P0 RESULTS (2026-07-02) — funnel characterized, design locked

Six human flings logged (`fling_recon.csv`). The recon settled every unknown; the mechanic is
concrete and the constants are known.

**The funnel is strong and generous.** Cleanest run (4), descent `horiz_dist` to the mouth per tick:
`20 → 17.7 → 15.5 → 13.3 → 11.3 → 9.2 → 7.0 → 4.9 → 2.9 → 1.2 → 0.7 → 1.4 → 1.5`, and entry velocity
at the portal was `(vx=-1, vy=3, vz=-833)` — **horizontal velocity ≈ 0**. So the funnel *centers* the
player over the mouth (hd→<1) AND zeroes horizontal velocity → a straight drop-in. The load-bearing
input is the **steep down-aim**: pitch held **82–89°** gave sub-1u convergence (runs 3, 4); run 5 at
a shallower ~50° converged slower (min hd 9.9). Continuous `aim_at(portal center)` supplies this.

Per-unknown:

1. **Funnel engages?** — yes, strongly; pulls ~100u offsets to hd≈0 and kills horizontal velocity.
2. **Alignment envelope** — the jump was committed from **hd = 86–125u** in every run and still
   funneled in. The verb needs only **coarse** positioning (~100u), not precision.
3. **Jump** — a clean **`vz = +207`** launch impulse every run; one `key_jump` pulse.
4. **Freeze tick** — transit is a **700–880u single-tick position jump**, trivially detected; the
   post-transit frame already carries the full exit velocity → freeze there = mid-flight w/ momentum.
5. **The fling** — exit = redirected fall, magnitude ∝ fall height. Run 4: entry `vz=-833` →
   **exit `(596, 3, 585)`** (huge up+forward). Same-z (run 2) is gentler (exit total ~334) since the
   jump's +207 is the only height source.

**Two cases confirmed:** *ledge / portal-below* (runs 3, 4, 6) is the money case — jump off toward
it, long fall builds `vz`, funnel centers, big fling. *Same-z* (run 2) works but is gentler (jump-up,
arc back down through the mouth edge).

**Locked mechanic:**

```
validate floor portal (normal.z up)
[approach] go_to(portal) — coarse, within ~100u (edge-guarded → stops at a ledge edge)
pulse key_jump                          # vz = +207
loop, tick-by-tick:
    aim_at(portal center)               # re-assert the steep down-look -> funnel
    advance 1 tick                      # gravity + funnel do the alignment
    if single-tick pos jump > ~128u:    # transit (observed 700-880u; huge margin)
        break                           # FREEZE here, m_vecVelocity untouched -> mid-flight
```

No hand-tuned funnel math: the engine's funnel does the aligning; the verb supplies jump + held
down-aim + free ticks + freeze. It's the `pass_through` skeleton with the stop swapped for a
momentum-preserving freeze.

## P1 SHIPPED (2026-07-02) — verb flings, verified in-game

`jump_into(target)` in `MacroExecutor.cpp`: floor-gate (`normal.z >= 0.7`, else `NOT_GROUND`) →
gentle-forward approach → `key_jump` → re-assert `aim_at(center)` per tick (funnel) → transit-detect
→ freeze with `m_vecVelocity` intact (`FLUNG`). Proto-free (reuses `target`). Same-z verified.

**The locked mechanic's `go_to` approach was wrong for the same-z case** (the build's real finding).
`go_to` kills velocity at ~100u out; a standing jump from there rises only ~36u (`vz=+207`), so the
down-aim to a same-z mouth stays shallow → the funnel never engages. And a *full-speed* run overshoots
the mouth by 100u+ — the funnel can only zero horizontal velocity when you're nearly **over** the
mouth aiming steep-down. What works: a **gentle forward** (`kJumpMoveSpeed=0.5`) that stops ~`40u`
short, so the jump arc lands at the mouth with little horizontal for the funnel to kill. The
ledge/portal-below money case is more forgiving (the fall supplies both height and steep aim).

On a miss, `NOT_ALIGNED` reports `closest Nu from mouth, peak Nu up, ended (x y z)` — the knob to turn
(overshoot vs funnel-didn't-close vs jump-didn't-fire) is readable from the numbers.

## Phasing

- ✅ **P0** — `sar_harness_fling_recon` logger + CSV; funnel characterized (above). SHIPPED.
- ✅ **P1** — `jump_into(portal)` verb: floor-gate + gentle-forward approach + `key_jump` +
  funnel-descent + transit-detect + freeze-with-momentum + miss diagnostics. SHIPPED (above).
- ✅ **P2** — grammar (`jump_into <Pb|Po>`, shares `_check_portal_target` with `pass_through`) +
  `percept_grammar_smoke`; fling verified end-to-end in-game (paused mid-flight, momentum intact).
- **Later** — `drop_into` (gentle self step-through / object-drop of a held cube).

## Open — not blockers

- **Ledge case untested in the verb.** Same-z is verified; the portal-below fling (the money case)
  should just work (more funnel margin) but hasn't been run through the verb — confirm when a ledge
  chamber is handy.
- **Jump timing:** an extra jump *at entry* (vs only at launch) is a speedrun refinement — not v0.
- Object-drop mechanics (track the cube's transit, not the player's) — the later `drop_into`.
