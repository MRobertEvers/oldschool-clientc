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

### A2. `pjstr` / `gcstring` are byte-transparent over windows-1252 *(Resolved)*

Earlier reasoning treated this as unfixable: the decoder mapped bytes 128–159
through a Unicode table then truncated with `(char)`, which is not injective
(bytes 149 and 153 both land on `0x22`, and the truncated results collide with
ordinary ASCII). That argued correctly against inventing a *reverse* map on the
encoder — but the defect was the map on the *decoder*. Two other read paths in
the same library (`RSCache_BufferReadParams`, npc opcodes 2 / 30–34) already
returned raw wire bytes.

Both directions are now byte-transparent: decoded strings are windows-1252 wire
bytes, and `encode(decode(x))` reproduces every byte in `0x01..0xFF`. Unicode is
a consumer concern via `RSCache_Cp1252ToUtf8` / `RSCache_Utf8ToCp1252` (the five
bytes windows-1252 leaves undefined map to U+0081..U+009D so the pair is a
bijection).

### A3. `gbit`/`pbit` were not added *(Deviation)*

The plan listed a bit cursor as needed for model and frame encode. Checked: **no
rscache decoder does bit-level access.** The only bit writer in the repo is a local
helper in a net test. Unused API not added.

### A4. Phase 8 (README) was done before Phase 5 (encoders) *(Deviation)*

Reordered deliberately. The README is one of the four explicit asks, and writing it
straight after the container work meant documenting formats I had just verified
field by field. Phase 5 is ~33 encoders and would have deferred it a long way.

### A5. Cross-cache porting *(Updated)*

The library itself still ships no automatic porting layer inside the codecs —
decode with the source profile, encode with the destination's, and let the
caller decide fields the destination added. What *has* been added is
`3rd/rscache/tools/` (`find_anims`, `port_npc`) plus the write helpers those
tools need (`RSCache_Dat2Edit*`, `RSCache_Dat1DiskWriteArchive`,
`RSCache_Dat1Edit*`, dat1 npc/seq/anim encoders). See `tools/README.md`.

Known lossy conversions the tools surface rather than hide:

- **osrs239 NPC records** are refused — the decoder does not consume them
  exactly under either head-icon shape.
- **dat2 → dat1 models** re-encode to OB2; OB3/V2/V3 texture render types and
  animaya skin data are dropped with a warning.
- **dat2 framemap → dat1 AnimBase** drops `transform_actor`, `masks`, and
  trailing skeletal blobs.
- **retexture / texture ids** are cache-local and do not map across revisions
  unless `--texture-map` is supplied.
- **bzip2 jagfile repacks** are valid but not byte-identical to Jagex (A1).

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

### B1c. Modern framemap trailing bytes — the skeletal rig, now decoded *(Resolved, one outlier)*

Originally measured by comparing each archive's size against what its decode accounts
for:

| Cache | Archives | delta 0 | delta 2 | other |
|---|---|---|---|---|
| `cache.kronos` (format 5, table ver 0) | 1887 | **1887** | 0 | 0 |
| `cache.osrs230` (format 7, table ver 67) | 2429 | 0 | 2338 | 91 |
| `cache.jan2026` | 2613 | 0 | 2495 | 118 |

The trailing region is the **SkeletalBase** (Animaya bind pose) that idx22 curve sets
animate, appended after the classic transform-type / bone-group lists:

```
u16 boneCount                       (0 == no rig; this is the "delta 2" 00 00)
if boneCount > 0:
  u8 poseCount
  boneCount x: s16 parentId, poseCount x (16 f32 localMatrix + 3 f32 unused)
```

`dat2_skeletalbase.c` decodes it out of `RSCache_Dat2Framemap.tail`. Re-measured with
that decoder, requiring the rig to consume the tail **exactly**
(`3 + bones * (2 + poses * 19 * 4) == tail_size`):

| Cache | Framemaps | no tail | `00 00` (no rig) | rigs | exact | unexplained |
|---|---|---|---|---|---|---|
| `cache.kronos` | 1887 | **1887** | 0 | 0 | — | 0 |
| `cache.osrs230` | 2429 | 0 | 2338 | 90 | **90** | 1 |
| `cache.osrs239` | 2674 | 0 | 2547 | 126 | **126** | 1 |

Every rig consumes its tail to the byte, so the layout is established rather than
guessed. The single outlier is archive **1941** in both OSRS caches: a 1538-byte tail
beginning `00 03 00 03 00 03 01 05 …`, which reads as `boneCount=3, poseCount=0` and is
rejected. The shape (2-byte pairs) looks like more *classic* group data, i.e. that one
archive's header is under-consumed, not a malformed rig — untraced.

Byte-exactness is unaffected either way: the encoder already appends `tail` verbatim,
so a decode→encode round-trip is exact for old and modern caches alike. Quantified in
`dat2_framemap.h`.

The bake on top of the rig (`RSCache_Dat2SkeletalBaseBakePalette`) has no encoder —
same decode-only footing as `dat2_animaya.c`, which it consumes.

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
| loc | the largest lossy set of any type — roughly 23 opcodes consumed without storing (25, 44, 45, 88/90/91/96–105, 163–191 and the boolean-flag block), plus opcode 95's pre-220 payload. (Opcode 61, `category`, left this list on 2026-08-02, for the same shape of reason opcode 69 did: it is the loc's category in the same id space `dat2_config_npc.c` decodes at opcode 18 and `dat2_config_obj.c` at 94, and the server needs it to answer a category-keyed trigger — LostCity's `doors.rs2` binds `[oploc1,_door_closed]`. That it is a category rather than some other u16 was checked against the data, not assumed: 684 is 63 records of which 43 display 'Bank booth', 237 is 11 bank chests, 907 is 360 bookshelves, and the ids share a space with the other two types (max npc 2504 / obj 2506 / loc 2474; 9 ids carried by both npc and loc). Measured effect on loc round-trip, osrs239: **exact 581 -> 786**, and the text layer's `lost-here` went 0 -> 205 -> 0 as `cp_loc.c` gained the key, which is the sequence worth knowing — storing a field the text form cannot express is a regression in the tool even though it is a fidelity gain in the library. 8,407 of 62,194 records state one; the golden digests moved on exactly those 8,407.) (Opcode 69, `force_approach`, left this list on 2026-07-29 — the client's pathfinder approach test needs it; see docs/PATHING_INTERACTION_PARITY.md. 2459 of `cache.osrs230`'s locs carry it. Measured effect on loc round-trip, osrs230: **exact 508 -> 545**, same-len-incl-exact 29667 -> 29492, so 175 records moved same-len -> differ. Those 175 were length *coincidences*: they carry opcode 69 **and** other opcodes from this same lossy list, and the two losses happened to cancel in length. Nothing regressed — `semantic` stays 100% on every cache, and cachepack's `lost-here` stays 0.) Also collapses three groups of aliased opcodes: actions 0–4 (writable via 30+i *or* 150+i), map_function_id (60, 82, 107) and map_scene_id (68, 102) — the encoder emits the lowest opcode of each. Hence ~0–1% byte-exact against ~52% same-length. |

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
         14 -> u8 waterScale << 2         NEW    15 -> u16 secondaryTexture (65535 => -1) NEW
         16 -> u8 waterIntensity          NEW

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

### B20. Sound effects — one codec, two eras, and the samples that are not decoded *(Resolved, one gap)*

Sound effects are an FM synthesiser program, not a recording, so "decoding" them has two
halves: `sound_synth.c` reads the program and `sound_render.c` runs it. Both are here for
the same reason `dat2_proctexture.c` is: the meaning of the format belongs next to the
decoder, or every consumer re-derives it.

The record layout is identical in both containers — the packaging is all that differs — so
the files carry no `dat1_`/`dat2_` prefix:

| Lineage | Packaging | Filter | Corpus result |
|---|---|---|---|
| rs2/dat1 254 | `sounds.dat` in config archive 8 | no | 4 caches, whole file byte-exact |
| rs2/dat1 377 | same | yes | 2727 effects, 929467/929467 bytes, byte-exact |
| oldschool/dat2 184–239 | one archive per id | yes | 4211 / 10279 / 12007 effects, all byte-exact |
| rs2/dat2 634, 643 | one archive per id | yes | 10154 / 10232 effects, all byte-exact |

**The era gate is measured, not guessed, but it cannot be detected.** The tone record grew
a trailing `SynthFilter` + envelope when the engine was rewritten. The two flavours are
indistinguishable byte-for-byte at record level — the only signal is that a whole container
stops consuming exactly, which is a property of the *file*, not the record. Parse 254 with
filters and it desyncs on the second record; parse 377 without and it desyncs immediately.
So `RSCache_SoundCodecVersion` states the boundary from the profile, at RS2 revision 377.

*Gap:* no cache in the corpus sits between 255 and 376, so the exact revision that
introduced the filter is unmeasured. 377 is the earliest cache that has it and 254 the
latest that does not. `test_sound.c` asserts the codec each cache resolves to, so a wrong
gate fails loudly instead of decoding the other flavour.

**Byte-exactness is 100%, and that is not a coincidence.** Unlike the config types (B2, B3),
this format has no opcode ordering to reproduce and no field the decoder drops, so
`encode(decode(x)) == x` is achievable and is *asserted*, not merely reported. Two things
were needed for it:

- **The dat1 bank preserves wire order.** `sounds.dat` records are not sorted by id —
  `cache.rs377`'s first record is id 1278 — so `RSCache_SoundBank` carries an explicit
  `order[]`. Encoding in id order was the first version and it failed at byte 0.
- **The filter's envelope has non-zero defaults.** When the wire carries no filter stage
  list, the reference's envelope keeps what its *constructor* set: a two-stage 0..65535
  ramp. Leaving it empty decodes fine and round-trips fine, and silently freezes the filter
  sweep at zero — audible, and invisible to every framing check. The decoder installs the
  defaults; the encoder does not write them.

*Gap: Jagex-compressed samples are identified, not decoded.* Modern OldSchool (239) also
ships audio in a "BCV" container in the same table — 119 archives pair it with a synth
record as group file 1, and 3 archives are sample-only. `RSCache_SoundSampleKindOf`
recognises them so a caller skips them rather than decoding noise; the 119 paired ones still
play their synth record. Decoding BCV means a Vorbis implementation plus the shared codebook
file, and rt4's `VorbisSound` header layout does not even match these bytes, so there is no
reference to port — it would be a codec re-derived from scratch. Left out deliberately;
the effect is 3 of 12010 OldSchool-239 sound ids being silent.

*Deviation: the noise table is always the seeded sequence.* Waveform 4 reads a table of
±1 values. rt4 fills it from `new Random(0L)`; Client-TS and Client3 fill it from an
unseeded RNG, so their table differs run to run. Both are white noise and neither client's
exact sequence is meaningful, so the renderer always reproduces Java's seeded sequence
(`java_random` in sound_render.c). That is what makes a render byte-stable, which is what
lets the test compare two renders of the same effect at all.

**The render was checked against the reference implementation, not just against
itself.** Client3's `src/sound/{envelope,tone,wave}.c` — a C port of the 2004 client's sound
code, and what the dat1 path is written against — was run over the same `sounds.dat` with
only its unseeded noise table replaced by the seeded one (see below), and compared byte for
byte. **Every effect in all four dat1 caches is byte-identical**: 696 in cache254.lostcity,
579 in cache254, 696 each in rs254_zuk and rs254_steeltitan; 2667 effects, 55 million
samples, zero differing bytes. `test_sound.c` keeps a CRC of that verified output as a golden
constant, since the comparison itself needs the reference's sources next to ours.

Getting there took one non-obvious thing. **An unusable loop range changes the output
length.** The WAVE generation's length is `sampleCount + span * (loopCount - 1)`, and the
validity check forces `loopCount` to 0 — leaving `sampleCount - span`. So an effect whose
loop end runs past its own duration comes out *shorter than its own tones* (id 221 loses
200ms of tail) and one whose loop bounds are reversed comes out *longer*, silence-padded (id
383 gains 1.2s). Nine of cache254.lostcity's 696 effects depend on it. Rendering the natural
length instead was the first version, and it was the only thing standing between 687/696 and
696/696. `RSCache_SoundPcmExpandLoops` handles the matching zero-repeat case for a *valid*
span the same way.

*Deviation: the two mixing generations are kept apart.* Tone synthesis is identical in both
eras; summing tones into the output is not. dat1 (Client-TS `Wave.generate`) starts the
buffer at -128 and adds in **wrapping** 8-bit arithmetic — an overflowing sum wraps, which
is audible and is part of how these effects sound. dat2 (rt4 `SynthSound.getSamples`) starts
at 0 and **clamps**. Each era is matched to its own reference client rather than both being
cleaned up to clamp. The gate is the same one the filter uses, on the argument that both
changed with the same engine rewrite; that pairing is a judgement call, not a measurement.

### B21. World map geography — read for OSRS <= 237, and now for >= 238 too *(Fixed)*

`dat2_worldmap_geography.c` decodes cache table 18 (the tiles the world map draws:
floor ids, overlay shapes/rotations, and the locs that become wall lines and map
scene icons). It is complete for the layout used up to OSRS 237, where the
compositemap record names its geography group/file outright and the file repeats
its own marker and region coords — verified by rendering `cache.osrs230`'s main
area, which comes out as recognisable Gielinor.

From OSRS 238 the compositemap drops the group/file pair
(`RSCACHE_WORLDMAP_DECODE_REV238_NO_GROUP_FILE`) and the geography file drops its
header. The addressing was worked out first and has been correct throughout:
table 18 becomes a sparse array indexed by `(region_x << 8) | region_y`, one file
per group — `cache.osrs239`'s 15938-slot table has 2101 populated groups, the
first being 3872 = region 15,32 and Lumbridge's 49,48 being 12592.

**The tile record itself was mis-measured, not unknown — the grammar was already
solved elsewhere in this tree and just never reached the shared decoder.**
`tools/cachepack/cp_decode.c`'s `geo_decode` (written for `--assets` export, a
separate codec from this file) worked it out: the tile grammar is unchanged from
<=237 with exactly one field width difference — **a loc's id is a plain `u32`,
not a `BigSmart`.** Real loc ids are always well under 32768, so a `BigSmart`
read always takes its 2-byte form; reading a true 4-byte field as 2 under-reads
by 2 bytes on every loc-bearing tile, and the deficit compounds until a
downstream byte looks like a bad floor id or the stream runs out mid-tile. That
is exactly what the old "consumes exactly, yields floor ids that don't exist"
symptom was: 355 of the 2,101 files happened to have zero loc-bearing tiles (or a
coincidentally length-preserving one) and decoded "exactly" by luck, which read
as evidence the grammar was close to right when it was the desync not yet having
had a chance to happen. `geo_decode`'s own measurement: reading the id as
`BigSmart` put 883/2,101 files out of bytes mid-tile; reading it as `u32` takes
that to 2,057/2,057 (every single-file archive) exact.

Ported into the shared decoder (`RSCache_WorldMapGeographyDecodeInplace`,
`worldmap_read_tile`) so the live client benefits, not just cachepack's asset
export. `task_dat2_worldmap_geography_load.c` no longer refuses derived-address
records; `headerless` is now a decoder parameter orthogonal to `kind` (region vs
chunk), since the compositemap record always knows its own kind and destination
regardless of which era's addressing found the file. Verified: `cache.osrs239`'s
Lumbridge/Al Kharid area renders as real Gielinor terrain
(`./run-worldmap.sh manifest_osrs239_worldmap.ini --headless`), all 2,101
geography files still round-trip byte-exact through `geo_decode` (unaffected —
separate codec), and the client's own decode goes from "decoded nothing" on
every region to zero `archive=MISSING` / zero truncated-stream errors under
`TORIRS_WORLDMAP_DEBUG=1`.

**Still open:** the ~165 files whose tile count is a multiple of 64 but not
4,096 — a region assembled from more than one chunk bundled back to back in a
single file, with no per-chunk coordinate inside the file to say where each 64-
tile block after the first goes. The decoder reads only the requesting record's
own chunk and stops rather than guess the rest's placement; those regions may be
incomplete rather than wrong. Worth revisiting if a specific area turns out to
be one of the 165 and matters.

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
| D26 | **npc opcodes 100/101 read ambient and contrast as *unsigned* bytes, and did not pre-scale contrast.** The reference is `ambient = g1b()` and `contrast = g1b() * 5` (Client-TS `config/NpcType.ts`, matching obj opcode 114). `g1` turned every darkening npc into a brightening one — `cache.osrs230` decoded 113 records at `ambient=231` and 24 at `246`, which are `-25` and `-10`. `calculateNormals` then lit them at `64 + 231`, saturating every face to white. The encoder now writes `p1b` and `contrast / 5`, which is byte-identical to what it wrote before, so no round-trip number moves. | The value distribution *is* the proof: a signed field misread as unsigned has a hole in the middle and a cluster at the top, and npc ambient had exactly one — nothing between 100 and 231, then 231/241/246. obj and loc, which already used `g1b`, showed the negatives directly. Semantic round-trip stays 100% on all six caches. |
| D27 | **npc recolour/retexture pairs were stored in `short`.** Both are unsigned 16-bit — an HSL word or a texture id — so everything from `0x8000` up came back negative, and the only reason nothing broke was that the encoder and the client-side adaptor each happened to cast back through `uint16_t`. Widened to `int`, which is what every other config struct in the library already uses for the same data, and the two compensating casts are gone. | Found by auditing the library for colour data in signed types after D26. Latent rather than live — but it is a trap that only holds while every consumer remembers to undo it, and `find_named` already printed the negatives. |
| D28 | **loc opcode 39 pre-scaled contrast by 25 for every era, including dat1.** dat2/OldSchool is `readByte() * 25`; dat1 is `g1b() * 5` (Client-TS `config/LocType.ts`). A dat1 loc therefore came out five times as attenuated as the reference. Now gated on `RSCACHE_CONFIG_LOC_DECODE_DAT`, with the encoder dividing by the same era-dependent multiplier. | Surfaced while fixing the client's lighting, which had a compensating `* 5` on the *consumer* side (`ToriDraw_LightModelDefault`) — correct for dat1's raw byte, wrong for anything the library had already scaled. With the consumer's multiplier removed, the era gate is what keeps dat1 right. |
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

## G. The CS2 language layer

`src/cs2/` decompiles a clientscript to CS2 source and compiles it back. The
decompiler is a port of [RuneStar/cs2](https://github.com/RuneStar/cs2) (Kotlin,
at 2a8b8fc); the compiler has no upstream and is new.

### G1. Two scripts decompile differently from the reference, by coin flip *(Gap)*

Measured by `test_cs2`, which decompiles all 7,884 scripts in RuneStar's `input/`
dump and compares byte for byte against the output its own implementation
produced:

| | |
|---|---|
| scripts in the dump | 7,884 |
| decompiled by this port | 7,366 |
| the reference also produced | 6,491 |
| **byte-identical** | **6,489** |
| different | 2 |

That 6,489/2 split held unchanged through every later change — the client's
opcode table, 41 inferred arities, a new type, three relaxations of upstream's
strictness. It is the regression signal for all of it.

The two are `[clientscript,build_makeover_feet]` and
`[clientscript,makeover_feet_select]`, and the difference is one identifier:
`$settextalignv6` against `$settextalignh6`.

Both are correct. The value is passed to `cc_settextalign` as *both* its
horizontal and its vertical argument, so the identifier solver is choosing
between two prototypes that are equally strong. Upstream picks by iterating a
`HashSet<Prototype?>` and taking the first match — Java's hash order. There is no
right answer to reproduce, only a specific JVM's iteration order, so this is
allowed by exact count rather than chased: `test_cs2` fails on a third
divergence.

Everything else about those two scripts matches, and an identifier is a naming
hint with no effect on what the script does.

### G2. The compiler reaches a source fixed point on 99.2% of what it compiles *(Gap)*

Two different questions, and they answer differently.

**Does compiling the source reproduce the cache's bytes?** `cs2 roundtrip`:

| | |
|---|---|
| decompiled | 7,467 |
| compiled | 6,614 (89%) |
| same length | 3,036 |
| **byte-exact** | **2,877** |

**Does compiling and decompiling again reproduce the source?** Decompile the
cache, compile that, decompile the result, compare source to source:

| | |
|---|---|
| compared | 6,995 |
| **fixed point** | **6,939 (99.2%)** |
| changed | 56 |

The second is the real bar for a compiler and the first is capped by design, so
they are reported separately. Byte-exactness is limited by **information the
decompiler deliberately discards**: control-flow reconstruction collapses an
`if`/`else` whose taken branch never rejoins into a plain `if` followed by the
next statement. Both mean the same thing, both run identically, and they compile
to different jump targets. `[proc,min]` is the smallest example — 11 opcodes, one
operand different. Recovering it would mean emitting an `else` the decompiler
went out of its way to simplify away, making every listing worse to read in order
to improve a number.

The fixed point has no such excuse, and chasing it found three real bugs that the
byte comparison had not isolated:

1. **Script names are unique only per trigger.** `1v1arena_hud_toggle` is
   clientscript 2716 *and* proc 2717, and there are hundreds of such pairs. The
   name lookup matched the bare name and returned whichever came first, so
   `~1v1arena_hud_toggle` compiled to a call to the wrong script — valid
   bytecode, different program. `RSCache_CS2_NamesScriptId` now requires the
   trigger. Fixed point 5,816 -> 6,604.
2. **`&` binds tighter than `|`.** The condition parser applied them
   left-to-right, so `a & b | c & d` compiled as `a & (b | c) & d`. Also silent:
   it compiles, and changes when the branch is taken. Fixed point 6,604 -> 6,939.
3. **Markup rollback emitted the preceding text twice.** `<...>` is both string
   interpolation and the game's own markup, told apart by trying to parse it;
   the rollback unwound the expression but not the literal run before it, so
   `"<col=ff9040>Tutors</col>"` came back with its text duplicated.

The remaining 56 are two shapes: a `.`-form hook losing its active-component
flag, and an `else if` chain compiling to a jump structure that reconstructs as
two separate `if`s. Both run identically to the original; neither is diagnosed.

### G3. Hook argument descriptors are inferred, not read *(Gap)*

The largest single class of compile failure is one thing. A hook registration carries a
descriptor naming each callback argument's *type*:

```
if_setonop("ignore_op(event_opindex, $string1, $string0)", $component0);
```

The descriptor (`iss`) is not written in the source — the decompiler drops it
because the argument list already implies it. Compiling back therefore has to
re-derive one letter per argument, and does so from the argument expression: a
local's declared type, a command's result type, an `event_*` value's fixed type,
or a name only one table claims.

Three cases defeat that, and all three are refused rather than guessed:

- `null`, which is `-1` for every type;
- a name like `coins_995`, which the decompiler prints identically for `obj`,
  `loc`, `npc`, `model`, `struct` and `seq`;
- a `~proc` call, whose return types live in the callee.

All three are answerable from the **callee's** signature, which a compiler
processing a whole directory of sources has and this one does not yet consult.
That is the fix, and it is plumbing rather than research: a pre-pass over the
source set building script id → argument types, threaded through
`RSCache_CS2_CompileOptions`. Not done here.

A wrong letter does not fail loudly — it changes how the *callee* reads its own
arguments — which is why inference stops rather than picking the likeliest.

### G4. 227 of OSRS 230's scripts do not decompile, and the arities were solved from the corpus *(Gap)*

Against `cache.osrs230` (7,884 scripts), **7,657 decompile and 227 do not** — up
from 6,634 when this section was first written. Two things closed the gap, and
the second is a method worth recording.

**Layering the client's stack table** (`src/cs2vm2`) over RuneStar's 2021
`Command.kt` supplied arities for opcodes the vendored sources predate: 6,634 to
7,553. See `tools/README.md`.

**Solving the rest from the corpus.** An opcode's pop/push counts are not in the
bytecode, but they are *implied* by it: a script only interprets to the end if
every arity keeps the operand stack balanced. `cs2 infer-arity` searches for
them:

1. Take the scripts where the opcode is the only unknown, so nothing else can
   absorb a wrong answer.
2. Try every plausible (int in, str in, int out, str out) and keep those under
   which the script interprets.
3. Intersect across all such scripts, and iterate — each solved opcode turns
   more two-unknown scripts into one-unknown ones.

Two refinements did most of the work:

- **Judge on interpretation alone, not on a full decompile.** A script can
  interpret cleanly and still fail later on an unnamed constant or an unrelated
  type contradiction. Grading arities on the whole pipeline reported "no arity
  works" for opcodes whose arity was in fact pinned.
- **Require every `return` to match the arity the script's own epilogue
  declares.** The language has no varying-arity return, so a mismatch is not a
  property of that statement — it is evidence that something above popped the
  wrong number of values. This one constraint took the solver from 15 opcodes to
  31, and is now enforced during ordinary decompiles too, where it converts a
  class of silent mis-decompiles into refusals.

A third phase solves **pairs**: an opcode that never appears alone is still
constrained jointly, since most (X, Y) combinations do not balance. That found
nine more.

41 opcodes were solved this way and are recorded in `local_commands.py` with
their witness counts, because the evidence differs: opcode 4124 agrees across 30
scripts, several others across one. Where survivors produced *identical source*
the smallest was taken and marked `output-equivalent` — an opcode whose results
nobody consumes decompiles the same however many it is said to push.

**This is inference, and it is labelled as such.** The prototypes are plain
`int`/`string`, because the method establishes counts and not meanings: a value
that should print as `$width3` prints as `$int3`. What makes it safe rather than
guessing is the check that it changed nothing: the reference comparison (G1)
stayed at exactly 6,489 identical and 2 different across every round, so no
inferred signature altered a single output that the RuneStar implementation also
produced.

What remains:

| Cause | Scripts |
|---|---|
| opcode with no arity — appears only alongside other unknowns | 104 |
| operand-stack shape disagreements | 62 |
| type or identifier contradictions | 16 |
| `gosub` argument stack-type mismatches | 10 |
| return arity or type disagreements | 11 |
| other (one unknown descriptor byte 0xB8, one absent callee, one unnamed maparea) | 3 |

The first row needs 39 opcodes that occur only in scripts with three or more
unknowns; the search is combinatorial past pairs, so those need identifying in a
client rather than solving. The stack-shape rows are the interesting residue:
they name `pop_int_local` and `pop_string_local` as the point of failure, which
means some *known* opcode's recorded signature has drifted from what revision
230 actually does. The same solver could be pointed at signed opcodes to find
which — not done here.

### G5. Name tables are optional, and four types made them mandatory *(Resolved)*

Ids are what a cache stores; names are community-recovered and ship in RuneStar's
`*-names.tsv`. They are loaded at run time and not vendored — 2.1 MB of data that
belongs to a corpus, not to a codec — so a decompile without them is correct but
reads `obj_995` rather than `coins_995`.

The exception *was* `boolean`, `stat`, `maparea` and `fontmetrics`, which have
no numeric spelling in the language. An id those four tables did not name could
not be printed and the script was refused — which made a corpus of names a
*correctness* dependency rather than a legibility one, and quietly halved what
`cachepack` could decompile, since it has no name directory to point at. Against
cache.osrs239: **3,612 failures without the tables against 683 with them.**

Resolved, and the two halves have different reasons.

`false` and `true` are now seeded in `RSCache_CS2_NamesInit`. They are not
community-recovered data — they are the language's own words for 0 and 1, and
`boolean-names.tsv` is two lines long. A loaded TSV still overwrites them. That
alone was 2,472 scripts.

`stat`, `maparea` and `fontmetrics` fall back to `<literal>_<id>`. The claim
that they have no numeric spelling was simply wrong: `cs2_cc_parse_id_suffixed`
already reads that form back for every unique-id type, so it round-trips
exactly. And refusing was not even protective — it still took 12 scripts *with*
the tables loaded, because a live cache outruns a community table. `stat_23` is
Sailing; `maparea_42` is a region added since those TSVs were last written. A
listing that says `stat_23` is exact. One that refuses the script says nothing.

`boolean` alone still refuses a value outside its table, and that is deliberate:
0 and 1 are both always named, so a third value is not a missing name — it is a
slot the type solver put a non-boolean in, and `boolean_7` would hide it.

`char`, `area` and `mapelement` print as plain numbers, joining `newvar`,
`spotanim` and `player_uid` below under the same argument.

Coverage is now identical with and without the tables: 9,308 of osrs239's 9,725
either way.

Two further departures from upstream, both strictly more capable:

- **A stale script name degrades instead of failing.** When the name table says a
  script is a `clientscript` but the bytecode calls it as a proc, upstream throws;
  this writes `script<id>`, which is exact. It is upstream's largest failure
  class, and dropping it recovered roughly 700 scripts.
- **`newvar`, `spotanim` and `player_uid` print as numbers.** Upstream has no
  spelling for them and throws. They are plain ints with no name table, and the
  number compiles back.

Neither changes any of the 6,489 outputs that match the reference.

---

## H. `cachepack` — the cache unpacker / packer

`tools/cachepack` turns an OldSchool cache's configs into editable text and back,
on LostCity_Server's architecture. Its full documentation is in `tools/README.md`;
what belongs here are the places it stops short.

### H1. `dbrow` and `dbtable` are unpack-only *(Gap)*

rscache decodes both (`dat2_config_db.c`) and encodes neither, so `pack` skips them
and the base cache's records pass through unchanged. Writing an encoder from the
decoder's struct would be an unvalidated guess at a format nothing in the repo can
check, and a wrong dbrow does not fail loudly — it feeds a CS2 script a plausible
value from the wrong column. The text is still emitted, since it is the only
readable view of what a client database table declares.

### H2. The text layer is faithful; the codecs' losses are inherited *(Gap, measured)*

Everything B2 and B3 record about the encoders applies to a repack, because a repack
goes through them. What `cachepack verify` adds is the ability to *attribute* a loss:
it runs the library's own decode→encode beside the full record→text→record trip and
reports `lost-here`, the count of records the codec reproduced byte-exactly and the
text did not.

Measured on `cache.osrs230` (157,253 records over 18 encodable types) and on
`cache.osrs239`: **`lost-here` is 0 for every type**, and the `exact` column equals
`codec-ex` throughout. So the text layer costs nothing beyond what the library
already costs.

That column is not decorative. It was built after the first full run showed overlay
and param losing 188 and 122 records more than their codecs did, both from the same
mistake: `hide_underlay` and `auto_disable` default to **true**, and their opcodes
*clear* them, so writing the line only when the flag is set drops the opcode from
every record that carries it. Against raw differ counts of 192 and 416 those read as
"this type is a bit lossy". Against codec baselines of 4 and 294 they were obvious.

The stronger check is the **source fixed point** — unpack, pack, unpack, diff — which
holds even where a record re-encodes to different bytes, because what it asserts is
that nothing the client can see was lost. `cache.osrs230` is a fixed point across all
twenty `all.<type>` files and all twenty pack files, and the client's boot log against
the packed cache is identical to the original's.

### H3. Two bugs this found in code it does not own *(Resolved)*

Recorded because both are traps for the next caller, not just for this tool.

- **`RSCache_Dat2ConfigIdkFree` is `free(idk)` and nothing else.** It releases the
  struct and leaks every array on it, so it cannot be used on a stack record — and
  using it anyway aborts on a free of a stack address. Every other config type has a
  matching `...FreeInplace`; identkit does not. `cp_idk.c` carries a local one.
- **`RSCache_ReferenceTable.archives` is indexed by archive id, not packed by
  position.** The decoder writes `table->archives[ids[i]]` and leaves the gaps at
  `index == -1`. A linear search for a matching `index` finds a different entry
  entirely, and the symptom is subtle: a reference table rewritten on every import
  carrying some other archive's CRC.

### H4. The reference-table CRC covers the container minus its version trailer

Not a gap — a fact that had to be established, and one nothing in the library states
because the library's own writer never emits a trailer, so for it the two spans
coincide. A stored Jagex archive does carry a u16 version after the payload, and the
CRC does **not** include it.

Measured over `cache.osrs230` idx13: `crc32` of the whole stored archive matches the
table's CRC on **0 of 10** archives, `crc32` of `size - 2` on **10 of 10**. Getting it
wrong writes a CRC the client then rejects the archive for, so `cp_binary.c` derives
the body length from the container header rather than assuming a trailer width.

### H5. Two ways out for the non-config tables, and neither transcodes *(Deviation, deliberate)*

`--binary` carries models, sprites, maps, scripts and sounds as the bytes the
container holds. Raw is byte-exact by construction, lets XTEA-encrypted map archives
through untouched, and keeps A1's bzip2 non-identity out of the picture entirely —
at the cost of being unreadable.

`--assets` is the readable one: the archive's *payload*, named, in a directory
laid out like LostCity's `content/`. Round-trips exactly at the payload level
(unpack → pack → unpack returns `cache.osrs239`'s whole tree byte-identical, 117,086
files, and the client's boot log against the repacked cache matches the original's
line for line), but not at the container level, because the payload is recompressed
on the way in.

**Neither converts formats**, and that is the point. Writing LostCity's actual
formats would mean transcoding, which A5 already records as lossy for exactly the
assets anyone would want it for — dat2 → dat1 `.ob2` drops OB3/V2/V3 texture render
types and animaya skinning, and dat2 framemap → `.base` drops `transform_actor`,
masks and skeletal blobs. `tools/port_lostcity` is the converter; keeping the two
apart means neither has to compromise.

### H6. Extensions in the asset tree are read off the payload, not assumed from the era

Worth recording because the obvious thing to do is wrong in both directions.

LostCity stores models as `.ob2` because a rev-254 model *is* an OB2. An OldSchool
model is not: a census over all 61,615 in `cache.osrs239` finds **26,990 osrs-v2,
34,625 osrs-v3, and zero ob2 or ob3**. Naming them `.ob2` would label every file
after a format it does not contain. So the extension comes from the magic trailer,
per file — a real ob2 gets `.ob2`, a real ob3 `.ob3`, the OldSchool formats
`.model`.

The same check pays off the other way. Sniffing the payload for container magic
finds that **table 10 is JPEG and table 20 is PNG**, which is how `binary/title.jpg`
comes out right with no converter — and that the **music tables are not MIDI**
(`17 07 f6 02`, Jagex's own container), so they get `.jmid` rather than a `.mid` that
no player would open.

One trap the sniffing introduces and has to avoid: a *whole* multi-file archive
begins with its first member's bytes, so detecting on it names the archive after one
of the files inside. Hence `CP_ASSET_SIZE_ARCHIVE`, which suppresses detection for
that case.

### H7. Most multi-file archives are not exploded into directories *(Deviation from the first cut)*

The first implementation turned every multi-file archive into a directory of its
files. It worked and round-tripped, and it was the wrong shape: **352,849 files,
209,295 of them individual animation frames.**

Two things say so. LostCity does not do it — an `.anim` *is* the archive, which is
why its `models/` holds 4,229 files and not a quarter of a million. And an animation
frame is not independently useful: the animset is the unit anything loads, names or
edits.

So the flag is now the exception, kept for `textures` (one archive, 210 material
definitions, each with its own id and its own line in LostCity's `texture.pack`) and
`dbindex` (a master index plus one file per indexed column). Everything else stores
the archive's payload whole, which costs nothing — the payload is the container's own
file table plus the files — and takes the tree from 352,849 files to 117,086.

### G7. OldSchool stopped typing hook arguments, and the strict freeze took 1,102 scripts *(Deviation, deliberate)*

A hook passes its callee's arguments with a descriptor string — one type letter
each — and the reference freezes the callee's parameter to each letter. This port
did the same, faithfully, and 1,102 of osrs239's 9,725 scripts died of it: every
`component/int`, `graphic/int`, `enum/int` and `struct/int` contradiction in the
failure tally, 996 of them `component` against `int`.

The cause is not in either implementation. Script 119 is four instructions in both
RuneStar's cache and rev 239, with an unchanged body that hands its argument to
`if_sethide`'s component slot — and its descriptor reads `I` (component) in the
old cache and `i` (int) in the new one. Running this decompiler on RuneStar's own
`input/119` decompiles it; on rev 239's it fails. **OldSchool stopped putting the
real type in hook descriptors and started writing `i`.**

So `int` in a modern descriptor is no longer a claim, it is the absence of one, and
freezing it asserts knowledge the byte does not carry. It is now the one letter
that does not freeze: the callee's parameter is left to the solver, which types it
from the body's use, or falls back to `int` at its last step if the body says
nothing either. Every other letter still means what it says.

Two controls, because a type solver that fails less is not obviously right:

- **The reference comparison did not move.** Decompiling RuneStar's own cache and
  diffing against their published output stays at **6,489 identical / 2 different**,
  the same figure it has held through every previous change — as it must, since the
  deviation only fires on a letter their cache almost never wrote.
- **The 91 already-working scripts it did change, it improved.** All 91 are
  parameters that read `int` before and now read `component`, in scripts that go on
  to call `if_getx($component0)` and `if_setontimer(null, $component2)`. The old
  output was not wrong syntax; it was the wrong type, printed confidently.

7,878 -> 9,042 of 9,725. The same measurement retired the last of the
`identifier 'keychar' does not belong to type 'int'` failures, which were the same
contradiction reaching the identifier solver instead of the type solver.

### H8. Three encoders the library was missing, written for the readable asset forms

`--assets` can decode maps, interfaces and the world map into text only because both
directions exist. None did before.

**`RSCache_MapTerrainEncode` dropped a byte on every square.** The tile loop is a
fixed 4 × 64 × 64 and the stream carries no length, so the decoder stops when it has
read the last tile — and every OldSchool 239 square has one byte after that, non-zero
in 1,612 of 2,934. One square (archive 16457) has 9,564. Nothing reads them, which
is exactly why nothing noticed: the loss was symmetric, so the source fixed point
still held and only a payload comparison against the original cache found it. They
are now kept on `RSCache_MapTerrain.trailing` and written back — the B8 pattern
again — and appear in the jm2 as `trailing=<hex>` in the MAP header.

The same measurement found the other half: **a buffer read past the end returns
zeros**, so a file that is not a terrain stream decodes to a plausible *empty square*
rather than failing. Archive 25287's file 0 is three bytes and produced a 32 KB
square of nothing. The codec now re-encodes both halves and compares them against
the bytes they came from, writing a `.jm2` only on an exact match; that square falls
back to `.map` and the other 2,933 are guaranteed exact. **14,702 / 14,702 map files
byte-identical**, against 11,767 before.

**`RSCache_MapLocsEncode`.** The loc stream is two nested runs of *unsigned*
deltas, so entries have to leave sorted by `(loc_id, packed position)` or a delta
cannot be spelled. The encoder sorts rather than demanding it of the caller: a
decoded stream is already in that order, so it is a no-op on a round trip, and it
makes the function usable by something that assembled locs itself. Ties keep caller
order, because the client draws in stream order. **2,933 / 2,933 loc streams
byte-exact, 4,968,456 locs.**

**`RSCache_Dat2ComponentEncodeIf3`**, dispatching IF1 and IF3 on the component's
own flag. IF1 is not vestigial — 2,096 of osrs239's components and 11,086 of
osrs184's. **74,719 / 74,719 byte-exact** across three caches.

Three things it had to get right, and one it caught:

- Several stream bytes are not stored but folded into `clickMask` — type 2's four
  flag bytes, the op-present bits, type 7's flag, button-type 2's 6-bit field.
  Each owns a distinct bit, so they read back out.
- **Two ambiguities do not**, and both took decode-time provenance (the B8 pattern).
  An empty `option` decodes to a default by button type, so an absent option and
  one spelling out `Continue` are the same state afterwards — interface 99 file 30
  carries the word in full in all three caches, which is how the first guess was
  caught, by byte diff rather than by reasoning. And an inventory slot's flag byte
  is dropped with absence marked by graphic id -1, so a present slot whose graphic
  is genuinely -1 lost its eight bytes. `optionFromDefault` and `invSlotPresent[20]`
  now record what the wire said.
- Sizing the encode bound for IF3 alone tripped rsbuffer's write-past-the-end
  assert on an IF1 type-2 widget, which carries 20 inventory slots and five object
  ops no IF3 component has. The bound covers both layouts.

**`RSCache_WorldMapAreaEncode` and `RSCache_WorldMapAreaEncodeIcons`.** Two fields
the area decode reads and never uses (a 32-bit int after `origin`, a byte before
`is_main`) had to become struct fields, or every record re-encoded two zeros where
the cache had values — the B8 pattern again. The compositemap needed one more:
`data0_count`, the index where the first region block ends. The two blocks are
stored back to back and each record repeats its own kind, but the stream is free to
disagree with the split, so re-deriving it from the records would be a guess.
`EncodeIcons` also takes the same `RSCache_WorldMapFlags` the decode used, because
OSRS >= 238 drops the trailing group/file BigSmart pair from every record and
writing it anyway shifts everything after it. **207 / 207 files byte-exact.**

### H9. The world map table is unlabelled, so the codec validates rather than assumes

idx19 is 54 archives and nothing in the cache says which holds what. The obvious
move — hardcode "archive 0 is areas, archive 1 is compositemaps" — is a claim about
one cache dressed up as a decoder.

Instead each record is decoded, immediately re-encoded, and compared against the
bytes it came from. Text is written only on a byte-exact match; anything else
declines to its raw payload. The layout then reported itself: archive 0 is 52 area
records, archive 1 is 52 compositemaps, archive 2 is 52 PNGs, and the remaining 51
single-file archives match neither decoder and stay `.bin`.

The cost is decoding twice on export, which is nothing at 748 KB. What it buys is
that a cache laid out differently produces correct output rather than confident
nonsense, and that a decoder bug shows up as "declined" rather than as a text file
that packs back into different bytes.

One thing this caught that a semantic round trip would not: the area's background
colour is `0xFF000000`, and writing it as `%06X` dropped the alpha byte. The text
re-read as `0x000000`, re-encoded a different colour, and the source fixed point
still held because the loss was symmetric. Only the payload comparison against the
original cache found it.

### H10. Two crashes the round trip found in the library, and the invariant behind both

Packing the tree back aborted twice, in different places, for the same underlying
reason: **a decoded and a raw form shared an extension.**

`interfaces` wrote `.if` both ways and `textures` wrote `.texture` both ways. That
is harmless while every record decodes — and both do — but the moment one declines,
its raw payload lands on the name the decoder also uses. On import the reader
declines the same record, falls back to the raw path, and packs *the text file* into
the cache as though it were the payload. The archive that produces is malformed in a
way nothing downstream expects.

`assert_extensions_distinct()` now checks the register at startup, because this is a
property of a table of literals and should not wait for a cache to expose it. Both
rows were changed to a `bin` fallback. (EXCEPTIONS already recorded this exact bug
for `scripts` during development; it was not generalised at the time.)

What the malformed archives then hit was two real defects in the library:

- **`RSCache_FileListNewFromDecode` freed pointers it never allocated.** `files` was
  `malloc`'d rather than `calloc`'d, and every `goto error` runs
  `RSCache_FileListFree`, which walks the whole array. A group whose size table does
  not parse takes the error path with most of the array still uninitialised. Three
  more locals (`chunk_sizes`, `sizes`, `file_offsets`) were declared *after* labels
  that jump to `error`, so those frees read indeterminate values. All four are now
  declared and nulled up front, the chunk count is checked against the payload size,
  and a negative or absurd file length refuses instead of reaching `malloc`.
- **`cachepack` freed a stack struct.** `RSCache_CS2_Compile` fills a caller-supplied
  `RSCache_ClientScript`, which `script_read` keeps on the stack;
  `RSCache_ClientScriptFree` ends in `free(script)`. The library had no in-place
  release, so `RSCache_ClientScriptFreeInplace` is now the primitive and
  `RSCache_ClientScriptFree` is it plus the `free`.

Neither is reachable from a well-formed cache, which is why neither showed up until
the tool wrote a bad one.

### G6. Opcode 210, and what "established" bought *(Resolved)*

Booting the client on `cache.osrs239` aborted in `CS2VM2_Op_StackMetaStub`: the
rev-239 gameframe scripts call opcode 210, which neither `Opcodes.kt` nor
`Command.kt` lists and `src/cs2vm2` had no stack signature for. The decompiler
refused the script for the same reason.

`cs2 infer-arity` settled it without a reference client: ten call sites across the
cache, every one solving to a six-int pop with nothing pushed, and no other
candidate surviving at any of them. Recorded in both tables — `local_commands.py`
for the decompiler and `gen_opcode_stack.py` for the VM — because the README's own
rule is that there is one answer to "what is opcode N".

What it does is still unknown. The VM pops the six and does nothing, which is
stack-correct and behaviourally a no-op; script 8489 now reads
`_210(2372, $int0, 2373, $int1, 0, 0)`. Three more scripts decompile than before
(5,589 of 9,725 at the time), and the client boots, logs in and renders.

Worth noting what the method *did not* give: the trace made the first argument look
like a component, which would have been a natural thing to write into the signature.
The decompile shows the component belonged to the preceding gosub. The arity is
evidence; the types would have been a guess, so they are plain `INT`.

### G8. The DB family's stack shape is in the data, not the opcode *(Resolved)*

Script 7603 failed with `opcode 9 pc 47: left 1 values on the operand stack`.
Opcode 9 is `branch_less_than`, and it had nothing to do with it — a stack
failure names the op that *noticed*, not the one that lied.

A `dbcolumn` literal packs

    (table << 12) | (column << 4) | (field + 1)

and field 0 means **the whole tuple**. `db_getfield(row, 0xa6200, i)` therefore
pushes four values, of that column's four types, while `db_getfield(row,
0xa6204, i)` — same table, same column, field 3 — pushes one. The generated
table carried three in and one out for both, which is correct for a single-field
column and desynchronises every script that reads a wider one: 80 of them.

The `+1` is the whole point and it was not known. `db_unpack_column` in
`src/game/rs_cs2_host.c` read the low nibble as a plain field index, which makes
"the whole tuple" indistinguishable from "field 0" — so the client had the same
defect, unnoticed because nothing there checks arity. The memory of that work
recorded "dbcolumn packing unverified"; this is the verification.

**Established by consumption, not by reading a client.** Over every call site in
cache.osrs239: `0xa6200` is followed by four int pops at all 26 of its sites,
`0xa6204` by exactly one at all 11, `0x170` by two, `0x90` by three. No fixed
signature serves the first two, which name the same column.

The find family was wrong the other way. `db_find`, `db_find_with_count`,
`db_find_filter` and `db_find_filter_with_count` take **three** arguments, not
two: all 142 call sites push `(dbcolumn, value, 0)` and no script balances
without the third. What the third means is not known; the count is.

Both now have their own command kinds and resolve their shape from the dbtable
config, supplied by the caller through
`RSCache_CS2_DecompileOptions.db_columns` — the same arrangement `param_types`
uses, for the same reason: it is a property of the cache, not of the opcode.
`tools/common/cs2_db_columns.c` is the one provider, shared by `cs2` and
`cachepack`.

**One gap inside the fix.** A dbtable's field types are ScriptVarType
*ordinals*, not the descriptor characters CS2 uses elsewhere — table 78 column 1
is `0 36 36 23`, which as descriptors would be NUL, `$`, `$` and end-of-medium.
Only the string ordinal (36) is established, exhaustively, so the provider
answers `int` or `string` and no finer. That is all the shape depends on. Running
the ordinals through `RSCache_CS2_TypeOfDescAuto` — the obvious thing, and what
the first cut did — returns "no such type" for every one of them, which reads as
"column unknown" and falls straight back to the single int this exists to
remove.

Two smaller shape corrections came out of the same pass:

- **`pop_array_int` is not always an int.** The element type is `define_array`'s
  operand, and an array outlives a `gosub` — so a proc stores into one its
  caller defined and this script holds no definition at all. The store now takes
  whatever stack the value is on, which cannot desynchronise anything (one value
  either way) and recovered 15 scripts storing `join_string` results.
- **A local nothing constrained printed as `?`.** `[clientscript,x](? $int0)` is
  not source. It prints as its bank now.

Against cache.osrs239 the whole of section G's work moves **6,113 -> 9,433**
decompiled of 9,725 (and 9,042 -> 9,433 for a caller that has RuneStar's name
tables; the two are now equal — see G5). `test_cs2`'s `decompiled` figure rose
7,467 -> 7,518; its `identical` figure moved 6,489 -> 6,485 for the reason in
G10, which is era drift rather than a loss of fidelity.

### G9. The compiler was measured by a gate that measured nothing *(Resolved)*

`cs2 roundtrip` is the standing correctness gate, and in `--cache` mode it
compared the compiled bytes against nothing: only `--raw` kept the originals, so
every cache run printed "0 same-length, 0 exact" — a line that looks like a
result. The mode `cachepack` depends on was the blind one.

With it fixed the compiler turned out to be well behind the decompiler: 8,531 of
9,310 sources compiled. That is not cosmetic, because `cachepack pack` drops an
archive whose source will not compile — a repacked osrs239 was missing 65
scripts. Nine causes, all in `tools/README.md`; the largest was a hook argument
written as `calc(...)`, which is arithmetic and therefore `int`, with nothing
saying so.

G3 predicted the hook-descriptor problem would need a whole-directory pre-pass
building script id -> argument types. It did not. The descriptor letter's only
operational job is to name the *stack* an argument came off, and all three of
G3's defeating cases answer at that resolution: `null` is -1 on the int stack, an
ambiguous `coins_995` is an int whichever of the six types claims it, and a
`~proc` call's stack types are readable from the callee's bytecode through
`RSCache_CS2_ScriptReturnTypes`, which already existed.

Now 9,368 of 9,433. The residue is 65 scripts whose *decompiled source* is not
valid CS2 — a statement that is a bare local, an array passed to a proc without
its `$`, and a callback string containing nested quotes. Those are generator
defects rather than parser ones and are not fixed here.

**`cachepack` no longer depends on the two halves agreeing.** The script codec
compiles its own output before accepting it and declines the record if that
fails, writing the raw bytecode instead.

What that buys is *not* a complete cache — `pack --base` copies the base first,
so a declined archive already kept the base cache's bytes and nothing was ever
missing. It buys the tree meaning what it says. A `.cs2` that will not compile
is a file you can edit, pack, and watch ship the original bytes with nothing but
a counter to say so — the same silent-substitution defect `cp_assets.c` calls
out two branches up. After the change every `.cs2` in the tree is one the
compiler accepts, so editing it takes effect; the 357 records that cannot make
that promise are `.cs2b` and are visibly bytecode.

**The promise is conditional on the name tables.** Verification runs at unpack
with whatever `CACHEPACK_CS2_NAMES` pointed at, and a source full of
`coins_995` and `~wom_item_move` does not compile without them: packing the same
tree with the variable unset declines 4,991 records instead of 357. Unpack and
pack the same way, or unpack without names — coverage is identical either way
(G5), only the spellings differ.

### G10. Two opcodes changed arity between eras, and the table has one slot *(Open)*

`mec_category` (6695) and `_6623` return a pair of values in OldSchool 239 and a
single value in the 2021 dump `test_cs2` compares against. Both readings are
right; they are right about different clients.

`src/cs2/cs2_command.gen.h` has one row per opcode and no era dimension, so one
of them has to lose. The 239 reading is installed, because 239 is the revision
this work targets and it is worth 23 scripts there against 4 on the older
corpus. `test_cs2`'s `identical` bar moves 6,489 -> 6,485 to record it, which is
the first time that number has gone *down*.

Both were found the same way: install a candidate with `cs2 decompile
--override`, decompile the scripts that use the opcode, and count. That is also
what says the conflict is real rather than a mistake — each signature strictly
beats the other on its own cache and strictly loses on the other one.

**The fix is not large and is not done here.** `RSCache_CS2_CommandOverride`
already exists so "a caller with era-specific knowledge can supply one without
touching the library", and the profile machinery already knows which revision a
cache is. Threading an era-scoped signature table through that seam would let
both readings coexist and would retire this entry. What is needed first is a
survey: these two are the ones osrs239 and the 2021 corpus happen to disagree
about *and* both exercise, and there is no reason to think they are the only
two.

### H11. Readable asset forms, and what each one costs

Six kinds decode to something editable; the rest stay payload. Five of the six are
a fixed point; scripts are not, and the gap is the CS2 layer's rather than this
tool's — see the measurement below the table.

| kind | form | note |
|---|---|---|
| maps | `.jm2` | terrain + locs, 2,933 of 2,934 squares; the three undecoded per-square files ride alongside as `.extra<N>.bin` |
| interfaces | `.if` | one `[com_<id>]` block per component |
| worldmap/areas | `.wma`, `.wmc` | areas and compositemaps; the ground layer stays PNG |
| textures | `.texture` | text, because an OldSchool texture is a **record**, not a bitmap |
| scripts | `.cs2` | via the existing decompiler; 9,042 of 9,725 with name tables |
| sprites | `.bmp` + `pack.meta` | palette recorded, not re-derived; alpha rides in the 32-bit BMP |

**Scripts are the one readable form that is not a fixed point.** Measured on
`cache.osrs239`: all 683 scripts that decline decompilation round-trip
byte-exactly. Of the 9,042 that decompile, most **re-encode to different bytes** —
the compiler produces valid, equivalent bytecode, not the original bytecode — and
on a second unpack 754 of them stop decompiling and 68 decompile to different
source. **8,220 / 9,042 (90.9%)** source fixed point, exact for every other kind. This is G1/G2
showing up at the tool level; `--raw-assets` trades the readable forms for an exact
tree.

**Declining is a first-class outcome.** A record the codec cannot express writes its
raw payload under a different extension instead — 683 clientscripts take that path
without name tables. Sharing one extension between the decoded and raw forms was a
real bug during development: every script fell back, kept the `.cs2` name, and the
output looked like successful decompilation of garbage. They are now `.cs2` and
`.bin`.

**A jm2 is shared with something that is not this tool.** MAP and LOC are the
cache's sections; a content tree also keeps a square's npc and obj spawns in the same
file, the way LostCity does. So the codec owns two sections and treats every other as
foreign: on export it reads the existing file first and carries the foreign sections
through verbatim, and on import it skips them rather than trying to read
`0 41 42: 3045` as a tile. A file with no MAP or LOC section at all declines, because
encoding it would put a blank square into the cache — which reads as a hole in the
world rather than as the absence it is.

One bug worth recording because the symptom pointed nowhere near the cause: passing
the param config's type *character* to `RSCache_CS2_NamesSetParamType` instead of
converting it through `RSCache_CS2_TypeOfDescAuto` mistypes every param, and the
result is not a wrong name — it is a stack-shape mismatch that fails the decompile
of every script touching a param. All 9,725 declined until that was fixed.

### H12. The dat1 rows joined the opcode-codec registry, and measuring them was the point

`opcode_codec.c` held twenty rows, all dat2. Six dat1 types — obj, idk, spotanim,
npc, seq, component — now have rows too, so the registry answers for both epochs.
Nothing about the dat2 side moved: every fidelity count in `make test` is
byte-for-byte what it was, and the only line that changed is `20 codecs
registered` becoming `26`.

**The one hazard in the work, and it is not a small one.** Five of the six decoders
seeded their defaults *at the top of their own decode loop*. Lifting the loop out to
the shared driver leaves those behind, and `WRAP_INIT_ZERO` is exactly the wrong
answer for all of them: dat1 npc alone has nineteen non-zero defaults — `size` 1,
`resizeh`/`resizev` 128, `turnspeed` 32, five anim ids at -1 — and **every one of
those values is also a legitimate field value**, so the record decodes with no error
and nothing about it looks wrong. What breaks is downstream: the encoders write an
opcode only where a field *differs from its default*, so a zeroing init silently
changes which opcodes are emitted. That is the mechanism by which dat2 npc dropped
from 99 byte-exact records to 0 when its `record_finish` hook was missing (see the
`record_finish` note in `opcode_codec.h`), and it has no other symptom. Each type's
defaults are now a named `Init` shared by the registry and the type's own entry
point, so there is one definition rather than two that agree today.

The same reasoning covers the *order* of the stream. dat1 config records are not in
ascending opcode order — cache254's first idk runs 1, 60, 2 — so obj, idk and
spotanim record the order they decoded and replay it, the device
`RSCache_Dat2ConfigHitsplat` uses. That bookkeeping moved into `decode_op` rather
than staying in the loop, so the entry point and the registry cannot disagree about
it, and an opcode the handler declines is now never recorded (replaying one the
encoder cannot write fails the whole record).

**component takes the whole-record `decode` override, not `decode_op`.** It is not
an opcode stream: a fixed header, then sections chosen by `type` and `buttonType`,
with no code byte to dispatch on and no zero terminator — the record ends where its
last conditional section ends. It is also the only dat1 row with no encoder, and
`RSCache_OpcodeCodecCanEncode` answers false for it rather than the row pretending
otherwise.

**The measurement, which is new — these encoders had never been held to a bar.**
`test_dat1_encode` covered idk, obj and spotanim; seq, npc and component were
unmeasured. Against `cache254`, driven through the registry:

| type | records | exact | differ | same-len | consumption |
|---|---|---|---|---|---|
| idk | 82 | **82 (100%)** | 0 | 0 | exact |
| spotanim | 270 | **270 (100%)** | 0 | 0 | exact |
| obj | 2978 | 2973 (99.8%) | 5 | 3 | exact |
| seq | 1103 | 535 (48.5%) | 568 | 566 | exact |
| npc | 1055 | **0** | 1055 | 954 | exact |
| component | 8140 | — (no encoder) | — | — | exact |

**Every record of every type consumes to its last byte**, which is the bar that
matters most here: dat1 configs are one concatenated stream, so a single misjudged
width would shift every later record and the walk could not land on the archive's
end. component's 8,140 records are the largest instance of that check in the suite.

npc's `0 exact` is not loss of 1,055 records, and the `same-len` column is what says
so — B3's argument, applied to a new type. 954 of the 1,055 re-encode to the
**identical length**, i.e. the source stated its opcodes in an order the ascending
encoder does not reproduce. The 101 that are shorter are two already-documented
classes, both confirmed by inspection rather than assumed:

- **A repeated opcode.** npc 6 carries opcode 31 (`"Attack"`) *twice*; the struct
  holds one string per slot, so the second overwrites the first and the encoder
  writes it once — 8 bytes short. Same property `run_obj` already allows for in
  obj's 5 differing records.
- **A literal `"hidden"` action**, which the decoder normalises to NULL and the
  encoder therefore omits. 43 of the 101. This is B2's `obj` row reappearing on
  dat1 npc.

Both are decoder-side losses that predate this work and neither is introduced by
the registry; recording them is the whole reason the numbers are worth taking.

---

## F. Not ours

`src/engine/uitree_builder/task_interface_open.c` shows as modified in the working
tree and was not touched by this work.

---

## Current state

`make -C 3rd/rscache test` → 3035 checks plus 9 bzip2-interop checks.

| Suite | Checks |
|---|---|
| rsbuffer | 1816 |
| container | 505 |
| roundtrip | 246 |
| profile | 121 |
| sound | 120 |
| compression | 99 |
| dat1_write | 46 |
| dat1_encode | 39 |
| config_var | 37 |
| cache_edit | 22 |
| font_metrics | 20 |
| cs2 | 3 |
| bzip2 interop | 9 |

`test_cs2`'s three checks are worth more than the count suggests: each one is a
threshold over 7,884 scripts decompiled and compared against another
implementation's output (G1). It skips when the reference checkout is absent —
set `CS2_REFERENCE`, or clone it beside this repository.

Client builds; `test-db`, `test-net-login`, `test-world-builder`,
`test-uitree-builder-dat1` and `test-ui-slots` pass; both offline boots report
unchanged asset counts (dat1 219 locs / 222 models, dat2 430 locs / 399 models).

**Encoders: 27 — every datatype this library decodes.** The 19 config types (struct,
enum, param, idk, spotanim, obj, underlay, overlay, texture, mapelement, npc, loc,
sequence, varbit, varplayer, varclient, inv, healthbar, hitsplat) plus framemap,
sprites, map terrain, frame, clientscript, model and **sound effects** — the last of
these being the only one whose byte-exactness is *asserted* at 100% rather than
reported (B20).

Semantic round-trip is asserted at 100% on all twenty-six, across every dat2 cache
that carries the datatype.

Byte-exactness, which is reported rather than asserted (see E):

| Band | Datatypes |
|---|---|
| 100% | **sound effects (asserted, every cache)**, varbit, varplayer, varclient, inv, healthbar, hitsplat, model, frame, clientscript, framemap (old caches), struct |
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
