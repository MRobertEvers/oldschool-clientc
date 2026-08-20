# ToriDraw rasterizer optimization survey

Analysis date: 2026-07-28. Scope: `3rd/toridraw/` — the triangle rasterizers
(`graphics/raster/**`, `triangles/**`), the face-order/raster driver
(`toridraw_render.u.c`, `toridraw_raster.u.c`), and the 2D/sprite/font blitters
(`toridraw_2d.c`, `toridraw_sprite.c`, `toridraw_font.c`).

> **Status: implemented 2026-07-28.** Items 1–7 and 10–12 plus both Appendix A
> defects are in the tree and verified byte-identical. Items 8 and 9 were
> implemented, measured, and **reverted** — see
> [Measured results](#measured-results) for why. The code blocks below are the
> original proposals and are kept as the rationale for each change; where the
> shipped code diverges from the proposal it is called out inline.

---

## Measured results

Verified with two deterministic references, both compared byte-for-byte against
a baseline binary built from `a8caf7fe`:

- `make OPT=1 scanline-compare` + `TORIRS_SCANLINE_HEADLESS=16` on model 148
  (1004 faces, 48 textured, 420 alpha) across 16 yaw steps × 4 alpha modes.
- `torirs --offline --manifest ../manifests/manifest_rs254.ini` with
  `TORIRS_WORLD_MAP=50,50 TORIRS_WORLD_BMP=1` — a full client frame, world plus
  the whole interface tree, which is what covers items 4–7.

**Every shipped item is pixel-identical on both.** `make test-scanline` and
`make test-rotate-blit` pass, and a debug build (asserts enabled) produces the
same frame.

World draw (project + sort + raster), interleaved A/B over 8 pairs to cancel
machine drift:

| | baseline | optimized |
|---|---|---|
| mean | 1.2328 ms | 1.1468 ms |
| median | 1.2056 ms | 1.1470 ms |
| min | 1.1975 ms | 1.1261 ms |

**~5–7% faster, 8/8 pairwise wins.** The model-only raster loop
(`scanline_compare` model mode) moved 2–4%; it is a small model dominated by
per-triangle setup, so it shows less.

The machine was under concurrent load from other work during these runs, which
is why the numbers are interleaved rather than batched — single-batch runs
drifted by more than the effect size.

---

## Ranked summary

| # | Where | Finding | Outcome |
|---|-------|---------|---------|
| 1 | `gouraud.screen.opaque.bary.branching.s4.c` | Gouraud scanline has no `noclip` fast path; flat already has one | **shipped**, identical |
| 2 | `tex.span.neon.u.c`, `tex.span.scalar.u.c` | Every 8-px block computes the *next* block's `1/w`, `u`, `v` — then the next iteration computes them again | **shipped**, identical |
| 3 | `toridraw_render.u.c` | `parition_faces_by_priority` and `sort_face_draw_order` walk the identical depth-bucket set twice, unpacking the same nibble | **shipped**, identical |
| 4 | `toridraw_2d.c` | `ToriDraw2D_FillRect` calls `ToriDraw2D_BlendArgbPixel` per pixel, which re-clips and recomputes `y*stride+x` | **shipped**, identical |
| 5 | `toridraw_2d.c` | Scaled/tiled/masked blits do 1–4 integer divides *or* two `%` per pixel | **shipped**, identical |
| 6 | `toridraw_sprite.c`, `toridraw_2d.c` | Alpha blend uses three `/255` | **shipped**, identical (exact form, not the ±1 LSB one — see below) |
| 7 | `toridraw_sprite.c`, `toridraw_font.c` | Per-pixel clip rejection instead of clamping the row/column range once | **shipped**, identical |
| 8 | edge setup (flat/gouraud/texture) | 3 `idiv` per triangle for edge slopes; `g_reciprocal16` exists and is already used by the clipper | **reverted** — measurably slower *and* changes pixels |
| 9 | `gouraud_barycentric_steps.h` | Two divides by the *same* `sarea` per triangle | **reverted** — an exact shared reciprocal costs more than it saves |
| 10 | `toridraw_raster.u.c` | `getenv()` in the per-face texture-miss path | **shipped**, identical |
| 11 | `toridraw_raster.u.c` | Texture map lookup repeated per face; faces are already grouped by texture | **shipped**, identical |
| 12 | `toridraw_render.u.c` | Pick test does 2 float divides per face | **shipped**, identical |

Both correctness defects in
[Appendix A](#appendix-a--non-performance-observations) are also fixed.

### Where the shipped code differs from the proposal

- **#5, #6** — the proposal used a 16.16 DDA for the scaled blits and the
  `>> 8` packed blend for alpha. Both are *approximations*: the truncated DDA
  step drifts one unit low wherever `dst_w` divides `x * src_w`, and the packed
  blend divides by 256 rather than 255. Since this client is under active
  pixel-parity work against reference clients, both shipped as **exact**
  variants instead — a remainder-carrying accumulator for the DDA, and
  `(v + (v>>8) + 1) >> 8` for the division, which is bit-identical to `v / 255`
  across the whole `0 .. 255*255` range a channel blend can produce (verified
  exhaustively). The divides still go away; the pixels do not move.
- **#2** — shipped for all five span implementations: NEON, scalar, SSE2,
  SSE4.1 and AVX2. See [x86 span variants](#x86-span-variants) for how the
  non-native ones were verified.
- **#8, #9** — see below.

---

## 1. Gouraud opaque scanline is missing the `noclip` split

**File:** `3rd/toridraw/graphics/raster/gouraud/gouraud.screen.opaque.bary.branching.s4.c`

The flat rasterizer already proves the pattern: before the scanline loop it
calls `flat_screen_fixed_edges_no_hclip()` once per trapezoid, and if both edges
stay inside `[0, screen_width)` for the whole segment it dispatches to a
scanline that skips the clamps entirely
(`flat.screen.opaque.branching.s4.c:200-230`).

The gouraud path never got that treatment. Every scanline of every gouraud face
— which is the overwhelming majority of world geometry — pays:

```c
if( x_start_ish16 == x_end_ish16 ) return;
int x_end = x_end_ish16 >> 16;
if( x_end >= screen_width ) x_end = screen_width - 1;   /* redundant when noclip */
int x_start = x_start_ish16 >> 16;
if( x_start < 0 ) x_start = 0;                          /* redundant when noclip */
if( x_start >= x_end ) return;
```

The clamps are cheap individually but they sit on the critical path *before*
`offset += x_start` and the colour prestep, so they serialize the address
computation for a 4-pixel run. Hoisting them is free — the predicate is already
written and unit-shaped for reuse.

### Edited copy

Add a `_noclip` twin and hoist the predicate. The header
`graphics/raster/flat/flat_screen_edges.h` is generic despite the name, so it
can be included as-is.

```c
/* --- EDITED COPY of gouraud.screen.opaque.bary.branching.s4.c --- */

#include "graphics/tori_compat.h"
#include "graphics/dash_restrict.h"
#include "graphics/raster/gouraud/gouraud_barycentric_steps.h"
#include "graphics/raster/flat/flat_screen_edges.h"   /* + reuse the flat predicate */
#include "graphics/shared_tables.h"

/* NEW: no left/right clamp. Caller has proven both edges stay on screen for
 * the whole trapezoid, so x_start >= 0 and x_end < screen_width by
 * construction. */
static inline void
draw_scanline_gouraud_screen_opaque_bary_branching_s4_ordered_noclip(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int x_start_ish16,
    int x_end_ish16,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8)
{
    if( x_start_ish16 == x_end_ish16 )
        return;

    int x_start = x_start_ish16 >> 16;
    int x_end = x_end_ish16 >> 16;
    if( x_start >= x_end )
        return;

    offset += x_start;
    color_hsl16_ish8 += x_start * color_step_hsl16_ish8;

    int span = x_end - x_start;
    int steps = span >> 2;
    color_step_hsl16_ish8 <<= 2;

    while( steps-- > 0 )
    {
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8];
        pixel_buffer[offset + 0] = rgb_color;
        pixel_buffer[offset + 1] = rgb_color;
        pixel_buffer[offset + 2] = rgb_color;
        pixel_buffer[offset + 3] = rgb_color;
        offset += 4;
        color_hsl16_ish8 += color_step_hsl16_ish8;
    }

    int rgb_color = g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8];
    switch( span & 0x3 )
    {
    case 3: pixel_buffer[offset++] = rgb_color; /* fallthrough */
    case 2: pixel_buffer[offset++] = rgb_color; /* fallthrough */
    case 1: pixel_buffer[offset]   = rgb_color;
    }
}

/* (draw_scanline_..._ordered — the clamping version — is unchanged.) */
```

and in `raster_gouraud_screen_opaque_bary_branching_s4_ordered`, replace the
first `if(...)` arm's two loops with the hoisted form (the second arm is the
mirror image):

```c
    /* --- EDITED COPY: hoisted noclip predicate, first arm --- */
    if( (y0 == y1 && step_edge_x_AC_ish16 <= step_edge_x_BC_ish16) ||
        (y0 != y1 && step_edge_x_AC_ish16 >= step_edge_x_AB_ish16) )
    {
        int seg1_count = y1 - y0;
        int seg2_count = y2 - y1;
        if( seg1_count < 0 ) seg1_count = 0;
        if( seg2_count < 0 ) seg2_count = 0;

        /* + hoisted once per trapezoid instead of per scanline */
        int noclip_s1 = flat_screen_fixed_edges_no_hclip(
            edge_x_AB_ish16, step_edge_x_AB_ish16,
            edge_x_AC_ish16, step_edge_x_AC_ish16,
            seg1_count, screen_width);
        int noclip_s2 = flat_screen_fixed_edges_no_hclip(
            edge_x_BC_ish16, step_edge_x_BC_ish16,
            edge_x_AC_ish16 + seg1_count * step_edge_x_AC_ish16, step_edge_x_AC_ish16,
            seg2_count, screen_width);

        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            if( noclip_s1 )
                draw_scanline_gouraud_screen_opaque_bary_branching_s4_ordered_noclip(
                    pixel_buffer, offset,
                    edge_x_AB_ish16, edge_x_AC_ish16,
                    hsl_ish8, step_x_hsl_ish8);
            else
                draw_scanline_gouraud_screen_opaque_bary_branching_s4_ordered(
                    pixel_buffer, offset, screen_width, 0,
                    edge_x_AB_ish16, edge_x_AC_ish16,
                    hsl_ish8, step_x_hsl_ish8);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;
            hsl_ish8 += step_y_hsl_ish8;
            offset += stride;
        }

        while( y2-- > 0 )
        {
            if( noclip_s2 )
                draw_scanline_gouraud_screen_opaque_bary_branching_s4_ordered_noclip(
                    pixel_buffer, offset,
                    edge_x_BC_ish16, edge_x_AC_ish16,
                    hsl_ish8, step_x_hsl_ish8);
            else
                draw_scanline_gouraud_screen_opaque_bary_branching_s4_ordered(
                    pixel_buffer, offset, screen_width, 0,
                    edge_x_BC_ish16, edge_x_AC_ish16,
                    hsl_ish8, step_x_hsl_ish8);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;
            hsl_ish8 += step_y_hsl_ish8;
            offset += stride;
        }
    }
```

The same edit applies verbatim to
`gouraud.screen.alpha.bary.branching.s4.c` (the alpha twin), which has the
identical scanline prologue at lines 24-45.

> **Caveat on the clamp semantics.** `flat_screen_fixed_edges_no_hclip` proves
> `x_end_ish16 >> 16 < screen_width`, whereas the clamping scanline forces
> `x_end = screen_width - 1`. Those differ for a span that ends *exactly* at
> `screen_width`: the clamped version drops one more pixel. Since the clamped
> version already drops the pixel at `x_end` (the loop covers
> `[x_start, x_end)`), the noclip path draws the same set. Worth a
> pixel-diff regression run anyway.

---

## 2. Perspective texture span recomputes each block's `1/w` twice

**File:** `3rd/toridraw/graphics/raster/texture/span/tex.span.neon.u.c:640-695`
(the AVX/SSE4.1/SSE2/scalar twins have the same structure)

`CALC_BLOCK_PARAMS` computes, for block *k*:

- `inv_w`, `cur_u`, `cur_v` — at pixel `8k`
- `inv_w_n`, `nxt_u`, `nxt_v` — at pixel `8(k+1)`

then the loop advances `au/bv/cw` by `step*8` and iteration *k+1* computes
`inv_w`/`cur_u`/`cur_v` at pixel `8(k+1)` — **the values it just computed as
`nxt_*`**. Every interior block pays for two reciprocals and four
float→int conversions where one and two would do.

For a full-screen textured floor this is the single hottest scalar block in the
frame: two `fdiv` (≈10 cycle latency each on M-series, not pipelined
back-to-back here because `inv_w_n` depends on the same `texture_shift`
shift chain) per 8 pixels.

### Edited copy

Carry `nxt_*` forward into `cur_*`. The `if( (cw >> texture_shift) != 0 )`
guard complicates it slightly: when a block is skipped the carried state is
stale, so track validity.

```c
/* --- EDITED COPY of draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered
 *     (tex.span.neon.u.c). Only the block loop changed. --- */
static inline void
draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered(
    int* RESTRICT pixel_buffer,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* RESTRICT texels,
    int texture_width)
{
    int x0 = (screen_x0_ish16 - 1) >> 16;
    if( x0 < 0 )
        x0 = 0;
    int x1 = screen_x1_ish16 >> 16;
    if( x1 >= screen_width )
        x1 = screen_width - 1;
    if( x0 >= x1 )
        return;

    int adjust = x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    int texture_shift = (texture_width == 128) ? 7 : 6;
    int v_mask = (texture_width == 128) ? 0x3F80 : 0x0FC0;
    int u_mask = texture_width - 1;

    int steps = x1 - x0;
    int offset = pixel_offset + x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * x0;

    int blocks = steps >> 3;
    int remaining = steps & 7;

    int step_au8 = step_au_dx << 3;
    int step_bv8 = step_bv_dx << 3;
    int step_cw8 = step_cw_dx << 3;

    /* + carried state: u/v at the START of the current block, valid only if
     *   have_cur is set (the previous block computed them as its nxt_*). */
    int cur_u = 0, cur_v = 0;
    int have_cur = 0;

    while( blocks-- )
    {
        int w = cw >> texture_shift;
        if( w != 0 )
        {
            /* - float inv_w = 1.0f / (float)(cw >> texture_shift);
             * - int cur_u = (int)(au * inv_w);
             * - int cur_v = (int)(bv * inv_w);
             * + only when the previous block did not already hand us cur_u/cur_v */
            if( !have_cur )
            {
                float inv_w = 1.0f / (float)w;
                cur_u = (int)(au * inv_w);
                cur_v = (int)(bv * inv_w);
            }

            int w_n = (cw + step_cw8) >> texture_shift;
            int nxt_u, nxt_v;
            if( w_n != 0 )
            {
                float inv_w_n = 1.0f / (float)w_n;
                nxt_u = (int)((au + step_au8) * inv_w_n);
                nxt_v = (int)((bv + step_bv8) * inv_w_n);
            }
            else
            {
                /* degenerate next block: hold the gradient flat rather than
                 * extrapolating from a zero denominator */
                nxt_u = cur_u;
                nxt_v = cur_v;
            }

            int s_u = (nxt_u - cur_u) << (texture_shift - 3);
            int s_v = (nxt_v - cur_v) << (texture_shift - 3);

            raster_linear_opaque_blend_lerp8_v3(
                (uint32_t*)&pixel_buffer[offset],
                (uint32_t*)texels,
                cur_u << texture_shift,
                cur_v << texture_shift,
                s_u,
                s_v,
                texture_shift,
                u_mask,
                v_mask,
                shade8bit_ish8 >> 8);

            /* + hand the next block its start uv; it will not recompute them */
            cur_u = nxt_u;
            cur_v = nxt_v;
            have_cur = (w_n != 0);
        }
        else
        {
            have_cur = 0;   /* skipped block: carried uv is stale */
        }

        au += step_au8;
        bv += step_bv8;
        cw += step_cw8;
        offset += 8;
        shade8bit_ish8 += (step_shade8bit_dx_ish8 << 3);
    }

    if( remaining > 0 && (cw >> texture_shift) != 0 )
    {
        int w = cw >> texture_shift;
        if( !have_cur )
        {
            float inv_w = 1.0f / (float)w;
            cur_u = (int)(au * inv_w);
            cur_v = (int)(bv * inv_w);
        }
        int w_n = (cw + step_cw8) >> texture_shift;
        int nxt_u = cur_u, nxt_v = cur_v;
        if( w_n != 0 )
        {
            float inv_w_n = 1.0f / (float)w_n;
            nxt_u = (int)((au + step_au8) * inv_w_n);
            nxt_v = (int)((bv + step_bv8) * inv_w_n);
        }
        int s_u = (nxt_u - cur_u) << (texture_shift - 3);
        int s_v = (nxt_v - cur_v) << (texture_shift - 3);

        int u_scan = cur_u << texture_shift;
        int v_scan = cur_v << texture_shift;
        int shade = shade8bit_ish8 >> 8;

        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            pixel_buffer[offset++] = shade_blend(texels[u + v], shade);
            u_scan += s_u;
            v_scan += s_v;
        }
    }
}
```

Two behavior deltas to check before adopting:

- Today, a block whose `w_n == 0` (i.e. `(cw + step*8) >> shift == 0`) is
  drawn with a garbage `s_u`/`s_v` derived from a division by zero — in the
  current code the guard only tests `cw`, not `cw + step_cw8`. The edit
  above makes that case explicit (flat gradient). That is arguably a *fix*, but
  it changes pixels near the horizon, so it should be diffed.
- The transparent twin (`..._transparent_blend_branching_lerp8_v3_ordered`,
  line 699) additionally clamps `cur_u`/`nxt_u` to `[0, texture_width-1]` and
  recomputes `s_u` after the clamp. When carrying forward, carry the
  **clamped** `nxt_u` so the next block's `cur_u` matches what the current
  block used as its endpoint.

---

## 3. The priority face sort walks the depth buckets twice

**File:** `3rd/toridraw/toridraw_render.u.c:279-433` (and the `_small` twins at
609-764)

`ToriDraw_ComputeProjectedFaceOrder` calls, back to back:

1. `parition_faces_by_priority()` — for each depth from high to low, for each
   face in that bucket: `faceprio_unpack()`, then append to
   `face_priority_buckets[prio]`.
2. `sort_face_draw_order()` — for each depth from high to low, for each face in
   that bucket: `faceprio_unpack()` **again**, accumulate `counts[prio]`,
   `priority_depths[prio]`, and fill the two flex arrays.

Same iteration order, same faces, same nibble unpack. The second pass also
recomputes `counts[]`, which pass 1 has already produced as
`face_priority_bucket_counts[]`.

This runs once per model per frame, over every front-facing face. For a scene
with a few hundred models it is a meaningful fraction of the sort cost, and the
bucket arrays are large enough (`1500 << 9` faceints ≈ 1.5 MB) that a second
traversal is not free in cache terms either.

### Edited copy

Fuse into one pass. `sort_face_draw_order` keeps its emission logic but takes
the counts/sums as inputs.

```c
/* --- EDITED COPY of toridraw_render.u.c: parition_faces_by_priority +
 *     the accumulation half of sort_face_draw_order, fused. --- */

/* Fills the priority buckets AND the per-priority counts, depth sums and flex
 * arrays in a single traversal. Replaces parition_faces_by_priority() and the
 * first loop of sort_face_draw_order(). */
static inline void
partition_and_accumulate_faces_by_priority(
    faceint_t* face_priority_buckets,
    faceint_t* face_priority_bucket_counts,
    faceint_t* priority_depths,
    int* flex_prio11_face_to_depth,
    int* flex_prio12_face_to_depth,
    int* counts,                       /* out: int[12] */
    faceint_t* face_depth_buckets,
    faceint_t* face_depth_bucket_counts,
    const uint8_t* face_priorities,
    int depth_lower_bound,
    int depth_upper_bound)
{
    if( depth_upper_bound >= 1500 )
        depth_upper_bound = 1499;

    for( int depth = depth_upper_bound; depth >= depth_lower_bound; depth-- )
    {
        int face_count = (int)face_depth_bucket_counts[depth];
        if( face_count == 0 )
            continue;

        faceint_t* faces = &face_depth_buckets[depth << 9];
        for( int i = 0; i < face_count; i++ )
        {
            faceint_t face_idx = faces[i];
            int prio = faceprio_unpack(face_priorities, face_idx);   /* once, not twice */
            int n = counts[prio];

            /* --- from parition_faces_by_priority --- */
            face_priority_buckets[prio * 2000 + n] = face_idx;

            /* --- from sort_face_draw_order's accumulation loop --- */
            if( prio < 10 )
                priority_depths[prio] += (faceint_t)depth;
            else if( prio == 10 )
                flex_prio11_face_to_depth[n] = depth | (face_idx << 16);
            else /* prio == 11 */
                flex_prio12_face_to_depth[n] = depth | (face_idx << 16);

            counts[prio] = n + 1;
            face_priority_bucket_counts[prio] = (faceint_t)(n + 1);
        }
    }
}

/* sort_face_draw_order() keeps everything from `int average_depth1_2 = 0;`
 * (line 359) onward verbatim, but drops its own accumulation loop and takes
 * `counts` and `priority_depths` as parameters instead of computing them. */
static inline int
emit_face_draw_order(
    const faceint_t* priority_depths,
    int* flex_prio11_face_to_depth,
    const int* flex_prio12_face_to_depth,
    int* face_draw_order,
    const faceint_t* face_priority_buckets,
    int* counts)
{
    int average_depth1_2 = 0;
    int count1_2 = counts[1] + counts[2];
    if( count1_2 > 0 )
        average_depth1_2 = (priority_depths[1] + priority_depths[2]) / count1_2;
    /* ... identical to lines 363-432 of the original ... */
    int average_depth3_4 = 0;
    int count3_4 = counts[3] + counts[4];
    if( count3_4 > 0 )
        average_depth3_4 = (priority_depths[3] + priority_depths[4]) / count3_4;
    int average_depth6_8 = 0;
    int count6_8 = counts[6] + counts[8];
    if( count6_8 > 0 )
        average_depth6_8 = (priority_depths[6] + priority_depths[8]) / count6_8;

    for( int i = 0; i < counts[11]; i++ )
        flex_prio11_face_to_depth[counts[10] + i] = flex_prio12_face_to_depth[i];
    counts[10] += counts[11];

    int flexible_face_index = 0;
    int order_index = 0;

    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth1_2 )
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index++] >> 16;

    for( int prio = 0; prio < 3; prio++ )
        for( int i = 0; i < counts[prio]; i++ )
            face_draw_order[order_index++] = face_priority_buckets[prio * 2000 + i];

    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth3_4 )
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index++] >> 16;

    for( int prio = 3; prio < 5; prio++ )
        for( int i = 0; i < counts[prio]; i++ )
            face_draw_order[order_index++] = face_priority_buckets[prio * 2000 + i];

    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth6_8 )
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index++] >> 16;

    for( int prio = 5; prio < 10; prio++ )
        for( int i = 0; i < counts[prio]; i++ )
            face_draw_order[order_index++] = face_priority_buckets[prio * 2000 + i];

    while( flexible_face_index < counts[10] )
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index++] >> 16;

    return order_index;
}
```

Call site in `ToriDraw_ComputeProjectedFaceOrder` (replacing lines 505-530):

```c
    memset(scene->tmp_priority_depth_sum, 0, 12 * sizeof(faceint_t));
    memset(scene->tmp_priority_face_count, 0, 12 * sizeof(faceint_t));
    int counts[12] = { 0 };

    partition_and_accumulate_faces_by_priority(
        scene->tmp_priority_faces,
        scene->tmp_priority_face_count,
        scene->tmp_priority_depth_sum,
        scene->tmp_flex_prio11_face_to_depth,
        scene->tmp_flex_prio12_face_to_depth,
        counts,
        scene->tmp_depth_faces,
        scene->tmp_depth_face_count,
        face_priorities,
        model_min_depth,
        model_max_depth);

    scene->tmp_face_order_count = emit_face_draw_order(
        scene->tmp_priority_depth_sum,
        scene->tmp_flex_prio11_face_to_depth,
        scene->tmp_flex_prio12_face_to_depth,
        scene->tmp_face_order,
        scene->tmp_priority_faces,
        counts);
```

**Ordering is preserved exactly**: both original loops visit the same faces in
the same order, and the fused loop writes `face_priority_buckets` and the flex
arrays at the same indices they got before (both used `counts[prio]` /
`face_priority_bucket_counts[prio]` as the running index, which were always
equal).

The `_small` variants (`parition_faces_by_priority_small` at 610 +
`sort_face_draw_order_small` at 641) take the identical edit — they iterate
`sm_depth_offset[depth]..sm_depth_offset[depth+1]` instead of a `<<9` bucket,
but are otherwise line-for-line the same duplication.

---

## 4. `ToriDraw2D_FillRect` re-clips every pixel

**File:** `3rd/toridraw/toridraw_2d.c:69-107`

```c
    int a = (argb >> 24) & 0xFF;
    for( int y = y0; y < y1; y++ )
        for( int x = x0; x < x1; x++ )
        {
            if( a >= 255 )                              /* loop-invariant branch */
                pixel_buffer[y * stride + x] = argb;
            else
                ToriDraw2D_BlendArgbPixel(view_port, x, y, argb, pixel_buffer);
        }
```

The rect is already clipped to `[x0,x1) × [y0,y1)` above the loop, yet
`ToriDraw2D_BlendArgbPixel` re-tests all four clip edges per pixel, and
computes `y * stride + x` up to three times (lines 55, 65). `a >= 255` is
loop-invariant and should hoist, but the call to a non-`static`,
externally-visible `ToriDraw2D_BlendArgbPixel` blocks the compiler from
proving `pixel_buffer` isn't aliased by `view_port`, so the row base can't be
kept in a register either.

Every UI panel, every chatbox background, every scrollbar goes through this.

### Edited copy

```c
/* --- EDITED COPY of ToriDraw2D_FillRect (toridraw_2d.c) --- */
void
ToriDraw2D_FillRect(
    struct ToriDraw_ViewPort* view_port,
    int x0,
    int y0,
    int x1,
    int y1,
    int argb,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);

    int const clip_left = view_port->clip_left;
    int const clip_top = view_port->clip_top;
    int const clip_right = view_port->clip_right;
    int const clip_bottom = view_port->clip_bottom;
    int const stride = view_port->stride;

    if( x0 < clip_left )   x0 = clip_left;
    if( y0 < clip_top )    y0 = clip_top;
    if( x1 > clip_right )  x1 = clip_right;
    if( y1 > clip_bottom ) y1 = clip_bottom;

    /* + early out; the original ran a zero-trip loop but still paid the setup */
    if( x0 >= x1 || y0 >= y1 )
        return;

    int const a = (argb >> 24) & 0xFF;
    if( a == 0 )
        return;

    /* + opaque: straight row fill, no per-pixel branch, no re-clip */
    if( a >= 255 )
    {
        int const opaque = (argb & 0x00FFFFFF) | 0xFF000000;
        for( int y = y0; y < y1; y++ )
        {
            int* RESTRICT row = pixel_buffer + (size_t)y * stride;
            for( int x = x0; x < x1; x++ )
                row[x] = opaque;
        }
        return;
    }

    /* + translucent: blend inline. Same math as ToriDraw2D_BlendArgbPixel but
     *   with the clip test and the address computation hoisted, and /255
     *   replaced by the mul-shift used elsewhere in the codebase
     *   (graphics/alpha.h). */
    int const inv = 255 - a;
    int const sr = (argb >> 16) & 0xFF;
    int const sg = (argb >> 8) & 0xFF;
    int const sb = argb & 0xFF;
    int const src_rb = (sr << 16) | sb;
    int const src_g = sg << 8;

    for( int y = y0; y < y1; y++ )
    {
        int* RESTRICT row = pixel_buffer + (size_t)y * stride;
        for( int x = x0; x < x1; x++ )
        {
            int const d = row[x];
            int const rb = ((((d & 0xFF00FF) * inv) >> 8) & 0xFF00FF) +
                           (((src_rb * a) >> 8) & 0xFF00FF);
            int const g  = ((((d & 0xFF00) * inv) >> 8) & 0xFF00) +
                           (((src_g * a) >> 8) & 0xFF00);
            row[x] = (int)0xFF000000 | rb | g;
        }
    }
}
```

`ToriDraw2D_BlendArgbPixel` stays as-is for the callers that legitimately need
a single clipped pixel.

Knock-on effect: `ToriDraw2D_FillRectGradientVertical` /
`...GradientAlpha` (lines 110-169) call `FillRect` once per scanline, so they
inherit the win without changes — though each of those also does three
`toridraw2d_lerp_channel()` calls containing an integer divide per row, which
could be a fixed-point accumulator if gradients ever show up in a profile.

---

## 5. Per-pixel integer division in the scaled and tiled blits

**File:** `3rd/toridraw/toridraw_2d.c:266-298` (`BlitArgbScaled`), `301-345`
(`BlitArgbTiled`), `348-457` (`BlitArgbMasked` / `...Inverted`), `460-553`
(`BlitArgbRotatedMaskedInverted`)

- `BlitArgbScaled`: `int sx = (x * src_w) / dst_w;` — one `idiv` per pixel.
- `BlitArgbMasked`: **three** per pixel (`mx`, `cx`, `cy` — `cy` is
  loop-invariant in `x` and doesn't even need to be inside the inner loop).
- `BlitArgbTiled`: `((sx % src_w) + src_w) % src_w` — two `idiv` per pixel, for
  a value that increments by exactly 1 each iteration.
- `BlitArgbRotatedMaskedInverted`: two `double` multiplies, two adds and two
  `lround()` per pixel, plus one `idiv` for `mx`.

All four are DDA-able: the source coordinate is an affine function of the
destination coordinate, so a 16.16 accumulator with a constant step replaces
the divide entirely.

### Edited copy — `BlitArgbScaled`

```c
/* --- EDITED COPY of ToriDraw2D_BlitArgbScaled (toridraw_2d.c) --- */
void
ToriDraw2D_BlitArgbScaled(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    uint32_t const* src,
    int src_w,
    int src_h,
    int* pixel_buffer)
{
    assert(view_port);
    assert(pixel_buffer);
    assert(src);
    if( src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 )
        return;

    /* + hoist the clip rect and iterate only the visible destination range,
     *   instead of walking every destination pixel and rejecting it inside
     *   ToriDraw2D_BlendArgbPixel */
    int const cl = view_port->clip_left;
    int const ct = view_port->clip_top;
    int const cr = view_port->clip_right;
    int const cb = view_port->clip_bottom;
    int const stride = view_port->stride;

    int px0 = dst_x < cl ? cl : dst_x;
    int py0 = dst_y < ct ? ct : dst_y;
    int px1 = dst_x + dst_w; if( px1 > cr ) px1 = cr;
    int py1 = dst_y + dst_h; if( py1 > cb ) py1 = cb;
    if( px0 >= px1 || py0 >= py1 )
        return;

    /* + 16.16 DDA replaces (x * src_w) / dst_w and (y * src_h) / dst_h */
    int const step_u = (int)(((int64_t)src_w << 16) / dst_w);
    int const step_v = (int)(((int64_t)src_h << 16) / dst_h);
    int const u0 = (px0 - dst_x) * step_u;
    int v = (py0 - dst_y) * step_v;

    int const u_max = (src_w - 1) << 16;
    int const v_max = (src_h - 1) << 16;

    for( int py = py0; py < py1; py++, v += step_v )
    {
        int vv = v > v_max ? v_max : v;
        uint32_t const* srow = src + (size_t)(vv >> 16) * src_w;
        int* RESTRICT drow = pixel_buffer + (size_t)py * stride;

        int u = u0;
        for( int px = px0; px < px1; px++, u += step_u )
        {
            int uu = u > u_max ? u_max : u;
            uint32_t const s = srow[uu >> 16];
            int const a = (int)(s >> 24) & 0xFF;
            if( a == 0 )
                continue;
            if( a == 255 )
            {
                drow[px] = (int)((s & 0x00FFFFFFu) | 0xFF000000u);
                continue;
            }
            int const inv = 255 - a;
            int const d = drow[px];
            int const rb = ((((d & 0xFF00FF) * inv) >> 8) & 0xFF00FF) +
                           (((((int)s & 0xFF00FF) * a) >> 8) & 0xFF00FF);
            int const g  = ((((d & 0xFF00) * inv) >> 8) & 0xFF00) +
                           (((((int)s & 0xFF00) * a) >> 8) & 0xFF00);
            drow[px] = (int)0xFF000000 | rb | g;
        }
    }
}
```

### Edited copy — `BlitArgbTiled` inner wrap

```c
/* --- EDITED COPY of the ToriDraw2D_BlitArgbTiled loop body (toridraw_2d.c) --- */

    /* - int sy = y - origin_y;  sy = ((sy % src_h) + src_h) % src_h;
     * + one modulo at the top of the row, then increment-and-wrap */
    int sy = ((y0 - origin_y) % src_h + src_h) % src_h;
    int sx0 = ((x0 - origin_x) % src_w + src_w) % src_w;

    for( int y = y0; y < y1; y++ )
    {
        uint32_t const* srow = src + (size_t)sy * src_w;
        int* RESTRICT drow = pixel_buffer + (size_t)y * stride;

        int sx = sx0;
        for( int x = x0; x < x1; x++ )
        {
            uint32_t const s = srow[sx];

            /* ... blend as in BlitArgbScaled above ... */

            if( ++sx == src_w )     /* + replaces two idiv per pixel */
                sx = 0;
        }

        if( ++sy == src_h )
            sy = 0;
    }
```

### `BlitArgbRotatedMaskedInverted`

Same treatment: `ux`/`uy` are affine in `px`, so

```c
    /* - double lx = (double)(px - cx);
     * - double ly = (double)(py - cy);
     * - double ux = lx * cos_a + ly * sin_a;
     * - double uy = -lx * sin_a + ly * cos_a;
     * - int csx = (int)lround((double)content_cx + ux);
     * - int csy = (int)lround((double)content_cy + uy);
     *
     * + 16.16 fixed point, stepped once per pixel. cos_q/sin_q are the
     *   rotation computed once outside the loop; the row start is stepped
     *   by the row derivative. */
    int const cos_q = (int)lround(cos_a * 65536.0);
    int const sin_q = (int)lround(sin_a * 65536.0);

    /* row start at (x0, py) */
    int const lx0 = x0 - cx;
    int row_ux = ((content_cx) << 16) + lx0 * cos_q + (py - cy) * sin_q + 32768;
    int row_uy = ((content_cy) << 16) - lx0 * sin_q + (py - cy) * cos_q + 32768;

    for( int px = x0, ux = row_ux, uy = row_uy;
         px < x1;
         px++, ux += cos_q, uy -= sin_q )
    {
        int csx = ux >> 16;
        int csy = uy >> 16;
        if( (unsigned)csx >= (unsigned)content_w || (unsigned)csy >= (unsigned)content_h )
            continue;
        /* ... */
    }
    /* and per row: row_ux += sin_q; row_uy += cos_q; */
```

Note the `+ 32768` reproduces `lround`'s round-half-up for positive values;
for the negative side it rounds half toward +inf where `lround` rounds half
away from zero. Off-by-one on a texel boundary in a rotated minimap mask — check
a screenshot diff, but it is almost certainly invisible.

The `(unsigned)csx >= (unsigned)content_w` form also folds the two-sided range
check into one compare.

---

## 6. `/255` in the blend paths

**Files:** `toridraw_2d.c:62-64`, `toridraw_sprite.c:203-205`

```c
    int const r = (src_r * blend + dst_r * inv) / 255;
    int const g = (src_g * blend + dst_g * inv) / 255;
    int const b = (src_b * blend + dst_b * inv) / 255;
```

Three integer divides per pixel. The codebase already carries the standard
fix in `graphics/alpha.h` — `alpha_blend()` does the whole RGB blend with two
multiplies and shifts by packing r/b into one word:

```c
static inline int
alpha_blend(int alpha, int base, int other)
{
    int alpha_inv = 0xFF - alpha;
    return ((((base  & 0xFF00FF) * alpha_inv) >> 8) & 0xFF00FF) +
           ((((other & 0xFF00FF) * alpha)     >> 8) & 0xFF00FF) +
           ((((other & 0xFF00)   * alpha)     >> 8) & 0xFF00) +
           ((((base  & 0xFF00)   * alpha_inv) >> 8) & 0xFF00);
}
```

### Edited copy — `sprite_blend_pixel`

```c
/* --- EDITED COPY of sprite_blend_pixel (toridraw_sprite.c) --- */
#include "graphics/alpha.h"

static void
sprite_blend_pixel(
    int* dst,
    uint32_t src,
    int alpha)
{
    if( alpha >= 255 )
    {
        *dst = (int)src;
        return;
    }
    if( alpha <= 0 )
        return;

    if( (src & 0xFFFFFFFFu) == 0 )
        return;

    /* - int const r = (src_r * blend + dst_r * inv) / 255;  (x3)
     * + one packed mul-shift blend, no division */
    *dst = (int)(0xFF000000u | (uint32_t)(alpha_blend(alpha, *dst, (int)src) & 0x00FFFFFF));
}
```

Difference vs `/255`: the shift-by-8 form divides by 256, so the result is
biased low by up to `value/256` — at most 1 LSB per channel at `alpha=255`,
and exactly 0 at `alpha=0`. The 3D rasterizers already ship this
approximation for every translucent face, so the UI matching it is a
consistency win as much as a speed one. If bit-exactness with the current
output matters, `(v * 257 + 257) >> 16` is an exact `/255` for the 16-bit
products here.

---

## 7. Per-pixel clip rejection in sprite and font blits

**Files:** `toridraw_sprite.c:241-259` (`BlitSpriteAlpha`), `293-313`
(`BlitSprite_subrect`), `toridraw_font.c:701-717` (`font_draw_glyph_pixels`)

All three have the shape:

```c
    for( int y = 0; y < src_h; y++ )
    {
        int const dst_y = y + y_offset;
        if( dst_y < ct || dst_y >= cb )
            continue;                     /* whole row rejected — but we still looped to get here */
        for( int x = 0; x < src_w; x++ )
        {
            int const dst_x = x + x_offset;
            if( dst_x < cl || dst_x >= cr )
                continue;                 /* 2 compares on EVERY pixel */
            ...
        }
    }
```

The row test is cheap enough, but the column test runs per pixel for the entire
source width even when the sprite is 90% offscreen, and it prevents the inner
loop from being vectorized or from hoisting the destination row pointer.

Clamping the iteration bounds once converts both tests into loop bounds.

### Edited copy — `ToriDraw2D_BlitSprite_subrect`

```c
/* --- EDITED COPY of ToriDraw2D_BlitSprite_subrect (toridraw_sprite.c) --- */
void
ToriDraw2D_BlitSprite_subrect(
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_ViewPort* view_port,
    int x_offset,
    int y_offset,
    int src_x,
    int src_y,
    int src_w,
    int src_h,
    int* pixel_buffer)
{
    assert(sprite);
    assert(sprite->pixels_argb);
    assert(view_port);
    assert(pixel_buffer);
    if( src_w <= 0 || src_h <= 0 )
        return;
    if( src_x < 0 || src_y < 0 || src_x + src_w > sprite->width || src_y + src_h > sprite->height )
        return;

    /* Client.ts Pix8.draw(x,y): destination is (x + cropX, y + cropY) */
    x_offset += sprite->crop_x;
    y_offset += sprite->crop_y;

    int const cl = view_port->clip_left;
    int const ct = view_port->clip_top;
    int const cr = view_port->clip_right;
    int const cb = view_port->clip_bottom;
    int const stride = view_port->stride;
    int const sw = sprite->width;

    /* + clamp the source iteration range once, in source space, instead of
     *   testing each destination pixel against four clip edges */
    int x_begin = 0;
    int x_stop = src_w;
    if( x_offset + x_begin < cl ) x_begin = cl - x_offset;
    if( x_offset + x_stop  > cr ) x_stop  = cr - x_offset;

    int y_begin = 0;
    int y_stop = src_h;
    if( y_offset + y_begin < ct ) y_begin = ct - y_offset;
    if( y_offset + y_stop  > cb ) y_stop  = cb - y_offset;

    if( x_begin >= x_stop || y_begin >= y_stop )
        return;

    for( int y = y_begin; y < y_stop; y++ )
    {
        uint32_t const* RESTRICT srow = sprite->pixels_argb + (size_t)(src_y + y) * sw + src_x;
        int* RESTRICT drow = pixel_buffer + (size_t)(y + y_offset) * stride + x_offset;

        for( int x = x_begin; x < x_stop; x++ )
        {
            uint32_t const pixel = srow[x];
            if( pixel == 0 )
                continue;
            drow[x] = (int)pixel;
        }
    }
}
```

`BlitSpriteAlpha` (line 210) and `font_draw_glyph_pixels` (line 681) take the
identical edit. For the font glyph in particular this matters: glyphs are
small (5-12 px wide) so the per-pixel clip test is a large fraction of the
per-glyph work, and text is drawn thousands of times per frame in the chat and
the interface tree.

One extra note on `font_draw_glyph_pixels`: it reads `font->glyph_alpha[gi][...]`
purely as a zero/non-zero mask and always writes the flat `color`. If the
glyph mask were stored as a bitmask (or a run-length of spans) the inner loop
would become a run copy. That is a data-layout change rather than a rasterizer
change, so it is out of scope here, but it is the biggest remaining win in the
font path.

---

## 8. Integer division for the edge slopes — TRIED, REVERTED

> **Measured 2026-07-28: this is slower than the divide it replaces, and it
> changes pixels. Do not re-attempt without new evidence.**
>
> Implemented exactly as written below (reciprocal table with a `dy < 4096`
> gate and a 64-bit fallback), applied to the flat, gouraud and all six texture
> rasterizers. Results:
>
> - **World draw got slower**: 1.1911 ms mean vs 1.1468 ms with the same change
>   absent (5 samples each). `g_reciprocal16` is 16 KB and the `dy` index is
>   effectively random per triangle, so the table load misses cache more often
>   than the `sdiv` it replaces costs. A 32-bit integer divide on an M4 is
>   simply not slow enough for a 4096-entry lookup to beat.
> - **It changes output**: 2577–2626 differing pixels out of ~77 000 drawn
>   (~3.4%) on the model sweep, and 3961 differing bytes in the client frame.
>   That is an order of magnitude more than the "sub-pixel, invisible" estimate
>   below — the drift is per-scanline and accumulates down a tall edge.
> - It also broke the `scanline_compare` harness's standing invariant that a
>   textures-stripped model must render identically under both raster families
>   (1928–1986 differing pixels), because only the branching family was changed.
>
> The commented-out reciprocal lines in
> `gouraud.screen.opaque.bary.branching.s4.c` were left in place with a note
> recording this result, so the next reader does not have to rediscover it.
>
> The one part worth keeping from this section is the **overflow observation**:
> `dx << 16` is undefined once `|dx| >= 32768`. That is a latent issue in the
> current code independent of the reciprocal question, and is not fixed.

The original analysis follows.



**Files:** `flat.screen.opaque.branching.s4.c:145-158`,
`gouraud.screen.opaque.bary.branching.s4.c:126-153`,
`texshadeblend.persp.texopaque.branching.lerp8_v3.u.c:64-69`, and the
corresponding lines in every other `raster_*_ordered`.

```c
    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    ...
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    ...
    if( y2 != y1 )
        step_edge_x_BC_ish16 = ((x2 - x1) << 16) / (y2 - y1);
```

Three 32-bit `sdiv` per triangle. On Apple silicon a 32-bit `sdiv` is ~7-12
cycles and doesn't pipeline with itself. For the typical model face — a few
dozen pixels — the setup is comparable to the fill.

The commented-out lines directly above each divide show this was already tried:

```c
        // assert(dy_AC < 4096);
        // step_edge_x_AC_ish16 = (dx_AC)*g_reciprocal16[dy_AC];
```

and `g_reciprocal16[4096]` is live — `ToriDraw_TriangleSlopei` in
`triangles/toridraw_triangle_clip.u.c:23` uses it today. The comment in the
gouraud file explains the hazard:

> *Attention! This relies on the reciprocol table, and that triangles that are
> too big are already clipped away.*

Post-near-clip screen-space `dy` can exceed 4096 (a near-plane-straddling wall
projects to a very tall triangle), which is presumably why it was reverted. A
gated form keeps the fast path for the common case without the out-of-bounds
read:

### Edited copy

```c
/* --- EDITED COPY of the edge-slope setup, any raster_*_ordered --- */

/* + Reciprocal-table slope with a range gate. g_reciprocal16[d] == (1<<16)/d
 *   for d in [1, 4095]; fall back to a real divide outside that. The branch
 *   is perfectly predicted (essentially always taken) so it costs nothing
 *   next to the divide it replaces. */
static inline int
edge_step_ish16(int dx, int dy)
{
    if( dy <= 0 )
        return 0;
    if( dy < 4096 )
        return dx * g_reciprocal16[dy];
    return ((int64_t)dx << 16) / dy;
}

    /* - if( dy_AC > 0 ) step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
     * - else            step_edge_x_AC_ish16 = 0;                        */
    int step_edge_x_AC_ish16 = edge_step_ish16(dx_AC, dy_AC);
    int step_edge_x_AB_ish16 = edge_step_ish16(dx_AB, dy_AB);
    int step_edge_x_BC_ish16 = edge_step_ish16(x2 - x1, y2 - y1);
```

**Precision.** `dx * g_reciprocal16[dy]` truncates the reciprocal before
multiplying, so it differs from `(dx << 16) / dy` by up to `|dx|` in the
16.16 fraction — i.e. up to `dx / 65536` pixels of drift per scanline, or
`dx * dy / 65536` pixels accumulated over the edge. For `dx = 500, dy = 400`
that is ~3 sub-pixel units — invisible. For a very wide, very tall triangle
(`dx = 4000, dy = 4000`) it reaches ~244 units ≈ 0.004 px. Safe, but this is
the one item on the list that changes output bits, so it needs a
screenshot-diff pass and is ranked accordingly.

**Overflow.** `dx * g_reciprocal16[dy]` overflows `int` when
`|dx| * 65536/dy > 2^31`, i.e. when `|dx|/dy > 32768` — a triangle 32768×
wider than it is tall. The existing `(dx_AC << 16)` already overflows at
`|dx| >= 32768`, so this is not a new failure mode, but if the clipper does
not bound screen-space `dx` it is worth an assert either way.

---

## 9. Two divides by the same denominator in the gouraud setup — REVERTED

> **Not worth it.** The result of these two divides indexes
> `g_hsl16_to_rgb_table`, so it has to match the division exactly or the
> gouraud gradient lands on a different palette entry. The float reciprocal
> proposed below is not exact — `numerator << 8` routinely exceeds 2^24, past a
> float mantissa. Making it exact needs a 64-bit divide for the reciprocal plus
> a multiply-back correction per gradient, which is *more* work than the two
> 32-bit divides it was meant to replace.
>
> `gouraud_barycentric_steps.h` now carries a comment recording this so the
> idea does not get picked up again. The original analysis follows.



**File:** `graphics/raster/gouraud/gouraud_barycentric_steps.h` +
`gouraud.screen.opaque.bary.branching.s4.c:113-116`

```c
    int step_x_hsl_ish8 =
        gouraud_barycentric_hsl_step_ish8(d_hsl_AB * dy_AC - d_hsl_AC * dy_AB, sarea);
    int step_y_hsl_ish8 =
        gouraud_barycentric_hsl_step_ish8(d_hsl_AC * dx_AB - d_hsl_AB * dx_AC, sarea);
```

with

```c
static inline int
gouraud_barycentric_hsl_step_ish8(int numerator, int sarea)
{
    return (numerator << 8) / sarea;
}
```

Two divides, same `sarea`. Combined with #8 that is five divides in the setup
of every gouraud triangle.

Because the numerators are small (HSL16 deltas × screen deltas) and `sarea` can
be large, a shared reciprocal has to be computed at enough precision. A
`float` reciprocal is exact enough here — the result is truncated to an
8-bit-fraction integer anyway:

### Edited copy

```c
/* --- EDITED COPY of gouraud_barycentric_steps.h --- */
#ifndef GOURAUD_BARYCENTRIC_STEPS_H
#define GOURAUD_BARYCENTRIC_STEPS_H

static inline int
gouraud_barycentric_hsl_step_ish8(
    int numerator,
    int sarea)
{
    return (numerator << 8) / sarea;
}

/* + Both gouraud steps divide by the same sarea. Compute the reciprocal once.
 *   Truncation matches the integer form to within 1 ulp of the 8-bit
 *   fraction for the magnitudes involved (|numerator<<8| < 2^31 is already
 *   required by the original). */
static inline void
gouraud_barycentric_hsl_steps_ish8(
    int numerator_x,
    int numerator_y,
    int sarea,
    int* out_step_x_ish8,
    int* out_step_y_ish8)
{
    float const inv = 1.0f / (float)sarea;
    *out_step_x_ish8 = (int)((float)(numerator_x << 8) * inv);
    *out_step_y_ish8 = (int)((float)(numerator_y << 8) * inv);
}

#endif
```

call site:

```c
    /* - int step_x_hsl_ish8 = gouraud_barycentric_hsl_step_ish8(
     * -     d_hsl_AB * dy_AC - d_hsl_AC * dy_AB, sarea);
     * - int step_y_hsl_ish8 = gouraud_barycentric_hsl_step_ish8(
     * -     d_hsl_AC * dx_AB - d_hsl_AB * dx_AC, sarea);                  */
    int step_x_hsl_ish8, step_y_hsl_ish8;
    gouraud_barycentric_hsl_steps_ish8(
        d_hsl_AB * dy_AC - d_hsl_AC * dy_AB,
        d_hsl_AC * dx_AB - d_hsl_AB * dx_AC,
        sarea,
        &step_x_hsl_ish8,
        &step_y_hsl_ish8);
```

Caveat: `float` has 24 bits of mantissa, so `(numerator << 8)` above 2^24
loses low bits that the integer divide keeps. Given the result is immediately
`>> 8`'d into a 16-bit HSL index inside the scanline, that loss is below the
visible threshold — but if it needs to be exact, a 32.32 fixed-point
reciprocal (`(1ll << 40) / sarea`, then `(num * recip) >> 32`) gives the same
one-divide saving with no precision question.

---

## 10. `getenv()` in the per-face raster loop

**File:** `3rd/toridraw/toridraw_raster.u.c:154-168`

```c
        if( texture == NULL )
        {
            /* TORIRS_RASTER_TEX_DEBUG=1: tally skipped textured faces. */
            static int skip_tally[TORIDRAW_TEXTURE_ID_CAPACITY];
            static int skip_total = 0;
            if( texture_id >= 0 && texture_id < TORIDRAW_TEXTURE_ID_CAPACITY &&
                getenv("TORIRS_RASTER_TEX_DEBUG") )
            { ... }
            return;
        }
```

`getenv()` is a linear scan of `environ` doing a `strncmp` per entry. This is
inside `ToriDraw_RasterModelFace`, called once per face per model per frame.
It only fires when a texture hasn't loaded yet — but that is exactly the
steady state during map streaming, when every face of every newly-arrived
model misses — the same condition the texture-wants-registry work was chasing.

Trivial fix — cache the lookup:

### Edited copy

```c
/* --- EDITED COPY of the texture-miss branch (toridraw_raster.u.c) --- */
        if( texture == NULL )
        {
            /* TORIRS_RASTER_TEX_DEBUG=1: tally skipped textured faces. */
            static int skip_tally[TORIDRAW_TEXTURE_ID_CAPACITY];
            static int skip_total = 0;
            /* + resolve the env var once, not once per skipped face */
            static int debug_enabled = -1;
            if( debug_enabled < 0 )
                debug_enabled = getenv("TORIRS_RASTER_TEX_DEBUG") ? 1 : 0;

            if( debug_enabled && texture_id >= 0 &&
                texture_id < TORIDRAW_TEXTURE_ID_CAPACITY )
            {
                skip_tally[texture_id]++;
                if( ++skip_total % 500 == 1 )
                    fprintf(
                        stderr,
                        "raster_tex_skip: total=%d id=%d (count=%d)\n",
                        skip_total,
                        texture_id,
                        skip_tally[texture_id]);
            }
            return;
        }
```

(The reordered condition also short-circuits on the flag first, so the two
bounds compares vanish in the normal case.)

---

## 11. Memoize the per-face texture lookup

**File:** `3rd/toridraw/toridraw_raster.u.c:138-179`

```c
    if( ctx->face_textures != NULL )
        texture_id = ctx->face_textures[face];
    else
        texture_id = -1;

    if( texture_id != -1 )
    {
        texture = (texture_id >= 0 && texture_id < TORIDRAW_TEXTURE_ID_CAPACITY)
                      ? ToriDraw_TextureMapGet(ctx->texture_map, texture_id)
                      : NULL;
        ...
        texels = texture->texels;
        texture_size = texture->width;
        texture_opaque = texture->opaque;
```

Per textured face: a bounds check, an indirect load from the texture map, then
three dependent loads from the `ToriDraw_Texture` struct. Models overwhelmingly
use one or two textures, and the face order (depth-bucketed) does not
randomize the texture id much within a run.

Caching the last resolved `(id → texels/size/opaque)` in the raster context
turns the common case into one compare:

### Edited copy

```c
/* --- EDITED COPY: add to struct ToriDrawModelRasterContext --- */
struct ToriDrawModelRasterContext
{
    /* ... existing fields ... */
    struct ToriDraw_TextureMap* texture_map;
    int flags;
    bool allow_near_clip;

    /* + last-resolved texture memo; cache_id == -1 means empty. Reset in
     *   context_from_handle(). */
    int cache_texture_id;
    const int* cache_texels;
    int cache_texture_size;
    int cache_texture_opaque;
};

/* --- EDITED COPY: the lookup in ToriDraw_RasterModelFace --- */
    if( texture_id != -1 )
    {
        if( texture_id == ctx->cache_texture_id )
        {
            /* + hit: three register reads instead of four dependent loads */
            texels = ctx->cache_texels;
            texture_size = ctx->cache_texture_size;
            texture_opaque = ctx->cache_texture_opaque;
        }
        else
        {
            texture = (texture_id >= 0 && texture_id < TORIDRAW_TEXTURE_ID_CAPACITY)
                          ? ToriDraw_TextureMapGet(ctx->texture_map, texture_id)
                          : NULL;
            if( texture == NULL )
            {
                /* ... miss handling from #10 ... */
                return;
            }

            texels = texture->texels;
            texture_size = texture->width;
            texture_opaque = texture->opaque;

            ctx->cache_texture_id = texture_id;
            ctx->cache_texels = texels;
            ctx->cache_texture_size = texture_size;
            ctx->cache_texture_opaque = texture_opaque;
        }

        if( color_c == TORIDRAWHSL16_FLAT )
            goto textured_flat;
        else
            goto textured;
    }
```

and in `context_from_handle`, alongside `ctx->flags = 0;`:

```c
        ctx->cache_texture_id = -1;     /* + memo starts empty per model */
        ctx->cache_texels = NULL;
        ctx->cache_texture_size = 0;
        ctx->cache_texture_opaque = 0;
```

**Invalidation.** The memo lives for one model's raster pass only (the context
is a stack local built fresh in `ToriDraw_RasterWithFaceIndices`), so
`ToriDraw_TextureMapAnimate` / `ToriDraw_TextureMapSet` can't invalidate it
mid-pass — a texture swap between models is picked up because the next model
gets a fresh context. That is the same lifetime the current code effectively
has.

---

## 12. Pick test uses float division per face

**File:** `3rd/toridraw/toridraw_render.u.c:967-1053`

```c
static inline bool
toridraw_triangle_contains_point(int x1,int y1,int x2,int y2,int x3,int y3,int x,int y)
{
    int denominator = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
    if( denominator != 0 )
    {
        float a = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / (float)denominator;
        float b = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / (float)denominator;
        float c = 1 - a - b;
        return (a >= 0 && b >= 0 && c >= 0);
    }
    return false;
}
```

Two float divides and an int→float conversion per face, and
`ToriDraw_ProjectedModelContainsPoint` runs this over **every** face of the
model (line 1034) — no early AABB-per-face, no winding early-out. This is the
mouse-pick path; it runs for every candidate model under the cursor, every
frame the cursor moves.

The barycentric signs don't need the division at all — only the sign relative
to `denominator`:

### Edited copy

```c
/* --- EDITED COPY of toridraw_triangle_contains_point (toridraw_render.u.c) --- */

/* Sign-only barycentric containment: the division by `denominator` scales all
 * three coordinates by the same factor, so comparing the unnormalized
 * numerators against zero (with the sign of the denominator folded in) gives
 * the identical answer with no divides and no float. */
static inline bool
toridraw_triangle_contains_point(
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int x,
    int y)
{
    int denominator = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
    if( denominator == 0 )
        return false;

    int a_num = (y2 - y3) * (x - x3) + (x3 - x2) * (y - y3);
    int b_num = (y3 - y1) * (x - x3) + (x1 - x3) * (y - y3);
    int c_num = denominator - a_num - b_num;   /* c = 1 - a - b, unnormalized */

    /* + fold the denominator's sign in instead of dividing */
    if( denominator < 0 )
    {
        a_num = -a_num;
        b_num = -b_num;
        c_num = -c_num;
    }

    return a_num >= 0 && b_num >= 0 && c_num >= 0;
}
```

This is bit-exact with the float version except at the boundary, where the
float version could round a tiny negative to `-0.0f` and pass `>= 0`. The
integer form is the stricter (and more correct) test.

While in here: `ToriDraw_ProjectedModelContainsPoint`'s loop could also
reject a face in two compares before the eight multiplies, using the same
screen-space min/max the sort pass already touches:

```c
    for( int i = 0; i < face_count; i++ )
    {
        int face_a = fia[i], face_b = fib[i], face_c = fic[i];
        int x1 = scene->screen_vertices_x[face_a];
        int x2 = scene->screen_vertices_x[face_b];
        int x3 = scene->screen_vertices_x[face_c];

        /* + cheap x-extent reject before touching y or doing any multiply */
        int xmin = x1 < x2 ? (x1 < x3 ? x1 : x3) : (x2 < x3 ? x2 : x3);
        if( adjusted_screen_x < xmin )
            continue;
        int xmax = x1 > x2 ? (x1 > x3 ? x1 : x3) : (x2 > x3 ? x2 : x3);
        if( adjusted_screen_x > xmax )
            continue;

        int y1 = scene->screen_vertices_y[face_a];
        int y2 = scene->screen_vertices_y[face_b];
        int y3 = scene->screen_vertices_y[face_c];
        if( toridraw_triangle_contains_point(x1, y1, x2, y2, x3, y3,
                                             adjusted_screen_x, adjusted_screen_y) )
            return true;
    }
```

---

## x86 span variants

`tex.span.sse2.u.c`, `tex.span.sse41.u.c` and `tex.span.avx.u.c` carried both
defects — the double-divide per 8-pixel block (#2) and the
`continue`-skips-advance bug (Appendix A2) — in four functions each:
`draw_texture_scanline_{opaque,transparent}_blend_branching_lerp8{,_v3}_ordered`.

They are not compiled on this Apple-silicon target, so rather than edit them
blind:

1. **Transplanted, not retyped.** The 12 regions were replaced by script
   (`port_fix.py` in the session scratchpad), which refuses to touch a region
   unless it is byte-for-byte identical to the corresponding region in the
   a8caf7fe baseline of the already-fixed NEON/scalar donor. All 12 matched
   (modulo two comment-only lines), so the x86 copies were verified to be
   literal duplicates before anything was changed.
2. **Built and run under Rosetta.** `cc -arch x86_64` plus Rosetta 2 executes
   SSE2, SSE4.1 *and* AVX2 on this machine, so these are actually tested, not
   just compiled. A probe harness drives all four entry points over 3200
   deterministic span geometries per ISA (both texture sizes, guard bands
   around the destination) and hashes the result.

The same pass also closed a gap in the scalar file: its two `_v3` functions had
been left on the old double-divide (only its non-`v3` pair was fixed in the
first round). Its `_v3` *opaque* variant is the one function of the twelve that
is not a literal duplicate of a NEON donor — uniquely, it clamps `u` where the
NEON opaque twin relies on `u_mask` — so that one was written by hand rather
than transplanted.

Results — baseline vs fixed, all five implementations:

| implementation | normal spans (`w` never 0) | degenerate spans (`w` crosses 0), px drawn |
|---|---|---|
| NEON | **hash identical** | 352 522 → 352 506 |
| scalar | **hash identical** | 352 522 → 355 792 |
| SSE2 | **hash identical** | 352 515 → 355 792 |
| SSE4.1 | **hash identical** | 352 515 → 355 792 |
| AVX2 | **hash identical** | 352 515 → 355 792 |

NEON moves the other way in the degenerate column because it never had the
advance bug — only the division-by-zero `(int)` conversion, whose replacement
by a flat gradient changes which texels a *transparent* span skips. The four
that did have the bug converge on the same 355 792.

The normal sweep being bit-identical is the point: the carry-forward refactor
changes nothing about ordinary spans. The degenerate sweep differing *is the
bug fix* — the old code skipped the `offset += 8` when a block's `w` was zero,
so every later block in that span landed 8 pixels left of where it belonged and
the span's tail was dropped. Note the guard bands were never touched in either
build: this was misplaced and dropped pixels, **not** a buffer overrun.

All three ISAs also agree with each other exactly, and a full
`toridraw_unity.c` builds warning-clean for x86_64 under each of `-msse2`,
`-msse4.1` and `-mavx2`.

### A pre-existing divergence this turned up

The probe also shows something that was already true and is worth recording:
**the scalar span does not agree with the SIMD spans**, even on ordinary spans,
at baseline as well as after the fix. NEON, SSE2, SSE4.1 and AVX2 all produce
one hash; scalar produces a different one for the same inputs.

The cause is that the scalar non-`v3` path computes `au / w` with an integer
divide, while every SIMD path multiplies by a `float` reciprocal `1.0f / w` —
different rounding, so texels differ by one along some spans. (The `_v3`
functions are now consistent: all five use the float reciprocal.)

This is not introduced here and is not fixed here. It matters because the web
build is the one most likely to select the scalar path: if wasm output ever has
to match native pixel-for-pixel, this is where the difference comes from.

---

## Appendix A — non-performance observations

These came out of reading the same code. Neither is a speed issue; recording
them so they aren't lost.

### A1. Depth bucket overflow is unchecked

`bucket_sort_by_average_depth` (`toridraw_render.u.c:244-272`):

```c
            const int count = face_depth_bucket_counts[depth_avg];
            face_depth_bucket_counts[depth_avg] = count + 1;
            face_depth_buckets[(depth_avg << 9) + count] = (faceint_t)f;
```

The `<< 9` fixes the bucket capacity at 512 faces per depth level, and nothing
bounds `count`. A model with more than 512 front-facing triangles sharing one
quantized depth (a large flat wall viewed edge-on, a terrain patch) silently
writes into the *next* depth's bucket and corrupts the draw order. The
`_small` variant (line 534) uses a counting sort with exact offsets and does
not have this hazard.

A one-line guard costs nothing measurable:

```c
            const int count = face_depth_bucket_counts[depth_avg];
            if( count < 512 )   /* + bucket capacity is (1 << 9) */
            {
                face_depth_bucket_counts[depth_avg] = count + 1;
                face_depth_buckets[(depth_avg << 9) + count] = (faceint_t)f;
                if( depth_avg < min_d ) min_d = depth_avg;
                if( depth_avg > max_d ) max_d = depth_avg;
            }
```

Also note `bucket_sort_by_average_depth` hardcodes `1500` in three places
(`min_d = 1500`, the `< 1500` range test, and `parition_faces_by_priority`'s
clamp) while the `_small` path reads `scene->depth_levels`. If
`depth_levels` is ever configured to something other than 1500 the large path
will disagree with its own allocation.

### A2. Scalar texture span skips pixels without advancing

`tex.span.scalar.u.c:182-198`:

```c
    while( lerp8_steps-- > 0 )
    {
        int w = (cw) >> texture_shift;
        if( w == 0 )
            continue;          /* does not advance au/bv/cw or offset */
        ...
```

The `continue` skips the `au += step_au_dx` / `offset += 8` at the bottom of
the loop, so a single `w == 0` block leaves the span misaligned for every
block after it, and `cw` never changes so the condition stays true for the
rest of the span. The NEON `_v3` path got this right — it guards only the draw
and always advances.

**Fixed** in the scalar file and in all three x86 variants, which carried the
identical code. Measured effect in the probe harness: on spans where `w`
crosses zero the old code drew 352 515 pixels and the fixed code draws
355 792 — the difference is span content that was being silently dropped and
misplaced. Guard bands were clean in both, so this was never a buffer overrun.

---

## How to measure

`src/flamegraph.svg` and `src/main.folded` are already in the tree, so the
folded-stack workflow exists. For per-item attribution the useful split is:

- **#1, #2, #8, #9** — triangle throughput. `TORIRS_WORLD_BMP` with a fixed
  camera gives a deterministic frame; time `ToriDraw_RenderModel3Raster` alone.
- **#3** — sort cost. Time `ToriDraw_RenderModel2SortFaces` across a full
  world frame; it is cleanly separable from projection and raster by design.
- **#4-#7** — UI cost. A frame with the interface tree open and the world
  hidden isolates the 2D path.
- **#12** — pick cost. `TORIRS_SIM_MOUSE_CLICK` / `SIM_HOVER` drive it
  deterministically.

### What actually happened

The prediction that #2, #6, #8 and #9 would "change output bits" was right
about the risk and wrong about which items. #2 and #6 turned out to be
implementable exactly (the `w_n == 0` degenerate block never arises in
practice, and `/255` has an exact shift form), so they ship bit-identical. #8
and #9 were the ones that genuinely could not be — and #8 was also the only
item on the list that turned out to be a *pessimization*.

The lesson worth carrying: on this target, a 32-bit integer divide is cheap
enough that table-lookup reciprocals lose to it once the table is big enough to
miss cache. The wins here all came from removing *work* — redundant traversals,
re-clipping, recomputed reciprocals, per-pixel function calls — not from
replacing arithmetic with cleverer arithmetic.
