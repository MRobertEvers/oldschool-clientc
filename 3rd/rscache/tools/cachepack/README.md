# cachepack — the shape of an unpacked cache

`cachepack unpack` turns an OldSchool cache into an editable tree; `cachepack pack`
turns that tree back into a cache. This document is about the **layout** the tree
has and why each table gets the layout it does. For the architecture — which
LostCity tool each driver corresponds to, and why the opcode readers are rscache's
rather than this tool's — see the header of `cachepack.h`.

    cachepack unpack --cache cache.osrs239 --rev osrs239 --src <tree> --assets
    cachepack pack   --src <tree> --out cache.mine --base cache.osrs239 --assets
    cachepack verify --cache cache.osrs239 --rev osrs239 --src <tree> --assets

### Summoning texture-only reapplication

The rev-530 Summoning manifests share a reviewed 680-row material table named
`texture_map_530_to_239.ini` beside their manifests. To apply a changed table without
regenerating NPC/object configs or importing deferred gameplay closure assets, use:

    cachepack import --manifest <summoning-manifest> --apply --textures-only

`--textures-only` requires `--apply`; it rewrites the imported model payloads only. The shared
table is mandatory for `import:scape2009` manifests that do not provide their own inline map,
and an incomplete table or unmapped model face is an error.

---

## Three levels of index, and only three

Everything in the tree is addressed by a number the client's cache fixes, and every
one of those numbers is written down in a file. Nothing is recovered by listing a
directory or by parsing a filename.

**1. `pack/<index>_<name>.pack` — archives.** One line per archive, `id=name`, for
every cache index. This is the id authority: `cp_assets_import` walks the pack, so an
archive with no pack line is not written. One file per namespace, hand-ownable —
comments and blank lines survive a rewrite, and a save merges rather than truncating
(`lc_pack.h`).

**Components have no pack file of their own.** A component is a child of an
interface, so `interfaces/bankmain.compack` — the member index over exactly those
children — carries the cache's own names for them, and the id is composed the way the
client composes it: `(interface << 16) | child`. `pack/component.pack` used to hold
26,491 lines of `786432=bankmain:infinite` while the compack beside the interface said
`0=com_0`: two indexes over the same members, one named and one filler.

Composing from the two files that remain is also more accurate than the file was. It
drops 41 entries for `hosidius_strip_rewards`, which gameval archive 14 names but this
cache has no interface archive for, and adds 28 real components the archive holds and
the gameval table never named. Of the 26,450 in both, **zero** changed name.

**The index is in the name** because `pack/` holds two kinds of file that otherwise
look identical, both `id=name`. `7_models.pack` lists the archives of cache index 7;
`npc.pack` lists records *inside* archive 9 of index 2. A leading number means the
first kind and nothing else does, and `ContentRegister_Validate` enforces it against
the register's `cache_index` so the name cannot drift from the fact.

It also cost nothing to notice, once written down, that **index 2 had no archive
index at all**. Its archives are the config groups, and `configs/all.npc.compack` names the
records in archive 9 rather than the archive — so the twenty config groups were the
only archives in the cache that nothing named. `2_configs.pack` is that file:

```
6=loc
9=npc
10=obj
```

The ids are not consecutive; the gaps are groups this revision has no decoder for.

Filler ids keep the short singular form — `model_412`, not `7_models_412` — because
filler is a *file* name too (`models/model_412.model`). Deriving it from the pack name
renamed 48,000 exported files and left a duplicate of each one behind, so the two are
separate fields (`CP_Asset.pack` and `CP_Asset.filler`).

```
pack/3_interfaces.pack     12=bankmain
pack/9_textures.pack       0=texture_0
pack/7_models.pack         0=npc/royal_dwarf_citizen1_head
```

**2. A member index — the files inside one archive.** A multi-file archive needs
something to say which member is which, and it sits *beside the archive* in the
table's own folder:

| index | when | contents |
|---|---|---|
| `<archive>.compack` | the members have a text form, so they are blocks in one file | `<file id>=<block name>` |
| ” | every config type included: `configs/all.seq.compack` over `configs/all.seq` | `2=swarm_block` |
| `<archive>.filepack` | they do not, so they are files | `<file id>=<path under the table's folder>` |
| `pack.meta` | a sprite pack, whose "members" are frames of one member | `count=`, `sprite<N>=…` |

All three are plain pack files, so `lc_pack_*` reads and writes them and they
inherit comment preservation and merge semantics. **Ascending ids are the order the
container gets**, which is the order its payload and the reference table's child
list must agree on.

**The index lists every member, including the ones a codec claims.** Codec and split
are not alternatives applied per table; they are applied in order, per member, and one
archive can need both. A map square is five files and its filepack names all five —
two at text paths the codec owns, three at `.bin`:

```
0=m15_32.jm2      terrain
1=m15_32.jl2      locs
2=m15_32/2.bin
```

Locs used to be a second section of the jm2, which made one path stand for two members
and left the filepack starting at `2=` — the two members with a text form were the only
ones the index did not name. They are their own file now, so the rule holds without an
exception: every member is addressable and the index says where it is.

Codec first, split second, on export, on import and in the fidelity pass; those three
disagreed once, and the packer silently shipped 52 of 54 worldmap archives.

**3. `configs/all.<type>` — config records.** `[name]` blocks of `key=value`, one per
record, indexed by `configs/all.<type>.compack`.

That index is level 2, not a third kind. A config record is a *file* of a config
archive — `[swarm_walk]` is file 0 of archive 12 — so what binds `0=swarm_walk` is a
member index with exactly the contract above, and it belongs beside the archive it
indexes. It used to be `pack/seq.pack`, which put it in `pack/` under the
archive-level name and extension, so one directory held both levels under one
spelling and only the reader's knowledge told them apart.

---

## Every table

| table | on disk | member index |
|---|---|---|
| `interface` | `interfaces/bankmain.if` — one block per component, named by the cache | `interfaces/bankmain.compack` |
| `texture` | `textures/texture_0.texture` — one block per material | `textures/texture_0.compack` |
| `map` | `maps/m15_32.jm2` terrain, `maps/m15_32.jl2` locs | `maps/m15_32.filepack` — all five members |
| `dbindex` | `dbindex/dbindex_0.dbi` — one block per column | `dbindex/dbindex_0.compack` |
| `worldmaparea` | `worldmap/areas/details.wma` — a block per map | `details.compack` |
| ” | `worldmap/areas/compositetexture/main.png` | `compositetexture.filepack` |
| `worldmapgeo` | `worldmap/geography/worldmapgeo_10016.wmg` — one line per tile | `worldmapgeo_10137.filepack` (multi-file only) |
| `sprite` | `sprites/<name>/0.bmp` — one BMP per frame | `sprites/<name>/pack.meta` |
| `script` | `scripts/<name>.cs2`, or `.bin` where it does not decompile | — one payload |
| `model` | `models/npc/goblin.model` | — one payload |
| `animset` | `animsets/animset_0.anim` | — one payload |
| `base` | `framemaps/base_0.base` | — one payload |
| `synth` | `synth/synth_0.synth` | — one payload |
| `song` `jingle` | `songs/song_0.jmid` | — one payload |
| `sample` `patch` | `samples/sample_0.sample` | — one payload |
| `font` | `fonts/font_494.fm` — advance widths as text | — one payload |
| `binary` | `binary/binary_0.jpg` | — one payload |
| `worldmapground` | `worldmap/ground/worldmapground_10016.png` | — one payload |
| `animaya` | `animayas/animaya_0.animaya` | — one payload |

"one payload" means the archive is stored whole, so there is no member list to
index and nothing between the bytes on disk and the bytes in the cache. That is the
default and it applies to sixteen of the twenty tables.

### The world map is laid out kind by kind

Table 19 is the one table whose *archive* is not an asset. The archive is one of the
five names `class305` declares and the **file** is one map:

| archive | kind | on disk |
|---|---|---|
| 0 | `details` | `details.wma` — `[main]`, `[ancient_cavern]`, … |
| 1 | `compositemap` | `compositemap.wmc` — same blocks, region and icon lists |
| 2 | `compositetexture` | `compositetexture/<map>.png` |
| 3–54 | `labels` | `labels_10.wml` — `label<N>=name,x,y,size` |

Table 18, the geography, is separate from all of this and is covered below.

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

### Geography is text too

`worldmap/geography` is a bare stream of tiles — no header, no count, no dimensions.
What ends it is the end of the file, so **exact consumption is the only evidence the
grammar is right**, and it is a sharp test: 2,146 of 2,146 members now decode, encode
back byte-identically, and land on either 4,096 tiles (a 64×64 square, 1,981 of them)
or a multiple of 64 (whole 8×8 zones, 165). Nothing lands anywhere else.

    tiles=4096
    0: s u0 o454
    232: u0 lv1 ov:448;0;0 L0:60453;10;2

`s` is the short form, `u`/`o` underlay and overlay, `lv` the declared loc levels,
`ov:` the overlay list and `L<n>:` the locs on level n as `id;shape;rot`. Empty tiles
are omitted, which is why `tiles=` is stated rather than counted: 179 members end in
an empty tile, and a file that stopped at its last non-empty one would encode short
and the cache would take it.

Two things had to be right that the deob does not give you:

- **The loc id is a plain `u32`.** `class295.method5572` reads it with the 2-or-4-byte
  bigsmart, and following that, 883 of 2,101 files ran out of bytes mid-tile while 355
  parsed by luck. As a `u32` it is 2,057 of 2,057 — every single-file archive, exactly.
  The deob is a later revision than this cache and the field was widened between them.
  Where the two disagree the bytes win.
- **The other 44 archives hold two or three files.** Stored whole, a `.wmg` was its
  members concatenated *plus the container's trailer*, so no tile decoder could ever
  have read it. Splitting the table made all 44 readable at once. They were the entire
  residue — the grammar was never the problem, the container was.

The deob's 3-byte square header (`class281`) and 5-byte zone header (`class289`) are
also later additions: nothing in osrs239 carries them, and the first byte of every file
is a tile's flags.

**The area decoder's "unknown section type 91 / 219 / 249" flood was the same class of
mistake** — it was being run over every member of every archive, PNG bytes included.
Those were never section types; they were a compressed image read as a section list.

### The db indexes are text

Table 21 is what `DB_FIND` scans. For one dbtable, file 0 is the master — every row
id, which `DB_FINDALL` returns — and file N is the index for column N-1.

    [column_33]
    base=0:int
    base=1:int
    index=0:0:23,46,51,57
    index=1:32000:61

A column with several typed fields gets one tuple position per field, which is why
column 33 of table 0 has two: its declared types are 17 and 0.

`base=` is written for **every** position including the empty ones — `dbindex_188`
file 2 is seven empty int slots and nothing else, so the positions are the only thing
in that file. A position with no `index=` line has entry_count 0. Entries keep their
binary order, because `DB_FIND` scans linearly and sorting them would change what the
cache says. Ints print signed to match dbrow; strings are quoted, since a value can
contain the `:` that separates the fields.

This is **derived** data — a projection of the dbrows — so the readable form is for
inspection and CS2 debugging rather than as a source of truth. It still round-trips
byte-exactly (147/147): like the jm2 and the geography, a member is written as text
only once re-encoding it has reproduced the original bytes, and anything else falls
back to bytes plus a filepack.

osrs239's indexes are all `int`. The decoder handles `long` and `string`, so the text
form does too, but nothing in this cache exercises those paths.

### Fonts are metrics, not glyphs

Table 13 is 256 advance widths and an ascent — every one of osrs239's 21 files is
exactly 257 bytes, `RSCACHE_DAT2_FONT_METRICS_V1_SIZE`. There is no image in it.

    [metrics]
    ascent=35
    advance=32:4   // ' '
    advance=65:8   // 'A'

**The pictures are in the sprite table.** `p11_full`, `p12_full`, `b12_full` and the
rest are sprite archives with one member per character, and those already export as
BMP — `sprites/p11_full/65.bmp` is a 5x8 capital A. So nothing here needed rendering;
what the table needed was for its numbers to be readable, and a printable character
gets its glyph in a trailing comment because `advance=32:4` alone says nothing.

Every position is written even when the width is 0: the position is what gives the
byte its meaning, and a file with lines missing would encode short. V2 (263 bytes,
two-byte header, ascent at 258) is not written by this cache, so the codec declines it
rather than guessing at the six bytes it would have to reproduce.

### Which shape a table gets

1. **Does the archive hold one payload?** Then store it whole. The payload *is* the
   container's file table plus its files, so it round-trips byte for byte and an
   index would have nothing to say. An animset holds hundreds of frames and is still
   this case: the animset is the unit anyone edits, loads or names.
2. **Do its members have a text form?** Then the index names where each one is, and
   the only question left is whether a member is a *block* or a *document*.
   - Blocks in one file plus a `.compack`, when a member is a record among many —
     interfaces and textures.
   - One file each plus a `.filepack`, when a member is a document in its own right —
     a map square's terrain and locs, a geography member's tiles.
3. **Otherwise** — files plus a `.filepack`. `dbindex`, `worldmaparea`.

### What the extensions mean

Extensions come from the payload's own magic, not from the era: an OldSchool model
gets `.model` and a real OB2 gets `.ob2`, table 10 gets `.jpg` and table 20 `.png`,
and the music tables get `.jmid` rather than a `.mid` no player would open — they
open `17 07 f6 02`, Jagex's container, not MIDI. `.jl2` is this tool's own: a map
square's locs, in the jm2's grammar, split out so file 1 has a path of its own. A codec's extension must differ
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
made it concrete: `pack/7_models.pack` was deleted by the sparse-write rule (every name
in it was `<ns>_<id>` filler), and because `cp_assets_import` walks the pack,
`cachepack pack --assets` then wrote **zero** models — silently, because a pack with
no lines has nothing to walk. 53,390 of the 61,615 models had also been exported to
content-derived paths like `models/npc/goblin.model`, which contain no id at all, so
the mapping was recoverable only by re-deriving the names from the configs.

**The config indexes had the same bug in a quieter form.** A record the cache does not
name is emitted as the block `[mapelement_0]`, so `0=mapelement_0` restates the header
and was left out — 93,000 of this tree's 306,818 index lines, and six config types with
no index file at all. Nothing broke, because `cp_name_find` read the id back out of the
name. That is the same trade as the id living in `models/npc/goblin.model`: it works
until a block is renamed, and it means the index is not the authority — the *spelling
of a header* is.

Hence the one rule now in force everywhere: **every index is written in full, and
nothing is ever recovered from a name.** A filler line is still an index entry; `unpack`
records every archive id and every record id whether or not anything has named it;
`cp_name_find` looks in the index and reports a miss rather than guessing; and the
naming passes that derive model and map names from the configs run whenever the index
is built rather than only when files are written.

Writing config filler cannot lose an authored name or the prose around it: a save is a
merge, the in-memory pack was loaded from the same file, and a filler line is only ever
added for an id that has none. `configs/all.param.compack` keeps its 75-line header
through a full rewrite, which is the case that once justified not writing at all.

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
