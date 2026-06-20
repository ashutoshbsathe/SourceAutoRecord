# VPK (Valve Pak) archive format

Reference for the **VPK** archive format as the Source engine ships it, and as our
offline BSP recon tool reads it. VPK is how Portal 2 stores nearly all of its shared
content (materials, models, sounds, scripts) outside of any single `.bsp`. Our recon
tool needs VPKs because the data a map *references* but does not *embed* — most
notably the `.vmt` material definitions that carry portalability — lives in the game
VPKs, not in the map's own pakfile.

Everything below is grounded in the `srctools` implementation we actually call
(`srctools 2.7.0`, Python 3.14):
- `srctools/vpk.py` — the VPK reader/writer (`VPK`, `FileInfo`, header parse).
- `srctools/filesys.py` — `VPKFileSystem`, `ZipFileSystem`, `FileSystemChain`.

Sibling format docs:
[bsp_format.md](bsp_format.md) ·
[vmt_format.md](vmt_format.md) ·
[nut_vscript.md](nut_vscript.md) ·
[vtf_mdl.md](vtf_mdl.md)

---

## 1. What a VPK is, and the dir/archive split

A VPK is logically one big read-only filesystem (a flat tree of `folder/name.ext`
paths → file bytes). Physically it is split across two kinds of file on disk:

- **One directory file**, `<prefix>_dir.vpk` (e.g. `pak01_dir.vpk`). It holds the
  whole *index*: every filename, its CRC, where its bytes live, and optionally a
  small chunk of the file's leading bytes inlined right there ("preload").
- **Zero or more numbered archive files**, `<prefix>_NNN.vpk` (`pak01_000.vpk`,
  `pak01_001.vpk`, …), three-digit zero-padded. These are pure data blobs — no index,
  no header — just concatenated file contents that the directory file points into by
  `(archive_index, offset, length)`.

A single `.vpk` *can* also be self-contained (no numbered files), but Portal 2 is not:
it is a directory + many archives.

`srctools/vpk.py:get_arch_filename()` is the exact naming rule:

```
get_arch_filename('pak01', None) -> 'pak01_dir.vpk'      # the directory file
get_arch_filename('pak01', 0)    -> 'pak01_000.vpk'      # f'{prefix}_{index:>03}.vpk'
get_arch_filename('pak01', 173)  -> 'pak01_173.vpk'
```

`VPK.filename` decides dir-vs-singular by suffix: a name ending in `_dir.vpk` is a
directory VPK and `_dir_prefix` is the name minus those 8 chars (`vpk.py:filename`
setter, `filename[:-8]`).

### Portal 2 on disk (what our recon sees)

Confirmed on the install we use (`.../steamapps/common/Portal 2/portal2/`):

```
portal2/
  pak01_dir.vpk        3,817,845 bytes   <- the index (v1)
  pak01_000.vpk  ┐
  pak01_001.vpk  │  174 archive files: pak01_000.vpk .. pak01_173.vpk
   ...           │  (contiguous; each ~tens-to-hundreds of MB)
  pak01_173.vpk  ┘
portal2_dlc1/pak01_dir.vpk   <- DLC content overlays in their own dir VPK
portal2_dlc2/pak01_dir.vpk
```

So "the game VPKs" we resolve materials against are really three directory files
(`portal2`, `portal2_dlc1`, `portal2_dlc2`), each fronting its own pool of numbered
archives.

---

## 2. Header: VPK v1 vs v2

`srctools/vpk.py:load_dirfile()` reads the directory file's header. The first three
little-endian `uint32`s are common to both versions:

```
offset  size  field        meaning
 0      4     signature    must be 0x55aa1234  (VPK_SIG in vpk.py)
 4      4     version      1 or 2 (anything else raises)
 8      4     tree_length  byte length of the directory tree that follows the header
```

read in srctools as `vpk_sig, version, tree_length = struct_read('<III', dirfile)`.

If `version >= 2`, **four more** `uint32`s follow (`struct_read('<4I', ...)`):

```
offset  size  field          (v2 only)
12      4     data_size      embedded file-data chunk size
16      4     ext_md5_size   per-archive MD5 section size
20      4     dir_md5_size   directory MD5 section size
24      4     sig_size       signature (public-key) section size
```

srctools reads these four into `data_size, ext_md5_size, dir_md5_size, sig_size` and
otherwise ignores them on read (they trail the data and are integrity/signing
metadata, not part of path resolution). After the header, srctools computes:

```
header_len = dirfile.tell() + tree_length
            \________________/   \_________/
            12 (v1) or 28 (v2)    the tree
```

and parses the tree until `tell()` reaches `header_len`.

### Portal 2 fact

Portal 2's `pak01_dir.vpk` is **VPK v1**. Verified by reading its first 12 bytes:

```
signature = 0x55aa1234   version = 1   tree_length = 3,817,833
```

Note `tree_length (3,817,833) + header (12) = 3,817,845` = the whole file. The entire
`_dir.vpk` is header + tree, with **no footer data** at the end — every file's bytes
live out in the numbered archives, not inlined. (See §6 on why that matters: it means
preload chunks are empty for Portal 2, so reading a `.vmt` always touches one
`pak01_NNN.vpk`.)

---

## 3. The directory tree: extension → folder → filename

The tree after the header is a 3-level nesting of **null-terminated ASCII strings**,
where an empty string (a lone `\x00`) terminates the current level. The nesting order
is extension first, then directory, then filename — so all `.vmt` files are grouped,
then within that all files in `materials/metal/`, etc.

```
TREE  (null-terminated strings; "" closes a level)
+-----------------------------------------------------------+
| ext   = "vmt"                                             |
|   dir   = "materials/metal"                               |
|     file = "black_wall_metal_002c"   <FileEntry>          |
|     file = "..."                     <FileEntry>          |
|     file = ""   (end of files in this dir)                |
|   dir   = "materials/tile"                                |
|     file = "white_floor_tile002a"    <FileEntry>          |
|     file = ""                                             |
|   dir   = ""    (end of dirs for this ext)                |
| ext   = "vtf"                                             |
|   dir   = "materials/metal"                               |
|     ...                                                   |
| ext   = ""      (end of tree)                             |
+-----------------------------------------------------------+
```

Two srctools quirks worth knowing (`vpk.py:iter_nullstr`, `_write_nullstring`):

- A path component that is *genuinely* one space character is stored as `" "` and
  decoded back to `''` (a real empty path / root). An empty string in the stream means
  end-of-level. This is how a file at the VPK root (no folder) is represented.
- The full filename is reassembled as `_join_file_parts(dir, name, ext)` →
  `"{dir}/{name}.{ext}"`, e.g. `materials/metal/black_wall_metal_002c.vmt`.

So to look up a material you don't search bytes — srctools has already walked the whole
tree into a nested dict `_fileinfo[ext][dir][name] -> FileInfo` (and
`VPKFileSystem` additionally builds a casefolded `name_to_file` flat map for
case-insensitive lookup).

---

## 4. A per-file entry (the `FileEntry` struct)

Immediately after each filename string comes a fixed 18-byte record, then `preload`
bytes inlined right there. srctools parses it with `struct.Struct('<IHHIIH')`
(`vpk.py:load_dirfile`):

```
FileEntry  (18 bytes, little-endian '<IHHIIH')   then `preload_length` bytes follow
+--------+------+--------------------+-------------------------------------------+
| offset | size | field              | meaning                                   |
+--------+------+--------------------+-------------------------------------------+
|  0     |  4 I | crc                | CRC32 of the *whole* file contents        |
|  4     |  2 H | preload_length     | bytes inlined into _dir right after this  |
|  6     |  2 H | archive_index      | which pak01_NNN.vpk; 0x7fff = in _dir     |
|  8     |  4 I | entry_offset       | byte offset of data within that archive   |
| 12     |  4 I | entry_length       | byte length of data in the archive        |
| 16     |  2 H | terminator         | must be 0xffff (else "bad terminator")    |
+--------+------+--------------------+-------------------------------------------+
| 18     |  N   | preload bytes      | N = preload_length, copied verbatim       |
+--------+------+--------------------+-------------------------------------------+
```

How srctools maps these onto a `FileInfo` (`vpk.py:load_dirfile`):

- `archive_index == 0x7fff` (`DIR_ARCH_INDEX`) → `arch_index = None`, i.e. the file's
  archive bytes (if any) live **inside `_dir.vpk` itself**, in the footer region after
  the tree. `entry_offset` is then relative to that footer (`VPK.footer_data`).
- `entry_length == 0` → there are no archive bytes at all; the entire file is the
  preload chunk, and `offset` is forced to 0.
- The trailing `preload_length` bytes are read straight from the dir file and stored as
  `FileInfo.start_data`.

`FileInfo` field names (constructed at `vpk.py:457`):
`(vpk, dir, filename, ext, crc, arch_index, offset=entry_offset, arch_len=entry_length, start_data=preload)`.

---

## 5. Where a file's bytes live: preload + archive split

A file's full contents are `start_data + archive_bytes`. Either part can be empty.
`FileInfo.read()` (`vpk.py:174`) is the whole story:

```
FileInfo.read():
    if arch_len == 0:                 # entire file is inline preload
        return start_data
    if arch_index is None:            # archive bytes live in _dir's footer
        return start_data + dir_footer[offset : offset+arch_len]
    else:                             # archive bytes live in pak01_<index>.vpk
        open  pak01_<arch_index:03>.vpk
        seek  offset
        return start_data + read(arch_len)
```

Visually, the three storage cases:

```
(a) small file, fully inlined        (b) split: preload in _dir, body in archive
    pak01_dir.vpk                         pak01_dir.vpk            pak01_017.vpk
    +----------------------+              +-------------------+    +-----------------+
    | FileEntry (18B)      |              | FileEntry (18B)   |    | ...other files  |
    | preload = WHOLE file |              | preload = head    |    | [offset]        |
    +----------------------+              +-------------------+    | body (arch_len) |
    arch_len = 0                          arch_index=17       ---> | ...             |
                                          offset, arch_len         +-----------------+
                                          read() = preload + body

(c) Portal 2's actual case: preload_length = 0, body entirely in pak01_NNN.vpk
    pak01_dir.vpk                                        pak01_NNN.vpk
    +-------------------+                                +-----------------+
    | FileEntry (18B)   |  arch_index=N, offset, arch_len| [offset]        |
    | preload = (none)  | ------------------------------>| file bytes      |
    +-------------------+                                +-----------------+
```

The preload mechanism lets the engine grab a file's header (e.g. a VTF's mip
descriptor) from the already-resident directory without opening an archive. For Portal
2's `pak01_dir.vpk` the preload chunks are empty (the dir is pure index, §2), so each
`materials/<name>.vmt` read always opens exactly one `pak01_NNN.vpk` and reads
`arch_len` bytes at `offset`. That's cheap and the whole index is in RAM, so resolving
a map's ~44 materials against the game VPKs is fast even though the archives total
several GB.

---

## 6. How srctools exposes a VPK as a filesystem

We never index VPK structs by hand — we use srctools' filesystem layer
(`srctools/filesys.py`):

- `VPK(path)` (`vpk.py`) parses one `_dir.vpk` into `_fileinfo[ext][dir][name]`.
- `VPKFileSystem(path)` (`filesys.py:663`) wraps a `VPK` and exposes a flat,
  **case-insensitive** path API. On construction it builds
  `name_to_file = { file.filename.casefold(): FileInfo }`. `FileInfo` is imported there
  under the alias `VPKFile` (`from srctools.vpk import VPK, FileInfo as VPKFile`), so
  `open_bin(name)` ultimately calls `FileInfo.read()` from §5 and hands back a
  `BytesIO`. Lookups casefold and `\\ -> /` normalize the name first.
- `ZipFileSystem(path_or_zip)` (`filesys.py:591`) does the same over a ZIP archive.
  The BSP **pakfile lump is an embedded ZIP**, so `ZipFileSystem(bsp.pakfile)` makes
  the map's embedded assets look like the same kind of filesystem.

```
VPK file on disk ──► VPK (vpk.py) ──► VPKFileSystem (filesys.py) ──► open_bin(name)
                     _fileinfo            name_to_file               -> BytesIO(FileInfo.read())
                     [ext][dir][name]     (casefolded, '/'-normalized)
```

---

## 7. The FileSystemChain search-path model (how we resolve a material)

`FileSystemChain` (`filesys.py:294`) chains several filesystems into one prioritized
whole. `_get_file(name)` tries each child **in order** and returns the first hit
(`filesys.py:352`):

```
FileSystemChain._get_file(name):
    for (sys, prefix) in self.systems:     # in insertion order
        full = (prefix + '/' + name) normalized
        try:    return sys._get_file(full) # FIRST match wins
        except FileNotFoundError: continue
    raise FileNotFoundError(name)
```

So order = priority. The chain we build for material resolution puts the map's own
embedded pakfile first, then the game VPKs:

```
FileSystemChain(
    ZipFileSystem(bsp.pakfile),                  # 1. map-embedded assets (highest priority)
    VPKFileSystem(".../portal2/pak01_dir.vpk"),  # 2. base game content
    VPKFileSystem(".../portal2_dlc1/pak01_dir.vpk"),  # 3. DLC overlays
    VPKFileSystem(".../portal2_dlc2/pak01_dir.vpk"),
)
```

Pakfile-first mirrors the engine's own behavior: a map may ship a custom override of a
stock material in its pakfile, and that must win over the base-game copy. For a
PeTI-vanilla chamber the pakfile holds only cubemap `.vtf` and `.vhv` vertex-lighting
(no `.vmt`), so material lookups fall through to the VPKs — but the ordering is still
correct in general.

### The full `materials/<name>.vmt` resolution chain

Tying it all together — going from a baked texinfo material name on a BSP face to the
shader keyvalues that tell us portalability:

```
BSP face.texinfo.mat                e.g. "metal/black_wall_metal_002c"
        │  (already lowercase, no "materials/" prefix, no ".vmt")
        ▼  prepend "materials/", append ".vmt"
"materials/metal/black_wall_metal_002c.vmt"
        │
        ▼  FileSystemChain._get_file()  — pakfile, then portal2, then dlc1/dlc2
   VPKFileSystem hit -> FileInfo (arch_index N, offset, len)
        │
        ▼  open_bin() -> FileInfo.read()  (open pak01_<N>.vpk, seek, read)
   raw .vmt text bytes
        │
        ▼  srctools Material.parse(...) + apply_patches(fsys)
   resolved shader keyvalues:  shader=LightmappedGeneric,
                               $surfaceprop=..., %compilenoportal=0/1
```

The `apply_patches` step matters because many stock `.vmt`s are `patch` shaders that
`include` a base `.vmt` and override a couple of keys; resolving the patch requires the
same `FileSystemChain` to fetch the included base file (which may itself be in a
different VPK). See [vmt_format.md](vmt_format.md) for the `patch`/`%compilenoportal`
details.

> Note: for **static** surfaces our recon does not actually need this chain at all —
> portalability is **baked** into `face.texinfo.flags` as `SURF_NOPORTAL` at compile
> time (`%compilenoportal` in the source VMT → flag), and `dump_ents.py`'s
> `extract_geometry()` reads it directly (`not (ti.flags & SurfFlags.NOPORTAL)`). The
> VMT/VPK chain is for *non-baked* questions (the authored shader, `$surfaceprop`, the
> `%compilenoportal` source value, dynamic/flip-panel faces) where the baked flag isn't
> the final word. See [bsp_format.md](bsp_format.md) for the texinfo/surface-flag path.

---

## 8. Gotchas / invariants

- **Read-only for us.** srctools *can* write VPKs, but only v1
  (`write_dirfile` raises `NotImplementedError` for v2). We only ever read.
- **Case-insensitive, forward-slash paths.** `VPKFileSystem` casefolds and converts
  `\\ -> /` on every lookup; build names with `/` and don't worry about case.
- **`0x7fff` ≠ "no data".** `archive_index == 0x7fff` means "bytes are in `_dir`'s
  footer," which is different from `entry_length == 0` ("file is entirely preload").
  Portal 2 uses neither (everything is in numbered archives).
- **Numbered archives have no header.** They are raw concatenations; without the
  `_dir.vpk` index they are unreadable. Don't try to parse a `pak01_017.vpk` standalone.
- **`tree_length` bounds the parse.** srctools stops the tree walk exactly when
  `tell() == header(12 or 28) + tree_length`; anything after is footer/data/integrity
  sections.
