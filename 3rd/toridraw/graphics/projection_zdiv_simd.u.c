#ifndef PROJECTION_ZDIV_SIMD_U_C
#define PROJECTION_ZDIV_SIMD_U_C

#include <stdint.h>

/*
 * ONE LADDER. It picks the lane file AND names that lane's two clipping entry
 * points, so the wrappers below each carry a single call. They used to carry a
 * copy of this ladder apiece, five arms deep, which said the same thing twice
 * and made the two functions look like they might disagree about which lane a
 * build has.
 *
 * The scalar arm's entry takes a start index the vector lanes do not, so the
 * uniform shape is spelled out per arm rather than by pasting a suffix onto a
 * common stem.
 */
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "projection_zdiv_simd.neon.u.c"
#define TORIDRAW_ZDIV_PASS_TEX_CLIP(oz, sx, sy, sz, n, mid, near) \
    projection_zdiv_tex_neon_clip((oz), (sx), (sy), (sz), (n), (mid), (near))
#define TORIDRAW_ZDIV_PASS_NOTEX_CLIP(sx, sy, sz, n, mid, near) \
    projection_zdiv_notex_neon_clip((sx), (sy), (sz), (n), (mid), (near))
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "projection_zdiv_simd.scalar.u.c"
#include "projection_zdiv_simd.avx.u.c"
#define TORIDRAW_ZDIV_PASS_TEX_CLIP(oz, sx, sy, sz, n, mid, near) \
    projection_zdiv_tex_avx2_clip((oz), (sx), (sy), (sz), (n), (mid), (near))
#define TORIDRAW_ZDIV_PASS_NOTEX_CLIP(sx, sy, sz, n, mid, near) \
    projection_zdiv_notex_avx2_clip((sx), (sy), (sz), (n), (mid), (near))
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "projection_zdiv_simd.scalar.u.c"
#include "projection_zdiv_simd.sse41.u.c"
#define TORIDRAW_ZDIV_PASS_TEX_CLIP(oz, sx, sy, sz, n, mid, near) \
    projection_zdiv_tex_sse41_clip((oz), (sx), (sy), (sz), (n), (mid), (near))
#define TORIDRAW_ZDIV_PASS_NOTEX_CLIP(sx, sy, sz, n, mid, near) \
    projection_zdiv_notex_sse41_clip((sx), (sy), (sz), (n), (mid), (near))
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "projection_zdiv_simd.scalar.u.c"
#include "projection_zdiv_simd.sse2.u.c"
#define TORIDRAW_ZDIV_PASS_TEX_CLIP(oz, sx, sy, sz, n, mid, near) \
    projection_zdiv_tex_sse2_clip((oz), (sx), (sy), (sz), (n), (mid), (near))
#define TORIDRAW_ZDIV_PASS_NOTEX_CLIP(sx, sy, sz, n, mid, near) \
    projection_zdiv_notex_sse2_clip((sx), (sy), (sz), (n), (mid), (near))
#else
#include "projection_zdiv_simd.scalar.u.c"
#define TORIDRAW_ZDIV_PASS_TEX_CLIP(oz, sx, sy, sz, n, mid, near) \
    projection_zdiv_tex_scalar_range_clip((oz), (sx), (sy), (sz), 0, (n), (mid), (near))
#define TORIDRAW_ZDIV_PASS_NOTEX_CLIP(sx, sy, sz, n, mid, near) \
    projection_zdiv_notex_scalar_range_clip((sx), (sy), (sz), 0, (n), (mid), (near))
#endif

/* No callers today. Pinned to the clipping family, which is what this did
 * before the near-clip split: a future caller must pick a family deliberately. */
static inline void
projection_zdiv_pass_tex(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_linear_slots,
    int model_mid_z,
    int near_plane_z)
{
    TORIDRAW_ZDIV_PASS_TEX_CLIP(
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
}

/* Reads/writes screen vertex buffers only; orthographic arrays are never touched. */
static inline void
projection_zdiv_pass_notex(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_linear_slots,
    int model_mid_z,
    int near_plane_z)
{
    TORIDRAW_ZDIV_PASS_NOTEX_CLIP(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        num_linear_slots,
        model_mid_z,
        near_plane_z);
}

#endif
