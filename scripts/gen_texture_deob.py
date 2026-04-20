#!/usr/bin/env python3
"""Build texture.deob.u.c from gouraud.deob.u.c triangle + embedded raster."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GOUR = ROOT / "src/graphics/raster/gouraud/gouraud.deob.u.c"
OUT = ROOT / "src/graphics/raster/texture/texture.deob.u.c"

RASTER_C = r'''#ifndef TEXTURE_DEOB_U_C
#define TEXTURE_DEOB_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/deob/pix3d_deob_state.h"
#include "graphics/shade.h"

#include <stdint.h>

extern int g_reciprocal15[4096];

/* 128×128 layout: same u/v indexing as tex.span / texshadeflat scanlines (texture_shift 7). */
static inline int
pix3d_deob_texture_texel_fetch_128(int* texels, int curU, int curV)
{
    int u = (curU >> 7) & 127;
    int v = curV & 0x3f80;
    return texels[u + v];
}

static inline int
pix3d_deob_texture_shade_u8(int shadeA_ish)
{
    int s = shadeA_ish >> 8;
    if( s > 255 )
        s = 255;
    if( s < 0 )
        s = 0;
    return s;
}

static inline void
pix3d_deob_texture_raster(
    int xA,
    int xB,
    int* RESTRICT dst,
    int off,
    int* texels,
    int curU,
    int curV,
    int u,
    int v,
    int w,
    int uStride,
    int vStride,
    int wStride,
    int shadeA,
    int shadeB)
{
    if( !texels || xA >= xB )
        return;

    int shadeStrides;
    int strides;
    if( g_pix3d_deob_hclip )
    {
        shadeStrides = ((shadeB - shadeA) / (xB - xA)) | 0;
        if( xB > g_pix3d_deob_size_x )
            xB = g_pix3d_deob_size_x;
        if( xA < 0 )
        {
            shadeA -= xA * shadeStrides;
            xA = 0;
        }
        if( xA >= xB )
            return;
        strides = (xB - xA) >> 3;
        shadeStrides <<= 12;
    }
    else
    {
        if( xB - xA > 7 )
        {
            strides = (xB - xA) >> 3;
            shadeStrides = ((shadeB - shadeA) * g_reciprocal15[strides]) >> 6;
        }
        else
        {
            strides = 0;
            shadeStrides = 0;
        }
    }

    shadeA <<= 9;
    off += xA;

    int nextU = 0, nextV = 0, curW, dx, stepU, stepV;

    if( g_pix3d_deob_low_mem )
        return;

    dx = xA - g_pix3d_deob_origin_x;
    u = u + (uStride >> 3) * dx;
    v = v + (vStride >> 3) * dx;
    w = w + (wStride >> 3) * dx;
    u |= 0;
    v |= 0;
    w |= 0;

    curW = w >> 14;
    if( curW != 0 )
    {
        curU = (u / curW) | 0;
        curV = (v / curW) | 0;
        if( curU < 0 )
            curU = 0;
        else if( curU > 16256 )
            curU = 16256;
    }

    u = u + uStride;
    v = v + vStride;
    w = w + wStride;
    u |= 0;
    v |= 0;
    w |= 0;

    curW = w >> 14;
    if( curW != 0 )
    {
        nextU = (u / curW) | 0;
        nextV = (v / curW) | 0;
        if( nextU < 7 )
            nextU = 7;
        else if( nextU > 16256 )
            nextU = 16256;
    }

    stepU = (nextU - curU) >> 3;
    stepV = (nextV - curV) >> 3;

#define DEOB_TEX_PIX()                                                                               \
    do                                                                                             \
    {                                                                                              \
        int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);                        \
        int shade = pix3d_deob_texture_shade_u8(shadeA);                                             \
        if( g_pix3d_deob_opaque )                                                                  \
            dst[off++] = shade_blend(texel, shade);                                                \
        else                                                                                       \
        {                                                                                          \
            if( texel != 0 )                                                                       \
                dst[off] = shade_blend(texel, shade);                                              \
            off++;                                                                                 \
        }                                                                                          \
        curU += stepU;                                                                             \
        curV += stepV;                                                                             \
    } while( 0 )

#define DEOB_TEX_BLOCK8()                                                                          \
    do                                                                                             \
    {                                                                                              \
        DEOB_TEX_PIX();                                                                            \
        DEOB_TEX_PIX();                                                                            \
        DEOB_TEX_PIX();                                                                            \
        DEOB_TEX_PIX();                                                                            \
        DEOB_TEX_PIX();                                                                            \
        DEOB_TEX_PIX();                                                                            \
        DEOB_TEX_PIX();                                                                            \
        DEOB_TEX_PIX();                                                                            \
        curU = nextU;                                                                              \
        curV = nextV;                                                                              \
        u += uStride;                                                                              \
        v += vStride;                                                                              \
        w += wStride;                                                                              \
        u |= 0;                                                                                    \
        v |= 0;                                                                                    \
        w |= 0;                                                                                    \
        curW = w >> 14;                                                                            \
        if( curW != 0 )                                                                            \
        {                                                                                          \
            nextU = (u / curW) | 0;                                                                \
            nextV = (v / curW) | 0;                                                                \
            if( nextU < 7 )                                                                        \
                nextU = 7;                                                                         \
            else if( nextU > 16256 )                                                               \
                nextU = 16256;                                                                     \
        }                                                                                          \
        stepU = (nextU - curU) >> 3;                                                               \
        stepV = (nextV - curV) >> 3;                                                               \
        shadeA += shadeStrides;                                                                    \
    } while( 0 )

    while( strides-- > 0 )
        DEOB_TEX_BLOCK8();

    strides = (xB - xA) & 0x7;
    while( strides-- > 0 )
        DEOB_TEX_PIX();

#undef DEOB_TEX_BLOCK8
#undef DEOB_TEX_PIX
}

'''

g = GOUR.read_text()
s = g.index("static inline void\npix3d_deob_gouraud_triangle(")
tri = g[s:]

tri = tri.replace("pix3d_deob_gouraud_triangle", "pix3d_deob_texture_triangle")
tri = tri.replace("#endif /* GOURAUD_DEOB_U_C */", "#endif /* TEXTURE_DEOB_U_C */")

tri = tri.replace(
    "    int colourA,\n    int colourB,\n    int colourC)",
    "    int shadeA,\n    int shadeB,\n    int shadeC,\n    int originX,\n    int originY,\n    int originZ,\n    int txB,\n    int txC,\n    int tyB,\n    int tyC,\n    int tzB,\n    int tzC,\n    int* texels,\n    int is_opaque)",
)
tri = tri.replace("{\n    int xStepAB = 0;", "{\n    g_pix3d_deob_opaque = is_opaque;\n\n    int verticalX = originX - txB;\n    int verticalY = originY - tyB;\n    int verticalZ = originZ - tzB;\n    int horizontalX = txC - originX;\n    int horizontalY = tyC - originY;\n    int horizontalZ = tzC - originZ;\n    int u = (horizontalX * originY - horizontalY * originX) << 14;\n    int uStride = (horizontalY * originZ - horizontalZ * originY) << 8;\n    int uStepVertical = (horizontalZ * originX - horizontalX * originZ) << 5;\n    int v = (verticalX * originY - verticalY * originX) << 14;\n    int vStride = (verticalY * originZ - verticalZ * originY) << 8;\n    int vStepVertical = (verticalZ * originX - verticalX * originZ) << 5;\n    int w = (verticalY * horizontalX - verticalX * horizontalY) << 14;\n    int wStride = (verticalZ * horizontalY - verticalY * horizontalZ) << 8;\n    int wStepVertical = (verticalX * horizontalZ - verticalZ * horizontalX) << 5;\n\n    int xStepAB = 0;",
)

calls = [
    ("pix3d_deob_gouraud_raster(xC >> 16, xB >> 16, colourC >> 7, colourB >> 7, g_pix3d_deob_pixels, yA, 0)",
     "pix3d_deob_texture_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeB >> 8)"),
    ("pix3d_deob_gouraud_raster(xC >> 16, xA >> 16, colourC >> 7, colourA >> 7, g_pix3d_deob_pixels, yA, 0)",
     "pix3d_deob_texture_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeA >> 8)"),
    ("pix3d_deob_gouraud_raster(xB >> 16, xC >> 16, colourB >> 7, colourC >> 7, g_pix3d_deob_pixels, yA, 0)",
     "pix3d_deob_texture_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeC >> 8)"),
    ("pix3d_deob_gouraud_raster(xA >> 16, xC >> 16, colourA >> 7, colourC >> 7, g_pix3d_deob_pixels, yA, 0)",
     "pix3d_deob_texture_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeC >> 8)"),
    ("pix3d_deob_gouraud_raster(xB >> 16, xA >> 16, colourB >> 7, colourA >> 7, g_pix3d_deob_pixels, yA, 0)",
     "pix3d_deob_texture_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeA >> 8)"),
    ("pix3d_deob_gouraud_raster(xA >> 16, xB >> 16, colourA >> 7, colourB >> 7, g_pix3d_deob_pixels, yA, 0)",
     "pix3d_deob_texture_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeB >> 8)"),
    ("pix3d_deob_gouraud_raster(xA >> 16, xB >> 16, colourA >> 7, colourB >> 7, g_pix3d_deob_pixels, yB, 0)",
     "pix3d_deob_texture_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeB >> 8)"),
    ("pix3d_deob_gouraud_raster(xC >> 16, xB >> 16, colourC >> 7, colourB >> 7, g_pix3d_deob_pixels, yB, 0)",
     "pix3d_deob_texture_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeB >> 8)"),
    ("pix3d_deob_gouraud_raster(xB >> 16, xC >> 16, colourB >> 7, colourC >> 7, g_pix3d_deob_pixels, yB, 0)",
     "pix3d_deob_texture_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeC >> 8)"),
    ("pix3d_deob_gouraud_raster(xB >> 16, xA >> 16, colourB >> 7, colourA >> 7, g_pix3d_deob_pixels, yB, 0)",
     "pix3d_deob_texture_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeA >> 8)"),
    ("pix3d_deob_gouraud_raster(xA >> 16, xC >> 16, colourA >> 7, colourC >> 7, g_pix3d_deob_pixels, yB, 0)",
     "pix3d_deob_texture_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeC >> 8)"),
    ("pix3d_deob_gouraud_raster(xC >> 16, xA >> 16, colourC >> 7, colourA >> 7, g_pix3d_deob_pixels, yB, 0)",
     "pix3d_deob_texture_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeA >> 8)"),
    ("pix3d_deob_gouraud_raster(xB >> 16, xA >> 16, colourB >> 7, colourA >> 7, g_pix3d_deob_pixels, yC, 0)",
     "pix3d_deob_texture_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeA >> 8)"),
    ("pix3d_deob_gouraud_raster(xB >> 16, xC >> 16, colourB >> 7, colourC >> 7, g_pix3d_deob_pixels, yC, 0)",
     "pix3d_deob_texture_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeC >> 8)"),
    ("pix3d_deob_gouraud_raster(xA >> 16, xB >> 16, colourA >> 7, colourB >> 7, g_pix3d_deob_pixels, yC, 0)",
     "pix3d_deob_texture_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeB >> 8)"),
    ("pix3d_deob_gouraud_raster(xC >> 16, xB >> 16, colourC >> 7, colourB >> 7, g_pix3d_deob_pixels, yC, 0)",
     "pix3d_deob_texture_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeB >> 8)"),
    ("pix3d_deob_gouraud_raster(xA >> 16, xC >> 16, colourA >> 7, colourC >> 7, g_pix3d_deob_pixels, yC, 0)",
     "pix3d_deob_texture_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeC >> 8)"),
    ("pix3d_deob_gouraud_raster(xC >> 16, xA >> 16, colourC >> 7, colourA >> 7, g_pix3d_deob_pixels, yC, 0)",
     "pix3d_deob_texture_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeA >> 8)"),
]

for old, new in calls:
    if old not in tri:
        print("MISSING call pattern:", old[:80])
    tri = tri.replace(old, new)

if "pix3d_deob_gouraud_raster" in tri:
    raise SystemExit("gouraud_raster calls remain")

tri = tri.replace("colourStep", "shadeStep")
tri = tri.replace("colourA", "shadeA")
tri = tri.replace("colourB", "shadeB")
tri = tri.replace("colourC", "shadeC")

tri = tri.replace("shadeA <<= 15;", "shadeA <<= 16;")
tri = tri.replace("shadeB <<= 15;", "shadeB <<= 16;")
tri = tri.replace("shadeC <<= 15;", "shadeC <<= 16;")

tri = tri.replace("shadeA >> 8 >> 8", "shadeA >> 8")
tri = tri.replace("shadeB >> 8 >> 8", "shadeB >> 8")
tri = tri.replace("shadeC >> 8 >> 8", "shadeC >> 8")

# UVW row step after each y += width (Pix3D)
UV = """                u += uStepVertical;
                v += vStepVertical;
                w += wStepVertical;
                u |= 0;
                v |= 0;
                w |= 0;
"""
for yvar in ("yA", "yB", "yC"):
    tri = tri.replace(
        f"                            {yvar} += g_pix3d_deob_width;\n                        }}",
        f"                            {yvar} += g_pix3d_deob_width;\n"
        + UV.replace("                ", "                            ")
        + "                        }",
    )
    tri = tri.replace(
        f"                        {yvar} += g_pix3d_deob_width;\n                }}",
        f"                        {yvar} += g_pix3d_deob_width;\n"
        + UV.replace("                ", "                        ")
        + "                }",
    )
    tri = tri.replace(
        f"                    {yvar} += g_pix3d_deob_width;\n            }}",
        f"                    {yvar} += g_pix3d_deob_width;\n"
        + UV.replace("                ", "                    ")
        + "            }",
    )

# dy blocks: after shade clamp blocks, before if((yA...
DY_PATTERNS = [
    (
        "            if( (yA != yB && xStepAC < xStepAB) || (yA == yB && xStepAC > xStepBC) )",
        "            {\n                int dy = yA - g_pix3d_deob_origin_y;\n                u += uStepVertical * dy;\n                v += vStepVertical * dy;\n                w += wStepVertical * dy;\n                u |= 0;\n                v |= 0;\n                w |= 0;\n            }\n\n            if( (yA != yB && xStepAC < xStepAB) || (yA == yB && xStepAC > xStepBC) )",
        1,
    ),
]

# Pix3D first branch (yB<yC): dy after yB clamp, before if xStep — only first occurrence
needle = "            if( yB < 0 )\n            {\n                xB -= xStepBC * yB;\n                shadeB -= shadeStepBC * yB;\n                yB = 0;\n            }\n\n            if( (yA != yB && xStepAC < xStepAB)"
if needle in tri:
    tri = tri.replace(
        needle,
        "            if( yB < 0 )\n            {\n                xB -= xStepBC * yB;\n                shadeB -= shadeStepBC * yB;\n                yB = 0;\n            }\n\n            {\n                int dy = yA - g_pix3d_deob_origin_y;\n                u += uStepVertical * dy;\n                v += vStepVertical * dy;\n                w += wStepVertical * dy;\n                u |= 0;\n                v |= 0;\n                w |= 0;\n            }\n\n            if( (yA != yB && xStepAC < xStepAB)",
        1,
    )

# Second big branch (xB=xA else): dy = yA - originY after yC clamp
needle2 = "            if( yC < 0 )\n            {\n                xC -= xStepBC * yC;\n                shadeC -= shadeStepBC * yC;\n                yC = 0;\n            }\n\n            if( (yA == yC || xStepAC >= xStepAB)"
if needle2 not in tri:
    needle2 = "            if( yC < 0 )\n            {\n                xC -= xStepBC * yC;\n                shadeC -= shadeStepBC * yC;\n                yC = 0;\n            }\n\n            if( (yA != yC && xStepAC < xStepAB)"
tri = tri.replace(
    "            if( yC < 0 )\n            {\n                xC -= xStepBC * yC;\n                shadeC -= shadeStepBC * yC;\n                yC = 0;\n            }\n\n            if( (yA != yC && xStepAC < xStepAB)",
    "            if( yC < 0 )\n            {\n                xC -= xStepBC * yC;\n                shadeC -= shadeStepBC * yC;\n                yC = 0;\n            }\n\n            {\n                int dy = yA - g_pix3d_deob_origin_y;\n                u += uStepVertical * dy;\n                v += vStepVertical * dy;\n                w += wStepVertical * dy;\n                u |= 0;\n                v |= 0;\n                w |= 0;\n            }\n\n            if( (yA != yC && xStepAC < xStepAB)",
    1,
)

# Pix3D uses different condition: (yA === yC || xStepAC >= xStepAB) && (yA !== yC || xStepBC <= xStepAB)
tri = tri.replace(
    "            if( (yA != yC && xStepAC < xStepAB) || (yA == yC && xStepBC > xStepAB) )",
    "            if( (yA == yC || xStepAC >= xStepAB) && (yA != yC || xStepBC <= xStepAB) )",
    1,
)

# yB<=yC branch: dy = yB - originY after yC clamp (yC < yA case)
tri = tri.replace(
    "            if( yC < 0 )\n            {\n                xC -= xStepAC * yC;\n                shadeC -= shadeStepAC * yC;\n                yC = 0;\n            }\n\n            if( (yB != yC && xStepAB < xStepBC)",
    "            if( yC < 0 )\n            {\n                xC -= xStepAC * yC;\n                shadeC -= shadeStepAC * yC;\n                yC = 0;\n            }\n\n            {\n                int dy = yB - g_pix3d_deob_origin_y;\n                u += uStepVertical * dy;\n                v += vStepVertical * dy;\n                w += wStepVertical * dy;\n                u |= 0;\n                v |= 0;\n                w |= 0;\n            }\n\n            if( (yB != yC && xStepAB < xStepBC)",
    1,
)

# xC=xB branch dy = yB - originY
tri = tri.replace(
    "            if( yA < 0 )\n            {\n                xA -= xStepAC * yA;\n                shadeA -= shadeStepAC * yA;\n                yA = 0;\n            }\n\n            yC -= yA;",
    "            if( yA < 0 )\n            {\n                xA -= xStepAC * yA;\n                shadeA -= shadeStepAC * yA;\n                yA = 0;\n            }\n\n            {\n                int dy = yB - g_pix3d_deob_origin_y;\n                u += uStepVertical * dy;\n                v += vStepVertical * dy;\n                w += wStepVertical * dy;\n                u |= 0;\n                v |= 0;\n                w |= 0;\n            }\n\n            yC -= yA;",
    1,
)

# yC branch top: dy = yC - originY after yA clamp (yA < yB)
tri = tri.replace(
    "            if( yA < 0 )\n            {\n                xA -= xStepAB * yA;\n                shadeA -= shadeStepAB * yA;\n                yA = 0;\n            }\n\n            yB -= yA;",
    "            if( yA < 0 )\n            {\n                xA -= xStepAB * yA;\n                shadeA -= shadeStepAB * yA;\n                yA = 0;\n            }\n\n            {\n                int dy = yC - g_pix3d_deob_origin_y;\n                u += uStepVertical * dy;\n                v += vStepVertical * dy;\n                w += wStepVertical * dy;\n                u |= 0;\n                v |= 0;\n                w |= 0;\n            }\n\n            yB -= yA;",
    1,
)

# else xA=xC: dy = yC - originY after yB clamp
tri = tri.replace(
    "            if( yB < 0 )\n            {\n                xB -= xStepAB * yB;\n                shadeB -= shadeStepAB * yB;\n                yB = 0;\n            }\n\n            yA -= yB;",
    "            if( yB < 0 )\n            {\n                xB -= xStepAB * yB;\n                shadeB -= shadeStepAB * yB;\n                yB = 0;\n            }\n\n            {\n                int dy = yC - g_pix3d_deob_origin_y;\n                u += uStepVertical * dy;\n                v += vStepVertical * dy;\n                w += wStepVertical * dy;\n                u |= 0;\n                v |= 0;\n                w |= 0;\n            }\n\n            yA -= yB;",
    1,
)

tri = tri[tri.index("static inline void\npix3d_deob_texture_triangle") :]

OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(RASTER_C + "\n" + tri)
print("Wrote", OUT, "len", len(tri))
