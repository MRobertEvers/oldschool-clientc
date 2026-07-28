# rscache

A C library for reading and writing RuneScape caches, across both container
generations and a range of game revisions.

- **dat1** — the jagfile era (`main_file_cache.dat` + `.idx0..4`). Old-generation
  clients, roughly revisions 200–254.
- **dat2** — the JS5 era (`main_file_cache.dat2` + `.idx0..24` + `.idx255`).
  OldSchool RuneScape and the 643-era RS2 branch.

Every byte layout below was verified against the caches in this repo — `cache`,
`cache.643`, `cache.jan2026`, `cache.kronos`, `cache.osrs184`, `cache.osrs230`,
`cache.osrs239`, `cache254` and `cache254.lostcity` — by decoding, re-encoding and
comparing. Where a field's meaning is inferred rather than confirmed, it says so.

> **[EXCEPTIONS.md](EXCEPTIONS.md)** is the companion to this file: every place the
> implementation departed from its plan, stopped short of complete, or accepted a
> shortfall — with the reason. Read it before concluding something here is simply
> missing. It also lists the one open defect awaiting a decision (dat2 npc default
> values) and the bugs found and fixed along the way.

## Contents

- [Layout of the library](#layout-of-the-library)
- [Common ground: sectors and index files](#common-ground-sectors-and-index-files)
- [dat2 — the JS5 container](#dat2--the-js5-container)
  - [The variable and inventory config kinds](#the-variable-and-inventory-config-kinds)
- [dat1 — the jagfile container](#dat1--the-jagfile-container)
- [The revision model](#the-revision-model)
- [Model stream layouts](#model-stream-layouts)
- [CS2 — the clientscript language](#cs2--the-clientscript-language)
- [Writing a cache](#writing-a-cache)
- [Building and testing](#building-and-testing)

---

## Layout of the library

Three layers, each depending only on the ones below it.

```
revisions/          rev_dat2_osrs230.c, rev_dat1_lc254.c, …
                    One explicit profile per supported revision, plus a registry.
                    Declares identity: game, container, epoch, revision, quirks.

datatypes/          dat2_config_loc.c, model.c, dat2_component.c, sound_synth.c, …
                    Per-type codecs. Each owns its own
                      RSCache_<Type>Flags(cache)         — field-level era gates
                      RSCache_<Type>CodecVersion(cache)  — whole-codec selection
                    and the shared decode_<t>_vN / encode_<t>_vN bodies.

primitives          rsbuffer, checksum, whirlpool, compression,
                    archive, filelist, reference_table, dat1disk, dat2disk,
                    xtea_config, rscache_profile
```

The organising idea is borrowed from [rsprot](https://github.com/blurite/rsprot):
a revision is *declared*, not inferred, so "what is **OSRS** revision 230" is
answered by opening `rev_dat2_osrs230.c`. It differs in one deliberate way — rsprot
copy-forwards a complete tree per revision, which is affordable on the JVM; here a
revision module is a thin declaration and the codecs are shared and versioned,
because duplicating ~19k lines of C per revision is not.

`include/rscache.h` is the only public header; it pulls in everything else.
`rscache_unity.c` is the single translation unit the build compiles.

---

## Common ground: sectors and index files

Both containers store archives as a linked list of fixed-size sectors, addressed
through a separate index file. dat1 and dat2 share this machinery entirely — the
same reader serves both.

### Index record — 6 bytes, in `main_file_cache.idx<N>`

The record for archive `id` sits at byte offset `id * 6`. There is no count or
header; the file's length implies how many archives a table has.

| Offset | Size | Field |
|---|---|---|
| 0 | u24 | archive length in bytes (the container, compressed) |
| 3 | u24 | first sector number |

A record of all zeros means "no such archive". `RSCache_Dat2DiskIndexFileReadRecord`
rejects `length <= 0 || sector <= 0`, which has a consequence for writers: **nothing
may be stored at sector 0**, or its index entry is indistinguishable from an absent
one. `RSCache_Dat2DiskWriteArchive` reserves sector 0 on a fresh file for exactly
this reason.

### Sector — 520 bytes, in `main_file_cache.dat`/`.dat2`

Sector `n` begins at byte offset `n * 520`. Sectors of one archive need not be
contiguous; each names its successor.

Header is 8 bytes normally, but **10 bytes when the archive id exceeds 0xFFFF**,
widening the id field from u16 to u32:

| Field | 8-byte header | 10-byte header |
|---|---|---|
| archive id | u16 @ 0 | u32 @ 0 |
| part number | u16 @ 2 | u16 @ 4 |
| next sector | u24 @ 4 | u24 @ 6 |
| table id | u8 @ 7 | u8 @ 9 |

Payload is the remaining 512 or 510 bytes. A reader walks the chain until it has
collected the length the index record promised, checking that the archive id, table
id and part number of each sector match what it expected — those fields exist to
catch a corrupt or misaddressed chain, not to be trusted for addressing.

### The dat1 off-by-one

dat1 index files are `main_file_cache.idx0` through `.idx4` — 0-based, exactly like
dat2 — but a dat1 **sector header stores the table id one higher** than the index
file it was reached through. `RSCache_Dat2DiskDatFileReadArchive` bridges this by
passing `index_id + 1` as the expected header value before delegating to the dat2
reader; see the comment in [dat2disk.c](src/dat2disk.c). The filename is not
affected. Get it wrong and every sector's table-id check fails while the addressing
still looks correct.

---

## dat2 — the JS5 container

### Files

```
main_file_cache.dat2       every archive's bytes, as 520-byte sectors
main_file_cache.idx0       index for table 0  (animations)
main_file_cache.idx1..24   … one per table
main_file_cache.idx255     index for the master table
xteas.json                 optional: XTEA keys for encrypted map archives
```

### Tables

| Id | Contents | Id | Contents |
|---|---|---|---|
| 0 | animations (frames) | 13 | fonts |
| 1 | skeletons (framemaps) | 14 | music samples |
| 2 | **configs** | 15 | music patches |
| 3 | interfaces | 18 | worldmap geography |
| 4 | sound effects | 19 | worldmap |
| 5 | maps | 20 | worldmap ground |
| 6 | music tracks | 21 | dbtable index |
| 7 | models | 22 | animayas |
| 8 | sprites | 24 | gamevals |
| 9 | textures | | |
| 10 | binary | **255** | **master index** |
| 11 | music jingles | | |
| 12 | clientscript | | |

Ids 0–15 are shared between OSRS and RS2; **16+ mean different things** on each
branch (e.g. 18 is worldmap geography on OSRS and npcs on RS2). See
[dat2disk.h](src/dat2disk.h).

Table 2 (configs) is itself subdivided: archive `k` within it holds all records of
config kind `k`, one file per record id. The kinds are in
[dat2_configs.h](src/datatypes/dat2_configs.h).

| Kind | Type | Decoder | Kind | Type | Decoder |
|---|---|---|---|---|---|
| 1 | underlay | yes | 16 | varplayer | yes |
| 3 | identkit | yes | 19 | varclient | yes |
| 4 | overlay | yes | 32 | hitsplat | yes |
| 5 | inv | yes | 33 | healthbar | yes |
| 6 | locs | yes | 34 | struct | yes |
| 8 | enum | yes | 35 | area / mapelement | yes |
| 9 | npc | yes | 38 | dbrow | yes |
| 10 | obj | yes | 39 | dbtable | yes |
| 11 | params | yes | | | |
| 12 | sequence | yes | | | |
| 13 | spotanim | yes | | | |
| 14 | varbit | yes | | | |
| 15 | varclient string | **no** | | | |

Seven of these had no decoder until recently, and they were not obsolete or
era-specific: every one is present and populated in every OSRS cache, and `varbit` holds
more records than `npc`. The enum was transcribed from RuneLite's `ConfigType` in full,
and decoders were written only for the types something drew. Six are now done; only
`varclient string` remains, and it is the one this corpus genuinely cannot validate. See
below.

### The variable and inventory config kinds

`varbit` (14), `varplayer` (16), `varclient` (19) and `inv` (5) live in
`dat2_config_var.c` and `dat2_config_inv.c`. Each round-trips at **100% byte-exact**
over the whole corpus with exact consumption asserted, which fixed-shape records make
possible.

Record counts, measured across the corpus:

| Kind | Type | 643 | kronos | osrs184 | osrs230 | osrs239 | jan2026 |
|---|---|---|---|---|---|---|---|
| 14 | varbit | – | 9488 | 9510 | 17426 | 19008 | **19650** |
| 16 | varplayer | 2121 | 2600 | 2604 | 4729 | 5705 | 5219 |
| 19 | varclient | 1514 | 381 | 382 | 1259 | 1505 | 1345 |
| 5 | inv | 609 | 633 | 633 | 920 | 1026 | 1003 |
| 32 | hitsplat | 2008 | 14 | 14 | 78 | 83 | 82 |
| 33 | healthbar | 177 | 12 | 12 | 81 | 85 | 83 |
| 15 | varclient string | 351 | 0 | – | – | 8 | – |

643's dashes are not empty groups — they are `Failed to read dat2 index entry for
table 2 archive N`. The 643 branch shards its configs differently, so that column is
not comparable to the OSRS ones.

**What each is, and when it is read.**

- **varbit** — a named bit range inside a varp: `basevar`, `startbit`, `endbit`. This
  is how the game packs many small values (quest stages, setting toggles, diary
  progress) into a shared player variable. Read whenever a script or interface asks for
  one: the CS2 `read varbit` op, IF1 button handlers, and CS2 hook triggers.
- **varplayer** — the *type* of a player variable. The only client-relevant field is
  `clientcode`, which ties a varp to built-in client behaviour (run energy, weight,
  chat filters). Read on a varp change to drive the client-side side effect.
- **varclient** / **varclient string** — client-only variables, never server-synced.
  The config says which ones persist across a logout. CS2 reads and writes these
  constantly for UI state: selected tab, scroll offsets, options.
- **inv** — the capacity of each inventory id (player inventory, bank, shops,
  equipment). Needed by every inventory operation that has to know the container size.
- **hitsplat** — how a damage splat is drawn: sprites, font, colours, offsets,
  duration. Read on every combat hit.
- **healthbar** — how an overhead health bar is drawn: front and back sprite ids,
  width, duration.

**How the four settled formats were established.** Every record of the group in five
caches was dumped and the operand widths brute-forced over 0-4. In each case **exactly
one** assignment consumes 100% of records, so the widths are pinned rather than assumed;
the same sweep proves the opcode *sets* complete, since a record holding any other
opcode would have failed to parse and none did. `varbit` and `varplayer` also match
`Client-TS/src/config/VarBitType.ts` and `VarpType.ts`, the dat1 form of the same types.

| Kind | Opcodes | Record shape |
|---|---|---|
| 14 varbit | `1` = u16 basevar, u8 startbit, u8 endbit · `10` = debugname (dat1 only) | fixed 6 bytes |
| 5 inv | `2` = u16 size | fixed 4 bytes |
| 16 varplayer | `5` = u16 clientcode | 1 byte (empty) or 4 |
| 19 varclient | `2` = flag, no operand · `3` = u16 | 1, 2 or 5 bytes |

Two details in there are load-bearing rather than incidental:

- **A varbit with no base variable decodes to -1, not 0.** varplayer 0 is a real
  variable, so the two must not be confused.
- **varclient tracks opcode 3's *presence* separately from its value.** Forty records
  carry it with an explicit zero, so keying the encoder on the value would drop the
  opcode and shorten the record.

The decoders take a **cursor**, not a `(data, size)` pair, with a thin per-record
wrapper on top. That is what lets one codec serve both eras: dat2 splits a group into
one file per id, while dat1 stores the same records back to back in a single
`varbit.dat` blob behind a u16 count. On an opcode it does not know, a decoder stops
rather than guessing an operand width, which leaves `_consumed` short of the record —
the signal the round-trip harness asserts on, so an unrecognised field surfaces as a
test failure instead of silently misaligning everything after it.

**healthbar needed a second constraint, because exact consumption could not settle it**
— the first place in this library that technique came up short. Brute-forcing its seven
opcodes yields **41 assignments that all consume 100%** of 85 distinct records; only
`7 -> u16`, `11 -> u16` and `14 -> u8` are common to all of them. The records are short
with few opcodes, so there is room to re-segment them into a different but
equally-consuming parse — the opposite of loc, npc and spotanim, where long varied
records made consumption decisive.

What settled it: **an operand larger than the biggest sprite id in its own cache cannot
be an id, a width, a duration or a colour index** — it is a wrong width read across a
field boundary. That cuts 41 candidates to exactly one:

| Kind | Opcodes | Packing order |
|---|---|---|
| 33 healthbar | `2` = u8 · `3` = u8 · `5` = u16 · `7` = u16 sprite · `8` = u16 sprite · `11` = u16 · `14` = u8 | 7, 8, 2, 3, 5, 11, 14 — *not* ascending |

Note the order: every record leads with the two sprite ids and only then drops to the
low opcodes. Opcodes 7 and 8 hold values that are all valid sprite ids in adjacent pairs
(0x0587/0x0588), which is what a front-and-back bar pair looks like — though which is
the front is not established, so they are named `sprite_id_a`/`sprite_id_b`. The other
five keep their opcode numbers as names: the widths are known, the meanings are not, and
inventing names would be a guess dressed as knowledge.

Derived from osrs230, osrs239 and jan2026 — and byte-exact on kronos and osrs184 too,
which were never in the search.

**hitsplat needed the same constraint plus a wider search.** Bounding operand widths at
4 bytes yields **zero** assignments that consume the file — which looks like a statement
about the format but is really a statement about the search: **opcode 18 carries an
11-byte composite payload**, and a search that cannot represent it fails everywhere
rather than pointing at it.

Reading the records found it where searching could not. The shortest is unambiguous by
inspection — `05 0d c1 | 08 00 00 | 09 00 96 | 00` is three u16 opcodes and a terminator
— and the longest, `09 00 32 | 12 <11 bytes> | 00`, only balances at eleven. With opcode
18 allowed up to 16 wide, and the plausibility rule applied to every scalar width rather
than just 1-2 bytes, exactly one assignment survives:

| Kind | Opcodes |
|---|---|
| 32 hitsplat | `5` = u16 sprite id · `8` = u16 · `9` = u16 · `11` = flag · `13` = u16 · `18` = 11-byte composite · `49` = u8 |

Two things are carried rather than interpreted. **Opcode order is per record** — `5,8,9`,
`5,8,11,9`, `8,49,5,9`, `8,49,5,9,13` and `9,18` all occur, which look like distinct kinds
of splat — so the order is recorded and replayed. And **opcode 18's payload stays raw**:
it looks like `u16, i16, u16, u8, u16, u16` (bytes 2-3 are `ff ff` on every record, an
i16 `-1` sentinel, and the last three fields track each other), but that is suggestive
rather than established, and round-tripping needs the bytes, not their names.

**varclient string cannot be validated here at all.** The group is absent from
`cache.osrs230` and `cache.jan2026`, empty in `cache.kronos`, and the 8 records in
`cache.osrs239` do not look like string variables — 27 to 113 bytes, with runs of
ascending u16s and opcodes up to 0x21. Either the id means something else by 239 or the
type changed; 643 has 351 records but a different table-2 mapping, so it cannot
corroborate either.

### Table 255 — the master index

Table 255 is read exactly like any other table, but its archive `N` contains the
**reference table** for table `N`: per-archive CRCs, versions, child file ids and
optionally names. Nothing else records how many archives a table has or what files
they contain, so a cache is unusable without it.

### Reference table

After the container is decompressed:

| Field | Encoding | Notes |
|---|---|---|
| format | u8 | 5, 6 or 7 |
| version | i32 | **only when format ≥ 6** |
| flags | u8 | see below |
| archive count | u16, or usmart if format ≥ 7 | |
| archive ids | count × (u16 / usmart) | **deltas**, accumulated |
| identifiers | count × i32 | only if flag `0x1` |
| CRCs | count × i32 | reflected CRC-32 of the *container* bytes |
| whirlpools | count × 64 bytes | only if flag `0x2` |
| sizes | count × (i32 compressed, i32 uncompressed) | only if flag `0x4` |
| versions | count × i32 | |
| child counts | count × (u16 / usmart) | |
| child ids | per archive, count × (u16 / usmart) | **deltas** |
| child identifiers | per archive, count × i32 | only if flag `0x1` |

Flags: `0x1` IDENTIFIERS (name hashes present), `0x2` WHIRLPOOL, `0x4` SIZES,
`0x8` HASH.

Note the field *order* is not the struct's declaration order, and the whirlpool
block sits between the CRCs and the sizes. Both halves of a codec pair have to
agree on that or every subsequent field shifts.

**Format 7** is not merely "6 plus a field" — it replaces every count and delta in
the table with a usmart (u16 when the high bit is clear, else u31), which is why a
format-7 table cannot be read with a format-6 reader at all.

Measured across the caches here:

| Cache | Format | Flags seen |
|---|---|---|
| `cache`, `cache.jan2026`, `cache.osrs230`, `cache.osrs239` | 7 | 4, 5 |
| `cache.643` | 6 | 0, 1 |
| `cache.kronos`, `cache.osrs184` | 5 and 6 mixed | 0, 1 |

**No cache in this repo sets the whirlpool flag.** The digest is implemented
(`RSCache_Whirlpool`) so a cache that does set it round-trips, but nothing here
exercises it against real data.

### Archive container

Every archive — including reference tables — is wrapped like this:

| Field | Encoding | Notes |
|---|---|---|
| compression | u8 | 0 none, 1 bzip2, 2 gzip, 5 "non-OSRS packed" |
| compressed length | i32 | payload bytes only |
| uncompressed length | i32 | **absent when compression is 0** |
| payload | bytes | |

XTEA, when a key applies, covers `compressed length + 4` bytes starting
immediately after the compressed-length field — that is, the uncompressed-length
field plus the payload. XTEA operates on 8-byte blocks and leaves a trailing
partial block in the clear.

Compression `5` is a non-OSRS variant that behaves like 0 but **skips
decryption**; see `NON_OSRS_PACKED_ARCHIVE_FORMAT` in [archive.c](src/archive.c).

Map archives in table 5 are the ones that are usually encrypted. Keys come from
`xteas.json` via [xtea_config.c](src/xtea_config.c), which is a process-global
table rather than a property of the open cache.

### Group → files

An archive in a config-like table is a *group* holding many numbered files. After
decompression:

- **A single-file group is the file.** No table, no trailer. Readers special-case
  `file_count == 1`, so appending a size table to a one-file group corrupts it.
- **Otherwise**: all payloads concatenated, then `chunks × file_count` big-endian
  i32 size deltas, then a final u8 chunk count. Sizes accumulate within each
  chunk; a file's total is the sum of its per-chunk sizes.

Multiple chunks let Jagex interleave partial updates. One chunk is the normal and
simplest encoding, and what `RSCache_FileListEncode` writes.

> **The position/id trap.** `RSCache_FileList.files[]` is indexed by **position**
> in the group, not by cache file id. Most config groups are dense and 0-based so
> the two coincide — but animation frame archives are 1-based and can be sparse.
> Map an id to a position through `RSCache_Dat2DiskArchive.file_ids[]` first.
> Indexing by id on a sparse group silently reads the wrong file.

Name hashes in dat2 are djb2: `hash = (hash << 5) - hash + c`.

---

## dat1 — the jagfile container

### Files

```
main_file_cache.dat        archives, as 520-byte sectors
main_file_cache.idx0..4    one index per table
```

### Tables

| Table | Contents | Archive compression |
|---|---|---|
| 0 (`.idx0`) | **configs** — jagfiles | per the jagfile header (bzip2) |
| 1 (`.idx1`) | models | whole-archive gzip |
| 2 (`.idx2`) | animations | whole-archive gzip |
| 3 (`.idx3`) | sounds | whole-archive gzip |
| 4 (`.idx4`) | maps | whole-archive gzip |

The configs table holds eight jagfiles: 1 title & fonts, 2 configs, 3 interfaces,
4 2D media, 5 version list, 6 textures, 7 chat system, 8 sound effects. Their
member files are catalogued in [dat1disk.h](src/dat1disk.h).

The non-config tables are addressed through the **version list** jagfile rather
than a reference table. It holds `model_version`, `anim_version`, `midi_version`,
`map_version` (u16 per file), matching `*_crc` files (i32 per file), and index
files — of which `map_index` is the important one: 7 bytes per entry, giving map
square id, land archive, loc archive and a members flag.

### Jagfile

The container inside a configs-table archive:

| Offset | Size | Field |
|---|---|---|
| 0 | u24 | uncompressed size |
| 3 | u24 | compressed size |

**If the two differ**, the entire remainder — file table included — is one bzip2
stream, and each member's bytes sit inside it uncompressed. **If they are equal**,
the remainder is plain and each member is *individually* bzip2 compressed.

Then:

| Size | Field |
|---|---|
| u16 | file count |
| per file, 10 bytes: | i32 name hash, u24 uncompressed size, u24 compressed size |
| | then the payloads, in order |

> **There is no stored mode.** Both arrangements mandate bzip2 — a reader always
> calls bunzip, on either the whole body or each member. This is why authoring a
> jagfile needs a bzip2 *compressor*, and why `3rd/bzip/bzip_encode.c` exists at
> all: the vendored `bzip.c` is decompress-only.

Jagfiles store only name **hashes**, never names, using an uppercase rolling
polynomial: `hash = hash * 61 + toupper(c) - 32`. A round trip preserves lookup by
name but cannot recover the original strings.

RuneScape's bzip2 streams have the **4-byte `BZh1` magic stripped**; the client
prepends it before decompressing. Block size is always 1 (100k). An encoder must
emit a level-1 stream and remove those four bytes —
`RSCache_CompressionBzipCompress` does both.

### `.dat` / `.idx` record pairs

Inside the configs jagfile, config types are stored as a pair: `obj.dat` holding
concatenated variable-length records and `obj.idx` giving their extents.

The `.idx` is a u16 entry count followed by one u16 per entry — each the **length**
of that record. Absolute offsets accumulate from a base of **2**, so record 0
starts at `.dat` offset 2.

`flo.dat` has no `.idx` and is only addressable sequentially, which is why
`RSCache_FileListDatIndexed` covers some config types and not others.

Note that nothing stores an offset past the final record, so the last entry's
length is not recoverable from decoded offsets alone; the encoder derives it from
the `.dat` size.

---

## The revision model

Field layouts change between game revisions, and identifying "which layout" is the
subtlest part of reading a cache. Identity is **stated, never detected**.

### Four load-bearing fields

`struct RSCache` ([rscache_profile.h](src/rscache_profile.h)) carries:

| Field | Values | Meaning |
|---|---|---|
| `game` | `oldschool` \| `rs2` | Which revision lineage `revision` is numbered in |
| `epoch` | `dat1` \| `dat2` | On-disk container (jagfile vs js5) |
| `revision` | integer | Game revision within `game`'s lineage |
| `quirks` | `none` \| `kronos` \| `void_rs634_no_xteas` | Client-build overrides that no revision number implies |

The three lineages this library cares about are `(game, epoch)` pairs:

| Lineage | `(game, epoch)` | Example revisions | Notes |
|---|---|---|---|
| dat1 classic | `(rs2, dat1)` | ~200–254 | 2004-era sequence; continuous with RS2 |
| RS2 / main | `(rs2, dat2)` | 377 onward; **643** | Same lineage as dat1 across the container change |
| OldSchool | `(oldschool, dat2)` | ~184–239 in this corpus | Restarted at 1 in 2013 |

A bare numeric comparison across `game` values is meaningless (OSRS rev N ≠ RS2 rev N).
That bug was D16 / D20 / D25 in [EXCEPTIONS.md](EXCEPTIONS.md).

Boot manifests state all four under `[cache:boot]`; the client builds the profile with
`RSCache_ProfileForIdentity`. `[net:boot] client_version=` is the login-block value only
and does not select the cache profile.

### Archive revision is a different quantity

Separately, each reference-table entry has an **archive revision** — a
**per-archive counter**, and the only era signal available when nobody told you
the game revision. It is treacherous for two reasons:

1. **It is per-archive, not per-cache.** The npc, sequence and loc config groups
   all live in table 2 yet carry unrelated counters, which is why the reference
   client's thresholds for them are unrelated numbers. Measured here:

   | Cache | npc | loc | seq | obj |
   |---|---|---|---|---|
   | `cache` | 1691 | 1820 | 1368 | 1591 |
   | `cache.kronos` | 1362 | 1315 | 786 | 1969 |
   | `cache.osrs184` | 1203 | 1262 | 779 | 968 |
   | `cache.osrs230` | 1688 | 1815 | 1360 | 1583 |
   | `cache.osrs239` | 1784638223 | 1784638222 | 1784638224 | 1784638224 |

2. **Its units changed.** Old caches use small integers; modern OSRS stores a unix
   timestamp. A threshold written for one convention is meaningless under the
   other.

### How the library resolves it

Two lineage predicates replace every open-coded field test:

```c
bool RSCache_RevisionAtLeastOsrs(cache, type, game_rev, archive_rev_threshold,
                                 default_when_unknown);
bool RSCache_RevisionAtLeastRs2(cache, type, game_rev, archive_rev_threshold,
                                default_when_unknown);
```

Each prefers the declared game revision when `game` matches; falls back to the
group's archive revision against the reference client's own threshold; and
otherwise returns the caller's stated default. An UNSET profile satisfies neither.
Derived helpers: `RSCache_IsDat1`, `RSCache_IsOsrs`, `RSCache_IsRs2Dat2`.

### Supported named profiles

| Name | game | epoch | revision |
|---|---|---|---|
| `lc254`, `lc245_2` | rs2 | dat1 | 254 / 245 |
| `osrs184`, `kronos` | oldschool | dat2 | 184 (+ Kronos quirk) |
| `osrs230`, `osrs233`, `xrsps233`, `osrs239` | oldschool | dat2 | 230 / 233 / 239 |
| `643`, `rs643` | rs2 | dat2 | 643 |

### Identity is stated, never detected

`game`, `epoch`, `revision` and `quirks` cannot be recovered reliably from bytes on
disk: 643-era caches number their reference tables in the same small-integer range
OSRS used before timestamps, and revision numbers collide across lineages.
Whoever opens the cache has to say which it is — via a named profile, via
`RSCache_ProfileForIdentity`, or via required `[cache:boot]` keys. Tools take the
same four flags (`--game/--epoch/--revision/--quirks`) or `--rev <name>`.

### Known era gates

| Datatype | Gate | Effect |
|---|---|---|
| loc | OSRS game 220 / archive 2000 | opcode 93 becomes sound fades, 95/96 carry a byte |
| npc | OSRS game 210 / archive 1493 | opcode 102 becomes a head-icon bitfield |
| sequence | OSRS game 220 & 226 / archive 1141 & 1268 | three distinct frame-sound records |
| texture | OSRS game 233 / archive 2000 | simplified 7-byte single-sprite record |
| component | OSRS game 237 | type-6 model ids widen u16 → i32 |
| component | RS2 dat2 | type-5 carries a colour int; flips reorder |
| loc, and others | epoch dat1 | strings are newline- not NUL-terminated |
| map terrain | not OldSchool | tile attributes / overlay ids stay u8 (OSRS widens at 209) |
| map locs | OldSchool game ≥ 237 | archives stored plain; no keys shipped or applied |
| map locs | RS2 dat2 game ≥ 414 | archives XTEA-encrypted (dat2 itself from ~377; XTEA from 414) |
| map locs | quirk VOID_RS634_NO_XTEAS | Void 634 pre-stripped keys; treat locs as plain |
| map tables | OldSchool caches without archive identifiers | one multi-file archive per region id (`(x<<8)|z`); file 0 = terrain, file 1 = locs (named `mX_Z`/`lX_Z` lookup is preferred when identifiers exist) |
| loc | quirk KRONOS | opcodes 78/79 omit a byte |
| sound | RS2 dat1 game 377 | tone records gain a trailing filter + its envelope; the mixer clamps instead of wrapping |

Map XTEA cannot be answered from the key file. Pre-237 OldSchool caches ship keys;
`cache.osrs239` ships none because the archives are plain. RS2 dat2 caches from 414
(including 643) ship keys; Void's modified 634 cache does not (`void_rs634_no_xteas`).
Sniffing "is there a key for this square?" fails both
ways: missing keys on an encrypted square yield zero locs, and applying a key to
plain data corrupts silently. `RSCache_MapLocsEncrypted` is the gate.

Model format is the exception: it is identified by a magic trailer in the last two
bytes (`FF FF` ob3, `FF FE` OSRS extended, `FF FD` OSRS material, otherwise ob2)
and ignores the revision entirely.

### Known decode gaps

Honest inventory of what the decoders do *not* handle, all found by exact
consumption over the corpus:

- ~~**Revision 239 records.**~~ Closed. `cache.osrs239` now reaches exact
  consumption for both types this entry named: npc 16292/16292 and spotanim
  4010/4010. The layouts the entry called unimplemented are the ones the rev-237
  opcode gates and the count-prefixed colour lists cover (opcodes 61/62 for int
  model ids, 251–253 for entity ops, 40/41 for recolour/retexture). Kept as a
  crossed-out line rather than deleted because the numbers were cited elsewhere:
  `tools/port_npc` refused revision 239 outright on the strength of them, and now
  checks the individual record's consumed length instead.
- **Jagex-compressed audio samples.** 122 of `cache.osrs239`'s 12,010
  sound-effect archives hold "BCV" compressed audio rather than a synth program
  (119 alongside one as group file 1, 3 on their own).
  `RSCache_SoundSampleKindOf` identifies them so a caller skips rather than
  decodes noise; the codec itself is not implemented — it needs a Vorbis
  implementation plus its shared codebook, and rt4's `VorbisSound` header layout
  does not match these bytes. Every synth program in every cache decodes and
  re-encodes byte-exactly.
- **Lossy config decoders.** Several decoders consume fields without storing them,
  so those records cannot re-encode byte-exactly: enum drops opcodes 1, 7 and 8;
  param does not record whether the type arrived via opcode 1 or 8; mapelement
  keeps only the fields the MEC_* scripts read. All still round-trip
  *semantically*. Cache strings are windows-1252 wire bytes end-to-end
  (`gcstring` / `pjstr` are byte-transparent); use `RSCache_Cp1252ToUtf8` when a
  Unicode form is needed.

### A worked example: how spotanim was wrong

Worth recording because it shows what exact consumption buys. The dat2 spotanim
decoder treated opcodes 40–79 as "one colour slot per opcode, u16 each". Modern
records instead carry **count-prefixed lists** — opcode 40 is `u8 count` then
`count × (u16 from, u16 to)` — and some caches add an **opcode 9 name string**
(`cache.osrs230` has `soul_wars` on spotanim 0; stock `cache.jan2026` does not).

Reading a bare u16 at opcode 40 desynced the rest of the record, and the decoder
bailed on whatever byte came next with `Unrecognized dat2 spotanim opcode 210`.
Because opcodes 1 and 2 (model and animation) come *first*, those fields were
correct and spotanims mostly looked fine — every recolour was silently lost. Exact
consumption was 0/3295 on `cache.osrs230` before the fix and 3295/3295 after.

The same pass found the arrays were 6 wide while the opcode ranges are 10, so
opcode 46 wrote past `recol_s` into `recol_d`.

---

## Model stream layouts

Worth its own section because models are the one datatype whose format is **not** a
revision property. It is stamped on the file, in the last two bytes, and a single
cache holds a mix — `cache.osrs230` is 89% V2 and 11% V3. So a decode sniffs the magic
rather than consulting the profile, and `RSCache_ModelCodecVersion` answers only "what
would this cache write?".

| Magic | Format | Trailer | Adds |
|---|---|---|---|
| *(none)* | OB2 | 18 | the 2004 baseline; texture ids packed into the per-face info byte |
| `FF FF` | OB3 | 23 | texture render types, a separate face-texture section, complex/cube texture mapping |
| `FF FE` | V2 | 23 | OB2's section order plus skeletal (animaya) skin data |
| `FF FD` | V3 | 26 | OB3's section order plus animaya |

A model body is a set of **parallel column sections**, not a record stream: the trailer
holds the *byte counts* of four of them, and the decoder walks up to eight cursors over
one buffer at once. There are two section orders, not four:

```
OB2, V2   vertex flags · face index types · [priorities] · [face skins] · [face infos]
          · [vertex skins (+animaya)] · [alphas] · index deltas · colours
          · texture p/m/n · X · Y · Z · tail

OB3, V3   render types · vertex flags · [face infos] · face index types · [priorities]
          · [face skins] · [vertex skins (+animaya)] · [alphas] · index deltas
          · [face textures] · texture coords · colours · X · Y · Z
          · simple texture p/m/n · tail
```

Three things about the encoding are worth knowing before touching it:

- **Vertex positions are delta-encoded** with a per-vertex flag byte naming which axes
  carry a delta, and the three axes go to three *separate* streams. The flag bit is set
  exactly when the delta is non-zero — verified over ~377,000 models — so it is
  derived, not stored.
- **Face indices reuse the previous face's** under one of four schemes, chosen per
  face. The scheme is in the stream but not recoverable from the decoded model, since
  more than one can express the same triangle. Type 1 spells all three out and is
  always legal, so it is the fallback.
- **The tail is not decoded.** Every format except OB2 ends with a gate byte
  introducing a further per-face block, then a flag byte introducing 10 more; OB3/V3
  also keep their complex and cube texture mapping payloads there. See B11 in
  `EXCEPTIONS.md`.

## CS2 — the clientscript language

Table 12 holds ClientScript2 bytecode: the language OldSchool interfaces are
scripted in. `src/cs2/` is a decompiler and a compiler for it, sitting on top of
the `clientscript` codec that reads and writes the container.

```
cs2_command.gen.h   opcode <-> name, and each one's signature (generated)
cs2_types.c         the type system: types, stack types, triggers, prototypes
cs2_interp.c        bytecode -> IR, by simulating the operand stack
cs2_dfa.c           nine data-flow passes: inlining, type and identifier solving
cs2_cfa.c           IR -> if/while/switch, via a dominator tree
cs2_gen.c           structured IR -> source text
cs2_compile.c       source text -> bytecode
cs2_names.c         id -> name tables, loaded at run time
```

The decompiler is a port of [RuneStar/cs2](https://github.com/RuneStar/cs2) and
is checked the only way that means anything: against that implementation's own
output. `test_cs2` decompiles all 7,884 scripts in its dump and compares byte for
byte — **6,489 of the 6,491 the reference also produced are identical**, and the
two that differ are one identifier chosen by Java hash order (EXCEPTIONS.md G1).

The compiler has no upstream. It is checked by `cs2 roundtrip`, which compiles
the decompiled source and compares against the cache's own bytes.

Three things are worth knowing before using either half.

**An unknown opcode arity is fatal, on purpose.** The bytecode does not say how
many values an opcode pops. Get it wrong and nothing fails — the operand stack
shifts by one and every later argument belongs to a different call, producing a
readable decompile of a program that is not the one in the cache. So an opcode
with no recorded signature refuses the script. Arities come from RuneStar's
`Command.kt`, from this repo's own CS2 VM (`src/cs2vm2`, which knows opcodes the
2021 sources predate), and from `tools/cs2/local_commands.py` for anything
established here. 833 opcodes are signed; 7,553 of OSRS 230's 7,884 scripts
decompile, and EXCEPTIONS.md G4 accounts for the rest.

**Names are not in the cache.** Ids are; `coins_995` and `^iftype_rectangle` come
from community-recovered tables loaded at run time from a directory. Without them
a decompile is correct but reads `obj_995`. Four types — `boolean`, `stat`,
`maparea`, `fontmetrics` — have no numeric spelling in the language at all, so
they are the one case where a missing table refuses the script.

**Source is UTF-8; the cache is windows-1252.** String constants are converted on
the way out and back on the way in, through the bijection in `rsbuffer.h`. This
is the one place the library treats cache strings as text rather than as bytes
(A2), because a `.cs2` file is text: a `0xA0` in a message is a non-breaking
space, and emitting it raw makes the listing invalid UTF-8.

## Writing a cache

The write path mirrors the read path stage for stage.

```
  your struct
      │  RSCache_<Type>Encode(cache, …)          per-datatype encoder
      ▼
  record bytes
      │  RSCache_FileListEncode                  dat2: group + size table
      │  RSCache_FileListDatEncode               dat1: jagfile (bzip2 required)
      ▼
  group bytes
      │  RSCache_ArchiveEncode                   compression byte, lengths, XTEA
      ▼
  container bytes
      │  RSCache_Dat2DiskWriteArchive            sectors + index record
      ▼
  main_file_cache.dat2 + .idx<N>
```

Then the reference table for each touched table needs rebuilding — CRC over the
*container* bytes, sizes, and a bumped version — encoded with
`RSCache_ReferenceTableEncode` and written into table 255 as archive `N`.

Available primitives:

| Need | Function |
|---|---|
| grow-on-write buffer | `RSCache_BufferInitAlloc` / `Detach` / `Release` |
| every `p` writer | `p1 p1b p2 p2b p3 p4 p8 pf pusmart pbigsmart pshortsmart pushortsmart puintsmartshortcompat pvarint2 pparams pjstr pbuf` |
| reflected CRC-32 | `RSCache_Crc32` — for reference tables |
| bzip2 CRC-32 | `RSCache_Crc32Bzip2` — *different function*, internal to bzip2 |
| whirlpool | `RSCache_Whirlpool` |
| gzip | `RSCache_CompressionGzipCompress` |
| bzip2 | `RSCache_CompressionBzipCompress` — magic stripped, level 1 |
| XTEA | `xteas_encrypt` |

Two cautions:

- The two CRC-32s are not interchangeable. A reference table wants the reflected
  (zlib/ISO-HDLC) variant over the container bytes; bzip2's own block checksums use
  the unreflected variant. They agree on no non-empty input.
- `RSCache_Dat2DiskWriteArchive` appends and re-points, orphaning the old sectors.
  Repacking in place grows the file; write to a fresh directory to compact.

### Cross-cache porting

Per-datatype encode/decode is the seam: decode with the source profile, encode
with the destination's. The library will not invent fields the destination
revision added — that is a caller decision.

For NPCs (and their models, sequences, frames, framemaps, and BasType), the
offline tools in [`tools/`](tools/README.md) implement that caller:

```sh
make -C 3rd/rscache tools
./3rd/rscache/tools/find_anims/find_anims --rev osrs230 cache.osrs230 --npc 1
./3rd/rscache/tools/port_npc/port_npc \
  --from-rev osrs230 cache.osrs230 --to-rev rs643 cache.rs643 \
  --npc 1 --out /tmp/cache.out   # dry-run; add --apply to write
```

Group-edit helpers used by those tools live in `cache_edit.h`
(`RSCache_Dat2Edit*`, `RSCache_Dat1Edit*`). Dat1 destinations also need
`RSCache_Dat1DiskWriteArchive` and the dat1 npc/seq/anim encoders.

---

## Building and testing

rscache is a unity build: `rscache_unity.c` is one translation unit.

```sh
make -C 3rd/rscache          # compile, warnings on
make -C 3rd/rscache test     # unit tests + bzip2 interop
make -C 3rd/rscache clean
```

The client builds rscache with `-w`, so **the standalone build is the only place
rscache's own warnings are visible**. Keep it warning-clean: the shadowed
`rev210_head_icons` bug that misdecoded npc records in two caches had been showing
up as an unused-variable warning the whole time.

Tests:

| Suite | Covers |
|---|---|
| `test_rsbuffer` | every `g`/`p` pair, both sides of each smart-encoding width boundary, buffer ownership and growth |
| `test_profile` | era gates and codec selection at every threshold, via both the game-revision and archive-revision paths |
| `test_compression` | CRC vectors, XTEA, gzip and bzip2 round trips, Whirlpool test vectors |
| `test_container` | archive, group, jagfile, `.idx`, reference table round trips; and a synthetic cache written to disk and reopened |
| `test_roundtrip` | per-datatype encode/decode over every cache in the corpus |
| `test_cs2` | the CS2 decompiler against RuneStar/cs2's own output, script by script |
| `test_bzip_interop.sh` | our bzip2 streams verified by the reference `bzip2 -t` and `-dc` |

`test_roundtrip` takes the corpus location as its argument; `make test` passes the
repo root. Point it elsewhere with `make test CACHE_ROOT=/path/to/caches`. It skips
silently when no cache directories are found.

It reports three numbers per datatype, and the third is what makes the second
interpretable:

- **semantic** — asserted. Anything below 100% is an encoder bug.
- **exact** — `encode(decode(x)) == x`, byte for byte.
- **same-len** — the re-encode came out the same *length*. This separates the two
  quite different reasons a round trip is not byte-exact:

| same-len | exact | Diagnosis |
|---|---|---|
| high | low | **Field ordering.** Nothing lost or invented — the packer emits opcodes in an order of its own. Harmless: an opcode stream is order-independent to any conforming reader. |
| low | low | **Loss.** The decoder dropped a field it read, so it cannot be written back. |

Measured on `cache` (the other five caches are within a few points):

| Datatype | Records | Semantic | Exact | Same-len | Shortfall is |
|---|---|---|---|---|---|
| struct | 5,869 | 100% | 100% | 100% | — |
| underlay | 229 | 100% | 99% | 99% | — |
| overlay | 412 | 100% | 74% | 99% | ordering |
| idk | 307 | 100% | 41% | **100%** | ordering, entirely |
| obj | 30,928 | 100% | 45% | 91% | mostly ordering |
| spotanim | 3,295 | 100% | 87% | 98% | ordering |
| param | 2,277 | 100% | 85% | 87% | loss (opcode 8 type path) |
| enum | 5,678 | 100% | 56% | 56% | loss (opcodes 1, 7, 8) |
| mapelement | 1,027 | 100% | 0% | 0% | loss, by construction |

Jagex's obj packer writes, for example, `1, 7, 8, 4, 6, 5, 75, 39, 16, 2, 3` — the
strings last, not ascending. Reproducing that order exactly would raise the exact
column but change nothing a client can observe, so it has not been chased. The
lossy rows match what [known decode gaps](#known-decode-gaps) documents
independently.

What matters for regression purposes is that none of these columns *fall*.

The bzip2 encoder gets an interop test because agreement between our own encoder
and our own decoder would not rule out a shared misreading of the format.

For decoder changes, the strongest available check is **exact consumption**: decode
every record in a real cache and compare bytes consumed against the file size. A
decoder reading the wrong shape almost never lands exactly on its terminator, so
the count is a sharp signal. `_consumed` on the loc and npc structs exists for
this; `TORIRS_LOC_SCAN=1` runs it for locs.
