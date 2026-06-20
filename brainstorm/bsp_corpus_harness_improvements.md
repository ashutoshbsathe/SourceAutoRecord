# BSP corpus → harness improvements — the §8 recon battery, executed for real

This is [offline_map_preprocessing.md](offline_map_preprocessing.md) **§8 recon battery EXECUTED**
against the **whole 277-map workshop corpus** (not the one training BSP the doc scoped). Where §8
proposed go/no-go probes on a hand-traced chamber, this doc runs them at scale: parser, causal
graph, affordance prior, portalability, exit oracle, VScript ceiling — all measured. The corpus
tool is `py/bsp_recon/dump_ents.py` (per-map entity JSON + `*.geo.json` + `index.json`).

It also **corrects a load-bearing premise**: §4/§8 assumed *our* chambers are "PeTI-vanilla, no
VScript" (fork #4, P0b). For this corpus that is **false** — all 277 maps are BEEmod/PeTI with
`vscript:true` and io-proxy indirection. The corrected picture (and why it barely matters) is §4
below.

> **Numbers discipline:** every figure here survived an adversarial verifier. Where the first-pass
> analyst was wrong, the **corrected** number is used and the original is dropped. Refuted claims are
> cut. A few are marked *(unverifiable)* — trust them only as direction, not decimals.

> **Confound discipline (carried verbatim from fork #7 / §2d):** percept may carry **FACTS**
> (`door L: blocked_by cube 5`, `this brush is a flip panel`), **never the CONCLUSION**
> (`press button 3`, `button 3 → door 7 opens`). The default consumer of the causal graph is the
> **experimenter** (ground-truth, eval construction, scoring). Model-facing wiring is a *dialled
> hint-ablation*, never a baked-in assist.

---

## 1. TL;DR + lay of the land

**The recon battery comes back GREEN on its headline buys, with one premise corrected.** Across 277
workshop maps:

- **The I/O causal graph is statically recoverable.** The thing §8/R2.1 calls "the money test"
  passes: BEEmod's `func_instance_io_proxy.OnProxyRelayN` re-emission targets are **baked into the
  dump at compile time, not set at runtime** — verified in **100% of maps** (every map's proxies own
  static `OnProxyRelayN` outputs; 6,353 proxies, 21,172 static relay outputs). A proxy/relay/counter-
  chasing BFS resolves **~56% of buttons** (315/564) and **~53% of laser catchers** (169/319) to a
  concrete door/fizzler/mover/funnel; **75% of button-bearing maps** (137/182) have ≥1 fully-recovered
  source→effect edge. The residual gap is `!self`/`@global` refs + a thin tail of cross-instance names
  — **not runtime rewiring of puzzle logic.**
  > *BFS is spec'd in §5 (sources/sinks/gate classes, depth bound, chain-counting). The headline
  > percentages reproduce only against that spec — the per-class hit-counts (315/564, 169/319) are the
  > load-bearing figures; the rolled-up "75%" is spec-sensitive (naive all-edge counting drifts to
  > 128–257/277). Cite the per-class numbers, treat the rollup as direction.*
- **Exit detection is a solved, near-perfect anchor.** `@relay_pti_level_end` is present and output-
  driven in **277/277 maps (100%)** — the single OR-set primary key the shipped `PuzzleExit` oracle
  already latches on. The only failure mode is **7 stitched multi-room maps (2.5%)** carrying >1 such
  relay (latch-once false-positive).
- **Portalability is the one non-dominated geometry payload, but the bare flag is a trap.** The
  `SURF_NOPORTAL` bit alone is an **~89%-false-positive** portal-placement oracle (per-map median;
  goo/sky/fizzler-planes/grating/glass all read portalable). A *material-filtered* white-tile panel map
  is real and tiny (~0.5 KB/map, median ~29 coalesced panels) and is information runtime A\* **cannot**
  synthesize.
- **VScript taint is real but narrow.** `vscript:true` fires on **277/277** and is useless as a gate.
  The honest ceiling is **29 maps (10.5%)** shipping custom `inject_*.nut` (`RunScriptFile`'d at
  runtime) → graph *may* be silently incomplete. **86.6% of maps are HIGH-confidence** (only
  framing/cosmetic Squirrel).

**The census (verified):**

| Quantity | Value |
|---|---|
| Maps | 277 |
| Entity count / map | min 183, p25 569, **med 708**, p75 907, p90 1311, max 2429 |
| Edge (I/O) count / map | med 518, max 3450 |
| Distinct stock mechanics / map | **median 6** (mode 6 = 89 maps; 7 = 31 maps; single-mechanic = 2 maps) |
| Scaffolding/decoration share | **~90% broad** (median 90.1%) / ~52% narrow (lights+vgui+relay+proxy) |
| `func_instance_io_proxy` (corpus) | 6,353 |
| `logic_relay` (corpus) | 17,756 |
| `vscript:true` | 277/277 (non-discriminating) |
| Maps embedding custom `inject_*.nut` | **29 (10.5%)** |

The puzzle is **a needle in a decoration haystack**: ~700 entities/map, but only ~1 cube, ~3 buttons,
~2 real fizzlers, ~2 lasers actually *do* anything. A player-facing census must hard-filter to the
stock ~14 classes (`kClassColors` + panels/gels/bridges).

---

## 2. The PuzzleMaker dialect — archetypes, distributions, analogies

**This corpus is monolingual: 100% BEEmod/PeTI, zero raw Hammer.** Every map carries vscript, ~90%
of entities are scaffolding, and puzzle causality runs `button.OnPressed → proxy.OnProxyRelayN →
real target`. Any percept/reasoning harness must treat `io_proxy` + `logic_relay` as **transparent
indirection hops**, not real entities.

**Mechanic prevalence (maps with ≥1, verified):**

| Mechanic | Maps | % | Entity count note |
|---|---|---|---|
| Fizzler (`trigger_portal_cleanser`) | 277 | **100%** | 2,419 raw — inflated by brush-splitting + door-fizz |
| linked_portal_door (BEEmod portal-conjuring) | 233 | **84%** | 1,015 total, med 4 among present, max 10 |
| Weighted cube | 227 | **82%** | med 1/map (med 2 among present), mean 2.2, max 30 |
| Laser | 139 | **50%** | ~394 ents (catcher+relay+target) — *not* 597 |
| Portal gun (`weapon_portalgun`) | 138 | **50%** | half the corpus solves *without* a gun |
| Funnel (`prop_tractor_beam`) | 115 | 42% | 166 ents |
| Light bridge (`prop_wall_projector` emitter) | 98 | 35% | 160 ents |
| Faith plate (`trigger_catapult`) | 71 | 26% | 402 ents |
| Turret (`npc_portal_turret_floor`) | 30 | 11% | **235 ents** (not 30) |
| Gel (`paint_sphere`) | **5** | **1.8%** | 273–326 spheres each — *not* 15% |

**Cube types (n=618 with `CubeType` kv; 76 without):** standard 200, **reflection/laser 159**,
companion 114, sphere/edgeless 41, antique 20.

**Vivid analogies (the dialect, made memorable):**

- **The median chamber is a 1-cube, 3-button, 2-fizzler optics-or-portal room wearing a 700-entity
  costume.** ~90% is stage lighting, video screens, and noportal paint; only ~30 entities actually
  do anything.
- **Fizzlers are punctuation, not sentences.** A PeTI "fizzler" explodes into 3+
  `trigger_portal_cleanser` brushes plus an auto `-fizz` on every door. The raw 2,419 / median-6 count
  is *letters when you meant words* — de-dup by targetname prefix before reasoning about cardinality.
- **linked_portal_door is the corpus's pronoun for "portal."** 84% of maps pre-conjure portals as
  connections, and **half the corpus never hands the player a portal gun.** Portals here are mostly
  furniture, not a tool.
- **This is a laser-relay civilization, not a block-pushing one.** Lasers (50%) beat every other
  "interesting" mechanic; the most common cube *type* is the mirror cube; `ref_cube_laser.nut` ships on
  ~29 maps. Think "redirect the beam," not "stack the boxes."
- **io_proxy + logic_relay are the grammar particles.** 6.3k proxies + 17.8k relays mean every "A
  presses, B opens" is really "A → -proxy.OnProxyRelayN → relay → B." Reading causality is reading a
  language where every verb routes through a postal forwarding address.
- **Gel maps are a different *micro*-dialect.** Not a 1-in-6 phenomenon — only **5 maps (1.8%)**, mostly
  2–3 gelocity variants. But where it shows up it shows up by the hundreds (gelocity: 326 spheres); a
  "gel accent" spoken fluently by ~5 maps and not at all by the other 272. Plan it as a rare special-case,
  not a corpus-wide surface problem.

**Archetype labels (for eval stratification):** laser, funnel, gel, portal-conjuring, light-bridge,
faith, minimalist. Most maps are **combinatorial** (laser+cube+funnel+fizzler), not single-gimmick —
median 6 distinct mechanics; only 2 maps are true single-mechanic.

**Census traps (must de-scaffold before reasoning):**
- `prop_testchamber_door` (median 6/map) bundles **entry + exit + every BEEmod connection door** — not
  6 puzzle gates.
- Raw `trigger_portal_cleanser` counts triple-count one editor fizzler + auto door-fizz.
- `io_proxy`/`logic_relay` must be excluded from any "element inventory" — they are indirection, not
  elements.

---

## 3. The offline → online OPPORTUNITY MAP

The complementarity line (from §2's table, validated): **offline owns static structure + causal wiring
+ affordance envelopes + portalability; runtime owns realized dynamic state** (door open?, catcher lit?,
portals where?, cube where?). Below, grouped by family. **Effort** S/M/L. **Confound risk** is the
fork-#7 dial: how much exposing the payload to the *model* trivializes the reasoning eval.

> **Effort scale:** **S** = <1 day (recon + dump-field change), **M** = 2–5 days (recon + new code +
> test), **L** = 1–2 weeks (full BFS + geo.json parsing + validation). **Confound scale** (model-observable
> trivialization): **none** = <5% (experimenter-only, or correctness fix), **low** = 5–20% (affordance
> prior — *what is here*, not *what to do*), **MEDIUM** = 20–50% (exposes state that's also frame-readable),
> **HIGH** = >50% (hands the solution). The §6 ordering is `(value × confidence) / (effort × confound)`
> under these scales — experimenter-side payloads dominate because their confound is `none`.

> **Honesty about runtime A\*/telemetry domination:** the static occupancy grid and region graph are
> **DOMINATED** by runtime A\* hull-traces (which see *current dynamic blockers*) and are **not
> proposed** — exactly §2a/§2d's parked verdict, re-confirmed.

### Causal graph

| Mechanism | Offline (BSP recon) buys | Runtime already owns | Online delivery path | Effort | Confound |
|---|---|---|---|---|---|
| button/laser → effect wiring | resolved `src → effect` edge-list (proxy/relay/counter-collapsed), per-edge kind+depth+gate-arity | current effect *state* (door open bit, fizzler on/off, catcher powered) — never *what controls it* | JSON sidecar; experimenter holds GT "mark 3 (button) → mark 7 (door) Open"; **model-facing only as dialled hint** | M | **HIGH** if model-facing at full-target; none as experimenter oracle |
| AND-gate arity | `math_counter.max` = "press both buttons" arity, statically | nothing | sidecar field on the edge | S | low (facts) |

### Exit detection

| Mechanism | Offline buys | Runtime already owns | Online delivery path | Effort | Confound |
|---|---|---|---|---|---|
| chamber-complete anchor | exact per-map `@relay_pti_level_end` targetname(s) + count | the shipped `PuzzleExit` OR-set latch → `chamber_complete` bit | hand harness the exact relay name at reset → exact-match instead of global OR-set; for the 7 multi-relay maps, latch on the **N-th / terminal** relay | S–M | **none** (completion is experimenter-side; agent sees only the bit) |
| smoke-test assertion | GT that `SIG_PTI_LEVEL_END` must fire on 100% of PeTI maps | — | `agentloop_smoke.py` asserts the bit on completion → catches template name-drift | S | none |

### Dynamic geometry (movers, fizzlers, lasers, funnels)

| Mechanism | Offline buys | Runtime already owns | Online delivery path | Effort | Confound |
|---|---|---|---|---|---|
| mover envelope | **100% recoverable** movedir/movedistance/speed/startposition (+ closed AABB) → exact swept volume of folding stairs / extending bridge | live OBB *now* | affordance row joined to live entity by targetname; render "extends 60u along −Z, currently retracted" | M | **MEDIUM** (affordance = low; the resolved *gate edge* = high → separate ablation tier) |
| fizzler gate root | proxy-chased physical root (button/laser/trigger) for the gated subset; always-on exit-fizz correctly flagged (0 incoming edges) | fizzler on/off | same affordance table | M | medium |
| trigger-volume gates | the trigger AABB + `filter_activator_class` ("extends when a CUBE enters region X") — reclaims most "unresolved" movers | trigger occupancy | report occupancy live | S | medium (region-trigger IS often the puzzle) |
| faith-plate aim | `launchTarget` origin (298/299 resolve) or `launchDirection`+`playerSpeed` → **exact parabola endpoint** | — | "this plate throws you to (x,y,z)" | S | medium |
| **laser emitter → catcher** | **NOT in the I/O graph** — `env_portal_laser` emits 0 output edges (203/203). Spatial only: origin+angles **raycast** vs catcher AABBs (one portalable reflection) | catcher powered bit | offline raycast pairs + graph-side catcher→effect | **L** | **HIGH** (hands the whole laser-routing solution) |

The laser chain decomposes cleanly: **[spatial: emitter→catcher] + [graph: catcher→effect]**. The
graph half is high-confidence; the spatial half is the only mechanism whose gating is fundamentally a
geometry problem, not a graph problem — keep it behind an ablation flag or omit.

### Geometry + portalability

| Mechanism | Offline buys | Runtime already owns | Online delivery path | Effort | Confound |
|---|---|---|---|---|---|
| **white-tile portalable panel map** | material-filtered portalable surfaces, coalesced to 128u panel-cells, stable geometry-hash panel-IDs (`S88` namespace, reorder-stable, 0 material collisions) | **ZERO portalability** — A\* gives collision but not "can a portal land here" | load panel map at reset (CRC-keyed); answer "where are portal surfaces"; live `TraceFirePortal` confirms conditional flip-panels only | M | **low-med** (affordance prior; still PSPACE to reason *where/how* to chain) |
| material-class enrichment | tag each face real_portal_tile / goo / sky / fizzler / grating / glass / black_noportal / trim / tool | bare `portalable` bool | dump-side enrichment in `extract_geometry` → fixes the ~89% FP at source | S | none (correctness fix) |
| occupancy grid / region graph | — | **runtime A\* dominates** (dynamic blockers) | **not proposed** | — | — |
| areaportal region label | corrected recon: areaportals exist (87% of maps, **85%+ of 242 `func_areaportal` carry NO_TARGETNAME**); the **door-seal** semantics are *inferred from targetname patterns* (`*_door_areaportal`), **not yet read from the BSP AREAS lump** → tentatively no room graph | — | **build nothing** *(pending the AREAS-lump check below)*; if left/right ever needed, hand-authored AABB label Python-side (§2d, P0d-gated) | S | n/a |

### Annotation (percept completeness)

| Mechanism | Offline buys | Runtime already owns | Online delivery path | Effort | Confound |
|---|---|---|---|---|---|
| folding panels / stairs / movelinears | GT label table for the ~1,499 named panel/stair `func_brush` (115 maps) + **543 `func_movelinear`** the classname-walk drops | boxes the 18 `kClassColors` fixtures (7,336 ents) perfectly | extend `PuzzleAnnotate` to box/mark by entity-index/targetname (not classname); MarkTable gives them stable marks | M | **LOW** (reports a visible affordance) |
| gel / light-bridge surfaces | resolve gel splat footprints + bridge planes → world quads; gel TYPE from sprayer kv / material (the sphere carries no readable paintType) | nothing (surfaces, not boxable) | translucent colored quads via the A5 OverlayRender primitive, gated on `sar_harness_annotate` | M | LOW |
| status badges co-located with marks | value→meaning map (already in `status_field_recon.md`) | `[net]` status fields | small glyph next to each mark; needs snapshotter to register the `[dm]` fields; **door-open stays the deferred holdout** | M | LOW (current observable state = same as the frame) |
| antlines as built-in wiring viz | recon: 9,593 `info_overlay_accessor` (260 maps), 472 `prop_indicator_panel` (173 maps) link panel→antline via kv | — | the "partial" hint tier = highlight the antlines the game *already draws* (low-confound) vs full mark→mark arrows (high) | S | low (partial) / **HIGH** (full arrows) |
| causal wiring arrows mark→mark | the resolved edge-list projected onto marks ("mark 3 → mark 7 [opens]") | — | top notch of the hint-ablation dial, **never default** | M | **HIGH** (hands the puzzle's wiring) |

**The annotator's blindness is asymmetric in the worst way:** it boxes the FIXTURES (buttons, doors,
lasers) perfectly but misses the TERRAIN the agent walks on (extending stairs, flip panels, gel,
bridges). For a movement-planning agent that's the wrong half to miss — you can *see* a door in-frame,
but you can't tell from a frame whether that ramp is solid or whether that floor is bounce-gel.

---

## 4. The VScript-taint reality — the corrected ceiling

**Correcting the doc's premise.** [offline_map_preprocessing.md](offline_map_preprocessing.md) §4/§8
(P0b, R2.4) assumed the in-scope chambers are "PeTI-vanilla (no VScript) → complete graph." For this
**workshop corpus that is false**: `vscript:true` is **277/277**. But the flag is a *smoke detector
wired to the toaster* — it trips on stock PeTI plumbing (`@video_splitter`, `@transition_script`,
`@glados`, `voting_dialog`) every map ships. Using it as the taint gate would force a human glance at
100% of the corpus while the true suspect set is **10.5%**.

**The honest 3-tier ceiling (per-map `causal_confidence`):**

| Tier | Maps | % | Meaning | Graph trust |
|---|---|---|---|---|
| **HIGH** | 239–240 | **86.6%** | only framing/cosmetic Squirrel (elevator transitions, video splitter, glados VO, cube-dropper auto-dissolve) | **fully baked → trust as GT** |
| **MEDIUM** | 8–9 | ~3% | readable puzzle-vocab `RunScriptCode` (death-fizzler counters, sendtor) but no inject nut | trust + log residual edges for a glance |
| **LOW** | **29** | **10.5%** | ships custom `inject_*.nut`, `RunScriptFile`'d at runtime — static trace dead-ends | **completeness unprovable → fall back to runtime status fields** |

**Why it barely dents the headline buy:**
- Rewiring inputs corpus-wide: `RunScriptCode` 5,055, `AddOutput` 824, `CallScriptFunction` 107,
  `RunScriptFile` 51. **~95% of these are framing-glue or cosmetic** — the entry/exit elevator transition
  state machine and cube-dropper `OnUser1 !self:Dissolve`, mechanically identical across maps and
  trivially signature-whitelisted.
- The genuinely dangerous primitive is `RunScriptFile(inject_*.nut)` — **51 edges across 15 maps**,
  always attached to `prop_weighted_cube !self` scopes (custom cube behaviors). This is the irreducible
  ceiling, and the **LOW set ⊇ the tier-3 set** (every RunScriptFile map embeds an inject nut), so the
  whole confidence flag collapses to **one cheap static test**: *does the pakfile embed a script under
  `scripts/vscripts/inject/`?*
- **One map deliberately evades the parser**: `3388131850875293306/1389752009` char-builds its target
  classname via `(108).tochar()+(105).tochar()+...` = `"light"` and `EntFireByHandle`s a runtime-computed
  target. Decoded it only touches `light` (cosmetic), but it proves undecidable endpoints exist in the
  wild → any `tochar`/concat-built `EntFire` target should **force-LOW** regardless of apparent target.

**Important asymmetry:** once a map can `RunScriptFile` an opaque nut, static recovery flips from a
sound *over*-approximation to a possible *under*-approximation (it may **miss** edges) — strictly worse
for a planner than drawing extra edges. So the LOW set must be gated **out** of causal-oracle evals, or
sourced from runtime status fields.

**The fix (one offline field):** replace the binary `vscript` flag with a 3-tier `causal_confidence` in
`index.json` + a boilerplate-signature whitelist so the recovered graph **drops** the ~95% cosmetic
rewiring noise instead of flagging whole maps.

**Experiment decision rule for LOW maps (so "gate out" is unambiguous):**
1. **Primary:** exclude the 29 LOW maps from causal-oracle ground-truth evals entirely — the static
   graph may *under*-approximate, so it cannot serve as GT.
2. **Allowed:** run them in a **separate, reported bucket** scored from runtime `[net]`/`[dm]` status
   fields only (no graph GT), so the status-only baseline stays visible rather than being silently dropped.
3. **Never:** silently trust an incomplete LOW graph as if it were HIGH, and never quietly swap LOW maps
   for HIGH ones inside a fixed eval split (that biases archetype balance — §6(6)).

---

## 5. Concrete recoverable wiring (real chains, with map ids)

All verified by the adversary. These are the §8/R2.1 "money test" hits, at scale:

**The resolver BFS (spec — so the percentages above are reproducible):**
- **SRC (counted as a "button/catcher"):** `prop_floor_button`, `prop_under_floor_button`,
  `prop_button`, `prop_laser_catcher`/`prop_laser_relay`. **SINK (a "concrete effect"):**
  `prop_door*`/`func_door*` (incl. `func_door_rotating` flip-panels), `trigger_portal_cleanser`,
  `func_movelinear`/`func_door` movers, `prop_tractor_beam`, `prop_testchamber_door`.
- **Transparent hops (chase through, don't count as endpoints):** `func_instance_io_proxy`
  (`OnProxyRelayN`), `logic_relay`, `math_counter` (`OnHitMax`/`OnChangedFromMin`), `logic_branch`.
- **Depth bound 8** (deepest real chain observed = 6, see the stairs case below); cycles guarded by a
  visited-set on `(targetname, output)`.
- **Counting:** a SRC is "resolved" iff ≥1 path reaches a SINK class (unique-SRC count, **not** total
  edges — naive total-edge counting is what produces the unstable 128–257 rollup). `!self`/`@global`/
  cross-instance unresolved targets terminate a branch without counting.


**Button → fizzler (depth 3):**
`1008186498344588142/1587302363` — `bt46-btn` (prop_floor_button) `--OnPressed/OnUser1-->` `bt46-proxy`
`--OnProxyRelay3-->` `lg47` (math_counter) `--OnChangedFromMin-->` `fiz15-brush`
(trigger_portal_cleanser) `.Disable`.

**Proxy static-output proof (the whole thesis in one entity):** same map — `lc24-proxy`
(func_instance_io_proxy) *statically owns* `OnProxyRelay3 → fiz21-mdl.SetAnimation(open)`,
`OnProxyRelay4 → fiz21-brush.Activate`, `OnProxyRelay5 → fiz21-brush.Enable`,
`OnProxyRelay8 → fiz21-brush.Disable`. **The re-emission map is in the dump, not set at runtime.**

**Button → stairs/mover (depth 6, the deep gated-platform case):**
`1014940448863302055/1583330483` — `bt308-btn` `→ bt308-proxy --OnProxyRelay4→` `lg310` (logic_relay)
`→ db351-proxy → db351-counter` (math_counter) `--OnHitMax→` `db351-branch_toggle` (logic_branch)
`→ db351-lower_clip` (func_movelinear) `.Open`. Envelope: movedir `-90 -90 0`, movedistance 60, speed
120, startposition 0, closed AABB `[[-30,-64,-34],[30,64,34]]` — i.e. *folding stairs, statically
recovered with exact swept geometry.*

**Button → excursion funnel (depth 3):** same map — `bt308-btn → bt308-proxy --OnProxyRelay1→` `lg372`
(math_counter) `→ tb309-tbeam` (prop_tractor_beam) `.Enable`.

**Button → flip-panel door (depth 3):** `1009311134768350451/1585960398` — `bt56-btn → bt56-proxy
--OnProxyRelay5→` `lg58` (math_counter) `→ fp57-flipping_panel` (func_door_rotating) `.Close`.

**Laser catcher → fizzler:** `1026203402972095304/archetype7` — `catcher_main` (prop_laser_catcher)
`Enable/Disable → FizzlerVertical` (trigger_portal_cleanser).

**Laser catcher → chamber exit:** `103980755816358218/1433602858` — `lasercatch5-laser_catcher →
@exit_door` (prop_testchamber_door) — the catcher gates completion.

**Laser → lift platform (mover):** `271725976964019239/1396822117` — `lasercatch118-laser_catcher →
pistonlift30-lift_platform` (func_movelinear).

**Non-BEEmod direct wire (depth 1):** `1763691615833569319/portal_diversity_part2` — a
`prop_floor_button → chamber05_train` (func_tracktrain) `.SetSpeed`. (One of the rare custom-Hammer
direct wires.)

**Always-on exit fizzler — correctly NOT a puzzle gate:** `1008186498344588142/1587302363` —
`doorexit2-fizz` has **0 incoming I/O edges** (one outgoing cleanup edge). The resolver must not treat
it as a controllable gate.

**Reference / archetype maps:**
- Near-median composite: `1013814283954742337/1585435718` (ec 635: 2 cubes, 4 buttons, 8 doors, 3
  fizzler brushes, 4 linked_portal_door).
- Laser heavyweight: `542928347336135119/3sat` (ec 2268: 30 cubes mostly reflection, 3 laser ents).
- Gel sub-genre: `53247938654329108/sp_gelocity_3_v02` (326 spheres, 0 cubes/lasers).
- Funnel archetype: `41975171609715706/1406896028` (ec 1727, 7 cubes, no laser/gel).
- Portal-conjuring archetype: `900989592159567671/1361740370` (ec 1289, 0 cube/laser/gel; solved via
  connections, no portal gun).
- Minimalist floor: `919012515970124784/m01v2` (ec 192, 1 cube, 3 buttons, 2 fizzlers).

---

## 6. Prioritized recommendations

Ordered by **(value × confidence) / (effort × confound)**. The split: **experimenter-side ground-truth**
(safe, build now) vs **model-facing ablation** (gated, measured) vs **park**.

### Build first — experimenter-side ground-truth (zero/low confound, high value)

1. **`causal_confidence` 3-tier field + inject-nut gate (S).** The single cheapest, highest-leverage
   change: one static test (`scripts/vscripts/inject/` in pakfile) collapses the useless binary
   `vscript` flag into HIGH(86.6%)/MEDIUM/LOW(10.5%). Gates every downstream causal-oracle use; quarantines
   the 29 LOW maps. **This is the most important single deliverable** — without it, the whole graph's
   trustworthiness is unlabelled.
   **Online delivery path (so the field is actionable, not decorative):**
   - **Lives** as a per-map field in the `index.json` sidecar `dump_ents.py` already emits, keyed by the
     same map id; CRC/hash-keyed so a recompiled BSP that no longer matches falls back to `unknown`.
   - **Harness load:** the experiment harness reads the sidecar at `Reset` (the same hook that loads the
     panel map / exit-relay name); it is **never streamed to the model**.
   - **Conditional sampling logic:** causal-oracle GT and `[dm]` scoring **include HIGH+MEDIUM only**;
     **exclude LOW** (its graph may *under*-approximate — §4). Archetype/difficulty sampling may still
     draw LOW maps for non-causal evals (e.g. pure movement), just without graph-derived ground truth.
   - **Failure mode (field missing / `unknown`):** treat as LOW for causal scoring (conservative —
     never silently trust an unlabelled graph), but still usable for runtime-status-only evals.

2. **Per-map causal edge-list JSON sidecar (M).** Run the proxy/relay/counter + self-FireUser BFS once
   per HIGH/MEDIUM map; emit `{src, effect, effect_class, depth, gate_arity}`. Experimenter holds GT
   "button mark 3 → door mark 7." 56% buttons / 53% catchers / 75% of button-maps recovered — a real,
   usable oracle for scoring causal reasoning. **Default consumer = experimenter only.**

3. **Boilerplate-signature whitelist (M).** Drop the ~95% cosmetic rewiring edges (elevator/transition/
   video/glados/vote glue, cube-dropper dissolve) so even MEDIUM maps present a clean button→door graph
   plus a short residual list. Pure offline post-processing.

4. **Offline exit-relay resolver (S–M).** Hand the harness each map's exact `@relay_pti_level_end`
   name(s) + count at reset; switch `PuzzleExit` from a global hardcoded OR-set to an exact match, and
   fix the **7 multi-relay false-positives** by latching on the terminal/N-th relay. Falls back to
   today's OR-set when no sidecar. + Add the `SIG_PTI_LEVEL_END`-fires-on-100% assertion to
   `agentloop_smoke.py`.

5. **Material-class enrichment in `dump_ents.extract_geometry` (S).** Tag every face's portal-class
   (real_tile/goo/sky/fizzler/grating/glass/black/trim/tool). Fixes the ~89% bare-flag FP **at the
   source** so every downstream consumer sees honest portalability. Pure correctness, no model exposure.

6. **Archetype + difficulty stratification metadata (S).** Emit per-map archetype label + (entity-count
   percentile, distinct-mechanic count, max chain depth). Lets the eval sample balanced across
   laser(50%)/funnel(42%)/gel(1.8%)/portal-conj/bridge/faith/minimalist + difficulty deciles instead of
   overfitting the dominant laser bucket. **Never seen by the model** — pure experiment design.

### Build next — percept completeness (LOW confound, on-by-default to make the percept honest)

*Ranked within-tier by `impact × commonality` — the asymmetry in §3 says missing TERRAIN hurts a
movement agent more than missing fixture-state, so panels lead, badges trail.*

7. **Folding-panel/stair + movelinear GT boxing (M). [highest within-tier]** Close the A2 category-2 gap:
   box/mark the ~1,499 named panel/stair `func_brush` (115 maps) + 543 `func_movelinear` via offline
   targetname GT. The agent stops trying to walk through un-annotated flip panels. **Low confound —
   reports a visible affordance, not a solution.** *Expected impact: largest of the three — this is the
   "walk through solid terrain" failure mode on 115 maps; predicted big drop in mis-routes (verify by
   spot-check on 3 panel-heavy maps before scaling).*

8. **Gel/light-bridge surface overlays (M). [mid]** Translucent colored quads (blue/orange/white gel,
   cyan bridge) via the A5 OverlayRender primitive, for the gel/bridge maps. Equivalent to making the
   already-visible-but-unlabelled surface legible. *Expected impact: moderate but **narrow** — bridges
   span ~35% of maps (real reach) yet gel is only 1.8% (5 maps), so the gel half is a rare special-case,
   not a corpus-wide win.*

9. **Status badges co-located with marks (M). [lowest within-tier]** Glyph for button-pressed / catcher-
   powered / portal-linked next to each mark, from already-resolved `[net]` fields. Gated on the
   snapshotter registering the `[dm]` fields; **door-open stays the deferred holdout.** Low confound
   (current observable state). *Expected impact: smallest — the percept frame already *shows* most of
   this state; the badge is legibility, not new information. Run §7(6) before committing the `[dm]`
   snapshotter work.*

### Model-facing — only as a dialled, measured hint-ablation (HIGH confound)

10. **Wiring hint dial (M).** Inject the edge as a textual percept at graded levels:
    **none → kind-only → antlines-highlight → full mark→mark target arrows.** *Full target is an
    upper-bound oracle, never on by default* — it hands "button 3 → door 7" and largely solves single-
    gate chambers for the model. The honest condition is kind-only / antlines-partial.

11. **Laser emitter→catcher raycast (L). Future ablation only — do NOT build for the default hint set.**
    Handing the model offline emitter↔catcher pairs supplies *where to aim / where to place a portal*,
    i.e. the laser-routing solution itself. **The confound is not yet quantified** (no laser-map sample
    has measured the solve-step reduction); the "hands the entire solution" claim is an *a priori*
    structural argument, not a measured number. **Reserve for a future upper-bound-oracle experiment**
    that measures it (predicted: given emitter+catcher pairs, laser-heavy maps collapse to near-trivial
    routing) — and it must **never** ship in the default percept regardless of the measured value.

### Park (dominated or dead)

12. **Static occupancy grid / region graph — PARK.** Dominated by runtime A\* (sees dynamic blockers);
    `nav_generate`/watershed break on portal/fling connectivity; a wrong region graph poisons the eval.
    Re-confirms §2a/§2d. **Amend §2d:** areaportals are present (mostly unnamed) and *inferred* to be all
    door-seals, not "never emitted." Park is sound **only if** the AREAS-lump check (§8 #9) confirms no
    areaportal partitions a playable room; until then the verdict is provisional.
13. **Brute-force save/load solver — PARK (record-only).** PSPACE-complete core + ~4.3 s/node warmup →
    shallow chambers only; pays off only if automated chamber generation lands. The causal graph + atom
    novelty features are its prune, recorded for §7.

---

## 7. Long-term experiments

**Dependency gate (which experiments are blocked on which §6 deliverables):**

| Experiment | Blocked on (§6) | Feasible now? | Workaround if blocked |
|---|---|---|---|
| 1. Hint-ablation curve | (2) edge-list sidecar, (10) wiring dial, partial (8) badges | only after (2)+(10) land | hardcode edges from the §5 spot-checked maps (small N, high variance) |
| 2. Completeness A/B | (7) panel/movelinear boxing, (8) gel/bridge overlays | only after (7)+(8) land | run on the subset already classname-boxed (fixtures only) — weaker contrast |
| 3. Difficulty stratification | (6) archetype/difficulty metadata | **yes** (metadata is pure offline) | n/a |
| 4. LOW-map nut audit | (1) `causal_confidence` (to name the 29) | **yes** (one-time pakfile read) | n/a |
| 5. HIGH-map trust calibration | (1) field + (2) edge-list | after (1)+(2) | n/a |
| 6. Status-badge value test | (9) badges + `[net]` snapshotter | after (9) | n/a |
| 7. Brute-force solver prior | (2) graph + automated chamber gen (not yet scoped) | **no** | record-only until chamber gen lands |

1. **Hint-ablation difficulty curve (the flagship).** Same chambers at 0% / kind-only / antlines-partial
   / full mark→mark arrows. Measure solve-rate + step-count vs hint level. **The partial↔full gap
   quantifies how much the wiring "hands the model the solution"** — the core reasoning-vs-perception
   probe the whole project exists to run, now with a real graph to dial.
   **Pre-registered hypothesis (gives the ablation a success criterion):** on single-gate chambers
   (1 button → 1 door), full target arrows make the task near-trivial (predicted solve-rate **≥80%**);
   kind-only (no target) drops it to **~50%**. **The partial↔full solve-rate delta IS the wiring-confound
   measure** — if it's small (<10pts) the hint barely leaks; if it's the predicted ~30pts the full arrows
   are a genuine upper-bound oracle and must stay off by default. *"HIGH confound" = this predicted
   >50%-of-the-gap-from-arrows-alone effect, not a narrative label.*

2. **Completeness A/B (isolate completeness from wiring leak).** Classname-walk annotation vs
   classname-walk + offline GT panels/stairs/gels/bridges, on the 115 panel-maps + 129 gel/bridge-maps.
   Hypothesis: completeness helps movement/routing chambers most with **near-zero confound** — separates
   "the percept was incomplete" from "we leaked the answer."

3. **Difficulty stratification from the corpus.** Build the eval split from the verified distributions:
   complexity deciles (entity-count p25/med/p75/p90), distinct-mechanic count (median 6), max chain depth
   (deep gated chains reach depth 6), archetype. Report **per-archetype solve rates** — the laser bucket
   (50%) will otherwise dominate any aggregate.

4. **LOW-map nut audit.** Extract the 29 LOW maps' `inject_*.nut` bodies from their pakfiles (one-time,
   no parser — short Squirrel) and hand-classify puzzle-relevant vs benign (custom cube skin / vac-tube).
   Converts the "*can* rewire" ceiling into a concrete count of maps that *actually* rewire puzzle edges
   — likely far below 29.

5. **HIGH-map trust calibration.** On 3–5 HIGH maps, cross-check the static button→door graph against
   in-engine `sar_harness_dump_fields` status for a handful of triggers — empirically confirm zero missing
   edges where the flag says HIGH, calibrating the 86.6% number.

6. **Status-badge value test.** Annotate with vs without co-located current-state badges (`[net]` fields
   only, skip the `[dm]` door holdout). If no solve-rate lift, the percept was already sufficient and the
   `[dm]` snapshotter work can be deferred.

7. **Brute-force solver prior (capstone, record-only).** The causal graph + affordance prior prune the
   save/load search (goal-regression to the exit; annotation atoms = IW(1) novelty features). Experimenter-
   side solvability + difficulty oracle, only if automated chamber generation lands. De-risk first via the
   §8/R5 save/load cost + cube/portal fidelity checks.

---

## 8. Open questions / risks

1. **Recovery rate is a conservative floor, not a ceiling.** 56% buttons / 53% catchers excludes
   `func_brush` terminal hits (ambiguous fizzler-twin vs mover) and trigger-volume gates. The "unresolved"
   remainder is mostly **intentionally always-on** (exit fizzlers) or **gated by trigger volumes** (cube-
   on-trigger), not broken wiring. Don't read "~44% ungated" as "~44% broken."

2. **Polarity through `logic_branch` is the soft spot.** A path *exists* to an effect, but Open-vs-Close /
   Enable-vs-Disable can flip on a branch's runtime boolean. Fine for GT scoring (read live entity state);
   a soft spot for a pure static oracle.

3. **Laser emitter→catcher is genuinely missing from the graph** (0/203 emitters emit edges). The full
   emitter→effect chain needs the geo.json raycast; the headline catcher-resolution number is the *graph
   half only*.

4. **`paint_sphere` carries no readable paintType** (all read `?`); gel color must come from the sprayer
   kv or the surface material — an offline-resolve step the box-walk never needed.

5. **Worldspawn geo.json misses brush-entity panels.** The white-tile panel map is **incomplete** for
   chambers heavily skinned with `func_brush`/`prop_static` panels (5/50 sampled maps had zero worldspawn
   portal faces). Flip panels are runtime-conditional anyway → mark conditional, lean on live
   `TraceFirePortal`.

6. **Panel-ID recompile-stability is untested.** Reorder-stability is solid; the 128u centroid-snap
   *should* absorb vbsp re-splits, but the stronger R4.2 test needs the source `.vmf` the corpus lacks.

7. **MEDIUM tier leans on signature reads.** The tier classifier is a heuristic, not a proof — a map
   could in principle hide puzzle logic behind a framing-glue targetname. HIGH/LOW is conservative
   (inject-presence alone forces LOW); MEDIUM is the bucket to spot-check.

8. **Don't generalize the corpus to the training chamber.** The hand-authored `testchamber_000` /
   training BSP may genuinely be script-free; re-run the classifier on it rather than inheriting the
   86.6% HIGH figure. (This is the §8/R1.3 + R2.4 cross-check, still owed.)

9. **The areaportal "door-seal, not room-divider" claim is *inferred, not lump-verified*.** It rests on
   targetname patterns + the 85%+ NO_TARGETNAME share, **not** on parsing the BSP AREAS lump. That claim
   is the whole justification for parking the region graph — if any areaportals actually partition the
   playable space into rooms, the park verdict reverses. **Owed:** a one-shot `srctools`/direct
   `bsp.areas` read on a sample to confirm no areaportal divides a playable room (cheap, S).
