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

### Where the client reads them, and what that corroborates

One block, `Statics.java:37160`–`:37299`, and it reads **all thirteen** — the
eleven sprites and opcode 5's two models. It counts them: `if (var69 < 13)`,
with `var69 * 100 / 14` driving the loading bar.

| slot | field | id | name | loader | → field |
| ---- | ----- | -- | ---- | ------ | ------- |
| 0  | `field65` | 169 | compass          | `method9593` — one sprite  | `field2579` |
| 1  | `field66` | 424 | mapedge          | `method9593` — one sprite  | `field4024` |
| 2  | `field63` | 317 | mapscene         | `method6590`+`method5456`  | `field5200` |
| 3  | `field75` | 439 | headicons_pk     | `method6840` — array       | `field6362` |
| 4  | `field69` | 440 | headicons_prayer | `method6840`               | `field703`  |
| 5  | `field67` | 441 | headicons_hint   | `method6840`               | `field692`  |
| 6  | `field71` | 422 | mapmarker        | `method6840`               | `field1211` |
| 7  | `field72` | 299 | cross            | `method6840`               | `field4579` |
| 8  | `field73` | 300 | mapdots          | `method6840`               | `field5235` |
| 9  | `field74` | 316 | scrollbar        | `method6590`+`method5456`  | `field2374` |
| 10 | `field68` | 423 | mod_icons        | `method6590`+`method5456`  | `field1625` |
| —  | `field76` | 57378 | —              | `class142.method4501`      | `field4180` |
| —  | `field77` | 57379 | —              | `class142.method4501`      | `field3090` |

The loader kinds are a check on the names that does not go through the sprite
table at all. There are three distinct load paths, and every name lands in the
one it should: the two single images (`compass`, `mapedge`) take the single-
sprite call, the six multi-frame sets (`headicons_*`, `mapmarker`, `cross`,
`mapdots`) take the `class657[]` array loader, and the three palette-indexed
sheets (`mapscene`, `scrollbar`, `mod_icons`) take the `class670[]` one.
Nothing is in the wrong bucket.

Two draw sites settle their slots on their own:

- **slot 7, `cross`.** `client.java:9983` draws `field4579[timer / 100]` at
  `(mouseX - 8, mouseY - 8)` and `:9986` draws `[timer / 100 + 4]` — two
  four-frame banks at the cursor, centred on a 16x16. That is the red/yellow
  click cross.
- **slot 2, `mapscene`.** On completion the block calls
  `method10602(field5200[0], r±10, g±10, b±10)` seeded from `Math.random()` —
  the per-session map-scenery tint jitter.

So four independent things agree: the deob gives the ordering, index 17 gives
the ids, index 8 gives the names, and the loader shapes and draw sites say the
three line up.

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

**Opcode 5's two model ids** are 57378 and 57379, and they are the one part of
this record the cache names nowhere. `pack/7_models.pack` has them as filler
`model_57378` / `model_57379` while both their neighbours are named
(`loc/fai_falador_roof_edge_short1z`, `obj/huntguide_moonlight_moth`) — a
model's name in that pack is recovered from the loc/obj/npc that references it,
and nothing references these two except this record, which nothing was reading.
So the draw sites are the only evidence there is.

They say this much, and it is not in doubt:

- Both load through `class142.method4501(models, id, 0)` and are drawn into the
  world by `Statics.method1711(scene, angle, model)`, which places a model at a
  compass **bearing**, at radius `max(512, 1400 - f(zoom))` from the player's
  tile.
- Both are gated on `client.field1144 > 0`, a **30-tick countdown**.
- That countdown and `client.field1088` are set together in `class377`, by a
  click on a widget of kind `class528.field6175`: it takes `atan2` of the click
  about the widget's centre, subtracts camera yaw, and quantises to **16
  directions**. The result is stored and sent to the server as one byte.
- **57379** draws at `field1088` — the bearing just chosen.
- **57378** draws at `field831.field6801[last] * 128` where that entry's kind is
  60 — a bearing already queued — and is suppressed while the countdown runs if
  the new choice equals it, so one bearing is never marked twice.

So they are a pair of bearing markers placed out in the world, and the tree
calls them `bearing_marker_selected` (57379) and `bearing_marker_queued`
(57378). Both names describe placement, which the draw math settles by itself.

**What is deliberately not claimed:** that this is the Sailing helm.
`3rd/rsprot` carries a one-byte `SET_HEADING` at rev239 whose shape matches this
packet exactly — but the deob gives the packet opcode **109**, while our rev239
client table puts `SET_HEADING` at **44** and has no client 109 at all. Either
the generated table and this jar are different sub-revisions, or the packet
match is wrong. Until that is settled, "bearing" here means the angle the model
is placed at and says nothing about which system asked for it.

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
- `3rd/rscache/src/datatypes/dat2_defaults.{c,h}` — the record codec: decode,
  encode, and a round-trip check for both shapes.
- `3rd/rscache/rscache_unity.c` — the new TU.
- `3rd/rscache/test/test_dat2_defaults.c` — 50 checks over the real bytes.
- `3rd/rscache/tools/cachepack/cachepack.h` — `CP_ASSET_DEFAULTS`.
- `3rd/rscache/tools/cachepack/cp_assets.c` — the register row.
- `3rd/rscache/tools/cachepack/cp_decode.c` — `cp_codec_defaults`.

The row carries `CP_ASSET_SPLIT` because its two groups are opposite shapes:
group 3 is one record and lands as a bare file, while group 1's 3164 members
land as a directory plus a `defaults_1.filepack`. That is the
worldmap-geography shape exactly.

### The readable form

Group 3 unpacks to `defaults/defaults_3.defaults`:

```ini
[defaults]
opcodes=1,2,3,5
legacy=000139
sprite=0:169  // compass
sprite=1:424  // mapedge
sprite=2:317  // mapscene
sprite=3:439  // headicons_pk
sprite=4:440  // headicons_prayer
sprite=5:441  // headicons_hint
sprite=6:422  // mapmarker
sprite=7:299  // cross
sprite=8:300  // mapdots
sprite=9:316  // scrollbar
sprite=10:423 // mod_icons
ramp=0:000000,ff0000,ffff00,ffffff,ffffff
ramp=1:000000,00ff00,00ffff,ffffff,ffffff
ramp=2:000000,0000ff,ff00ff,ffffff,ffffff
model=0:57378  // bearing_marker_queued
model=1:57379  // bearing_marker_selected
```

`opcodes` is there because the repack has to be byte-exact and the opcode order
is part of the bytes — as is *which* opcode carried the ids, since 2 and 6 write
the same eleven.

### Where the slot names come from, and why they are a comment

They are keyed on the **slot index**, and the name is commentary, because those
are two facts of very different strength and the file should not blur them.

Three sources feed those lines:

| what | source | strength |
| ---- | ------ | -------- |
| slot ordering | the deob's `class11.method235` read order | direct, structural |
| the ids | index 17 group 3, the bytes themselves | direct |
| the names | index 8's reference table name hashes | direct, but names the *target* |
| "slot 0 is the compass slot" | RuneLite's injected `setCompass` on `field2579` | indirect |

Two things follow that are easy to get backwards.

**The names are not the client's.** Grep any deob tree in this repo's toolchain
for `compass`, `mapedge`, `mapscene`, `mod_icons`, `scrollbar` or `headicons`
and you get nothing — the only `compass` in the jar is RuneLite's injected hook
name. No sprite name is compiled into the client.

**The names are the cache's, and they are verified.** Index 8's reference table
carries a name hash per group (`flags = 0x5`) and `djb2` of all eleven names
matches its stored hash exactly — `compass` is `950484242` in the cache and
`950484242` computed. Nothing here is authored or imported.

But index 8 names *the sprite a slot points at*, not the slot. At rev239 those
coincide. A revision that pointed slot 0 at some other group would still have a
compass slot at 0, and the name would then be describing the new target. So
parsing on the name would make a cross-table, revision-specific coincidence
load-bearing, and would reject exactly the cache most worth reading. The slot
index is what opcode 2 encodes, so the slot index is what the file keys on.

Group 1's members unpack to `defaults/defaults_1/<id>.colours`:

```ini
[colours]
stop=ff0000
gap=7
stop=f6a1fd
```

### Why a codec here needed a round-trip gate

Bigsmart is ambiguous on the way out: an id below 32767 can legally be written
two bytes or four, and only the source bytes say which. So both codecs decode,
**re-encode, and compare against the bytes they came from**, and write the raw
payload instead when that fails. Every record in `cache.osrs239` passes.

That gate is also what makes the RS2 half safe. `rs727`'s idx28 is the same
table with a different schema, and it opens on opcode 3 — which this record
*does* have — so the decoder gets 45 bytes in before the disagreement surfaces,
at a byte holding 142, which is not an opcode here. It declines. A decoder that
limped on would have written a misread 3,067-byte RS2 record into an
OldSchool-shaped struct and cachepack would have believed it.
`test_dat2_defaults.c` pins that decline against the real 47 leading bytes.

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

# the record codec, against the real bytes of both revisions
make -C 3rd/rscache build/test_dat2_defaults
3rd/rscache/build/test_dat2_defaults

# the whole path, cache -> text -> cache
make -C 3rd/rscache/tools cachepack
3rd/rscache/tools/cachepack/cachepack.exe unpack \
  --cache cache.osrs239 --rev osrs239 --src /tmp/t --assets=defaults
3rd/rscache/tools/cachepack/cachepack.exe verify \
  --cache cache.osrs239 --rev osrs239 --src /tmp/t --assets=defaults --tmp /tmp/v
```

Current result — both groups go through the codec (`codec` = 2, `declin` = 0)
and both reproduce byte-for-byte:

```
dat2 defaults: 50 checks passed

  table                records    exact same-len   differ    codec declin unread
  defaults                   2        2        0        0        2      0      0
```

Note that `cachepack unpack` against `cache.rs727_preeoc` segfaults during the
*config* pass, long before it reaches any asset. That is pre-existing on that
lane and not related to this table — `--assets=binary` crashes identically —
which is why the RS2 decline is pinned by the unit test rather than by a run.
