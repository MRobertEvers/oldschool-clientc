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
- [dat1 — the jagfile container](#dat1--the-jagfile-container)
- [The revision model](#the-revision-model)
- [Writing a cache](#writing-a-cache)
- [Building and testing](#building-and-testing)

---

## Layout of the library

Three layers, each depending only on the ones below it.

```
revisions/          rev_dat2_osrs230.c, rev_dat1_lc254.c, …
                    One explicit profile per supported revision, plus a registry.
                    Declares identity: game, container, epoch, revision, quirks.

datatypes/          dat2_config_loc.c, model.c, dat2_component.c, …
                    Per-type codecs. Each owns its own
                      RSCache_<Type>Flags(cache)         — field-level era gates
                      RSCache_<Type>CodecVersion(cache)  — whole-codec selection
                    and the shared decode_<t>_vN / encode_<t>_vN bodies.

primitives          rsbuffer, checksum, whirlpool, compression,
                    archive, filelist, reference_table, dat1disk, dat2disk,
                    xtea_config, rscache_profile
```

The organising idea is borrowed from [rsprot](https://github.com/blurite/rsprot):
a revision is *declared*, not inferred, so "what is revision 230" is answered by
opening one file. It differs in one deliberate way — rsprot copy-forwards a
complete tree per revision, which is affordable on the JVM; here a revision module
is a thin declaration and the codecs are shared and versioned, because duplicating
~19k lines of C per revision is not.

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

Table 2 (configs) is itself subdivided: archive `k` within it holds all records of
config kind `k`. The kinds are in [dat2_configs.h](src/datatypes/dat2_configs.h) —
1 underlay, 3 identkit, 4 overlay, 5 inv, 6 locs, 8 enum, 9 npc, 10 obj, 11 params,
12 sequence, 13 spotanim, 14 varbit, 15 varclient string, 16 varplayer,
19 varclient, 32 hitsplat, 33 healthbar, 34 struct, 35 area/mapelement, 38 dbrow,
39 dbtable.

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
subtlest part of reading a cache.

### Two different numbers

- **Game revision** — 230, 233, 254. What a manifest's `client_version` and the
  login handshake carry. Unambiguous.
- **Archive revision** — the `version` field of a reference-table entry. A
  **per-archive counter**, and the only era signal available when nobody told you
  the game revision.

The archive revision is treacherous for two reasons:

1. **It is per-archive, not per-cache.** The npc, sequence and loc config groups
   all live in table 2 yet carry unrelated counters, which is why the reference
   client's thresholds for them are unrelated numbers. Measured here:

   | Cache | npc | loc | seq | obj |
   |---|---|---|---|---|
   | `cache` | 1691 | 1820 | 1368 | 1591 |
   | `cache.jan2026` | 1767694599 | 1767694598 | 1767703645 | 1767694599 |
   | `cache.kronos` | 1362 | 1315 | 786 | 1969 |
   | `cache.osrs184` | 1203 | 1262 | 779 | 968 |
   | `cache.osrs230` | 1688 | 1815 | 1360 | 1583 |
   | `cache.osrs239` | 1784638223 | 1784638222 | 1784638224 | 1784638224 |

2. **Its units changed.** Old caches use small integers; modern OSRS stores a unix
   timestamp. A threshold written for one convention is meaningless under the
   other.

### How the library resolves it

`struct RSCache` ([rscache_profile.h](src/rscache_profile.h)) carries the game
revision when known, the per-group archive revisions, the container, the layout
epoch and any client-build quirks. One predicate resolves era questions:

```c
bool RSCache_RevisionAtLeast(cache, type, game_rev, archive_rev_threshold,
                             default_when_unknown);
```

It prefers the declared game revision; falls back to the group's archive revision
against the reference client's own threshold; and otherwise returns the caller's
stated default, so behaviour on an unidentifiable cache is a deliberate choice per
datatype rather than an accident.

### Epoch is not derivable

`epoch` distinguishes the OSRS and 643 layout families, and it exists as a separate
field because **the two cannot be told apart from any revision number**: 643-era
caches number their reference tables in the same small-integer range OSRS used
before it moved to timestamps. Whoever opens the cache has to say which it is.
`RSCache_ProfileDat2Rs643()` is how it says 643.

### Known era gates

| Datatype | Gate | Effect |
|---|---|---|
| loc | game 220 / archive 2000 | opcode 93 becomes sound fades, 95/96 carry a byte |
| npc | game 210 / archive 1493 | opcode 102 becomes a head-icon bitfield |
| sequence | game 220 & 226 / archive 1141 & 1268 | three distinct frame-sound records |
| texture | game 233 / archive 2000 | simplified 7-byte single-sprite record |
| component | game 237 | type-6 model ids widen u16 → i32 |
| component | epoch 643 | type-5 carries a colour int; flips reorder |
| loc, and others | container dat1 | strings are newline- not NUL-terminated |
| map terrain | container dat1 | tile attributes are u8 not u16 |
| loc | quirk KRONOS | opcodes 78/79 omit a byte |

Model format is the exception: it is identified by a magic trailer in the last two
bytes (`FF FF` ob3, `FF FE` OSRS extended, `FF FD` OSRS material, otherwise ob2)
and ignores the revision entirely.

### Known decode gaps

Honest inventory of what the decoders do *not* handle, all found by exact
consumption over the corpus:

- **Revision 239 records.** `cache.osrs239` fails exact consumption for npc
  (2462/16292 under either head-icon shape) and spotanim (0/4010). Revision 239
  changed layouts that are not implemented. No manifest references that cache — it
  is validation data — so the client is unaffected, but a port targeting 239 would
  need this closed first. The round-trip suite prints it as a KNOWN GAP on every
  run rather than asserting on it.
- **Lossy config decoders.** Several decoders consume fields without storing them,
  so those records cannot re-encode byte-exactly: enum drops opcodes 1, 7 and 8;
  param does not record whether the type arrived via opcode 1 or 8; mapelement
  keeps only the fields the MEC_* scripts read. All still round-trip
  *semantically*.
- **Strings in the 128–159 byte range.** The decoder maps them through a Unicode
  table and truncates to `char`, which is not reversible — see
  `RSCache_BufferPjstr`. Affects byte-exactness only, for the rare record carrying
  such a byte.

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

The library provides symmetric encode/decode per datatype and deliberately ships
no porting layer. To move an asset between caches, decode it with the source
profile and encode it with the destination's — the neutral struct in between is the
seam. What the library will *not* do is guess at fields the destination revision
added; that is a decision for the caller.

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
