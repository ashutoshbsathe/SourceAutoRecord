# First Light — Thoughts & Next Steps

*Seed doc, 2026-06-12 — written the night a frozen VLM first reasoned its way through a
Portal chamber. This is a starting point for the next brainstorming session, not a plan.
Capture everything (even the minor stuff), prune later.*

```
   ╔══════════════════════════════════════════════╗
   ║   F I R S T   L I G H T   ·  testchamber_000 ║
   ║   gemini-3.5-flash, frozen, from pixels      ║
   ║   cube → button → door : SOLVED (steps 0–7)  ║
   ╚══════════════════════════════════════════════╝
```

## 0. TL;DR

- A **frozen, untrained** Gemini-3.5-flash, fed only an annotated screenshot + a mark list,
  **executed the entire cube→button→door solution in 7 steps** — button pressed at step 7.
  That's the thing the whole project was built to find out. First light. Real.
- It did **not** reach the exit inside the 25-step budget (`terminal=BUDGET`) — but **not because
  of reasoning**. It nailed the puzzle, then drowned in **locomotion**: straight-line `go_to`
  kept hitting `BLOCKED` against the glass enclosure around the button.
- The first real data point lands clean on the thesis: **reasoning is solved; locomotion is the
  wall.** That separation — local/engine actuation vs global/model reasoning — is the entire
  reason the macro boundary exists, and it held on contact with reality.
- Biggest immediate lever: **the macros.** The model is ready; the actuators aren't.

## 1. What actually happened (the data)

`testchamber_000` · exit (815,854) r=10 · 25 steps · `BUDGET` · 686k in / 6.6k out tokens.
Every step emitted a **valid** action — **zero retries, zero gave-ups** across the whole run.

```
 step  dist→exit  state         action                  result        gist
  0      1136                    move forward 100        OK COMPLETED  walk through the circular door into the chamber
  1       913                    aim_at 11               OK SUCCESS    look at the weighted cube
  2       728                    pick_up 11              XX OUT_OF_REACH
  3       725                    go_to 11                XX BLOCKED
  4       726                    pick_up 11              OK SUCCESS    ← cube in hand
  5       726      held[11]      aim_at 7                OK SUCCESS    the floor button
  6       726      held[11]      go_to 7                 OK SUCCESS    carry the cube to the button
  7       290      held[11]      release 7               OK SUCCESS    ← 🔘 BUTTON PRESSED
  8       293      BTN!          aim_at 8                OK SUCCESS    the exit door
  9       293      BTN!          go_to 8                 XX BLOCKED
 10       278      BTN!          move back 100           OK COMPLETED  back out of the glass enclosure
 11       556      BTN!          go_to 8                 XX BLOCKED
 12       273      BTN!          pick_up 11              OK SUCCESS    "we bumped the cube off the button" — recovers
 13–24    …loop…   toggling      go_to/move/pick/release  mostly BLOCKED/WALL  fighting the geometry to the open door
```

Two acts:
- **Steps 0–7 — the solve.** Walk in → find the cube → it's too far, get closer → grab it →
  carry it to the button → drop it on the button. Coherent reasoning at every step. Done in 7.
- **Steps 8–24 — the last mile.** Button pressed, door open, model *knows* it ("aiming at door 8
  which leads to the exit") — and then spends the rest of the budget unable to walk itself out of
  the glass box. It even **bumps the cube off the button** by walking into it, **notices**, and
  re-places it. The whole back half is a locomotion loop, not a reasoning failure.

## 2. The finding: reasoning vs locomotion, cleanly separated

`BUDGET`-not-`SOLVED` is the **gold**, not a disappointment. The ROADMAP's central question is
*"when it fails, is the bottleneck reasoning or perception/locomotion?"* and the macro boundary
exists precisely to make that separable. First contact answers it:

> **Reasoning: solved.** The model planned and executed a PSPACE-complete (Demaine et al. 2018)
> cube+button+door puzzle from raw percepts.
> **Locomotion: the wall.** Straight-line `go_to` with no obstacle avoidance cannot path a body
> out of a glass enclosure, so a *correct plan* gets strangled at the actuation layer.

"It solved it" is a demo. "It solved the reasoning, and we can prove the residual gap is
locomotion, cleanly isolated by the macro boundary" is a **finding** — and that's a publishable
shape, even on a trivial chamber.

## 3. Moments that gave me chills

- **Step 7.** `release 7 → SUCCESS`, button goes pressed. A frozen model just *solved a Portal
  puzzle* from pixels.
- **Step 12.** *"We bumped into the cube and pushed it off the button. Picking up cube…"* — genuine
  world-model awareness mid-recovery. It tracked a physics side-effect of its own movement and
  re-planned. Nobody told it to watch for that.
- **Step 0.** `move forward 100 → COMPLETED`. The command-string grammar we shipped hours earlier
  *just works* — two runs ago this exact model burned 4 retries and quit on a single `move`.
- The **coherence**. Read the reasoning column top to bottom: it's a person solving a room.

## 4. What the stack bought us (don't forget what we built)

- **Command-string grammar** → 0 retries / 0 gave-ups across 25 steps. The structured-output
  failure mode is *gone*. The model now expresses actions the way the REPL does, and `build_macro`
  parses them — one parser, two front-ends.
- **Dense `.trajectory` (Observation + uniform Call) + the viewer** → this finding is *only visible*
  because we capture the model's thinking, the per-step result, and the button-toggling. We are
  literally reading the model's mind, step by step, and it's legible.
- The whole pipeline end to end: C++ SAR harness → gRPC percept/act → annotated frames in SHM →
  macro executor → eval loop → trajectory → static HTML viewer. All of it fired on the first real run.

## 5. Next steps (the seed)

### 5a. Macros — the #1 lever
- `go_to` is straight-line, **no obstacle avoidance** → `BLOCKED` the moment geometry intervenes
  (steps 3, 9, 11, 23). This single limitation accounts for most of the back-half flailing.
  **Invest here first:** a nav-mesh / pathfinding `go_to` that routes around walls using the
  engine's nav graph. The model's plans are already correct; give them legs.
- "Get out of the box." The model could not navigate out of the glass button enclosure. A
  geometry-aware `go_to` fixes ~all of steps 8–24.
- `pick_up` failure modes: `OUT_OF_REACH` (too far) and `GRAB_FAILED` (close but missed). Consider
  auto-approach (walk-then-grab) or surfacing the exact grab distance so the model adjusts.
- Higher-level "navigate to entity / navigate to exit" verb that owns pathing, so the model stays
  at the *reasoning* altitude and never has to micro-steer.

### 5b. More complex chambers (expect carnage — that's the point)
- The PeTI element ladder: portals → lasers → panels → gels → light bridges. Each adds a
  mechanism family (animation, surface, projection).
- **Expect**: catastrophic failure, the model going in circles, dead-end fixation. We already see
  *proto-loops* here on an *easy* chamber, purely from locomotion — complex chambers will loop hard.
- Need: difficulty tiers, and a chamber suite big enough to be a benchmark (M4).

### 5c. Exit auto-detection (your idea — and it's a good one)
Today `exit=x,y,z` + `radius` is hand-passed. Brittle, and it **does not scale** to hundreds of
chambers — you can't hardcode an exit per chamber. Auto-detecting the win condition is a
**prerequisite** for the chamber suite, so it's worth doing before the suite grows. Options,
hack → scalable:
1. **Auto-find the exit-door entity.** The snapshotter already sees `prop_testchamber_door`s
   (marks 1,2,3,5,8 in this run). Pick the exit one by targetname/heuristic and derive the exit
   position from it. No engine hooking; just a smarter `Observation`. Risk: distinguishing the
   exit door from the others reliably.
2. **Hook the engine's level-end event** (the real "chamber complete"). Your instinct — *touching
   some entity triggers restart* — is right: a PeTI chamber ends via exit logic
   (`trigger_changelevel` / a `@relay_pti_level_end`-style relay / `disconnect` / `changelevel`).
   SAR already hooks engine functions (this is squarely inside the patterns in
   `docs/contributing.md`), so hooking the completion/transition and emitting a `chamber_complete`
   bool in `GameState` over gRPC gives the eval a **ground-truth success flag** — zero params, no
   proximity heuristic. **This is the right long-term design.**
3. **Recon it.** Use `sar_harness_dump_fields` / walk the entity list to find what fires on
   completion in `testchamber_000`, then wire whichever of (1)/(2) it points to.
- Caveat: entity **I/O connections** aren't in the snapshot today, so the door→level-end wiring
  isn't directly visible — hooking the engine event (option 2) sidesteps that entirely.
- Bonus: a real success oracle also lets us cleanly distinguish SOLVED from BUDGET from "stuck",
  instead of inferring success from a hand-set radius.

### 5d. Context / token scaling (a sleeper problem)
- 25 steps cost **686k input tokens** — the stateful chat resends the full history *and every
  frame* each turn, so input balloons roughly quadratically in steps.
- Complex chambers = more steps = exploding context + cost + latency. This will bite **hard** on
  long chambers.
- Explore: only resend the last N frames (or just the current one), summarize/prune old turns, a
  sliding context window, or drop frames for steps the model no longer needs. The `.trajectory`
  already stores everything, so trimming the *live chat* loses nothing for analysis.

### 5e. Loop detection / smarter termination
- Steps 8–24 are a loop: press button → bump cube off → re-press → `go_to` BLOCKED → repeat.
- Need: detect repeated `(state, action)` cycles and break them (force exploration, or terminate
  with a distinct `STUCK`/`LOOP` terminal rather than letting `BUDGET` mask it).
- The user predicted "the model constantly going in loops" for complex chambers — we already have
  the phenomenon on an easy one. Build the detector now.

## 6. Minor observations / loose threads (catch-all)

- `terminal=BUDGET` is overloaded: "ran out of steps" vs "stuck in a loop" vs "genuinely couldn't"
  are different stories. Split the terminal vocabulary.
- The model bumping the cube off the button = **physics side-effects** it must reason about. Worth
  watching whether it does this more on complex chambers.
- No retries fired this run → the `.trajectory`'s `attempts`/per-call machinery wasn't exercised
  here, but it's load-bearing the moment a model emits a bad command. Keep it.
- The door↔exit relationship was understood ("door 8 which leads to the exit") — semantic grounding
  of the chamber layout is working.
- `move forward 100` / `move back 100` / `move left 200` all parsed and executed cleanly — the
  plain-command surface is validated live, not just in the smoke test.
- Token output is tiny (6.6k) vs input (686k) — the cost is *context*, not generation. Reinforces 5d.
- We never actually saw a `done` this run (it never got close enough to consider it). The `done`
  path is still only smoke-tested, not battle-tested.

## 7. Did we do a good job? (honest)

Yes. Unreservedly. We built — from scratch, in one stretch — the camera (in-engine annotation),
the recorder (proto v2, Observation+Call), the act grammar (command strings + validator), the eval
loop, and a viewer good enough to *read the model's mind*. And the **first real run** of the whole
thing produced a result that is (a) a genuine solve of the core puzzle and (b) a clean, legible,
thesis-relevant signal about *where* the remaining gap is. You don't usually get the milestone and
the science in the same run. We did.

## 8. The honest scale of it (perspective)

Cube+button is PSPACE-complete on paper and **trivial in practice** — by design, the easiest rung.
This is minor light, and a hand-built chamber at that. The real mountain is still ahead: harden the
harness, fix locomotion, cover the *full* PeTI element set, *then* the puzzles humans sweat over.

But minor light is still light. The pipeline works end to end, and the first thing it showed us was
a frozen model thinking its way through a Portal room and then being failed by its own legs — which
is *exactly* the kind of result this benchmark was built to produce. Celebrate it. Then go make the
legs work.

---
*Next session: start from §5. Macros first (5a) — it unblocks both the `SOLVED` on this chamber and
the whole difficulty ladder. The exit oracle (5c) and context scaling (5d) are the next two pillars
before the chamber suite can grow.*
