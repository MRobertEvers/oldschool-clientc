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

`make -C 3rd/rscache test` → 1093 checks plus 9 bzip2-interop checks.

| Suite | Checks |
|---|---|
| rsbuffer | 267 |
| container | 469 |
| profile | 111 |
| roundtrip | 147 |
| compression | 99 |
| bzip2 interop | 9 |

Client builds; `test-db`, `test-net-login`, `test-world-builder`,
`test-uitree-builder-dat1` and `test-ui-slots` pass; both offline boots report
unchanged asset counts (dat1 219 locs / 222 models, dat2 430 locs / 399 models).

Encoders done (12): struct, enum, param, idk, spotanim, obj, underlay, overlay,
texture, mapelement, npc, loc. Semantic round-trip is 100% on all twelve across all
six dat2 caches.

Remaining: sequence, then the binary types (model, frame, framemap, sprites, maps,
clientscript), the six missing decoders (varbit, varplayer, varclient,
varclient_string, inv, hitsplat, healthbar), and engine wiring.
