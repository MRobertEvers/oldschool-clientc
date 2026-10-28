#include "mem/torirs_mem.h"
#include "engine/title_panel.h"

#include "engine/jpeg_decode.h"
#include "engine/png_decode.h"
#include "engine/torirs_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * The reference title panel, and the only size this composite produces.
 *
 * Both eras use it: Client-TS's canvas is exactly 765x503, and the deob centres
 * a panel of the same size in whatever window it has. These are decode
 * geometry -- how the picture is assembled out of what the cache stores --
 * rather than layout: where the backdrop is PUT is the layout row's business.
 */
#define TITLE_PANEL_W 765
#define TITLE_PANEL_H 503

/* Blit `src` into the `dst_w`x`dst_h` panel at (dst_x, 0), clipped, optionally
 * mirrored. The only two placements the reference uses are the unmirrored left
 * and the mirrored right. */
static void
blit_half(
    uint32_t* dst,
    int dst_w,
    int dst_h,
    uint32_t const* src,
    int src_w,
    int src_h,
    int dst_x,
    int mirrored)
{
    assert(dst);
    assert(src);

    for( int y = 0; y < src_h && y < dst_h; y++ )
    {
        for( int x = 0; x < src_w; x++ )
        {
            int out_x = dst_x + (mirrored ? (src_w - 1 - x) : x);
            if( out_x < 0 || out_x >= dst_w )
                continue;
            dst[(size_t)y * dst_w + out_x] = src[(size_t)y * src_w + x];
        }
    }
}

/* One frame of `w`x`h` holding `panel`, which the sprite takes over. */
static struct ToriRS_Sprite*
title_panel_sprite(
    uint32_t* panel,
    int w,
    int h)
{
    struct ToriRS_Sprite* sprite;
    struct ToriRS_SpriteFrame* frame;

    assert(panel);
    frame = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*frame));
    assert(frame);
    frame->pixels_argb = panel;
    frame->width = w;
    frame->height = h;
    frame->crop_width = w;
    frame->crop_height = h;

    sprite = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*sprite));
    assert(sprite);
    sprite->frames = frame;
    sprite->frame_count = 1;
    return sprite;
}

struct ToriRS_Sprite*
ToriRS_TitlePanelFromJpeg(
    void const* data,
    int data_size)
{
    uint32_t* panel;
    uint32_t* half = NULL;
    int half_w = 0;
    int half_h = 0;

    assert(data);
    if( data_size <= 0 )
        return NULL;

    if( !JpegDecode_ArgbRsCache(ToriMem_Frame(), data, data_size, &half_w, &half_h, &half) )
        return NULL;

    panel = ToriMem_Calloc(ToriMem_Assets(), (size_t)TITLE_PANEL_W * TITLE_PANEL_H, sizeof(*panel));
    assert(panel);

    /* The stored half at the left, its mirror butted against the right edge.
     * Anchoring the mirror to the right edge rather than to the first half's
     * width is what makes the seam land on a shared column: at the reference's
     * 383 the two overlap by one, and a cache that stored a different width
     * still fills the panel instead of leaving a gap or running over. */
    blit_half(panel, TITLE_PANEL_W, TITLE_PANEL_H, half, half_w, half_h, 0, /*mirrored=*/0);
    blit_half(
        panel, TITLE_PANEL_W, TITLE_PANEL_H, half, half_w, half_h, TITLE_PANEL_W - half_w,
        /*mirrored=*/1);
    ToriMem_Free(ToriMem_Frame(), half); /* the decode; the panel above holds the copy */
    return title_panel_sprite(panel, TITLE_PANEL_W, TITLE_PANEL_H);
}

struct ToriRS_Sprite*
ToriRS_TitleWidePanelFromPng(
    void const* data,
    int data_size)
{
    uint32_t* panel;
    uint32_t* half = NULL;
    int half_w = 0;
    int half_h = 0;
    int panel_w;

    assert(data);
    if( data_size <= 0 )
        return NULL;

    /* Argb, keeping the file's alpha (0xFF throughout titlewide.png): an
     * opaque backdrop, as JpegDecode_Argb writes title.jpg's -- not 0x00RRGGBB,
     * whose black would blit as the sprite's transparent colour. */
    if( !PngDecode_Argb(ToriMem_Frame(), data, data_size, &half_w, &half_h, &half) )
        return NULL;
    if( half_w <= 0 || half_h <= 0 )
    {
        ToriMem_Free(ToriMem_Frame(), half);
        return NULL;
    }

    /* client.method1991: the mirror at `x + width - 1`, so the halves share
     * their seam column exactly as title.jpg's do. */
    panel_w = half_w * 2 - 1;
    panel = ToriMem_Calloc(ToriMem_Assets(), (size_t)panel_w * half_h, sizeof(*panel));
    assert(panel);
    blit_half(panel, panel_w, half_h, half, half_w, half_h, 0, /*mirrored=*/0);
    blit_half(panel, panel_w, half_h, half, half_w, half_h, half_w - 1, /*mirrored=*/1);
    ToriMem_Free(ToriMem_Frame(), half);
    return title_panel_sprite(panel, panel_w, half_h);
}
