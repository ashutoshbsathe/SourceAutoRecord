# Fixed-Ontology Scope — go deep on PeTI, not wide on everything

*Decision record. Companion to [puzzlemaker_elements.md](puzzlemaker_elements.md) (the element
list) and [status_field_recon.md](status_field_recon.md) (the fields). This doc is the
**why** and the **layering** — read it first.*

## Decision

The harness commits to a **fixed, curated ontology** of stock Portal 2 Puzzle Maker
(PeTI) puzzle elements. We model that set **deeply** (semantic, typed status per class)
and treat the long tail of custom/community entities as **geometry only** — perceived as
a box, never semantically modeled in v0.

> Go **deep** into a small predefined set, not **wide** (and inevitably shallow) across all
> entities a mapmaker could place.

## Why this is mostly already decided — and what's actually new

The *element-list* half is already on the books: v0 = stock PeTI, with **Sendificate /
BEEmod-custom / Hammer contraptions explicitly P1** (puzzlemaker_elements.md, and the
project CLAUDE.md note). That scope today governs only the **top** layer — the annotation
overlay (`kClassColors` in `HarnessAnnotate.cpp`) and the status resolver.

**What this doc adds:** the same scope now governs the **data layer** — the
`EntitySnapshotter`, the `.hdem` recorder, the rollout, and the gRPC observation path —
which today is still **generic** (Phase 4 discovers *all* SendTable fields on *all*
classes; see [phase4_sendtable_discovery.md](phase4_sendtable_discovery.md)). So this isn't
a new "should we go narrow?" question; it **resolves a layering inconsistency**: the
percept was scoped to PeTI while the substrate beneath it still tried to be universal.

## Why deep-narrow beats wide-shallow

1. **The recon proved generic is worst-of-both-worlds.** SendTable discovery floods ~64
   junk fields/entity (`m_nModelIndex`, render color, sim time…) **and still misses** the
   datamap-only fields that actually carry meaning (`m_bPowered`, `m_nCubeType`,
   `m_toggle_state` — see [status_field_recon.md](status_field_recon.md)). "Generic" was
   high-volume, low-signal, *and* incomplete on the signal.
2. **An agent needs a small, stable, typed percept** ("reflective cube here, not held;
   catcher lit"), not a raw field firehose it has to learn to ignore.
3. **Tractability + reproducibility.** A fixed element ontology bounds the classes the
   annotation/resolver/snapshotter must support and keeps chambers reproducible by anyone
   with retail Portal 2. This mirrors how the Portal complexity literature treats the game
   — and that literature also shows a *small subset of stock elements already captures the
   full reasoning hardness* (see **Precedent** below).
4. **It's a prerequisite for separating confounding failure modes** (VP feedback). A
   curated semantic percept + the macro grammar (`go_to mark=N`, `shoot_portal mark=N`)
   lets the agent **reason over marks** while the engine **handles precise aim** — so a
   *reasoning gap* (chose the wrong mark/verb) is cleanly separable from a *locomotion gap*
   (aimed 1° / 1 cm off). The generic firehose conflates the two. This is the decisive
   argument: the scope cut is what *enables* the higher-level API, not just tidiness.

## Precedent: the Portal complexity paper (Demaine, Lockhart & Lynch, FUN 2018)

[*The Computational Complexity of Portal and Other 3D Video Games*](https://arxiv.org/abs/1611.10319)
is direct methodological cover for this decision. Its thesis (abstract): *"We isolate
individual mechanics of the game and prove NP-hardness, PSPACE-completeness, or
(pseudo)polynomiality depending on the specific game mechanics allowed."* Four takeaways:

1. **A fixed, finite element set is the right unit of analysis.** Their whole method is to
   enumerate a finite catalog of Portal mechanics — portal gun, long fall, weighted cubes,
   Heavy Duty Super Buttons, timed buttons, doors, Emancipation Grills (fizzlers),
   lasers/relays/catchers, turrets, High Energy Pellets, moving platforms, excursion
   funnels — and reason about *subsets*. Our fixed ontology is the same move.

2. **A small subset of *stock* elements already yields maximal hardness — so the tail costs
   us zero reasoning richness.** *Thm 8.4:* **cubes + weighted buttons + doors alone are
   PSPACE-complete** (reduction from Nondeterministic Constraint Logic — buttons/cubes
   implement door-controlling switches). *Thm 8.5:* **lasers + relays + portals + moving
   platforms are PSPACE-complete.** *Thm 3.1:* portals alone are only pseudopolynomial;
   *Thm 5.2 / Cor 6.2:* turrets, or timed door buttons, are independently NP-hard. Every
   element in the PSPACE results is a stock PeTI element in our **category A**. The full
   reasoning challenge is therefore *already inside v0*; Sendificate/BEEmod add perception
   and generalization burden but **no new reasoning-complexity class.** This is the
   decisive argument for the scope cut.

3. **The elements they *don't* formalize match our deferrals.** They explicitly skip hard
   light bridges, gels, and faith plates — exactly our **category B** (surfaces /
   chamber-mutating geometry, deferred past v0). Independent convergence on core-vs-later.

4. **They separate continuous momentum from discrete logic — the VP's split, formalized.**
   Their §4 hardness is encoded in *momentum/flinging* (numbers stored in velocities
   acquired by falling, `v = √(2αs)`, requiring unbounded terminal velocity → weakly
   NP-hard via Subset Sum), whereas most other proofs treat play as *discrete state
   transitions*; they bridge the two by noting positions/velocities are fixed-point and
   time is discretized. That is formal precedent for isolating the **continuous
   locomotion/physics** difficulty from the **discrete reasoning** difficulty — precisely
   the *reasoning-gap vs locomotion-gap* separation our macro grammar is built to expose
   (agent reasons over discrete marks/verbs; the engine handles the continuous aim/fling).

## The tier model — scope differs by layer

The key refinement: **not** narrow-vs-wide everywhere. Scope per layer, because the layers
have different reversibility and different consumers.

| Layer | Scope | Rationale |
|---|---|---|
| Annotation overlay | **strict ontology** | boxing unknown `func_brush`/`prop_dynamic` floods the view — the A2 lesson |
| Agent observation (gRPC percept) | **strict ontology + semantic status** | a confusion-free typed percept; this is what the VP failure-mode split rides on |
| **`.hdem` recorder (substrate)** | **generous — the full snapshotter superset: all networked fields (all classes) + curated datamap status (known classes)** | recording is **irreversible** (can't recover an uncaptured field; live demos can't be re-recorded), so keep maximal insurance. Junk networked fields cost ~nothing — delta compression flattens static values. Adding the curated datamap status here is the *same* snapshotter registration the percept needs, so it's free. |
| rollout | **the curated PeTI subset** — a *projection/view* over the substrate | consumed by the RL trainer + visualizer; re-derivable from `.hdem` (`hdem_to_rollout.py`) under an expanded schema **without re-recording** |

## The tail (custom / absurdist maps)

Perceived as **bare geometry** (a box) or dropped from the percept; **never semantically
modeled in v0**. This is both honest and generalization-friendly — the model must reason
about an unknown shape, which it would have to do anyway. We do **not** pretend to
understand Sendificate. Going fully blind in the **recorder** (not even capturing the box)
is the one thing to avoid, per the tier table.

## BEEmod is not a separate tier

Many BEEmod items compile down to stock classnames (a custom fizzler is likely still
`trigger_portal_cleanser` with different keyvalues; custom buttons may reuse the
`prop_*_button` family). Those come in **for free** via the classname set. Only BEEmod
items that introduce **new classnames** are P1 alongside Sendificate. **Let the classname
set decide — not a policy knob.**

## What changes (work items)

- **Percept/observation path:** replace generic Phase-4 SendTable discovery with the
  **curated per-class status schema** (the recon table). This is the *"register curated
  datamap status fields in the snapshotter"* item already noted in the phased plan and the
  [phase4_sendtable_discovery.md](phase4_sendtable_discovery.md) follow-up.
- **`.hdem` recorder:** settle the open decision below, then align.
- **Annotation:** already narrow (`kClassColors`) — no change.
- **Catcher/relay caveat carries over:** power lives on a child `point_laser_target`, not
  the prop; the resolver must associate them (see recon doc).

## Decisions

1. **`.hdem` recorder scope — RESOLVED (2026-06-08): generous.** `.hdem` dumps the full
   snapshotter superset (all networked fields + curated datamap status); percept and
   rollout **project** to the curated PeTI subset. Reversibility wins: a generous substrate
   keeps rollouts re-derivable under an expanded schema without re-recording. (See the tier
   table — narrowing is a consumer-side filter, not a capture-side cut.)
2. **arxiv 1611.10319 citation — RESOLVED (2026-06-08): verified.** Scope and headline
   results confirmed against the paper; captured in the Precedent section above. Safe to
   cite externally.
3. **v0 element scope — EXPANDED (2026-06-09): the full stock-PeTI surface set is the v0
   *target*, but phased *after* first light.** Category B (flip panels, gels, light
   bridges) is no longer P1 — it's in v0. The line that matters is **mechanism, not
   in/out**, and it splits B into two families:
   - **Animation-state family — flip panels + pedestal button + door.** One mechanism:
     read the entity's animation state (`m_nSequence`) + a targetname-pattern matcher for
     panels (`func_brush` / `ramp_*` / `*_panel`). Building the panel reader is the same
     reader that finally cracks the door. Panels look **server-animated** (boot log shows
     server `makeramp_*` sequence events) → likely server-readable; the door is
     **client-animated / logic-gated** (confirmed: it opens visually but every server
     field stays static) → its open-state likely lives on the controlling relay or
     client-side, not the door prop.
   - **Surface family — gels + light bridges.** Genuinely separate sensors (paint-map
     read; projector/volume read), **no synergy** with the animation family. Worth doing,
     but their own phase, off the first-light critical path.

   *Rationale for expand-but-sequence:* the full surface set is needed for a *broad*
   benchmark; **first light needs only one simple cube→button→door chamber**, so target =
   everything, order = first light first. (The "more elements → more robust detection"
   intuition holds *within* the animation family — one consolidated reader beats
   per-element special-casing — but not across to gels/bridges, which are independent.)
   Sequence: Phase 1 status rework → first light → Phase 2 animation family (panels+door)
   → Phase 3 surface family (gels, bridges).

## See also

- [puzzlemaker_elements.md](puzzlemaker_elements.md) — the element list (categories A/B/C, P1)
- [status_field_recon.md](status_field_recon.md) — per-class status fields + net/dm tags
- [phase4_sendtable_discovery.md](phase4_sendtable_discovery.md) — snapshotter discovery + the curated-registration follow-up
- [entity_snapshotter_redesign.md](entity_snapshotter_redesign.md) — snapshotter hot-path design
- [llm_percept_act_phased_plan.md](llm_percept_act_phased_plan.md) — build order
