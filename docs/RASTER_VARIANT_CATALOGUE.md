# Rasterization variant catalogue

This document is the **standalone export** of the master variant table, including a **Variant_ID** column for every **F** / **G** / **TF** / **TS** row. IDs follow the grammar documented in the Cursor plan (see link below): `<family>.<space>.<gate>.<kernel>[.<walk>][.<layer>][.vec]`, with optional `legacy:`, `unused:`, or `reference:` prefixes.

**Canonical narrative** (SIMD chains, face/scanline/span naming, proposed file layout, migration notes) lives in the plan — do not duplicate it here unless you need an offline copy.

- **Plan (SIMD, naming, migration):** [Raster variant catalogue](../.cursor/plans/raster_variant_catalogue_2aabb9e0.plan.md)

> **The `gouraud` family is now `gouraudhsllightness`.** The name was always
> describing what the kernel does *not* do: it interpolates the packed HSL16
> word and resolves it through the palette, so hue and saturation ride in the
> high bits and only lightness is meant to vary. A gradient between two
> vertices of genuinely different hue walks *through* the palette rather than
> between the two colours. The rename is confined to the kernel layer - the
> directory, the files, and the `raster_*` / `draw_scanline_*` / `scanline_span_*`
> symbols. `ToriDraw_TriangleFaceGouraud*`, `FACE_TYPE_GOURAUD` and
> `TORIDRAW_ZBUF_MODE_GOURAUD` are shading-mode names, not kernel names, and are
> unchanged.
>
> **`gouraudrgb` (GR1/GR2) is the family that does interpolate colour**, per
> channel, with no palette in the path.
>
> **Textured families are named for their projection.** `texplane` (TS12) is
> render type 0: the face names three vertices whose positions *are* the texture
> plane, and the kernel walks it. `texcylinder`, `texcube` and `texsphere`
> (render types 1/2/3) carry a *mapping* instead — a projection about the face
> group's centre — and are **not implemented yet**; see the note under SL4.
> `texshadeblend` / `texshadeflat` remain the older names for the plane
> projector's two shade modes and are unchanged; folding them into `texplane` is
> a wider rename not done here.

**Primary include graph (live client):** [`dash.c`](../src/graphics/dash.c) includes [`render_flat.u.c`](../src/graphics/old/render_flat.u.c), [`render_gouraud.u.c`](../src/graphics/render_gouraud.u.c), and [`render_texture.u.c`](../src/graphics/old/render_texture.u.c) (which pulls in affine routing via [`render_texture_affine.u.c`](../src/graphics/old/render_texture_affine.u.c)).

---

## Master table (with Variant_ID)

Rows are **distinct implementation surfaces**: triangle-level `raster_*` entry points, ordered-triangle helpers, face/near-clip wrappers, scanline helpers, and SIMD ISA backends that re-implement the same symbols.

When one catalogue row maps to **multiple** canonical IDs (e.g. opaque vs transparent, or ordered siblings), **Variant_ID** lists them separated by ` / ` in a single cell (same convention as the plan).

| Row | Variant_ID | Category | Projection | Role / shading | Representative symbol(s) | Source file(s) |
|-----|------------|----------|------------|------------------|---------------------------|----------------|
| F1 | `flat.screen.opaque.branching.s4` | Flat | screen | opaque triangle | `raster_flat_screen_opaque_branching_s4` | [`flat_branching_s4.c`](../src/graphics/old/flat_branching_s4.c) → [`raster/flat/flat.screen.opaque.branching.s4.c`](../src/graphics/raster/flat/flat.screen.opaque.branching.s4.c) |
| F2 | `flat.screen.alpha.branching.s4` | Flat | screen | alpha fill triangle | `raster_flat_screen_alpha_branching_s4` | `flat_branching_s4.c` → [`raster/flat/flat.screen.alpha.branching.s4.c`](../src/graphics/raster/flat/flat.screen.alpha.branching.s4.c) |
| F3 | `flat.screen.opaque.branching.s4.ordered` / `flat.screen.alpha.branching.s4.ordered` | Flat | screen | ordered-walk helpers | `raster_flat_screen_opaque_branching_s4_ordered`, `raster_flat_screen_alpha_branching_s4_ordered` | same as F1/F2 (`raster/flat/flat.screen.*.branching.s4.c`) |
| F4 | `legacy:flat.screen.opaque.sort.s4` / `legacy:flat.screen.alpha.sort.s4` | Flat (legacy) | screen | opaque / alpha s4 path | `raster_flat_screen_opaque_sort_s4`, `raster_flat_screen_alpha_sort_s4` | [`flat.u.c`](../src/graphics/old/flat.u.c) → [`raster/flat/flat.screen.opaque.sort.s4.u.c`](../src/graphics/raster/flat/flat.screen.opaque.sort.s4.u.c), [`flat.screen.alpha.sort.s4.u.c`](../src/graphics/raster/flat/flat.screen.alpha.sort.s4.u.c) (not called from `render_flat` today) |
| F5 | `flat.screen.face` | Flat | screen | dispatch + clip faces | `raster_flat`, `raster_face_flat*`, `raster_face_flat_near_clip` | `render_flat.u.c` |
| G1 | `gouraudhsllightness.screen.opaque.bary.branching.s4` | Gouraud | screen | opaque bs4 barycentric | `raster_gouraudhsllightness_screen_opaque_bary_branching_s4` | [`gouraud_branching_barycentric.c`](../src/graphics/old/gouraud_branching_barycentric.c) → [`raster/gouraudhsllightness/gouraudhsllightness.screen.opaque.bary.branching.s4.c`](../src/graphics/raster/gouraudhsllightness/gouraudhsllightness.screen.opaque.bary.branching.s4.c) |
| G2 | `gouraudhsllightness.screen.alpha.bary.branching.s4` | Gouraud | screen | alpha bs4 barycentric | `raster_gouraudhsllightness_screen_alpha_bary_branching_s4` | `gouraud_branching_barycentric.c` → [`raster/gouraudhsllightness/gouraudhsllightness.screen.alpha.bary.branching.s4.c`](../src/graphics/raster/gouraudhsllightness/gouraudhsllightness.screen.alpha.bary.branching.s4.c) |
| G3 | `gouraudhsllightness.screen.opaque.bary.branching.s4.ordered` / `gouraudhsllightness.screen.alpha.bary.branching.s4.ordered` | Gouraud | screen | ordered bary variants | `raster_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered`, `raster_gouraudhsllightness_screen_alpha_bary_branching_s4_ordered` | same as G1/G2 (`raster/gouraudhsllightness/gouraudhsllightness.screen.*.bary.branching.s4.c`) |
| G4 | `gouraud.screen.opaque.bary.branching.s1` | Gouraud | screen | opaque branching s1 barycentric | `raster_gouraud_bary_branching_s1` | `gouraud_s1_branching_barycentric.c` |
| G5 | `gouraud.screen.opaque.bary.branching.s1.ordered` | Gouraud | screen | ordered s1 helper | `raster_gouraud_bary_branching_s1_ordered` | `gouraud_s1_branching_barycentric.c` |
| G5a | `gouraudhsllightness.screen.alpha.bary.branching.s1` | Gouraud | screen | alpha branching s1 (smooth path) | `raster_gouraudhsllightness_screen_alpha_bary_branching_s1`, `raster_gouraudhsllightness_screen_alpha_bary_branching_s1_ordered` | [`gouraud_s1_branching_barycentric.c`](../src/graphics/old/gouraud_s1_branching_barycentric.c) → [`raster/gouraudhsllightness/gouraudhsllightness.screen.alpha.bary.branching.s1.c`](../src/graphics/raster/gouraudhsllightness/gouraudhsllightness.screen.alpha.bary.branching.s1.c) |
| G6 | `gouraud.screen.face` | Gouraud | screen | dispatch + clip faces | `raster_gouraud`, `raster_gouraud_s1`, `raster_face_gouraud*`, `raster_face_gouraud_near_clip*` | [`render_gouraud.u.c`](../src/graphics/render_gouraud.u.c) |
| G7 | `legacy:gouraud.screen.opaque.edge.sort.s4` / `gouraud.screen.alpha.edge.sort.s4` | Gouraud (legacy S4) | screen | edge-walk s4 + alpha | `raster_gouraud_screen_opaque_edge_sort_s4`, `raster_gouraud_screen_alpha_edge_sort_s4` | [`gouraud.u.c`](../src/graphics/old/gouraud.u.c) → [`raster/gouraudhsllightness/gouraudhsllightness.screen.opaque.edge.sort.s4.u.c`](../src/graphics/raster/gouraudhsllightness/gouraudhsllightness.screen.opaque.edge.sort.s4.u.c), [`gouraud.screen.alpha.edge.sort.s4.u.c`](../src/graphics/raster/gouraudhsllightness/gouraudhsllightness.screen.alpha.edge.sort.s4.u.c) (s4 opaque commented out in render; alpha still used from s1 path) |
| G8 | `gouraud.screen.opaque.bary_s4` | Gouraud (alt bary) | screen | full-triangle bary s4 | `raster_gouraud_s4_bary` | `gouraud_barycentric.u.c` (included from `gouraud.u.c`; not render default) |
| G9 | `gouraudhsllightness.screen.alpha.span_alpha.vec` | Gouraud span blend | screen | per-pixel alpha span | `raster_linear_alpha_s4` | [`raster/gouraudhsllightness/span/gouraudhsllightness.screen.alpha.span.u.c`](../src/graphics/raster/gouraudhsllightness/span/gouraudhsllightness.screen.alpha.span.u.c) (via [`gouraud.u.c`](../src/graphics/old/gouraud.u.c); SIMD — see plan § SIMD Integration) |
| G10 | `gouraud.screen.opaque.edge.sort.s1` / `gouraud.screen.alpha.edge.sort.s1` | Gouraud (parallel TU) | screen | s1 edge path | `raster_gouraud_screen_opaque_edge_sort_s1`, `raster_gouraud_screen_alpha_edge_sort_s1` | [`gouraud_s1.u.c`](../src/graphics/old/gouraud_s1.u.c) → [`raster/gouraudhsllightness/gouraudhsllightness.screen.opaque.edge.sort.s1.u.c`](../src/graphics/raster/gouraudhsllightness/gouraudhsllightness.screen.opaque.edge.sort.s1.u.c), [`gouraud.screen.alpha.edge.sort.s1.u.c`](../src/graphics/raster/gouraudhsllightness/gouraudhsllightness.screen.alpha.edge.sort.s1.u.c) (alternate TU; `GOURAUD_S1_U_C`) |
| G11 | `unused:gouraud.screen.opaque.branching.s4` / `unused:gouraud.screen.opaque.branching.s4.ordered` | Gouraud (unused TU) | screen | non-bary branching bs4 | `raster_gouraud_bs4`, `raster_gouraud_ordered_bs4` | [`archive/gouraud_branching.c`](../src/graphics/archive/gouraud_branching.c) (never `#include`d in repo) |
| G12 | `unused:gouraud.screen.opaque.branching.s1.ordered` / `unused:gouraud.screen.opaque.branching.s4` | Gouraud (unused TU) | screen | bs1 + misnamed bs4 | `raster_gouraud_ordered_bs1`, `raster_gouraud_bs4` | [`archive/gouraud_s1_branching.c`](../src/graphics/archive/gouraud_s1_branching.c) (never `#include`d) |
| G13 | `reference:gouraud.screen.ref_deob` | Gouraud (reference) | n/a | decomp triangle | `gouraud_deob_draw_triangle`, `gouraud_deob_draw_scanline` | [`reference/gouraud_deob.c`](../src/graphics/reference/gouraud_deob.c) (standalone reference; not in `dash.c`) |
| TF1 | `texshadeflat.persp.texopaque.branching.lerp8` / `texshadeflat.persp.texopaque.sort.lerp8` | Texture flat | perspective | opaque triangle | **`raster_texshadeflat_persp_texopaque_branching_lerp8`** (active; `render_texture_flat`), `raster_texshadeflat_persp_texopaque_branching_lerp8_ordered`, `raster_texshadeflat_persp_texopaque_sort_lerp8` (y-sort; retained) | [`texture.u.c`](../src/graphics/old/texture.u.c) + [`texshadeflat.persp.texopaque.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeflat.persp.texopaque.branching.lerp8.u.c), [`texshadeflat.persp.texopaque.sort.lerp8.u.c`](../src/graphics/raster/texture/texshadeflat.persp.texopaque.sort.lerp8.u.c) (8-px span: `raster_linear_opaque_texshadeflat_lerp8` → **TS8** `raster_linear_opaque_blend_lerp8` per ISA) |
| TF2 | `texshadeflat.persp.textrans.branching.lerp8` / `texshadeflat.persp.textrans.sort.lerp8` | Texture flat | perspective | transparent triangle | **`raster_texshadeflat_persp_textrans_branching_lerp8`** (active), `raster_texshadeflat_persp_textrans_branching_lerp8_ordered`, `raster_texshadeflat_persp_textrans_sort_lerp8` | `texture.u.c` + [`texshadeflat.persp.textrans.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeflat.persp.textrans.branching.lerp8.u.c), [`texshadeflat.persp.textrans.sort.lerp8.u.c`](../src/graphics/raster/texture/texshadeflat.persp.textrans.sort.lerp8.u.c) (ordered scanlines: `raster_linear_transparent_texshadeflat_lerp8` → **TS8** blend span) |
| TF3 | `texshadeflat.persp.texopaque.sort.lerp8.scanline` / `textrans…` + `…ordered.lerp8.scanline` (both gates) | Texture flat | perspective | scanline helpers | `raster_texshadeflat_persp_texopaque_sort_lerp8_scanline`, `raster_texshadeflat_persp_textrans_sort_lerp8_scanline` (x-swap); `raster_texshadeflat_persp_texopaque_ordered_lerp8_scanline`, `raster_texshadeflat_persp_textrans_ordered_lerp8_scanline` (no x-swap; branching path) | `texture.u.c` + four shards: [`texshadeflat.persp.texopaque.sort.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeflat.persp.texopaque.sort.lerp8.scanline.u.c), [`texshadeflat.persp.textrans.sort.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeflat.persp.textrans.sort.lerp8.scanline.u.c), [`texshadeflat.persp.texopaque.ordered.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeflat.persp.texopaque.ordered.lerp8.scanline.u.c), [`texshadeflat.persp.textrans.ordered.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeflat.persp.textrans.ordered.lerp8.scanline.u.c) |
| TF4 | `texshadeflat.affine.face` | Texture flat | affine | dispatch (reuses blend) | `raster_texture_flat_affine`, `raster_texture_flat_affine_v3` | `render_texture_affine.u.c` |
| TF5 | `texshadeflat.affine.texopaque.branching.lerp8` / `texshadeflat.affine.textrans.branching.lerp8` / `texshadeflat.affine.texopaque.branching.lerp8_v3` / `texshadeflat.affine.textrans.branching.lerp8_v3` | Texture flat | affine | opaque / transparent tri | `raster_texshadeblend_affine_texopaque_branching_lerp8(_v3)`, `raster_texshadeblend_affine_textrans_branching_lerp8(_v3)` | [`texture_blend_branching_affine.u.c`](../src/graphics/old/texture_blend_branching_affine.u.c), [`texture_blend_branching_affine_v3.u.c`](../src/graphics/old/texture_blend_branching_affine_v3.u.c) → shared [`texshadeblend.affine.*.branching.lerp8*.u.c`](../src/graphics/raster/texture/texshadeblend.affine.texopaque.branching.lerp8.u.c) shards (same symbols as TS6; flat vs blend is usage) |
| TF6 | `texshadeflat.affine.face.nearclip` | Texture flat | affine | face + near clip | `raster_face_texture_flat_affine*`, `raster_face_texture_flat_affine*_near_clip` | `render_texture_affine.u.c` |
| TF7 | `texshadeflat.persp.face.nearclip` | Texture flat | perspective | face + near clip | `raster_face_texture_flat*`, `raster_face_texture_flat_near_clip` | `render_texture.u.c` |
| TF8 | `texshadeflat.persp.face` | Texture flat | perspective | dispatch | `raster_texture_flat` | `render_texture.u.c` |
| TS1 | `texshadeblend.persp.texopaque.branching.lerp8_v3` / `texshadeblend.persp.textrans.branching.lerp8_v3` | Texture Gouraud | perspective | opaque / transparent v3 | `raster_texshadeblend_persp_texopaque_branching_lerp8_v3`, `raster_texshadeblend_persp_textrans_branching_lerp8_v3` | [`texture_blend_branching_v3.u.c`](../src/graphics/old/texture_blend_branching_v3.u.c) → [`raster/texture/texshadeblend.persp.texopaque.branching.lerp8_v3.u.c`](../src/graphics/raster/texture/texshadeblend.persp.texopaque.branching.lerp8_v3.u.c), [`texshadeblend.persp.textrans.branching.lerp8_v3.u.c`](../src/graphics/raster/texture/texshadeblend.persp.textrans.branching.lerp8_v3.u.c) (active; TS9 span) |
| TS2 | `texshadeblend.persp.texopaque.branching.lerp8` / `texshadeblend.persp.textrans.branching.lerp8` | Texture Gouraud | perspective | non-v3 branching.lerp8 | `raster_texshadeblend_persp_texopaque_branching_lerp8`, `raster_texshadeblend_persp_textrans_branching_lerp8`, `*_ordered` siblings, `draw_texture_scanline_*_blend_branching_lerp8_ordered` (span) | [`texture_blend_branching.u.c`](../src/graphics/old/texture_blend_branching.u.c) → [`raster/texture/texshadeblend.persp.texopaque.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.persp.texopaque.branching.lerp8.u.c), [`texshadeblend.persp.textrans.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.persp.textrans.branching.lerp8.u.c) (superseded by TS1 for default dispatch) |
| TS3 | `texshadeblend.persp.face.nearclip` | Texture Gouraud | perspective | face + near clip | `raster_face_texture_blend*`, `raster_face_texture_blend_near_clip` | `render_texture.u.c` |
| TS4 | `texshadeblend.persp.face` | Texture Gouraud | perspective | dispatch | `raster_texture_blend` | `render_texture.u.c` |
| TS5 | `texshadeblend.affine.face` | Texture Gouraud | affine | dispatch | `raster_texture_blend_affine`, `raster_texture_blend_affine_v3` | `render_texture_affine.u.c` |
| TS6 | `texshadeblend.affine.texopaque.branching.lerp8` / `texshadeblend.affine.textrans.branching.lerp8` / `texshadeblend.affine.texopaque.branching.lerp8_v3` / `texshadeblend.affine.textrans.branching.lerp8_v3` | Texture Gouraud | affine | opaque / transparent tri | same affine symbols as TF5 (shade varies per vertex) | [`texture_blend_branching_affine.u.c`](../src/graphics/old/texture_blend_branching_affine.u.c), [`texture_blend_branching_affine_v3.u.c`](../src/graphics/old/texture_blend_branching_affine_v3.u.c) → [`texshadeblend.affine.texopaque.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.affine.texopaque.branching.lerp8.u.c), [`texshadeblend.affine.textrans.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.affine.textrans.branching.lerp8.u.c), [`texshadeblend.affine.texopaque.branching.lerp8_v3.u.c`](../src/graphics/raster/texture/texshadeblend.affine.texopaque.branching.lerp8_v3.u.c), [`texshadeblend.affine.textrans.branching.lerp8_v3.u.c`](../src/graphics/raster/texture/texshadeblend.affine.textrans.branching.lerp8_v3.u.c); TS10 span |
| TS7 | `texshadeblend.affine.face.nearclip` | Texture Gouraud | affine | face + near clip | `raster_face_texture_blend_affine*` | `render_texture_affine.u.c` |
| TS8 | `texshadeblend.persp.texopaque.lerp8.span.vec` / `texshadeblend.persp.textrans.lerp8.span.vec` / `texshadeblend.persp.texopaque.lerp8_v3.span.vec` / `texshadeblend.persp.textrans.lerp8_v3.span.vec` | Texture (SIMD kernel) | perspective | 8-pixel lerp8 + v3 | `raster_linear_*_blend_lerp8[_v3]`; parallel **texshadeflat** entry `raster_linear_{opaque,transparent}_texshadeflat_lerp8` (forwards to blend lerp8) | [`raster/texture/span/tex.span.u.c`](../src/graphics/raster/texture/span/tex.span.u.c) → `tex.span.{scalar,sse2,sse41,avx,neon}.u.c` |
| TS9 | `texshadeblend.persp.texopaque.branching.lerp8_v3.scanline` / `texshadeblend.persp.textrans.branching.lerp8_v3.scanline` | Texture (SIMD scan) | perspective | scanline branching.lerp8 v3 | `draw_texture_scanline_*_blend_branching_lerp8_v3_ordered` | `raster/texture/span/tex.span.*.u.c` (scalar + SIMD: both gates) |
| TS10 | `texshadeblend.affine.texopaque.branching.lerp8.scanline` / `texshadeblend.affine.textrans.branching.lerp8.scanline` / `texshadeblend.affine.texopaque.branching.lerp8_v3.scanline` / `texshadeblend.affine.textrans.branching.lerp8_v3.scanline` | Texture (SIMD scan) | affine | ordered scanline (non-ish16 + ish16 v3) | `draw_texture_scanline_*_blend_affine_branching_lerp8_ordered`, `draw_texture_scanline_*_blend_affine_branching_lerp8_ish16_ordered` | `raster/texture/span/tex.span.*.u.c` (all ISAs including scalar) |
| GR1 | `gouraudrgb.screen.opaque.bary.branching.s4` | Gouraud RGB | screen | per-channel RGB, opaque | `raster_gouraudrgb_screen_opaque_bary_branching_s4`, `raster_gouraudrgb_screen_opaque_bary_branching_s4_ordered` | [`raster/gouraudrgb/gouraudrgb.screen.opaque.bary.branching.s4.c`](../3rd/toridraw/graphics/raster/gouraudrgb/gouraudrgb.screen.opaque.bary.branching.s4.c) + shared walker [`gouraudrgb.screen.bary.branching.s4.inc`](../3rd/toridraw/graphics/raster/gouraudrgb/gouraudrgb.screen.bary.branching.s4.inc) |
| GR2 | `gouraudrgb.screen.alpha.bary.branching.s4` | Gouraud RGB | screen | per-channel RGB, alpha blended | `raster_gouraudrgb_screen_alpha_bary_branching_s4`, `..._ordered` | [`raster/gouraudrgb/gouraudrgb.screen.alpha.bary.branching.s4.c`](../3rd/toridraw/graphics/raster/gouraudrgb/gouraudrgb.screen.alpha.bary.branching.s4.c) (same template) |
| TS12 | `texplane.persp.<gate>[.facealpha][.modulate].branching.lerp8_v3` (12 IDs) | Texture Gouraud | perspective | the compositing matrix over the **plane projector** (render type 0): gate {`texopaque`,`textrans`,`texalpha`} x `facealpha` x `modulate` | `raster_texplane_persp_{texopaque,textrans,texalpha}[_facealpha][_modulate]_branching_lerp8_v3` | one file per variant, `texplane.persp.*.branching.lerp8_v3.u.c`, over a shared walker template [`texplane.persp.branching.lerp8_v3_tmpl.inc`](../3rd/toridraw/graphics/raster/texture/texplane.persp.branching.lerp8_v3_tmpl.inc); TS14 spans |
| TS13 | `tex{cylinder,cube,sphere}.persp.<gate>[.facealpha][.modulate].branching.lerp8_v3` (36 IDs) | Texture Gouraud | perspective | the three families that carry a *mapping* rather than a projector. Each computes uv per vertex itself — through the TS16 table, not libm — and interpolates it perspective-correctly. Twelve gate variants each (the full matrix; unlike `texplane` they have no plain SIMD twin to defer to) | `raster_tex{cylinder,cube,sphere}_persp_<gate>[_facealpha][_modulate]_branching_lerp8_v3` | one file per variant, `tex{cylinder,cube,sphere}.persp.*.u.c`, over a shared walker [`texmap.persp.branching.lerp8_v3_tmpl.inc`](../3rd/toridraw/graphics/raster/texture/texmap.persp.branching.lerp8_v3_tmpl.inc) + [`texmap_common.h`](../3rd/toridraw/graphics/raster/texture/texmap_common.h); TS14 spans |
| TS14 | `tex.span.gates` (10 IDs) | Texture (span) | perspective | 8-pixel + exact-block spans for TS12 | `draw_texture_scanline_<gate>[_facealpha][_modulate]_branching_lerp8_v3_ordered` | [`span/tex.span.gates.u.c`](../3rd/toridraw/graphics/raster/texture/span/tex.span.gates.u.c) + [`span/tex.span.gates_tmpl.inc`](../3rd/toridraw/graphics/raster/texture/span/tex.span.gates_tmpl.inc). Grouped per file like the per-ISA span files, not per variant. **Outside the ISA rotation** — scalar only, same read-modify-write reason as SL4 |
| TS16 | `atan.turns16` | Table | — | arctangent table, and the arcsine entry point backed by it | `g_atan_turns16_table`, `ToriDraw_Atan2Turns16`, `ToriDraw_AsinTurns16`, `ToriDraw_InitAtanTable` | [`graphics/shared_tables.h`](../3rd/toridraw/graphics/shared_tables.h) |
| TS17 | `texture_uv` | UV generation | — | per-vertex uv for render types 0-3, from the decoded mapping parameters | `ToriDraw_ComputeTextureUv`, `ToriDraw_ComputeTextureUvBases` | [`toridraw_texture_uv.c`](../3rd/toridraw/toridraw_texture_uv.c) |
| TS15 | `tex_sampler` | Texture (sampler) | — | per-triangle sampler state: texels, width/shift/masks, per-axis clamp, face alpha, per-channel tint | `ToriDraw_TexSampler`, `tex_sampler_index`, `tex_sampler_mul255`, `tex_sampler_tint` | [`tex_sampler.h`](../3rd/toridraw/graphics/raster/texture/tex_sampler.h) |
| TS11 | `texshadeblend.persp.texopaque.sort.lerp8` / `texshadeblend.persp.textrans.sort.lerp8` | Texture Gouraud | perspective | SWAP-sorted triangle lerp8 | `raster_texshadeblend_persp_texopaque_sort_lerp8`, `raster_texshadeblend_persp_textrans_sort_lerp8` | [`texture.u.c`](../src/graphics/old/texture.u.c) + [`raster/texture/texshadeblend.persp.texopaque.sort.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.persp.texopaque.sort.lerp8.u.c), [`texshadeblend.persp.textrans.sort.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.persp.textrans.sort.lerp8.u.c) + scanlines [`texshadeblend.persp.texopaque.sort.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeblend.persp.texopaque.sort.lerp8.scanline.u.c), [`texshadeblend.persp.textrans.sort.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeblend.persp.textrans.sort.lerp8.scanline.u.c) (`raster_texshadeblend_persp_*_sort_lerp8_scanline`; legacy path; TS8 span) |

---

## The `scanline` family (SL1–SL5)

A fourth walk alongside `branching` and `sort`, living in
[`3rd/toridraw/graphics/raster/scanline/`](../3rd/toridraw/graphics/raster/scanline/).
**Off by default** — `ToriDraw_RasterSetScanline(true)` or
`TORIDRAW_RASTER_SCANLINE=1` switches every `ToriDraw_Triangle*` dispatcher over
to it.

What it does differently, all of it once per triangle rather than per row or per
pixel (see [`scanline_common.h`](../3rd/toridraw/graphics/raster/scanline/scanline_common.h)):

- **One y-sort**, not a six-way permutation dispatch (`branching`) and not an
  attribute swap (`sort`). Attribute gradients are plane equations, so they are
  invariant under the permutation and are derived from the *unsorted* vertices;
  the sorted signed area comes back from the permutation parity for free.
- **Left/right edge decided once** from the sign of that signed area, instead of
  per row.
- **Vertical clipping folded into the edge accumulators**, so no walker tests y
  inside its loop.
- **Horizontal clip classified once per trapezoid segment**
  (`scanline_segment_no_hclip`): when every row of a segment is inside
  `[0, screen_width)` the span kernel runs with no bounds arithmetic at all.
  Previously only `flat` had this; here `gouraud` and every textured variant get it.
- **Gouraud flat-span degeneration**: a zero colour step across x drops the run
  into the flat fill (constant on terrain and untextured walls).
- **Per-triangle hoisting** of texture shift, v mask and the screen-centre uv
  bias, which the `branching` texture kernels recompute per scanline.

| Row | Variant_ID | Category | Symbol(s) | Source |
|-----|------------|----------|-----------|--------|
| SL1 | `flat.screen.opaque.scanline.s8` / `flat.screen.alpha.scanline.s8` | Flat | `raster_flat_screen_{opaque,alpha}_scanline_s8` | [`scanline.flat.screen.u.c`](../3rd/toridraw/graphics/raster/scanline/scanline.flat.screen.u.c) |
| SL2 | `gouraudhsllightness.screen.opaque.bary.scanline.s4` / `gouraudhsllightness.screen.alpha.bary.scanline.s4` | Gouraud | `raster_gouraudhsllightness_screen_{opaque,alpha}_bary_scanline_s4` | [`scanline.gouraudhsllightness.screen.u.c`](../3rd/toridraw/graphics/raster/scanline/scanline.gouraudhsllightness.screen.u.c) |
| SL3 | `texshade{flat,blend}.{persp,affine}.{texopaque,textrans}.scanline.lerp8` (8 IDs) | Texture | `raster_texshade*_scanline_lerp8` | [`scanline.texture.u.c`](../3rd/toridraw/graphics/raster/scanline/scanline.texture.u.c) + [`scanline.texture_tmpl.inc`](../3rd/toridraw/graphics/raster/scanline/scanline.texture_tmpl.inc) |
| SL4 | `texshade{flat,blend}.{persp,affine}.{texopaque,textrans}.facealpha.scanline.lerp8` (8 IDs) | Texture | `raster_texshade*_facealpha_scanline_lerp8` | same template |
| SL5 | `scanline.span.solid` | Span | `scanline_span_{flat,gouraudhsllightness}_{opaque,alpha}` | [`span/scanline.span.solid.u.c`](../3rd/toridraw/graphics/raster/scanline/span/scanline.span.solid.u.c) |

**SL4 was new capability, not a port**: per-face alpha on a *textured* span had
no `branching` counterpart, so alpha-blended textured geometry had to be drawn
opaque. The 8-pixel inner kernels are the existing per-ISA SIMD spans (**TS8**)
except in the facealpha variants, which need a read-modify-write.

**TS12 closes that gap for `branching`, and widens it** — the perspective
`texshadeblend` family now spans gate x facealpha x modulate, all ten variants
generated from one walker template shared with their plain siblings so they
cannot drift. Four things are worth knowing:

- **Every capability is a bit-exact no-op at its neutral setting.**
  `facealpha(0xFF)`, `modulate(256,256,256)` and `texalpha` on an all-opaque
  alpha plane each equal the plain SIMD kernel *exactly*. That is by
  construction: a fully opaque result takes a plain store rather than
  `alpha_blend`, which would otherwise round a 0xFF channel down to 0xFE, and
  `tex_sampler_mul255` is exact rather than `(a*b)>>8`. Those identities are the
  anchor the whole test chains off.
- **The exact (cold) path clamps u even in repeat mode.** `tex_span_exact_block`
  does, so that is what `repeat` has always meant on that path, while the
  8-pixel path masks. The two disagree in the reference and the disagreement is
  load-bearing — matching it is what makes the identities above hold. Diverging
  cost ~26% of covered pixels sampling the neighbouring texel.
- **The colour key, the texel alpha and the face alpha are three things.** A
  keyed or zero-alpha texel is skipped outright, which is not the same as
  compositing at alpha 0 (`alpha_blend(0, dst, src)` rounds `dst` down). The
  texel alpha and the face alpha compose multiplicatively in one pass.
- **The high byte of a framebuffer word is undefined.** `shade_blend` masks and
  shifts, so scalar spans leave it zero while the NEON spans leave the
  shade-scaled texel alpha there. Nothing downstream reads it; comparisons must
  mask to 24 bits.

None of it is reachable from the model raster yet: `toridraw_raster.u.c` passes
no alpha, tint or sampler for textured faces, so a textured face still draws
through the plain gate. Wiring that up is a deliberate separate step — it
changes how existing content renders.

**`texcylinder` / `texcube` / `texsphere` (TS13)** are the families for render
types 1, 2 and 3, where the face carries a mapping rather than a projector. Four
things about them:

- **The projection happens in the kernel, per triangle.** It could be a pre-pass
  writing a uv array, and for a static model that would be cheaper — but the uv
  depends on the *animated* vertex positions, so a rigged model would rebuild
  that whole array every frame where triangle setup touches only the three
  vertices being drawn. The caller therefore passes model-space positions
  alongside the screen and camera-space ones.
- **They are separate kernels, not one kernel with a mode.** The projection and
  its seam fixup are what distinguishes them; a per-pixel branch on a
  per-triangle property is waste, and a symbol named `texsphere` should not be
  able to draw a cube. The test asserts the three actually produce different
  pixels, so the split cannot silently degenerate into three names for one thing.
- **They need no new span.** TS14 already computes `au / (cw >> shift)` per
  8-pixel block, which *is* perspective-correct interpolation of u/w over 1/w —
  so only the setup differs, and it builds those accumulators from explicit uv
  instead of from a plane's normals.
- **The fixed-point normalisation is the delicate part.**
  `ToriDraw_TexturePlanePrepare32` could not be reused: its `base` comes from
  projecting a 3D normal's z, not from an arbitrary screen-space plane. The
  scale is instead chosen from the values the span will actually see — a plane's
  extremes over the clipped screen bounding box are at its corners — which
  bounds every intermediate including the eight-pixel lookahead past a short
  tail.

Their uv generator twin (TS17) shares the same projection code, so a generated
uv and a kernel-drawn uv cannot disagree.

### Where it is and is not bit-identical

- **Flat and gouraud: exact.** Verified against `branching` over interior,
  clipped, degenerate, single-row/column and inverted-winding triangles, and on
  real cache geometry (see below).
- **`texshadeflat.persp.texopaque`: exact.** Its reference counterpart uses the
  same left-edge rule.
- **Other textured variants: differ on span-boundary columns.** The reference
  `textrans` and `lerp8_v3` kernels start a span at `(x - 1) >> 16`, which begins
  a row one pixel earlier whenever the left edge lands exactly on a pixel
  boundary — and because that shifts the 8-pixel block alignment it changes uv
  and shade for the whole row. `texshadeflat.persp.texopaque` uses `x >> 16`, as
  do all the flat and gouraud kernels. The two reference conventions are
  mutually exclusive; the scanline family uses `x >> 16` everywhere so textured
  faces do not seam against the flat/gouraud faces they share edges with.
- The scanline family also clamps a clipped right edge to `screen_width` rather
  than `screen_width - 1`, so it draws the final column that the existing
  kernels drop.

### Testing

- `make -C src test-gouraudrgb` — [`toridraw_gouraudrgb_test.c`](../3rd/toridraw/toridraw_gouraudrgb_test.c).
  GR1/GR2 have no reference rasterizer to diff against, so the checks are
  properties: coverage identical to the `gouraudhsllightness` twin (the walker is
  a verbatim copy, so every clip branch shows up here), a constant colour
  reproduced exactly, the colour plane against a double-precision plane (tight on
  a purely vertical gradient, bounded by the s4 stair otherwise), the channel
  clamp against `gouraudrgb_pack_ish8` directly, and the alpha algebra per pixel.
- `make -C src test-texmap` — [`toridraw_texmap_test.c`](../3rd/toridraw/toridraw_texmap_test.c).
  TS13. The projections against a libm reference (bounded by the TS16 table's own
  accuracy, ~2e-5 tiles measured); the fixed-point uv planes against a double
  reference at real pixel positions; the same neutral-setting identity chain as
  TS12; that the three projections differ; and cube face selection, both the
  dominant-axis rule and that the per-axis scales reach it. Six mutations caught,
  including one — a dropped axis scale — that only the last check sees, because
  every other test triangle has an axis-aligned normal.
- `make -C src test-texture-matrix` — [`toridraw_texture_matrix_test.c`](../3rd/toridraw/toridraw_texture_matrix_test.c).
  TS12/TS14/TS15. Eight bit-exact identities against the plain kernels anchor the
  suite; the algebra, the gate semantics, the sampler's addressing and
  `mul255` are chained off them. Coverage is recovered by rendering the plain
  kernel over two different backgrounds and taking the pixels that agree, rather
  than against a background sentinel — an alpha blend can land back on the
  background, so "changed" is not "covered". Six mutations of the span and
  sampler are each caught.
- `make -C src test-scanline` — [`toridraw_scanline_parity_test.c`](../3rd/toridraw/toridraw_scanline_parity_test.c).
  Diffs every variant against its `branching` counterpart over a triangle set
  covering interior / clipped / degenerate / inverted cases, then chains the
  textured variants off the one exact anchor (gate axis: `textrans == texopaque`
  with no zero texels; shade axis: `texshadeblend == texshadeflat` at equal
  shades; space axis: `affine == persp` at constant depth; alpha axis: the
  facealpha blend checked algebraically at five alphas). The shade plane is
  checked against a double-precision plane rather than another rasterizer.
- `make -C src scanline-compare` — [`scanline_compare_sdl.c`](../src/render/test/scanline_compare_sdl.c).
  Draws a textured, alpha-blended model from a dat1 cache into **two side-by-side
  panels of one window** (left `branching`, right `scanline`). Both panels render
  from the same camera state, so the rotate/tilt/zoom keys move the model in both
  at once and the images stay directly comparable; `d` adds a third panel with
  the amplified difference.

  ```sh
  make -C src scanline-compare
  ./src/build/scanline_compare cache254 148      # one model on a turntable
  ./src/build/scanline_compare cache254 world    # a real map square at 50,50
  ```

  **World mode** builds the square through `WorldBuilder` + `CreateTask_WorldLoad`
  and draws it through the production `painter_paint_bucket` → `ToriRS_Frame` →
  `ToriRS_Soft3D_Execute` path, so what is compared is exactly what the client
  draws. The painter runs once per frame and both families replay the identical
  command list.

  Keys — both modes: `←/→` yaw, `↑/↓` pitch, `+/-` zoom (model) or camera height
  (world), `m` switch mode, `d` diff panel, `x` diff scale, `r` reset, `q` quit.
  Model mode adds `[`/`]` model, `space` turntable, `a` face alpha (FF/C0/80/40).
  World mode adds `1`/`2` map X ∓, `3`/`4` map Z ∓, `w`/`s`/`a`/`e` pan.

  Non-interactive: `TORIRS_SCANLINE_HEADLESS=N` sweeps N yaw steps per alpha mode
  with no window and dumps BMPs; `TORIRS_SCANLINE_SHOT=path` writes a single frame
  of exactly what the window shows; `TORIRS_SCANLINE_MAP=x,z` starts in world mode
  on that square.

  On `cache254` model 148 (1004 faces, 48 textured, 420 alpha) the two families
  differ on 0.77 % of drawn pixels, and **0** once the model's textures are
  stripped — which is what pins the residual on the reference span start rather
  than on the walker. Lumbridge (50,50) differs on 3.5 % of the viewport, all of
  it on textured terrain and roofs.

### Performance

Numbers below are from an optimized build (`make -C src OPT=1 scanline-compare`);
a debug build says so in the panel, because `-O0` timings are meaningless here.

| Subject | Bracket | branching | scanline | |
|---|---|---|---|---|
| model 148 / 597 / 1178 | raster only | 0.042 / 0.027 / 0.018 ms | 0.037 / 0.024 / 0.016 ms | **0.87–0.96×** |
| world 50,50 | draw incl. project + sort | 1.22 ms | 1.21 ms | 1.00× |

Model mode brackets the raster alone — projection and face sorting run once and
are shared. World mode replays a painter command list, which owns per-model
projection and sorting; that work is identical for both families but dilutes the
raster difference to a wash. The honest summary is a **modest raster win, not an
end-to-end one**: at world scale the transform and sort dominate.

---

## The textured variant matrix

Four **projection** families, twelve **compositing** variants each — 48 kernels,
one file per variant, four shared walker templates.

| | `texopaque` | `textrans` | `texalpha` |
|---|:--:|:--:|:--:|
| plain | ✓ | ✓ | ✓ |
| `.facealpha` | ✓ | ✓ | ✓ |
| `.modulate` | ✓ | ✓ | ✓ |
| `.facealpha.modulate` | ✓ | ✓ | ✓ |

× `texplane` (TS12) · `texcylinder` · `texcube` · `texsphere` (TS13)

**The projection axis** is what the face carries:

| family | render type | what the face carries | linear in the vertex? |
|---|:--:|---|:--:|
| `texplane` | 0 | a *projector* — three vertices whose positions are the texture plane | yes (it *is* a plane) |
| `texcylinder` | 1 | a *mapping* — angle about an axis, height along it | no (arctangent) |
| `texcube` | 2 | a *mapping* — the triangle normal picks one of six faces, then flat | yes, per face |
| `texsphere` | 3 | a *mapping* — longitude and latitude | no (arctangent, arcsine) |

`texplane` walks its plane directly and never forms a uv. The three mapped
families compute uv per vertex in triangle setup — through the TS16 arctangent
table, never libm — and interpolate it perspective-correctly into the same TS14
spans. That is why they are four families and not one with a mode: the
projection and its seam fixup are the difference, and a symbol named `texsphere`
should not be able to draw a cube. The test asserts the three mapped families
produce different pixels, so the split cannot silently collapse.

**The compositing axes** are orthogonal to the projection and identical across
all four families:

| axis | what it adds | neutral setting |
|---|---|---|
| gate | how a texel decides coverage: all / colour-key / its own alpha byte | `texopaque` |
| `facealpha` | the face's alpha, composed multiplicatively with the gate's | `0xFF` |
| `modulate` | a per-channel tint by the face's colour | `(256,256,256)` |

Every axis at its neutral setting is a **bit-exact no-op**, which is what the
tests chain off. Addressing (repeat / clamp, per axis) is deliberately *not* an
axis — it rides the sampler as data, because it changes an index computation
rather than the shape of the composite. That is also why `texplane` carries its
own scalar `texopaque` and `textrans` despite the SIMD kernels covering the same
gates: those take `texels` and `texture_width` directly and have nowhere to say
"clamp", which three of the QBD's fifteen materials need.

### Reaching them: `ToriDraw_RenderHD`

The 48 kernels are not reachable from the stock raster, which can only express
"is there a texture" and "is it colour-keyed". `ToriDraw_RenderHD`
([`toridraw_render_hd.h`](../3rd/toridraw/toridraw_render_hd.h)) is the flow that
routes to them: same projection, same face sort, same walkers, but four
decisions per face — projection from `texture_render_types`, gate / `modulate`
from the material, `facealpha` from the face.

- **`TORIDRAWMK_MODEL_HD` is a model *variant*, not four more fields.** Almost no
  model is HD, so the mapping array hangs off `struct ToriDraw_ModelHD` whose
  `base` is embedded by value — a `ToriDraw_ModelHD*` IS a `ToriDraw_Model*`, and
  every existing entry point keeps working through `ToriDraw_ModelAsFull`. The
  scene and lighting paths were deliberately *not* widened to the new kind;
  `ToriDraw_ModelKindIsFull` marks the ones that genuinely handle both.
- **Fallbacks are counted, never silent.** A material with no texels draws flat
  (`fallback_no_texels`); a mapped render type on a model with no mappings draws
  through the plane kernel (`fallback_no_mapping`). The stock raster *skips* a
  face whose texture has not streamed in — right for a game, wrong for a viewer.
- **`ToriDraw_HDRenderStats` exists because routing fails silently.** A cube face
  drawn through the plane kernel still produces pixels. `make -C src
  test-render-hd` asserts on those counters; four mutations of the routing are
  each caught.

### Fixed point

Per-pixel work is 16.16 integer in all 48, as in every other kernel here, and
every kernel input is `int` — except `ToriDraw_TexMapping`, which the mapped
families take per face group and which carries floats. Float otherwise appears
in exactly two places: the perspective reciprocal in the spans
(`1.0f / (float)w`, once per 8 pixels — **pre-existing**, and copied verbatim so
the identities above stay bit-exact), and the mapped families' per-triangle
projection and uv-plane solve. A fully fixed-point mapping basis would need a
per-group exponent rather than a flat 16.16: cube scales are `64 / raw` and the
raw range measured over `cache.rs727_preeoc` is `[0, 16777215]`, so the basis
spans about seven orders of magnitude and the small end is not representable in
16.16.

---

## How to read this (short)

- **Flat / Gouraud:** triangle work is mostly in `raster_*`; **F5** and **G6** are **face-level** APIs from the model pipeline.
- **Texture flat vs texture Gouraud:** same affine kernels (**TF5** / **TS6**); flat mode passes a repeated per-vertex shade through the blend affine entry points (affine **branching.lerp8** triangle shards + **TS10** span scanlines).
- **Perspective shaded path today:** `raster_texture_blend` uses the **v3 branching.lerp8** symbols (**TS1**: `raster_texshadeblend_persp_texopaque_branching_lerp8_v3`, etc.), not the older `texture.u.c` triangle rasterizers (**TS11**). **Perspective flat** (`raster_texture_flat`) uses **TF1/TF2** (`raster_texshadeflat_persp_texopaque_branching_lerp8`, etc.); sort variants remain in the same TU for parity / benches.
- **SIMD:** **G9** and **TS8–TS10** — see the plan’s **SIMD Integration** section. Perspective **texshadeflat** scanlines use **`raster_linear_*_texshadeflat_lerp8`** (same ISA files as **TS8**; thin forwarders to `raster_linear_*_blend_lerp8`).
- **Scalar TS9:** `draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered` is implemented in [`tex.span.scalar.u.c`](../src/graphics/raster/texture/span/tex.span.scalar.u.c) (parity with other ISAs).
- **Repo-only / not in `dash.c`:** F4, G8, G10–G13, standalone [`texture_opaque_blend_affine.c`](../src/graphics/archive/texture_opaque_blend_affine.c) / [`texture_transparent_blend_affine.c`](../src/graphics/archive/texture_transparent_blend_affine.c), and [`gouraud_raster.c`](gouraud_raster.c) (standalone demo in this folder).

---

## Fixed-width master + Variant_ID (plan mirror)

Same rows as the plan’s ASCII table, with a **Variant_ID** column so the plan file can stay narrow. Convention: multiple IDs in one cell use ` / `.

```text
Row  Variant_ID (short)                                    Category / projection / notes
---  ----------------------------------------------------  ------------------------------------------------------------------
F1   flat.screen.opaque.branching.s4                      Flat screen opaque triangle
F2   flat.screen.alpha.branching.s4                       Flat screen alpha triangle
F3   flat.screen.opaque.branching.s4.ordered / ...alpha... Flat ordered helpers
F4   legacy:flat.screen.opaque.sort.s4 / ...alpha...      Flat legacy sort.s4
F5   flat.screen.face                                     Flat face dispatch
G1   gouraudhsllightness.screen.opaque.bary.branching.s4  HSL16-lightness gouraud opaque bary bs4
G2   gouraudhsllightness.screen.alpha.bary.branching.s4   HSL16-lightness gouraud alpha bary bs4
G3   gouraudhsllightness.screen.opaque.bary.branching.s4.ordered / ...  ordered bary
G4   gouraud.screen.opaque.bary.branching.s1              Gouraud opaque bary branching s1
G5   gouraud.screen.opaque.bary.branching.s1.ordered      Gouraud ordered s1
G5a  gouraudhsllightness.screen.alpha.bary.branching.s1   alpha branching s1 (smooth path)
G6   gouraud.screen.face                                   Gouraud face dispatch
G7   legacy:gouraud.screen.opaque.edge.sort.s4 / gouraud...alpha.edge.sort.s4  Gouraud legacy s4 + alpha
G8   gouraud.screen.opaque.bary_s4                        Gouraud alt bary s4
G9   gouraudhsllightness.screen.alpha.span_alpha.vec      SIMD alpha span
G10  gouraud.screen.opaque.edge.sort.s1 / ...alpha.edge.sort.s1  Gouraud s1 edge TU
G11  unused:gouraud.screen.opaque.branching.s4 / ...ordered  archive/gouraud_branching.c
G12  unused:gouraud.screen.opaque.branching.s1.ordered / ... archive/gouraud_s1_branching.c
G13  reference:gouraud.screen.ref_deob                    reference/gouraud_deob.c
TF1  texshadeflat.persp.texopaque.branching.lerp8 / ...sort...  Texture flat persp opaque (active branching)
TF2  texshadeflat.persp.textrans.branching.lerp8 / ...sort...   Texture flat persp trans (active branching)
TF3  texshadeflat...sort.lerp8.scanline + ...ordered...scanline  Texture scanline sort vs ordered
TF4  texshadeflat.affine.face                             Texture flat affine dispatch
TF5  texshadeflat.affine.texopaque.branching.lerp8 / ... (4 IDs)   Texture flat affine tris
TF6  texshadeflat.affine.face.nearclip                    Texture flat affine face clip
TF7  texshadeflat.persp.face.nearclip                     Texture flat persp face clip
TF8  texshadeflat.persp.face                              Texture flat persp dispatch
TS1  texshadeblend.persp.texopaque.branching.lerp8_v3 / ...trans... Active persp shadeblend v3
TS2  texshadeblend.persp.texopaque.branching.lerp8 / ...trans...  Legacy branching.lerp8
TS3  texshadeblend.persp.face.nearclip                    Texture blend persp face clip
TS4  texshadeblend.persp.face                             Texture blend persp dispatch
TS5  texshadeblend.affine.face                            Texture blend affine dispatch
TS6  texshadeblend.affine.texopaque.branching.lerp8 / ... (4 IDs)  Texture blend affine tris
TS7  texshadeblend.affine.face.nearclip                   Texture blend affine face clip
TS8  texshadeblend.persp.texopaque.lerp8[_v3].span.vec (4)  SIMD 8-pixel kernels
TS9  texshadeblend.persp.texopaque.branching.lerp8_v3.scanline / ...  SIMD persp scanlines (scalar has both)
TS10 texshadeblend.affine.texopaque.branching.lerp8.scanline / ...branching.lerp8_v3.scanline / ... SIMD affine scanlines
TS11 texshadeblend.persp.texopaque.sort.lerp8 / ...trans...    Legacy shadeblend (SWAP sort)
```

---

## Maintenance

Keep the **plan** as the source of truth for SIMD detail, naming proposals, and directory migration. Update **this** file when the master table or Variant_ID conventions change.

- **Directory migration progress:** [RASTER_DIRECTORY_MIGRATION.md](RASTER_DIRECTORY_MIGRATION.md)
- **Optional C rename scope:** [RASTER_C_SYMBOL_RENAME_SCOPE.md](RASTER_C_SYMBOL_RENAME_SCOPE.md)
