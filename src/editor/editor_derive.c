#include "editor_derive.h"

#include "editor_doc.h"
#include "engine/torirs_types.h"

#include <rscache.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* ---- derivation --------------------------------------------------------- */

int
Editor_SquareDeriveTerrain(
    const struct Editor_Square* square,
    const struct RSCache* profile,
    struct ToriRS_MapTerrain* out_terrain)
{
    struct RSCache_MapTerrain* authored = NULL;
    struct RSCache_MapTerrain* derived = NULL;
    uint8_t* encoded = NULL;
    uint32_t bound;
    uint32_t written;
    int flags;

    assert(square);
    assert(profile);
    assert(out_terrain);

    flags = RSCache_MapTerrainFlags(profile);

    /* 1. Authored record -> the cache's terrain struct, unfixed. `height` here
     *    is the raw wire value (0 where the file said nothing), which is
     *    exactly what the decoder would have produced. */
    authored = malloc(sizeof(*authored));
    assert(authored);
    memset(authored, 0, sizeof(*authored));
    authored->map_x = square->map_x;
    authored->map_z = square->map_z;
    authored->is_fixedup = false;

    for( int level = 0; level < EDITOR_SQUARE_LEVELS; level++ )
    {
        for( int x = 0; x < EDITOR_SQUARE_X; x++ )
        {
            for( int z = 0; z < EDITOR_SQUARE_Z; z++ )
            {
                const struct Editor_Tile* tile = &square->tiles[Editor_TileIndex(x, z, level)];
                struct RSCache_MapFloor* floor =
                    &authored->tiles_xyz[RSCACHE_MAP_TILE_COORD(x, z, level)];

                floor->height = tile->has_height ? (int16_t)tile->height : 0;
                floor->height_authored = tile->has_height;
                floor->authored_height = tile->height;
                /* The attribute opcode is the stored form; shape and rotation
                 * are views on it. The encoder writes the opcode, so it is the
                 * one that has to be right. */
                floor->attr_opcode =
                    tile->has_overlay ? (uint8_t)(tile->shape * 4 + tile->rotation + 2) : 0;
                floor->overlay_id = tile->overlay_id;
                floor->shape = tile->shape;
                floor->rotation = tile->rotation;
                floor->settings = tile->settings;
                floor->underlay_id = tile->underlay_id;
            }
        }
    }

    if( square->trailing_size > 0 )
    {
        authored->trailing = malloc((size_t)square->trailing_size);
        assert(authored->trailing);
        memcpy(authored->trailing, square->trailing, (size_t)square->trailing_size);
        authored->trailing_size = square->trailing_size;
    }

    /* 2. Through the real codec, so the heights the renderer gets are the ones
     *    the game would have produced from this file. */
    bound = RSCache_MapTerrainEncodeBoundFor(authored, flags);
    encoded = malloc(bound ? bound : 1);
    assert(encoded);
    written = RSCache_MapTerrainEncode(authored, flags, encoded, bound);
    if( written == 0 )
    {
        free(encoded);
        RSCache_MapTerrainFree(authored);
        return 0;
    }

    derived = RSCache_MapTerrainNewFromDecodeFlags(
        (char*)encoded, (int)written, square->map_x, square->map_z, flags);
    free(encoded);
    RSCache_MapTerrainFree(authored);
    if( !derived )
        return 0;

    /* 3. Into the client's form. */
    out_terrain->map_x = square->map_x;
    out_terrain->map_z = square->map_z;
    for( int i = 0; i < EDITOR_SQUARE_TILES; i++ )
    {
        const struct RSCache_MapFloor* from = &derived->tiles_xyz[i];
        struct ToriRS_MapFloor* to = &out_terrain->tiles_xyz[i];

        to->overlay_id = from->overlay_id;
        to->underlay_id = from->underlay_id;
        to->height = from->height;
        to->settings = from->settings;
        to->shape = from->shape;
        to->rotation = from->rotation;
    }

    RSCache_MapTerrainFree(derived);
    return 1;
}

int
Editor_SquareDeriveLocs(
    const struct Editor_Square* square,
    struct ToriRS_MapLocs* out_locs)
{
    assert(square);
    assert(out_locs);

    out_locs->chunk_mapx = square->map_x;
    out_locs->chunk_mapz = square->map_z;
    out_locs->locs_count = square->loc_count;
    out_locs->locs = NULL;

    if( square->loc_count > 0 )
    {
        out_locs->locs = malloc((size_t)square->loc_count * sizeof(*out_locs->locs));
        assert(out_locs->locs);
        for( int i = 0; i < square->loc_count; i++ )
        {
            const struct Editor_Loc* from = &square->locs[i];
            struct ToriRS_MapLoc* to = &out_locs->locs[i];

            to->loc_id = from->loc_id;
            to->shape_select = from->shape;
            to->orientation = from->rotation;
            to->chunk_pos_x = from->x;
            to->chunk_pos_z = from->z;
            to->chunk_pos_level = from->level;
        }
    }
    return 1;
}
