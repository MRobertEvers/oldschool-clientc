#ifndef TEXTURE_DEOB_U_C
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


static inline void
pix3d_deob_texture_triangle(
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
    g_pix3d_deob_opaque = is_opaque;

    int verticalX = originX - txB;
    int verticalY = originY - tyB;
    int verticalZ = originZ - tzB;
    int horizontalX = txC - originX;
    int horizontalY = tyC - originY;
    int horizontalZ = tzC - originZ;
    int u = (horizontalX * originY - horizontalY * originX) << 14;
    int uStride = (horizontalY * originZ - horizontalZ * originY) << 8;
    int uStepVertical = (horizontalZ * originX - horizontalX * originZ) << 5;
    int v = (verticalX * originY - verticalY * originX) << 14;
    int vStride = (verticalY * originZ - verticalZ * originY) << 8;
    int vStepVertical = (verticalZ * originX - verticalX * originZ) << 5;
    int w = (verticalY * horizontalX - verticalX * horizontalY) << 14;
    int wStride = (verticalZ * horizontalY - verticalY * horizontalZ) << 8;
    int wStepVertical = (verticalX * horizontalZ - verticalZ * horizontalX) << 5;

    int xStepAB = 0;
    if( yB != yA )
        xStepAB = (((xB - xA) << 16) / (yB - yA)) | 0;

    int xStepBC = 0;
    if( yC != yB )
        xStepBC = (((xC - xB) << 16) / (yC - yB)) | 0;

    int xStepAC = 0;
    if( yC != yA )
        xStepAC = (((xA - xC) << 16) / (yA - yC)) | 0;

    int shadeStepAB = 0;
    if( yB != yA )
        shadeStepAB = (((shadeB - shadeA) << 15) / (yB - yA)) | 0;

    int shadeStepBC = 0;
    if( yC != yB )
        shadeStepBC = (((shadeC - shadeB) << 15) / (yC - yB)) | 0;

    int shadeStepAC = 0;
    if( yC != yA )
        shadeStepAC = (((shadeA - shadeC) << 15) / (yA - yC)) | 0;

    if( yA <= yB && yA <= yC )
    {
        if( yA >= g_pix3d_deob_clip_max_y )
            return;

        if( yB > g_pix3d_deob_clip_max_y )
            yB = g_pix3d_deob_clip_max_y;

        if( yC > g_pix3d_deob_clip_max_y )
            yC = g_pix3d_deob_clip_max_y;

        if( yB < yC )
        {
            xA <<= 16;
            xC = xA;
            shadeA <<= 16;
            shadeC = shadeA;

            if( yA < 0 )
            {
                xC -= xStepAC * yA;
                xA -= xStepAB * yA;
                shadeC -= shadeStepAC * yA;
                shadeA -= shadeStepAB * yA;
                yA = 0;
            }

            xB <<= 16;
            shadeB <<= 16;

            if( yB < 0 )
            {
                xB -= xStepBC * yB;
                shadeB -= shadeStepBC * yB;
                yB = 0;
            }

            {
                int dy = yA - g_pix3d_deob_origin_y;
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;
                u |= 0;
                v |= 0;
                w |= 0;
            }

            if( (yA != yB && xStepAC < xStepAB) || (yA == yB && xStepAC > xStepBC) )
            {
                yC -= yB;
                yB -= yA;
                yA = g_pix3d_deob_scanline[yA];

                while( 1 )
                {
                    yB--;

                    if( yB < 0 )
                    {
                        while( 1 )
                        {
                            yC--;

                            if( yC < 0 )
                                return;

                            pix3d_deob_texture_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeB >> 8);
                            xC += xStepAC;
                            xB += xStepBC;
                            shadeC += shadeStepAC;
                            shadeB += shadeStepBC;
                            yA += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeA >> 8);
                    xC += xStepAC;
                    xA += xStepAB;
                    shadeC += shadeStepAC;
                    shadeA += shadeStepAB;
                    yA += g_pix3d_deob_width;
                }
            }
            else
            {
                yC -= yB;
                yB -= yA;
                yA = g_pix3d_deob_scanline[yA];

                while( 1 )
                {
                    yB--;

                    if( yB < 0 )
                    {
                        while( 1 )
                        {
                            yC--;

                            if( yC < 0 )
                                return;

                            pix3d_deob_texture_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeC >> 8);
                            xC += xStepAC;
                            xB += xStepBC;
                            shadeC += shadeStepAC;
                            shadeB += shadeStepBC;
                            yA += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeC >> 8);
                    xC += xStepAC;
                    xA += xStepAB;
                    shadeC += shadeStepAC;
                    shadeA += shadeStepAB;
                    yA += g_pix3d_deob_width;
                }
            }
        }
        else
        {
            xA <<= 16;
            xB = xA;
            shadeA <<= 16;
            shadeB = shadeA;

            if( yA < 0 )
            {
                xB -= xStepAC * yA;
                xA -= xStepAB * yA;
                shadeB -= shadeStepAC * yA;
                shadeA -= shadeStepAB * yA;
                yA = 0;
            }

            xC <<= 16;
            shadeC <<= 16;

            if( yC < 0 )
            {
                xC -= xStepBC * yC;
                shadeC -= shadeStepBC * yC;
                yC = 0;
            }

            {
                int dy = yA - g_pix3d_deob_origin_y;
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;
                u |= 0;
                v |= 0;
                w |= 0;
            }

            if( (yA == yC || xStepAC >= xStepAB) && (yA != yC || xStepBC <= xStepAB) )
            {
                yB -= yC;
                yC -= yA;
                yA = g_pix3d_deob_scanline[yA];

                while( 1 )
                {
                    yC--;

                    if( yC < 0 )
                    {
                        while( 1 )
                        {
                            yB--;

                            if( yB < 0 )
                                return;

                            pix3d_deob_texture_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeA >> 8);
                            xC += xStepBC;
                            xA += xStepAB;
                            shadeC += shadeStepBC;
                            shadeA += shadeStepAB;
                            yA += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeA >> 8);
                    xB += xStepAC;
                    xA += xStepAB;
                    shadeB += shadeStepAC;
                    shadeA += shadeStepAB;
                    yA += g_pix3d_deob_width;
                }
            }
            else
            {
                yB -= yC;
                yC -= yA;
                yA = g_pix3d_deob_scanline[yA];

                while( 1 )
                {
                    yC--;

                    if( yC < 0 )
                    {
                        while( 1 )
                        {
                            yB--;

                            if( yB < 0 )
                                return;

                            pix3d_deob_texture_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeC >> 8);
                            xC += xStepBC;
                            xA += xStepAB;
                            shadeC += shadeStepBC;
                            shadeA += shadeStepAB;
                            yA += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yA, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeB >> 8);
                    xB += xStepAC;
                    xA += xStepAB;
                    shadeB += shadeStepAC;
                    shadeA += shadeStepAB;
                    yA += g_pix3d_deob_width;
                }
            }
        }
    }
    else if( yB <= yC )
    {
        if( yB >= g_pix3d_deob_clip_max_y )
            return;

        if( yC > g_pix3d_deob_clip_max_y )
            yC = g_pix3d_deob_clip_max_y;

        if( yA > g_pix3d_deob_clip_max_y )
            yA = g_pix3d_deob_clip_max_y;

        if( yC < yA )
        {
            xB <<= 16;
            xA = xB;
            shadeB <<= 16;
            shadeA = shadeB;

            if( yB < 0 )
            {
                xA -= xStepAB * yB;
                xB -= xStepBC * yB;
                shadeA -= shadeStepAB * yB;
                shadeB -= shadeStepBC * yB;
                yB = 0;
            }

            xC <<= 16;
            shadeC <<= 16;

            if( yC < 0 )
            {
                xC -= xStepAC * yC;
                shadeC -= shadeStepAC * yC;
                yC = 0;
            }

            {
                int dy = yB - g_pix3d_deob_origin_y;
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;
                u |= 0;
                v |= 0;
                w |= 0;
            }

            if( (yB != yC && xStepAB < xStepBC) || (yB == yC && xStepAB > xStepAC) )
            {
                yA -= yC;
                yC -= yB;
                yB = g_pix3d_deob_scanline[yB];

                while( 1 )
                {
                    yC--;

                    if( yC < 0 )
                    {
                        while( 1 )
                        {
                            yA--;

                            if( yA < 0 )
                                return;

                            pix3d_deob_texture_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeC >> 8);
                            xA += xStepAB;
                            xC += xStepAC;
                            shadeA += shadeStepAB;
                            shadeC += shadeStepAC;
                            yB += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeB >> 8);
                    xA += xStepAB;
                    xB += xStepBC;
                    shadeA += shadeStepAB;
                    shadeB += shadeStepBC;
                    yB += g_pix3d_deob_width;
                }
            }
            else
            {
                yA -= yC;
                yC -= yB;
                yB = g_pix3d_deob_scanline[yB];

                while( 1 )
                {
                    yC--;

                    if( yC < 0 )
                    {
                        while( 1 )
                        {
                            yA--;

                            if( yA < 0 )
                                return;

                            pix3d_deob_texture_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeA >> 8);
                            xA += xStepAB;
                            xC += xStepAC;
                            shadeA += shadeStepAB;
                            shadeC += shadeStepAC;
                            yB += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeA >> 8);
                    xA += xStepAB;
                    xB += xStepBC;
                    shadeA += shadeStepAB;
                    shadeB += shadeStepBC;
                    yB += g_pix3d_deob_width;
                }
            }
        }
        else
        {
            xB <<= 16;
            xC = xB;
            shadeB <<= 16;
            shadeC = shadeB;

            if( yB < 0 )
            {
                xC -= xStepAB * yB;
                xB -= xStepBC * yB;
                shadeC -= shadeStepAB * yB;
                shadeB -= shadeStepBC * yB;
                yB = 0;
            }

            xA <<= 16;
            shadeA <<= 16;

            if( yA < 0 )
            {
                xA -= xStepAC * yA;
                shadeA -= shadeStepAC * yA;
                yA = 0;
            }

            {
                int dy = yB - g_pix3d_deob_origin_y;
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;
                u |= 0;
                v |= 0;
                w |= 0;
            }

            yC -= yA;
            yA -= yB;
            yB = g_pix3d_deob_scanline[yB];

            if( xStepAB < xStepBC )
            {
                while( 1 )
                {
                    yA--;

                    if( yA < 0 )
                    {
                        while( 1 )
                        {
                            yC--;

                            if( yC < 0 )
                                return;

                            pix3d_deob_texture_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeB >> 8);
                            xA += xStepAC;
                            xB += xStepBC;
                            shadeA += shadeStepAC;
                            shadeB += shadeStepBC;
                            yB += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeB >> 8);
                    xC += xStepAB;
                    xB += xStepBC;
                    shadeC += shadeStepAB;
                    shadeB += shadeStepBC;
                    yB += g_pix3d_deob_width;
                }
            }
            else
            {
                while( 1 )
                {
                    yA--;

                    if( yA < 0 )
                    {
                        while( 1 )
                        {
                            yC--;

                            if( yC < 0 )
                                return;

                            pix3d_deob_texture_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeA >> 8);
                            xA += xStepAC;
                            xB += xStepBC;
                            shadeA += shadeStepAC;
                            shadeB += shadeStepBC;
                            yB += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yB, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeC >> 8);
                    xC += xStepAB;
                    xB += xStepBC;
                    shadeC += shadeStepAB;
                    shadeB += shadeStepBC;
                    yB += g_pix3d_deob_width;
                }
            }
        }
    }
    else
    {
        if( yC >= g_pix3d_deob_clip_max_y )
            return;

        if( yA > g_pix3d_deob_clip_max_y )
            yA = g_pix3d_deob_clip_max_y;

        if( yB > g_pix3d_deob_clip_max_y )
            yB = g_pix3d_deob_clip_max_y;

        if( yA < yB )
        {
            xC <<= 16;
            xB = xC;
            shadeC <<= 16;
            shadeB = shadeC;

            if( yC < 0 )
            {
                xB -= xStepBC * yC;
                xC -= xStepAC * yC;
                shadeB -= shadeStepBC * yC;
                shadeC -= shadeStepAC * yC;
                yC = 0;
            }

            xA <<= 16;
            shadeA <<= 16;

            if( yA < 0 )
            {
                xA -= xStepAB * yA;
                shadeA -= shadeStepAB * yA;
                yA = 0;
            }

            {
                int dy = yC - g_pix3d_deob_origin_y;
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;
                u |= 0;
                v |= 0;
                w |= 0;
            }

            yB -= yA;
            yA -= yC;
            yC = g_pix3d_deob_scanline[yC];

            if( xStepBC < xStepAC )
            {
                while( 1 )
                {
                    yA--;

                    if( yA < 0 )
                    {
                        while( 1 )
                        {
                            yB--;

                            if( yB < 0 )
                                return;

                            pix3d_deob_texture_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeA >> 8);
                            xB += xStepBC;
                            xA += xStepAB;
                            shadeB += shadeStepBC;
                            shadeA += shadeStepAB;
                            yC += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeC >> 8);
                    xB += xStepBC;
                    xC += xStepAC;
                    shadeB += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += g_pix3d_deob_width;
                }
            }
            else
            {
                while( 1 )
                {
                    yA--;

                    if( yA < 0 )
                    {
                        while( 1 )
                        {
                            yB--;

                            if( yB < 0 )
                                return;

                            pix3d_deob_texture_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeB >> 8);
                            xB += xStepBC;
                            xA += xStepAB;
                            shadeB += shadeStepBC;
                            shadeA += shadeStepAB;
                            yC += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeB >> 8);
                    xB += xStepBC;
                    xC += xStepAC;
                    shadeB += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += g_pix3d_deob_width;
                }
            }
        }
        else
        {
            xC <<= 16;
            xA = xC;
            shadeC <<= 16;
            shadeA = shadeC;

            if( yC < 0 )
            {
                xA -= xStepBC * yC;
                xC -= xStepAC * yC;
                shadeA -= shadeStepBC * yC;
                shadeC -= shadeStepAC * yC;
                yC = 0;
            }

            xB <<= 16;
            shadeB <<= 16;

            if( yB < 0 )
            {
                xB -= xStepAB * yB;
                shadeB -= shadeStepAB * yB;
                yB = 0;
            }

            {
                int dy = yC - g_pix3d_deob_origin_y;
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;
                u |= 0;
                v |= 0;
                w |= 0;
            }

            yA -= yB;
            yB -= yC;
            yC = g_pix3d_deob_scanline[yC];

            if( xStepBC < xStepAC )
            {
                while( 1 )
                {
                    yB--;

                    if( yB < 0 )
                    {
                        while( 1 )
                        {
                            yA--;

                            if( yA < 0 )
                                return;

                            pix3d_deob_texture_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeC >> 8);
                            xB += xStepAB;
                            xC += xStepAC;
                            shadeB += shadeStepAB;
                            shadeC += shadeStepAC;
                            yC += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeC >> 8);
                    xA += xStepBC;
                    xC += xStepAC;
                    shadeA += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += g_pix3d_deob_width;
                }
            }
            else
            {
                while( 1 )
                {
                    yB--;

                    if( yB < 0 )
                    {
                        while( 1 )
                        {
                            yA--;

                            if( yA < 0 )
                                return;

                            pix3d_deob_texture_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeB >> 8);
                            xB += xStepAB;
                            xC += xStepAC;
                            shadeB += shadeStepAB;
                            shadeC += shadeStepAC;
                            yC += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                            u |= 0;
                            v |= 0;
                            w |= 0;
                        }
                    }

                    pix3d_deob_texture_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yC, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeA >> 8);
                    xA += xStepBC;
                    xC += xStepAC;
                    shadeA += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += g_pix3d_deob_width;
                }
            }
        }
    }
}

#endif /* TEXTURE_DEOB_U_C */
