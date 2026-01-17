#ifndef SCENEBUILDER_SHADEDMAP_U_C
#define SCENEBUILDER_SHADEDMAP_U_C

#include "rscache/tables/maps.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

struct Shademap
{
    int* map;
    int width;
    int height;
    int levels;
};

int
shademap_coord(
    struct Shademap* shademap,
    int sx,
    int sz,
    int level)
{
    return sx + sz * shademap->width + level * shademap->width * shademap->height;
}

static struct Shademap*
shademap_new(
    int tile_width,
    int tile_height,
    int levels)
{
    struct Shademap* shademap = malloc(sizeof(struct Shademap));
    shademap->map = malloc(tile_width * tile_height * levels * sizeof(int));
    memset(shademap->map, 0, tile_width * tile_height * levels * sizeof(int));
    shademap->width = tile_width;
    shademap->height = tile_height;
    shademap->levels = levels;
    return shademap;
}

static void
shademap_free(struct Shademap* shademap)
{
    free(shademap->map);
    free(shademap);
}

static bool
shademap_in_bounds(
    struct Shademap* shademap,
    int sx,
    int sz,
    int level)
{
    return sx >= 0 && sx < shademap->width && sz >= 0 && sz < shademap->height && level >= 0 &&
           level < shademap->levels;
}

static void
shademap_set_wall_corner(
    struct Shademap* shademap,
    int sx,
    int sz,
    int slevel,
    int orientation,
    int shade)
{
    switch( orientation )
    {
    case 0:
    {
        // TODO: Fix bounds
        if( shademap_in_bounds(shademap, sx, sz + 1, slevel) )
            shademap->map[shademap_coord(shademap, sx, sz + 1, slevel)] = shade;
    }
    break;
    case 1:
    {
        if( shademap_in_bounds(shademap, sx + 1, sz + 1, slevel) )
            shademap->map[shademap_coord(shademap, sx + 1, sz + 1, slevel)] = shade;
    }
    break;
    case 2:
    {
        if( shademap_in_bounds(shademap, sx + 1, sz, slevel) )
            shademap->map[shademap_coord(shademap, sx + 1, sz, slevel)] = shade;
    }
    break;
    case 3:
    {
        if( shademap_in_bounds(shademap, sx, sz, slevel) )
            shademap->map[shademap_coord(shademap, sx, sz, slevel)] = shade;
    }
    break;
    default:
    {
        assert(false && "Invalid orientation");
    }
    }
}

static void
shademap_set_wall(
    struct Shademap* shademap,
    int sx,
    int sz,
    int slevel,
    int orientation,
    int shade)
{
    // switch( orientation )
    //                 {
    //                 case 0:
    //                 {
    //                     shade_map[MAP_TILE_COORD(tile_x, tile_y, tile_z)] = 50;
    //                     if( tile_y < MAP_TERRAIN_Y - 1 )
    //                         shade_map[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)] = 50;
    //                 }
    //                 break;
    //                 case 1:
    //                 {
    //                     if( tile_y < MAP_TERRAIN_Y - 1 )
    //                         shade_map[MAP_TILE_COORD(tile_x, tile_y + 1, tile_z)] = 50;
    //                     if( tile_x < MAP_TERRAIN_X - 1 && tile_y < MAP_TERRAIN_Y - 1 )
    //                         shade_map[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)] = 50;
    //                 }
    //                 break;
    //                 case 2:
    //                 {
    //                     if( tile_x < MAP_TERRAIN_X - 1 )
    //                         shade_map[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)] = 50;
    //                     if( tile_x < MAP_TERRAIN_X - 1 && tile_y < MAP_TERRAIN_Y - 1 )
    //                         shade_map[MAP_TILE_COORD(tile_x + 1, tile_y + 1, tile_z)] = 50;
    //                 }
    //                 break;
    //                 case 3:
    //                 {
    //                     shade_map[MAP_TILE_COORD(tile_x, tile_y, tile_z)] = 50;
    //                     if( tile_x < MAP_TERRAIN_X - 1 )
    //                         shade_map[MAP_TILE_COORD(tile_x + 1, tile_y, tile_z)] = 50;
    //                 }
    //                 break;
    //                 default:
    //                 {
    //                     assert(false && "Invalid orientation");
    //                 }
    //                 }

    switch( orientation )
    {
    case 0:
    {
        if( shademap_in_bounds(shademap, sx, sz, slevel) )
            shademap->map[shademap_coord(shademap, sx, sz, slevel)] = shade;
        if( shademap_in_bounds(shademap, sx, sz + 1, slevel) )
            shademap->map[shademap_coord(shademap, sx, sz + 1, slevel)] = shade;
    }
    break;
    case 1:
    {
        if( shademap_in_bounds(shademap, sx, sz + 1, slevel) )
            shademap->map[shademap_coord(shademap, sx, sz + 1, slevel)] = shade;
        if( shademap_in_bounds(shademap, sx + 1, sz + 1, slevel) )
            shademap->map[shademap_coord(shademap, sx + 1, sz + 1, slevel)] = shade;
    }
    break;
    case 2:
    {
        if( shademap_in_bounds(shademap, sx + 1, sz, slevel) )
            shademap->map[shademap_coord(shademap, sx + 1, sz, slevel)] = shade;
        if( shademap_in_bounds(shademap, sx + 1, sz + 1, slevel) )
            shademap->map[shademap_coord(shademap, sx + 1, sz + 1, slevel)] = shade;
    }
    break;
    case 3:
    {
        if( shademap_in_bounds(shademap, sx, sz, slevel) )
            shademap->map[shademap_coord(shademap, sx, sz, slevel)] = shade;
        if( shademap_in_bounds(shademap, sx + 1, sz, slevel) )
            shademap->map[shademap_coord(shademap, sx + 1, sz, slevel)] = shade;
    }
    break;
    default:
    {
        assert(false && "Invalid orientation");
    }
    }
}

static void
shademap_set_sized(
    struct Shademap* shademap,
    int sx,
    int sz,
    int slevel,
    int size_x,
    int size_z,
    int shade)
{
    for( int x = 0; x < size_x; x++ )
    {
        for( int z = 0; z < size_z; z++ )
        {
            if( shademap_in_bounds(shademap, sx + x, sz + z, slevel) )
                shademap->map[shademap_coord(shademap, sx + x, sz + z, slevel)] = shade;
        }
    }
}

static int
shademap_get(
    struct Shademap* shademap,
    int sx,
    int sz,
    int slevel)
{
    assert(shademap_in_bounds(shademap, sx, sz, slevel));
    return shademap->map[shademap_coord(shademap, sx, sz, slevel)];
}

#endif