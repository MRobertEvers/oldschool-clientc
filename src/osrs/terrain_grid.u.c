#ifndef TERRAIN_GRID_U_C
#define TERRAIN_GRID_U_C

#include "osrs/rscache/tables/maps.h"

struct TerrainGrid
{
    struct CacheMapTerrain* map_terrain[196];
    int mapx_sw;
    int mapz_sw;
    int mapx_ne;
    int mapz_ne;
    int offset_x;
    int offset_z;
    int size_x;
    int size_z;
};

struct TileHeights
{
    int sw_height;
    int se_height;
    int ne_height;
    int nw_height;
    int height_center;
};

struct TerrainGridOffsetFromSW
{
    int x;
    int z;
    int level;
};

static int
terrain_grid_x_width(struct TerrainGrid* terrain_grid)
{
    return terrain_grid->size_x;
}

static int
terrain_grid_z_height(struct TerrainGrid* terrain_grid)
{
    return terrain_grid->size_z;
}

static int
terrain_grid_size(struct TerrainGrid* terrain_grid)
{
    int map_width = terrain_grid->mapx_ne - terrain_grid->mapx_sw + 1;
    int map_height = terrain_grid->mapz_ne - terrain_grid->mapz_sw + 1;

    return map_width * map_height * CHUNK_TILE_COUNT;
}

static void
terrain_grid_offset_from_sw_maploc(
    struct TerrainGrid* terrain_grid,
    int mapx,
    int mapz,
    int chunk_pos_x,
    int chunk_pos_z,
    int clevel,
    struct TerrainGridOffsetFromSW* offset)
{
    mapx -= terrain_grid->mapx_sw;
    mapz -= terrain_grid->mapz_sw;

    chunk_pos_x -= terrain_grid->offset_x;
    chunk_pos_z -= terrain_grid->offset_z;

    while( chunk_pos_x < 0 )
    {
        chunk_pos_x += MAP_TERRAIN_X;
        mapx--;
    }
    while( chunk_pos_z < 0 )
    {
        chunk_pos_z += MAP_TERRAIN_Z;
        mapz--;
    }

    chunk_pos_x += mapx * 64;
    chunk_pos_z += mapz * 64;

    offset->x = chunk_pos_x;
    offset->z = chunk_pos_z;
    offset->level = clevel;
}

static bool
tile_in_bounds_from_maploc(
    struct TerrainGrid* terrain_grid,
    int mapx,
    int mapz,
    // Assumes 0 is from the offset.
    int chunk_pos_x,
    int chunk_pos_z)
{
    struct TerrainGridOffsetFromSW offset;
    terrain_grid_offset_from_sw_maploc(
        terrain_grid, mapx, mapz, chunk_pos_x, chunk_pos_z, 0, &offset);
    return offset.x >= 0 && offset.x < terrain_grid->size_x && offset.z >= 0 &&
           offset.z < terrain_grid->size_z;
}

static bool
tile_in_bounds_from_sw(
    struct TerrainGrid* terrain_grid,
    int sx,
    int sz,
    int slevel)
{
    return sx >= 0 && sx < terrain_grid->size_x && sz >= 0 && sz < terrain_grid->size_z &&
           slevel >= 0 && slevel < MAP_TERRAIN_LEVELS;
}

// static struct CacheMapFloor*
// tile_at(
//     struct TerrainGrid* terrain_grid,
//     int mapx,
//     int mapz,
//     int cx_unbounded,
//     int cz_unbounded,
//     int clevel)
// {
//     mapx -= terrain_grid->mapx_sw;
//     mapz -= terrain_grid->mapz_sw;

//     cx_unbounded = cx_unbounded + terrain_grid->offset_x;
//     cz_unbounded = cz_unbounded + terrain_grid->offset_z;

//     while( cx_unbounded >= MAP_TERRAIN_X )
//     {
//         cx_unbounded -= MAP_TERRAIN_X;
//         mapx++;
//     }
//     while( cz_unbounded >= MAP_TERRAIN_Z )
//     {
//         cz_unbounded -= MAP_TERRAIN_Z;
//         mapz++;
//     }

//     int map_width = terrain_grid->mapx_ne - terrain_grid->mapx_sw + 1;

//     int chunk_idx = mapz * map_width + mapx;

//     int tile_idx = MAP_TILE_COORD(cx_unbounded, cz_unbounded, clevel);

//     assert(tile_idx < CHUNK_TILE_COUNT);
//     assert(
//         chunk_idx < (terrain_grid->mapx_ne - terrain_grid->mapx_sw + 1) *
//                         (terrain_grid->mapz_ne - terrain_grid->mapz_sw + 1));
//     return &terrain_grid->map_terrain[chunk_idx]->tiles_xyz[tile_idx];
// }

static struct CacheMapFloor*
tile_from_sw_origin(
    struct TerrainGrid* terrain_grid,
    int sx,
    int sz,
    int slevel)
{
    sx += terrain_grid->offset_x;
    sz += terrain_grid->offset_z;
    int mapx = 0;
    int mapz = 0;

    while( sx >= MAP_TERRAIN_X )
    {
        sx -= MAP_TERRAIN_X;
        mapx++;
    }
    while( sz >= MAP_TERRAIN_Z )
    {
        sz -= MAP_TERRAIN_Z;
        mapz++;
    }

    int map_width = terrain_grid->mapx_ne - terrain_grid->mapx_sw + 1;

    int chunk_idx = mapz * map_width + mapx;
    int tile_idx = MAP_TILE_COORD(sx, sz, slevel);
    assert(tile_idx < CHUNK_TILE_COUNT);
    return &terrain_grid->map_terrain[chunk_idx]->tiles_xyz[tile_idx];
}

static void
tile_heights_at_sized(
    struct TerrainGrid* terrain_grid,
    int sx,
    int sz,
    int slevel,
    int size_x,
    int size_z,
    struct TileHeights* tile_heights)
{
    int start_x = sx;
    int end_x = sx + 1;
    int start_z = sz;
    int end_z = sz + 1;

    // This is what OSRS uses.
    // Older revs do not.
    if( tile_in_bounds_from_sw(terrain_grid, sx + size_x, sz, slevel) )
    {
        start_x = (size_x >> 1) + sx;
        end_x = ((size_x + 1) >> 1) + sx;
    }

    if( tile_in_bounds_from_sw(terrain_grid, sx, sz + size_z, slevel) )
    {
        start_z = (size_z >> 1) + sz;
        end_z = ((size_z + 1) >> 1) + sz;
    }

    tile_heights->sw_height = tile_from_sw_origin(terrain_grid, start_x, start_z, slevel)->height;

    tile_heights->se_height = tile_heights->sw_height;
    if( tile_in_bounds_from_sw(terrain_grid, end_x, start_z, slevel) )
    {
        tile_heights->se_height = tile_from_sw_origin(terrain_grid, end_x, start_z, slevel)->height;
    }

    tile_heights->ne_height = tile_heights->sw_height;
    if( tile_in_bounds_from_sw(terrain_grid, end_x, end_z, slevel) )
    {
        tile_heights->ne_height = tile_from_sw_origin(terrain_grid, end_x, end_z, slevel)->height;
    }

    tile_heights->nw_height = tile_heights->sw_height;
    if( tile_in_bounds_from_sw(terrain_grid, start_x, end_z, slevel) )
    {
        tile_heights->nw_height = tile_from_sw_origin(terrain_grid, start_x, end_z, slevel)->height;
    }

    int height_center = (tile_heights->sw_height + tile_heights->se_height +
                         tile_heights->ne_height + tile_heights->nw_height) >>
                        2;

    tile_heights->height_center = height_center;
}
#endif