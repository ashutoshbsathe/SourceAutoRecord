# Act-Grammar Altitude — Closed Semantic Verbs vs Code-as-Action (Voyager/Mineflayer-style)

**Status:** decision brief / brainstorm. Resolves the one open *altitude* question hanging over the
act grammar in [`llm_percept_act_grammar.md`](llm_percept_act_grammar.md) §4: should the frozen model emit
**one closed semantic JSON verb per step** (the current candidate) or **write code** against a rich API
and accumulate a **skill library** (the Voyager/Mineflayer paradigm), possibly as a **visual+coding hybrid**?

**Provenance:** user question (2026-06-10) — "Voyager felt like a complete cheat because it never received
anything visual; ours would be a visual+coding hybrid — does that make sense?" Backed by an industry-wide
sweep of gaming-agent harnesses (11 paradigms researched, 8 load-bearing facts adversarially fact-checked).

**TL;DR recommendation:** **Ship the closed semantic verb set as the primary instrument. Do _not_ make
code-as-action the v0 grammar, and do _not_ build a skill library.** Keep the visual+coding hybrid as a
**P1, separate A/B arm** — built only if first-light data shows the bottleneck is *in-step composition*
(not perception, not single-step reasoning). The survey *strengthens* the choices already in the decisions
log; it is the mainstream principled design for a reasoning-isolation eval, not a compromise.

---

## 1. The one axis that matters

Every gaming agent sits on a single spectrum: **how much competence the harness pre-bakes vs how much the
model must produce per step.**

```
RAW CONTROL ───────────► CLOSED VERB / SKILL SELECTION ───────────► OPEN CODE-AS-ACTION
(VPT, SIMA)              (SayCan, AlphaStar, OpenAI Five,           (Voyager, CodeAct,
 pixels→keys             ReAct, ★ this project's candidate)          CRADLE: free programs
 fused score             function-id + typed args / pick-a-verb      + skill library)
```

A second, **orthogonal** axis is the *observation channel*: **pixels-only** (VPT, SIMA, CRADLE, Computer-Use)
· **symbolic-only** (Voyager, OpenAI Five, AlphaStar, Mineflayer) · **visual+symbolic-together** (Set-of-Marks
family, Claude/Gemini Plays Pokémon, **★ this project**).

**What each cell *measures* is the crux** — and it's the whole reason this project exists:

- **Raw control** → perception + motor + reasoning fused into *one inseparable score*. You cannot tell a
  missed jump from a missed plan. This is the exact confound the project is built to remove.
- **Closed verb/skill selection** → isolates the *global decision*, because the engine owns local actuation.
  A failure is attributable to the *choice*, not the *execution*.
- **Open code-as-action** → measures composition / skill reuse, but **re-fuses reasoning with code-writing
  competence**: arbitrary code can implement locomotion *and* the puzzle solution, dissolving the
  local(engine)/global(model) line this project deliberately draws (grammar doc §4).

**The project occupies a cell no surveyed system fully occupies:** *closed semantic verb-selection* (isolates
actuation) **+** *visual+symbolic-together percept with shared marks* (keeps perception a measurable variable
instead of deleting it). **That cell is the instrument the scientific question requires.**

---

## 2. The industry sweep (what each system actually does)

All facts below were independently fact-checked against primary sources.

| System | Observation | Action grammar | Frozen? | Isolates reasoning? |
|---|---|---|---|---|
| **Voyager** (NVIDIA/Caltech '23, [2305.16291](https://arxiv.org/abs/2305.16291)) | **Symbolic-only, NO pixels** (confirmed). Mineflayer state: inventory, blocks ≤32u, entities, biome, health, 3D pos + error text | **Open code-as-action**: GPT-4 writes async JS over Mineflayer; skill library of verified JS keyed by description embeddings, top-5 retrieved + composed | **Frozen** GPT-4; all learning in the external skill library | **No on perception** (deletes it — the "cheat"); code re-fuses reasoning w/ code-writing (hallucinates nonexistent fns) |
| **Mineflayer** (host API) | Symbolic-only privileged JS accessors (`bot.entities`, `bot.findBlock`) | Free code + a **closed** declarative sub-grammar (`pathfinder` Goals) living *inside* the code API | N/A (protocol client) | No — exact perception handed over |
| **AlphaStar** (DeepMind '19, Nature; [1708.04782](https://arxiv.org/abs/1708.04782)) | Structured unit list (≤512 ents), **NOT pixels**; re-hidden by fog/camera (not god-mode) | **Factored**: function-id (100s) + autoregressive typed args (unit pointer-net, 256×256 target grid, delay); ~10²⁶ space; legal-action set returned | **Trained** (SL on replays + league RL) | Mirror of this project: *constrains* a policy to human limits to isolate strategy |
| **OpenAI Five** (OpenAI '19, [1912.06680](https://arxiv.org/abs/1912.06680)) | ~16k-number vector from Bot API, **NOT pixels** (human-accessible info only) | **Factored**: primary (≤30) + param heads (Delay 4, Unit 189, Offset 9×9); per-step action filters | **Trained** (10mo PPO self-play) | Strongest precedent *for* closed/factored verbs at scale; symbolic-only made perception unaskable |
| **VPT / MineDojo / MineRL** | Pixels-only (VPT 128×128); perception *is* the task | **Raw human motor** (~11 keys + mouse bins / `MultiDiscrete`) | **Trained** (VPT: 70k hrs video + RL) | **No** — bundles all three; the confound to avoid |
| **SIMA 1/2** (DeepMind '24/'25, [2404.10179](https://arxiv.org/abs/2404.10179)) | **Pixels-only, no privileged state** + language | Raw k/m (SIMA 2: structured text → keypresses) | **Trained** (BC + online RL) | **No** — proof-by-counterexample for the thesis |
| **CRADLE / Computer-Use** (CRADLE [2403.03186](https://arxiv.org/abs/2403.03186)) | Pixels-only screenshots; documented coord-hallucination floor | CRADLE: **code-as-action** (Python skills, post-hoc validation only); Computer-Use: closed **pixel** verbs `click[x,y]` | CRADLE frozen GPT-4V; mixed | **No** — pixels-only is the reasoning-vs-perception confound |
| **CodeAct** (Wang et al. ICML '24, [2402.01030](https://arxiv.org/abs/2402.01030)) | Text/tool I/O; not embodied | Free Python as unified action. **+20.7pp** over JSON on M3ToolEval w/ GPT-4 — but **only** on multi-turn *compositional* tasks; **regresses vs JSON** on the best open model & atomic tasks | Format edge shown on frozen frontier models | No inherently — edge is composition + fewer turns, both discounted in a frozen turn-based loop |
| **ReAct / SayCan / ALFWorld** ([2210.03629](https://arxiv.org/abs/2210.03629), [2204.01691](https://arxiv.org/abs/2204.01691)) | ReAct text; SayCan scalar affordances | **Closed/fixed verb sets** (ReAct 3 verbs *deliberately weak to force reasoning*; SayCan 551-skill menu) | **Frozen** | **Yes — home turf.** Fixed verb set is the *control variable* |
| **Claude / Gemini Plays Pokémon** | **Visual+symbolic-together** (screenshot + RAM overlay; Gemini *needed* RAM injection) | Claude: closed verbs + `navigator((x,y))` **macro** (engine pathfinds, model picks target); Gemini *evolved* toward `run_code` | **Frozen**, eval-only | Partial — navigator macro draws exactly the local/global line |

**Three things this table makes undeniable:**

1. **Every system that successfully isolated reasoning did it by *closing* the action space** (ReAct, SayCan)
   and/or *handing over perception symbolically* (Voyager, OpenAI Five) — but that second move is exactly
   what makes the perception question *unanswerable*.
2. **Code-as-action's only robust empirical win** (CodeAct's +20.7pp) is **conditional on strong models +
   compositional multi-turn tasks**, and its *mechanism* (fewer turns, tight closed-loop revision) is
   **near-worthless in a world-frozen, no-deadline, turn-based eval**.
3. **The closest real precedent is Claude/Gemini Plays Pokémon** — frozen production model, eval-only,
   visual+symbolic percept, and a *navigation macro* that is literally `go_to(mark)`. Copy that template.

---

## 3. Was Voyager "a cheat"? (the user's question, answered precisely)

**The fact is unambiguous: original Voyager received NO pixels, ever** — the authors confirm a *hard*
architectural reason ("Due to Mineflayer's limitation, we currently can not directly get the bot's view"),
and they explicitly *decline* to compare against pixel-input baselines because it "would not be an
apple-to-apple comparison." The demo videos were rendered post-hoc; the agent never saw them.

**But "cheat" is unfair as a blanket judgment, and being precise about *why* is what informs our design.**
Voyager was not answering the perception-vs-reasoning question. Its question was: *can a frozen LLM do
open-ended, lifelong, compositional skill acquisition?* For **that** question, deleting perception is a
legitimate scoping move — like a chemist using purified reagents. **The "cheat" framing only becomes correct
*relative to our question.* If we imported Voyager symbolic-only, *we* would be deleting the exact variable we
set out to measure — and *that* would be the cheat, committed by us, not by them.**

**What symbolic-only actually *bought* Voyager** (and why it mattered):
1. **Drift-free object reference for free** — `findBlock`/`entities` give ground-truth handles, so code can
   name objects reliably and there is zero grounding tax.
2. **It made code-as-action *viable* at all** — a skill library of callable functions only works if the host
   API exposes a *real, reliable symbolic surface*. You cannot accumulate composable skills over a flaky
   perceptual channel.
3. **It sidestepped 3D perception + sensorimotor control entirely**, concentrating all difficulty on
   sequencing. The tell that perception was genuinely *deleted*, not solved: for spatially complex builds the
   authors **fell back to humans** giving visual critique. The agent was literally blind to anything not
   surfaced as text.

**The honest verdict:** *not* a cheat by Voyager (correct scoping for their question); it *would* be a cheat
*for our question*; and **this project's visual+symbolic-with-shared-marks percept is the principled
correction** — it's the "Voyager with eyes" that Voyager itself couldn't build, and it's already decision #2
in the grammar doc ("visual+symbolic always").

---

## 4. Is a visual+coding hybrid coherent? (yes — and here's exactly what it'd be)

**Coherence is settled: yes.** CRADLE (frozen GPT-4V, screenshots → executable Python skills), SIMA 2 (Gemini
reasons over pixels → parsed actions), and Gemini Plays Pokémon's drift toward `run_code` are existence proofs.
Mineflayer even shows closed verbs (`pathfinder` Goals) living *inside* an open code API. So the question is
not "can it exist?" but **"is it the right *primary* instrument for *this* eval?"** — and there the answer is no.

**What the hybrid would concretely look like here** (Voyager-faithful, not a regression to the cheat): the
model *sees* exactly what it sees today — the annotated 224×224 Set-of-Marks frame + player telemetry + the
entity-list JSON. **Only the output changes:** instead of one JSON verb/turn, the model writes a short program
in a **sandboxed mini-language whose *only* callable primitives are the existing macros**, re-exposed as typed
functions — `go_to(mark)`, `shoot_portal(color, mark)`, `pick_up_cube(mark)`, `wait(ticks)`, … — plus loops,
conditionals, variables, and the ability to branch on the **same structured failure codes**
(`INVALID_SURFACE`, `CANT_FIT`, `FIZZLED`) raised as catchable exceptions. The macros still run in the C++
executor; **the marks still bridge pixels ↔ symbolic list ↔ code API to one engine handle** (the bridge
Voyager lacked). Optionally, verified per-chamber programs persist as named code for cross-chamber reuse.

**What the hybrid ADDS** (honestly, three things):
1. **In-step composition** — `for m in [3,5,7]: shoot_portal("blue", m); go_to(m)` as one validated action.
   *This is the only thing CodeAct's evidence says code reliably buys — and only on compositional tasks.*
2. **Cross-chamber skill reuse** — a Voyager-style library; pays off *across many episodes*, not within one.
3. **A higher expressivity ceiling** for maneuvers the verb authors didn't anticipate (chained portals,
   momentum tricks).

**What it COSTS, specific to *this* science** (four things, each directly damaging):
1. **It re-fuses reasoning with code-writing competence** — "did it fail the puzzle or fail to write valid
   code?" becomes ambiguous, *directly contaminating the reasoning-vs-perception attribution.* A frozen VLM
   (likely not frontier-code-tier) emits code less reliably than a code-tuned LLM — and **CodeAct itself shows
   code *regresses* vs JSON on weaker models and atomic tasks.**
2. **You lose clean pre-execution validation** — you cannot statically check arbitrary code against the live
   entity list before it runs, so you fall back to Voyager/CRADLE *post-hoc* error feedback, forfeiting the
   "reject before burning a step" property the frozen turn-based loop makes nearly free (grammar doc §8.1).
3. **The skill library re-opens the control variable** — "did it reason, or replay an accumulated
   chamber-specific macro?"
4. **CodeAct's headline advantages are discounted to ~zero here** — its win comes from *fewer turns* and
   *tight closed-loop revision*, both worth most under a latency/turn budget. The world-frozen, no-deadline
   loop is precisely the regime where JSON wins.

**Net: coherent, yes; worth it as the *primary* instrument, no.**

---

## 5. Recommendation

> **Ship the CLOSED SEMANTIC VERB SET as the primary, headline instrument.** Do **not** make code-as-action
> the v0 grammar; do **not** build a skill library for this benchmark. Treat the visual+coding hybrid as a
> **P1, separate A/B condition** — never blended into the primary eval — and build it **only if** the
> closed-verb data shows the bottleneck is *in-step composition* (not perception, not single-step reasoning).

Why this is the right call *for this project specifically*:

- Your question is **reasoning-isolation under granted perception+actuation.** The closed-verb design uniquely
  gives you what an eval needs and code forfeits: a **bounded, analyzable action space**; **pre-execution
  validation against the live entity list** (the lexer ≈ OpenAI strict-mode function-calling + SayCan's
  affordance gate moved to validation time); **deterministic structured failure codes** that cleanly attribute
  failure to reasoning-vs-actuation; and a **clean local/global line** that code dissolves.
- The **skill-library argument — Voyager's whole reason for code — evaporates here.** You are eval-only,
  single-chamber, success/fail, with **no lifelong horizon to compound over.** A library would re-open the
  control variable for *zero* scientific gain — a direct KISS violation.
- **You lose nothing by deciding late.** Closed-verbs and code-as-action are **two altitudes over the same C++
  macro set.** Building the verbs now (already on the critical path: Track C) wastes nothing if you later add a
  code arm — the host-API investment is shared (the Mineflayer lesson; AlphaStar/OpenAI Five both built *typed
  primitives*, not code). So: **ship closed verbs, get first light, and let DATA — not CodeAct's frontier-model
  headline — decide whether the hybrid arm is ever needed.**

---

## 6. Design principles (apply regardless of altitude)

These are grammar-*independent* and several are higher-ROI than the verbs-vs-code choice itself:

1. **Decide the PRIMITIVES first; altitude is a late, swappable choice.** The macro set is the real artifact.
2. **Target by STABLE HANDLE, never coordinates.** Set-of-Marks integers anchored to `(entity_index, serial)`
   = AlphaStar's pointer-net unit selection + OpenAI Five's 189 unit slots. This kills the
   coordinate-grounding tax that Computer-Use/CRADLE pay and the drift that SAM-segmentation SoM has — the
   engine-anchored marks are a **strict upgrade** over published SoM (which itself jumps GPT-4V grounding from
   25.7 → 86.4 on RefCOCOg, [2310.11441](https://arxiv.org/abs/2310.11441)).
3. **Keep perception a measurable VARIABLE, never delete it.** Visual+symbolic-together with shared marks is
   the bridge Voyager lacked. (Already decision #2.)
4. **Make legality first-class & PRE-EXECUTION, checked vs LIVE state.** The Python lexer = AlphaStar's
   available-action list + OpenAI Five's action filters. Code-as-action forfeits this; preserve it.
5. **Structured typed failure codes are the highest-ROI, grammar-independent component — invest here first.**
   `INVALID_SURFACE`/`CANT_FIT`/`FIZZLED` = Reflexion + Inner-Monologue structured feedback that drives
   frozen-model recovery. They move the needle more than verbs-vs-code.
6. **Draw macros at the LOCOMOTION/AIM line, never the PUZZLE-DECISION line.** Risk (cf. Gemini pre-computing
   spinner destinations): a macro that auto-selects the target or pre-solves a sub-puzzle **leaks the answer.**
   `state{...}` must describe **what exists, never what to do.**
7. **Keep the world-freeze / no-deadline loop and frame it as the removal of a latency confound** — SIMA's
   async world penalizes thinking *time* not *quality*; the freeze is the eval-time luxury AlphaStar/OpenAI
   Five couldn't take, and the reason CodeAct's "fewer turns" edge doesn't transfer.
8. **If you ever build the hybrid, FENCE the code:** its only callables are the validated macros (not raw
   engine access), it must consume the annotated frame + mark-indexed list, and it raises the typed failure
   codes as catchable exceptions. Run it as an explicit A/B on the **same chambers, same frozen VLM, same
   percept** — the delta measures what expressivity buys *on top of* grounded perception.
9. **Track the BALROG knowing-doing gap as a first-class metric** ([2411.13543](https://arxiv.org/abs/2411.13543)):
   probe the model out-of-band for a chamber's solution *and* measure in-loop execution. A large gap localizes
   the bottleneck to actuation/grounding rather than reasoning — directly answering the scientific question,
   nearly free given the transcripts.

---

## 7. Risks

- **Long-horizon failure is grammar-INDEPENDENT and will bite either way.** Both Pokémon harnesses
  looped/forgot for hours (Claude 78h in Mt. Moon) *with* summarization + external memory. Closed verbs don't
  fix it; keep chambers short, re-inject the goal each step, log per-step state to detect loops. Don't blame
  the grammar for a loop.
- **Over-helpful annotation silently turns a reasoning task into a lookup.** If `state{...}` or marks encode
  *what to do*, you contaminate the measurement — invisible until you audit transcripts.
- **Observability-mode confound:** global mode leaks info a player couldn't have. The §6 egocentric toggle is
  the control; headline claims must state which mode produced them; A/B both before any "reasoning is/isn't the
  bottleneck" verdict.
- **Premature hybrid** spends the sandbox/safety/complexity budget CodeAct & CRADLE warn about *and* muddies
  attribution. The CodeAct headline is a frontier-model, compositional, latency-budgeted result — none of those
  conditions hold here.
- **Frozen-VLM code-writing weakness:** adopting code "because CodeAct said code wins" would likely *lower*
  measured performance and add variance — the worst-case regime for code-as-action.
- **Long-pause networking:** the no-deadline freeze relies on a long-held gRPC stream with **no keepalive
  configured today** (grammar doc §3/§10). Add keepalive/max-connection-age before relying on long
  deliberation, or dropped streams will be misread as model failures. (= Track C8.)

---

## 8. Open questions (resolve at the grammar checkpoint / before any hybrid arm)

1. **What fraction of the chamber suite actually NEEDS in-step composition** vs decomposes into
   one-deliberate-move-per-step? *This single fact decides whether the hybrid arm is ever worth building.*
   If <~10% need composition, closed verbs are sufficient permanently. **(Audit the chambers.)**
2. **Portal-traversal-during-`go_to`** (halt+`blocked_by_portal` vs walk-through) is a *primitive-semantics*
   decision affecting **both** grammars equally — resolve at the macro level first (grammar doc §4).
3. **Save/load anchors** (§4, model-chosen named slots): expose as **verbs** in the closed set, or reserve for
   a future tree-search/code arm? They're the one place "compose moves" value appears *without* a skill library.
4. **If the hybrid A/B runs:** what success metric cleanly separates "code helped *reasoning*" from "code
   helped because the model writes valid code"? Design a **code-validity-normalized** metric *before* building.
5. **Does egocentric observability change the verdict?** Active perception adds steps that code could batch —
   run any A/B under **both** observability modes.

---

## 9. Relationship to existing decisions

This brief **confirms and makes explicit** what the decisions log already implies — it is not a reversal:

- Grammar doc §8: *eval-only · visual+symbolic always · macros C++-side* → all three are exactly what makes the
  closed-verb altitude correct and code-as-action ill-fitting.
- The new explicit decision to add to the ROADMAP log: **"Act-grammar altitude = closed semantic verbs
  (v0); code-as-action + skill library is a P1 A/B arm, gated on a demonstrated composition bottleneck."**

> See also: [`llm_percept_act_grammar.md`](llm_percept_act_grammar.md) (the verbs + thesis),
> [`llm_percept_act_phased_plan.md`](llm_percept_act_phased_plan.md) (Track C builds the shared macro
> primitives), [`ROADMAP.md`](ROADMAP.md) (state + decisions log).
