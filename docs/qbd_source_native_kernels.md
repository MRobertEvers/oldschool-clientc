# Rendering the RS727 QBD from source: the kernel list

What a native software renderer would have to draw to render the Queen Black
Dragon **from the RS727 cache**, rather than from the backported OSRS239 lane.
This is a scoping document — it lists kernels and says why each is needed; it
implements none of them.

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
| `texshadeblend.persp.texopaque.facealpha.branching.lerp8_v3` | **added in this change.** Opaque-frame materials on a face that carries alpha |
| `texshadeblend.persp.textrans.facealpha.branching.lerp8_v3` | **added in this change.** Only useful if a frame is reduced to a colour key — see 2.2(1) for why that is the wrong answer here |

### 2.2 New kernels the source model needs

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

**(1) Cube-map uv generation (render type 2). The largest single gap.**

1,338 of 6,533 textured faces per model, across materials 1420, 1607 and 1685.
`RSCache_ModelNewDecode` **counts** these faces and then advances past their
parameter bytes without decoding them — only render type 0 populates
`textured_p/m/n_coordinate` ([`3rd/rscache/src/datatypes/model.c:1836`](../3rd/rscache/src/datatypes/model.c#L1836)).
So today those faces reach the raster with no texture projector at all. This is
a decoder change plus a uv generator, and no kernel work matters until it lands:
a fifth of the dragon's textured surface has nothing to sample with.

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

## 4. Summary

Four new kernels — per-texel alpha, per-texel alpha with face alpha, a modulate
variant of each, and a clamp-addressed sampler — plus the two textured facealpha
kernels that landed with this change. Of those, the modulate pair and the alpha
pair are the ones the QBD cannot be drawn correctly without.

But the ordering matters: **cube-map uv is upstream of all of it.** Until the
decoder reads render-type-2 parameters, 20% of the dragon's textured faces have
no projector, and that is a model-decode problem, not a raster one.
