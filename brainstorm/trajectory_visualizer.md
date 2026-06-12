# Trajectory Visualizer -- Design Brainstorm

> **Status:** capture (proto v2 + recorder) is DONE. `first_light.trajectory` (5 steps) exists on
> disk. This doc is now ONLY about the standalone viewer. The prior "reuse the `py/vis` NiceGUI stack
> in a new `py/vis_traj` module" recommendation is **SUPERSEDED** -- no NiceGUI, no FastAPI, no server.

## 1. TL;DR / Recommendation

- **Build a zero-dependency static-HTML generator.** One pure-Python script reads a `.trajectory` and
  emits ONE self-contained `.html` -- frames base64'd inline, every token inlined -- a vertical scroll
  of decision cards. No server, no port, no JS framework, no new deps. The `.html` *is* the artifact.
- **Module path:** `py/llm_eval/vis_trajectory.py` (a single file; promote to a package only if it
  grows). Reuses `trajectory_io.read_trajectory` and `harness_pb2` (both already in the repo).
- **How the user runs it:** `uv run python -m llm_eval.vis_trajectory first_light.trajectory` -> writes
  `first_light.trajectory.html` next to it and (unless `--no-open`) pops it in the browser. `-o PATH`
  overrides output. Diff two solves = open two tabs. Hand to a VP = attach the file.
- **v0 cut line:** v0 already shows **ALL** data -- the long thinking trace (collapsible markdown-lite),
  failed/STUCK steps (red border + result_code, unmissable), and rejected retries (nested amber
  sub-tries). Nothing in the proto is hidden in v0; empty fields degrade to honest placeholders.
- **What it deliberately is NOT:** not a live server, not a verdict-writeback instrument, not a
  multi-run dashboard. Those are real but *later* and *separate* (section 6/8). A static file can't host
  them, and that's the point -- artifact over instrument, for now.
- **Why this and not the other two standalone options:** a tiny FastAPI server buys live `j/k` nav and
  future verdict-writeback but at the cost of a process+port and "you can't email it"; an editor-native
  Markdown report is even less code but loses the side-by-side card and renders `<details>`/`<img>` as
  literal noise in non-rendering views. The static `.html` is the KISS sweet spot: lowest liability that
  still shows everything cleanly AND travels as one file. (Full table in section 4.)

---

## 2. Framing: a sparse DECISION transcript where THINKING is the payload

A `.trajectory` is **~5-20 steps**, not a video. `first_light` is 5. Each `Step` is **one full LLM
decision cycle**: annotated frame in -> one verb out -> engine result back. The unit is the **decision,
not the tick**. There is no continuous timeline. So: **no play, no pause, no scrubber.** The right form
is a vertical column of cards you *read* top-to-bottom.

What's new since the prior doc: **the thinking trace is the payload.** In `first_light` it runs
750-2346 chars of multi-paragraph markdown per step (`**Analyzing the Scene**\n\nOkay, here's the
situation...`) -- often longer than everything else on the step combined. Rendering it richly *without
drowning the telemetry* is the central UX problem this design is built around (section 5).

This serves the **perception-vs-reasoning thesis** directly -- the verdict falls out of reading one card:

| Field | Question it answers | Failure it isolates |
|---|---|---|
| `frame_png` (what it *saw*) | Did the pixels contain the answer? | Entity missing/mis-annotated -> **perception gap** |
| `percept_json` (what it was *told* -- grounded state) | Was the grounded state correct? | `pressed:false` but reasoning says "button is pressed" -> **grounding gap** |
| `thinking` + `reasoning` (what it *reasoned*) | Did it choose right *given* a correct percept? | Frame fine + marks fine + wrong verb -> **reasoning gap** |
| `result` (the *outcome*) | Did the macro execute? | Right verb + `STUCK` -> **actuation/macro-layer gap** |

`first_light` step 0 is exactly this last case: a sensible `go_to mark=7` that comes back `STUCK
"dist=824 after march"` -- a clean read of "the model was fine, the locomotion macro wasn't," which is
the whole reason this benchmark exists. `result_code` (`SUCCESS|STUCK|BLOCKED|WALL|EDGE|BAD_MARK`) is
the cheap pre-classifier of *where* it broke and tints the card.

---

## 3. What we capture now (DONE) -- the data-completeness contract

Verified against `trajectory.proto` (v2, shipped) and `first_light.trajectory` (5 real steps). Every
field below has a home in the card. **Nothing is dropped.**

| Field | Type / real example | UI element |
|---|---|---|
| `Header.map` | `testchamber_000` | sticky header title |
| `Header.model` | `gemini-3.5-flash` | sticky header title |
| `Header.exit_pos` | `(815,854,1)` | header line + map exit ring |
| `Header.success_radius` | `10` | header line + map ring radius |
| `Header.system_prompt` | ~1509 chars | ONE collapsed `<details>` in header (identical every step -- shown once) |
| `Header.grammar` | ~623 chars (verb signatures) | collapsed `<details>` in header (provenance) |
| `Step.index` | `0..4` | card title `STEP N` + map path-dot number |
| `Step.frame_png` | 640×480 RGB PNG, 270-340KB, boxes+labels baked in | `<img src=data:image/png;base64,...>`; click -> lightbox; empty -> `(no frame captured)` |
| `Step.percept_json` | 11 marks, each `{mark,class,name,pos,dist,bearing,state{...}}` | PERCEPT pane, one row/mark; state dict as chips; action-target mark highlighted |
| `Step.player` | `(192,-96,0)` | FRAME caption + map `☖` dot |
| `Step.eye_yaw` | `90.0 / 64.6 / -25.4` | FRAME caption `yaw N°` + map facing arrow |
| `Step.held_mark` | `0` (none) here | PERCEPT header `held: none` / `held: ▣ mark N` |
| `Step.terminal` | `""` here (`SOLVED\|DONE\|BUDGET\|GAVE_UP` on final) | banner on final card + echoed in header |
| `Step.reasoning` | one-liner, e.g. "Walking towards the floor button..." | REASONING line, **always visible** |
| `Step.thinking` | 750-2346 chars multi-paragraph markdown | THINKING `<details>`, markdown-lite; OPEN on fails, collapsed on ok (section 5) |
| `Step.prompt_sent` | ~1040-1058 chars (exact percept text given) | collapsed `PROMPT SENT` `<details>`, `<pre>` |
| `Step.raw_response` | ~110-205 chars JSON | collapsed `RAW RESPONSE` `<details>`, `<pre>` |
| `Step.action` | `harness.MacroRequest` -> `go_to mark=7` / `look yaw=-90` / `aim_at mark=2` | verb pill in card title (decoded via `harness_pb2`; non-zero args only) |
| `Step.result` | `harness.MacroResult` -> ok bool + `result_code` + detail | RESULT strip; `result_code` chip drives card color |
| `Step.usage` | `TokenUsage{input,output,cached,image,thinking}` e.g. `in1964 img1064 out428 think394 cached0` | TOKENS line, gemini_agent's exact format |
| `Step.attempts` | `repeated Attempt{raw_response, rejection_reason, usage}` -- EMPTY in `first_light`, populated on bad JSON / rejected verb | ATTEMPTS block above the accepted action; nested sub-tries; `none (first try)` when empty |

**Derived for free (zero extra capture):** a top-down map from `player` (path), `eye_yaw` (facing),
`exit_pos`+`success_radius` (ring), and per-mark world `pos` (dots) -- all fields already present.

---

## 4. Approaches compared

| Option | One-liner | New deps | Simplicity | Verdict |
|---|---|---|---|---|
| **A. static-HTML** (recommended) | emit one self-contained `.html`, no server | **0** | ~250 LOC, 1 file | **Winner.** Lowest liability that shows everything AND travels as one file. |
| **B. tiny-server** | ~50-line FastAPI + one hand-written `index.html`, live `j/k` nav | 0 (FastAPI vendored) | ~350 LOC, 2 files + a port | Runner-up. Buys live nav + a future verdict-writeback seam; costs a process you can't email. |
| **C. editor-native Markdown** | emit `.md` + sidecar PNGs; VSCode/GitHub renders it | 0 | ~120 LOC, least code | Best for "the thinking is already markdown," but no side-by-side card; `<details>`/`<img>` are literal noise in plain-text views. |

**A (static-HTML) -- recommended.** Pure stdlib (`base64`, `html`, `json`, `webbrowser`) + the two
protos already in the repo. Emits one `.html` with ~40 lines of inline `<style>` and ~30 lines of inline
vanilla JS (collapse-all / fails-only toggles -- no framework, no CDN, no fetch). The map is inline SVG
built in Python. For `first_light`'s 5 steps the output is ~2.6 MB (2.1 MB inline frames + 0.5 MB
text/markup): drag to Slack, attach to a paper, drop in a VP deck -- it renders offline, forever, in any
browser. The honest ceiling is file size: a 20-step run is ~8-9 MB of HTML (still opens; heavy to email).

**B (tiny-server).** A single FastAPI process serves the parsed trajectory as one `/traj` JSON blob plus
a `/frame/{n}` raw-PNG passthrough, rendered by one vanilla-JS `index.html`. It wins precisely when you
re-open *many* solves and want instant `j/k` nav, a failed-only filter, `#step=N` deep-links, and -- the
real future payoff -- a one-`POST` **verdict-writeback** that turns the viewer into the labelled-dataset
front-end. It loses on "it's a process, not a file": you can't email it, and the agent (forbidden to
launch processes) can never smoke-test the running page. Right *later* tool, wrong *first* tool.

**C (editor-native Markdown).** The least code, and uniquely good at the one hard requirement -- the
thinking trace is *already* markdown, so it renders natively with zero transform. But frame/percept/
reasoning stack vertically (no side-by-side card), there's no failure-filter or step-walk, and on a
non-rendering view the `<details>`/`<summary>`/`<img>` tags show as literal noise. Keep it in mind as a
near-free alternate emitter, but A is the cleaner default artifact.

---

## 5. Recommended design: static-HTML decision cards

One sticky header + a vertical scroll of N decision cards (one per `Step`). Below is the **real**
`first_light` step 0 (the STUCK failure), then a sketch of a card with retries populated.

```
┌────────────────────────────────────────────────────────────────────────────────┐
│ testchamber_000  ·  gemini-3.5-flash      5 steps · 1 STUCK · terminal: (none)   │ ← sticky header
│ exit (815,854,1) r=10     tokens Σ in 32.6k / out 2.4k / cached 3.8k             │   (counts from data)
│ [▾ collapse all thinking]   [show: ● all  ○ fails only]   ▸system_prompt 1509c   │   (~4 lines inline JS)
│                                                           ▸grammar 623c          │
└────────────────────────────────────────────────────────────────────────────────┘

╔══ STEP 0 ═══════════════════════════════ go_to mark=7  →  ✗ STUCK ═══════════════╗ ← RED left-border (ok=False)
║ ┌── FRAME (640×480, click→full) ──┐  PERCEPT  11 marks  (held: none)              ║
║ │  <img src=data:image/png;base64 │  [7]● prop_floor_button (576,704,8)           ║ ← [7] highlighted: action target
║ │   ...inline, boxes + mark labels │      d=887 b=-26  {pressed:false}            ║
║ │   already baked in by annotator>│  [2]  prop_testchamber_door (192,-13,0)       ║
║ │                                 │      d=83  b=0    {open:null}                 ║
║ │  player (192,-96,0)  yaw 90°    │  [11] prop_weighted_cube (65,703,430)         ║
║ └─────────────────────────────────┘      d=809 b=9 {cube_type:standard,          ║
║                                                     on_button:false}             ║
║                                    ▸ 8 more marks (1,3,4,5,6,8,9,10) …            ║ ← <details>, collapsed
║                                                                                  ║
║ REASONING  "Walking towards the floor button to enter the main area…"            ║ ← one-liner, ALWAYS shown
║                                                                                  ║
║ ▾ THINKING  (1042 chars)            ← <details> OPEN by default on FAIL steps     ║
║   ┌──────────────────────────────────────────────────────────────────────────┐  ║
║   │ Analyzing the Scene                          (markdown-lite: **x** → <h4>, │  ║
║   │ ───────────────────                           blank line → <p>, [7] → chip)│  ║
║   │ Okay, here's the situation. We are at the start, facing the … We should    │  ║
║   │ head for button [7] which is the floor button …                           │  ║
║   └──────────────────────────────────────────────────────────────────────────┘  ║
║                                                                                  ║
║ ⚠ RESULT  ✗ STUCK   "dist=824 after march"   reached=False final_dist=823.8      ║ ← RED chip, unmissable
║ TOKENS  in 1964 (img 1064) · out 428 (think 394) · cached 0                      ║
║ ATTEMPTS  none (accepted first try)                                              ║ ← quiet when empty
║                                                                                  ║
║ ▸ PROMPT SENT (1040c)   ▸ RAW RESPONSE (121c)        ← two collapsed <details>   ║
║                                                                                  ║
║ ┌─ MAP (inline SVG) ──────────────┐  player ☖→(yaw 90°)   exit ◎(r=10)           ║
║ │  ◎exit          ·11cube         │  marks = dots; action-target [7] ringed      ║
║ │       ·7button (target, ringed) │  path so far: ☖0  (single dot on step 0)     ║
║ │  ☖0→  ·2door                    │                                              ║
║ └─────────────────────────────────┘                                              ║
╚══════════════════════════════════════════════════════════════════════════════════╝

╔══ STEP 0' (HYPOTHETICAL, retries populated) ════ pick_up mark=9 → ✗ then ✓ ═══════╗
║ ⟲ ATTEMPTS  2 rejected before accept     ← AMBER block, EXPANDED by default       ║
║   ┌ try 1  ✗ rejected ───────────────────────────────────────────────────────┐  ║
║   │ reason: "move: ticks must be an integer in [1,400], got None"             │  ║ ← rejection_reason (red)
║   │ raw:    {"verb":"move","dir":"forward"}                                   │  ║ ← the bad raw_response
║   │ cost:   in 1964 (img 1064) · out 120 · think 60                           │  ║ ← attempt.usage
║   └──────────────────────────────────────────────────────────────────────────┘  ║
║   ┌ try 2  ✗ rejected ───────────────────────────────────────────────────────┐  ║
║   │ reason: "bad_mark: no mark 99 in percept"                                 │  ║
║   │ raw:    {"verb":"pick_up","mark":99}                                      │  ║
║   └──────────────────────────────────────────────────────────────────────────┘  ║
║   ─── accepted ───  pick_up mark=9    (then the normal card body, as above)       ║
╚══════════════════════════════════════════════════════════════════════════════════╝
```

**The LONG thinking trace (the central UX problem) -- exact treatment, three moves, no JS framework:**
1. **Containment.** Each trace lives in a `<details>` summarized `THINKING (N chars)`. **ok** steps render
   it COLLAPSED (the page stays a scannable column of verbs+results); **fail/STUCK** steps render it OPEN
   (when it broke, you want the reasoning in your face). A header `[▾ collapse all thinking]` flips them
   all via ~5 lines of `querySelectorAll` JS.
2. **Markdown-lite at generate-time** (in Python, NOT a markdown library -- that'd be a dep): split on
   blank lines into `<p>`; a line that is `**X**` becomes `<h4>`; `html.escape` everything else; regex the
   inline `[N]` mark refs into styled chips so the prose links to the same numbers on the frame and in the
   percept list. The real traces are exactly this shape, so ~15 lines covers them.
3. **Readability.** A distinct "internal monologue" treatment -- left rule, muted color, `max-width ~70ch`
   so paragraphs don't sprawl -- visually separate from the terse telemetry.

**FAILED / STUCK made unmissable.** A card with `result.ok==False` gets a **RED left-border on the whole
box** + the verb pill reads `→ ✗ STUCK` (the `result_code`) so it screams even when scrolled past. The
RESULT strip puts `result_code` in a colored chip (green SUCCESS/DONE; red STUCK/BLOCKED/WALL/EDGE/
BAD_MARK) with `detail` and `reached`/`final_dist` alongside. The header carries a run-level `1 STUCK`
tally and a `fails only` toggle (inline JS: hide `data-ok='true'` cards) so a 20-step triage jumps
straight to the breaks.

**Rejected ATTEMPTS as nested sub-tries.** `Step.attempts` renders as an **AMBER** `ATTEMPTS ⟲ N rejected`
block that sits **ABOVE** the accepted action and is **EXPANDED by default** (rejected tries are exactly
what the user wants to see). Each is a nested sub-card: red `rejection_reason` header (e.g. `move: ticks
must be an integer in [1,400], got None`), the bad `raw_response` in a `<pre>`, and its token cost from
`attempt.usage`. Empty (as in `first_light`) -> a quiet `ATTEMPTS none (accepted first try)`. This is the
"perception or reasoning?" instrument: clean percept + wrong verb = reasoning; missing/mis-stated mark =
perception; right verb + STUCK = actuation.

**The rest of the card.** `reasoning` is the always-visible one-liner. The **verb pill** in the title is
the decoded `MacroRequest` (`go_to mark=7`, non-zero args only). The **token line** is gemini_agent's
exact format (`in 1964 (img 1064) · out 428 (think 394) · cached 0`). `eye_yaw`/`player` are the FRAME
caption + map; `held_mark` is the PERCEPT header; `terminal` is a banner on the final card. `system_prompt`
and `grammar` are shown **once** in the header (identical every step); `prompt_sent` and `raw_response`
are two collapsed `<details>` per card -- on demand, `<pre>` verbatim. The **top-down map** is inline SVG
built in Python from data already present (path polyline, facing arrow, exit ring, mark dots,
action-target ringed) -- zero extra capture.

---

## 6. Why this is the KISS call

**Hackability now.** ~250 LOC, one file, zero new deps -- the lowest-liability option on the table and
the literal "code is a liability" winner; you can read the whole generator top-to-bottom in one sitting.
It reuses `read_trajectory` (clean `(header, list[Step])`) and `harness_pb2` (decodes action/result).
Sparse data (5-20 decisions) is the *perfect* fit for a static vertical scroll: no playback, no scrubber,
no streaming, no async mount, no port math, no framework version coupling. The output is one
self-contained `.html` -- the artifact IS the share, and it doubles as the permanent archived record of a
run even after the `.trajectory` rots.

**What it does NOT foreclose (no rewrite to add later):**
- **A later live mode** -- if the nightly loop shifts to re-opening *many* solves with `j/k` nav and a
  failed-only filter, that's the tiny-server (option B): same loader, same `harness_pb2` decode, a
  ~50-line FastAPI shell. The static emitter survives as the "download standalone HTML" button.
- **Verdict-writeback** -- per-step `ok/perception/reasoning/actuation` labels into a `<traj>.verdicts.json`
  sidecar are the labelled dataset the M5 "reasoning-gap vs locomotion-gap" result aggregates. A static
  file can't persist them (would need a server or `localStorage`, contradicting "one shareable file"), so
  this is deliberately OUT of v0 -- it arrives with the live mode.
- **A multi-trajectory index / gallery / success matrix** -- premature while a handful of trajectories
  exist. Adopt a cheap `runs/<chamber>/<model>/<iso>.trajectory` directory convention now (free); build
  the aggregate UI when runs actually pile up. That's a *second tool*, not an extension of this one.

The trade is explicit and tasteful: **artifact over instrument.** Ship the smallest thing that shows
everything; let the instrument earn its server later.

---

## 7. Phased PR plan

All under `py/llm_eval/vis_trajectory.py`. Capture (proto v2 + recorder) is **already shipped** -- these
PRs are viewer-only.

| PR | Scope (one sentence) | ~LOC | Cut line |
|---|---|---|---|
| **PR1** | Loader + main()/argparse/`webbrowser`; the f-string HTML template (header strip + decision cards) showing frame (base64 inline), percept rows + state chips, reasoning, decoded verb pill, **RESULT strip with red-border + `result_code` for fails**, token line, terminal banner; `--no-open`/`-o`. | ~170 | ◀── **v0 (already shows fails)** |
| **PR2** | Thinking `<details>` with the ~15-line markdown-lite renderer (collapsed on ok, open on fail) + `[N]`-chip cross-link; **ATTEMPTS nested amber sub-tries** (reason + bad raw + cost); collapsed `prompt_sent`/`raw_response`/`system_prompt`/`grammar`; header `collapse-all-thinking` + `fails-only` toggles. | ~60 | ◀── **v0 cut line (ALL data incl. retries + thinking)** |
| **PR3** | Inline-SVG top-down map (path polyline, `eye_yaw` facing arrow, exit ring, mark dots, action-target ringed). | ~40 | deferred |
| **PR4** | (only if the nightly loop demands it) tiny-server live mode -- `/traj` JSON + `/frame/{n}` raw PNG + `j/k`/`fails-only`/`#step=N` + a `POST /verdict` writing `<traj>.verdicts.json`; the static emitter becomes its "download HTML" button. | ~150 | deferred |
| **PR5** | (M4, when runs accumulate) `runs/index.json` + success matrix + step-locked A/B compare, consuming PR4's verdict sidecars. | ~250 | deferred |

**v0 = PR1 + PR2.** It already shows **everything in the proto** -- failures (red border + result_code),
rejected retries (nested amber sub-tries), and the long thinking trace (collapsible markdown-lite) --
with empty fields degrading to honest placeholders. No further capture work is needed.

---

## 8. Open questions / future

- **Frame↔mark correspondence limit.** Boxes are baked into the PNG and **no camera pose/FOV is stored**,
  so an on-frame box highlight on hover is impossible in v0. Correspondence is via the shared `[N]`
  integer (box in the frame == row in percept == `[N]` chip in the thinking) + the map dot -- not a live
  ring on the image. Closing it would mean emitting per-mark `repeated Box2D mark_boxes` (the in-engine
  annotator already computes them to draw labels) in a later capture PR -- the one genuine
  perception-fidelity gap. Defer until someone actually needs hover-to-ring.
- **Huge-file guard.** Inline-base64 frames bloat ~33%: 5 steps ≈ 2.6 MB (fine), 20 steps ≈ 8-9 MB
  (opens, heavy to email), 50 steps would hurt. `loading=lazy` on `<img>` helps first-paint, not size.
  A `--thumbnail` downscale would re-introduce a cv2 dep and break zero-dep purity. Decide a step-count
  threshold above which we warn (or switch to a sidecar-frames mode) rather than silently emit a 50 MB
  file.
- **Live mode.** When the loop becomes "re-open many solves and jump to the break," graduate to the
  tiny-server (PR4). Same loader, same decode; the static emitter stays as the share button.
- **Verdict-writeback.** Per-step `ok/perception/reasoning/actuation` labels -> `<traj>.verdicts.json`,
  keyed by `(header content-hash, step_index)` so a re-record invalidates cleanly. Needs the live process
  (PR4). Before M4, decide schema discipline / provenance (who labeled, when, confidence).
- **Multi-trajectory gallery (M4).** Resist building a success matrix for 2 rows ("a matrix for 2 rows is
  theatre"). The directory convention + verdict sidecars are the contract; the trigger is "more than a
  handful of trajectories to aggregate."
- **Linking out to `.rollout`/`.dem` for video.** This viewer is intentionally *not* a video. For
  frame-accurate motion (debugging locomotion behind a `STUCK`), **link out** to the `.rollout`/`.dem`
  tick-viewer per step rather than embedding playback here.
- **Cross-model thinking shapes.** `Step.thinking` is a plain string, so Claude/GPT summaries drop in
  unchanged; markdown-lite handles `**bold**` + paragraphs only. A future model emitting tables / code
  fences / nested lists renders flat. Acceptable today; revisit if a model's traces get richer. An
  empty trace must degrade to "no trace," never block.
