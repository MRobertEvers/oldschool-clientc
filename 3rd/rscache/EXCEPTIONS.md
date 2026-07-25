# rscache — exceptions register

Every place the read/write work departed from the plan, stopped short of complete,
or accepted a shortfall — with the reason, so none of it has to be re-derived or
re-litigated later.

Scope: the cache read/write expansion (revision-explicit structure, encoders,
container writers, README). Phases 1–4 and 8 are complete; Phase 5 is partway.

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

### B8. `frame` cannot be encoded until its decode stops synthesizing *(Gap)*

Every encoder inverts a *parse*. `frame`'s decode is not a parse — it **synthesizes
entries that were never in the stream** — so an encoder would persist invented data as
if it had been authored.

`frame_new_decode` runs two cursors (a flag stream and a data stream) and *inserts*
entries: for a bone whose type is non-zero it walks backwards and adds a
zero-translation entry for the nearest preceding type-0 bone. It also drops bones
whose flag byte is `<= 0`, so absent and zero-flagged are indistinguishable. The
output arrays interleave real and synthesized entries with nothing marking which is
which.

Inverting that means reconstructing the flag stream *and* knowing which entries were
invented. The synthesized ones are inferable (all-zero translation on a type-0 bone
immediately preceding a non-zero one) but the inference is fragile, and getting it
subtly wrong yields animation data that decodes to different bone transforms — a
failure that looks like a rendering glitch rather than a cache error.

**The fix is the same one applied to maps** (see below): record provenance during
decode — a per-entry `synthesized` flag, plus the raw flag bytes — rather than trying
to recover it afterwards. Not yet done.

### B8b. `maps` terrain — resolved by recording provenance

Previously listed here as unencodable for the same reason. `fixup_terrain` rewrites
`height` for **every** tile — procedurally from Perlin noise where the file gave none,
scaled by the tile-height basis where it did — so nothing writable survives in that
field, and "absent" and "authored" become the same state.

Fixed rather than worked around, by recording provenance at decode time:

- `RSCache_MapFloor.height_authored` — opcode 1 was present.
- `RSCache_MapFloor.authored_height` — the raw wire byte. Needed *as well as* the
  flag, because the fixup overwrites `height` when it is 0, so a tile that genuinely
  specified height 0 would otherwise still lose its value. That case is covered
  explicitly in the test.
- `RSCACHE_MAP_TERRAIN_DECODE_NO_FIXUP` — opt-in raw decode, for callers that want the
  stored values untouched.

Both fields are set whether or not the fixup runs, so **one encoder handles a raw
stream and a fixed-up one identically** — the flag is not required for encoding.

Client behaviour is unchanged: the fixup still runs by default, nothing sets
NO_FIXUP, and `MapFloor` was 9 bytes padded to 10 so the two new bytes cost no memory.
One related correction: the flags word was tested with `==` against
`RSCACHE_MAP_TERRAIN_DECODE_U16` (zero), which would have silently selected the wrong
width the moment any other bit was added; it is now a proper `&` test.

`shape` and `rotation` need no reconstruction — `attr_opcode` stores the original
overlay opcode verbatim and both derive from it.

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

---

## F. Not ours

`src/engine/uitree_builder/task_interface_open.c` shows as modified in the working
tree and was not touched by this work.

---

## Current state

`make -C 3rd/rscache test` → 1171 checks plus 9 bzip2-interop checks.

| Suite | Checks |
|---|---|
| rsbuffer | 267 |
| container | 505 |
| profile | 111 |
| roundtrip | 189 |
| compression | 99 |
| bzip2 interop | 9 |

Client builds; `test-db`, `test-net-login`, `test-world-builder`,
`test-uitree-builder-dat1` and `test-ui-slots` pass; both offline boots report
unchanged asset counts (dat1 219 locs / 222 models, dat2 430 locs / 399 models).

Encoders done (15). **Every config datatype**: struct, enum, param, idk, spotanim,
obj, underlay, overlay, texture, mapelement, npc, loc, sequence. Plus two binary
types: framemap and sprites. Semantic round-trip is 100% on all fifteen across all
six dat2 caches.

The round-trip harness now scans both traversals — config groups (many records per
archive) and whole-archive tables (one record per archive, capped at 2000 archives
per table with the cap printed).

Encoders done (16): the 13 config types, plus framemap, sprites and map terrain.

Remaining binary types:
  - **model** — four formats behind a magic trailer, ~2300 lines of decoder. Tractable
    but large; its own increment.
  - **clientscript** — cs2 bytecode; not yet examined.
  - **frame** — blocked on B8: needs provenance recorded at decode, the same treatment
    map terrain just received.

Then the six missing decoders (varbit, varplayer, varclient, varclient_string, inv,
hitsplat, healthbar) and engine wiring.
