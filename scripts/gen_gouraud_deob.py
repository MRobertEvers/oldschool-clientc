#!/usr/bin/env python3
"""Generate gouraud.deob.u.c triangle body by transforming flat.deob.u.c triangle."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
flat_path = ROOT / "src/graphics/raster/flat/flat.deob.u.c"
out_path = ROOT / "src/graphics/raster/gouraud/gouraud.deob.u.c"

flat = flat_path.read_text()

raster = r'''#ifndef GOURAUD_DEOB_U_C
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

'''

start = flat.index("static inline void\npix3d_deob_flat_triangle(")
end = flat.index("#endif /* FLAT_DEOB_U_C */")
tri_block = flat[start:end]

# Rename function
tri_block = tri_block.replace("pix3d_deob_flat_triangle", "pix3d_deob_gouraud_triangle")
tri_block = tri_block.replace("int colour)", "int colourA,\n    int colourB,\n    int colourC)")

# Insert colour steps after xStepAC block (before first if yA <= yB)
insert = """
    int colourStepAB = 0;
    if( yB != yA )
        colourStepAB = (((colourB - colourA) << 15) / (yB - yA)) | 0;

    int colourStepBC = 0;
    if( yC != yB )
        colourStepBC = (((colourC - colourB) << 15) / (yC - yB)) | 0;

    int colourStepAC = 0;
    if( yC != yA )
        colourStepAC = (((colourA - colourC) << 15) / (yA - yC)) | 0;

"""
marker = "        xStepAC = (((xA - xC) << 16) / (yA - yC)) | 0;\n\n    if( yA <= yB"
tri_block = tri_block.replace(marker, "        xStepAC = (((xA - xC) << 16) / (yA - yC)) | 0;\n" + insert + "    if( yA <= yB")

# Chained assignments from TS -> C
repls = [
    ("            xC = xA <<= 16;", "            xA <<= 16;\n            xC = xA;\n            colourA <<= 15;\n            colourC = colourA;"),
    ("                if (yA < 0)", "                if( yA < 0 )"),  # normalize spaces - flat uses if( yA
]

for a, b in repls:
    tri_block = tri_block.replace(a, b)

# Fix remaining chained assigns (flat uses xC = xA <<= 16 without space variants)
tri_block = tri_block.replace("            xC = xA <<= 16;", "            xA <<= 16;\n            xC = xA;\n            colourA <<= 15;\n            colourC = colourA;")
tri_block = tri_block.replace("                xB = xA <<= 16;", "                xA <<= 16;\n                xB = xA;\n                colourA <<= 15;\n                colourB = colourA;")
tri_block = tri_block.replace("                xA = xB <<= 16;", "                xB <<= 16;\n                xA = xB;\n                colourB <<= 15;\n                colourA = colourB;")
tri_block = tri_block.replace("                xC = xB <<= 16;", "                xB <<= 16;\n                xC = xB;\n                colourB <<= 15;\n                colourC = colourB;")
tri_block = tri_block.replace("                xB = xC <<= 16;", "                xC <<= 16;\n                xB = xC;\n                colourC <<= 15;\n                colourB = colourC;")
tri_block = tri_block.replace("                xA = xC <<= 16;", "                xC <<= 16;\n                xA = xC;\n                colourC <<= 15;\n                colourA = colourC;")

# xB <<= 16 -> add colourB <<= 15 where TS has it (after xB only, not xC)
tri_block = tri_block.replace(
    "                xB <<= 16;\n\n                if( yB < 0 )",
    "                xB <<= 16;\n                colourB <<= 15;\n\n                if( yB < 0 )",
)
tri_block = tri_block.replace(
    "                xC <<= 16;\n\n                if( yC < 0 )",
    "                xC <<= 16;\n                colourC <<= 15;\n\n                if( yC < 0 )",
)
tri_block = tri_block.replace(
    "                xC <<= 16;\n\n                if( yC < 0 )",
    "                xC <<= 16;\n                colourC <<= 15;\n\n                if( yC < 0 )",
    1,
)

# More xC <<= 16 blocks (different indentation)
tri_block = tri_block.replace(
    "                xC <<= 16;\n\n                if( yC < 0 )",
    "                xC <<= 16;\n                colourC <<= 15;\n\n                if( yC < 0 )",
)

tri_block = tri_block.replace(
    "                xA <<= 16;\n\n                if( yA < 0 )",
    "                xA <<= 16;\n                colourA <<= 15;\n\n                if( yA < 0 )",
)
tri_block = tri_block.replace(
    "                xB <<= 16;\n\n                if( yB < 0 )",
    "                xB <<= 16;\n                colourB <<= 15;\n\n                if( yB < 0 )",
)

tri_block = tri_block.replace(
    "                xA <<= 16;\n\n                if( yA < 0 )",
    "                xA <<= 16;\n                colourA <<= 15;\n\n                if( yA < 0 )",
)

tri_block = tri_block.replace(
    "                xB <<= 16;\n\n                if( yB < 0 )",
    "                xB <<= 16;\n                colourB <<= 15;\n\n                if( yB < 0 )",
)

# yB branch: xC <<= 16 after xA = xB
tri_block = tri_block.replace(
    "                xC <<= 16;\n\n                if( yC < 0 )",
    "                xC <<= 16;\n                colourC <<= 15;\n\n                if( yC < 0 )",
)

# yB branch else: xA <<= 16
tri_block = tri_block.replace(
    "                xA <<= 16;\n\n                if( yA < 0 )",
    "                xA <<= 16;\n                colourA <<= 15;\n\n                if( yA < 0 )",
)

# Third major branch xA <<= 16 after xB = xC
tri_block = tri_block.replace(
    "                xA <<= 16;\n\n                if( yA < 0 )",
    "                xA <<= 16;\n                colourA <<= 15;\n\n                if( yA < 0 )",
)

# Fourth branch xB <<= 16 after xA = xC
tri_block = tri_block.replace(
    "                xB <<= 16;\n\n                if( yB < 0 )",
    "                xB <<= 16;\n                colourB <<= 15;\n\n                if( yB < 0 )",
)

# yA < 0 blocks with xC/xA only - add colour adjustments
tri_block = tri_block.replace(
    "                if( yA < 0 )\n                {\n                    xC -= xStepAC * yA;\n                    xA -= xStepAB * yA;\n                    yA = 0;\n                }",
    "                if( yA < 0 )\n                {\n                    xC -= xStepAC * yA;\n                    xA -= xStepAB * yA;\n                    colourC -= colourStepAC * yA;\n                    colourA -= colourStepAB * yA;\n                    yA = 0;\n                }",
)
tri_block = tri_block.replace(
    "                if( yA < 0 )\n                {\n                    xB -= xStepAC * yA;\n                    xA -= xStepAB * yA;\n                    yA = 0;\n                }",
    "                if( yA < 0 )\n                {\n                    xB -= xStepAC * yA;\n                    xA -= xStepAB * yA;\n                    colourB -= colourStepAC * yA;\n                    colourA -= colourStepAB * yA;\n                    yA = 0;\n                }",
)

tri_block = tri_block.replace(
    "                if( yB < 0 )\n                {\n                    xB -= xStepBC * yB;\n                    yB = 0;\n                }",
    "                if( yB < 0 )\n                {\n                    xB -= xStepBC * yB;\n                    colourB -= colourStepBC * yB;\n                    yB = 0;\n                }",
)

tri_block = tri_block.replace(
    "                if( yC < 0 )\n                {\n                    xC -= xStepBC * yC;\n                    yC = 0;\n                }",
    "                if( yC < 0 )\n                {\n                    xC -= xStepBC * yC;\n                    colourC -= colourStepBC * yC;\n                    yC = 0;\n                }",
)

tri_block = tri_block.replace(
    "                if( yB < 0 )\n                {\n                    xA -= xStepAB * yB;\n                    xB -= xStepBC * yB;\n                    yB = 0;\n                }",
    "                if( yB < 0 )\n                {\n                    xA -= xStepAB * yB;\n                    xB -= xStepBC * yB;\n                    colourA -= colourStepAB * yB;\n                    colourB -= colourStepBC * yB;\n                    yB = 0;\n                }",
)

tri_block = tri_block.replace(
    "                if( yC < 0 )\n                {\n                    xC -= xStepAC * yC;\n                    yC = 0;\n                }",
    "                if( yC < 0 )\n                {\n                    xC -= xStepAC * yC;\n                    colourC -= colourStepAC * yC;\n                    yC = 0;\n                }",
)

tri_block = tri_block.replace(
    "                if( yB < 0 )\n                {\n                    xC -= xStepAB * yB;\n                    xB -= xStepBC * yB;\n                    yB = 0;\n                }",
    "                if( yB < 0 )\n                {\n                    xC -= xStepAB * yB;\n                    xB -= xStepBC * yB;\n                    colourC -= colourStepAB * yB;\n                    colourB -= colourStepBC * yB;\n                    yB = 0;\n                }",
)

tri_block = tri_block.replace(
    "                if( yA < 0 )\n                {\n                    xA -= xStepAC * yA;\n                    yA = 0;\n                }",
    "                if( yA < 0 )\n                {\n                    xA -= xStepAC * yA;\n                    colourA -= colourStepAC * yA;\n                    yA = 0;\n                }",
)

tri_block = tri_block.replace(
    "                if( yC < 0 )\n                {\n                    xB -= xStepBC * yC;\n                    xC -= xStepAC * yC;\n                    yC = 0;\n                }",
    "                if( yC < 0 )\n                {\n                    xB -= xStepBC * yC;\n                    xC -= xStepAC * yC;\n                    colourB -= colourStepBC * yC;\n                    colourC -= colourStepAC * yC;\n                    yC = 0;\n                }",
)

tri_block = tri_block.replace(
    "                if( yA < 0 )\n                {\n                    xA -= xStepAB * yA;\n                    yA = 0;\n                }",
    "                if( yA < 0 )\n                {\n                    xA -= xStepAB * yA;\n                    colourA -= colourStepAB * yA;\n                    yA = 0;\n                }",
)

tri_block = tri_block.replace(
    "                if( yB < 0 )\n                {\n                    xB -= xStepAB * yB;\n                    yB = 0;\n                }",
    "                if( yB < 0 )\n                {\n                    xB -= xStepAB * yB;\n                    colourB -= colourStepAB * yB;\n                    yB = 0;\n                }",
)

tri_block = tri_block.replace(
    "                if( yC < 0 )\n                {\n                    xA -= xStepBC * yC;\n                    xC -= xStepAC * yC;\n                    yC = 0;\n                }",
    "                if( yC < 0 )\n                {\n                    xA -= xStepBC * yC;\n                    xC -= xStepAC * yC;\n                    colourA -= colourStepBC * yC;\n                    colourC -= colourStepAC * yC;\n                    yC = 0;\n                }",
)

# Replace all flat_raster calls with gouraud_raster (unique patterns from flat file)
call_map = [
    ("pix3d_deob_flat_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yA, colour)",
     "pix3d_deob_gouraud_raster(xC >> 16, xB >> 16, colourC >> 7, colourB >> 7, g_pix3d_deob_pixels, yA, 0)"),
    ("pix3d_deob_flat_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yA, colour)",
     "pix3d_deob_gouraud_raster(xC >> 16, xA >> 16, colourC >> 7, colourA >> 7, g_pix3d_deob_pixels, yA, 0)"),
    ("pix3d_deob_flat_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yA, colour)",
     "pix3d_deob_gouraud_raster(xB >> 16, xC >> 16, colourB >> 7, colourC >> 7, g_pix3d_deob_pixels, yA, 0)"),
    ("pix3d_deob_flat_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yA, colour)",
     "pix3d_deob_gouraud_raster(xA >> 16, xC >> 16, colourA >> 7, colourC >> 7, g_pix3d_deob_pixels, yA, 0)"),
    ("pix3d_deob_flat_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yA, colour)",
     "pix3d_deob_gouraud_raster(xC >> 16, xA >> 16, colourC >> 7, colourA >> 7, g_pix3d_deob_pixels, yA, 0)"),
    ("pix3d_deob_flat_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yA, colour)",
     "pix3d_deob_gouraud_raster(xB >> 16, xA >> 16, colourB >> 7, colourA >> 7, g_pix3d_deob_pixels, yA, 0)"),
    ("pix3d_deob_flat_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yA, colour)",
     "pix3d_deob_gouraud_raster(xA >> 16, xB >> 16, colourA >> 7, colourB >> 7, g_pix3d_deob_pixels, yA, 0)"),
    ("pix3d_deob_flat_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yA, colour)",
     "pix3d_deob_gouraud_raster(xA >> 16, xC >> 16, colourA >> 7, colourC >> 7, g_pix3d_deob_pixels, yA, 0)"),
    ("pix3d_deob_flat_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yB, colour)",
     "pix3d_deob_gouraud_raster(xA >> 16, xB >> 16, colourA >> 7, colourB >> 7, g_pix3d_deob_pixels, yB, 0)"),
    ("pix3d_deob_flat_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yB, colour)",
     "pix3d_deob_gouraud_raster(xC >> 16, xB >> 16, colourC >> 7, colourB >> 7, g_pix3d_deob_pixels, yB, 0)"),
    ("pix3d_deob_flat_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yB, colour)",
     "pix3d_deob_gouraud_raster(xB >> 16, xC >> 16, colourB >> 7, colourC >> 7, g_pix3d_deob_pixels, yB, 0)"),
    ("pix3d_deob_flat_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yB, colour)",
     "pix3d_deob_gouraud_raster(xB >> 16, xA >> 16, colourB >> 7, colourA >> 7, g_pix3d_deob_pixels, yB, 0)"),
    ("pix3d_deob_flat_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yB, colour)",
     "pix3d_deob_gouraud_raster(xA >> 16, xC >> 16, colourA >> 7, colourC >> 7, g_pix3d_deob_pixels, yB, 0)"),
    ("pix3d_deob_flat_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yB, colour)",
     "pix3d_deob_gouraud_raster(xC >> 16, xA >> 16, colourC >> 7, colourA >> 7, g_pix3d_deob_pixels, yB, 0)"),
    ("pix3d_deob_flat_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yB, colour)",
     "pix3d_deob_gouraud_raster(xB >> 16, xA >> 16, colourB >> 7, colourA >> 7, g_pix3d_deob_pixels, yB, 0)"),
    ("pix3d_deob_flat_raster(xB >> 16, xA >> 16, g_pix3d_deob_pixels, yC, colour)",
     "pix3d_deob_gouraud_raster(xB >> 16, xA >> 16, colourB >> 7, colourA >> 7, g_pix3d_deob_pixels, yC, 0)"),
    ("pix3d_deob_flat_raster(xB >> 16, xC >> 16, g_pix3d_deob_pixels, yC, colour)",
     "pix3d_deob_gouraud_raster(xB >> 16, xC >> 16, colourB >> 7, colourC >> 7, g_pix3d_deob_pixels, yC, 0)"),
    ("pix3d_deob_flat_raster(xA >> 16, xB >> 16, g_pix3d_deob_pixels, yC, colour)",
     "pix3d_deob_gouraud_raster(xA >> 16, xB >> 16, colourA >> 7, colourB >> 7, g_pix3d_deob_pixels, yC, 0)"),
    ("pix3d_deob_flat_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yC, colour)",
     "pix3d_deob_gouraud_raster(xC >> 16, xB >> 16, colourC >> 7, colourB >> 7, g_pix3d_deob_pixels, yC, 0)"),
    ("pix3d_deob_flat_raster(xC >> 16, xB >> 16, g_pix3d_deob_pixels, yC, colour)",
     "pix3d_deob_gouraud_raster(xC >> 16, xB >> 16, colourC >> 7, colourB >> 7, g_pix3d_deob_pixels, yC, 0)"),
    ("pix3d_deob_flat_raster(xA >> 16, xC >> 16, g_pix3d_deob_pixels, yC, colour)",
     "pix3d_deob_gouraud_raster(xA >> 16, xC >> 16, colourA >> 7, colourC >> 7, g_pix3d_deob_pixels, yC, 0)"),
    ("pix3d_deob_flat_raster(xC >> 16, xA >> 16, g_pix3d_deob_pixels, yC, colour)",
     "pix3d_deob_gouraud_raster(xC >> 16, xA >> 16, colourC >> 7, colourA >> 7, g_pix3d_deob_pixels, yC, 0)"),
]

seen = set()
for old, new in call_map:
    if old in tri_block:
        tri_block = tri_block.replace(old, new)

# Deduplicate accidental duplicate map entry - already same

# After inner loop lines that increment x, add colour increments (match TS order)
# Pattern: inner double loop body has 4 lines xC,xB,yA then inner single has 4 lines xC,xA,yA
inner_pairs = [
    ("                            xC += xStepAC;\n                            xB += xStepBC;\n                            yA += g_pix3d_deob_width;",
     "                            xC += xStepAC;\n                            xB += xStepBC;\n                            colourC += colourStepAC;\n                            colourB += colourStepBC;\n                            yA += g_pix3d_deob_width;"),
    ("                        xC += xStepAC;\n                        xA += xStepAB;\n                        yA += g_pix3d_deob_width;",
     "                        xC += xStepAC;\n                        xA += xStepAB;\n                        colourC += colourStepAC;\n                        colourA += colourStepAB;\n                        yA += g_pix3d_deob_width;"),
    ("                                xC += xStepBC;\n                                xA += xStepAB;\n                                yA += g_pix3d_deob_width;",
     "                                xC += xStepBC;\n                                xA += xStepAB;\n                                colourC += colourStepBC;\n                                colourA += colourStepAB;\n                                yA += g_pix3d_deob_width;"),
    ("                        xB += xStepAC;\n                        xA += xStepAB;\n                        yA += g_pix3d_deob_width;",
     "                        xB += xStepAC;\n                        xA += xStepAB;\n                        colourB += colourStepAC;\n                        colourA += colourStepAB;\n                        yA += g_pix3d_deob_width;"),
    ("                                xC += xStepBC;\n                                xA += xStepAB;\n                                yA += g_pix3d_deob_width;",
     "                                xC += xStepBC;\n                                xA += xStepAB;\n                                colourC += colourStepBC;\n                                colourA += colourStepAB;\n                                yA += g_pix3d_deob_width;"),
    ("                            xA += xStepAB;\n                            xC += xStepAC;\n                            yB += g_pix3d_deob_width;",
     "                            xA += xStepAB;\n                            xC += xStepAC;\n                            colourA += colourStepAB;\n                            colourC += colourStepAC;\n                            yB += g_pix3d_deob_width;"),
    ("                        xA += xStepAB;\n                        xB += xStepBC;\n                        yB += g_pix3d_deob_width;",
     "                        xA += xStepAB;\n                        xB += xStepBC;\n                        colourA += colourStepAB;\n                        colourB += colourStepBC;\n                        yB += g_pix3d_deob_width;"),
    ("                            xA += xStepAB;\n                            xC += xStepAC;\n                            yB += g_pix3d_deob_width;",
     "                            xA += xStepAB;\n                            xC += xStepAC;\n                            colourA += colourStepAB;\n                            colourC += colourStepAC;\n                            yB += g_pix3d_deob_width;"),
    ("                        xA += xStepAB;\n                        xB += xStepBC;\n                        yB += g_pix3d_deob_width;",
     "                        xA += xStepAB;\n                        xB += xStepBC;\n                        colourA += colourStepAB;\n                        colourB += colourStepBC;\n                        yB += g_pix3d_deob_width;"),
    ("                            xA += xStepAC;\n                            xB += xStepBC;\n                            yB += g_pix3d_deob_width;",
     "                            xA += xStepAC;\n                            xB += xStepBC;\n                            colourA += colourStepAC;\n                            colourB += colourStepBC;\n                            yB += g_pix3d_deob_width;"),
    ("                        xC += xStepAB;\n                        xB += xStepBC;\n                        yB += g_pix3d_deob_width;",
     "                        xC += xStepAB;\n                        xB += xStepBC;\n                        colourC += colourStepAB;\n                        colourB += colourStepBC;\n                        yB += g_pix3d_deob_width;"),
    ("                            xA += xStepAC;\n                            xB += xStepBC;\n                            yB += g_pix3d_deob_width;",
     "                            xA += xStepAC;\n                            xB += xStepBC;\n                            colourA += colourStepAC;\n                            colourB += colourStepBC;\n                            yB += g_pix3d_deob_width;"),
    ("                        xC += xStepAB;\n                        xB += xStepBC;\n                        yB += g_pix3d_deob_width;",
     "                        xC += xStepAB;\n                        xB += xStepBC;\n                        colourC += colourStepAB;\n                        colourB += colourStepBC;\n                        yB += g_pix3d_deob_width;"),
    ("                            xB += xStepBC;\n                            xA += xStepAB;\n                            yC += g_pix3d_deob_width;",
     "                            xB += xStepBC;\n                            xA += xStepAB;\n                            colourB += colourStepBC;\n                            colourA += colourStepAB;\n                            yC += g_pix3d_deob_width;"),
    ("                        xB += xStepBC;\n                        xC += xStepAC;\n                        yC += g_pix3d_deob_width;",
     "                        xB += xStepBC;\n                        xC += xStepAC;\n                        colourB += colourStepBC;\n                        colourC += colourStepAC;\n                        yC += g_pix3d_deob_width;"),
    ("                            xB += xStepBC;\n                            xA += xStepAB;\n                            yC += g_pix3d_deob_width;",
     "                            xB += xStepBC;\n                            xA += xStepAB;\n                            colourB += colourStepBC;\n                            colourA += colourStepAB;\n                            yC += g_pix3d_deob_width;"),
    ("                        xB += xStepBC;\n                        xC += xStepAC;\n                        yC += g_pix3d_deob_width;",
     "                        xB += xStepBC;\n                        xC += xStepAC;\n                        colourB += colourStepBC;\n                        colourC += colourStepAC;\n                        yC += g_pix3d_deob_width;"),
    ("                            xB += xStepAB;\n                            xC += xStepAC;\n                            yC += g_pix3d_deob_width;",
     "                            xB += xStepAB;\n                            xC += xStepAC;\n                            colourB += colourStepAB;\n                            colourC += colourStepAC;\n                            yC += g_pix3d_deob_width;"),
    ("                        xA += xStepBC;\n                        xC += xStepAC;\n                        yC += g_pix3d_deob_width;",
     "                        xA += xStepBC;\n                        xC += xStepAC;\n                        colourA += colourStepBC;\n                        colourC += colourStepAC;\n                        yC += g_pix3d_deob_width;"),
    ("                            xB += xStepAB;\n                            xC += xStepAC;\n                            yC += g_pix3d_deob_width;",
     "                            xB += xStepAB;\n                            xC += xStepAC;\n                            colourB += colourStepAB;\n                            colourC += colourStepAC;\n                            yC += g_pix3d_deob_width;"),
    ("                        xA += xStepBC;\n                        xC += xStepAC;\n                        yC += g_pix3d_deob_width;",
     "                        xA += xStepBC;\n                        xC += xStepAC;\n                        colourA += colourStepBC;\n                        colourC += colourStepAC;\n                        yC += g_pix3d_deob_width;"),
]

for old, new in inner_pairs:
    tri_block = tri_block.replace(old, new)

tri_block = tri_block.replace("#endif /* FLAT_DEOB_U_C */", "#endif /* GOURAUD_DEOB_U_C */")

out_path.parent.mkdir(parents=True, exist_ok=True)
out_path.write_text(raster + tri_block)
print("Wrote", out_path, "lines", len(tri_block.splitlines()))

# Verify no flat_raster left
if "pix3d_deob_flat_raster" in out_path.read_text():
    raise SystemExit("ERROR: flat_raster still present")
if "colourStepAB" not in out_path.read_text():
    raise SystemExit("ERROR: colour steps missing")
