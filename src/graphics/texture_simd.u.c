#ifndef TEXTURE_SIMD_U_C
#define TEXTURE_SIMD_U_C

#include <stdint.h>

// clang-format off
#include "shade.h"
// clang-format on

/* Per-triangle constants for draw_texture_scanline_*_blerp8_v3 (filled once per triangle). */
typedef struct TextureScanlineV3Context
{
    int* pixel_buffer;
    int screen_width;
    int step_au_dx;
    int step_bv_dx;
    int step_cw_dx;
    int step_shade8bit_dx_ish8;
    int* texels;
    int texture_width;
    int texture_shift;
    int u_mask;
    int v_mask;
} TextureScanlineV3Context;

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "texture_simd.neon.u.c"
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "texture_simd.avx.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "texture_simd.sse41.u.c"
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "texture_simd.sse2.u.c"
#else
#include "texture_simd.scaler.u.c"
#endif

#endif
