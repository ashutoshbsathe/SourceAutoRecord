# Lens: puzzle-gen-curriculum — puzzles as a generative substrate

*Divergent brainstorm, 2026-06-12 (day after first light). One of 10 council lenses.
Mode: DIVERGE — volume + originality, pruning later. Every idea is concrete enough to act on.*

## The lens thesis

Every other lens treats chambers as **given**. This lens treats them as **emitted**. Portal 2 is
the only AAA game where the level editor (PeTI) ships inside the game, the level format (`.p2c`)
is plain-text KeyValues on an integer voxel grid, ~954k community chambers already exist on the
Workshop (verified mid-2024 count; third-largest workshop on Steam), and — uniquely — **the element
subsets map onto formal complexity classes** (Demaine, Lockhart & Lynch, FUN 2018: portals alone =
pseudopoly; +grills = NP-hard; turrets = NP-hard; timed buttons = NP-hard; cubes+buttons+doors =
PSPACE-complete; lasers+relays+platforms = PSPACE-complete). Demaine et al. even note the `.p2c`
format bounds instances to pseudopolynomial size — generated benchmarks are *formally bounded*.

The structural insight that orders everything below: **a generated chamber comes with ground truth
by construction.** The generator knows the exit door's position and targetname, the element list,
the button→door wiring, and (with a reference solver) a machine-verified solution and optimal
macro-step count. Four open problems on the ROADMAP — the exit oracle, difficulty labels,
contamination resistance, and SFT/solution data — are *hard on found maps and free on generated
ones*. That inverts the default sequencing: generation is not "after the chamber suite"; it is how
the chamber suite should be built.

Toolchain facts to exploit: `.p2c` documented on VDC; `Kyle0654/Portal2.Puzzle` (C#, bitwise voxel
editing) and `gusarba/p2c_conv` exist as reference implementations; TeamSpen210's **`srctools`**
Python library (the BEE2.4 substrate) parses/writes VMF and BSP, including the plain-text entity
lump; `steamcmd workshop_download_item` gives programmatic chamber download; the harness farm
(`py/render_demos.py` + `py/game_launcher.py`) is already the queue+workers+headless-instances
shape any of this needs.

The one genuine technical unknown of this whole lens is the **headless `.p2c → .bsp` compile path**
(idea 3). Everything else is composition of things that already exist in this repo or in public
tooling.

---

## Ideas (ordered roughly by leverage)

### 1. Chamber Manifest standard + hand-authored suite v0 (`chambers/`)
**Build:** A `chambers/` directory convention: each chamber = `NNN_name/` containing the `.bsp`,
the source `.p2c` (when we own it), and a `manifest.json` sidecar: exit position+radius (or exit
door targetname), element census, Demaine-class tag, expected-solvable-by tier, optional reference
solution (macro string list), author/provenance/license. Patch `py/run_eval.py` to take
`--chamber chambers/003_laser_redirect` instead of `--map M --exit x,y,z`. Hand-author 12–15 PeTI
chambers walking the element ladder (cube/button → pedestal+timer → laser+relay → fizzler →
faith plate → funnel → turrets), one mechanism introduced per chamber, in the in-game editor
(hours of authoring, it's a consumer tool).
**Why:** This is M4 (the chamber suite) unblocked *now*, and the manifest is the interchange
contract every other idea below emits into. It also defuses the exit-oracle blocker for the owned
tier: the manifest carries the exit, so the engine-hook oracle is only needed for *found* maps.
**Effort:** S (days).
**Deps:** none — works with today's eval loop.
**Unlocks:** every multi-chamber experiment; the difficulty probes (#5); the suite the first-light
result graduates into.

### 2. `py/puzzlegen/`: a Python `.p2c` chamber DSL
**Build:** A small library that writes valid `.p2c` files: `Chamber(size=(6,6,4))`,
`c.carve(box)`, `c.add(FloorButton, at=(3,2,0), connects=door)`, `c.exit_door(at=...)`,
`c.save('gen_0001.p2c')`. Voxel grid + item entries + connections, mirroring the documented
KeyValues format; port logic from `Kyle0654/Portal2.Puzzle` rather than reverse-engineering from
scratch. v0 element coverage = the category-A list in `puzzlemaker_elements.md` (the annotation
ontology and the generator ontology should be the *same list* — one scope decision, two layers).
Validate round-trip by opening generated files in the in-game editor.
**Why:** The foundation of the entire lens. Once chambers are objects in Python, generation,
mutation, canonicalization, and LLM-authoring are all ordinary code.
**Effort:** S for a minimal room+cube+button+door subset; M for full category-A coverage.
**Deps:** none (file format only; compile is #3).
**Unlocks:** #3–#16, #19, #21.

### 3. Headless compile pipeline: `.p2c → .bsp` without a human
**Build:** Recon-first, three candidate paths, in order of preference: (a) drive the in-game PeTI
compile through the harness — recon the `puzzlemaker_*` console command surface and what the
editor's "build" button actually invokes (`ExecuteCommand` RPC is the escape hatch; gamescope
instance already headless); (b) sidestep PeTI: convert `.p2c → .vmf` ourselves (srctools; BEE2.4's
compiler is reference code for exactly this transform) and run `vbsp/vvis/vrad` as CLI tools
(check for Linux binaries in `bin/`; else Authoring Tools under wine/proton); (c) worst case, a
batch-compile job on a Windows box. Wrap the winner in a `compile_farm.py` shaped like
`render_demos.py` (queue + N workers). Acceptance test: 100 generated chambers compile and load in
the harness unattended.
**Why:** This is the single load-bearing unknown of the lens. If it works, "infinite Portal 2" is
real; if only path (c) works, generation still works but with a Windows hop. Either way the recon
result is pitch-critical information.
**Effort:** M — and explicitly recon-heavy; budget a throwaway week before committing.
**Deps:** #2 (need files to compile).
**Unlocks:** everything generative at scale (#5 probe volume, #10–#16, #19, #21).

### 4. Workshop ingestion probe pipeline + chamber DB
**Build:** `py/workshop/ingest.py`: given Workshop item IDs (enumerated via ISteamUGC queries),
`steamcmd workshop_download_item` each, load it in a harness instance, and run an automatic probe:
does the map load; entity census via `EntitySnapshotter` (which classnames, how many); map bounds;
does an exit-door candidate exist; 60 seconds of scripted random-macro behavior without a crash.
Write one row per chamber into a sqlite/parquet **chamber DB**: elements, counts, size, Workshop
metadata (rating, favorites, subscriptions via the Steam API), probe outcomes. Start with 1k maps,
then 10k. Maps stay download-on-demand (the UGC-licensing-safe path — never redistribute).
**Why:** Turns "~954k items, most garbage" into a *queryable corpus*. The Universe post-mortem
says never pitch raw breadth — this is the curation machine that makes the breadth claim honest.
Also the empirical grounding for difficulty heuristics (#6) and dedup (#17).
**Effort:** M.
**Deps:** harness farm (exists); exit-door heuristic helps but isn't required for the census.
**Unlocks:** #6, #17, #20; the "held-out community chambers" eval tier; the headline corpus number
for the pitch, verified by our own pipeline.

### 5. Solve-rate difficulty probes: agents as difficulty meters
**Build:** A probe battery run N times per chamber, cheapest first: (P0) random-macro agent,
(P1) scripted greedy agent (go_to nearest unpressed button / cube / exit), (P2) frozen Flash-tier
VLM at a small step budget, (P3) frozen Pro-tier VLM. Difficulty vector per chamber = solve rate +
steps-to-solve distribution + terminal taxonomy (SOLVED/BUDGET/LOOP) per probe tier. Store in the
chamber DB; the existing `.trajectory` format records everything for free. Define the canonical
scalar: `difficulty = first probe tier that solves it` (a Crafter-style one-number metric).
**Why:** Difficulty grounded in *agent behavior*, not hand-waving — this is the labeling function
for curriculum (#7), the calibration target for static predictors (#6), and the fitness signal for
setter/solver (#13). The probe ladder also IS the benchmark headline table.
**Effort:** M (orchestration is S; the cost is API budget + wall-clock across instances).
**Deps:** #1 (manifests/oracle for the probed set); exit oracle for found maps.
**Unlocks:** #6, #7, #13, #21; tiering of both generated and Workshop chambers.

### 6. Static difficulty predictor over the chamber DB
**Build:** Features from the census (element counts/types, chamber volume, voxel connectivity
stats, wiring fan-out, distance metrics exit↔entry) + Workshop social signals → gradient-boosted
regressor/classifier predicting the probe-derived difficulty from #5. Report which features carry
signal (does turret count predict VLM failure? does wiring depth?).
**Why:** You can't run probes on 100k maps; a calibrated static predictor pre-sorts the corpus so
probe budget goes to the interesting band. Also a mini-paper on "what makes a Portal 2 chamber
hard for a frozen VLM" — reviewable science from pure exhaust.
**Effort:** S once #4+#5 data exists (it's a tabular-ML afternoon plus iteration).
**Deps:** #4, #5.
**Unlocks:** corpus-scale curriculum; cheap test-set stratification.

### 7. Curriculum scheduler: `next_chamber(agent_history)`
**Build:** A ~200-line service over the chamber DB: serve the next chamber from the band where the
agent's recent solve rate is in a target window (e.g. 30–70% — the learnable frontier), with
staleness/novelty bonuses and per-element-family quotas (so the curriculum doesn't collapse onto
one mechanism). Two clients from day one: the VLM eval loop (eval curriculum = ordered report) and
the RL track (training curriculum proper). It's deliberately dumb — bandit over difficulty bins,
not a learned teacher.
**Why:** The word "curriculum" in the pitch needs an artifact. This is the smallest one that is
real, and it's shared infrastructure across both tracks (the macro-altitude RL agent and the VLM).
**Effort:** S.
**Deps:** #5 (or #6) for the difficulty field; #1 manifests.
**Unlocks:** auto-curriculum claims; the POET loop (#13) replaces this scheduler's *pool* but
keeps its interface.

### 8. Reference solver: macro-altitude search with save/load anchors
**Build:** BFS/best-first/MCTS over the *macro* action space (the same closed verb set the LLM
uses), using the C9 save/load anchors + frame-stepped determinism (the harness brief says tree
search is "architecturally free"; anchors are ~40 LOC). State signature = entity snapshot
projection (button states, cube positions quantized, player cell). Output per chamber:
`solvable: yes/no/timeout`, optimal-ish macro-step count, and the solution as a macro string list
written into the manifest.
**Why:** This is the **solvability certifier** for every generated chamber (a generator without a
verifier emits garbage), the source of ground-truth difficulty (optimal step count), AND a
machine-generated solution-trajectory corpus in the agent's own action space — i.e. SFT data for
the distillation story, produced without humans. Also the bridge artifact to the speedrun-routing
lens (same search, tick altitude later).
**Effort:** M for honest BFS on small PeTI chambers (branching factor over marks is small);
L to make it robust on big/physics-heavy chambers. Be honest in the pitch: physics
semi-determinism (p2sr TAS wiki: airborne objects desync) bounds how "exact" certificates can be —
certify with k repeated rollouts of the found solution.
**Deps:** save/load anchors (C9, unbuilt); #1 manifests to write into.
**Unlocks:** #10–#14 validation, difficulty labels that don't need API spend, SFT corpus,
routing-search groundwork.

### 9. Demaine ladder: parametric chamber families keyed to complexity classes
**Build:** One generator function per formal rung, each with difficulty knobs:
`family_cube_button(n_rooms, n_distractors)` (PSPACE gadget set),
`family_laser_relay(depth, crossings)` (PSPACE), `family_timed_button(timing_slack)` (NP-hard),
`family_turret_gauntlet(n, cover_density)` (NP-hard), `family_portal_traverse(gap_topology)`
(pseudopoly). Every emitted chamber: verified by #8, manifest with family/knobs/class tag,
deterministic from a seed.
**Why:** THE differentiating benchmark asset: a task **distribution** (Procgen lesson) whose
difficulty axis is a *complexity-theory dial*. No prior game benchmark can say "this eval tier is
literally NP-hard, this one PSPACE, and we can emit unlimited fresh instances per tier." It is
also the contamination story: test sets are regenerated from seeds, never published.
**Effort:** M (given #2, #3, #8; each family is days, the first one is the expensive one).
**Deps:** #2, #3; #8 for certification.
**Unlocks:** #19 (secret sets), #21 (scaling laws), the core pitch slide.

### 10. Mutation operators + adversarial hardening loop
**Build:** A `.p2c` mutation library: move/rotate an element, stretch/shrink a room, add/remove
glass walls, swap cube type, add a fizzler across a corridor, re-wire a connection, add a
distractor. Then the loop: when an agent solves chamber C, search (random or LLM-guided) for the
*minimal mutation* that makes it fail while #8 still certifies solvability. Archive
(C, mutation, failure-mode) triples. Note the poetry: first light's killer — a glass enclosure
around the button — is exactly one `add_glass_wall` mutation on a solved chamber. The thing that
beat Gemini by accident becomes a systematic generator of adversarial micro-variations.
**Why:** Converts every solve into a frontier probe; produces the failure taxonomy the
reasoning-vs-locomotion headline aggregates over; it is the minimal viable version of
setter/solver (#13) with none of the open-endedness machinery.
**Effort:** M.
**Deps:** #2, #3, #8; an agent to harden against (exists).
**Unlocks:** #13; regression suites ("the agent that solves v2026.06 must also solve all archived
mutants"); failure-mode-labeled data.

### 11. LLM puzzle setter: natural-language spec → chamber
**Build:** Give a frozen LLM the `py/puzzlegen` DSL as a tool/code surface: "a two-room chamber
where a laser must be redirected through a fizzler-guarded corridor" → DSL code → `.p2c` → compile
(#3) → certify (#8) → screenshot back to the LLM for self-critique → iterate. Reject/retry loop
identical in spirit to the macro grammar's validator-as-reprompt.
**Why:** (a) The demo that sells "generative substrate" to a VP in 30 seconds — type a sentence,
walk into the chamber. (b) Science: LLM-as-setter vs LLM-as-solver is the same model on both sides
of the table — can it author puzzles it cannot solve, or solve puzzles it cannot author?
(c) Feeds #13 with a smarter proposal distribution than random mutation.
**Effort:** M (given #2+#3+#8; the agent loop itself is days — it's run_eval's shape pointed at a
different tool).
**Deps:** #2, #3, #8.
**Unlocks:** #13 (LLM as the setter policy); a public "prompt-to-chamber" artifact; spec↔chamber
paired data for #16.

### 12. Distractor & clutter ablation generator
**Build:** From any solved chamber, emit a controlled ablation grid: +k unwired buttons, +k inert
cubes, +decoy doors, antlines hidden vs visible (PeTI draws indicator lines from button to door —
that's *visual wiring information*; generate the same chamber with wiring visible/invisible),
mark-density stress (20+ annotated entities). Run the probe battery (#5) across the grid; plot
solve rate vs clutter.
**Why:** Cheapest *publishable* science in this lens: does the VLM read the wiring (antlines) or
pattern-match "cube goes on button"? Does reasoning degrade with perceptual clutter even when the
logical puzzle is unchanged? Directly tests the "visual+symbolic always" design decision.
**Effort:** S (given #2+#3; it's a parameter sweep).
**Deps:** #2, #3; #5 probes.
**Unlocks:** a clean ablation section for the first paper; robustness requirements for the
annotation layer (mark occlusion, label density).

### 13. Setter/solver open-ended coevolution (POET-style)
**Build:** The full loop: a population/archive of chambers with per-agent solve rates; a setter
(mutation operators from #10, optionally LLM-guided from #11) proposes children of frontier
chambers; minimal-criterion gate (certified solvable by #8 AND current solver's solve rate in
(0.1, 0.9)); solver = the macro-altitude RL agent (and/or periodically the frozen VLM as an
audit probe). Archive grows; transfer attempts across niches POET-style. Run on the harness farm.
**Why:** The open-endedness flagship — "the benchmark that grows itself at the frontier of agent
ability." This is the strongest version of the scalability story vs every fixed benchmark suite
that a frontier lab one-shots (the VPT moment). It's also the headcount ask in microcosm: one RE
can build the loop; running it productively is a team's worth of compute and analysis.
**Effort:** L (months) — and honestly XL to do *well* (the solver has to actually learn; the
dormant RL stack needs reviving at macro altitude).
**Deps:** #2, #3, #5, #8, #10; a learning solver.
**Unlocks:** the open-ended-learning paper; an ever-fresh competition test-set source; the
DeepMind-native pitch frame (XLand/Genie cousins, but with real physics and verifiable ground
truth).

### 14. Formal gadget-graph compiler: provable instances
**Build:** Encode Demaine et al.'s actual reduction gadgets (locks, one-way doors, crossovers,
timed traversals) as reusable parameterized `.p2c` macros; a compiler from an abstract "gadget
graph" (or directly from small SAT/QBF/motion-planning instances) to a playable chamber. Emit
instances whose solvability — and in some families, optimal solution length — is *provable* from
the source instance, not just empirically certified.
**Why:** The maximal version of the complexity story: benchmark items with proofs attached.
"Here are 500 chambers that are satisfiable iff this 3-SAT instance is" is a sentence no other
embodied benchmark can say. Also generates arbitrarily hard *reasoning* instances with trivially
easy locomotion — the perfect dissection tool given first light's finding.
**Effort:** L (gadget engineering in a physics game is fiddly; each gadget needs empirical
hardening against the engine).
**Deps:** #2, #3, #8 (for empirical re-verification of "provable" gadgets — physics is semi-
deterministic, proofs assume ideal mechanics).
**Unlocks:** the theory-meets-embodiment paper; reasoning-difficulty axis fully decoupled from
locomotion difficulty.

### 15. Canonicalization + dedup of the Workshop corpus
**Build:** A canonical fingerprint per chamber. For PeTI maps, recon whether published BSPs embed
the source `.p2c` (flag: unverified); regardless, a robust fallback exists: parse the BSP's
plain-text entity lump (srctools), quantize entity positions to the 128-unit voxel grid, take the
multiset of (classname, quantized-pos, wiring) modulo the 8 horizontal symmetries + translation →
hash. Exact-dup buckets + LSH for near-dups (one-element edits). Run over the ingested corpus (#4).
**Why:** "954k items" includes re-uploads, trivial edits, and tutorial clones by the thousand.
Dedup is what makes corpus-scale claims honest, keeps test sets leak-free (train chamber's near-dup
in the test set = silent contamination), and the cluster structure itself is data (what does the
community converge on?).
**Effort:** M.
**Deps:** #4.
**Unlocks:** honest corpus statistics for the pitch; leak-safe train/test splits for #21; the
"community puzzle phylogeny" curiosity paper.

### 16. Auto-annotation: causally-correct language spans from ground truth
**Build:** For any trajectory (agent `.trajectory` or human `.hdem`) on a chamber with a manifest,
auto-generate language annotations from entity-state transitions: "picked up cube 11", "placed
cube on button 7 → door 8 opened", segmented into SIMA-style spans (sub-sequence + single
instruction). For generated chambers, also emit the *chamber spec* in language (from #11's
spec↔chamber pairs or templated from the manifest).
**Why:** SIMA 1's two stated bottlenecks were studio access and *human language annotation* —
Portal 2 + this harness deletes both: the entity snapshot gives causally-correct event streams for
free. (Frame, low-level input, span instruction, score) is literally the SIMA 2 ingestion tuple;
this makes the .hdem corpus DeepMind-shaped.
**Effort:** M.
**Deps:** manifests (#1), entity-state diffs (exist), .hdem corpus (other lens).
**Unlocks:** the training-data pitch axis; instruction-conditioned BC; weak labels for a reward
model.

### 17. Secret held-out test-set protocol (the Procgen private-env analog)
**Build:** A documented protocol, not just code: test sets are generated from the parametric
families (#9) by a seed held by the maintainer, regenerated per eval round/competition season,
never published; published instead is the generator version + family distribution + per-family
aggregate scores. A `make testset SEED=...` target and a hash-committed manifest (commit the hash
before the round, reveal seed after).
**Why:** Anti-contamination and anti-VPT-moment armor for both the benchmark and any competition;
near-zero cost because the generator already exists; it's the property that makes "evergreen
benchmark" more than a slogan to a VP who watched chess saturate in a weekend.
**Effort:** S.
**Deps:** #9.
**Unlocks:** competition design (other lens); credibly fresh leaderboards.

### 18. P2-Bench-100: the curated community tier
**Build:** Hand-curate ~100 acclaimed community chambers (Mevious-tier designers, community
hall-of-fame lists, high-rating + high-uniqueness from the chamber DB), stratified across the
element ladder and difficulty bands from #5. Distribution = Workshop item-ID list + download-on-
demand script (UGC-license-safe; author permission where feasible). Manifests via the exit-oracle
hook + hand verification.
**Why:** Generated chambers prove scale; *human-designed* chambers prove meaning. "Solves chambers
that humans rate as masterpieces" is the headline a generated suite can't deliver, and the human
play data on these maps already exists (community times, walkthroughs) as context.
**Effort:** M (curation + per-map verification is the cost, not code).
**Deps:** #4 (DB), exit oracle for found maps (the engine-hook version — this tier is what forces
building it).
**Unlocks:** the flagship eval tier; the "humans sweat over these" comparison set for the
human-baseline lens.

### 19. Cosmetic augmentation: same logic, different pixels
**Build:** For any generated chamber, emit k visual variants with identical logical structure:
PeTI style/theme swaps (clean/overgrown/destroyed via style packs), lighting changes, room
proportions jittered within walkability constraints, element positions perturbed inside the same
voxel cells. Manifest marks them as a logical-equivalence class.
**Why:** Visual-robustness eval for the VLM (does solve rate survive a reskin?) and cheap
domain-randomization data for the RL/BC track. Logical-equivalence classes are also exactly what a
world-model evaluation wants (same dynamics, different appearance).
**Effort:** S (given #2+#3).
**Deps:** #2, #3.
**Unlocks:** robustness ablations; augmented training corpora.

### 20. Human-data difficulty calibration on the suite
**Build:** Run human sessions (the user + friends + community volunteers) over the v0 suite and a
sample of generated chambers, recorded as `.hdem`; difficulty-from-humans = time, deaths, macro
count (via #16's span segmentation), give-up rate. Correlate against probe-based difficulty (#5)
and solver-based difficulty (#8); publish the correlation matrix.
**Why:** Anchors the difficulty scale to the thing a VP actually cares about ("hard for people,
not just hard for our scripted probe"), and the same sessions are the human-trajectory corpus the
training-data lens needs — one collection effort, two products.
**Effort:** M (the cost is humans and scheduling, not code).
**Deps:** #1 suite; .hdem recorder (exists).
**Unlocks:** human-calibrated tiers; knowing-doing-gap baselines; BC seed corpus.

### 21. "Infinite Portal 2" generalization scaling law (the pitch chart)
**Build:** The Procgen experiment at PeTI scale: train the macro-altitude agent (RL or SFT-on-#8-
solutions) on N generated chambers for N ∈ {10, 100, 1k, 10k} from the same family distribution;
evaluate on a fixed secret held-out set (#17) and on P2-Bench-100 (#18). One plot: held-out solve
rate vs N. Secondary: per-Demaine-family breakdown.
**Why:** Procgen's "agents need 500–1000 levels to generalize" is the canonical citation for task
distributions; reproducing its shape in a 3D physics puzzler with a complexity dial is THE chart
for the funding pitch — it simultaneously proves the generator works, the curriculum matters, and
the corpus is the moat. This is also where the headcount ask becomes legible: one RE can produce
the N=100 point; the N=10k point is a team.
**Effort:** L (months; mostly compute orchestration + the solver having to actually learn).
**Deps:** #3, #8 or RL solver, #9, #17.
**Unlocks:** the centerpiece result of the whole program.

### 22. Puzzle kernel extraction (chamber minimization)
**Build:** Given any chamber (esp. Workshop), greedily delete elements/voxels while #8 still
certifies solvability and the solution length stays within ε of original → the minimal "kernel"
puzzle. Cluster kernels across the corpus (with #15's canonicalization) → an empirical inventory
of the community's actual puzzle motifs and their frequencies.
**Why:** Research-grade corpus science: what are the atomic puzzle ideas humans actually compose?
Kernels are also the natural curriculum atoms (teach kernels first, compositions later) and a
principled dedup beyond syntactic hashing.
**Effort:** L (search cost per chamber is high; needs #8 to be fast and the p2c-extraction recon
from #15).
**Deps:** #8, #15.
**Unlocks:** motif-level curriculum; "grammar of Portal 2 puzzles" paper; smarter setters for #13.

---

## Spiciest take

**The 800k-Workshop corpus is the demo slide; the generator is the product — and generation should
come BEFORE the chamber suite, not after it.** The roadmap's standing blockers — exit oracle,
difficulty labels, contamination, terminal-vocabulary ambiguity, even SFT data — are all properties
a *found* map lacks and a *generated* map carries by construction (the generator knows the exit,
the wiring, the certificate, and the solution). Hand-authoring chambers past ~15 and
heuristic-detecting exits on maps we could have emitted ourselves is solving self-inflicted
problems. Invert the order: build `puzzlegen` + the compile path first, and the suite, the oracle,
the tiers, and the training data fall out of one artifact. The Workshop corpus then re-enters
where it's actually irreplaceable — as the *human-meaning* tier (#18) and the held-out
generalization target — not as v0 infrastructure.

## If I could only do ONE thing next week

Build the minimal `py/puzzlegen` DSL (#2, room+cube+button+door only) and drive ONE
programmatically generated `.p2c` through compile (#3 recon) → manifest (#1) → `run_eval` → SOLVED.
That single end-to-end artifact derisks the lens's only real unknown (headless compile), makes the
"infinite Portal 2" claim demonstrable with a one-line command, and hands the pitch its best demo:
*a chamber no human ever authored, solved by a frozen model, with ground truth attached.*
