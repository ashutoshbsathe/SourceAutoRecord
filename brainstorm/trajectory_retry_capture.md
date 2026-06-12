# Capturing LLM retries in the `.trajectory` format

*Decision note — `brainstorm/`. Status: RECOMMENDED by design panel — pending user sign-off. Supersedes the discard-rejected-thinking behaviour in `gemini_agent.py`.*

## 1. Recommendation (TL;DR)

Adopt a **breaking, restrained "Observation + repeated Call" Step** (proposal #4's structure, minus the speculative bits). The Step holds the shared observation once; every Gemini call — accepted *and* rejected — is the **same `Call` record** carrying `{prompt_sent, thinking, raw_response, reasoning, usage, outcome}`. The executed action/result live on the **accepted Call**, not the Step.

- **One record type for every call.** Kill the accepted-at-top-level vs. rejected-in-`Attempt` asymmetry. The data the brief says we're discarding (per-retry `thinking`, the exact re-prompt) becomes first-class on every Call, because there's only one kind of call.
- **Frame stored once, structurally.** `frame_png` (~300KB) + percept + player live on `Step.obs`; `Call` carries no pixels. N retries can't duplicate the frame — it isn't expressible.
- **GAVE_UP stops being a hack.** It's the structural absence of an accepted Call (`not any(c.accepted)`), not a synthetic empty `MacroRequest` + fake `result_code='GAVE_UP'`.
- **No `Verdict` enum, no flat stream, no `Record` oneof, no `Observation` id-join.** Those are the over-engineered parts. The enum needs `macro_grammar.validate()` to stop returning prose (out of scope, brittle otherwise); the flat stream buys cross-run query power we don't need at 5–20 steps. Rejection stays a string; "accepted" stays a bool on the Call.

**Hackability vs. scalability call:** I am *not* taking the pure-hack additive option (#1: bolt `thinking`+`prompt_sent` onto `Attempt`), even though it ships today with zero migration. The reason is narrow and concrete: the additive path **cements** the asymmetry the brief explicitly puts under question, keeps the GAVE_UP hack, keeps `prompt_sent`'s latent bug (it stores the try-1 percept even when the accept happened on a later try), and leaves `prompt_sent` semantically overloaded (full-percept on try 0, template on retries) with no field saying which. We pay that asymmetry on *every* read and render, forever. The format is **young — exactly 2 files, both re-recordable** — so the breaking change is cheap *now* and only gets more expensive. This is the textbook moment to take the clean structural fix rather than the graft. It is **not** the maximal-scalability pick: I'm rejecting the flat-call-stream (#3) and the Verdict enum (#4's own embellishment) as ceremony for this scale. The chosen model adds **two messages and deletes one** — net proto surface barely grows, and both the recorder and viewer get *simpler*.

**Path: breaking.** Re-record the 2 files; do not write a converter (it's more code than re-running two evals). Add a `format_version` to the header purely so a stale old file **fails loudly** instead of mis-parsing.

## 2. The chosen proto

```proto
syntax = "proto3";
package llm_eval;

message Vec3 { float x = 1; float y = 2; float z = 3; }

message TokenUsage {       // unchanged
  int32 input = 1;
  int32 output = 2;
  int32 cached = 3;
  int32 image = 4;         // share of input
  int32 thinking = 5;      // share of output
}

message TrajectoryHeader {
  string map = 1;
  Vec3 exit_pos = 2;
  float success_radius = 3;
  string model = 4;
  string system_prompt = 5;
  string grammar = 6;
  int32 format_version = 7;   // NEW: 1 = Observation+Call model. Reader rejects != 1 loudly.
}

// The shared observation the model re-answers across retries. Stored ONCE per step.
message Observation {
  bytes  frame_png    = 1;   // annotated frame PNG (~300KB), embedded once
  string percept_json = 2;   // the marked-entity list the agent saw
  Vec3   player       = 3;
  float  eye_yaw      = 4;
  int32  held_mark    = 5;
}

// One LLM round-trip against the step's observation. IDENTICAL shape whether the
// validator accepted or rejected it -- that symmetry is the whole point.
message Call {
  string prompt_sent      = 1;  // EXACT message sent THIS call: percept text on try 0,
                                //   the short re-prompt template on retries (frame lives
                                //   in Observation, never re-embedded here)
  string thinking         = 2;  // this call's thought trace -- the load-bearing signal
                                //   (try 2 said "80 ticks" while emitting no ticks field)
  string raw_response     = 3;  // the model's verbatim reply (the JSON, or the garbage)
  string reasoning        = 4;  // parsed call["reasoning"]; "" if the JSON didn't parse
  TokenUsage usage        = 5;  // per-call tokens; step cost = sum over calls
  bool   accepted         = 6;  // validator passed this call. At most one true per step.
  string rejection_reason = 7;  // "" iff accepted; else "invalid JSON" | the validator string
  bytes  action           = 8;  // serialized harness.MacroRequest -- set ONLY on accepted call
  bytes  result           = 9;  // serialized harness.MacroResult  -- set ONLY on accepted call
}

message Step {
  int32       index    = 1;
  Observation obs      = 2;   // shared frame/percept/player/yaw/held, stored once
  repeated Call calls  = 3;   // try 0..N in order; last is accepted, or none is (gave up)
  string      terminal = 4;   // ""|SOLVED|DONE|BUDGET|GAVE_UP (final step only)
}
```

This satisfies every requirement: every call carries `{prompt_sent, thinking, raw_response, usage, outcome}` (outcome = `accepted` + `action`/`result`, or `rejection_reason`); the frame is in `Observation` (stored once); the accepted call is not duplicated (it's just the one `Call` with `accepted=true`); token rollups are derived (`sum(c.usage for c in calls)` per step; sum over steps per run — no stored aggregate to drift).

**Why `bool accepted` over a `oneof`/`accepted_index`:** KISS. The writer's invariant ("at most one accepted, and it's last") is trivially held in a single loop. A `oneof outcome { Executed; string rejection }` would make the illegal "two accepted" state unrepresentable, but adds an `Executed` wrapper message and indirection at every read site for a guarantee one writer enforces. Not worth it here. (If a step ever legitimately executes >1 call, revisit — but that's a different benchmark.)

## 3. How it handles the 4 scenarios

| Scenario | Representation |
|---|---|
| **(a) first-try success** | `Step{obs, calls=[Call(accepted=true, action, result, thinking, usage)]}`. One call, no amber tries. |
| **(b) N retries then success** | `calls=[Call(accepted=false, rejection_reason="BAD_MARK…", thinking, usage), …×N, Call(accepted=true, action, result)]`. The flail is the literal list; the executed action is the last call. Each losing call now carries its own `thinking` + exact re-prompt — the headline win. |
| **(c) N retries then GAVE_UP** | `calls=[Call(accepted=false…)×(max_retries+1)]`, **no** accepted call, `terminal="GAVE_UP"`. No synthetic `MacroResult`, no empty `MacroRequest`. Gave-up = `not any(c.accepted)`, derived not sentineled. |
| **(d) invalid-JSON then valid** | `calls=[Call(accepted=false, rejection_reason="invalid JSON", raw_response=<garbage>, reasoning="", thinking), Call(accepted=true, action, result)]`. Identical machinery to a validator rejection — "invalid JSON" is just one `rejection_reason` value. |

**No-duplication check:** frame in `obs` (once); accepted call's data lives only in its `Call`; `action`/`result` only on the accepted call. ✅ for all four.
**Backward-compat check:** breaking by design; `format_version` makes old files fail loudly. ✅ (controlled).

## 4. Recorder + viewer impact

**`gemini_agent.py` (`__call__` retry loop) — gets simpler.** The loop already builds `{raw, thinking, usage, prompt}` per iteration. Replace the asymmetric "append `_attempt()` on reject / `return AgentAction` with top-level fields on accept" with: **every iteration appends a `Call`**; on accept, set `accepted=true` + `action`/`result` and break; on reject/invalid-JSON, set `rejection_reason` and continue. `AgentAction` collapses to roughly `(observation, list[Call])`. `_attempt()` and the dataclass's top-level `reasoning`/`raw_response`/`thinking`/`prompt_sent` fields go away. **This is where the actual bug-fix lands** — `gemini_agent.py:248` currently drops the rejected call's `thinking`; now it's captured because it's the same code path as the accepted one. Set each call's `prompt_sent` to the real `message` text in scope at that iteration (fixes #1's latent `prompt_sent` inaccuracy for free).

**`run_eval` — loses its GAVE_UP juggling.** No more building a synthetic `MacroResult(result_code='GAVE_UP')` + empty `MacroRequest`. It checks `any(c.accepted for c in calls)`; if none, set `terminal='GAVE_UP'` and write the Step (which already carries the rejected calls). The executed action for stepping the game is the accepted call's `action`.

**`trajectory_io.py`:** `make_step` reshapes to take `(index, obs, calls, terminal)` and build one `Observation` + extend `calls`. `read_trajectory` reads `header.format_version`, raises if `!= 1`. Net: small churn, mostly the `obs`-vs-top-level field moves.

**`vis_trajectory.py` card — gets simpler.** Today the card hand-assembles the accepted call from scattered `s.*` fields and renders `s.attempts` as a separate amber loop (two paths for one concept). New: one `_render_call(call)` helper renders any `Call` (prompt → thinking → raw → reason/result → cost). The card reads frame/percept/map from `s.obs.*` (one-line swaps in `_frame`/`_percept`/`_map`/`_projector`/`_card`, plus `_action`/`_result` decode the *accepted* call's bytes), picks `accepted = next((c for c in s.calls if c.accepted), None)`, renders **all** calls as a vertical list — accepted green/prominent, rejected amber. The amber tries now show their own `thinking` (impossible today). Gave-up card: no green call, terminal badge from the Step. **Net: ~40 lines, mostly deletion** — the accepted-vs-attempts split collapses into one render path. The schema-drops-a-field pathology becomes visible inline: the amber try's thinking says "80 ticks", its raw JSON has no ticks field, its reason explains the reject — stacked in one box.

## 5. Migration

**Breaking.** `first_light.trajectory` and `eval.trajectory` (confirmed at repo root — *note: proposal #3's "zero files on disk" claim is wrong; they exist, just not under `py/llm_eval/`*) **do not survive** and are **re-recorded**, not converted. A converter is ~30 lines and re-running two evals is less work and less liability — KISS says skip it. `format_version=7` on `TrajectoryHeader` (default 0 for the old shape) lets `read_trajectory` reject any stale file with a clear error instead of decoding field 13 (`repeated Attempt`) as field 3 (`repeated Call`) into garbage. No old-field-number reuse anywhere, so a silent mis-parse is impossible. C++ is untouched (no C++ writes `.trajectory`); `make proto_py` regenerates the Python stub only — but run `make proto` to keep the checked-in C++ stub honest per the build convention.

## 6. Options considered & rejected

- **#1 Additive (`thinking`+`prompt_sent` on `Attempt`)** — the genuine hack-champion; zero migration, ships today, captures 100% of the *signal*. Rejected because it cements the accepted/rejected asymmetry the brief questions, keeps the GAVE_UP hack and the `prompt_sent` overload/bug, and "all calls for this step" stays `[accepted] + attempts[]` with permanent special-casing. Cheap to *land*, expensive to *live with* — wrong trade for a 2-file format. (If velocity were the only axis, this would win.)
- **#3 Flat call-stream (`Record` oneof, `Observation` id-join, terminal on header)** — the maximal-scalability pole. Rejected as over-engineered for 5–20-step solves: it pays an obs↔call join, a `Record` wrapper, dense-id bookkeeping, and load-bearing writer ordering (truncation can dangle calls) to buy cross-run flat-query power we don't need. Its own author concedes "`Step{obs; repeated Call}` gets ~90% of the win with no join" — which is exactly what we chose.
- **#2 Uniform Turns** — structurally *identical* to the chosen model (`Step` = obs + `repeated Turn`); it's the same good idea. Folded in. The only deltas: we name it `Call` (consistency with the loop's mental model), keep the obs as a nested `Observation` message (cleaner than inlining six fields on `Step` and trivially extensible), and add `format_version` for a loud failure. No real disagreement.
- **#4's `Verdict` enum** (surfaced by the use-case lens) — rejected. `macro_grammar.validate()` returns free-text prose; an enum needs either brittle string-matching at the recorder or changing `validate()`'s contract, widening the blast radius into `macro_grammar.py` (out of scope, and it feeds the prose straight back to the model as the re-prompt). At 2 files we have no evidence anyone will query by category yet. Keep `rejection_reason` as the string; add the enum later *if* a "count INVALID_JSON vs BAD_ARG across a run" need actually materializes — that's a pure additive field then.
- **`API_ERROR`/exception-as-Call** (#4) — rejected as speculative; today an exception bubbles and the `_RETRY` backoff handles transient API failures. Not modeling a Call that never produced a candidate.

## 7. Phased PRs

- **PR 1 — proto + IO (the structural break).** Rewrite `trajectory.proto` (add `Observation`, `Call`; delete `Attempt`; restructure `Step`; `format_version` on header). `make proto`. Reshape `make_step` and guard `read_trajectory` on `format_version`. Round-trip unit test over the 4 scenarios (build Steps by hand, write, read back, assert shape). *Reviewable in isolation; nothing renders yet.*
- **PR 2 — recorder.** Rewrite the `__call__` loop to append one `Call` per iteration; collapse `AgentAction` to `(obs, calls)`; delete `_attempt()` and the GAVE_UP synthesis in `run_eval`. **This PR lands the actual bug-fix** (rejected `thinking` + exact `prompt_sent` now captured). Update `agentloop_smoke.py` / any trajectory assertion: `any(c.accepted)` on a solved step, `not any(...)` on a gave-up step, a rejected call has non-empty `thinking`. **Re-record `first_light` + `eval`** here (user runs the evals).
- **PR 3 — viewer.** Add `_render_call`; swap `s.*` → `s.obs.*`; one uniform call list, color by `accepted`; gave-up renders with no green call. Net-negative lines.

**v0 cut line:** PRs 1+2 are v0 — the data model is correct and the signal is captured the moment the recorder writes it; that alone answers the thesis question (the data is *in the file*, inspectable via `read_trajectory`). PR 3 (the prettier card) is a fast follow and can land same-day but isn't gating. Three small, independently reviewable slices; no C++ rebuild; no converter.