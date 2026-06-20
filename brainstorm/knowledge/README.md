# Source file-format reference (offline BSP recon)

These docs explain the Source-engine on-disk file formats that our **offline BSP recon**
tool — [`py/bsp_recon/dump_ents.py`](../../py/bsp_recon/dump_ents.py) — reads to build a
static, per-map record for the **LLM-percept/act** project: the entity I/O causal graph,
geometry + baked portalability, static props, and a VScript "taint" flag. Everything in
these docs is grounded in the installed `srctools` 2.7.0 source (byte layouts, lump
indices, struct formats, flag values are quoted from `srctools/{bsp,vpk,vmt,vtf,mdl,const,
filesys,surfaceprop}.py`, not from memory). The *why* (design intent — the
button→relay→door affordance prior, the static-vs-runtime boundary) lives in
[`../offline_map_preprocessing.md`](../offline_map_preprocessing.md); these are the *how
the bytes are laid out* companions to it.

---

## The Source content / asset pipeline

How the formats relate. A `.bsp` is self-contained for *structure* (entity graph,
geometry, baked flags) but refers to its *content* (materials, models) **by name**; those
names resolve through a prioritized filesystem chain.

```
                         ┌────────────────────────────────────────────────┐
                         │                  .bsp  (one map)               │
                         │  ┌──────────────┐  ┌───────────┐  ┌──────────┐ │
                         │  │ ENTITY GRAPH │  │ GEOMETRY  │  │ PAKFILE  │ │
                         │  │ (I/O wiring, │  │ faces /   │  │ embedded │ │
                         │  │  keyvalues)  │  │ brushes / │  │   ZIP    │ │
                         │  └──────┬───────┘  │ planes +  │  └────┬─────┘ │
                         │         │          │ baked     │       │       │
                         │         │          │ SURF_*    │       │       │
                         │         │          │ portal-   │       │       │
                         │         │          │ ability   │       │       │
                         │         │          └───────────┘       │       │
                         └─────────┼──────────────────────────────┼───────┘
              attaches .nut        │   references BY NAME         │ may embed custom
        (vscripts kv / logic_      │   (no paths, no ".vmt"/.mdl) │ .vtf / .vmt / .nut
         script) — REWIRES the     │                              │
         graph at RUNTIME          ▼                              │
              ┌──────────┐   material name      model name        │
              │  .nut    │  "tile/white_..."   "props/.../x.mdl"  │
              │ VScript  │        │                  │            │
              │ (opaque) │        ▼                  ▼            │
              └──────────┘   ┌───────────────────────────────────────────────┐
                             │  FileSystemChain (first hit wins)             │
                             │   1. [map pakfile]  ──────────────────────────┼◄┘ (highest priority)
                             │   2. [game VPKs] portal2 / dlc1 / dlc2        │
                             └──────┬─────────────────────────┬──────────────┘
                                    ▼                          ▼
                              materials/<name>.vmt        models/<name>.mdl  (+ .vvd/.vtx/.phy)
                                    │  $basetexture             │ cdmaterials[] + skins[]
                                    ▼                           └──────► materials/<cd>/<tex>.vmt
                              materials/<tex>.vtf                              │
                              (pixels — VTF)                                   ▼
                                                                         (its own .vtf)
```

Read the diagram as: **the .bsp embeds** an entity graph + geometry + a pakfile (ZIP);
it **references materials (VMT) and models (MDL) by name**; those names **resolve through
the filesystem chain** `[map pakfile] → [game VPKs]`; **VMTs point at VTF textures**;
and entities may **attach a .nut VScript that rewires the entity graph at runtime** —
which is the one thing the offline parse cannot follow.

---

## The docs

| Doc | Read it for |
|-----|-------------|
| [bsp_format.md](bsp_format.md) | The `.bsp` container: header, 64-lump directory, the ENTITIES causal graph, FACES→SURFEDGES→EDGES→VERTEXES winding, brush models, BRUSHES as half-space intersections, baked `SURF_NOPORTAL`, instance fixup, the embedded PAKFILE — and how each maps to `dump_ents.py`. |
| [vpk_format.md](vpk_format.md) | The VPK archive (`pak01_dir.vpk` index + `pak01_NNN.vpk` data split), the `FileEntry` struct, and the `FileSystemChain` priority model that resolves a material/model name to bytes. |
| [vmt_format.md](vmt_format.md) | The `.vmt` material script: shader KeyValues, the `patch`/`include` resolution (`apply_patches`), `$surfaceprop`, and the `%compile*` pragmas that get baked into BSP face flags at compile (why the VMT read is usually unnecessary). |
| [nut_vscript.md](nut_vscript.md) | Squirrel VScript — the static-analysis ceiling: how a `.nut` attaches (`vscripts` kv / `logic_script`), how `EntFire`/`AddOutput`/`ConnectOutput` rewire the graph at runtime, and how `dump_ents.py` emits a conservative taint flag instead of trying to parse Squirrel (srctools has no Squirrel parser). |
| [vtf_mdl.md](vtf_mdl.md) | The two name-referenced asset families: VTF texture headers/mips and the MDL studio-model fileset (`.mdl`/`.vvd`/`.vtx`/`.phy`), the static-prop `sprp` game lump we extract `{model, origin, angles, skin}` from, and the (unbuilt) prop-classification next hop. |

---

## The static-recovery boundary

What an offline parse of the shipped `.bsp` (+ the game VPKs) **can** and **cannot**
recover. This is the load-bearing honesty of the whole recon: the static graph is a
*sound superset* of guaranteed edges only when no script touches the wiring.

**CAN recover (static, from the file):**
- The **entity I/O graph** — every baked output edge `(output, target, input, params,
  delay, times)`, including indirect `button → relay → … → door` chains via logic classes.
- **Geometry** — brushes (half-space plane sets), face windings, planes, vertexes, AABBs
  of brush entities.
- **Baked portalability** — `SURF_NOPORTAL` per face, frozen in at compile from
  `%compilenoportal`; read directly, no VMT I/O needed for static surfaces.
- **All entity keyvalues** (origin, targetname, classname, instance-fixup-baked names).
- The **embedded pakfile** (its ZIP `namelist()`) and the **referenced** material/model
  names; the static-prop list.

**CANNOT recover (runtime / out-of-band):**
- **Runtime VScript rewiring** — edges a `.nut` adds via `EntFire`/`AddOutput`/
  `ConnectOutput`, or any target string computed at runtime. srctools has no Squirrel
  parser; we emit a *taint flag*, never a parse.
- **Steam workshop titles / metadata** — the human-facing map name and publish info live
  in workshop metadata, not in the `.bsp`.
- **Realized dynamic state** — actual door open/closed, button pressed, cube position,
  proxy-animated material vars; these are *runtime truth*, owned by the live engine, not
  the static file.

> Conditional/flip-panel portalability and `$surfaceprop`/shader identity sit on the edge:
> recoverable in principle via the VMT chain, but the baked flag isn't the final word for
> dynamic faces — see [vmt_format.md](vmt_format.md) §6 and [bsp_format.md](bsp_format.md) §8.
