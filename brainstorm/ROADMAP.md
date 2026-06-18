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

## Top of mind (2026-06-18) — next brainstorm targets

Two priorities lead now; each gets its own brainstorm next session:

1. **Annotation improvements** — the in-engine percept (boxes + Set-of-Marks labels + portal reticle) is built (Track A, A1–A5); next is sharpening what it shows and how. *(brainstorm pending)*
2. **Movement tech** — `go_to` dodging around obstacles/hazards and smarter locomotion (the local-vs-global line the macros draw). *(brainstorm pending)*

**Exit detection — parked as "good enough."** The PuzzleExit oracle's C++ core (P1–P4) is shipped + verified: it latches `chamber_complete` from the AcceptInput OR-set, reads out over gRPC, and re-arms per episode. The remaining phases (P5+: Python terminate-on-bit, smoke gate, prevention, radius-oracle deletion) are **not critical** and can wait. → [exit_detector_impl_plan.md](exit_detector_impl_plan.md)

---

## Current status — 2026-06-11

- ✅ **Annotation (Track A, A1–A5):** colored boxes + Set-of-Marks labels + portal reticle, in-engine.
- ✅ **Recon mechanism:** every category-A status encoding understood. Schema locked in `status_field_recon.md`.
- ✅ **Scope:** decided — full stock-PeTI surface set is the v0 target, **phased after first light**.
- ✅ **Phase 1a — status into the snapshotter:** curated `[dm]` fields (cube type/activated, laser-target powered, faith-plate disabled) now flow over gRPC. Verified in-engine.
- ✅ **Macro executor (Track C, PR0–PR4):** all verbs built + validated against a live game — `aim_at`/`look`/`go_to`/`move`/`pick_up`/`release`/`interact`/`wait`/`done`. **Gate 2 passed: `testchamber_000` (an auto-dropper cube→button→door chamber) solved by hand through the real executor.** As-built deviations are in `macro_executor_impl_plan.md` §As-built carry-forward.
- ✅ **First-light chamber + manual driver:** `testchamber_000` provided and solvable; `py/macro_repl.py` is the human-driven macro REPL (the manual analogue of the ReAct driver).
- 🔜 **Next (critical path to first light):** the Python layer — `PR5` (entity parser + macro validator + `step_macro`), `PR6` (gRPC keepalive), then `PR7` ReAct driver (= ⭐ first light).

---

## Milestones

- [x] **M0 — Lock the ontology.** Annotation built; recon mechanism done; status schema + scope locked.
- [ ] **M1 — Status-aware percept** (Phase 1): curated category-A status flows over gRPC. *(1a ✅; 1b/1c remaining)*
- [ ] **M2 — ⭐ FIRST LIGHT:** frozen VLM solves one cube→button→door chamber (no portals). The perception-vs-reasoning signal. *(Executor + chamber + manual solve ✅; remaining: the LLM ReAct driver — PR5–PR7.)*
- [ ] **M3 — Ramp complexity:** add portals → lasers → panels; grow the chamber suite into difficulty tiers.
- [ ] **M4 — Public benchmark:** multi-model eval (Claude/Gemini/GPT-class), scoring, reproducible packaging.
- [ ] **M5 — VP talk, with data:** the reasoning-gap-vs-locomotion-gap result.

---

## Phase plan — critical path

**To first light (M2):**
1. **Phase 1 — status into the snapshotter** *(the observation rework)* — **effectively done for first light**: `1a` ✅ + buttons are networked (SendTable already captures them); door deferred but non-blocking (visual + trigger-based success).
   - ✅ `1a` register curated per-class status (the `[dm]` fields the SendTable walk drops) — *done, verified in-engine*
   - `1b` catcher/relay → child `point_laser_target` association — **deferred → M3** (laser-only; not on the first-light path)
   - `1c` faith-plate name filter (avoid safety-net flood) — **deferred → ~M4** (stock-chamber hygiene; a hand-designed chamber has no safety-net flood to filter)
2. **First-light macro executor — ✅ done (PR0–PR4), validated live + Gate 2 solved by hand.** `C1` proto, `C2` aim_at+look, `C4` go_to+move, `C6` pick_up/release/interact/wait, `C7` mark-in-telemetry. (`C8` keepalive = PR6, still open.) Skipped `C3`/`C5` (no portals). As-built deviations: stable per-entity marks (not dense 1..N), full-`map`-reload reset for droppers, `+use` held ≥3 ticks, grabbable-class gate + movement-based grab-confirm — see `macro_executor_impl_plan.md` §As-built carry-forward.
3. **Python ReAct driver (PR5–PR7, remaining)** — `D1` entity parser, `D2` macro validator, `D3` driver + transcript logger, `D4` run on the chamber. (`py/macro_repl.py` is the human-driven prototype: `build_macro` ≈ D2, the loop ≈ D3.)

**Widen (after first light), by mechanism family:**
4. **Animation family** — `m_nSequence` state reader + flip-panel targetname matcher → brings panels **and** the door into the percept (one reader). Adds `C3` shoot_portal + `C5` nav-probe for portal chambers.
5. **Surface family** — gels (paint-map read), then light bridges (projector/volume read). Independent phases.
6. **Coverage cleanup (anytime):** recon a chamber with funnel + monster box + turret-tip.

---

## Code hygiene / tech debt (anytime, low-stakes)

Not on the critical path, but worth a sweep when touching a file: small *taste* debt
has crept into `py/` (and likely `src/`). Fix in passing, don't make a project of it.

- [ ] **Dict-access taste sweep** — kill pointless `.get(k)` guards on dicts whose
      schema *guarantees* `k` (e.g. mark dicts from `entities._mark_dict` always carry
      all keys), and the inconsistency of mixing `m['x']` and `m.get('x')` in one
      expression. Hoist the value into a local instead of burying a quoted subscript in
      an f-string (`f' "{m["name"]}"'` → `name = m['name']; tag = f' "{name}"'`). *Keep
      the legitimate `.get`s*: sparse delta field-maps and untrusted LLM JSON genuinely
      may miss keys. (First pass done in `gemini_agent._percept_text` + `macro_repl.dump_marks`, 2026-06-18.)
- [ ] **General readability pass** — convoluted inline f-strings / nested conditionals,
      when you're already in the file. Minimal, surgical, no churn-for-churn's-sake.

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
| 2026-06-09 | **First-light-first; element breadth is decoration for the core science** | Demaine: cube+button+door alone is PSPACE-complete → the reasoning-difficulty ladder needs no new elements. So `1b` (lasers) → M3, `1c` (safety-net filter) deferred (~M4 stock-chamber hygiene; a hand-designed chamber sidesteps the flood). First light is the de-risking gateway; in every VP/headcount scenario the *result* is the currency and breadth is downstream. Next = macro executor (C) + ReAct driver (D) + chamber. |
| 2026-06-10 | **Act-grammar altitude = closed semantic verbs (v0)**; code-as-action + skill library is a **P1 A/B arm**, gated on a demonstrated *in-step composition* bottleneck (not built for v0) | `llm_act_grammar_altitude.md` |
| — | **Eval only, no training**; **visual+symbolic always**; **macros C++-side** | `llm_percept_act_grammar.md` §8 |

---

## Doc index — what to read for what

| Doc | Read it for |
|---|---|
| **ROADMAP.md** (this) | state, milestones, decisions, where to look |
| `fixed_ontology_scope.md` | *why* deep-narrow + the v0 scope decision (read first for scope) |
| `puzzlemaker_elements.md` | the element list (categories A/B/C, P1) |
| `status_field_recon.md` | per-class status fields (the locked schema) + recon protocol + I/O-edge vocabulary (PeTI vs BEEmod) |
| `exit_detection_brainstorm.md` | the map-completion oracle (exit detector): signal recon across 8 maps, the OR-set, the §9 shippable-v0 spec + corpus sweep |
| `exit_detector_impl_plan.md` | the PR-by-PR exit-detector build plan — **P1–P4 done + verified; P5+ deferred** (not critical, see Top of mind) |
| `exit_criteria_structure.md` | (parked, post-oracle) exposing the exit's dependency graph as structured hints / dense reward |
| `llm_percept_act_grammar.md` | the percept/act **design** (the macro verbs, the thesis) |
| `llm_act_grammar_altitude.md` | **why closed verbs over Voyager-style code-as-action** (industry sweep + decision) |
| `llm_percept_act_phased_plan.md` | the **build order** (tracks A–D, phase-by-phase) |
| `macro_executor_impl_plan.md` | **code-grounded PR plan** for the macro executor + driver (Track C/D detail, PR0–PR7 to first light) |
| `rollout_visualizer.md` | the `.rollout` browser viewer |
| `trajectory_visualizer.md` | the `.trajectory` (LLM eval) self-contained HTML viewer design |
| `trajectory_retry_capture.md` | the Observation+Call `.trajectory` data model (per-call retries) |
| `first_light_and_next_steps.md` | **⭐ post-first-light: the result, the reasoning-vs-locomotion finding, and next steps** |
| `diverge_catalog.md` | **the full idea space** — 259 consolidated ideas (A–M themes, stable IDs), skeptic's corner, next-7-days shortlist; raw council output in `.council/` |
| `hdem_implementation.md` / `_plan.md` | the `.hdem` sidecar recorder design |
| `entity_snapshotter_redesign.md` | snapshotter hot-path / perf design |
| `phase4_sendtable_discovery.md` | snapshotter field discovery + curated-status follow-up |
| `phase4_fixing_slowness_and_crashes.md` | snapshotter perf/crash post-mortem (lessons) |
| `entity_state_observe.md` / `_server_during_demo.md` | early entity-state extraction brainstorms |
