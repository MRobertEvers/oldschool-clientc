---
name: Raster variant catalogue
overview: Inventory of all software rasterization implementations under `src/graphics/`, grouped into Flat, Gouraud, texture flat-shaded, and texture Gouraud-shaded, with a single reference table (including perspective vs affine, opaque vs transparent, and SIMD/scanline layers).
todos:
  - id: optional-doc
    content: If desired, copy the master table into a project doc (e.g. docs/) — user did not request a file write in this task.
    status: pending
isProject: false
---

# Rasterization variant catalogue

Primary include graph for the live client: [`src/graphics/dash.c`](src/graphics/dash.c) pulls in [`render_flat.u.c`](src/graphics/render_flat.u.c), [`render_gouraud.u.c`](src/graphics/render_gouraud.u.c), and [`render_texture.u.c`](src/graphics/render_texture.u.c) (which transitively includes affine routing via [`render_texture_affine.u.c`](src/graphics/render_texture_affine.u.c)).

Below is a **single master table** (fixed-width inside a code block so it stays readable where markdown tables are discouraged). Rows are **distinct implementation surfaces**: triangle-level `raster_*` entry points, important **ordered-triangle** siblings used internally, **face/near-clip** wrappers, **scanline** helpers, and **SIMD ISA backends** that re-implement the same symbols.

```text
Row  Category              Projection   Role / shading           Symbol(s) (representative)                    Source file(s)
---  --------------------  -----------  -------------------------  ------------------------------------------  ----------------------------------------------
F1   Flat                  screen       opaque triangle          raster_flat_bs4                             flat_branching_bs4.c
F2   Flat                  screen       semi-transparent tri.    raster_flat_alpha_bs4                       flat_branching_bs4.c
F3   Flat                  screen       ordered-walk helpers     raster_flat_ordered_bs4, raster_flat_alpha_ordered_bs4  flat_branching_bs4.c (internal to F1/F2)
F4   Flat (legacy)         screen       opaque / alpha s4 path   raster_flat_s4, raster_flat_alpha_s4        flat.u.c (not called from render_flat today)
F5   Flat                  screen       dispatch + clip faces    raster_flat, raster_face_flat*, raster_face_flat_near_clip  render_flat.u.c
--
G1   Gouraud               screen       opaque bs4 barycentric   raster_gouraud_bary_bs4                     gouraud_branching_barycentric.c
G2   Gouraud               screen       alpha bs4 barycentric    raster_gouraud_alpha_bary_bs4               gouraud_branching_barycentric.c
G3   Gouraud               screen       ordered bary variants    raster_gouraud_ordered_bary_bs4, raster_gouraud_ordered_bary_alpha_bs4  gouraud_branching_barycentric.c
G4   Gouraud               screen       opaque bs1 barycentric   raster_gouraud_bary_bs1                     gouraud_s1_branching_barycentric.c
G5   Gouraud               screen       ordered bs1 helper       raster_gouraud_ordered_bary_bs1             gouraud_s1_branching_barycentric.c
G6   Gouraud               screen       dispatch + clip faces    raster_gouraud, raster_gouraud_s1, raster_face_gouraud*, raster_face_gouraud_near_clip*  render_gouraud.u.c
G7   Gouraud (legacy S4)   screen       edge-walk s4 + alpha     raster_gouraud_s4, raster_gouraud_alpha_s4 gouraud.u.c (s4 opaque commented out in render; alpha still used from s1 path)
G8   Gouraud (alt bary)    screen       full-triangle bary s4    raster_gouraud_s4_bary                     gouraud_barycentric.u.c (included from gouraud.u.c; not render default)
G9   Gouraud span blend    screen       per-pixel alpha span     raster_linear_alpha_s4                     gouraud_simd_alpha.u.c (multiple definitions behind ISA)
G10  Gouraud (parallel TU) screen       s1 edge path             raster_gouraud_s1, raster_gouraud_alpha_s1  gouraud_s1.u.c (same GOURAUD_U_C guard as gouraud.u.c — alternate translation unit, not mixed with gouraud.u.c in one build)
G11  Gouraud (unused TU)   screen       non-bary branching bs4   raster_gouraud_bs4, raster_gouraud_ordered_bs4  gouraud_branching.c (never #included in repo)
G12  Gouraud (unused TU)   screen       bs1 + misnamed bs4       raster_gouraud_ordered_bs1, raster_gouraud_bs4  gouraud_s1_branching.c (never #included)
G13  Gouraud (reference)   n/a          decomp triangle          gouraud_deob_draw_triangle, gouraud_deob_draw_scanline  gouraud_deob.c (standalone reference; not in dash.c)
--
TF1  Texture flat          perspective  opaque tri               raster_texture_opaque_lerp8                 texture.u.c
TF2  Texture flat          perspective  transparent tri          raster_texture_transparent_lerp8            texture.u.c
TF3  Texture flat          perspective  scanline helpers         raster_texture_scanline_opaque_lerp8, raster_texture_scanline_transparent_lerp8 (+ blend scanlines exist in same file)  texture.u.c
TF4  Texture flat          affine       dispatch (reuses blend)  raster_texture_flat_affine, raster_texture_flat_affine_v3  render_texture_affine.u.c -> texture_blend_branching_affine*.u.c
TF5  Texture flat          affine       opaque / transparent     raster_texture_opaque_blend_affine(_v3), raster_texture_transparent_blend_affine(_v3)  texture_blend_branching_affine.u.c, texture_blend_branching_affine_v3.u.c (+ ordered_* siblings)
TF6  Texture flat          affine       face + near clip         raster_face_texture_flat_affine*, raster_face_texture_flat_affine*_near_clip  render_texture_affine.u.c
TF7  Texture flat          perspective  face + near clip         raster_face_texture_flat*, raster_face_texture_flat_near_clip  render_texture.u.c
TF8  Texture flat          perspective  dispatch                 raster_texture_flat                         render_texture.u.c
--
TS1  Texture Gouraud       perspective  opaque / transparent v3  raster_texture_opaque_blend_blerp8_v3, raster_texture_transparent_blend_blerp8_v3  texture_blend_branching_v3.u.c (active from render_texture.u.c)
TS2  Texture Gouraud       perspective  non-v3 blerp8            raster_texture_*_blend_blerp8, *_ordered_blerp8  texture_blend_branching.u.c (included; superseded by TS1 for default dispatch)
TS3  Texture Gouraud       perspective  face + near clip         raster_face_texture_blend*, raster_face_texture_blend_near_clip  render_texture.u.c
TS4  Texture Gouraud       perspective  dispatch                 raster_texture_blend                        render_texture.u.c
TS5  Texture Gouraud       affine       dispatch                 raster_texture_blend_affine, raster_texture_blend_affine_v3  render_texture_affine.u.c
TS6  Texture Gouraud       affine       opaque / transparent     same affine symbols as TF5 (shade varies per vertex)  texture_blend_branching_affine*.u.c
TS7  Texture Gouraud       affine       face + near clip         raster_face_texture_blend_affine*  render_texture_affine.u.c
TS8  Texture (persp inner) perspective  scanline lerp8 + v3      raster_linear_*_blend_lerp8(_v3)            texture_simd.{scalar,sse2,sse41,avx,neon}.u.c (one picked via texture_simd.u.c)
TS9  Texture (persp inner) perspective  ordered blerp8 v3        draw_texture_scanline_*_blend_ordered_blerp8_v3  texture_simd.*.u.c (scalar: transparent only; others: opaque+transparent)
TS10 Texture (affine inner) affine       ordered affine ish16    draw_texture_scanline_*_blend_affine_ordered_ish16  texture_simd.*.u.c
TS11 Texture Gouraud       perspective  full-triangle (non-v3)   raster_texture_*_blend_lerp8                texture.u.c (legacy / bench-oriented vs branching path)
```

### How to read this

- **Flat / Gouraud**: triangle work is almost entirely in the `raster_*` symbols above; **F5** and **G6** are the **face-level** APIs used from the model pipeline (near clipping, vertex fetch).
- **Texture flat vs texture shaded**: both share the **same affine kernels** (**TF5** / **TS6**); flat mode passes a **repeated per-vertex shade** through the blend affine entry points (see [`raster_texture_flat_affine_v3`](src/graphics/render_texture_affine.u.c) around lines 222–248).
- **Perspective “shaded” path today**: [`raster_texture_blend`](src/graphics/render_texture.u.c) always calls the **`_v3` blerp8** symbols (**TS1**), not the older `texture.u.c` **TS11** triangle rasterizers.
- **SIMD**: **TS8–TS10** are not separate “categories”; they are **ISA-specialized implementations** of the same function names selected by [`texture_simd.u.c`](src/graphics/texture_simd.u.c).
- **Repo-only / not in `dash.c`**: **F4**, **G8**, **G10–G13**, standalone copies [`texture_opaque_blend_affine.c`](src/graphics/texture_opaque_blend_affine.c) / [`texture_transparent_blend_affine.c`](src/graphics/texture_transparent_blend_affine.c) (duplicate symbol names; not `#include`d anywhere found), and [`docs/gouraud_raster.c`](docs/gouraud_raster.c) (standalone demo).

No code changes are required for this catalogue; it is documentation-only unless you want this table committed to a specific doc path later.
