#include "pix3d_deob_state.h"

#include <stdlib.h>

int* g_pix3d_deob_pixels = NULL;
int g_pix3d_deob_width = 0;
int g_pix3d_deob_height = 0;
int g_pix3d_deob_size_x = 0;
int g_pix3d_deob_clip_max_y = 0;
int g_pix3d_deob_origin_x = 0;
int g_pix3d_deob_origin_y = 0;
int* g_pix3d_deob_scanline = NULL;

int g_pix3d_deob_hclip = 0;
int g_pix3d_deob_trans = 0;
int g_pix3d_deob_low_detail = 1;
int g_pix3d_deob_low_mem = 0;
int g_pix3d_deob_opaque = 1;

void pix3d_deob_free(void)
{
    if( g_pix3d_deob_scanline )
    {
        free(g_pix3d_deob_scanline);
        g_pix3d_deob_scanline = NULL;
    }
}

void pix3d_deob_set_clipping(
    int* pixels,
    int width,
    int height,
    int size_x,
    int clip_max_y)
{
    pix3d_deob_free();

    g_pix3d_deob_pixels = pixels;
    g_pix3d_deob_width = width;
    g_pix3d_deob_height = height;
    g_pix3d_deob_size_x = size_x;
    g_pix3d_deob_clip_max_y = clip_max_y;
    g_pix3d_deob_origin_x = (width / 2);
    g_pix3d_deob_origin_y = (height / 2);

    g_pix3d_deob_scanline = (int*)malloc((size_t)height * sizeof(int));
    if( !g_pix3d_deob_scanline )
        return;

    for( int y = 0; y < height; y++ )
        g_pix3d_deob_scanline[y] = width * y;
}

void pix3d_deob_set_alpha(int trans)
{
    g_pix3d_deob_trans = trans;
}

void pix3d_deob_set_hclip(int hclip)
{
    g_pix3d_deob_hclip = hclip;
}

void pix3d_deob_set_low_detail(int low_detail)
{
    g_pix3d_deob_low_detail = low_detail;
}

void pix3d_deob_set_low_mem(int low_mem)
{
    g_pix3d_deob_low_mem = low_mem;
}
