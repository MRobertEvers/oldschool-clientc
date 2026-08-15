# Rendering the RS727 QBD from source: the kernel list

What a native software renderer would have to draw to render the Queen Black
Dragon **from the RS727 cache**, rather than from the backported OSRS239 lane.
**Status: the kernels and the decoder half are implemented.** This began as a
scoping document; §2.2 and §3(1) are now done and are marked so inline. What
remains is one kernel family (§5) and the plumbing that reaches it.

Everything below is measured, not inferred. The numbers come from

```sh
make -C src rs2012-qbd-kernel-survey
./src/build_opt/rs2012_qbd_kernel_survey            # models 70260 / 70267 / 70268
./src/build_opt/rs2012_qbd_kernel_survey --model 70260
```

([`src/engine/proctex/test/rs2012_qbd_kernel_survey.c`](../src/engine/proctex/test/rs2012_qbd_kernel_survey.c)),
which reads `cache.rs727_preeoc` read-only, plus the per-material alpha
histograms in
[`rs2012_materials_backport/bake_report.txt`](rs2012_materials_backport/bake_report.txt).

Related: [`rs2012_materials_backport/README.md`](rs2012_materials_backport/README.md)
(what the backport does *instead* of all this),
[`rs2012_materials_backport/HISTORICAL_alpha_kernels.md`](rs2012_materials_backport/HISTORICAL_alpha_kernels.md)
(a previous attempt at two of these kernels, since removed),
[`RASTER_VARIANT_CATALOGUE.md`](RASTER_VARIANT_CATALOGUE.md) (the naming grammar).

---

## 1. What the model declares

Per model (70260; 70267 and 70268 differ by single-digit counts):

| | |
|---|---|
| vertices / faces | 4,816 / 6,863 |
| textured faces | 6,533 (95.2%) |
| face shading | **gouraud 6,863, flat 0, hidden 0** |
| texture render types | simple(0) 226, **cube(2) 7**, cylinder(1) 0, sphere(3) 0 |
| faces on cube-projected uv | **1,338 of 6,533 textured (20.5%)** |
| faces with non-zero alpha | 166 (40 at 1–63, 40 at 64–127, 80 at 128–254, 6 at 255) |
| face priorities | present — p0=596 p2=498 p4=4,768 p5=144 p6=117 p7=730 p8=10 |
| distinct materials | 13 (15 across all three models) |

The 15 materials, with the properties that decide compositing:

| material | faces (×3 models) | valid | alpha_mode | repeat | shader | uv types (simple/cube) | baked frame alpha |
|---:|---:|:--|---:|:--|---:|:--|:--|
| 394 | 624 | no | 0 | both | 0 | 624/0 | 100% opaque |
| 693 | 618 | no | 0 | both | 0 | 618/0 | 100% opaque |
| 694 | 450 | no | 0 | both | 0 | 450/0 | 100% opaque |
| 1218 | 636 | **yes** | **2** | **s only** | 0 | 636/0 | **0% opaque** (59/27/14) |
| 1394 | 2,112 | no | 0 | both | 0 | 2112/0 | 100% opaque |
| 1420 | 918 | no | 0 | both | 1 | 228/**690** | **0% opaque** (0/78/22) |
| 1549 | 3,078 | no | 0 | both | 0 | 3078/0 | 100% opaque |
| 1554 | 246 | **yes** | **2** | **neither** | 6 | 246/0 | **0% opaque** (17/71/12) |
| 1607 | 2,118 | no | 0 | both | 1 | 30/**2088** | **0% opaque** (0/100/0) |
| 1685 | 1,236 | no | 0 | both | 1 | 0/**1236** | **0% opaque** (0/100/0) |
| 1688 | 360 | no | **2** | **s only** | 0 | 360/0 | mixed (17/6/3/**73**) |
| 2121 | 546 | no | **2** | both | 0 | 546/0 | mixed (1/15/7/**77**) |
| 2164 | 432 | **yes** | **2** | both | 0 | 432/0 | **0% opaque** (53/4/44) |
| 2171 | 3,078 | no | 0 | both | 0 | 3078/0 | 100% opaque |
| 2172 | 3,078 | no | 0 | both | 1 | 3078/0 | **0% opaque** (0/33/67) |

Alpha columns are `0 / 1-127 / 128-254 / 255` percentages of the 128×128 baked
frame. **9 of 15 materials contain no fully-opaque texel at all**, and 2 more are
mixed. Every material is `combine_mode 0` (modulate), `anim_u == anim_v == 0`,
`mipmap 2`.

---

## 2. The kernels

### 2.1 Already in the tree, and used as-is

| variant | what it draws here |
|---|---|
| `gouraudhsllightness.screen.opaque.bary.branching.s4` | the ~330 untextured faces per model |
| `gouraudhsllightness.screen.alpha.bary.branching.s4` | untextured faces among the 166 carrying alpha |
| `texshadeblend.persp.texopaque.branching.lerp8_v3` | the 6 fully-opaque materials — 4,346 of 6,533 textured faces on model 70260 |
| `texshadeblend.persp.texopaque.facealpha[.modulate].branching.lerp8_v3` | opaque-frame materials on a face that carries alpha — part of the §2.2 matrix |

### 2.2 New kernels the source model needs — **implemented**

All four landed as one flag matrix,
[`texshadeblend.persp.tex2.branching.lerp8_v3.u.c`](../3rd/toridraw/graphics/raster/texture/texshadeblend.persp.tex2.branching.lerp8_v3.u.c):
ten variants over gate x `facealpha` x `modulate`, sharing one walker template
with the plain kernels, with addressing carried as sampler data rather than as an
eleventh variant axis. Verified by `make -C src test-texture-matrix` — eight
bit-exact identities against the plain SIMD kernels, then the algebra chained off
them; six mutations of the span and sampler are each caught.


**(1) `texshadeblend.persp.texalpha.branching.lerp8_v3` — per-texel alpha.**

The load-bearing one. Nine materials have a continuous 0–255 alpha ramp and no
fully-opaque texel anywhere. The existing `textrans` gate is a *one-bit* colour
key: a texel is drawn or skipped. Thresholding a ramp through it invents hard
holes the source never had — that is the documented "QBD neck stripe" failure
(`HISTORICAL_alpha_kernels.md` §1). Drawing them through `texopaque` instead
replaces every soft edge with a hard one.

Per pixel: `dst = alpha_blend(texel_a, dst, shade_blend(texel_rgb, shade))`,
with `texel_a` the frame's own alpha byte. Needs a read-modify-write, so like
the facealpha spans it sits outside the per-ISA SIMD rotation.

**(2) `texshadeblend.persp.texalpha.facealpha.branching.lerp8_v3` — both alphas.**

166 faces per model carry a face alpha, and they are not disjoint from the
blend-layer materials. The two weights compose multiplicatively
(`(texel_a * face_a) >> 8`), which is one kernel, not two passes.

**(3) `texshadeblend.persp.texalpha.modulate.branching.lerp8_v3` — face-colour tint.**

12 of 15 materials are `valid == false`: greyscale detail maps whose measured
RGB is near-neutral and whose colour is *not* the surface colour. RS727 combined
them with the model's own face colour. SD's contract is `texel × lightness` with
no colour term, so drawn literally they render grey — this is exactly the fault
`HISTORICAL_alpha_kernels.md` §2 was written for, and that document also records
the two traps: tint with hue and saturation only (the authored lightness already
arrives as the shade, so folding it in counts it twice and collapses
lightness-0 faces to black), and normalise the mask by its own peak at bake time.

That kernel existed once and was removed. It would be coming back.

**(4) Clamp addressing — a `texclamp` axis on the sampler.**

Materials 1218 and 1688 are `repeat_s` only, 1554 is neither. Every current span
hardcodes wrap through `u_mask` / `v_mask`. Only two of the four combinations are
actually referenced (clamp-t, and clamp-both), so this is a sampler variant
rather than a full matrix.

### 2.3 Not needed, on the evidence

- **`texshadeflat.*`** — every one of the 6,863 faces is gouraud. There are zero
  flat faces on any of the three models.
- **Animated uv** — `anim_u` and `anim_v` are 0 on all 15 materials. (25 materials
  animate *lane-wide*; none of them is on the QBD body.)
- **Cylinder (type 1) and sphere (type 3) uv** — zero faces.
- **alpha_mode 1 (cutout)** — zero materials. Only modes 0 and 2 appear.
- **combine_mode other than modulate** — all 15 are mode 0.

---

## 3. Not kernels — prerequisites without which no kernel helps

**(1) Cube-map uv generation (render type 2) — decoder and generator done.**

1,338 of 6,533 textured faces per model, across materials 1420, 1607 and 1685.
`decode_ob3` used to **count** these faces and advance past their parameter bytes
without decoding them, so they reached the raster with no projector at all.

Both halves now exist:

- **Decode.** `decode_ob3` reads the complex mapping sections with six
  independent cursors (the sections cannot be walked with one, because a face
  draws from a section only if its render type calls for it). New fields on
  `RSCache_Model`: `texture_scale_x/y/z`, `texture_rotation`,
  `texture_direction`, `texture_speed`, `texture_trans_u/v`. Per-section cursor
  asserts catch any mis-sized block. Verified over 72,069 models of
  `cache.rs727_preeoc` (0 decode failures, 463,439 complex faces) and by the
  library's own byte-exact model round-trip, still 100% on all five caches.
- **Generation.** [`model_texture_uv.c`](../3rd/rscache/src/datatypes/model_texture_uv.c)
  ports the reference's `computeTextureCoords` / `calculateTextureScales` for all
  four render types, producing explicit per-vertex uv. Across the whole cache:
  26.8M textured faces, **0 non-finite**, and the QBD's cube faces come out at a
  mean span of 0.21 tiles — i.e. a sane projection.

Two findings came out of that sweep and are recorded rather than papered over:

- **467 type-0 faces have a degenerate projector** (p/m/n collinear), where the
  reference divides by zero and gets Infinity. JS carries that harmlessly; here
  it would reach a rasterizer and poison a whole triangle, so the determinant is
  checked and the face collapses to one texel.
- **The 24-bit scale field's signedness is unresolved.** The only reference in
  reach (`ByteBuffer.readMedium`) is unambiguously unsigned and this decoder
  matches it, but `calculateTextureScales` tests `scaleX <= 0`, which is dead
  code under an unsigned read. The sweep shows why that matters: 0.316% of scale
  samples set the high bit, and the worst cylinder face comes out with a uv span
  of **16384.31 ≈ 0xFFFFFF / 1024** — exactly what a small negative scale read as
  a large unsigned one produces. Under a signed read that face takes the
  `scaleX <= 0` branch and lands at a sane scale. This does not affect the QBD,
  which uses only cube faces, where the scales are consumed as `64/scale` with no
  sign test.

**(2) The proctex frame's alpha plane has to survive to the raster.**
`ProcTexGenerator_Render` already emits ARGB8888 and reports `out_transparent`.
The upload path must not threshold or discard that channel — which is precisely
what the OSRS239 backport does, because a stock texture record cannot carry it.

**(3) Texture size.** The span kernels accept 64 and 128 only (`texture_shift`
6 or 7). proctex bakes at whatever size is asked for and the lane uses 128, so
this is satisfied — but three materials are flagged `small`, and if that is
honoured as a 64×64 bake the sampler already handles it.

**(4) Face priority ordering.** These models carry real per-face priorities
spread over seven levels. The existing face sort handles priorities; worth
confirming against the source model rather than assuming, since 4,768 of 6,863
faces sit at a single level and the remainder is what separates the membranes
from the body.

**(5) Unknowns, flagged rather than scoped.** `shader_id` is 1 on four materials
and 6 on one, with no reference in reach describing what those shaders do.
`mipmap` is 2 on every material; SD never mipmapped, so ignoring it is a
deliberate divergence rather than an oversight.

---

## 4. What landed

- **Ten texture kernels** (§2.2), one flag matrix, `make -C src test-texture-matrix`.
- **The decoder** (§3.1), reading every complex mapping parameter, validated over
  72k models with the byte-exact round-trip still green.
- **The uv generator** (§3.1) for all four render types, 0 non-finite over 26.8M
  faces.

## 5. What is still missing

**A kernel that consumes explicit per-vertex uv.** Every textured kernel in
ToriDraw derives its uv basis from three orthographic vertex *positions* — a
plane. That is the right representation for render type 0 and cannot represent
types 1 and 3 at all, which are non-linear in the vertex (atan2 / asin). The
generator produces explicit uv because that is the only representation covering
all four types, and nothing currently rasterizes it.

So the QBD's 1,338 cube faces per model now have correct texture coordinates and
still cannot be drawn. That kernel — perspective-correct interpolation of u/z,
v/z and 1/z, reusing the sampler and gate matrix from §2.2 — is the next piece.

Also still open, unchanged by this work: routing any of it from
`toridraw_raster.u.c` (which passes no alpha, tint or sampler for textured
faces), the material-to-sampler binding that decides which variant a face takes,
and the `shader_id` 1/6 and `mipmap` unknowns in §3(5).
