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
| on transit | kill velocity + settle → **stop** (removed in drop_into D1 — see the family invariant below) | **preserve `m_vecVelocity`, freeze** (mid-flight) |

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
- **Next** — `drop_into` (below).

## `drop_into` — the gentle sibling (BUILT 2026-07-04; in-game verify pending)

**Family invariant (all portal traversal verbs): never destroy the emerged momentum.**
No verb zeroes `m_vecVelocity` — that would erase exactly the state the frozen model is
supposed to reason about. Clearing the movement INPUT stays everywhere — that's "stop
pressing forward", not physics tampering. Two shapes then follow from what the verb leaves
you doing:
- **The fling verbs (`jump_into`, `drop_into`) end AT the transit, frozen mid-flight** —
  they advance no further ticks, so the harness tick-gate IS the pause; `wait N` resumes
  the fall with momentum intact.
- **`pass_through` arrives grounded** (walk in, walk out) so it runs its short settle —
  ground friction bleeds the walk-speed naturally, which is physics, not zeroing. It must
  still not *zero* velocity (it did, shipped before this invariant; **D1 removes that**),
  so an airborne emergence keeps its momentum.

`drop_into <Pb|Po> [mark]` — enter a **floor** portal WITHOUT a jump (self), or drop the
**held** object into it (object). The only difference from `jump_into` is the ENTRY: no
`+207` jump impulse means less entry height/velocity (the P0 jump-vs-walk measurement), so
what emerges is a gentle exit, not a fling. Exit handling is identical — freeze at the
transit tick, momentum intact.

Two arms per the dossier signature `drop_into(portal, object=None)`; the object mark rides
the existing `aim` field — **zero proto changes**. Explicit, not held-state-implicit: a
player holding a cube may still want the SELF arm (both ride the transit together).

**Self (`drop_into Pb`)** — `jump_into`'s approach minus the stop-short and the jump: gentle
forward + per-tick aim at the mouth center, walk over the lip, fall in (the transit detector
armed the whole way). Transit → end the verb right there, mid-emergence; detail = exit
position + exit velocity vs the partner mouth.

**Object (`drop_into Pb 14`)** — the mark must be HELD (else `NOT_HOLDING`, "pick_up first";
interpose precedent). Carry to a stand-off `kDropStandoff` (~64u) from the mouth center on
the player's own side — the straight march never crosses the disc; `heldKey` skipped as an
obstacle. Aim steep-down at the center so the carry swings the object over the disc,
`DropHeld`, then advance ticks tracking the OBJECT's single-tick position jump
(> `kJumpTransit`) = transit; verify emergence within `kEmergeRadius` of the partner, end
the verb with the object mid-flight (detail = its exit position + velocity), `LookBackAt`
so the outcome is on-screen. If the player clips the disc en route and transits, abort
`FELL_THROUGH` — the model must know its position changed.

Codes: `TRANSITED` · self `NOT_AT_MOUTH` / object `NOT_IN` (tick cap hit with no transit;
detail = current position + distance from the mouth center, the retry knob) ·
`FELL_THROUGH` · `NOT_HOLDING` · `NOT_GROUND` · `UNLINKED` ·
`NO_SUCH_PORTAL`/`WRONG_TARGET` · `BLOCKED` · `CANCELLED`.

Reused machinery: `kFloorNormalZ` gate, the transit detector (`kJumpTransit`),
`kEmergeRadius`, `MarchTo`/`RouteAround`, `DropHeld`, `LookBackAt`. New constant:
`kDropStandoff` (tuned in D1 verification).

### drop_into phasing

- **D1 — C++ verb** in `MacroExecutor.cpp` (both arms share the floor-portal gate + a
  transit detector; one new constant `kDropStandoff`), **plus the `pass_through` fix**:
  deleted its `m_vecVelocity` zeroing (input-clear stays). **BUILT — compiles + links; adversarial
  static review clean (no correctness bug survived skeptic verification).** `macro_repl` verify
  (PENDING, in-game): self arm through a floor→wall pair emerges gently and frozen (contrast
  `jump_into`'s fling from the SAME pair — the whole point); `wait` lands the player; object
  arm pops the cube out frozen mid-flight and reports exit pos+velocity; gate codes on
  wall/unlinked/not-held; `pass_through` emerges with its walking momentum intact. ⚠️ watch
  for the residual-velocity drift (removing the zero may re-surface the `place_portal`-after-
  `pass_through` drift quirk; friction over the settle should cover the grounded case).
- **D2 — Python surface**: grammar (`drop_into Pb [mark]`, `_check_drop_into` wraps
  `_check_portal_target` + a held-object gate), repl/docs strings, `percept_grammar_smoke`
  case. **BUILT — smoke 8/8.**

## Open — not blockers

- **Ledge case untested in the verb.** Same-z is verified; the portal-below fling (the money case)
  should just work (more funnel margin) but hasn't been run through the verb — confirm when a ledge
  chamber is handy.
- **Jump timing:** an extra jump *at entry* (vs only at launch) is a speedrun refinement — not v0.
