#ifndef TEXTURE_DEOB2_U_C
#define TEXTURE_DEOB2_U_C

#include "graphics/raster/texture/texture.deob.u.c"

/* Assumes yA <= yB <= yC (screen-space). Perspective u/v/w and strides are precomputed by the wrapper. */
static inline void
pix3d_deob2_texture_triangle_ordered(
    int xA,
    int xB,
    int xC,
    int yA,
    int yB,
    int yC,
    int shadeA,
    int shadeB,
    int shadeC,
    int u,
    int v,
    int w,
    int uStride,
    int vStride,
    int wStride,
    int uStepVertical,
    int vStepVertical,
    int wStepVertical,
    int* texels)
{
    if( yA >= g_pix3d_deob_clip_max_y )
        return;

    if( yB > g_pix3d_deob_clip_max_y )
        yB = g_pix3d_deob_clip_max_y;

    if( yC > g_pix3d_deob_clip_max_y )
        yC = g_pix3d_deob_clip_max_y;

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
            int rowsTop = yB - yA;
            int rowsBot = yC - yB;
            int rowOff = g_pix3d_deob_scanline[yA];

            while( rowsTop-- > 0 )
            {
                pix3d_deob_texture_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, rowOff, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeA >> 8);
                xC += xStepAC;
                xA += xStepAB;
                shadeC += shadeStepAC;
                shadeA += shadeStepAB;
                rowOff += g_pix3d_deob_width;
            }

            while( rowsBot-- > 0 )
            {
                pix3d_deob_texture_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, rowOff, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeB >> 8);
                xC += xStepAC;
                xB += xStepBC;
                shadeC += shadeStepAC;
                shadeB += shadeStepBC;
                rowOff += g_pix3d_deob_width;
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
            int rowsTop = yB - yA;
            int rowsBot = yC - yB;
            int rowOff = g_pix3d_deob_scanline[yA];

            while( rowsTop-- > 0 )
            {
                pix3d_deob_texture_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, rowOff, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeC >> 8);
                xC += xStepAC;
                xA += xStepAB;
                shadeC += shadeStepAC;
                shadeA += shadeStepAB;
                rowOff += g_pix3d_deob_width;
            }

            while( rowsBot-- > 0 )
            {
                pix3d_deob_texture_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, rowOff, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeC >> 8);
                xC += xStepAC;
                xB += xStepBC;
                shadeC += shadeStepAC;
                shadeB += shadeStepBC;
                rowOff += g_pix3d_deob_width;
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
        /* yB == yC: flat bottom (same as pix3d_deob_texture_triangle when yA is min and yB >= yC). */
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
            int rowsFirst = yC - yA;
            int rowsSecond = yB - yC;
            int rowOff = g_pix3d_deob_scanline[yA];

            while( rowsFirst-- > 0 )
            {
                pix3d_deob_texture_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, rowOff, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeB >> 8, shadeA >> 8);
                xB += xStepAC;
                xA += xStepAB;
                shadeB += shadeStepAC;
                shadeA += shadeStepAB;
                rowOff += g_pix3d_deob_width;
            }

            while( rowsSecond-- > 0 )
            {
                pix3d_deob_texture_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, rowOff, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeC >> 8, shadeA >> 8);
                xC += xStepBC;
                xA += xStepAB;
                shadeC += shadeStepBC;
                shadeA += shadeStepAB;
                rowOff += g_pix3d_deob_width;
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
            int rowsFirst = yC - yA;
            int rowsSecond = yB - yC;
            int rowOff = g_pix3d_deob_scanline[yA];

            while( rowsFirst-- > 0 )
            {
                pix3d_deob_texture_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, rowOff, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeB >> 8);
                xB += xStepAC;
                xA += xStepAB;
                shadeB += shadeStepAC;
                shadeA += shadeStepAB;
                rowOff += g_pix3d_deob_width;
            }

            while( rowsSecond-- > 0 )
            {
                pix3d_deob_texture_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, rowOff, texels, 0, 0, u, v, w, uStride, vStride, wStride, shadeA >> 8, shadeC >> 8);
                xC += xStepBC;
                xA += xStepAB;
                shadeC += shadeStepBC;
                shadeA += shadeStepAB;
                rowOff += g_pix3d_deob_width;
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

    if( yA <= yB && yA <= yC )
    {
        if( yB <= yC )
        {
            if( yA >= g_pix3d_deob_clip_max_y )
                return;

            pix3d_deob2_texture_triangle_ordered(
                xA,
                xB,
                xC,
                yA,
                yB,
                yC,
                shadeA,
                shadeB,
                shadeC,
                u,
                v,
                w,
                uStride,
                vStride,
                wStride,
                uStepVertical,
                vStepVertical,
                wStepVertical,
                texels);
        }
        else
        {
            if( yA >= g_pix3d_deob_clip_max_y )
                return;

            pix3d_deob2_texture_triangle_ordered(
                xA,
                xC,
                xB,
                yA,
                yC,
                yB,
                shadeA,
                shadeC,
                shadeB,
                u,
                v,
                w,
                uStride,
                vStride,
                wStride,
                uStepVertical,
                vStepVertical,
                wStepVertical,
                texels);
        }
    }
    else if( yB <= yC )
    {
        if( yC <= yA )
        {
            if( yB >= g_pix3d_deob_clip_max_y )
                return;

            pix3d_deob2_texture_triangle_ordered(
                xB,
                xC,
                xA,
                yB,
                yC,
                yA,
                shadeB,
                shadeC,
                shadeA,
                u,
                v,
                w,
                uStride,
                vStride,
                wStride,
                uStepVertical,
                vStepVertical,
                wStepVertical,
                texels);
        }
        else
        {
            if( yB >= g_pix3d_deob_clip_max_y )
                return;

            pix3d_deob2_texture_triangle_ordered(
                xB,
                xA,
                xC,
                yB,
                yA,
                yC,
                shadeB,
                shadeA,
                shadeC,
                u,
                v,
                w,
                uStride,
                vStride,
                wStride,
                uStepVertical,
                vStepVertical,
                wStepVertical,
                texels);
        }
    }
    else
    {
        if( yA <= yB )
        {
            if( yC >= g_pix3d_deob_clip_max_y )
                return;

            pix3d_deob2_texture_triangle_ordered(
                xC,
                xA,
                xB,
                yC,
                yA,
                yB,
                shadeC,
                shadeA,
                shadeB,
                u,
                v,
                w,
                uStride,
                vStride,
                wStride,
                uStepVertical,
                vStepVertical,
                wStepVertical,
                texels);
        }
        else
        {
            if( yC >= g_pix3d_deob_clip_max_y )
                return;

            pix3d_deob2_texture_triangle_ordered(
                xC,
                xB,
                xA,
                yC,
                yB,
                yA,
                shadeC,
                shadeB,
                shadeA,
                u,
                v,
                w,
                uStride,
                vStride,
                wStride,
                uStepVertical,
                vStepVertical,
                wStepVertical,
                texels);
        }
    }
}

#endif /* TEXTURE_DEOB2_U_C */
