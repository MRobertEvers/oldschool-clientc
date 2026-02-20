#include "shademap.h"

#include "rscache/tables/maps.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline int
shademap2_coord(
    struct Shademap2* shademap,
    int sx,
    int sz,
    int level)
{
    assert(sx >= 0 && sx < shademap->width);
    assert(sz >= 0 && sz < shademap->height);
    assert(level >= 0 && level < shademap->levels);
    return sx + sz * shademap->width + level * shademap->width * shademap->height;
}

struct Shademap2*
shademap2_new(
    int tile_width,
    int tile_height,
    int levels)
{
    struct Shademap2* shademap = malloc(sizeof(struct Shademap2));
    shademap->map = malloc(tile_width * tile_height * levels * sizeof(int));
    memset(shademap->map, 0, tile_width * tile_height * levels * sizeof(int));
    shademap->width = tile_width;
    shademap->height = tile_height;
    shademap->levels = levels;
    return shademap;
}

void
shademap2_free(struct Shademap2* shademap)
{
    free(shademap->map);
    free(shademap);
}

bool
shademap2_in_bounds(
    struct Shademap2* shademap,
    int sx,
    int sz,
    int level)
{
    return sx >= 0 && sx < shademap->width && sz >= 0 && sz < shademap->height && level >= 0 &&
           level < shademap->levels;
}

void
shademap2_set_wall_corner(
    struct Shademap2* shademap,
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
        if( shademap2_in_bounds(shademap, sx, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz + 1, slevel)] = shade;
    }
    break;
    case 1:
    {
        if( shademap2_in_bounds(shademap, sx + 1, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz + 1, slevel)] = shade;
    }
    break;
    case 2:
    {
        if( shademap2_in_bounds(shademap, sx + 1, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz, slevel)] = shade;
    }
    break;
    case 3:
    {
        if( shademap2_in_bounds(shademap, sx, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz, slevel)] = shade;
    }
    break;
    default:
    {
        assert(false && "Invalid orientation");
    }
    }
}

void
shademap2_set_wall(
    struct Shademap2* shademap,
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
        if( shademap2_in_bounds(shademap, sx, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz, slevel)] = shade;
        if( shademap2_in_bounds(shademap, sx, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz + 1, slevel)] = shade;
    }
    break;
    case 1:
    {
        if( shademap2_in_bounds(shademap, sx, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz + 1, slevel)] = shade;
        if( shademap2_in_bounds(shademap, sx + 1, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz + 1, slevel)] = shade;
    }
    break;
    case 2:
    {
        if( shademap2_in_bounds(shademap, sx + 1, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz, slevel)] = shade;
        if( shademap2_in_bounds(shademap, sx + 1, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz + 1, slevel)] = shade;
    }
    break;
    case 3:
    {
        if( shademap2_in_bounds(shademap, sx, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz, slevel)] = shade;
        if( shademap2_in_bounds(shademap, sx + 1, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz, slevel)] = shade;
    }
    break;
    default:
    {
        assert(false && "Invalid orientation");
    }
    }
}

void
shademap2_set_sized(
    struct Shademap2* shademap,
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
            if( shademap2_in_bounds(shademap, sx + x, sz + z, slevel) )
                shademap->map[shademap2_coord(shademap, sx + x, sz + z, slevel)] = shade;
        }
    }
}

int
shademap2_get(
    struct Shademap2* shademap,
    int sx,
    int sz,
    int slevel)
{
    assert(shademap2_in_bounds(shademap, sx, sz, slevel));
    return shademap->map[shademap2_coord(shademap, sx, sz, slevel)];
}
