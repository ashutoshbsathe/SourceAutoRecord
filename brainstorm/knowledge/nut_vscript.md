# Squirrel VScript (`.nut`) in Source / Portal 2 — reference

What this documents, and why it lives in our recon knowledge base: VScript is the one place
where the **entity I/O causal graph we extract statically from a `.bsp` can be wrong by
omission**. Our offline recon tool ([`py/bsp_recon/dump_ents.py`](../../py/bsp_recon/dump_ents.py))
reads a map's entities and their baked output wiring (`button.OnPressed -> door.Open`) directly
from the BSP entity lump. That wiring is a static, plaintext fact — *unless* a Squirrel script
rewires it while the game runs. A running script can add, remove, or re-target connections that
were never in the file. Those edits are invisible to any file parse. So VScript is the hard ceiling
on causal-graph completeness, and the reason `dump_ents.py` emits a **VScript taint flag** per map.

> **srctools has no Squirrel parser. None.** This document is *not* describing a srctools file
> format the way [`bsp_format.md`](bsp_format.md) or [`vpk_format.md`](vpk_format.md) do. It
> describes an **engine-runtime behavior** — how the Source VM and the entities that drive it use
> `.nut` files at play time — documented from how the engine and FGD treat them, plus exactly the
> two things srctools *does* expose: that `.nut` is a packable file type, and that the `vscripts`
> keyvalue exists. Everything about *what a script does* is runtime, opaque to us, and stated here
> from Source/VScript engine semantics, not from a parsed grammar.

Sibling docs: [`bsp_format.md`](bsp_format.md) (where the entity lump and pakfile live),
[`vpk_format.md`](vpk_format.md) (where the *stock* `.nut` files actually ship),
[`vmt_format.md`](vmt_format.md), [`vtf_mdl.md`](vtf_mdl.md).

---

## 1. What Squirrel / VScript is

**Squirrel** is a small dynamically-typed scripting language (C-like syntax, garbage-collected,
classes + closures). **VScript** is the Source-engine subsystem that embeds a script VM and binds
the engine's C++ entity API into it. In Portal 2 the VM is Squirrel; the script files have the
`.nut` extension. (Source also supports Lua/Gamemonkey VMs in other titles, but Portal 2 ships
Squirrel only.)

VScript is an **interpreter running inside the game process**, on the same tick clock as everything
else. A script is *attached to an entity* (or to the map globally), gets a **script scope** (a
table of its variables and functions), and can be driven by:

- a per-tick `think` function the engine calls on the owning entity,
- entity **inputs** fired at the script (`RunScriptCode`, `CallScriptFunction`, `RunScriptFile`),
- the script calling back **into the engine** to spawn, query, move, or **re-wire** other entities.

That last capability — re-wiring the I/O graph at runtime — is the whole reason this doc exists.

```
  Source engine (game process, one tick clock)
  ┌──────────────────────────────────────────────────────────────────────┐
  │   C++ entity system  ── exposes API ──►  VScript VM (Squirrel)         │
  │   (positions, I/O,                        ┌──────────────────────────┐ │
  │    think calls)        ◄── calls back ──  │ script scope per entity  │ │
  │                                           │  vars + functions        │ │
  │                                           │  loaded from a .nut file │ │
  │                                           └──────────────────────────┘ │
  └──────────────────────────────────────────────────────────────────────┘
```

---

## 2. How a `.nut` attaches to the map

Two static hooks put a script into the world. Both are plain entity keyvalues / classnames in the
BSP entity lump, so a file parse *can* see that a script is present — it just can't see what the
script does.

### 2a. The `vscripts` keyvalue (any entity)

Almost every entity class in Portal 2 accepts a `vscripts` keyvalue: a space-separated list of
`.nut` paths (relative to `scripts/vscripts/`). At spawn the engine compiles each listed file into
that entity's script scope. The FGD models this as two value types — srctools defines them in
`fgd.py`:

```
fgd.ValueTypes.STR_VSCRIPT        = 'scriptlist'   # the `vscripts` keyvalue: a LIST of .nut paths
fgd.ValueTypes.STR_VSCRIPT_SINGLE = 'script'       # a single .nut path (e.g. some inputs)
```

```
  entity { classname "<anything>"
           targetname "thing"
           vscripts   "glados sp_transition_list"   <- two .nut files, space-separated
           thinkfunction "Think"                    <- optional: name of a fn in the script scope
           ...
         }
        │
        └─ engine at spawn: compile scripts/vscripts/glados.nut
                                   + scripts/vscripts/sp_transition_list.nut
                                   into this entity's script scope;
                            if `thinkfunction` set, call scope.Think() every tick
```

### 2b. `logic_script` / `point_script` (a dedicated script-host entity)

`logic_script` is a point entity whose entire purpose is to host a script — it has `vscripts` plus
a set of `Group00..GroupNN` keyvalues naming other entities the script gets handles to. PeTI and
stock maps use it as the "global glue" script holder. `point_script` is the older variant.

```
  entity { classname  "logic_script"
           targetname "@glados_script"
           vscripts   "glados"        <- scripts/vscripts/glados.nut
           Group00    "@some_relay"   <- entity handles handed to the script scope
           Group01    "..." }
```

**What `dump_ents.py` keys on (the only static signal).** Our `vscript_taint()` flags an entity if
*either* it carries a non-empty `vscripts` keyvalue *or* its classname is `logic_script` /
`point_script`:

```python
# py/bsp_recon/dump_ents.py — vscript_taint()
scripts = ent['vscripts']
cls = ent['classname']
if scripts or cls in ('logic_script', 'point_script'):
    ent_hits.append({'classname': cls, 'targetname': ent['targetname'], 'vscripts': scripts})
```

It then also lists every `.nut` embedded in the map's pakfile (see §6). The map-level record's
`vscript` field is `{'entities': [...], 'nut_files': [...]}`, and the corpus index marks a map
`vscript: True` if *either* list is non-empty. That boolean is the **low-confidence flag** for the
causal graph: any map with it set may have runtime wiring we cannot see.

---

## 3. The inputs that drive a script

A script scope is dormant until something invokes it. The engine fires this either from a `think`
function (set via `thinkfunction`) or from **entity I/O inputs** aimed at the script-hosting entity.
The three load-bearing inputs (these are engine inputs, available on entities with a script scope):

```
  Input                Payload (the I/O "params" field)        Effect
  ───────────────────  ──────────────────────────────────────  ─────────────────────────────────
  RunScriptCode        a literal Squirrel expression string     compile + eval it NOW in the scope
  CallScriptFunction   the name of a function in the scope      call scope.<name>()
  RunScriptFile        a .nut path (under scripts/vscripts/)    compile + run that whole file
```

Crucially, these inputs are themselves **ordinary entity-I/O connections** — they show up in the
output tuple `(output, target, input, params, delay, times)` that `dump_ents.py` extracts. So a
parse *can* see "entity X fires `RunScriptCode` at script Y with params Z." What it cannot see is
what code `Z` actually does, and — if `input == RunScriptFile` — the parse can't follow into the
referenced `.nut` at all. The moment the chain enters a script, the static trace stops.

```
  STATIC (visible to dump_ents)         RUNTIME (opaque)
  ───────────────────────────────       ─────────────────────────────────────────────
  logic_auto.OnMapSpawn ──────────────► @script.RunScriptCode "EntFire(\"door\",\"Open\")"
     output  target  input  params                │
     (all six fields captured)                    └─ what this string does is INVISIBLE.
                                                      The parse sees the input fires; it
                                                      cannot know it opens "door".
```

---

## 4. How a script mutates the I/O graph at runtime — and why that breaks a static parse

A baked BSP entity output is a **static edge**: it was written into the entity lump at compile time
and never changes. The VScript engine API lets a running script create edges that were *never* in
the file, target entities by name computed at runtime, or fire inputs directly. The three primitives
that matter for causal-graph completeness:

```
  Squirrel call (engine-bound)                 What it does to the graph
  ───────────────────────────────────────────  ──────────────────────────────────────────────────
  EntFire("door","Open", param, delay)          Fire an input at an entity NOW. A one-shot edge
                                                 that exists only for this call — never in the file.

  ent.ConnectOutput("OnTrigger","Callback")      Add a NEW persistent output connection from `ent`
                                                 to a script function. A real new graph edge,
                                                 created at runtime.

  EntFire("relay","AddOutput",                   AddOutput is the input-level form: it appends a
          "OnTrigger door:Open::0:-1")            brand-new output line to a live entity. The
                                                 connection did not exist in the compiled BSP.
```

Because the *target* string can be built at runtime (string concat, a loop over `Group0N` handles,
a `math_counter` read), even the *endpoints* of these edges can be undecidable statically.

### The boundary, drawn explicitly

```
  ┌─────────────────────────────────────────────────────────────────────────────────────┐
  │                       THE STATIC-ANALYSIS CEILING                                     │
  │                                                                                       │
  │  WHAT WE CAN RECOVER (file parse, dump_ents.py)   │  WHAT WE CANNOT RECOVER (runtime) │
  │  ─────────────────────────────────────────────   │  ──────────────────────────────  │
  │  • every baked entity output edge                 │  • edges added by ConnectOutput  │
  │    (output,target,input,params,delay,times)       │  • edges added by AddOutput      │
  │  • that a `vscripts` keyvalue / logic_script      │  • inputs fired by EntFire       │
  │    is present (the taint flag)                    │  • any target computed at runtime│
  │  • which .nut files ship in the pakfile           │  • the BODY of any .nut (no      │
  │  • that an output fires RunScriptCode/            │    Squirrel parser exists)       │
  │    CallScriptFunction/RunScriptFile (the input)   │  • what RunScriptCode's string,  │
  │                                                   │    or RunScriptFile's file, does │
  └─────────────────────────────────────────────────────────────────────────────────────┘
```

This is exactly the over-approximation decision in
[`offline_map_preprocessing.md`](../offline_map_preprocessing.md) (fork #4): the static graph is a
sound *superset* of guaranteed edges only when **no script touches the wiring**; once a script can
`AddOutput`/`EntFire`, the static graph may be *missing* edges (an under-approximation), which is
strictly worse for a planner. Hence the taint flag — it tells the consumer "this graph may be
incomplete," and that is the only honest mitigation available without a Squirrel interpreter.

### Static graph vs. realized graph (the picture)

```
  STATIC graph (what dump_ents emits)            REALIZED graph at runtime (truth)
  ───────────────────────────────────            ─────────────────────────────────────────────
        button7                                        button7
          │ OnPressed                                    │ OnPressed
          ▼                                              ▼
    func_instance_io_proxy                         func_instance_io_proxy
          │ (relay)                                      │ (relay)
          ▼                                              ▼
      math_counter.Add                              math_counter.Add
          │ OnHitMax                                     │ OnHitMax
          ▼                                              ▼
     logic_branch.OnTrue                            logic_branch.OnTrue
          │                                              │
          ▼                                              ▼
   prop_testchamber_door.Open                     prop_testchamber_door.Open
                                                         ╎
   (a script-added edge is simply absent)          @glados_script ┄┄► (Open a 2nd door)
                                                         ▲   added at runtime via
                                                         ╎   EntFire / AddOutput —
                                                         ╎   NEVER in the BSP file
```

The solid chain on the left is the real one we traced and verified (`button7 -> ... ->
prop_testchamber_door.Open`). The dotted edge on the right is the class of thing a script can add
that we will never see in a parse.

---

## 5. Why this *doesn't* sink us for stock PeTI chambers

The reassuring half: in **stock Puzzle Maker (PeTI) and stock single-player** maps, the `.nut`
scripts that ship are **framing/atmosphere glue, not puzzle wiring**. The recurring stock files:

```
  scripts/vscripts/
    sp_transition_list.nut   chapter/level transition bookkeeping
    glados.nut               GLaDOS voice-line / dialogue sequencing
    voting_dialog.nut        co-op map voting UI
    video_splitter.nut       co-op split-screen video logic
    sp_elevator_motifs.nut   elevator music motif selection
```

None of these touch the **button -> relay -> door** puzzle logic — the puzzle wiring in a PeTI
chamber is all baked entity I/O (the kind we trace cleanly). So a stock chamber can carry a
`vscripts` keyvalue and still have a **complete** static causal graph. The taint flag will be set,
but for these files the wiring is recoverable; the flag is a *conservative* "needs a human glance,"
not a hard "graph is broken."

**Where these stock scripts actually live (and why the pakfile scan is not the whole story).** Stock
`.nut` files ship in the **game VPK archives** (`portal2/pak01_dir.vpk` + its numbered archives, and
the dlc VPKs), **not** in the map's embedded pakfile. See [`vpk_format.md`](vpk_format.md). A map
that merely *references* `glados.nut` via a `vscripts` keyvalue will therefore show **zero `.nut`
files in its pakfile** — the script is resolved from the game VPK at load time. Conversely, a custom
map that bundles its own script *will* embed the `.nut` in its pakfile.

```
  A stock PeTI chamber:                       A custom/BEEmod chamber with bundled script:
  ──────────────────────                      ─────────────────────────────────────────────
  entity vscripts "glados"                    entity vscripts "my_puzzle_logic"
        │ referenced                                 │ referenced
        ▼                                            ▼
  game VPK: pak01_dir.vpk                      map BSP pakfile (embedded ZIP):
    scripts/vscripts/glados.nut                  scripts/vscripts/my_puzzle_logic.nut
        │                                            │
   bsp.pakfile.namelist() => []                 bsp.pakfile.namelist() => ['scripts/.../my_puzzle_logic.nut']
   (so nut_files is EMPTY here)                  (so nut_files is NON-empty — higher suspicion)
```

This is why `vscript_taint()` reports **both** signals separately: `entities` (the `vscripts`/
script-class hits, which catch stock-script references that live in VPKs) **and** `nut_files` (the
pakfile-embedded scripts, which catch custom logic the author shipped with the map). A map with
embedded `.nut`s is the higher-suspicion case — someone bothered to ship custom Squirrel.

---

## 6. How `dump_ents.py` detects VScript presence (exact mechanics)

Two independent reads, both grounded in srctools' BSP API. Neither parses any Squirrel.

**(a) Entity keyvalue / classname scan** — over `bsp.ents` (the entity lump parsed as a VMF), check
the `vscripts` keyvalue and the classname, per §2a/§2b.

**(b) Pakfile `.nut` listing** — the BSP pakfile lump is an embedded ZIP. srctools exposes it as a
`zipfile.ZipFile` (`bsp.py`: `pakfile: ParsedLump[ZipFile]`), so we just filter its `namelist()`:

```python
# py/bsp_recon/dump_ents.py — vscript_taint()
nut_files = [n for n in bsp.pakfile.namelist() if n.lower().endswith('.nut')]
```

```
  BSP file
  ├── LUMP_ENTITIES (0)   ──► bsp.ents  ──► scan `vscripts` kv + logic_script/point_script class
  └── LUMP_PAKFILE  (40)  ──► bsp.pakfile (a zipfile.ZipFile)
                                   └── .namelist() ──► keep *.nut  ──► nut_files[]
```

(Lump indices `ENTITIES=0`, `PAKFILE=40` are srctools' `BSP_LUMPS` enum values; see
[`bsp_format.md`](bsp_format.md) for the lump table.)

---

## 7. What srctools actually knows about `.nut` (and what it refuses to do)

To be precise about the dependency: srctools treats Squirrel as an opaque **resource file**, never
as a language. The three places it appears, all verified in srctools 2.7.0 source:

- **`const.py`** — `FileType.VSCRIPT_SQUIRREL = 'nut'`. A `.nut` is just a file type, like `vtf`
  or `wav`. The value *is* the extension; there is no parser behind it.
- **`fgd.py`** — `ValueTypes.STR_VSCRIPT = 'scriptlist'` and `STR_VSCRIPT_SINGLE = 'script'`. These
  are the FGD value types behind the `vscripts` keyvalue (a list) and single-script inputs. They
  tell srctools "this string names script files," nothing more.
- **`packlist.py`** — the *only* code in srctools that opens a `.nut` body is `_get_vscript_files()`,
  used when **packing** a map's dependencies. Its own docstring says it is *"very dynamic, this only
  looks for obvious calls"* and it is a deliberately **sloppy byte-level regex** over the file:

```
  packlist.py: func_pattern = re.compile(rb'([a-zA-Z]+)\s*\(\s*"([^"]+)"')   # match  func("param"
  SCRIPT_FUNC_TYPES = {                # the ONLY calls it recognizes — all resource precaches:
      b'IncludeScript'      : ('scripts/vscripts/', VSCRIPT_SQUIRREL),
      b'DoIncludeScript'    : ('scripts/vscripts/', VSCRIPT_SQUIRREL),
      b'PrecacheScriptSound': ('', GAME_SOUND),
      b'PrecacheSoundScript': ('', GAME_SOUND),
      b'PrecacheModel'      : ('', MODEL),
  }
```

This regex extracts **filename arguments to precache calls** so the packer knows which sounds/models/
sub-scripts to bundle. It does **not** understand control flow, `EntFire`, `AddOutput`,
`ConnectOutput`, string concatenation, or anything about the I/O graph. There is no AST, no VM, no
semantic model. So even srctools' single touch of `.nut` contents confirms the ceiling: nobody is
parsing Squirrel — the engine is the only thing that ever *runs* it.

---

## 8. Bottom line for the causal-graph extraction

```
  ┌────────────────────────────────────────────────────────────────────────────┐
  │  Static parse recovers the FULL puzzle wiring  ⟺  no script mutates the I/O  │
  │  graph at runtime.                                                           │
  │                                                                              │
  │  We cannot prove that property from a parse (no Squirrel parser exists), so  │
  │  dump_ents emits a conservative TAINT FLAG instead:                          │
  │     vscript = bool(vscripts-keyvalue/logic_script hits  OR  embedded .nut)   │
  │                                                                              │
  │  • flag clear  -> the static causal graph is trusted complete.               │
  │  • flag set, stock .nut only (glados/transition/...) -> wiring still complete;│
  │    flag is a "human glance" hint, not a defect.                              │
  │  • flag set, EMBEDDED custom .nut -> treat the graph as possibly incomplete  │
  │    (under-approximate): a script may add edges we never see.                 │
  └────────────────────────────────────────────────────────────────────────────┘
```

For our corpus the practical outcome is good: PeTI puzzle wiring is baked entity I/O, the stock
`.nut`s are atmosphere glue living in the game VPKs, and the taint flag cleanly separates
"trust the graph" from "a human needs to read this map's script." VScript remains the one hard
ceiling — and it is correctly *flagged*, never silently assumed away.
