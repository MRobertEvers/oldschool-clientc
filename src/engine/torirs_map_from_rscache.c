#include "engine/torirs_map_from_rscache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriRS_MapTerrain*
ToriRS_MapTerrainFromRSCache(
    int map_x,
    int map_z,
    const struct RSCache_MapTerrain* src)
{
    struct ToriRS_MapTerrain* terrain;
    int tile_count;

    assert(src);

    terrain = calloc(1, sizeof(*terrain));
    assert(terrain);

    terrain->map_x = map_x;
    terrain->map_z = map_z;

    tile_count = TORIRS_MAP_TERRAIN_X * TORIRS_MAP_TERRAIN_Z * TORIRS_MAP_TERRAIN_LEVELS;
    for( int i = 0; i < tile_count; i++ )
    {
        const struct RSCache_MapFloor* s = &src->tiles_xyz[i];
        struct ToriRS_MapFloor* d = &terrain->tiles_xyz[i];

        d->overlay_id = s->overlay_id;
        d->underlay_id = s->underlay_id;
        d->height = s->height;
        d->settings = s->settings;
        d->shape = s->shape;
        d->rotation = s->rotation;
        /* Carried, not derivable: once the fixup has run, `height` no longer
         * says whether the file authored one. Anything writing terrain back
         * reads these instead. */
        d->has_authored_height = s->height_authored;
        d->authored_height = s->authored_height;
    }

    return terrain;
}

struct ToriRS_MapLocs*
ToriRS_MapLocsFromRSCache(const struct RSCache_MapLocs* src)
{
    struct ToriRS_MapLocs* locs;

    assert(src);

    locs = calloc(1, sizeof(*locs));
    assert(locs);

    locs->chunk_mapx = src->chunk_mapx;
    locs->chunk_mapz = src->chunk_mapz;
    locs->locs_count = src->locs_count;

    if( src->locs_count > 0 && src->locs )
    {
        locs->locs = malloc((size_t)src->locs_count * sizeof(*locs->locs));
        assert(locs->locs);
        for( int i = 0; i < src->locs_count; i++ )
        {
            const struct RSCache_MapLoc* s = &src->locs[i];
            struct ToriRS_MapLoc* d = &locs->locs[i];

            d->loc_id = s->loc_id;
            d->shape_select = s->shape_select;
            d->orientation = s->orientation;
            d->chunk_pos_x = s->chunk_pos_x;
            d->chunk_pos_z = s->chunk_pos_z;
            d->chunk_pos_level = s->chunk_pos_level;
        }
    }

    return locs;
}
