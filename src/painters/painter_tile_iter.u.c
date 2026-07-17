#ifndef PAINTER_TILE_ITER_U_C
#define PAINTER_TILE_ITER_U_C

#include "painters_i.h"

struct TileIter
{
    struct Painter* painter;
    int min_x;
    int max_x;
    int min_z;
    int max_z;
    int min_s;
    int max_s;
    int s;
    int z;
    int x;
    int ti;
    int row_start;
    int valid;
};

static void
tile_iter_init(
    struct TileIter* it,
    struct Painter* painter,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int min_level,
    int max_level)
{
    it->painter = painter;
    it->min_x = min_draw_x;
    it->max_x = max_draw_x;
    it->min_z = min_draw_z;
    it->max_z = max_draw_z;
    it->min_s = min_level;
    it->max_s = max_level;

    if( min_draw_x >= max_draw_x || min_draw_z >= max_draw_z || min_level >= max_level )
    {
        it->valid = 0;
        return;
    }

    it->s = min_level;
    it->z = min_draw_z;
    it->x = min_draw_x;
    it->row_start = painter_coord_idx(painter, min_draw_x, min_draw_z, min_level);
    it->ti = it->row_start;
    it->valid = 1;
}

/* Writes draw x/z for the returned tile index when the pointers are non-NULL. */
static int
tile_iter_next(
    struct TileIter* it,
    int* out_x,
    int* out_z)
{
    if( !it->valid )
        return -1;

    if( out_x )
        *out_x = it->x;
    if( out_z )
        *out_z = it->z;

    int cur = it->ti;

    it->x++;
    if( it->x < it->max_x )
    {
        it->ti = step_idx_east(it->painter, it->ti);
    }
    else
    {
        it->x = it->min_x;
        it->z++;
        if( it->z < it->max_z )
        {
            it->row_start = step_idx_north(it->painter, it->row_start);
            it->ti = it->row_start;
        }
        else
        {
            it->z = it->min_z;
            it->s++;
            if( it->s < it->max_s )
            {
                it->row_start = painter_coord_idx(it->painter, it->min_x, it->min_z, it->s);
                it->ti = it->row_start;
            }
            else
                it->valid = 0;
        }
    }

    return cur;
}

#endif /* PAINTER_TILE_ITER_U_C */
