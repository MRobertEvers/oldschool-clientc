#include "game/rs_worldmap_view.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int
RS_WorldMapVisit_Compare(struct RS_WorldMapVisit const* lhs, struct RS_WorldMapVisit const* rhs)
{
    assert(lhs);
    assert(rhs);
    /* Nearest first -- that is the budget. */
    if( lhs->distance != rhs->distance )
        return lhs->distance < rhs->distance ? -1 : 1;
    /* And then by region, so two regions the same distance away are ordered at
     * all. Left to the sort, the pair swap places whenever the sort feels like
     * it, and at a steady zoom each keeps taking the frame's allowance from the
     * other: two tiles that never finish loading while nothing is moving. */
    if( lhs->region_y != rhs->region_y )
        return lhs->region_y - rhs->region_y;
    return lhs->region_x - rhs->region_x;
}

static int
rs_worldmap_visit_cmp(void const* lhs, void const* rhs)
{
    return RS_WorldMapVisit_Compare(
        (struct RS_WorldMapVisit const*)lhs, (struct RS_WorldMapVisit const*)rhs);
}

int
RS_WorldMapVisits_Build(
    struct RS_WorldMapVisits* out,
    struct RS_WorldMapRegionBounds const* bounds,
    struct RS_WorldMapViewport const* viewport)
{
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int min_region_x;
    int max_region_x;
    int min_region_y;
    int max_region_y;
    int region_x;
    int region_y;

    assert(out);
    assert(bounds);
    assert(viewport);

    out->count = 0;
    /* A box with no area, or a zoom that draws a region as no pixels: a real
     * state while a layout is still being sized, and one where every number
     * below would divide by zero or come back meaningless. */
    if( viewport->region_px <= 0 || viewport->box_w <= 0 || viewport->box_h <= 0 )
        return 0;

    /* Half a box either way, plus one whole region of slack, so the region
     * that is only partly on screen at the edge is still drawn. */
    min_x = viewport->display_tile_x -
            viewport->box_w * RS_WORLDMAP_REGION_TILES_X / (2 * viewport->region_px) -
            RS_WORLDMAP_REGION_TILES_X;
    max_x = viewport->display_tile_x +
            viewport->box_w * RS_WORLDMAP_REGION_TILES_X / (2 * viewport->region_px) +
            RS_WORLDMAP_REGION_TILES_X;
    min_y = viewport->display_tile_z -
            viewport->box_h * RS_WORLDMAP_REGION_TILES_Z / (2 * viewport->region_px) -
            RS_WORLDMAP_REGION_TILES_Z;
    max_y = viewport->display_tile_z +
            viewport->box_h * RS_WORLDMAP_REGION_TILES_Z / (2 * viewport->region_px) +
            RS_WORLDMAP_REGION_TILES_Z;

    min_region_x = min_x / RS_WORLDMAP_REGION_TILES_X;
    max_region_x = max_x / RS_WORLDMAP_REGION_TILES_X;
    min_region_y = min_y / RS_WORLDMAP_REGION_TILES_Z;
    max_region_y = max_y / RS_WORLDMAP_REGION_TILES_Z;
    /* Nothing outside the area: a region beyond its bounds has no terrain to
     * bake, and asking for one spends the frame's allowance on nothing. */
    if( min_region_x < bounds->low_x )
        min_region_x = bounds->low_x;
    if( max_region_x > bounds->high_x )
        max_region_x = bounds->high_x;
    if( min_region_y < bounds->low_y )
        min_region_y = bounds->low_y;
    if( max_region_y > bounds->high_y )
        max_region_y = bounds->high_y;

    for( region_y = min_region_y; region_y <= max_region_y; region_y++ )
    {
        for( region_x = min_region_x; region_x <= max_region_x; region_x++ )
        {
            struct RS_WorldMapVisit* visit;
            int centre_tile_x = region_x * RS_WORLDMAP_REGION_TILES_X + RS_WORLDMAP_REGION_TILES_X / 2;
            int centre_tile_y = region_y * RS_WORLDMAP_REGION_TILES_Z + RS_WORLDMAP_REGION_TILES_Z / 2;
            int dx = centre_tile_x - viewport->display_tile_x;
            int dy = centre_tile_y - viewport->display_tile_z;

            if( out->count >= RS_WORLDMAP_VISITS_MAX )
                break;
            visit = &out->items[out->count++];
            visit->region_x = region_x;
            visit->region_y = region_y;
            /* From the region's CENTRE, not its corner: measured from the
             * corner, the region the view is standing in loses to its
             * neighbour up and to the left on half the map. */
            visit->distance = dx * dx + dy * dy;
        }
    }

    qsort(out->items, (size_t)out->count, sizeof(out->items[0]), rs_worldmap_visit_cmp);
    return out->count;
}

void
RS_WorldMapTiles_Reset(struct RS_WorldMapTiles* tiles)
{
    assert(tiles);
    tiles->count = 0;
}

struct UITreeWorldMapTile*
RS_WorldMapTiles_Push(struct RS_WorldMapTiles* tiles)
{
    struct UITreeWorldMapTile* tile;

    assert(tiles);
    if( tiles->count >= RS_WORLDMAP_TILES_MAX )
        return NULL;
    tile = &tiles->items[tiles->count++];
    memset(tile, 0, sizeof(*tile));
    return tile;
}
