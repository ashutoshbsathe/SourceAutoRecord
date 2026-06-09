# ROADMAP — START HERE

**The single source of truth for project state, milestones, and decisions.** The other
docs hold the detailed design; this file is the index + tracker over them. If something
here disagrees with a detail doc, fix one of them — don't let them drift.

---

## What this is

A **frozen-LLM/VLM reasoning benchmark on Portal 2**. We hand an *untrained* model a clean
percept (the chamber, visually annotated + a structured entity list) and reliable actuators
(macro verbs like `go_to mark=N`), and measure whether it can solve chambers. The thesis:
**when perception + actuation are handed to the model for free, is the bottleneck reasoning
or perception — and when it fails, which one?** Macros draw the line at *local (engine) vs
global (model)*, so a reasoning gap is cleanly separable from a locomotion gap. Methodological
cover: Demaine et al. 2018 (cube+button+door alone is PSPACE-complete).

---

## Current status — 2026-06-09

- ✅ **Annotation (Track A, A1–A5):** colored boxes + Set-of-Marks labels + portal reticle, in-engine.
- ✅ **Recon mechanism:** every category-A status encoding understood. Schema locked in `status_field_recon.md`.
- ✅ **Scope:** decided — full stock-PeTI surface set is the v0 target, **phased after first light**.
- 🔜 **Next:** Phase 1 — status into the snapshotter (the observation rework). Unblocked.
- ⏳ **Needed from user:** the first-light chamber (simple cube→button→door, no portals).

---

## Milestones

- [x] **M0 — Lock the ontology.** Annotation built; recon mechanism done; status schema + scope locked.
- [ ] **M1 — Status-aware percept** (Phase 1): curated category-A status flows over gRPC.
- [ ] **M2 — ⭐ FIRST LIGHT:** frozen VLM solves one cube→button→door chamber (no portals). The perception-vs-reasoning signal.
- [ ] **M3 — Ramp complexity:** add portals → lasers → panels; grow the chamber suite into difficulty tiers.
- [ ] **M4 — Public benchmark:** multi-model eval (Claude/Gemini/GPT-class), scoring, reproducible packaging.
- [ ] **M5 — VP talk, with data:** the reasoning-gap-vs-locomotion-gap result.

---

## Phase plan — critical path

**To first light (M2):**
1. **Phase 1 — status into the snapshotter** *(the observation rework)*
   - `1a` register curated per-class status (the `[dm]` fields the SendTable walk drops)
   - `1b` catcher/relay → child `point_laser_target` association
   - `1c` faith-plate name filter (avoid safety-net flood)
2. **First-light macro executor** — `C1` proto, `C2` look_at, `C4` go_to, `C6` pick_up/release/wait, `C7` mark-in-telemetry, `C8` keepalive. **Skip `C3` shoot_portal + `C5` nav-probe** (chamber 1 has no portals).
3. **Python ReAct driver** — `D1` entity parser, `D2` macro validator, `D3` driver + transcript logger, `D4` run on the chamber.

**Widen (after first light), by mechanism family:**
4. **Animation family** — `m_nSequence` state reader + flip-panel targetname matcher → brings panels **and** the door into the percept (one reader). Adds `C3` shoot_portal + `C5` nav-probe for portal chambers.
5. **Surface family** — gels (paint-map read), then light bridges (projector/volume read). Independent phases.
6. **Coverage cleanup (anytime):** recon a chamber with funnel + monster box + turret-tip.

---

## Key decisions (log)

| Date | Decision | Detail |
|---|---|---|
| 2026-06-08 | Deep-narrow **fixed ontology** (stock PeTI), not wide-shallow | `fixed_ontology_scope.md` |
| 2026-06-08 | `.hdem` recorder = **generous** substrate; percept/rollout project to curated subset | `fixed_ontology_scope.md` #1 |
| 2026-06-09 | **v0 scope = full PeTI surface set, sequenced after first light** (panels = animation family; gels/bridges = surface family) | `fixed_ontology_scope.md` #3 |
| 2026-06-09 | Locomotion **engine-simulated**, split at **local (engine) / global (model)** | `llm_percept_act_grammar.md` §4 |
| 2026-06-09 | **Save/load anchors** reuse engine `save`/`load` (no custom restore) — *checkpoint-deferred grammar* | `llm_percept_act_grammar.md` §4 |
| 2026-06-09 | Portal-traversal during `go_to` — *checkpoint-deferred* | `llm_percept_act_grammar.md` §4 |
| 2026-06-09 | **Canonical mark numbering** (pure fn of world state, save/load-invariant) | `llm_percept_act_phased_plan.md` A3 |
| 2026-06-09 | **Category-A status schema locked** (bool vs `m_nSequence` vs child-target); door deferred | `status_field_recon.md` |
| — | **Eval only, no training**; **visual+symbolic always**; **macros C++-side** | `llm_percept_act_grammar.md` §8 |

---

## Doc index — what to read for what

| Doc | Read it for |
|---|---|
| **ROADMAP.md** (this) | state, milestones, decisions, where to look |
| `fixed_ontology_scope.md` | *why* deep-narrow + the v0 scope decision (read first for scope) |
| `puzzlemaker_elements.md` | the element list (categories A/B/C, P1) |
| `status_field_recon.md` | per-class status fields (the locked schema) + recon protocol |
| `llm_percept_act_grammar.md` | the percept/act **design** (the macro verbs, the thesis) |
| `llm_percept_act_phased_plan.md` | the **build order** (tracks A–D, phase-by-phase) |
| `rollout_visualizer.md` | the `.rollout` browser viewer |
| `hdem_implementation.md` / `_plan.md` | the `.hdem` sidecar recorder design |
| `entity_snapshotter_redesign.md` | snapshotter hot-path / perf design |
| `phase4_sendtable_discovery.md` | snapshotter field discovery + curated-status follow-up |
| `phase4_fixing_slowness_and_crashes.md` | snapshotter perf/crash post-mortem (lessons) |
| `entity_state_observe.md` / `_server_during_demo.md` | early entity-state extraction brainstorms |
