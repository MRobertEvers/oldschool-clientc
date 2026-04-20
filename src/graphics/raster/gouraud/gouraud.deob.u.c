#ifndef GOURAUD_DEOB_U_C
#define GOURAUD_DEOB_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/deob/pix3d_deob_state.h"
#include "graphics/shared_tables.h"

#include <stdint.h>

extern int g_reciprocal15[4096];

/* Port of Pix3D.ts gouraudRaster + gouraudTriangle (deobfuscated reference). */

static inline void
pix3d_deob_gouraud_raster(int xA, int xB, int colourA, int colourB, int* RESTRICT dst, int off, int len)
{
    int rgb;

    if( g_pix3d_deob_low_detail )
    {
        int colourStep;

        if( g_pix3d_deob_hclip )
        {
            if( xB - xA > 3 )
                colourStep = ((colourB - colourA) / (xB - xA)) | 0;
            else
                colourStep = 0;

            if( xB > g_pix3d_deob_size_x )
                xB = g_pix3d_deob_size_x;

            if( xA < 0 )
            {
                colourA -= xA * colourStep;
                xA = 0;
            }

            if( xA >= xB )
                return;

            off += xA;
            len = (xB - xA) >> 2;
            colourStep <<= 2;
        }
        else if( xA < xB )
        {
            off += xA;
            len = (xB - xA) >> 2;

            if( len > 0 )
                colourStep = ((colourB - colourA) * g_reciprocal15[len]) >> 15;
            else
                colourStep = 0;
        }
        else
            return;

        if( g_pix3d_deob_trans == 0 )
        {
            while( 1 )
            {
                len--;

                if( len < 0 )
                {
                    len = (xB - xA) & 0x3;

                    if( len > 0 )
                    {
                        rgb = g_hsl16_to_rgb_table[colourA >> 8];

                        do
                        {
                            dst[off++] = rgb;
                            len--;
                        } while( len > 0 );

                        return;
                    }

                    break;
                }

                rgb = g_hsl16_to_rgb_table[colourA >> 8];
                colourA += colourStep;
                dst[off++] = rgb;
                dst[off++] = rgb;
                dst[off++] = rgb;
                dst[off++] = rgb;
            }
        }
        else
        {
            int alpha = g_pix3d_deob_trans;
            int invAlpha = 256 - g_pix3d_deob_trans;

            while( 1 )
            {
                len--;

                if( len < 0 )
                {
                    len = (xB - xA) & 0x3;

                    if( len > 0 )
                    {
                        rgb = g_hsl16_to_rgb_table[colourA >> 8];
                        rgb = ((((rgb & 0xff00ff) * invAlpha) >> 8) & 0xff00ff)
                            + ((((rgb & 0xff00) * invAlpha) >> 8) & 0xff00);

                        do
                        {
                            dst[off++] = rgb
                                + ((((dst[off] & 0xff00ff) * alpha) >> 8) & 0xff00ff)
                                + ((((dst[off] & 0xff00) * alpha) >> 8) & 0xff00);
                            len--;
                        } while( len > 0 );
                    }

                    break;
                }

                rgb = g_hsl16_to_rgb_table[colourA >> 8];
                colourA += colourStep;
                rgb = ((((rgb & 0xff00ff) * invAlpha) >> 8) & 0xff00ff)
                    + ((((rgb & 0xff00) * invAlpha) >> 8) & 0xff00);

                dst[off++] = rgb
                    + ((((dst[off] & 0xff00ff) * alpha) >> 8) & 0xff00ff)
                    + ((((dst[off] & 0xff00) * alpha) >> 8) & 0xff00);
                dst[off++] = rgb
                    + ((((dst[off] & 0xff00ff) * alpha) >> 8) & 0xff00ff)
                    + ((((dst[off] & 0xff00) * alpha) >> 8) & 0xff00);
                dst[off++] = rgb
                    + ((((dst[off] & 0xff00ff) * alpha) >> 8) & 0xff00ff)
                    + ((((dst[off] & 0xff00) * alpha) >> 8) & 0xff00);
                dst[off++] = rgb
                    + ((((dst[off] & 0xff00ff) * alpha) >> 8) & 0xff00ff)
                    + ((((dst[off] & 0xff00) * alpha) >> 8) & 0xff00);
            }
        }
    }
    else if( xA < xB )
    {
        int colourStep = ((colourB - colourA) / (xB - xA)) | 0;

        if( g_pix3d_deob_hclip )
        {
            if( xB > g_pix3d_deob_size_x )
                xB = g_pix3d_deob_size_x;

            if( xA < 0 )
            {
                colourA -= xA * colourStep;
                xA = 0;
            }

            if( xA >= xB )
                return;
        }

        off += xA;
        len = xB - xA;

        if( g_pix3d_deob_trans == 0 )
        {
            do
            {
                dst[off++] = g_hsl16_to_rgb_table[colourA >> 8];
                colourA += colourStep;
                len--;
            } while( len > 0 );
        }
        else
        {
            int alpha = g_pix3d_deob_trans;
            int invAlpha = 256 - g_pix3d_deob_trans;

            do
            {
                rgb = g_hsl16_to_rgb_table[colourA >> 8];
                colourA += colourStep;
                rgb = ((((rgb & 0xff00ff) * invAlpha) >> 8) & 0xff00ff)
                    + ((((rgb & 0xff00) * invAlpha) >> 8) & 0xff00);

                dst[off++] = rgb
                    + ((((dst[off] & 0xff00ff) * alpha) >> 8) & 0xff00ff)
                    + ((((dst[off] & 0xff00) * alpha) >> 8) & 0xff00);
                len--;
            } while( len > 0 );
        }
    }
}

static inline void
pix3d_deob_gouraud_triangle(
    int xA,
    int xB,
    int xC,
    int yA,
    int yB,
    int yC,
    int colourA,
    int colourB,
    int colourC)
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

    int colourStepAB = 0;
    if( yB != yA )
        colourStepAB = (((colourB - colourA) << 15) / (yB - yA)) | 0;

    int colourStepBC = 0;
    if( yC != yB )
        colourStepBC = (((colourC - colourB) << 15) / (yC - yB)) | 0;

    int colourStepAC = 0;
    if( yC != yA )
        colourStepAC = (((colourA - colourC) << 15) / (yA - yC)) | 0;

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
            colourA <<= 15;
            colourC = colourA;

            if( yA < 0 )
            {
                xC -= xStepAC * yA;
                xA -= xStepAB * yA;
                colourC -= colourStepAC * yA;
                colourA -= colourStepAB * yA;
                yA = 0;
            }

            xB <<= 16;
            colourB <<= 15;

            if( yB < 0 )
            {
                xB -= xStepBC * yB;
                colourB -= colourStepBC * yB;
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

                            pix3d_deob_gouraud_raster(xC >> 16, xB >> 16, colourC >> 7, colourB >> 7, g_pix3d_deob_pixels, yA, 0);
                            xC += xStepAC;
                            xB += xStepBC;
                            colourC += colourStepAC;
                            colourB += colourStepBC;
                            yA += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xC >> 16, xA >> 16, colourC >> 7, colourA >> 7, g_pix3d_deob_pixels, yA, 0);
                    xC += xStepAC;
                    xA += xStepAB;
                    colourC += colourStepAC;
                    colourA += colourStepAB;
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

                            pix3d_deob_gouraud_raster(xB >> 16, xC >> 16, colourB >> 7, colourC >> 7, g_pix3d_deob_pixels, yA, 0);
                            xC += xStepAC;
                            xB += xStepBC;
                            colourC += colourStepAC;
                            colourB += colourStepBC;
                            yA += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xA >> 16, xC >> 16, colourA >> 7, colourC >> 7, g_pix3d_deob_pixels, yA, 0);
                    xC += xStepAC;
                    xA += xStepAB;
                    colourC += colourStepAC;
                    colourA += colourStepAB;
                    yA += g_pix3d_deob_width;
                }
            }
        }
        else
        {
            xA <<= 16;
            xB = xA;
            colourA <<= 15;
            colourB = colourA;

            if( yA < 0 )
            {
                xB -= xStepAC * yA;
                xA -= xStepAB * yA;
                colourB -= colourStepAC * yA;
                colourA -= colourStepAB * yA;
                yA = 0;
            }

            xC <<= 16;
            colourC <<= 15;

            if( yC < 0 )
            {
                xC -= xStepBC * yC;
                colourC -= colourStepBC * yC;
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

                            pix3d_deob_gouraud_raster(xC >> 16, xA >> 16, colourC >> 7, colourA >> 7, g_pix3d_deob_pixels, yA, 0);
                            xC += xStepBC;
                            xA += xStepAB;
                            colourC += colourStepBC;
                            colourA += colourStepAB;
                            yA += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xB >> 16, xA >> 16, colourB >> 7, colourA >> 7, g_pix3d_deob_pixels, yA, 0);
                    xB += xStepAC;
                    xA += xStepAB;
                    colourB += colourStepAC;
                    colourA += colourStepAB;
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

                            pix3d_deob_gouraud_raster(xA >> 16, xC >> 16, colourA >> 7, colourC >> 7, g_pix3d_deob_pixels, yA, 0);
                            xC += xStepBC;
                            xA += xStepAB;
                            colourC += colourStepBC;
                            colourA += colourStepAB;
                            yA += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xA >> 16, xB >> 16, colourA >> 7, colourB >> 7, g_pix3d_deob_pixels, yA, 0);
                    xB += xStepAC;
                    xA += xStepAB;
                    colourB += colourStepAC;
                    colourA += colourStepAB;
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
            colourB <<= 15;
            colourA = colourB;

            if( yB < 0 )
            {
                xA -= xStepAB * yB;
                xB -= xStepBC * yB;
                colourA -= colourStepAB * yB;
                colourB -= colourStepBC * yB;
                yB = 0;
            }

            xC <<= 16;
            colourC <<= 15;

            if( yC < 0 )
            {
                xC -= xStepAC * yC;
                colourC -= colourStepAC * yC;
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

                            pix3d_deob_gouraud_raster(xA >> 16, xC >> 16, colourA >> 7, colourC >> 7, g_pix3d_deob_pixels, yB, 0);
                            xA += xStepAB;
                            xC += xStepAC;
                            colourA += colourStepAB;
                            colourC += colourStepAC;
                            yB += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xA >> 16, xB >> 16, colourA >> 7, colourB >> 7, g_pix3d_deob_pixels, yB, 0);
                    xA += xStepAB;
                    xB += xStepBC;
                    colourA += colourStepAB;
                    colourB += colourStepBC;
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

                            pix3d_deob_gouraud_raster(xC >> 16, xA >> 16, colourC >> 7, colourA >> 7, g_pix3d_deob_pixels, yB, 0);
                            xA += xStepAB;
                            xC += xStepAC;
                            colourA += colourStepAB;
                            colourC += colourStepAC;
                            yB += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xB >> 16, xA >> 16, colourB >> 7, colourA >> 7, g_pix3d_deob_pixels, yB, 0);
                    xA += xStepAB;
                    xB += xStepBC;
                    colourA += colourStepAB;
                    colourB += colourStepBC;
                    yB += g_pix3d_deob_width;
                }
            }
        }
        else
        {
            xB <<= 16;
            xC = xB;
            colourB <<= 15;
            colourC = colourB;

            if( yB < 0 )
            {
                xC -= xStepAB * yB;
                xB -= xStepBC * yB;
                colourC -= colourStepAB * yB;
                colourB -= colourStepBC * yB;
                yB = 0;
            }

            xA <<= 16;
            colourA <<= 15;

            if( yA < 0 )
            {
                xA -= xStepAC * yA;
                colourA -= colourStepAC * yA;
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

                            pix3d_deob_gouraud_raster(xA >> 16, xB >> 16, colourA >> 7, colourB >> 7, g_pix3d_deob_pixels, yB, 0);
                            xA += xStepAC;
                            xB += xStepBC;
                            colourA += colourStepAC;
                            colourB += colourStepBC;
                            yB += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xC >> 16, xB >> 16, colourC >> 7, colourB >> 7, g_pix3d_deob_pixels, yB, 0);
                    xC += xStepAB;
                    xB += xStepBC;
                    colourC += colourStepAB;
                    colourB += colourStepBC;
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

                            pix3d_deob_gouraud_raster(xB >> 16, xA >> 16, colourB >> 7, colourA >> 7, g_pix3d_deob_pixels, yB, 0);
                            xA += xStepAC;
                            xB += xStepBC;
                            colourA += colourStepAC;
                            colourB += colourStepBC;
                            yB += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xB >> 16, xC >> 16, colourB >> 7, colourC >> 7, g_pix3d_deob_pixels, yB, 0);
                    xC += xStepAB;
                    xB += xStepBC;
                    colourC += colourStepAB;
                    colourB += colourStepBC;
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
            colourC <<= 15;
            colourB = colourC;

            if( yC < 0 )
            {
                xB -= xStepBC * yC;
                xC -= xStepAC * yC;
                colourB -= colourStepBC * yC;
                colourC -= colourStepAC * yC;
                yC = 0;
            }

            xA <<= 16;
            colourA <<= 15;

            if( yA < 0 )
            {
                xA -= xStepAB * yA;
                colourA -= colourStepAB * yA;
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

                            pix3d_deob_gouraud_raster(xB >> 16, xA >> 16, colourB >> 7, colourA >> 7, g_pix3d_deob_pixels, yC, 0);
                            xB += xStepBC;
                            xA += xStepAB;
                            colourB += colourStepBC;
                            colourA += colourStepAB;
                            yC += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xB >> 16, xC >> 16, colourB >> 7, colourC >> 7, g_pix3d_deob_pixels, yC, 0);
                    xB += xStepBC;
                    xC += xStepAC;
                    colourB += colourStepBC;
                    colourC += colourStepAC;
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

                            pix3d_deob_gouraud_raster(xA >> 16, xB >> 16, colourA >> 7, colourB >> 7, g_pix3d_deob_pixels, yC, 0);
                            xB += xStepBC;
                            xA += xStepAB;
                            colourB += colourStepBC;
                            colourA += colourStepAB;
                            yC += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xC >> 16, xB >> 16, colourC >> 7, colourB >> 7, g_pix3d_deob_pixels, yC, 0);
                    xB += xStepBC;
                    xC += xStepAC;
                    colourB += colourStepBC;
                    colourC += colourStepAC;
                    yC += g_pix3d_deob_width;
                }
            }
        }
        else
        {
            xC <<= 16;
            xA = xC;
            colourC <<= 15;
            colourA = colourC;

            if( yC < 0 )
            {
                xA -= xStepBC * yC;
                xC -= xStepAC * yC;
                colourA -= colourStepBC * yC;
                colourC -= colourStepAC * yC;
                yC = 0;
            }

            xB <<= 16;
            colourB <<= 15;

            if( yB < 0 )
            {
                xB -= xStepAB * yB;
                colourB -= colourStepAB * yB;
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

                            pix3d_deob_gouraud_raster(xB >> 16, xC >> 16, colourB >> 7, colourC >> 7, g_pix3d_deob_pixels, yC, 0);
                            xB += xStepAB;
                            xC += xStepAC;
                            colourB += colourStepAB;
                            colourC += colourStepAC;
                            yC += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xA >> 16, xC >> 16, colourA >> 7, colourC >> 7, g_pix3d_deob_pixels, yC, 0);
                    xA += xStepBC;
                    xC += xStepAC;
                    colourA += colourStepBC;
                    colourC += colourStepAC;
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

                            pix3d_deob_gouraud_raster(xC >> 16, xB >> 16, colourC >> 7, colourB >> 7, g_pix3d_deob_pixels, yC, 0);
                            xB += xStepAB;
                            xC += xStepAC;
                            colourB += colourStepAB;
                            colourC += colourStepAC;
                            yC += g_pix3d_deob_width;
                        }
                    }

                    pix3d_deob_gouraud_raster(xC >> 16, xA >> 16, colourC >> 7, colourA >> 7, g_pix3d_deob_pixels, yC, 0);
                    xA += xStepBC;
                    xC += xStepAC;
                    colourA += colourStepBC;
                    colourC += colourStepAC;
                    yC += g_pix3d_deob_width;
                }
            }
        }
    }
}

#endif /* GOURAUD_DEOB_U_C */
