# Cache indices 16 and 17

Until this change `enum RSCache_Dat2OsrsDiskTable` jumped straight from
`MUSIC_PATCHES = 15` to `WORLDMAP_GEOGRAPHY = 18`, and `cachepack` neither
unpacked nor packed whatever sat in the gap. This is what is actually there.

Everything below is measured against `cache.osrs239` (rev 239) and
`cache.rs727_preeoc`, and traced in the deobfuscated client at
`git_repos/Deob/src_osrs239/deob`. Where something is inferred rather than
proven it says so.

## The short answer

| index | OldSchool (osrs239)      | RS2 (rs727)            |
| ----- | ------------------------ | ---------------------- |
| 16    | **does not exist**       | `LOC` — already named  |
| 17    | **client defaults** — new | `ENUM` — already named |

So only one of the two was ever missing, and only on one branch. Index 16 is
not an omission in our table: OldSchool has no such index to name.

### Index 16 is genuinely absent from OldSchool

Two independent sources agree:

- The cache. `main_file_cache.idx255` — the index of indices — lists
  `0..15, 17..22, 24`. There is no `idx16` file on disk either.
- The client. Its own index register is `class553`, one instance per index:
  `0..15, 17..25, 255`. No `class553(16)` exists.

The RS2 branch does have an index 16 (`LOC`), and `RSCACHE_DAT2_RS2_TABLE_LOC = 16`
has named it all along. Nothing to add.

## Index 17: the defaults table

### It has no name

The reference table for index 17 decodes as protocol 7, `flags = 0x4`. Bit 0 is
the "has name hashes" bit and it is **clear**, so the cache stores no name for
the index or for either of its groups. Neither does the gameval table (index 24,
also `flags = 0x4`, and its 14 groups are per *config kind*, not per index).

**"defaults" is this tree's word for it.** It is chosen to match the vocabulary
we already had — `RSCACHE_DAT2_TABLE_DEFAULTS` existed for the RS2 idx28
equivalent — and not recovered from anywhere. The deob names it only
structurally, and in obfuscated form: the index is `class553.field6342`
(= 17), and its groups are `class9.field56` (= 3) and `class9.field57` (= 1).

### Two groups, and the client register agrees

```
=== index 17 reference table ===
protocol=7 revision=1785237706 flags=0x4 groups=2
  group 1: 3164 files
  group 3: 1 file
```

Exactly the two the client's `class9` register declares. The whole index is
small enough that the client preloads *every* group at startup (`method12477`)
and it is worth 1% of the loading bar.

### How it is decoded (`class11`, deob)

```java
field5159 = new class11();
field5159.method235(field460, ...);   // field460 = the index-17 archive
```

`method235` reads **group 3** (`class9.field56`) as a plain opcode loop.
`class11`'s constructor pre-sets every id to `-1`, so nothing here is
hardcoded — the record supplies the values.

| opcode | payload                                      |
| ------ | -------------------------------------------- |
| 0      | end of record                                |
| 1      | one 24-bit value, **read and discarded**     |
| 2      | 11 sprite ids, bigsmart                      |
| 3      | `int[3][5]` of 24-bit RGB                    |
| 5      | 2 model ids, int32                           |
| 6      | as 2, plus one trailing bigsmart, discarded  |

"bigsmart" is `method13607`: if the next byte's high bit is set, read 4 bytes
and mask `& 0x7FFFFFFF`; otherwise read a `u16` where `32767` means `-1`. It is
an *id* encoding, not a hash — a 32-bit hash cannot survive the mask or the
sentinel.

### osrs239 group 3, decoded in full

The whole record is 83 bytes and every one is accounted for
(`1 + 3 + 1 + 22 + 1 + 45 + 1 + 4 + 4 + 1 = 83`):

```
01 000139                        opcode 1, discarded
02 00a9 01a8 013d 01b7 01b8 01b9  opcode 2, eleven sprite ids
   01a6 012b 012c 013c 01a7
03 <45 bytes>                     opcode 3, 3x5 RGB
05 0000e022 0000e023              opcode 5, two model ids
00                                end
```

**The eleven sprite ids**, resolved against
`OSRS-Content/osrs239-content/pack/8_sprites.pack`:

| # | id  | name               |
| - | --- | ------------------ |
| 1 | 169 | `compass`          |
| 2 | 424 | `mapedge`          |
| 3 | 317 | `mapscene`         |
| 4 | 439 | `headicons_pk`     |
| 5 | 440 | `headicons_prayer` |
| 6 | 441 | `headicons_hint`   |
| 7 | 422 | `mapmarker`        |
| 8 | 299 | `cross`            |
| 9 | 300 | `mapdots`          |
| 10| 316 | `scrollbar`        |
| 11| 423 | `mod_icons`        |

This is the graphic-defaults set, and it is **our own
`src/engine/static_sprites.c` list in the same order** — drop the three
dat1-era entries we carry that rev239 does not have here (`mapfunction`,
`headicons`, `hitmarks`) and the two agree exactly, entry for entry.

Worth stating plainly, because it is the practical consequence: the official
client resolves `compass` to sprite group 169 by reading *this record*.
`CacheProvider_SpriteIdByName("compass")` is our own inversion — it hashes the
name and scans index 8's reference table (`dat2_resolve_sprite_archive_by_name`,
`src/engine/dat2/task_dat2_sprite_load.c`). Both land on 169 at rev239. They
would diverge if this record ever pointed at a group whose name is not
`compass`.

**Opcode 3's `int[3][5]`** is three 5-stop 24-bit ramps:

```
row 0: 000000 ff0000 ffff00 ffffff ffffff
row 1: 000000 00ff00 00ffff ffffff ffffff
row 2: 000000 0000ff ff00ff ffffff ffffff
```

Black to a primary, through its secondary, to white — one ramp per primary.

**Opcode 5's two model ids** are 57378 and 57379. They are loaded from the
models archive and drawn into the scene through `method1711`, gated on a client
counter. *What they depict is not resolved here* — the draw sites
(`Statics.java:46924` and `:47598`) show how they are used, not what they are.

### Group 1: shipped, preloaded, and never read

`class9.field57` (= group 1) is declared in the register and referenced nowhere
else in the rev239 client. The only content read of the index-17 archive is
`class11.method235`, and it reads group 3. Group 1 still ships, and
`method12477` still pulls it into memory at startup.

Its 3164 files decode as **colour stops with an interval between them**:

```
colour(3 bytes)  [ interval(1 byte)  colour(3 bytes) ]*
```

The file sizes are exactly what that grammar allows and nothing else:

| size | entries              | count |
| ---- | -------------------- | ----- |
| 3    | 1 colour             | 3152  |
| 7    | 2 colours, 1 gap     | 11    |
| 63   | 16 colours, 15 gaps  | 1     |

`63 = 16*3 + 15` and `7 = 2*3 + 1`. File 5012 is the 63-byte one and is
`ff0000 07 f6a1fd 01` repeated eight times — red pulsing to pink. Across all
3164 files there are only **37 distinct colours**.

**What the file ids key into is unresolved.** They run 3615..17038, sparsely,
and the low block is strikingly regular — runs of ~35 consecutive ids repeating
every 256 (3615..3649, 3871..3905, 4127..4161, ...) — which suggests a packed
composite id rather than a flat entity id. The upper range is not regular. Since
rev239 never reads the group, the client offers no evidence either way, and
guessing here would be worse than leaving it open.

### RS2 has the same table at a different id and a different schema

`cache.rs727_preeoc` idx28 is the same *role* — six groups, group 3 the record —
but not the same format: group 3 there is 3067 bytes and opens on opcode 3.
`RSCACHE_DAT2_TABLE_DEFAULTS` therefore names a job, not a layout. That is why
it sits at the end of the RS2-only block in `enum RSCache_Dat2Table` with a
comment rather than being moved up among the tables that share an *id*: moving
it would renumber every value above it for no gain.

## What changed

- `3rd/rscache/src/dat2disk.h` — `RSCACHE_DAT2_OSRS_TABLE_DEFAULTS = 17`, and
  `RSCACHE_DAT2_TABLE_DEFAULTS` reclassified from RS2-only to both-epochs.
  Every existing enum value keeps its number.
- `3rd/rscache/src/dat2disk.c` — the OldSchool map sends
  `RSCACHE_DAT2_TABLE_DEFAULTS` to 17 instead of `ABSENT`.
- `3rd/rscache/tools/cachepack/cachepack.h` — `CP_ASSET_DEFAULTS`.
- `3rd/rscache/tools/cachepack/cp_assets.c` — the register row.

The row carries `CP_ASSET_SPLIT` and no codec. Split because its two groups are
opposite shapes: group 3 is one record and lands as a bare
`defaults/defaults_3.dflt`, while group 1's 3164 files land as
`defaults/defaults_1/<id>.dflt` plus a `defaults_1.filepack`. That is the
worldmap-geography shape exactly.

No codec, deliberately. Group 3's schema is written out above and we could
render it as text — but knowing a schema is not the same as proving a
byte-exact round-trip for it, and the only thing a wrong codec buys is a cache
that repacks to different bytes than it unpacked. The two revisions we hold
disagree on the format, so a codec should be tested against both before it
becomes the default. Raw payload until then.

Unpack writes `pack/17_defaults.pack`:

```
1=defaults_1
3=defaults_3
```

Both lines are filler names, because — see above — the cache names neither.

## Verifying

```sh
export PATH=toolchains/mingw64/bin:$PATH
export CC=gcc
make -C 3rd/rscache/tools cachepack

3rd/rscache/tools/cachepack/cachepack.exe unpack \
  --cache cache.osrs239 --rev osrs239 --src /tmp/t --assets=defaults
3rd/rscache/tools/cachepack/cachepack.exe verify \
  --cache cache.osrs239 --rev osrs239 --src /tmp/t --assets=defaults --tmp /tmp/v
```

Current result — both groups reproduce byte-for-byte:

```
  table                records    exact same-len   differ    codec declin unread
  defaults                   2        2        0        0        0      0      0
```
