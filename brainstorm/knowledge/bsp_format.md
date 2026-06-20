# BSP file format (Source v21, "VBSP")

Reference for the Source-engine **`.bsp`** map format as our offline recon tool
[`py/bsp_recon/dump_ents.py`](../../py/bsp_recon/dump_ents.py) reads it (via `srctools` 2.7.0 on
Python 3.14). Everything here is grounded in the installed srctools source
(`srctools/bsp.py`, `srctools/const.py`, `srctools/vmf.py`); byte layouts, lump indices, and
constants are quoted from there, not from memory.

Sibling docs: [vpk_format.md](vpk_format.md) (where the game's materials/models live),
[vmt_format.md](vmt_format.md) (material scripts + `%compilenoportal`),
[nut_vscript.md](nut_vscript.md) (the VScript static-analysis ceiling),
[vtf_mdl.md](vtf_mdl.md) (textures + models referenced by the BSP).

The design context — *why* we parse BSPs offline (the button→relay→door I/O causal graph, the
affordance prior, baked portalability) — is in
[../offline_map_preprocessing.md](../offline_map_preprocessing.md). This doc is the *how the bytes
are laid out* companion to that.

Concrete numbers below come from our reference dump of the training/eval workshop chamber
`workshop/14115283150118884095/1781795990.bsp` (Portal 2, **BSP version 21**): **533 entities,
1420 brushes, 7704 planes, 2195 faces, 4959 vertexes, 116 brush models, 44 materials, 159 static
props, `map_revision` 30.**

---

## 1. What a BSP is

A `.bsp` is a single binary file produced by Valve's map compiler chain
(`vbsp` → `vvis` → `vrad`) from a Hammer/PeTI `.vmf`. It is a flat container of **64 numbered
"lumps"** — independent byte regions, each holding one kind of data (the entity list, the planes,
the faces, the embedded ZIP, …). There is no cross-lump framing: a lump is just `(offset, length)`
into the file, and its *contents* are an array of fixed-size structs (or, for `ENTITIES`/`PAKFILE`,
plaintext / a ZIP). Indices in one lump point into another by array position.

The magic is the 4 ASCII bytes **`VBSP`** (`srctools/bsp.py:53` `BSP_MAGIC = b'VBSP'`). Portal 2 is
**version 21** (`srctools/bsp.py:100` `PORTAL_2 = 21`; also `const.py` `PORTAL_2 = '620'` is the
Steam appID, a different thing). `dump_ents._version()` reports this number.

```
 .bsp file (one map)
 ┌─────────────────────────────────────────────────────────────────────┐
 │ HEADER                                                                │
 │   "VBSP"  (4 bytes magic)                                             │
 │   version (int32)            -> 21 for Portal 2                       │
 │   lump directory: 64 entries x 16 bytes  (the lump index table)      │
 │   map_revision (int32)       -> compile/save counter (30 here)        │
 ├─────────────────────────────────────────────────────────────────────┤
 │ LUMP DATA (in arbitrary file order; located via the directory)       │
 │   ... ENTITIES bytes ...                                             │
 │   ... PLANES bytes ...                                               │
 │   ... FACES bytes ...                                                │
 │   ... PAKFILE bytes (an embedded ZIP) ...                           │
 │   ... GAME_LUMP bytes (static props 'sprp', etc.) ...               │
 └─────────────────────────────────────────────────────────────────────┘
```

---

## 2. The header and the lump directory

srctools reads the header as two struct formats (`srctools/bsp.py:55-56`):

```
HEADER_1   = '<4si'   # magic (4s), version (int32)
HEADER_LUMP= '<4i'    # one lump directory entry: 4 x int32
```

Read order in `BSP.read()` (`srctools/bsp.py:1566-1631`):

```
 offset  size  field
 ──────  ────  ─────────────────────────────────────────────────────────
   0      4    magic   = "VBSP"                       struct '<4s'
   4      4    version = 21                           struct '<i'
   8     ...   lump directory: 64 x 16-byte entries   (LUMP_COUNT=64)
  ...     4    map_revision (int32) AFTER the 64 entries  '<i'
 ──────  ────  ─────────────────────────────────────────────────────────
 header size = 8 + 64*16 + 4 = 1036 bytes, then lump data follows
```

`LUMP_COUNT` is `max(lump.value) + 1 == 64` (`srctools/bsp.py:331`). `map_revision` is parsed right
after the directory (`bsp.py:1631 [self.map_revision] = struct_read('i', file)`) and surfaced by
`dump_ents.extract()` as `'map_revision': bsp.map_revision` (30 for our map). It is a save/compile
counter, *not* the format version.

### 2.1 A lump directory entry

Each of the 64 entries is **16 bytes**, four little-endian int32s (`HEADER_LUMP = '<4i'`):

```
 lump directory entry (16 bytes, struct '<4i')   index i  ->  BSP_LUMPS(i)
 ┌────────┬────────┬────────┬────────────────────────────────────────────┐
 │ off    │ off+4  │ off+8  │ off+12                                       │
 ├────────┼────────┼────────┼────────────────────────────────────────────┤
 │ fileofs│ filelen│ version│ fourCC / uncompressed-size                   │
 │ int32  │ int32  │ int32  │ int32                                        │
 │ byte   │ byte   │ lump-  │ 0  -> lump is stored raw                     │
 │ offset │ count  │ format │ >0 -> lump is LZMA-compressed; this value    │
 │ in file│ in file│ version│       is the *uncompressed* size in bytes    │
 └────────┴────────┴────────┴────────────────────────────────────────────┘
```

srctools comments the 4th field exactly (`bsp.py:1617-1619`): *"originally the fourCC identity, but
is instead used to indicate the unpacked size if compressed."* The parse:

```python
offset, length, version, uncomp_size = struct_read(HEADER_LUMP, file)   # bsp.py:1619
```

The per-lump `version` field is the **lump-format version**, distinct from the file version 21, and
srctools warns it is *"Not totally reliable, Valve sometimes modifies lumps without updating the
ID"* (`bsp.py:531-533`) — this is exactly the "infer static-prop layout from byte size, not the
version field" footgun called out in
[../offline_map_preprocessing.md](../offline_map_preprocessing.md) §4. We never roll our own struct
sizing; srctools handles it.

> **L4D2 quirk (not us, but in the loader):** for Left 4 Dead 2 the entry field order is permuted to
> `version, offset, length` (`bsp.py:1621-1623`). Portal 2 uses the standard order above. We pass no
> `expected_version`, so srctools auto-detects 21 and keeps the standard layout.

### 2.2 Loading lump bytes (+ LZMA)

After reading all 64 directory entries, srctools seeks to each `fileofs`, reads `filelen` bytes, and
if `uncomp_size > 0` LZMA-decompresses them (`bsp.py:1633-1643`):

```python
file.seek(offset)
lump_data = file.read(length)
if uncomp_size > 0:                 # 4th directory field non-zero => compressed
    lump.is_compressed = True
    lump_data = decompress_lzma(lump_data)
```

So **compression is per-lump**, signalled entirely by the directory's 4th int being non-zero. Source
uses its own LZMA container (`LZMA` magic + a small header, not a `.7z`/`.xz` stream);
`srctools.compress.decompress_lzma` handles that wrapper. The `PAKFILE` lump is special-cased and is
never compressed (`bsp.py:1819` *"Normal lump, pakfiles can't be compressed."*). Our recon never
touches compressed bytes directly — `srctools` transparently inflates, and `dump_ents` reads only
the parsed objects.

---

## 3. The lump INDEX table (the ones we use)

The numeric index is the directory slot; the name and number are from
`srctools/bsp.py:155-243` (`class BSP_LUMPS`). The right column is the `srctools.BSP` attribute
`dump_ents` reads, when relevant. Aliased indices (22–25, 49, 51–52) were reused across games; only
the Portal-2-relevant meaning is shown.

```
 idx  BSP_LUMPS name        srctools attr        what it holds / our use
 ───  ─────────────────────  ───────────────────  ─────────────────────────────────────
  0   ENTITIES              bsp.ents (VMF)       plaintext keyvalues + I/O. THE causal graph.
  1   PLANES                bsp.planes           half-space planes (normal, dist). 7704 here.
  2   TEXDATA               (in bsp.texinfo)     material name index + texture size/reflectivity
  3   VERTEXES              bsp.vertexes         raw float xyz points. 4959 here.
  6   TEXINFO               bsp.texinfo          per-surface: SURF_ flags + -> TexData -> material
  7   FACES                 bsp.faces            renderable faces (winding + texinfo). 2195 here.
 12   EDGES                 (in bsp.surfedges)   vertex-pair edges
 13   SURFEDGES             bsp.surfedges        signed indices into EDGES (winding order)
 14   MODELS                bsp.bmodels          brush models: AABB + face range. 116 here.
 18   BRUSHES               bsp.brushes          convex solids = intersection of half-spaces. 1420.
 19   BRUSHSIDES            (in bsp.brushes)     one (plane, texinfo) per brush face
 27   ORIGINALFACES         bsp.orig_faces       pre-split faces (carry Hammer IDs)
 35   GAME_LUMP             (sub-lumps 'sprp'…)  static props live here -> bsp.props. 159 here.
 40   PAKFILE               bsp.pakfile (ZipFile)embedded ZIP: cubemap .vtf, .vhv, any .nut, etc.
 42   CUBEMAPS              bsp.cubemaps         env_cubemap sample positions
 43   TEXDATA_STRING_DATA   (in bsp.textures)    the material-name string blob
 44   TEXDATA_STRING_TABLE  bsp.textures         offsets into the string blob -> 44 materials here
 45   OVERLAYS              bsp.overlays         decals/overlays projected onto faces
 29   PHYSCOLLIDE           (in bsp.bmodels)     compiled VCollide blobs (we deliberately avoid)
```

`dump_ents._geometry_counts()` reports `len()` of `brushes, planes, faces, vertexes, props,
cubemaps, overlays`; `dump_ents.extract()` reports `bsp.textures` (the 44 material names) and the
`bsp.pakfile.namelist()`. `extract_geometry()` walks `bsp.faces` and `bsp.brushes`.

Note srctools' lump→attribute mapping is *not* one struct per lump: several attributes pull in
helper lumps. `bsp.surfedges` consumes both SURFEDGES(13) **and** EDGES(12); `bsp.brushes` consumes
BRUSHES(18) **and** BRUSHSIDES(19); `bsp.textures` consumes TEXDATA_STRING_TABLE(44) **and**
TEXDATA_STRING_DATA(43); `bsp.texinfo` consumes TEXINFO(6) **and** TEXDATA(2). This is declared with
`ParsedLump(primary, *extra)` descriptors (`bsp.py:1501-1543`). The GAME_LUMP is a lump-of-lumps —
see §8.

---

## 4. The ENTITIES lump (index 0) — the causal-graph source

This lump is **plaintext**, not structs: a sequence of brace-delimited blocks of `"key" "value"`
pairs, exactly like the entity section of a `.vmf`. srctools tokenizes it
(`bsp.py:3215-3305`), decoding the bytes as `ascii` with `surrogateescape` (*"VMFs don't have a
clear encoding"*), and builds a `srctools.vmf.VMF` object. `dump_ents.load_bsp()` returns
`bsp.ents` as the second tuple element.

```
 ENTITIES lump bytes (plaintext, NUL-terminated)
 ┌────────────────────────────────────────────────────────────────┐
 │ {                                                                │  <- entity 0 = worldspawn
 │   "classname" "worldspawn"                                       │     (the whole-map brush model)
 │   "skyname" "sky_black_nofog"                                    │
 │   "mapversion" "30"                                              │
 │ }                                                                │
 │ {                                                                │  <- entity 1
 │   "classname" "prop_floor_button"                                │
 │   "targetname" "cubedropper6-button7"                            │   (instance prefix baked in, §7)
 │   "origin" "128 256 64"                                          │
 │   "OnPressed" "cubedropper6-relay...,Trigger,,0,-1"              │   <- an I/O connection (output)
 │ }                                                                │
 │ ... 533 entities total ...                                       │
 │ \x00                                                             │  <- NUL terminates the lump
 └────────────────────────────────────────────────────────────────┘
```

### 4.1 worldspawn is entity 0

The first block **must** be `worldspawn` (`bsp.py:3243-3245` raises otherwise). srctools stores it
separately as `vmf.spawn` (a property), *not* in `vmf.entities` (`bsp.py:3247-3248`). `dump_ents`
reflects this: it iterates `vmf.entities` for the 533 entities and pulls `vmf.spawn` separately into
the `'worldspawn'` field. worldspawn owns brush model `*0` (the entire static world geometry) — see
§6.

### 4.2 keyvalue vs. I/O connection (the comma-vs-`0x1B` heuristic)

A line like `"OnPressed" "door,Open,,0.5,-1"` is an **output** (an I/O wire), not an ordinary
keyvalue. srctools disambiguates (`bsp.py:3276-3299`):

- If the value contains the byte **`0x1B`** (`ESC`, post-Left-4-Dead separator), it is *always* an
  output (`OUTPUT_SEP = chr(27)`, `vmf.py:53`).
- Otherwise (older comma style — what Portal 2 uses), it is treated as an output **iff it has exactly
  4 commas** and parses; else it is a plain keyvalue.

Parsed outputs are stored on the entity (`cur_ent.add_out(...)`) and exposed as `ent.outputs`.

### 4.3 The output tuple we extract

`srctools.vmf.Output` (`vmf.py:3595-3658`) holds these fields; `dump_ents.extract()` reads exactly:

```
 Output field   meaning                                      dump_ents JSON key
 ────────────   ──────────────────────────────────────────  ──────────────────
 o.output       the event on THIS entity (e.g. "OnPressed")  "output"
 o.target       targetname of the entity to fire at          "target"
 o.input        the input to fire (e.g. "Open", "Trigger")   "input"
 o.params       parameter string passed to the input         "params"
 o.delay        seconds to wait before firing (float)        "delay"
 o.times        fire count; -1 = unlimited, 1 = once         "times"
```

So our extraction tuple is **`(output, target, input, params, delay, times)`**. `dump_ents` also
tags `"via_logic"` when `o.target` names a relay/branch/counter class (`RELAY_CLASSES`), so an
indirect `button → relay → … → door` chain is visible in the dump.

The real chain we hand-traced on the reference map (proving the static graph is correct):

```
 prop_floor_button . OnPressed
        │  (output -> target,input)
        ▼
 func_instance_io_proxy            (instance boundary relay, baked by VBSP)
        │  .OnProxyRelay -> math_counter , Add
        ▼
 math_counter . OnHitMax
        │
        ▼
 logic_branch . OnTrue
        │
        ▼
 prop_testchamber_door . Open      <- the door finally opens
```

`func_instance_io_proxy`, `math_counter`, and `logic_branch` are all in `dump_ents.RELAY_CLASSES`,
so every hop is flagged `via_logic`. **Caveat (the static-analysis ceiling):** if a VScript does a
runtime `AddOutput`/`EntFire`, that wire is invisible to this parse — see
[nut_vscript.md](nut_vscript.md) and `dump_ents.vscript_taint()`.

---

## 5. The face winding chain (FACES → SURFEDGES → EDGES → VERTEXES)

A face's polygon outline ("winding") is stored **indirectly**, so that adjacent faces can share edge
and vertex data. `dump_ents.extract_geometry()` reads each face's `verts` as `[e.a for e in
f.edges]` — srctools has already resolved the whole chain into `Edge` objects with `.a`/`.b` `Vec`s.
Here is what it resolves.

The FACES lump struct in v21 (`LUMP_LAYOUT_STANDARD["FACE"]`, `bsp.py:268`) is
`'<H??i4h4sif5iHHI'`; srctools unpacks the fields (`bsp.py:2212-2229`). The two that drive the
winding:

```
 FACE struct (subset)            from struct '<H??i4h4sif5iHHI'
 ───────────────────────────────────────────────────────────────
 plane_num   (H)   index into PLANES   -> the face's supporting plane
 side        (?)   same_dir_as_plane   -> winding orientation vs plane normal
 on_node     (?)
 first_edge  (i)   index into SURFEDGES  ──┐  the contiguous run of
 num_edges   (h)   count of surfedges    ──┘  surfedges for this face
 texinfo_ind (h)   index into TEXINFO   -> SURF_ flags + material (§4 portalability)
 ...
```

srctools slices the surfedges range directly:
`self.surfedges[first_edge : first_edge + num_edges]` (`bsp.py:2251`).

The resolution chain, end to end:

```
 Face
  │  first_edge, num_edges
  ▼
 SURFEDGES[first_edge .. first_edge+num_edges]      lump 13, struct 'i' (signed int32)
  │   each entry is a SIGNED index into EDGES:
  │     ind >= 0  -> EDGES[ind]            (vertices in forward order  a->b)
  │     ind <  0  -> EDGES[-ind].opposite  (vertices REVERSED          b->a)
  ▼
 EDGES[k]                                            lump 12, struct '<HH' (two uint16)
  │   = (vertex_index_a, vertex_index_b)
  ▼
 VERTEXES[a], VERTEXES[b]                            lump 3, struct '<fff' (xyz float)
```

The sign trick is srctools `_lmp_read_surfedges` verbatim (`bsp.py:2075-2085`):

```python
edges = [Edge(verts[a], verts[b]) for a, b in EDGE_struct.iter_unpack(EDGES_data)]
for [ind] in struct.iter_unpack('i', edge_inds):     # SURFEDGES are signed int32
    if ind < 0:   yield edges[-ind].opposite          # reversed winding
    else:         yield edges[ind]
```

Walking `face.edges` and taking each edge's `.a` gives the ordered ring of corner points — the
polygon. That is precisely `verts: [_vec(e.a) for e in f.edges]` in `extract_geometry()`. The signed
indirection is what guarantees a consistent CCW winding even though two faces share one physical
EDGE (one sees it forward, the neighbour sees `.opposite`).

> **EDGES uses uint16 vertex indices** in v21 (`'<HH'`), capping at 65535 vertexes — fine for PeTI
> chambers (4959 here). Strata Source widened this to uint32 (`bsp.py:316`); not our concern.

---

## 6. Brush models — how a brush entity references its geometry

Entities like `func_door`, `func_brush`, `func_movelinear`, `func_noportal_volume`, and worldspawn
are **brush entities**: their solid geometry is a *brush model* stored in the MODELS lump (14), and
the entity points at it with a `"model" "*N"` keyvalue where `N` is the brush-model index.

`srctools` resolves this for us as `bsp.bmodels`, a `WeakKeyDictionary[Entity, BModel]`
(`bsp.py:2783-2829`):

```python
brush_ents[vmf.spawn] = bmodel_list[0]          # worldspawn is ALWAYS *0
for ent in vmf.entities:
    if ent['model'].startswith('*'):
        mdl_ind = int(ent.pop('model')[1:])     # "*7" -> 7
        brush_ents[ent] = bmodel_list[mdl_ind]
```

The MODELS struct is `'<9fiii'` (`bsp.py:2786-2797`):

```
 BModel struct (lump 14, struct '<9fiii', 48 bytes)
 ┌──────────────────────────────────────────────────────────────────┐
 │  mins   (3 x float)  ─┐ axis-aligned bounding box (world space)    │
 │  maxes  (3 x float)  ─┘                                            │
 │  origin (3 x float)    pivot for rotating brush ents               │
 │  headnode (int32)      root VisTree node for this model            │
 │  first_face (int32) ─┐ range into FACES for this model's surfaces  │
 │  num_face   (int32) ─┘                                             │
 └──────────────────────────────────────────────────────────────────┘
```

`dump_ents.extract()` reads `bm.mins`/`bm.maxes` per entity into the `"aabb"` field
(`[_vec(bm.mins), _vec(bm.maxes)]`). That AABB is the **static, world-space** bounding box of the
brush entity — load-bearing for the affordance prior (e.g. a `func_door`'s travel distance is
`size-along-axis − lip`, which needs this box; see
[../offline_map_preprocessing.md](../offline_map_preprocessing.md) §2c). Note srctools *pops* the
`"model"` key, so it won't appear in the per-entity `keyvalues` dict — the AABB carries that
information instead.

---

## 7. BRUSHES — convex solids as intersections of half-space PLANES

The world (and brush entities) are built from **brushes**: convex polyhedra defined as the
**intersection of half-spaces**. Each half-space is a `PLANES`-lump plane (a `normal` + `dist`), and
a brush is "the set of points behind *all* of its sides' planes." A point is inside iff
`dot(normal, p) <= dist` for every side. This is the representation `dump_ents.extract_geometry()`
emits per brush (`{contents, sides:[{material, flags, plane}]}`).

```
 BRUSHES lump (18, struct '<iii')        BRUSHSIDES lump (19, struct '<HhhH' in v21)
 ┌───────────────────────────┐           ┌──────────────────────────────────────────┐
 │ first_side (int32) ──────────────────▶│ [first_side .. first_side+side_count)      │
 │ side_count (int32)         │           │   each side:                                │
 │ contents   (int32) ─┐      │           │     plane_num (H) -> PLANES[plane_num]      │
 └─────────────────────┼──────┘           │     texinfo   (h) -> TEXINFO[texinfo]       │
                       │                  │     dispinfo  (h)                           │
              BSPContents flags           │     bevel     (H) (bit0 = axial bevel side) │
              (const.py:189; e.g.         └──────────────────────────────────────────┘
               SOLID=0x1, GRATE=0x8,
               PLAYER_CLIP=0x10000,
               DETAIL=0x8000000)

 A brush = ⋂ over its sides of  { p : dot(plane.normal, p) <= plane.dist }

     plane A normal─►          a convex solid is carved by its bounding planes:
        ╲                          ┌───────────┐   each edge of the box is one
         ╲   inside (behind        │           │   half-space; the brush is the
          ╲  every plane)          │  brush    │   region behind all of them
   plane D ─────────────── plane B │  (solid)  │
          ╱                        │           │
         ╱                         └───────────┘
        ╱  plane C normal─►
```

srctools `_lmp_read_brushes` (`bsp.py:2381-2397`) builds the side list first, then slices per brush:

```python
sides = [BrushSide(planes[plane_num], texinfo[texinfo], dispinfo,
                   bool(bevel & 1), bevel & ~1)
         for (plane_num, texinfo, dispinfo, bevel) in BRUSHSIDE_struct.iter_unpack(...)]
for first_side, side_count, contents in struct.iter_unpack('<iii', data):
    yield Brush(BrushContents(contents), sides[first_side:first_side+side_count])
```

`extract_geometry()` reports `str(br.contents)` (a `BSPContents` flag set), and per side the
material name + raw `flags` + plane. The PLANES struct itself is `'<ffffi'` (`bsp.py:2052-2056`):
3-float `normal`, 1-float `dist`, then an int `PlaneType` (axis classification). `dump_ents._plane()`
keeps just `{normal:[x,y,z], dist}`.

> Some brushsides are **bevel planes** — artificial axial sides VBSP inserts so the brush has a full
> bounding box for collision inflation (`bsp.py:1048-1051`, `is_bevel_plane`). They are real planes
> but not "authored" faces; the `bevel & 1` bit distinguishes them. We currently keep all sides.

> Building an occupancy/floor grid from this (BRUSHES ∩ BRUSHSIDES ∩ PLANES) is the *parked* static
> nav payload — runtime A\* already gets dynamics-aware geometry, so we deliberately do **not** do
> this, and we avoid the compiled `PHYSCOLLIDE`(29) VCollide blobs entirely. See
> [../offline_map_preprocessing.md](../offline_map_preprocessing.md) §2a / fork #5.

---

## 8. TEXINFO + SURF_ flags — portalability baked at compile time

Every face and brushside carries a **texinfo index** (lump 6). A `TexInfo` (`bsp.py:637-718`) holds
texture s/t projection axes, a `SurfFlags` bitfield, and a reference to a `TexData` (lump 2) whose
`.mat` is the **material name** (resolved through the TEXDATA string table, lumps 43/44). So:

```
 Face / BrushSide
   │ texinfo index
   ▼
 TexInfo  ──.flags──►  SurfFlags bitfield   (SURF_NOPORTAL etc.)
   │ ._info
   ▼
 TexData  ──.mat──►    "metal/black_wall_metal_002c"   (material path, sans materials/ + .vmt)
                          │  (this name is what we'd resolve to a VMT — see vmt_format.md)
```

`dump_ents.extract_geometry()` reads `ti.mat`, `ti.flags.value`, and crucially:

```python
'portalable': not (ti.flags & SurfFlags.NOPORTAL),
```

The `SurfFlags` come from `const.py:149-186`; the bit that matters:

```
 SurfFlags (const.py, bspflags.h SURF_*)
 ──────────────────────────────────────────────────────
 LIGHT       0x0001   has lighting info
 SKYBOX_2D   0x0002
 SKYBOX_3D   0x0004
 WATER_WARP  0x0008
 TRANSLUCENT 0x0010
 NOPORTAL    0x0020   ◄── "Portalgun blocking material"  ← THE one we read
 TRIGGER     0x0040
 NODRAW      0x0080   invisible / tool texture
 HINT        0x0100
 SKIP        0x0200
 NOLIGHT     0x0400
 ...
```

**Portalability is baked at compile time.** `SURF_NOPORTAL` is *not* authored directly — `vbsp`
sets it on a face when the face's material VMT carries `%compilenoportal 1`. On the reference map:
`metal/black_wall_metal_002c.vmt` has `%compilenoportal 1` → its faces get `SURF_NOPORTAL` →
**not** portalable; `tile/white_floor_tile002a.vmt` has no such key → faces lack the flag →
portalable. So `dump_ents` reads portalability for *static* surfaces straight from the lump, with
**no VMT lookup needed** — that is the whole point of the baked flag and why `extract_geometry()`'s
docstring says "no VMT lookup is needed for static surfaces." (Resolving the VMT is still needed for
`$surfaceprop`, the shader name like `LightmappedGeneric`, and to confirm `%compilenoportal`
authorship — that hop is [vmt_format.md](vmt_format.md), and flip-panel faces whose portalability is
*conditional* still need runtime confirmation.)

---

## 9. Instance fixup — how VBSP flattens `func_instance`

PeTI/Hammer maps are built from **instances** (`func_instance`): a sub-VMF (e.g. a cube dropper, a
button assembly) dropped in many times. At compile, `vbsp` **flattens** every instance into the main
map and **prefixes the instance's local targetnames** so they stay unique. The prefix is derived
from the instance entity's name/fixup style (e.g. `cubedropper6-`, `button7-`, `InstanceAuto3-`).

```
 Authoring time (.vmf)                 Compiled (.bsp ENTITIES lump)
 ┌───────────────────────────┐         ┌─────────────────────────────────────────┐
 │ func_instance "cubedropper"│         │ prop_floor_button                         │
 │   ├─ button "btn"          │  vbsp   │   targetname "cubedropper6-btn"           │
 │   ├─ relay  "rly"          │ ──────► │ logic_relay                               │
 │   └─ io proxy              │ flatten │   targetname "cubedropper6-rly"           │
 │ (placed 6th -> id 6)       │ +prefix │ func_instance_io_proxy                    │
 └───────────────────────────┘         │   targetname "cubedropper6-proxy"         │
                                        │   (all targetnames now globally unique)   │
                                        └─────────────────────────────────────────┘
```

**Why this matters for us:** by the time we parse the `.bsp`, the fixup is *already baked in* — every
output `target` is a concrete, real targetname (no `$instance`/`@` placeholders left to resolve).
That is what makes the static causal-graph extraction in §4 work without us re-implementing instance
resolution: `OnPressed → cubedropper6-relay,Trigger` already names a real entity in the same lump.
The instance boundary survives only as `func_instance_io_proxy` relay entities (which we treat as a
relay class). This is the "targetnames are already fixup-baked in the compiled BSP" assumption in
[../offline_map_preprocessing.md](../offline_map_preprocessing.md) §4 (P1) and recon item R2.2.

---

## 10. The PAKFILE lump (40) — an embedded ZIP

PAKFILE is a complete, standard **ZIP archive** embedded as a lump. srctools exposes it as
`bsp.pakfile`, a Python `zipfile.ZipFile` opened over the lump bytes (`bsp.py:2889-2893`). It carries
per-map baked assets: compiled cubemap textures (`.vtf`), per-vertex lighting (`.vhv`), and *any*
custom content the map author packed in (custom materials, models, or `.nut` VScripts).

`dump_ents` uses it two ways:

- `extract()` lists `bsp.pakfile.namelist()` into the JSON `"pakfile"` field.
- `vscript_taint()` scans `namelist()` for `*.nut` to flag a map whose wiring might be set at runtime
  by an *embedded* script (`bsp.py:138`).

```
 PAKFILE lump bytes  ==  a literal .zip
 ┌──────────────────────────────────────────────────────┐
 │ ZIP local file header + data  (materials/.../cube.vtf)│  cubemap textures
 │ ZIP local file header + data  (.../something.vhv)     │  per-vertex lighting
 │ [ possibly  scripts/vscripts/foo.nut ]                │  embedded VScript (flagged)
 │ ... central directory ...                             │
 └──────────────────────────────────────────────────────┘
```

**Important distinction (see [nut_vscript.md](nut_vscript.md)):** *stock* P2 VScripts the map
*references* via a `vscripts` keyvalue (e.g. `sp_transition_list`, `glados`, `voting_dialog`) live in
the **game VPKs**, NOT in this pakfile. So an empty `pakfile` `.nut` list does **not** mean the map
is VScript-free — `vscript_taint()` checks both the embedded pakfile (here) **and** the `vscripts`
keyvalue / `logic_script` classname on entities. Stock workshop chambers (like our reference map) are
typically PeTI-vanilla with no embedded `.nut`, which is why the static causal graph is *complete*
for our corpus.

---

## 11. How this maps to `dump_ents.py`

Two extraction entry points, both starting from `srctools.bsp.BSP(path)`:

```
 extract(path)                     ── the primary per-map record
 ─────────────────────────────────────────────────────────────────────────
 bsp.ents (VMF)        §4  -> entities[]: classname, targetname, origin,
                                          keyvalues, outputs[(out,tgt,in,
                                          params,delay,times)]  (the causal graph)
 bsp.bmodels           §6  -> per-entity "aabb": [mins, maxes]
 bsp.textures          §8  -> "materials": 44 names
 bsp.pakfile           §10 -> "pakfile": namelist();  vscript_taint -> .nut scan
 bsp.map_revision      §2  -> "map_revision": 30
 bsp.version           §2  -> "version": 21
 _geometry_counts()    §3  -> len(brushes/planes/faces/vertexes/props/cubemaps/overlays)
 bsp.props             §8(game lump) -> static_props[]: model, origin, angles, skin

 extract_geometry(path)            ── raw geometry (the --geometry sidecar)
 ─────────────────────────────────────────────────────────────────────────
 bsp.faces             §5  -> faces[]: material, flags, portalable(=not NOPORTAL),
                                       plane, verts(=face winding ring)
 bsp.brushes           §7  -> brushes[]: contents, sides[(material,flags,plane)]
```

Everything `dump_ents` produces is read-only: we never call `bsp.save()`, never mutate the entity
lump. The risqué "patch the entity lump offline" idea is explicitly rejected
([../offline_map_preprocessing.md](../offline_map_preprocessing.md) §5).

---

## 12. Gotchas / footguns (grounded)

- **Lump `version` ≠ file version.** The per-lump version field is unreliable (`bsp.py:531-533`);
  static-prop layout especially must be inferred from per-prop byte size, which srctools does —
  never roll your own sprp parser.
- **`map_revision` is a save counter, not the format version.** 30 here; format is 21.
- **`SURF_NOPORTAL` gives static portalability for free, but not conditional/flip-panel surfaces** —
  those need runtime confirmation; and `$surfaceprop`/shader still require the VMT hop
  ([vmt_format.md](vmt_format.md)).
- **An empty pakfile `.nut` list is not "no VScript."** Stock scripts are referenced from game VPKs
  ([nut_vscript.md](nut_vscript.md)); always also scan the `vscripts` keyvalue. Runtime
  `AddOutput`/`EntFire` is the hard static-analysis ceiling — the causal graph is a sound
  *over-approximation*, not guaranteed edges.
- **`"model"` is popped off brush entities** during `bmodels` resolution — read geometry from the
  `aabb`, not from a `keyvalues["model"]`.
- **Worldspawn is entity 0 and lives in `vmf.spawn`, outside `vmf.entities`.** Don't double-count or
  miss it.
- **SURFEDGES indices are signed; EDGE vertex indices are uint16 in v21** (65535 cap) — fine for
  PeTI, but a thing to know if a corpus map is unusually dense.
