#include "torirs_chrome_panel_draw.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>

static int
panel_scaled(int value, int scale, int origin, int* out)
{
    int64_t const result = (int64_t)value * scale + origin;
    if( result < INT_MIN || result > INT_MAX )
        return 0;
    *out = (int)result;
    return 1;
}

static int
panel_rect_equal(
    struct ToriRSChromeRect const* a,
    struct ToriRSChromeRect const* b)
{
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

unsigned
ToriRSChromePanelDraw_Changes(
    int previous_valid,
    struct ToriRSChromeRect previous_region,
    struct ToriRSChromeRect previous_clip,
    int next_valid,
    struct ToriRSChromeRect next_region,
    struct ToriRSChromeRect next_clip)
{
    unsigned changes = 0;

    if( !next_valid )
    {
        if( !previous_valid )
            return 0;
        changes |= TORIRS_CHROME_PANEL_DRAW_HIDDEN;
        if( previous_clip.w > 0 || previous_clip.h > 0 )
            changes |= TORIRS_CHROME_PANEL_DRAW_CLIP;
        return changes;
    }
    if( !previous_valid )
        return TORIRS_CHROME_PANEL_DRAW_SIZE |
               TORIRS_CHROME_PANEL_DRAW_CLIP;
    if( previous_region.w != next_region.w ||
        previous_region.h != next_region.h )
        changes |= TORIRS_CHROME_PANEL_DRAW_SIZE;
    if( previous_region.x != next_region.x ||
        previous_region.y != next_region.y )
        changes |= TORIRS_CHROME_PANEL_DRAW_ORIGIN;
    if( !panel_rect_equal(&previous_clip, &next_clip) )
        changes |= TORIRS_CHROME_PANEL_DRAW_CLIP;
    return changes;
}

int
ToriRSChromePanelDraw_Transform(
    struct UITreeEntityOverlay const* item,
    int origin_x,
    int origin_y,
    int scale,
    struct ToriRSChromeRect visible_clip,
    struct UITreeEntityOverlay* out)
{
    struct ToriRSChromeRect clip = visible_clip;

    assert(item);
    assert(out);
    if( scale <= 0 || clip.w <= 0 || clip.h <= 0 )
        return 0;

    *out = *item;
    if( !panel_scaled(item->x, scale, origin_x, &out->x) ||
        !panel_scaled(item->y, scale, origin_y, &out->y) ||
        !panel_scaled(item->w, scale, 0, &out->w) ||
        !panel_scaled(item->h, scale, 0, &out->h) )
        return 0;
    if( out->line_width > 0 )
        out->line_width = (uint8_t)(scale > 255 / out->line_width
                                        ? 255
                                        : out->line_width * scale);

    if( item->clip_w > 0 && item->clip_h > 0 )
    {
        struct ToriRSChromeRect own;
        int right = clip.x + clip.w;
        int bottom = clip.y + clip.h;

        if( !panel_scaled(item->clip_x, scale, origin_x, &own.x) ||
            !panel_scaled(item->clip_y, scale, origin_y, &own.y) ||
            !panel_scaled(item->clip_w, scale, 0, &own.w) ||
            !panel_scaled(item->clip_h, scale, 0, &own.h) )
            return 0;
        if( own.x > clip.x )
            clip.x = own.x;
        if( own.y > clip.y )
            clip.y = own.y;
        if( own.x + own.w < right )
            right = own.x + own.w;
        if( own.y + own.h < bottom )
            bottom = own.y + own.h;
        clip.w = right - clip.x;
        clip.h = bottom - clip.y;
    }
    if( clip.w <= 0 || clip.h <= 0 )
        return 0;

    out->clip_x = clip.x;
    out->clip_y = clip.y;
    out->clip_w = clip.w;
    out->clip_h = clip.h;
    return 1;
}
