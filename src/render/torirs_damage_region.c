#include "render/torirs_damage_region.h"

#include <assert.h>
#include <string.h>

void
ToriRS_DamageRegionReset(struct ToriRS_DamageRegion* region)
{
    assert(region);
    memset(region, 0, sizeof(*region));
}

/* Fold one area into the rect list. */
static void
torirs_damage_region_rect_add(
    struct ToriRS_DamageRegion* region,
    int x,
    int y,
    int w,
    int h)
{
    for( int i = 0; i < region->rect_count; i++ )
    {
        struct ToriRS_DamageRect* rect = &region->rects[i];
        int rect_x1 = rect->x + rect->w;
        int rect_y1 = rect->y + rect->h;

        if( x < rect_x1 && x + w > rect->x && y < rect_y1 && y + h > rect->y )
        {
            int merged_x = x < rect->x ? x : rect->x;
            int merged_y = y < rect->y ? y : rect->y;
            int merged_x1 = x + w > rect_x1 ? x + w : rect_x1;
            int merged_y1 = y + h > rect_y1 ? y + h : rect_y1;

            rect->x = merged_x;
            rect->y = merged_y;
            rect->w = merged_x1 - merged_x;
            rect->h = merged_y1 - merged_y;
            return;
        }
    }
    if( region->rect_count >= TORIRS_DAMAGE_RECT_MAX )
    {
        region->rect_count = -1; /* poisoned: too many, use the box */
        return;
    }
    if( region->rect_count < 0 )
        return;
    region->rects[region->rect_count].x = x;
    region->rects[region->rect_count].y = y;
    region->rects[region->rect_count].w = w;
    region->rects[region->rect_count].h = h;
    region->rect_count++;
}

/* Grow the box to cover [x, x+w) x [y, y+h). */
static void
torirs_damage_region_box_add(
    struct ToriRS_DamageRegion* region,
    int x,
    int y,
    int w,
    int h)
{
    int x1 = x + w;
    int y1 = y + h;

    if( !region->valid )
    {
        region->x = x;
        region->y = y;
        region->w = w;
        region->h = h;
        region->valid = 1;
        return;
    }
    if( x < region->x )
    {
        region->w += region->x - x;
        region->x = x;
    }
    if( y < region->y )
    {
        region->h += region->y - y;
        region->y = y;
    }
    if( x1 > region->x + region->w )
        region->w = x1 - region->x;
    if( y1 > region->y + region->h )
        region->h = y1 - region->y;
}

void
ToriRS_DamageRegionAdd(
    struct ToriRS_DamageRegion* region,
    int x,
    int y,
    int w,
    int h)
{
    assert(region);
    if( w <= 0 || h <= 0 )
        return;
    torirs_damage_region_box_add(region, x, y, w, h);
    torirs_damage_region_rect_add(region, x, y, w, h);
}

void
ToriRS_DamageRegionClamp(
    struct ToriRS_DamageRegion* region,
    int width,
    int height,
    bool keep_rects)
{
    assert(region);
    if( !region->valid )
        return;

    /* Clamp to the canvas; an added area may hang off an edge. */
    if( region->x < 0 )
    {
        region->w += region->x;
        region->x = 0;
    }
    if( region->y < 0 )
    {
        region->h += region->y;
        region->y = 0;
    }
    if( region->x + region->w > width )
        region->w = width - region->x;
    if( region->y + region->h > height )
        region->h = height - region->y;
    if( region->w <= 0 || region->h <= 0 )
        region->valid = 0;

    /* A box that covers the canvas anyway is not worth the extra clip test in
     * every draw, nor the second present path. */
    if( region->valid && region->w >= width && region->h >= height )
        region->valid = 0;

    if( !region->valid || region->rect_count <= 0 || !keep_rects )
    {
        region->rect_count = 0;
        return;
    }

    /* Clamp each rect the same way the box was, and drop the whole list if any
     * of it falls outside -- the box still covers those pixels, so the frame
     * stays correct, it just does the larger amount of work. */
    for( int i = 0; i < region->rect_count; i++ )
    {
        struct ToriRS_DamageRect* rect = &region->rects[i];

        if( rect->x < 0 )
        {
            rect->w += rect->x;
            rect->x = 0;
        }
        if( rect->y < 0 )
        {
            rect->h += rect->y;
            rect->y = 0;
        }
        if( rect->x + rect->w > width )
            rect->w = width - rect->x;
        if( rect->y + rect->h > height )
            rect->h = height - rect->y;
        if( rect->w <= 0 || rect->h <= 0 )
        {
            region->rect_count = 0;
            return;
        }
    }
}

int
ToriRS_DamageRegionBox(
    struct ToriRS_DamageRegion const* region,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    assert(region);
    assert(out_x);
    assert(out_y);
    assert(out_w);
    assert(out_h);
    if( !region->valid )
        return 0;
    *out_x = region->x;
    *out_y = region->y;
    *out_w = region->w;
    *out_h = region->h;
    return 1;
}

int
ToriRS_DamageRegionRects(
    struct ToriRS_DamageRegion const* region,
    struct ToriRS_DamageRect const** out_rects)
{
    assert(region);
    assert(out_rects);
    if( !region->valid || region->rect_count <= 0 )
        return 0;
    *out_rects = region->rects;
    return region->rect_count;
}
