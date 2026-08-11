# HD material kernels

Three texture span kernels added to ToriDraw so an imported RS727 (HD) material
can be drawn by a standard-detail rasterizer, what each one does differently,
how a face reaches one, and the before/after that says they work.

Evidence: [`hd_kernels/images/`](hd_kernels/images/). Regenerate with
`tools/hd_kernels_shots.sh`.

Related: [`rs2012_materials_backport/README.md`](rs2012_materials_backport/README.md)
(what the materials are and how the bake classifies them),
[`../RS2012_BACKPORT.md`](../RS2012_BACKPORT.md) §2–§4.

---

## 1. The four kernels

SD's contract for a textured face is one line: `pixel = texel × lightness`. The
texture *is* the surface. Every kernel below is a different answer to "what if
it isn't".

| kernel | what a texel means | where a texel is absent | face is |
|---|---|---|---|
| `..._opaque_blend_...` (stock) | the surface | n/a, all texels drawn | opaque |
| `..._transparent_blend_...` (stock) | the surface | pure black = skip | see-through |
| `raster_linear_alpha_blend_lerp8_v3` | the surface | blend by the texel's own alpha | see-through |
| `raster_linear_alpha_modulate_lerp8_v3` | a **mask**: greyscale detail, colour comes from the face | blend by alpha | see-through |
| `raster_linear_detail_lerp8_v3` | a **detail map**: scales the colour the face would have had | neutral texel = no change | **opaque** |

All three new ones live in
[`3rd/toridraw/graphics/raster/texture/span/tex.span_uv.h`](../3rd/toridraw/graphics/raster/texture/span/tex.span_uv.h),
beside the uv rules they share, because they are scalar control flow around the
peer-declared 8-pixel kernels and contain no intrinsics in any ISA file.

### alpha — coverage instead of a colour key

Stock OSRS textures are colour-keyed: a texel is drawn or skipped, and there is
no in-between. An imported material whose alpha varies continuously can only be
thresholded, which invents holes the source never had — that is what produced
the QBD's neck striping. This kernel composites each texel over the framebuffer
by its own alpha.

### modulate — the colour belongs to the face

225 of the lane's 256 materials are greyscale: their RGB is a detail pattern,
not a colour. RS727 got the surface colour by combining the material with the
model's own; SD cannot, because texturing a face is exactly what discards its
hue and saturation (the lighting overwrites `colors_a/b/c` with plain
lightness). So this kernel multiplies the texel by the face's chroma, which the
raster reads from `face_colors` — still on the drawable model, just unused until
now — at a fixed reference lightness, once per face.

**The lightness trap.** Tint with chroma only, never the authored lightness: for
a textured face the lightness already arrives as the shade, so folding it in
again counts it twice and every lightness-0 face goes black. Material 2164 did
exactly that and vanished.

### detail — the texture is not the surface at all

The remaining 204 materials fail the ledger's `valid` flag, and the bake used to
erase the texture from every face naming one — 274,715 lane faces, 95% of the
QBD. They are HD *programs*: things that modulated something else in the source
renderer. Drawn as a surface they are blown-out white; skipped, the model loses
its detail; blended against the framebuffer they show whatever the painter drew
first, which is the striping.

This kernel does not treat them as the surface. It reconstructs the colour the
face would have had **without** the texture — its chroma looked up in the
palette at the per-pixel lightness, which is precisely what the gouraud path
draws — and lets the texel darken it. Three consequences:

- **every pixel is written**, so the face is opaque: no holes, and no dependence
  on what was drawn underneath. The same face renders the same whatever the sort
  does with it.
- **the brightest texel is the identity, and it can only darken.** A program
  that bakes to nothing useful degrades to the flat colour the lane already
  falls back to, rather than to white. That is what makes referencing all 204
  safe.
- it cannot brighten at all, deliberately — see below.

**Two mistakes worth not repeating**, both caught by looking at the sheet:

*Rebuild the base through the palette, not by multiplying.* The first version
computed the base as `chroma-at-mid-lightness × shade`, a straight line, where
the palette's lightness ramp is a curve that washes toward white at the top and
falls off at the bottom. A textured face then had a visibly steeper gradient
than the untextured faces beside it.

*Bound the gain at 1.* The version after that used a neutral midpoint so a
bright texel brightened. On top of a base that already washes to white, any gain
above 1 clamps whole regions to flat white — worse than what it replaced. A
detail map that can only remove light cannot do either.

## 2. How a face reaches a kernel

```
rs2012_material_bake            classifies the material, writes flags
        |                       into the texture record
        v
texture_0.texture               alpha=yes / modulate=yes / detail=yes
        |
        v  cachepack
v2 texture record + 8th byte    RSCACHE_TEXTURE_EXT_{ALPHA_BLENDED,MODULATE,DETAIL}
        |                       bitfield; absent from every stock record
        v  task_dat2_texture_load -> UITreeSceneBridge_PublishTextures
ToriDraw_Texture                .alpha_blended .modulate .detail
        |
        v  toridraw_raster.u.c, per face
   alpha_blended || detail  ->  ToriDraw_TriangleFaceTextureBlendAlpha(tint)
        |                       tint = face chroma at TORIDRAW_MODULATE_LIGHTNESS,
        |                       tint.detail selects the detail kernel
        v
   tex.span_uv.h                per 8-pixel block:
                                  tint && tint->detail -> detail kernel
                                  tint                 -> modulate kernel
                                  else                 -> alpha kernel
```

The dispatch is at [`toridraw_raster.u.c`](../3rd/toridraw/toridraw_raster.u.c),
tested **before** opaque and before transparent: an alpha texture is never
marked opaque, and the transparent path would threshold it into holes.

**Stock content is untouched.** The flags ride an optional 8th byte on a record
that is fixed at seven bytes everywhere else, written only when non-zero, so a
stock cache is byte-identical and a stock texture takes exactly the path it
always did. The tint *selects between* kernels rather than being threaded
through one, so a stock face does not pay three multiplies a pixel to multiply
by white.

## 3. The evidence

`tools/hd_kernels_shots.sh` renders each subject three times from **one set of
assets**, flipping only a runtime switch, then diffs the pairs. Nothing is
re-baked between them, so a difference is the kernel and cannot be an asset
change.

| switch | meaning |
|---|---|
| `TORIDRAW_TEX_LEGACY=1` | both flags ignored — what the renderer did before |
| `TORIDRAW_NO_MODULATE=1` | coverage on, tint and detail off |
| *(neither)* | shipping |

| subject | coverage changed | modulate+detail changed | net |
|---|---:|---:|---:|
| [QBD head](hd_kernels/images/qbd_head_sheet.png) | 4.4% | 30.7% | **31.2%** |
| [QBD body](hd_kernels/images/qbd_body_sheet.png) | 1.8% | 8.0% | 8.1% |
| [giant worm](hd_kernels/images/worm_sheet.png) | 7.6% | 10.1% | 9.9% |
| [tortured soul](hd_kernels/images/soul_sheet.png) | 0.0% | 0.0% | 0.0% |

The masks are beside each sheet (`*_diff.png`) — "these two differ" is not a
claim a reader should have to take on trust.

**What to look at.** On the head: legacy and coverage-only are a white and grey
metal mess; with the kernels on, the head is red-brown, the horns tan, the frill
purple — the colours the model was authored with, arriving from the faces rather
than from the textures. That is the whole point of the modulate and detail
kernels in one picture.

**The tortured soul scores 0.0% and that is correct**, not a broken case: none
of its materials classify as masks or detail maps, so no face on it reaches a
new kernel. A subject that does not change is worth keeping in the sheet — it
says the kernels are gated and not a global filter.

## 4. Reproducing

```sh
# assets: classify and flag the materials (writes the content tree)
src/build_win64_opt/rs2012_material_bake.exe --detail-textures --apply

# the sheets, straight from the content tree - no cache pack needed
tools/hd_kernels_shots.sh
SUBJECTS=qbd_head tools/hd_kernels_shots.sh      # one subject

# the kernels in isolation, ~1s, no assets at all
make -C src test-texture-alpha-blend
```

In the live client, the same switches work and the arena harness parks the
camera so a capture frames the subject:

```powershell
.\tools\qbd_shot.ps1 -Yaw 1024 -Pitch 300 -Frames 700 -TexDebug
$env:TORIDRAW_TEX_LEGACY="1"   # same command again for the before
```

`TORIRS_RASTER_TEX_DEBUG=1` prints `raster_tex_alpha: faces=<n> id=<texture>`,
which is how you confirm faces are reaching a new kernel at all, and
`raster_tex_skip` if a texture never arrived.

## 5. Limits

- **The detail kernel is a rescue, not a port.** It makes an HD program
  contribute plausible detail instead of garbage; it does not make it the
  material the source renderer drew. The upstream question — what those 204
  programs actually are, and whether they can bake to a real surface map —
  is still open (`RS2012_BACKPORT.md` §2).
- **Draw order still matters for the alpha and modulate kernels.** They
  composite against the framebuffer, so a see-through face shows whatever the
  painter put down first. Only the detail kernel is order-independent, because
  it is opaque. If striping remains on faces that use *coverage*, this is where
  to look.
- The viewer's texels come from the content tree, so they skip the pack
  palette's gamma 0.8; a viewer frame is slightly brighter than the client's.
  Both sides of an A/B see the same texels, so a difference between them is
  still the kernel.
