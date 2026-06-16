# Exit / Completion Detection — brainstorm + recon plan

*A living design space for a robust, scalable, non-hallucinatable map-completion oracle for
the SAR harness. This is catalog **A1** (`chamber_complete` engine oracle) + **A2** (Challenge
Mode timer as oracle) made concrete — see [diverge_catalog.md](diverge_catalog.md) §A. Replaces
the hand-passed `--exit x,y,z --radius` hack that caps the project at ~5 hand-measured chambers.*

> **Revision note (2026-06-16):** rewritten from the first draft after a code-grounded recon
> pass (10-area parallel read of the speedrun/CM/session/entity/harness surface). The first
> draft's biggest claim — "workshop/PeTI maps are in Challenge Mode" — is **wrong under the
> harness** and that error inverts the conclusion; see §2.0. Everything below cites `file:line`
> from the actual tree. Nothing here is built yet; the gate to building is the §6 recon session.

---

## 0. TL;DR (the punchline)

1. **The user's instinct is correct and most of it already exists.** SAR already detours
   `CBaseEntity::AcceptInput` *globally* (`Server.cpp:562`), already recognizes the universal
   Portal 2 "level ended" input (`portal_stats_controller.OnLevelEnd`, `Server.cpp:620`),
   already has a `CM_FLAGS` event carrying the game's own challenge-complete flag
   (`Server.cpp:411/428`), and already fires `SESSION_END{transition}` on a campaign changelevel
   (`Session.cpp:164`). The speedrun timer is literally a rule engine that turns "entity X got
   input Y" and "map transitioned" into timer splits (`Categories.cpp:172-244`). **We are not
   inventing a detector; we are subscribing to one.**

2. **THE catch nobody had written down:** the harness loads maps **bare** —
   `map sp_a2_triple_laser` / `restart_level` (`autoexec.cfg:15`, `Portal2HarnessImpl.cpp:514`) —
   with no challenge args. So `client->GetChallengeStatus() == NONE`, which **gates OFF both**
   CM paths (`Server.cpp:620` and the end-node init at `Server.cpp:706`). The single cleanest
   "chamber ended" event, `Event::CM_FLAGS`, **never fires under the harness today.** The unlock
   is one of: (a) drop/widen that gate, or (b) load in challenge mode. Both are small.

3. **No single signal covers all three map families** (PeTI workshop / Hammer custom / stock
   campaign). The end state is a small **layered oracle** (X7): try the precise engine signal,
   fall back to the next, geometric backstop last — first to fire wins, surfaced as one
   `bool chamber_complete` in `GameState`.

4. **Partly de-risked by a first recon (§2.4, 2026-06-16).** On a real bare-loaded workshop chamber
   the exit edge that fires is **`RunScriptCode(ReadyForTransition())` on the `*departure_elevator*`**
   (the PeTI exit-elevator VScript) — challenge-independent, visible on the current load. My earlier
   `@relay_pti_level_end` / `OnLevelEnd` guesses did **not** fire. So **X2 now has a verified match
   key**; the open bit is whether that name is stable across PeTI template versions (confirm on a few
   more maps). The same capture confirmed the transition never completes (no host-state change), so X1
   is dead for standalone workshop.

**Recommended spine:** ship the shared `chamber_complete` plumbing (§7) → land **X1**
(`SESSION_END{transition}`, ~1 line, campaign/Hammer) + **X2** (match `ReadyForTransition` via a
`HarnessExit` callback — **now signal-confirmed**, workshop) → fold both into the **X7** ladder → add
**X3** (force-CM `CM_FLAGS`) only if we need the chamber to *terminate cleanly* (not just be detected)
or to corroborate. Reach for the map-instrumenter family (**X4–X6**) only if pure observation can't
see the exit on real workshop maps.

---

## 1. Goal & context — what we're replacing

Today the harness decides "done" with a hand-passed exit coordinate + radius:

- `py/rl_challenge_env.py:146` — `_check_terminated` is literally `return bool(abs(dist) <= 100)`.
- `py/testchamber_session.py:44` — `reached_exit(obs, exit_pos, radius)`.
- `py/macro_repl.py:198-262` — `--exit "x,y,z" --radius 64`.

Failure modes of the radius oracle:
- **Doesn't scale** — every chamber needs a hand-measured coordinate, so bulk eval over hundreds
  of workshop maps is impossible. This is *the* thing capping the suite at a handful of chambers.
- **False positive** — agent stands near the exit door without solving the puzzle → "SOLVED".
- **False negative** — agent finishes but ends a hair outside the radius → episode never terminates.
- **2D-told / 3D-judged distance bug** noted in first light (catalog A10).

We want a server-authoritative, position-independent, edge-accurate success bit that needs **zero
per-map config** for the common case.

---

## 2. What SAR / the engine already knows — the signal inventory

### 2.0 The master hook + the gate that disables it under the harness

Everything funnels through one chokepoint: **`AcceptInput_Hook`** (`Server.cpp:562`), a detour on
`CBaseEntity::AcceptInput` installed unconditionally at every `SESSION_START`
(`InitAcceptInputTrampoline`, `Server.cpp:669/705`). **Every entity input in the running map**
passes through it — including the exit relay's `Trigger` and the stats controller's `OnLevelEnd`.
SAR already uses it to drive the speedrun timer (`TestInputRules`, `Server.cpp:584`) and to record
demos. The harness subscribes to **none** of this today; it only takes `SESSION_START/END` for hdem.

**The gate.** Two of the cleanest signals are wrapped in `client->GetChallengeStatus() == CHALLENGE`:

- the `portal_stats_controller.OnLevelEnd` branch (`Server.cpp:620`);
- the `challenge_mode_end_node` `StartTouch` hook only *binds* in challenge mode
  (`InitCMFlagHook` is called on `SESSION_START` only when `GetChallengeStatus() == CHALLENGE`,
  `Server.cpp:706`), and the node itself is only *spawned* by `ChallengeMode::CreateNode` when
  `sv_bonus_challenge` is set and only for hardcoded `customNodes` maps (`ChallengeMode.cpp:19-25`).

Because the harness loads bare (`Portal2HarnessImpl.cpp:514`), `GetChallengeStatus()` returns
`NONE`, so **all of this is inert.** The first draft of this doc assumed the opposite ("Challenge
Mode, which includes playtesting/workshop maps"). That is false for the harness load path and is
the single most important correction here.

### 2.1 The signal table

| # | Signal | What fires it | Hook / event (file:line) | Gated by CHALLENGE? | PeTI/workshop | Hammer custom | Stock campaign |
|---|--------|---------------|--------------------------|:---:|:---:|:---:|:---:|
| S1 | `portal_stats_controller.OnLevelEnd` input | map's own level-end logic fires `OnLevelEnd` | `AcceptInput_Hook` → `Server.cpp:620` | **yes (removable)** | likely (unverified) | only if map uses the controller | partial (campaign uses changelevel) |
| S2 | `challenge_mode_end_node` `StartTouch` → `CM_FLAGS{end}` | player touches the exit node | `StartTouchChallengeNode` `Server.cpp:440`; init `Server.cpp:679-693` | **yes (mode-dependent)** | strong **iff** node exists/armed | none (no node) | none (not CM) |
| S3 | `Event::SESSION_END{transition}` | engine `changelevel` (host state → `HS_CHANGE_LEVEL_*`) | `Session.cpp:164`; `engine->isLevelTransition` `Engine.cpp:376` | no | weak (stay-on-map rating card → no changelevel) | strong (most end via changelevel) | **strong** |
| S4 | PeTI exit elevator: `RunScriptCode(ReadyForTransition())` on `*departure_elevator*` | player rides the exit elevator | `AcceptInput_Hook` (`Server.cpp:584`) | no | **confirmed on a real bare load (§2.4)** | n/a (PeTI prefab) | n/a |
| S5 | campaign transition entities | `@transition_from_map.Trigger` (SP), `relay_exit_succeed`/`timer_try_exit` (coop) | already matched at `Server.cpp:630-642` (under `sar_transition_timer`) | no | n/a | rare | strong (the actual SP transition edge) |
| S6 | rating-card / leaderboard UI opens | run-end `openleaderboard` (`+leaderboard 4`) / `cmboard` net msg | `Client.cpp:343/454`, `g_leaderboardOpen` | yes | corroborator only | none | none |

**The shape of the problem this table makes obvious:** S1/S2/S4 are the workshop signals (and
two of three are CHALLENGE-gated); S3/S5 are the campaign/Hammer signals (un-gated, already
firing); S6 is a narrow corroborator. A robust oracle ORs across rows, with a priority order so
the *precise* signal wins over the *coarse* one when both fire.

### 2.2 The speedrun timer is the existence proof

The whole pattern is already implemented for speedruns. A category is an ordered list of named
`SpeedrunRule`s; each rule has an action (`START/STOP/SPLIT/...`) and one of 8 trigger variants —
`EntityInputRule`, `ZoneTriggerRule`, `MapLoadRule`, `MapEndRule`, … (`Categories.cpp:172`,
`Rules.cpp`). `MapEndRule::Test()` is content-free (always true) and is triggered purely by events:
`SESSION_END{transition}` (`Categories.cpp:235`) and `CM_FLAGS{end}` (`Categories.cpp:239`). So
"the map ended → fire a timer event" is **shipping code**. The catch (per recon): the per-stock-map
*rules* are 100% hardcoded targetnames in `CategoriesPreset.cpp` and do **not** generalize to
arbitrary maps — but the *event plumbing* (S2/S3) is fully general and is what we lift.

### 2.3 SAR can also *find* and *spawn* entities (for the instrumenter family)

- **Find:** `EntityList::GetEntityInfoByClassName` (`EntityList.cpp:41`),
  `EntityList::QuerySelector` (`EntityList.cpp:100`), and the `sar_find_ent` / `sar_find_ents`
  console commands. Walk the list by index over `Offsets::NUM_ENT_ENTRIES`.
- **Spawn:** `server->Create(classname, origin, angles, …)` is used live by
  `ChallengeMode::CreateNode` (`ChallengeMode.cpp:25`); the lower-level
  `CreateEntityByName` / `DispatchSpawn` / `SetKeyValue*` / `KillEntity` are all bound
  (`Server.hpp:29-43`, `Server.cpp:127`).
- **Caveat:** the only *demonstrated* runtime spawn is a `prop_dynamic_override` (a non-solid prop,
  via `GhostEntity`). Spawning a *touchable* `trigger_multiple` is plausible but **unproven** here.
  And `AddOutput` (runtime relay re-wiring) appears **nowhere** in `src/` — its availability in this
  Portal 2 build is unverified.

### 2.4 Field observation — standalone workshop loads fire the exit but **hang the transition** (2026-06-16)

Empirical, from loading a workshop chamber the harness way: `map workshop/928173417862098216/1462057267`.
At the exit the elevator's end sequence **fired** ("it kept triggering something") but **never
completed the transition to the next chamber** — no rating card, no changelevel, the elevator just
sits.

**Why:** PeTI/workshop chambers are *not* self-contained levels — they are nodes in a **playlist /
session**. The exit elevator is wired to transition to the **next map in the session's queue** (the
in-game community-testing / puzzlemaker flow, built on `sp_transition_list` + the `@transition_*`
framework). A raw `map workshop/<file>` load gives you the BSP and nothing else — no session, no
"next map" — so the `changelevel` destination is empty/invalid: the exit logic fires, but the engine
has nowhere to go, the host state never leaves `HS_RUN`, and it stalls. The "End of Playtest" rating
card is likewise a *session*-flow artifact, so a bare load shows neither the card nor the transition.

**Console capture (`sar_show_entinp 1`, condump `000`) — the ONLY non-noise lines at the exit:**
```
14933 InstanceAuto4-departure_elevator-elevator_1_path_1.InPass()
14933 InstanceAuto4-departure_elevator-elevator_1_player_teleport.RunScriptCode(ReadyForTransition())
14973 InstanceAuto4-departure_elevator-elevator_1_path_3.InPass()  + ...RunScriptCode(ReadyForTransition())
15053 ...path_4.InPass()  + ...RunScriptCode(ReadyForTransition())
15091 ...path_5.InPass()  + ...RunScriptCode(ReadyForTransition())
15129 ...path_6.InPass()  + ...RunScriptCode(ReadyForTransition())
```
Decode: `departure_elevator` is the PeTI **exit** elevator prefab — a `func_tracktrain` descending
through `path_track` nodes (`path_N.InPass()`). At each node it calls the VScript **`ReadyForTransition()`**
on its `player_teleport`. In the normal session flow `ReadyForTransition()` finds the next map and
fires the changelevel; **standalone it no-ops, so the elevator keeps descending and re-calling it** —
the exact "kept triggering but never finished." Notably **absent** from the capture:
`portal_stats_controller.OnLevelEnd`, `@relay_pti_level_end.Trigger`, `@transition_from_map.Trigger`,
any `*.ChangeLevel`, and any `HS_CHANGE_LEVEL` host-state change. So on *this* map the level-end relay
hypotheses (S1/S4) **did not fire at all**; the real, observable, challenge-independent exit edge is
the VScript call. (Player noclipped to the exit, so this confirms the *signal*, not the solve-gating.)

**What this means for the approaches (this is a load-bearing data point):**
- **The real PeTI exit signal, confirmed on this map, is `RunScriptCode(ReadyForTransition())` on
  `*departure_elevator*…player_teleport`** — challenge-*independent*, visible on the current bare load,
  no relay-name guessing. **Match rule: `inputName == "RunScriptCode" && param contains
  "ReadyForTransition"`.** This replaces the unverified `@relay_pti_level_end` guess as X2/X7-tier-2's
  match key. The completion **signal fired even though the transition didn't complete** — we only ever
  needed to *observe the input edge* via the always-on `AcceptInput` hook. → **strong evidence for X2.**
- **Semantics + caveat:** `ReadyForTransition()` means "the exit elevator started its departure," i.e.
  **"entered the exit"** (the right oracle definition — it matches the radius hack's intent). The exit
  elevator is normally **solve-gated** (the exit door opens only on completion), so for a real agent
  reaching it ≡ solved; the noclip repro bypassed that. If we ever want a *solve*-vs-*exit* distinction,
  look for a separate "exit door opened" output. **Stability TODO:** confirm `departure_elevator` /
  `ReadyForTransition` are constant across 2–3 more PeTI exports (the `InstanceAuto4-` prefix varies).
- **Evidence *against* X1 for standalone workshop maps:** if the transition never completes,
  `Event::SESSION_END{transition}` may never fire. X1 stays the campaign/Hammer leg; it **cannot** be
  the workshop leg under bare loads. (Upgrades the §2.1 "workshop = weak" for S3 from *unknown* to
  *confirmed weak*.)
- **"Kept triggering" = a stuck retry loop** on the failed changelevel → whatever we latch must be
  **latch-once / debounced** (X7 latches once; this is now a requirement, not a nicety).
- **This is exactly the problem challenge mode is *designed* to solve.** CM makes a single chamber
  self-terminating via the `challenge_mode_end_node` touch — *no next map required*. So forcing CM
  (**X3**) plausibly **fixes the hang AND yields the clean `CM_FLAGS{end}` signal** in one move — a
  real bump to X3's appeal (hypothesis to confirm in §6).
- SAR *already* matches one such input un-gated today: `@transition_from_map.Trigger` (SP) /
  `relay_exit_succeed` (coop) at `Server.cpp:630-642`, behind `sar_transition_timer` — so this is
  directly observable right now (see §6.0).

---

## 3. The approaches

Stable IDs **X1–X9** (X = eXit). Effort: **S** = ≤ a few days · **M** = a week-ish · **L** = weeks+.
Taste / Robustness are 1–5. Coverage is per map family. Each carries its own recon ask; the union
is §6.

### Family A — observe a signal SAR already computes (KISS; "harness stays dumb")

#### X1 · `SESSION_END{transition}` latch — the campaign/Hammer one-liner
- **Idea:** the harness already handles `ON_EVENT(SESSION_END)`; add one
  `if (data.transition) chamberComplete = true;`. That's the campaign/Hammer changelevel signal
  with one line of new logic.
- **Mechanism:** `engine->isLevelTransition` is set by the changelevel detours (`Engine.cpp:376`)
  and carried into `SESSION_END` (`Session.cpp:164`); the harness already subscribes. Reset the
  latch in the existing `SESSION_START` handler.
- **Lives:** harness C++ only. Smallest possible diff of any approach.
- **Coverage:** campaign **strong** · Hammer **medium-strong** (any map ending in changelevel) ·
  workshop **weak** (stay-on-map rating card issues no changelevel → never fires).
- **Effort S · Taste 5 · Robustness 3** — rock-solid where it fires, but fires at session
  *teardown* (a few ticks late) and a multi-room map that internally `changelevel`s between
  sub-rooms is a **false positive** mid-chamber.
- **Failure modes:** in-place rating-card workshop completion → total miss; internal
  sub-transitions → premature "complete"; teardown timing isn't the exact completion tick;
  **standalone workshop exits *attempt* a transition that never completes → the event may not fire at
  all** (confirmed in §2.4).
- **Recon:** does a bare-loaded PeTI map ever hit `HS_CHANGE_LEVEL` / fire `SESSION_END{transition}`,
  or stay `HS_RUN` on the same map? This single observation decides workshop coverage.

#### X2 · ⭐ Match the exit input in a `HarnessExit` AcceptInput callback (now signal-confirmed)
- **Idea:** the exit edge is *challenge-independent at the input layer* — SAR sees it on every bare
  load; only SAR's *handling* was gated. §2.4 confirmed the actual PeTI signal: **`RunScriptCode` with
  param containing `ReadyForTransition`** on the `*departure_elevator*` entity. Latch a bit on that
  (and OR-in `portal_stats_controller.OnLevelEnd` un-gated, `Server.cpp:620`, for transition-style
  maps). No relay-name guessing required.
- **Mechanism:** don't edit the core matcher — piggyback the existing
  **`ReloadedFix::OverrideInput` callback pattern** (`Server.cpp:656`, `ReloadedFix.cpp:23`): a tiny
  `HarnessExit::OnInput(targetname, className, inputName, param)` invoked from `AcceptInput_Hook`,
  matching `inputName=="RunScriptCode" && strstr(param,"ReadyForTransition")` (plus the un-gated
  `OnLevelEnd`), latch-once. Zero new hooks (the chokepoint already runs on every input, `Server.cpp:584`).
- **Lives:** SAR C++ — recognition in a `HarnessExit` feature; one call-out line in `Server.cpp`.
  The truth is a server-thread input edge Python can never see, so it must live here.
- **Coverage:** workshop **strong (signal confirmed on a real bare load, §2.4)** · campaign **partial**
  (campaign exits are changelevel, not `ReadyForTransition`/`OnLevelEnd` — use X1) · Hammer **weak**
  (only if the map uses a PeTI-style elevator or a stats controller).
- **Effort S–M · Taste 4 · Robustness 4** — *upgraded from M/3/3 now that the match key is verified
  rather than guessed.* The `ReadyForTransition` match is self-describing and template-stable; no magic
  per-map name. Latch-once handles the repeat-fire. Still server-side, edge-accurate, position-independent
  — kills the radius false-pos/neg. The one open risk is cross-map string stability (§2.4 TODO).
- **Failure modes:** `ReadyForTransition`/`departure_elevator` naming drifts across PeTI template
  versions → silent dead match (mitigate: also match `OnLevelEnd` and the `path_track.InPass` on
  `departure_elevator`); semantics are "entered exit elevator," not "logically solved" (fine for the
  oracle, see §2.4 caveat); a Hammer map with no PeTI elevator emits nothing here (→ X1/X4); prefer an
  **OR-in harness path**, don't mutate the speedrun timer's gate (regression risk for CM users).
- **Recon:** mostly **done** (§2.4). Remaining: confirm `ReadyForTransition` fires on 2–3 more PeTI
  exports with the same name; confirm it does **not** fire spuriously before the exit; decide whether to
  also match `OnLevelEnd` (does it ever fire bare?). (`sar_show_entinp` reads the *same hook*,
  `Server.cpp:614` — it prints exactly what the matcher sees.)

#### X3 · Force challenge mode + subscribe to `CM_FLAGS` — reuse Valve's own end-node
- **Idea:** don't write a detector; change the **load** so `GetChallengeStatus() == CHALLENGE`,
  which arms Valve's `challenge_mode_end_node` `StartTouch` + the `OnLevelEnd` branch unmodified.
  The harness just does a ~6-line `ON_EVENT(CM_FLAGS)` and latches `event.end`.
- **Mechanism:** S2. `TriggerCMFlag` → `Event::CM_FLAGS` already fires on node touch (`Server.cpp:428`)
  and is already consumed elsewhere (`Categories.cpp:239`). Note SAR *already knows how to force the
  cvar past the engine's reset*: `ApplyGameSettings` resets `sv_bonus_challenge`, and SAR's detour
  re-sets it true (`Server.cpp:737-741`) — so the "fight the reset" problem is half-solved.
- **Lives:** load incantation in `autoexec.cfg` / `game_launcher.py`; ~6 lines of harness C++. The
  detector itself is untouched upstream SAR.
- **Coverage:** workshop/CM **excellent** (this is exactly what `CM_FLAGS` is for; how SAR's CM timer
  stops) · campaign **weak** (forcing CM on a campaign map is semantically wrong) · Hammer **weak**
  (node-less maps gain nothing).
- **Effort S–M · Taste 4–5 · Robustness 5 (in-domain) / 2 (cross-map)** — the most elegant answer
  *for PeTI*: arm existing battle-tested code instead of writing new code. The ding: forcing the run
  MODE drags in CM HUD / leaderboard popup / scoring behavior that may **perturb the agent's
  observation distribution** — a confound the eval must sign off on.
- **Failure modes:** bare-loaded PeTI maps may not *contain/spawn* `challenge_mode_end_node` outside
  the genuine challenge path → hook silently never binds; `GetChallengeStatus()` could return
  `WRONG_WARP` if `sv_bonus_challenge` is set but `m_iBonusChallenge` isn't networked under the
  headless load; `InitCMFlagHook` binds via an entity scan once per `SESSION_START` → must re-arm
  every Reset; coop `end==true` needs both slots (`Server.cpp:445`) — fine for SP, document it.
- **Recon:** load a workshop map bare vs `sv_bonus_challenge 1; map X`; `sar_find_ents
  challenge_mode_end_node` to see if the node exists/armed; temp `console->Print` in `TriggerCMFlag`
  and walk the exit; confirm CM mode doesn't change the agent-visible env.

### Family B — instrument the map ourselves ("can we add our own entities?")

> This is the user's explicit ask. Honest finding: "instrument" is most elegant when it means
> *observing the existing level-end I/O* (X2 — zero new entities) and heaviest when it means
> *spawning/rewiring* entities the infra may not cleanly support. Ranked accordingly.

#### X4 · Spawn a SAR-owned `trigger_multiple` at the auto-discovered exit
- **Idea:** on `SESSION_START`, walk the entity list to find the exit anchor
  (`challenge_mode_end_node` / `trigger_changelevel` / elevator landmark), then `CreateEntityByName`
  a `trigger_multiple` there whose `StartTouch` SAR watches. A self-firing geometric completion
  volume that works on bare non-CM loads, with **no per-map targetname string**.
- **Mechanism:** `QuerySelector`/`GetEntityInfoByClassName` for the anchor →
  `CreateEntityByName`+`SetKeyValue*`+`DispatchSpawn` (`Server.hpp:29-43`) → VMT-hook its `StartTouch`
  (same pattern as `InitCMFlagHook`, `Server.cpp:679`) → `KillEntity` on reset.
- **Lives:** SAR C++ (`HarnessExit` feature; spawn-on-`SESSION_START` + kill-on-reset lifecycle),
  right next to `ChallengeMode.cpp` which already spawns a node at coords.
- **Coverage:** workshop **the main payoff** (exit signal even when CM never arms, *as long as some
  anchor exists to derive coords from*) · Hammer **medium** (needs a `trigger_changelevel`/elevator
  landmark) · campaign **works but redundant** with X1.
- **Effort M · Taste 3 · Robustness 3** — real, self-contained, with precedent — but reintroduces
  liabilities X2 avoids: live entity lifecycle, cleanup on *every* RL reset (slot exhaustion if
  `KillEntity` isn't perfectly paired), a default box-size magic number, and a spawned object that
  is now part of the sim (trigger sounds / prediction could perturb the eval).
- **Failure modes:** anchor may not exist under a bare load (`challenge_mode_end_node` is CM-gated;
  `trigger_changelevel` may be absent/mislocated); point-entity anchor gives no bounds → magic box
  size; player **faded/teleported past** the volume on the elevator ride → missed `StartTouch`;
  leaked entities across many resets.
- **Recon:** does a touchable `trigger_multiple` actually spawn+fire via `CreateEntityByName`+
  `DispatchSpawn` in this build (only `prop_dynamic_override` is proven)? Does the player physically
  enter the volume or does the fade skip it? Does `KillEntity` free the slot cleanly across resets?

#### X5 · `sar_on_load` console `ent_fire … AddOutput` re-wire (config-driven; also the recon probe)
- **Idea:** drive instrumentation entirely from `autoexec`'s `sar_on_load` + the harness
  `ExecuteCommand` RPC — `ent_create` a watcher and/or `ent_fire <relay> AddOutput` so the level-end
  relay *also* fires a SAR-recognizable output. Zero new C++.
- **Lives:** `autoexec.cfg` / Python; recognition reuses X2's branch (matching a **SAR-chosen** name,
  which is stable because *we* pick it).
- **Coverage:** works only where you know the per-map relay targetname to `AddOutput` onto — same
  unverified-name problem as X2/X4, **worse** because it's baked into a cfg per map family.
- **Effort S (code) / XL (robust per-map table) · Taste 2 · Robustness 2** — string-templated console
  commands editing a live map's I/O graph are fragile and opaque (command-buffer timing, per-map
  names, and `AddOutput` is **unverified** — 0 refs in `src/`). **High value as a throwaway recon
  instrument, low value as a production oracle.**
- **Recon:** does `ent_fire <relay> AddOutput "OnTrigger …"` actually re-wire a `logic_relay` at
  runtime in this build? Does `sar_on_load` run *after* map entities spawn? This is a clean go/no-go
  for the whole rewire flavor — run it during the §6 session.

#### X6 · Offline `.bsp` entity-lump injection (static preprocessing sibling)
- **Idea:** before the harness ever loads a map, an offline pass injects a SAR-recognizable
  `logic_auto`/output into the map's entity lump so completion self-announces. Runtime stays trivial
  (reuses X2's recognition branch); fully deterministic per map.
- **Lives:** a standalone Python tool over the map corpus → patched `.bsp` copies the launcher loads.
  The most faithful "harness barely does anything."
- **Coverage:** **all three** map families *if* the offline pass can locate an exit anchor (and a
  human can fix the rare miss, since it's offline). Strongest worst-case coverage of any approach.
- **Effort L · Taste 3 · Robustness 4** — once patched, detection is rock-solid and deterministic
  (no gating, no timing, no live spawn). But the BSP rewriter is a chunky new tool (lump-0 text +
  offset fixups + possible LZMA-compressed lumps), and it **forks the map files** (ship+maintain
  patched copies; workshop maps are downloaded by ID → must intercept the download path).
- **Recon:** are target workshop maps uncompressed in lump 0 or LZMA? Does Portal 2 load a `.bsp`
  whose entity lump was rewritten out-of-tool (CRC/signing)? Is an injected output preserved through
  load and visible to the hook?

### Family C — compose for robustness

#### X7 · ⭐ The Layered Exit Oracle (priority ladder) — the recommended end-state
- **Idea:** one `HarnessExit` feature latches `chamber_complete` from whichever signal fires first,
  in a fixed priority order; first to fire wins; read out once in `InternalObserve`:
  1. `Event::CM_FLAGS{end}` (X3, only if challenge mode is forced) —
  2. un-gated `AcceptInput` match: `RunScriptCode(ReadyForTransition())` on `*departure_elevator*`
     (the confirmed PeTI exit edge, §2.4) / `portal_stats_controller.OnLevelEnd` /
     `trigger_changelevel.ChangeLevel` (X2) —
  3. `SESSION_END{transition}` (X1) —
  4. geometric exit-zone fallback (the discovered anchor's AABB; the principled successor to the
     radius hack, and only as a last resort).
- **Lives:** SAR C++, a new `HarnessExit` feature (added via `AddFeature` in `SAR.cpp` like `Harness`
  itself), so `Observe`/`AgentLoop`/`Reset`/`RenderDemo` all inherit the bit through `InternalObserve`.
- **Coverage:** all three families with **graceful degradation** — if the precise signal is absent the
  bit still latches from a coarser tier instead of silently failing.
- **Effort M · Taste 4 · Robustness 4** — one feature, one latched bit, one read-out; tiers 1–3 are
  pure subscriptions to things that already fire; each tier is ~5 lines. The ding: a 4-tier ladder is
  more machinery than a single signal, and tier 4 reintroduces the magic-radius smell (so keep it
  truly last-resort and *log* when it's the one that fired).
- **Failure modes:** if *both* a precise signal and a geometric anchor are absent (node-less Hammer,
  wrong relay name) the bit never latches → episode times out as BUDGET (honest, not a success);
  latch must reset on every Reset or "complete" bleeds into the next episode; multi-room internal
  transitions → prefer tier-1 CM flag (fires only at the true exit) over tier-2 `trigger_changelevel`.

#### X8 · Self-verifying ensemble + oracle-disagreement telemetry (catalog A10)
- **Idea:** X7 + provenance. Run all signals in parallel, latch on the highest-confidence one, but
  *also* surface which fired and whether they agreed: `bool chamber_complete`, `int32 exit_signal_mask`,
  `bool oracle_disagreement`. Turns "the oracle might be wrong" from silent corruption into a
  **visible, filterable** benchmark-integrity metric.
- **Effort M–L · Taste 4 · Robustness 5** — the most trustworthy framing for a *measurement
  instrument* (the credibility of the success bit IS the product). For a pure RL reward it's
  gold-plating (taste 3). Cost is mostly plumbing the extra fields through proto → rollout →
  trajectory and teaching the eval to record provenance.
- **Failure modes:** the K-tick agreement window is a new magic constant to tune; disagreement can't
  help when *no* signal exists; more proto fields = more `make proto` sync surface.
- **Note:** this is the natural v1.1 once X7 is trusted — it answers the skeptic's "how do you know
  the oracle is right?" with data, and it directly feeds catalog A10 (oracle-disagreement meta-eval).

#### X9 · Wildcard — rating-card / leaderboard-open UI witness
- **Idea:** read the game's own "you finished" UI. The CM rating/leaderboard card that opens at
  run-end (`Client::openleaderboard` `+leaderboard 4` / the `cmboard` net message, `g_leaderboardOpen`,
  `Client.cpp:343/454`) is a player-facing, hard-to-fake completion witness orthogonal to every
  entity-I/O signal — use it as a *corroboration* tier in X8's ensemble.
- **Effort S (in-engine, reuse the detour) / L (pixel-OCR variant) · Taste 3 · Robustness 3** —
  high-confidence *when* it fires and truly independent (real ensemble diversity), but narrow
  (PeTI-only), CHALLENGE-gated (needs X3), suppressible (`sar_disable_challenge_stats_hud`), and
  *late* (post-fade) so it's a corroborator, never the completion timestamp. The pixel-OCR fallback
  (read the SHM framebuffer) is exactly the brittle, magic-heavy code to avoid unless the in-engine
  signal proves suppressed.
- **Use:** ensemble diversity member only — "if the rating card opened, completion is near-certain"
  catches the case where an entity heuristic fired spuriously.

### Orthogonal bonus — the spatial `exit_pos` percept (from the first draft's Strategy 1)
Independent of *when*-complete, the *where*-is-the-exit anchor found in §2.3 should be surfaced as
`exit_pos` in `GameState`. It (a) feeds X7's geometric tier 4, (b) gives the LLM/RL agent a dynamic
exit target with no `--exit` flag, and (c) is the cross-check coordinate for X8's disagreement test.
Cheap, reusable, and it deletes the *other* half of the hand-passed config.

---

## 4. Comparison at a glance

| ID | Approach | Effort | Taste | Robust | PeTI | Hammer | Campaign | Bare-load-ready? |
|----|----------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| X1 | `SESSION_END{transition}` latch | S | 5 | 3 | ✗ | ◑ | ✓ | yes |
| X2 | Un-gate `OnLevelEnd` + relay match | M | 3 | 3 | ◑? | ✗ | ◑ | yes |
| X3 | Force CM + `CM_FLAGS{end}` | S–M | 4 | 5/2 | ✓ | ✗ | ✗ | needs load change |
| X4 | Spawn `trigger_multiple` at exit | M | 3 | 3 | ◑ | ◑ | ✓(redundant) | yes |
| X5 | `sar_on_load` `AddOutput` rewire | S/XL | 2 | 2 | ◑? | ◑? | ✓ | yes (if AddOutput works) |
| X6 | Offline `.bsp` lump injection | L | 3 | 4 | ✓ | ✓ | ✓ | yes (offline) |
| **X7** | **Layered oracle (ladder)** | **M** | **4** | **4** | **✓** | **✓** | **✓** | **yes** |
| X8 | Ensemble + disagreement telemetry | M–L | 4 | 5 | ✓ | ✓ | ✓ | yes |
| X9 | Rating-card UI witness (corroborator) | S/L | 3 | 3 | ◑ | ✗ | ✗ | needs X3 |

✓ strong · ◑ partial · ✗ none/weak · `?` unverified domain knowledge (depends on §6 recon).

---

## 5. Recommended path

1. **Ship shared plumbing (§7) first** — proto `bool chamber_complete = 8`, read in `InternalObserve`,
   `make proto`, and a `HarnessExit` feature scaffold. Everything else slots into this.
2. **Run the §6 recon session.** It decides X2-vs-X3 for the PeTI case and whether X4 is even needed.
   *Do not write a single string-match before this.*
3. **Land X1 + X2** (the un-gate, OR-in, no core mutation) → the campaign/Hammer + workshop halves.
4. **Wrap them in X7** (the ladder) so the bit is one OR with graceful degradation; add the geometric
   tier 4 from the `exit_pos` anchor as the principled successor to the radius hack.
5. **Add X3 as tier 1** *iff* recon shows bare loads don't fire `OnLevelEnd` (i.e. workshop needs the
   forced-CM node) **and** the eval signs off on CM-mode side effects.
6. **Defer X4/X6** to the case where pure observation can't see a real workshop exit; **X5** is a recon
   tool, not a product; **X8/X9** are the v1.1 "trust the oracle" layer (feeds catalog A10).
7. **Delete the radius oracle** (`rl_challenge_env.py:146`, `testchamber_session.reached_exit`,
   `macro_repl.py --exit/--radius`) once X7 passes the smoke test.

---

## 6. The recon protocol — the ONE session that unblocks everything

Goal: observe what *actually* fires at the exit of (a) a bare-loaded PeTI workshop chamber and (b) a
campaign map, the **same way the harness loads them**. `sar_show_entinp` reads from the exact hook
the matcher would use (`Server.cpp:614`), so its console output *is* the matcher's input.

### Setup (per the autoexec; `sv_cheats` is already forced on by the harness)
```
sv_cheats 1
developer 1
sar_show_entinp 1     // print every entity input (the AcceptInput chokepoint)
sar_tick_debug 2      // host-state transitions (HS_RUN vs HS_CHANGE_LEVEL_*)
sar_transition_timer 1 // SAR already matches @transition_from_map.Trigger / relay_exit_succeed
```

### 6.0 Reproduce the hang first (DONE — map 1, §2.4)
Repro: `map workshop/928173417862098216/1462057267` — exit fires `RunScriptCode(ReadyForTransition())`
on `*departure_elevator*` repeatedly, transition never completes, host stays `HS_RUN`. ✅ captured.

### 6.1 ⭐ Next-session checklist (run this tomorrow — workshop stability + story mode)

> Goal: (a) prove the workshop match key `ReadyForTransition` is **stable across maps**, not one-map
> luck, and (b) pin down the **story-mode** exit signal so X1/X2 cover campaign too. ~20 min.

**Clean setup (kills the GameFrame spam that drowned condump000):**
```
sv_cheats 1
sar_tick_debug 0          // ← the fix: no more CServerGameDLL::GameFrame / NET_Tick noise
sar_show_entinp 1         // every entity input — the literal AcceptInput chokepoint (Server.cpp:614)
developer 1
```

**Part A — workshop stability (2–3 *different*, ideally small, PeTI maps):**
1. `map workshop/<id>` → `noclip` → fly to the exit elevator, step in / pass through it.
2. In console, read the input line(s) at the elevator. Record: does
   `…departure_elevator…RunScriptCode(ReadyForTransition())` appear, with **only** the `InstanceAuto<N>-`
   prefix differing? Anything *else* fire (an `OnLevelEnd`? a relay)?
3. `condump` after each. → If `ReadyForTransition` shows on all of them, the X2 match key is **locked**.

**Part B — story / campaign mode (1–2 maps, e.g. `sp_a2_intro`, `sp_a1_wakeup`):**
1. `map sp_a2_intro` → `noclip` → fly to the chamber exit (campaign maps end in a `trigger_changelevel`,
   not the PeTI elevator).
2. Walk into the exit trigger and record **what fires at the boundary**:
   - an entity input? — `@transition_from_map.Trigger` (SP)? `portal_stats_controller.OnLevelEnd`?
     `trigger_changelevel.ChangeLevel`? *(turn on `sar_transition_timer 1` — SAR already matches the
     first two, Server.cpp:630-642.)*
   - does the screen actually **changelevel** to the next map (host state leaves `HS_RUN`)? → if yes,
     `SESSION_END{transition}` fires = **X1 confirmed for campaign**.
3. This tells us whether campaign is covered by X1 (transition) alone, or also wants an X2 input match
   (e.g. `@transition_from_map.Trigger`) for an earlier/tighter completion tick.

**Recon results log (fill in tomorrow):**

| map | type | input(s) at exit (entName.inputName(param)) | fired repeatedly? | host leaves HS_RUN? | → conclusion |
|---|---|---|---|---|---|
| `…1462057267` | PeTI | `*departure_elevator*.RunScriptCode(ReadyForTransition())` | yes | no | X2 key = ReadyForTransition |
| (workshop 2) | PeTI | | | | |
| (workshop 3) | PeTI | | | | |
| `sp_a2_intro` | campaign | | | | |
| (campaign 2) | campaign | | | | |

When this table is filled, X2's match key is locked, campaign coverage is decided, and the next step
is pure code (the `HarnessExit` feature + §7 plumbing). The detailed per-type steps below (A/B/C) are
the long-form version of this checklist.

### A. Bare PeTI / workshop map (the decisive test)
1. Load a representative workshop chamber **bare** (`map <id>`), the harness way — *not* via the
   challenge menu.
2. `sar_find_ents challenge_mode_end_node` and `sar_find_ents portal_stats_controller` — **does the
   node exist under a bare load?** (decides X3 viability) Does the stats controller exist? (X2)
3. Play to the elevator and read the console **at the exact fade** *(mostly answered on map 1, §2.4 —
   now repeat on 2–3 more PeTI exports to prove the match key is stable, not one-map luck)*:
   - Does `RunScriptCode(ReadyForTransition())` fire on `*departure_elevator*`, with the **same**
     `departure_elevator`/`ReadyForTransition` names (only the `InstanceAuto<N>-` prefix varying)? →
     confirms X2's match key generalizes.
   - Does `portal_stats_controller.OnLevelEnd` *ever* fire on a bare load? (it didn't on map 1) → if
     yes anywhere, OR-it-in as a second match key.
   - Does host state ever leave `HS_RUN`? (it didn't on map 1 → X1 dead for standalone workshop.)
4. Re-load `sv_bonus_challenge 1; map <id>`; confirm `GetChallengeStatus()==CHALLENGE` (not
   `WRONG_WARP`) and that touching the exit fires `CM_FLAGS{end}` (temp `console->Print` in
   `TriggerCMFlag`, `Server.cpp:428`). → decides X3.
5. *(Optional, X5 go/no-go)* try `ent_fire <relay> AddOutput "OnTrigger _sar_exit:Trigger"` and see
   if it re-wires at runtime in this build.

### B. Campaign map (confirm the un-gated legs)
1. `map sp_a2_intro` (or similar). `sar_find_ents trigger_changelevel`.
2. Walk into the exit trigger; confirm `SESSION_END{transition=true}` fires (X1) and that
   `trigger_changelevel.ChangeLevel` shows in `sar_show_entinp` a few ticks earlier (tighter tier for
   X7). Verify un-gated `OnLevelEnd` does **not** misfire mid-chamber.

### C. Re-arm across Reset (every approach depends on this)
Confirm `SESSION_START` re-fires on the harness `restart_level`/`map` Reset path
(`Portal2HarnessImpl.cpp:514`) so the latch clears and `InitCMFlagHook`/anchor-scan re-bind every
episode. A latch that never resets bleeds "complete" into the next episode; a hook that binds once
dies after episode 1.

### Decision tree the recon resolves
- **`OnLevelEnd` fires bare** → X2 un-gate is the workshop spine; X3 unnecessary. *(best)*
- **Only fires in CM** → X3 (forced CM) is the workshop tier; verify no eval-perturbing side effects.
- **Neither, but node/anchor exists** → X4 (spawn a trigger at the anchor).
- **No signal and no anchor** → that map needs manual annotation / `exit_pos`; X6 if it's a whole corpus.

---

## 7. Shared plumbing (every approach needs this)

- **proto:** add `bool chamber_complete = 8;` to `GameState` (`harness.proto:129`, next free tag).
  *(X8 later adds `int32 exit_signal_mask = 9; bool oracle_disagreement = 10;` and `exit_pos`.)*
  Run `make proto` — regenerates **both** the C++ (`*.pb.cpp`) and Python (`harness_pb2*.py`) stubs;
  they must stay in sync (CLAUDE.md).
- **read-out:** latch an `std::atomic<bool>` on the `HarnessExit`/`Harness` object; read it in
  `Portal2HarnessImpl::InternalObserve` (`Portal2HarnessImpl.cpp:208`), mirroring how
  `EntityState.mark` is threaded out server-side. Single assembly point → every RPC inherits it.
- **reset:** clear the latch in the harness `SESSION_START` handler so episodes don't bleed.
- **Python:** replace `_check_terminated` (`rl_challenge_env.py:146`) and `reached_exit`
  (`testchamber_session.py:44`) with a read of `state.chamber_complete`; the `+10000` terminal
  reward (`rl_challenge_env.py:256`) now keys off the real bit.
- **smoke test (PR gate, CLAUDE.md):** extend `py/agentloop_smoke.py` to assert
  `state.chamber_complete` flips at a known completion — this changes the gRPC surface, so the gate
  applies. The `mark`↔label-style "did it fire at the *right* tick" check stays a documented manual
  visual check (the §6 session doubles as it).

---

## 8. Open questions / parking lot

- ~~**Is `@relay_pti_level_end` real?**~~ **RESOLVED (§2.4):** it did **not** fire on the test map.
  The real PeTI exit edge is `RunScriptCode(ReadyForTransition())` on `*departure_elevator*`, observed
  on a bare load. Remaining sub-question: is that name **stable across PeTI template versions / maps**?
  Confirm on 2–3 more exports before hardcoding the match (mitigation: also match `path_track.InPass`
  on `departure_elevator` and `OnLevelEnd`).
- **Should the harness load workshop maps via a session-aware path at all?** §2.4 shows a bare
  `map workshop/<file>` load leaves the chamber unable to *complete* its exit (no playlist / next map).
  Detecting the fired signal (X2) sidesteps that for the oracle — but if the hung elevator/retry loop
  perturbs the agent's observation near the exit, we may want to (a) force CM so the chamber
  self-terminates (X3), or (b) find the proper workshop/community-test load incantation that supplies a
  session. Open: does the hang change the percept the agent is scored on in the final ticks?
- **Does forcing CM perturb the eval?** CM HUD, leaderboard popup, possible cube/turret/scoring
  deltas. If yes, X3 is off the table as a *default* and X2/X4 carry workshop.
- **Multi-room workshop maps** that internally `changelevel` between sub-rooms: every transition-based
  leg (X1, X7 tier 2/3) false-positives mid-chamber. Need a "final exit vs internal" discriminator —
  `hoststate->m_levelName` destination check, or prefer the CM end-flag (fires only at true exit).
- **Headless networking:** does the harness's no-real-player load network `m_iBonusChallenge` so
  `GetChallengeStatus()` returns `CHALLENGE` and not `WRONG_WARP`? (X3 hinges on this.)
- **Touchable `trigger_multiple` spawn** is unproven in this build (only `prop_dynamic_override` is) —
  X4 go/no-go.
- **`AddOutput` runtime availability** is unverified (0 refs in `src/`) — X5 go/no-go.
- **Coop** semantics (`end==true` needs both slots, `Server.cpp:445`): RL is SP slot-0; document and
  ignore, but don't let a coop test mislead the recon.
- **Non-Portal-2 titles** (Aperture Tag / INFRA / mods) use different host-state enums
  (`Session.cpp:227-241`) — out of v0 scope, note it before generalizing.
```
