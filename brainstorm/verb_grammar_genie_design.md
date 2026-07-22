# Verb Grammar — Point Algebra Design (genie harness)

> Output of the 23-agent point-algebra brainstorm (recon → battery → 4-way debate →
> adversarial → synthesis). **Supersedes [`verb_grammar_genie_seed.md`](verb_grammar_genie_seed.md)
> where they touch.** Design-only; open questions still live (see bottom). Nothing here
> is committed to code yet — this is the language design the seed asked for.

## What we locked going in (do not re-litigate)

- **TYPED AST**, not a string DSL. The algebra rides as a structured recursive proto
  `PointExpr`. Proto is *allowed to grow*; the goal is a **simplified** (typed, validated)
  surface, not a smaller one.
- **object and point unify** — everything a verb touches is a locus / point-expression,
  resolved through **one `ResolveTarget` choke**.
- **CONSOLIDATE, don't accrete** — reuse the existing machinery; show what deletes/reuses.
- **Genie principle** — grant the literal *spatial* wish best-effort, report residual +
  execution taxonomy, then STOP. Never close the wish→intent loop.

## The design (winner = Minimal Orthogonal + grafts)

Minimal Orthogonal Constructors won the backbone (scored 7/6/6, cleanest spanning basis).
Grafts: resolver-checked `TYPE_ERROR` from Typed-Locus (minus its model-declared `type`
fumble trap); UNSAT/AMBIGUOUS/RESIDUAL *arithmetic* + the "current-tick loci only" invariant
from Declarative (minus its least-squares solver — that IS the puzzle-solver); the `on:`
skew-foot selector, `near:` disambiguator, and first-class `through` from Relational.

### The primitive set — `Ref` + 9 constructors, one recursive `PointExpr`

| node | signature | job |
|---|---|---|
| `Ref` | `Ref(mark) -> Locus` | leaf → typed locus (point / 1-D ray / 2-D patch / disc / OBB) |
| `along` | `along(locus_1D, t) -> Point` | N@f, **ex-`percent`** (dimension-locked to 1-D) |
| `on_patch` | `on_patch(locus_2D, u, v) -> Point` | Sn@u,v forward bilerp, **ex-`where`** (2-D) |
| `project` | `project(p, onto) -> Point` | foot of a point onto a locus (unambiguous) |
| `meet` | `meet(a, b, on) -> Point` | locus∩locus; `on`∈{a,b} names which carries the skew-foot |
| `line` | `line(a, b) -> Locus_1D` | line through two points |
| `through` | `through(portal, x, preimage) -> Point` | portal-linkage transform, **points only** |
| `angle_on` | `angle_on(locus_1D, vertex, ref, theta, near) -> Point` | cone∩locus, mandatory `near:` |
| `offset` | `offset(base, dir, dist) -> Point` | translate along a `Dir` |
| `drop` | `drop(base) -> Point` | down-trace to floor rest (world probe) |

Splitting `along`/`on_patch` (P1 had one arity-overloaded `coords[]`) and `project`/`meet`
(P1's `meet` had a fatal foot-on-`b` ordering ambiguity) is the "grow toward a typed,
*validated* surface" decision in action — 10 nodes, still one choke.

### Battery expressions (12 mined construction problems)

```
1  collinear-through-portal-zlift    redirect_to(target=Ref("15"), aim=through(portal="Po", x=Ref("14"), preimage=true))
2  beam-interpose-projection-fraction interpose(target=Ref("15"), aim=drop(along(Ref("L10"), 0.24)))
3  beam-end-N1.0                     interpose(target=Ref("15"), aim=drop(offset(along(Ref("L10"),1.0), dir=axis_of(Ref("L10")), dist=-8)))
4  redirect-cube-aim-plusX           redirect_to(target=Ref("15"), aim=Ref("14"))          // +X aiming is verb-internal
5  line-through-portal-intersect-laser-skew  aim_at(target=meet(a=line(Ref("y"),Ref("Po")), b=Ref("L11"), on=Ref("L11")))
6  place-portal-beam-terminus-panel  place_portal(color="orange", target=meet(a=Ref("L10"), b=Ref("S3"), on=Ref("S3")))
7  panel-fractional-uv               place_portal(color="blue", target=on_patch(Ref("S15"), 0.98, 0.20))
8  through-portal-polyline-interpose interpose(target=Ref("15"), aim=drop(along(Ref("L10"), 0.6)))   // GATED (portal-recursion recon)
9  player-foot-projection-perp-offbeam offset(project(Ref("me"), onto=Ref("L10")), dir=perp_of(Ref("L10"), toward=Ref("me")), dist=32)
10 cube-on-button-seat               release(target=Ref("15"), aim=Ref("btn"))             // seat mechanics verb-internal
11 portal-front-stand-cell           go_to(target=drop(offset(Ref("Pb"), dir=normal_of(Ref("Pb")), dist=24)))
12 floor-portal-ledge-fling          place_portal(color="blue", target=on_patch(Ref("Sn"), 0.9, 0.5))
```

### The oracle kill — why the claim stays airtight (structural, not policy)

The one fatal every adversary found: `residual` against a *goal* locus, over a *hypothetical
routed beam*, is a hill-climbable gradient to the beam-routing answer. Closed by three rules
that fall out of the **typing**, not policy:

1. **`through` is points-only** → "the beam as it would be after the portal" is not a
   constructable ray.
2. **A reflector cube is an OBB; its local +X redirect axis is NOT a 1-D locus** →
   `axis_of(cube)` is a `TYPE_ERROR`. Redirect aiming lives *inside* `redirect_to`, never a
   model-writable locus. (P2's fatal `Foot(receiver, Through(BoxAxis(cube,+X), Pb))` is now
   unspellable.)
3. **Only current-tick realized loci are addressable; no verb output is ever a `PointExpr`
   operand.**

Consequence: the only beam you can `project`/`meet` against is the **real, current,
percept-visible** one — whose miss you can already read off the frame. No gradient over poses
is constructable. This keeps `neg-collinear-through-portal-z-compensated` (the razor, one hair
from the legit positive) inexpressible. **5/5 solver-smuggle negatives rejected.**

## Proto delta

```proto
// ===== ONE recursive locus expression (resolves to a ResolvedLocus) =====
message PointExpr {
  oneof node {
    Ref ref = 1; Along along = 2; OnPatch on_patch = 3; Project project = 4;
    Meet meet = 5; Line line = 6; Through through = 7; AngleOn angle_on = 8;
    Offset offset = 9; Drop drop = 10;
  }
}
message Ref      { string mark = 1; }                                   // "<n>"|"S<n>"|"Pb"|"Po"|"L<n>"|"me"
message Along    { PointExpr locus = 1; float t = 2; }                  // resolver: locus must be 1-D
message OnPatch  { PointExpr patch = 1; float u = 2; float v = 3; }     // resolver: patch must be 2-D
message Project  { PointExpr p = 1; PointExpr onto = 2; }
message Meet     { PointExpr a = 1; PointExpr b = 2; PointExpr on = 3; } // on must be a or b
message Line     { PointExpr a = 1; PointExpr b = 2; }
message Through  { string portal = 1; PointExpr x = 2; bool preimage = 3; }
message AngleOn  { PointExpr locus = 1; PointExpr vertex = 2; PointExpr ref = 3;
                   float theta_deg = 4; PointExpr near = 5; }           // near REQUIRED; theta literal-only
message Offset   { PointExpr base = 1; Dir dir = 2; float dist = 3; }
message Drop     { PointExpr base = 1; }
message Dir {
  oneof d {
    PointExpr normal_of = 1;   // surface/portal outward normal (2-D locus)
    PointExpr axis_of   = 2;   // ray/funnel/line forward axis (1-D ONLY — cube OBB => TYPE_ERROR)
    Perp      perp_of   = 3;   // horizontal perp, side pinned by `toward`
    bool      up        = 4;
    Bearing   bearing   = 5;
  }
}
message Perp    { PointExpr locus = 1; PointExpr toward = 2; }
message Bearing { PointExpr from = 1; PointExpr to = 2; }

// ===== MacroRequest: retype 2 strings, DELETE 2 side-channels =====
message MacroRequest {
  string    verb   = 1;                // KEEP
  PointExpr target = 2;                // WAS string
  PointExpr aim    = 3;                // WAS string
  int32     ticks  = 4;                // KEEP (move/wait analog escape hatch)
  string    dir    = 5;                // KEEP (move)
  int32     yaw    = 6;                // KEEP (look)
  int32     pitch  = 7;                // KEEP (look)
  string    color  = 9;                // KEEP (place_portal)
  reserved 8, 10; reserved "percent", "where";   // percent -> Along ; where -> OnPatch
}

// ===== MacroResult: ADD the construction-honesty legs =====
enum ConstructCode { CC_EXACT=0; CC_RESIDUAL=1; CC_UNSAT=2; CC_AMBIGUOUS=3; CC_TYPE_ERROR=4; CC_NOT_OBSERVABLE=5; }
enum ResidualKind  { RK_NONE=0; RK_SKEW_FOOT=1; RK_CLAMPED=2; RK_PITCH_FORCED=3; RK_BACKOFF=4; RK_ANGULAR_DEG=5; }
message MacroResult {
  bool ok = 1; string result_code = 2; string detail = 3;   // KEEP (verb execution taxonomy)
  bool reached = 4; float final_dist = 5; float moved_dist = 6; float aim_pitch = 7; float aim_yaw = 8;  // KEEP
  Vector3 resolved_point = 9;      // NEW leg-1 "here's what I did"
  ConstructCode construct_code = 10;
  float residual = 11;             // NEW deviation (units in detail: "u" | "deg")
  ResidualKind residual_kind = 12;
  int32 solution_count = 13;       // NEW 0=UNSAT | 1 | >=2 / -1=AMBIGUOUS
  string bad_node = 14;            // NEW on CC_TYPE_ERROR: "along.locus expected 1-D, got PATCH"
}
```

**Deletes (C++):** the `TargetRef` tagged union, `RequireEntityMark`, the four hand-rolled
per-verb `ClassifyTarget` re-parses (place_portal / pass_through / jump_into / drop_into), the
`InterposeGate` percent plumbing. **Reuses:** `ResolveTarget` (MacroExecutor.cpp:556) grown
into `Eval(PointExpr) -> ResolvedLocus`; `ClassifyTarget` (522) becomes the `Ref`-leaf lexer;
`ResolvePanelPoint` = `OnPatch`; `ComputeBeamSegment` = `Along`/`Meet`/`Project` over a laser;
`ComputeRedirectYaw` stays inside `redirect_to`; `DownTraceRest` = `Drop`.

## Unsat / multi-solution / residual (four ordered tiers)

- **Tier 0 — `CC_TYPE_ERROR` (no world access):** walk the AST, dimension-check each operand
  (`along.locus` 1-D, `on_patch.patch` 2-D, `through.x` a point, `axis_of` 1-D). Counted as a
  separate **syntax-fumble** class, never a geometry failure.
- **Tier 1 — leaf resolution:** `Ref` → `BAD_MARK`/`WRONG_KIND`; operand whose feeding percept
  field is absent → `CC_NOT_OBSERVABLE` (gates laser/through/patch pre-percept).
- **Tier 2 — geometric solve (closed-form):**
  - `CC_EXACT` — clean incidence, residual ≈ 0.
  - `CC_RESIDUAL` (common, mandatory) — best-effort *always* yields a point, so the honest
    deviation is compulsory; **no code path snaps without a residual** (the first_light
    transient-seat lie is structurally unrepresentable). skew `meet` → common-perpendicular
    foot on `on:`, `RK_SKEW_FOOT`; out-of-domain `along`/`on_patch` → clamp, `RK_CLAMPED`;
    non-coplanar `through` a flat cube → `RK_PITCH_FORCED` (azorae's 3.7°).
  - `CC_UNSAT` (0-solution) — parallel/non-meeting loci, ray misses patch, `drop` over void.
    `solution_count=0`, verb aborts — fact about the geometry, never a verdict on the plan.
  - `CC_AMBIGUOUS` (>1) — coincident loci, cone∩line=2. **Harness never silently picks**;
    family-generating nodes (`angle_on`) carry a required `near:` anchor.

`resolved_point` echoed on every committing verb (leg-1 honesty → model reasons on the true
realized premise).

## Scoreboard

| proposal | coverage | smuggle | percept |
|---|---|---|---|
| **Minimal Orthogonal** (winner) | 7 | 6 | 6 |
| Declarative Solver | 7 | 4 | 4 |
| Relational Predicate | 6 | 5 | 4 |
| Typed-Locus | 5 | **3 (killed)** | 5 |

## Required percept additions (legibility = experimenter's job)

Each gated constructor returns `CC_NOT_OBSERVABLE` (naming the missing field) until its percept
dependency ships — a valid wish over an unsurfaced locus is a crisp *non-reasoning* reject.

1. **Beam polyline on `GameState`** (top priority, unblocks ~5 positives) — per emitter: `L<n>`
   handle + origin + forward vector + first-hit endpoint (+ segment vertices once portal
   recursion lands). `ComputeBeamSegment` confirmed portal-blind.
2. Portal linkage basis / transform M (makes `through` foreseeable).
3. Panel `corners[4]` in `SurfaceMark` (currently only the axis-aligned AABB).
4. Receiver/catcher acceptance direction on `point_laser_target`.
5. Cube redirect-axis + emitter/funnel forward axes as unit vectors (today only Euler).
6. Button pressed-state (so the caused "smack" is observable).
7. (minor) portal disc radius; `drop` `NO_FLOOR` point-contents read (goo/void).

## OPEN QUESTIONS (unresolved — for the user)

**Genuinely need a ruling (philosophically load-bearing, and coupled):**

1. **Residual as a partial oracle (top risk).** `project(receiver, onto=current_beam).residual`
   still reports the *current* beam's miss to a goal — a bounded, monotone number (observable
   from percept anyway, but a gradient). **Claude's lean: ship as-is, documented** — suppressing
   residual on "goal-class" operands is the special-case the whole philosophy kills, and leaky
   anyway. Adversaries: major, not fatal.
2. **`aim_at` honesty legs (#1's evil twin).** `aim_at` is free + reversible, so echoing
   `residual` gives a cheap iterable dry-run to hill-climb aim without spending a turn.
   **Claude's lean: report `resolved_point` (legibility) but STRIP `residual`** — deny the free
   iteration. Trades against legibility, so it's the user's call.

**Claude leans rubber-stamp (flagged for a nod):**

3. **`angle_on` in v0 — defer.** No battery positive needs it; softest smuggle brush.
4. **`ComputeBeamSegmentThroughPortals` (portal-recursion) — defer.** Gates 1 positive; 8 of 12
   need no laser. Ship the straight cases first.
5. **Wire-typed vs resolver-checked dimensions — resolver-checked for v0** (Tier-0 TYPE_ERROR);
   measure fumble-rate, escalate to true wire-types only if high.
6. **Verify-in-engine, not design calls:** `through` direction convention (M vs M⁻¹, which
   mouth) against real `m_matrixThisToLinked`; button trigger-center vs OBB-center for `release`.
7. **Portal transform M in percept:** ship *current-state* M (safe, derivable from two mouth
   bases anyway), never a hypothetical routed ray — confirm this line.

## Pointers

- Workflow transcript: `subagents/workflows/wf_409783e8-378/` (23 agents, journal.jsonl).
- [`verb_grammar_genie_seed.md`](verb_grammar_genie_seed.md) — the philosophy this implements.
- [`azorae_stride_remake_postmortem.md`](azorae_stride_remake_postmortem.md) §6 — source of the algebra.
- Battery detail (12 positives + 5 negatives) captured in the workflow output.
