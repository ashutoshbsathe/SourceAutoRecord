# Puzzlemaker Elements — v0 Scope

**Scope decision.** v0 targets only the elements an average researcher can place
in Portal 2's stock in-game **Puzzle Maker** (PeTI — Perpetual Testing
Initiative), out of the box: no BEEmod, no Hammer. Custom/community elements
(e.g. **Sendificate**) and Hammer-only logic contraptions are **P1**, explicitly
out of v0.

Why: keeps chambers reproducible by anyone with retail Portal 2, and bounds the
set of entity classes the annotation overlay + status resolver must support.

This list is the *what*; the *why* and the cross-layer scope decision (deep-narrow
vs wide-shallow, and how scope differs across annotation / observation / `.hdem` /
rollout) live in [fixed_ontology_scope.md](fixed_ontology_scope.md). That decision
extends this scope **down into the data layer** (snapshotter / hdem / rollout), which
is currently still generic.

The annotation set (`kClassColors` in `src/Features/Harness/HarnessAnnotate.cpp`)
and the status resolver (see [status_field_recon.md](status_field_recon.md))
should cover **category A** below. Classnames marked `?` are best-guess —
confirm via the recon dump.

## A. Interactive puzzle elements — discrete entities (annotate + status; v0 core)

| PeTI element | Entity classname(s) | Status we want |
|---|---|---|
| Pedestal Button | `prop_button` | pressed |
| Floor Button (weighted / cube / sphere) | `prop_floor_button`, `prop_floor_cube_button`, `prop_floor_ball_button`, `prop_under_floor_button` | pressed (`m_bButtonState`, confirmed for floor button) |
| Weighted Cube (+ Companion / Redirection / Sphere) | `prop_weighted_cube` (`m_nCubeType`), `prop_monster_box` | cube type; held-state tracked by the agent, no engine signal |
| Laser Emitter | `env_portal_laser` | on/off |
| Laser Catcher | `prop_laser_catcher` | powered |
| Laser Relay | `prop_laser_relay` | powered |
| Laser target surface | `point_laser_target` ? | powered (Hammer-side; may not appear in pure PeTI) |
| Turret | `npc_portal_turret_floor` | alive / tipped |
| Fizzler (Emancipation Grill) | `trigger_portal_cleanser` | enabled |
| Faith Plate | `trigger_catapult` | (static launch config) |
| Excursion Funnel | `prop_tractor_beam` | direction / on |
| Chamber Door (entry / exit) | `prop_testchamber_door` | open / closed |
| Cube Dropper | dropper template ? | — (track the cube it spawns, not the dropper) |
| Portals (player-created) | `prop_portal` | active, linked (`m_hLinkedPortal`), color (`m_bIsPortal2`) |

## B. Surfaces / chamber-mutating geometry — perceive, but NOT a box-able prop

These matter to the agent but don't map to a clean discrete entity + OBB, so they
need a different mechanism (read paint/projector volumes; targetname-pattern
match for brush geometry). This is the phased plan's "category 2" taxonomy —
deferred past v0 MVP.

| PeTI element | Underlying | Approach (TBD) |
|---|---|---|
| Hard Light Bridge | projected surface | projector/volume read |
| Gels: Repulsion / Propulsion / Conversion / Cleansing | paint surfaces; `info_paint_sprayer` droppers | paint-map read |
| Angled / Flip Panel, Stairs | `func_brush` (named) | targetname-pattern filter |
| Piston / Track Platform | moving brush (`func_movelinear` / track) | moving-geometry handling |
| Glass / Grating | static brush | mostly ignore (geometry) |
| Goo (toxic liquid) | `trigger_hurt` + water volume | hazard-volume read |

## C. Cosmetic — ignore

Light Strip, Observation Room, Antlines, Indicator Lights.

## P1 (out of v0)

Custom community elements distributed via BEEmod / Hammer — e.g. **Sendificate**,
and any non-PeTI logic contraptions. Revisit only once v0 (stock PeTI) works
end-to-end.
