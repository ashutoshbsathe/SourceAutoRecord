# VMT (Valve Material Type) format

Reference for the `.vmt` material files our offline BSP recon tool
([`py/bsp_recon/dump_ents.py`](../../py/bsp_recon/dump_ents.py)) resolves through the
Portal 2 VPK chain. Every fact below was checked against the installed `srctools` 2.7.0
source (`.venv/.../srctools/vmt.py`, `surfaceprop.py`, `const.py`, `filesys.py`); no byte
offsets or constants are quoted from memory.

Sibling docs: [bsp_format.md](bsp_format.md) (where face → texinfo → material name comes
from, and where the *baked* `SURF_NOPORTAL` flag lives), [vpk_format.md](vpk_format.md)
(the archive a `.vmt` is read out of), [nut_vscript.md](nut_vscript.md) (the runtime layer
a static parse can't see), [vtf_mdl.md](vtf_mdl.md) (the `.vtf` textures a `.vmt`
references and the `.mdl` props that carry their own materials).

The one-line takeaway, stated up front because it changes how much of this doc matters in
practice: **for static world surfaces we almost never need to read the VMT at all** —
portalability is *baked into the BSP face flags at compile time*, so the recon tool reads a
single bit (`SURF_NOPORTAL`) straight from the `.bsp` and skips material I/O entirely. The
VMT machinery below is the *authoring-side ground truth* (the compile flag the artist set,
the surface physics, the shader) and the fallback for anything not pre-baked.

---

## 1. What a VMT is

A `.vmt` is a **KeyValues text file** (the same nested `"key" "value"` / `{ … }` grammar
used by BSP entity lumps, `.vmf` maps, and game scripts). It has a fixed two-level shape:

- The **root key is the shader name** (e.g. `LightmappedGeneric`). It is the first string
  token in the file — `srctools` literally requires this (`Material.parse`: "look for the
  shader name — which must be the first string in the file").
- Its single value is a **brace block of shader parameters** (`$basetexture`, `$surfaceprop`,
  …) plus optional sub-blocks (`Proxies`, fallback shader blocks).

```
 file: materials/tile/white_floor_tile002a.vmt
 ┌──────────────────────────────────────────────┐
 │  "LightmappedGeneric"        <- shader (root)  │
 │  {                                             │
 │      "$basetexture"  "tile/white_floor_..."    │  <- $param  -> .vtf texture
 │      "$surfaceprop"  "tile"                     │  <- $param  -> physics class
 │      "$detail"       "detail/..."              │
 │      "%keywords"     "portal2"                  │  <- %param  -> compile-time only
 │      "Proxies"                                  │  <- sub-block (runtime var anim)
 │      {                                          │
 │          "TextureScroll" { ... }               │
 │      }                                          │
 │  }                                             │
 └──────────────────────────────────────────────┘
```

Parameter naming conventions (all case-insensitive — `srctools` `casefold()`s every key):

```
 $name   shader variable  -- consumed by the renderer at runtime ($basetexture, $color)
 %name   compile pragma    -- consumed by VBSP/VRAD at MAP COMPILE, then DISCARDED
 #name   rarely used; same KeyValues grammar
```

The `%`-prefixed pragmas are the ones the map compiler acts on and then *throws away*: by
the time you have a shipped `.bsp`, a `%compile*` flag has already been turned into a face
flag or a brush content flag (see §5). The runtime engine never sees `%compilenoportal`; it
sees `SURF_NOPORTAL` on the face.

---

## 2. The `srctools.Material` API (verified from `vmt.py`)

`Material` subclasses `MutableMapping[str, str]`, so it behaves like a dict of shader
params with these fields:

```
 Material
 ├─ .shader   : str             root key, e.g. "LightmappedGeneric" / "patch"
 ├─ mapping   : "$param" -> str  via __getitem__/__setitem__ (keys casefolded internally,
 │                               original case preserved in a Variable(name, value))
 ├─ .proxies  : list[Keyvalues] parsed "Proxies { … }" sub-block (one Keyvalues per proxy)
 └─ .blocks   : list[Keyvalues] any OTHER sub-block (fallback shaders, patch insert/replace)
```

Key methods we rely on:

- `Material.parse(data, filename='') -> Material` — tokenises the VMT text. It reads the
  shader name (first string), expects `{`, then loops params. A bare name followed by a
  newline (`%compilenodraw`) is stored with value `''`; a bare name followed by `{` becomes
  a sub-block — `proxies` → `self.proxies`, anything else → `self.blocks`. Flag tokens
  (`[$lowend]` style `PROP_FLAG`) are skipped. (verified: `Material.parse`, lines that
  branch on `Tok.NEWLINE` / `Tok.BRACE_OPEN`.)
- `Material.apply_patches(fsys, *, limit=100, parent_func=None) -> Material` — see §4. This
  is the call that turns a `patch` shader into the real material by reading its `include`
  off `fsys`.
- mapping access: `mat['$surfaceprop']`, `'$translucent' in mat`, `mat.get('$basetexture')`.
  `__contains__`/`__getitem__`/`__delitem__` all casefold the key, so case never matters.

There is no separate "load this VMT by name" helper on `Material`; **you** open the file
off a `FileSystem` and feed the text stream to `Material.parse`. That is exactly what the
recon resolution chain in §3 does.

---

## 3. Material name → VMT file → VPK chain resolution

A BSP face stores a **material name** in its texinfo, *not* a file path
(`bsp.faces[i].texinfo.mat`, surfaced as `material` in our `.geo.json`). The name is the
path under `materials/`, **without** the `materials/` prefix and **without** the `.vmt`
extension, using forward slashes:

```
 texinfo material name           VMT file actually read
 ───────────────────────────     ────────────────────────────────────────
 "tile/white_floor_tile002a"  →  materials/tile/white_floor_tile002a.vmt
 "metal/black_wall_metal_002c" → materials/metal/black_wall_metal_002c.vmt
 "TOOLS/TOOLSNODRAW"          →  materials/tools/toolsnodraw.vmt   (case-insensitive)
```

Those `.vmt` files do **not** live in the map. Stock Portal 2 materials live in the game's
VPK archives; only map-specific content (custom textures, cubemap `.vtf`s, `.vhv` vertex
lighting) is in the BSP's own embedded ZIP pakfile. So to read an arbitrary surface's VMT
we build a **`FileSystemChain`** and look up `materials/<name>.vmt` through it:

```
 FileSystemChain  (srctools.filesys; searches members in order, first hit wins)
 ┌─ ZipFileSystem(bsp.pakfile)          map-embedded ZIP   -- custom / per-map materials
 ├─ VPKFileSystem(portal2/pak01_dir.vpk)        stock Portal 2 content
 ├─ VPKFileSystem(portal2_dlc1/pak01_dir.vpk)
 └─ VPKFileSystem(portal2_dlc2/pak01_dir.vpk)
        │
        │  fsys["materials/tile/white_floor_tile002a.vmt"]
        ▼
   File  → .open_str()  → text stream
        │
        ▼
   Material.parse(stream, filename) ──► apply_patches(fsys) ──► resolved Material
```

`ZipFileSystem` first puts per-map overrides ahead of stock content (a map can ship a
material that shadows the game's). `VPKFileSystem` is pointed at the **`pak01_dir.vpk`**
directory file; it transparently reads file *bodies* out of the sibling numbered archives
(`pak01_000.vpk` … there are ~170 of them) — the split is internal to the VPK format and
explained in [vpk_format.md](vpk_format.md). `FileSystemChain._get_file` "searches for a
file on each filesystem in turn" (verified, `filesys.py`).

> Our shipped `dump_ents.py` does **not** currently build this chain — `extract_geometry`
> reads portalability from the baked flag instead (§6), which is why no VMT I/O shows up in
> the tool today. This chain is the resolution path for any future per-surface material
> attribute we *can't* get from a baked flag (e.g. `$surfaceprop`, shader name, translucency
> for a specific face), and the path the design doc reserves for the affordance prior.

---

## 4. Patch materials and `apply_patches`

A huge fraction of stock materials are not authored directly — they are thin **patch
materials**: a VMT whose shader is the literal string `patch`, which `include`s a base VMT
and overrides a few params. Source uses these so e.g. a hundred near-identical tile variants
share one base and only differ in `$basetexture`.

```
 materials/.../some_variant.vmt          materials/.../base.vmt  (the include target)
 ┌─────────────────────────────────┐     ┌──────────────────────────────────────┐
 │ "patch"                          │     │ "LightmappedGeneric"                 │
 │ {                                │     │ {                                    │
 │   "include"                      │     │   "$basetexture" "tile/old"          │
 │     "materials/.../base.vmt"     │ ─── │   "$surfaceprop" "tile"              │
 │   "replace"                      │ inc │   "%compilenoportal" "0"            │
 │   {                              │ lude│ }                                    │
 │     "$basetexture" "tile/new"    │     └──────────────────────────────────────┘
 │   }                              │
 │   "insert"                       │
 │   {                              │
 │     "$detail" "detail/x"         │
 │   }                              │
 │ }                                │
 └─────────────────────────────────┘
```

`Material.apply_patches(fsys)` resolves this (verified from `_apply_patch` in `vmt.py`):

```
 apply_patches(fsys, limit=100, parent_func=None)
   │  shader != "patch"  ────────────────────────────────►  return self  (nothing to do)
   │  shader == "patch":
   ├─ filename = self["include"]              (KeyError if no include  -> ValueError)
   ├─ parent = Material.parse(fsys[filename]) (FileNotFound -> ValueError "does not exist")
   ├─ parent = parent._apply_patch(...)       RECURSE: include can point at another patch
   │           (depth > limit  -> RecursionError "Parsed too deep a Patch tree!")
   ├─ copy   = parent's params + proxies + blocks   (start from the fully-resolved base)
   └─ for each top-level block in the patch:
        ├─ "replace" block:  set key ONLY IF key already exists in copy   (else skipped)
        ├─ "insert"  block:  always set the key
        ├─ value == ""    :  DELETE the key from copy
        └─ "proxies" child:  append proxy blocks to copy.proxies
        returns copy
```

Semantic distinctions worth remembering:

- **`replace` only overrides params that already exist** in the resolved base; a `replace`
  of a param the base never defined is silently dropped. **`insert` always adds.** (This is
  the exact `always_add` branch in `_apply_patch`.)
- An **empty-string value deletes** the inherited param.
- Patches **chain**: an `include` may itself point at another `patch`, resolved recursively
  up to `limit` (default 100), raising `RecursionError` past that.
- `parent_func` is an optional callback invoked with each included VMT's path — useful if we
  ever want to log which base materials a map's surfaces actually pull in.

The upshot for recon: **always call `apply_patches(fsys)` after `parse`** before reading any
`$param`, or you'll read `patch`/`include` instead of the real shader and its values.

---

## 5. Params we care about, and the `%compile*` flags

### Shader params (`$`)

| param            | meaning                                                              | recon use |
|------------------|----------------------------------------------------------------------|-----------|
| `$basetexture`   | path to the diffuse `.vtf` (under `materials/`, no ext)              | links to [vtf_mdl.md](vtf_mdl.md) |
| `$surfaceprop`   | name of the physics surface class (`tile`, `metal`, `glass`, …)      | §5.1 — friction / sound / game-material |
| `$translucent`   | `1` = alpha-blended; mirrors the `SURF_TRANSLUCENT` (0x10) face flag | translucency / visibility heuristics |
| `$bumpmap`       | normal map `.vtf`                                                     | — |
| `$detail`        | detail-texture overlay                                               | — |

Common shaders (the root key), and what they signal:

```
 LightmappedGeneric   brush/world surface, baked lightmaps   <- the overwhelmingly common
                                                                world-geometry shader (our
                                                                test map: walls/floors)
 UnlitGeneric         fullbright, no lighting (HUD, tool textures, glows)
 VertexLitGeneric     model surfaces lit per-vertex          <- props/.mdl (vtf_mdl.md)
 Water                water surface (refraction/reflection)
 WorldVertexTransition blended world surface (e.g. two-tex floor blends)
 Refract / Sprite / Cable / SpriteCard / patch  ...others
```

### Compile pragmas (`%compile*`) → baked BSP flags

These are the load-bearing ones for us. VBSP reads them off the brush-side material at
compile and turns them into face surface flags / brush content flags, then discards the
pragma. The `SurfFlags` bit values are verified from `srctools/const.py` (`SurfFlags(Flag)`,
the `SURF_*` flags):

```
 VMT pragma            authoring intent                         baked into the BSP as
 ────────────────────  ───────────────────────────────────────  ─────────────────────────────
 %compilenoportal      portal gun can't stick here               SURF_NOPORTAL  = 0x20  (face)
 %compileclip          invisible player clip brush               CONTENTS_PLAYERCLIP   (brush)
 %compileskip / nodraw face removed / invisible                   SURF_SKIP 0x200 / NODRAW 0x80
 %compilesky / 2dsky   skybox surface                            SURF_SKYBOX_3D 0x4 / 2D 0x2
 %compilehint          vis hint brush (compile-time vis tuning)   SURF_HINT      = 0x100
 %compiletrigger       brush is a trigger volume                  SURF_TRIGGER   = 0x40
```

For completeness, the full `SurfFlags` set in `const.py` (used by our `flags` field in
`.geo.json`): `LIGHT 0x1, SKYBOX_2D 0x2, SKYBOX_3D 0x4, WATER_WARP 0x8, TRANSLUCENT 0x10,`
`NOPORTAL 0x20, TRIGGER 0x40, NODRAW 0x80, HINT 0x100, SKIP 0x200, NOLIGHT 0x400,`
`BUMPLIGHT 0x800, NO_SHADOWS 0x1000, NO_DECALS 0x2000, NO_SUBDIVIDE 0x4000, HITBOX 0x8000`.

The two materials we traced on the test map:

```
 metal/black_wall_metal_002c.vmt  ──  %compilenoportal 1  ──►  face has SURF_NOPORTAL set
                                                                 => NOT portalable (black wall)

 tile/white_floor_tile002a.vmt    ──  (no %compilenoportal) ──►  face has no NOPORTAL bit
                                                                 => portalable (white tile)
```

### 5.1 `$surfaceprop` and surfaceproperties (verified from `surfaceprop.py`)

`$surfaceprop` names a row in the game's `scripts/surfaceproperties*.txt` manifest, parsed
by `SurfaceProp.parse_file` / `parse_manifest`. A `SurfaceProp` carries physics + audio
fields (`friction`, `elasticity`, `density`, footstep/impact sound names) and a
`gamematerial: SurfChar` — a single-character code (`T`=tile, `M`=metal, `C`=concrete,
`G`=grate, `Y`=glass, `W`=wood, …; full enum in `surfaceprop.py`). Surfaceprops inherit:
each definition can name a `base`, and unset fields fall through to the parent (or to a
synthesised `default`). We don't read surfaceprops in the current tool, but it's the
authoritative source if we ever want "is this floor metal vs tile" for an affordance prior
rather than relying on the texture name string.

---

## 6. Why the VMT read is *usually unnecessary* for static surfaces

This is the whole reason `dump_ents.py` doesn't open materials at all for geometry. The
question we actually want answered per face — **"can a portal stick here?"** — was already
answered by the map compiler and frozen into the BSP face's surface flags. So instead of
the §3 chain (open VPK → parse VMT → apply patches → read `%compilenoportal`), the recon
tool reads one bit off the face:

```
 dump_ents.py :: extract_geometry()           (verified from py/bsp_recon/dump_ents.py)
 ─────────────────────────────────────────────────────────────────────────────────────
 from srctools.bsp import SurfFlags
 for f in bsp.faces:
     ti = f.texinfo
     portalable = not (ti.flags & SurfFlags.NOPORTAL)      # <- single baked bit, no VMT I/O
     ... material = ti.mat                                  #    name kept for reference only
```

Trade-off, stated honestly:

- **Baked flag (what we ship):** O(1) per face, no filesystem, no VPK mounting, no patch
  resolution; reflects exactly what VBSP compiled (the *realized* portalability of the
  shipped map). Cannot recover an authoring intent the compiler dropped, and gives only the
  portal bit — not `$surfaceprop`, shader, or translucency-as-authored.
- **VMT read (the §3 fallback):** gives the full material (surfaceprop, shader, every
  `$param`), and the *source* `%compile*` intent — but costs VPK mounting + a text parse +
  recursive patch resolution per distinct material, and for portalability would just
  reproduce a bit we already have for free.

So the rule of thumb in the recon tool: **read the baked face flag for portalability;
reach for the VMT chain only when you need a material attribute that was never baked into
the BSP** (surface physics class, shader identity, per-face translucency authored but not
flagged, etc.).

---

## 7. Material proxies (brief)

A `Proxies { … }` sub-block defines **runtime-animated shader variables** — small scripted
controllers (`TextureScroll`, `Sine`, `LinearRamp`, `AnimatedTexture`, `Equals`) that
mutate a `$param` every frame (scrolling conveyor textures, pulsing glows). `srctools` parses
them into `Material.proxies` as a list of `Keyvalues` blocks but **does not execute them** —
they are pure runtime render behaviour. For static recon they are **noise**: a proxy can
animate `$basetexture` or a color at runtime, which is invisible to (and irrelevant for) our
static structural parse — analogous to the VScript runtime-rewiring blind spot described in
[nut_vscript.md](nut_vscript.md). We record nothing from them.

---

## Summary of srctools facts grounded here

- `vmt.py`: `Material(MutableMapping)` with `.shader/.proxies/.blocks`; `parse()` requires
  the shader name as the first string then a `{` block; `apply_patches()`/`_apply_patch()`
  resolves `patch` shaders via `fsys[include]` with `replace` (override-if-exists) vs
  `insert` (always-add), empty-string deletion, recursive `include` up to `limit=100`.
- `const.py` `SurfFlags(Flag)`: `NOPORTAL = 0x20`, plus the full `SURF_*` bit table quoted
  in §5; `surfaceprop.py` `SurfaceProp`/`SurfChar` for `$surfaceprop` physics classes;
  `filesys.py` `FileSystemChain`/`VPKFileSystem`/`ZipFileSystem` for the materials lookup.
- `py/bsp_recon/dump_ents.py` `extract_geometry()` reads `portalable = not (ti.flags &
  SurfFlags.NOPORTAL)` straight off the baked face flag — confirming §6's "VMT read usually
  unnecessary" claim against our actual code.
