#include "shademap.h"

#include <assert.h>
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
    if( !shademap )
        return NULL;
    shademap->map = malloc((size_t)tile_width * (size_t)tile_height * (size_t)levels * sizeof(int));
    if( !shademap->map )
    {
        free(shademap);
        return NULL;
    }
    memset(shademap->map, 0, (size_t)tile_width * (size_t)tile_height * (size_t)levels * sizeof(int));
    shademap->width = tile_width;
    shademap->height = tile_height;
    shademap->levels = levels;
    return shademap;
}

void
shademap2_free(struct Shademap2* shademap)
{
    if( !shademap )
        return;
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
    /* The shademap is build-only (freed at build end). A runtime loc spawn
     * (WorldBuilder_ApplyLocChange) has none, and doesn't need shade bake — all
     * shademap writers gate on this, so a NULL map makes them no-ops. */
    if( !shademap )
        return false;
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
        if( shademap2_in_bounds(shademap, sx, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz + 1, slevel)] = shade;
        break;
    case 1:
        if( shademap2_in_bounds(shademap, sx + 1, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz + 1, slevel)] = shade;
        break;
    case 2:
        if( shademap2_in_bounds(shademap, sx + 1, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz, slevel)] = shade;
        break;
    case 3:
        if( shademap2_in_bounds(shademap, sx, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz, slevel)] = shade;
        break;
    default:
        assert(false && "Invalid orientation");
        break;
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
    switch( orientation )
    {
    case 0:
        if( shademap2_in_bounds(shademap, sx, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz, slevel)] = shade;
        if( shademap2_in_bounds(shademap, sx, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz + 1, slevel)] = shade;
        break;
    case 1:
        if( shademap2_in_bounds(shademap, sx, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz + 1, slevel)] = shade;
        if( shademap2_in_bounds(shademap, sx + 1, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz + 1, slevel)] = shade;
        break;
    case 2:
        if( shademap2_in_bounds(shademap, sx + 1, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz, slevel)] = shade;
        if( shademap2_in_bounds(shademap, sx + 1, sz + 1, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz + 1, slevel)] = shade;
        break;
    case 3:
        if( shademap2_in_bounds(shademap, sx, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx, sz, slevel)] = shade;
        if( shademap2_in_bounds(shademap, sx + 1, sz, slevel) )
            shademap->map[shademap2_coord(shademap, sx + 1, sz, slevel)] = shade;
        break;
    default:
        assert(false && "Invalid orientation");
        break;
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
