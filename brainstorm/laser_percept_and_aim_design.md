# Lasers — powered-state percept + reflector-cube aiming verb

> **Status:** brainstorm, not a plan. Lays out the design space + the genuine forks for the two
> laser asks: (1) show a laser **catcher/relay** in the percept with a `powered`/`activated` state
> (the "child thing"), and (2) **aim a reflector cube** so its re-emitted beam hits a chosen
> catcher/relay, then **release** preserving that alignment. Read alongside
> [locomotion_tech.md](locomotion_tech.md) §5 (the original L0–L3 laser sketch — this doc supersedes
> its verb design with the teleport-aim insight), [release_place_on_button_design.md](release_place_on_button_design.md)
> (the place-on-button teleport spine we reuse), and [status_field_recon.md](status_field_recon.md)
> (the catcher/relay/target field findings).

---

## TL;DR — the thesis

**Both asks are mostly *wiring + one closed-form rotation*, not new subsystems** — because two
prototypes already shipped:

1. **The percept substrate is ~90% built.** `env_portal_laser`, `prop_laser_catcher`,
   `prop_laser_relay`, `point_laser_target` are *already* annotated marks ([PuzzleAnnotate.cpp:56-59](../src/Features/Harness/PuzzleAnnotate.cpp#L56)),
   and `point_laser_target.m_bPowered` *already* flows over gRPC ([EntitySnapshotter.cpp:164](../src/Features/Harness/EntitySnapshotter.cpp#L164)).
   The one missing piece is the **catcher/relay → child `point_laser_target` association** so the
   *catcher's own* mark reads `powered` — plus the Python projection that turns the bit into text.
2. **The aim actuator is the place-on-button teleport.** `SeatEntity(ent, origin, angles)` =
   `CBaseEntity::Teleport` ([MacroExecutor.cpp:440-447](../src/Features/Harness/MacroExecutor.cpp#L440))
   already drops a free cube at an **arbitrary position + arbitrary orientation** with zeroed
   velocity. Aiming a reflector cube is the *same teleport* with the angle computed to point the
   re-emitted beam at the catcher instead of to lay the cube flat. **This sidesteps the #1 laser
   unknown** (how a *held* cube's orientation couples to the view) — we never aim while held; we
   drop, then teleport to a solved orientation.

So the net-new work is: **(A)** a catcher→target resolver (proximity or parent handle) + one Python
projection line, and **(B)** one closed-form "rotate the cube so its redirect axis points at the
catcher" calc feeding the existing `SeatEntity`, gated by one recon spike that measures the cube's
redirect axis in its local frame. The closed-loop confirm reuses the `m_bActivated`-dwell skeleton,
reading `point_laser_target.m_bPowered` instead.

---

## The two asks, restated

| # | Ask | What it decomposes to |
|---|-----|------------------------|
| **A** | Percept: catcher/relay shows `activated`/`powered` | resolve catcher↔target (the "child thing") → stamp `powered` on the catcher mark → project to percept text |
| **B** | Verb(s): align the reflector cube to hit a catcher; release preserving alignment | spike the redirect axis → compute the cube orientation that aims the beam → place it (teleport) → confirm via `m_bPowered` |

B's *confirm* depends on A's *resolver* (it reads the target's `m_bPowered`), so A lands first.

---

## What's already there (so we don't rebuild it)

Grounding the "we can probably find the answer already" intuition — most of it is true for **A**:

- **Annotated + marked classes.** `kClassColors` ([PuzzleAnnotate.cpp:43-72](../src/Features/Harness/PuzzleAnnotate.cpp#L43))
  is the single "do we mark this?" gate ([IsHarnessMarkedClass](../src/Features/Harness/PuzzleAnnotate.cpp#L76)).
  Emitter (red), catcher (red), relay (red), target (magenta) are all in it → they already get a box
  + a stable `(index,serial)`-keyed mark.
- **The sensor bit already flows.** `point_laser_target.m_bPowered` is a curated `[dm]` field
  ([EntitySnapshotter.cpp:160-167](../src/Features/Harness/EntitySnapshotter.cpp#L160)); it rides the
  generic `repeated EntityField fields` on `EntityState` ([harness.proto:117](../src/Features/Harness/harness.proto#L117)).
  `env_portal_laser.m_bLaserOn` is `[net]`, so the SendTable walk auto-discovers it for free.
- **The actuator + the loop.** `SeatEntity`/`Teleport` (place at pos+angle), `ComputeSeat`
  (trace-find a rest spot, [MacroExecutor.cpp:220-273](../src/Features/Harness/MacroExecutor.cpp#L220)),
  `CheckFairness` (5 static gates, [:345-424](../src/Features/Harness/MacroExecutor.cpp#L345)), and the
  settle→read→dwell→`SEATED`/`NOT_SEATED` skeleton in `Release` ([:1403-1573](../src/Features/Harness/MacroExecutor.cpp#L1403)).
- **The main-thread field reader.** `EntField::getServerOffset` reads any registered datamap/net
  field synchronously — the same reader the snapshotter uses; the aim-confirm uses it for
  `m_bPowered`.

**What is genuinely *not* there:**
- **No parent-handle resolution.** Only `m_hOwnerEntity` + `m_hLinkedPortal` are shipped handles
  ([EntitySnapshotter.cpp:64-82](../src/Features/Harness/EntitySnapshotter.cpp#L64)); `m_hParent`/
  `m_hMoveParent` are not snapshotted and nothing translates a handle → mark. So catcher↔target
  association is net-new (Part A).
- **No cube redirect-axis knowledge.** We don't know, in the cube's *local* frame, which way the
  re-emitted beam exits, nor whether a *free* (dropped) reflector cube redirects at all. One spike
  (Part B, L0) settles it; the verb's aim math is closed-form once it's known.
- **No Python projection for lasers.** `_project_state` (py/p2harness/entities.py) maps cube/button
  fields → semantic state; it has no catcher/relay/target/emitter case yet.

---

## Part A — the percept (the "child thing")

A catcher/relay prop carries **no** power field; the powered bool lives on a separate child
`point_laser_target.m_bPowered` that flips while the beam strikes (`status_field_recon.md` C1/C2).
So "show the catcher as activated" = **associate each catcher/relay with its target, copy the bit
onto the catcher's mark.**

### A.1 — how to associate (the real fork)

| Opt | Mechanism | Pros | Cons |
|---|---|---|---|
| **A1 proximity (C++)** | for each catcher/relay, find the nearest `point_laser_target` within R; copy its `m_bPowered` | zero new fields; robust; ~20 LOC in the snapshot/percept build | can mispair in dense multi-catcher chambers; needs a radius constant; log when used |
| **A2 parent handle (C++)** | add `m_hParent` (or `m_hMoveParent`) as a curated handle on `point_laser_target`; resolve handle→index→catcher | exact, no ambiguity | **needs a recon spike** to confirm the target is actually *parented* to the catcher (it may just be co-located); adds the first handle→entity resolver |
| **A3 proximity (Python)** | ship the target mark + `m_bPowered` as-is; pair target→catcher in the percept builder | fastest iteration, no rebuild | duplicates association logic in Python that C++ has cleaner data for; still needs the display-merge decision |
| **A4 logic-branch mirror** | read the `*_powered_branch` `logic_branch` bool (`status_field_recon.md` I/O recon) | clean semantic bool; *also* yields "what this catcher powers" (the door edge) | heavy — snapshot logic entities, parse PeTI-vs-BEEmod names; brittle. This is the **I/O-causal-graph** frontier, not the minimal percept |

**Lean:** **A1 (proximity) as the baseline**, with A2 as a *precision upgrade gated on the spike* —
prefer the parent handle if it exists, fall back to proximity, log when proximity is used. This is
exactly the policy `status_field_recon.md` and `locomotion_tech.md` §5.2 already call for. A4 is the
right tool for ROADMAP frontier #4 (the causal graph), **not** for "is this catcher lit."

### A.2 — the display decision (separate from association)

The `point_laser_target` is *already* a magenta mark, co-located with the catcher's red box. Showing
both is redundant + noisy for the LLM. Choices:

- **Merge (recommended):** stamp `powered` onto the **catcher/relay** mark; **drop the target's own
  mark** (it's an internal sensor, not an addressable goal). The agent sees `[7] prop_laser_catcher
  state={'powered': True}` and nothing about a "laser target."
- **Keep both:** label the target as the sensor. More faithful, more clutter. Skip for v0.

### A.3 — the emitter-vs-segment wrinkle (must fix for a clean percept)

Redirected beams spawn **new transient `env_portal_laser` segment entities** with fresh indices each
tick (`status_field_recon.md` C2 note 7). Since `env_portal_laser` is in `kClassColors`, **every beam
segment currently gets its own mark** → mark churn + noise. Fix: only mark an `env_portal_laser` that
is a real **emitter** — filter by non-empty `targetname` (emitters are map-placed `laseremitNN`;
segments are nameless + transient). One predicate in `MarkTable`/`IsHarnessMarkedClass`'s caller.

### A.4 — stretch (defer): what the catcher powers

L2 stretch from `locomotion_tech.md` §5: read the catcher's `OnPowered` output target so the percept
can say *"catcher 7 powers @exit_door."* This is the button→door wiring the agent otherwise guesses —
but it's the I/O-graph frontier (#4), heavier, and **not needed to show `powered`**. Note it, build it
with the causal graph, not here.

---

## Part B — aim the reflector cube + release preserving alignment

### B.0 — the mechanics + the one real unknown

From `locomotion_tech.md` §5.1 + game truth: the reflector cube (`prop_weighted_cube`,
`m_nCubeType==2`) intercepts a beam and **re-emits along a cube-fixed axis** (a routed lens face, *not*
a mirror reflection). While **held**, the cube tracks the player view (the redirect follows your aim);
on **drop**, it freezes at the last orientation. The beam **kills the player** on contact.

The aim problem reduces to: **orient the cube so its redirect axis points cube→catcher, with the cube
intercepting the incoming beam.**

**The #1 unknown** is *how* to set that orientation. There are two control surfaces:
- **(held)** the engine couples the held cube's orientation to the player view — but the exact
  coupling (continuous? quantized? laggy? what local axis?) is unverified and engine-controlled
  ([pick_up never sets it](../src/Features/Harness/MacroExecutor.cpp#L1337)).
- **(free)** `SeatEntity`/`Teleport` sets a *free* cube's orientation **exactly and deterministically**.

**The insight that collapses the problem:** *don't aim while held.* Drop the cube (free it from the
grab controller), then `Teleport` it to the orientation that aims the beam — computed closed-form. The
held-coupling unknown evaporates; we only need to know the cube's redirect axis in its **local frame**
(a single constant), which one spike measures.

### B.1 — L0 recon spike (the gateway, do FIRST) — *what we still have to find out*

The percept "child thing" is already answered; **this** is the part that isn't. On a laser chamber,
with the existing recon discipline (`sar_harness_dump_fields` / `macro_repl`):
1. **Does a *free* (dropped) reflector cube redirect a beam?** (Leading assumption: yes — the canonical
   "place a cube to hold the beam" mechanic. Must confirm — the whole teleport approach rests on it.)
2. **The local redirect axis.** Drop the cube at known orientations; trace the re-emitted
   `env_portal_laser` segment; back out which **local axis** the beam exits along (e.g. +X / lens
   normal). → the constant the aim math needs.
3. **Is redirect independent of the incoming direction?** ("routes out a face" ⇒ yes; if it's actually
   a reflection, the math takes the incoming beam too). 
4. **Position tolerance.** How far off the beam centerline can the cube sit and still redirect? → sets
   how precise the placement must be.
5. **Is `point_laser_target.m_bPowered` a dense-enough signal**, or do we need to trace the beam
   endpoint for a gradient? (Binary `powered` can't hill-climb; see B.4.)

Deliverable: a short write-up like `status_field_recon.md`. **This decides whether B is "one
closed-form teleport" (B3) or "a hill-climbing loop" (B4).**

### B.2 — the verb shape: new verb vs overload `aim_at`/`release`

The user floated "`aim_at` or a new verb + modified `release`." Key constraint that shapes this: **you
cannot deterministically set a *held* cube's orientation** (the engine owns it). So a clean "aim while
held, then release-preserving" split is only possible via the *view coupling* (fuzzy, B5). The
deterministic path **unifies aim+place into one teleport** — which argues for a single new verb, not
two.

| Opt | Surface | Notes |
|---|---|---|
| **B-new `aim_laser <catcher>`** (recommended) | one verb: ensure-in-beam → compute orient → drop+`Teleport` → confirm `powered` | least surprise; mirrors `release`'s closed-loop; the teleport does aim *and* placement at once |
| **B-overload `aim_at`** | if holding a reflector cube and the mark is a catcher, `aim_at` orients the cube instead of the camera | overloads a camera verb with object semantics — surprising; rejected on KISS/least-surprise |
| **B-split `aim` + `release`** | an `aim` verb sets orientation; `release` preserves it | only works via the fuzzy held-view coupling (B5); the deterministic teleport makes the split artificial |

If the chamber needs the cube to **stay** put (it usually does — the beam must hold), the verb leaves
the cube placed; the "release preserving alignment" ask is then *subsumed* — release **is** the place,
at the aimed angle. The only standalone "release preserving alignment" need is dropping a
*hand-aimed* cube (B5).

### B.3 — primary actuator: teleport-place-and-orient (closed-form)

Reuse the place-on-button spine; swap the **angle policy** and the **position target**:
- **Orientation (the only new math).** Desired out-dir `d = normalize(catcher_center − cube_pos)`.
  Given the local redirect axis `â` (from L0), solve the cube world rotation `R` with `R·â = d`
  (one rotation; roll is free — pick the one that keeps the cube level, or 0). Pack into a `QAngle`.
  This **generalizes `ComputeSeat`'s angle policy**: today it's `{0, yaw, 0}` flat
  ([:270](../src/Features/Harness/MacroExecutor.cpp#L270)); for a reflector→catcher it's the aim
  rotation.
- **Position.** v0 (KISS): **orient in place** — the agent has already walked the cube into the beam
  (`go_to`/`move`); the verb drops + teleports at the cube's *current* X/Y, only changing the angle.
  Refinement: solve a point on the emitter's forward ray (clear out-line to the catcher) and teleport
  there too — but that needs the in-beam geometry; defer behind the spike.
- **Fairness/safety.** Reach-gate as `release` does. **Player safety is much easier than the §5 L3
  worry** — because we *teleport* the cube into the beam, the player never has to stand in the live
  beam. Add one gate: the chosen placement's **out-line (cube→catcher) is unobstructed** (a trace), so
  we don't claim a hit that geometry blocks.
- **Confirm.** Drop → `Teleport` → settle → read the catcher's target `m_bPowered` (via Part A's
  resolver) with the same dwell as `m_bActivated` → `POWERED` / `NOT_POWERED`.

This is the recommended primary: deterministic, ~one rotation of net-new math + the Part-A resolver,
reuses everything else, no held-coupling unknown, no player-in-beam.

### B.4 — robust fallback: closed-loop hill-climb (the original §5 L3)

If L0 says the open-loop aim is unreliable (loose redirect axis, position-sensitive, or `m_bPowered`
flips only at the very end), fall back to perturb→settle→read→retry: nudge the cube orientation
(re-teleport by ±small) and/or position, advance ticks, read `m_bPowered`, hill-climb to true. Because
`powered` is **binary** (no gradient), the climb needs a **surrogate**: trace the re-emitted beam
endpoint and minimize its distance to the catcher until the bit flips. More code; build only if B3's
open-loop aim misses in practice.

### B.5 — cheap experiment / alt: hand-aim via the view coupling, then release-preserve

The literal "two-verb" reading: hold the cube **in the beam**, `aim_at <catcher>` to point the view
(and, *if* the held cube tracks the view per L0, the redirect axis) at the catcher, then `release`
**preserving full `abs_angles`** to freeze it. This needs:
- `release` to **preserve full orientation for a reflector cube** — today `ComputeSeat` zeroes
  pitch/roll ([:270](../src/Features/Harness/MacroExecutor.cpp#L270)), which would re-aim/break a
  non-horizontal beam. Gate on `m_nCubeType==2`: keep `cube->abs_angles()` verbatim.
- the view→redirect coupling to actually hold (L0 #2/#3).

**Value:** it's the fastest way to *validate the coupling* in-game (a 1-line `ComputeSeat` tweak + an
existing verb) and a fallback if teleport-orient misbehaves. **Risk:** the held carry pose is
view-coupled and tumbled; "preserve" freezes whatever it is, and the player stands near the live beam
(danger). Treat as an experiment that informs B3, not the primary.

### B.6 — the common spine (why B is cheap once place-on-button exists)

`aim_laser` and `release`-on-button are the *same* controller — `place(pos, angle) → settle →
ReadBool(predicate) → dwell → accept/retry`:
- **place-on-button:** predicate `m_bActivated`; angle = flat; pos = button seat.
- **aim_laser:** predicate `point_laser_target.m_bPowered` (via Part A); angle = beam-aim rotation;
  pos = in-beam.
Net-new vs place-on-button: the **redirect-axis rotation** (B3) + the **out-line clear trace** + the
**Part-A predicate read**. Everything else — `SeatEntity`, the drop, the reach gate, the dwell, the
`result_code` plumbing — is built.

---

## Risks / open questions

- **Free-cube redirect (B.1 #1)** — load-bearing for the whole teleport approach. If a *free*
  reflector cube does **not** redirect (must be held), B3 collapses and we're forced onto B5
  (view-coupled hand-aim) with all its fuzz. **Confirm in the spike before committing.**
- **Redirect axis stability** — if it's face-quantized (90° snaps) rather than continuous, aiming
  becomes "pick the face + position the cube" — a more constrained but still teleportable problem.
- **Beam kills the player** — minimized by teleporting the cube (player out of beam), but the
  *out-line clear* + *in-beam* placement traces must run before the teleport; never leave the player
  standing in the beam during a B5 hand-aim.
- **Association mispairing (A1)** — proximity can pair a target to the wrong catcher in dense
  chambers; prefer the parent handle (A2) if the spike confirms parenting, and log proximity use.
- **Relay vs catcher semantics** — catcher latches while struck; relay can toggle/retrigger. The
  dwell-confirm handles both, but tune the dwell so a relay's toggle doesn't read as `NOT_POWERED`.
- **Multi-hop beams** (chained through several cubes) — P1, out of v0 (single emitter / cube /
  catcher, one hop).
- **`m_bPowered` is `[dm]`** — already curated for the target; if A2 reads `m_hParent`, that's a new
  curated handle field (cheap, but a `make`-and-verify).

---

## Forks for you to decide (before this becomes a plan)

1. **Association (A.1):** proximity baseline (A1) now, parent-handle (A2) as a spike-gated upgrade —
   or go straight for the parent handle? *(Lean: A1 now, A2 if the spike shows parenting.)*
2. **Display (A.2):** merge the target's bit into the catcher mark + drop the target mark, or show
   both? *(Lean: merge.)*
3. **Verb shape (B.2):** one new `aim_laser <catcher>` verb (deterministic teleport), or the
   two-verb hand-aim split (B5)? *(Lean: one verb; B5 only as a validation experiment.)*
4. **Aim path (B.3 vs B.4):** open-loop closed-form teleport first, hill-climb only if it misses —
   or build the closed loop up front? *(Lean: open-loop first; the spike decides.)*
5. **Position (B.3):** v0 "orient in place" (agent positions the cube), or solve the in-beam point
   too? *(Lean: orient-in-place for v0.)*

A proposed phased build (L0 spike → A-resolver + percept → B reflector-aim verb → Python grammar)
follows the many-small-phases / C++-before-Python pattern once you pick the forks — I left it out
deliberately so we align on the design first.
