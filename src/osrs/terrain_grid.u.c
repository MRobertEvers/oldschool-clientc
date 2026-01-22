#ifndef TERRAIN_GRID_U_C
#define TERRAIN_GRID_U_C

#include "osrs/rscache/tables/maps.h"

struct TerrainGrid
{
    struct CacheMapTerrain* map_terrain[36];
    int mapx_sw;
    int mapz_sw;
    int mapx_ne;
    int mapz_ne;
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
    return (terrain_grid->mapx_ne - terrain_grid->mapx_sw + 1) * MAP_TERRAIN_X;
}

static int
terrain_grid_z_height(struct TerrainGrid* terrain_grid)
{
    return (terrain_grid->mapz_ne - terrain_grid->mapz_sw + 1) * MAP_TERRAIN_Z;
}

static int
terrain_grid_size(struct TerrainGrid* terrain_grid)
{
    int width = terrain_grid->mapx_ne - terrain_grid->mapx_sw + 1;
    int height = terrain_grid->mapz_ne - terrain_grid->mapz_sw + 1;

    return width * height * CHUNK_TILE_COUNT;
}

static bool
tile_in_bounds(
    struct TerrainGrid* terrain_grid,
    int mapx,
    int mapz,
    int cx_unbounded,
    int cz_unbounded)
{
    if( cx_unbounded >= MAP_TERRAIN_X )
    {
        cx_unbounded -= MAP_TERRAIN_X;
        mapx++;
    }
    if( cz_unbounded >= MAP_TERRAIN_Z )
    {
        cz_unbounded -= MAP_TERRAIN_Z;
        mapz++;
    }

    return mapx >= terrain_grid->mapx_sw && mapx <= terrain_grid->mapx_ne &&
           mapz >= terrain_grid->mapz_sw && mapz <= terrain_grid->mapz_ne;
}

static struct CacheMapFloor*
tile_at(
    struct TerrainGrid* terrain_grid,
    int mapx,
    int mapz,
    int cx_unbounded,
    int cz_unbounded,
    int clevel)
{
    mapx -= terrain_grid->mapx_sw;
    mapz -= terrain_grid->mapz_sw;

    if( cx_unbounded >= MAP_TERRAIN_X )
    {
        cx_unbounded -= MAP_TERRAIN_X;
        mapx++;
    }
    if( cz_unbounded >= MAP_TERRAIN_Z )
    {
        cz_unbounded -= MAP_TERRAIN_Z;
        mapz++;
    }

    int width = terrain_grid->mapx_ne - terrain_grid->mapx_sw + 1;

    int chunk_idx = mapz * width + mapx;

    int tile_idx = MAP_TILE_COORD(cx_unbounded, cz_unbounded, clevel);

    assert(tile_idx < CHUNK_TILE_COUNT);
    assert(
        chunk_idx < (terrain_grid->mapx_ne - terrain_grid->mapx_sw + 1) *
                        (terrain_grid->mapz_ne - terrain_grid->mapz_sw + 1));
    return &terrain_grid->map_terrain[chunk_idx]->tiles_xyz[tile_idx];
}

static struct CacheMapFloor*
tile_from_sw_origin(
    struct TerrainGrid* terrain_grid,
    int sx,
    int sz,
    int slevel)
{
    if( sx == 34 && sz == 98 )
    {
        printf("sx: %d, sz: %d, slevel: %d\n", sx, sz, slevel);
    }
    int mapx = sx / MAP_TERRAIN_X + terrain_grid->mapx_sw;
    int mapz = sz / MAP_TERRAIN_Z + terrain_grid->mapz_sw;

    int cx = sx % MAP_TERRAIN_X;
    int cz = sz % MAP_TERRAIN_Z;
    return tile_at(terrain_grid, mapx, mapz, cx, cz, slevel);
}

static void
tile_heights_at(
    struct TerrainGrid* terrain_grid,
    int mapx,
    int mapz,
    // Tile from the SW chunk
    int cx,
    int cz,
    int clevel,
    struct TileHeights* tile_heights)
{
    tile_heights->sw_height = tile_at(terrain_grid, mapx, mapz, cx, cz, clevel)->height;

    tile_heights->se_height = tile_heights->sw_height;
    if( tile_in_bounds(terrain_grid, mapx, mapz, cx + 1, cz) )
    {
        tile_heights->se_height = tile_at(terrain_grid, mapx, mapz, cx + 1, cz, clevel)->height;
    }

    tile_heights->ne_height = tile_heights->sw_height;
    if( tile_in_bounds(terrain_grid, mapx, mapz, cx + 1, cz + 1) )
    {
        tile_heights->ne_height = tile_at(terrain_grid, mapx, mapz, cx + 1, cz + 1, clevel)->height;
    }

    tile_heights->nw_height = tile_heights->sw_height;
    if( tile_in_bounds(terrain_grid, mapx, mapz, cx, cz + 1) )
    {
        tile_heights->nw_height = tile_at(terrain_grid, mapx, mapz, cx, cz + 1, clevel)->height;
    }

    int height_center = (tile_heights->sw_height + tile_heights->se_height +
                         tile_heights->ne_height + tile_heights->nw_height) >>
                        2;

    tile_heights->height_center = height_center;
}

static void
terrain_grid_offset_from_sw(
    struct TerrainGrid* terrain_grid,
    int mapx,
    int mapz,
    int cx,
    int cz,
    int clevel,
    struct TerrainGridOffsetFromSW* offset)
{
    assert(cx < MAP_TERRAIN_X && cx >= 0);
    assert(cz < MAP_TERRAIN_Z && cz >= 0);
    assert(clevel < MAP_TERRAIN_LEVELS && clevel >= 0);

    assert(mapx >= terrain_grid->mapx_sw && mapx <= terrain_grid->mapx_ne);
    assert(mapz >= terrain_grid->mapz_sw && mapz <= terrain_grid->mapz_ne);

    mapx -= terrain_grid->mapx_sw;
    mapz -= terrain_grid->mapz_sw;

    offset->x = mapx * MAP_TERRAIN_X + cx;
    offset->z = mapz * MAP_TERRAIN_Z + cz;
    offset->level = clevel;
}

#endif