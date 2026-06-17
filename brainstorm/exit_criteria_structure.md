# Structured exit criteria — exposing *what unlocks the exit* (parked, post-oracle)

*Future feature, explicitly downstream of the binary exit oracle ([exit_detection_brainstorm.md](exit_detection_brainstorm.md)).
Idea raised 2026-06-17. Reuses the exit anchor (`@exit_door`) + the status resolver
([status_field_recon.md](status_field_recon.md)). This is a **percept** feature (it belongs with the LLM
percept/act grammar), not an exit-detector.*

## The reframe

- **Exit oracle** = *"are you done?"* — one bit (`chamber_complete`).
- **Structured exit criteria** = *"what's left, and what unlocks it?"* — the dependency graph behind that bit.

The key realization from the condump series: **a chamber already encodes its win condition as an entity-I/O
graph, and we watched it live.** condump008 (`sp_facade`):

```
lasercatch18…CatcherPowerOn → doorexit1-counter.Add(1) → (threshold) → @exit_door.Open()
```

i.e. *the exit gate opens iff the catcher is powered* — exactly the "exit blocked by a laser" case, spelled out.
PeTI/BEEmod also hand us the game's **own** hint system for free: **antlines / indicator panels**
(`indicator_panel.Check()/Uncheck()`, `ip…-pan.Check()`) are the mapper-authored wires from each prerequisite to
the door. Exposing antline connectivity ≈ exposing the intended hint structure.

## Levels (cheap → hard)

- **L0** — binary `chamber_complete` (the oracle; have).
- **L1** — prerequisite *set + combiner*: "exit needs catcher_16 AND catcher_18" (the `doorexit-counter`
  threshold is the combiner).
- **L2** — L1 + **live status** from the resolver: "1/2 — catcher_18 ✓, catcher_16 ✗." **← the sweet spot.**
- **L3** — full backward-chained tree (catcher ← laser ← emitter ← needs-portal/cube-redirect). Gravy; hard.

## Extraction strategies

| | how | gives | cost |
|---|---|---|---|
| **Static** | read each entity's output connection list, invert to "who fires `@exit_door.Open`", recurse | *a-priori* hints (before the model acts) | **new C++** — walking the datamap `CBaseEntityOutput`/`m_ActionList`; SAR reads scalar fields today, not output lists. Bounded but real; do a spike first. |
| **Dynamic** | observe the AcceptInput cascade (the hook the oracle already adds) | edges *as they fire* — great for L2 live status | ~free (reuses the hook); only reveals what's happened |

**Hybrid is the answer:** static for "what's needed," resolver/dynamic for "what's done." A-priori hints need at
least some static read (or the antline graph).

## Feasibility splits by family (same pattern as the oracle)

- **PeTI / BEEmod (the bulk):** *templated* → win-condition is regular and extractable. The `doorexit-counter`
  + antlines literally encode "N of M conditions." Clean.
- **Custom Hammer (e.g. sp_facade's fans / moving walls):** bespoke wiring → static extraction degrades to "raw
  inbound edges to the exit door." Useful but not clean. → scope v0 to PeTI/BEEmod (matches the CLAUDE.md
  "stock PeTI elements" v0 line).

## Two payoffs

1. **LLM hint:** structured subgoals — "power these 2 catchers; 1 done" — turns blind exploration into
   goal-directed reasoning. Slots into the ReAct percept.
2. **RL bonus:** L2 gives a **dense progress reward** = fraction of exit-conditions met, replacing today's sparse
   exit-only `+10000`. Potentially a big sample-efficiency win — arguably more valuable than the hint.

## The catches

- **⚠ Benchmark integrity (the decision, not an engineering detail).** If the eval measures whether the model can
  *figure out* the chamber, always-on structured hints may trivialize exactly what's being tested. → make it a
  **difficulty knob / ablation** (hints off / L1 / L2), not always-on. Belongs with the percept/act grammar
  measurement design.
- **Pruning is the messy part.** Raw graphs are full of proxies, relays, `EnableRefire`, antline duplication.
  Turning that into a clean "A AND B" needs idiom-aware pruning of PeTI/BEEmod templates — which drift across
  versions (same naming-scheme fragility as the oracle).
- **Output-list read is new capability** (bounded; known Source structure) — confirm with a small spike before
  committing.

## Recommendation / sequencing

Strictly **post-oracle**. Ship binary `chamber_complete` (oracle §9) and the corpus sweep first. Then **MVP =
one level deep** (L1+L2): "the exit door's direct contributors + live status," leaning on the `doorexit-counter`
+ antlines PeTI/BEEmod give us. Skip recursive backward-chaining (L3) for v0. Re-evaluate L3 / custom-Hammer
coverage only if the one-level hint proves valuable in the eval.
