

> **Binding corrections for every design below:** an OSRS239 NPC_INFO v5 add carries a 16-bit
> per-client NPC index (`0xffff` terminator), then a 14-bit initial NPC definition. For definition
> ids 16384..65535, its extended/update flag and update-mask `0x1` replace that definition in the
> same packet with a transformed unsigned 16-bit `p2Alt3` / `UShortLEAdd` value. Thus all
> id-ceiling, free-run, roster-tier, or allocation-budget reasoning based on the direct 14-bit
> field is superseded. Rev-727 CS2 is likewise not osrs239 source: preserve raw
> instruction/operand plus stack-effect disassembly, decompile with an explicit 727 dialect, and
> translate only accepted logic into newly authored osrs239 CS2. The plan and red-team document
> remain authoritative over these historical designs. Loc ids are separate again:
> `LOC_ADD_CHANGE_V2` carries the loc config as an exact 16-bit `p2Alt3`, so historical loc 70000
> examples are invalid for runtime placement; the obelisk uses target loc 62201.

===== DESIGN: design-asset-pipeline =====
# 530 → 239 asset import pipeline — design

Everything below is measured against the two trees unless marked GUESS or UNVERIFIED. Recon claims I re-measured and that changed are called out.

---

## 0. Three measurements that set the shape of the design

| Measurement | Result | Consequence |
|---|---|---|
| Model trailer census, `OSRS-Content/osrs239-content/models/*.model` (61,615 files) | **34,625 `FF FD` (V3) + 26,990 `FF FE` (V2). Zero OB2, zero OB3.** | 530's 39,694 OB3 + 5,778 OB2 are a **disjoint** format set. Models cannot be byte-copied; they must be re-encoded. |
| `grep -c "/" pack/7_models.pack` | **53,421 of 61,615 names already contain a `/`** (`0=npc/royal_dwarf_citizen1_head`) | A "distinct, clearly-marked ported folder" for assets needs **zero tool changes**. `import_one` (`cp_assets.c:1407`) does `snprintf(base,"%s/%s",root,name)`. |
| `find_named --model` on familiar models 30443 / 31211 / 30435 / 31168 / 30469 | **every one carries 3–21 texture triangles and a `textures` array** | Material-id remapping is on the critical path for *every* familiar model, not a tail case. This is the pipeline's biggest risk (§8). |

And one correction to recon: `cp_name_find` (`cp_names.c:1121-1126`) resolves a name against `configs/all.<ns>.compack` **and then `pack/<ns>.alloc`**. So new config ids never need to touch the machine-owned `configs/` tree that `test-server-clean` guards. That single fact makes the id story tractable (§4).

---

## 1. Where the tool lives, and why

**A new `cachepack import` subcommand in `3rd/rscache/tools/cachepack/`, written in C**, plus a revision profile at `3rd/rscache/src/revisions/rev_dat2_rs530.c`. Not a standalone tool. Not Python.

### Why cachepack and not a new tool

cachepack already owns *both* halves of the destination format, and a new tool would have to re-implement them:

- **Config text emitters** — `tools/cachepack/config/cp_{npc,obj,seq,loc,small,flo,idk,var,db}.c`. `cp_unpack_npc(ctx, id, record, size, out)` (`cp_npc.c:135`) decodes with **`ctx->profile`** and emits the tree's `[name] key=value` block. Point `ctx->profile` at 530 and the same function emits a 530 record as tree text. That is the entire config import, already written.
- **Asset codecs** — `cp_assets.c:84-165` `g_assets[]`, plus the sprite/interface/texture/`.cs2` codecs in `cp_decode.c`. Sprites already export to `<name>/N.bmp` + `pack.meta` and import back.
- **The id/name layer** — `lc_pack` for `pack/*.pack`, `ctx->names.alloc[]` for `pack/*.alloc`, `cp_membership.c` for `.client`/`.server`. Id allocation and recording are one call each.
- **The reference-table writer** — `cp_reference_sync` + `cp_reference_set_name` (`cp_assets.c:1345-1352`), which is what makes a *new* archive id reachable at all.

### Why C and not Python

Repo precedent is unambiguous and split by job:

- **C, in `3rd/rscache/tools/`** for anything touching cache bytes: `port_npc/` (804 LOC), `port_lostcity/` (5,556 LOC), `common/{cache_write,port_plan,transcode,anim_affinity}.c`, `find_named`, `anim_compare`, `audioprobe`, `cs2`.
- **Python, in `tools/`** only for *text ledger diffing* — every `port_*.py` reads two already-unpacked trees and writes a `.map` with a `--check` mode wired into `make -C src test-port`. None of them opens a `.dat2`.

The decoders, the codec ladder, the encoders and the profile system are all C library surface (`3rd/rscache/src/`). A Python importer would be a second decoder for four formats, which is the "two authorities agreeing by hand" failure the register was built to remove (`content.ini:5-8`).

### File layout

```
3rd/rscache/src/revisions/rev_dat2_rs530.c          NEW  ~70 LOC   profile
3rd/rscache/src/revisions/revisions.c               EDIT +2 rows   registry
3rd/rscache/src/datatypes/dat2_config_sequence.c    EDIT ~120 LOC  SEQUENCE_RS2_530 codec
3rd/rscache/src/datatypes/dat2_config_obj.c         EDIT ~60 LOC   OBJ_RS2_530 opcodes
3rd/rscache/src/datatypes/dat2_framemap.{c,h}       EDIT ~40 LOC   EncodeCodec (bug fix, §3.3)
3rd/rscache/tools/cachepack/cp_common.c             EDIT ~80 LOC   sharded-config reader
3rd/rscache/tools/cachepack/cp_import.{c,h}         NEW  ~1600 LOC the subcommand
3rd/rscache/tools/cachepack/main.c                  EDIT ~60 LOC   dispatch + usage
3rd/rscache/tools/anim_compare/main.c               EDIT ~200 LOC  --b-cache (verification)
tools/port_scape2009_ids.py                         NEW  ~300 LOC  port/ ledger + --check
```

### The manifest

`cachepack import` is manifest-driven, copying `port_lostcity`'s grammar verbatim (`3rd/rscache/tools/port_lostcity/dragon_claws.ini` is the template):

```ini
[import:scape2009]
from_rev   = rs530
from_cache = /Users/…/2009scape/Server/data/cache
to_rev     = osrs239
to_tree    = OSRS-Content/osrs239-content
lane       = ported/scape2009_summoning     ; the marked folder, used for BOTH
                                            ; asset paths and the configs dir
configs    = server/scripts/ported_scape2009_summoning/configs
ledger     = port/scape2009_530.map

[export:npc]
6829 = summ_spirit_wolf
6830 = summ_spirit_wolf_combat
…
[export:obj]
12047 = summ_spirit_wolf_pouch
…
[export:loc]
28716 = summ_obelisk
[texture_map]
; 530 material id = osrs239 material id   (hand-built; see §8)
34 = 17
[label_map]
; framemap-local joint renumbering, only if a rig retarget is needed
```

Closure (models, chatheads, seqs, frames, framemaps, spotanims, sounds, sprites) is **computed**, not listed — `tools/common/port_plan.c` already builds exactly that closure for an npc and `tool_neutral_npc_from_dat2` already flattens BasType into anim slots (`port_plan.c:184-201`).

`--dry-run` is the default (port_npc's convention, `port_npc/main.c:5`); `--apply` writes.

---

## 2. The 530 revision profile

```c
/* 3rd/rscache/src/revisions/rev_dat2_rs530.c */
struct RSCache RSCache_ProfileDat2Rs530(void)
{
    struct RSCache cache = RSCache_ProfileZero();
    cache.game     = RSCACHE_GAME_RS2;
    cache.epoch    = RSCACHE_EPOCH_DAT2;
    cache.revision = 530;

    cache.codec[RSCACHE_TYPE_LOC]      = RSCACHE_CODEC_LOC_RS2;
    cache.codec[RSCACHE_TYPE_OVERLAY]  = RSCACHE_CODEC_FLO_RS2;
    cache.codec[RSCACHE_TYPE_UNDERLAY] = RSCACHE_CODEC_FLO_RS2;
    cache.codec[RSCACHE_TYPE_FRAMEMAP] = RSCACHE_CODEC_FRAMEMAP_V3;   /* 530 is the threshold */
    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_RS2_530;  /* NEW */
    cache.codec[RSCACHE_TYPE_OBJ]      = RSCACHE_CODEC_OBJ_RS2_530;       /* NEW */
    /* FRAME: deliberately NOT pinned. Auto derives V1 (threshold 610,
       dat2_frame.c:19). Pinning FRAME_V2 the way rs643 does is what makes
       `--rev rs643` corrupt every 530 animation frame. */
    /* MODEL: not pinned. The format is stamped in the file trailer and
       model.c:1976-1993 sniffs it; the profile only answers "what would this
       cache *write*", which import never asks. */
    return cache;
}
```

Two rows in `revisions.c:22` (`"530"`, `"rs530"`).

Why each pin, verified:

- **FRAME** — `dat2_frame.c:14-24` derives V1 below RS2 rev 610. `rev_dat2_rs643.c:50` pins V2. Probing with `--rev rs643` therefore reads a leading unused byte that isn't there and shifts the whole transform stream. **Everything the recon produced with `--rev rs643` against 530 frames/framemaps is suspect**, including the `port_npc` plan that reported `framemaps: 54090 -> 54090` (idx1 only has 2,435 groups; 54090 is garbage).
- **FRAMEMAP** — `dat2_framemap.c:16-25`, the V3 threshold is exactly `>= 530`. 530 sits on the boundary; pin it so the boundary is a declaration rather than an off-by-one waiting to happen.
- **SEQUENCE** — `dat2_config_sequence.c:1139-1155` calls `RSCache_RevisionAtLeastOsrs(..., default_when_unknown=true)`. An `rs2` profile can never satisfy an OSRS threshold, so both tests take the default and **every RS2 cache falls through to V3**. This is a pre-existing bug on the whole RS2 branch (recon measured 649 desync errors on 530, 2,977 on `cache.void634`, 3,139 on `cache.rs727_preeoc`, 0 on osrs239). Pinning 530 fixes 530 without touching 634/727 — do it that way, and **A/B 634/727 before and after** if you ever generalise the fix, per the `pristine-baseline-skips` memory note.
- **OBJ** — see §3.7.

**COMPONENT is deliberately not pinned**, because we are not importing interfaces (§3.9).

---

## 3. Per asset kind

Legend: **COPY** = raw bytes move, **TRANSCODE** = decode → neutral struct → re-encode, **REAUTHOR** = the formats are not related closely enough; write it fresh in the 239 vocabulary.

### 3.1 Models — TRANSCODE, format-normalising

| | 530 | osrs239 |
|---|---|---|
| Formats present | 39,694 OB3 (`FF FF`) + 5,778 OB2 (no magic) | 34,625 V3 (`FF FD`) + 26,990 V2 (`FF FE`) |

Path: `RSCache_ModelNewDecodeProvenance(bytes, size, &prov)` (trailer-sniffed, `model.c:1976-2003`) → `struct RSCache_Model` + `struct RSCache_ModelProvenance` → `RSCache_ModelEncodeFormat(model, prov, target, out, cap)` (`model.h:350`).

Target mapping, chosen so the section order never changes:

- **OB3 → V3.** `model.h:174-179`: V3 is "OB3's section order plus animaya". `RSCache_ModelEncodeFormat` writes `prov->tail` first then `faceZOffsets` for V3 (`model.c:2729-2745`), which is the OB3 complex/cube payload landing where V3 expects it. Animaya sections are emitted empty.
- **OB2 → V2.** V2 is "OB2's section order plus animaya" (`model.h:176`).

Gotcha: `RSCache_ModelEncode` (no `Format`) **prefers `provenance->format`** (`model.c:2891-2896`), i.e. it would faithfully re-emit OB3 bytes. Import must call `RSCache_ModelEncodeFormat` with an explicit target.

**UNVERIFIED and worth measuring before committing to normalisation:** whether the rev-239 OSRS/RuneLite client's `ModelData` decoder still accepts OB3 (`FF FF`). 3draster's own client certainly does — it goes through the same trailer-sniffing `RSCache_ModelNewDecodeProvenance` (`src/engine/torirs_model_from_rscache.c:397`). If RuneLite 239 also accepts it, OB3 becomes a **byte COPY** and the model risk collapses. The check is a 20-minute experiment: put one OB3 model at a free id in `cache.osrs239.baked`, point a loc at it, boot `./run-osrs239.sh`. **Do this in Phase 0.**

**Texture ids inside faces are the real problem, not the container.** See §8.

### 3.2 Frames (idx0) — COPY + 2-byte patch

Both 530 and osrs239 are `FRAME_V1` (threshold 610). `cache_write.c:610-650` already implements this correctly: copy the file, rewrite the framemap id at offset 0 (offset 1 under V2). The `same_frame_codec && old_fm == new_fm` fast path is a straight `Dat2EditPutFile`.

The unit is the **archive** (a frame group holds hundreds of frames), and `seq->frame_ids[i] = (archive << 16) | file`. Import must remap the archive id and rewrite every referring seq — `cache_write.c:684-700` does exactly this.

### 3.3 Framemaps (idx1) — TRANSCODE V3 → V1, **and there is a real bug to fix first**

530 = `FRAMEMAP_V3` (types, then `transform_actor` u8×n, then `masks` u16×n, then group lengths, then group bytes, then tail). osrs239 = `FRAMEMAP_V1` (types, group lengths, group bytes).

**Confirmed latent bug.** `RSCache_Dat2FramemapEncode(def, out, cap)` (`dat2_framemap.c:212-260`) takes **no codec version** — it emits `transform_actor`, `masks` and `tail` whenever the struct's `has_transform_actor` / `has_masks` / `tail_size` are set, and the decoder (`dat2_framemap.c:146-160`) sets them. The cross-codec branch in `tools/common/cache_write.c:553-580` decodes with the source profile and re-encodes without clearing any of them:

```c
struct RSCache_Dat2Framemap* fm = RSCache_Dat2FramemapNewDecodeProfile(&src->profile, …);
fm->id = dst_id;
uint32_t n = RSCache_Dat2FramemapEncode(fm, enc, bound);   /* ← still V3 */
```

So `port_npc`'s V3→V1 path is a **no-op that ships V3 bytes into a V1 cache**, and nothing errors. The client then reads `transform_actor` as group lengths, every bone group comes out empty, and the familiar animates but does not move — the exact symptom `rev_dat2_rs643.c:44-47` documents in the opposite direction.

The asymmetry is the tell: frames already have `RSCache_Dat2FrameEncodeCodec(frame, codec, fm, out, cap)` and framemaps do not.

**Fix (library, not tool):**

```c
/* dat2_framemap.h */
uint32_t RSCache_Dat2FramemapEncodeCodec(const struct RSCache_Dat2Framemap* def,
                                         int codec_version,
                                         uint8_t* out, uint32_t out_capacity);
uint32_t RSCache_Dat2FramemapEncodeBoundCodec(const struct RSCache_Dat2Framemap* def,
                                              int codec_version);
```

emitting `transform_actor` only at `>= V2`, `masks` only at `>= V3`, and the tail only when the target is the same family. Keep the old two-arg form as a wrapper that passes "whatever the flags say", so `test_roundtrip.c` stays green, and point `cache_write.c:553-580` at the new one. Add a downgrade case to `3rd/rscache/test/test_roundtrip.c`.

**Measure whether the downgrade is lossy in fact.** For the ~80 familiar rigs, count non-zero `transform_actor` bytes and non-zero `masks`. If they are all zero (plausible: `transform_actor` selects which actor a transform applies to, and a familiar is a single actor), V3→V1 is lossless for this content and can be asserted as such. If they are not, we have a genuine data loss that no verification tier will catch except T2 render-compare. **This is a Phase-0 measurement, ~30 lines of throwaway C against `find_named`'s framemap dump.**

### 3.4 Sequences (idx20) — TRANSCODE, needs a NEW codec

530's layout (`2009scape/.../cache/def/impl/AnimationDefinition.java:108-180`) vs rscache's `decode_sequence_v1`:

| opcode | 530 | rscache V1 | verdict |
|---|---|---|---|
| 1 | u16 count, durations u16×n, frame-lo u16×n, frame-hi u16×n | identical | ✅ |
| 2–12 | identical | identical | ✅ |
| **13** | **u16 count**; per entry `u8 n`, and if `n>0`: medium + `(n-1)×u16` | **u8 count**, then `decode_frame_sound_v1` per entry | ❌ **different** |
| **14** | **bare flag**, no payload | `anim_maya_id = g4()` | ❌ **different** |
| 15+ | absent (2009scape's decoder silently `continue`s) | 15/16/17/18/19 present | 530 stops at 14 |

Two mismatched opcodes is enough to desync the whole record. Add `RSCACHE_CODEC_SEQUENCE_RS2_530` with its own `decode_sequence_rs2_530()` next to the three existing ones in `dat2_config_sequence.c`, dispatched from `decode_sequence_dispatch` (`:1119-1136`).

Encode target is osrs239 = `SEQUENCE_V3`. The 530 frame-sound shape has no V3 equivalent, so **drop frame sounds on import** and re-author them as osrs239 seq opcode 13/15 rows if any familiar actually needs them. That is a declared, one-line loss, not a silent one.

### 3.5 Spotanims (idx21) — TRANSCODE, straight

Same RS2 config codec both sides; the record is `{model, seq, resize_x/y, angle, ambient, contrast, recol/retex}`. Path is `RSCache_Dat2ConfigSpotanimNewDecodeProfile` (530) → `cp_unpack_spotanim` text → rank-1 `.spotanim` overlay. Depends entirely on §3.1 (its model) and §3.4 (its seq) landing first.

Gate: exact-consumption sweep over the 80 spotanim ids the recon listed (all ≥ 1295) before trusting a single one.

### 3.6 Sprites (idx8) — TRANSCODE via BMP, cachepack owns both ends

`cachepack unpack --rev rs530 --assets=sprites` writes `sprites/<name>/N.bmp` + `pack.meta` (`count=`, `palette=`, `pN=0xRRGGBB`, `spriteN=w,h,x,y,…`). `cachepack pack --assets` reads it back. The sprite container did not change between RS2 and OSRS.

Two traps, both already documented in `EXCEPTIONS.md`:
- **B3b** — decode with `RSCACHE_SPRITELOAD_FLAG_NONE`. `NORMALIZE` rewrites in place and a repack ships full-size unoffset sprites.
- `cp_decode.c:2475-2497` — the palette in `pack.meta` is written and read back, never re-derived. A colour outside it snaps to the nearest entry. Ported sprites must carry their own palette.

### 3.7 Objs / NPCs / Locs (idx19/18/16) — TRANSCODE via the existing text emitters

Decode with the 530 profile, emit with `cp_unpack_{obj,npc,loc}` into rank-1 `.obj`/`.npc`/`.loc` files under the ported configs dir. `cp_pack.c:1669` already ranks `server/scripts/**` as rank 1, so those overlays merge and pack with no walker change.

**Obj needs a new codec.** 530 carries opcodes rscache's `CODEC_OBJ_DEFAULT` has no case for — `96` (itemType u8), `121`/`122` (lend/lendTemplate u16), `125`/`126` (3 bytes each), `127`–`130` (byte + u16 each) — and reads `23`/`25` as a bare u16 with **no trailing offset byte** (`ItemDefinition.java:337-347`, the `buffer.get()` commented out). Meanwhile the default codec has cases 13/14/15, 44-54, 139/140/148/149/160, 200-202 that do not exist at 530 and will desync mid-record. Recon measured **9,178 of 14,654 objs decoding exactly, 5,476 short**. Add `RSCACHE_CODEC_OBJ_RS2_530`. (All 84 summoning pouch objs happen to decode exactly today, but a crawl over ingredients, drop tables or noted templates does not.)

**NPC needs nothing** — recon measured 8,590/8,590 exact with the base RS2 codec. Confirmed by hand: `find_named --rev rs643 --npc 6829` returns a clean record.

**The BasType flattening is the important npc transform.** 530 npcs carry `bas_type_id` with every anim slot `-1` (measured: npc 6829 → `bas_type_id 1326`, all six anim slots `-1`). osrs239 npc records have no BasType opcode. `tools/common/port_plan.c:184-201` **already resolves this** — `tool_neutral_npc_from_dat2` loads the BasType and writes `idle/walk/walk_back/walk_left/walk_right` into `Tool_NeutralNpc.anim[]`. Reuse it; do not re-solve it. (`cp_npc.c:119` will happily emit `bastype=1326` into the tree — that field must be **dropped**, not carried, or the osrs239 encoder writes an opcode the client does not know.)

### 3.8 Sounds (idx4) — **COPY**, and this is the good news

`RSCache_SoundCodecVersion` (`sound_synth.c:11-24`) returns `RSCACHE_CODEC_SOUND_SYNTH` for **every dat2 cache**, both 530 and osrs239. Format-identical → raw archive copy. Recon's `audioprobe --sweep` on 530 returned 0 failures across 485 samples / 668 tracks / 390 jingles / 148 patches.

This is why sound is Phase 1 alongside sprites: it is the cheapest kind that still exercises the entire alloc → pack → membership → bake → JS5 loop.

The "one synth codec per era" caution in the `sound-audio-session` memory note is about **content ids** (530's summoning sounds 4161/4164/4214/4265/4372 sit above the 3826 OSRS divergence point and mean nothing in osrs239), not about the container. Only sound 188 (`summon_npc`) exists on both sides.

### 3.9 Interfaces — **REAUTHOR. Do not transcode.**

Three independent reasons, any one sufficient:

1. **The decoder does not exist for 530.** `RSCache_Dat2ComponentDecodeRevFromProfile` (`dat2_component.c:236-251`) picks the era from `RSCache_IsRs2Dat2` alone — there are exactly two values. 530 is a hybrid: type-5 sprite blocks follow the **OSRS** rule (no trailing colour int, flips V,H) while type-6 model blocks follow the **643** rule (each size-override short gated on its own mode). Neither existing era is right, and there is no seam for a third without editing that function.
2. **Every hook is a dead pointer.** A 530 component's `onload`/`onop` args name 530 clientscript ids. `sendRunScript(757/765)` in `SummoningCreator.java:62` names scripts that do not exist in osrs239's 9,725.
3. **Every graphic/font/model id inside is wrong.** Interface 747 references sprites 1206/1244/1245/1200; those ids are unrelated art in osrs239.

`RSCache_Dat2ComponentEncodeIf3` explicitly refuses to invert the RS2 layout (`dat2_component.c:1428`). Authoring a fresh `.if` + `.compack` in `OSRS-Content/osrs239-content/interfaces/` is both cheaper and the only path the encoder supports. Read the 530 interface with `tools/dump_interface` for **layout reference only**.

### 3.10 CS2 clientscripts — **REAUTHOR.** Cannot be imported.

`src/cs2/cs2_command.gen.h` is generated from RuneStar's **OSRS** opcode table. 530 uses a different numbering entirely. `cs2_opcode_dialect.h:43` notes RS2_DAT2 was verified against the **634** client only, so even the RS2 dialect is unverified at 530. Decompile 530 scripts with `3rd/rscache/tools/cs2/cs2 decompile --rev rs530` to read the *logic*, then write fresh `.cs2` against osrs239 opcodes.

### 3.11 Textures / materials — **REAUTHOR or hand-map. Incompatible.**

| | 530 | osrs239 |
|---|---|---|
| Count | **680 materials**, idx26, procedural (11 bytes each; measured group size 7482 = 2 + 680×11) | **210 materials**, one archive, **sprite-backed** (`[mat_0] averagehsl=5654 opaque=yes sprite1=447,0,0`) |
| System | proctexture op graph | sprite reference |

There is no transcode. `EXCEPTIONS.md` A5 states it: *"retexture/texture ids are cache-local and do not map across revisions unless `--texture-map` is supplied"*. `RSCache_*ProctextureEncode` does not exist (decode + evaluator only, EXCEPTIONS B18). The `[texture_map]` manifest section is a **hand-built table**, and building it is human work with a colour picker.

### 3.12 Animayas (idx22) — N/A

530 predates skeletal animation (idx22 does not exist in its 29 indices as an animaya table). And the encoder is ABSENT anyway (`cp_assets.c` marks `22_animayas` pass-through; `grep RSCache_.*AnimayaEncode` finds nothing). Nothing to do, but worth stating so a future modern-OSRS familiar port knows it hits a wall.

### 3.13 Maps — out of scope, and stay that way

`CP_ASSET_ENCRYPTED`; owning obelisk placements means owning `xteas.json`. Out of scope per the brief. Obelisk **loc records** (§3.7) are in scope; obelisk **placements** are not.

---

## 4. Id allocation and the ported folder

### 4.1 Assets — works today, zero tool changes

The pack file **is** the path, and subdirectory names are established precedent (53,421 of 61,615 model names already carry a `/`).

```
pack/7_models.pack        100000=ported/scape2009_summoning/spirit_wolf
models/ported/scape2009_summoning/spirit_wolf.model

pack/0_animations.pack     20000=ported/scape2009_summoning/spirit_wolf_frames
pack/1_skeletons.pack       8000=ported/scape2009_summoning/spirit_wolf_rig
pack/8_sprites.pack        20000=ported/scape2009_summoning/pouch_icon
pack/4_soundeffects.pack   20000=ported/scape2009_summoning/summon_puff
```

Bases from `src/content/content_register.c` column 5: `7_models` 100000 (current high-water 61,615), `0_animations` 20000 (10,902), `1_skeletons` 8000 (2,674), `8_sprites` 20000 (8,534), `4_soundeffects` 20000 (12,010). All have headroom.

Model archive id 100000 > 0xFFFF is safe: `header_size_for_archive` (`dat2disk.c:95-97`) writes the 10-byte extended sector header on both read and write, and every osrs239 reference table is format 7 (smart ids).

**Assets have no membership gate at all** — no `.client`/`.server` for asset namespaces. An asset in the pack is in the client cache. So for assets, "which cache you bake" is the only flag (§6 of the feature-flag question, not this one).

### 4.2 Configs — `pack/<ns>.alloc`, never `configs/all.<ns>.compack`

Verified: `cp_name_find` (`cp_names.c:1121-1126`) checks the `.compack` member index, then `pack/<ns>.alloc`. So:

```
pack/npc.alloc         20000=summ_spirit_wolf
pack/obj.alloc         40000=summ_spirit_wolf_pouch
pack/seq.alloc         20000=summ_spirit_wolf_howl
pack/spotanim.alloc     6000=summ_puff_small
pack/loc.alloc         62201=summoning_obelisk
```

This keeps the machine-owned `configs/` tree untouched, which is what `test-server-clean` (`src/Makefile:1876-1886`) demands.

**`ss_allocate.py` does not sweep these namespaces.** `SERVER_NAMESPACES` (`tools/ss_allocate.py:84-93`) is `enum, struct, dbtable, dbrow, param, mesanim, inv, varp`. Two options:

- **(a)** Hand-author above the `// --- allocated below this line by tools/ss_allocate.py; do not hand-edit ---` marker. Legal by the file's own contract, and correct for a one-shot import where the tool already knows every id it is minting.
- **(b)** Extend `SERVER_NAMESPACES`. Higher blast radius — the allocator sweeps the whole tree and would start minting npc/obj ids for any unresolved name anywhere.

**Recommend (a)**, written by `cachepack import` itself via `lc_pack_save` (which merges, per `test/test_pack.c`), and audited by the `port/` ledger (§4.4).

### 4.3 Membership — the step that will bite

**`routing_client_member` (`cp_pack.c:~695-760`) sends an alloc'd name SERVER-ONLY.** The order is: `<ns>.client` → base-cache substrate → `origin_rank == 0` → `<ns>.server` → **`cp_name_find_alloc(...) >= 0` → `server_by_alloc`, return 0** → cell (c). So a ported npc that only has an `.alloc` line reaches the server band and **never reaches the client cache**, silently, with no error.

Every client-visible ported config record must therefore **also** be named in `pack/<ns>.client`.

Files that must be created (`cachepack membership --src OSRS-Content/osrs239-content --rev osrs239 --types obj,seq,spotanim`; it creates only files that do not exist and refuses to overwrite):

| namespace | `.client` today | needed |
|---|---|---|
| npc | exists, **0 data lines** | add ported names |
| loc | exists, **0 data lines** | add ported names |
| **obj** | **ABSENT** | create + add |
| **seq** | **ABSENT** | create + add |
| **spotanim** | **ABSENT** | create + add |

Plus `[namespace:obj] membership = authored` (and seq, spotanim) blocks in `content.ini`.

Be honest about this in the plan: **all five existing `.client` files have zero data lines.** `docs/PACK_ENTITY_SPLIT_PLAN.md` §11.1 says step 4 "author" is unexercised. This import is the first consumer of a designed-but-unrun mechanism. Budget for finding its bugs, and make Phase 1 (§7) exist precisely to find them on a throwaway record.

### 4.4 The ledger — `port/scape2009_530.map`

New sibling to the existing `port/*.map` files, same contract: generated columns re-derived by `--check`, human columns never regenerated.

```
# kind  src_id  src_name        dst_id  dst_name                    disposition  signoff
npc     6829    Spirit wolf     20000   summ_spirit_wolf            minted       ok
model   30443   -               100000  ported/…/spirit_wolf        minted       ok
framemap 1491   -               8000    ported/…/spirit_wolf_rig    minted       downgrade-v3-v1
seq     8297    -               20000   summ_spirit_wolf_idle       minted       ok
texture 34      -               17      -                           hand-mapped  unreviewed
```

Written by `cachepack import`, checked by a new `tools/port_scape2009_ids.py --check` wired into `make -C src test-port` (`src/Makefile:1992-2016`, which already runs 10 such `--check` invocations).

`port/names.map:22-25` states the rule this file exists to enforce, verbatim: *"`lc_id` — the reference's own id. **NEVER copy it**."*

---

## 5. How the client actually receives the new records

Unchanged from the existing path — **new archives need no JS5 wiring**.

1. `cachepack pack --src OSRS-Content/osrs239-content --base cache.osrs239 --out cache.osrs239.baked --rev osrs239 --assets --binary --gamevals` (`src/makefile:1677`, target `make -C src torirsserver-cache`).
   - `--base` copies the pristine cache first. **Mandatory** — `EXCEPTIONS.md` B4: `RSCache_Dat2DiskWriteArchive` appends and re-points, orphaning old sectors, so repacking in place grows forever. The recipe already `rm -rf`s the output.
   - For a new archive id, `cp_reference_sync` (`cp_binary.c:375-470`) grows **both** the reference table's `archives` array (indexed by archive id, gaps at `index == -1`) and its ascending `ids` list; `cp_reference_set_name` names it (`cp_assets.c:1350`).
   - CRC gotcha, `EXCEPTIONS.md` H4: the reference-table CRC covers the container **minus** the u16 version trailer. Get it wrong and the client rejects the archive. This is already right; don't re-derive it.
2. `torirsserver-cache-check` asserts all 23 idx tables landed.
3. **Point both the world and JS5 at the bake.** `js5_server --cache cache.osrs239.baked` and `[cache:boot] dir=cache.osrs239.baked`. This is the one-cache rule; `manifest_osrs239.ini` boots pristine `cache.osrs239` and will not see a single ported record — which is, usefully, the flag-off client for free.
4. The client's metadata-prime barrier (`docs/JS5_INCREMENTAL_CACHE.md`, "Boot contract") validates `255/255` and every reference table **before `App_Init`**, then fills the sparse local cache on demand. A new archive is served like any other.
5. Verify with `tools/js5_cache_verify.py` and `tools/js5_probe.py`.

`--gamevals` skips archive 14 (`cp_names.c:1412` — nested interface+component, and that writer is flat), so ported interface component names never enter the baked cache's symbol table. Cosmetic; nothing at runtime reads gamevals.

---

## 6. Verification — five tiers, each catching a different class of error

The design principle: **each tier must be able to fail.** Per the `verify-blocker-and-failing-test` memory note, mutate the implementation and confirm the check goes red before believing it.

### T0 — Exact consumption on the source side (catches: wrong codec)

Every decoder records `_consumed`. `port_npc/main.c:31-60` already refuses a record whose `_consumed != record_size`, and `cp_unpack_npc:146` warns on it. Extend to a **sweep**: for each of npc/obj/loc/seq/spotanim over the 530 cache, report `exact / short / total`.

Pass bar: **every record the import touches is exact.** Not the whole cache — the recon's 5,476 short objs are elsewhere in the table and are not this slice's problem, but any *ported* record being short is a hard stop.

This is the check that would have caught `--rev rs643`'s FRAME_V2 pin.

### T1 — Semantic round-trip on the destination side (catches: bad encode)

Byte comparison is impossible across formats (OB3 ≠ V3 by construction). Semantic identity is not. After encoding into the tree/cache, decode again with the **osrs239** profile and compare field-by-field against the 530-decoded neutral struct:

- model: vertex count, face count, per-vertex x/y/z, per-face a/b/c, colours, alphas, priorities, texture triangle p/m/n. (Texture **ids** deliberately excluded — they are remapped.)
- framemap: length, types[], bone_groups_lengths[], bone_groups[][]. This is the check that fails today on `cache_write.c`'s V3→V1 no-op.
- frame: framemap id, per-transform deltas.
- seq: frame_ids[], frame_lengths[], every scalar.
- sound: **byte-identical** (it is a copy).
- sprite: **byte-identical** after BMP round-trip — `cachepack verify` already holds sprites to `differ == 0` (`test/test_cachepack_fidelity.sh`).

Where byte identity IS available, demand it. Where it is not, say so explicitly rather than downgrading the bar silently.

### T2 — Render compare (catches: rigs, joints, geometry)

**`3rd/rscache/tools/anim_compare` already is this harness.** Its header states the case exactly: *"the only reliable way to tell a good correspondence from a bad one is to watch both play the same motion and see which frames diverge."* It renders left = source cache seq, right = ported artifact, `frame_NNN.bmp` + `sheet.bmp`, with `--by-label` colouring faces by **vertex label** so mismatched joints show as a colour mismatch.

The one gap: the B side today reads dat1 `.ob2` + `.anim` files (`--b-models DIR | --b-model FILE.ob2 --b-anim FILE.anim`). Add:

```
anim_compare --a-rev rs530 --a-cache <530> --a-seq 8297 --a-model 30443 \
             --b-cache cache.osrs239.baked --b-rev osrs239 --b-seq 20000 --b-model 100000 \
             --by-label --sheet --out build/verify/spirit_wolf
```

~200 LOC: a second `Tool_Dat2Cache` for the B side and a seq→frames→model load, all of which `asset_access.h` already provides.

**Pass bar: the two panels are pixel-identical.** Not "looks right" — identical, because both sides run the same `toridraw` rasteriser on the same geometry. Any divergence is a real difference in the ported data. (Textures are the exception; render with `--by-label` so materials do not participate, then do a second pass without it and expect divergence exactly where the texture map is wrong — which turns T2 into a *texture* check too.)

### T3 — Catalog + viewer (catches: "it decoded but nothing points at it")

`tools/entity_viewer/ev_catalog --rev osrs239 cache.osrs239.baked --names OSRS-Content/osrs239-content --out out/summoning_anims`, then `ev_server --catalog …`.

Pass bar: every ported familiar appears in `npc_catalog.csv` with `rig_match_classic > 0`, its seed idle/walk seqs resolve, and `npc_rigs.csv` shows it on **its own** ported rig — not on framemap 0, the shared human rig with 3,905 sequences on it. A ported familiar landing on framemap 0 means the rig id got remapped to a collision.

Per the `npc-anims-from-rig-catalog` memory note, the rig-share gate is the reliable signal; name matching is not.

### T4 — In-client, headless, permanent (the PORTING_GUIDE §4.3 bar)

```sh
SDL_VIDEODRIVER=dummy TORIRSSERVER_VERBOSE=1 \
  TORIRS_SIM_CLICK_AT=… TORIRS_EXIT_BMP=build/verify/familiar.bmp \
  src/build/torirs --manifest manifest_osrs239_summoning.ini
```

Plus `audioprobe --sweep cache.osrs239.baked osrs239` → 0 failures, and a `::debugproc` per slice.

**Leave the check permanent** — a `ToriRSServer_Pack` rule, a `make -C src test-*` target, or a selftest stanza. "It compiles" is not done (`PORTING_GUIDE.md:633-640`).

⚠️ `test_cachepack_fidelity.sh` **skips loudly when no cache is present, and a skip reads as a pass** (the `pristine-baseline-skips` trap). Any CI claim about a summoning bake must assert the suite actually ran.

---

## 7. Phased order of attack

The dependency graph is asset-closure shaped: **nothing animates until frames + framemaps + seqs all land, and nothing renders until models land.**

```
Phase 0  library + profile            ── unblocks everything
   │
   ├─ Phase 1  sprites + sounds       ── the PIPELINE SMOKE TEST
   │             │
   │             └─ proves: alloc → pack → .client → bake → JS5 → client
   │
   └─ Phase 2  models                 ── unblocks static objs + the obelisk loc
                 │
                 ├─ Phase 3  framemaps → frames → seqs   ── unblocks animated npcs
                 │             │
                 │             └─ Phase 4  spotanims (need model + seq)
                 │
                 └─ Phase 5  npc / obj / loc config records
                               │
                               └─ Phase 6  interfaces + CS2 — REAUTHORED, off-pipeline
```

**Phase 0 — library and profile.** `rev_dat2_rs530.c`; `SEQUENCE_RS2_530`; `OBJ_RS2_530`; `RSCache_Dat2FramemapEncodeCodec` + the `cache_write.c` fix; sharded-config reader in `cp_group_open_disk`. Gate: T0 sweep exact on every type the import will touch. Also run the two cheap experiments that could shrink the whole project: **(i)** does RuneLite 239 accept an OB3 model? **(ii)** are the ported framemaps' `transform_actor`/`masks` all zero?

**Phase 1 — sprites + sounds.** Deliberately first, and deliberately trivial. These are the only two kinds that are format-compatible (sound is a byte copy; sprite round-trips through BMP with byte identity). Their *purpose* is to exercise, on one throwaway record, every mechanism nobody in this tree has ever run: a `pack/<ns>.alloc` line, a `pack/obj.client` file that does not exist yet, a `[namespace:obj] membership = authored` block, a nested asset path, `cp_reference_sync` growing a reference table, and JS5 serving an archive id past the base cache's high-water mark. **If Phase 1 takes three days instead of one, that is the schedule telling you the truth about Phases 2–5.**

**Phase 2 — models.** Highest volume, no dependencies, and it makes pouch/scroll inventory icons and the obelisk visible. Verified by T1 semantic round-trip + T2 render (static, `--frames 0-0`). Textures deliberately deferred: import with `retexture` dropped and faces untextured, land the geometry, then hand-build `[texture_map]` as a separate pass. **Untextured-but-correct beats textured-but-wrong**, and it makes the texture work a measurable diff rather than an invisible defect.

**Phase 3 — framemaps → frames → seqs, in that order.** Framemap first because a frame's head names one and a seq names frames. This is where the V3→V1 downgrade lands and where T2 `--by-label` earns its keep.

**Phase 4 — spotanims.** Pure composition of Phases 2 and 3.

**Phase 5 — npc / obj / loc config records.** Text emitters into the ported configs dir. Cheap once ids exist; this is where the BasType flattening and the `bastype=` field drop happen.

**Phase 6 — interfaces + CS2.** Not this pipeline. Author fresh against osrs239.

---

## 8. Effort, and the single biggest risk

### Effort

| Work | Estimate |
|---|---|
| Phase 0 library: profile + 2 codecs + framemap fix + sharded reader + tests | **~900 LOC C**, 3–5 days |
| `cachepack import` subcommand (manifest, closure walk, remap, tree writer, ledger) | **~1,600 LOC C**, 8–12 days |
| `anim_compare --b-cache` | **~200 LOC C**, 1 day |
| Membership + `content.ini` + `.alloc` plumbing + `port/` ledger + `test-port` wiring | **~300 LOC Python + config**, 2–3 days *if the untested add-path behaves*, **open-ended if it does not** |
| Verification harness T0–T4 + permanent checks | ~400 LOC, 3 days |
| **Pipeline subtotal** | **~3–5 weeks of focused work** |
| Content: ~80 familiar npcs (×2 with wilderness forms) + ~160 objs + ~80 spotanims + closure ≈ **5,000–8,000 records** | mostly machine-driven once the pipeline exists; **the human cost is the texture map and the per-familiar render review**, and that is *per model*, not per batch |
| Texture map (680 → 210, hand-built, colour-matched) | **1–2 weeks, irreducibly human** |

Total honest range: **6–10 weeks** for the asset pipeline alone, excluding interfaces, CS2, the skill/stat plumbing, and all server behaviour.

### The single biggest risk

**Texture ids on model faces.** Measured, not assumed: every familiar model sampled (30443, 31211, 30435, 31168, 30469) carries 3–21 texture triangles and a live `face_textures` array. Those ids index 530's **680 procedural materials**; osrs239's table is **210 sprite-backed materials**. There is no transcode (`EXCEPTIONS.md` A5; no `ProctextureEncode` exists), the mapping is hand-built, and — the part that makes it the top risk — **it is the one error class the pipeline cannot detect automatically**:

- T0 exact-consumption: passes. The id decoded fine.
- T1 semantic round-trip: passes. The id was faithfully carried across.
- T2 render-compare: the two sides run *different texture systems*, so they will never be pixel-identical on a textured face whether the map is right or wrong. `--by-label` (the mode that catches rig bugs) explicitly ignores materials.
- T3/T4: a plausible-looking wrong texture reads as "done".

So a mis-mapped material renders a wolf with a stone hide and nothing anywhere says so. The mitigation is procedural rather than technical: **Phase 2 imports with textures dropped**, the texture map is a separate reviewed pass with its own `port/` ledger column and a `signoff` value that starts at `unreviewed`, and every ported model gets a human-eyeballed `ev_server` screenshot before its row flips to `ok`. That converts an invisible defect into a visible backlog.

Runners-up, both real, both cheaper to close:

1. **Framemap V3→V1 downgrade is a confirmed silent no-op today** (`cache_write.c:553-580` + `RSCache_Dat2FramemapEncode`'s missing codec parameter). Fixable in ~40 LOC, and the measurement in §3.3 tells you whether the downgrade even loses anything. Left unfixed, every ported familiar animates in bind pose with no error.
2. **The membership add-path has never been run.** All five `.client` files have zero data lines; `PACK_ENTITY_SPLIT_PLAN.md` §11.1 says so. `pack/obj.client`, `seq.client` and `spotanim.client` do not exist, and a record with only an `.alloc` line routes **server-only, silently**. Phase 1 exists to find this mechanism's bugs on a throwaway sprite rather than on 8,000 records.

---

## 9. Capabilities this design adds that are ABSENT today

| # | Capability | Where | Size |
|---|---|---|---|
| 1 | rev-530 cache profile | `src/revisions/rev_dat2_rs530.c` + `revisions.c:22` | ~70 LOC |
| 2 | `SEQUENCE_RS2_530` codec (op 13 u16 count, op 14 bare flag) | `src/datatypes/dat2_config_sequence.c` | ~120 LOC |
| 3 | `OBJ_RS2_530` codec (96/121/122/125-130; 23/25 with no trailing byte) | `src/datatypes/dat2_config_obj.c` | ~60 LOC |
| 4 | `RSCache_Dat2FramemapEncodeCodec` — **fixes a confirmed silent data bug** | `src/datatypes/dat2_framemap.{c,h}` + `tools/common/cache_write.c:553` | ~40 LOC |
| 5 | Sharded RS2 config reader (lift the `cp_common.c:58` refusal) | `tools/cachepack/cp_common.c` | ~80 LOC |
| 6 | `cachepack import` subcommand + manifest grammar | `tools/cachepack/cp_import.{c,h}`, `main.c` | ~1,600 LOC |
| 7 | `anim_compare --b-cache/--b-rev/--b-seq/--b-model` | `tools/anim_compare/main.c` | ~200 LOC |
| 8 | `pack/{obj,seq,spotanim}.client` + `content.ini` membership blocks | `cachepack membership`, `content.ini` | config |
| 9 | Id allocation for npc/obj/loc/seq/spotanim (unswept by `ss_allocate.py`) | hand-authored `.alloc` lines written by #6 | — |
| 10 | `port/scape2009_530.map` + `tools/port_scape2009_ids.py --check` + `test-port` row | `port/`, `tools/`, `src/Makefile:1992` | ~300 LOC |

## 10. Two things this design deliberately refuses

- **Do not extend `port_npc` into this role.** It writes a *binary destination cache*, allocates with `tool_alloc_id` ("keep the source id if free, else the next free upward" — `port_plan.c`), which ignores `server_base` entirely and would scatter ported records through occupied osrs239 id space. Its closure walk and its neutral-npc/BasType flattening are worth *reusing as library code from `tools/common/`*; its allocation policy and its output target are wrong for this tree.
- **Do not use `--rev rs643` for anything, ever again, on the 530 cache.** It pins `FRAME_V2` (a rev-610 format). Every animation-frame and framemap result produced with it — including the recon's `port_npc` plan reporting `framemaps: 54090 -> 54090` against a 2,435-group idx1 — is garbage. Phase 0 exists partly to make `--rev rs530` the only spelling that works.

===== DESIGN: design-skill-and-cs2 =====
Verification complete. Here is the design.

---

# Summoning as a real, drawable 24th skill in `3draster` / `osrs239-content`

Everything below was checked against the tree and `cache.osrs239`. Where I say "measured", I ran it in this session.

---

## 0. The one decision everything hangs off: **Summoning is stat 24, not 23**

**Stat 23 is already Sailing, and it is wired end-to-end in `cache.osrs239`.** Measured:

| record | evidence |
|---|---|
| `configs/all.enum` `[enum_681]` (int→stat, the client's canonical roster) | `val=24,23` — slot 24 maps to stat 23 |
| `[enum_108]` (slot→string) | `valstr=24,Sailing` |
| `[enum_680]` (stat→string) | `valstr=23,Sailing` |
| `[enum_255]` (stat→25px icon) | `val=23,228` → sprite pack `staticons2_13` |
| `[enum_5917]` (stat→silhouette) | `val=23,7454` → `stat_silhouette_23` |
| `[enum_1505]` (stat→13px guide title icon) | `val=23,3230` |
| `[enum_1497]` (members-only) | `val=23,1` |
| `interfaces/stats.compack` | `24=sailing`, a real 62×30 cell at x=127 y=211 |
| `interfaces/levelup_display.compack` | `57=sailing` |
| `scripts/script_1003.cs2` / `script_1004.cs2` | XP-drop listener already passes `stat_xp(stat_23)` |
| `scripts/script_8950.cs2` | `case 23 : return(~script8951(1))` — the hide gate |

2009scape's `Skills.SUMMONING = 23` is a **direct collision**. Taking 23 would make 26 cache-native clientscripts draw a boat for Summoning and would delete a shipped skill from the substrate. **Summoning takes stat id 24 / `enum_681` slot 25.**

Client headroom is exact: `src/game/rs_player_stats.h:11` `#define RS_PLAYER_STATS_SKILL_COUNT 25` — index 24 is the last valid slot. **No client C change is needed for storage.** (Bump to 32 opportunistically if you want future room; not required.)

### Every place that must learn about it

| # | Layer | File | Edit |
|---|---|---|---|
| 1 | server C | `src/torirsserver/torirs_server.h:558` | `TORIRSSERVER_STAT_COUNT = 23` → `25`. 40 call sites, all `< TORIRSSERVER_STAT_COUNT` bounds or `[TORIRSSERVER_STAT_COUNT]` arrays (measured). `stat_dirty` is `uint32_t` — 25 fits. `torirs_server_save.c:202/491` is id-keyed ini, so saves stay forward/backward compatible. |
| 2 | skill-name pack | `OSRS-Content/osrs239-content/pack/stat.pack` | append `23=sailing` **and** `24=summoning`. The file stops at `22=construction` today, so it already disagrees with the cache; adding 23 is a correctness fix with no behaviour (nothing awards Sailing xp). Namespace is `ids = protocol, names = authored`, not swept by `ss_allocate.py` — a hand edit is the sanctioned path. |
| 3 | ServerScript stat commands | none | `ssc_compile.c:755-771` gives `STAT*`/`NPC_STAT*` opcodes `base_hint = SSC_SYM_STAT`, so `stat_base(summoning)`, `stat_advance(summoning, …)`, `stat(summoning)` resolve **the moment #2 lands**. No compiler change. |
| 4 | xp table | none | `torirs_server_combat.c:400-441` `g_xp_table[99]` is per-level, skill-agnostic. `rs_player_stats.c:19-24` likewise. |
| 5 | total level | see §1 | three implementations, two auto-extend, one is deliberately frozen |
| 6 | combat level | **none — verified safe by construction** | see §1 |
| 7 | validator | `src/torirsserver/torirs_server_pack.c:336` | already `< TORIRSSERVER_STAT_COUNT`; widens with #1. Lets an obj carry `require=summoning,N`. |

### Name-collision guard (`[advancestat,summoning]`)

`ssc_compile.c:2286-2296` resolves a trigger subject with `SSC_SYM_UNKNOWN` — any namespace, first match wins. Measured: **`summoning` and `sailing` are free in every `pack/*.pack` and every `configs/all.*.compack` today.** The Summoning content port will add ~80 objs and ~120 npcs; **no ported record may be named exactly `summoning`** (use `summoning_pouch`, `summoning_obelisk`, `spirit_wolf`, …). Add this as a `ToriRSServer_Pack --check-only` rule: *every name in `pack/stat.pack` must be unique across all namespaces.* That is a two-line check and it converts a silent mis-resolution into a load error.

---

## 1. Total level and combat level

**Combat level must not move, and it cannot.** All three implementations name stats explicitly — none loops:

- `src/game/rs_player_stats.c:76-89` — `RS_SKILL_{DEFENCE,HITPOINTS,PRAYER,ATTACK,STRENGTH,RANGED,MAGIC}` only.
- `src/torirsserver/torirs_server_combat.c:913-925` — `TORIRSSERVER_STAT_{DEFENCE,HITPOINTS,PRAYER,ATTACK,STRENGTH}` only.
- `server/scripts/player/scripts/combat_level.rs2:5-11` — `stat_base(defence|hitpoints|prayer|attack|strength|ranged|magic)` only.

**No change, and no risk.** Add a regression assertion anyway (§7).

**Total level does move, correctly and automatically.** `scripts/script_1007.cs2` (`[proc,stat_totallevel]`) walks `enum_681` from key 1 until `enum(...)` returns `null`, skipping any stat where `~script8950($stat) != 0`. Same for `script_1008` (total XP) and `script_1320` (F2P total). Adding `val=25,24` to `enum_681` puts Summoning in Total level with **zero script edits** — and the `~script8950` gate (§3.3) takes it back out when the feature flag is off.

⚠️ **`enum_681` keys must stay contiguous.** Key 25 with no gap; a hole truncates the loop and silently drops every later skill from Total level.

`src/game/rs_player_stats.c:51` (`for i in 0..17` + runecraft) is the **dat1/CS1** total, deliberately 2004-shaped. Leave it alone — rev-239 total comes from CS2.

---

## 2. The wire

**Nothing needs widening.** The stat index is a plain byte at every revision, and no decoder bounds it:

- osrs239: `src/net/rev/osrs239/osrs239_parse.c:755-763` — `g1 invisibleBoostedLevel, g1 level, g1_add128 stat, g4_3412 xp`.
- osrs230: `src/net/rev/osrs230/osrs230_parse.c:380-400` — `g1 stat, g1 base, g4 xp, g1 boosted`.
- Encoder: `torirs_server_encode.c:1572-1600` `ToriRSServer_SendStat` → `pl->update_stat` (rev-dispatched) or the 7-byte fallback.
- Transmit loop: `torirs_server_world.c:8925-8935`, `for stat < TORIRSSERVER_STAT_COUNT` + `stat_dirty` bit.

The only bound in the whole path is a **stale comment**: `src/net/rev/revpacket.h:116` `int stat; /* g1: 0-22 */`. Fix the comment.

### Flag-off safety — two different clients, two different answers

| receiver | behaviour on `UPDATE_STAT stat=24` | verdict |
|---|---|---|
| **`torirs`** (this repo's client) | `rs_gameproto_exec.c:561-575` bounds against `RS_PLAYER_STATS_SKILL_COUNT` (25). 24 passes: xp stored, `current_level` set, `RS_CS2Host_NotifyStatChanged(24)` fires, combat level recomputed (unchanged, §1). Nothing draws it because no interface reads stat 24. **Silent, harmless no-op.** | ✅ safe |
| **RuneLite / a real Jagex 239 client** (`./run-osrs239.sh`) | its `Skill` table has 24 entries, 0..23. Index 24 is out of range. | ⚠️ **unsafe** |

**Therefore the server-side rule is: never transmit stat 24 unless the flag is on.** Concretely, gate it at the single transmit site rather than at every writer:

```c
/* torirs_server_world.c, phase_client_out stat loop */
for( int stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
{
    if( (player->stat_dirty & (1u << stat)) == 0 )
        continue;
    if( stat == TORIRSSERVER_STAT_SUMMONING && !srv->feature_summoning )
        continue;                      /* a client without the record cannot draw it */
    ...
}
```

That is one branch and one boolean, and it is the *only* C the feature flag needs. It also makes the flag-off case safe for a third-party client, which the content-side gate alone cannot do.

---

## 3. The skills tab

### 3.1 The exact records

- **Interface 320 = `stats`** — `pack/3_interfaces.pack:321`; source `interfaces/stats.if` (481 lines, 34 components) + `interfaces/stats.compack`.
- **Layout is hand-authored geometry, not computed.** Measured from `stats.if`:
  - `[universe]` **190×261**
  - 24 cells, **3 columns × 8 rows**, width **62**, columns at **x = 1, 64, 127**, rows at **y = 1, 31, 61, 91, 121, 151, 181, 211**, height **30** (last row **32**)
  - `[total]` at **y=241, 190×19**, with children `com_26..com_32` (five 36×36 stone graphics + one text driven by script 396)
  - `[tooltip]` = 320:33
- **The sidebar slot is fixed.** `toplevel_osrs_stretch.if` `[side0]..[side13]` are all `width=190 height=261`, absolute, no size modes. **A taller `stats` will clip. There is no 4th column and no 9th row at the current pitch.**
- Each cell's entire content is one onload: `onload = i:393, i:-2147483645, i:20971553, i:<enum_681 slot>, i:<icon x-nudge>` where `20971553 = (320<<16)|33 = stats:tooltip`.

### 3.2 The edit — a 3 × 9 grid at 26px pitch

`scripts/script_393.cs2` (`[clientscript,stats_init]`) builds every cell: two 36×36 stone graphics at (0,0) and (31,0), the skill icon **25×25 at (3+nudge, 4)**, and two 15×12 texts at (32,4) / (44,16).

Icons are **not** all padded — measured `sprites/staticons_0/pack.meta` is `sprite0=25,25,25,25,0,0` (full-bleed 25×25). So shrinking the cell without moving the icon clips real art. Both must change together, and both files compile (proved in §3.4).

**Implemented correction — `interfaces/stats.if`** uses 25 cells at pitch 26, but Total is not
left untouched:

```
columns x = 1, 64, 127        (unchanged, width 62)
rows    y = 1, 27, 53, 79, 105, 131, 157, 183, 209   (height 26)
grid bottom = 235;  [universe] stays 190×261
```

The earlier centred-lone-cell recommendation was visually wrong. The correct assignment, proven
against the supplied/reference layout in the real client, is Construction / Hunter / Summoning
across row 8; Sailing is `x=1,y=209`; Total is `x=64,y=209,width=126,height=26` and spans the
other two cells of row 9.

New block, appended to `stats.if`:

```ini
[summoning_stats_cell]
if3=yes
type=0
x=127
y=183
width=62
height=26
layer=20971520
clickmask=6
name=
targetverb=
op1=*
op2=*
onload=i:1198,i:-2147483645,i:20971553,i:25,i:2
```

The final `int3=2` makes the icon's local x position `3+2=5`, matching the measured rev-530
wolf-head component. Script 1198 is the dedicated Summoning-cell copy; component 34 does not
reuse script 393.

and `interfaces/stats.compack` gains `34=summoning`, `35=tooltip` — **no.** `tooltip` is already file id 33 and the `.compack` is the id authority with holes legal; append **`34=summoning`** and leave `tooltip` at 33. Component ids are data, order in the `.if` is not.

**`scripts/script_393.cs2`** — three lines, uniform across all 25 cells:

```
cc_setposition(calc(3 + $int3), 4, ...)  →  cc_setposition(calc(3 + $int3), 0, ...)   # icon
cc_setposition(32, 4, ...)               →  cc_setposition(32, 2, ...)                 # level text
cc_setposition(44, 16, ...)              →  cc_setposition(44, 13, ...)                # .cc dotted twin
```

(The two 36×36 stone graphics and the two lock overlays stay — they already overflow the cell and are clipped; more overlap tiles fine.)

**`configs/all.enum`-level records** (authored as rank-1 overlays, §5):

| enum | line to add | meaning |
|---|---|---|
| `enum_681` | `val=25,24` | slot 25 → stat 24 — **the row that makes everything else work** |
| `enum_108` | `valstr=25,Summoning` | slot → name (tab tooltip, guide title, XP-drop config list) |
| `enum_680` | `valstr=24,Summoning` | stat → name |
| `enum_255` | `val=24,<25px sprite>` | tab icon |
| `enum_5917` | `val=24,<25px silhouette>` | locked/silhouette icon |
| `enum_1505` | `val=24,<13px sprite>` | skill-guide title icon |
| `enum_1497` | `val=24,1` | members-only (Summoning is P2P) |
| `enum_5750` | `val=25,1` | *optional* — enables the "Level-up Unlocks" pseudo-tab |

### 3.3 The feature gate — the cache already ships one, and it is Sailing's

This is the single best find of the recon pass. `scripts/script_8950.cs2`:

```
[proc,script8950](int $int0)(int)
switch_int ($int0) { case 23 : return(~script8951(1)); case default : return(0); }
// 8951:  case 1 : return(~script607);
// 607:   return(~int_to_bool(%varbit18166));
```

`~script8950(stat)` returns **non-zero = this skill is locked**. Script 393 draws two 90%-transparent `miscgraphics,4`/`,6` lock plates over the cell; scripts 1007/1008/1320 skip the skill in Total level, Total XP and F2P total. Measured: `varbit 18166 = content_restrict_sailing_serverside`, on `basevar = content_restriction_temp_1` (**varp 4940**), `startbit=0 endbit=0`. **Bits 1..31 of varp 4940 are free.**

And — measured — **scripts 393 and 396 both already listen on varp 4940**:
```
if_setonvartransmit("stats_init($component0, $int1, $int2, $int3){var3278, var4940}", stats:0);
if_setonvartransmit("stats_skilltotal($component0, $int1){var3278, var4940}", $component0);
```
So flipping the bit repaints the whole tab and the Total row with no extra plumbing.

The edit:

```
// script_8950.cs2
case 24 : return(~script8951(2));
// script_8951.cs2
case 2  : return(~script<new>);       // or inline the varbit read
// new proc (or extend 607's shape)
return(~int_to_bool(%varbit<summoning_restrict>));
```

New varbit record: `basevar = content_restriction_temp_1`, `startbit = 1`, `endbit = 1`, name `content_restrict_summoning_serverside`. See §5.3 for the namespace prerequisite.

**This is the in-cache soft gate.** It is optional — see the flag layering in §5.4 — but it is what lets one baked cache ship both states, and it is the mechanism the client already honours in the tab, the totals and the F2P total.

### 3.4 Decompile → edit → compile → repack, and the round-trip risk

**Measured this session, and the headline is: the risk is essentially zero for every script Summoning touches.**

```
$ cs2 compile --cache cache.osrs239 --rev osrs239 \
      --names ~/Documents/git_repos/cs2/src/main/resources/org/runestar/cs2 \
      --src <23 skill-tab/guide/orb/xpdrop scripts>
  compiled 22, failed 1        # only script_1904

$ cs2 compile … --src <all 27 scripts that read enum_681>
  compiled 26, failed 1        # only script_1904

$ cs2 compile … --src <all 70 scripts that mention xpdrops>
  compiled 70, failed 0

$ cs2 roundtrip --cache cache.osrs239 --rev osrs239 1904 1902 1903 393
  4/4 decompiled, 4 compiled, 4 same-length, 4 exact
```

So: `393, 394, 395, 396, 2366, 1007, 1008, 1320, 8950, 8951, 607, 9337, 9348, 82, 446, 447, 2069, 1000-1042, 1902, 1903, 912` **all compile from the committed tree today.**

**The single failure and its exact cause** — `script_1904.cs2:62`:
```
stale (tree):  ... parawidth(.cc_gettext, .cc_getwidth, ._1703(2523)) ...
fresh decomp:  ... parawidth(.cc_gettext, .cc_getwidth, .cc_getcomponentparam(param_2523)) ...
```
The tree's source predates the decompiler learning opcode 1703's name. **The tree is stale, the compiler is fine.** Fix is a one-line edit, or `cachepack unpack --assets=scripts` restricted to that id.

**The procedure:**

```sh
# 0. refresh only the stale sources you need (1904 is the only one)
CACHEPACK_CS2_NAMES=~/Documents/git_repos/cs2/src/main/resources/org/runestar/cs2 \
  3rd/rscache/tools/cachepack/cachepack unpack --cache cache.osrs239 --rev osrs239 \
    --src OSRS-Content/osrs239-content --assets=scripts
#    ⚠ this also rewrites script_73.cs2 / script_7304.cs2, which carry hand-authored
#      comments guarded by tools/check_crystal_set_contract.py (a hard prerequisite
#      of torirsserver-cache). Re-apply those two comments, or unpack selectively.

# 1. edit, keeping the `// <id>` header line

# 2. GATE — this is the step that stops a silent no-op
mkdir -p /tmp/cs2src && cp <edited>.cs2 /tmp/cs2src/
3rd/rscache/tools/cs2/cs2 compile --cache cache.osrs239 --rev osrs239 \
  --names $CACHEPACK_CS2_NAMES --src /tmp/cs2src --out /tmp/cs2bin
#    require: "compiled N, failed 0"

# 3. bake
make -C src torirsserver-cache          # cachepack pack --base cache.osrs239 \
                                   #   --out cache.osrs239.baked --assets --binary --gamevals

# 4. run
./src/torirs --manifest manifest_osrs230_embed.ini --user testc --pass test
```

Two traps to write into the runbook:
- **`CACHEPACK_CS2_NAMES` is a hard, undeclared dependency.** Without it, `script_393.cs2` fails on `unknown constant '^iftype_graphic'` (measured), `cachepack` prints one stderr line and **ships the base cache's bytes**. `src/makefile:1675` hard-codes `$(HOME)/Documents/git_repos/cs2/...`.
- **`manifest_osrs239.ini` boots the pristine `cache.osrs239` and will never show an edit.** Use `manifest_osrs230*.ini` / `manifest_osrs239_net.ini`, which point at `cache.osrs239.baked`.

`stat_24` is a **legal CS2 spelling with no names-table entry** — `3rd/rscache/src/cs2/cs2_names.c:630-677` falls back to `<literal>_<id>` for `RSCACHE_CS2_TYPE_STAT` and the compiler reads it back. The cache itself already does this: `script_1003.cs2` spells Sailing `stat_xp(stat_23)`. **Do not depend on editing the external RuneStar `stat-names.tsv`** — that would make the build non-hermetic.

### 3.5 Fallback if a target script will not round-trip

Ranked, all of them real:

1. **Re-decompile that id** (`cs2 roundtrip` proves it is exact; 1904 is the worked example). Covers the only known failure.
2. **Author a new clientscript for the new cell.** This is implemented, not a fallback: allocated
   script 1198 is `summoning_stats_init`, and only component 34 calls it. The pitch-26 legacy-cell
   adjustment remains in the gated script-393 overlay, while the wolf-specific nudge and new-cell
   behavior are isolated in 1198.
3. **Move the work into the interface record.** Cell backgrounds, the icon and the two texts can all be authored as real `type=5`/`type=4` children in `stats.if` with `graphic=`/`font=` set statically, leaving the clientscript to write only the numbers. Bigger `.if` diff, zero CS2 edit.
4. **Do not shrink the grid at all.** Keep 3×8 at pitch 30 and put Summoning nowhere in the tab; reach the guide from the orb's op menu instead. Ugly, but it un-blocks the rest of the port.

---

## 4. The skill guide

### 4.1 The plumbing (all verified)

- **Interface 860 = `skill_guide_v2`** (`pack/3_interfaces.pack:861`). Interface 214 `skill_guide` is the dead legacy one.
- 860 draws none of itself. Everything is clientscript **1902** → `if_setonresize("script1903(...)")` → **1904**, which lays out the window and builds every row.
- Selection state is **client varcs, not varbits**: `%varcint1172` = skill (an `enum_681` **slot**, 1..24), `%varcint1173` = subsection id. (The rev-530 `varbit 3288/3289` in the recon is 2009scape's, not this cache's.)
- Server side already exists and is complete for 24 skills:
  - `server/scripts/interface_skill_guide/configs/skill_guide.constant` — `^clientscript_skill_guide_init = 1902`, `^skill_guide_tab_default = 0`, `^skill_guide_attack..^skill_guide_sailing = 1..24`
  - `server/scripts/interface_skill_guide/scripts/skill_guide.rs2` — `[proc,skill_guide_login]` with 24 `if_setevents(stats:<skill>, 0, 0, ^if_event_op2)`, `[proc,skill_guide_open](int $skill)` = `if_opensub(toplevel_osrs_stretch:mainmodal, skill_guide_v2, 0)` **then** `runclientscript*(1902)($skill, 0, 0, 0)` (order load-bearing), and 24 `[if_button2,stats:<skill>]` handlers.

### 4.2 Where the content lives — two cache dbtables, keyed by slot

Measured schemas from `configs/all.dbtable`:

```ini
[skill_guide_subsections]        ; dbtable 212, 196 rows
columns=4
columndef=0:skill,int            ; enum_681 SLOT (agility rows say 8), not a stat id
columndef=1:id,int               ; 0 = Overview, 1.. = real tabs
columndef=2:header,string
columndef=3:membersonly,boolean

[skill_features]                 ; dbtable 213, 3447 rows
columns=10
columndef=0:icon,obj             ; default 7620
columndef=1:sprite,graphic,int,int,int,int
columndef=2:text,string
columndef=3:skill,int,int,int    ; TUPLE: (slot, level, subsectionId) — repeatable
columndef=4:quest,dbrow
columndef=5:otherreq,string
columndef=6:membersonly,boolean
columndef=7..9:otherdata_{magic,sailing,construction},obj
```

Script 1904's queries, decoded (`(table<<12)|(column<<4)|element`):
- `db_find_with_count(868352, %varcint1172, 0)` → 212 col 0 = skill slot
- `db_find_filter_with_count(868368, %varcint1173, 0)` → 212 col 1 = tab id
- `db_getfield(row, 868384 / 868400, 0)` → header string / membersonly
- `db_find_with_count(872497, %varcint1172, 0)` → 213 col 3 element **1** = slot
- `db_find_filter_with_count(872498, $level, 0)` → element 2 = level, iterated 0..100
- `db_find_filter_with_count(872499, %varcint1173, 0)` → element 3 = subsection

A real row, for shape:

```ini
[skill_feature_agility_pipesqueezeinkaramjadungeon]
columns=10
table=skill_features
columndef=0:icon,obj
values=0:0:6520
columndef=2:text,string
values=2:0:Pipe squeeze in Karamja Dungeon
columndef=3:skill,int,int,int
values=3:0:8,34,2
columndef=6:membersonly,boolean
values=6:0:1
```

### 4.3 The exact records to add

**Constants + arming** (`server/scripts/interface_skill_guide/`):
```
^skill_guide_summoning = 25
if_setevents(stats:summoning, 0, 0, ^if_event_op2);
[if_button2,stats:summoning]  ~skill_guide_open(^skill_guide_summoning);
```
(Put the ported half in the ported lane and cross-file it, or amend these two shared files — see §5.4.)

**`skill_guide_subsections` rows** — one per tab, `skill = 25`. Mirror 2009scape's `1019.cs2` tab set, minus what this repo does not have:

| id | header | membersonly |
|---|---|---|
| 0 | Overview | 1 |
| 1 | Familiars | 1 |
| 2 | Summoning Scrolls | 1 |
| 3 | Pets | 1 |
| 4 | Equipment | 1 |
| 5 | Other | 1 |

⚠️ Row **id 0 ("Overview") is mandatory** — `^skill_guide_tab_default = 0` and `script_1904` special-cases `%varcint1173 = 0` to `~script9176`, the Overview body. `script_9176.cs2b` is **bytecode-only** (one of the 357 declined) so its behaviour cannot be altered; supply the row and let it render.

**`skill_features` rows** — one per unlock. From `SummoningPouch.java`'s 78 familiars + `SummoningScroll.java`'s 82 scrolls, the natural source is 2009scape's own guide text (`dumps/scripts/1020.cs2`, 691 lines of `(level, itemId, "name<br>ingredients", …)`). Per row:

```ini
[skill_feature_summoning_spiritwolf]
columns=10
table=skill_features
columndef=0:icon,obj
values=0:0:<osrs239 obj id of the ported Spirit wolf pouch>
columndef=2:text,string
values=2:0:Spirit wolf - Gold charm, wolf bones, 7 spirit shards
columndef=3:skill,int,int,int
values=3:0:25,1,1
columndef=6:membersonly,boolean
values=6:0:1
```

`icon` is an **obj id in the target tree's own space** — resolved through `pack/obj.pack`, never copied from rev 530.

**`dbindex` — the fragile step, and the fix**

`db_find` reads cache table 21, not the dbrows. `dbindex/dbindex_212.dbi` and `dbindex_213.dbi` carry `[master]` plus `[column_N]` blocks of `index=<tuplePos>:<value>:<row>,<row>,…` in binary order. The file header says *"Derived from the dbrows rather than authored"* — but **measured: there is no regenerator.** `dbindex_read` (`cp_decode.c:4759`) parses and encodes the text exactly as written; nothing in `tools/` or `src/makefile` rebuilds it. A row missing from the index is invisible to `db_find`, with no error.

**Write `tools/gen_dbindex.py` as part of this slice.** It reads `configs/all.dbrow` + `all.dbtable` + `pack/dbrow.alloc` and emits every `dbindex/*.dbi`. Its acceptance test is self-proving: **regenerate all 294 and require a byte-identical diff against the committed tree** before adding a single Summoning row. That converts the most fragile step in the whole design into a checked one, and it pays for itself the next time anyone adds a drop table or a quest.

**Routing** — `fields/dbrow.ini` says `records = server`, and `routing_client_member` (`cp_pack.c:690-760`) routes an alloc-claimed record server-side. A guide row must reach the **client**. The gate's own order is *named in `<ns>.client` → substrate → rank-0 → `<ns>.server` → alloc → default*, so the fix is:
- `[namespace:dbrow] membership = authored` in `content.ini`
- `cachepack membership --src OSRS-Content/osrs239-content --rev osrs239 --types dbrow` to seed `pack/dbrow.client`
- name every Summoning guide row in `pack/dbrow.client`

This is the **first real use of the designed-but-unexercised add path** (all five existing `.client` files are empty). Budget for finding its bugs; prove it with a 2-row spike before authoring 200.

### 4.4 Icons and sprites

This proposal was superseded for the stats icon. The implemented icon is an exact export of
rev-530 sprite pack 222, remapped to the marked target name
`ported/scape2009_summoning/summoning_staticon` at target pack id 229. Source 222 is provenance;
229 is the target allocation. Its exact canvas is `sprite0=25,25,22,23,0,2` and its SHA-256 is
`89726834d13ce73b8fff38eb34567ed2e52c7757b2d8405577e801979e4178cd`.

The remaining future guide/orb sprites still require independent target allocations:

| id | pack name | size | consumer |
|---|---|---|---|
| 20002 | `ported_scape2009/summoning/guideicon` | 13×13 | `enum_1505` val=24 — guide title |
| 20003 | `ported_scape2009/summoning/orbicon` | 20×20 | the orb (§6) |

Each is `sprites/ported_scape2009/summoning/<name>/{0.bmp, pack.meta}` plus a line in `pack/8_sprites.pack`. The **path is the pack name** — `cp_assets.c:1383-1387` walks the pack, not the directory (`snprintf(base, "%s/%s", root, name)`), and `models/npc/…` already uses subdirectories. That is how the assets get a clearly-marked ported folder with no tool change.

`pack.meta` must state the palette explicitly:
```
count=1
palette=<n>
p0=0x000000
p1=…
sprite0=25,25,25,25,0,0     # mem_w,mem_h,crop_w,crop_h,off_x,off_y
```
`cp_decode.c:2475-2497`: the palette is written and read back, never re-derived — **a colour absent from `pack.meta` snaps to the nearest entry** (and says so).

The old warning against id 229 was too broad: copying the *source* id would be wrong, but source
222 was not copied. The marked overlay deliberately gives the target name `summoning_staticon`
the vacant target id 229 and records the 222→229 translation in `port/summoning_530.map`.

---

## 5. Where it all lives, and the feature flag

### 5.1 The ported folder — a real mechanism, not a naming convention

Measured in `3rd/rscache/tools/cachepack/cp_merge.c:300-380`: **arity is observed from rank 0**, and *"a key rank 0 states more than once is a list: append, never replace."* `enum_681` states `val=` 24 times in `configs/all.enum` (rank 0), so `val` is multi-valued for enums, so a rank-1 block:

```ini
; OSRS-Content/osrs239-content/server/scripts/ported_scape2009_summoning/configs/summoning.enum
; Policy: 2009scape Skills.java (SUMMONING) — remapped to stat 24, see docs/SUMMONING_PORT.md
[enum_681]
val=25,24

[enum_108]
valstr=25,Summoning

[enum_680]
valstr=24,Summoning
...
```

**appends** those rows to the cache's records rather than replacing the list. And routing: the record's `origin_rank` is 0 (rank 0 created it), `enum_681` is in the base cache → **client by the substrate clause**, and it is not in `pack/enum.server` → not server-side. Verified by reading `routing_client_member`; **prove it with a spike before building on it** (§7, Spike 0).

Tree layout:

```
OSRS-Content/osrs239-content/
  server/scripts/ported_scape2009_summoning/
    configs/  summoning.enum          # the 8 enum overlays above
              summoning.constant      # ^summoning_enabled, ^skill_guide_summoning
              summoning.varp          # server-side runtime state
              summoning_guide.dbrow   # the guide rows  (client-routed, §4.3)
    scripts/  summoning_skill.rs2     # advancestat, guide arming, orb arming
  sprites/ported_scape2009/summoning/{staticon,staticon_silhouette,guideicon,orbicon}/
  interfaces/stats.if / stats.compack        # 25th cell            (shared, edited)
  interfaces/orbs.if  / orbs.compack         # summoning orb        (shared, edited)
  scripts/script_393.cs2, 8950, 8951, 1003, 1004   # (shared, edited)
  pack/stat.pack, pack/8_sprites.pack, pack/dbrow.client
```

Provenance marking follows the tree's actual convention (measured: 1,101 files say `ported`, 295 say `2009scape`, zero directories are provenance-named — but `port_lostcity`'s `areas/area_ported` default is the one precedent): **a `// Policy: 2009scape <Class>.java` header on every file**, plus the directory name, plus a `port/summoning_530.map` ledger wired into `make -C src test-port`.

Records that *must* live in shared files (the 25th cell in `stats.if`, the `case 24` in `script_8950.cs2`) get a `// ported: 2009scape Summoning — see server/scripts/ported_scape2009_summoning/` comment. `content.ini`'s existing-but-unused `names = imported` value ("a foreign revision's table — every line is a *claim*") is the right register marker for any name table crawled out of the 530 cache.

### 5.2 Prerequisite: `varbit` must be allowed to grow

`content.ini` currently pins `[namespace:varbit] ids = cache`. `content_register.c:171` already declares `server_base = 25000` against a cache high-water of **20410** — the register anticipates this; `content.ini` forbids it. Per `PORTING_GUIDE` §2.4 item 4 (*"a namespace that cannot grow is a bug, not a constraint"*), flip it:

- `content.ini`: `[namespace:varbit] ids = server`
- `tools/ss_allocate.py`: add `'varbit'` to `SERVER_NAMESPACES` (currently 8 entries, no varbit)
- new `fields/varbit.ini` with `records = client` **or** `pack/varbit.client` naming the summoning varbits (prefer the per-entity file — one boolean for the whole namespace is the blunt instrument the enum membership block explicitly moved away from)

The gate varbit is then `content_restrict_summoning_serverside`: `basevar=content_restriction_temp_1` (varp 4940), `startbit=1 endbit=1`.

**Interim fallback if that is too much for slice 1:** author the varbit directly into `configs/all.varbit` + `.compack` at id 20411 and note it in the port ledger. Ugly, works, reversible.

### 5.3 What does *not* need to change

- `pack/varp.alloc` — the gate rides an existing cache varp; no new varp.
- `TORIRSSERVER_VARP_COUNT` (6217, already exceeded by `varp.alloc` at 6225) — untouched here, but flag it separately; it is a live silent-drop bug.
- `src/features/features.h` — this is the **client-era** table (`ToriRS_FeatureTable`, resolved from cache lineage by `ToriRS_Features_ForCache`, which discards `revision`). Summoning is not an era divergence. **Do not add a field here.** Adding one would make `ForCache` a liar and would still not reach the server, which builds its own copy in `torirs_server_boot.c:98` and only honours `TORIRSSERVER_GROUND_CLICK_NEAREST`.

### 5.4 The flag, in three layers

| layer | mechanism | what it gates | cost |
|---|---|---|---|
| **1. bake** (primary) | which cache is baked / booted. `manifest_osrs239.ini` → pristine `cache.osrs239` = flag-off client, for free. `make -C src torirsserver-cache TORIRSSERVER_CACHE_DIR=$PWD/cache.osrs239.summoning` + `manifest_osrs239_summoning.ini` = flag-on. | every client-visible record: the 25th cell, the enums, the guide rows, the orb, the sprites | zero new mechanism |
| **2. server** | `^summoning_enabled` in `ported_scape2009_summoning/configs/summoning.constant`, tested at the top of every ported entry point; plus the one C branch in §2 gating the stat-24 transmit | all server behaviour, and third-party-client safety | one constant + one `if` |
| **3. in-cache soft gate** (polish) | `script_8950` `case 24` → `content_restrict_summoning_serverside` on varp 4940 bit 1 | lets **one** baked cache ship both states: cell greys out, Total level excludes it, guide still opens | needs §5.2 |

Layers 1+2 are sufficient and cheap. Layer 3 is what you want once the port is real, because the tab and the totals already listen on varp 4940 (measured) and repaint the instant the bit flips.

---

## 6. XP drops and level-up

### 6.1 XP drops — pure CS2, two files, both compile

The drop path is **not** engine-driven here. Measured: `%varcint953..966` (the engine's pending-drop ring) is never written by `src/`, so the live path is `script_1003.cs2`'s stattransmit diff:

```
// script_1003.cs2  [proc,xpdrops_setstatlistener]
if_setonstattransmit("xpdrops_stattransmit(0, …, stat_xp(attack), …, stat_xp(hunter), stat_xp(stat_23))", $component1);
if_setontimer     ("xpdrops_stattransmit(1, …, stat_xp(attack), …, stat_xp(hunter), stat_xp(stat_23))", $component1);
```

24 snapshot values in, and `script_1004.cs2` diffs each against `stat_xp(<skill>)` to find what moved. **Sailing is already there as `stat_23`** — that is the exact precedent.

Edits:
1. `script_1003.cs2` — append `, stat_xp(stat_24)` to both listener strings.
2. `script_1004.cs2` — add a 25th `int` param and one diff block:
   ```
   $int37 = calc(stat_xp(stat_24) - $int35);
   if ($int37 > 0) { $statarray0($int36) = stat_24; $intarray1($int36) = $int37; $int36 = calc($int36 + 1); }
   ```
   and bump `def_int $length35 = calc(24 + 1)` → `calc(25 + 1)`.
3. `script_1010.cs2` (the XP-drops **config panel**) — the `while ($int48 <= 24)` loops and `def_stat $statarray0(24)` become 25; the skill list in the counter/progress-bar dropdowns then picks Summoning up from `enum_681`/`enum_680` automatically.

Measured: **all 70 xpdrops-touching scripts compile from the tree, 0 failures.** Trigger-list ceilings are not at risk (`RS_CS2_HOST_TRANSMIT_TRIGGER_MAX = 32`; script 393's longest list is 25 varps).

Server side: `xp_drops` is mounted by `server/scripts/interface_orbs/scripts/orbs.rs2:41-68` behind `%xpdrops_enabled` — nothing to change.

### 6.2 Level-up

Server: one line, in the ported lane rather than the shared file:

```
// server/scripts/ported_scape2009_summoning/scripts/summoning_skill.rs2
[advancestat,summoning]
if (%summoning_enabled = 0) { return; }
@levelup;                      // the shared label in levelup/scripts/levelup.rs2
```

`ToriRSServer_CombatAddXp` (`torirs_server_combat.c:469-508`) fires `SS_TRIGGER_ADVANCESTAT` with the stat as subject on any base-level change; the shared `[label,levelup]` does `mes("You feel yourself getting stronger."); ~summary_combat_level_push;`.

**Be explicit that the level-up card is a pre-existing gap, not a Summoning bug.** Measured: `interfaces/levelup_display` (interface 233) exists with per-skill children (`57=sailing`, `58=com_58`), and **no clientscript and no `.rs2` in the tree references `interface_233`.** No skill gets a level-up card today. `SS_OP_MIDI_JINGLE 2064` likewise has a name in `torirs_server_wire.c:1282` and no handler. Add `59=summoning` + a `[summoning]` block to `levelup_display.{if,compack}` for parity (2-line diff, mirrors `[sailing]`: `type=0 width=479 height=96 layer=15269888 hidden=yes`), and record the gap in the queue rather than trying to close it inside this slice.

---

## 7. The summoning orb

**Yes, authorable, and it is the lowest-risk piece of the whole design** — the minimap chrome is a *cache record*, not CS2-generated.

**Interface 160 = `orbs`**, mounted into `toplevel_osrs_stretch:orbs` (161:33) by `server/scripts/player/configs/gameframe.enum`. 57 components today, ids 0..56.

Measured layout (the descending arc):

| block | x,y | w×h | onload |
|---|---|---|---|
| `xp_drops` | 0,17 | 27×27 | 1039 |
| `orb_health` | 0,37 | 57×34 | 446 |
| `orb_prayer` | 0,71 | 57×34 | **82** |
| `orb_runenergy` | 10,103 | 57×34 | 447 |
| `orb_specenergy` | 32,128 | 57×34 | **2069** |
| `orb_store` | 85,143 | 34×34 | 2396 |
| `orb_contentrecom` | 54,163 | 34×34 | 2480 |

**Implemented correction:** `(54,158)` is behind the fixed client's side-tab strip; a framebuffer
proved nearly the entire orb was occluded. The visible target position is **`x=89,y=128,57×34`**,
immediately right of `orb_specenergy`, with no tab overlap. The clientscript still hides the two
inert store/content-recommendation layers in the feature cache.

The implemented visual is more faithful than the proposed spec-orb copy: it ports the exact six
visible components of rev-530 interface 747 and adds one target-only transparent call button.
Eight new components occupy ids 57..64:

```
57=summoning_orb                type=0  x=89 y=128 57×34
58=summoning_orb_backing        type=5  source sprite 1206 → target 20001
59=summoning_orb_indicator      type=5  source sprite 1244 → target 20002
60=summoning_orb_empty          type=0  source-sized clipping layer
61=summoning_orb_empty_contents type=5  source sprite 1245 → target 20003
62=summoning_orb_text           type=4  dynamic stat-24 points
63=summoning_orb_icon           type=5  source sprite 1200 → target 20000
64=summoning_orb_button         type=0  op1 "Call familiar"
```

`onload` calls authored clientscript 12004, which registers a stat-24 transmit hook, applies the
modern 57x34/right-hand-26px component geometry and device-aware hover frame, then lets the target's
generic `orbs_update` helper draw the server-owned 0..60 familiar special points. Transmitted active
and special varps drive the clientscript, which hides the orb when no familiar is active.
Clientscript 12000 remains packed as the legacy source-31px
stat-points implementation.

The clientscript is short, because `~orbs_update` already exists. From `script_82.cs2`:

```
def_int $int8 = stat_base(stat_24);
~orbs_update($fillComponent, $textComponent, stat(stat_24), $int8);
```

That is it: **summoning points are the *dynamic* level of stat 24 and max points are the *base* level** — exactly 2009scape's model (`Skills.SUMMONING` dynamic = current points, `ObeliskOptionPlugin.java:38` "renew points" is literally `setLevel(SUMMONING, staticLevel)`), and it rides the ordinary `UPDATE_STAT` packet with zero new wire. Points must be excluded from `SkillRestore`-style regeneration, which this tree does not have — nothing to do.

The special-move energy bar (0..60) is a separate scalar with no stat. Drive it from a server varp declared in the ported lane (`%summoning_special_points`, `transmit=yes`), read as `%var<id>` in the orb's clientscript — measured: `%var<id>` is a legal varp literal in this dialect (`%var0`, `%var1009`, … appear throughout `scripts/`).

Arming, in the ported `.rs2`, reuses the already implemented call action:
```
if_setevents(orbs:summoning_orb_button, 0, 0, ^if_event_op1);
[if_button,orbs:summoning_orb_button]
~summoning_call_familiar;
```
The real client sends interface-160 component-64 op1 and the server responds by calling the owned
Spirit wolf. Scroll activation remains Phase 5 rather than being faked in the orb slice.

⚠️ **`orbs` is a shared record.** The orb ships to every player of that bake. Gate it with `if_sethide` from the clientscript on the same varp 4940 bit, following the `xp_drops` precedent (not in `gameframe.enum` at all; mounted by `~xpdrops_sync_mount` behind `%xpdrops_enabled`).

*(For the record: rev 530's own orb 747 has **no** special-move button — it is structurally identical to the hitpoints orb, and the special button lives on interface 662, the familiar tab. Putting both on the orb is a deliberate improvement, not a port.)*

---

## 8. Test plan

### Spike 0 — prove the three unexercised mechanisms *before* writing content
| # | spike | pass condition |
|---|---|---|
| 0a | rank-1 enum overlay: a throwaway `server/scripts/_spike/configs/x.enum` with `[enum_681] val=25,24`; `cachepack pack --base cache.osrs239 --out /tmp/spike --rev osrs239 --types enum` | `enum_681` in `/tmp/spike` has **25** rows, key 25 → 24; no other enum changed |
| 0b | `pack/dbrow.client` add path: `cachepack membership --types dbrow`, name two throwaway rows, pack | `cachepack pack` exits 0 (no cell-(c) error) and the rows appear in the baked cache's group 38 |
| 0c | `tools/gen_dbindex.py` | regenerating all 294 `dbindex/*.dbi` produces a **byte-identical** `git diff` |
| 0d | new-sprite id ≥ 20000 with a slashed pack name | `cache.osrs239.baked` idx8 contains archive 20000; `find_named --sprite 20000` decodes |

Any spike that fails changes the design, not the schedule.

### Unit / build gates
```sh
make -C src test-content        # test-content-register, test-servercodec, test-ss-symbols,
                                # torirsserver-scripts, torirsserver-servpack, test-membership,
                                # torirsserver-pack, test-server-clean, test-port
make -C src test-ToriRSServer        # ./src/build/torirsserver --selftest
make -C src test-torirsserver-coverage
make -C 3rd/rscache test
src/build/ToriRSServer_Pack --check-only     # must be 0 errors
```

New assertions to add (each must be proved able to fail by mutating the impl — the `verify-blocker` rule):

| assertion | where |
|---|---|
| `TORIRSSERVER_STAT_COUNT == 25` and `stat.pack` names exactly ids 0..24 with no gap | `ToriRSServer_Pack` validator |
| every `pack/stat.pack` name is unique across **all** namespaces (`summoning`, `sailing`) | `ToriRSServer_Pack` validator — this is the `[advancestat]` mis-resolution guard |
| `ToriRSServer_CombatLevel()` is invariant under `stat_level[24] = 1..99` | `torirs_server_world.c` selftest |
| `~player_combat_level` is invariant under `stat_advance(summoning, 13034431)` | a `::debugproc` in the ported lane |
| `enum_681` keys are contiguous 1..N in the baked cache | `ToriRSServer_Pack` or a `test-port` check |
| every `skill_features` / `skill_guide_subsections` row is present in `dbindex_213`/`212` | `tools/gen_dbindex.py --check`, wired into `test-port` |
| CS2 compile gate: every edited `.cs2` compiles standalone | a `make -C src test-cs2-edited` target running `cs2 compile` over the edited ids, requiring `failed 0` |
| stat 24 is not transmitted when `feature_summoning == 0` | `test-torirsserver-embed` — flip the flag, award xp, assert no `UPDATE_STAT` with `stat=24` on the wire |
| saves round-trip: a 24-stat save loads on a 23-stat build and vice versa | `ToriRSServer_Save` selftest |

### Headless screenshot assertions

```sh
SDL_VIDEODRIVER=dummy TORIRSSERVER_VERBOSE=1 TORIRSSERVER_SAVES=$SCRATCH/saves \
TORIRS_SIM_HOOK=... TORIRS_EXIT_BMP=build/summ_tab.bmp \
  src/build/torirs --manifest manifest_osrs239_summoning.ini --user testc --pass test
```
(use a scratch `TORIRSSERVER_SAVES` — headless runs are not independent)

| # | scenario | assertion |
|---|---|---|
| S1 | open the stats tab, flag **on** | `build/summ_tab.bmp` shows 25 cells; the row-9 cell at (64,209) is non-blank; pixel-diff vs a golden |
| S2 | same, flag **off** (pristine `manifest_osrs239.ini`) | 24 cells, `[universe]` unchanged, no row 9 |
| S3 | flag on, `content_restrict_summoning` = 1 | Summoning cell carries the two lock plates; `Total level:` string equals S2's |
| S4 | `::setlevel summoning 50`, reopen tab | cell reads `50/50`; Total level = S2's + 49 |
| S5 | right-click Summoning → "View Summoning guide" | interface 860 mounts; title reads `Summoning - Overview`; guide title icon is sprite 20002 |
| S6 | click the "Familiars" tab in the guide | ≥1 row renders; row text matches the authored dbrow; `%varcint1173 = 1` |
| S7 | award Summoning xp | an XP droplet appears with the Summoning icon; the counter panel lists Summoning |
| S8 | boot with the orb | a 5th 57×34 orb at (54,158); its number equals `stat(24)`; `orb_store`/`orb_contentrecom` hidden |
| S9 | click the orb's special-move button | server sees `[if_button,orbs:summoningbutton]` (assert via `TORIRSSERVER_ECHO_MES`) |
| S10 | **flag-off client, flag-on server** | award Summoning xp; assert the client logs nothing, does not crash, and `RS_PlayerStats` slot 24 stays 0 (the transmit gate held) |

Triage order when a panel is blank, in this order: `TORIRS_DUMP_TREE_EXIT=1` → `TORIRS_DUMP_BOUNDS=320` → `TORIRS_DUMP_SETSIZE` → `TORIRS_CS2_TRACE=1`.

### Manual checklist
1. `cachepack pack` summary line read in full — **`codec-declined` and `script failed` must both be 0.** A failed CS2 compile ships base-cache bytes and is otherwise indistinguishable from success.
2. `diff <(cachepack --list-assets on cache.osrs239.baked)` vs pristine for archive 12 script 393 — confirm the edit physically landed.
3. Fixed / Resizable-Classic / Resizable-Modern: the stats tab renders in all three sidebars (161 / 548 / 164 all use 190×261 side slots — verify, do not assume).
4. Log out and back in: Summoning level and xp persist (`saves/<user>.ini` `[stats] 24 = <boosted> <xp_tenths>`).
5. Hover the Summoning cell → tooltip shows `Summoning XP` / `Next level at` (script 395 is generic; no edit needed — confirm).
6. Total level and Total XP tooltips include Summoning when unlocked, exclude it when restricted.
7. Combat level printed by `~summary_combat_level_push` is unchanged at Summoning 1 and Summoning 99.
8. Boot `run-osrs239.sh` (RuneLite) with the flag **off** and grind xp for 5 minutes — no exception in the client log. Then with the flag **on**, confirm RuneLite either handles or is intentionally unsupported, and write the answer into the runbook.

---

## 9. Governance — this reverses written policy

Four files skip-list Summoning and must be amended in the same change, or the agent loops will keep deleting the lane:

| file:line | text |
|---|---|
| `docs/PORTING_GUIDE.md:35` | *"Never copy rev-530 ids; skip bots/holiday/**Summoning**/RS2-only"* |
| `docs/PORTING_GUIDE.md:683` | *"(bots, holiday events, **Summoning**, Fist of Guthix, …)"* |
| `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65` | `| content/global/skill/summoning/**, Wolf Whistle | Summoning is not in OSRS |` |
| `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:68` | `| Evil Turnip / summoning-linked patches | Summoning ecosystem |` |
| `docs/SKILLS_CONTENT_PORT_QUEUE.md:101` | `| Summoning / Fist of Guthix / RS2-only | not in OSRS |` |

Also: `SKILLS_CONTENT_PORT_QUEUE.md` claims *"Audit roster complete (23/23)"* at lines 148 and 348 — Summoning is row 24 and that claim breaks. The former `CLAUDE.md` question is settled: do not restore an agent-specific file; stale citations were deleted by explicit user direction.

Add a `docs/SUMMONING_PORT.md` topic doc and a `port/summoning_530.map` ledger under `make -C src test-port`, per §7's "docs are part of done".

---

## 10. Ordered slice list

| # | slice | gate |
|---|---|---|
| 0 | doc amendments (§9) + Spike 0 (§7) | spikes pass |
| 1 | `TORIRSSERVER_STAT_COUNT = 25`; `stat.pack` `23=sailing`, `24=summoning`; transmit gate; uniqueness validator; `revpacket.h:116` comment | `test-content`, `test-ToriRSServer`, save round-trip |
| 2 | `tools/gen_dbindex.py` + byte-identical regeneration of all 294 | `git diff` empty |
| 3 | sprites 20000-20003 + `pack/8_sprites.pack` | archive 20000 in the bake |
| 4 | enum overlays in the ported lane (`681, 108, 680, 255, 5917, 1497, 1505`) | S2 unchanged, `enum_681` has 25 contiguous keys |
| 5 | `stats.if`/`.compack` 25th cell + `script_393.cs2` pitch | S1, S4 |
| 6 | varbit namespace growth + `script_8950/8951` gate | S3 |
| 7 | `pack/dbrow.client` + guide dbrows + `skill_guide.rs2`/`.constant` | S5, S6 |
| 8 | `script_1003/1004/1010` XP drops | S7 |
| 9 | `[advancestat,summoning]` + `levelup_display` child | level-up message fires |
| 10 | summoning orb in `orbs.if` + clientscript + arming | S8, S9 |

Slices 1-5 give a Summoning skill you can `::setlevel` and see in the tab. That is the "real, drawable 24th skill" milestone; 6-10 are the rest of the surface.

---

## Residual risks

1. **`pack/dbrow.client` is the first use of the add path.** All five existing `.client` files are empty. Spike 0b is not optional.
2. **`dbindex` has no regenerator today** and a missing index row is invisible with no error. Slice 2 is a prerequisite, not a nicety.
3. **`enum_681` contiguity is a silent cliff** — a gap truncates the total-level loop for every later skill.
4. **`CACHEPACK_CS2_NAMES` is machine-local and undeclared**; without it every edited script silently reverts to base bytes.
5. **The `cachepack unpack --assets=scripts` refresh will clobber `script_73.cs2` / `script_7304.cs2`** and trip `check_crystal_set_contract.py`. Unpack selectively or re-apply the two comments.
6. **A real Jagex/RuneLite 239 client will fault on stat 24.** The transmit gate in §2 is a correctness requirement, not a convenience.
7. **The 190×261 sidebar slot is hard.** If the pitch-26 grid reads badly, fallback 2 (a dedicated clientscript for the Summoning cell only) is the escape hatch; do not try to grow `[universe]`.
8. **`TORIRSSERVER_VARP_COUNT = 6217` is already exceeded by `pack/varp.alloc` (6225)** and over-range writes are silently dropped. Not caused by this work, but the Summoning port will allocate varps into that region — fix it before slice 10.
9. **Everything above is the *skill*.** The familiars, pouches, scrolls, obelisks, the familiar sidebar tab (161 has `side0..side13` and **no spare slot**), and the rev-530 → osrs239 asset transcode are separate design questions with their own, larger, risks — notably that no `rs530` cache profile exists and `cachepack` cannot unpack RS2 sharded configs at all.

===== DESIGN: design-server-content =====
# Summoning in `3draster` — gameplay expression + engine work breakdown

Design answer to: *how is Summoning expressed in the ServerScript/config content model, and what engine work does it force?*

I re-measured the load-bearing claims from recon before designing. **Five recon findings are wrong or stale** and change the plan — they are corrected inline and listed in §0.

---

## 0. Facts I verified, and the four decisions they force

| Measured | Evidence | Consequence |
|---|---|---|
| `enum_681` key 24 → **stat 23 = Sailing** | `configs/all.enum` `[enum_681] val=24,23` | **Summoning is stat 24, not 23.** 2009scape's `SUMMONING = 23` is a direct collision. |
| `pack/stat.pack` ends at `22=construction` | verified, 23 lines | `23=sailing` is *missing* and must land **first**, or key 24 lands on a nameless hole. |
| `RS_PLAYER_STATS_SKILL_COUNT 25` | `src/game/rs_player_stats.h:11` | Client already addresses stat 24. Stat 24 is the **last** slot — no headroom after. |
| `TORIRSSERVER_STAT_COUNT = 23` | `src/torirsserver/torirs_server.h:558` | → 25. `stat_dirty` is `uint32_t`; 25 bits fits. |
| **CORRECTED:** NPC_INFO v5 has a 16-bit per-client index, then a 14-bit initial definition | rev239 writer/reader regression: index 321 + high definition 20000 | `content_register.c:63`'s `server_base = 20000` is valid. Type 20000 uses the same-packet extended/update + mask-`0x1` transformed-16-bit replacement. No roster tiering or id budget follows from the direct initial-field width. |
| `script_8950` `case 23 → ~script8951(1) → ~script607 → %varbit18166`, and varbit 18166 is named **`content_restrict_sailing_serverside`** | `scripts/script_8950.cs2`, `configs/all.varbit.compack:16780` | **The feature flag already exists as a shipped, cache-native, per-skill, server-driven pattern.** Recon said "no content feature-flag mechanism exists" — wrong. Consumed by `script_393` (lock overlay) and `script_1007`/`1008`/`1320` (total level / total xp / F2P level), all as `if (~script8950($stat) = 0)`. |

**Four corrections to recon** beyond those:

1. **varp headroom is fine.** `TORIRSSERVER_VARP_SERVER_HEADROOM = 1024` (not 512), ceiling 6729, high-water `6225=bank_wornview`. ~500 free varps. Recon's "already past the ceiling" is stale.
2. **`[namespace:varbit] ids = cache`** (`content.ini`) — varbits **cannot be allocated** today. This is the real blocker for the feature flag, not the absence of a mechanism.
3. **The BoB inv is cheaper than recon claims.** `torirs_server_bank.c:127 load_inv_sizes` reads inv sizes *from the cache disk*, not from a content walker. Baking the inv record into `cache.osrs239.baked` is sufficient for the server — **no `.inv` walker in `torirs_server_content.c` is needed.** `content_register.c:69` already gives inv `server_base = 2000` (cache max 1025).
4. **No natural boosted-level restore tick exists in ToriRSServer** (`grep stat_boosted` — only explicit `stat_heal`/`stat_boost`/HP sync). So summoning points naturally *don't* regenerate, which is the correct behaviour, for free. Leave a comment so a future restore tick excludes stat 24 alongside prayer.

### Decisions

- **D1. Summoning = stat 24.** Summoning *points* = `stat_boosted[24]`, max = `stat_base[24]`. No separate point varp: points ride `UPDATE_STAT` for free, exactly as 2009scape does, and the orb can read `stat(summoning)` directly.
- **D2. The flag is `script_8950 case 24` + a new `content_restrict_summoning_serverside` varbit**, set from a single `^summoning_enabled` constant at `[login]`. Not a new engine seam. `src/features/features.h` is **not** touched — it is a client-era table keyed on cache lineage (`features.c:190` discards revision), and Summoning is not an era divergence.
- **D3. Familiars are owner-bound NPCs.** This is the one genuinely new engine relation and it needs 3 opcodes in the `11000+` extra band (next free: **11022**).
- **D4. Tier 1 is 10 familiars, chosen to cover every mechanic category exactly once**, and deliberately excludes the two categories that are blocked on unrelated work (target-picking specials → the PvP/spell secondary-player dialect; wilderness transform forms → no wilderness).

---

## 1. The literal content tree

Provenance is marked three ways, because a directory name is invisible to `sscompile`, `cachepack` and `ToriRSServer` (all three walk recursively and only skip a leading `.`): (a) the folder name, (b) a `PROVENANCE.md`, (c) the mandatory `// Policy: 2009scape <Class>.java` header on every `.rs2` — the existing house convention (295 files already carry a `2009scape` header).

```
OSRS-Content/osrs239-content/
├── content.ini                                  # EDIT: +[namespace:obj|inv|seq|spotanim|struct]
│                                                #        membership = authored
│                                                #       [namespace:varbit] ids = cache -> server
├── fields/
│   └── inv.ini                                  # NEW: declares `size` etc. (default is scope=server/client=drop,
│                                                #      so `size` must say scope = client)
├── pack/
│   ├── stat.pack                                # EDIT: +23=sailing  +24=summoning
│   ├── obj.client        obj.server             # NEW  (cachepack membership --types obj)
│   ├── inv.client        inv.server             # NEW
│   ├── seq.client        spotanim.client        # NEW
│   ├── npc.client                               # EDIT: +10 familiars, +pikkupstix
│   ├── loc.client        loc.server             # EDIT: +summoning_obelisk
│   ├── varp.server                              # EDIT: +8 summoning varps
│   ├── npc.alloc  obj.alloc  loc.alloc          # NEW — written by ss_allocate.py (see slice E4)
│   │   seq.alloc  spotanim.alloc  inv.alloc     #       (npc.alloc base remains 20000 — see §5 correction)
│   ├── 7_models.pack  8_sprites.pack            # EDIT: ported ids, named `ported_2009scape/summoning/...`
│   ├── 0_animations.pack  1_skeletons.pack      # EDIT
│   └── 3_interfaces.pack                        # EDIT: 969=summoning_side  970=summoning_bob  971=summoning_infuse
│
├── configs/all.varbit  all.varbit.compack       # EDIT: +content_restrict_summoning_serverside (+ tab/timer varbits)
├── configs/all.enum    all.enum.compack         # EDIT: enum_681 val=25,24 · enum_108 valstr=25,Summoning
│                                                #       enum_255 val=24,<icon> · enum_5917 · enum_1497 val=24,1
├── configs/all.inv     all.inv.compack          # EDIT: +[summoning_bob] size=30   (rank 0 = machine-owned; see §10)
│
├── interfaces/
│   ├── orbs.if  orbs.compack                    # EDIT: +[orb_summoning] child 57 (copy of orb_prayer geometry)
│   ├── stats.if  stats.compack                  # EDIT: 25th cell — needs [universe]/[total] geometry change
│   ├── toplevel_osrs_stretch.{if,compack}       # EDIT: side14/stone14/icon14  (no spare slot exists)
│   └── ported_2009scape/
│       ├── summoning_side.if  .compack          # NEW  (interface 969) — familiar panel
│       ├── summoning_bob.if   .compack          # NEW  (970) — beast of burden
│       └── summoning_infuse.if .compack         # NEW  (971) — pouch/scroll creation
│
├── scripts/                                     # CS2 (client), all rank-0, machine-owned
│   ├── script_8950.cs2                          # EDIT: case 24 : return(~int_to_bool(%varbit<restrict>))
│   ├── script_912.cs2                           # (read-only) tab visibility = if_hassub, no edit needed
│   └── ported_2009scape/                        # NEW authored CS2 (first ever in this tree — see risk R6)
│       ├── script_12000.cs2                     # summoning_side builder (cc_create cells)
│       ├── script_12001.cs2                     # bob cell builder
│       └── script_12002.cs2                     # infuse list builder
│
├── models/ported_2009scape/summoning/*.model    # raw dat2 payloads, path IS the pack name
├── animsets/ported_2009scape/summoning/*.anim
├── framemaps/ported_2009scape/summoning/*.base
├── sprites/ported_2009scape/summoning/<name>/{0.bmp,pack.meta}
├── synth/ported_2009scape/summoning/*.synth
│
├── port/
│   ├── summoning530.map                         # NEW ledger: 530 id -> 239 id -> disposition
│   └── PROVENANCE.md                            # NEW
│
└── server/scripts/ported_2009scape_summoning/
    ├── PROVENANCE.md                            # "2009scape rev-530, Server/src/main/content/global/skill/summoning/**"
    ├── configs/
    │   ├── summoning.constant                   # ^summoning_enabled, ^summoning_special_max = 60,
    │   │                                        # ^summoning_special_regen = 15, ^summoning_regen_ticks = 50,
    │   │                                        # ^summoning_recall_range = 12, ^summoning_special_range = 15
    │   ├── summoning_runtime.varp               # the 8 varps (§4)
    │   ├── summoning.param                      # oc_param on pouches: level, npc, cost, lifetime, bob_size
    │   ├── summoning_pouch.dbtable              # the master table (§4)
    │   ├── summoning_pouch.dbrow                # 10 rows in tier 1
    │   ├── summoning_scroll.dbtable/.dbrow      # scroll -> pouch, xp, spec cost
    │   ├── summoning_charm.dbtable/.dbrow       # charm tiers
    │   ├── summoning_objs.obj                   # pouches, scrolls, charms, shards, blank pouch
    │   ├── summoning_npcs.npc                   # 10 familiars + pikkupstix (walkanim/readyanim explicit — see §13)
    │   ├── summoning_locs.loc                   # obelisk
    │   └── summoning.enum                       # pouch-slot -> pouch obj (drives the infuse list)
    └── scripts/
        ├── summoning_core.rs2                   # summon / dismiss / tick / call / login / logout / death
        ├── summoning_points.rs2                 # drain, obelisk renew, potions
        ├── summoning_infuse.rs2                 # pouch + scroll creation
        ├── summoning_special.rs2                # special engine + button arming
        ├── summoning_bob.rs2                    # beast of burden container + interface
        ├── summoning_tab.rs2                    # [if_open,summoning_side], buttons, HUD sync
        ├── summoning_familiars.rs2              # the 10 per-familiar behaviours
        ├── summoning_pikkupstix.rs2             # dialogue + shop
        └── summoning_debug.rs2                  # ::summon, ::sumpoints — the permanent headless proof
```

Two structural notes:

- **`cachepack` config merge roots are `{"configs", "server/scripts"}`** (`cp_pack.c:1669`, four identical literals). So every `.obj`/`.npc`/`.loc`/`.param`/`.enum` under `server/scripts/ported_2009scape_summoning/configs/` is a **rank-1 overlay** and is picked up by the baker with **zero tool change**. Verified.
- **Asset pack names carry the path.** `cp_assets.c` walks `pack/<ns>.pack`, not the directory, and builds `<root>/<dir>/<name>.<ext>`. So `pack/7_models.pack` line `100000=ported_2009scape/summoning/spirit_wolf` resolves to `models/ported_2009scape/summoning/spirit_wolf.model`. This is already in use (`models/npc/…`, `models/idk/`) — no tool change.

---

## 2. The feature flag — three layers, one source of truth

Recon proposed inventing a flag. Don't: the cache ships one.

**Layer 1 — the bake (asset presence).** A ported record reaches the client cache only if `pack/<ns>.client` names it *or* the base cache already holds its id (`cp_pack.c:690-760`, the "substrate clause"). Every summoning record is new ⇒ every one needs a membership line. Therefore **`manifest_osrs239.ini`, which boots the pristine `cache.osrs239`, is the flag-off client for free.** No build-time script exclusion is needed, and none is legal (`.cursor/rules/no-park-sibling-content.mdc`, `alwaysApply: true`, forbids `.skip` parking).

**Layer 2 — the server.** One constant:

```
// server/scripts/ported_2009scape_summoning/configs/summoning.constant
^summoning_enabled = 1
```

read by `[proc,summoning_enabled]` and asserted at the top of every entry point. Flipping it is `make -C src torirsserver-scripts` — no C, no env var, no namespace. This satisfies PORTING_GUIDE §2.4 items 2–3 (no game-facing constant in C).

**Layer 3 — the client tab/totals.** The `[login]` script writes the shipped gate varbit:

```
[login]
%content_restrict_summoning_serverside = calc(1 - ~summoning_enabled);
```

and `scripts/script_8950.cs2` grows one case, mirroring Sailing exactly:

```
[proc,script8950](int $int0)(int)
switch_int ($int0) {
	case 23 : return(~script8951(1));
	case 24 : return(~int_to_bool(%varbit<content_restrict_summoning_serverside>));
	case default : return(0);
}
```

Gate returns 1 ⇒ `script_393` draws the two 90%-transparent lock overlays on the stats cell, and `script_1007`/`1008`/`1320` skip the skill from total level / total xp / F2P total. All three consumers are already written; we add no client logic.

**Cost:** `[namespace:varbit] ids = cache` must become `ids = server` (base 25000 is already in `content_register.c:171`; cache max is 20410). This is PORTING_GUIDE §2.4 item 4 — *"a namespace that cannot grow is a bug, not a constraint."* It is a register edit, not an engine one.

**Asymmetry to respect (from rs-feature-flags, and correct):** a flag-*on* client against a flag-off server is benign. A flag-*off* client against a flag-on server is not — `IF_OPENSUB` on interface 969 which that cache has no group for is an unverified failure mode. **Rule: one embedded server per manifest; only `manifest_osrs239_summoning.ini` sets `^summoning_enabled = 1`.** Verify the missing-interface path (§Risks R2) before relying on anything else.

---

## 3. The 24th stat

Two slices, in this order.

**S1 — Sailing first.** `pack/stat.pack` `+ 23=sailing`. This is a standalone correctness fix: `enum_681` already spends key 24 on stat 23 and the tree has never named it. Land it alone, prove `make -C src test-content` stays green, then build on it.

**S2 — Summoning.**

| file | edit |
|---|---|
| `pack/stat.pack` | `24=summoning` |
| `src/torirsserver/torirs_server.h:558` | `TORIRSSERVER_STAT_COUNT = 23` → `25`. Widens `stat_level/boosted/xp_tenths[]` and npc `stat_drain[]`. `stat_dirty` is `uint32_t` — 25 bits fits. |
| `configs/all.enum` | `[enum_681] val=25,24` · `[enum_108] valstr=25,Summoning` · `[enum_680] valstr=24,Summoning` · `[enum_255] val=24,<sprite>` · `[enum_5917] val=24,<silhouette>` · `[enum_1497] val=24,1` |
| `interfaces/stats.{if,compack}` | 25th cell + `[universe]` / `[total]` geometry (see §Risks R4) |
| `server/scripts/levelup/scripts/levelup.rs2` | `[advancestat,summoning]` → `[label,levelup]` |
| `server/scripts/interface_skill_guide/` | `^skill_guide_summoning = 25`, `if_setevents(stats:summoning,…)`, `[if_button2,stats:summoning]` |

Three hazards, all measured:

- **`enum_681` must have no key gap.** `script_1007`/`1008`/`1320` walk from key 1 until `null`. Adding key 26 without 25 silently truncates total level for every skill after the hole.
- **`[advancestat,summoning]` resolves unhinted.** `ssc_compile.c:2286` resolves trigger subjects with `SSC_SYM_UNKNOWN`. It is safe *today* (no `summoning` symbol exists in any pack) and becomes a silent mis-resolution the moment a summoning obj/npc/loc is named exactly `summoning`. **Rule for this port: never name any record bare `summoning`.** Prefix everything (`summoning_pouch_*`, `summoning_obelisk`).
- **Save compatibility.** `torirs_server_save.c:491` rejects `stat >= TORIRSSERVER_STAT_COUNT` on load, and `:207` skips untouched stats on write. Widening 23→25 is forward-compatible; narrowing back is not. Assert this in `ToriRSServer_Save`'s selftest.

---

## 4. Data model — where each number lives

**Server-side truth: one dbtable.** Server dbtables are read from text by `ToriRSServer_DbLoad`, so there is **no `dbindex/*.dbi` hand-editing** (that is only for cache-side, CS2-read tables). This kills the recon's "single most fragile step".

```
// summoning_pouch.dbtable
[summoning_pouch]
column0=pouch,obj          column6=lifetime,int
column1=familiar,npc       column7=spec_cost,int
column2=level,int          column8=scroll,obj
column3=summon_xp,int      column9=bob_size,int
column4=infuse_xp,int      column10=peaceful,boolean
column5=point_cost,int     column11=combat,boolean      ← explicit, see below
                           column12=charm,obj  13=charm_count,int
                           column14=tertiary,obj  15=tertiary_count,int  16=shards,int
```

`combat` is **declared, not inferred**. 2009scape derives it from `NPCDefinition.forId(id+1).getName().equals(getName())` (`Familiar.java:167`) — a cache dependency that cannot survive the port, since we deliberately do not port wilderness `id+1` forms.

**Client-visible data: obj params**, not a new enum. `param` already has membership (`pack/param.client`, 44 live entries) and an allocator sweep in `ss_allocate.py`. Put `summoning_level`, `summoning_familiar`, `summoning_point_cost` on each pouch obj; the infuse-list CS2 reads them with `oc_param`. This reuses the proven path instead of exercising `pack/enum.client`, which has zero entries and has never been used.

**Player state: 8 varps**, all `scope=perm` so they persist (`torirs_server_save.c:260`), all `transmit=` per column. `pack/varp.server` gets 8 lines; `ss_allocate.py` mints ids from 6226.

| varp | transmit | holds |
|---|---|---|
| `summoning_familiar_npc` | yes | familiar npc type, `null` when none — the tab's model widget |
| `summoning_familiar_pouch` | yes | pouch obj, `null` when none |
| `summoning_ticks` | no | lifetime countdown (drives the varbits below) |
| `summoning_timer_display` | yes | carries `summoning_minutes` + `summoning_halfmin` varbits — the tab's clock |
| `summoning_special` | yes | 0..60 special points |
| `summoning_special_cost` | yes | current familiar's cost, for the bar |
| `summoning_bob_open` | no | is the BoB interface mounted |
| `summoning_frac_drain` | no | fixed-point drain accumulator (×1000) |

**Point-drain formula** (2009scape `Familiar.java:126-186`, a 2009scape invention but self-consistent): total points spent over a familiar's life == its level requirement.
`drain_per_life = level - point_cost + 1`, accumulated as `frac += drain*1000/lifetime` each tick, spending a point when `frac >= 1000`.

---

## 5. Familiar summoning — the content, and the one real engine gap

### 5a. The content

```
// summoning_core.rs2 — Policy: 2009scape SummonFamiliarPlugin.java, FamiliarManager.java:184-240
[opheld1,summoning_pouch_spirit_wolf]   ~summoning_summon(summoning_pouch_spirit_wolf);
[opheld1,summoning_pouch_dreadfowl]     ~summoning_summon(summoning_pouch_dreadfowl);
...                                     // 10 one-liners in tier 1

[proc,summoning_summon](obj $pouch)
if (~summoning_enabled = false) { mes("Nothing interesting happens."); return; }
def_dbrow $row = ~summoning_pouch_row($pouch);
if ($row = null) { return; }

if (stat_base(summoning) < db_getfield($row, summoning_pouch:level, 0)) {
    mes("You need a Summoning level of <...> to summon this familiar."); return;
}
if (%summoning_familiar_npc ! null) {
    if (%summoning_familiar_pouch = $pouch) { ~summoning_renew($row); return; }   // renew, no respawn
    mes("You already have a follower."); return;
}
def_int $cost = db_getfield($row, summoning_pouch:point_cost, 0);
if (stat(summoning) < $cost) { mes("You do not have enough summoning points."); return; }

inv_del(inv, $pouch, 1);
stat_sub(summoning, $cost, 0);
stat_advance(summoning, db_getfield($row, summoning_pouch:summon_xp, 0), false);
~summoning_spawn($row, $pouch);

[proc,summoning_spawn](dbrow $row, obj $pouch)
npc_add(~summoning_spawn_tile, db_getfield($row, summoning_pouch:familiar, 0), 0);  // 0 = we own the lifetime
npc_setowner;                                            // NEW 11022
npc_setmode(playerfollow);
npc_anim(null, 0); spotanim_npc(summoning_spawn_gfx, 0, 0);
%summoning_familiar_npc   = db_getfield($row, summoning_pouch:familiar, 0);
%summoning_familiar_pouch = $pouch;
%summoning_ticks          = db_getfield($row, summoning_pouch:lifetime, 0);
%summoning_special        = ^summoning_special_max;
%summoning_special_cost   = db_getfield($row, summoning_pouch:spec_cost, 0);
%summoning_frac_drain     = 0;
if (db_getfield($row, summoning_pouch:bob_size, 0) > 0) { ~summoning_bob_open_container($row); }
settimer(summoning_tick, 1);
~summoning_tab_mount;
sound_synth(summon_npc, 0, 0);
```

### 5b. The engine gap — owner-bound NPCs

**This is the only genuinely new entity relation the port needs.** Verified:

- `npc_run_mode` (`torirs_server_world.c:2752`) resolves the followed player as `srv->active_player`, with a comment at `:2773` admitting it: *"this mode machine asks 'whose turn is it' in a phase where it is nobody's … That is a separate defect."* `phase_npcs` never calls `ToriRSServer_WorldSetActive`, so during phase 4 `active_player` is whatever leaked from the previous tick. With `TORIRSSERVER_PLAYER_MAX = 8`, two summoners means both familiars follow the same arbitrary player.
- `run_trigger_script` (`torirs_server_scripts.c:1666`) sets `SSVM_ENT_PLAYER` from `srv->active_player` for **every** trigger including `[ai_timer]`/`[ai_queue]`/`[ai_spawn]` — so a familiar's own AI reads a stale player's varps.
- `npc_uid` has **no generation counter** (`torirs_server_scripts.c:4585` says so outright). A uid stashed across a despawn resolves to whoever took the slot.

**Per PORTING_GUIDE §2.4 item 5 and §4.4/§4.5 step 4** — the vocabulary the engine offers scripts may grow; a one-off C hook may not, and the opcode is logged in the queue's opcode-gap table *and implemented in the same slice*. Three new opcodes in the extra band (next free is **11022**, `ss_opcode.h:453`):

| opcode | id | signature | semantics |
|---|---|---|---|
| `NPC_SETOWNER` | 11022 | `()` | Bind the active npc to the active player. Stores the player's pid **and login generation**, so the binding cannot survive a slot reuse. |
| `NPC_OWNER` | 11023 | `()(int)` | Owner pid, `-1` if unowned. |
| `NPC_FINDOWNED` | 11024 | `()(boolean)` | Find the active *player's* owned npc, set it as the active npc, return whether one was found. **This removes the need for a uid varp entirely** and so dodges the uid-generation hazard rather than working around it. |

Engine changes (three, all small and all narrowing existing behaviour rather than adding a branch):

1. `struct ToriRSServerNpc` (`torirs_server.h:1563`) gains `int owner_pid; uint32_t owner_gen;` — zeroed by `npc_spawn`, so an unowned npc is unchanged.
2. `npc_run_mode` (`:2757`) resolves `player` from `owner_pid` **when set**, falling back to `srv->active_player` otherwise. This fixes the documented defect for owned npcs without touching any existing npc's behaviour, and it is the honest form of what `osrs230_mockserver.md §6.1` asks for.
3. `run_trigger_script` (`:1666`) prefers `npc->owner_pid` over `srv->active_player` when the trigger is an `ai_*` and the npc is owned.

Also required, and *not* an opcode:

4. **CORRECTED: retain npc `server_base = 20000`.** The 14-bit field is the initial definition,
   not a per-client NPC index. Type 20000 round-trips through the add's extended/update flag and
   update-mask `0x1`, whose replacement definition is transformed unsigned 16-bit
   `p2Alt3` / `UShortLEAdd`. `ss_allocate.py` may learn `npc`, but must not move the base to 16294
   or validate cache ids against the direct initial-definition width.

Deliberately **not** doing: per-player npc visibility. NPC_INFO derives visibility from `active` + range per player, so everyone sees everyone's familiar — which is correct for RS and needs no work.

Deliberately **not** doing: leashing. `maybe_aggress`/`target_within_maxrange` measure from the spawn tile, so a familiar would leash to where it was summoned. Familiars in tier 1 set `maxrange` high in the npc config; the engine stays untouched.

---

## 6. Lifetime, dismiss, logout, death, call

**One timer, not four.** `TORIRSSERVER_TIMER_MAX = 8` per player and existing content already competes for those slots. Decay, point drain, special regen, HUD sync and the two warnings all live in a single `[timer,summoning_tick]` at interval 1.

```
[timer,summoning_tick]
if (%summoning_familiar_npc = null) { cleartimer(summoning_tick); return; }
%summoning_ticks = calc(%summoning_ticks - 1);
~summoning_drain_points;                                   // frac accumulator, §4
if (calc(%summoning_ticks % ^summoning_regen_ticks) = 0) {
    %summoning_special = min(calc(%summoning_special + ^summoning_special_regen), ^summoning_special_max);
}
~summoning_hud_sync;                                       // minutes + half-minute varbits
if (%summoning_ticks = 100) { mes("<col=ef1020>Your familiar will vanish in 1 minute.</col>"); }
if (%summoning_ticks = 50)  { mes("<col=ef1020>Your familiar will vanish in 30 seconds.</col>"); }
if (%summoning_ticks <= 0)  { ~summoning_dismiss(^summoning_spill_bob); }
```

**Dismiss** — `[if_button1,summoning_side:dismiss]` opens the standard 3-option chatbox (`Dismiss Familiar / Yes / No`); op2 on the same button dismisses immediately, mirroring `SummoningTabListener.kt:38-54`.

```
[proc,summoning_dismiss](int $spill)
if (npc_findowned = true) {
    if ($spill = true) { ~summoning_bob_spill; }           // GroundItems at the familiar's tile
    npc_del;
}
%summoning_familiar_npc = null; %summoning_familiar_pouch = null;
%summoning_ticks = 0; %summoning_special = 0; %summoning_special_cost = 0;
cleartimer(summoning_tick);
~summoning_bob_close;
~summoning_tab_unmount;
```

**Logout / login — the asymmetry that matters.** 2009scape's logout calls `clear()`, *not* `dismiss()`, precisely so BoB contents survive (`Player.finishClear():378`). Getting this backwards loses a Pack Yak of items on every logout. Here:

```
[logout]  if (npc_findowned = true) { npc_del; }           // despawn the entity ONLY
[login]
if (~summoning_enabled = false) { %content_restrict_summoning_serverside = 1; return; }
%content_restrict_summoning_serverside = 0;
if (%summoning_familiar_npc ! null & %summoning_ticks > 0) { ~summoning_respawn; settimer(summoning_tick, 1); }
```

The varps are `scope=perm` and the BoB container is a `perm` container row, so the familiar survives a session boundary with its inventory. **Test it as open → logout → login → open**, not just open → logout.

**Death** — `[playerdeath]` (trigger 181, verified dispatched) → `~summoning_dismiss(true)`.

**Call familiar** — `[if_button1,summoning_side:call]` → `npc_findowned` + `npc_tele(coord)` (`NPC_TELE 2542`, implemented) + the summon spotanim. Free; no cost, matching `Familiar.call()`. The tick's recall (owner distance > 12) uses the same proc.

---

## 7. Summoning points

Points **are** `stat_boosted[24]`. This is the design's biggest simplification: no new storage, no new packet, and `stat(summoning)` / `stat_base(summoning)` work in both `.rs2` and CS2 with no engine change (`rs_cs2_host.c:5336` already bounds at 25).

- **Drain on summon**: `stat_sub(summoning, $cost, 0)`.
- **Drain per tick**: same op, driven by the accumulator.
- **Obelisk renew**: `stat_heal(summoning, stat_base(summoning), 0)` — restores to full. `SS_OP_SET_SKILL_LEVEL 2106` is unimplemented and is **not needed**; `stat_heal` covers it.
- **Potions** (`[opheld1,summoning_potion_4]` …): `stat_heal(summoning, calc(7 + stat_base(summoning) / 4), 0)` per `SummoningEffect.java` (`base 7 + level*0.25`). The super-restore mix and the special-restore side effect (`+15` special) are content one-liners.
- **No natural regeneration** — verified absent from the engine, which is the correct behaviour. Leave a comment at `torirs_server_combat.c:502` so a future restore tick excludes stat 24 alongside prayer.
- **Combat level**: all three implementations (`ToriRSServer_CombatLevel`, `[proc,player_combat_level]`, `RS_PlayerStats_RecomputeCombatLevel`) name their stats explicitly, so Summoning stays out by construction. 2009scape's `+staticLevel/8` term is **deliberately not ported** — it would change every existing player's combat level.

---

## 8. Pouch infusion, scrolls, charms

**The obelisk.** `[oploc1,summoning_obelisk]` = *Infuse-Pouch*, `[oploc2,…]` = *Renew-Points`. Options come from the loc record's `op1`/`op2` (`scope = client` in `fields/loc.ini`), so the verbs are cache-visible and the arming is the loc's own.

**Placement without maps.** Maps are out of scope, so the obelisk has nowhere to stand — *unless* it is spawned at runtime. Use `loc_add` from a zone/world-init script. This sidesteps `maps/` entirely (which is `CP_ASSET_ENCRYPTED` and would drag in `xteas.json` ownership) and is the honest answer to "locs but no maps".

**Infusion.** `[if_button1,summoning_infuse:slot<N>]`, amount from the op (`op1→1, op2→5, op3→10, op4→X, op5→All`) — the rev-230 idiom, replacing 2009scape's `sendRunScript(757/765)`, which is a rev-530 clientscript that does not exist here and cannot be ported (the CS2 command table is generated from RuneStar's **OSRS** opcodes; rev-530 numbering is a different language).

```
[proc,summoning_infuse](dbrow $row, int $count)
if (stat_base(summoning) < db_getfield($row, summoning_pouch:level, 0)) { ... return; }
// per unit: charm + blank pouch + tertiary + shards -> pouch
anim(summoning_infuse_anim, 0);
loc_anim(summoning_obelisk_charge);          // the loc the player interacted with, NOT a hardcoded coord
sound_synth(craft_pouch, 0, 0);
inv_del(...); inv_add(...);
stat_advance(summoning, db_getfield($row, summoning_pouch:infuse_xp, 0), true);
```

Note the bug **not** ported: `SummoningCreator.java:122` hardcodes the obelisk at `Location(2209,5344,0)` for the animation. Use the interacted loc.

**Scrolls**: 1 pouch → 10 scrolls, same interface in "transform" mode. **Fix the three source data bugs rather than copying them** (recon caught these and they are real): `DOOMSPHERE_SCROLL(…, -1)` leaves Karamthulhu scroll-less; `DEADLY_CLAW_SCROLL(…, 12162)` keys the Talon beast scroll to a *charm* not the pouch; `THIEVING_FINGERS_SCROLL` has xp `47` where neighbours are ≤ 8 (should be 0.9). Also `SummoningCreationPlugin.java:88` lists `28278` where it means `28728`. Record all four in `port/summoning530.map` as `corrected`.

**Charms.** Tier 1 ships the five charm objs (gold/green/crimson/blue/obsidian) and a **hand-seeded drop source only** — Pikkupstix's shop plus a `::givecharm` debugproc. The full charm drop table is 1,222 rev-530 npc ids across 179 entries keyed to a revision whose npc ids do not map 1:1 onto osrs239. That is a separate slice with its own id-translation ledger and belongs in tier 3.

---

## 9. Special moves

Energy: `%summoning_special` 0..60, `+15` per 50 ticks (2 minutes to full), per-familiar cost from `summoning_pouch:spec_cost`.

**Arming — nothing is clickable until `IF_SETEVENTS`**, and events are purged when the interface unmounts, so arming goes in `[if_open]`, which fires from `torirs_server_encode.c:933` on every mount:

```
[if_open,summoning_side]
if_setevents(summoning_side:call,    0, 0, ^if_event_op1);
if_setevents(summoning_side:dismiss, 0, 0, ^if_event_op1 | ^if_event_op2);
if_setevents(summoning_side:bob,     0, 0, ^if_event_op1);
if_setevents(summoning_side:special, 0, 0, ^if_event_op1);
~summoning_tab_refresh;
```

We speak the **v1 (i32)** mask even against a rev-239 cache — a v2 (i64) mask over a v1 packet arms nothing and reports no error.

```
[if_button1,summoning_side:special]  ~summoning_special_fire;

[proc,summoning_special_fire]
if (npc_findowned = false) { mes("You do not have a follower."); return; }
def_dbrow $row = ~summoning_pouch_row(%summoning_familiar_pouch);
def_int $cost = db_getfield($row, summoning_pouch:spec_cost, 0);
if (%summoning_special < $cost) { mes("You do not have enough special move points."); return; }
def_obj $scroll = db_getfield($row, summoning_pouch:scroll, 0);
if (inv_total(inv, $scroll) < 1) { mes("You do not have the right scrolls."); return; }
if (~summoning_special_dispatch($row) = false) { return; }        // per-familiar, may refuse
inv_del(inv, $scroll, 1);
%summoning_special = calc(%summoning_special - $cost);
anim(summoning_special_anim, 0); spotanim_pl(summoning_special_gfx, 0, 0);
stat_advance(summoning, ~summoning_scroll_xp($scroll), true);
```

**Tier 1 uses self-targeted specials only.** The target-picking family (`PacketProcessor.kt:449` treats interface 662 like a spellbook: pick a scroll button, then click an npc/player/ground item) needs the component-target-mask dialect for a *secondary* subject, which is the same thing that blocks the PvP secondary-player port (`SKILLS_CONTENT_PORT_QUEUE.md` opcode gap log). Tier 1 covers "damage the owner's current target" instead, which needs no target picking — see §11.

---

## 10. Beast of Burden

Containers here are LostCity's model: **resolve-or-create by inv id**, `torirs_server_container.h:57-96`, `containers[TORIRSSERVER_CONTAINER_MAX = 16]` per player. The size comes from `ToriRSServer_BankInvSize(inv_id)`, and — this is the load-bearing measurement — that reads **from the cache disk** (`torirs_server_bank.c:127 load_inv_sizes`), not from a content walker. So:

**What is actually needed** (much less than recon claimed):

1. `configs/all.inv` gains `[summoning_bob] size=30`, `all.inv.compack` gains `2000=summoning_bob`. Ids ≥ 2000 per `content_register.c:69` (cache max 1025). `configs/` is rank-0 and machine-owned, so this must be a **merge**, not a hand-truncate — see §Risks R5. The alternative that avoids touching rank 0 entirely is a rank-1 `summoning.inv` under `server/scripts/ported_2009scape_summoning/configs/`, which `cachepack`'s config roots already pick up; prefer this and prove it.
2. `content.ini` `+[namespace:inv] ids = server, names = cache, membership = authored`; `fields/inv.ini` declaring `size` as `scope = client`; `pack/inv.client` naming `summoning_bob` — without it the record is `cp_pack.c` **cell (c)**, a hard error.
3. `make -C src torirsserver-cache` bakes it, and **both the world and JS5 point at `cache.osrs239.baked`** (the one-cache rule). `load_inv_sizes` then sizes it; `ToriRSServer_ContainerResolve` stops returning NULL; all 27 `inv_*` opcodes work unchanged.
4. **No new `.inv` walker in `torirs_server_content.c`.** This is the simplification that makes BoB tractable in tier 1.

**The interface.** Rev 230 has **no `TYPE_INV`** — items are `cc_create`d type-5 graphics with `SETOBJECT`. So `interfaces/ported_2009scape/summoning_bob.if` is a container + a builder clientscript (`scripts/ported_2009scape/script_12001.cs2`) modelled on the bank's `script274` (`cc_create($c, 5, …) + cc_setoutline(1) + cc_setsize(36,32,0,0)`). Mount as **type 0 (modal)** so it blocks world input.

**Rules** ported from `BurdenBeast.java:85-109`: value > 50,000 refused; untradeable refused; `bankable` respected; rune/pure essence only for abyssal pouches. Foragers are withdraw-only.

**Spill on death/expiry, not on logout.** `dismiss()` drops the container to the ground; `clear()` does not. §6 keeps them separate.

---

## 11. Familiar combat

`torirs_server_combat.c` is strictly player ↔ npc: `ToriRSServerPlayer.combat_target` is an npc slot, `ToriRSServerNpc.combat_target` is a player pid, and **there is no npc↔npc loop anywhere**. Death credit is `death_credit_players[]`, player-indexed.

**Tier 1 does not add one.** A tier-1 combat familiar deals damage entirely from content: the special (or an `[ai_timer]`) reads the owner's current combat target and calls `npc_damage` — implemented, credited via the existing player-indexed path so the owner gets the xp and the drop, exactly as `Familiar.sendFamiliarHit` does (`target.getImpactHandler().handleImpact(owner, …)`). Impact delay `2 + dist/2`, range 8, all content arithmetic.

**Tier 3, if wanted**, is where an npc↔npc pairing lands: `ToriRSServerNpc.combat_target_npc`, a second approach/attack-clock path in `ToriRSServer_CombatNpcTick`, retaliation, and death credit routed through the owner. That is a substantial engine slice — it should be planned and costed on its own, not smuggled in under Summoning. Auto-assist (`Familiar.java:258-270`, gated on all three parties being in multi) is meaningless without wilderness/multi zones and is out of scope.

---

## 12. Tiering — 10 familiars, one per mechanic

This ten-familiar grouping may still be useful as a mechanics-validation order, but **not as an
id-budget tier**. The 14-bit value is only the direct initial definition; high definitions use
the same-packet extended update path and are not constrained by it. The settled Summoning scope is
all 82 familiars. Wilderness `id+1` forms stay
out because there is no wilderness in this content tree and `combat` is declared data.

| # | familiar | npc / pouch | lvl | mechanic category it *uniquely* proves |
|---|---|---|---|---|
| 1 | Spirit wolf | 6829 / 12047 | 1 | Baseline lifecycle: summon, timer, drain, dismiss, call. Self-target special (Howl). |
| 2 | Dreadfowl | 6825 / 12043 | 4 | **Invisible skill boost** (+1 Farming) — the `getBoost` read path. |
| 3 | Spirit spider | 6841 / 12059 | 10 | **Item-generating special** (Egg spawn → inventory). |
| 4 | Thorny snail | 6806 / 12019 | 13 | **Beast of Burden, smallest (3 slots)** — proves the container + interface with a trivial case. |
| 5 | Spirit kalphite | 6994 / 12063 | 25 | **Damage special against the owner's target** (BoB 6). First combat contact. |
| 6 | Beaver | 6808 / 12021 | 33 | **Forager** (passive production into a withdraw-only 30-slot BoB) + `peaceful` + Woodcutting +2. |
| 7 | Bull ant | 6867 / 12087 | 40 | Second BoB size (9) — proves size is data, not a constant. |
| 8 | Bunyip | 6813 / 12029 | 68 | **Passive tick heal** + **use-item-on-familiar** interaction. |
| 9 | Unicorn stallion | 6822 / 12039 | 88 | **NPC option on the familiar itself** ("Cure", costs 2 points) — the `[opnpc]` path. |
| 10 | Pack yak | 6873 / 12093 | 96 | **Capstone BoB (30)** + banking special. The one players actually want. |

**Covered by tier 1:** lifecycle, renew, points, drain, three BoB sizes, forager, invisible boost, passive heal, self-target special, item-gen special, damage special, npc-op special, peaceful flag, banking special, use-on-familiar.

**Deliberately excluded from tier 1, with the reason:**
- **Target-picking specials** — blocked on the secondary-subject component dialect (same blocker as PvP). Tier 3.
- **Wilderness transform forms** — no wilderness. Never.
- **Teleport familiars, remote view, Macaw/Fruit bat drops** — each needs camera/ground-spawn work orthogonal to Summoning. Tier 3.
- **Pets, incubator, Stealing Creation clay familiars, Phoenix** — a separate lifecycle (`Pet extends Familiar`, no timer, hunger/growth) that `FamiliarManager.parse` entangles with the familiar save blob. Explicitly out; stub the save shape so it can be added later without a migration.

**Next mechanics group (~20):** the remaining BoBs (Spirit terrorbird, War tortoise, Abyssal parasite/lurker/titan), the 6 minotaurs (one shared special), the 7 cockatrice variants (one shared special), the titans (Fire/Moss/Ice/Steel/Iron/Lava/Geyser/Swamp). This is an implementation ordering only; there is no NPC-id ceiling to fit inside.

**Tier 3:** target-picking specials, charm drop tables at scale (1,222 rev-530 npc ids needing translation), teleport/remote-view familiars, npc↔npc combat.

---

## 13. What "porting a loc/npc" mechanically means here

**NPCs to port (tier 1):** the 10 familiars above + **Pikkupstix** (tutor/shop). Pikkupstix goes first *because* he is the simplest proof of the whole path: one npc, one model, one dialogue, no follow, no rig retarget risk beyond his own.

**Locs to port:** `summoning_obelisk` (rev-530 loc 28716/28719/28722/28725/**28728**/28731/28734 — note 28728, which the 2009scape source typos as 28278). One loc record, placed by `loc_add` at runtime.

**Not ported: the Taverley training area / Wolf Whistle.** It is a dynamic region (11573) plus map data, both out of scope. The quest gate is replaced by the feature varbit.

**The mechanical steps for one npc:**

1. `port_npc --from-rev rs530 <2009scape/Server/data/cache> --to-rev osrs239 cache.osrs239 --npc 6829 --out /tmp/port530 --include-related-anims` — computes the asset closure (models, seqs, frames, framemaps) and a remap plan. **This is blocked until the rev-530 profile exists** (§14 phase B).
2. `cachepack unpack --cache /tmp/port530 --rev osrs239 --src <tree> --assets=models,animsets,framemaps` — lands the raw payloads.
3. Move them under `ported_2009scape/summoning/` and name them in `pack/7_models.pack`, `pack/0_animations.pack`, `pack/1_skeletons.pack` at ids from the declared bases (models 100000, animations 20000, skeletons 8000).
4. Author the record in `.../configs/summoning_npcs.npc` — with **explicit `walkanim=` / `readyanim=`, not `bas_type_id`**. `RSCache_Dat2ConfigBasEncode` exists in the library but **`bas` is not a cachepack type**, so BasType cannot be authored from the tree; every rev-530 familiar uses `bas_type_id` with `standing_anim = -1` and would T-pose. `fields/npc.ini` declares `walkanim`/`readyanim` as `scope = client`, so this is a one-line-per-npc workaround that costs nothing and removes an entire tool dependency.
5. `pack/npc.client` line + `pack/npc.server` line if it states any band field.
6. Id from `pack/npc.alloc`, base **20000**.
7. A row in `port/summoning530.map`: `530_id ⟶ 239_id ⟶ disposition`.

Same shape for a loc, plus `pack/loc.client`.

---

## 14. Implementation-ordered work breakdown

### Phase A — governance (no code; the plan is currently illegal under the repo's own docs)

| # | file | edit |
|---|---|---|
| A1 | `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65` | delete the `summoning/**`, Wolf Whistle skip row → convert to owned rows `36a…` |
| A2 | `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:68` | delete the Evil Turnip skip row (additive-only to the live `skill_farming/` tree) |
| A3 | `docs/PORTING_GUIDE.md:35`, `:683` | drop "Summoning" from both skip clauses |
| A4 | `docs/SKILLS_CONTENT_PORT_QUEUE.md:101`, `:148`, `:348` | drop the skip row; add audit row #24; correct the "23/23 complete" claims |
| A5 | `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:265-282` | log **all four** new opcodes (11022–11024) + the `npc.alloc` base fix, **before any C is written** (§2.4/§4.5 step 4) |
| A6 | `docs/SUMMONING_PORT.md` | new topic doc — "done" includes it (§7) |
| A7 | obsolete `CLAUDE.md` citations | remove; explicit user decision says the agent-specific file is intentionally absent |

### Phase B — cache toolchain (nothing lands in the tree until this works)

- **B1** `3rd/rscache/src/revisions/rev_dat2_rs530.c` + one row in `revisions.c:22`. Pin `FRAMEMAP_V3`, `LOC_RS2`, `FLO_RS2`. **Do NOT copy 643's `FRAME_V2` pin** — that is a rev-610 format; at 530 frames are V1 and `--rev 643` silently corrupts every animation frame. Any prior `port_npc` output produced with `--rev 643` has garbage framemap ids and must be redone.
- **B2** `RSCACHE_DAT2_COMPONENT_DECODE_ERA_530` — 530 is 643's type-6 rule with OSRS's type-5 rule; `dat2_component.c:245` derives the era from `IsRs2Dat2` alone and has no seam for a third value. On the critical path only if 530 interfaces are decoded; **avoidable in tier 1 by authoring the three interfaces fresh** (recommended — a ported 530 IF3 record's `graphic=`/`font=`/hook ids all point at rev-530 assets and scripts that do not exist here, so every field needs remapping anyway).
- **B3** Fix `3rd/rscache/tools/common/cache_write.c:545-580`: the cross-codec framemap branch calls `RSCache_Dat2FramemapEncode` **without clearing `has_transform_actor` / `has_masks` / `tail`**, so a V3→V1 port is a silent no-op that ships V3 bytes into a V1 cache. Familiars load with dead rigs and no error. Only `transcode.c:118` (the dat1 path) warns.
- **B4** 530 obj codec: 5,476 of 14,654 objs decode short (missing opcodes 96, 121/122, 125–130; 23/25 read without the trailing offset byte). Summoning pouches happen to decode, but any obj-adjacent crawl is reading truncated records. Needed before charm drop tables.
- **B5** *(defer, and A/B first)* `dat2_config_sequence.c:1139-1155` calls `RevisionAtLeastOsrs(default_when_unknown=true)`, which an `rs2` profile structurally cannot satisfy → always V3. 649 bad seqs at 530, **2,977 on `cache.void634`, 3,139 on `cache.rs727_preeoc`**. Fixing it changes 634/727 behaviour. Pre-existing; not a Summoning bug.
- **B6** `--texture-map` for every ported model. 530 has 680 material archives; osrs239's whole texture table is one archive. An unmapped textured face indexes into an unrelated osrs239 texture (`EXCEPTIONS.md` A5).

**Exit test:** `port_npc --from-rev rs530 … --npc 6829 --to-rev osrs239 --out /tmp/t` produces a plan with a *remapped* framemap id, and `find_named --framemap <new>` decodes.

### Phase C — the 24th stat

- **C1** `pack/stat.pack` `+23=sailing`. Alone. `make -C src test-content` green.
- **C2** `TORIRSSERVER_STAT_COUNT` 23→25; `pack/stat.pack +24=summoning`; save round-trip selftest in `ToriRSServer_Save`.
- **C3** `configs/all.enum` — the six enum rows (§3). **No key gap in `enum_681`.**
- **C4** `interfaces/stats.{if,compack}` 25th cell + `[universe]`/`[total]` geometry; `levelup.rs2` `[advancestat,summoning]`.
- **C5** Skill guide: `^skill_guide_summoning = 25`, `if_setevents`, `[if_button2,stats:summoning]`, plus `dbrow`s in `skill_guide_subsections` (212) / `skill_features` (213) **and the hand-edited `dbindex/dbindex_21{2,3}.dbi`** — the one place with no regenerator. Client-side tables only; the server tables in §4 need none.
- **C6** `make -C src torirsserver-cache`; boot `manifest_osrs230_embed.ini`; screenshot the stats tab headlessly. Summoning cell present, greyed.

### Phase D — the feature gate

- **D1** `content.ini` `[namespace:varbit] ids = cache` → `server` (base 25000 already declared).
- **D2** New varbit `content_restrict_summoning_serverside` over a cache varp with free bits; `configs/all.varbit{,.compack}`.
- **D3** `scripts/script_8950.cs2` `case 24`. **Gate the edit** with a standalone `cs2 compile --src /tmp/one --names $RUNESTAR_CS2_NAMES` run asserting `compiled 1, failed 0` — a failed compile at bake time prints one stderr line and ships the base cache's bytes, and only a counter says the edit went nowhere.
- **D4** `[login]` writes the varbit from `^summoning_enabled`.
- **D5** Prove it: flip `^summoning_enabled` to 0, rebuild scripts only, screenshot — cell locked, total level unchanged. Flip to 1 — cell live, total level +1.

### Phase E — tree scaffolding + membership (the untested path; budget for finding its bugs)

- **E1** `cachepack membership --src … --types obj,inv,seq,spotanim` → creates `pack/*.client`/`*.server`. `content.ini` `membership = authored` blocks.
- **E2** `fields/inv.ini` (`size` = `scope = client`).
- **E3** Prove the add path with a throwaway: one obj at id 40000 named in `pack/obj.client`, bake, boot, `::give`. **All five existing `.client` files have zero data lines — nothing in this tree has ever added a record to the client cache.** Find the bugs here, on a two-line change, not under 300 summoning records.
- **E4** `tools/ss_allocate.py`: add `npc, obj, loc, seq, spotanim, inv` to `SERVER_NAMESPACES`; retain npc `server_base = 20000`. Regression-test the actual v5 path: 16-bit per-client index, 14-bit initial definition, then extended/update + mask-`0x1` transformed-16-bit replacement for type 20000.
- **E5** The directory skeleton of §1 + `PROVENANCE.md` + `port/summoning530.map` + a `tools/port_scape2009_summoning.py --check` wired into `make -C src test-port`.

### Phase F — engine: owner-bound familiars

- **F1** `ToriRSServerNpc.owner_pid` / `owner_gen`; zeroed on spawn.
- **F2** Opcodes 11022–11024 (`ss_opcode.h`, `torirs_server_ops_npc.c`, `gen_opcode_meta.py`, coverage regen).
- **F3** `npc_run_mode:2757` and `run_trigger_script:1666` resolve the owner when set. Existing npcs unchanged.
- **F4** Selftest in `torirs_server_world.c`: two players, two owned npcs, assert each follows its own owner across 20 ticks. **Mutate the impl to prove the assertion can fail** before claiming coverage.

### Phase G — tier-1 content, in dependency order

| slice | files | proves |
|---|---|---|
| G1 | `summoning.constant`, `summoning_runtime.varp`, `summoning_pouch.dbtable/.dbrow`, `summoning.param` | symbols before scripts (§4.1 step 2) |
| G2 | `summoning_objs.obj` + `pack/obj.client` + assets for **pouch/scroll/charm icons only** | the obj add path at real scale |
| G3 | `summoning_npcs.npc` + `pack/npc.client` + **Pikkupstix** + `summoning_pikkupstix.rs2` | the npc port path on the easiest case |
| G4 | `summoning_core.rs2` + Spirit wolf | summon → follow → timer → drain → dismiss → logout/login → death |
| G5 | `summoning_points.rs2`, `summoning_locs.loc`, obelisk `loc_add` | renew + potions |
| G6 | `interfaces/ported_2009scape/summoning_infuse.*` + `script_12002.cs2` + `summoning_infuse.rs2` | first authored interface + first authored CS2 |
| G7 | `interfaces/ported_2009scape/summoning_side.*` + `script_12000.cs2` + `summoning_tab.rs2` + side14/stone14/icon14 in `toplevel_osrs_stretch` | the tab; `if_hassub` visibility; `[if_open]` re-arming |
| G8 | `orbs.if` `[orb_summoning]` (copy `orb_prayer`: `type=0 width=57 height=34 layer=10485760`) + an orb clientscript modelled on `script_82.cs2` | the orb reads `stat(summoning)` directly |
| G9 | `summoning_special.rs2` + familiars 1–3, 9 | special engine, self-target, npc-op |
| G10 | `configs/all.inv` / rank-1 `.inv` + `pack/inv.client` + `summoning_bob.*` + `script_12001.cs2` + `summoning_bob.rs2` + familiars 4, 7, 10 | **the most expensive slice.** Test open → close → **reopen** (`IF_CLOSESUB` slot poison) and open → logout → login → open |
| G11 | familiars 5, 6, 8 | damage special, forager, passive heal |
| G12 | `summoning_debug.rs2` — `::summon`, `::sumpoints`, `::sumspec` | the permanent headless proof |

### Phase H — tier 2/3

H1 remaining BoBs · H2 minotaurs + cockatrices (shared specials) · H3 titans · H4 target-picking special dialect · H5 charm drop tables + the 530→239 npc id translation ledger · H6 npc↔npc combat (its own plan).

### Definition of done, per slice

`ToriRSServer_Pack --check-only` 0 errors · `make -C src test-content` green (incl. `test-membership`, `test-server-clean`, `test-port`) · `make -C src test-ToriRSServer` + `test-torirsserver-coverage` green · verified in the real client headlessly (`SDL_VIDEODRIVER=dummy` + `TORIRS_SIM_*` + `TORIRS_EXIT_BMP`) with a `::` debugproc, and the check left **permanent** · state persists across logout/login · existing content untouched · one queue log line recording `scripts N; pack 0 errors`.

---

## RISKS / OPEN

- **R1 — CORRECTED: no 14-bit cache NPC-id ceiling.** The 14-bit field is the direct initial definition; ids 16384..65535 use the add's extended/update flag plus mask `0x1` transformed-16-bit replacement in the same packet. The measured free run below 16384 is not a roster budget.
- **R2 — missing-interface behaviour is unverified.** No handler was found for `IF_OPENSUB` targeting a group absent from the cache. If it hangs rather than logs-and-drops, "flag-off client, flag-on server" is a hard failure. **Verify this before Phase D**; it is a 10-minute experiment (`if_opensub` a nonexistent group against pristine `cache.osrs239`).
- **R3 — CORRECTED: `loc` `server_base = 70000` exceeds the measured wire.** Rev239 `LOC_ADD_CHANGE_V2` writes an exact 16-bit `p2Alt3`; 70000 truncates to 4464. Source obelisk 28716 is mapped to the free native-contiguous target id 62201. This is not analogous to NPC_INFO, whose 14-bit field is an initial definition and whose high-definition update path is conditional.
- **R4 — the stats tab has no free cell.** `[universe]` is 190×261 with a 3×8 grid at 62px columns and `[total]` at y=241; `1 + 8*30 + 19 + 1 = 261` exactly. A 25th cell needs `[universe]` and `[total]` geometry changed, and the sidebar container that hosts it lives in gameframe 161 — **unverified whether rev-239's sidebar renders a taller 320 without clipping.**
- **R5 — `configs/` is machine-owned and `test-server-clean` guards it.** Whether a hand-added line in `configs/all.inv.compack` survives the next `cachepack unpack` is unverified (`content.ini:19-22` claims pack saves merge, but that guarantee is stated for `pack/<ns>.pack`, not for `all.<ns>.compack`). Prefer the rank-1 `.inv` route and prove it empirically in E3.
- **R6 — no authored CS2 has ever existed in this tree.** Every `pack/12_clientscripts.pack` line is `<id>=script_<id>`; zero named script files, zero ids above the cache's. Also **95 of 9,368 committed `.cs2` sources do not compile back today** (stale, written by an older cachepack), including `script_1904.cs2` (the skill-guide layout builder, `unknown command '_1703'`). Fresh decompiles round-trip exactly, so `cachepack unpack --assets=scripts` is the fix — but it will overwrite the hand-authored comments in `script_73.cs2`/`script_7304.cs2` and trip `tools/check_crystal_set_contract.py`, a hard prerequisite of `torirsserver-cache`. Re-apply those comments or unpack selectively.
- **R7 — `CACHEPACK_CS2_NAMES` is an undeclared hard dependency.** Without the RuneStar names dir, `^iftype_graphic` and friends do not resolve and *every* edited script silently reverts to base bytes. `src/makefile:1675` hard-codes `$(HOME)/Documents/git_repos/cs2/…`. Its `stat-names.tsv` stops at 22 — harmless (`stat_24` is a legal spelling, `cs2_names.c:630-677`), but decompiled sources will spell the skill `stat_24`, not `summoning`.
- **R8 — the point-drain formula is a 2009scape invention**, not Jagex-verified (`Familiar.java:126-185`, GL #1903). Record that in `port/summoning530.map`; it is a design decision the port inherits, not a fact.
- **R9 — four rev-530 lifetime values look like source bugs**, and they feed the drain divisor: Void torcher/shifter 9,400 ticks vs 2,700 for the other two Void familiars; Rune minotaur 15,100 vs ≤6,600. None are in tier 1; fix them in tier 2 rather than copying.
- **R10 — sound.** Only `SUMMON_NPC_188` is safely present; 4161/4164/4214/4265/4372 are all above 3826, the documented OSRS divergence point, and must be transcoded. `Familiar.java:713` TODOs the missing per-familiar summon sounds — 2009scape has no such table, so tier 1 ships one shared summon sound.
- **R11 — not investigated:** whether `TORIRSSERVER_PLAYER_MAX = 8` × 16 containers + a BoB row hits a static ceiling; the RSProt/osrs239 codec path for a 25th stat; `ToriRSServer_MapInstance` interaction with owned npcs.

===== DESIGN: design-flag-and-risk =====
# Summoning port — flag architecture, verification, risk register, phasing

*Design pass over 12 recon reports. Everything below marked **[verified]** I re-read in the tree during this pass; where recon was wrong I say so.*

---

## 0. Four recon corrections that change the design

| Recon claim | Reality **[verified]** | Consequence |
|---|---|---|
| "Summoning is stat 23 (2009scape's index)" | `configs/all.enum` `[enum_681]` `val=24,23`; `[enum_680]` `val=24,23`; `interfaces/stats.compack` `24=sailing`. **Stat 23 is Sailing in the osrs239 cache.** | Summoning must be **stat 24**. `RS_PLAYER_STATS_SKILL_COUNT 25` (`src/game/rs_player_stats.h:11`) makes 24 the last addressable slot — **zero client C change**. |
| "`pack/varp.alloc` (6225) has already breached `TORIRSSERVER_VARP_COUNT` (6217)" | `TORIRSSERVER_VARP_SERVER_HEADROOM = 1024` (`torirs_server.h:325`), so `TORIRSSERVER_VARP_COUNT = 6729`. High-water 6225. | **Not a blocker.** Delete this from the risk list; ~500 varps of headroom. |
| "a 25th skill needs a taller interface 320 grid" | The grid already has 24 cells and cell 24 is **Sailing**, which this tree does not implement (`pack/stat.pack` stops at `22=construction`, no sailing anywhere in server content). | **No geometry change.** Summoning takes the existing cell 24. This is the single biggest scope reduction available and it should be the primary plan. |
| "new pouch objs will hard-error the bake (cell c)" | True, and it is *the flag surface*: `record_is_client()` (`cp_pack.c:524-528`) returns false for any rank-1 record whose type lacks `records = client`. Only `dbtable/dbrow/param/enum` declare it **[verified: `grep ^records fields/`]**. `obj`, `npc`, `loc`, `varp` do not. | An authored pouch/familiar is **server-only by construction**. It reaches the client cache only via an explicit `pack/<ns>.client` line. That is a per-entity, opt-in, already-implemented gate. |

Two more load-bearing facts confirmed:

- **`cachepack`'s config walk has exactly two roots**: `static const char* const ROOTS[] = { "configs", "server/scripts" };` at `cp_pack.c:1669, 1788, 2169, 2605`. A top-level directory in the content tree is invisible to it.
- **`RSCache_Dat2EditCommit` merges**: `dat2_edit_load_existing_files()` (`3rd/rscache/src/cache_edit.c:387`, called at `:513`) reads the base archive's existing files before applying puts, and `cp_pack.c:1576` puts **one file per record**. A *partial* source tree therefore preserves every base record it does not state. This is what makes a chained overlay bake viable.

**CORRECTED:** the old “hard ceiling” paragraph reversed the NPC_INFO fields. Rev239 uses a
16-bit per-client index followed by a 14-bit initial definition; type 20000 is carried by the
same-packet extended/update + mask-`0x1` transformed-16-bit replacement path. The full
82-familiar roster is not constrained by the direct initial-field width.

---

## 1. The feature flag

### 1.1 It is not a `features.h` field, and here is the proof

`struct ToriRS_FeatureTable` (`src/features/features.h:112-245`) is resolved by `ToriRS_Features_ForCache(game, epoch, revision)` — and `features.c:190` explicitly `(void)`-discards `revision`. It keys off **lineage only**. There is no way for that function to answer "is this the summoning bake?" without lying. Its documented contract (`features.h:10-14`) is *"a zero field means classic"* — every one of the 14 fields selects between two behaviours the engine implements for data that exists in every era. Not one gates content.

**The Summoning flag adds zero fields to `features.h` and zero new engine env vars.** That is the strongest layering claim in this design and it should be defended.

### 1.2 The flag, exactly

| Layer | Name | Kind | Where declared | What it does |
|---|---|---|---|---|
| **Build** | `SUMMONING=1` | make variable | `src/makefile` (new targets `torirsserver-cache-summoning`, `torirsserver-scripts-summoning`, `torirsserver-summoning`) | Selects whether the overlay bake and the second script pack are produced at all. **Default unset.** |
| **Client boot** | `[cache:boot] dir=cache.osrs239.summoning` | existing manifest key | `manifest_osrs239_summoning.ini` (new file, copy of `manifest_osrs230_embed.ini` with one line changed) | Which cache the client loads. No new manifest schema, no `bootmanifest.c` change. |
| **Server boot** | `TORIRSSERVER_SCRIPTS=<tree>/server/scripts/build-summoning` | existing env var (`torirs_server_boot.c`) | set by the summoning manifest's launcher / the make run target | Which compiled script pack the server loads. No new env var. |
| **Content compile** | `^summoning_enabled` | ServerScript constant | `ported_scape2009_summoning/server/scripts/configs/summoning.constant` | Every ported entry point opens `if (^summoning_enabled = 0) { return; }`. Defence in depth against a mis-staged build. |
| **Content runtime** | `%summoning_enabled` | server varp, `transmit=no`, `scope=perm` | same folder, `summoning.varp` | Per-account kill switch and the hook a future Wolf-Whistle-style unlock hangs on. |
| **Client CS2** | *(none required)* | — | — | With the flag off the summoning cache records **do not exist**, so there is nothing for CS2 to hide. The `script_8950` → `script_8951` → `%varbit18166` chain (the shipped Sailing gate **[verified]**) stays available as an optional in-game unlock in a later phase. |

### 1.3 Where the ported content physically lives

```
OSRS-Content/osrs239-content/
  ported_scape2009_summoning/          <- NEW top-level dir. Invisible to every walker.
    README.md                          <- provenance, policy, the flag contract
    client/                            <- the overlay half
      configs/all.enum + .compack      <- ONLY the edited/new blocks
      configs/all.npc  + .compack
      configs/all.obj  + .compack
      configs/all.varbit + .compack
      interfaces/stats.{if,compack}    <- full copy, cell 24 renamed
      interfaces/summoning_*.{if,compack}
      scripts/script_8950.cs2          <- full copy
      models/ animsets/ framemaps/ sprites/ synth/
      pack/                            <- ADDITIVE asset packs + obj.client/npc.client/...
    server/scripts/                    <- .rs2 + configs/, staged into the second script tree
```

Why this is invisible with the flag off, mechanically:

1. **cachepack configs** — the walk roots are `configs/` and `server/scripts/` only (`cp_pack.c:1669`). `ported_scape2009_summoning/` is neither.
2. **cachepack assets** — `import_one` walks `pack/<ns>.pack`, not the directory (`cp_assets.c:1383-1387`: *"a file whose name it does not list has no id to be written to"*). The base tree's pack files gain no lines.
3. **ToriRSServer content** — `walk_configs` (`torirs_server_content.c:2879`) recurses `server/scripts` only.
4. **sscompile** — `collect_sources` (`ssc_compile.c:2867`) recurses `--src`, which is `server/scripts`.

No walker is modified. No `.` -prefix parking (forbidden by `.cursor/rules/no-park-sibling-content.mdc`). No sibling lane touched.

### 1.4 How the flag-on bake works

Chained two-pass, using unmodified cachepack:

```sh
# pass 1 — today's bake, byte-for-byte unchanged
cachepack pack --src OSRS-Content/osrs239-content \
  --base cache.osrs239 --out cache.osrs239.baked \
  --rev osrs239 --assets --binary --gamevals

# stage the overlay: base tree's meta.ini/content.ini/fields/ + full *.compack
# indexes (inert: they name ids, they do not emit records) + the ported client/ dir
python3 tools/stage_summoning_overlay.py --tree OSRS-Content/osrs239-content \
  --out build/summoning-overlay

# pass 2 — only the records/assets the overlay states
cachepack pack --src build/summoning-overlay \
  --base cache.osrs239.baked --out cache.osrs239.summoning \
  --rev osrs239 --assets --binary --gamevals
```

Pass 2 is safe because `Dat2EditCommit` merges into the existing archive (§0) and `--base` copies first (`cp_pack.c:1616-1621`, and EXCEPTIONS B4 requires exactly this — never repack in place).

The two things the staging tool must get right, and they are the spike in Phase 0:

- `configs/all.<type>.compack` must be **full copies** (so a ported npc block naming an existing seq resolves) while `configs/all.<type>` holds **only the blocks we state** (so only those records are written). Compack lines without blocks are inert.
- `pack/<asset>.pack` must be **additive only** (only summoning ids), or pass 2 re-imports all 61,615 models and doubles the output file.

**Alternative if the staging tool proves fragile:** teach cachepack a third walk root (`CP_WALK_MAX_ROOTS = 4` leaves room; four identical `ROOTS[]` literals + the asset import + `cp_names_load`). Cleaner long-term, ~1 day of tool surgery, but it is engine change on the critical path. Recommend the staging tool first, and only fall back if the spike fails.

### 1.5 With the flag OFF, is `cache.osrs239.baked` byte-identical to today?

**Yes, and it must be a hard gate.** The property is *structural*, not empirical:

- No file under any cachepack walk root changes.
- No `pack/*.pack` asset line is added.
- No `pack/*.client` line is added.
- The one base-tree file the port edits unconditionally is `pack/stat.pack` (`23=sailing`, `24=summoning`) — and `stat` is `names = authored` with **no gameval archive**, so it is never packed into the cache. It is read by `sscompile` and `torirs_server_content.c` only.
- The one C change (`TORIRSSERVER_STAT_COUNT`) is server-side and touches no cache.

Two checks, both permanent:

1. **`tools/check_summoning_isolation.py --check`** (new, wired into `make -C src test-content`): asserts (a) nothing under `ported_scape2009_summoning/` is reachable from `configs/` or `server/scripts/` by path or symlink; (b) no line in `pack/*.pack` or `pack/*.client` names a summoning entity; (c) the summoning commits' `git diff --stat` touches no file under `configs/ interfaces/ scripts/ sprites/ models/ animsets/ framemaps/ synth/` or `pack/*.pack` / `pack/*.client`.
2. **Phase-0 empirical proof, recorded once in the queue doc**: `shasum -a 256 cache.osrs239.baked/*` before and after landing the ported folder — identical. Do not check the digest in as a test; it is a digest of a derived artifact that legitimately moves when unrelated content lands.

*(Note the baseline that matters is `cache.osrs239.baked`. `cache.osrs239` pristine is never written by anything and is trivially identical.)*

---

## 2. Cross-cutting safety

### 2.1 Summoning records in the cache, vanilla client (i.e. someone boots the summoning bake with a stock RuneLite)

| Record kind | Effect | Severity |
|---|---|---|
| New objs (pouches, scrolls, charms) at id ≥ 40000 | Extra obj records the client never references. Inert. | none |
| New npcs at 20000+ | Inert until spawned. | none |
| Added `enum_681` slot 25 → stat 24 | Sailing remains slot 24; component 34 shows Summoning and `stat_totallevel` (`script_1007`) includes it. The corrected layout puts Summoning beside Hunter and Sailing beside Total. | **cosmetic, expected** |
| New interface group ≥ 969 | Never opened unless the server opens it. | none |
| Edited `script_8950.cs2` | Only changes which stat the tab greys out. | none |
| New varbits / varps | Inert. | none |

**Verdict: the summoning cache is safe to boot with vanilla content.** The reverse — vanilla cache, summoning server — is where it breaks.

### 2.2 Summoning packets to a flag-off client

| Packet | Behaviour | Severity | Mitigation |
|---|---|---|---|
| `UPDATE_STAT` stat=24 | `rs_gameproto_exec.c:561-575` bounds against `RS_PLAYER_STATS_SKILL_COUNT` = 25. **24 passes**, xp is stored, nothing renders it. Silent no-op. | none | none needed. Note that bumping `TORIRSSERVER_STAT_COUNT` to 25 makes the login refresh loop transmit stats 23 and 24 at level 1/xp 0 — two extra packets, harmless, but confirm the seed loop's dirty-marking so it is not a per-tick cost. |
| NPC spawn of a familiar id absent from the client cache | NpcType lookup falls to the nameless placeholder. Renders as a blank blocking entity. **Not verified.** | medium | Server gate is the answer; see below. |
| Inventory slot holding a pouch obj absent from the cache | Slot with no model/name. | medium | same |
| **`IF_OPENSUB` for an interface group the cache has no archive for** | **Unverified and the dangerous one.** No "missing interface" handler was found. The tree's own precedent (`memory/cs2-yield-missing-targets`: *never yield un-loadable ids*) says an interface load that never completes is a stuck open-sub, i.e. a soft hang. | **high** | see below |

**Mitigation, and it is the whole answer:** the flag is *paired* — the summoning script pack and the summoning cache are produced by the same `SUMMONING=1` build and named by the same manifest. There is no supported configuration in which a summoning server faces a vanilla client. Enforce it:

- `manifest_osrs239_summoning.ini` is the only manifest whose launcher sets `TORIRSSERVER_SCRIPTS=.../build-summoning`, and it is the only manifest pointing at `cache.osrs239.summoning`.
- `^summoning_enabled` is `0` in the base tree's copy of the constant and `1` only in the staged summoning script tree. A mis-staged build summons nothing rather than opening a missing interface.
- Phase-0 must **verify the missing-interface behaviour empirically** (boot the pristine cache, `IF_OPENSUB` a nonexistent group id from a debugproc, watch for a hang). If it hangs, file it as a pre-existing client robustness bug and fix it independently — do not let Summoning own it.

---

## 3. Documentation work

### 3.1 Mandatory amendments (the port is illegal under current docs)

| File:line **[verified]** | Current | Action |
|---|---|---|
| `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65` | `\| content/global/skill/summoning/**, Wolf Whistle \| Summoning is not in OSRS \|` | Delete the skip row; replace with a pointer to the new queue doc. Wolf Whistle stays skipped for now (quest, out of scope) — state that explicitly rather than by omission. |
| `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:68` | `\| Evil Turnip / summoning-linked patches \| Summoning ecosystem \|` | Leave skipped in Phase 1-3; un-skip only when the Evil Turnip pouch lands. Note the blast radius: it touches the **live** `skill_farming/` tree (rows 1a-1g, all `done`) and must be additive. |
| `docs/PORTING_GUIDE.md:35` | `…skip bots/holiday/Summoning/RS2-only` | Drop `Summoning`; add "Summoning is ported behind a flag — see `SUMMONING_PORT_QUEUE.md`". |
| `docs/PORTING_GUIDE.md:683` | `(bots, holiday events, Summoning, Fist of Guthix, …)` | Same. |
| `docs/SKILLS_CONTENT_PORT_QUEUE.md:101` | `\| Summoning / Fist of Guthix / RS2-only \| not in OSRS \|` | Split the row: Fist of Guthix stays, Summoning moves out. |
| `docs/SKILLS_CONTENT_PORT_QUEUE.md:148, 348` | "Audit roster complete (23/23)" | Either add a #24 Summoning audit row and correct both counts, **or** state that Summoning is audited in its own queue and the 23/23 claim scopes to OSRS-native skills. Prefer the latter — less churn. |

The former `CLAUDE.md` restoration proposal is rejected by explicit user direction. The correct
governance fix is to remove its stale citations and keep binding rules in `PORTING_GUIDE.md`,
queue documents and `.cursor/rules/no-park-sibling-content.mdc`.

### 3.2 A new `docs/SUMMONING_PORT_QUEUE.md` — yes, warranted

Not because Summoning is special, but because it is the first port that is **(a) cross-cache-revision, (b) behind a flag, (c) asset-bearing**. None of the four existing queue docs have a column for any of that. Contents:

1. **Charter** — why the skip lists were reversed, who decided, the flag contract, and the byte-identity requirement stated as a bar.
2. **The id ledger** — the one thing no existing doc provides: rev-530 id → osrs239 id, per namespace, with disposition. Mirrors `port/names.map`'s contract (generated columns re-derived by `--check`, human columns never regenerated). Backed by new files `OSRS-Content/osrs239-content/port530/{npc,obj,seq,spotanim,model,sprite}.map` and a new `tools/port530_diff.py --check` wired into `make -C src test-port`. **`port/` today is LostCity-keyed and cannot host this** — its diff tools take a RuneScript content checkout as `--reference` and 2009scape has no `.rs2`, no `^constant`, no `pack/`.
3. **The slice queue** — same `#`/status/notes shape as SCAPE2009.
4. **The opcode gap log** — every new VM opcode, logged *before* C is written (PORTING_GUIDE §4.5 step 4).
5. **The NPC mapping ledger** — a live rev-530 definition id → osrs239 cache/config id map, updated per slice. This is translation and collision bookkeeping, not a budget: NPC_INFO's 14-bit field is only its direct initial definition, and high definitions use the extended update path.
6. **Known-bad source data**, transcribed once so nobody copies it: `DOOMSPHERE_SCROLL` pouch `-1`; `DEADLY_CLAW_SCROLL` keyed to charm 12162 not pouch 12794; `THIEVING_FINGERS_SCROLL` xp `47` (should be 0.9); loc `28278` typo for `28728`; Void torcher/shifter 9400 ticks and Rune minotaur 15100 ticks are outliers that feed the drain divisor; npc 6883/6884 double-registered.

### 3.3 Also update

- `OSRS-Content/README.md` — the `ported_scape2009_summoning/` directory and what it means; correct the two stale claims recon found (`pack/<type>.pack` as "the id authority, 40 of them"; `server/pack/*.pack` holding "skills and varp aliases").
- `3rd/rscache/EXCEPTIONS.md` — **H1 is stale** (dbrow/dbtable are packable; verified 16711/16711 + 246/246 exact). Add an entry for the rev-530 profile and its codec deltas.
- `docs/DBTABLES.md` — same H1 staleness.
- A topic doc `docs/summoning.md` (case-file style, like `docs/skill_guide.md`) written *as part of done*, not after.

---

## 4. Verification strategy

### 4.1 The full command list

```sh
# --- build (never CMake; set PLATFORM_OBJ_BASE if another agent shares the repo)
make -C src
make -C 3rd/rscache/tools cachepack cs2 port_npc find_named

# --- content, flag OFF (must be green before and after every summoning commit)
make -C src test-content
#   = test-content-register test-servercodec test-ss-symbols torirsserver-scripts
#     torirsserver-servpack test-membership torirsserver-pack test-server-clean test-port
src/build/ToriRSServer_Pack --check-only                 # 0 errors, always
python3 tools/check_summoning_isolation.py --check   # NEW: the byte-identity gate

# --- engine / VM
make -C src test-ToriRSServer                 # includes ./src/build/torirsserver --selftest
make -C src test-torirsserver-coverage        # fails on a stale opcode table
make -C src test-torirsserver-embed
make -C src test-ss-provider test-db test-torirsserver-param test-torirsserver-npc

# --- cache fidelity (read 3rd/rscache/EXCEPTIONS.md FIRST)
make -C 3rd/rscache test
cachepack verify --cache cache.osrs239 --rev osrs239 \
  --src OSRS-Content/osrs239-content --assets      # lost-here == 0 per type

# --- the flag-off bake, unchanged
make -C src torirsserver-cache
shasum -a 256 cache.osrs239.baked/main_file_cache.dat2   # compare to the recorded baseline

# --- the flag-on bake + scripts
make -C src SUMMONING=1 torirsserver-summoning
cachepack verify --cache cache.osrs239.summoning --rev osrs239 \
  --src build/summoning-overlay
3rd/rscache/tools/cs2/cs2 compile --cache cache.osrs239 --rev osrs239 \
  --names $RUNESTAR_CS2_NAMES --src build/summoning-overlay/scripts --out /tmp/cs2bin
#   MUST print "compiled N, failed 0" — a cachepack CS2 failure is near-silent
#   (cp_decode.c:2447-2452 ships base bytes and only a counter says so)

# --- headless in-client proof (per slice, left permanent)
TORIRSSERVER_SAVES=$(mktemp -d) SDL_VIDEODRIVER=dummy TORIRSSERVER_VERBOSE=1 \
TORIRS_SIM_HOOK=... TORIRS_EXIT_BMP=build/summoning_slice1.bmp \
  src/build/torirs --manifest manifest_osrs239_summoning.ini --user testc --pass test

# --- triage when a panel is blank (in this order)
TORIRS_DUMP_TREE_EXIT=1 ; TORIRS_DUMP_BOUNDS=<group> ; TORIRS_DUMP_SETSIZE=1
```

Memory-debugging on this machine is `MallocScribble`, **not ASAN** (`ENABLE_ASAN=1` hangs).

### 4.2 Per-phase acceptance criteria

| Phase | Acceptance |
|---|---|
| **0 Prerequisites** | `rev_dat2_rs530.c` exists and decodes rev-530 npc/obj/seq/model/framemap with a full exact-consumption sweep, not a spot check. The framemap V3→V1 transcode bug in `tools/common/cache_write.c:545-580` is fixed with a test that fails on the old code. `stage_summoning_overlay.py` produces a cache where `cmp` against the pass-1 bake differs only in the archives the overlay states. Docs amended. `check_summoning_isolation.py --check` green. Missing-interface behaviour empirically characterised. |
| **1 Vertical slice** | With `manifest_osrs239_summoning.ini`: log in → Summoning shows in the skill tab at level 1 → `::setlevel summoning 20` raises it → use the pouch → one familiar spawns, follows across a region boundary, survives a logout/login, dismisses on click and on timer expiry. Proved by BMP screenshots at four points and a `::summontest` debugproc. `make -C src test-content` and `ToriRSServer_Pack --check-only` green **on the flag-off tree**. Flag-off bake digest unchanged. |
| **2 Skill surfaces** | Skill guide opens on Summoning and lists real rows from a new dbtable (with the `dbindex_21{2,3}.dbi` hand-edit verified by an actual `db_find` hit, not by inspection). Points orb renders and drains. Obelisk loc gives Renew-points. Infusion UI produces a pouch from ingredients with correct xp. |
| **3 Breadth** | N familiars summonable, each with its own model/anims/sounds; scroll specials fire; every rev-530 definition id has a recorded osrs239 cache/config mapping. No acceptance criterion is derived from the direct initial-definition width. |
| **4 Beast of Burden** | `fields/inv.ini` + `[namespace:inv]` landed (this unblocks `shop` too — bank the credit). BoB container opens, stores, withdraws, spills on death, **survives logout** (the `clear()` vs `dismiss()` asymmetry). |

Every phase additionally: state persists across logout/login; no new silently-missing opcodes in `ToriRSServer_ScriptsReportGaps`; **prove the assertion can fail** before claiming coverage (mutate the impl / unbind the script).

---

## 5. Risk register (ranked)

| # | Risk | Likelihood | Blast radius | Mitigation |
|---|---|---|---|---|
| **1** | **A test suite SKIPS because the data is absent, and a skip reads as a pass.** `test_cachepack_fidelity.sh` skips loudly with no cache; a pristine worktree skips whole suites. Every "green" claim about the summoning bake is suspect. | **high** | The entire verification story is worthless; regressions land silently | Make every summoning-relevant target **assert it ran**: `check_summoning_isolation.py` prints and requires a non-zero check count; the bake targets fail if the cache dir is missing rather than skipping. In CI-equivalent runs, grep the output for `SKIP` and fail. Never accept "tests passed" without the per-suite counts. |
| **2** | **CORRECTED: treating the direct 14-bit initial definition as an id ceiling.** | resolved | None for roster scope | Rev239's add has a 16-bit per-client index, then a 14-bit initial definition; type 20000 uses same-packet extended/update + mask-`0x1` transformed-16-bit replacement. Permanent regression: index 321, type 20000. |
| **3** | **Silent CS2 / codec declines.** `cachepack` prints one stderr line and ships base-cache bytes when a script fails to compile (`cp_decode.c:2447-2452`); 95 of the tree's 9,368 committed `.cs2` sources already do not recompile, **including `script_1904.cs2`** (the skill-guide builder). `RUNESTAR_CS2_NAMES` is an undeclared hard dependency at `$HOME/Documents/git_repos/cs2/...`. | high | A cache that boots, looks fine, and has no summoning in it | Run the standalone `cs2 compile` gate on the overlay's scripts *before* every bake and require `failed 0`. Refresh the stale sources with `cachepack unpack --assets=scripts` **selectively** (a blanket refresh overwrites the hand-authored comments in `script_73.cs2`/`script_7304.cs2` and trips `check_crystal_set_contract.py`). Make `RUNESTAR_CS2_NAMES` a hard prerequisite check in the summoning targets. |
| **4** | **Framemap V3→V1 transcode is a silent no-op.** `tools/common/cache_write.c:545-580` re-encodes without clearing `has_transform_actor`/`has_masks`/`tail`, so `RSCache_Dat2FramemapEncode` emits V3 blocks into a V1 cache. | high | Every ported familiar T-poses or animates to garbage, with no error | Fix it in Phase 0 with a test that fails on the current code (decode a real 530 framemap, downgrade, assert the byte length and the V1 shape). This is a **pre-existing library bug**; fixing it may move 634/727 behaviour — A/B before attributing anything to Summoning. |
| **5** | **The chained overlay bake is unproven and the "add" path has never been run.** All five `pack/*.client` files have zero data lines; `PACK_ENTITY_SPLIT_PLAN.md §11.1` says step 4 "author" is unexercised. | high | Phase 0 slips; worst case the byte-identity design needs the cachepack `--overlay` fallback | Spike it first with a throwaway two-component interface at id 969 and one new obj at 40000, before designing anything on top. Budget a full day. Fallback is the third walk root (`CP_WALK_MAX_ROOTS = 4`). |
| **6** | **`IF_OPENSUB` for a group absent from the cache may hang the client.** Unverified; no missing-interface handler found. | medium | A flag-mismatched boot is a soft hang, not an error message | Characterise it in Phase 0 with a debugproc. Pair the flag (§2.2) so the mismatch is unreachable in supported configurations. If it does hang, fix it as an independent client-robustness slice — do not let Summoning own it. |
| **7** | **Headless runs are not independent.** A shared `saves/` carries state between runs; a "passing" screenshot can be a previous run's state. `pkill -f build/torirsserver` also kills `ToriRSServer_Dev`. | high | False greens on every acceptance criterion | `TORIRSSERVER_SAVES=$(mktemp -d)` on **every** headless invocation, no exceptions, baked into the make recipe rather than left to the operator. Never bare `pkill`. |
| **8** | **`embed_test` decode is broken pre-existing** — nothing decodes past login. | certain | Any embed-path failure gets misattributed to Summoning | A/B against `HEAD~` before blaming any change. Do not add summoning coverage to `embed_test` until it is fixed; use the real client headless path as the oracle instead. |
| **9** | **`fields/inv.ini` / `[namespace:inv]` do not exist**, which is the same blocker that has kept `shop` unportable. `ToriRSServer_ContainerResolve` returns NULL for an inv the cache does not size, and every container op aborts. | certain | Beast of Burden is unshippable; ~1 week of unplanned prerequisite work | Move BoB to **Phase 4** and treat the inv namespace as its own slice with its own acceptance, credited against `shop` as well. Do not let it block Phases 1-3. |
| **10** | **Engine `active_player` staleness.** `npc_run_mode` (`torirs_server_world.c:2764`) follows `srv->active_player`, which `phase_npcs` never sets; `run_trigger_script` (`torirs_server_scripts.c:1666`) sets ACTIVE_PLAYER from the same stale pointer for `ai_*` triggers. npc uids have no generation counter. | certain | With two players, both familiars follow one arbitrary player and every `[ai_timer]` reads a stale player's varps | Fix generically in Phase 1: add `owner_pid` + a generation counter to `ToriRSServerNpc`; resolve the player from the owner in `npc_run_mode` and in `ai_*` dispatch; add generic opcodes `NPC_SETOWNER` / `NPC_OWNER` in the **11000+ EXTRA band**, logged in the queue's opcode-gap table before the C is written. Frame it as fixing a documented pre-existing defect (`osrs230_mockserver.md §6.1`), not as summoning plumbing. |

**Standing hazards, not ranked because they are procedural:** never `git stash` in this repo (a no-op push turns `pop` into restoring an old stash); no ASAN on this Mac; distrust prose counts in docs — re-measure from generated sources; a bare name in argument position resolves to the wrong namespace and never fails, so never name a ported record exactly `summoning` (prefix everything `summoning_*` and lint for it — `[advancestat,summoning]` resolves via an unhinted `SSC_SYM_UNKNOWN` lookup at `ssc_compile.c:2286`).

---

## 6. Phasing

### Phase 0 — Prerequisites and proof (nothing user-visible)

`rev_dat2_rs530.c` + one row in `revisions.c` (auto-derives `FRAMEMAP_V3` + `FRAME_V1`; needs explicit `LOC_RS2`/`FLO_RS2`; **must not** copy 643's `FRAME_V2` pin). Full exact-consumption sweep over 530's npc/obj/seq/loc. Fix the framemap transcode bug. Fix or work around the RS2 sequence codec (`dat2_config_sequence.c:1139-1155` can never satisfy `RevisionAtLeastOsrs` on an rs2 profile → always V3 → 649 bad seqs at 530; this is a whole-RS2-branch bug). `stage_summoning_overlay.py` + the chained bake spike. `check_summoning_isolation.py`. Docs amendments + `SUMMONING_PORT_QUEUE.md`; remove obsolete `CLAUDE.md` citations. Characterise missing-interface behaviour.

### Phase 1 — Vertical slice: Spirit wolf

One familiar, end to end, and the skill exists.

- **Stat 24.** `pack/stat.pack` += `23=sailing`, `24=summoning`; `TORIRSSERVER_STAT_COUNT` 23→25.
  Overlay: `enum_681 val=25,24`, component `34=summoning_stats_cell`, dedicated script 1198,
  `script_8950` case 24, and the corrected row layout. The exact source-222 wolf head is remapped
  to the marked target sprite name at target id 229; Sailing is not displaced.
- **Assets.** Port npc 6829 (+ its combat twin if the flag design keeps twins) with `port_npc --from-rev rs530 --to-rev osrs239`, then `cachepack unpack` the result into the ported folder. Model, framemap, seqs, the `bas` type — **note `bas` has no cachepack type**, so either add one or set explicit anim slots on the ported npc rather than relying on BAS.
- **Objs.** `spirit_wolf_pouch`, `pouch`, `spirit_shards`, `gold_charm` at ids 40000+, with `pack/obj.client` created (`cachepack membership --types obj`) and a `[namespace:obj] membership = authored` block in the overlay's `content.ini`.
- **Server.** `owner_pid` + generation counter on `ToriRSServerNpc`; `NPC_SETOWNER`/`NPC_OWNER` opcodes; owner-aware `npc_run_mode` and `ai_*` dispatch. `[opheld1,spirit_wolf_pouch]` → level check → `npc_add` → `npc_setowner` → `npc_setmode(playerfollow)` → `settimer` for decay → dismiss. Points as `stat_base/stat_sub` on stat 24.
- **UI.** Reuse an existing sidebar slot or defer the tab entirely — Phase 1 does **not** need interface 662's analogue. Dismiss via a debugproc + the familiar's npc op. This keeps the 161 sidebar geometry problem out of the slice.

**Demo:** log in, `::setlevel summoning 20`, click the pouch, a wolf appears and follows you to Varrock, log out and back in, it is still there, timer expires, it leaves. Four BMPs.

### Phase 2 — Skill surfaces
Skill guide (dbtable 212/213 rows + the hand-edited `dbindex_21{2,3}.dbi` — **the single most fragile step in the whole port**, since no regenerator exists and a wrong order makes `db_find` silently miss). Points orb appended to `interfaces/orbs.if` (cache-built chrome, the lowest-risk authored UI in the tree — copy the `orb_prayer` block verbatim). Summoning sidebar tab (161 `side0..side13` is full — this needs new components in three toplevels and new stone/icon geometry; consider deferring to Phase 5). Obelisk locs + Renew-points. Infusion UI authored fresh in the 239 vocabulary (do **not** transcode 530's 669 — every `graphic=`/`font=` id needs remapping anyway).

### Phase 3 — Breadth
Familiars grouped by shared mechanics, never by an NPC-id budget. Scrolls + special moves. Charm drops (the 1222-npc drop table needs a 530→239 cache/config id map; expect heavy attrition because definitions differ between revisions, not because of the direct 14-bit initial-definition field). Skill boosts. Foragers.

### Phase 4 — Beast of Burden
`fields/inv.ini` + `[namespace:inv]` + the `.inv` walker + world-scoped `ToriRSServer_ContainerScope()`. Then BoB containers, interfaces, the logout `clear()` vs death `dismiss()` asymmetry.

### Phase 5 — Polish
Sidebar tab if deferred. `script_8950` per-account unlock. Wolf Whistle. Sounds (only sound 188 is safe; 4161/4164/4214/4265/4372 are above the 3826 divergence point and need transcoding).

---

## 7. Effort estimate

Engineer-days, assuming one experienced agent working this repo, and assuming Phase 0's spikes succeed. **These are honest, not optimistic — every phase has an unexercised mechanism in it.**

| Phase | Days | Dominated by |
|---|---:|---|
| 0 Prerequisites & proof | **10-14** | rev-530 profile + sweep (4-5), framemap/seq codec fixes (2-3), overlay staging spike (2-3), docs (2), missing-interface characterisation (1) |
| 1 Vertical slice | **12-18** | asset port pipeline first-run (4-6), owner/generation engine slice + opcodes (3-4), stat 24 through cache+server+tab (3-4), scripts and headless proof (2-4) |
| 2 Skill surfaces | **12-18** | dbindex hand-edit and its verification (3-5), orb (2), infusion UI authored fresh (4-6), obelisk locs (2-3), tab if not deferred (+4) |
| 3 Breadth (≈20 familiars) | **15-25** | per-familiar asset port ~0.5-1 day once the pipeline is warm; specials ~0.3/each; charm drop id map (3-5) |
| 4 Beast of Burden | **8-12** | inv namespace prerequisite (5-7) is most of it |
| 5 Polish | **6-10** | sidebar tab across three toplevels (4), sounds (2-3) |
| **Total** | **63-97** | |

Scope levers, in order of value:
- **Drop pets entirely** — saves ~8 days because they are a separate lifecycle, not because of ids.
- **Drop wilderness combat twins** — saves ~5 days and matches the absence of wilderness; replace the inferred `combatFamiliar` with an explicit ported-data flag.
- **Defer the sidebar tab to Phase 5** — saves 4 days off the critical path.
- **Order the roster by mechanics groups** — useful for review cadence, but the settled scope remains all 82.

A "Phase 0 + Phase 1 only" deliverable — Summoning in the skill tab, one familiar you can summon, that follows you, that you can dismiss, behind a flag, with the flag-off cache proven byte-identical — is **22-32 days** and is the right thing to commit to first.
