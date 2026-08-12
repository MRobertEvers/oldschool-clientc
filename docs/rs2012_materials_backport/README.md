# `rs2012_material_bake` — the RS727 → OSRS239 porting rules

The backporter for the QBD / Tormented Demon lane
(`ported/rs2012_qbd_td`, 660 models). Source is
[`src/engine/proctex/test/rs2012_material_bake.c`](../../src/engine/proctex/test/rs2012_material_bake.c);
build it with `make -C src rs2012-material-bake` (or `.\make.ps1
rs2012-material-bake`) and it lands at `src/build_win64_opt/rs2012_material_bake.exe`.

**Most people should not run this tool.** Its output is committed to
OSRS-Content, so pulling the content repo and packing a cache is enough; see §9.
Run it when you are changing the port itself.

```sh
# rebake the lane the way it ships, then re-pack the cache
make -C src rs2012-lane-bake          # --matte 60 --apply, into MOCK230_CONTENT_DIR
make -C src mock230-cache-rs2012

# prove the bake is reproducible (two bakes into scratch dirs, must be identical)
make -C src rs2012-lane-bake-check
```

Both need the RS727 source cache — `cache.rs727_preeoc`, ~461 MB, matched by
`.gitignore`'s `cache.*/`, so a clone does not carry it and it has to be
obtained out of band. Point `RS2012_SRC_CACHE=<dir>` at it if it is not at the
repo root; the targets fail with that instruction rather than baking something
wrong. `RS2012_LANE_MATTE` (60) is the one load-bearing tuning value and lives
in the makefile because it used to live only in a branch name.

Underneath, the targets are just:

```sh
# dry run — writes OB3s to a scratch dir, never touches the lane
src/build_win64_opt/rs2012_material_bake.exe \
    --cache cache.rs727_preeoc \
    --tree build/osrs-content-rs2012/osrs239-content \
    --matte 60 --models-out /tmp/models

# the same thing, written into the lane
… --matte 60 --apply
```

`--models-out` and `--apply` are independent: **`--models-out` alone never
writes the lane**, so a cache packed after one shows no change. That is the
usual explanation for "I re-backported and the cachepack didn't pick it up."

**This document is the rule list.** Every decision the tool makes about a face
is one of the rules below, each with the flag that changes it and the evidence
it rests on. Two things are worth knowing before reading them:

- **The tool is byte-reproducible**, and there is a make target that proves it:
  `make -C src rs2012-lane-bake-check` bakes twice into scratch directories and
  fails if the two differ. It was *not* reproducible until the `p[256]` fix
  described in §9 — 246 of 512 material frames and 392 of 660 models used to
  differ between two runs on identical arguments — so a difference against an
  older lane is expected and is not evidence about a flag.
- **Most rules are inferences, and they are labelled.** RS727's material table
  carries `valid` and `alpha_mode`; everything else — is this a mask, a
  membrane, a wisp — is read back out of the baked frame. Where a rule
  contradicts the table, it says so, because those are the rules that have gone
  wrong before.

Related: [`../../RS2012_BACKPORT.md`](../../RS2012_BACKPORT.md) (why the lane
exists), [`HISTORICAL_alpha_kernels.md`](HISTORICAL_alpha_kernels.md) (the
removed renderer half, and the per-material mask analysis that is still valid),
[`../../tools/rs2012_backport_audit/README.md`](../../tools/rs2012_backport_audit/README.md)
(the animation-aware audit that runs over the output).

---

## 0. The shape of the problem

An RS727 face names a **procedural program** (idx26 metadata, idx9 programs,
idx8 sprite inputs). An OSRS239 face names a member of the single sprite-backed
texture archive. There is no mechanical translation, so the tool renders each
program to a 128×128 ARGB frame through the client-matched evaluator and decides,
per material, whether that frame can be a destination texture at all — and when
it cannot, what to do with the faces that named it.

Almost all of them cannot. Of 256 materials, **204 are erased**, taking ~295,000
lane faces with them. The rules below are mostly about those faces.

## 1. Which models are ported

Every model reachable from the lane's config closure, remapped through
`pack/7_models.pack` to destination ids from 110000.

- **Hand-authored lane models are skipped** — a model whose lane name carries a
  suffix (`rs2012_model_<id>_<suffix>`) has no RS727 source to re-read and no
  source materials to remap. Two on this lane; the run prints
  `skipped 2 authored lane model(s) with no RS727 source`.
- A model that is not OB3, or cannot be reopened, is a hard failure — the tool
  refuses rather than shipping it unconverted.

## 2. Which materials survive as textures

A material is kept as a destination texture only if **both** hold:

| test | source | meaning |
|---|---|---|
| `material.valid` | RS727 table, first flag | the source client's own SD/HD split — `TextureLoader.isSd` |
| `!blend_layer` | measured from the frame | fewer than half the frame's texels are alpha 255 |

`valid` is the gate and **it must stay on**. The 204 materials that fail it are
greyscale HD effect programs, and referencing them anyway has been measured
twice: the arena renders as blown-out white and green shards. Being a greyscale
detail map is necessary for a mask and nowhere near sufficient.
`--no-ground-mesh-fallback` exists only to re-run that experiment.

`blend_layer` disqualifies separately, and for a format reason: OSRS239 textures
are **colour-keyed**, not alpha-blended — a texel is drawn or skipped.
Thresholding a continuous alpha ramp invents holes the source never had. On this
lane the two populations are cleanly separated: every diffuse map is 100% alpha
255, every blend layer is 0%.

Kept faces are rewritten to the destination texture id and **never enter the
face bake** — no colour composite, no alpha, no matte. (This is why
`--matte 60` leaves e.g. the tortured soul unchanged: it was processed, not
skipped.)

### Two measured material properties the rules read

Both are computed once per material from the un-quantised frame:

- **`blend_layer`** — `opaque_texels * 2 < 128*128`, i.e. fewer than half the
  texels are fully opaque. *Is the alpha channel continuous?*
- **`greyscale`** — mean per-texel spread between brightest and darkest channel,
  over covered texels only, below `GREYSCALE_CHROMA_LIMIT` (30). *Does the RGB
  mean anything?* The lane's distribution is bimodal either side of it: 225 of
  256 materials sit under 30, a separate cluster of real coloured maps sits at
  90+.

A material can be either, both or neither.

## 3. What happens to an erased face

The face's texture reference is stripped and it falls back to its flat HSL
colour — but not before the tool recovers what it can from the frame. Three
things happen in order: a **footprint sample**, a **colour composite**, and an
**alpha rule**.

### 3.1 The footprint sample

The face's own UV triangle is sampled on a 12-step barycentric grid against the
128×128 frame, repeat-wrapped exactly like the destination sampler. It yields an
alpha-weighted mean colour and a **coverage** — the mean alpha over the
footprint. Nothing is thresholded: a blend layer's coverage *is* its alpha.

- **Degenerate projector fallback.** A face whose UV projector is unusable (bad
  index, or zero area) has no footprint. It falls back to the material's global
  frame mean, and is counted as `degenerate` — 111,935 faces on this lane,
  mostly animated projectors. **A degenerate face keeps its authored alpha**: a
  global mean is a guess about the projector, not evidence about this face, and
  inventing translucency from it turned whole surfaces (the QBD's neck shells
  and belt) into ghosts. The two exceptions are wisp and cutout materials, whose
  authored state never reached the HD screen at all.
- **Uncovered.** If the material's frame has zero coverage anywhere, the face
  was never a visible surface: colour is left alone and the alpha rule makes it
  transparent. 4,080 faces.

### 3.2 The colour composite — `--face-color-bake`

`off` | `tint` | **`modulate`** (default), scaled by `--face-color-strength`
(0-100, default 100). With `k = coverage * strength/100`:

| mode | op | when it is right |
|---|---|---|
| `tint` | `out = mix(base, footprint_mean, k)` | absolute — a diffuse-like map, where the material's colour *is* the surface colour |
| `modulate` | `out = base * mix(1, footprint_mean/material_mean, k)` | relative — the model keeps its authored tone and gains only the frame's spatial variation |

**`modulate` is the lane default** because every QBD material is a greyscale
detail map whose absolute level is meaningless. `tint` on those is the
"blown-out white" failure in a different costume.

Wisp and cutout faces take a **third path** instead — `face_bake_tint_hsl`,
which keeps the authored hue and saturation and takes lightness as the geometric
mean of authored and sampled. HD tinted those greyscale frames with the face's
own colour, so the authored identity (the fin's red/blue/green section keys, the
crest's purple) is the part worth keeping. One carve-out: a **near-black,
near-grey** authored colour is the erased-bake placeholder this whole pass
exists to replace, so the frame colour stands alone there. A dark but
*saturated* authored colour is intent, and still tints.

### 3.3 The alpha rules — the part that has gone wrong

Face alpha is `0 = opaque, 255 = invisible`. Opacities compose multiplicatively
with authored alpha, so the bake can only ever make a face *more* transparent.
Disable the whole section with `--no-face-alpha-bake`.

The dispatch, in order — the first match wins:

| # | rule | test | alpha treatment |
|---|---|---|---|
| 1 | **cutout** | `!blend_layer && !valid && alpha_mode == 2` | coverage → alpha, linear and uncapped |
| 2 | **wisp** | `blend_layer && !valid && coverage < 0.12 && hard_frac < 0.005` | `--wisp-alpha`, **default off** |
| 3 | **mode-2 blend** | `blend_layer && alpha_mode == 2` | coverage → alpha, linear and uncapped |
| 4 | **everything else** | — | opaque, unless `coverage < 0.02` |

`hard_frac` is the fraction of *covered* texels (a > 0.1) that are essentially
solid (a > 0.9) — how much hard silhouette the frame has anywhere.

**Rules 1 and 3 are authored evidence.** `alpha_mode == 2` is the material
table's own 629+ alpha-blending column: the HD engine composites this material
on screen, so a texel's alpha is literal screen opacity. Coverage therefore maps
to face alpha directly, bypassing the gamma and the cap. Rule 1 selects exactly
three materials lane-wide (1520, 1688, 2121), each a dense bimodal frame — a
real surface with hard-edged holes torn in it, like the QBD's ragged dorsal
crest. Rule 3 is the QBD's neck membranes.

**Rule 4 is the default, and it is opaque on purpose.** Mode 0 draws opaque,
mode 1 is a cutout test, and a blend layer's alpha under either is a *layer-mask
weight*, not screen opacity — the QBD's neck strap means ~0.2 and ships solid in
HD. Only a footprint landing entirely in a hole (coverage < 0.02) produces
alpha here.

**Rule 2 is an inference, and it contradicts the table.** Every material the
wisp rule selects on this lane carries `alpha_mode == 0` — HD saying *draw
opaque*. The rule overrides that on the argument that an invalid row has no
layer stack for its alpha to mask, so a frame covering almost nothing with no
hard silhouette anywhere cannot have been a solid surface. That is right for a
standalone filament and wrong for a sparse greyscale detail layer sitting on a
solid rock, **and no field in the material row separates the two**: material
1685 (the QBD crest fringe, which the rule was written for) and material 214
(the arena rocks, which it ruined) are the same shader, the same greyscale, the
same coverage band. So the control is not a better classifier — it is how far
the inference is allowed to go:

| `--wisp-alpha` | behaviour |
|---|---|
| `screen` | the inference wins outright: `sqrt(coverage)` → alpha, linear and uncapped, bypassing gamma and cap. The behaviour through `v10-m60`. |
| `capped` | the same inference, bounded — coverage takes the ordinary gamma and lands under `--face-alpha-cap`. It may soften a surface, never erase it. |
| **`off`** | **default.** The inference loses; the face is the opaque mode-0 layer its row says it is, and only rule 4's hole test applies. |

`screen` is what ghosted the arena. It put 36,527 faces through an uncapped map
— material 214 at coverage 0.097 → `sqrt` 0.311 → alpha 176, 69% see-through on
375 of the rocks' 826 faces. Lane-wide it left the braziers at 85% translucent,
the dragonbone set at ~50%, and four locs rendering as literally nothing.

`off` is the default because a 660-model side-by-side showed it restoring every
solid object (rocks, braziers, kiteshields, the whole dragonbone set, the four
invisible locs) while leaving the QBD itself within 1% of baseline — the crest
the rule was written for does not measurably regress — and leaving **every**
genuinely translucent model pixel-identical, because smoke, spotanims and
membranes reach their alpha through rules 1 and 3, not through this one. Models
≥35% see-through fall 57 → 31, and 29 of those 31 are byte-identical to
baseline.

The wisp *colour* treatment (§3.2) is unaffected at every setting: the
placeholder-colour half of the original wisp bug is fixed by the tint,
independently of opacity.

### 3.4 Gamma, cap and drop

Applied by `face_bake_apply_alpha` to every rule that is **not** flagged
screen-blend (so rules 1 and 3 are exempt, and rule 2 is exempt only under
`--wisp-alpha screen`):

- **`--face-alpha-gamma`** (default 0.45) — coverage is raised to this power
  before becoming opacity, so a derived translucency is pulled back toward
  solid.
- **`--face-alpha-cap`** (default 64) — the resulting alpha is clamped to at
  most 64 (or the authored alpha, whichever is *more* transparent), for any
  face with coverage ≥ 0.02. A derived guess cannot make something more than
  25% see-through.
- **Authored alpha is exempt from both** — coverage 1.0 returns it verbatim.
- **Drop.** A face whose composed alpha reaches 255 is fully invisible, so it is
  **removed from the model** rather than shipped as a triangle the renderer
  would only skip. 6,439 faces. Vertices and bone maps are deliberately left
  alone: unused entries are harmless and vertex skinning stays intact, so
  animations survive.

## 4. Cutout face synthesis — `--face-synth`

A flat per-face colour and alpha reads poorly on the three cutout membranes: HD
shows a pale scaly surface with hard-edged torn holes, and averaging that into
one triangle gives a featureless ghost.

Each non-degenerate cutout face is instead tessellated K×K along its own UV
footprint (`--face-synth-k`, default 3, clamped 2–6), the frame is sampled per
sub-face, sub-faces whose coverage lands below `--face-synth-hole` (default
0.20) are **dropped**, and the rest are tinted. The tears become geometry the SD
renderer can draw. Parents are removed by the existing drop pass; sub-vertices
are deduped by exact rounded coordinate so shared edges stay watertight, and
grid corners reuse the parent's own vertices so the membrane still meets the
surrounding mesh. `--face-synth off` disables it.

Typical run: `parents=1032 sub_faces=8875 holes=413`.

## 5. The matte pass — `--matte N`

OSRS surfaces are matte. The HD frames this bake samples carry the source
engine's baked-in specular, so neighbouring faces of one material can land 30+
lightness steps apart — a gloss gradient the destination style does not have.

`--matte N` (0-100, default **0 = off**; the lane ships **60**) runs a per-model
post-pass over every face *this bake coloured*:

1. compress each face's HSL16 lightness toward the mean lightness of its source
   material's baked faces by N%;
2. compress anything still above the gloss knee (100) toward the knee by N%
   again.

Hue and saturation are never touched. **Faces the bake never coloured are
exempt** — authored geometry and kept-texture faces (§2) are unreachable at any
`--matte` value, which is the usual explanation for "I raised the matte and this
model did not change."

## 6. What is emitted

- sparse lane sprite/texture records (`textures/texture_0.{texture,compack}`),
  ids from `DEST_TEXTURE_BASE` 211 / `DEST_SPRITE_BASE` 8535, quantised to the
  deterministic 6×7×6 sprite palette;
- the five historical **map-floor overlay** materials reserved first and in
  order (348→211, 408→212, 600→213, 616→214, 651→215) — an OSRS overlay stores
  its texture id in a `u8`, so they must land under 256;
- an exact metadata ledger (TSV), with runtime features the OSRS texture record
  cannot express recorded **per row** rather than silently claimed as exact
  (`alpha-threshold-128`, `dual-axis-animation-dominant-axis-only`, …);
- the rewritten OB3s, either into the lane (`--apply`) or flat into a directory
  (`--models-out DIR`, which works without `--apply`).

The tool refuses, rather than approximating: unsupported program graphs, absent
dependencies, unsafe id collisions, unknown model formats, and unmapped textured
faces.

## 7. Flags

| flag | default | §|
|---|---|---|
| `--cache DIR` | `cache.rs727_preeoc` | |
| `--tree DIR` | `OSRS-Content/osrs239-content` | |
| `--apply` | off (dry run) | 6 |
| `--models-out DIR` | — | 6 |
| `--no-ground-mesh-fallback` | fallback on | 2 |
| `--face-color-bake off\|tint\|modulate` | `modulate` | 3.2 |
| `--face-color-strength 0-100` | 100 | 3.2 |
| `--no-face-alpha-bake` | alpha bake on | 3.3 |
| `--wisp-alpha screen\|capped\|off` | `off` | 3.3 |
| `--face-alpha-gamma F` | 0.45 | 3.4 |
| `--face-alpha-cap N` | 64 | 3.4 |
| `--face-synth off` / `--face-synth-k N` / `--face-synth-hole F` | on, 3, 0.20 | 4 |
| `--matte 0-100` | 0 (lane ships 60) | 5 |

Diagnostics: `--face-bake-debug MODEL` (per-material sampling census for one
source model), `--face-dump CSV` (one row per erased-material face, written
after the alpha gates so `final` is what ships; `-1` = dropped, `mid_frac -3` =
kept texture), `--frame-dump DIR` (every mapped material's baked frame as
`mat_N.ppm` + `mat_N_a.pgm`), `--alpha-report`.

## 8. Looking at the result

[`tools/rs2012_qbd_model_sheet.py`](../../tools/rs2012_qbd_model_sheet.py)
renders every model in a `--models-out` directory to a single contact sheet:

```sh
python tools/rs2012_qbd_model_sheet.py \
    --models /tmp/models \
    --out docs/rs2012_materials_backport/sheets/attempt2_wisp_alpha_off_m60.png \
    --stats-out /tmp/stats.json
```

It renders each model twice, on black and on white, and recovers true per-pixel
alpha as `1 - (out_white - out_black)/255` — a single render cannot distinguish
a translucent surface from a dark one. The result is composited over a
checkerboard so translucency is visible, and each tile is labelled with its
source id, config-derived name, and the fraction of covered pixels below 90%
opacity. `--sort soft` and `--only-soft` bring the translucent models together;
`--stats-out` writes the per-model numbers as JSON for diffing two runs.

Note `--near 49` (its default): the engine's fast cull discards models taller
than the default near plane of 50, and the QBD (y −386..386) renders empty
without it.

Sheets live in [`sheets/`](sheets/):

- `attempt1_baseline_m60.png` — `--matte 60`, wisp alpha `screen` (the
  `v10-m60` behaviour). 139 translucent, 57 heavy.
- `attempt2_wisp_alpha_off_m60.png` — `--matte 60`, wisp alpha `off` (current
  default). 80 translucent, 31 heavy.
- `attempt3_determinism_fix_m60.png` — the same configuration after the `p[256]`
  fix (§9). Also 80 translucent / 31 heavy: the fix changed 429 of 660 models,
  but only in the procedural texture noise, which is what it should have done.
  **This is the lane as it ships.**

## 9. Reproducibility — how another machine gets these models

Two machines on the same OSRS-Content commit must render the same lane. Two
separate things had to be true for that, and only one of them is about this
tool.

### The bake is deterministic (fixed)

`proctex_permutations` in
[`src/engine/proctex/proctex_ops.u.c`](../../src/engine/proctex/proctex_ops.u.c)
built its 512-entry Perlin permutation table with `malloc`. The init and shuffle
loops write indices 0..255 and 257..511 — **`p[256]` is never assigned**. The
Java original is a `byte[512]`, which the JVM zero-fills, so there `p[256]` is a
defined 0 and every lookup landing on it is stable; under `malloc` it was
whatever the heap held. The table is cached per seed for the process lifetime,
so a single run was self-consistent and only *separate* runs disagreed — which
is exactly the shape that hides from a re-run and shows up on someone else's
machine.

It is `calloc(512, 1)` now. Two bakes on identical arguments went from **246 of
512 material frames and 392 of 660 models differing** to **0 and 0**.
`rs2012-lane-bake-check` is the standing gate. Only the offline bake was ever
affected: `proctex_generator.o` links into `test-proctex-coverage` and
`rs2012-material-bake` and nothing else, so no shipped client read that table.

### The models have to actually be in the commit

This is the part that bites, and it is not a tool problem. The baked OB3s are
**tracked content** — they live in OSRS-Content at
`osrs239-content/models/ported/rs2012_qbd_td/`. A machine that pulls the content
repo and runs `make -C src mock230-cache-rs2012` therefore needs no RS727 cache
and no bake at all; it packs the models the commit carries.

Which means: if a rebake is not committed *and reachable from the commit the
superproject's submodule pointer names*, nobody else sees it. The failure is
silent — `cachepack pack --base` keeps the base cache's bytes for anything the
overlay does not supply, so a stale or unstaged lane ships the *old* models
rather than erroring. The checklist after a rebake is:

1. commit the lane in OSRS-Content;
2. merge it to the branch the superproject tracks and push;
3. bump the `OSRS-Content` submodule pointer in this repo, commit, push.

Skipping (3) is the common one, and it reproduces as "same commit, different
models" — the submodule pointer, not the branch name, is what a `git submodule
update` resolves.
