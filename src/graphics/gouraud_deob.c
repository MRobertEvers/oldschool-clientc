#include "gouraud_deob.h"

#include "screen.h"

#include <stdbool.h>

#define BOTTOM_Y (SCREEN_HEIGHT)
#define WIDTH (SCREEN_WIDTH)

extern int g_hsl16_to_rgb_table[65536];

static void gouraud_deob_draw_scanline(
    int* var0, int var1, int var2, int var3, int var4, int var5, int var6, int var7);

// public static void drawGouraudTriangle(int y1, int y2, int y3, int x1, int x2, int x3, int
// hsl1, int hsl2, int hsl3) {
//         int var9 = x2 - x1;
//         int var10 = y2 - y1;
//         int var11 = x3 - x1;
//         int var12 = y3 - y1;
//         int var13 = hsl2 - hsl1;
//         int var14 = hsl3 - hsl1;
//         int var15;
//         if (y3 != y2) {
//             var15 = (x3 - x2 << 14) / (y3 - y2);
//         } else {
//             var15 = 0;
//         }

//         int var16;
//         if (y1 != y2) {
//             var16 = (var9 << 14) / var10;
//         } else {
//             var16 = 0;
//         }

//         int var17;
//         if (y1 != y3) {
//             var17 = (var11 << 14) / var12;
//         } else {
//             var17 = 0;
//         }

//         int var18 = var9 * var12 - var11 * var10;
//         if (var18 != 0) {
//             int var19 = (var13 * var12 - var14 * var10 << 8) / var18;
//             int var20 = (var14 * var9 - var13 * var11 << 8) / var18;
//             if (y1 <= y2 && y1 <= y3) {
//                 if (y1 < BOTTOM_Y) {
//                     if (y2 > BOTTOM_Y) {
//                         y2 = BOTTOM_Y;
//                     }

//                     if (y3 > BOTTOM_Y) {
//                         y3 = BOTTOM_Y;
//                     }

//                     hsl1 = var19 + ((hsl1 << 8) - x1 * var19);
//                     if (y2 < y3) {
//                         x3 = x1 <<= 14;
//                         if (y1 < 0) {
//                             x3 -= y1 * var17;
//                             x1 -= y1 * var16;
//                             hsl1 -= y1 * var20;
//                             y1 = 0;
//                         }

//                         x2 <<= 14;
//                         if (y2 < 0) {
//                             x2 -= var15 * y2;
//                             y2 = 0;
//                         }

//                         if ((y1 == y2 || var17 >= var16) && (y1 != y2 || var17 <= var15)) {
//                             y3 -= y2;
//                             y2 -= y1;
//                             y1 = scanOffsets[y1];

//                             while (true) {
//                                 --y2;
//                                 if (y2 < 0) {
//                                     while (true) {
//                                         --y3;
//                                         if (y3 < 0) {
//                                             return;
//                                         }

//                                         gouraud_deob_draw_scanline(pixels, y1, 0, 0, x2 >>
//                                         14, x3 >> 14, hsl1, var19); x3 += var17; x2 += var15;
//                                         hsl1 += var20;
//                                         y1 += WIDTH;
//                                     }
//                                 }

//                                 gouraud_deob_draw_scanline(pixels, y1, 0, 0, x1 >> 14, x3
//                                 >> 14, hsl1, var19); x3 += var17; x1 += var16; hsl1 += var20;
//                                 y1
//                                 += WIDTH;
//                             }
//                         } else {
//                             y3 -= y2;
//                             y2 -= y1;
//                             y1 = scanOffsets[y1];

//                             while (true) {
//                                 --y2;
//                                 if (y2 < 0) {
//                                     while (true) {
//                                         --y3;
//                                         if (y3 < 0) {
//                                             return;
//                                         }

//                                         gouraud_deob_draw_scanline(pixels, y1, 0, 0, x3 >>
//                                         14, x2 >> 14, hsl1, var19); x3 += var17; x2 += var15;
//                                         hsl1 += var20;
//                                         y1 += WIDTH;
//                                     }
//                                 }

//                                 gouraud_deob_draw_scanline(pixels, y1, 0, 0, x3 >> 14, x1
//                                 >> 14, hsl1, var19); x3 += var17; x1 += var16; hsl1 += var20;
//                                 y1
//                                 += WIDTH;
//                             }
//                         }
//                     } else {
//                         x2 = x1 <<= 14;
//                         if (y1 < 0) {
//                             x2 -= y1 * var17;
//                             x1 -= y1 * var16;
//                             hsl1 -= y1 * var20;
//                             y1 = 0;
//                         }

//                         x3 <<= 14;
//                         if (y3 < 0) {
//                             x3 -= var15 * y3;
//                             y3 = 0;
//                         }

//                         if (y1 != y3 && var17 < var16 || y1 == y3 && var15 > var16) {
//                             y2 -= y3;
//                             y3 -= y1;
//                             y1 = scanOffsets[y1];

//                             while (true) {
//                                 --y3;
//                                 if (y3 < 0) {
//                                     while (true) {
//                                         --y2;
//                                         if (y2 < 0) {
//                                             return;
//                                         }

//                                         gouraud_deob_draw_scanline(pixels, y1, 0, 0, x3 >>
//                                         14, x1 >> 14, hsl1, var19); x3 += var15; x1 += var16;
//                                         hsl1 += var20;
//                                         y1 += WIDTH;
//                                     }
//                                 }

//                                 gouraud_deob_draw_scanline(pixels, y1, 0, 0, x2 >> 14, x1
//                                 >> 14, hsl1, var19); x2 += var17; x1 += var16; hsl1 += var20;
//                                 y1
//                                 += WIDTH;
//                             }
//                         } else {
//                             y2 -= y3;
//                             y3 -= y1;
//                             y1 = scanOffsets[y1];

//                             while (true) {
//                                 --y3;
//                                 if (y3 < 0) {
//                                     while (true) {
//                                         --y2;
//                                         if (y2 < 0) {
//                                             return;
//                                         }

//                                         gouraud_deob_draw_scanline(pixels, y1, 0, 0, x1 >>
//                                         14, x3 >> 14, hsl1, var19); x3 += var15; x1 += var16;
//                                         hsl1 += var20;
//                                         y1 += WIDTH;
//                                     }
//                                 }

//                                 gouraud_deob_draw_scanline(pixels, y1, 0, 0, x1 >> 14, x2
//                                 >> 14, hsl1, var19); x2 += var17; x1 += var16; hsl1 += var20;
//                                 y1
//                                 += WIDTH;
//                             }
//                         }
//                     }
//                 }
//             } else if (y2 <= y3) {
//                 if (y2 < BOTTOM_Y) {
//                     if (y3 > BOTTOM_Y) {
//                         y3 = BOTTOM_Y;
//                     }

//                     if (y1 > BOTTOM_Y) {
//                         y1 = BOTTOM_Y;
//                     }

//                     hsl2 = var19 + ((hsl2 << 8) - var19 * x2);
//                     if (y3 < y1) {
//                         x1 = x2 <<= 14;
//                         if (y2 < 0) {
//                             x1 -= var16 * y2;
//                             x2 -= var15 * y2;
//                             hsl2 -= var20 * y2;
//                             y2 = 0;
//                         }

//                         x3 <<= 14;
//                         if (y3 < 0) {
//                             x3 -= var17 * y3;
//                             y3 = 0;
//                         }

//                         if ((y3 == y2 || var16 >= var15) && (y3 != y2 || var16 <= var17)) {
//                             y1 -= y3;
//                             y3 -= y2;
//                             y2 = scanOffsets[y2];

//                             while (true) {
//                                 --y3;
//                                 if (y3 < 0) {
//                                     while (true) {
//                                         --y1;
//                                         if (y1 < 0) {
//                                             return;
//                                         }

//                                         gouraud_deob_draw_scanline(pixels, y2, 0, 0, x3 >>
//                                         14, x1 >> 14, hsl2, var19); x1 += var16; x3 += var17;
//                                         hsl2 += var20;
//                                         y2 += WIDTH;
//                                     }
//                                 }

//                                 gouraud_deob_draw_scanline(pixels, y2, 0, 0, x2 >> 14, x1
//                                 >> 14, hsl2, var19); x1 += var16; x2 += var15; hsl2 += var20;
//                                 y2
//                                 += WIDTH;
//                             }
//                         } else {
//                             y1 -= y3;
//                             y3 -= y2;
//                             y2 = scanOffsets[y2];

//                             while (true) {
//                                 --y3;
//                                 if (y3 < 0) {
//                                     while (true) {
//                                         --y1;
//                                         if (y1 < 0) {
//                                             return;
//                                         }

//                                         gouraud_deob_draw_scanline(pixels, y2, 0, 0, x1 >>
//                                         14, x3 >> 14, hsl2, var19); x1 += var16; x3 += var17;
//                                         hsl2 += var20;
//                                         y2 += WIDTH;
//                                     }
//                                 }

//                                 gouraud_deob_draw_scanline(pixels, y2, 0, 0, x1 >> 14, x2
//                                 >> 14, hsl2, var19); x1 += var16; x2 += var15; hsl2 += var20;
//                                 y2
//                                 += WIDTH;
//                             }
//                         }
//                     } else {
//                         x3 = x2 <<= 14;
//                         if (y2 < 0) {
//                             x3 -= var16 * y2;
//                             x2 -= var15 * y2;
//                             hsl2 -= var20 * y2;
//                             y2 = 0;
//                         }

//                         x1 <<= 14;
//                         if (y1 < 0) {
//                             x1 -= y1 * var17;
//                             y1 = 0;
//                         }

//                         if (var16 < var15) {
//                             y3 -= y1;
//                             y1 -= y2;
//                             y2 = scanOffsets[y2];

//                             while (true) {
//                                 --y1;
//                                 if (y1 < 0) {
//                                     while (true) {
//                                         --y3;
//                                         if (y3 < 0) {
//                                             return;
//                                         }

//                                         gouraud_deob_draw_scanline(pixels, y2, 0, 0, x1 >>
//                                         14, x2 >> 14, hsl2, var19); x1 += var17; x2 += var15;
//                                         hsl2 += var20;
//                                         y2 += WIDTH;
//                                     }
//                                 }

//                                 gouraud_deob_draw_scanline(pixels, y2, 0, 0, x3 >> 14, x2
//                                 >> 14, hsl2, var19); x3 += var16; x2 += var15; hsl2 += var20;
//                                 y2
//                                 += WIDTH;
//                             }
//                         } else {
//                             y3 -= y1;
//                             y1 -= y2;
//                             y2 = scanOffsets[y2];

//                             while (true) {
//                                 --y1;
//                                 if (y1 < 0) {
//                                     while (true) {
//                                         --y3;
//                                         if (y3 < 0) {
//                                             return;
//                                         }

//                                         gouraud_deob_draw_scanline(pixels, y2, 0, 0, x2 >>
//                                         14, x1 >> 14, hsl2, var19); x1 += var17; x2 += var15;
//                                         hsl2 += var20;
//                                         y2 += WIDTH;
//                                     }
//                                 }

//                                 gouraud_deob_draw_scanline(pixels, y2, 0, 0, x2 >> 14, x3
//                                 >> 14, hsl2, var19); x3 += var16; x2 += var15; hsl2 += var20;
//                                 y2
//                                 += WIDTH;
//                             }
//                         }
//                     }
//                 }
//             } else if (y3 < BOTTOM_Y) {
//                 if (y1 > BOTTOM_Y) {
//                     y1 = BOTTOM_Y;
//                 }

//                 if (y2 > BOTTOM_Y) {
//                     y2 = BOTTOM_Y;
//                 }

//                 hsl3 = var19 + ((hsl3 << 8) - x3 * var19);
//                 if (y1 < y2) {
//                     x2 = x3 <<= 14;
//                     if (y3 < 0) {
//                         x2 -= var15 * y3;
//                         x3 -= var17 * y3;
//                         hsl3 -= var20 * y3;
//                         y3 = 0;
//                     }

//                     x1 <<= 14;
//                     if (y1 < 0) {
//                         x1 -= y1 * var16;
//                         y1 = 0;
//                     }

//                     if (var15 < var17) {
//                         y2 -= y1;
//                         y1 -= y3;
//                         y3 = scanOffsets[y3];

//                         while (true) {
//                             --y1;
//                             if (y1 < 0) {
//                                 while (true) {
//                                     --y2;
//                                     if (y2 < 0) {
//                                         return;
//                                     }

//                                     gouraud_deob_draw_scanline(pixels, y3, 0, 0, x2 >> 14,
//                                     x1 >> 14, hsl3, var19); x2 += var15; x1 += var16; hsl3 +=
//                                     var20; y3 += WIDTH;
//                                 }
//                             }

//                             gouraud_deob_draw_scanline(pixels, y3, 0, 0, x2 >> 14, x3 >>
//                             14, hsl3, var19); x2 += var15; x3 += var17; hsl3 += var20; y3 +=
//                             WIDTH;
//                         }
//                     } else {
//                         y2 -= y1;
//                         y1 -= y3;
//                         y3 = scanOffsets[y3];

//                         while (true) {
//                             --y1;
//                             if (y1 < 0) {
//                                 while (true) {
//                                     --y2;
//                                     if (y2 < 0) {
//                                         return;
//                                     }

//                                     gouraud_deob_draw_scanline(pixels, y3, 0, 0, x1 >> 14,
//                                     x2 >> 14, hsl3, var19); x2 += var15; x1 += var16; hsl3 +=
//                                     var20; y3 += WIDTH;
//                                 }
//                             }

//                             gouraud_deob_draw_scanline(pixels, y3, 0, 0, x3 >> 14, x2 >>
//                             14, hsl3, var19); x2 += var15; x3 += var17; hsl3 += var20; y3 +=
//                             WIDTH;
//                         }
//                     }
//                 } else {
//                     x1 = x3 <<= 14;
//                     if (y3 < 0) {
//                         x1 -= var15 * y3;
//                         x3 -= var17 * y3;
//                         hsl3 -= var20 * y3;
//                         y3 = 0;
//                     }

//                     x2 <<= 14;
//                     if (y2 < 0) {
//                         x2 -= var16 * y2;
//                         y2 = 0;
//                     }

//                     if (var15 < var17) {
//                         y1 -= y2;
//                         y2 -= y3;
//                         y3 = scanOffsets[y3];

//                         while (true) {
//                             --y2;
//                             if (y2 < 0) {
//                                 while (true) {
//                                     --y1;
//                                     if (y1 < 0) {
//                                         return;
//                                     }

//                                     gouraud_deob_draw_scanline(pixels, y3, 0, 0, x2 >> 14,
//                                     x3 >> 14, hsl3, var19); x2 += var16; x3 += var17; hsl3 +=
//                                     var20; y3 += WIDTH;
//                                 }
//                             }

//                             gouraud_deob_draw_scanline(pixels, y3, 0, 0, x1 >> 14, x3 >>
//                             14, hsl3, var19); x1 += var15; x3 += var17; hsl3 += var20; y3 +=
//                             WIDTH;
//                         }
//                     } else {
//                         y1 -= y2;
//                         y2 -= y3;
//                         y3 = scanOffsets[y3];

//                         while (true) {
//                             --y2;
//                             if (y2 < 0) {
//                                 while (true) {
//                                     --y1;
//                                     if (y1 < 0) {
//                                         return;
//                                     }

//                                     gouraud_deob_draw_scanline(pixels, y3, 0, 0, x3 >> 14,
//                                     x2 >> 14, hsl3, var19); x2 += var16; x3 += var17; hsl3 +=
//                                     var20; y3 += WIDTH;
//                                 }
//                             }

//                             gouraud_deob_draw_scanline(pixels, y3, 0, 0, x3 >> 14, x1 >>
//                             14, hsl3, var19); x1 += var15; x3 += var17; hsl3 += var20; y3 +=
//                             WIDTH;
//                         }
//                     }
//                 }
//             }
//         }
//     }

// Code:
// public static void gouraud_deob_draw_scanline(int var0[], int var1, int var2, int var3, int
// var4, int var5, int var6, int var7) {
//         if (textureOutOfDrawingBounds) {
//             if (var5 > lastX) {
//                 var5 = lastX;
//             }

//             if (var4 < 0) {
//                 var4 = 0;
//             }
//         }

//         if (var4 < var5) {
//             var1 += var4;
//             var6 += var4 * var7;
//             int var8;
//             int var9;
//             int var10;
//             if (aBoolean1464) {
//                 var3 = var5 - var4 >> 2;
//                 var7 <<= 2;
//                 if (alpha == 0) {
//                     if (var3 > 0) {
//                         do {
//                             var2 = hslToRgb[var6 >> 8];
//                             var6 += var7;
//                             var0[var1++] = var2;
//                             var0[var1++] = var2;
//                             var0[var1++] = var2;
//                             var0[var1++] = var2;
//                             --var3;
//                         } while(var3 > 0);
//                     }

//                     var3 = var5 - var4 & 3;
//                     if (var3 > 0) {
//                         var2 = hslToRgb[var6 >> 8];

//                         do {
//                             var0[var1++] = var2;
//                             --var3;
//                         } while(var3 > 0);
//                     }
//                 } else {
//                     var8 = alpha;
//                     var9 = 256 - alpha;
//                     if (var3 > 0) {
//                         do {
//                             var2 = hslToRgb[var6 >> 8];
//                             var6 += var7;
//                             var2 = (var9 * (var2 & 65280) >> 8 & 65280) + (var9 * (var2 &
//                             16711935) >> 8 & 16711935); var10 = var0[var1]; var0[var1++] =
//                             ((var10 & 16711935) * var8 >> 8 & 16711935) + var2 + (var8 *
//                             (var10 & 65280) >> 8 & 65280); var10 = var0[var1]; var0[var1++] =
//                             ((var10 & 16711935) * var8 >> 8 & 16711935) + var2 + (var8 *
//                             (var10 & 65280) >> 8 & 65280); var10 = var0[var1]; var0[var1++] =
//                             ((var10 & 16711935) * var8 >> 8 & 16711935) + var2 + (var8 *
//                             (var10 & 65280) >> 8 & 65280); var10 = var0[var1]; var0[var1++] =
//                             ((var10 & 16711935) * var8 >> 8 & 16711935) + var2 + (var8 *
//                             (var10 & 65280) >> 8 & 65280);
//                             --var3;
//                         } while(var3 > 0);
//                     }

//                     var3 = var5 - var4 & 3;
//                     if (var3 > 0) {
//                         var2 = hslToRgb[var6 >> 8];
//                         var2 = (var9 * (var2 & 65280) >> 8 & 65280) + (var9 * (var2 &
//                         16711935)
//                         >> 8 & 16711935);

//                         do {
//                             var10 = var0[var1];
//                             var0[var1++] = ((var10 & 16711935) * var8 >> 8 & 16711935) + var2
//                             + (var8 * (var10 & 65280) >> 8 & 65280);
//                             --var3;
//                         } while(var3 > 0);
//                     }
//                 }

//             } else {
//                 var3 = var5 - var4;
//                 if (alpha == 0) {
//                     do {
//                         var0[var1++] = hslToRgb[var6 >> 8];
//                         var6 += var7;
//                         --var3;
//                     } while(var3 > 0);
//                 } else {
//                     var8 = alpha;
//                     var9 = 256 - alpha;

//                     do {
//                         var2 = hslToRgb[var6 >> 8];
//                         var6 += var7;
//                         var2 = (var9 * (var2 & 65280) >> 8 & 65280) + (var9 * (var2 &
//                         16711935)
//                         >> 8 & 16711935); var10 = var0[var1]; var0[var1++] = ((var10 &
//                         16711935)
//                         * var8 >> 8 & 16711935) + var2 + (var8 * (var10 & 65280) >> 8 &
//                         65280);
//                         --var3;
//                     } while(var3 > 0);
//                 }

//             }
//         }
//     }

static inline void
gouraud_deob_draw_triangle(
    int* pixels, int y1, int y2, int y3, int x1, int x2, int x3, int hsl1, int hsl2, int hsl3)
{
    int dx_AB = x2 - x1;
    int dy_AB = y2 - y1;
    int dx_AC = x3 - x1;
    int dy_AC = y3 - y1;
    int d_hsl_AB = hsl2 - hsl1;
    int d_hsl_AC = hsl3 - hsl1;
    int dxdy_BC;
    if( y3 != y2 )
    {
        dxdy_BC = ((x3 - x2) << 14) / (y3 - y2);
    }
    else
    {
        dxdy_BC = 0;
    }

    int dxdy_AB;
    if( y1 != y2 )
    {
        dxdy_AB = (dx_AB << 14) / dy_AB;
    }
    else
    {
        dxdy_AB = 0;
    }

    int dxdy_AC;
    if( y1 != y3 )
    {
        dxdy_AC = (dx_AC << 14) / dy_AC;
    }
    else
    {
        dxdy_AC = 0;
    }
    // 2d cross product to get signed area of triangle (*2)
    int var18 = dx_AB * dy_AC - dx_AC * dy_AB;
    if( var18 != 0 )
    {
        // Barycentric coordinates for color.
        // d_hsl_ab is some scalar factor of d_ab.
        // So we are implicitly converting to the hsl value
        // by using d_hsl
        int var19 = ((d_hsl_AB * dy_AC - d_hsl_AC * dy_AB) << 8) / var18;
        int var20 = ((d_hsl_AC * dx_AB - d_hsl_AB * dx_AC) << 8) / var18;
        if( y1 <= y2 && y1 <= y3 )
        {
            if( y1 < BOTTOM_Y )
            {
                if( y2 > BOTTOM_Y )
                {
                    y2 = BOTTOM_Y;
                }

                if( y3 > BOTTOM_Y )
                {
                    y3 = BOTTOM_Y;
                }

                hsl1 = var19 + ((hsl1 << 8) - x1 * var19);
                if( y2 < y3 )
                {
                    x3 = x1 <<= 14;
                    if( y1 < 0 )
                    {
                        x3 -= y1 * dxdy_AC;
                        x1 -= y1 * dxdy_AB;
                        hsl1 -= y1 * var20;
                        y1 = 0;
                    }

                    x2 <<= 14;
                    if( y2 < 0 )
                    {
                        x2 -= dxdy_BC * y2;
                        y2 = 0;
                    }

                    if( (y1 == y2 || dxdy_AC >= dxdy_AB) && (y1 != y2 || dxdy_AC <= dxdy_BC) )
                    {
                        y3 -= y2;
                        y2 -= y1;
                        y1 = WIDTH * y1;

                        while( true )
                        {
                            --y2;
                            if( y2 < 0 )
                            {
                                while( true )
                                {
                                    --y3;
                                    if( y3 < 0 )
                                    {
                                        return;
                                    }

                                    gouraud_deob_draw_scanline(
                                        pixels, y1, 0, 0, x2 >> 14, x3 >> 14, hsl1, var19);
                                    x3 += dxdy_AC;
                                    x2 += dxdy_BC;
                                    hsl1 += var20;
                                    y1 += WIDTH;
                                }
                            }

                            gouraud_deob_draw_scanline(
                                pixels, y1, 0, 0, x1 >> 14, x3 >> 14, hsl1, var19);
                            x3 += dxdy_AC;
                            x1 += dxdy_AB;
                            hsl1 += var20;
                            y1 += WIDTH;
                        }
                    }
                    else
                    {
                        y3 -= y2;
                        y2 -= y1;
                        y1 = WIDTH * y1;

                        while( true )
                        {
                            --y2;
                            if( y2 < 0 )
                            {
                                while( true )
                                {
                                    --y3;
                                    if( y3 < 0 )
                                    {
                                        return;
                                    }

                                    gouraud_deob_draw_scanline(
                                        pixels, y1, 0, 0, x3 >> 14, x2 >> 14, hsl1, var19);
                                    x3 += dxdy_AC;
                                    x2 += dxdy_BC;
                                    hsl1 += var20;
                                    y1 += WIDTH;
                                }
                            }

                            gouraud_deob_draw_scanline(
                                pixels, y1, 0, 0, x3 >> 14, x1 >> 14, hsl1, var19);
                            x3 += dxdy_AC;
                            x1 += dxdy_AB;
                            hsl1 += var20;
                            y1 += WIDTH;
                        }
                    }
                }
                else
                {
                    x2 = x1 <<= 14;
                    if( y1 < 0 )
                    {
                        x2 -= y1 * dxdy_AC;
                        x1 -= y1 * dxdy_AB;
                        hsl1 -= y1 * var20;
                        y1 = 0;
                    }

                    x3 <<= 14;
                    if( y3 < 0 )
                    {
                        x3 -= dxdy_BC * y3;
                        y3 = 0;
                    }

                    if( y1 != y3 && dxdy_AC < dxdy_AB || y1 == y3 && dxdy_BC > dxdy_AB )
                    {
                        y2 -= y3;
                        y3 -= y1;
                        y1 = WIDTH * y1;

                        while( true )
                        {
                            --y3;
                            if( y3 < 0 )
                            {
                                while( true )
                                {
                                    --y2;
                                    if( y2 < 0 )
                                    {
                                        return;
                                    }

                                    gouraud_deob_draw_scanline(
                                        pixels, y1, 0, 0, x3 >> 14, x1 >> 14, hsl1, var19);
                                    x3 += dxdy_BC;
                                    x1 += dxdy_AB;
                                    hsl1 += var20;
                                    y1 += WIDTH;
                                }
                            }

                            gouraud_deob_draw_scanline(
                                pixels, y1, 0, 0, x2 >> 14, x1 >> 14, hsl1, var19);
                            x2 += dxdy_AC;
                            x1 += dxdy_AB;
                            hsl1 += var20;
                            y1 += WIDTH;
                        }
                    }
                    else
                    {
                        y2 -= y3;
                        y3 -= y1;
                        y1 = WIDTH * y1;

                        while( true )
                        {
                            --y3;
                            if( y3 < 0 )
                            {
                                while( true )
                                {
                                    --y2;
                                    if( y2 < 0 )
                                    {
                                        return;
                                    }

                                    gouraud_deob_draw_scanline(
                                        pixels, y1, 0, 0, x1 >> 14, x3 >> 14, hsl1, var19);
                                    x3 += dxdy_BC;
                                    x1 += dxdy_AB;
                                    hsl1 += var20;
                                    y1 += WIDTH;
                                }
                            }

                            gouraud_deob_draw_scanline(
                                pixels, y1, 0, 0, x1 >> 14, x2 >> 14, hsl1, var19);
                            x2 += dxdy_AC;
                            x1 += dxdy_AB;
                            hsl1 += var20;
                            y1 += WIDTH;
                        }
                    }
                }
            }
        }
        else if( y2 <= y3 )
        {
            if( y2 < BOTTOM_Y )
            {
                if( y3 > BOTTOM_Y )
                {
                    y3 = BOTTOM_Y;
                }

                if( y1 > BOTTOM_Y )
                {
                    y1 = BOTTOM_Y;
                }

                hsl2 = var19 + ((hsl2 << 8) - var19 * x2);
                if( y3 < y1 )
                {
                    x1 = x2 <<= 14;
                    if( y2 < 0 )
                    {
                        x1 -= dxdy_AB * y2;
                        x2 -= dxdy_BC * y2;
                        hsl2 -= var20 * y2;
                        y2 = 0;
                    }

                    x3 <<= 14;
                    if( y3 < 0 )
                    {
                        x3 -= dxdy_AC * y3;
                        y3 = 0;
                    }

                    if( (y3 == y2 || dxdy_AB >= dxdy_BC) && (y3 != y2 || dxdy_AB <= dxdy_AC) )
                    {
                        y1 -= y3;
                        y3 -= y2;
                        y2 = WIDTH * y2;

                        while( true )
                        {
                            --y3;
                            if( y3 < 0 )
                            {
                                while( true )
                                {
                                    --y1;
                                    if( y1 < 0 )
                                    {
                                        return;
                                    }

                                    gouraud_deob_draw_scanline(
                                        pixels, y2, 0, 0, x3 >> 14, x1 >> 14, hsl2, var19);
                                    x1 += dxdy_AB;
                                    x3 += dxdy_AC;
                                    hsl2 += var20;
                                    y2 += WIDTH;
                                }
                            }

                            gouraud_deob_draw_scanline(
                                pixels, y2, 0, 0, x2 >> 14, x1 >> 14, hsl2, var19);
                            x1 += dxdy_AB;
                            x2 += dxdy_BC;
                            hsl2 += var20;
                            y2 += WIDTH;
                        }
                    }
                    else
                    {
                        y1 -= y3;
                        y3 -= y2;
                        y2 = WIDTH * y2;

                        while( true )
                        {
                            --y3;
                            if( y3 < 0 )
                            {
                                while( true )
                                {
                                    --y1;
                                    if( y1 < 0 )
                                    {
                                        return;
                                    }

                                    gouraud_deob_draw_scanline(
                                        pixels, y2, 0, 0, x1 >> 14, x3 >> 14, hsl2, var19);
                                    x1 += dxdy_AB;
                                    x3 += dxdy_AC;
                                    hsl2 += var20;
                                    y2 += WIDTH;
                                }
                            }

                            gouraud_deob_draw_scanline(
                                pixels, y2, 0, 0, x1 >> 14, x2 >> 14, hsl2, var19);
                            x1 += dxdy_AB;
                            x2 += dxdy_BC;
                            hsl2 += var20;
                            y2 += WIDTH;
                        }
                    }
                }
                else
                {
                    x3 = x2 <<= 14;
                    if( y2 < 0 )
                    {
                        x3 -= dxdy_AB * y2;
                        x2 -= dxdy_BC * y2;
                        hsl2 -= var20 * y2;
                        y2 = 0;
                    }

                    x1 <<= 14;
                    if( y1 < 0 )
                    {
                        x1 -= y1 * dxdy_AC;
                        y1 = 0;
                    }

                    if( dxdy_AB < dxdy_BC )
                    {
                        y3 -= y1;
                        y1 -= y2;
                        y2 = WIDTH * y2;

                        while( true )
                        {
                            --y1;
                            if( y1 < 0 )
                            {
                                while( true )
                                {
                                    --y3;
                                    if( y3 < 0 )
                                    {
                                        return;
                                    }

                                    gouraud_deob_draw_scanline(
                                        pixels, y2, 0, 0, x1 >> 14, x2 >> 14, hsl2, var19);
                                    x1 += dxdy_AC;
                                    x2 += dxdy_BC;
                                    hsl2 += var20;
                                    y2 += WIDTH;
                                }
                            }

                            gouraud_deob_draw_scanline(
                                pixels, y2, 0, 0, x3 >> 14, x2 >> 14, hsl2, var19);
                            x3 += dxdy_AB;
                            x2 += dxdy_BC;
                            hsl2 += var20;
                            y2 += WIDTH;
                        }
                    }
                    else
                    {
                        y3 -= y1;
                        y1 -= y2;
                        y2 = WIDTH * y2;

                        while( true )
                        {
                            --y1;
                            if( y1 < 0 )
                            {
                                while( true )
                                {
                                    --y3;
                                    if( y3 < 0 )
                                    {
                                        return;
                                    }

                                    gouraud_deob_draw_scanline(
                                        pixels, y2, 0, 0, x2 >> 14, x1 >> 14, hsl2, var19);
                                    x1 += dxdy_AC;
                                    x2 += dxdy_BC;
                                    hsl2 += var20;
                                    y2 += WIDTH;
                                }
                            }

                            gouraud_deob_draw_scanline(
                                pixels, y2, 0, 0, x2 >> 14, x3 >> 14, hsl2, var19);
                            x3 += dxdy_AB;
                            x2 += dxdy_BC;
                            hsl2 += var20;
                            y2 += WIDTH;
                        }
                    }
                }
            }
        }
        else if( y3 < BOTTOM_Y )
        {
            if( y1 > BOTTOM_Y )
            {
                y1 = BOTTOM_Y;
            }

            if( y2 > BOTTOM_Y )
            {
                y2 = BOTTOM_Y;
            }

            hsl3 = var19 + ((hsl3 << 8) - x3 * var19);
            if( y1 < y2 )
            {
                x2 = x3 <<= 14;
                if( y3 < 0 )
                {
                    x2 -= dxdy_BC * y3;
                    x3 -= dxdy_AC * y3;
                    hsl3 -= var20 * y3;
                    y3 = 0;
                }

                x1 <<= 14;
                if( y1 < 0 )
                {
                    x1 -= y1 * dxdy_AB;
                    y1 = 0;
                }

                if( dxdy_BC < dxdy_AC )
                {
                    y2 -= y1;
                    y1 -= y3;
                    y3 = WIDTH * y3;

                    while( true )
                    {
                        --y1;
                        if( y1 < 0 )
                        {
                            while( true )
                            {
                                --y2;
                                if( y2 < 0 )
                                {
                                    return;
                                }

                                gouraud_deob_draw_scanline(
                                    pixels, y3, 0, 0, x2 >> 14, x1 >> 14, hsl3, var19);
                                x2 += dxdy_BC;
                                x1 += dxdy_AB;
                                hsl3 += var20;
                                y3 += WIDTH;
                            }
                        }

                        gouraud_deob_draw_scanline(
                            pixels, y3, 0, 0, x2 >> 14, x3 >> 14, hsl3, var19);
                        x2 += dxdy_BC;
                        x3 += dxdy_AC;
                        hsl3 += var20;
                        y3 += WIDTH;
                    }
                }
                else
                {
                    y2 -= y1;
                    y1 -= y3;
                    y3 = WIDTH * y3;

                    while( true )
                    {
                        --y1;
                        if( y1 < 0 )
                        {
                            while( true )
                            {
                                --y2;
                                if( y2 < 0 )
                                {
                                    return;
                                }

                                gouraud_deob_draw_scanline(
                                    pixels, y3, 0, 0, x1 >> 14, x2 >> 14, hsl3, var19);
                                x2 += dxdy_BC;
                                x1 += dxdy_AB;
                                hsl3 += var20;
                                y3 += WIDTH;
                            }
                        }

                        gouraud_deob_draw_scanline(
                            pixels, y3, 0, 0, x3 >> 14, x2 >> 14, hsl3, var19);
                        x2 += dxdy_BC;
                        x3 += dxdy_AC;
                        hsl3 += var20;
                        y3 += WIDTH;
                    }
                }
            }
            else
            {
                x1 = x3 <<= 14;
                if( y3 < 0 )
                {
                    x1 -= dxdy_BC * y3;
                    x3 -= dxdy_AC * y3;
                    hsl3 -= var20 * y3;
                    y3 = 0;
                }

                x2 <<= 14;
                if( y2 < 0 )
                {
                    x2 -= dxdy_AB * y2;
                    y2 = 0;
                }

                if( dxdy_BC < dxdy_AC )
                {
                    y1 -= y2;
                    y2 -= y3;
                    y3 = WIDTH * y3;

                    while( true )
                    {
                        --y2;
                        if( y2 < 0 )
                        {
                            while( true )
                            {
                                --y1;
                                if( y1 < 0 )
                                {
                                    return;
                                }

                                gouraud_deob_draw_scanline(
                                    pixels, y3, 0, 0, x2 >> 14, x3 >> 14, hsl3, var19);
                                x2 += dxdy_AB;
                                x3 += dxdy_AC;
                                hsl3 += var20;
                                y3 += WIDTH;
                            }
                        }

                        gouraud_deob_draw_scanline(
                            pixels, y3, 0, 0, x1 >> 14, x3 >> 14, hsl3, var19);
                        x1 += dxdy_BC;
                        x3 += dxdy_AC;
                        hsl3 += var20;
                        y3 += WIDTH;
                    }
                }
                else
                {
                    y1 -= y2;
                    y2 -= y3;
                    y3 = WIDTH * y3;

                    while( true )
                    {
                        --y2;
                        if( y2 < 0 )
                        {
                            while( true )
                            {
                                --y1;
                                if( y1 < 0 )
                                {
                                    return;
                                }

                                gouraud_deob_draw_scanline(
                                    pixels, y3, 0, 0, x3 >> 14, x2 >> 14, hsl3, var19);
                                x2 += dxdy_AB;
                                x3 += dxdy_AC;
                                hsl3 += var20;
                                y3 += WIDTH;
                            }
                        }

                        gouraud_deob_draw_scanline(
                            pixels, y3, 0, 0, x3 >> 14, x1 >> 14, hsl3, var19);
                        x1 += dxdy_BC;
                        x3 += dxdy_AC;
                        hsl3 += var20;
                        y3 += WIDTH;
                    }
                }
            }
        }
    }
}
// gouraud_deob_draw_scanline(int* pixels, int y, int x1, int x2, int x3, int hsl1, int hsl2, int
// hsl3)

static inline void
gouraud_deob_draw_scanline(
    int* var0, int var1, int var2, int var3, int var4, int var5, int var6, int var7)
{
    if( var4 < 0 || var5 < 0 )
    {
        return;
    }

    if( var4 > WIDTH || var5 > WIDTH )
    {
        return;
    }
    // if( textureOutOfDrawingBounds )
    // {
    //     if( var5 > lastX )
    //     {
    //         var5 = lastX;
    //     }

    //     if( var4 < 0 )
    //     {
    //         var4 = 0;
    //     }
    // }

    if( var4 < var5 )
    {
        var1 += var4;
        var6 += var4 * var7;
        int var8;
        int var9;
        int var10;
        if( true )
        {
            var3 = (var5 - var4) >> 2;
            var7 <<= 2;
            if( true )
            {
                if( var3 > 0 )
                {
                    do
                    {
                        var2 = g_hsl16_to_rgb_table[var6 >> 8];
                        var6 += var7;
                        var0[var1++] = var2;
                        var0[var1++] = var2;
                        var0[var1++] = var2;
                        var0[var1++] = var2;
                        --var3;
                    } while( var3 > 0 );
                }

                var3 = (var5 - var4) & 3;
                if( var3 > 0 )
                {
                    var2 = g_hsl16_to_rgb_table[var6 >> 8];

                    do
                    {
                        var0[var1++] = var2;
                        --var3;
                    } while( var3 > 0 );
                }
            }
        }
        else
        {
            var3 = var5 - var4;
            if( true )
            {
                do
                {
                    var0[var1++] = g_hsl16_to_rgb_table[var6 >> 8];
                    var6 += var7;
                    --var3;
                } while( var3 > 0 );
            }
        }
    }
    else
    {
        int iii = 0;
        assert(0);
    }
}