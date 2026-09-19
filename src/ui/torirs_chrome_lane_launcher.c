#include "torirs_chrome_lane_launcher.h"

#include <assert.h>
#include <string.h>

int
ToriRSLaneLauncher_Pitch(int anchor_y, int previous_y, int anchor_h)
{
    assert(anchor_h > 0);
    if( previous_y < 0 || previous_y >= anchor_y )
        return anchor_h;
    return anchor_y - previous_y;
}

int
ToriRSLaneLauncher_Y(int bottom, int anchor_h, int pitch)
{
    assert(anchor_h > 0);
    assert(pitch > 0);
    /* The last shown stone's TOP, plus one pitch. `bottom - anchor_h` is that
     * top whenever the column's stones are the anchor's size, which is what a
     * stone column is. */
    return bottom - anchor_h + pitch;
}

/** src-over, with `src`'s alpha. Both premultiplied by nothing. */
static uint32_t
lane_launcher_blend(uint32_t dst, uint32_t src)
{
    uint32_t const alpha = src >> 24;
    uint32_t const inverse = 255u - alpha;

    if( alpha == 255u )
        return src;
    if( alpha == 0u )
        return dst;
    {
        uint32_t const dst_a = dst >> 24;
        uint32_t const out_a = alpha + ((dst_a * inverse) / 255u);
        uint32_t const red =
            ((((src >> 16) & 0xFFu) * alpha) + (((dst >> 16) & 0xFFu) * inverse)) / 255u;
        uint32_t const green =
            ((((src >> 8) & 0xFFu) * alpha) + (((dst >> 8) & 0xFFu) * inverse)) / 255u;
        uint32_t const blue = (((src & 0xFFu) * alpha) + ((dst & 0xFFu) * inverse)) / 255u;
        return (out_a << 24) | (red << 16) | (green << 8) | blue;
    }
}

void
ToriRSLaneLauncher_Compose(
    uint32_t const* backing,
    int backing_w,
    int backing_h,
    uint32_t const* icon,
    int icon_w,
    int icon_h,
    int out_w,
    int out_h,
    int icon_x,
    int icon_y,
    int icon_box_w,
    int icon_box_h,
    uint32_t* out)
{
    assert(backing);
    assert(backing_w > 0);
    assert(backing_h > 0);
    assert(icon);
    assert(icon_w > 0);
    assert(icon_h > 0);
    assert(out);
    assert(out_w > 0);
    assert(out_h > 0);
    assert(out_w <= TORIRS_LANE_LAUNCHER_MAX_W);
    assert(out_h <= TORIRS_LANE_LAUNCHER_MAX_H);
    assert(icon_box_w > 0);
    assert(icon_box_h > 0);

    for( int row = 0; row < out_h; row++ )
    {
        int const source_row = row * backing_h / out_h;
        for( int column = 0; column < out_w; column++ )
        {
            int const source_column = column * backing_w / out_w;
            out[(row * out_w) + column] = backing[(source_row * backing_w) + source_column];
        }
    }

    for( int row = 0; row < icon_box_h; row++ )
    {
        int const target_row = icon_y + row;
        int const source_row = row * icon_h / icon_box_h;

        /* The glyph box comes off a laid-out node and the button box off
         * another; a frame caught mid-relayout can hand us one that hangs over
         * the edge. Skipped, not asserted -- it is live geometry, not a
         * caller's promise. */
        if( target_row < 0 || target_row >= out_h )
            continue;
        for( int column = 0; column < icon_box_w; column++ )
        {
            int const target_column = icon_x + column;
            int const source_column = column * icon_w / icon_box_w;
            uint32_t* pixel;

            if( target_column < 0 || target_column >= out_w )
                continue;
            pixel = &out[(target_row * out_w) + target_column];
            *pixel = lane_launcher_blend(*pixel, icon[(source_row * icon_w) + source_column]);
        }
    }
}
