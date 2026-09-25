#include "engine/boot_bar.h"

#include <assert.h>
#include <stddef.h>

/*
 * Written against the raw pixel buffer rather than through the renderer,
 * because this bar draws in the one window where the renderer's scene has
 * nothing in it yet. Both references do the same: Client-TS strokes and fills
 * the bare canvas, the deob draws into a 304x34 offscreen Image and blits it.
 */

/** A one-pixel outline of a w x h box. AWT's drawRect(x, y, w, h) covers w+1
 *  by h+1 pixels, so the reference's `drawRect(0, 0, 303, 33)` is this at
 *  304x34 and its `drawRect(1, 1, 301, 31)` is this at 302x32. */
static void
outline(
    uint32_t* pixels,
    int width,
    int height,
    int box_x,
    int box_y,
    int box_w,
    int box_h,
    uint32_t color)
{
    assert(pixels);
    if( box_w <= 0 || box_h <= 0 )
        return;

    for( int x = box_x; x < box_x + box_w; x++ )
    {
        if( x < 0 || x >= width )
            continue;
        if( box_y >= 0 && box_y < height )
            pixels[(size_t)box_y * width + x] = color;
        if( box_y + box_h - 1 >= 0 && box_y + box_h - 1 < height )
            pixels[(size_t)(box_y + box_h - 1) * width + x] = color;
    }
    for( int y = box_y; y < box_y + box_h; y++ )
    {
        if( y < 0 || y >= height )
            continue;
        if( box_x >= 0 && box_x < width )
            pixels[(size_t)y * width + box_x] = color;
        if( box_x + box_w - 1 >= 0 && box_x + box_w - 1 < width )
            pixels[(size_t)y * width + box_x + box_w - 1] = color;
    }
}

static void
fill(
    uint32_t* pixels,
    int width,
    int height,
    int box_x,
    int box_y,
    int box_w,
    int box_h,
    uint32_t color)
{
    assert(pixels);
    /* A zero-width fill is the ordinary state at 0% and, for the cover, at
     * 100%. Neither is a caller mistake, so both simply draw nothing. */
    if( box_w <= 0 || box_h <= 0 )
        return;

    for( int y = box_y; y < box_y + box_h; y++ )
    {
        if( y < 0 || y >= height )
            continue;
        for( int x = box_x; x < box_x + box_w; x++ )
        {
            if( x < 0 || x >= width )
                continue;
            pixels[(size_t)y * width + x] = color;
        }
    }
}

int
BootBar_OriginX(int width)
{
    return width / 2 - BOOT_BAR_W / 2;
}

int
BootBar_OriginY(int height)
{
    return height / 2 - BOOT_BAR_ABOVE_CENTRE;
}

void
BootBar_Draw(
    uint32_t* pixels,
    int width,
    int height,
    int percent)
{
    int bar_x;
    int bar_y;
    int fill_w;

    assert(pixels);
    assert(width > 0);
    assert(height > 0);

    if( percent < 0 )
        percent = 0;
    if( percent > 100 )
        percent = 100;

    bar_x = BootBar_OriginX(width);
    bar_y = BootBar_OriginY(height);
    fill_w = percent * BOOT_BAR_PX_PER_PERCENT;

    for( int i = 0; i < width * height; i++ )
        pixels[i] = 0x000000u;

    /*
     * Border, fill, then the black inset rule and the cover over what is not
     * filled yet. That order is the reference's and it matters: the rule is
     * drawn AFTER the fill and frames it, which is what gives the bar its
     * recessed look. Client-TS omits the rule; the deob draws it, and this
     * follows the deob.
     */
    outline(pixels, width, height, bar_x, bar_y, BOOT_BAR_W, BOOT_BAR_H, BOOT_BAR_COLOR);
    fill(
        pixels,
        width,
        height,
        bar_x + BOOT_BAR_INSET,
        bar_y + BOOT_BAR_INSET,
        fill_w,
        BOOT_BAR_FILL_H,
        BOOT_BAR_COLOR);
    outline(
        pixels,
        width,
        height,
        bar_x + 1,
        bar_y + 1,
        BOOT_BAR_W - 2,
        BOOT_BAR_H - 2,
        0x000000u);
    fill(
        pixels,
        width,
        height,
        bar_x + BOOT_BAR_INSET + fill_w,
        bar_y + BOOT_BAR_INSET,
        BOOT_BAR_FILL_W - fill_w,
        BOOT_BAR_FILL_H,
        0x000000u);
}
