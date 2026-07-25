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

### B12. `cache.643` ob3 models do not decode *(Gap)*

Revision 643 is RS2-branch, not OSRS, and its models do not fit the ob3 layout this
library implements: re-deriving the section offsets independently, **1795 of 3000
sampled models lay out past the end of the file**, and 355 use a face index type 0 that
appears in no OSRS cache. Byte-exactness of 85.9% is the honest signal here; the 100%
*semantic* figure is not evidence of anything, because both decodes are wrong in the
same way and therefore agree.

`cache.643` is deliberately **not** in the round-trip harness's cache list, so this
does not show up as a passing test. Nothing in the client boots a 643 cache today. The
work needed is a fifth format (or a 643-gated variant of ob3), which belongs with the
decoder.

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
  `CacheProvider_SetProfile`. It defaults to `RSCache_ProfileZero()` — "OSRS dat2,
  revision unknown" — so a provider whose profile was never set decodes exactly as it
  did before profiles existed.
- `app_provider_set_cache_profile` resolves it from the manifest's `client_version` and
  `kind` through `RSCache_ProfileForContainerRevision`, which handles the exact match,
  the nearest-lower fallback, and the revision-unset case itself. The boot log states
  what it picked: `container=dat1 epoch=0 revision=254` and
  `container=dat2 epoch=1 revision=230`.
- Converted: the dat1 loc decode (was a literal `RSCACHE_CONFIG_LOC_DECODE_DAT`) and
  the dat2 loc decode (was `FlagsForRevision(archive->revision)`), the latter via a new
  `RSCache_Dat2ConfigLocNewDecodeProfile`. Both are equivalent on the caches the client
  boots, and both now additionally carry the container and the Kronos quirk, neither of
  which a revision number can imply.
- Wiring this up is what exposed **D16** — the cross-lineage revision comparison that
  would have added `OSRS_220` to every dat1 loc decode.

**Left deliberately:** `spotanim` and `component` still take a bare revision.
`RSCache_Dat2ConfigSpotanimNewDecode` `(void)`s its `revision` parameter outright, so
converting those three call sites would change no behaviour at all; `component` already
resolves its era through `RSCache_Dat2ComponentDecodeRev`, which works. Both are
cosmetic, and skipped as such rather than missed.

### B15. Varbits now load at boot; the dat1 half does not *(Gap)*

`CreateTask_Dat2VarbitLoad` loads config group 14 at boot and installs the table into
`VarPManager` — **17,605 types from 17,426 records** on `cache.osrs230`, ids 0..17604
with the holes zeroed to `basevar = -1`. It runs before anything that can execute a
script, because a varbit read happens deep inside CS2 with nowhere to yield to a load.
That closes the B13 consequence: `GetVarbit` returns real values now, so a script
branching on a varbit takes the branch the server intended.

Fixing the loader also required **D17** — both the reader and the writer masked one bit
too narrow, which alone would have made every single-bit varbit read 0 even with the
table loaded.

**dat1 is not wired.** Its varbits live in the config jagfile as a single `varbit.dat`
blob, and `VarPManager_LoadVarbitDat` already parses that exact shape — it just needs
calling from the dat1 config load. Not done because the dat1 client is CS1, whose
varbit opcode path is a different host request, so the payoff needs checking before the
plumbing. The task is gated `cache_kind != APP_CACHE_DAT1` so the dat1 boot is
untouched.

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
| D16 | **`RSCache_RevisionAtLeast` compared revisions across lineages.** dat1 revisions run to 254 in a 2004-era sequence; OldSchool restarted from 1 and is now in the 230s. With no epoch guard, a dat1 rev-254 profile satisfied **every** threshold in the library — 210, 220, 226, 237 — each one switching a decoder to a field layout that postdates the cache by nearly twenty years. Renamed to `RSCache_RevisionAtLeastOsrs` so the lineage is unmissable at the call site, and step 1 now requires an OSRS epoch. | Latent until Phase 7: nothing passed a real profile to a `Flags` function, so the comparison never ran on a dat1 cache. Found by hand-evaluating `RSCache_Dat2ConfigLocFlags` for the rev-254 profile *before* wiring it up — it would have added `OSRS_220` to every dat1 loc decode. Now pinned by a test that asserts a dat1 profile clears all four thresholds and that its loc flags are exactly `RSCACHE_CONFIG_LOC_DECODE_DAT`. |
| D17 | **`VarPManager_GetVarbit` and `SetVarbitOptimistic` masked one bit too narrow.** `endbit` is inclusive, so the width is `endbit - startbit + 1`; both functions used the difference alone. Every varbit read one bit short, and a **single-bit** varbit — `startbit == endbit`, the commonest shape — hit the `bit_count <= 0` guard and returned 0 for every possible varp value. The reference masks with `(1 << (msb - lsb + 1)) - 1`. | The cache settles the inclusive question: varbits 0..4 of `cache.osrs230` are `start=end=0,1,2,3,4` — five consecutive one-bit flags in varp 318, which would all be zero-width under an exclusive reading. Invisible because `varbit_count` was always 0, and because **the existing test could not discriminate**: it read `var[0] = 0x0E` and expected 7, which holds at both 3 and 4 bits wide since bit 4 is clear. Rewritten to use `0x1E`, where the two readings differ (15 vs 7), plus a single-bit case and a write-then-read check. |
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
