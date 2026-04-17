# Rasterization variant catalogue

This document is the **standalone export** of the master variant table, including a **Variant_ID** column for every **F** / **G** / **TF** / **TS** row. IDs follow the grammar documented in the Cursor plan (see link below): `<family>.<space>.<gate>.<kernel>[.<walk>][.<layer>][.vec]`, with optional `legacy:`, `unused:`, or `reference:` prefixes.

**Canonical narrative** (SIMD chains, face/scanline/span naming, proposed file layout, migration notes) lives in the plan — do not duplicate it here unless you need an offline copy.

- **Plan (SIMD, naming, migration):** [Raster variant catalogue](../.cursor/plans/raster_variant_catalogue_2aabb9e0.plan.md)

**Primary include graph (live client):** [`dash.c`](../src/graphics/dash.c) includes [`render_flat.u.c`](../src/graphics/old/render_flat.u.c), [`render_gouraud.u.c`](../src/graphics/render_gouraud.u.c), and [`render_texture.u.c`](../src/graphics/old/render_texture.u.c) (which pulls in affine routing via [`render_texture_affine.u.c`](../src/graphics/old/render_texture_affine.u.c)).

---

## Master table (with Variant_ID)

Rows are **distinct implementation surfaces**: triangle-level `raster_*` entry points, ordered-triangle helpers, face/near-clip wrappers, scanline helpers, and SIMD ISA backends that re-implement the same symbols.

When one catalogue row maps to **multiple** canonical IDs (e.g. opaque vs transparent, or ordered siblings), **Variant_ID** lists them separated by ` / ` in a single cell (same convention as the plan).

| Row | Variant_ID | Category | Projection | Role / shading | Representative symbol(s) | Source file(s) |
|-----|------------|----------|------------|------------------|---------------------------|----------------|
| F1 | `flat.screen.opaque.branching.s4` | Flat | screen | opaque triangle | `raster_flat_branching_bs4` | [`flat_branching_bs4.c`](../src/graphics/old/flat_branching_bs4.c) → [`raster/flat/flat.screen.opaque.branching.s4.c`](../src/graphics/raster/flat/flat.screen.opaque.branching.s4.c) |
| F2 | `flat.screen.alpha.branching.s4` | Flat | screen | alpha fill triangle | `raster_flat_alpha_branching_bs4` | `flat_branching_bs4.c` → [`raster/flat/flat.screen.alpha.branching.s4.c`](../src/graphics/raster/flat/flat.screen.alpha.branching.s4.c) |
| F3 | `flat.screen.opaque.branching.s4.ordered` / `flat.screen.alpha.branching.s4.ordered` | Flat | screen | ordered-walk helpers | `raster_flat_branching_bs4_ordered`, `raster_flat_alpha_branching_bs4_ordered` | same as F1/F2 (`raster/flat/flat.screen.*.branching.s4.c`) |
| F4 | `legacy:flat.screen.opaque.sort.s4` / `legacy:flat.screen.alpha.sort.s4` | Flat (legacy) | screen | opaque / alpha s4 path | `raster_flat_sort_s4`, `raster_flat_alpha_sort_s4` | [`flat.u.c`](../src/graphics/old/flat.u.c) → [`raster/flat/flat.screen.opaque.sort.s4.u.c`](../src/graphics/raster/flat/flat.screen.opaque.sort.s4.u.c), [`flat.screen.alpha.sort.s4.u.c`](../src/graphics/raster/flat/flat.screen.alpha.sort.s4.u.c) (not called from `render_flat` today) |
| F5 | `flat.screen.face` | Flat | screen | dispatch + clip faces | `raster_flat`, `raster_face_flat*`, `raster_face_flat_near_clip` | `render_flat.u.c` |
| G1 | `gouraud.screen.opaque.bary.branching.s4` | Gouraud | screen | opaque bs4 barycentric | `raster_gouraud_bary_branching_bs4` | [`gouraud_branching_barycentric.c`](../src/graphics/old/gouraud_branching_barycentric.c) → [`raster/gouraud/gouraud.screen.opaque.bary.branching.s4.c`](../src/graphics/raster/gouraud/gouraud.screen.opaque.bary.branching.s4.c) |
| G2 | `gouraud.screen.alpha.bary.branching.s4` | Gouraud | screen | alpha bs4 barycentric | `raster_gouraud_alpha_bary_branching_bs4` | `gouraud_branching_barycentric.c` → [`raster/gouraud/gouraud.screen.alpha.bary.branching.s4.c`](../src/graphics/raster/gouraud/gouraud.screen.alpha.bary.branching.s4.c) |
| G3 | `gouraud.screen.opaque.bary.branching.s4.ordered` / `gouraud.screen.alpha.bary.branching.s4.ordered` | Gouraud | screen | ordered bary variants | `raster_gouraud_bary_branching_bs4_ordered`, `raster_gouraud_alpha_bary_branching_bs4_ordered` | same as G1/G2 (`raster/gouraud/gouraud.screen.*.bary.branching.s4.c`) |
| G4 | `gouraud.screen.opaque.bary.branching.s1` | Gouraud | screen | opaque branching s1 barycentric | `raster_gouraud_bary_branching_s1` | `gouraud_s1_branching_barycentric.c` |
| G5 | `gouraud.screen.opaque.bary.branching.s1.ordered` | Gouraud | screen | ordered s1 helper | `raster_gouraud_bary_branching_s1_ordered` | `gouraud_s1_branching_barycentric.c` |
| G5a | `gouraud.screen.alpha.bary.branching.s1` | Gouraud | screen | alpha branching s1 (smooth path) | `raster_gouraud_alpha_bary_branching_s1`, `raster_gouraud_alpha_bary_branching_s1_ordered` | [`gouraud_s1_branching_barycentric.c`](../src/graphics/old/gouraud_s1_branching_barycentric.c) → [`raster/gouraud/gouraud.screen.alpha.bary.branching.s1.c`](../src/graphics/raster/gouraud/gouraud.screen.alpha.bary.branching.s1.c) |
| G6 | `gouraud.screen.face` | Gouraud | screen | dispatch + clip faces | `raster_gouraud`, `raster_gouraud_edge_sort_s1`, `raster_face_gouraud*`, `raster_face_gouraud_near_clip*` | [`render_gouraud.u.c`](../src/graphics/render_gouraud.u.c) |
| G7 | `legacy:gouraud.screen.opaque.edge.sort.s4` / `gouraud.screen.alpha.edge.sort.s4` | Gouraud (legacy S4) | screen | edge-walk s4 + alpha | `raster_gouraud_edge_sort_s4`, `raster_gouraud_alpha_edge_sort_s4` | [`gouraud.u.c`](../src/graphics/old/gouraud.u.c) → [`raster/gouraud/gouraud.screen.opaque.edge.sort.s4.u.c`](../src/graphics/raster/gouraud/gouraud.screen.opaque.edge.sort.s4.u.c), [`gouraud.screen.alpha.edge.sort.s4.u.c`](../src/graphics/raster/gouraud/gouraud.screen.alpha.edge.sort.s4.u.c) (s4 opaque commented out in render; alpha still used from s1 path) |
| G8 | `gouraud.screen.opaque.bary_s4` | Gouraud (alt bary) | screen | full-triangle bary s4 | `raster_gouraud_s4_bary` | `gouraud_barycentric.u.c` (included from `gouraud.u.c`; not render default) |
| G9 | `gouraud.screen.alpha.span_alpha.vec` | Gouraud span blend | screen | per-pixel alpha span | `raster_linear_alpha_s4` | [`raster/gouraud/span/gouraud.screen.alpha.span.u.c`](../src/graphics/raster/gouraud/span/gouraud.screen.alpha.span.u.c) (via [`gouraud.u.c`](../src/graphics/old/gouraud.u.c); SIMD — see plan § SIMD Integration) |
| G10 | `gouraud.screen.opaque.edge.sort.s1` / `gouraud.screen.alpha.edge.sort.s1` | Gouraud (parallel TU) | screen | s1 edge path | `raster_gouraud_edge_sort_s1`, `raster_gouraud_alpha_edge_sort_s1` | [`gouraud_s1.u.c`](../src/graphics/old/gouraud_s1.u.c) → [`raster/gouraud/gouraud.screen.opaque.edge.sort.s1.u.c`](../src/graphics/raster/gouraud/gouraud.screen.opaque.edge.sort.s1.u.c), [`gouraud.screen.alpha.edge.sort.s1.u.c`](../src/graphics/raster/gouraud/gouraud.screen.alpha.edge.sort.s1.u.c) (alternate TU; `GOURAUD_S1_U_C`) |
| G11 | `unused:gouraud.screen.opaque.branching.s4` / `unused:gouraud.screen.opaque.branching.s4.ordered` | Gouraud (unused TU) | screen | non-bary branching bs4 | `raster_gouraud_bs4`, `raster_gouraud_ordered_bs4` | [`archive/gouraud_branching.c`](../src/graphics/archive/gouraud_branching.c) (never `#include`d in repo) |
| G12 | `unused:gouraud.screen.opaque.branching.s1.ordered` / `unused:gouraud.screen.opaque.branching.s4` | Gouraud (unused TU) | screen | bs1 + misnamed bs4 | `raster_gouraud_ordered_bs1`, `raster_gouraud_bs4` | [`archive/gouraud_s1_branching.c`](../src/graphics/archive/gouraud_s1_branching.c) (never `#include`d) |
| G13 | `reference:gouraud.screen.ref_deob` | Gouraud (reference) | n/a | decomp triangle | `gouraud_deob_draw_triangle`, `gouraud_deob_draw_scanline` | [`reference/gouraud_deob.c`](../src/graphics/reference/gouraud_deob.c) (standalone reference; not in `dash.c`) |
| TF1 | `texshadeflat.persp.texopaque.branching.lerp8` / `texshadeflat.persp.texopaque.sort.lerp8` | Texture flat | perspective | opaque triangle | **`raster_texture_opaque_branching_lerp8`** (active; `render_texture_flat`), `raster_texture_opaque_branching_lerp8_ordered`, `raster_texture_opaque_sort_lerp8` (y-sort; retained) | [`texture.u.c`](../src/graphics/old/texture.u.c) + [`texshadeflat.persp.texopaque.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeflat.persp.texopaque.branching.lerp8.u.c), [`texshadeflat.persp.texopaque.sort.lerp8.u.c`](../src/graphics/raster/texture/texshadeflat.persp.texopaque.sort.lerp8.u.c) (8-px span: `raster_linear_opaque_texshadeflat_lerp8` → **TS8** `raster_linear_opaque_blend_lerp8` per ISA) |
| TF2 | `texshadeflat.persp.textrans.branching.lerp8` / `texshadeflat.persp.textrans.sort.lerp8` | Texture flat | perspective | transparent triangle | **`raster_texture_transparent_branching_lerp8`** (active), `raster_texture_transparent_branching_lerp8_ordered`, `raster_texture_transparent_sort_lerp8` | `texture.u.c` + [`texshadeflat.persp.textrans.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeflat.persp.textrans.branching.lerp8.u.c), [`texshadeflat.persp.textrans.sort.lerp8.u.c`](../src/graphics/raster/texture/texshadeflat.persp.textrans.sort.lerp8.u.c) (ordered scanlines: `raster_linear_transparent_texshadeflat_lerp8` → **TS8** blend span) |
| TF3 | `texshadeflat.persp.texopaque.sort.lerp8.scanline` / `textrans…` + `…ordered.lerp8.scanline` (both gates) | Texture flat | perspective | scanline helpers | `raster_texture_scanline_opaque_sort_lerp8`, `raster_texture_scanline_transparent_sort_lerp8` (x-swap); `raster_texture_scanline_opaque_ordered_lerp8`, `raster_texture_scanline_transparent_ordered_lerp8` (no x-swap; branching path) | `texture.u.c` + four shards: [`texshadeflat.persp.texopaque.sort.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeflat.persp.texopaque.sort.lerp8.scanline.u.c), [`texshadeflat.persp.textrans.sort.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeflat.persp.textrans.sort.lerp8.scanline.u.c), [`texshadeflat.persp.texopaque.ordered.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeflat.persp.texopaque.ordered.lerp8.scanline.u.c), [`texshadeflat.persp.textrans.ordered.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeflat.persp.textrans.ordered.lerp8.scanline.u.c) |
| TF4 | `texshadeflat.affine.face` | Texture flat | affine | dispatch (reuses blend) | `raster_texture_flat_affine`, `raster_texture_flat_affine_v3` | `render_texture_affine.u.c` |
| TF5 | `texshadeflat.affine.texopaque.branching.lerp8` / `texshadeflat.affine.textrans.branching.lerp8` / `texshadeflat.affine.texopaque.branching.lerp8_v3` / `texshadeflat.affine.textrans.branching.lerp8_v3` | Texture flat | affine | opaque / transparent tri | `raster_texture_opaque_blend_affine_branching_lerp8(_v3)`, `raster_texture_transparent_blend_affine_branching_lerp8(_v3)` | [`texture_blend_branching_affine.u.c`](../src/graphics/old/texture_blend_branching_affine.u.c), [`texture_blend_branching_affine_v3.u.c`](../src/graphics/old/texture_blend_branching_affine_v3.u.c) → shared [`texshadeblend.affine.*.branching.lerp8*.u.c`](../src/graphics/raster/texture/texshadeblend.affine.texopaque.branching.lerp8.u.c) shards (same symbols as TS6; flat vs blend is usage) |
| TF6 | `texshadeflat.affine.face.nearclip` | Texture flat | affine | face + near clip | `raster_face_texture_flat_affine*`, `raster_face_texture_flat_affine*_near_clip` | `render_texture_affine.u.c` |
| TF7 | `texshadeflat.persp.face.nearclip` | Texture flat | perspective | face + near clip | `raster_face_texture_flat*`, `raster_face_texture_flat_near_clip` | `render_texture.u.c` |
| TF8 | `texshadeflat.persp.face` | Texture flat | perspective | dispatch | `raster_texture_flat` | `render_texture.u.c` |
| TS1 | `texshadeblend.persp.texopaque.branching.lerp8_v3` / `texshadeblend.persp.textrans.branching.lerp8_v3` | Texture Gouraud | perspective | opaque / transparent v3 | `raster_texture_opaque_blend_branching_lerp8_v3`, `raster_texture_transparent_blend_branching_lerp8_v3` | [`texture_blend_branching_v3.u.c`](../src/graphics/old/texture_blend_branching_v3.u.c) → [`raster/texture/texshadeblend.persp.texopaque.branching.lerp8_v3.u.c`](../src/graphics/raster/texture/texshadeblend.persp.texopaque.branching.lerp8_v3.u.c), [`texshadeblend.persp.textrans.branching.lerp8_v3.u.c`](../src/graphics/raster/texture/texshadeblend.persp.textrans.branching.lerp8_v3.u.c) (active; TS9 span) |
| TS2 | `texshadeblend.persp.texopaque.branching.lerp8` / `texshadeblend.persp.textrans.branching.lerp8` | Texture Gouraud | perspective | non-v3 branching.lerp8 | `raster_texture_*_blend_branching_lerp8`, `*_branching_lerp8_ordered`, `draw_texture_scanline_*_blend_branching_lerp8_ordered` (span) | [`texture_blend_branching.u.c`](../src/graphics/old/texture_blend_branching.u.c) → [`raster/texture/texshadeblend.persp.texopaque.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.persp.texopaque.branching.lerp8.u.c), [`texshadeblend.persp.textrans.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.persp.textrans.branching.lerp8.u.c) (superseded by TS1 for default dispatch) |
| TS3 | `texshadeblend.persp.face.nearclip` | Texture Gouraud | perspective | face + near clip | `raster_face_texture_blend*`, `raster_face_texture_blend_near_clip` | `render_texture.u.c` |
| TS4 | `texshadeblend.persp.face` | Texture Gouraud | perspective | dispatch | `raster_texture_blend` | `render_texture.u.c` |
| TS5 | `texshadeblend.affine.face` | Texture Gouraud | affine | dispatch | `raster_texture_blend_affine`, `raster_texture_blend_affine_v3` | `render_texture_affine.u.c` |
| TS6 | `texshadeblend.affine.texopaque.branching.lerp8` / `texshadeblend.affine.textrans.branching.lerp8` / `texshadeblend.affine.texopaque.branching.lerp8_v3` / `texshadeblend.affine.textrans.branching.lerp8_v3` | Texture Gouraud | affine | opaque / transparent tri | same affine symbols as TF5 (shade varies per vertex) | [`texture_blend_branching_affine.u.c`](../src/graphics/old/texture_blend_branching_affine.u.c), [`texture_blend_branching_affine_v3.u.c`](../src/graphics/old/texture_blend_branching_affine_v3.u.c) → [`texshadeblend.affine.texopaque.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.affine.texopaque.branching.lerp8.u.c), [`texshadeblend.affine.textrans.branching.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.affine.textrans.branching.lerp8.u.c), [`texshadeblend.affine.texopaque.branching.lerp8_v3.u.c`](../src/graphics/raster/texture/texshadeblend.affine.texopaque.branching.lerp8_v3.u.c), [`texshadeblend.affine.textrans.branching.lerp8_v3.u.c`](../src/graphics/raster/texture/texshadeblend.affine.textrans.branching.lerp8_v3.u.c); TS10 span |
| TS7 | `texshadeblend.affine.face.nearclip` | Texture Gouraud | affine | face + near clip | `raster_face_texture_blend_affine*` | `render_texture_affine.u.c` |
| TS8 | `texshadeblend.persp.texopaque.lerp8.span.vec` / `texshadeblend.persp.textrans.lerp8.span.vec` / `texshadeblend.persp.texopaque.lerp8_v3.span.vec` / `texshadeblend.persp.textrans.lerp8_v3.span.vec` | Texture (SIMD kernel) | perspective | 8-pixel lerp8 + v3 | `raster_linear_*_blend_lerp8[_v3]`; parallel **texshadeflat** entry `raster_linear_{opaque,transparent}_texshadeflat_lerp8` (forwards to blend lerp8) | [`raster/texture/span/tex.span.u.c`](../src/graphics/raster/texture/span/tex.span.u.c) → `tex.span.{scalar,sse2,sse41,avx,neon}.u.c` |
| TS9 | `texshadeblend.persp.texopaque.branching.lerp8_v3.scanline` / `texshadeblend.persp.textrans.branching.lerp8_v3.scanline` | Texture (SIMD scan) | perspective | scanline branching.lerp8 v3 | `draw_texture_scanline_*_blend_branching_lerp8_v3_ordered` | `raster/texture/span/tex.span.*.u.c` (scalar + SIMD: both gates) |
| TS10 | `texshadeblend.affine.texopaque.branching.lerp8.scanline` / `texshadeblend.affine.textrans.branching.lerp8.scanline` / `texshadeblend.affine.texopaque.branching.lerp8_v3.scanline` / `texshadeblend.affine.textrans.branching.lerp8_v3.scanline` | Texture (SIMD scan) | affine | ordered scanline (non-ish16 + ish16 v3) | `draw_texture_scanline_*_blend_affine_branching_lerp8_ordered`, `draw_texture_scanline_*_blend_affine_branching_lerp8_ish16_ordered` | `raster/texture/span/tex.span.*.u.c` (all ISAs including scalar) |
| TS11 | `texshadeblend.persp.texopaque.sort.lerp8` / `texshadeblend.persp.textrans.sort.lerp8` | Texture Gouraud | perspective | SWAP-sorted triangle lerp8 | `raster_texture_*_blend_sort_lerp8` | [`texture.u.c`](../src/graphics/old/texture.u.c) + [`raster/texture/texshadeblend.persp.texopaque.sort.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.persp.texopaque.sort.lerp8.u.c), [`texshadeblend.persp.textrans.sort.lerp8.u.c`](../src/graphics/raster/texture/texshadeblend.persp.textrans.sort.lerp8.u.c) + scanlines [`texshadeblend.persp.texopaque.sort.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeblend.persp.texopaque.sort.lerp8.scanline.u.c), [`texshadeblend.persp.textrans.sort.lerp8.scanline.u.c`](../src/graphics/raster/texture/texshadeblend.persp.textrans.sort.lerp8.scanline.u.c) (`raster_texture_scanline_*_blend_sort_lerp8`; legacy path; TS8 span) |

---

## How to read this (short)

- **Flat / Gouraud:** triangle work is mostly in `raster_*`; **F5** and **G6** are **face-level** APIs from the model pipeline.
- **Texture flat vs texture Gouraud:** same affine kernels (**TF5** / **TS6**); flat mode passes a repeated per-vertex shade through the blend affine entry points (affine **branching.lerp8** triangle shards + **TS10** span scanlines).
- **Perspective shaded path today:** `raster_texture_blend` uses the **v3 branching.lerp8** symbols (**TS1**), not the older `texture.u.c` triangle rasterizers (**TS11**). **Perspective flat** (`raster_texture_flat`) uses **TF1/TF2 branching.lerp8**; sort variants remain in the same TU for parity / benches.
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
G1   gouraud.screen.opaque.bary.branching.s4              Gouraud opaque bary bs4
G2   gouraud.screen.alpha.bary.branching.s4               Gouraud alpha bary bs4
G3   gouraud.screen.opaque.bary.branching.s4.ordered / ... Gouraud ordered bary
G4   gouraud.screen.opaque.bary.branching.s1              Gouraud opaque bary branching s1
G5   gouraud.screen.opaque.bary.branching.s1.ordered      Gouraud ordered s1
G5a  gouraud.screen.alpha.bary.branching.s1               Gouraud alpha branching s1 (smooth path)
G6   gouraud.screen.face                                   Gouraud face dispatch
G7   legacy:gouraud.screen.opaque.edge.sort.s4 / gouraud...alpha.edge.sort.s4  Gouraud legacy s4 + alpha
G8   gouraud.screen.opaque.bary_s4                        Gouraud alt bary s4
G9   gouraud.screen.alpha.span_alpha.vec                   Gouraud SIMD alpha span
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
