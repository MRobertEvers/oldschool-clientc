#ifndef TEXTURE_DEOB2_U_C
#define TEXTURE_DEOB2_U_C

#include "graphics/raster/texture/texture.deob.u.c"

/*
 * Delegate to pix3d_deob_texture_triangle (Pix3D.textureTriangle port).
 *
 * An earlier implementation permuted which orthographic sample was passed as barycentric
 * origin vs B/C legs based on y-order. That is incorrect: Pix3D.ts keeps fixed roles —
 * vertical = origin − txB (P−M), horizontal = txC − origin (N−P) with origin = texture P,
 * txB/tyB/tzB = M, txC/tyC/tzC = N — regardless of which screen vertex has ymin.
 * (See Client-TS/src/dash3d/Pix3D.ts ~1632–1650 and Model.ts textureTriangle call site.)
 */
static inline void
pix3d_deob2_texture_triangle(
    int xA,
    int xB,
    int xC,
    int yA,
    int yB,
    int yC,
    int shadeA,
    int shadeB,
    int shadeC,
    int originX,
    int originY,
    int originZ,
    int txB,
    int txC,
    int tyB,
    int tyC,
    int tzB,
    int tzC,
    int* texels,
    int is_opaque)
{
    pix3d_deob_texture_triangle(
        xA,
        xB,
        xC,
        yA,
        yB,
        yC,
        shadeA,
        shadeB,
        shadeC,
        originX,
        originY,
        originZ,
        txB,
        txC,
        tyB,
        tyC,
        tzB,
        tzC,
        texels,
        is_opaque);
}

#endif /* TEXTURE_DEOB2_U_C */
