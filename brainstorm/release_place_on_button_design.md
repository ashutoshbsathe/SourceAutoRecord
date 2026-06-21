# Robustifying `release <mark>` — place a held object on a button

> **Status:** brainstorm, not a plan. Surfaces the design space and the genuine forks for
> robustifying `release <mark>` so a held cube/sphere reliably and *durably* seats on a marked
> button. The leading idea on the table is **teleport-the-held-entity onto the button**; that idea
> runs against a committed plan (`locomotion_tech.md` §P-manip = physics drop loop) and a dated
> fairness principle, so the central job of this doc is to lay the tradeoff out honestly, not to pick.
> Read alongside `locomotion_tech.md` (P-manip + §5 laser), `status_field_recon.md` (status fields),
> `llm_percept_act_grammar.md` §4 (the no-snap fairness quote), `puzzlemaker_elements.md` (button taxonomy).

---

## TL;DR — the thesis

**Durability lives in the loop, not in the teleport.** Today's `release` is open-loop — drop the
cube, always return SUCCESS — so cubes "only transiently seat" (`on_button` flips back). The fix is
to **close the loop on ground-truth state**: place → settle → read the press status with a *dwell*
(pressed **and still-pressed** N ticks later) → retry ≤K → `SEATED`/`NOT_SEATED`. That single change
is where the robustness comes from, independent of how the cube gets placed.

**Teleport-vs-drop is one knob inside that loop: the *perturbation primitive*.** It sits on a
spectrum of increasing power / cost / against-the-grain-ness:

1. **drop where aimed** — today, open-loop, flaky.
2. **position the player over the button center, then drop** — the committed P-manip plan
   (`locomotion_tech.md:260-266`). Fair (engine-simulated), cheap (reuses `PulseUse`), KISS.
3. **teleport the cube's hull into the button trigger** — the brief's idea. Durable, but needs a
   *net-new entity-transform writer* (none exists), *trigger-volume target selection* (the
   least-grounded piece), and it **reverses a dated no-snap fairness principle**.

Everything else — verify, retry, orientation, range-gate — is shared across all three.

**Three findings that collapse or invert the hard parts:**
- **Orientation is a non-problem for pressing.** Source (`prop_floor_button.cpp`): a press is pure
  *trigger-touch overlap filtered by cube-type only* — no orientation, no weight, no "seated flat"
  check. The ridged socket is cosmetic. The brief's "snap to a valid cube-button orientation" is
  wasted effort. The *only* real orientation constraint is **preserving** a reflector cube's yaw so
  its laser stays aimed → **policy: never canonicalize, always preserve `m_angAbsRotation`.**
- **Verify the held cube, not the button.** `m_bButtonState` says *"something presses it,"* not *"my
  cube seated"* — a different cube already on the button gives a false-positive. Authoritative signal
  = the held cube's `m_bActivated`. Ordering trap: `Release` clears `g_heldEntityKey` *first*
  (`MacroExecutor.cpp:1086`), so a closed-loop upgrade must cache it before clearing.
- **The teleport primitive already exists in-tree.** `FCPS.cpp:377-379` resolves
  `CBaseEntity::Teleport(pos, ang, vel, slowAccurate)` (`VMT<>(ent, Offsets::StartTouch+11)`) and
  already calls it on an *arbitrary prop* — origin + angles + velocity + relink + vphysics-resync in
  one vfunc. So the teleport is **~3 lines, 0 new offsets** (not the dominant cost the first pass
  assumed). The real remaining work moved to the **fairness check** (§5.6), the **placement
  seat-find** (§5.7), and the **verify dwell** (§5.4) — all of which are also reuse.

**Direction (locked 2026-06-21 — see Decisions below):** build **rung 3, the teleport**, but as a
**range-gated central teleport**: a *heavy fairness check* decides whether the teleport is something
the agent could plausibly have done by a clean hand-drop from where it stands; if it passes, we yank
the cube from the player's hand and place it **dead-center** on the button, orientation preserved —
**no edge-fiddling, no micro-step, no re-grab loop** (the engine wasting ticks nudging the cube is
exactly what we're killing). The closed loop still wraps it (settle → dwell-verify on the held cube's
`m_bActivated` → `SEATED`/`NOT_SEATED`). The open design question that remains — and the one this doc
now expands — is **what the fairness check actually is** (the brief's "sim 2 ticks forward" is one
option; §5.6 ladders out better ones).

---

## Decisions locked (2026-06-21)

Resolving the §9 forks (kept here so the body below still reads as the menu we chose *from*):

| # | Fork | Decision |
|---|---|---|
| 1 | Teleport vs drop | **Build the teleport.** It's the chosen perturbation primitive, not a fallback. |
| 2 | Fairness scope | **Range-gated teleport, no fiddling.** A heavy fairness check (in-reach + target bbox clear + orientation fully preservable) gates a single central snap. Do **not** burn engine ticks nudging the cube to seat. Fairness = "could a clean hand-drop from here have done this?" (§5.6 is the new brainstorm on *how* to check it.) |
| 3 | Orientation | **Always preserve `m_angAbsRotation`.** Never canonicalize. (§4) |
| 4 | Verify strictness | **All three:** authoritative = held cube's `m_bActivated`; multi-tick dwell-resample; `SEATED`/`NOT_SEATED` free-form `result_code`. (§5.4) |
| 5 | Non-button / no-mark | **Open-loop, untouched** — today's look-down `+use` drop. (§6) |
| 6 | Spheres | **Deferred**, but the implementation **must not preclude** a sphere on a ball button — sphere-compatibility is a design constraint, not a feature to build now. (§7) |
| 7 | `AcceptsBall` | **Don't snapshot it.** Accept the rare false-positive (teleporting a sphere onto a cube-button that a mapper set `AcceptsBall=0`). |

Everything below stays as the full design space; the rest of this pass adds **§5.6 (fairness-check
mechanisms)**, **§5.7 (placement-target computation)**, and **§5.8 (teleport-primitive choice)** —
the three things the locked direction now actually has to build.

---

## 1. Problem statement + scope

`release` drops a HELD entity. The robust version we want: when the mark is a **button**, the held
object ends up *pressing it and staying pressed* — confirmed against ground-truth state, not
act-and-pray. When the mark is **not** a button (or absent), keep today's behaviour: drop where the
camera looks.

**Why this exists.** The first_light forensics: the agent's blind march knocked its own cube off the
button (`on_button` True→False) and `release` only *transiently* seated cubes because it is
open-loop (`locomotion_tech.md:8-11`). Both runs died on the last mile, not on reasoning. Fixing the
last mile is the point.

**What `release` is today** (`MacroExecutor::Release`, `MacroExecutor.cpp:1085-1125`): clear
`g_heldEntityKey`; orient (face the mark via `AimAt`, or look down `kReleasePitch=75°` if `mark<=0`);
`PulseUse(kSettle, view)` to fire a held `+use` edge and drop the cube where the camera aims; then
**unconditionally return SUCCESS**. No class check, no seating confirmation, no teleport. The code's
own comment is honest about it: *"The caller tracks held-state, so there's nothing to confirm — always
SUCCESS."*

**v0 element scope** (stock PeTI, per `puzzlemaker_elements.md`): the four button entities plus the
pedestal. Custom/BEEmod/Hammer is P1, out of scope here.

**Held-object scope.** Weighted cube (standard / companion), reflector ("laser") cube, and the
sphere (Weighted Edgeless Safety Cube). The reflector cube is **not a separate class** — it is
`prop_weighted_cube` with `m_nCubeType==2` (`EntitySnapshotter.cpp:161-164`). The sphere is
**unverified** (see §7); "spheres benefit too" currently has no substrate.

---

## 2. The core proposal, stated plainly

When the resolved mark's class is a button and the held object's type is accepted by it (§3):

> **Teleport the held entity so its collision hull overlaps the button's press trigger, zero its
> velocity, wake the physics object so it re-settles, preserve its current orientation, then verify
> the press latched and *stays* latched.**

The decisive ground-truth finding (from the actual Portal 2 `prop_floor_button.cpp`): a floor/cube/
ball button detects a press **purely by trigger touch** — a child `CPortalButtonTrigger` fires
`StartTouch`/`EndTouch`, filtered only by *cube type*. There is **no orientation check, no contact
normal, no "seated flat" geometry test, and no weight measurement**. If the held object's bbox
overlaps the trigger volume and its cube type passes the filter, it presses on the next touch
evaluation. So the mechanic reduces to: **make the hull overlap the trigger, with a matching cube
type, and keep it there.**

The harness has **no entity-teleport primitive today** — zero `Teleport`/`SetAbsOrigin`/`SetPosition`
usage anywhere in `src/Features/Harness/` (grep-confirmed). This is the single largest net-new cost
of the proposal (§8) and the reason the committed design (§5) chose a physics drop instead.

---

## 3. Button taxonomy — accepts / orientation / what release must do

Acceptance is **class-name + cube-type**, nothing else. Source truth from `prop_floor_button.cpp`;
class list confirmed in `MacroExecutor.cpp:284-294` (`IsGoToObstacleClass`) and
`PuzzleAnnotate.cpp:34-69` (`kClassColors`, the single annotation source of truth).

| Class | Accepts | Press field | Orientation constraint | What `release` must do |
|---|---|---|---|---|
| `prop_button` (pedestal) | **player `+use`**, not objects | `m_nSequence` 0→3 (`[net]`) | n/a | **Don't teleport** — this is a use-press, not a place. Treat as `interact`, or reject. |
| `prop_floor_button` (round, Heavy-Duty) | player **OR** any cube/sphere | `m_bButtonState` 0/1 (`[net]`, **verified**) | none | Overlap trigger, any cube type, any yaw. |
| `prop_floor_cube_button` (ridged socket, `box_socket.mdl`) | cubes; **also spheres** unless `AcceptsBall=0` (default 1) | ⏳ **un-reconned**, presumed `m_bButtonState` | **none** — socket is cosmetic, not a gate | Overlap trigger; cube type ≠ sphere always OK, sphere usually OK. |
| `prop_floor_ball_button` (concave dish, `ball_button.mdl`) | **sphere only** (`CUBE_SPHERE`, `m_nCubeType==3`) | ⏳ **un-reconned** | none for press | Reject non-sphere held object up front; overlap trigger. |
| `prop_under_floor_button` (flush) | same as `prop_floor_button` | ⏳ **un-reconned**, presumed `m_bButtonState` | none | Same as floor button. |

**The crisp inverted insight** (only one recon agent's source dive caught it): the task brief worries
about *"ridged cube-buttons accept only limited valid orientations."* **That is a non-problem.** The
press has no orientation gate; the square socket is purely visual. Designing a "snap to a valid button
orientation" routine would be wasted effort — and, for the reflector cube, *actively harmful* (§4).

`AcceptsBall` is a per-button keyvalue we do **not** snapshot. A stock PeTI cube button accepts
spheres by default, but a mapper can disable it. We can't read it today; either snapshot it or
accept the rare false-positive. Open question for §9.

⏳ rows = `status_field_recon.md:96-98` marks cube/ball/under-floor buttons absent from reconned
chambers; only `prop_floor_button.m_bButtonState` is verified. **Any verify loop that targets those
buttons rests on an unverified field name** — a recon pass (`sar_harness_dump_fields`) gates it.

---

## 4. Orientation deep-dive

Four cases, and the rule is *not* uniform:

- **Sphere → free.** Rotationally symmetric; yaw is meaningless. Nothing to preserve or snap.
- **Generic cube on a generic/cube/floor button → preserve.** The button doesn't care about yaw, so
  the cheapest correct policy is **keep the held `m_angAbsRotation`** and don't touch it.
- **"Cube button" (ridged) → still preserve.** The temptation is "snap to a valid orientation set."
  Source says there is no valid-orientation set — overlap is sufficient. Snapping buys nothing.
- **Reflector cube powering a laser → MUST preserve.** This is the only genuine orientation tension,
  and it is the *opposite* of the task's framing. The reflector cube (`m_nCubeType==2`) redirects a
  beam out of a curved lens face; its yaw determines where the beam goes. Snapping it to a canonical
  button yaw **re-aims the beam and breaks the laser sub-goal**. The wiki confirms drop *keeps*
  whatever orientation the cube had. So **preserve `m_angAbsRotation` on release** — strictly safe for
  the button, mandatory for the laser.

**Net policy: never canonicalize. Always preserve the held cube's current orientation.** This is
both simpler and more correct than the orientation-snapping the brief imagined. `m_angAbsRotation` is
already snapshotted (`EntitySnapshotter.cpp:66`, well-known `HDEM_FIELD_ANGLES`) and readable
main-thread, so "preserve" is free to read.

⚠️ Two unverified pieces remain: (1) the held-cube orientation coupling to the player is the **#1 open
laser unknown** (`locomotion_tech.md:288-295,359-362`) — we don't yet reliably know the cube's facing
*while held*; (2) it is unverified in-engine that a teleport/drop preserving angles keeps the beam
redirected. Both want an in-engine spike before any laser-aware release is trusted.

---

## 5. Robustness / fairness / durability

### 5.1 Is teleport fair? (the central unresolved conflict)

The dated no-snap principle (`llm_percept_act_grammar.md:102`, 2026-06-09): locomotion is
engine-simulated, *"no snap/teleport (an earlier 'snap-move to remove flakiness' idea was rejected:
it trades fidelity and invites 'you cheated the movement')."* The committed P-manip design
(`locomotion_tech.md:260-266,84`) deliberately stays inside that envelope — re-grab + micro-step, let
physics seat the cube, **no new `place_on` verb, no teleport.**

The teleport proposal runs **against the documented grain**: it supersedes a committed plan and
reverses (for manipulation) a principle whose own reasoning argues against it. Two true things hold
simultaneously and must be weighed by the user, not by us:

- The no-snap quote is **explicitly scoped to locomotion** (`go_to`/`aim_at`). Whether it extends to
  *manipulation* is an open call. A defensible read: locomotion fairness is about the agent traversing
  space (cheatable, observable); placing a held object is a fine-motor primitive the engine's own
  `+use` already abstracts, so a teleport-place is arguably no more "cheating" than `+use` is.
- The **fairness lever is range-gating**, not the teleport itself. If `release` only teleport-seats
  when the player is *already in reach of the button* (mirroring `pick_up`'s `kGrabRange=96`,
  `MacroExecutor.cpp:76`, and `interact`'s walk-into-reach-then-act, `MacroExecutor.cpp:1127-1148`),
  then the teleport is a *short, in-reach snap of an object the agent legitimately holds and is
  standing next to* — not a teleport across the chamber. That reframes "fair" as "the agent did the
  hard part (got here, holds the cube); we just remove the physics-drop flakiness on the last inch."

**What's actually lost by staying with the committed physics-drop loop?** No agent quantified this,
and it is the information the user needs to justify (or reject) the override. The honest answer from
recon: the drop loop is *flakier* (cubes bounce/slide, may need re-grab + micro-step retry ≤K) and
*slower*, but no chamber class has been shown to make it *fail outright*. If the drop loop is "merely
flaky," the teleport's large net-new cost (§8) + fairness reversal is hard to justify. **This decision
should be made before the engine work, not after.**

### 5.2 Teleport vs precise-physics-drop — a perturbation spectrum, not a binary

Reframe the choice: both are the same closed loop (§5.4) with a different **perturbation primitive**
for getting the cube onto the press surface. Today's drop-where-aimed is rung 1 of the same ladder.
The loop (settle → dwell-verify → retry) is shared; only the placement step differs.

| | Teleport-onto-trigger | Physics-drop loop (committed) |
|---|---|---|
| New engine code | **Large** — entity-position writer that doesn't exist (§8) | **Small** — reuses `PulseUse`; needs only the field reader |
| Fairness posture | Reverses a committed principle (§5.1) | Stays simulated, inside the envelope |
| Flakiness | Low *if* trigger-overlap target is right (§7 says it may not be) | Moderate — bounce/slide, retry ≤K |
| Settle behaviour | Must wake + zero velocity or cube sinks/snaps back | Engine handles it natively |
| Reflector laser | Preserve angles = safe (§4) | Same |

Both paths **need the field reader first** (`ReadBoolField`, P0.3, deferred to P-manip as its first
consumer, `locomotion_tech.md:158-159`). Teleport needs that *plus* an entity-transform writer.

### 5.3 Re-pickup after release (the recovery path)

`g_heldEntityKey` is the only held-state C++ keeps (`MacroExecutor.cpp:88-89`), set on a confirmed
grab, cleared by `Release` (`:1086`) and at `SESSION_START`. Re-pickup hinges entirely on `pick_up`'s
**motion-based grab-confirm**: the object must end within `kHeldDist=80` of the eye **and move
`>kMinGrabMove=8`** (`MacroExecutor.cpp:1023-1048`), else `GRAB_FAILED`.

**The teleport path interacts badly with this heuristic and no agent connected the two.** A
teleport-seated cube lands at **zero velocity**; if we *freeze* it (DisableMotion) to hold the snap,
it is asleep/inert. On the next `pick_up`, the engine may grab it but the cube barely **moves**, so
the `moved>8` confirm can spuriously return `GRAB_FAILED` even on a successful grab. And if we froze
it, **`EnableMotion`/`Wake` must run before it is re-grabbable** — there is no `IPhysicsObject`
EnableMotion wrapper today (would go through `AcceptInput`, untested on cubes). The committed drop
loop sidesteps this entirely: dropped cubes stay full-physics dynamic and re-pickup unchanged.

Recovery matters because the first_light failure *was* the agent knocking its cube off and never
recovering. If teleport-release breaks the motion grab-confirm, we lose the escape hatch.

### 5.4 Verification via the status loop — and the timing contract

The committed predicate is `m_bButtonState` (button side) OR `m_bActivated` (cube side); both flow
over gRPC, and `m_bActivated` flips in lockstep with `m_bButtonState` (`status_field_recon.md:106`).
The executor can read either synchronously on the main thread via `EntField::getServerOffset`
(`Entity.hpp:56`, the snapshotter's reader; `RegisterCuratedStatusFields`,
`EntitySnapshotter.cpp:160-167`). No `ReadBoolField` helper exists yet, but `getServerOffset` gives one
in ~5 lines. **No proto change needed** to add `SEATED`/`NOT_SEATED` — `result_code` is a free-form
string.

Two non-obvious correctness traps the recon surfaced:

- **Read the HELD cube, not the button.** `m_bButtonState` tells you *"something presses it,"* not
  *"my cube seated."* In a multi-cube chamber (or when a different cube already presses the button),
  reading the button gives a **false-positive SEATED**. Authoritative signal = the held cube's
  `m_bActivated` (`g_heldEntityKey` → `prop_weighted_cube.m_bActivated`,
  `EntitySnapshotter.cpp:163`). The "two equivalent readouts" claim is only true for the
  1-cube-1-button chamber. **Ordering bug to watch:** `Release` clears `g_heldEntityKey` *first*
  (`MacroExecutor.cpp:1086`) — a closed-loop upgrade must read the held cube *before* clearing, or
  cache its key.
- **Settle-then-resample, not a point read.** The press *output* is instant on `StartTouch`, but the
  button schedules a deferred settle-think (~2s in source) and a mis-balanced cube can slide off and
  `EndTouch`→**unpress after the first read**. A single-sample read at the wrong tick **reproduces
  the original "transiently seated" bug under a new name.** The durability contract is therefore a
  **multi-tick dwell**: read `pressed==true` *and still-true* after N settle ticks (sample at t and
  t+N), not once. `kSettle=20`/`kGoToSettle=24` exist but were tuned for a physics drop, not a
  teleport-then-settle; the dwell count is an unpinned tuning constant.

### 5.5 Settle / zero-velocity gotchas (engine physics)

VPhysics (Havok-derived) **sleeps** objects after ~1s and does **not** wake them on internal state
changes — *"if you change a simulation property and want it to respond immediately, Wake() it."* A
raw `SE(ent)->field<Vector>("m_vecAbsOrigin") = pos` write sets the networked value but **bypasses
`SetAbsOrigin`'s side-effects** (`CalcAbsolutePosition`, collision relink, VPhysics
`UpdateObjectPosition`) — the physics body can stay at the old position and snap back. The reliable
recipe: set origin+angles via the engine path, **zero linear + angular velocity**, then
`Wake()`/`EnableMotion` so it re-settles and the button trigger re-evaluates the overlap. This is
*untested on `prop_weighted_cube` in this build* and is the riskiest unverified link.

### 5.6 The fairness check — kill the forward-sim, gate with static traces

The brief's *"simulate the player forward 2 ticks and see if the cube presses"* is the wrong tool, and
the ground truth says why: a stock PeTI press is **pure static trigger-overlap filtered by cube type**
— no weight, no flat-seating, no impact dynamics. *There is nothing to simulate.* A forward-sim is
strictly **more expensive and less reliable** than a static geometric predicate — it re-introduces
the `sv_alternateticks` physics RNG that is the exact flakiness we're removing (it can false-*pass* a
drop physics would botch and false-*fail* one that would've worked). **Drop the forward-sim.**

Replace it with a small battery of **static traces, prove-or-refuse** (all reuse
`engine->Trace`/`TraceHull`; zero new offsets). "Fair" = *a clean hand-drop from where the agent
stands could have reached this seat.* That decomposes into:

| # | Check | Defeats | Reuse | v0? |
|---|---|---|---|---|
| 1 | **Reach gate** — eye within `kGrabRange` of seat, holding an accepted cube | cross-chasm teleport (the one true cheat) | `pick_up` reach gate verbatim (`MacroExecutor.cpp:988-994`) | **yes** |
| 2 | **Press-face normal** — down-trace onto the button, require `tr.plane.normal.z > 0.7` | wall / ceiling / steep buttons (a dropped cube falls off) | `CheckEdge` down-trace (`MacroExecutor.cpp:254`); reads the normal the trace already fills | **yes** — *also* the §5.7 seat-finder (one trace, two uses) |
| 3 | **Drop-corridor hull sweep** — `TraceHull` the cube OBB straight down from one cube-height above the seat to the seat | 2D-blocked + fizzler-in-corridor (`CONTENTS_GRATE`∈`MASK_PLAYERSOLID`) + grate + gap-arc, in **one** cast of the *actual cube hull* | `GoToPlanner` `TraceHull` idiom (`GoToPlanner.cpp:103`) | **yes** |
| 4 | **Seat-occupancy** — zero-length `TraceHull` at seat; reject `SEAT_OCCUPIED` if startsolid vs a *non-button* ent | stray cube in trigger / two-cube contention (don't eject the incumbent) | `GoToPlanner::Probe` step 2 | when a contention chamber needs it |
| 5 | **Eye-to-seat identity ray** — inspect `tr.m_pEnt`; reject glass/world short of seat | "in reach but walled off behind glass" a *vertical* corridor misses | `RayClearance` single ray (`MacroExecutor.cpp:264`) | defer; #3 covers most |

**Cheapest correct fairness = #1 + #2 + #3** — three traces, deterministic, ~25 lines, and #2 is
shared with the seat-finder. The reframe in one line: **geometry gates placement; the settle-dwell
(§5.4) *is* the dynamic-fairness check; the forward-sim has no niche.** The only cases geometry can't
prove are dynamic (faith-plate button, sphere roll-off, moving platform) — and the dwell observes the
*real* engine outcome for those for free (a launched/rolled cube `EndTouch`es within N → `NOT_SEATED`).

### 5.7 Placement target — trace-find the real press surface (don't trust the prop OBB)

`EntityCenter` returns the *unrotated* OBB center of the **visual** prop, and `box_socket.mdl` (cube
button) is a recessed *hole* — center-placing the cube there clips/buries it (§7's crux). The fix needs
no trigger-volume dimensions:

- **Z (rest height):** take the press-face down-trace's `tr.endpos.z` (the *actual reported collision
  plane*, already computed for fairness #2) + `cubeHalfHeight = (cubeOBBMaxs.z − cubeOBBMins.z)/2 + ε`.
  Grounds rest-height in real collision, so the recessed-socket caveat is irrelevant. Seat the cube
  *bottom* a hair into the trigger (press = overlap → a hair-in presses, a hair-above never does).
- **X/Y:** center of `button->collision().WorldSpaceTriggerBounds()` (press-volume midpoint,
  `ICollideable.hpp:41`); fall back to `button abs_origin` X/Y if that vfunc turns out to read render
  bounds (buttons are axis-aligned PeTI, so X/Y is safe either way).
- **Orientation:** carry the held cube's current `abs_angles()` verbatim → reflector yaw preserved
  *by construction*; the "orientation-preservable" fairness term is a no-op.
- **Origin, not center:** `Teleport` takes an *origin*. Convert with the FCPS idiom
  (`FCPS.cpp:376`): `targetOrigin = seatCenter + (cubeAbsOrigin − cubeCurrentCenter)`. For a tilted
  cube, FCPS's `CollisionToWorldTransform` + `dotProdAbs` (`FCPS.cpp:357-368`) gives the *rotated*
  world center — i.e. the deferred unrotated-OBB caveat already has a worked solution in-tree.

Sphere (deferred, not precluded): identical, Z from `triggerTop + sphereRadius`, no yaw.

### 5.8 Teleport primitive — it already exists (FCPS)

The first pass called this *"the dominant cost / OFFSET NOT YET LOCATED."* **Both wrong.**
`FCPS.cpp:377-379` resolves and already calls, on an *arbitrary prop*:

```cpp
using _Teleport = void(__rescall*)(void* ent, const Vector* pos, const QAngle* ang, const Vector* vel, bool slowAccurate);
_Teleport Teleport = Memory::VMT<_Teleport>(entity, Offsets::StartTouch + 11);
Teleport(cubeEnt, &targetOrigin, &cubeAngles, &zeroVel, /*slowAccurate=*/true);
```

One game-layer vfunc does `SetAbsOrigin` + spatial relink + vphysics resync, and `&zeroVel` kills
velocity — exactly the drop-flakiness removal the feature is for. **~3 lines, 0 new offsets.** It runs
*after* `Release`'s `+use` drop pulse (so `g_heldEntityKey` is already cleared and the cube is a free
prop — no grab-controller fight, identical to FCPS's case). This **supersedes** the manual
`SetPosition`+`Wake`+`SetVelocity` recipe (§5.5), which becomes the *fallback* if `Teleport` misbehaves
on a just-dropped cube. **Do not** pursue `AcceptInput("Teleport")` — input existence on the cube is
unconfirmed and packing origin/angles through `variant_t` is fiddly; lowest payoff with `Teleport`
sitting right there.

### 5.9 Alternatives considered (parked)

- **Settle-and-resnap — teleport only the residual.** Keep today's open-loop `+use` drop *unchanged*;
  settle; read `m_bActivated`. If pressed → done, **no teleport**. Only on not-pressed do we run the
  fairness battery + Teleport. Makes the teleport a *rare correction*, keeps behaviour maximally
  physical, and a cube already resting near the button is itself an implicit fairness signal. Cost: one
  settle+read on the happy path (flag-gate if it bites the RL hot loop). **Genuine architectural fork
  vs always-teleport-when-fair — §9.** Tasteful framing: less "the harness places cubes for you," more
  "the harness fixes a drop that didn't latch."
- **Dormant `sar_harness_seat_check` debug command.** Mirror `sar_harness_goto_plan`
  (`MacroExecutor.cpp:1154-1214`): given a button mark + held cube, run the *full fairness battery
  read-only*, print each verdict + the deciding trace, teleport nothing. Red-team the fairness logic on
  adversarial chambers (wall button, fizzler-between, contention) **before the entity writer is even
  wired** — separates "is the fairness logic right" from "does the teleport stick." On-brand with this
  repo's verify-gate convention (go_to shipped a dormant planner first). Keep it print-only.
- **Shared occupancy oracle (defer).** Generalize `GoToPlanner::Probe` into one free-space test shared
  by pathing *and* the fair-check ("one notion of blocked in the codebase"). Nice KISS dividend, but the
  planner is player-hull + floor-grid; lifting it to arbitrary hull/skip-set risks tentacles. The local
  3-line `TraceHull` wins unless this falls out cleanly.
- **`TestCollision` / `WorldSpaceTriggerBounds` as the *press* oracle (rejected).** Elegant ("ask the
  engine the literal overlap question"), but unconfirmed whether those vfuncs read the child
  `CPortalButtonTrigger` or the prop's render/socket shell. Use `WorldSpaceTriggerBounds` for the
  low-stakes placement X/Y only — not as the fairness/verify authority.
- **State-poke (rejected outright).** Write `m_bButtonState=1` / fire the button's input without moving
  the cube. Maximally robust, maximally *un*faithful — the cube isn't there, the world desyncs, and it
  breaks any puzzle needing the cube to *stay*. Named only to mark the far end of the spectrum.

---

## 6. The non-button case (drop anywhere)

When the mark is not a button, or absent, **keep today's behaviour** — look down `kReleasePitch=75°`
and `+use`-drop at the player's feet (`MacroExecutor.cpp:1098-1111`). This already matches the brief's
*"just drop randomly."* The committed design keeps this branch unchanged (`locomotion_tech.md:264`).
The gate is a class check on the resolved mark — read the class off the resolved `CEntInfo` exactly
as `IsGrabbableClass` does (`MacroExecutor.cpp:131-138`); if it isn't a button-family class, fall
through to the physics drop. (Open question: is teleport-at-feet also wanted for the no-mark case, or
strictly physics-drop?)

---

## 7. Failure modes & edge cases

- **Wrong cube already presses the button.** `m_bButtonState==1` before we ever seat ours →
  false-positive SUCCESS, and a teleport could *displace/eject the incumbent cube and unpress it*.
  Mitigation: verify on the **held cube's `m_bActivated`** (§5.4), not the button.
- **A loose cube sits in the trigger volume.** Teleporting our cube to the same spot lands it
  *intersecting* the stray. Need overlap-but-not-intersect target selection (§ below).
- **N-buttons-held-simultaneously puzzles.** Releasing here must not disturb a cube already on another
  button — a teleport's displacement radius matters; a physics drop is locally contained.
- **Teleport-target volume is the trigger, not the prop OBB.** `EntityCenter`
  (`MacroExecutor.cpp:140-156`) returns the **unrotated** OBB center of the *button prop's* collision
  (the visual disc), **not** the child `CPortalButtonTrigger`'s volume. For `box_socket.mdl` the prop
  OBB is a *hole*; teleporting the cube's center to the prop OBB-top-center may **miss the trigger or
  clip the recessed socket.** The trigger is a child entity — **does it appear in the entity list, get
  a mark, or get snapshotted at all?** Only the listed `prop_*` classes are marked
  (`PuzzleAnnotate.cpp:34-69`), so the trigger is likely **invisible to mark resolution.** This is the
  **least-grounded piece of the whole approach.** `engine->Trace` (`Engine.hpp:151`) could find the
  press *surface* by tracing down, but no agent grounded the trigger-volume target. The deferred
  unrotated-OBB caveat (`MacroExecutor.cpp:141`) becomes **load-bearing** for any angled/wall button.
- **Fizzlers / grates / portals between agent and button.** A `trigger_portal_cleanser`
  (`m_bDisabled`, already snapshotted, `status_field_recon.md:108`) **dissolves** a cube whose
  trajectory (drop *or* teleport arc) crosses its plane. The cube then respawns at its dropper with a
  **new serial** → `g_heldEntityKey`/the mark goes **stale**. `ResolveMarkInfo` revalidates serial and
  returns `BAD_MARK` (`MacroExecutor.cpp:115-129`) — that's the only stale-guard, so the retry logic
  must treat *"my held cube no longer exists"* as a **distinct terminal state, not retry forever.** No
  current `Release` path is fizzler-aware.
- **Sphere rolls out before the press latches.** A teleported sphere with zero velocity on a domed
  `ball_button.mdl` dish is a stable-equilibrium question recon never resolved; on a square
  `box_socket` it may roll off. The dwell-resample (§5.4) catches it as `NOT_SEATED`, but the cause is
  geometry, not a read bug.
- **`+use` is a context toggle.** A `release` issued while *not* holding (but stood on a grabbable)
  **grabs instead** (`MacroExecutor.cpp:1113-1117`). Software held-tracking keeps intent aligned today;
  any rewrite must preserve that invariant.

**Guards vs accepted flakes (red-team summary).** Each guard is one cheap trace/read, zero new offsets:

| Edge case | Disposition |
|---|---|
| Wall / ceiling / steep button | **Guard:** press-face `normal.z > 0.7` (§5.6 #2); distinct reject |
| Fizzler between cube and seat | **Guard:** corridor sweep (#3) + eye-line `m_pEnt==trigger_portal_cleanser` → `FIZZLER_BETWEEN`, **never retry** (cube dissolves → new serial → stale mark) |
| Glass / wall in reach but between | **Guard:** eye-to-seat identity ray (#5) |
| Seat occupied / two-cube contention | **Guard:** seat-occupancy hull (#4) + pre-teleport incumbent baseline → `WRONG_CUBE_INCUMBENT` |
| Recycled entity slot on resample | **Guard:** serial-revalidate `heldKey` (`ResolveMarkInfo` pattern) |
| `box_socket` recessed hole | **Guard:** trace-find seat from `tr.endpos`, not prop OBB (§5.7) |
| Walkable-ramp button (~0.8 normal) → slide | **Accept:** dwell returns `NOT_SEATED`, fails safe |
| Sphere roll-off on domed dish | **Accept:** dwell catches `EndTouch` |
| Faith-plate / moving-platform button | **Accept:** dwell catches unpress within N (tune N) |
| `AcceptsBall=0` mapper override | **Accept:** per decision #7 |

---

## 8. Implementation cost & tastefulness sketch (costs only)

**Where it lives:** `MacroExecutor::Release` (`MacroExecutor.cpp:1085-1125`) is upgraded **in place** —
no new verb (`locomotion_tech.md:84`). Verb dispatch is the flat if-chain at
`MacroExecutor.cpp:612-628`; nothing changes there.

**What EXISTS to build on (cheap, reuse):**
- The whole tick/view machinery: `AdvanceTicksBlocking` (`HarnessThread.hpp:22`), per-batch
  `ApplyAbsoluteView` re-assert (`MacroExecutor.cpp:183` — the documented "view not held across ticks"
  gotcha; *any* tick-advancing release must keep re-asserting), `PulseUse` (`MacroExecutor.cpp:229`).
- Mark→entity resolution + reach gating: `ResolveMarkInfo` (serial revalidate→`BAD_MARK`),
  `ResolveMarkCenter`, `EntityCenter`, `PlayerEye`, `AimAnglesTo`, `kGrabRange=96`
  (`MacroExecutor.cpp:104-165,76`). `pick_up`/`interact` are the reach-gate templates.
- Held-entity identity: `g_heldEntityKey` + `PackEntKey` (`MacroExecutor.cpp:84-89`) — we already know
  *which* entity is held; no engine "what am I holding" query needed (none exists, by design —
  `llm_percept_act_grammar.md:99-100`).
- Status reads: `getServerOffset` main-thread reader; cube `m_nCubeType`/`m_bActivated` and button
  `m_bButtonState` already curated/snapshotted (`EntitySnapshotter.cpp:160-167`). `m_angAbsRotation`,
  OBB mins/maxs already snapshotted (`EntitySnapshotter.cpp:66,139`).
- The verify-gate pattern: `sar_harness_goto_plan` (`MacroExecutor.cpp:1154-1214`) is the model for a
  debug-only `sar_harness_*` command that *inspects* a planned placement without mutating state.

**What is NEW (cost — the FCPS finding shrinks this to mostly composition of existing primitives):**
- **Fairness battery** (§5.6): 3 static traces — reach (reuse `pick_up` gate, `MacroExecutor.cpp:988`)
  + press-normal down-trace (reuse `CheckEdge`, `:254`) + drop-corridor `TraceHull` (reuse
  `GoToPlanner`, `GoToPlanner.cpp:103`). *~25 lines, 0 new offsets. Small.* Seat-occupancy + eye-line
  are cheap add-ons, build on demand.
- **Placement seat-find** (§5.7): the press-normal down-trace's `endpos` + cube half-height; X/Y from
  `WorldSpaceTriggerBounds`. *Shares the §5.6 #2 trace. Tiny.*
- **Teleport** (§5.8): reuse `FCPS.cpp:377-379` `CBaseEntity::Teleport`. *~3 lines, 0 new offsets.*
  Fallback `SetPosition`+`Wake` (VphysHud template) only if it misbehaves.
- **Verify dwell** (§5.4): cache `heldKey` *before* `Release` clears it (`:1086` ordering trap);
  `se->field<bool>("m_bActivated")` (already compiles — `PuzzleAnnotate.cpp:98`, **no new reader
  needed**) at t and t+N; AND-reduce; optional at-rest geometric guard + incumbent baseline. *Small.*
- **`Release` rewrite** (in place, no new verb): button-class gate → fairness battery → Teleport →
  dwell → result code (`SEATED`/`NOT_SEATED`/`NOT_FAIR`/`SEAT_OCCUPIED`/`FIZZLER_BETWEEN`/
  `WRONG_CUBE_INCUMBENT`); non-button falls through to today's drop. *Medium — this is the
  orchestration, the bulk of the actual diff.*
- **Dormant `sar_harness_seat_check`** (§5.9): read-only verify-gate that prints fairness verdicts.
  *Small; recommended — de-risks the fairness logic before the writer touches anything.*
- **Proto:** **none.** All result codes ride the free-form `result_code` string (`harness.proto:83-95`).
  A new *request arg* would force `make proto` + rebuild + `agentloop_smoke`; we don't need one.
- **Python sync** (`P-py`): `macro_grammar.py` release doc, `gemini_agent.py` notes, an
  `agentloop_smoke` place-on-button assertion. New codes flow to the model automatically. Sphere
  support also needs `GRABBABLE_CLASSES`/`IsGrabbableClass` widened *first* — deferred, but the design
  above is already sphere-shaped (decision #6), so don't structurally preclude it.
- **Recon (gating, not code):** `sar_harness_dump_fields` (`PuzzleAnnotate.cpp:252`) +
  `sar_harness_seat_check` confirm (a) cube/ball/under-floor button press-field names,
  (b) `WorldSpaceTriggerBounds` reads the *trigger*, not render bounds, (c) sphere class +
  `m_nCubeType`, (d) the dwell N for a faith-plate cube. `macro_repl.py` drives all by hand.

**Tastefulness read (revised after FCPS):** this collapsed from *"a new physics subsystem + a fairness
reversal"* to **a composition of primitives that already exist** — `Teleport` (FCPS), `TraceHull`
(planner), `field<bool>` (annotate), the reach gate + down-trace (executor). Net new *engine surface ≈
zero offsets*; the real work is ~3 traces + a ~3-line teleport + a dwell loop + the `Release`
orchestration, plus a dormant verify command. Two sub-problems the first pass feared evaporated: the
orientation-snapping (§4 — it's a no-op, preserve `abs_angles`) and the entity-writer (§5.8 — FCPS).
This is now a *small, KISS, mostly-reuse* feature — satisfying rather than scary.

---

## 9. Build forks — RESOLVED (2026-06-21)

First-pass forks are in *Decisions locked*; the build-shaping forks this pass surfaced are now resolved:

1. **Always teleport when fair** — no settle-and-resnap. Fewest moving parts, fully deterministic
   control flow, and the agent never depends on a physics drop landing.
2. **Ship all five fairness checks** (reach / press-normal / drop-corridor / seat-occupancy /
   eye-line) — all are pure traces needing no recon, so all land in v0.
3. **Recon pass: yes** — one `dump_fields`/`seat_check` sweep for the gated field names + the dwell `N`.
4. **`seat_check` first: yes** — and the dormant `sar_harness_*` commands **may stay**; a later ROADMAP
   phase can strip them, but keeping them is acceptable.
5. **Recon-gate the unverified field names** — placement/verify lean on
   `prop_floor_button.m_bButtonState` until R7 confirms cube/ball/under-floor; the rest *fail open*
   (no-op to the held-cube `m_bActivated` check) meanwhile.

---

## 10. Implementation plan (phased)

Per the many-small-phases preference: each phase is small, hand-verifiable in `macro_repl` or a debug
command, **C++ before Python**. Spike risk is isolated to C4 (does FCPS `Teleport` behave on a
just-dropped `prop_weighted_cube`) and R7 (field names); C1–C3 are read-only geometry that can't break
anything live. All new C++ lands in `MacroExecutor.cpp` unless noted, reuse-heavy per §8.

| Ph | Scope | Verify by |
|----|-------|-----------|
| **C1** | **Seat-find** `ComputeSeat(buttonSE, cubeSE) → {origin, angles}`: press-face down-trace `endpos` + cube half-height + ε (Z); `WorldSpaceTriggerBounds` center (X/Y, fallback `abs_origin`); preserve `cubeSE->abs_angles()`; origin via the FCPS center→origin idiom. | (printed by C2) |
| **C2** | **Dormant `sar_harness_seat_check <button-mark>`** (mirror `sar_harness_goto_plan`): resolve button + held cube, print the computed seat (and, once C3 lands, every fairness verdict). **Read-only, mutates nothing.** | `macro_repl`: seat prints centered-on-top on a floor button |
| **C3** | **Fairness battery** → 5 verdicts: reach (reuse `pick_up` gate) · press-normal `normal.z>0.7` · drop-corridor `TraceHull` · seat-occupancy zero-len hull · eye-to-seat identity ray. `seat_check` prints all. | adversarial chambers (wall button / fizzler-between / occupied seat): the right verdict fires in the printout |
| **C4** | **Teleport primitive** `SeatEntity(ent, origin, angles)` = FCPS `CBaseEntity::Teleport` (`VMT<>(ent, StartTouch+11)`) + `&zeroVel`. Temp `sar_harness_seat_place <mark>` = seat-find + teleport, **no fairness/verify**, to prove the snap lands. | `seat_place`: held cube snaps dead-center, button presses (visual + `dump_fields`) |
| **C5** | **Verify dwell** `ConfirmSeated(heldKey, buttonKey) → SEATED/NOT_SEATED`: cache key pre-clear; `field<bool>("m_bActivated")` at t & t+N (AND-reduce); + optional at-rest (`abs_velocity≈0`) + incumbent baseline. | after `seat_place`, `ConfirmSeated`→SEATED; knock cube off → NOT_SEATED |
| **C6** | **`Release` rewrite** (in place, no new verb): cache `heldKey` → button-class gate (non-button → today's drop, *byte-identical*) → `ComputeSeat` → fairness battery → **fair?** `PulseUse`(free the grab) + `SeatEntity` + `ConfirmSeated` **: not fair?** open-loop drop + `NOT_FAIR`. Result codes per §8. Retire temp `seat_place`; keep `seat_check`. | `macro_repl`: `release <button-mark>` seats + SEATED; unfair target → drop + NOT_FAIR; non-button release unchanged |
| **R7** | **Recon pass** (no engine code): `dump_fields` + `seat_check` confirm `WorldSpaceTriggerBounds` reads the *trigger*, cube/ball/under-floor press-field names, sphere class + `m_nCubeType`, and the faith-plate dwell `N`. Feed field-names/constants back into C3/C5. | printouts answer each gated item; notes appended to `status_field_recon.md` |
| **P8** | **Python sync**: `macro_grammar.py` release doc (drop-where-aimed → seats-on-button-when-fair), `gemini_agent.py` prompt note on the new codes, `agentloop_smoke` place-on-button assertion. New `result_code`s flow to the model automatically. | `agentloop_smoke` passes the place-on-button gate |
| **D9** | **ROADMAP + cleanup**: add this doc to the doc-index (`ROADMAP.md:131-159`), cross-link the P-manip line; add a *later* phase to **strip the dormant `sar_harness_*` debug commands** (or a noted decision to keep them). | — |

**Sequencing invariants (don't violate):**
- **Teleport runs *after* a `+use` drop, never while held** — the drop releases the grab controller so
  the teleport doesn't fight it (FCPS's free-prop precondition). The drop's settle can be short since we
  re-place immediately; this is the unavoidable hand-off, *not* iterative nudging.
- **Cache `heldKey` before anything clears `g_heldEntityKey`** (`Release` clears it at its first line,
  `:1086`) — C5 and C6 both depend on resolving the cube post-drop.
- **No proto change** anywhere — all signalling is the free-form `result_code` string.
- **Sphere stays deferred but un-precluded**: every helper takes the cube via `g_heldEntityKey` and reads
  its OBB/type generically, so widening `GRABBABLE_CLASSES` is the *only* sphere-specific change later.

---

*Direction + plan locked. Doc-index + P-manip cross-link in `ROADMAP.md` **done** (2026-06-21); D9's
remaining item is the later strip-or-keep decision on the dormant `sar_harness_*` debug commands.*

**Build log:**
- **C1 + C2 shipped (2026-06-21), builds clean.** `ComputeSeat(button, cube) -> {origin, angles, …}`
  (trigger-bounds X/Y · press-surface down-trace Z + rotation-aware cube half-height · preserved
  `abs_angles` · centre→origin shift) + dormant read-only `sar_harness_seat_check <button-mark>
  [cube-mark]` that prints the seat. Both file-local in `MacroExecutor.cpp`; `release` untouched.
  *In-game verify still owed:* in `macro_repl`, hold a cube + `sar_harness_seat_check <floor-button>`
  → seat should print centred-on-top (`normal.z≈1`, `center` over the button, `surface.z` = disc top).
  **Verified 2026-06-21** on a `prop_floor_button` (player stood on it, cube held): geometry correct —
  `surface z=14.39 normal.z=1.000`, `center`/`origin` centred at `(704,704,31.5)`, half-height ≈18
  (rotation-aware extent works on a tumbled hold).
- **Orientation decision: keep #3 as-is (preserve held `abs_angles`).** The verify exposed that a held
  cube's angles are its *carry pose* (view-coupled, e.g. `p270 y150 r180`), not a rest orientation, so
  a teleport at those angles lands tumbled. Press is overlap-only so it still latches; the visual/laser
  question is **deferred to C4** (revisit once we see real post-drop placement), not canonicalised now.
- **New condition — player-on-button is a fair seat with player displacement.** When the agent stands on
  the target button while holding the cube, that is unambiguously fair, but the player must step off so
  the cube can take the seat. Folds into fairness #4 (seat-occupancy): occupant == *self* ⇒ not
  `SEAT_OCCUPIED`, instead displace the player. **Shipped read-only (2026-06-21):** `FindPlayerStandoff`
  (ring-probe a nearby spot where the player hull fits on solid floor) + `seat_check` now prints
  `on-button: ground=/geo=` and the chosen `standoff` (no teleport yet — C4 wires the moves: displace
  player → drop → seat cube). *In-game verify owed:* stand on the button holding a cube, run
  `seat_check` → expect `on-button ground=Y` (or `geo=Y`) and a sane `standoff` just off the button.
  **Verified 2026-06-21:** `ground=Y geo=Y` on a `prop_floor_button`; `standoff (823,704,0)` valid (off
  the button on floor). Ground-entity detector is reliable.
- **C3 shipped read-only (2026-06-21), builds clean.** `CheckFairness(button, cube, player, seat)` runs
  all five static gates and `seat_check` prints each verdict + raw measurement: **reach**
  (`dist(eye,seat) ≤ kGrabRange`), **press-normal** (`normalZ > 0.7`), **corridor** (cube-hull drop
  sweep settles within `kCorridorSlack` of the seat — catches grate/fizzler/geometry), **eye-line**
  (clear ray eye→seat, rejects glass), **seat-occupancy** (zero-len cube hull at the seat skipping the
  button: `CLEAR` / `SELF`→displace / `OCCUPIED by <class>`). `TraceSkip2` filter skips both placer and
  placed cube so neither self-blocks. Overall `fair = reach && press-normal && corridor && eye-line &&
  (seatClear || selfOnSeat)`. **Consolidation:** fairness #4 `SELF` (player hull overlaps the seat
  volume) replaced the earlier ground/geo on-button print — it is the precise displacement trigger
  (only fires when the player is actually *in the seat*, not merely standing on the button); standoff
  now prints under `SELF`. Thresholds are doc defaults, printed raw for calibration. *In-game verify
  owed:* (a) stand-on-button + held cube → `fair: YES`, `seat SELF`, standoff printed; (b) a wall/steep
  button → `press-normal N`; (c) fizzler/glass between → `corridor`/`eye-line N`; (d) a stray cube on
  the button → `seat OCCUPIED`.
  **Verified + bugfix 2026-06-21:** on-button → `fair: YES seat SELF standoff (823,704,0)` ✓. Far-away
  exposed a bug: seat read `OCCUPIED by prop_floor_button` because the seat dips `kSeatBias` *into* the
  button and the button-skip filter didn't exclude the started-inside entity from `startsolid`'s
  `m_pEnt`. **Fixed:** the occupancy hull now rests `kSeatBias + kOccupancyLift` above the seat (clear of
  the button), so only foreign occupants register. On-button `SELF` still detected (player overlaps the
  lifted box).
- **C4 sequencing note (from the verify):** `surface.z` is **21.68 when the button is at rest** vs
  **14.39 when the player stands on it** — `prop_floor_button` physically depresses ~7u under weight. So
  C4 must **displace the player first, then (re)compute the seat at rest height**; seating at the
  pressed-down Z would place the cube too low once the button rises. The cube's own weight re-presses it;
  the C5 dwell observes the settled state.
