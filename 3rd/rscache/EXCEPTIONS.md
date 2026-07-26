# rscache — exceptions register

Every place the read/write work departed from the plan, stopped short of complete,
or accepted a shortfall — with the reason, so none of it has to be re-derived or
re-litigated later.

Scope: the cache read/write expansion (revision-explicit structure, encoders,
container writers, README). Phases 1–5, 7 and 8 are complete; Phase 6 is complete
except for three types blocked on a reference client (B13).

Three kinds of entry:

- **Deviation** — the plan said one thing, the work did another.
- **Gap** — something knowingly incomplete, safe as it stands.
- **Open** — a real defect found and deliberately *not* fixed, needing a decision.

---

## A. Deviations from the approved plan

### A1. bzlib was not vendored; the bzip2 encoder is ours *(Deviation)*

The plan said "vendor the bzlib compressor". **No bzip2 sources exist on this
machine** — only the compiled library and `bzlib.h` (Homebrew's cellar has
`include`/`lib` and no `.c`; the SDK has `libbz2.tbd`). Reconstructing
`blocksort.c` from memory and presenting it as the reference implementation would
have been dishonest about its provenance.

Instead `3rd/bzip/bzip_encode.c` is an in-tree implementation. It also avoids a
system-libbz2 dependency, which would have been wrong for a library that targets
mingw, Emscripten and NXDK.

Trade-off accepted: the block sort is a plain comparison sort, not bzip2's tuned
`mainSort`/`fallbackSort`, and Huffman table selection is heuristic. Output is a
valid stream but **not byte-identical to what the `bzip2` binary produces**. Fine
for offline packing; would matter if someone expected reproducible-vs-Jagex
archives.

Verified against the reference binary rather than only against our own decoder:
`test_bzip_interop.sh` runs `bzip2 -t` (which checks the per-block and stream
CRCs) and `bzip2 -dc` over 9 inputs including 64 KB of urandom and real cache
bytes.

### A2. `pjstr` did **not** get a reverse cp1252 table *(Deviation)*

The plan called this a bug to fix. It is not fixable, and attempting it would have
caused corruption.

The reader maps input bytes 128–159 through a Unicode table then truncates each
result with `(char)`. That truncation is **not injective** — bytes 149 and 153 both
land on `0x22` — and its outputs collide with 14 printable ASCII bytes:
`space ! " & 0 9 : R S ` a x } ~`. Applying the inverse would rewrite every space
and every `'a'` in ordinary text.

Byte-transparency — what the function already did by accident — is the only correct
behaviour. What was actually wrong was the misleading dead if/else chain (four
identical branches); that was removed and the reasoning documented in the header.

Consequence: a string whose original bytes were in 128–159 cannot round-trip
byte-exactly. The information is lost at *decode*, before any encoder runs.

### A3. `gbit`/`pbit` were not added *(Deviation)*

The plan listed a bit cursor as needed for model and frame encode. Checked: **no
rscache decoder does bit-level access.** The only bit writer in the repo is a local
helper in a net test. Unused API not added.

### A4. Phase 8 (README) was done before Phase 5 (encoders) *(Deviation)*

Reordered deliberately. The README is one of the four explicit asks, and writing it
straight after the container work meant documenting formats I had just verified
field by field. Phase 5 is ~33 encoders and would have deferred it a long way.

### A5. No cross-cache porting layer

Not a deviation — scope was set explicitly: *"Do not worry about porting from one to
the other in this. If you provide the tools to encode/decode each data type, a
client of the library can implement."* The library therefore ships symmetric
encode/decode and no porting code. Recorded here because the original brief asked
for porting and someone reading that brief will wonder.

---

## B. Known gaps

### B1. `cache.osrs239` does not decode *(Gap)*

| Datatype | Exact consumption |
|---|---|
| npc | 2462 / 16292 (and *identical* under both head-icon shapes, so not that gate) |
| spotanim | 0 / 4010 |

Revision 239 changed record layouts that are not implemented. Pre-existing, found
by exact-consumption scanning. No manifest or config references that cache — it is
validation data — so the client is unaffected.

The round-trip suite prints this as `KNOWN GAP` on every run instead of asserting,
so the suite stays green while the gap stays visible. Semantic round-trip *is* still
asserted for that cache: an encoder must reproduce whatever the decoder managed to
read, even when that is incomplete.

### B1b. One sequence record in `cache.kronos` does not decode *(Gap)*

Sequence **8127** is 2631 bytes; the decode stops at 1863. It carries a 291-frame
opcode-1 block and then an opcode the v1 codec does not recognise. Kronos is a
custom client build, so a record with a field newer than its archive revision (786)
implies is plausible — but the specific opcode has not been identified, and is not
guessed at. One record in 8526, mirrored in `cache.osrs184`.

Handled by an **exact allowance** in the harness (`CONSUMPTION_ALLOWANCES`), not a
blanket skip: the count is pinned at 1, so a regression to 2 still fails. It prints
as `ALLOWED` on every run.

### B1c. Modern framemap archives carry trailing bytes the decoder ignores *(Gap)*

Measured by comparing each archive's size against what its decode accounts for:

| Cache | Archives | delta 0 | delta 2 | other |
|---|---|---|---|---|
| `cache.kronos` (format 5, table ver 0) | 1887 | **1887** | 0 | 0 |
| `cache.osrs230` (format 7, table ver 67) | 2429 | 0 | 2338 | 91 |
| `cache.jan2026` | 2613 | 0 | 2495 | 118 |

Old caches decode exactly; modern ones leave **2 unread trailing bytes**, observed as
`00 00`, and ~4% leave some other amount — so it is *not* simply a two-zero-byte
pad. The meaning has not been established and is not guessed at, so the bytes are
neither decoded nor written back.

Consequence: a modern framemap round-trips semantically but re-encodes 2 bytes short,
which is why byte-exactness reads 100% on old caches and 0% on modern ones. Quantified
in `dat2_framemap.h`.

### B2. Lossy decoders cap byte-exactness *(Gap)*

These decoders consume fields without storing them, so an encoder cannot write them
back. All still round-trip **semantically**.

| Datatype | What is lost |
|---|---|
| mapelement | ~24 of its opcodes. Only sprite, name, text size and category are kept, so byte-exactness is **0% by construction**. Documented in the header as deliberately lossy. |
| enum | opcodes 1, 7, 8 |
| param | whether the type arrived via opcode 1 or opcode 8 (only the resulting char is kept) |
| obj | opcode 9 (a discarded string); an action of literal `"Hidden"` (normalised to NULL, so indistinguishable from absent); a name of literal `"null"` (same) |
| texture v1 | a run of `count - 1` bytes read and discarded after the sprite types; re-encodes as zeros |
| npc | opcodes 93, 107, 109 *clear* flags the decoder never sets true, so their presence is unrecoverable. No client code reads those three fields. |
| spotanim | recolour/retexture slots past 6 (the opcode ranges are 10 wide) |
| sequence | v1's frame-sound record packs id/loops/location into 24 bits, leaving `retain` and `weight` nowhere to live; v1 and v2 index frame sounds by *position*, so the list is written dense with zero-filled holes (the decoder's own `id >= 1` filter drops them again); opcode 100's blend table is consumed without storing |
| sprites | the per-sprite flags byte, which the decoder does not retain. Row-major is always written (never FLAG_VERTICAL) and FLAG_ALPHA only when the alpha channel cannot be re-derived from `index != 0`, so a vertically-stored sprite is same-length but different bytes — sprites measure **100% same-length, 24–29% exact**, i.e. the shortfall is entirely byte ordering. Also: a palette entry of 0, which the decoder rewrites to 1 on the way in. |
| loc | the largest lossy set of any type — roughly 25 opcodes consumed without storing (25, 44, 45, 61, 69, 88/90/91/96–105, 163–191 and the boolean-flag block), plus opcode 95's pre-220 payload. Also collapses three groups of aliased opcodes: actions 0–4 (writable via 30+i *or* 150+i), map_function_id (60, 82, 107) and map_scene_id (68, 102) — the encoder emits the lowest opcode of each. Hence ~0–1% byte-exact against ~52% same-length. |

### B3. Jagex's opcode ordering is not reproduced *(Gap)*

Jagex's packer does not write opcodes in ascending order — obj 0 is
`1, 7, 8, 4, 6, 5, 75, 39, 16, 2, 3`, strings last. The encoders write ascending.

This caps byte-exactness but loses nothing: an opcode stream is order-independent to
any conforming reader. Confirmed by measurement, not assumed — mismatching obj
records come out at **identical length** (91/91, 80/80, 79/79, 112/112).

That is why the round-trip suite reports a `same-len` column alongside `exact`:
high same-len with low exact means reordering; low both means loss. idk is 41% exact
but **100% same-length**, i.e. purely reordering.

Deliberately not chased: matching the order would raise one column and change
nothing observable.

### B3b. Sprite packs must be decoded unnormalised to re-encode faithfully *(Gap)*

`RSCACHE_SPRITELOAD_FLAG_NORMALIZE` rewrites the pack **in place**: `crop_width`/
`crop_height` become the memory dimensions, the offsets are zeroed and the pixel
buffers are resized. Encoding a normalised pack therefore produces a valid pack of
full-size, unoffset sprites rather than a copy of the source.

Not a defect — normalising is what the renderer wants — but it means a repack
pipeline has to decode with `RSCACHE_SPRITELOAD_FLAG_NONE`. Stated in the encoder's
header.

### B4. Container writer limitations *(Gap)*

- **`RSCache_FileListEncode` always writes one chunk.** Multi-chunk groups decode
  correctly; one chunk is the simplest valid encoding.
- **`RSCache_FileListDatIndexedEncodeIndex` derives the final record's length** from
  the `.dat` size, because nothing stores an offset past the last record. Exact for
  every earlier entry; differs only if the source `.dat` had trailing slack.
- **`RSCache_Dat2DiskWriteArchive` appends and re-points**, orphaning old sectors —
  so repacking in place grows the file. Write to a fresh directory to compact.
- **Sector 0 is reserved** on a fresh `.dat2`, because the index reader treats
  `sector <= 0` as "archive absent" and `AppendArchive` would otherwise return 0.

### B5. Whirlpool is implemented but unexercised by real data *(Gap)*

**No cache in the repo sets the reference-table whirlpool flag (0x2)** — verified
across all seven. The hash exists so a cache that *does* set it round-trips.

It is validated against the three published test vectors rather than against cache
data, which is meaningful here because every table is derived from the spec's
mini-box construction rather than transcribed: an error anywhere in the derivation
changes the digest completely. (The first attempt failed the vectors — the round
constants need eight consecutive S-box entries, not one byte and zeros. Caught by
prototyping in Python before writing the C.)

### B6. `clientscript` trailer flag is unconditional *(Gap)*

`RSCache_ClientScriptFlags` returns `LEGACY` for every profile, which is what the
client already hardcoded at its single call site. **No revision threshold for the
trailer width has been verified against a cache**, and guessing one would silently
break script decode on whichever side of the guess is wrong. The function exists to
give the decision one home, not to change it.

### B7. `LARGE_MODEL_IDS` is defined but never set *(Gap)*

No loc profile sets it: an exact-consumption scan of the jan2026 cache showed
big-smart model ids misaligning 15k files. Kept as a flag because the field does
widen in other lineages.

---

### B8. Provenance recording — `frame` and `maps` terrain, both resolved

Both were previously listed here as unencodable. Every other encoder inverts a
*parse*; these two decodes are **transformations** that destroy or invent
information, so no amount of inspecting the output recovers the source.

The fix in both cases was the same, and it is worth stating as a pattern:
**record provenance at decode time rather than trying to recover it afterwards.**
The existing transform keeps running untouched, so client behaviour does not change,
and one encoder then handles a raw stream and a transformed one identically — the
raw-decode flag is *not* required for encoding.

**`frame` — the decode invents entries.** `frame_new_decode` runs two cursors and
inserts entries that were never in the stream: for a bone whose type is non-zero it
walks backwards and adds a zero-translation entry for the nearest preceding type-0
bone. It also skips bones whose flag byte is `<= 0`, and fills any absent translation
component with a type-derived default (128 for type 3, else 0) that is
indistinguishable from an authored value equal to it.

Recorded: `flag_count`, `bone_flags` (the flag stream verbatim) and a per-entry
`synthesized` flag. The flag bytes answer which components were present, so nothing
has to be inferred from the values; `synthesized` says which entries to skip.

Result: **100% byte-exact on all six caches, including `cache.osrs239`** — the most
exact datatype in the suite, and the one that looked least tractable.

**`maps` terrain — the decode fabricates heights.** `fixup_terrain` rewrites `height`
for *every* tile: procedurally from Perlin noise where the file gave none, scaled by
the tile-height basis where it did. Nothing writable survives in that field, and
"absent" and "authored" collapse into one state. An encoder reading only `height`
would emit an opcode-1 height for every tile and bake generated terrain into the file
— silent corruption for a repack pipeline, not a size regression.

Recorded: `height_authored` (opcode 1 was present) and `authored_height` (the raw wire
byte). Both are needed, not just the flag: the fixup overwrites `height` when it is 0,
so a tile that genuinely specified height 0 would otherwise still lose its value.
That case is asserted present in the test, so the test cannot silently stop covering
it. Also added `RSCACHE_MAP_TERRAIN_DECODE_NO_FIXUP` for callers wanting untouched
values.

`MapFloor` was 9 bytes padded to 10, so the two new bytes cost no memory. One latent
bug surfaced: the flags word was tested with `==` against
`RSCACHE_MAP_TERRAIN_DECODE_U16` (zero), which would have silently selected u8 widths
— shifting every tile of every map — the moment any second bit was added. Now a
proper `&` test.

`shape` and `rotation` need no reconstruction: `attr_opcode` stores the original
overlay opcode verbatim and both derive from it.

### B9. `clientscript` — resolved; the trailer relation was measured

Previously listed here as unimplementable because the arithmetic between
`trailer_len`, `footer_size` and the switch-table bytes would not reconcile from
reading the decoder. Settled by measuring real scripts instead of reasoning further:

| switch bytes S | trailer_len |
|---|---|
| 0 | 1 |
| 50 | 51 |
| 92 | 93 |
| 162 | 163 |
| 192 | 193 |
| 418 | 419 |

**`trailer_len = S + 1`** — the +1 being the switch-count byte that `footer_size` does
not cover. `data_size - trailer_pos` came to `F + S + 2` in every case, the 2 being
the trailing `trailer_len` u16 the decoder never reads back.

Result: **100% byte-exact over ~10,000 scripts across all five caches carrying table
12.** Byte-exactness at that scale is what confirms the relation; a guessed layout
could not achieve it.

One field remains unreproducible: `PUSH_CONSTANT_LONG` carries two u32s and the decode
keeps only the low one, because `int_operands` is `int`. The encoder sign-extends from
the low word, restoring any constant that fits in 32 bits; a genuinely 64-bit constant
is already lost at decode. Widening `int_operands` to `int64_t` is the fix and would
touch the CS2 VM.

### B10. `model` — resolved; three of four formats are byte-exact

Four formats selected by a magic trailer (`FF FF` ob3, `FF FE` OSRS extended, `FF FD`
OSRS material, otherwise ob2), ~2300 lines of decoder between them. Now encoded, at
**100% semantic and 100% byte-exact over 273,065 models** in the five stock caches,
with two bounded exceptions recorded below.

| Cache | Models | Formats | Byte-exact (provenance) | Semantic (no provenance) |
|---|---|---|---|---|
| `cache` | 56323 | v2 + v3 | 100% | 100% |
| `cache.jan2026` | 59885 | v2 + v3 | 100% | 100% |
| `cache.osrs230` | 56306 | v2 + v3 | 100% | 100% |
| `cache.osrs239` | 61615 | v2 + v3 | 100% | 100% |
| `cache.osrs184` | 38936 | ob2 + ob3 | 100% | 100% |
| `cache.kronos` | 39117 | ob2 + ob3 | 99.87% (49 short) | 100% |
| `cache.643` | 65014 | ob3 | 85.9% | 96.5% |

**What the plan above got wrong, and what measurement replaced it with.** The two
provenance questions it flagged were both settled by measuring rather than assuming,
and the answers went opposite ways:

1. *Vertex flag bits* — **derivable, confirmed.** "A bit per axis with a non-zero
   delta" reproduces the flag byte on every one of ~377,000 models: no flag was ever
   set over a zero delta, and no flag byte ever carried a bit above `0x4`. So nothing
   is recorded, and the deltas and their flags are recomputed. The same sweep settled
   the byte counts in the trailer: every one matches a minimal shortsmart re-encode,
   so those are recomputed too rather than carried.
2. *Face-index delta type* — **not derivable, recorded.** As predicted.

The plan also missed a third thing, which turned out to be the largest: **the trailing
region**. Every format except ob2 ends with sections this library does not decode at
all — ob3/v3's complex and cube texture mapping payloads, and a gate byte introducing
a further per-face block followed by a flag byte introducing 10 more. Those are
carried verbatim as an opaque tail. See B11.

**Design.** Sections are built into separate growable buffers and concatenated in the
order the format wants, which is what makes the "trailer carries section byte counts"
problem disappear — the counts are just the buffers' lengths, so there is no
backfilling and no second pass. The four layouts turn out to be two orders (ob2 = v2,
ob3 = v3) and four trailers.

**The exceptions.**

*`cache.kronos`, 49 of 39117 not byte-exact.* Not an encoder fault: the source packed
some shortsmarts in the **two-byte form for values that fit in one**. `c0 3f` and `7f`
both decode to 63; this encoder always writes the shorter. Semantic round-trip is
100% and the output is 1-4 bytes smaller, so these are re-packings, not losses. Zero
occurrences in the other six caches, which is consistent with kronos being a custom
cache whose models were packed by a different tool. Reproducing it would mean
recording a width bit per delta; not worth it for a canonically-shorter encoding.

*`cache.643`, 85.9% byte-exact.* This one is a **decoder** gap, not an encoder one —
see B12.

### B11. The model trailing region is carried, not understood *(Gap)*

Bytes between the last section this library decodes and the trailer are preserved
verbatim in `RSCache_ModelProvenance.tail`. Two distinct things live there:

- **ob3/v3 only:** the complex and cube texture mapping blocks. The decoder reads the
  *simple* (render type 0) p/m/n triples and skips the rest, sizing them as parallel
  column blocks of 19 bytes per complex triangle plus 2 per cube triangle. Since they
  are never decoded they cannot be re-derived, only copied.
- **v2/ob3/v3:** a gate byte whose non-zero value introduces one further per-face
  block, then a flag byte whose non-zero value introduces 10 more bytes. This shape
  was established by measurement — reading the region that way accounts for the file
  length **exactly** on all 2676 v2 models in four caches, where ignoring it left every
  one of them short by `face_count + 1`. The per-face block holds small values (0-3, in
  runs) and is set on roughly a fifth of v2 models. What it *means* is not established,
  so it is carried rather than interpreted.

Consequences, both bounded and deliberate:

- A model authored from scratch (no provenance) gets the minimal instance of the
  trailing structure — two zero bytes — rather than nothing. Not cosmetic: `decode_ob3`
  and `decode_version3` read a byte there without checking they are still inside the
  file, so a body ending at the trailer had them read the vertex count as a flag and,
  if it were non-zero, ten bytes from past the end.
- Editing a model's *complex* texture triangles is not supported. Simple triangles are
  re-encoded from the struct and can be edited freely.

The fix is to decode the region properly, which is a decoder change and out of scope
here. Until then the encoder is faithful without being complete.

### B12. `cache.643` models — fixed; the rest of 643 followed *(Resolved)*

> **Later state.** The "rest of 643 is not fixed" framing below is the state this section was
> written in and is kept for the traps it records. All three gaps it lists are resolved, and so
> are the two that came after: textures (B18, 1164/1164 bake) and the loc/npc era gates
> (B19, 57,282/57,282 and 13,636/13,636 exact). A 643 world renders with textured scenery.


**Models decode.** `cache.643` went from 85.9% byte-exact to **65,014 / 65,014**, and the
no-provenance path from 2,273 failures to zero. Two errors in `decode_ob3`, both found by
reading `rs-map-viewer`'s `ModelData.decodeV1` — the only reference to hand that covers
the RS2 branch:

1. **Header byte 4 is a bitmask, not a boolean.** Bit 0 is the face-render-types flag;
   bit 1 announces particle effects, bit 2 billboards, bit 3 a version byte sitting
   immediately *before* the 23-byte header. Testing the byte for `== 1` therefore read
   bit 0 as clear on any model with particles or billboards, dropping a face-sized
   section and shifting every offset after it.
2. **A complex texture face is 15 bytes, not 19** — a 6-byte p/m/n triple, a scale block
   whose width the version selects (6, or 7 at version 14, or 9 from 15), then one byte
   each of rotation, direction and translation. The old figure overshot by 4 bytes per
   complex face, which is why 1795 of 3000 sampled models laid out past their own end.

Neither error could show on an OSRS cache: they contain **no complex texture faces at
all**, so any per-complex figure is correct, and their flag byte is only ever 0 or 1.
That is the whole reason this sat as a gap — the corpus that validated everything else
could not see it.

**A third error surfaced later, and it is the exact trap this section warned about** ("a
byte-exact model decode still permits a wrong interpretation"): `decodeV1` ends with
`if (version >= 13) scaleDown(2)` — version-13+ models store their vertices at **4x** and the
reference shifts them down after decode. All 65,014 records round-tripped byte-exactly while
every version-13+ model rendered 4x too large; the visible symptom was single-tile gravel
decor spanning three tiles, burying whole squares.

The version is **per model**, which is why this cannot be an era-wide rule —
`test_rs2_sweep <root> modelvers` censuses it:

| cache | models | version distribution | scaleDown applies |
|---|---|---|---|
| `cache.643` | 65,014 | 1 x38,840 · 14 x1,955 · 15 x24,219 | 26,174 (40.3%) |
| `cache.osrs230` | 56,306 | — (never reaches this decoder) | 0 |
| `cache.jan2026` | 59,885 | — | 0 |

Note the 60% that must be left alone: a blanket shift would be as wrong as none. OldSchool
models take the version2/version3 branches and never reach `decode_ob3`, so the field stays 0
there and the shift is structurally unreachable. The decoder deliberately does NOT apply
the shift (it drops two bits, which would break the round-trip bar); it records
`format_version` on the struct and the engine's ToriRS adaptor shifts — so byte-fidelity and
reference geometry live on opposite sides of the adaptor, each checked by its own harness.
`RSCache_ModelNewCopy`/`NewMerge` must carry the field: the dat2 model task adapts a *copy*,
and a dropped version silently un-scales everything again. Settled by decoding model 1139
through rs-map-viewer's own loader headlessly and comparing vertex-for-vertex: ours
`(164, -34, -172)` vs theirs `(41, -9, -43)` — exactly `>> 2` (arithmetic, matching JS).

The particle and billboard payloads still are not decoded, by rs-map-viewer either — it
computes `particleEffectsOffset` and never reads it. They sit past every other section,
so the opaque tail already carries them and round-trips them byte-exactly.

**The rest of 643 is not fixed**, and `manifest_rs643.ini` documents it: a 643 world does
not render. Three gaps, none about models:

1. **Config kind ids differ.** 643's table 2 lacks archives 6 (loc), 8, 9, 10, 12, 13 and
   14 — the ids OSRS uses — and carries 15, 23, 36 and 40..48 instead. The kinds were
   renumbered, so the loc group is not where the client looks.
2. **Overlay decode aborts.** `RSCache_Dat2ConfigOverlayDecodeInplace` hits its
   `assert(false)` on 643 records. A hard abort, so nothing renders regardless of (1).
3. **Table 5 archives fail to decompress**, though the table is present with 5230
   archives — a container-level difference rather than a missing table.

**The three gaps are now fully specified**, from `void` (the 643-era server at
`~/Documents/git_repos/void`) — clean named Kotlin decoders, which is what the two
client deobs were not. Nothing below is inferred; it is transcribed.

*(1) The 643 layout is not "renumbered config groups" — several types are their own
top-level table.* From `cache/.../Index.kt`:

| Type | OSRS | 643 |
|---|---|---|
| loc | table 2, group 6 | **table 16** |
| enum | table 2, group 8 | **table 17** |
| npc | table 2, group 9 | **table 18** |
| obj | table 2, group 10 | **table 19** |
| sequence | table 2, group 12 | **table 20** |
| spotanim | table 2, group 13 | **table 21** |
| varbit | table 2, group 14 | **table 22** |

That explains the survey's "missing" archives exactly: they were never absent, they had
moved out of table 2 altogether. It also means **table ids themselves are era-dependent**,
which this library currently treats as universal — `RSCACHE_DAT2_DISK_TABLE_WORLDMAP` is
19, but 643's table 19 is items; `..._WORLDMAP_GEOGRAPHY` is 18, and 643's 18 is npcs.
Fixing this properly means the table enum becomes a per-profile mapping rather than a
fixed set, which is a real design change and the reason 643 support is not a small job.

The config kinds that *stayed* in table 2 mostly kept their ids (underlay 1, identkit 3,
overlay 4, inv 5, params 11, sequences 12, varc strings 15, varp 16, varc 19) but four
moved: **structs 34 -> 26, hitsplat 32 -> 46, healthbar 33 -> 72, varbit 14 -> 69**.

*(2) The overlay/underlay opcode tables*, from `OverlayDecoder.kt` / `UnderlayDecoder.kt`.
This is what our `assert(false)` was hitting — 643 overlay has nine opcodes OSRS does not:

```
overlay   1 -> u24 rgb        2 -> u8 texture     3 -> u16 texture (65535 => -1)  NEW
          5 -> hideUnderlay=false                 7 -> u24 blend rgb
          8 -> flag, no operand                   9 -> u16 scale << 2             NEW
         10 -> flag blockShadow=false     NEW    11 -> u8                         NEW
         12 -> flag underlayOverrides     NEW    13 -> u24 waterColour            NEW
         14 -> u8 waterScale << 2         NEW    16 -> u8 waterIntensity          NEW

underlay  1 -> u24 rgb        2 -> u16 texture (65535 => -1)     3 -> u16 scale << 2
          4 -> flag blockShadow=false             5 -> flag
```

Note underlay opcode 2 is a **u16** in 643 where ours reads a byte, and overlay gains a
u16 texture at opcode 3 — so even the shared opcodes are not all the same width.

*(3) Table 5 decompression is NOT LZMA — that conclusion was wrong.* `RSCache_ArchiveDecryptDecompress` returns false, whereas an unknown compression byte *asserts*, so it could never have been a missing compressor. The real cause is mundane: `cache.rs643`'s `xteas.json` holds 1591 keys and **none is `l50_50` or `m50_50`**, so Lumbridge specifically cannot be decrypted. Any square with a key works — `TORIRS_WORLD_MAP=40,55` loads its maps fine. No LZMA decoder is needed for a world render. Kept below for the record because the compression byte *does* have a fourth value in this era, so it may still matter for some archive somewhere.

*(3-original) The LZMA reading*, from `cache/.../compress/DecompressionContext.kt`.
The container compression byte has a fourth value this library does not implement:

```
0 = none      1 = bzip2      2 = gzip      3 = LZMA      (5 = non-OSRS packed, ours)
```

`archive.c` handles 0, 1, 2 and its own 5, then `assert("Unknown compression method" && 0)`
— which is the "Failed to decompress dat2 archive for table 5" on 643. So this one is not
a 643 *format* difference at all, it is a missing compressor: 643 packs some archives with
LZMA. The header is the usual `type, compressed size, decompressed size`, then raw LZMA
properties + stream at the current position. Needs an LZMA decoder vendored, which is a
new third-party dependency and the only part of 643 support that is not just code.

**Step 1 is done: the table allow-list is gone.**
`RSCache_Dat2DiskIsValidTableId` was an allow-list over the *named* OSRS enum values, and
`init_reference_tables` skips a rejected id **before** attempting the load — so an
unnamed table was unreachable rather than merely absent, with nothing logged. It now
accepts any id the container can address (`0 .. RSCACHE_DAT2_DISK_TABLE_COUNT`, widened to
36). A table genuinely missing from a cache already returns NULL, which callers handle, so
gating on a name bought nothing and cost a whole generation of support.

`cache.rs643` now reports tables 16 (loc, 224 groups) and 17 (enum, 15) loaded. No
regression: the OSRS caches load *more* tables than before (15, 17, 21 were previously
unnamed), never fewer, and all round-trip checks stay green. RS2 ids are declared as a
separate `enum RSCache_Dat2Rs2DiskTable`, not added to the OSRS enum, because 18-22
collide between the lineages.

**Step 2's addressing is confirmed, and it exposed a fourth gap.** Reading table 16 with
`group = id >> 8, file = id & 0xFF` yields real loc records — **651 of 2048 across groups
0-7 consume exactly** using the existing decoder. That proves the table and the addressing;
what it also proves is that **643 loc records use opcodes the OSRS loc decoder does not
implement** — 26, 42, 48, 76, 108, 116, 118, 126, 131, 184, 193, 202, 210, 220, 236, 237
were all observed. The ~32% that decode are the records that happen to use only shared low
opcodes.

**Step 2b is done: `RSCACHE_CONFIG_LOC_DECODE_RS2`.** 643 locs went from
**651 / 2048 (32%)** consuming exactly to **56,345 / 57,282 (98.4%)** across all 224 groups.

The list of "16 unimplemented opcodes" was almost entirely a red herring — post-desync data
bytes being read as opcodes. The actual difference is two opcodes, and it is structural:

| | OSRS | RS2 (643) |
|---|---|---|
| opcode 1 | `u8 count`, then `count x (u16 model, u8 shape)` | `u8 count`, then per entry `u8 shape, u8 model_count, model_count x u16` |
| opcode 5 | `u8 count`, then `count x u16 model` | **two** of the above blocks |

RS2 inverts the nesting — one shape owns a list of models, rather than each model naming its
shape. Reading an RS2 record with the OSRS shape desynchronises inside the *first* opcode,
which is why everything downstream looked like an unknown opcode. The 32% that survived were
records whose opcode 1 was absent or degenerate. Per `void`'s `ObjectDecoder.kt`, whose
`skip` helper is called once for opcode 1 and twice for opcode 5.

Opcode 5's second block is consumed and dropped; the first is kept, since that is what a
world render draws. The reference discards both — it only needs the stream aligned.

*Remaining 1.6% (937 records):* a scattered long tail — opcodes 108, 20, 127, 4, 6, 10, 180,
110, 86, 83, each on 5-52 records. None appear in void's table either, so they are residual
desyncs from some further RS2 shape rather than missing handlers. Also still absent versus
void's table, though not implicated in any current failure: 42 (`u8 count` then `count`
bytes), 162 (u32), 164/165/166 (u16).

Fan-out is **not uniform** across tables: loc and obj are 256 files per group, npc is 128, so
the shift and mask must be per-type rather than a fixed `>> 8`. Not yet wired into the
profile.

No regression: every OSRS cache's loc figures are unchanged, both boots unchanged, 1,359
checks green.

**The shape of the fix: two container generations, not one with variations.**

The right framing is a split — `DAT2_RS2` and `DAT2_OSRS` — rather than patching a table
map onto today's single dat2. What differs is not a handful of fields, it is three
namespaces at once:

| | RS2 (643) | OSRS |
|---|---|---|
| table 18 | npcs | worldmap geography |
| table 19 | items | worldmap |
| table 22 | varbit / structs | animayas |
| config kind 26 / 46 / 72 / 69 | structs / hitsplat / healthbar / varbit | *unused* |
| config kind 34 / 32 / 33 / 14 | *other* | structs / hitsplat / healthbar / varbit |
| compression byte 3 | LZMA | *unused* |
| model format | ob3 with particle/billboard bits | v2 / v3 |

The two share the sector and index layer completely — the same reader serves both — so the
split belongs above that: `enum RSCache_Dat2DiskTable` and `enum RSCache_Dat2ConfigKind` are
logical names today, resolved to era-specific ids through `RSCache_Dat2DiskTableForGame` /
`RSCache_Dat2DiskSetProfile`. `game` (rs2 vs oldschool) is the lineage key for that lookup;
`epoch` only says whether the on-disk container is dat1 or dat2.

This is why 643 is not a small job, and it is worth doing as a *container* change rather
than a datatype one: every "643 field differs" symptom found so far turned out to be a
consequence of reading the wrong archive.

**The 643 world loads.** With `TORIRS_WORLD_MAP=40,55` (a square whose XTEA key exists):

```
world_load: 1 chunks, 7 underlays, 5 overlays, 39 textures, 100 locs, 72 models, 3 seqs
```

72 models is not "some" — it is **every** model the square's locs reference, matching an
independent cache-side count exactly. Verified alongside it: 1881 loc instances, 1793 of
them shape-matched against their definitions, 100 distinct loc ids, 96 carrying models.

**The 3D viewport is still blank**, and that is now an engine/UI question rather than a
cache one. Three pieces of evidence separate the two:

1. The same offline harness renders the OSRS square in full — house, river, trees, terrain
   — so the renderer and the BMP path both work.
2. 643's frame emits **103** 2D draw commands against OSRS's 8, i.e. far more UI is being
   composited, and the viewport region is flat dark.
3. Booting 643 with `interface_id=0` renders its "Choose a banner" dialog correctly, with
   text, sprites and layout — so 643 **interface** decode and render are fine too.

Two hypotheses were tested and **both are wrong**, which narrows this usefully:

- *"548 is the wrong gameframe id."* No — void's `gameframe.ifaces.toml` states
  `[toplevel] id = 548, type = "full_screen"` outright.
- *"`clientCode` 1337 is an OSRS-only convention, so 643 has no world component."* No —
  `clientCode` is a real u16 cache field (see `dat2_component.c`), and 643's iface 548
  carries **1337 on component 5**, in the same shape OSRS 161 carries it on component 91:
  `type = 0` (layer), baked size `0x0`. The zero size is normal for both, so dimensions come
  from layout at runtime rather than from the pack.

Also ruled out: nothing in the 643 tree is hidden (`hidden=1` count is 0), so this is not
the rev230 "mounted but hidden pending a varc" situation.

**Localised precisely: the world *is* drawn, then painted over.** Both eras emit exactly one
`UITREE_EMIT_WORLD` command and both set `world_view_valid`, so every earlier hypothesis
about a missing or hidden viewport was wrong. The instrumentation
(`TORIRS_WORLD_VIEW_DEBUG=1`) puts it beyond doubt:

| | world node | world rect | emit index | UI draws after it |
|---|---|---|---|---|
| OSRS 161 | 91 | 0,0 **723x503** | **0** (first) | 6 |
| 643 548 | 5 | 4,4 **512x334** | 1 | **101** |

`512x334` at `(4,4)` is the classic RS *fixed* gameframe viewport, so 643's rect is correct,
not degenerate. The failure is ordering and occlusion: 101 subsequent kind-1 (fill/sprite)
commands cover the viewport region, which is why the screenshot shows correct sidebar,
minimap ring and tabs with a flat dark rectangle where the scene should be.

**Occlusion was then ruled out too.** An overlap test over every command drawn after the
world found **nothing** covering more than 5% of the viewport rect, so the region is clear
and the scene simply renders empty into it. The screenshot is byte-identical before and after
the D19 fix took models from 6 to 72, which says the same thing.

The sharpest clue turned out to be that **the minimap ring was also empty**. The minimap is
baked from terrain and does not depend on the 3D camera, so two independent consumers of the
scene were both blank — which pointed at the built scene rather than at the viewport, the
camera or the draw order, all three of which were excluded by then. That was correct, and led
to D20 below.

**Both halves are now resolved. Terrain renders; scenery does not, for a separate and much
larger reason.**

*Terrain* was D20: `RSCache_MapTerrainFlags` gated the tile widths on the **container**
(`IsDat1 ? u8 : u16`) when the real gate is the **era** — u8 until OldSchool 209. 643 is dat2,
so it took the wide layout, desynced on its first tile, and kept only **120 of 15,376** tiles
against ~4,200 for a working square. Fixed, and the dat2 terrain task now passes the profile
(`RSCache_MapTerrainNewFromArchiveProfile`) instead of assuming modern widths.

*Scenery* was chased down the whole pipeline with four env-gated counters, and every stage is
**correct**:

| Stage | 643 | OSRS 230 control |
|---|---|---|
| loc instances in the square | 1881 | 4728 |
| reached `scenery_add` (config + morph + bounds all OK) | **1881** | 4725 |
| became scene elements | **1880** | 4542 |
| painter commands emitted (kind ELEMENT) | **1436** | 2948 |
| draws issued to the raster | **1436** | 2948 |
| dropped as dead/model-less | **0** | 0 |

Camera, level mask and roof check are **byte-identical** between the two
(`campos=(4160,-2000,4160) pitch=450 yaw=0 level_mask=0xf roof=3`), and the models are healthy
— sampling distinct locs gives 362 vertices / 524 faces at ±445 units, walls at
`y=[-959..0]`. So 1,436 correct models were being submitted, positioned, and drawn every
frame, and produced no pixels.

The reason: **the raster skips faces whose texture is absent, and every 643 texture load
fails** (70 of them). Terrain underlays are flat colour, so they still drew — which is exactly
why the viewport showed smooth untextured ground and nothing else. Confirmed decisively with
the pre-existing `TORIRS_STRIP_TEXTURES=1`: with face textures cleared, the scenery appears
immediately — trees, rubble, plants, the lot.

**Root cause: 643 does not use sprite-backed textures at all.** Per rs-map-viewer's
`Dat2CacheLoaderFactory.getTextureLoader`, there are *three* texture systems, chosen by era:

| Loader | Selected when | What we implement |
|---|---|---|
| `SpriteTextureLoader` | `oldschool`, **or** `runescape` && rev < 474 | ✅ this one only |
| `ProceduralTextureLoader` | rev ≥ 474 **and** a materials index exists | ❌ |
| `OldProceduralTextureLoader` | otherwise | ❌ |

643 is `runescape` at rev 643 with **table 26 (materials) present**, so it takes the
*procedural* path. Verified against the dump: `cache.643` table 26 holds 27,938 bytes and
table 9 archive 8 **loads fine at 31 bytes** — the archive is there, it is the *decode* that
rejects it, because a 643 record is a procedural material definition, not the OSRS
sprite-backed shape. `cache.osrs230` has no table 26 at all. The RS2 index block confirms the
naming: `materials: 26, particles: 27, defaults: 28`.

This is a **new subsystem, not a fix**: rs-map-viewer's procedural texture code is ~7,700
lines across 42 distinct operations (Perlin and Voronoi noise, Mandelbrot, bricks, herringbone,
weave, kaleidoscope, emboss, gradients, curves, mixers…) — effectively a small texture-shader
VM whose programs are stored in the materials archive. Nothing smaller will make 643 scenery
appear textured; the only cheap alternative is to render 643 untextured on purpose
(flat-shaded), which is what `TORIRS_STRIP_TEXTURES` already demonstrates and is a legitimate
interim state.

Remaining engine-side note, unrelated to textures: this era slots gameframe children by
`fixedIndex`/`resizeIndex` (void's `interface_types.toml`) — a fixed-vs-resizable duality this
client has no notion of.

Diagnostics added along the way, all env-gated and kept: `TORIRS_TERRAIN_DEBUG` (shape-tile
counts + height range), `TORIRS_SCENERY_DEBUG` (per-drop-reason counts, level/shape histograms,
per-loc model extents), `TORIRS_PAINT_DEBUG` (painter commands by kind + camera),
`TORIRS_FRAME_DEBUG` (draws issued vs dropped).

**`cache.rs643` is a different dump from `cache.643`** and is the better one to work
against — 36 index files against 39, and the RS2 tables are populated (t18 npcs, t19 objs,
t20 seqs, t21 spotanims, t22 varbits) where `cache.643` had them empty. Two things to note
before relying on it:

- Its RS2 table counts are small — npc 107, obj 81, seq 120, spotanim 12, varbit 9. Either
  a partial dump or those ids mean something else again; not yet established.
- **Tables 16 and 17 are rejected by an allow-list, not failing to decode.**
  `RSCache_Dat2DiskIsValidTableId` is a switch over the *named* enum values, and 16/17
  have no entry, so `init_reference_tables` hits `continue` **before** attempting the
  load — hence no error message. Reading 255/16 directly and decoding it by hand works
  first time: format 6, version 1182, **224 groups with dense ids 0..223**. So nothing is
  broken here; the table is simply unreachable by name. This is the first fix and it is
  tiny.

- **RS2 loc addressing is group+file, not one flat config group.** 224 groups against
  OSRS's single group of ~56k files implies ~256 files per group, i.e. `group = id >> 8,
  file = id & 0xFF`. The exact fan-out is inferred from the arithmetic and should be
  confirmed against a group's file count before the loader is written. The small counts
  seen earlier (npc 107, obj 81, seq 120) are **group** counts for the same reason, not
  evidence of a partial dump.

Doing this properly is sequenced: the table mapping (1) has to land before the flo tables
(2) are even reachable, since the loc group cannot be found until then. (3) is independent
and can be done at any point.

Also worth recording (**resolved**): a manifest used to be unable to request the 643
profile by revision. The profile declared `version = RSCACHE_REVISION_UNKNOWN` so it
would never pass OldSchool thresholds (D16), and
`RSCache_ProfileForContainerRevision(DAT2, 643)` missed it entirely — leaving
`client_version` unset "worked" only because `0 == RSCACHE_REVISION_UNKNOWN`. That
coincidence is gone: `[cache:boot]` states `game=rs2 revision=643` and boot goes
through `RSCache_ProfileForIdentity`. See D25.

### B18. Procedural textures (RS2 / 643) — decode and evaluator complete *(Resolved)*

**Current state: 1,164 / 1,164 textures in `cache.643` decode with exact consumption, are
fully supported by the evaluator, and bake to pixels at 128x128.** Every operation the format
defines has an evaluator. Measure it with `make -C src test-proctex-coverage`, which also
reports which unported operation blocks the most textures should that ever regress.

Across eleven keyed map squares (1,014 distinct textures requested by scenery), the client
now refuses **0**; before the last round of work it refused 78. A refused texture is not a
cosmetic loss: the raster skips faces whose texture is absent, so those faces simply do not
draw.


**Why this exists at all:** 643 does not use sprite-backed textures. Per rs-map-viewer's
`Dat2CacheLoaderFactory.getTextureLoader` there are *three* texture systems, selected by era:

| Loader | Selected when | Status here |
|---|---|---|
| `SpriteTextureLoader` | `oldschool`, or `runescape` && rev < 474 | `dat2_texture.c`, working |
| `ProceduralTextureLoader` | rev >= 474 **and** a materials index exists | **this section** |
| `OldProceduralTextureLoader` | otherwise | not implemented |

A 643 texture is not an image; it is a **program** — a DAG of up to 255 operations (noise
fields, gradients, brick/weave generators, arithmetic, colour transforms, a vector rasteriser)
evaluated at whatever size the caller wants. That is why every 643 texture load used to fail
and, because the raster skips faces whose texture is absent, why all 1,880 scenery models
rendered invisible while flat-coloured terrain still drew (see B12).

#### Cache decode — done and validated

`dat2_proctexture.c` decodes both archives. Validated by **exact consumption**, the bar used
throughout this library:

| Record | Result |
|---|---|
| materials (table 26, group 0) | **27,938 / 27,938 bytes — byte-exact** |
| texture programs (table 9, one archive per id) | **1,164 / 1,164 exact (100%)**, 0 hard failures |

Three findings worth keeping, each one a bug the obvious implementation has:

- **The materials record is column-major**, not one record per material: a `u16 count`, then one
  full pass over every material per field. Columns also *skip* materials whose `exists` byte was
  0, so a column is not a fixed stride. Reading it row-major desyncs on the first absent
  material.
- **Operation field defaults are load-bearing on the stream layout, not just on semantics.**
  Perlin noise's field 2, when negative, is followed by `field1` inline u16 amplitudes — and
  `field1` *defaults to 4*. Zero-initialising instead of defaulting read none of them and
  desynced 8 bytes, which surfaced as absurd "unknown field" ids (255, 196, 234) several
  operations later rather than as a perlin problem. That alone accounted for **74 of 88** broken
  textures.
- **A clamped array must still consume the whole array.** Curve markers and gradient stops are
  stored in fixed-size structs; clamping the *loop* rather than the *store* under-consumed 4
  bytes per surplus marker and mis-read the next operation's field id. That was the remaining 6.

- **`diagonal_gradient` fields 2, 4, 5 and 6 are payload-free flags** — and settling that took
  a different kind of evidence, worth recording as a technique.

  Two textures (275, 742) carry them, and **rs-map-viewer does not decode them either**: its
  handler covers 0, 1 and 3 only, so the reference silently desyncs on these two records and
  never notices, having no consumption check. An earlier pass here tried u8 and u16 and gave
  up correctly — each width consumes plausibly, moves the failure a few operations along, and
  yields another in-range-looking field id. That is the E1 trap: a guess that "progresses"
  tells you nothing about which assumption was wrong, so no width was asserted.

  What broke it was **an anchor outside the field block**. An operation record starts with its
  own `id`, and ids are *sequential* — so the byte after the field block must be
  `previous id + 1`, and the two bytes after that a valid operation type and a plausible cache
  size. That is three independent checks at a known offset, and only the zero-width reading
  satisfies all three. It then chains correctly through every remaining operation to the end of
  the archive, in both records, and the resulting textures render as clean diagonal patterns
  rather than noise — which a mis-wired graph would not.

  The general lesson: when a field's width is ambiguous *locally*, look for a structure the
  format repeats at a known distance. Exact consumption is one such anchor and it was too far
  away here (the failure and the end of the record are hundreds of bytes apart, with many
  operations between them, so many wrong readings survive to the end). A sequential id three
  bytes away is a much tighter one.

Also recorded: **three trailing bytes** that the reference stops short of. Over all 1,164
decodable textures, byte 0 is `0x22` in the overwhelming majority (else 0x00/0x02/0x20 — a
bitmask), byte 1 is a small count 0..13, byte 2 is 0 or 1. They are consumed but **not
interpreted**, because nothing in reach reads them. The tail *before* them is confirmed:
`anim_u`/`anim_v` match the independently-decoded materials table for all 26 textures whose
material declares a non-zero animation, negative values included — which is what pins the
alignment.

#### Evaluator — every operation ported, 100% of textures renderable

`src/engine/proctex/` implements the evaluation model: pull-per-scanline, 12.4 fixed point,
monochrome/colour conversion rules, full per-line caching (the reference's LRU is a memory
optimisation that cannot affect output), Java `util.Random` and Jagex's `nextIntJagex` for the
seeded noise operations, and cycle detection the format cannot express but nothing validates.
Split into `proctex_generator.c` (model, lifecycle, the simple operations), `proctex_ops.u.c`
(the rest) and `proctex_raster.u.c` (the vector rasteriser), unity-included, matching how
`world_builder.c` is arranged.

**All 40 operations are ported.** The seeded noise family (`perlin_noise`, `voronoi_noise`,
`pseudo_random_noise`, `line_noise`), the neighbourhood operations (`blur`, `emboss`, both
edge detectors), the pattern generators (`bricks`, `irregular_bricks`, `weave`,
`herringbone`, `square_waveform`), the warps (`trig_warp`, `mirror`, `kaleidoscope`,
`tiling`), `mixer`, `hsl`, `brightness`, `mandelbrot`, `op37` and `rasterizer`.

Four of them render the **whole image at once** rather than per scanline, because their output
is not decomposable by line: `line_noise` strokes cross scanlines, an `irregular_bricks` brick's
top depends on the row below it, `rasterizer` shapes span whatever they span. They fill their
own cache and mark every line resident (`proctex_mark_all_done`), which is exactly what the
reference's `imageCache.dirty` flag achieves — it is only ever true until the first `getAll()`.

Three things about the reference are semantics rather than accidents, and are reproduced
deliberately:

- **`rasterizer` colours are packed 24-bit RGB**, not the 12.4 triples every other operation
  deals in, and a *monochrome* rasterizer writes those packed values straight into its
  monochrome plane. That is meaningless as a signal, but downstream operations were authored
  against it.
- **Out-of-range writes are dropped, not clipped.** The reference draws into JavaScript typed
  arrays, where a store past either end is silently discarded, and its line clipper relies on
  that: it re-solves x when it clamps y and never re-clamps the result. In C the same store is
  memory corruption, so every raw write in `proctex_raster.u.c` is range-checked.
- **`line_noise` writes transposed** in one of its two branches — `pixels[x][y]` where every
  other operation writes `pixels[y][x]`. Harmless only because a texture is always square here.
  "Correcting" it rotates half the strokes by 90 degrees.

**Gradient presets are client constants, not cache data.** A `gradient` operation either
carries its stops inline (`preset == 0`) or names one of six built-ins, which had to be
transcribed from `GradientOperation.setGradientPreset`. Missing them was worth 2 textures on
its own, and the distinction between "no gradient field at all" (defaults to preset 1) and
"preset 0 with zero stops" (stays black) is load-bearing — the reference's `init()` only
substitutes when the stop list is *absent*, and an empty array is not absent.

**Dependency resolution is the part that is not just an operation port.** `texture_source`
names another texture's program and `sprite_source` names a sprite, so baking is not
self-contained — and the generator runs synchronously inside one protothread step, so nothing
can be fetched once it starts. The texture task therefore walks the **dependency closure**
first: a worklist that grows while being walked (a dependency has dependencies), terminated by
residency — an already-decoded program is never re-queued, so a cyclic `texture_source`
reference is naturally bounded rather than needing its own cycle check. Programs and
flattened-to-ARGB sprites are cached on the buildcache by id, and a nested texture is rendered
on demand at **brightness 1.0** rather than the final 0.8, because it is an intermediate signal
feeding more operations and gamma-correcting it twice would be wrong.

Coverage, measured over all 1,164 textures in `cache.643` by
`make -C src test-proctex-coverage`:

| Ported set | Textures fully renderable |
|---|---|
| 14 operations, no dependency resolution | 39 (3%) |
| 32 operations, no dependency resolution | 463 (39%) |
| 35 operations + dependency closure | 961 (82%) |
| **all 40 operations + gradient presets + diagonal_gradient decode** | **1,164 (100%)** |

**Measure coverage transitively, not per operation.** A texture is renderable only when every
operation it names *and* every texture it pulls in through `texture_source` is renderable, so
the operation blocking a texture is usually not one the texture names directly. Ranking
unported operations by raw usage put `rasterizer` first (69 direct uses); ranking them by
transitive blocking put `line_noise` first (53 direct uses, 101 textures blocked). The
coverage tool computes the closure by fixpoint and reports both numbers side by side.

**Static coverage is not proof the evaluator runs.** The same tool bakes every texture it
calls renderable and reports the two counts apart; "renderable but failed to bake" is the
interesting cell, and it is what caught the missing gradient presets. Beyond that, `PROCTEX_DUMP`
writes every baked texture as a BMP and `PROCTEX_MONTAGE` writes one sheet per operation tiling
the textures that use it — which is how the ports were actually checked, since a transposed
index or a sign error yields a perfectly valid image of the wrong thing. Bricks have to look
like bricks.

An unported operation still yields flat mid-grey and increments a counter, and `proctex_bake`
**refuses any texture with a non-zero count** — as does a texture whose dependency cannot be
resolved. A partially-evaluated texture composites into something that looks real and is not,
and that is worse than a missing one, which just falls back to flat shading and reads as "not
done yet".

**D22, found while verifying this end to end:** the dependency closure walk stored decoded
programs under a **garbage id**. The id was read from the worklist into a *local* before
`PT_YIELD` and used after it — and a protothread resumes at the yield with the frame's locals
gone. Every later `texture_source`/`sprite_source` lookup then missed, so 12 of the 40 textures
this square needs were refused with `unsupported=0`: the signature of a dependency failure
rather than a coverage gap, which is what distinguished it from "needs more operations".

The gotcha itself is documented in this codebase (loop cursors live on the task struct for
exactly this reason), but it bit harder here because the corrupted value was a **cache key**
rather than an index — a bad index would have tripped a bounds check, whereas a bad key just
silently stored the program somewhere nobody looks. Both worklists now keep the in-flight id on
the task, and the program pointer is re-read through the cache after the await instead of being
carried across it. 12 failures went to 1, and that 1 was texture 275 — a decode gap, since
resolved above.

Real 643 scenery is textured from this path across the world, not just at the one square this
started from. Over eleven keyed map squares (40,55 · 51,53 · 47,51 · 42,54 · 44,54 · 48,54 ·
49,54 · 50,54 · 52,50 · 48,48 · 47,48) requesting 1,014 distinct textures between them, refusals
went **78 -> 0**.

#### The SD gate — most materials must NOT be drawn *(the finding after "100% bake")*

"Every texture bakes" turned out to be the wrong success criterion, and the failure it hid was
spectacular: whole 643 squares rendered as a blanket of glaring white-tan chunks, every ground
decor apparently "the same model". Nothing in the decode was wrong — spawns, configs, models
and bakes all matched the reference byte for byte (each stage was verified independently,
including baking texture 154 through rs-map-viewer's own loaders in node: identical pixels).

The missing rule is a **selection** rule: the material's `valid` byte is rs-map-viewer's
`TextureLoader.isSd`, and in `cache.643` only **284 of 1,164** materials are SD-drawable. The
rest are HD-only, and the SD client *drops* them:

- `ModelData.light()` nulls the face texture of any face whose material is not SD — the face
  then lights from its **face colour**, which the loc's recolour list has usually already
  darkened. 643's pebble/rubble decor is the visible case: an HD gravel material over a
  recoloured dark-brown base. Drawing the texture anyway ignores the recolour and renders the
  bright HD base — the white blanket.
- `SceneBuilder` applies the same gate to terrain overlays: a non-SD overlay texture falls
  back to the overlay's own HSL colour (the 643 desert path is this).

Ported as `CacheProvider_TextureIsSd` (a vtable slot; unset = always true, which is the
reference's `SpriteTextureLoader.isSd`, so OSRS and dat1 are untouched) applied where the
reference applies it — after every recolour/retexture, before lighting
(`ToriDraw_ModelDropNonSdTextures`), plus the terrain overlay site and the world-load texture
preloads. Two rules of thumb out of it:

- **"Decodes correctly" and "renders correctly" are separated by selection logic.** Every
  record here decoded byte-faithfully; the defect was drawing data the reference deliberately
  ignores. When a render is wrong but every decoder validates, diff the *filters*, not the
  decoders.
- The visible signature of a missing drop-rule is *uniformity*: many distinct configs
  converge on the same look because the same undropped ingredient (here, a shared HD base
  texture) dominates all of them.

**Texture ids also outgrew the byte.** 234 of the 284 SD materials have ids above 255, and the
engine's texture pipeline was 256-slot end to end — the wants registry, the scene texture map,
the raster's id guard, the bridge's failed-set, and a `(uint8_t)` truncation in the overlay
map setter. Faces naming them were silently skipped (the raster's missing-texture rule), i.e.
invisible geometry. Widened to `TORIDRAW_TEXTURE_ID_CAPACITY` (2048) throughout; the model
chain itself was already `int16_t` and needed nothing.

`TORIRS_PROCTEX_DEBUG` names every refusal and its cause, and the causes are deliberately
distinguishable: a `REFUSED` line is an unresolvable dependency, a `no evaluator` line is an
unported operation, and there are separate lines for a graph with no output operation and for
one containing a cycle. They all reach the caller as the same `render failed`, so without
these they are indistinguishable.

### B19. RS2 loc and npc records — era gates, not new formats *(Resolved)*

`cache.643` now decodes **57,282 / 57,282 loc** and **13,636 / 13,636 npc** records with exact
consumption, up from 98.4% and 6.4%. Neither needed a new codec: both were the *same* class of
defect, an OldSchool-only field read against an RS2 record.

Run `make -C 3rd/rscache build/test_rs2_sweep && 3rd/rscache/build/test_rs2_sweep` to measure.
The main round-trip suite cannot see these types — it scans config *groups* in table 2, and RS2
promotes loc/npc/obj/seq/spotanim into their own sharded tables (see
`RSCache_RecordAddressFor`), so they were invisible to it.

**loc: four opcodes, four gates.** Each is stated outright in rs-map-viewer's
`LocType.decodeOpcode`, and each cost bytes that the rest of the record then read from the
wrong place:

| Opcode | OldSchool | RS2 | our bug |
|---|---|---|---|
| 78 | ambient sound + distance + **retain byte** (rev >= 220) | no retain byte | read it |
| 79 | same retain byte | no retain byte | read it |
| 82 | map function id (u16) | bare flag | read a u16 |
| 91 | sound-distance fade curve (u8) | bare members flag | read a byte |
| 190, 191 | a byte each | bare flags | read a byte each |

**npc: the same four-gate story, plus thirty opcodes that were simply absent.** 114, 115, 122
and 123 all branch on `game === "oldschool"` in `NpcType.decodeOpcode`, and 115 differs by six
bytes (four sequence ids against two). Opcode 102's head-icon *bitfield* is likewise an
OldSchool-only addition, so RS2 takes the pre-210 bare-u16 shape regardless of archive
revision — comparing an RS2 revision against an OldSchool threshold is the D16 trap.

**The defect that hid all of it: an unknown npc opcode did not stop the decode.** The `default`
arm logged and *continued*, so every read after it came out of the middle of an unknown
payload; the decoder then "recognised" payload bytes as opcodes, wrote garbage into real
fields, and finally ran off the end. All 12,762 failures therefore reported the same
buffer-overrun, thousands of bytes and several bogus fields past the actual cause. Stopping
instead — the position the loc decoder has always taken — turned one undifferentiated symptom
into a ranked list of six missing opcodes (127, 159, 119, 125, 128, 163) in a single run.

Worth stating as a rule: **a decoder that continues past an unknown opcode destroys the
evidence needed to fix it.** The cost is not the garbage fields, which are at least suspicious;
it is that the *reported* failure is no longer near the *actual* one.

Two smaller points from the same work:

- The npc `_consumed` field was only assigned on the opcode-0 path, so every other exit left it
  at zero — indistinguishable from "read nothing". A partial decode has to report how far it
  got or the diagnostic is worthless.
- The thirty added opcodes are consumed but **not stored**. Inventing struct members for
  fields nothing reads would make the encoder lossy, which the round-trip suite would then have
  to be taught to ignore. The one worth promoting later is **127, `basTypeId`** — it redirects
  an npc's idle and walk sequences through a separate type, so a 643 npc animates from it
  rather than from opcodes 13/14.

All gates are conditional on flags only the 643 profile sets, so OldSchool and dat1 decode
byte-for-byte as before; the full round-trip suite is green across all six OSRS caches.

**Still open: `obj` cannot be measured.** `RSCache_Dat2ConfigObj` has no `_consumed` field, so
the sweep can only report that the decoder returned non-NULL — which it does for all 20,711
643 records while still logging ~150 buffer overruns per cache. The sweep prints that column
under "returned" rather than "exact" so the number is not mistaken for consumption, but the
real fix is to give the obj decoder a `_consumed` like loc and npc have, and then run the same
comparison against `ObjType.decodeOpcode`. Obj records are ground/inventory items, so this does
not affect a map render; it is listed because "100%" in an earlier version of that column meant
nothing at all.

### B13. The seven undecoded config kinds — four now done, three blocked *(Gap)*

**Status: varbit, varplayer, varclient and inv are implemented** — decoder, encoder and
round-trip visitor each, at **100% semantic and 100% byte-exact over ~190,000 records**
in six caches, with exact consumption asserted (not merely reported) because these are
fixed-shape records with nothing for a decoder to lose.

| Type | Records | Byte-exact | Consumption |
|---|---|---|---|
| varbit | 92,548 | 100% | exact |
| varplayer | 25,588 | 100% | exact |
| varclient | 6,131 | 100% | exact |
| inv | 5,135 | 100% | exact |

**`healthbar` and `hitsplat` are also implemented** — 354 and 349 records, both 100%
byte-exact — once a second constraint broke the tie exact consumption could not (B16,
B17).

Only `varclient_string` remains, and it is the one genuinely blocked case: the corpus
cannot validate a decoder for it at all.

#### The original survey

`varbit`, `varplayer`, `varclient`, `varclient_string`, `inv`, `hitsplat` and
`healthbar` are declared in `dat2_configs.h` with no decoder. Surveyed rather than
implemented; the survey is in `README.md` under "The undecoded config kinds" and the
work is specced as Phase 6.

Three things came out of it that were not previously known:

1. **They are not era-specific.** Every one is present and populated in every OSRS
   cache, and `varbit` holds more records than `npc` (19,650 vs 15,535 in
   `cache.jan2026`). The enum was transcribed from RuneLite's `ConfigType` wholesale —
   the comment is still in the v0 copy — and decoders were written only for the types
   something drew.
2. **Four wire formats were settled and implemented**, each pinned by a unique width
   assignment consuming 100% of records across five caches: varbit (`1` = u16 basevar +
   u8 startbit + u8 endbit), inv (`2` = u16 size), varplayer (`5` = u16 clientcode),
   varclient (`2` = bare flag, `3` = u16). varbit and varplayer also match Client-TS's
   dat1 equivalents. The same sweep proves the opcode *sets* complete for this corpus:
   a record holding any other opcode would have failed to parse, and none did.
3. **Both healthbar and hitsplat were settled after all**, by a second constraint the
   first pass had not tried (B16) and — for hitsplat — by widening a search bound that
   was quietly doing all the damage (B17).

**`varbit` has a live behavioural consequence**, which is why it was done first, and
which the decoder alone does **not** fix — Phase 7 still has to load the types at boot.
`VarPManager` implements varbit resolution fully, but nothing loads the types: the only
loader parses the *dat1* `varbit.dat` blob and is called only from tests. So
`varbit_count == 0` at runtime and `VarPManager_GetVarbit` returns 0 for every varbit —
every CS2 script branching on one takes the zero path, and no CS2 hook triggering on a
varbit ever fires. Varps themselves are unaffected (`apply_varp_value` grows `var[]`
untyped on server writes); it is specifically the types that are absent.

### B14. Engine wiring — done, with two sites deliberately left *(Gap)*

The engine now constructs a cache profile once at boot and hands it to the decoders
instead of passing a bare archive revision or a flag constant written out at the call
site.

- `struct CacheProvider` carries a `struct RSCache profile`, set by
  `CacheProvider_SetProfile`. It starts UNSET (`RSCache_ProfileZero`);
  `CacheProvider_Profile` asserts the identity is set, so a provider used before
  `SetProfile` crashes instead of decoding as the wrong branch.
- `app_provider_set_cache_profile` resolves it from the manifest's required
  `[cache:boot]` identity (`epoch`/`game`/`revision`/`quirks`) through
  `RSCache_ProfileForIdentity`. The boot log states what it picked by name:
  `epoch=dat2 game=rs2 revision=643 quirks=none`. `[net:boot] client_version=`
  is login-only and does not feed the cache.
- Converted: the dat1 loc decode (was a literal `RSCACHE_CONFIG_LOC_DECODE_DAT`) and
  the dat2 loc decode (was `FlagsForRevision(archive->revision)`), the latter via a new
  `RSCache_Dat2ConfigLocNewDecodeProfile`. Both are equivalent on the caches the client
  boots, and both now additionally carry the container and the Kronos quirk, neither of
  which a revision number can imply.
- Wiring this up is what exposed **D16** — the cross-lineage revision comparison that
  would have added `OSRS_220` to every dat1 loc decode. The later collapse of identity
  onto four load-bearing fields is **D25**.

**Left deliberately:** `spotanim` and `component` still take a bare revision.
`RSCache_Dat2ConfigSpotanimNewDecode` `(void)`s its `revision` parameter outright, so
converting those three call sites would change no behaviour at all; `component` already
resolves its era through `RSCache_Dat2ComponentDecodeRev`, which works. Both are
cosmetic, and skipped as such rather than missed.

### B15. Varbits load at boot, both eras *(Resolved)*

`CreateTask_Dat2VarbitLoad` and `CreateTask_Dat1VarbitLoad` install the varbit table into
`VarPManager` at boot, before anything that can execute a script — a varbit read happens
deep inside CS2/CS1 with nowhere to yield to a load.

| Boot | Source | Types |
|---|---|---|
| `manifest_osrs230` | dat2 config group 14, one file per id | 17,605 from 17,426 records (holes zeroed to `basevar = -1`) |
| `manifest_rs254` | dat1 `varbit.dat` in the config jagfile | 6, consuming the blob exactly |

That closes the B13 consequence: `GetVarbit` returns real values, so a script branching on
a varbit takes the branch the server intended. Both eras mattered, not just dat2 — CS1's
`CS1VM_HOST_REQUEST_VARBIT` routes to the *same* `VarPManager_GetVarbit`
(rs_cs1_host.c), so dat1 scripts were reading 0 too.

The two eras store identical records differently — dat2 one file per id, dat1 a u16 count
then that many terminator-delimited records in one blob — which is why rscache's decoders
take a cursor: one codec covers both shapes.

Fixing the loaders also required **D17**: both the reader and the writer masked one bit too
narrow, which alone would have made every single-bit varbit read 0 even with the table
loaded.

`VarPManager_LoadVarbitDat` gained an exact-consumption warning it lacked (the reference
prints "varbit load mismatch" for the same condition). It is silent on `cache254.lostcity`,
which is how the 6-type count was confirmed as real rather than a misparse.

## C. Open — real defects deliberately not fixed

### C1. dat2 npc has no reference defaults, and it reaches rendering *(Open)*

The dat2 npc decoder `calloc`s, so every unset field reads **0**. The reference —
and *this library's own dat1 npc decoder*, which does `npc->readyanim = -1` —
defaults the animation, scale and level fields to **-1**.

Two consequences:

1. For encoding, "absent" and "present with value 0" are the same state, so
   explicitly-zero fields get omitted. Harmless: semantic round-trip is 100%; the
   bytes are just shorter. This is what caps npc at ~0% exact / 33% same-length.
2. **Independently of encoding:** a dat2 npc with no opcode 13 gets
   `standing_animation = 0` rather than -1, and that value reaches the world as a
   sequence id (`info->idle->readyanim`, `src/world/world_cycle.c:32`). Asking for
   sequence 0 is not the same as asking for no animation — and dat1 and dat2
   disagree about the same logical field.

**Not fixed because** it changes what the client renders, it is pre-existing, and
validating it needs an actual NPC render rather than an offline boot. Closing it
would also let the encoder distinguish absent from zero and raise byte-exactness
substantially, so the two changes belong together.

Documented in `dat2_config_npc.h` with the same reasoning.

---

## D. Bugs found and fixed

Recorded because each involved changing a decoder the client uses — i.e. an
exception to "only add the write half".

| # | Bug | How it was confirmed |
|---|---|---|
| D1 | **npc opcode 102**: the era flag was computed at function scope then shadowed inside `case 102` by a hardcoded `true`, so every pre-210 cache misread the head-icon block and misaligned everything after it. | Exact consumption over 6 caches: kronos and osrs184 reach 100% *only* with the old shape (9326/9326, 9306/9306) and lose 15 records each under the modern one; the modern caches are the reverse. The compiler had been reporting it as an unused variable all along — invisible because `src/makefile` builds rscache with `-w`. |
| D2 | **dat2 spotanim opcodes 40/41** are count-prefixed lists (`u8 count`, then `count × (u16, u16)`), not one u16 per slot; and **opcode 9** is a name string the decoder did not know. Reading a bare u16 desynced every modern record, and the decoder bailed with `Unrecognized opcode 210`. Unnoticed because opcodes 1 and 2 (model, animation) come *first*, so those were right and only the recolours were silently lost. | Raw byte dumps across four caches, then exact consumption: `cache.osrs230` went from **0/3295 to 3295/3295**. |
| D3 | **spotanim array overflow** in both dat1 and dat2: `recol_s[6]` with opcode ranges 10 wide, so opcode 46 wrote past the array into `recol_d`. | Found while writing the encoder, which had to know the slot count. |
| D4 | **Reference table never read whirlpool digests** even when the flag was set, so every field after them would have been misread. Dormant only because no cache sets the flag. | Found writing the symmetric encoder. |
| D5 | **`P2`/`P4` had no bounds check at all** — they wrote past the end of a borrowed buffer silently. | Found while adding the growable buffer mode. |
| D6 | **npc encoder skipped empty-string names.** npc's decoder leaves `name` NULL when opcode 2 is absent, so `""` and absent are different states; real records carry empty names. | 26 records per cache failed semantic round-trip; dumping npc 325 in `cache.osrs230` showed the missing opcode 2. |
| D8 | **loc opcodes 78 and 79 are not mutually exclusive.** 78 carries a single ambient sound id; 79 carries the retrigger interval plus a list. Real records carry **both** (loc 16433 in `cache.osrs230`, with 79 first). The encoder used `else if` and silently dropped `ambient_sound_id`. | 30 records per cache failed semantic round-trip; the byte dump showed `4f …` immediately followed by `4e …`. |
| D10 | **sequence opcode 15 used the wrong list shape in v1/v2.** Per RuneLite's `SequenceLoader` — quoted verbatim at the top of `dat2_config_sequence.c` — opcode 13 is the *positional* list (u8 count, frame = loop index) while opcode 15 is the *framed* list (u16 count, explicit frame per entry). The decoder used the positional handler for both, so any record using opcode 15 would misalign. | Read directly off the inline reference while writing the encoder. **Unexercised**: no record in the corpus uses opcode 15, so the numbers did not move. Kept because it matches the documented reference, but it has no test coverage — noted so nobody assumes otherwise. |
| D11 | **sequence `chat_frame_ids` was allocated without recording its length.** That left the array unencodable *and* uncomparable — nothing knew how many entries it had. Added `chat_frame_id_count`, set in all three era decoders. | Found while writing the encoder: the field could not be emitted at all. |
| D12 | **`decode_ob3` read the vertex flag byte and the X delta through the same cursor**, starting at the X delta stream. The flag byte lives in its own section (right after the texture render types); reading both from one cursor walked the flags off into the X data and mangled every vertex of every ob3 model. `decode_version3__osrs_material` — the same layout, one format newer — has always had it right, which is what made the diagnosis quick. | The section layout, re-derived independently, consumes the file exactly for ob3 in `cache.kronos` and `cache.osrs184`, which pins where the flag section is. After the fix all 11,268 + 11,294 ob3 models in those two caches round-trip **byte-exactly** — impossible unless the decode now consumes the stream the way it was written. |
| D13 | **`decode_ob3` re-read the texture render type from the mapping section** instead of the array it had already read from the head of the file, consuming 7 bytes per simple triangle where its *own* offset arithmetic 20 lines earlier allots `simpleTextureFaceCount * 6`. Every texture triangle after the first therefore took its p/m/n from one byte too far along. Self-inconsistent within one function, and again `version3` reads the stored array. | Three models in `cache.osrs184` failed the round trip with `textured_p_coordinate` diverging and the output one byte short. Both went to zero with the fix, and the ob3 byte-exact rate went to 100%. |
| D14 | **`decode_ob3` discarded the texture render types entirely** — read into a local, never stored — even though `struct RSCache_Model` has the field and `version3` fills it. Without it there is no way to tell a simple texture triangle from a complex one after the fact, and the two are sized differently, so the mapping section could not be laid out at all. | Found writing the encoder: the section length was unknowable from the struct. |
| D15 | **Four arrays were `malloc`'d but only partially written**, leaving uninitialised heap where the reference implementation reads a zeroed array: `face_indices_a/b/c` (a face with index type 0 assigns none of them) in ob3/v2/v3, and `face_texture_coords` plus `textured_p/m/n_coordinate` (written only for textured faces / render-type-0 triangles) in ob3 and v3. Switched to `calloc`, which is also what Java's zero-initialised arrays give the reference. | Found by the round-trip comparison, which was nondeterministic until the reads were defined. Index type 0 occurs in no OSRS cache, but it does occur 355 times in `cache.643`. |
| D16 | **`RSCache_RevisionAtLeast` compared revisions across lineages.** dat1 revisions run to 254 in a 2004-era sequence; OldSchool restarted from 1 and is now in the 230s. With no lineage guard, a dat1 rev-254 profile satisfied **every** threshold in the library — 210, 220, 226, 237 — each one switching a decoder to a field layout that postdates the cache by nearly twenty years. Renamed to `RSCache_RevisionAtLeastOsrs` so the lineage is unmissable at the call site; step 1 now requires `game == OLDSCHOOL` (originally an OSRS *epoch* value that had been overloaded to carry lineage). | Latent until Phase 7: nothing passed a real profile to a `Flags` function, so the comparison never ran on a dat1 cache. Found by hand-evaluating `RSCache_Dat2ConfigLocFlags` for the rev-254 profile *before* wiring it up — it would have added `OSRS_220` to every dat1 loc decode. Now pinned by a test that asserts a dat1 profile clears all four thresholds and that its loc flags are exactly `RSCACHE_CONFIG_LOC_DECODE_DAT`. |
| D17 | **`VarPManager_GetVarbit` and `SetVarbitOptimistic` masked one bit too narrow.** `endbit` is inclusive, so the width is `endbit - startbit + 1`; both functions used the difference alone. Every varbit read one bit short, and a **single-bit** varbit — `startbit == endbit`, the commonest shape — hit the `bit_count <= 0` guard and returned 0 for every possible varp value. The reference masks with `(1 << (msb - lsb + 1)) - 1`. | The cache settles the inclusive question: varbits 0..4 of `cache.osrs230` are `start=end=0,1,2,3,4` — five consecutive one-bit flags in varp 318, which would all be zero-width under an exclusive reading. Invisible because `varbit_count` was always 0, and because **the existing test could not discriminate**: it read `var[0] = 0x0E` and expected 7, which holds at both 3 and 4 bits wide since bit 4 is clear. Rewritten to use `0x1E`, where the two readings differ (15 vs 7), plus a single-bit case and a write-then-read check. |
| D19 | **The loc group memo tested the group-local id, not the global one.** `dat2_buildcache_locs_init_from_archive_based` skipped a record when `dat2_buildcache_loc_get(id)` already had it — but `id` is the *file* id, which a sharded RS2 group numbers `0..255` locally. Once group 0 was resident, every file of every later group looked "already loaded" and was skipped wholesale, so only 4 of 100 loc definitions ever reached the provider. | Group 0 was the **only** group that worked, because there `base_id + id == id` and the bug is invisible — a textbook case of the identity case hiding an off-by-base. Found by printing the group, base and post-init lookup together: `loc 3825 -> table 16 group 14 base 3584 files 256 file_ids=yes -> get=MISS` proved the archive and addressing were all correct and the *add* was not. Fixing it took the 643 world from 6 models to **72** — exactly the count an independent cache-side probe predicted. |
| D18 | **Two independent allow-lists silently made whole cache tables unreachable.** `RSCache_Dat2DiskIsValidTableId` (reference tables) and `dat2_cache_table_supported` in `src/platform/platform_x_io.c` (archive reads) each enumerated only the *named* OSRS tables. An id absent from the list was skipped **before** any read, so it presented as "decode failed" or an empty result rather than "this table is not allowed" — nothing logged the rejection. Both had to be widened before the RS2 branch (tables 16-22) could be read at all. | The first surfaced as `tables[16] = NULL` while `255/16` loaded fine by hand and decoded to 224 dense groups. The second surfaced *after* that fix as `Failed to decode dat2 loc group for loc N` for every id, with the group demonstrably present. **The lesson is the pattern, not either instance:** an allow-list keyed on names conflates "unknown to us" with "not present", and the second copy cost an extra debugging cycle because the first fix did not prompt a search for siblings. |
| D20 | **`RSCache_MapTerrainFlags` gated the terrain tile widths on the *container* when the real gate is the *era*.** It read `IsDat1 ? u8 : u16`, so every dat2 cache got the wide layout — but the attribute opcode and overlay id only widen to u16 at **OldSchool 209** (rs-map-viewer: `game === "oldschool" && revision >= 209`). 643 is dat2 and pre-209, and its revision *number* clears every OSRS threshold while belonging to a different lineage, so both the container test and a naive revision test got it wrong. The gate is now `!RSCache_IsOsrs(cache) ? U8 : …` (lineage via `game`, not container/`epoch`). Separately, the dat2 load path never consulted the function at all — it called `RSCache_MapTerrainNewFromArchive`, which hardcoded modern widths — so fixing the predicate alone would have changed nothing. | **Not a clean failure, which is why it survived so long.** The u16 attribute read swallows the following tile's opcode, and the loop only breaks on 0 or 1, so the square resynchronises into plausible garbage instead of erroring: **120 of 15,376** tiles kept an underlay or overlay, against 4,481 for a working OSRS square. Both the 3D scene *and* the terrain-baked minimap came out blank, and that pair — two independent consumers of the scene, with every input verified present — is what pointed at the built scene rather than at the viewport. Found by counting shape-tiles at the one place both consumers read (`TORIRS_TERRAIN_DEBUG`). Now pinned by tests over the 643 profile and over OSRS 184 vs 209. |
| D21 | **A name-keyed table allow-list caused the same bug a third time.** `dat2_cache_table_supported` in `src/platform/platform_x_io.c` enumerated the *named* tables it would read, so the RS2 materials table (26) was refused **before any read** and the procedural texture system reported "no materials table" — indistinguishable from the cache genuinely not shipping one. Now a range check (`0 <= id < TABLE_COUNT`), matching `RSCache_Dat2DiskIsValidTableId`, which was converted for exactly this reason. | **The third occurrence of D18, and the lesson is now acted on rather than restated.** Enumerating names cannot work here because table ids are era-dependent: 19 is OSRS's worldmap and RS2's objs, 26 is RS2's materials and nothing in OSRS — so a name-keyed list structurally conflates "unknown to us" with "not present in the cache". Whether a table *means* anything is the profile's business; whether it may be *read* is only a question of it being a legal index id. A table the cache lacks now fails one layer down with a real message. |
| D22 | **A protothread read a worklist id into a local, then used it after the await.** The procedural-texture dependency walk in `task_dat2_texture_load.c` set `dep_id` before `PT_YIELD` and used it after, but a protothread resumes at the yield with the frame's locals gone — so the decoded program was stored under whatever garbage the slot held. Every later `texture_source`/`sprite_source` lookup missed. Both worklists now keep the in-flight id on the task struct, and the program is re-read through the cache after the await rather than carried across it. | **The value was a cache key, not an index, which is why it was silent.** A corrupted index would have tripped a bounds check; a corrupted key just files the program somewhere nothing looks, and the symptom appears much later as a missing dependency. It was separable from a coverage gap only because the failure reported `unsupported=0` — no unported operation had run — and because an independent probe confirmed all 12 textures' closures were fully supported. 12 refusals became 1, the remainder being a known decode gap. |
| D23 | **The client's default spawn square is not universally loadable, and failed as "no scenery" rather than "no keys".** `app_world_load_begin` hardcoded 50,50 with only `TORIRS_WORLD_MAP` able to override it. `cache.643` ships XTEA keys for 1,591 loc squares and 50,50 is not one of them — nor 49,49 / 50,49 / 51,49 / 51,50, a hole directly over Lumbridge. Added a `[cache:boot] spawn=<x>,<z>` manifest key (env still wins, a server REBUILD_NORMAL wins over both) and set the 643 manifest to a keyed square. | **Terrain archives are not encrypted; only `l*` loc archives are.** So an unkeyed square renders ground perfectly and then zero locs — which reads as a renderer or loc-decode fault, not as missing keys, and is indistinguishable from one without checking `world_load`'s loc count. It also hid behind every measurement taken with an explicit `TORIRS_WORLD_MAP=40,55`: the square that was verified working was never the square the client actually booted. Worth generalising — when a default is only valid for some inputs, an override existing is not the same as the default being right. |
| D24 | **Removing the table allow-lists left a bare range walk, which probed tables the cache never had.** `init_reference_tables` iterated 0..TABLE_COUNT and printed `Failed to load referencetable N` for each absent one, so every OldSchool boot emitted ~14 spurious failures for RS2's tables 23..34. Now gated on `dat2disk_table_present` — a table exists iff its `.idxN` file does. Absence is silent; a table whose index exists but whose reference table will not load is still reported. | **The fix for D18/D21 was right but incomplete: the question was never "which ids do we know", it is "which does this cache have".** Both a name allow-list and a range walk answer it from the library's side and are wrong in opposite directions — one hides real tables, the other invents missing ones. The cache itself is authoritative and free to ask. Also a reminder that log noise is a defect: 14 lines of routine "failure" per boot is exactly what trains someone to stop reading the log that would have shown D23. |
| D25 | **Identity fields were declared by every revision module and read by almost none.** `game` was written and never consulted; lineage was smuggled via a third `epoch` value (`EPOCH_643`) while 643 suppressed `revision` to `UNKNOWN` so it could never match a threshold — which made manifests unable to select it except by the `0 == UNKNOWN` coincidence on unset `client_version`. Collapsed to four load-bearing fields (`game`/`epoch`/`revision`/`quirks`), with `epoch` = dat1\|dat2 only, predicates `IsOsrs`/`IsRs2Dat2`/`RevisionAtLeastOsrs`/`RevisionAtLeastRs2`, and boot through required `[cache:boot]` keys → `RSCache_ProfileForIdentity`. Map XTEA is the same class of bug: presence of a key file is not the gate; OldSchool ≥ 237 stores locs plain and RS2 dat2 encrypts from 414 (`RSCache_MapLocsEncrypted`). | Latent for every multi-era boot. Symptoms looked like missing tables, blank scenery, or "works only when client_version is unset". Fixed end-to-end: manifests state identity; `manifest_osrs239.ini` is the unencrypted regression vehicle. |
| D9 | **The round-trip harness was under-specifying the profile.** It built a profile from the group's archive revision alone, so `cache.kronos` was scanned *without* `RSCACHE_QUIRK_KRONOS` — which no revision number can imply. That left 228 loc records misaligned on the ambient-sound retain byte. Fixed by resolving the cache directory name to a declared profile. | Consumption failures on kronos and osrs184 only, both dropping to zero once the quirk was applied. Incidentally the best end-to-end validation that the quirk mechanism works. |

### D7. A regression I introduced and caught

Rewriting `rsbuffer.c` wholesale, I dropped the `++` from `G1b` (cursor stopped
advancing) and replaced `CHARACTERS[]`'s `\uXXXX` escapes with literal UTF-8. Caught
by restoring from git and re-applying as surgical edits, then diffing *removed lines
only* to prove no reader had changed. **Lesson applied for the rest of the work:
edit decoders in place, never rewrite a file that already works.**

---

## E. Test policy exceptions

- **Byte-exactness is reported, not asserted.** 100% is unreachable for the lossy
  decoders in B2 and for the ordering in B3, so asserting it would mean a
  permanently red suite. The regression signal is a *fall* in the percentage.
- **Semantic round-trip is asserted** at 100%, on every datatype and every cache.
  That is the correctness bar.
- **Exact consumption is asserted except for `cache.osrs239`** (B1), which prints as
  `KNOWN GAP`.
- **`same-len` exists to make `exact` interpretable** (B3) — without it a low
  percentage cannot be told apart from data loss.

### B16. healthbar — settled by operand plausibility, not consumption *(Resolved)*

Recorded because the *technique* generalises and E1 would otherwise read as a dead end.

Exact consumption left **41 candidate width assignments** over healthbar's seven
opcodes, all consuming 100% of 85 distinct records. The tie was broken by a second,
independent constraint:

> An operand value larger than the biggest sprite id in its own cache cannot be an id,
> a width, a duration or a colour index. It is a wrong width being read across a field
> boundary.

Applying that to all 41 leaves **exactly one**, and it is the assignment a hand-parse of
the shortest records produces:

```
2 -> u8    3 -> u8    5 -> u16    7 -> u16    8 -> u16    11 -> u16    14 -> u8
```

Two things corroborate it beyond the search. Opcodes 7 and 8 hold values that are all
valid sprite ids, in adjacent pairs (0x0587/0x0588, 0x0880/0x0881) — what a
front-and-back bar pair looks like. And the derivation used only osrs230, osrs239 and
jan2026, yet the decoder is byte-exact on **kronos and osrs184 too**, which were never
in the search: 354 records, six caches, 100%.

Field naming stays honest about the split between what is settled and what is not. The
widths are known, so the records parse; the *meanings* mostly are not, so five fields
are named after their opcode and only the two sprite ids get real names —
`sprite_id_a`/`sprite_id_b`, since which is the front is still unverified. Also worth
noting the packing order is **7, 8, 2, 3, 5, 11, 14**, not ascending; emitting ascending
order would round-trip semantically while reading 0% byte-exact.

### B17. hitsplat — one composite opcode hid the whole format *(Resolved)*

Recorded at length because the **wrong conclusion** was drawn here first, and the
mistake is an easy one to repeat.

The measurement was right: brute-forcing every assignment of operand widths 0-4 over
243 distinct records yields **zero** that consume the file, with or without the
plausibility filter. The conclusion drawn from it — "not a fixed-width opcode stream,
needs a reference client" — was wrong. It is a fixed-width stream except for one opcode:

> **Opcode 18 carries an 11-byte composite payload.** A search bounded at 4 bytes cannot
> represent it, so instead of pointing at opcode 18 the search fails *everywhere* and
> looks like a statement about the whole format.

That is the trap: a search over too small a hypothesis space returns "no solution", which
reads like "the model is wrong" when it actually means "the model is too narrow". A
zero-survivor result says nothing about *which* assumption failed.

What found it was reading the data instead of searching it. The shortest record is
unambiguous by inspection — `05 0d c1 | 08 00 00 | 09 00 96 | 00` is three u16 opcodes
and a terminator — and the longest, `09 00 32 | 12 <11 bytes> | 00`, only balances if
opcode 18 consumes eleven. Re-running the search with opcode 18 allowed up to 16 wide
leaves four assignments; extending the healthbar plausibility constraint to every scalar
width (it had been capped at 2 bytes) leaves exactly one:

```
5 -> u16 (sprite id)   8 -> u16   9 -> u16   11 -> flag   13 -> u16
18 -> 11-byte composite   49 -> u8
```

243/243 records consume exactly. Derived from osrs230, osrs239 and jan2026; byte-exact on
kronos and osrs184 too, which were never in the search — 349 records, six caches, 100%.

Two things are carried rather than interpreted, both deliberately:

- **Opcode order is per record.** Unlike healthbar there is no single packing order:
  `5,8,9` · `5,8,11,9` · `8,49,5,9` · `8,49,5,9,13` · `9,18` all occur. These look like
  distinct kinds of splat with distinct field sets. The order is recorded and replayed,
  as the model encoder does for its per-face index types.
- **Opcode 18's 11 bytes stay raw.** They look like `u16, i16, u16, u8, u16, u16` —
  bytes 2-3 are `ff ff` on every record, which is what an i16 `-1` sentinel looks like,
  and the last three fields track each other. Suggestive, not established; splitting on
  an unverified boundary would bake a guess into the API and buy nothing, since
  round-tripping needs the bytes and not their names.

Only opcode 5 gets a real name (`sprite_id` — 49 distinct values in 1105-4770, every one
a valid sprite id). The rest keep their opcode numbers.

### E1. Exact consumption is necessary but not sufficient — where it fails

Worth stating plainly, because everything above leans on it and it has now come up
short once. Exact consumption proves a decoder **wrong** very sharply; it does not
prove one **right**.

Establishing the `healthbar` opcode widths from the corpus, brute-forcing its seven
observed opcodes over operand widths 0-4 gave **41 distinct assignments that all
consume 100% of 85 distinct records** across three caches. Only three opcodes are
pinned across every one of them.

The technique works when records are long and varied — loc, npc and spotanim have
enough opcodes per record that a wrong width desynchronises and misses the terminator.
It fails when records are short with few opcodes, because there is slack to
re-segment them into an equally-consuming but different parse. The four settled types
in B13 are the good case in the extreme: their records are *fixed length*, so a single
assignment survives.

Practical rule: treat a unique-assignment result as evidence and a multiple-assignment
result as "find a second constraint, or get a reference". Never ship the first
assignment that consumes 100%.

For healthbar the second constraint was operand plausibility against the cache's own
sprite table, which cut 41 candidates to 1 — see B16. That is the first thing to reach
for when consumption leaves a tie.

---

## F. Not ours

`src/engine/uitree_builder/task_interface_open.c` shows as modified in the working
tree and was not touched by this work.

---

## Current state

`make -C 3rd/rscache test` → 1359 checks plus 9 bzip2-interop checks.

| Suite | Checks |
|---|---|
| rsbuffer | 267 |
| container | 505 |
| profile | 120 |
| roundtrip | 331 |
| compression | 99 |
| config_var | 37 |
| bzip2 interop | 9 |

Client builds; `test-db`, `test-net-login`, `test-world-builder`,
`test-uitree-builder-dat1` and `test-ui-slots` pass; both offline boots report
unchanged asset counts (dat1 219 locs / 222 models, dat2 430 locs / 399 models).

**Encoders: 26 — every datatype this library decodes.** The 19 config types (struct,
enum, param, idk, spotanim, obj, underlay, overlay, texture, mapelement, npc, loc,
sequence, **varbit, varplayer, varclient, inv, healthbar, hitsplat**) plus framemap,
sprites, map terrain, frame, clientscript and model.

Semantic round-trip is asserted at 100% on all twenty-six, across every dat2 cache
that carries the datatype.

Byte-exactness, which is reported rather than asserted (see E):

| Band | Datatypes |
|---|---|
| 100% | **varbit, varplayer, varclient, inv, healthbar, hitsplat**, model, frame, clientscript, framemap (old caches), struct |
| ~99% | underlay |
| ~36% | sequence |
| ~25% | sprites — ordering only (B3b) |
| low | obj, loc, npc — ordering plus documented loss (B2, B3) |
| 0% | mapelement, by construction (B2) |

The round-trip harness scans both traversals — config groups (many records per
archive) and whole-archive tables (one record per archive, capped at 2000 archives per
table with the cap printed).

Remaining, both additive and neither blocking:

- **One missing decoder** — varclient_string, the only genuinely blocked case: absent
  from two caches, empty in a third, and the 8 records in osrs239 do not look like
  string variables at all. Six of the original seven are done — see B13, B16 and B17.
- **The remaining revision-taking call sites.** Phase 7 converted the ones that
  matter (see B14); `spotanim` and `component` still take a bare revision. spotanim
  `(void)`s it, so converting it changes no behaviour — cosmetic, and left alone
  deliberately rather than overlooked.

Two model-specific gaps are recorded above and are decoder work, not encoder work:
the trailing region is carried without being understood (B11), and `cache.643`'s ob3
models do not decode (B12).
