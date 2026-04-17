#ifndef DASH2D_SIMD_U_C
#define DASH2D_SIMD_U_C

#include <stdint.h>

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "dash2d_simd.neon.u.c"
#define dash2d_blit_sprite_subrect_fast dash2d_blit_sprite_subrect_neon
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "dash2d_simd.avx.u.c"
#define dash2d_blit_sprite_subrect_fast dash2d_blit_sprite_subrect_avx
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "dash2d_simd.sse41.u.c"
#define dash2d_blit_sprite_subrect_fast dash2d_blit_sprite_subrect_sse41
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "dash2d_simd.sse2.u.c"
#define dash2d_blit_sprite_subrect_fast dash2d_blit_sprite_subrect_sse2
#else
#include "dash2d_simd.scalar.u.c"
#define dash2d_blit_sprite_subrect_fast dash2d_blit_sprite_subrect_scalar
#endif

#endif
