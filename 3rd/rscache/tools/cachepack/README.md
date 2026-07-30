# cachepack — the shape of an unpacked cache

`cachepack unpack` turns an OldSchool cache into an editable tree; `cachepack pack`
turns that tree back into a cache. This document is about the **layout** the tree
has and why each table gets the layout it does. For the architecture — which
LostCity tool each driver corresponds to, and why the opcode readers are rscache's
rather than this tool's — see the header of `cachepack.h`.

    cachepack unpack --cache cache.osrs239 --rev osrs239 --src <tree> --assets
    cachepack pack   --src <tree> --out cache.mine --base cache.osrs239 --assets
    cachepack verify --cache cache.osrs239 --rev osrs239 --src <tree> --assets

---

## Three levels of index, and only three

Everything in the tree is addressed by a number the client's cache fixes, and every
one of those numbers is written down in a file. Nothing is recovered by listing a
directory or by parsing a filename.

**1. `pack/<ns>.pack` — archives.** One line per archive, `id=name`, for every
config type and every asset table. This is the id authority: `cp_assets_import`
walks the pack, so an archive with no pack line is not written. One file per
namespace, hand-ownable — comments and blank lines survive a rewrite, and a save
merges rather than truncating (`lc_pack.h`).

```
pack/interface.pack     12=bankmain
pack/texture.pack       0=texture_0
pack/model.pack         0=npc/royal_dwarf_citizen1_head
```

**2. A member index — the files inside one archive.** A multi-file archive needs
something to say which member is which, and it sits *beside the archive* in the
table's own folder:

| index | when | contents |
|---|---|---|
| `<archive>.compack` | the members have a text form, so they are blocks in one file | `<file id>=<block name>` |
| `<archive>.filepack` | they do not, so they are files | `<file id>=<path under the table's folder>` |
| `pack.meta` | a sprite pack, whose "members" are frames of one member | `count=`, `sprite<N>=…` |

All three are plain pack files, so `lc_pack_*` reads and writes them and they
inherit comment preservation and merge semantics. **Ascending ids are the order the
container gets**, which is the order its payload and the reference table's child
list must agree on.

**3. `configs/all.<type>` — config records.** `[name]` blocks of `key=value`, one
per record, resolved against `pack/<type>.pack`.

---

## Every table

| table | on disk | member index |
|---|---|---|
| `interface` | `interfaces/bankmain.if` — one block per component | `interfaces/bankmain.compack` |
| `texture` | `textures/texture_0.texture` — one block per material | `textures/texture_0.compack` |
| `map` | `maps/m15_32.jm2` — terrain and locs as text | `maps/m15_32.filepack` → `maps/m15_32/2.bin` |
| `dbindex` | `dbindex/dbindex_0/0.dbidx` — one file per column | `dbindex/dbindex_0.filepack` |
| `worldmaparea` | `worldmap/areas/details.wma` — a block per map | `details.compack` |
| ” | `worldmap/areas/compositetexture/main.png` | `compositetexture.filepack` |
| `sprite` | `sprites/<name>/0.bmp` — one BMP per frame | `sprites/<name>/pack.meta` |
| `script` | `scripts/<name>.cs2`, or `.bin` where it does not decompile | — one payload |
| `model` | `models/npc/goblin.model` | — one payload |
| `animset` | `animsets/animset_0.anim` | — one payload |
| `base` | `framemaps/base_0.base` | — one payload |
| `synth` | `synth/synth_0.synth` | — one payload |
| `song` `jingle` | `songs/song_0.jmid` | — one payload |
| `sample` `patch` | `samples/sample_0.sample` | — one payload |
| `font` | `fonts/font_494.fm` | — one payload |
| `binary` | `binary/binary_0.jpg` | — one payload |
| `worldmapgeo` | `worldmap/geography/worldmapgeo_10016.wmg` | — one payload |
| `worldmapground` | `worldmap/ground/worldmapground_10016.png` | — one payload |
| `animaya` | `animayas/animaya_0.animaya` | — one payload |

"one payload" means the archive is stored whole, so there is no member list to
index and nothing between the bytes on disk and the bytes in the cache. That is the
default and it applies to seventeen of the twenty tables.

### The world map is laid out kind by kind

Table 19 is the one table whose *archive* is not an asset. The archive is one of the
five names `class305` declares and the **file** is one map:

| archive | kind | on disk |
|---|---|---|
| 0 | `details` | `details.wma` — `[main]`, `[ancient_cavern]`, … |
| 1 | `compositemap` | `compositemap.wmc` — same blocks, region and icon lists |
| 2 | `compositetexture` | `compositetexture/<map>.png` |
| 3–54 | `labels` | `labels_10.wml` — `label<N>=name,x,y,size` |

So it carries a codec *and* the split flag: the codec owns the two archives with a
text form and declines the rest, which fall through to files plus a filepack. The
codec gets first refusal on export, on import and in the fidelity pass — three
places that must agree, and did not until each was fixed in turn.

The map names come from decoding archive 0, because a details record is the only
place they are written down (`main` / `Gielinor Surface`). All four archives list the
same file ids, so one decode names every member of every archive.

The `labels` archives are one file each, so they carry no member index at all — a
`.filepack` naming one entry says nothing, and splitting a single-member archive is
what produced 51 one-line filepacks beside 51 one-file directories. 50 of osrs239's
51 are a bare `00 00`; the fifty-first holds 548 place names:

    label1=Lumbridge,3239,3234,1
    label3=Kingdom of/Misthalin,3217,3321,2

The coordinates are world coordinates and `/` is a line break. The layout was read
off the bytes and confirmed by exact consumption — 548 labels, 10,261 bytes, zero
remainder — which is also what separates a labels file from a PNG, since both will
happily yield a plausible count and some strings.

### What is still binary

`worldmap/geography` — 2,101 `.wmg` files of per-tile data, which `class295.method5572`
decodes. The grammar is *mostly* read: flags byte, then `(flags & 1)` selects a short
form (optional overlay `u16`, underlay `u16`) and the long form carries an underlay,
an optional overlay list and an optional loc list over `((flags & 24) >> 3) + 1`
levels. 355 of the 2,101 files parse to exact consumption as a bare 64×64 grid with
no header; the rest either drift or run out, so at least one case is still misread.
The deob's containers declare a 3-byte header for a square (`class281`) and 5 for a
zone (`class289`), and neither shape fits the standalone files — the record header
appears to live in the compositemap stream rather than in the geography file.

**This is also where the "unknown section type 91 / 219 / 249" flood came from.** The
area decoder was being run over every member of every archive — PNG bytes included.
Those were never section types; they were a compressed image being read as a section
list.

### Which shape a table gets

1. **Does the archive hold one payload?** Then store it whole. The payload *is* the
   container's file table plus its files, so it round-trips byte for byte and an
   index would have nothing to say. An animset holds hundreds of frames and is still
   this case: the animset is the unit anyone edits, loads or names.
2. **Do its members have a text form?** Then one file of named blocks plus a
   `.compack`. Interfaces and textures.
3. **Otherwise** — files plus a `.filepack`. `dbindex`, `worldmaparea`, and a map
   square's members past terrain and locs.

### What the extensions mean

Extensions come from the payload's own magic, not from the era: an OldSchool model
gets `.model` and a real OB2 gets `.ob2`, table 10 gets `.jpg` and table 20 `.png`,
and the music tables get `.jmid` rather than a `.mid` no player would open — they
open `17 07 f6 02`, Jagex's container, not MIDI. A codec's extension must differ
from the raw fallback's, checked at startup, or a record that declines its codec
would be packed back as its own text.

---

## Why the member index exists

Three earlier shapes each put the member↔id mapping somewhere that is not a file:

- **a directory of `<file_id>.<ext>`** — the id was in a *filename*, so a member
  could not be renamed, and the importer recovered the member set from `readdir`;
- **probing `<stem>.extra2.bin` … `.extra255.bin`** — the archive an import produced
  depended on what the filesystem happened to hold;
- **the id encoded in a block header (`[mat_47]`)** — correct, but it makes the
  header a number when it wants to be a name.

None of these is visible as a mistake until something goes wrong. The failure that
made it concrete: `pack/model.pack` was deleted by the sparse-write rule (every name
in it was `<ns>_<id>` filler), and because `cp_assets_import` walks the pack,
`cachepack pack --assets` then wrote **zero** models — silently, because a pack with
no lines has nothing to walk. 53,390 of the 61,615 models had also been exported to
content-derived paths like `models/npc/goblin.model`, which contain no id at all, so
the mapping was recoverable only by re-deriving the names from the configs.

Hence the rules now in force: an asset pack is written **in full** (a filler line is
still an index entry), `unpack` records every archive id whether or not anything has
named it, and the naming passes that derive model and map names from the configs run
whenever the index is built rather than only when files are written.

## Adding an asset

Drop the file in the table's directory, give it a name in `pack/<ns>.pack` at or
above the namespace's declared allocation base (`server_base` in
`src/content/content_register.c` — 100000 for models), and reference it from a
config. `pack --assets` writes the payload **and extends the reference table**.

That last part is the whole of it. A dat2 archive is reachable only through its
reference table, so an id the table does not list is invisible to the client — and
the packer used to warn and move on:

    cachepack: idx7 archive 100000 is not listed in the reference table —
               writing the bytes but leaving the table alone

which reads as cosmetic, because the payload really was written and the per-table
count really did go up. Adding one model wrote 61,616 archives and shipped 61,615.
`cp_reference_sync` now grows both arrays — `archives`, indexed by archive id with
`index == -1` in the gaps, and `ids`, the ascending list the encoder walks, since
only ids in `ids` are written.

## Orphans

A rename leaves the old file behind. `unpack` reports files under a table's folder
that no index entry accounts for:

```
  interfaces             968 files, 1814147 bytes
    orphaned             942 file(s) no index entry accounts for, e.g. interface_535.if
```

Reported, never deleted: the rule that decides "no entry accounts for this" is
deliberately generous — a file belongs if *any* prefix of its path is an entry whose
id still answers to that name — and a tool that removes content on a heuristic is a
worse trade than a line of output.

## Fidelity

`cachepack verify` holds each table to a bar, and the bar differs by kind. Whole-cache
byte-identity is **not** the bar and is not reachable: Jagex's packer does not write
config opcodes in ascending order and the encoders do, so records come back at
identical length with different bytes (`EXCEPTIONS.md` B3).

| kind | bar |
|---|---|
| config types | `lost-here` = 0 — a record the library's codec reproduced byte-exactly and this tool's text layer did not |
| asset tables | `differ` = 0 — a *length* change is a field or a member that did not survive; a same-length mismatch is byte ordering |
| `script` | semantic only — its friendly form is decompiled source, and compiling it back gives this compiler's bytes rather than Jagex's. Measured by `test/test_cs2.c` |

Sprites are the worked example of why the asset bar is length rather than bytes:
100% same-length, 24–29% exact, because the decoder does not retain the per-sprite
flags byte and always writes row-major. Nothing is lost.

`test/test_cachepack_fidelity.sh` runs this on every `make -C 3rd/rscache test`, and
skips loudly when no cache is present — no cache is committed.
