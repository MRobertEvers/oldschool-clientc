# RS2012 blend-layer materials on an SD rasterizer

*Was this folder's `README.md` until the porting rules took that name; see
[`README.md`](README.md) for what the tool does today.*

> **HISTORICAL — the renderer half of this is gone.** The three span kernels
> this document describes (per-texel alpha, modulate by face colour, opaque
> detail map), the OB_TORI container that routed them per face, the dat2 texture
> extension byte, and the `rs2012_material_bake --alpha-textures /
> --detail-textures` flags that emitted it have all been removed. Any command
> below naming those flags, or `make -C src test-texture-alpha-blend`, no longer
> exists. `git log` has the code.
>
> What stays true is the **analysis**: which imported materials are masks rather
> than diffuse maps, which are greyscale, and what each is used with. That is
> the part worth keeping, and it is why this folder was not deleted with the
> kernels. The problem it describes — a lane whose materials cannot be drawn as
> surfaces — is still open. See
> [`../qbd_port_compare/README.md`](../qbd_port_compare/README.md).

How the imported RS727 materials that are **masks rather than diffuse maps** are
made to render on ToriDraw, which is standard-detail only.

Every mask under [`images/`](images/) is rendered straight out of the content
tree by [`dump_materials.py`](dump_materials.py): the first row of each sheet is
what the cache packer consumes, and the rows below simulate what the raster
makes of it for each face colour it is used with. [`masks.tsv`](masks.tsv) and
[`bake_report.txt`](bake_report.txt) are the machine-readable forms.

Related: [`../../RS2012_BACKPORT.md`](../../RS2012_BACKPORT.md) §2 and §3 (which
established that these materials existed and why they were being discarded),
[`../qbd_toridraw_streaks_debug.md`](../qbd_toridraw_streaks_debug.md).

---

## 1. What the problem is

39 of the lane's 256 materials bake with **no fully-opaque texel at all**. They
are not textures of a surface; they are overlays — a hair fringe, a soft patch,
a torn fur edge — whose shape lives in the alpha channel and whose RGB is a
greyscale detail pattern.

The Queen Black Dragon's three are the clearest case:

| material | dest | alpha 0 | 1–127 | 128–254 | 255 | what it is |
|---:|---:|---:|---:|---:|---:|---|
| 1218 | 334 | 59.3% | 26.7% | 14.0% | **0%** | the wispy fringe along the crest |
| 1554 | 380 | 17.2% | 70.9% | 11.9% | **0%** | a soft blob — the neck gashes |
| 2164 | 459 | 52.5% | 3.8% | 43.7% | **0%** | a torn fur edge |

Measured over the covered texels, their RGB is very close to neutral grey —
`(195,201,195)`, `(164,163,164)`, `(70,71,70)`. That is the signature of a mask:
the colour was never meant to be the surface's colour.

Two things then go wrong, and they are independent:

**The colour is wrong.** SD's contract for a textured face is `texel × lightness`.
There is no face-colour modulate, and there cannot be one without leaving SD.
RS727 produced the dark fur by combining the mask with the model's own colour;
we drew the mask's literal grey.

**The coverage was being thrown away.** OSRS239 textures are colour-keyed — a
texel is drawn or skipped — so a continuous alpha ramp had to be thresholded,
which invents holes the source never had. That is what made the QBD's neck
stripe. The lane's answer until now was to discard these materials entirely and
fall the faces back to flat colour (`RS2012_BACKPORT.md` §3), which is correct
but loses the detail.

## 2. What is done about it

Two changes, one per fault.

### Per-texel alpha, end to end

A texture may now carry real coverage instead of a colour key:

- an optional **8th byte** on the fixed-7-byte v2 texture record
  (`RSCACHE_TEXTURE_V2_ALPHA_BYTE`). The record has no opcode stream, so length
  is the only way to announce a field: a stock cache always writes exactly seven
  bytes, so a longer record is unambiguously ours, and a stock record decodes
  with `alpha_blended = false`. The byte is written **only when set**, so nothing
  in a stock cache changes by a single byte.
- the coverage itself rides the **stock dat2 sprite alpha plane** (`FLAG_ALPHA`),
  which the format already has and the encoder already emits when the alphas
  cannot be re-derived from the palette index.
- the raster gained a third span kernel,
  `raster_linear_alpha_blend_lerp8_v3`, dispatched on `alpha_blended` ahead of
  both the opaque and the transparent paths.

Enabled by `rs2012_material_bake --alpha-textures`.

### The colour: a modulate kernel

`raster_linear_alpha_modulate_lerp8_v3` is the fourth kernel. It is the alpha
kernel plus a per-channel multiply by a tint the caller supplies, selected by
the record's `modulate` flag — a second bit in the same extension byte.

The tint is the **face's own colour**, which is already there: `face_colors`
survives onto the drawable model, and the lighting only drops it for textured
faces (it overwrites `colors_a/b/c` with plain lightness). So the raster reads
it per face, converts it once, and hands the kernel three channel scales. No new
model format is needed and nothing about the geometry changes.

Two details that are not obvious, and both of which were wrong when this was
first attempted in the bake instead:

**Tint with the chroma, never the authored lightness.** A face colour's
lightness is not the surface's brightness — the lighting pass overwrites it per
vertex from the face normal, which is why an untextured face authored at
lightness 0 still renders lit rather than black. For a textured face that
lightness arrives at the kernel as the *shade*, so folding the authored value in
as well counts it twice and every lightness-0 face's mask collapses to black.
(Material 2164 did exactly this: it vanished.) The tint is therefore
`hsl16 & 0xFF80 | TORIDRAW_MODULATE_LIGHTNESS` — hue and saturation at the
midpoint of the 0..127 range, where the palette gives the pure hue.

**Normalise the mask by its own peak, at bake time.** A mask's overall level is
whatever the HD program happened to produce — the three above peak at 255, 209
and 163 — so an un-normalised mask dims the face colour by an amount that means
nothing (material 2164 would render every face it touches at 64%). Scaling by
the peak anchors the mask's brightest texel to the full face colour. That one
*is* an asset property, so it stays in the bake.

Both kernels are unit tested against an independent per-channel reference by
[`3rd/toridraw/toridraw_texture_alpha_blend_test.c`](../../3rd/toridraw/toridraw_texture_alpha_blend_test.c)
(`make -C src test-texture-alpha-blend`), including that a white tint is the
exact identity — which is why the tint is packed to 0..256 rather than 0..255.

## 3. Why both belong in the renderer, and what stays in the bake

Neither of the two properties a mask has — continuous coverage, and a colour
that belongs to the face — can be expressed in a stock OSRS texture. Coverage
can only be colour-keyed, which thresholds a gradient into holes the source
never had. Colour can only be baked into the texel, which is what a diffuse map
is and a mask is not.

Both were tried in the asset first. Baking the tint per (material, face colour)
does work: it produced 82 extra destination textures over 39 materials and the
arena rendered. It was replaced because it is worse on every axis that matters —
82 extra textures and sprites to allocate, collide-check and ship; a texture per
colour rather than per material, so the count grows with the content rather than
with the material set; and the tint quantised through the 6x7x6 palette twice
instead of being applied exactly at draw time.

What makes a kernel the right home here, where an HD modulate generally would
not be, is that it is **gated per texture by a flag stock content never sets**.
A stock diffuse map takes the path it always did, at the cost it always had: the
tint selects between two 8-pixel kernels rather than being threaded through one,
so nothing pays three multiplies a pixel to multiply by white.

What stays in the bake is what is genuinely an asset property: the peak
normalisation, and the decision that a material is a mask at all.

## 3a. What this does NOT fix — the 204 discarded materials

Only 42 of 256 materials reach a kernel. The other 204 fail the ledger's `valid`
flag and the bake **erases the texture reference from the face before the raster
ever sees it** — 274,715 lane faces. On the Queen Black Dragon that is 8,551 of
9,012 faces: the body, the scales, the head are all still flat colour, and no
kernel can change that, because those faces no longer name a texture.

It is tempting to think the modulate kernel rescues them, because measuring the
204 shows they are greyscale detail maps — the same shape of asset as the 42,
and their mean RGB explains the "blown-out white shards" that
`RS2012_BACKPORT.md` §2 recorded when they were last referenced (materials 57
and 75 average 236 and 231 — near-white, which is what a mask looks like when
nothing tints it).

**Tested, and it does not.** With modulate in place and the fallback lifted for
every greyscale material, lane fallback faces drop 274,715 → 26,294 and the
arena renders as white and green striped shards — the same failure, now tinted.
Being a greyscale detail map is necessary for a mask and nowhere near
sufficient: these are HD effect programs whose baked 128×128 frame is not a
surface map at any tint. The gate stays on `valid`.

So the open item from `RS2012_BACKPORT.md` §2 is still open, and is now narrower
than it was: it is not "the renderer cannot combine a mask with a face colour"
(it can, since this change) but "these 204 programs do not bake to anything a
surface sampler can use". That is the reclassify-or-HD-renderer question, and it
is upstream of everything here.

## 4. What it costs, and the invariants

- **No extra textures or sprites at all.** 39 masks, 39 destination textures,
  the ids they already had. They are used with 82 distinct face colours across
  20,651 faces, and one texture serves all of them.
- **Three multiplies a pixel, on masked faces only.** A stock textured face is
  unaffected: `modulate` is false, so it takes the plain kernel.
- **Stock OSRS content is untouched.** The mechanism is an optional trailing
  byte that stock records never carry plus a sprite plane the format already
  defines. A reader that predates the extension stops at seven bytes.
- The bake **skips hand-authored lane models** (`rs2012_model_<id>_<suffix>`):
  they have no RS727 source to re-read and no source materials to remap.

## 5. Verifying

```sh
# the kernel, in isolation - about a second, no cache needed
make -C src test-texture-alpha-blend

# the bake, without writing anything
src/build_win64/rs2012_material_bake.exe --alpha-textures --alpha-report

# apply, keep priorities (do NOT run rs2012_strip_priorities), re-pack, run
src/build_win64/rs2012_material_bake.exe --alpha-textures --apply
make -C src torirsserver-cache-rs2012
./dist/win64/torirs.exe --manifest manifest_osrs239_rs2012.ini --soft3d
```

Runtime evidence that the path is live, with `TORIRS_RASTER_TEX_DEBUG=1`:

```
raster_tex_alpha: faces=314501 id=380      # faces reaching the alpha kernel
```

and in the per-model NDJSON, the QBD:

```
faces=9012 drawn=7061 TEX=461 tex_miss=0 tex_ids=[334, 459, 380]
```

Those are the masks' own texture ids - the fringe, the fur edge and the neck
blob - with nothing between them and the face. `tex_miss=0` means every textured
face found its texture.

> **Two known gates fail for unrelated reasons.** `torirsserver-cache-rs2012` exits
> non-zero *after* producing the cache: the sprite fidelity gate reports every
> sprite as length-changed, and it does so for the **stock** `cache.osrs239`
> too (8559/8559, including the `codec` column, which is the library's own
> decode→encode with no text involved). That is a pre-existing codec or gate
> regression, not this work. `torirsserver-cache-check` — all 23 tables present —
> does pass.

## 6. Appendix — the materials

One sheet per mask in [`images/`](images/): the mask as baked (rgb, alpha,
composited over a surface), then what the modulate kernel makes of it for each
face colour it is used with. The QBD's three:

- [material 1218](images/material_1218.png) — crest fringe
- [material 1554](images/material_1554.png) — neck blob
- [material 2164](images/material_2164.png) — fur edge

Full table in [`masks.tsv`](masks.tsv); regenerate everything with:

```sh
src/build_win64/rs2012_material_bake.exe --alpha-textures --alpha-report \
    > docs/rs2012_materials_backport/bake_report.txt
python docs/rs2012_materials_backport/dump_materials.py
```
