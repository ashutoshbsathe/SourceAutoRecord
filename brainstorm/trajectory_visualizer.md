# Trajectory Visualizer -- Design Brainstorm

## 1. TL;DR / Recommendation

- **Build the transcript-of-decisions viewer: a vertically-scrolling agent trace where each step is a collapsed one-liner that expands into a three-pane decision card (perception | grounded-percept | reasoning→action→result).** This is the synthesis the judges converged on -- TURNS' chat-transcript shell carrying DecisionStrip's thesis-axis card, with a verdict-writeback sidecar as the one piece of interactivity that earns its weight.
- **Keep the existing NiceGUI + FastAPI + vanilla-JS + Canvas2D stack** (`py/vis`), reused verbatim for theme/chrome, in a *new* `py/vis_traj` module that shares only the theme tokens. No god-viewer: the `.rollout` tick-scrubber stays untouched -- two granularities, two correct viewers.
- **PR0 (capture) is the true critical path and is common to every design.** Land an additive `trajectory.proto` v2 (thinking text, retry attempts, token splits, per-turn prompt, eye-yaw) + a small `gemini_agent.py` patch *as its own PR, decoupled from the viewer*. Everything the user explicitly asked for (retries, thinking trace, prompt-in/response-out, token splits) is blocked on it.
- **v0 cut line:** PR1 (loader + page + three-pane transcript card + result strip, on *today's* fields) **+** PR2 (top-down map + mark↔text cross-highlight + verdict-writeback sidecar + tally + failure-filter, still today's fields). That alone answers "perception or reasoning?" on first light with zero recorder change; missing fields render as honest empty blocks.
- **Honesty constraint baked into v0:** on-frame box highlight is **impossible** in v0 (no per-mark pixel boxes, no camera pose stored). The achievable correspondence gesture is row-highlight + map-dot + `[N]`-in-text cross-highlight. Do not fake a ring the data can't draw.
- **Deferred (M4+):** Atlas's success-matrix / gallery / A/B-compare and the `runs/index.json` substrate -- premature while **zero `.trajectory` files exist on disk** (verified). Adopt the cheap directory convention now; build the aggregate UI when runs actually pile up. Decision Filmstrip's single-file HTML export is a *later button* for the VP/Slack share path, not the primary tool.

---

## 2. Framing: a trajectory is a sparse DECISION transcript, not a video

A `.trajectory` is **6 steps (Gemini) to 17 steps (human)**. Each `Step` is **one full LLM decision cycle** -- annotated frame in, one verb out, engine result back. The unit is the **decision, not the tick**. There is no continuous timeline to scrub.

This reframes the entire UX away from the `.rollout` viewer, which is correct tick-by-tick **video playback** (play/pause, range scrubber, per-frame canvas). That machinery -- and its NiceGUI async-mount race (`setTimeout(init, 50)` in `visualizer.js`) -- is the *wrong primitive* here. The right top-level control is a **clickable list/strip of discrete steps**, not a slider; the right per-step form is a **card you read**, not a frame you watch. Kill play/pause.

Why this serves the **perception-vs-reasoning thesis** directly: each step puts three things edge-to-edge and the verdict falls out of reading one row left-to-right.

| Pane | Question it answers | Failure it isolates |
|---|---|---|
| **LEFT** -- annotated frame (what it *saw*) | Did the pixels contain the answer? | Entity missing/mis-annotated in the frame → **perception gap** |
| **MIDDLE** -- percept marks + state dicts (what it was *told*) | Was the grounded state correct? | `on_button:false` but reasoning says "cube is on button" → **grounding gap** |
| **RIGHT** -- reasoning → verb → result_code | Did it choose right *given* a correct percept? | Frame fine + marks fine + wrong verb → **reasoning gap**; right verb + `STUCK` → **actuation gap** (a macro-layer bug signal) |

The `result_code` (`SUCCESS|STUCK|BLOCKED|WALL|EDGE|BAD_MARK`) on each step is the cheap pre-classifier of *where* it broke; it tints the collapsed line so the failure is one glance away in a 17-step run.

---

## 3. Capture audit + proto v2 (PR0, do this FIRST)

Verified against `trajectory.proto` (v1) and `gemini_agent.py`. **This is the prerequisite for everything the user asked to surface.** It is *common-mode* across all four designs, so it earns no design any points and must ship as its own PR.

| Signal | Stored today? | Where it is now | Needed for |
|---|---|---|---|
| Frame (annotated PNG) | ✅ `Step.frame_png` | proto:33 | LEFT pane (hero) |
| Percept marks | ✅ `Step.percept_json` | proto:34 | MIDDLE pane |
| Self-reported reasoning | ✅ `Step.reasoning` | proto:36 | RIGHT pane |
| Raw response (final attempt) | ✅ `Step.raw_response` | proto:37 | response panel |
| Action / result | ✅ `Step.action`/`result` (opaque harness protos) | proto:40-41 | verb + result_code |
| Player pos / held_mark / terminal | ✅ | proto:35,42,44 | map + outcome |
| Tokens in/out/cached | ✅ `TokenUsage` | proto:16-20 | token line (coarse) |
| **Thinking-trace TEXT** | ❌ folded into `output`, text discarded | `_usage`, gemini_agent:106 | RIGHT pane thinking block |
| **Retries (rejected attempts)** | ❌ console-only, dropped in loop | gemini_agent:187-207 | retry sub-turns |
| **Image / thinking token splits** | ❌ computed for stdout only | `_img_tokens`/`_token_line`:111-129 | token line parity w/ console |
| **Explicit per-turn prompt** | ❌ reconstructable, not serialized | `_percept_text`:78-96 | prompt-in panel |
| **Camera/eye yaw (facing)** | ❌ in-hand at capture, unstored | `obs.state.camera.y`:81 | exact map facing arrow |

Two misses the proposals understated, both confirmed in code:
1. Capturing the thinking trace also needs **`include_thoughts=True`** on `ThinkingConfig` (currently only `thinking_level=MEDIUM` is set, gemini_agent:172-174) -- without it the thought-summary Parts never arrive.
2. **`eye_yaw` is already in-hand** at capture (`obs.state.camera.y`, gemini_agent:81). Storing one float gives an *exact* facing arrow on the map -- far better than DecisionStrip's movement-delta fallback, which is wrong on `look`/`aim`/`wait` steps.

**Proposed `trajectory.proto` v2 (all additive proto3 -- old files still load):**

```proto
message TokenUsage {
  int32 input = 1;
  int32 output = 2;
  int32 cached = 3;
  int32 image = 4;      // image-modality input tokens (was console-only)
  int32 thinking = 5;   // thinking output tokens (was folded into `output`)
}

message Attempt {
  string raw_response = 1;     // the rejected reply, verbatim
  string rejection_reason = 2; // "invalid JSON" | the macro_grammar.validate() reason
  TokenUsage usage = 3;        // what the dropped attempt cost
}

message Step {
  // ... fields 1-11 unchanged ...
  string thinking = 12;          // Gemini thought-summary text (needs include_thoughts=True)
  repeated Attempt attempts = 13; // rejected attempts BEFORE the accepted one (empty if first-try)
  string prompt_sent = 14;       // the exact per-turn text the model got (incl. retry re-prompts)
  float eye_yaw = 15;            // obs.state.camera.y -- exact map facing, one field
}
```

**`gemini_agent.py` recorder changes (minimal):**
- `ThinkingConfig(thinking_level=MEDIUM, include_thoughts=True)` and read the thought-summary Parts off the response into `Step.thinking`.
- In `__call__`, accumulate each rejected attempt (`raw`, the `validate()` reason / `"invalid JSON"`, its `usage`) into a list instead of only printing it; return them on the `AgentAction` and serialize as `Step.attempts`.
- Populate `TokenUsage.image`/`.thinking` from the values `_img_tokens`/`_token_line` already compute.
- Pass `_percept_text(...)` (+ retry re-prompts) through to `Step.prompt_sent`; pass `obs.state.camera.y` to `Step.eye_yaw`.

Keep it tasteful: five fields and one message, no schema for provider internals (`thinking` is a plain string so Claude/GPT summaries drop in unchanged).

---

## 4. Approaches surveyed

| # | Lens | One-liner | Hack | Scale | Verdict |
|---|---|---|---|---|---|
| 1 | **DecisionStrip** | `/trajectory` page: step strip on top, three-pane perception\|percept\|reasoning card below | High | Med | **Runner-up.** Sharpest per-step *layout*; build its card first. No writeback / no aggregate seam. |
| 2 | **TURNS** | Vertically-scrolling agent-trace; collapsed lines → expanded cards; **verdict-writeback** sidecar | High | High | **Winner.** Correct native form + the one feature (verdict sidecar) whose output grows into the M5 result. |
| 3 | **Atlas** | Directory-backed gallery + model×chamber success matrix + step-locked A/B compare | Low | High | **Right idea, wrong time.** 600-750 LOC for cardinality that doesn't exist (0 trajectories). Bank for M4. |
| 4 | **Decision Filmstrip** | ~250 LOC pure-Python emitter → one self-contained `.html`, no server | Highest | Low | **Best demo artifact, wrong primary tool.** Steal the HTML export as a button. |
| — | **Wildcard: Rerun.io** | Native timeline viewer, image+scalar tracks | — | — | **Reject.** Models a *continuous* timeline that fights sparse-decision granularity; can't bake a frame next to its JSON/reasoning; can't hand a VP a `.rrd`. Revisit only if M3+ adds dense per-tick sub-traces. |
| — | **Wildcard: marimo/Jupyter** | Notebook for token/cost stats across many solves | — | — | **Defer, separate tool.** Right for *aggregate* analysis; not a per-solve diagnostic and not a VP artifact. |

**TURNS (winner).** A sparse decision log *is* a chat transcript -- the same thing a researcher already reads in the Anthropic/LangSmith console -- so it is instantly legible and screenshot-ready for a VP. Its load-bearing, design-unique idea is **verdict-writeback**: a one-keystroke `ok/perception/reasoning/actuation` label per step, persisted to a `<traj>.verdicts.json` sidecar and tallied in the header. That single feature is the difference between *a viewer* and *a benchmark front-end* -- it is simultaneously the fastest nightly triage action and the labelled dataset the M5 result aggregates from, for ~20 LOC and zero proto change. Its honest risk ("don't just re-skin the console") is real -- `gemini_agent.py` already prints percept-in, raw-out, the token line, and retry reasons to stdout -- but the verdict tally, the top-down map, and the failure-filter are exactly what stdout *cannot* do.

**DecisionStrip (build first).** The three-pane LEFT=perception / MIDDLE=grounded-percept / RIGHT=reasoning split is the single sharpest diagnostic primitive in the set, and it is *literally TURNS' expanded card body* -- so there is no conflict, the two merge. It was also the only proposal straight about the v0 on-frame-highlight impossibility, routing the live highlight to a map dot instead of faking a ring. Ship its card inside TURNS' transcript shell.

**Stack call -- keep NiceGUI.** The `.rollout` stack (NiceGUI + FastAPI + vanilla JS + Canvas2D, 350 LOC JS / 260 CSS) is reused for theme and chrome; `read_trajectory()` and `harness_pb2` already exist; `frame_png` serves raw with no re-encode (cheaper than the rollout loader, which re-encodes). Filmstrip's "abandon the server" argument is genuinely tasteful for a *share artifact* and we steal it there, but as the *primary nightly tool* a static one-way export can't host live verdict-writeback without contradicting itself (it'd need a server or `localStorage`). Keep the live viewer; add the HTML export later as a button.

---

## 5. Recommended design

**TURNS' transcript shell + DecisionStrip's three-pane card body + TURNS' verdict-writeback sidecar.** A single vertically-scrolling page; each step is a collapsed one-liner that expands into the card; a sticky right rail follows the selected step with the map, retry sub-turns, and raw prompt↔response tabs.

```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ testchamber_000 · gemini-3.5-flash · SOLVED in 6 steps      tokens in 41.2k / out 3.1k         │ ← sticky header
│ verdict tally:  ✓ok 4   ●reasoning 1   ◆perception 1   ▲actuation 0     [a]ll  [f]ails ▾  [⤓html]│
├──────────────────────────────────────────────┬─────────────────────────────────────────────────┤
│ TRANSCRIPT  (scroll · j/k to walk)            │ RIGHT RAIL (sticky, follows selection)          │
│ ▸ [0] go_to  mark=3   → SUCCESS   ✓ · · ·     │  ┌─ TOP-DOWN MAP ──────────────────────────────┐ │
│ ▸ [1] aim_at mark=2   → SUCCESS   ✓ · · ·     │  │   ◎exit (ring = success_radius)             │ │
│ ▾ [2] pick_up mark=1  → STUCK     ✗ ◆ ◆ ◆     │  │     ·2(button)   ·3(door)                   │ │
│ ┌──── PERCEPTION ────┬─ PERCEPT ──┬─ DECISION ─┐│ │   0→1→2● player path  ● = this step          │ │
│ │ [annotated 640×480 │ [1] cube   │ THINKING ▾ ││ │   →facing (eye_yaw)   ·1(cube)              │ │
│ │  frame, boxes+mark  │  d=20 b=+2 │ (medium)  ││ │  player (1216,-892,64)  exit dist 384       │ │
│ │  labels baked in;   │  {cube_type│"the cube  ││ └─────────────────────────────────────────────┘ │
│ │  click → lightbox]  │  :std,     │ is closest;│├─────────────────────────────────────────────────┤
│ │                     │  on_button │ grab it…" ││  RETRIES for step 2            (2 dropped)      │ │
│ │   [3]door  [2]btn   │  :false}◀hi│───────────││  ┌ try1 ✗ BAD_MARK "no mark 9" ─────────────┐  │ │
│ │     [1]▢cube        │ [2] button │ REASONING ││  │ raw: {"verb":"pick_up","mark":9}          │  │ │
│ │      ☖player        │  {pressed: │"carry cube ││  └────────────────────────────────────────────┘  │ │
│ │                     │  false}    │ to button" ││  ┌ try2 ✗ rejected: not grabbable ──────────┐  │ │
│ │  held: ▣ mark 1     │ [3] door   │───────────││  └────────────────────────────────────────────┘  │ │
│ └─────────────────────┴────────────┴ VERB:     ││├─────────────────────────────────────────────────┤
│ │ RESULT  ✗ STUCK  final_dist 88  "no path"    ││  PROMPT ⇄ RESPONSE  [ system | turn-in | out ]  │ │
│ │ tokens: in 6.8k (img 5.1k) · out 0.5k (think  ││  ┌────────────────────────────────────────────┐  │ │
│ │        412) · cached 0                        ││  │ player=(1216,-892,64) holding=nothing      │  │ │
│ │ VERDICT: ( )ok (●)reason (◆)percept ( )actu  ││  │ exit dist=384 bearing=-12  marks: [1]cube… │  │ │
│ │          [ note ____________________ ]        ││  └────────────────────────────────────────────┘  │ │
│ └──────────────────────────────────────────────┘│                                                 │ │
│ ▸ [3] go_to mark=2    → SUCCESS   ✓ · · ·       │                                                 │ │
│ ▸ [4] interact mark=2 → SUCCESS   ✓ · · ·       │                                                 │ │
│ ▸ [5] go_to exit·done → SOLVED ★  ✓ ✓ · ·       │                                                 │ │
└──────────────────────────────────────────────────┴─────────────────────────────────────────────────┘
 Collapsed line = index · verb+args · result_code · ✓/✗ · verdict-dots.  Red left-border when result.ok=false.
```

**Per-step content (the expanded card)**
- **PERCEPTION (left, the hero):** `Step.frame_png` served raw as a real `<img>` (640×480, colored boxes + numbered mark labels already baked in by `HarnessAnnotate`); click → lightbox. Empty-frame placeholder so the page never breaks. Below it: `held: ▣ mark N` and player `(x,y,z)`.
- **PERCEPT (middle, grounded state):** one row per mark from `percept_json` -- `[N]` (color-keyed to its baked-in box), class, `pos`, `dist`, `bearing`, and the **state dict as compact chips** (`{cube_type, on_button}` / `{pressed}` / `{open}`). The state chips are the reasoning↔state contradiction detector.
- **DECISION (right):** **thinking** trace (collapsible, "internal" styling -- from `Step.thinking`, PR3); then self-reported **reasoning** (`Step.reasoning`, today); then the **verb pill** (decoded `MacroRequest`: e.g. `pick_up mark=1`); then `held_mark` after the step.
- **RESULT strip:** `ok` ✓/✗ + `result_code` + `detail` (decoded `MacroResult`), plus `reached`/`final_dist`/`moved_dist` when present; then the **token line** in `gemini_agent.py`'s exact format `in N (img M) · out N (think M) · cached N` (img/think need PR3).
- **VERDICT control (inline):** 4-way radio `ok / reasoning / perception / actuation` + a free-text note, written to `<traj>.verdicts.json` via a tiny `POST /verdict`. Never touches the `.trajectory`. Keyed by `(header content-hash, step_index)` so a re-record invalidates cleanly.
- **Right rail (sticky, follows selection):** (1) **top-down map** -- player-path polyline `0→…→n`, selected dot brightened, every mark as a labeled dot, exit ring at `success_radius`, facing arrow from `eye_yaw`; all from data already in every Step (zero extra capture beyond `eye_yaw`). (2) **retry sub-turns** -- `Step.attempts`, each a nested card with raw text + rejection reason + token cost (PR3; empty until then). (3) **PROMPT ⇄ RESPONSE tabs** -- `system_prompt` (header) | `prompt_sent` (turn-in) | `raw_response` (out).
- **Terminal/outcome:** `★` on the final collapsed line + a banner (`SOLVED|DONE|BUDGET|GAVE_UP`), echoed in the sticky header.

**Controls (keyboard + mouse)**

| Key / gesture | Action |
|---|---|
| `j` / `k` (or ↓/↑) | Select next / prev step; right rail follows. *Primary motion -- you walk decisions.* |
| Click collapsed line · `Enter` | Expand / collapse that step's card |
| `Space` | Expand-and-advance (read one, drop to next) |
| `o` | Cycle the selected step's verdict (ok→reasoning→perception→actuation) |
| `n` | Focus the verdict note field |
| `f` / `a` | Filter to failed / non-ok-verdict steps · show all |
| `m` | Toggle map open/closed |
| `t` | Toggle the thinking-trace block globally (some find it noisy) |
| Hover a percept `[N]` row | Cross-highlight that `[N]` everywhere in the prompt/response **text** + brighten its **map dot**. *(On-frame box highlight is OFF the table in v0 -- no pixel boxes, no camera pose.)* |
| Click the frame | Lightbox at full size |
| Right-rail `Tab` | Switch system \| turn-in \| response raw-text |
| `#step=N` in URL | Deep-link "the step where it broke" to a colleague |
| **No** play/pause, **no** tick slider | Explicitly removed -- a 6-17 step transcript is read, not played |

---

## 6. Hackability vs scalability call

**The phased story: a hackable v0 on a substrate that grows to the scalable end-state without a rewrite.**

- **Hackable now.** Everything in v0 reuses what exists -- `read_trajectory()` returns a clean `(header, list[Step])`, `harness_pb2` decodes action/result, frames serve raw, the dark theme is lifted verbatim. The whole sparse transcript ships as one `/traj` JSON blob (a few hundred KB) -- no streaming, no playback loop, no canvas-drawn frames. The JS is layout (innerHTML from JSON), not logic.
- **The seam that scales.** Two v0 outputs are the contract the future *macroscope* consumes without touching the viewer: (1) the **verdict sidecars** -- per-step `perception/reasoning/actuation` labels are exactly the data the M4/M5 "reasoning-gap vs locomotion-gap" result aggregates; (2) the **directory convention** `runs/<chamber>/<model>/<iso>.trajectory` adopted now (free) so a later index page is a thin add, not a migration. Trajectories stay self-contained source-of-truth, exactly like `.rollout`.
- **What is v0:** the transcript + three-pane card + result strip + top-down map + verdict-writeback + tally + failure-filter -- on **today's** fields, with PR3's fields rendering as honest empty blocks.
- **What is deferred (no rewrite to add):** Atlas's success-matrix / gallery / step-locked A/B compare and a `runs/index.json` (graduates to SQLite behind the same endpoint when cardinality hurts) -- build at M4 when runs accumulate. Filmstrip's single-file HTML export -- a later button. Rerun -- only if trajectories go dense at M3+. Aggregate token/cost analysis -- a separate marimo notebook, never bolted onto this viewer.

---

## 7. Phased PR plan

| PR | Scope (one sentence) | ~LOC | Cut line |
|---|---|---|---|
| **PR0 (capture)** | Additive `trajectory.proto` v2 (`thinking`, `repeated Attempt`, `TokenUsage.image/.thinking`, `prompt_sent`, `eye_yaw`) + `gemini_agent.py` recorder patch (set `include_thoughts=True`, accumulate retries, store splits/prompt/yaw) + `make proto`; gated by re-running a real Gemini solve before merge. | ~150 | **parallel / decoupled -- do FIRST, do NOT block the viewer** |
| **PR1** | `trajectory_loader.py` + `/traj` page + `/frame/{n}` + collapsed/expandable transcript with the three-pane card and RESULT strip, on **today's** fields only (thinking/retries/splits absent). | ~200 | ◀── **v0** |
| **PR2** | Top-down map (with `eye_yaw` facing when present) + mark↔text/map cross-highlight + verdict-writeback sidecar + header tally + failure-filter (zero proto change -- this is the thesis instrument). | ~120 | ◀── **v0 cut line** |
| **PR3** | Wire PR0's fields into the existing panes: thinking block, retry sub-turns, token-split line, prompt-in tab (no layout change -- they were empty blocks). | ~80 | deferred |
| **PR4** | "Export standalone HTML" button -- frames data-URI'd into one self-contained file for VP/Slack/paper. | ~120 | deferred |
| **PR5** | (M4, when runs accumulate) `runs/index.json` + success matrix + step-locked A/B compare, consuming the verdict sidecars from PR2. | ~250 | deferred |

**v0 = PR1 + PR2.** It answers "perception or reasoning?" on first light with no recorder change. PR0 lands in parallel and is the shared critical path for the features the user explicitly asked for.

---

## 8. What to reuse from py/vis

**Reuse (lift verbatim):**
- **Theme tokens** -- `vis.css` palette (bg `#0b0d12`, text `#b8c8e0`, accent `#4080ff`); copy the `~30 lines` of chrome, do **not** refactor the rollout viewer to share code (premature DRY is the trap here).
- **NiceGUI + FastAPI chrome** -- the `@ui.page` + endpoint pattern from `visualize.py` (111 LOC) and the `/meta`-as-one-JSON-blob shape; extension-dispatch `.rollout`→rollout, `.trajectory`→new page from a ~10-line `visualize.py` shim.
- **The loader pattern** -- mirror `rollout_loader.py` (156 LOC): one `load(path)` → `(meta, steps)`; but serve `frame_png` **raw** (`Response(bytes, media_type='image/png')`) -- no `frame_to_png` re-encode, since the bytes are already PNG.
- **The defensive Vue-mount init pattern** -- reuse `visualizer.js`'s `setTimeout(init, 50)` poll so the new page's strip/canvas bind reliably (NiceGUI mounts async).

**Do NOT reuse:**
- The **tick scrubber / range slider / play-pause loop** -- wrong primitive for a sparse decision transcript; replaced by the collapsed-line transcript + `j/k`.
- The **per-tick frame streaming / canvas frame draw** -- a trajectory is small enough to ship in one fetch; frames are `<img>` tags, not canvas draws.
- The **entity-grid flash-on-update HUD** -- that's tick-video UX; the percept marks render as static state-dict chips per step instead.

Keep the new code in its own `py/vis_traj/` module sharing **only theme tokens** -- different granularity, different viewer, kept small.

---

## 9. Open questions / future

- **Camera pose for re-projection.** v0 stores only `eye_yaw` (enough for the map's facing arrow). True on-frame mark-box highlighting needs either per-mark **pixel boxes** (the in-engine annotator already computes them to draw the labels -- cheap to emit as `repeated Box2D mark_boxes`) or full camera pose + FOV. Until then, correspondence is row + map-dot + text-token only. Decide whether to add `mark_boxes` in a PR3.5 -- it's the one genuine perception-fidelity gap.
- **Multi-trajectory dashboard (M4).** When does run count justify Atlas's matrix/gallery/compare? The verdict sidecars + directory convention are the contract; the trigger is "more than a handful of trajectories to aggregate." Resist building it for 2 rows ("a matrix for 2 rows is theatre").
- **Human fault-labeling at scale.** The verdict sidecar is unversioned ad-hoc JSON, keyed by `(header content-hash, step_index)`. Fine for research; before M4 decide on schema discipline and whether labels need provenance (who labeled, when, confidence).
- **Video / smooth playback.** This viewer is intentionally *not* a video. For frame-accurate motion (e.g. debugging locomotion), **link out** to the `.rollout`/`.dem` tick-viewer rather than embedding playback here. If M3+ adds dense per-tick sub-traces inside a decision, that drill-down is a **Rerun `.rrd`** linked per step, not inlined.
- **First-contact risk.** **Zero `.trajectory` files exist on disk** (verified) -- first light just landed. Code the loader defensively against empty `frame_png` / `reasoning` / `raw_response` and percept shape drift, and **test against a hand-synthesized fixture before the first real Gemini run**, or the viewer's first contact is also its first bug report.
- **Cross-model thinking shapes.** `Step.thinking` is a plain string so Claude/GPT summaries drop in unchanged; some responses may have no trace -- the thinking block must degrade to "no trace" and never block.