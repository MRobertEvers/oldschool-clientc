#ifndef FLAT_DEOB_U_C
#define FLAT_DEOB_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/deob/pix3d_deob_state.h"

#include <stdint.h>

/* Port of Pix3D.ts flatRaster + flatTriangle (deobfuscated reference). */

static inline void
pix3d_deob_flat_raster(int xA, int xB, int* RESTRICT dst, int off, int colour)
{
    if( g_pix3d_deob_hclip )
    {
        if( xB > g_pix3d_deob_size_x )
            xB = g_pix3d_deob_size_x;

        if( xA < 0 )
            xA = 0;
    }

    if( xA >= xB )
        return;

    off += xA;
    int len = (xB - xA) >> 2;

    if( g_pix3d_deob_trans == 0 )
    {
        while( 1 )
        {
            len--;

            if( len < 0 )
            {
                len = (xB - xA) & 0x3;

                while( 1 )
                {
                    len--;

                    if( len < 0 )
                        return;

                    dst[off++] = colour;
                }
            }

            dst[off++] = colour;
            dst[off++] = colour;
            dst[off++] = colour;
            dst[off++] = colour;
        }
    }
    else
    {
        int alpha = g_pix3d_deob_trans;
        int invAlpha = 256 - g_pix3d_deob_trans;
        colour = ((((colour & 0xff00ff) * invAlpha) >> 8) & 0xff00ff)
            + ((((colour & 0xff00) * invAlpha) >> 8) & 0xff00);

        while( 1 )
        {
            len--;

            if( len < 0 )
            {
                len = (xB - xA) & 0x3;

                while( 1 )
                {
                    len--;

                    if( len < 0 )
                        return;

                    dst[off++] = colour
                        + ((((dst[off] & 0xff00ff) * alpha) >> 8) & 0xff00ff)
                        + ((((dst[off] & 0xff00) * alpha) >> 8) & 0xff00);
                }
            }

            dst[off++] = colour
                + ((((dst[off] & 0xff00ff) * alpha) >> 8) & 0xff00ff)
                + ((((dst[off] & 0xff00) * alpha) >> 8) & 0xff00);
            dst[off++] = colour
                + ((((dst[off] & 0xff00ff) * alpha) >> 8) & 0xff00ff)
                + ((((dst[off] & 0xff00) * alpha) >> 8) & 0xff00);
            dst[off++] = colour
                + ((((dst[off] & 0xff00ff) * alpha) >> 8) & 0xff00ff)
                + ((((dst[off] & 0xff00) * alpha) >> 8) & 0xff00);
            dst[off++] = colour
                + ((((dst[off] & 0xff00ff) * alpha) >> 8) & 0xff00ff)
                + ((((dst[off] & 0xff00) * alpha) >> 8) & 0xff00);
        }
    }
}

static inline void
pix3d_deob_flat_triangle(
    int xA,
    int xB,
    int xC,
    int yA,
    int yB,
    int yC,
    int colour)
{
    int xStepAB = 0;
    if( yB != yA )
        xStepAB = (((xB - xA) << 16) / (yB - yA)) | 0;

    int xStepBC = 0;
    if( yC != yB )
        xStepBC = (((xC - xB) << 16) / (yC - yB)) | 0;

    int xStepAC = 0;
    if( yC != yA )
        xStepAC = (((xA - xC) << 16) / (yA - yC)) | 0;

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
            xC = xA <<= 16;

            if( yA < 0 )
            {
                xC -= xStepAC * yA;
                xA -= xStepAB * yA;
                yA = 0;
            }

            xB <<= 16;

            if( yB < 0 )
            {
                xB -= xStepBC * yB;
                yB = 0;
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

                            pix3d_deob_flat_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yA, colour);
                            xC += xStepAC;
                            xB += xStepBC;
                            yA += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yA, colour);
                    xC += xStepAC;
                    xA += xStepAB;
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

                            pix3d_deob_flat_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yA, colour);
                            xC += xStepAC;
                            xB += xStepBC;
                            yA += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yA, colour);
                    xC += xStepAC;
                    xA += xStepAB;
                    yA += g_pix3d_deob_width;
                }
            }
        }
        else
        {
            xB = xA <<= 16;

            if( yA < 0 )
            {
                xB -= xStepAC * yA;
                xA -= xStepAB * yA;
                yA = 0;
            }

            xC <<= 16;

            if( yC < 0 )
            {
                xC -= xStepBC * yC;
                yC = 0;
            }

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

                            pix3d_deob_flat_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yA, colour);
                            xC += xStepBC;
                            xA += xStepAB;
                            yA += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yA, colour);
                    xB += xStepAC;
                    xA += xStepAB;
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

                            pix3d_deob_flat_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yA, colour);
                            xC += xStepBC;
                            xA += xStepAB;
                            yA += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yA, colour);
                    xB += xStepAC;
                    xA += xStepAB;
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
            xA = xB <<= 16;

            if( yB < 0 )
            {
                xA -= xStepAB * yB;
                xB -= xStepBC * yB;
                yB = 0;
            }

            xC <<= 16;

            if( yC < 0 )
            {
                xC -= xStepAC * yC;
                yC = 0;
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

                            pix3d_deob_flat_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yB, colour);
                            xA += xStepAB;
                            xC += xStepAC;
                            yB += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yB, colour);
                    xA += xStepAB;
                    xB += xStepBC;
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

                            pix3d_deob_flat_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yB, colour);
                            xA += xStepAB;
                            xC += xStepAC;
                            yB += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yB, colour);
                    xA += xStepAB;
                    xB += xStepBC;
                    yB += g_pix3d_deob_width;
                }
            }
        }
        else
        {
            xC = xB <<= 16;

            if( yB < 0 )
            {
                xC -= xStepAB * yB;
                xB -= xStepBC * yB;
                yB = 0;
            }

            xA <<= 16;

            if( yA < 0 )
            {
                xA -= xStepAC * yA;
                yA = 0;
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

                            pix3d_deob_flat_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yB, colour);
                            xA += xStepAC;
                            xB += xStepBC;
                            yB += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yB, colour);
                    xC += xStepAB;
                    xB += xStepBC;
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

                            pix3d_deob_flat_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yB, colour);
                            xA += xStepAC;
                            xB += xStepBC;
                            yB += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yB, colour);
                    xC += xStepAB;
                    xB += xStepBC;
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
            xB = xC <<= 16;

            if( yC < 0 )
            {
                xB -= xStepBC * yC;
                xC -= xStepAC * yC;
                yC = 0;
            }

            xA <<= 16;

            if( yA < 0 )
            {
                xA -= xStepAB * yA;
                yA = 0;
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

                            pix3d_deob_flat_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yC, colour);
                            xB += xStepBC;
                            xA += xStepAB;
                            yC += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yC, colour);
                    xB += xStepBC;
                    xC += xStepAC;
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

                            pix3d_deob_flat_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yC, colour);
                            xB += xStepBC;
                            xA += xStepAB;
                            yC += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yC, colour);
                    xB += xStepBC;
                    xC += xStepAC;
                    yC += g_pix3d_deob_width;
                }
            }
        }
        else
        {
            xA = xC <<= 16;

            if( yC < 0 )
            {
                xA -= xStepBC * yC;
                xC -= xStepAC * yC;
                yC = 0;
            }

            xB <<= 16;

            if( yB < 0 )
            {
                xB -= xStepAB * yB;
                yB = 0;
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

                            pix3d_deob_flat_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yC, colour);
                            xB += xStepAB;
                            xC += xStepAC;
                            yC += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yC, colour);
                    xA += xStepBC;
                    xC += xStepAC;
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

                            pix3d_deob_flat_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yC, colour);
                            xB += xStepAB;
                            xC += xStepAC;
                            yC += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_flat_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yC, colour);
                    xA += xStepBC;
                    xC += xStepAC;
                    yC += g_pix3d_deob_width;
                }
            }
        }
    }
}

#endif /* FLAT_DEOB_U_C */
