# VTF textures and MDL models — the named assets a BSP references

Reference for the two asset families a Portal 2 `.bsp` points at **by string path** rather than
embedding directly: **VTF** (Valve Texture Format, the actual pixel data behind a material) and
**MDL** (the studio-model fileset behind every `prop_static` / `prop_physics`). Our offline recon
tool [`py/bsp_recon/dump_ents.py`](../../py/bsp_recon/dump_ents.py) reads the static-prop list out of
the BSP and records each prop's model **path**, position, orientation and skin; this doc explains
what that path points at and what metadata you could pull from it.

This is a sibling of:
- [bsp_format.md](bsp_format.md) — the BSP container, lumps, the face→edge→vertex chain, the embedded pakfile ZIP, and the static-prop game lump.
- [vpk_format.md](vpk_format.md) — the VPK archive split (`pak01_dir.vpk` + `pak01_NNN.vpk`) where stock VTFs/MDLs live.
- [vmt_format.md](vmt_format.md) — the material script (`.vmt`) that *names* the VTF(s) a surface uses (`$basetexture`, `$compilenoportal`, `$surfaceprop`).
- [nut_vscript.md](nut_vscript.md) — VScript, the static-analysis blind spot.

> All struct/field facts below are read from the **srctools 2.7.0** source installed in this repo
> (`.venv/.../srctools/vtf.py`, `mdl.py`, `bsp.py`). Where a byte offset is quoted, it is the offset
> srctools actually unpacks at — not a number from memory.

---

## 0. How the BSP refers to these assets (and where they live)

A `.bsp` almost never embeds a VTF or MDL inline. It stores **names**:

```
 BSP                          name string                resolved file
 ───────────────────────────  ─────────────────────────  ───────────────────────────────────
 texinfo.texdata → string  →  "tile/white_floor_tile002a" → materials/<name>.vmt   (a VMT)
                                                                    │ $basetexture
                                                                    ▼
                                                              materials/<tex>.vtf   (a VTF)

 static-prop game lump 'sprp' → "props/.../button_base.mdl"  → models/<name>.mdl    (+ .vvd/.vtx/.phy)
```

Resolution order for the *file* behind a name (see [vpk_format.md](vpk_format.md) for the chain):

```
 FileSystemChain:
   1. ZipFileSystem(bsp.pakfile)   ← assets the mapper embedded in THIS map
   2. VPKFileSystem(pak01_dir.vpk) ← stock Portal 2 assets (portal2 / portal2_dlc1 / portal2_dlc2)
```

For our test map (`workshop/14115283150118884095/1781795990.bsp`) the embedded pakfile holds only
cubemap `.vtf` and `.vhv` vertex-lighting files; the 44 referenced materials and 159 static props'
models all resolve out of the **game VPKs**, not the map. So a faithful VTF/MDL lookup *requires the
VPK reader*, exactly as the VMT lookup does.

> **What `dump_ents.py` does today:** it reads the static-prop list and records `{model, origin,
> angles, skin}` per prop (the `model` is the `.mdl` path string). It does **not** open the `.mdl`,
> `.vvd`, `.vtx`, `.phy`, or any `.vtf`. This doc is the reference for the *next* hop — opening those
> files to classify a prop — which is the "future model-classification idea" in §5.

---

## 1. VTF — Valve Texture Format

A `.vtf` is one texture: header + an optional low-res thumbnail + the full-res image as a mipmap
pyramid (optionally many frames / cubemap faces / volume slices). srctools parses it in
`VTF.read()` (`vtf.py:731`).

### 1.1 File layout (top level)

```
 ┌──────────────────────────────────────────────────────────────┐
 │ signature  "VTF\0"                            4 bytes          │  must equal b'VTF\0' (vtf.py:740)
 │ version_major (uint32)  = 7                                    │
 │ version_minor (uint32)  0..5                                  │  P2 ships 7.x
 ├──────────────────────────────────────────────────────────────┤
 │ _HEADER  (fixed block, see §1.2)                              │
 ├──────────────────────────────────────────────────────────────┤
 │ [v7.2+]  depth (uint16)        volume-texture layer count      │
 ├──────────────────────────────────────────────────────────────┤
 │ [v7.3+]  resource directory (extensible; §1.4)                │
 │            ↳ LOW_RES  entry → offset of thumbnail              │
 │            ↳ HIGH_RES entry → offset of main image            │
 ├──────────────────────────────────────────────────────────────┤
 │ low-res thumbnail image data   (usually DXT1, ≤16×16)         │
 ├──────────────────────────────────────────────────────────────┤
 │ high-res image data — mipmap pyramid, SMALLEST mip FIRST      │  §1.5
 └──────────────────────────────────────────────────────────────┘
```

In **v7.2 and earlier** there is no resource directory: the thumbnail starts right after the header
(`low_res_offset = header_size`) and the main image follows it (`vtf.py:846`). In **v7.3+** the
directory's `LOW_RES`/`HIGH_RES` entries give the offsets explicitly (`vtf.py:814`).

### 1.2 The fixed header block (`_HEADER`)

srctools unpacks this with one `struct.Struct` (`vtf.py:351`). Little-endian; offsets are measured
from the start of `_HEADER` (i.e. **after** the 12-byte signature+version):

```
 off  size  type    field                 notes
 ───  ────  ──────  ────────────────────  ─────────────────────────────────────────────
   0   4    uint32  header_size           total header length, used to find image data
   4   2    uint16  width                 power of two
   6   2    uint16  height                power of two (need not equal width)
   8   4    uint32  flags                 VTFFlags bitfield (§1.3)
  12   2    uint16  frame_count           >1 for animated textures
  14   2    uint16  first_frame_index     "appears almost unused" (vtf.py:706)
  16   4    —       (4 pad bytes)
  20  12    3×f32   reflectivity (x,y,z)  avg color, tints bounced light
  32   4    —       (4 pad bytes)
  36   4    f32     bumpmap_scale
  40   4    int32   high-res image format ImageFormats enum index (§1.6)
  44   1    uint8   mipmap_count          number of mips in the pyramid
  45   4    int32   low-res image format  thumbnail format (usually DXT1)
  49   1    uint8   low-res width
  50   1    uint8   low-res height
 ───  ────  ──────  ────────────────────  ─────────────────────────────────────────────
   format string: '<I HH I H H 4x fff 4x f i B i BB'   (srctools _HEADER)
```

The Python attributes these land in (`vtf.py:759`): `width`, `height`, `flags`, `frame_count`,
`first_frame_index`, `reflectivity`, `bumpmap_scale`, `format`, `mipmap_count`, `low_format`,
low-res `width`/`height`. `version` is `(version_major, version_minor)`; `depth` defaults to 1 and is
only read for v7.2+ (`vtf.py:792`).

### 1.3 Flags (`VTFFlags`, `vtf.py:242`)

A bitfield; the ones that matter for static classification / rendering hints:

```
 0x00000001  POINT_SAMPLE        0x00001000  ONEBITALPHA   (auto, from data)
 0x00000002  TRILINEAR           0x00002000  EIGHTBITALPHA (auto)
 0x00000004  CLAMP_S             0x00004000  ENVMAP        ← cubemap; depth must be 1
 0x00000008  CLAMP_T             0x00008000  RENDER_TARGET
 0x00000010  ANISOTROPIC         0x00010000  DEPTH_RENDER_TARGET
 0x00000020  HINT_DXT5           0x00040000  SINGLE_COPY
 0x00000040  PWL_CORRECTED       0x00080000  PRE_SRGB
 0x00000080  NORMAL  (normal map)
 0x00000100  NO_MIP              0x04000000  VERTEX_TEXTURE
 0x00000200  NO_LOD              0x08000000  SS_BUMP
 0x00000400  ALL_MIPS            0x20000000  BORDER
 0x00000800  PROCEDURAL
```

`add_unknown(locals())` (`vtf.py:280`) auto-generates members for unused bits so unrecognized flags
round-trip. `ENVMAP` is the one to know: an env-mapped VTF stores **six cube faces** (RIGHT, LEFT,
BACK, FRONT, UP, DOWN — plus a 7th SPHERE face in <7.5), not depth slices. Cubemap `.vtf`s are what
get baked into the BSP pakfile after a `buildcubemaps`.

### 1.4 v7.3+ resource directory

```
 ┌───────────────────────────────────────────────┐
 │ 3 pad bytes                                     │
 │ num_resources (uint32)                          │   '<3xI8x' then 8 pad  (vtf.py:804)
 │ 8 pad bytes                                     │
 ├───────────────────────────────────────────────┤
 │ resource entry × num_resources, each 8 bytes:   │   '<3sBI'  (vtf.py:809)
 │   ┌──────────────────────────────────────────┐ │
 │   │ res_id  (3 bytes)  e.g. \x01\0\0 = LOW_RES│ │
 │   │ flags   (1 byte)   bit 0x02 = data inline │ │
 │   │ data    (uint32)   offset, OR inline value │ │
 │   └──────────────────────────────────────────┘ │
 └───────────────────────────────────────────────┘
```

Known resource IDs (`ResourceID`, `vtf.py:283`): `LOW_RES = \x01\0\0`, `HIGH_RES = \x30\0\0`,
`PARTICLE_SHEET = \x10\0\0`, `CRC`, `LOD`, `TSO` (extra flags), `KVD` (keyvalues). `LOW_RES`/
`HIGH_RES` are not stored as generic resources — their `data` field is the byte offset of the
thumbnail / main image (`vtf.py:814`). If a resource's `flags & 0x02` is **set**, `data` *is* the
value (inline uint32); otherwise `data` is an offset to `[uint32 size][bytes]` elsewhere in the file.

### 1.5 Mipmap packing — smallest first

The high-res image is the full mipmap pyramid, and srctools reads it **smallest mip → largest mip**:

```
 for data_mipmap in reversed(range(mipmap_count)):     # vtf.py:862
     mip_width  = max(width  >> data_mipmap, 1)
     mip_height = max(height >> data_mipmap, 1)
     for frame in range(frame_count):
         for depth_or_cube in depth_seq:               # volume slices OR cube faces
             <read frame_size(mip_w, mip_h) bytes>
             high_res_offset += frame_size(...)
```

So on disk, for a 256×256, 1-frame, non-cubemap texture with `mipmap_count = 9`:

```
 file order (after thumbnail):
   mip 8 (1×1) → mip 7 (2×2) → mip 6 (4×4) → … → mip 1 (128×128) → mip 0 (256×256)
   ▲ smallest read first                                            ▲ full-res last
```

Within a mip the loop nests **frame → (depth slice | cube face)**. The byte size of one mip image is
`ImageFormats.frame_size(w, h)` (`vtf.py:182`):

```
 compressed (DXT/ATI):  blocks_w = ceil(w/4); blocks_h = ceil(h/4)
                        bytes = size_bits * blocks_w * blocks_h / 8
 uncompressed:          bytes = size_bits * w * h / 8
```

(`size` is the per-block bits for compressed formats, per-pixel bits otherwise.)

### 1.6 Image-format enum

`format`/`low_format` are indices into `ImageFormats` (`vtf.py:112`), mapped back through
`FORMAT_ORDER` (`vtf.py:219`). The common Portal 2 values: `RGBA8888`, `BGR888`, `DXT1`, `DXT3`,
`DXT5`, `DXT1_ONEBITALPHA`, plus `NONE = -1` (the thumbnail may be `NONE`). srctools cannot decode
`RGBA16161616`/`RGBA16161616F` (16-bit HDR) — it returns metadata only for those (`vtf.py:853`).

> **For our recon:** the only VTFs in the test map's pakfile are baked cubemaps + `.vhv`. We do not
> decode pixels — for portalability we read the **baked surface flag** (`SURF_NOPORTAL`) and, when we
> need the source authoring intent, the `.vmt`'s `%compilenoportal` keyvalue (see
> [vmt_format.md](vmt_format.md)); we never need the VTF's actual texels. The VTF header is here for
> completeness and for any future "is this surface a screen / a sign / a decal" probe that needs
> dimensions or the `ENVMAP`/`NORMAL` flags.

---

## 2. MDL — the studio-model fileset

A "model" in Source is **not one file** — it is a set sharing a basename, where the `.mdl` is the
header/metadata and the geometry lives in siblings. srctools enumerates the extensions in
`MDL_EXTS` (`mdl.py:24`):

```
 props/.../button_base
   ├── button_base.mdl        studiohdr — metadata, materials, $keyvalues, surfaceprop, bone/bodypart counts
   ├── button_base.vvd        vertex data (positions, normals, UVs, tangents) — the raw verts
   ├── button_base.dx90.vtx   optimized triangle strips for a hardware level (.dx80.vtx, .sw.vtx variants)
   ├── button_base.phy        VPhysics collision hull + a $keyvalues block  (OPTIONAL)
   └── button_base.ani        externalized animation data                    (OPTIONAL)
```

```
            ┌─────────────┐    references materials    ┌──────────────────────────┐
            │   .mdl       │ ─ cdmaterials[] + skins[] → │ materials/<cd>/<tex>.vmt │ → .vtf
            │  studiohdr   │                            └──────────────────────────┘
            └──────┬──────┘
       offsets to ↓ siblings (engine loads these; the .mdl alone has no geometry)
         ┌─────────┼──────────┬──────────┐
       .vvd      .vtx       .phy       .ani
      (verts)  (strips)  (collision)  (anims)
```

srctools' `Model` class (`mdl.py:330`) parses **only the `.mdl` metadata** (and the `.phy` keyvalues
if present); it explicitly does not parse geometry or animation (`mdl.py:332`).

### 2.1 studiohdr — header fields srctools reads

`Model._load()` (`mdl.py:379`) reads, in order from offset 0:

```
 off    size  field            notes
 ─────  ────  ───────────────  ──────────────────────────────────────────────
   0     4    id   "IDST"       must equal b'IDST' (mdl.py:382)
   4     4    version (int32)   srctools accepts 44..49 (mdl.py:392); P2 = 48/49
   8     4    checksum (4s)     must match the .vvd/.vtx checksums at runtime
  12    64    name (64s)        internal model path, null-padded
  76     4    file_len (int32)
  80    12    eye_pos    (vec)  str_readvec
  92    12    illum_pos  (vec)
 104    12    hull_min   (vec)  collision/render bbox min   ← bounding box
 116    12    hull_max   (vec)  collision/render bbox max
 128    12    view_min   (vec)  clamping box
 140    12    view_max   (vec)
 152     4    flags (uint32)    Flags bitfield (§2.2)
 156   …      bone / bonecontroller / hitbox / anim / sequence counts+offsets
              texture / cdmaterials / skin-family / bodypart / attachment …
 ─────  ────  ───────────────  ──────────────────────────────────────────────
 308     4    surfaceprop_index → null-string  surfaceprop name  ← classification!
              keyvalue_index / keyvalue_count  → the $keyvalues block (§2.4)
              … mass, contents, includemodels, lod info …
```

srctools hard-asserts the cursor is at byte **308** before reading `surfaceprop_index`
(`mdl.py:474`) — a self-check that the variable-length count/offset blocks above were unpacked
correctly. The fields that matter for *classifying* a prop:

- **`version`** — 44–49 (Portal 2 ships 48 and 49). srctools validates the range.
- **`name`** — the model's own internal path string.
- **`hull_min` / `hull_max`** — the bounding box (use this for a size/shape prior).
- **`flags`** — see §2.2; `static_prop` (`1<<4`) confirms `$staticprop` (bones collapsed).
- **`surfaceprop`** — single null-terminated string at `surfaceprop_index` (`mdl.py:600`).
- **`keyvalues`** — the `$keyvalues` text block, read at `keyvalue_index` if `keyvalue_count` (`mdl.py:603`).
- **`cdmaterials` + `skins`** — material search folders + per-skin texture lists (`mdl.py:541`, `:576`).
- **`mass` / `contents`** — physics mass and content flags (`mdl.py:493`).

### 2.2 MDL `Flags` (`mdl.py:41`)

```
 1<<0  autogenerated_hitbox       1<<11 no_forced_fade
 1<<1  uses_env_cubemap (runtime) 1<<12 force_phoneme_crossfade
 1<<2  force_opaque  ($opaque)    1<<13 constant_directional_light_dot
 1<<3  translucent_twopass        1<<14 flexes_converted
 1<<4  static_prop  ($staticprop) 1<<15 built_in_preview_mode
 1<<5  uses_fb_texture            1<<16 ambient_boost
 1<<6  hasshadowlod               1<<17 do_not_cast_shadows
 1<<7  uses_bumpmapping (runtime) 1<<18 cast_texture_shadows
 1<<8  use_shadowlod_materials
```

Note `uses_env_cubemap` and `uses_bumpmapping` are **set at runtime, not stored in the file**
(`mdl.py:46`, `:56`) — don't trust them from a static parse.

### 2.3 The `.phy` collision sibling

Optional. `Model.__init__` looks for `<basename>.phy` (`mdl.py:372`) and, if present, parses its
header (`ST_PHY_HEADER = '<iiil'` = size, header_id, solid_count, checksum; `mdl.py:297`/`:790`),
skips the solid collision blobs, then reads a **trailing `$keyvalues` text block** into
`phys_keyvalues` (`mdl.py:800`). That keyvalues block often carries the model's collision
`prop_data` / mass / damage class — a second place to look for classification hints beyond the
`.mdl`'s own `$keyvalues`.

### 2.4 `$keyvalues` + `surfaceprop` — how you'd classify a prop

These two are the model-side classification signal:

```
 surfaceprop  (e.g. "metal", "flesh", "default")  → physics material / footstep / sound class
 $keyvalues   (free text block; common contents)  → "prop_data { base ... }", and for
              Portal-specific props things like editor metadata or weighted-cube tags
```

A prop being **a cube** vs **a button** vs **scenery** is read from a combination of: the **model
path** (`models/props/metal_box.mdl`, `models/props_underground/.../floor_button.mdl`), the
**`surfaceprop`**, and the **`$keyvalues`** block. The model path alone is usually decisive for the
PeTI stock set (the Puzzle Maker places a fixed, known set of models — see
[`brainstorm/puzzlemaker_elements.md`](../puzzlemaker_elements.md)); `surfaceprop`/`$keyvalues`
disambiguate variants.

---

## 3. Static props in the BSP — what we actually extract

`prop_static` instances are **not entities** — they live in the BSP's `sprp` game lump
(`LMP_ID_STATIC_PROPS = b'sprp'`, `bsp.py:63`), parsed into `StaticProp` objects (`bsp.py:1209`).
`dump_ents.py._static_props()` reads `bsp.props` and records four fields per prop:

```python
{'model': p.model, 'origin': _vec(p.origin), 'angles': _vec(p.angles), 'skin': p.skin}
```

```
 StaticProp (srctools, bsp.py:1209) — fields we use, and ones we ignore
 ─────────────────────────────────────────────────────────────────────────────
  model     str    "props/.../button_base.mdl"   ← the MDL path (the §2 fileset)
  origin    Vec    world position                ← _vec() → [x,y,z]
  angles    Angle  orientation (pitch,yaw,roll)  ← _vec() → [x,y,z]
  skin      int    skin-family index into MDL    ← selects which material set
 ─────────────────────────────────────────────────────────────────────────────
  scaling, visleafs, solidity, flags (StaticPropFlags), fade params,
  tint, renderfx, lightmap_x/y, dx/cpu/gpu levels — present, NOT read by us
```

`skin` is an index into the MDL's **skin families** (`Model.skins`, `mdl.py:576`): family 0 is the
default material set, family `k` swaps in alternate VMTs for the same geometry (e.g. a powered vs
unpowered look). So `(model, skin)` together pin the exact materials a prop renders with.

### 3.1 The version trap (the #1 ad-hoc-parser killer)

The `sprp` lump has a version number, **but several engine branches reuse the same number with a
different struct size**. srctools resolves the real layout by `(version, struct_size)`, not version
alone — `StaticPropVersion` (`bsp.py:402`) keys on both:

```
 (version, size) → variant            (bsp.py:417, _STATIC_PROP_VERSIONS at :455)
 ─────────────────────────────────────────────────────────────────────
 (4, 56)  V4         (7, 68)  V7        (10, 76) V10      (11, 80) V11
 (5, 60)  V5=DEFAULT (8, 68)  V8        (7, 72)  V_LIGHTMAP_v7
 (6, 64)  V6         (9, 72)  V9        (10, 72) V_LIGHTMAP_v10
                                        (12, 80) V_STRATA_V12  (13, 88) V_STRATA_V13
```

This is exactly why we use srctools rather than rolling a struct: **infer layout from per-prop byte
size, not the version field.** Our recon confirmed 159 static props parse cleanly on the v21 test
map; `dump_ents.py` wraps the prop read in a `try/except` (`dump_ents.py:114`) so an unrecognized
sprp variant on some other map degrades to an empty list rather than crashing the whole dump.

`StaticPropFlags` (`bsp.py:462`) carries render/lighting flags (`DOES_FADE`, `NO_SHADOW`,
`DISABLE_DRAW`, `BOUNCED_LIGHTING`, …); we don't read them today but they're available on the same
object if a future probe wants "is this prop invisible / non-shadowing."

---

## 4. The full name→pixels / name→geometry resolution chain

```
 a surface's material                       a static prop's model
 ────────────────────                       ──────────────────────
 face.texinfo.mat = "tile/white_floor..."    StaticProp.model = "props/.../x.mdl"
        │                                            │
        │  materials/<name>.vmt                      │  resolve .mdl + siblings via FileSystemChain
        ▼  (FileSystemChain: pakfile → VPKs)         ▼  (.vvd .vtx .phy share the basename)
   ┌──────────┐   $basetexture "tile/white..."  ┌──────────────┐  cdmaterials[] + skins[skin]
   │  .vmt    │ ──────────────────────────────▶ │  .mdl studio │ ───────────────────────────────▶ materials/<cd>/<tex>.vmt
   │ (vmt_…)  │   $compilenoportal / $surfprop  │  hdr (§2.1)  │   surfaceprop / $keyvalues
   └────┬─────┘                                 └──────────────┘
        │  materials/<tex>.vtf
        ▼  (FileSystemChain)
   ┌──────────┐
   │  .vtf    │  header §1.2 + mipmaps §1.5
   └──────────┘
```

Two independent name→file hops, both through the same `FileSystemChain` (pakfile first, then the
game VPKs). The VMT is the join node: a surface's VTF is reached *through* its VMT (§0,
[vmt_format.md](vmt_format.md)); a prop's VTFs are reached *through* its MDL's `cdmaterials`+`skins`,
which name VMTs, which name VTFs.

---

## 5. Future model-classification idea (not built)

Today we record only `(model, origin, angles, skin)` per static prop and never open the `.mdl`. The
natural next hop — and the reason this doc captures the MDL fields — is an **offline prop classifier**
that turns the model *path* into a *type label* (cube / button / scenery / sign / antline / etc.),
so the percept/annotation layer can say "prop 12 is a weighted cube at (x,y,z)" instead of an opaque
model path. The cheap-to-rich ladder:

1. **Path match only** (≈free, no file open) — regex/lookup over `StaticProp.model` against the known
   PeTI stock-model set. Decisive for the Puzzle Maker corpus, which uses a fixed model vocabulary.
2. **+ `surfaceprop` / MDL `$keyvalues`** — open the `.mdl` via the VPK reader, read `surfaceprop`
   (`mdl.py:600`) and the `$keyvalues` block (`mdl.py:603`) to disambiguate look-alike paths and skin
   variants. The `.phy` `phys_keyvalues` (`mdl.py:800`) is a fallback source for the same metadata.
3. **+ `hull_min`/`hull_max` size prior** — bbox dimensions as a shape tie-breaker.

This stays **offline + experimenter-side** (same discipline as the I/O causal graph): a static
*prior* the annotation/eval layer consumes, never a runtime authority — the engine remains the source
of truth for live entity state. It connects to the static-prop extraction we already do
([`dump_ents.py:103`](../../py/bsp_recon/dump_ents.py)) and the annotation gap noted in
[`brainstorm/offline_map_preprocessing.md`](../offline_map_preprocessing.md) §1.3 (the classname-walk
can't box surfaces/panels; a model classifier labels the prop side). It is **not in v0** — recorded
here so the seam is documented.
