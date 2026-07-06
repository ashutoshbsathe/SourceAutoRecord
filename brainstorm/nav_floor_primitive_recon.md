<!-- Recon on the NavSkeleton floor/stair primitive, run 2026-07-06 per goto_radical_rewrite.md §0.
Five parallel recon passes (repo ground truth, Valve .nav mesh, BSP collision lumps, runtime
trace flood, PeTI stairs/connectors reality) + synthesis. STATUS: verdict CONFIRMED — probes 1-3
all run in-game 2026-07-06 (results at bottom), all green incl. deploy-gate field. Recon COMPLETE;
flood implementation (F1-F4 phases) underway. Commits still held. -->

# Recon: what is a floor, what is a stair

## Verdict

**Runtime seeded hull-trace flood is the primitive** (a mini `nav_generate` inside the
harness), integrated in the hybrid shape: the existing BSP up-face enumeration is demoted
from truth-source to *seed list + annotation polygons*, and the `MASK_PLAYERSOLID` hull
trace — the same physics query the player's movement code issues — becomes the sole
definition of "standable". Stairs need no detector at all: a forward-trace-fails →
step-up ≤18u → recurse loop climbs brush risers, clip ramps, prop `.phy` stairs, and the
deployed PeTI stair ramp (a 26.6° slope, normal.z≈0.89) through one primitive.

## The two structural facts that kill static approaches

1. **The PeTI stairs walkable surface is an invisible `func_door` wedge whose 8 sides are
   all TOOLS/TOOLSINVISIBLE — zero `LUMP_FACES` entries.** 80 of 278 local workshop eval
   maps (including azorae stride) embed this instance verbatim. Any face-based primitive
   is structurally blind to ~29% of the corpus's stairs.
2. **No static BSP attribute encodes "real floor vs decorative ledge".** vbsp's
   contents/flags pipeline is 100% material-driven and never computes walkability.
   Floor-ness is a reachability/extent property; Valve's own answer (`nav_generate`) is
   exactly a walkable-seed + hull-trace flood.

## Candidate matrix

| Candidate | Floors | Stairs | Prop/clip collision | Verdict |
|---|---|---|---|---|
| **A. `LUMP_FACES` + smarter filters** | no ledge discriminator exists; misses playerclip/TOOLSINVISIBLE/prop `.phy` | campaign brush risers only | blind | **reject** |
| **B. Brush lumps (`LUMP_BRUSHES`/contents/SURF)** | richer candidates (playerclip, grates, glass) but still no discriminator | brush + clip ramps; no prop `.phy`, PeTI ramp is a bmodel off model 0 | no `.phy` access from the file | viable (as a *better seeder*, later) |
| **C. Valve `.nav` (`nav_generate` + parse v16)** | seeded flood in principle, but generation mask `MASK_NPCSOLID_BRUSHONLY` **excludes playerclip**; ClimbUpHeight=200 + fencetops leak ledges back | `NAV_MESH_STAIRS` exists, but generation traces **through `func_door`** (`WALK_THRU_EVERYTHING`) — the PeTI stair ramp can *never* be ground in the engine's own mesh | props ignored unless `nav_solid_props` | viable, but both headline gaps are structural, not tunable |
| **D. Runtime seeded hull-trace flood** | ledges rejected by construction (unreachable at climb ≤18–24u); trace sees everything the player collides with, at live pose | fall out of step-up recursion, no detector | yes — the trace hits `.phy`; per-entity trace filter is ours (pass `prop_testchamber_door`, keep `-stair_ramp_door` solid) | **recommend** |
| **E. Hybrid: BSP seed + flood authority** | D's correctness + enumerable state-independent seeds; never-flooded BSP candidates kept as "portal/fling-reachable-only" annotation instead of deleted | inherits D | inherits D | recommended *shape* of D |

## Why this is not the rejected flood-cleanup

The rejected design kept `normal.z>0.7` render faces as the truth source and bolted a
reachability flood **from the live player** on top — a state-dependent cleanup of a bad
primitive. Flood-as-primitive inverts that: the hull trace is the definition, seeds are
enumerable and state-independent (`info_player_start` + all BSP up-face candidates, not
the player), and BSP is demoted to seeder/annotation. It is Valve's own architecture,
minus Valve's two envelope-fatal filter choices.

State-gated connectors (retracted stairs, movers, funnels, bridges) are invisible-or-wrong
at scan time under **every** candidate — the entity name-signature gate overlay
(`-stair_ramp_door` func_door, `-lift_platform` func_movelinear, `trigger_catapult`,
`prop_tractor_beam`, `prop_wall_projector`) is orthogonal, mandatory work, and not a
discriminator between candidates.

## Cost

~150–300 LOC of rearrangement: the flood is ~80% already in-tree —
`GoToPlanner::Probe` (per-node ray+hull), `GoToPlanner::Passable` (swept-hull edge), and
a literal seeded BFS in `sar_harness_laser_reachability_test`. New work = neighbor-relative
floorZ, explicit dz≤18–24 edge gate, z-banded visited key, budget slicing, and z-band
connected-component clustering of flooded cells into NavSkeleton polygons. Comparable to
C's `.nav` parser alone, without C's ChangeLevel-reload orchestration.

## The one load-bearing unknown

**Per-trace cost is unmeasured** (3–20 µs/trace is a plausibility estimate; realistic
chambers need ~15–100k traces → 0.05–2 s). Valve budget-slices at 30 ms/frame even with a
thinner hull. Default to budget-slicing; let probe 1 decide whether a synchronous
build-at-first-go_to is safe.

## In-game probes (user-run, in de-risking order)

1. **`sar_harness_trace_bench`** (~20 LOC new command): N zero-length + N swept
   player-hull `TraceHull` calls, print µs/trace, on (a) a PeTI chamber and (b) azorae
   stride. THE gating number.
2. **TOOLSINVISIBLE solidity** on azorae stride (workshop 1522623535): stairs deployed,
   `TraceHull` straight down `MASK_PLAYERSOLID` onto the `stairs46-stair_ramp_door` ramp —
   expect hit with normal.z≈0.89; retracted — expect miss. D's stair story hinges on this.
3. **`sar_harness_dump_fields stairs46-stair_ramp_door`** in both deploy states: find the
   clean gate field (`m_toggle_state` TS_AT_TOP/TS_AT_BOTTOM vs origin delta) and whether
   `$start_deployed` inverts Open/Close semantics — feeds the gate overlay.
4. **Prop-stair march**: hull-trace up `props_underground/stair_128` (sp_a3_03) or a BTS
   hanging stair — confirm `.phy` reads as ≤18u steps or a ramp; validates the climb cap.
5. **BTS trace-down survey** (sp_a2_bts1): log hit entity class per cell — measures how
   much walkable area only D sees, confirms playerclip catwalk floors hit.
6. **Prototype flood on azorae stride**: seed at spawn; distinct surfaces at z=0/128/256
   and all 5 deployed stair instances become connectors. The acceptance test.
7. *(Optional, closes the door on C, ~10 min)*: `nav_generate` on a PeTI map with deployed
   stairs; `hexdump -C` first 16 bytes of `maps/<map>.nav` (expect `ce fa ed fe | 10 00 00
   00`); `nav_edit 1` — expect no area on the stair ramp and none on playerclip catwalks.
   If either expectation is violated, C re-enters consideration.

## Probe results (2026-07-06, azorae stride workshop 1522623535)

**Probe 1 (trace_bench) — GREEN, synchronous flood viable.** Two positions, n=10000
scattered ±256u, all three trace kinds: **1.2–1.8 µs/trace** (ray down 1.34/1.62, hull fit
1.23/1.80, hull sweep 1.17/1.59). 100k-trace projection 117–180 ms → a realistic chamber
flood (15–100k traces) is a **20–180 ms one-time cost**. Same class as the accepted
in-RENDER `.bsp` parse hitch; budget-slicing demoted from requirement to polish. The
3–20 µs unknown resolved *below* its optimistic end. Hit/startsolid counters confirmed a
genuinely varied workload (pos 2: 986/10000 points inside walls).

**Probe 2 (trace_down on the stair ramp) — GREEN, primitive confirmed.** Two separate
deployed instances (`stairs54-stair_ramp_door`, `stairs46-stair_ramp_door`), both:
`func_door`, material `TOOLS/TOOLSINVISIBLE`, normal **(0.468, 0, 0.884)** — z 0.884 vs
the predicted ~0.89; slope 27.9°. Contents `0x10000008` = GRATE|TRANSLUCENT (GRATE is in
MASK_PLAYERSOLID — the flood sees it; absent from beam masks — lasers pass through,
consistent). Plain floor control read worldspawn `metal/black_floor_metal_001c` 0x1.

Grid/cap coupling note: 27.9° slope → dz ≈ **16.9u per 32u cell** — under the engine's
18u step but with <1.2u margin. The flood's climb cap should be `kStepUp` (24u, already
in NavSkeleton), not 18u, or slope cells need finer sampling.

**Probe 3 (deploy gate + retracted state) — CLOSED 2026-07-06.** The instance wiring is
inverted (read from the BSP entity lump): `-ramp_up_relay` fires `Close` (deploy),
`-ramp_down_relay` fires `Open` (retract); the door has `spawnpos=1`, `wait=-1`,
`movedir 90` (opens by sinking into the floor). Confirmed in-game round-trip:
- **Gate field = `m_toggle_state` [dm]: 1 = deployed (closed), 0 = retracted (open).**
  Datamap-only — the snapshotter's SendTable walk misses it; the gate overlay must read
  the datamap (ReconReadField already does).
- **Retracted stairs leave a FLAT WALKABLE FLOOR, not a hole**: trace hits
  `func_brush "-brush_step_N"` (Solidity=Toggle, parented to the arm prop) flush at
  floor z, normal (0,0,1), ordinary `metal/black_wall_metal_002b` contents 0x1. The
  ramp `func_door` collision is fully gone. So deploy-state toggles a *level connector*
  on/off; the footprint itself stays standable in both states.
- Azorae stride has five instances (`stairs46/50/51/52/54`), all start-deployed.
- `sar_harness_dump_fields <name-substring>` (added for this probe) is the recon tool
  of record for entity gate fields regardless of class.
