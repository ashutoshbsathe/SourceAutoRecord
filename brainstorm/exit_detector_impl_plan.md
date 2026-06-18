# Exit Detector — phased implementation plan (code-grounded, ready to ship)

*The build plan for the map-completion oracle. Design + recon are done — see
[exit_detection_brainstorm.md](exit_detection_brainstorm.md) (§2.4–2.8 = the 8-map recon, §9 = the shippable-v0
spec). This doc is the **PR-by-PR execution order**: small, hand-verifiable phases, C++ before Python. Point me
here to start.*

> **One-line goal:** a server-authoritative `bool chamber_complete` in `GameState`, latched from an OR-set of
> exit-input signals seen in the always-on `AcceptInput` hook, that fires on ANY map with **zero per-map
> config** — replacing the hand-passed `--exit x,y,z --radius` hack.

---

## Status (2026-06-18)

**P0–P4 shipped + verified — the oracle works.** The C++ core latches `chamber_complete` from the
AcceptInput OR-set, reads it out over gRPC (`GameState.chamber_complete` / `exit_signal_mask`), and
re-arms on `SESSION_START`. Verified in the REPL (`exit_signal_mask=1` = `@relay_pti_level_end` at a
real PeTI exit) and over the wire. Side-work landed with it: REPL `--on-exit {none,restart,terminate}`
+ a per-step `chamber_complete` readout, and `agentloop_smoke.py` dumps the two fields.

**P5–P8 are deprioritized (user call) — the detector is good enough for now.** Pick them up when the
eval actually needs episodes to terminate on the bit. Focus has moved to annotation + movement tech
(see [ROADMAP.md](ROADMAP.md) "Top of mind").

## 0. Design decisions (resolved; flag if you disagree on wake)

| # | Decision | Choice | Why |
|---|---|---|---|
| D1 | Where the latch + matcher live | **New file pair `src/Features/Harness/PuzzleExit.{hpp,cpp}`** ✅ confirmed | file-local atomics + 4 free funcs (`OnInput`/`Reset`/`Get`/`GetMask`); keeps match logic out of `Server.cpp` & `Portal2HarnessImpl.cpp`; ~60 LOC |
| D7 | Naming convention | `Puzzle*` = chamber-understanding features (PuzzleExit, PuzzleAnnotate); `Harness*` = gRPC/control plumbing. **Console commands stay `sar_harness_*`** (cohesive API, doc-referenced) | `PuzzleExit` is new; `HarnessAnnotate`→`PuzzleAnnotate` file+class rename is P0 below |
| D2 | Match key set | `@relay_pti_level_end.Trigger` ▸ `@transition_from_map.Trigger` ▸ `RunScriptCode(…ReadyForTransition…)` ▸ `OnLevelEnd` ▸ `ChangeLevel`/`ChangeLevelPostFade` | The §2.8 matrix — every one of 8 maps hits ≥2 of these |
| D3 | Casing | **input names `!strcasecmp`** (campaign fired `Changelevel`, §2.5 fired `ChangeLevel`); entity names exact `strcmp` (`@`-prefixed = PeTI-compiler-stable) | §2.6 finding |
| D4 | Latch | **latch-once** (idempotent set), cleared on `SESSION_START` | several keys repeat-fire on the standalone hang (§2.7) |
| D5 | Gating | matcher early-outs unless `harnessControlActive` | don't touch state for normal SAR/speedrun users |
| D6 | Prevention (suppress the transition) | **separate fast-follow phase (P7)**, gated on `harnessControlActive` | moot for the hanging-workshop majority; only completing maps (campaign/multi-part) need it |

---

## 1. The matcher (the heart — reference implementation)

```cpp
// PuzzleExit.cpp (file-local)
static std::atomic<bool> g_complete{false};
static std::atomic<int>  g_mask{0};

enum ExitSignal {                  // also the proto exit_signal_mask bits
  SIG_PTI_LEVEL_END = 1 << 0,      // @relay_pti_level_end.Trigger      (PeTI anchor, §2.5/2.7/2.8)
  SIG_TRANSITION    = 1 << 1,      // @transition_from_map.Trigger      (PeTI std + campaign; SAR matches @ Server.cpp:640)
  SIG_READY         = 1 << 2,      // RunScriptCode(ReadyForTransition) (all elevator maps)
  SIG_LEVELEND      = 1 << 3,      // OnLevelEnd                        (entName blank → match input only)
  SIG_CHANGELEVEL   = 1 << 4,      // ChangeLevel / ChangeLevelPostFade (custom + campaign; absent on std PeTI)
};

void PuzzleExit::OnInput(const char *ent, const char *cls, const char *in, const char *param) {
  if (!harnessControlActive.load()) return;
  if (g_complete.load()) return;                                   // latch-once: skip the repeat-fire storm
  int sig = 0;
  if (!strcasecmp(in, "Trigger") && !strcmp(ent, "@relay_pti_level_end"))            sig = SIG_PTI_LEVEL_END;
  else if (!strcasecmp(in, "Trigger") && !strcmp(ent, "@transition_from_map"))       sig = SIG_TRANSITION;
  else if (!strcasecmp(in, "RunScriptCode") && param && strstr(param, "ReadyForTransition")) sig = SIG_READY;
  else if (!strcasecmp(in, "OnLevelEnd"))                                            sig = SIG_LEVELEND;
  else if (!strcasecmp(in, "ChangeLevel") || !strcasecmp(in, "ChangeLevelPostFade")) sig = SIG_CHANGELEVEL;
  if (!sig) return;
  g_mask.fetch_or(sig);
  g_complete.store(true);
}
```
Notes: `param` may be empty (`OnLevelEnd` shows blank entName + `(5)` param) — guard the `strstr`.
`FailSafeTransition` is intentionally *not* matched — it only fires after `ReadyForTransition` would already have
latched. Add it later only if a map is found that fires it first.

---

## Phase plan

Each phase is an independent, reviewable commit. **(agent)** = I can run it; **(you)** = needs a game launch, so
you run it (per the no-long-runs-in-agent-terminal rule).

### P0 — ✅ done · establish the `Puzzle*` convention
- Rename `HarnessAnnotate.{hpp,cpp}` → `PuzzleAnnotate.{hpp,cpp}`, class `HarnessAnnotate` → `PuzzleAnnotate`.
  Touch the ~5 referencing sites: the `#include` in `MarkTable.cpp`, the `AddFeature<…>` in `SAR.cpp`, the two
  comment refs (`Portal2HarnessImpl.cpp:104`, `PortalPlacement.cpp:30`), and the Makefile source entry.
- **Commands: keep `sar_harness_annotate` / `sar_harness_dump_fields` as-is. No aliases.** Two names per
  command is pure liability, and "not public yet" removes the only reason to carry a compat alias — so pick the
  one right name (which is the existing one). A `PuzzleAnnotate.cpp` registering `sar_harness_dump_fields` is a
  normal, harmless file↔command divergence, and `sar_harness_*` stays one tab-completable family. *(If the
  divergence ever nags: clean-rename — not alias — to nested `sar_harness_puzzle_annotate`/`_dump_fields` to keep
  the family; avoid flat `sar_puzzle_*`, which fragments it. Optional, deferred.)*
- **Verify (agent):** `make` builds; `./format.sh` clean; `grep -rn HarnessAnnotate src/` returns nothing.
- **Size:** ~5 files, mechanical. **Land on its own** (reviewable in isolation; not entangled with new code).
  Skippable — `PuzzleExit` can ship alongside `HarnessAnnotate`, just slightly inconsistent until later.

### P1 — ✅ done · `PuzzleExit` translation unit (latch + matcher + accessors)
- **New:** `src/Features/Harness/PuzzleExit.hpp` (decls for `OnInput/Reset/Get/GetMask`),
  `PuzzleExit.cpp` (the §1 code + `Reset()` clears both atomics; `Get()`/`GetMask()` read them).
- **Makefile:** add `PuzzleExit.cpp` to the Harness sources list (same place `Portal2HarnessImpl.cpp` etc. are
  listed). `#include` whatever exposes `harnessControlActive` (`Harness.hpp`).
- **Verify (agent):** `make` builds & links; `./format.sh` clean. No behaviour change yet (nothing calls it).
- **Size:** ~60 LOC, 2 files.

### P2 — ✅ done · proto field + read-out (observable, still always-false)
- **`harness.proto`:** add to `GameState` — `bool chamber_complete = 8;` and `int32 exit_signal_mask = 9;`
  (`GameState` currently ends at `entity_snapshot = 7` → 8/9 are the next free tags; automatic, no decision).
- `make proto` **(agent)** — regenerates BOTH C++ (`*.pb.cpp`) and Python (`harness_pb2*.py`) stubs.
- **`Portal2HarnessImpl.cpp` `InternalObserve` (~:208):** `state->set_chamber_complete(PuzzleExit::Get());`
  `state->set_exit_signal_mask(PuzzleExit::GetMask());` — mirror how `EntityState.mark` is threaded out.
- **Verify (you):** boot any map via the harness, call `Observe` — `chamber_complete` present in `GameState`,
  reads `false`. (gRPC surface changed → smoke-test gate now applies, addressed in P6.)
- **Size:** ~4 lines + proto.

### P3 — ✅ done · wire the matcher into `AcceptInput_Hook` (it now fires)
- **`src/Modules/Server.cpp`:** `#include "Features/Harness/PuzzleExit.hpp"`; add **one** call after the existing
  un-gated observe blocks (just before `reloadedFix->OverrideInput` at `:656`):
  `PuzzleExit::OnInput(entName, className, inputName, parameter.ToString());`
- **Verify (you):** load 2–3 recon maps (a std-PeTI like `…1361778957`, the campaign `sp_a2_triple_laser`, the
  custom `multiverse_part1`); play/noclip to exit; `Observe` → `chamber_complete` flips **true**, and
  `exit_signal_mask` shows the expected bit(s) per the §2.8 matrix. **This is the first real end-to-end win.**
- **Size:** 1 line + include.

### P4 — ✅ done · reset on `SESSION_START` (no bleed across episodes)
- **`PuzzleExit.cpp`:** `ON_EVENT(SESSION_START) { PuzzleExit::Reset(); }` (or fold into the Harness
  SESSION_START handler, `Harness.cpp:248`). The hook re-binds every SESSION_START already (`Server.cpp:705`), so
  the matcher stays live.
- **Verify (you):** complete a map (latch true) → harness `Reset()` / `restart_level` → next `Observe` reads
  `false`. Confirm no "complete" bleeds into episode 2.
- **Size:** ~3 lines.

### P5 — ⏸ deferred · Python: terminate on the bit + "restart, don't advance" + the exit-unseen flag
- **`py/rl_challenge_env.py`:** replace `_check_terminated` (`:146`, the `abs(dist) <= 100` radius hack) with
  `return bool(state.chamber_complete)`; key the `+10000` terminal reward (`:256`) off the real bit.
- **`py/testchamber_session.py`:** replace `reached_exit` (`:44`) with a `state.chamber_complete` read.
- **Restart-not-advance:** on terminate, the env/orchestrator loads the next eval map / `restart_level` (the
  standalone-hang majority needs nothing more — the reset preempts the looping elevator).
- **Exit-unseen flag (the sweep's telemetry):** when an episode ends **without** `chamber_complete` (timeout/
  truncation), log `{map, "exit-unseen"}`; on success log `exit_signal_mask`. This is the self-validating corpus
  sweep (§9.2) — the exit-unseen list = OR-set gaps.
- **Verify (you):** run one episode to a real exit → episode terminates at the exit tick, harness loads next.
- **Size:** ~10 lines.

### P6 — ⏸ deferred · smoke test (the PR gate)
- **`py/agentloop_smoke.py`:** assert `chamber_complete` is `false` mid-map and flips `true` at a known exit on a
  fixed test map (it boots a real game — this is the by-hand gate per CLAUDE.md). The mark↔label-style "fired at
  the *right tick*" remains a documented manual visual check.
- **Verify (you):** `uv run python py/agentloop_smoke.py` passes.
- **Size:** ~15 lines.

### P7 — ⏸ deferred · prevention (fast-follow; only for *completing* maps)
- **`src/Modules/Server.cpp` `AcceptInput_Hook`:** before the dispatch at `:658`, add
  `if (harnessControlActive.load() && (!strcasecmp(inputName,"ChangeLevel") || !strcasecmp(inputName,"ChangeLevelPostFade"))) return;`
  → the entity's changelevel never reaches the engine. Bookkeeping above (`:584`–`:656`, incl.
  `PuzzleExit::OnInput`) still runs, so detection is preserved; only the dispatch is skipped.
- **Why gated:** must not regress SAR/demo users (they need real changelevels). Demo-record at `:586` still logs
  the input → a suppressed harness run desyncs on replay (fine; harness runs aren't speedrun demos).
- **Verify (you):** on a campaign map whose next BSP **is** installed (`sp_a2_triple_laser`→`sp_a2_bts1`),
  reaching the exit latches `chamber_complete` but the game stays on the current map (no `Host_Changelevel`).
- **Skip if:** the P5 reset already wins the race on completing maps in practice — measure first.
- **Size:** ~2 lines.

### P8 — ⏸ deferred · delete the radius oracle
- Remove `--exit`/`--radius` (`py/macro_repl.py:198-262`), the dead `reached_exit`/`_check_terminated` distance
  math, and the 2D-told/3D-judged distance bug (catalog A10) with it.
- **Verify (agent):** `ruff` clean; grep shows no remaining `--radius`/`reached_exit` callers.
- **Do last** — only once the sweep (P5/P6) is green.

---

## Build / test cheatsheet
```
make          # build sar.so            (agent ok)
make proto    # regen C++ + Python stubs after harness.proto edits (agent ok)
./format.sh   # clang-format Harness srcs + ruff py/  (agent ok — never touches *.pb.cpp)
uv run python py/agentloop_smoke.py     # the end-to-end gate (you run — boots a game)
```

## Open items to confirm at build time
1. **Makefile Harness-sources location** for `PuzzleExit.cpp` (grep for `Portal2HarnessImpl.cpp`).
2. **Optional extra robustness key:** also match `path_track.InPass` on `*departure_elevator*` (a tier-2.5
   corroborator) — only if the sweep surfaces an elevator map that somehow fires none of the five. Defer.
*(Resolved: proto tags = 8/9, automatic; D1 = separate `PuzzleExit` file; naming = `Puzzle*` per D7;
commands stay `sar_harness_*`, no aliases.)*

## Definition of done (v0)
`chamber_complete` flips true at the exit of every map in the §2.4–2.8 recon set with the correct
`exit_signal_mask`; clears on reset; Python terminates + restarts on it; smoke test green; radius oracle deleted;
a corpus sweep produces a (hopefully short) exit-unseen flag list. Prevention (P7) landed iff a completing map
shows the reset losing the race.
