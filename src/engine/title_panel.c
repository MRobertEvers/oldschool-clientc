#include "engine/title_panel.h"

#include "engine/jpeg_decode.h"
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

/* Blit `src` into `dst` at (dst_x, 0), clipped, optionally mirrored. The only
 * two placements the reference uses are the unmirrored left and the mirrored
 * right. */
static void
blit_half(
    uint32_t* dst,
    uint32_t const* src,
    int src_w,
    int src_h,
    int dst_x,
    int mirrored)
{
    assert(dst);
    assert(src);

    for( int y = 0; y < src_h && y < TITLE_PANEL_H; y++ )
    {
        for( int x = 0; x < src_w; x++ )
        {
            int out_x = dst_x + (mirrored ? (src_w - 1 - x) : x);
            if( out_x < 0 || out_x >= TITLE_PANEL_W )
                continue;
            dst[(size_t)y * TITLE_PANEL_W + out_x] = src[(size_t)y * src_w + x];
        }
    }
}

struct ToriRS_Sprite*
ToriRS_TitlePanelFromJpeg(
    void const* data,
    int data_size)
{
    struct ToriRS_Sprite* sprite;
    struct ToriRS_SpriteFrame* frame;
    uint32_t* panel;
    uint32_t* half = NULL;
    int half_w = 0;
    int half_h = 0;

    assert(data);
    if( data_size <= 0 )
        return NULL;

    if( !JpegDecode_ArgbRsCache(data, data_size, &half_w, &half_h, &half) )
        return NULL;

    panel = calloc((size_t)TITLE_PANEL_W * TITLE_PANEL_H, sizeof(*panel));
    assert(panel);

    /* The stored half at the left, its mirror butted against the right edge.
     * Anchoring the mirror to the right edge rather than to the first half's
     * width is what makes the seam land on a shared column: at the reference's
     * 383 the two overlap by one, and a cache that stored a different width
     * still fills the panel instead of leaving a gap or running over. */
    blit_half(panel, half, half_w, half_h, 0, /*mirrored=*/0);
    blit_half(panel, half, half_w, half_h, TITLE_PANEL_W - half_w, /*mirrored=*/1);
    free(half);

    frame = calloc(1, sizeof(*frame));
    assert(frame);
    frame->pixels_argb = panel;
    frame->width = TITLE_PANEL_W;
    frame->height = TITLE_PANEL_H;
    frame->crop_width = TITLE_PANEL_W;
    frame->crop_height = TITLE_PANEL_H;

    sprite = calloc(1, sizeof(*sprite));
    assert(sprite);
    sprite->frames = frame;
    sprite->frame_count = 1;
    return sprite;
}
