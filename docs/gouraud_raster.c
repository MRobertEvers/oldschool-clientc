#include <stdbool.h>
#include <stdint.h>

#define WIDTH 800
#define HEIGHT 800

extern "C" int g_hsl16_to_rgb_table[65536];

void gouraudRaster(int x0, int x1, int color0, int color1, int* dst, int offset, int length);

void
gouraudTriangle(
    int* pixels, int xA, int xB, int xC, int yA, int yB, int yC, int colorA, int colorB, int colorC)
{
    int sxa = xA;
    int sxb = xB;
    int sxc = xC;
    int dxAB = xB - xA;
    int dyAB = yB - yA;
    int dxAC = xC - xA;
    int dyAC = yC - yA;

    int xStepAB = 0;
    int colorStepAB = 0;
    if( yB != yA )
    {
        xStepAB = (dxAB << 16) / dyAB;
        colorStepAB = ((colorB - colorA) << 15) / dyAB;
    }

    int xStepBC = 0;
    int colorStepBC = 0;
    if( yC != yB )
    {
        xStepBC = ((xC - xB) << 16) / (yC - yB);
        colorStepBC = ((colorC - colorB) << 15) / (yC - yB);
    }

    int xStepAC = 0;
    int colorStepAC = 0;
    if( yC != yA )
    {
        xStepAC = ((xA - xC) << 16) / (yA - yC);
        colorStepAC = ((colorA - colorC) << 15) / (yA - yC);
    }

    // this won't change any rendering, saves not wasting time "drawing" an invalid triangle
    int triangleArea = (dxAB * dyAC) - (dyAB * dxAC);
    if( triangleArea == 0 )
    {
        return;
    }

    if( yA <= yB && yA <= yC )
    {
        if( yA < 800 )
        {
            if( yB > 800 )
            {
                yB = 800;
            }

            if( yC > 800 )
            {
                yC = 800;
            }

            if( yB < yC )
            {
                xC = xA <<= 16;
                colorC = colorA <<= 15;
                if( yA < 0 )
                {
                    xC -= xStepAC * yA;
                    xA -= xStepAB * yA;
                    colorC -= colorStepAC * yA;
                    colorA -= colorStepAB * yA;
                    yA = 0;
                }

                xB <<= 16;
                colorB <<= 15;
                if( yB < 0 )
                {
                    xB -= xStepBC * yB;
                    colorB -= colorStepBC * yB;
                    yB = 0;
                }

                if( (yA != yB && xStepAC < xStepAB) || (yA == yB && xStepAC > xStepBC) )
                {
                    yC -= yB;
                    yB -= yA;
                    yA = 800 * yA;

                    while( --yB >= 0 )
                    {
                        gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7, pixels, yA, 0);
                        xC += xStepAC;
                        xA += xStepAB;
                        colorC += colorStepAC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                    while( --yC >= 0 )
                    {
                        gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7, pixels, yA, 0);
                        xC += xStepAC;
                        xB += xStepBC;
                        colorC += colorStepAC;
                        colorB += colorStepBC;
                        yA += 800;
                    }
                }
                else
                {
                    yC -= yB;
                    yB -= yA;
                    yA = 800 * yA;

                    while( --yB >= 0 )
                    {
                        gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7, pixels, yA, 0);
                        xC += xStepAC;
                        xA += xStepAB;
                        colorC += colorStepAC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                    while( --yC >= 0 )
                    {
                        gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7, pixels, yA, 0);
                        xC += xStepAC;
                        xB += xStepBC;
                        colorC += colorStepAC;
                        colorB += colorStepBC;
                        yA += 800;
                    }
                }
            }
            else
            {
                xB = xA <<= 16;
                colorB = colorA <<= 15;
                if( yA < 0 )
                {
                    xB -= xStepAC * yA;
                    xA -= xStepAB * yA;
                    colorB -= colorStepAC * yA;
                    colorA -= colorStepAB * yA;
                    yA = 0;
                }

                xC <<= 16;
                colorC <<= 15;
                if( yC < 0 )
                {
                    xC -= xStepBC * yC;
                    colorC -= colorStepBC * yC;
                    yC = 0;
                }

                if( (yA != yC && xStepAC < xStepAB) || (yA == yC && xStepBC > xStepAB) )
                {
                    yB -= yC;
                    yC -= yA;
                    yA = 800 * yA;

                    while( --yC >= 0 )
                    {
                        gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7, pixels, yA, 0);
                        xB += xStepAC;
                        xA += xStepAB;
                        colorB += colorStepAC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                    while( --yB >= 0 )
                    {
                        gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7, pixels, yA, 0);
                        xC += xStepBC;
                        xA += xStepAB;
                        colorC += colorStepBC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                }
                else
                {
                    yB -= yC;
                    yC -= yA;
                    yA = 800 * yA;

                    while( --yC >= 0 )
                    {
                        gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7, pixels, yA, 0);
                        xB += xStepAC;
                        xA += xStepAB;
                        colorB += colorStepAC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                    while( --yB >= 0 )
                    {
                        gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7, pixels, yA, 0);
                        xC += xStepBC;
                        xA += xStepAB;
                        colorC += colorStepBC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                }
            }
        }
    }
    else if( yB <= yC )
    {
        if( yB < 800 )
        {
            if( yC > 800 )
            {
                yC = 800;
            }

            if( yA > 800 )
            {
                yA = 800;
            }

            if( yC < yA )
            {
                xA = xB <<= 16;
                colorA = colorB <<= 15;
                if( yB < 0 )
                {
                    xA -= xStepAB * yB;
                    xB -= xStepBC * yB;
                    colorA -= colorStepAB * yB;
                    colorB -= colorStepBC * yB;
                    yB = 0;
                }

                xC <<= 16;
                colorC <<= 15;
                if( yC < 0 )
                {
                    xC -= xStepAC * yC;
                    colorC -= colorStepAC * yC;
                    yC = 0;
                }

                if( (yB != yC && xStepAB < xStepBC) || (yB == yC && xStepAB > xStepAC) )
                {
                    yA -= yC;
                    yC -= yB;
                    yB = 800 * yB;

                    while( --yC >= 0 )
                    {
                        gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7, pixels, yB, 0);
                        xA += xStepAB;
                        xB += xStepBC;
                        colorA += colorStepAB;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                    while( --yA >= 0 )
                    {
                        gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7, pixels, yB, 0);
                        xA += xStepAB;
                        xC += xStepAC;
                        colorA += colorStepAB;
                        colorC += colorStepAC;
                        yB += 800;
                    }
                }
                else
                {
                    yA -= yC;
                    yC -= yB;
                    yB = 800 * yB;

                    while( --yC >= 0 )
                    {
                        gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7, pixels, yB, 0);
                        xA += xStepAB;
                        xB += xStepBC;
                        colorA += colorStepAB;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                    while( --yA >= 0 )
                    {
                        gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7, pixels, yB, 0);
                        xA += xStepAB;
                        xC += xStepAC;
                        colorA += colorStepAB;
                        colorC += colorStepAC;
                        yB += 800;
                    }
                }
            }
            else
            {
                xC = xB <<= 16;
                colorC = colorB <<= 15;
                if( yB < 0 )
                {
                    xC -= xStepAB * yB;
                    xB -= xStepBC * yB;
                    colorC -= colorStepAB * yB;
                    colorB -= colorStepBC * yB;
                    yB = 0;
                }

                xA <<= 16;
                colorA <<= 15;
                if( yA < 0 )
                {
                    xA -= xStepAC * yA;
                    colorA -= colorStepAC * yA;
                    yA = 0;
                }

                if( xStepAB < xStepBC )
                {
                    yC -= yA;
                    yA -= yB;
                    yB = 800 * yB;

                    while( --yA >= 0 )
                    {
                        gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7, pixels, yB, 0);
                        xC += xStepAB;
                        xB += xStepBC;
                        colorC += colorStepAB;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                    while( --yC >= 0 )
                    {
                        gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7, pixels, yB, 0);
                        xA += xStepAC;
                        xB += xStepBC;
                        colorA += colorStepAC;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                }
                else
                {
                    yC -= yA;
                    yA -= yB;
                    yB = 800 * yB;

                    while( --yA >= 0 )
                    {
                        gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7, pixels, yB, 0);
                        xC += xStepAB;
                        xB += xStepBC;
                        colorC += colorStepAB;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                    while( --yC >= 0 )
                    {
                        gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7, pixels, yB, 0);
                        xA += xStepAC;
                        xB += xStepBC;
                        colorA += colorStepAC;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                }
            }
        }
    }
    else if( yC < 800 )
    {
        if( yA > 800 )
        {
            yA = 800;
        }

        if( yB > 800 )
        {
            yB = 800;
        }

        if( yA < yB )
        {
            xB = xC <<= 16;
            colorB = colorC <<= 15;
            if( yC < 0 )
            {
                xB -= xStepBC * yC;
                xC -= xStepAC * yC;
                colorB -= colorStepBC * yC;
                colorC -= colorStepAC * yC;
                yC = 0;
            }

            xA <<= 16;
            colorA <<= 15;
            if( yA < 0 )
            {
                xA -= xStepAB * yA;
                colorA -= colorStepAB * yA;
                yA = 0;
            }

            if( xStepBC < xStepAC )
            {
                yB -= yA;
                yA -= yC;
                yC = 800 * yC;

                while( --yA >= 0 )
                {
                    gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7, pixels, yC, 0);
                    xB += xStepBC;
                    xC += xStepAC;
                    colorB += colorStepBC;
                    colorC += colorStepAC;
                    yC += 800;
                }
                while( --yB >= 0 )
                {
                    gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7, pixels, yC, 0);
                    xB += xStepBC;
                    xA += xStepAB;
                    colorB += colorStepBC;
                    colorA += colorStepAB;
                    yC += 800;
                }
            }
            else
            {
                yB -= yA;
                yA -= yC;
                yC = 800 * yC;

                while( --yA >= 0 )
                {
                    gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7, pixels, yC, 0);
                    xB += xStepBC;
                    xC += xStepAC;
                    colorB += colorStepBC;
                    colorC += colorStepAC;
                    yC += 800;
                }
                while( --yB >= 0 )
                {
                    gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7, pixels, yC, 0);
                    xB += xStepBC;
                    xA += xStepAB;
                    colorB += colorStepBC;
                    colorA += colorStepAB;
                    yC += 800;
                }
            }
        }
        else
        {
            xA = xC <<= 16;
            colorA = colorC <<= 15;
            if( yC < 0 )
            {
                xA -= xStepBC * yC;
                xC -= xStepAC * yC;
                colorA -= colorStepBC * yC;
                colorC -= colorStepAC * yC;
                yC = 0;
            }

            xB <<= 16;
            colorB <<= 15;
            if( yB < 0 )
            {
                xB -= xStepAB * yB;
                colorB -= colorStepAB * yB;
                yB = 0;
            }

            if( xStepBC < xStepAC )
            {
                yA -= yB;
                yB -= yC;
                yC = 800 * yC;

                while( --yB >= 0 )
                {
                    gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7, pixels, yC, 0);
                    xA += xStepBC;
                    xC += xStepAC;
                    colorA += colorStepBC;
                    colorC += colorStepAC;
                    yC += 800;
                }
                while( --yA >= 0 )
                {
                    gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7, pixels, yC, 0);
                    xB += xStepAB;
                    xC += xStepAC;
                    colorB += colorStepAB;
                    colorC += colorStepAC;
                    yC += 800;
                }
            }
            else
            {
                yA -= yB;
                yB -= yC;
                yC = 800 * yC;

                while( --yB >= 0 )
                {
                    gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7, pixels, yC, 0);
                    xA += xStepBC;
                    xC += xStepAC;
                    colorA += colorStepBC;
                    colorC += colorStepAC;
                    yC += 800;
                }
                while( --yA >= 0 )
                {
                    gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7, pixels, yC, 0);
                    xB += xStepAB;
                    xC += xStepAC;
                    colorB += colorStepAB;
                    colorC += colorStepAC;
                    yC += 800;
                }
            }
        }
    }
}

void
gouraudRaster(int x0, int x1, int color0, int color1, int* dst, int offset, int length)
{
    int rgb;

    if( true )
    {
        int colorStep;

        if( x0 < 0 || x1 < 0 || x0 >= 800 || x1 >= 800 )
        {
            if( x1 - x0 > 3 )
            {
                colorStep = (color1 - color0) / (x1 - x0);
            }
            else
            {
                colorStep = 0;
            }

            if( x1 > 800 )
            {
                x1 = 800;
            }

            if( x0 < 0 )
            {
                color0 -= x0 * colorStep;
                x0 = 0;
            }

            if( x0 >= x1 )
            {
                return;
            }

            offset += x0;
            length = (x1 - x0) >> 2;
            colorStep <<= 2;
        }
        else if( x0 < x1 )
        {
            offset += x0;
            length = (x1 - x0) >> 2;

            if( length > 0 )
            {
                colorStep = (color1 - color0) / length;
            }
            else
            {
                colorStep = 0;
            }
        }
        else
        {
            return;
        }

        if( true )
        {
            while( --length >= 0 )
            {
                rgb = g_hsl16_to_rgb_table[color0 >> 8];
                color0 += colorStep;

                dst[offset++] = rgb;
                dst[offset++] = rgb;
                dst[offset++] = rgb;
                dst[offset++] = rgb;
            }

            length = (x1 - x0) & 0x3;
            if( length > 0 )
            {
                rgb = g_hsl16_to_rgb_table[color0 >> 8];

                while( --length >= 0 )
                {
                    dst[offset++] = rgb;
                }
            }
        }
    }
    else if( x0 < x1 )
    {
        int colorStep = (color1 - color0) / (x1 - x0);

        if( true )
        {
            if( x1 > 800 )
            {
                x1 = 800;
            }

            if( x0 < 0 )
            {
                color0 -= x0 * colorStep;
                x0 = 0;
            }

            if( x0 >= x1 )
            {
                return;
            }
        }

        offset += x0;
        length = x1 - x0;

        if( true )
        {
            while( --length >= 0 )
            {
                dst[offset++] = g_hsl16_to_rgb_table[color0 >> 8];
                color0 += colorStep;
            }
        }
    }
}
