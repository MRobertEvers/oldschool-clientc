#ifndef TEXTURE_DEOB_U_C
#define TEXTURE_DEOB_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/deob/pix3d_deob_state.h"
#include "graphics/shade.h"

#ifndef DEOB_TEXTURE_DEBUG_V_MODULO
#define DEOB_TEXTURE_DEBUG_V_MODULO 0 /* 1: wrap U/V columns & rows with %128 in texel fetch */
#endif

extern int g_reciprocal15[4096];

/*
 * UV indexing follows Pix3D/Java 128×128 packed coordinates (U subtexel + V row mask).
 * Shading does not: Pix3D may use texel>>>shadeShift and shade-related U tweaks for a
 * multi-shade texture grid; this engine keeps one ARGB texture and applies lighting via
 * shade_blend() (multiply), matching tex.span / lerp8 — not bit-shifted texel samples.
 */
/* U column 0..127 after subtexel shift (clamped); V row from curV & 0x3f80. With
 * DEOB_TEXTURE_DEBUG_V_MODULO: repeat UV via %128 on both. */
static inline int
pix3d_deob_texture_texel_fetch_128(
    int* texels,
    int curU,
    int curV)
{
// #define DEOB_TEXTURE_DEBUG_V_MODULO 1
#if DEOB_TEXTURE_DEBUG_V_MODULO
    int col = curU >> 7;
    col = ((col % 128) + 128) % 128;
    int uc = col;
    int row = curV >> 7;
    row = ((row % 128) + 128) % 128;
    int vr = row << 7;
#else
    int uc = curU >> 7;
    if( uc < 0 )
        uc = 0;
    else if( uc > 127 )
        uc = 127;
    int vr = (int)(((unsigned int)curV) & 0x3f80u);
#endif
    return texels[uc + vr];
}

/* Interpolated span shade → 0..255 for shade_blend; not Pix3D shadeShift on texel bits. */
static inline int
pix3d_deob_texture_shade_u8(int shadeA_ish)
{
    int s = shadeA_ish >> 16;
    if( s > 255 )
        s = 255;
    if( s < 0 )
        s = 0;
    return s;
}

/* Homogeneous barycentric setup: Pix3D.ts textureTriangle ~1632–1650 (vertical = origin−B,
 * horizontal = C−origin). TS parity spot-check: same int products and shifts; after each major
 * branch’s `dy` adjust, u/v/w match TS before the first textureRaster call when inputs match
 * Model.ts (fixed P/M/N, face order a,b,c). */
static inline void
pix3d_deob_texture_barycentric_from_corners(
    int originX,
    int originY,
    int originZ,
    int txB,
    int tyB,
    int tzB,
    int txC,
    int tyC,
    int tzC,
    int* u,
    int* v,
    int* w,
    int* uStride,
    int* vStride,
    int* wStride,
    int* uStepVertical,
    int* vStepVertical,
    int* wStepVertical)
{
    int verticalX = originX - txB;
    int verticalY = originY - tyB;
    int verticalZ = originZ - tzB;
    int horizontalX = txC - originX;
    int horizontalY = tyC - originY;
    int horizontalZ = tzC - originZ;
    int t;
    t = (horizontalX * originY - horizontalY * originX) << 14;
    *u = t;
    t = (horizontalY * originZ - horizontalZ * originY) << 8;
    *uStride = t;
    t = (horizontalZ * originX - horizontalX * originZ) << 5;
    *uStepVertical = t;
    t = (verticalX * originY - verticalY * originX) << 14;
    *v = t;
    t = (verticalY * originZ - verticalZ * originY) << 8;
    *vStride = t;
    t = (verticalZ * originX - verticalX * originZ) << 5;
    *vStepVertical = t;
    t = (verticalY * horizontalX - verticalX * horizontalY) << 14;
    *w = t;
    t = (verticalZ * horizontalY - verticalY * horizontalZ) << 8;
    *wStride = t;
    t = (verticalX * horizontalZ - verticalZ * horizontalX) << 5;
    *wStepVertical = t;
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
    if( !texels )
        return;

    /* Pix3D.textureRaster returns if xA >= xB (no swap). Callers are written so each active
     * edge usually passes increasing x; when xA > xB we normalize to (left,right) with matching
     * shades so UV/shade match the geometric span. */
    if( xA == xB )
        return;
    if( xA > xB )
    {
        return;
    }

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
            shadeStrides = ((shadeB - shadeA) * g_reciprocal16[strides]) >> 6;
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

    /* Pix3D.textureRaster (!lowMem): never return when w>>14==0; only skip u/w divide (Pix3D.ts
     * ~2700–2731). Early return here dropped whole spans and diverged from TS on slivers. */
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

    if( g_pix3d_deob_opaque )
    {
        while( strides-- > 0 )
        {
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                dst[off++] = shade_blend(texel, shade);
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                dst[off++] = shade_blend(texel, shade);
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                dst[off++] = shade_blend(texel, shade);
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                dst[off++] = shade_blend(texel, shade);
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                dst[off++] = shade_blend(texel, shade);
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                dst[off++] = shade_blend(texel, shade);
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                dst[off++] = shade_blend(texel, shade);
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                dst[off++] = shade_blend(texel, shade);
                curU = nextU;
                curV = nextV;
            }

            u += uStride;
            v += vStride;
            w += wStride;
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
            shadeA += shadeStrides;
        }

        strides = (xB - xA) & 0x7;

        while( strides-- > 0 )
        {
            int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
            int shade = pix3d_deob_texture_shade_u8(shadeA);
            dst[off++] = shade_blend(texel, shade);
            curU += stepU;
            curV += stepV;
        }
    }
    else
    {
        while( strides-- > 0 && texels )
        {
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                if( texel != 0 )
                    dst[off] = shade_blend(texel, shade);
                off++;
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                if( texel != 0 )
                    dst[off] = shade_blend(texel, shade);
                off++;
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                if( texel != 0 )
                    dst[off] = shade_blend(texel, shade);
                off++;
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                if( texel != 0 )
                    dst[off] = shade_blend(texel, shade);
                off++;
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                if( texel != 0 )
                    dst[off] = shade_blend(texel, shade);
                off++;
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                if( texel != 0 )
                    dst[off] = shade_blend(texel, shade);
                off++;
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                if( texel != 0 )
                    dst[off] = shade_blend(texel, shade);
                off++;
                curU += stepU;
                curV += stepV;
            }
            {
                int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
                int shade = pix3d_deob_texture_shade_u8(shadeA);
                if( texel != 0 )
                    dst[off] = shade_blend(texel, shade);
                off++;
                curU = nextU;
                curV = nextV;
            }

            u += uStride;
            v += vStride;
            w += wStride;
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
            shadeA += shadeStrides;
        }

        strides = (xB - xA) & 0x7;

        while( strides-- > 0 && texels )
        {
            int texel = pix3d_deob_texture_texel_fetch_128(texels, curU, curV);
            int shade = pix3d_deob_texture_shade_u8(shadeA);
            if( texel != 0 )
                dst[off] = shade_blend(texel, shade);
            off++;
            curU += stepU;
            curV += stepV;
        }
    }
}

/*
 * Triangle walk matches Pix3D.textureTriangle (Client-TS/src/dash3d/Pix3D.ts ~1619–2412): same
 * xStep and shadeStep edge slopes, same y-order branches. Flat-bottom under yA-min uses
 * `if( !((yA==yC||xStepAC>=xStepAB)&&(yA!=yC||xStepBC<=xStepAB)) )` here — De Morgan of TS line
 * ~1833 — with if/else bodies swapped vs TS text; control flow is equivalent.
 */
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

    int u, v, w;
    int uStride, vStride, wStride;
    int uStepVertical, vStepVertical, wStepVertical;
    pix3d_deob_texture_barycentric_from_corners(
        originX,
        originY,
        originZ,
        txB,
        tyB,
        tzB,
        txC,
        tyC,
        tzC,
        &u,
        &v,
        &w,
        &uStride,
        &vStride,
        &wStride,
        &uStepVertical,
        &vStepVertical,
        &wStepVertical);

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
        shadeStepAB = (((shadeB - shadeA) << 16) / (yB - yA)) | 0;

    int shadeStepBC = 0;
    if( yC != yB )
        shadeStepBC = (((shadeC - shadeB) << 16) / (yC - yB)) | 0;

    int shadeStepAC = 0;
    if( yC != yA )
        shadeStepAC = (((shadeA - shadeC) << 16) / (yA - yC)) | 0;

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

                            pix3d_deob_texture_raster(
                                xC >> 16,
                                xB >> 16,
                                g_pix3d_deob_pixels,
                                yA,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeC >> 8,
                                shadeB >> 8);
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

                    pix3d_deob_texture_raster(
                        xC >> 16,
                        xA >> 16,
                        g_pix3d_deob_pixels,
                        yA,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeC >> 8,
                        shadeA >> 8);
                    xC += xStepAC;
                    xA += xStepAB;
                    shadeC += shadeStepAC;
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

                            pix3d_deob_texture_raster(
                                xB >> 16,
                                xC >> 16,
                                g_pix3d_deob_pixels,
                                yA,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeB >> 8,
                                shadeC >> 8);
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

                    pix3d_deob_texture_raster(
                        xA >> 16,
                        xC >> 16,
                        g_pix3d_deob_pixels,
                        yA,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeA >> 8,
                        shadeC >> 8);
                    xC += xStepAC;
                    xA += xStepAB;
                    shadeC += shadeStepAC;
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

            /* TS ~1833: if T = ((yA==yC||xStepAC>=xStepAB)&&(yA!=yC||xStepBC<=xStepAB)); C uses !T
             * here with bodies swapped vs the TS listing — same edges as TS else/if. */
            if( (yA != yC && xStepAC < xStepAB) || (yA == yC && xStepBC > xStepAB) )
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

                            pix3d_deob_texture_raster(
                                xC >> 16,
                                xA >> 16,
                                g_pix3d_deob_pixels,
                                yA,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeC >> 8,
                                shadeA >> 8);
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

                    pix3d_deob_texture_raster(
                        xB >> 16,
                        xA >> 16,
                        g_pix3d_deob_pixels,
                        yA,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeB >> 8,
                        shadeA >> 8);
                    xB += xStepAC;
                    xA += xStepAB;
                    shadeB += shadeStepAC;
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

                            pix3d_deob_texture_raster(
                                xA >> 16,
                                xC >> 16,
                                g_pix3d_deob_pixels,
                                yA,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeA >> 8,
                                shadeC >> 8);
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

                    pix3d_deob_texture_raster(
                        xA >> 16,
                        xB >> 16,
                        g_pix3d_deob_pixels,
                        yA,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeA >> 8,
                        shadeB >> 8);
                    xB += xStepAC;
                    xA += xStepAB;
                    shadeB += shadeStepAC;
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

                            pix3d_deob_texture_raster(
                                xA >> 16,
                                xC >> 16,
                                g_pix3d_deob_pixels,
                                yB,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeA >> 8,
                                shadeC >> 8);
                            xA += xStepAB;
                            xC += xStepAC;
                            shadeA += shadeStepAB;
                            shadeC += shadeStepAC;
                            yB += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                        }
                    }

                    pix3d_deob_texture_raster(
                        xA >> 16,
                        xB >> 16,
                        g_pix3d_deob_pixels,
                        yB,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeA >> 8,
                        shadeB >> 8);
                    xA += xStepAB;
                    xB += xStepBC;
                    shadeA += shadeStepAB;
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

                            pix3d_deob_texture_raster(
                                xC >> 16,
                                xA >> 16,
                                g_pix3d_deob_pixels,
                                yB,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeC >> 8,
                                shadeA >> 8);
                            xA += xStepAB;
                            xC += xStepAC;
                            shadeA += shadeStepAB;
                            shadeC += shadeStepAC;
                            yB += g_pix3d_deob_width;
                            u += uStepVertical;
                            v += vStepVertical;
                            w += wStepVertical;
                        }
                    }

                    pix3d_deob_texture_raster(
                        xB >> 16,
                        xA >> 16,
                        g_pix3d_deob_pixels,
                        yB,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeB >> 8,
                        shadeA >> 8);
                    xA += xStepAB;
                    xB += xStepBC;
                    shadeA += shadeStepAB;
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

                            pix3d_deob_texture_raster(
                                xA >> 16,
                                xB >> 16,
                                g_pix3d_deob_pixels,
                                yB,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeA >> 8,
                                shadeB >> 8);
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

                    pix3d_deob_texture_raster(
                        xC >> 16,
                        xB >> 16,
                        g_pix3d_deob_pixels,
                        yB,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeC >> 8,
                        shadeB >> 8);
                    xC += xStepAB;
                    xB += xStepBC;
                    shadeC += shadeStepAB;
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

                            pix3d_deob_texture_raster(
                                xB >> 16,
                                xA >> 16,
                                g_pix3d_deob_pixels,
                                yB,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeB >> 8,
                                shadeA >> 8);
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

                    pix3d_deob_texture_raster(
                        xB >> 16,
                        xC >> 16,
                        g_pix3d_deob_pixels,
                        yB,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeB >> 8,
                        shadeC >> 8);
                    xC += xStepAB;
                    xB += xStepBC;
                    shadeC += shadeStepAB;
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

                            pix3d_deob_texture_raster(
                                xB >> 16,
                                xA >> 16,
                                g_pix3d_deob_pixels,
                                yC,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeB >> 8,
                                shadeA >> 8);
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

                    pix3d_deob_texture_raster(
                        xB >> 16,
                        xC >> 16,
                        g_pix3d_deob_pixels,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeB >> 8,
                        shadeC >> 8);
                    xB += xStepBC;
                    xC += xStepAC;
                    shadeB += shadeStepBC;
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

                            pix3d_deob_texture_raster(
                                xA >> 16,
                                xB >> 16,
                                g_pix3d_deob_pixels,
                                yC,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeA >> 8,
                                shadeB >> 8);
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

                    pix3d_deob_texture_raster(
                        xC >> 16,
                        xB >> 16,
                        g_pix3d_deob_pixels,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeC >> 8,
                        shadeB >> 8);
                    xB += xStepBC;
                    xC += xStepAC;
                    shadeB += shadeStepBC;
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

                            pix3d_deob_texture_raster(
                                xB >> 16,
                                xC >> 16,
                                g_pix3d_deob_pixels,
                                yC,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeB >> 8,
                                shadeC >> 8);
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

                    pix3d_deob_texture_raster(
                        xA >> 16,
                        xC >> 16,
                        g_pix3d_deob_pixels,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeA >> 8,
                        shadeC >> 8);
                    xA += xStepBC;
                    xC += xStepAC;
                    shadeA += shadeStepBC;
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

                            pix3d_deob_texture_raster(
                                xC >> 16,
                                xB >> 16,
                                g_pix3d_deob_pixels,
                                yC,
                                texels,
                                0,
                                0,
                                u,
                                v,
                                w,
                                uStride,
                                vStride,
                                wStride,
                                shadeC >> 8,
                                shadeB >> 8);
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

                    pix3d_deob_texture_raster(
                        xC >> 16,
                        xA >> 16,
                        g_pix3d_deob_pixels,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeC >> 8,
                        shadeA >> 8);
                    xA += xStepBC;
                    xC += xStepAC;
                    shadeA += shadeStepBC;
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
        }
    }
}

#endif /* TEXTURE_DEOB_U_C */
