#ifndef TEX_TRI_ASM_SUPPORT_U_C
#define TEX_TRI_ASM_SUPPORT_U_C

/*
 * The one C symbol tex_tri_i686.S calls.
 *
 * ToriDraw_TexturePlanePrepare32 is static inline in projection.u.c, which is
 * exactly what a C caller wants and exactly what an assembler cannot reach:
 * there is no out-of-line copy of it to call. This gives it one, and only in
 * the build that actually assembles the kernel -- see tex_tri_asm.h for why the
 * plane setup stays in C rather than being hand-written along with the rest.
 *
 * Must be included from a translation unit that has already pulled in
 * projection.u.c; the dispatch site in triangles/ does.
 */

#include "graphics/raster/texture/tex_tri_asm.h"

#if defined(TORIDRAW_TEXTRI_ASM) || defined(TORIDRAW_TEXTRI_NEON_ASM)

#include <assert.h>

int
toridraw_texplane_prepare32_asm(
    struct ToriDraw_TexturePlane32* plane,
    int screen_width,
    int screen_height,
    int camera_cot16)
{
    assert(plane);
    return ToriDraw_TexturePlanePrepare32(
               plane, screen_width, screen_height, camera_cot16)
               ? 1
               : 0;
}

#endif

#endif
