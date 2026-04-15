---
name: Raster variant catalogue
overview: Inventory of all software rasterization implementations under `src/graphics/`, grouped into Flat, Gouraud, texture flat-shaded, and texture Gouraud-shaded, with a single reference table (including perspective vs affine, opaque vs transparent, and SIMD/scanline layers). Follow-ups and todos live in this file only — do not fork a second plan for the same workstream.
todos:
  - id: fix-plan-filenames
    content: "Plan body uses correct repo paths: gouraud_simd_alpha.u.c (not gouraud_span_alpha); texture_simd.u.c (not tex_simd)."
    status: completed
  - id: variant-id-column
    content: Add Variant_ID column to the master table for all F/G/TF/TS rows per the naming grammar (legacy:/unused: where applicable).
    status: pending
  - id: optional-docs-export
    content: Optionally add docs/RASTER_VARIANT_CATALOGUE.md with table + intro; link back to this plan for SIMD/layout/migration.
    status: pending
isProject: false
---

# Rasterization variant catalogue

Primary include graph for the live client: [`src/graphics/dash.c`](src/graphics/dash.c) pulls in [`render_flat.u.c`](src/graphics/render_flat.u.c), [`render_gouraud.u.c`](src/graphics/render_gouraud.u.c), and [`render_tex.u.c`](src/graphics/render_tex.u.c) (which transitively includes affine routing via [`render_tex_affine.u.c`](src/graphics/render_tex_affine.u.c)).

Below is a **single master table** (fixed-width inside a code block). Rows are **distinct implementation surfaces**: triangle-level `raster_*` entry points, important **ordered-triangle** siblings used internally, **face/near-clip** wrappers, **scanline** helpers, and **SIMD ISA backends** that re-implement the same symbols.

```text
Row  Category              Projection   Role / shading            Symbol(s) (representative)                              Source file(s)
---  --------------------  -----------  ------------------------   ------------------------------------------              ----------------------------------------------
F1   Flat                  screen       opaque triangle           raster_flat_bs4                                         flat_branching_bs4.c
F2   Flat                  screen       alpha fill triangle       raster_flat_alpha_bs4                                   flat_branching_bs4.c
F3   Flat                  screen       ordered-walk helpers      raster_flat_ordered_bs4, raster_flat_alpha_ordered_bs4   flat_branching_bs4.c (internal to F1/F2)
F4   Flat (legacy)         screen       opaque / alpha s4 path    raster_flat_s4, raster_flat_alpha_s4                    flat.u.c (not called from render_flat today)
F5   Flat                  screen       dispatch + clip faces     raster_flat, raster_face_flat*, raster_face_flat_near_clip  render_flat.u.c
--
G1   Gouraud               screen       opaque bs4 barycentric    raster_gouraud_bary_bs4                                 gouraud_branching_barycentric.c
G2   Gouraud               screen       alpha bs4 barycentric     raster_gouraud_alpha_bary_bs4                           gouraud_branching_barycentric.c
G3   Gouraud               screen       ordered bary variants     raster_gouraud_ordered_bary_bs4, raster_gouraud_ordered_bary_alpha_bs4  gouraud_branching_barycentric.c
G4   Gouraud               screen       opaque bs1 barycentric    raster_gouraud_bary_bs1                                 gouraud_s1_branching_barycentric.c
G5   Gouraud               screen       ordered bs1 helper        raster_gouraud_ordered_bary_bs1                         gouraud_s1_branching_barycentric.c
G6   Gouraud               screen       dispatch + clip faces     raster_gouraud, raster_gouraud_s1, raster_face_gouraud*, raster_face_gouraud_near_clip*  render_gouraud.u.c
G7   Gouraud (legacy S4)   screen       edge-walk s4 + alpha      raster_gouraud_s4, raster_gouraud_alpha_s4              gouraud.u.c (s4 opaque commented out in render; alpha still used from s1 path)
G8   Gouraud (alt bary)    screen       full-triangle bary s4     raster_gouraud_s4_bary                                  gouraud_barycentric.u.c (included from gouraud.u.c; not render default)
G9   Gouraud span blend    screen       per-pixel alpha span      raster_linear_alpha_s4                                  gouraud_simd_alpha.u.c (SIMD -- see Chain 1 below)
G10  Gouraud (parallel TU) screen       s1 edge path              raster_gouraud_s1, raster_gouraud_alpha_s1              gouraud_s1.u.c (alternate TU; same GOURAUD_U_C guard)
G11  Gouraud (unused TU)   screen       non-bary branching bs4    raster_gouraud_bs4, raster_gouraud_ordered_bs4          gouraud_branching.c (never #included in repo)
G12  Gouraud (unused TU)   screen       bs1 + misnamed bs4        raster_gouraud_ordered_bs1, raster_gouraud_bs4          gouraud_s1_branching.c (never #included)
G13  Gouraud (reference)   n/a          decomp triangle           gouraud_deob_draw_triangle, gouraud_deob_draw_scanline  gouraud_deob.c (standalone reference; not in dash.c)
--
TF1  Texture flat          perspective  opaque triangle           raster_texture_opaque_lerp8                             texture.u.c (calls SIMD kernel TS8)
TF2  Texture flat          perspective  transparent triangle      raster_texture_transparent_lerp8                        texture.u.c (calls SIMD kernel TS8)
TF3  Texture flat          perspective  scanline helpers          raster_texture_scanline_opaque_lerp8, raster_texture_scanline_transparent_lerp8  texture.u.c
TF4  Texture flat          affine       dispatch (reuses blend)   raster_texture_flat_affine, raster_texture_flat_affine_v3  render_tex_affine.u.c
TF5  Texture flat          affine       opaque / transparent tri  raster_texture_opaque_blend_affine(_v3), raster_texture_transparent_blend_affine(_v3)  texture_blend_branching_affine.u.c, texture_blend_branching_affine_v3.u.c
TF6  Texture flat          affine       face + near clip          raster_face_texture_flat_affine*, raster_face_texture_flat_affine*_near_clip  render_tex_affine.u.c
TF7  Texture flat          perspective  face + near clip          raster_face_texture_flat*, raster_face_texture_flat_near_clip  render_tex.u.c
TF8  Texture flat          perspective  dispatch                  raster_texture_flat                                     render_tex.u.c
--
TS1  Texture Gouraud       perspective  opaque / transparent v3   raster_texture_opaque_blend_blerp8_v3, raster_texture_transparent_blend_blerp8_v3  texture_blend_branching_v3.u.c (active; calls SIMD scanline TS9)
TS2  Texture Gouraud       perspective  non-v3 blerp8             raster_texture_*_blend_blerp8, *_ordered_blerp8         texture_blend_branching.u.c (superseded by TS1 for default dispatch)
TS3  Texture Gouraud       perspective  face + near clip          raster_face_texture_blend*, raster_face_texture_blend_near_clip  render_tex.u.c
TS4  Texture Gouraud       perspective  dispatch                  raster_texture_blend                                    render_tex.u.c
TS5  Texture Gouraud       affine       dispatch                  raster_texture_blend_affine, raster_texture_blend_affine_v3  render_tex_affine.u.c
TS6  Texture Gouraud       affine       opaque / transparent tri  same affine symbols as TF5 (shade varies per vertex)    texture_blend_branching_affine*.u.c (calls SIMD scanline TS10)
TS7  Texture Gouraud       affine       face + near clip          raster_face_texture_blend_affine*                       render_tex_affine.u.c
TS8  Texture (SIMD kernel) perspective  8-pixel lerp8 + v3        raster_linear_*_blend_lerp8[_v3]                        texture_simd.{scalar,sse2,sse41,avx,neon}.u.c (Chain 2)
TS9  Texture (SIMD scan)   perspective  scanline blerp8 v3        draw_texture_scanline_*_blend_ordered_blerp8_v3         texture_simd.*.u.c (scalar: tex_trans only; SIMD: both)
TS10 Texture (SIMD scan)   affine       scanline affine ish16     draw_texture_scanline_*_blend_affine_ordered_ish16      texture_simd.*.u.c (all ISAs including scalar)
TS11 Texture Gouraud       perspective  full-triangle lerp8       raster_texture_*_blend_lerp8                            texture.u.c (legacy path; calls TS8)
```

### How to read this

- **Flat / Gouraud**: triangle work is almost entirely in the `raster_*` symbols above; **F5** and **G6** are the **face-level** APIs used from the model pipeline (near clipping, vertex fetch).
- **Texture flat vs texture Gouraud**: both share the **same affine kernels** (TF5 / TS6); flat mode passes a **repeated per-vertex shade** through the blend affine entry points (see [`raster_texture_flat_affine_v3`](src/graphics/render_tex_affine.u.c) lines 222-248 where it calls the opaque/transparent blend with `shade, shade, shade`).
- **Perspective "shaded" path today**: [`raster_texture_blend`](src/graphics/render_tex.u.c) always calls the **v3 blerp8** symbols (TS1), not the older `texture.u.c` triangle rasterizers (TS11).
- **SIMD**: G9 (Gouraud alpha span) and TS8-TS10 (texture inner loops) have explicit SIMD intrinsics -- see the SIMD Integration section below. All other variants rely on compiler auto-vectorization only.
- **Scalar gap**: `draw_texture_scanline_opaque_blend_ordered_blerp8_v3` (TS9 opaque perspective scanline) is **absent** from [`texture_simd.scalar.u.c`](src/graphics/texture_simd.scalar.u.c). Only the transparent variant is present. All SIMD ISA files (NEON, SSE2, SSE4.1, AVX2) provide both.
- **Repo-only / not in `dash.c`**: F4, G8, G10-G13, standalone copies [`tex_shadeblend_affine_opaque.c`](src/graphics/tex_shadeblend_affine_opaque.c) / [`tex_shadeblend_affine_trans.c`](src/graphics/tex_shadeblend_affine_trans.c) (duplicate symbol names; not `#include`d anywhere), and [`docs/gouraud_raster.c`](docs/gouraud_raster.c) (standalone demo).

No code changes are required for the core catalogue narrative; optional follow-ups may add a `docs/` export or extend the master table (see below).

### Plan maintenance

Keep all catalogue iteration in **this** plan file. Do not create a separate Cursor plan for the same documentation workstream.

### Planned documentation follow-ups

1. **Variant_ID column** — Add a column to the fixed-width master table for every row, using the grammar in [Proposed naming scheme](#proposed-naming-scheme) (`legacy:` / `unused:` prefixes where applicable). For texture rows with opaque/trans pairs, either one cell with two IDs separated by ` / ` or aligned sub-rows — pick one convention and use it consistently.
2. **Optional `docs/` export** — e.g. `docs/RASTER_VARIANT_CATALOGUE.md` with a short intro, the table (including Variant_ID once added), and pointers to the SIMD integration and migration sections in this plan (avoid duplicating the full document unless you want a standalone reader copy).
3. **Deferred tracks** (when you pick them up, still describe progress here): scalar `draw_texture_scanline_opaque_blend_ordered_blerp8_v3` in `texture_simd.scalar.u.c`; phased directory migration; C symbol renames.

---

## SIMD Integration

There are exactly **two separate SIMD selection chains** in the rasterizer. They cover different operations and different render categories.

### Chain 1 - Gouraud alpha span (`gouraud_simd_alpha.u.c`)

Covers: **G9** only.

**Selection:** single `#if/#elif/#else` block inside one file:

```text
NEON  ->  AVX2  ->  SSE2+SSE4.1 (shared block)  ->  scalar fallback
```

**Symbol:** one symbol, one width:

```text
raster_linear_alpha_s4(pixel_buffer, offset, rgb_color, alpha)  -- 4 pixels at a time
```

**Operation:** alpha-blends a solid `rgb_color` over 4 existing framebuffer pixels using `alpha / (0xFF - alpha)` weights. This is **destination compositing**, not shade multiplication.

**ISA details:**

- NEON: `vld1q_u32` load, `vdupq_n_u32` broadcast, `vmulq_n_u16` / `vmlaq_n_u16` / `vshrq_n_u16` multiply-accumulate, `vst1q_u32` store.
- AVX2: uses 128-bit XMM registers only (not YMM/256-bit). Helper named `alpha_blend4_avx2` is functionally identical to the SSE2 variant. (Source comment: SSE was found slower than compiler auto-vectorized scalar on efficiency cores; AVX does not have this penalty.)
- SSE2 / SSE4.1 (combined guard `defined(__SSE2__) || defined(__SSE4_1__)`): `_mm_loadu_si128`, `_mm_mullo_epi16`, `_mm_srli_epi16`, `_mm_packus_epi16`.
- Scalar: plain loop of 4 iterations calling `alpha_blend()`.

**Included by:** [`gouraud.u.c`](src/graphics/gouraud.u.c) and [`gouraud_s1.u.c`](src/graphics/gouraud_s1.u.c).

No SIMD exists for the **opaque** Gouraud paths (G1-G8). Those rely on compiler auto-vectorization of the `g_hsl16_to_rgb_table` lookup loops in `gouraud_barycentric.u.c` and `gouraud_branching_barycentric.c`.

---

### Chain 2 - Texture inner loops (`texture_simd.u.c`)

Covers: **TS8, TS9, TS10** -- and transitively every texture triangle raster that calls these kernels (TF1-TF5, TS1-TS2, TS6, TS11).

**Selection:** [`texture_simd.u.c`](src/graphics/texture_simd.u.c) picks one file at compile time:

```text
NEON  ->  AVX2  ->  SSE4.1  ->  SSE2  ->  scalar
```

**Symbols per ISA file (8 in SIMD builds, 7 in scalar -- opaque perspective scanline missing):**

```text
Layer           Symbol                                                       gate        scalar?
--------------  -----------------------------------------------------------  ----------  -------
8-pixel kernel  raster_linear_opaque_blend_lerp8                            tex_opaque  yes (plain C)
8-pixel kernel  raster_linear_transparent_blend_lerp8                       tex_trans   yes (plain C)
8-pixel kernel  raster_linear_opaque_blend_lerp8_v3                         tex_opaque  yes (plain C)
8-pixel kernel  raster_linear_transparent_blend_lerp8_v3                    tex_trans   yes (plain C)
persp scanline  draw_texture_scanline_opaque_blend_ordered_blerp8_v3        tex_opaque  NO (gap)
persp scanline  draw_texture_scanline_transparent_blend_ordered_blerp8_v3   tex_trans   yes
affine scanline draw_texture_scanline_opaque_blend_affine_ordered_ish16     tex_opaque  yes
affine scanline draw_texture_scanline_transparent_blend_affine_ordered_ish16 tex_trans  yes
```

The `_v3` 8-pixel kernels use `__restrict`-qualified pointer arithmetic (no `offset` parameter) and are the **active** path from the v3 triangle rasterizers. The non-`_v3` kernels are called from the legacy `texture.u.c` path (TS11 / TF1-TF2).

**Operation:** shade-multiply texel RGB by a per-pixel lighting factor (distinct from Chain 1 alpha compositing):

```text
output = (texel_rgb * shade) >> 8   -- tex_opaque: always write
output = (texel_rgb * shade) >> 8   -- tex_trans:  skip write if texel == 0
```

**ISA details:**

- NEON: `vld1q_u32`, `vmulq_n_u16`, `vshrq_n_u16`, `vqmovn_u16`, `vcombine_u8`; transparent uses `vceqq_u32` + `vbslq_u32`. 8 pixels per call via two 128-bit vectors.
- AVX2: `shade_blend8_avx2` uses a single `__m256i` pass for 8 pixels; transparent uses `_mm256_cmpeq_epi32` + `_mm256_blendv_epi8`. Only ISA that processes all 8 pixels in a single vector operation.
- SSE4.1: `shade_blend4_sse` via `smmintrin.h`; two passes of 4 pixels each. Transparent uses `_mm_blendv_epi8` (SSE4.1 instruction).
- SSE2: identical code to SSE4.1 including `_mm_blendv_epi8`. This is intentional: `texture_simd.u.c` checks `__SSE4_1__` before `__SSE2__`, so the SSE2 file only activates on targets that report SSE2 but not SSE4.1.
- Scalar: plain C loops, auto-vectorizable. Missing the opaque perspective scanline (noted above).

**Texel gather is always scalar.** Even in SIMD builds, `u`/`v` coordinate computation and the `texels[u + v]` fetch are done with scalar integer arithmetic before the SIMD shade-blend. No platform has a texel gather that avoids the scalar gather phase.

**Call hierarchy (perspective Gouraud shaded, active path):**

```text
raster_texture_blend                                          [render_tex.u.c -- dispatch]
  raster_texture_opaque_blend_blerp8_v3                      [texture_blend_branching_v3.u.c -- triangle]
    raster_texture_opaque_blend_ordered_blerp8_v3             [same file -- ordered edge walk]
      draw_texture_scanline_opaque_blend_ordered_blerp8_v3   [texture_simd.*.u.c -- scanline (TS9)]
        raster_linear_opaque_blend_lerp8_v3                  [texture_simd.*.u.c -- 8-pixel SIMD kernel (TS8)]
```

**Call hierarchy (affine path, both tex_shadeflat and tex_shadeblend):**

```text
raster_texture_blend_affine_v3                                [render_tex_affine.u.c -- dispatch]
  raster_texture_opaque_blend_affine_v3                       [texture_blend_branching_affine_v3.u.c -- triangle]
    raster_texture_opaque_blend_affine_ordered_v3              [same file -- ordered edge walk]
      draw_texture_scanline_opaque_blend_affine_ordered_ish16 [texture_simd.*.u.c -- scanline (TS10)]
        raster_linear_opaque_blend_lerp8_v3                   [texture_simd.*.u.c -- 8-pixel SIMD kernel (TS8)]
```

**No SIMD** in flat rasterizers (`flat.u.c`, `flat_branching_bs4.c`), solid Gouraud triangle walkers, or face/near-clip wrappers (those are control flow, not inner loops).

---

## Proposed naming scheme

Use **two layers**: a stable **variant ID** for docs, benches, and toggles, and keep existing **C symbol names** until a deliberate rename pass.

### Variant ID grammar (recommended)

Compose IDs from **fixed-order dimensions** so two people never invent incompatible strings:

```text
<family>.<space>.<gate>.<kernel>[.<walk>][.<layer>]
```

| Token | Allowed values | Meaning |
|-------|----------------|---------|
| `family` | `flat`, `gouraud`, `tex_shadeflat`, `tex_shadeblend` | `tex_shadeflat` = texture with constant (flat) shade; `tex_shadeblend` = per-vertex shade blended with texels |
| `space` | `screen`, `persp`, `affine` | `screen` = 2D triangle raster (flat/gouraud); `persp` / `affine` = texture projection mode |
| `gate` | see Gate semantics below | Dispatch branch: fill alpha for solid surfaces vs texel transparency mode for textures |
| `kernel` | see below | Algorithm / inner loop family |
| `walk` | (omit) or `ordered` | Triangle is decomposed with fixed vertex order (branching "ordered" paths) |
| `layer` | (omit) or `face`, `scanline`, `span` | Pipeline layer; omit = default triangle raster |

**Gate semantics (texture opaque != fill opaque)**

- **`flat` / `gouraud`:** `gate` is **fill coverage**: `opaque` = alpha `0xFF` path; `alpha` = partial opacity / blending path (e.g. `raster_flat_alpha_bs4`, `raster_gouraud_alpha_bary_bs4`). Triangle-level, not atlas-level.

- **`tex_shadeflat` / `tex_shadeblend`:** `gate` is **texel mode**, matching the `texture_opaque` boolean in the C code:
  - `tex_opaque` -- all texels treated as solid; `raster_texture_opaque_*`, `raster_linear_opaque_*`, `draw_texture_scanline_opaque_*`
  - `tex_trans` -- texture may have transparent texels; skip write where `texel == 0`; `raster_texture_transparent_*`, `raster_linear_transparent_*`

Using `tex_opaque` / `tex_trans` avoids collision with `gouraud.screen.alpha` (vertex blend alpha).

**Code to name quick map:**

| User phrase | gate | Typical C prefix |
|-------------|------|------------------|
| Texture opaque (solid atlas) | `tex_opaque` | `raster_texture_opaque_*`, `raster_linear_opaque_*` |
| Texture transparent (cutout atlas) | `tex_trans` | `raster_texture_transparent_*`, `raster_linear_transparent_*` |
| Flat / Gouraud opaque fill | `opaque` | `*_bs4` / `*_bary_bs4` without `alpha` |
| Flat / Gouraud alpha fill | `alpha` | `raster_flat_alpha_*`, `raster_gouraud_alpha_bary_*` |

**Kernel token decode (important):**

`b` in `bs4` / `bs1` already encodes "branching". So `bs4` = "branching, step-4" and `bs1` = "branching, step-1". The correct kernel tokens spell this out explicitly, dropping the `b` shorthand:

```text
Old token    Decoded meaning                Correct token
-----------  -----------------------------  ---------------
branch_bs4   branch + (branch + s4)         branch_s4     (b was redundant)
bary_bs4     barycentric + (branch + s4)    bary_branch_s4
bary_bs1     barycentric + (branch + s1)    bary_branch_s1
bary_full_s4 full-tri barycentric + s4      bary_s4       (non-branching, no "full" needed)
```

When `branch` appears as a standalone prefix it means the triangle decomposition itself uses a branching edge walk (no fixed vertex ordering). When `ordered` appears as a suffix it means the ordered-walk variant of the same algorithm.

**Kernel vocabulary** (extend only when a new algorithm lands):

- Flat: `branch_s4` (branching edge walk, step-4), `sort_s4` (legacy y-sorted).
- Gouraud: `bary_branch_s4`, `bary_branch_s1`, `bary_s4` (non-branching full-triangle bary), `edge_s4`, `edge_s1`, `span_alpha` (alpha span fill -- explicit SIMD), `ref_deob` (reference only).
- Texture: `lerp8`, `blerp8`, `blerp8_v3`, `affine`, `affine_v3`. The `lerp8` / `blerp8_v3` scanlines and spans have explicit SIMD implementations (see `vec` token below).

**Vectorization token: `vec`**

The naming scheme adds an optional `vec` suffix to indicate whether a symbol has **explicit SIMD intrinsics** (as opposed to relying on compiler auto-vectorization). This applies primarily at the **span layer** (`draw_span_*`) and is also relevant when cataloguing scanlines:

```text
draw_span_<family>_<gate>_<algo>[_v3][_vec]
```

- Without `_vec`: plain C loop; compiler may auto-vectorize but no intrinsics are written.
- With `_vec`: explicit intrinsics exist for this symbol (NEON / AVX2 / SSE4.1 / SSE2 implementations live in `texture_simd.*.u.c` or `gouraud_simd_alpha.u.c`). The scalar fallback that shares the same name does **not** carry `_vec` in docs.

**Which symbols have explicit SIMD (`_vec`):**

```text
draw_span_gouraud_alpha_s4_vec          -- raster_linear_alpha_s4 (gouraud_simd_alpha.u.c)
draw_span_tex_shadeblend_tex_opaque_lerp8_vec    -- raster_linear_opaque_blend_lerp8
draw_span_tex_shadeblend_tex_trans_lerp8_vec     -- raster_linear_transparent_blend_lerp8
draw_span_tex_shadeblend_tex_opaque_lerp8_v3_vec -- raster_linear_opaque_blend_lerp8_v3
draw_span_tex_shadeblend_tex_trans_lerp8_v3_vec  -- raster_linear_transparent_blend_lerp8_v3
```

The scanline coordinators (`draw_scanline_*`) themselves contain no intrinsics -- they call the `_vec` spans -- so they do not carry the `_vec` suffix. Triangle and face wrappers are always scalar control flow; `vec` never applies there.

**SIMD ISA** remains metadata only (`isa=neon|avx|sse41|sse2|scalar`). `_vec` answers "does this symbol have any explicit SIMD implementation?"; `isa=` answers "which ISA is selected in this build?".

**Examples (canonical IDs):**

- F1 -> `flat.screen.opaque.branch_s4`
- F2 -> `flat.screen.alpha.branch_s4`
- F4 -> `legacy:flat.screen.opaque.sort_s4`
- G1 -> `gouraud.screen.opaque.bary_branch_s4`
- G2 -> `gouraud.screen.alpha.bary_branch_s4`
- G4 -> `gouraud.screen.opaque.bary_branch_s1`
- G7 (commented) -> `legacy:gouraud.screen.opaque.edge_s4`; alpha still live -> `gouraud.screen.alpha.edge_s4`
- G8 -> `gouraud.screen.opaque.bary_s4`
- G9 -> `gouraud.screen.alpha.span_alpha.vec` (span layer, explicitly vectorized)
- TF1 / TF2 -> `tex_shadeflat.persp.tex_opaque.lerp8` / `tex_shadeflat.persp.tex_trans.lerp8`
- TF5 -> `tex_shadeflat.affine.tex_opaque.affine_v3` / `tex_shadeflat.affine.tex_trans.affine_v3`
- TS1 -> `tex_shadeblend.persp.tex_opaque.blerp8_v3` / `tex_shadeblend.persp.tex_trans.blerp8_v3`
- TS8 (8-pixel span kernel) -> `tex_shadeblend.persp.tex_opaque.lerp8_v3.span.vec` (explicitly vectorized span)
- TS9 (scanline) -> `tex_shadeblend.persp.tex_opaque.blerp8_v3.scanline`
- TS10 (affine scanline) -> `tex_shadeblend.affine.tex_opaque.affine_v3.scanline`

**Shorter slug (logs, column headers):** `FLAT-bs4-opq`, `GOUR-bary4-a`, `TEXF-lerp-TOPQ`, `TEXF-lerp-TTRN`, `TEXG-bl8v3-TOPQ`, `TEXG-bl8v3-TTRN`, `TEXG-aff-v3-TOPQ`.

### Orthogonal axes (do not bake into the main ID unless needed)

- **SIMD ISA:** `isa=neon|avx|sse41|sse2|scalar` in metadata only.
- **Face API vs raw triangle:** `layer=face` or `*.face.nearclip` in architecture docs only; keeps triangle IDs stable.

### Optional rename direction (if you ever normalize C symbols)

Pattern: `raster_<family>_<space>_<gate>_<kernel>` with `face_` / `draw_scanline_` prefixes for layers, e.g. `raster_tex_shadeblend_persp_tex_opaque_blerp8_v3`. Large diff; the ID scheme above gives you the vocabulary without renaming.

### Deprecation / unused tags

Prefix IDs with `unused:` or `legacy:` in docs only, e.g. `legacy:flat.screen.opaque.sort_s4`, `unused:gouraud.screen.opaque.bary_branch_s4` (G11).

---

## Face, Scanline, and Span naming review

### Current inconsistencies

There are three distinct sub-layers below the triangle entry point. Each has its own naming problems.

**Layer A -- Face wrappers (`raster_face_*`)**

```text
Current name                                      Issue
------------------------------------------------  ----------------------------------------
raster_face_flat_near_clip                        OK as baseline
raster_face_flat                                  OK

raster_face_gouraud_near_clip                     OK
raster_face_gouraud_near_clipf                    "f" suffix = float z; completely opaque
raster_face_gouraud_near_clip_s1                  near_clip BEFORE variant suffix (s1)
raster_face_gouraud                               OK
raster_face_gouraud_s1                            OK

raster_face_texture_blend_near_clip               "blend" means "gouraud-shaded" -- cryptic
raster_face_texture_blend                         same
raster_face_texture_flat_near_clip                "flat" means "const-shaded" -- less cryptic
raster_face_texture_flat                          OK

raster_face_texture_blend_affine_v3_near_clip     near_clip AFTER v3 (inconsistent with s1 above)
raster_face_texture_blend_affine_near_clip        near_clip at end -- OK if this is the rule
raster_face_texture_blend_affine_v3               OK
raster_face_texture_blend_affine                  OK
raster_face_texture_flat_affine_v3_near_clip      near_clip at end -- consistent with affine blend
raster_face_texture_flat_affine_near_clip         OK
raster_face_texture_flat_affine_v3                OK
raster_face_texture_flat_affine                   OK
```

Problems:
- `near_clip` placement varies: before variant (`near_clip_s1`) vs after variant (`affine_v3_near_clip`).
- `f` suffix on `near_clipf` is opaque -- means float z coordinates, not documented anywhere in the name.
- `texture_blend` for gouraud-shaded texture is confusing; `blend` elsewhere means alpha blending.

**Layer B -- Scanline functions (inner triangle loop, one horizontal span)**

Three different prefixes exist for the same layer:

```text
Prefix                       Used in
---------------------------  ------------------------------------
draw_scanline_flat_*         flat.u.c, flat_branching_bs4.c
draw_scanline_gouraud_*      gouraud.u.c, gouraud_s1.u.c, gouraud_barycentric.u.c,
                             gouraud_branching*.c, gouraud_s1_branching*.c
raster_texture_scanline_*    texture.u.c  (legacy lerp8 path)
draw_texture_scanline_*      texture_blend_branching*.u.c, texture_simd.*.u.c
```

Within each prefix, `ordered` appears at different positions:

```text
draw_scanline_gouraud_ordered_bary_bs4           ordered BEFORE algorithm (bary_bs4)
draw_texture_scanline_opaque_blend_ordered_blerp8_v3  ordered AFTER gate (opaque_blend)
```

**Layer C -- 8-pixel span / SIMD kernel**

```text
Current name                             Issue
---------------------------------------  -----------------------------------------
raster_linear_alpha_s4                   "linear" is ambiguous; no family in name
raster_linear_opaque_blend_lerp8         "linear" still ambiguous; family implicit
raster_linear_transparent_blend_lerp8    same
raster_linear_opaque_blend_lerp8_v3      same
raster_linear_transparent_blend_lerp8_v3 same
```

---

### Proposed consistent naming (all three layers)

**Rule for every layer:** `<prefix>_<family>_<gate>_<algo>[_v3][_ordered][_nearclip][_zfloat]`

Suffix order (always right-to-left from the most specific modifier):
`algo` -> `v3` -> `ordered` -> (end for scanline/span) or `nearclip` -> `zfloat` (face only)

**Layer A -- Face wrappers: `raster_face_<family>[_<variant>][_nearclip][_zfloat]`**

```text
Current                                      Proposed
-------------------------------------------  -------------------------------------------
raster_face_flat                             raster_face_flat (no change)
raster_face_flat_near_clip                   raster_face_flat_nearclip

raster_face_gouraud                          raster_face_gouraud (no change)
raster_face_gouraud_near_clip                raster_face_gouraud_nearclip
raster_face_gouraud_near_clipf               raster_face_gouraud_nearclip_zfloat
raster_face_gouraud_s1                       raster_face_gouraud_s1 (no change)
raster_face_gouraud_near_clip_s1             raster_face_gouraud_s1_nearclip  (s1 before nearclip)

raster_face_texture_blend                    raster_face_tex_shadeblend_persp
raster_face_texture_blend_near_clip          raster_face_tex_shadeblend_persp_nearclip
raster_face_texture_flat                     raster_face_tex_shadeflat_persp
raster_face_texture_flat_near_clip           raster_face_tex_shadeflat_persp_nearclip

raster_face_texture_blend_affine             raster_face_tex_shadeblend_affine
raster_face_texture_blend_affine_near_clip   raster_face_tex_shadeblend_affine_nearclip
raster_face_texture_blend_affine_v3          raster_face_tex_shadeblend_affine_v3
raster_face_texture_blend_affine_v3_near_clip  raster_face_tex_shadeblend_affine_v3_nearclip
raster_face_texture_flat_affine              raster_face_tex_shadeflat_affine
raster_face_texture_flat_affine_near_clip    raster_face_tex_shadeflat_affine_nearclip
raster_face_texture_flat_affine_v3           raster_face_tex_shadeflat_affine_v3
raster_face_texture_flat_affine_v3_near_clip   raster_face_tex_shadeflat_affine_v3_nearclip
```

Key decisions:
- `near_clip` -> `nearclip` (no underscore mid-word; consistent everywhere).
- `s1` always before `nearclip` (variant is part of the algorithm identity; clip is a pipeline modifier).
- `f` -> `zfloat` (self-documenting).
- `texture_blend` -> `tex_shadeblend_persp`; `texture_flat` -> `tex_shadeflat_persp` (matches family+space from the ID grammar; removes "blend" ambiguity).

**Layer B -- Scanline functions: `draw_scanline_<family>_<gate>_<algo>[_v3][_ordered]`**

Unified prefix is `draw_scanline_` for all families. `ordered` always at the end.

```text
Current                                               Proposed
----------------------------------------------------  ----------------------------------------------------
draw_scanline_flat_s4                                 draw_scanline_flat_opaque_s4
draw_scanline_flat_alpha_s4                           draw_scanline_flat_alpha_s4 (no change)
draw_scanline_flat_ordered_bs4                        draw_scanline_flat_opaque_branch_s4_ordered
draw_scanline_flat_alpha_ordered_bs4                  draw_scanline_flat_alpha_branch_s4_ordered

draw_scanline_gouraud_s4                              draw_scanline_gouraud_opaque_s4
draw_scanline_gouraud_alpha_s4                        draw_scanline_gouraud_alpha_s4 (no change)
draw_scanline_gouraud_s1                              draw_scanline_gouraud_opaque_s1
draw_scanline_gouraud_alpha_s1                        draw_scanline_gouraud_alpha_s1 (no change)
draw_scanline_gouraud_s4_bary                         draw_scanline_gouraud_opaque_bary_s4
draw_scanline_gouraud_ordered_bary_bs4                draw_scanline_gouraud_opaque_bary_branch_s4_ordered
draw_scanline_gouraud_ordered_bary_alpha_bs4          draw_scanline_gouraud_alpha_bary_branch_s4_ordered
draw_scanline_gouraud_ordered_bary_bs1                draw_scanline_gouraud_opaque_bary_branch_s1_ordered
draw_scanline_gouraud_ordered_bs1 (unused)            draw_scanline_gouraud_opaque_branch_s1_ordered
draw_scanline_gouraud_ordered_bs4 (unused)            draw_scanline_gouraud_opaque_branch_s4_ordered

raster_texture_scanline_opaque_lerp8                  draw_scanline_tex_shadeflat_tex_opaque_lerp8
raster_texture_scanline_transparent_lerp8             draw_scanline_tex_shadeflat_tex_trans_lerp8
raster_texture_scanline_opaque_blend_lerp8            draw_scanline_tex_shadeblend_tex_opaque_lerp8
raster_texture_scanline_transparent_blend_lerp8       draw_scanline_tex_shadeblend_tex_trans_lerp8

draw_texture_scanline_opaque_blend_ordered_blerp8     draw_scanline_tex_shadeblend_tex_opaque_blerp8_ordered
draw_texture_scanline_transparent_blend_ordered_blerp8 draw_scanline_tex_shadeblend_tex_trans_blerp8_ordered
draw_texture_scanline_opaque_blend_ordered_blerp8_v3  draw_scanline_tex_shadeblend_tex_opaque_blerp8_v3_ordered
draw_texture_scanline_transparent_blend_ordered_blerp8_v3 draw_scanline_tex_shadeblend_tex_trans_blerp8_v3_ordered
draw_texture_scanline_opaque_blend_affine_ordered     draw_scanline_tex_shadeblend_tex_opaque_affine_ordered
draw_texture_scanline_transparent_blend_affine_ordered draw_scanline_tex_shadeblend_tex_trans_affine_ordered
draw_texture_scanline_opaque_blend_affine_ordered_ish16  draw_scanline_tex_shadeblend_tex_opaque_affine_v3_ordered
draw_texture_scanline_transparent_blend_affine_ordered_ish16 draw_scanline_tex_shadeblend_tex_trans_affine_v3_ordered
```

Notes:
- `blend` removed from all texture scanline names (it is not alpha blending -- it means gouraud-shaded; replaced by `tex_shadeblend` family token).
- `ish16` suffix dropped; replaced by `_v3` which is the consistent generation marker already used at triangle level.
- `ordered` always at end.
- `raster_texture_scanline_*` prefix unified to `draw_scanline_tex_*`.
- `bs4` / `bs1` decoded: `b` = branching, so `bs4` = `branch_s4`. Wherever the old name had `bs4` or `bs1` those tokens are spelled out as `branch_s4` / `branch_s1` in the proposed names.

**Layer C -- 8-pixel span / SIMD kernel: `draw_span_<family>_<gate>_<algo>[_v3][_vec]`**

`_vec` marks symbols that have explicit SIMD intrinsic implementations (in addition to the scalar fallback that shares the same name).

```text
Current                                   Proposed
----------------------------------------  --------------------------------------------------
raster_linear_alpha_s4                    draw_span_gouraud_alpha_s4_vec
raster_linear_opaque_blend_lerp8          draw_span_tex_shadeblend_tex_opaque_lerp8_vec
raster_linear_transparent_blend_lerp8     draw_span_tex_shadeblend_tex_trans_lerp8_vec
raster_linear_opaque_blend_lerp8_v3       draw_span_tex_shadeblend_tex_opaque_lerp8_v3_vec
raster_linear_transparent_blend_lerp8_v3  draw_span_tex_shadeblend_tex_trans_lerp8_v3_vec
```

Notes:
- `raster_linear_` -> `draw_span_` makes the pipeline level explicit and removes the ambiguous "linear".
- `blend` removed from texture span names for the same reason as scanline (not alpha blending).
- Gouraud alpha span gains its family in the name.
- These are still implementation-internal helpers; `draw_span_` parallels `draw_scanline_` one level down.

**Summary of the three-level prefix scheme:**

```text
Layer         Prefix             What it does
------------  -----------------  ---------------------------------------------------
Face          raster_face_*      Fetches vertex indices, checks pre-clipped flags, dispatches to triangle or near-clip
Triangle      raster_*           Raw screen-coord triangle rasterizer (no vertex fetch)
Scanline      draw_scanline_*    One horizontal scan across the triangle (called per-row)
Span          draw_span_*        Processes 4 or 8 pixels of one scanline (innermost loop)
```

---

## Proposed file organization

### Current state

`src/graphics/` is a single flat directory with 67 files mixing active rasterizers, legacy/unused implementations, reference code, projection, animation, 2D, model, and utility headers.

The `.u.c` include-chain pattern means file locality matters: each `.u.c` file is `#include`d by exactly one parent, forming an implicit tree. Moving files to subdirectories requires updating all `#include` paths inside those files.

### Proposed directory tree

File names encode the full variant ID grammar (`<family>.<space>.<gate>.<kernel>[.<layer>]`). Each file contains exactly one variant. Exceptions:
- **Face files** (`*.face.u.c`) are dispatchers *above* the variant level — splitting them per variant would be circular.
- **`gouraud.screen.bary.u.c`** is a shared algorithm helper included by the edge TUs, not a standalone variant.
- **`tex.span.*.u.c`** files are ISA implementation modules; the ISA is the discriminator, not the variant — one file per ISA covers all span functions for that ISA.
- `tex_shadeflat.affine.tex_opaque.affine.u.c` has `affine` twice (space and kernel tokens collide); this is accepted — the plan's own ID grammar already uses this form.

```text
src/graphics/
  dash.c                     [top-level TU -- entry point for the whole pipeline]
  dash.h
  dash_*.h                   [type headers: dash_alphaint, dash_faceint, dash_hsl16, etc.]
  dash_model.c
  dash_model_internal.h
  dash_minimap.c  dash_minimap.h
  dashmap.c  dashmap.h
  anim.u.c

  support/                   [shared building blocks, no raster logic]
    alpha.h
    clamp.h
    shade.h
    sse2_41compat.h
    uv_pnm.h
    shared_tables.c  shared_tables.h
    lighting.c  lighting.h
    triangle_contains.u.c
    convex_hull.u.c
    render_clip.u.c
    render_face_alpha.u.c

  projection/
    projection.h
    projection.u.c
    projection_simd.u.c
    projection16_simd.u.c
    projection_sparse.u.c

  2d/
    dash2d_simd.u.c
    dash2d_simd.avx.u.c  dash2d_simd.neon.u.c
    dash2d_simd.scalar.u.c  dash2d_simd.sse2.u.c  dash2d_simd.sse41.u.c

  raster/
    flat/
      flat.screen.opaque.sort_s4.u.c         [from flat.u.c -- legacy opaque edge-walk s4]
      flat.screen.alpha.sort_s4.u.c          [from flat.u.c -- legacy alpha edge-walk s4]
      flat.screen.opaque.branch_s4.c         [from flat_branching_bs4.c -- active opaque]
      flat.screen.alpha.branch_s4.c          [from flat_branching_bs4.c -- active alpha]
      flat.face.u.c                          [from render_flat.u.c -- face dispatch; above variant level]

    gouraud/
      span/
        gouraud.screen.alpha.span.u.c        [from gouraud_simd_alpha.u.c -- draw_span_gouraud_alpha_s4_vec: NEON/AVX/SSE/scalar]
      gouraud.screen.bary.u.c                [from gouraud_barycentric.u.c -- shared bary helper; not a standalone variant]
      gouraud.screen.opaque.edge_s4.u.c      [from gouraud.u.c -- opaque edge-walk s4]
      gouraud.screen.alpha.edge_s4.u.c       [from gouraud.u.c -- alpha edge-walk s4]
      gouraud.screen.opaque.edge_s1.u.c      [from gouraud_s1.u.c -- opaque edge-walk s1]
      gouraud.screen.alpha.edge_s1.u.c       [from gouraud_s1.u.c -- alpha edge-walk s1]
      gouraud.screen.opaque.bary_branch_s4.c [from gouraud_branching_barycentric.c -- active opaque]
      gouraud.screen.alpha.bary_branch_s4.c  [from gouraud_branching_barycentric.c -- active alpha]
      gouraud.screen.opaque.bary_branch_s1.c [from gouraud_s1_branching_barycentric.c -- opaque only; no alpha variant exists]
      gouraud.face.u.c                       [from render_gouraud.u.c -- face dispatch; above variant level]

    texture/
      span/
        tex.span.u.c                         [ISA selector -- includes tex.span.{isa}.u.c]
        tex.span.scalar.u.c
        tex.span.sse2.u.c  tex.span.sse41.u.c
        tex.span.avx.u.c   tex.span.neon.u.c

      -- perspective lerp8 (legacy; from texture.u.c) --
      tex_shadeflat.persp.tex_opaque.lerp8.u.c     [TF1 -- active shadeflat opaque persp triangle]
      tex_shadeflat.persp.tex_trans.lerp8.u.c      [TF2 -- active shadeflat trans persp triangle]
      tex_shadeblend.persp.tex_opaque.lerp8.u.c    [TS11 opaque -- legacy shadeblend; superseded by blerp8_v3]
      tex_shadeblend.persp.tex_trans.lerp8.u.c     [TS11 trans -- legacy shadeblend; superseded by blerp8_v3]

      -- perspective blerp8 (from texture_blend_branching.u.c; superseded by v3) --
      tex_shadeblend.persp.tex_opaque.blerp8.u.c   [TS2 opaque -- superseded by blerp8_v3]
      tex_shadeblend.persp.tex_trans.blerp8.u.c    [TS2 trans -- superseded by blerp8_v3]

      -- perspective blerp8_v3 (from texture_blend_branching_v3.u.c; active) --
      tex_shadeblend.persp.tex_opaque.blerp8_v3.u.c  [TS1 opaque -- active persp shadeblend path]
      tex_shadeblend.persp.tex_trans.blerp8_v3.u.c   [TS1 trans -- active persp shadeblend path]

      -- affine (from texture_blend_branching_affine.u.c) --
      tex_shadeflat.affine.tex_opaque.affine.u.c   [TF5 opaque -- affine shadeflat]
      tex_shadeflat.affine.tex_trans.affine.u.c    [TF5 trans -- affine shadeflat]
      tex_shadeblend.affine.tex_opaque.affine.u.c  [TS6 opaque -- affine shadeblend]
      tex_shadeblend.affine.tex_trans.affine.u.c   [TS6 trans -- affine shadeblend]

      -- affine_v3 (from texture_blend_branching_affine_v3.u.c; active) --
      tex_shadeflat.affine.tex_opaque.affine_v3.u.c   [TF5 opaque v3 -- active affine shadeflat]
      tex_shadeflat.affine.tex_trans.affine_v3.u.c    [TF5 trans v3 -- active affine shadeflat]
      tex_shadeblend.affine.tex_opaque.affine_v3.u.c  [TS6 opaque v3 -- active affine shadeblend]
      tex_shadeblend.affine.tex_trans.affine_v3.u.c   [TS6 trans v3 -- active affine shadeblend]

      tex.persp.face.u.c               [from render_tex.u.c -- persp face dispatch; above variant level]
      tex.affine.face.u.c              [from render_tex_affine.u.c -- affine face dispatch; above variant level]

  archive/                     [never #included from active pipeline; kept for reference diff]
    gouraud.screen.branch_s4.c         [was gouraud_branching.c]
    gouraud.screen.branch_s1.c         [was gouraud_s1_branching.c]
    tex_shadeblend.affine.tex_opaque.c [was texture_opaque_blend_affine.c]
    tex_shadeblend.affine.tex_trans.c  [was texture_transparent_blend_affine.c]

  reference/                   [decompiled / original research code; never compiled into client]
    gouraud_deob.c
    gouraud_deob.h
```

### Outlier handling

**`gouraud_deob.c` / `gouraud_deob.h`**

This is a decompiled reconstruction of the original game's Gouraud rasterizer. The `.h` header has both declarations commented out; the include in `gouraud.screen.edge_s4.u.c` and `gouraud.screen.edge_s1.u.c` is also commented out. It is never compiled into the client.

- Move to `reference/`. This makes the research/archaeology intent obvious.
- The `docs/gouraud_raster.c` standalone demo belongs there too (already in `docs/`).
- The `gouraud_deob.h` can stay next to `gouraud_deob.c` inside `reference/`; no header guard change needed.

**`gouraud_branching.c`, `gouraud_s1_branching.c`** (non-barycentric branching)

These implement `raster_gouraud_bs4` / `raster_gouraud_ordered_bs4` and the s1 equivalents. They are **never `#include`d** anywhere in the active tree. The barycentric branching files (`gouraud_branching_barycentric.c`, `gouraud_s1_branching_barycentric.c`) superseded them.

- Move to `archive/` with a rename to `gouraud.screen.branch_s4.c` and `gouraud.screen.branch_s1.c`.

**`texture_opaque_blend_affine.c`, `texture_transparent_blend_affine.c`**

These define the same symbols as the code inside `tex_shadeblend.affine.tex_opaque.affine.u.c` and `tex_shadeblend.affine.tex_trans.affine.u.c` but as standalone `.c` files. They are not `#include`d from any active TU. They appear to be an earlier stage of development before the symbols were absorbed into the branching file.

- Move to `archive/` with a rename to `tex_shadeblend.affine.tex_opaque.c` and `tex_shadeblend.affine.tex_trans.c`.

### Include-path impact

Moving files to subdirectories touches `#include "..."` paths in the `.u.c` chain. The changes are mechanical (all relative paths) and localized:

```text
gouraud.face.u.c     : "gouraud.u.c" -> "gouraud.screen.opaque.edge_s4.u.c" + "gouraud.screen.alpha.edge_s4.u.c", etc.
flat.face.u.c        : "flat.u.c" -> "flat.screen.opaque.sort_s4.u.c" + "flat.screen.alpha.sort_s4.u.c", etc.
tex.persp.face.u.c   : "texture.u.c" -> "tex_shadeflat.persp.tex_opaque.lerp8.u.c" + ...,
                       "texture_blend_branching_v3.u.c" -> "tex_shadeblend.persp.tex_opaque.blerp8_v3.u.c" + ..., etc.
render_clip.u.c      : stays or moves to support/; update its includers
dash.c               : "render_gouraud.u.c" -> "raster/gouraud/gouraud.face.u.c",
                       "render_tex.u.c" -> "raster/texture/tex.persp.face.u.c", etc.
```

Because `dash.c` is the root of the entire `.u.c` include tree, and all paths are relative, each move only affects the one file that contains the `#include "..."`. No build system changes are needed if the files are still under the same source root.

### Migration order (if executing)

1. Move `archive/` files first (no include changes, just `git mv`).
2. Move `reference/` files (update the two commented-out includes in `gouraud.screen.edge_s4.u.c` / `gouraud.screen.edge_s1.u.c`).
3. Move `support/` and `projection/` (update includes in `render_clip.u.c`, `dash.c`, `projection*.u.c`, `tex*.u.c`).
4. Move `2d/` files.
5. Move rasterizer subdirs (`flat/`, `gouraud/`, `texture/`) last, updating face TUs and `dash.c`.
