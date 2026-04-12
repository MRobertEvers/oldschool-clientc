#ifndef PAINTERS_BUCKET_U_C
#define PAINTERS_BUCKET_U_C

#include "painters.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Manhattan distance from camera to any tile in a 128-wide grid is in [0, 2*127]. */
#define BUCKET_DIST_RANGE (2 * 128 + 1)

struct PainterBucketCtx
{
    int* bucket_next; /* [tile_capacity]; intrusive list per bucket, -1 = end */
    int* dist;        /* [tile_capacity] Manhattan distance key */
    uint8_t* in_heap;
    int bucket_heads[BUCKET_DIST_RANGE];
    int bucket_max;
};

#define BM(P) ((struct PainterBucketCtx*)(P)->bucket_ctx)

static void
bucket_reset(struct PainterBucketCtx* w)
{
    for( int i = 0; i < BUCKET_DIST_RANGE; i++ )
        w->bucket_heads[i] = -1;
    w->bucket_max = -1;
}

static void
bucket_push(
    struct PainterBucketCtx* w,
    int ti)
{
    int d = w->dist[ti];
    assert(d >= 0 && d < BUCKET_DIST_RANGE);
    w->bucket_next[ti] = w->bucket_heads[d];
    w->bucket_heads[d] = ti;
    if( d > w->bucket_max )
        w->bucket_max = d;
}

/* Pop farthest distance first; LIFO within a bucket (matches reference). */
static int
bucket_pop(struct PainterBucketCtx* w)
{
    while( w->bucket_max >= 0 )
    {
        int head = w->bucket_heads[w->bucket_max];
        if( head < 0 )
        {
            w->bucket_max--;
            continue;
        }
        w->bucket_heads[w->bucket_max] = w->bucket_next[head];
        w->bucket_next[head] = -1;
        return head;
    }
    return -1;
}

static void
bucket_push_tile(
    struct PainterBucketCtx* w,
    int ti)
{
    if( w->in_heap[ti] )
        return;
    bucket_push(w, ti);
    w->in_heap[ti] = 1;
}

static inline int
bucket_is_north(
    int camera_sz,
    int tile_sz)
{
    return tile_sz < camera_sz;
}

static inline int
bucket_is_east(
    int camera_sx,
    int tile_sx)
{
    return tile_sx > camera_sx;
}

static inline int
bucket_is_south(
    int camera_sz,
    int tile_sz)
{
    return tile_sz > camera_sz;
}

static inline int
bucket_is_west(
    int camera_sx,
    int tile_sx)
{
    return tile_sx < camera_sx;
}

static int
bucket_ctx_init(struct Painter* painter)
{
    struct PainterBucketCtx* w =
        (struct PainterBucketCtx*)calloc(1, sizeof(struct PainterBucketCtx));
    if( !w )
        return -1;
    w->bucket_next = (int*)malloc((size_t)painter->tile_capacity * sizeof(int));
    w->dist = (int*)malloc((size_t)painter->tile_capacity * sizeof(int));
    w->in_heap = (uint8_t*)malloc((size_t)painter->tile_capacity);
    if( !w->bucket_next || !w->dist || !w->in_heap )
    {
        free(w->bucket_next);
        free(w->dist);
        free(w->in_heap);
        free(w);
        return -1;
    }
    painter->bucket_ctx = w;
    return 0;
}

static void
bucket_ctx_free(struct Painter* painter)
{
    struct PainterBucketCtx* w = BM(painter);
    if( !w )
        return;
    free(w->bucket_next);
    free(w->dist);
    free(w->in_heap);
    free(w);
    painter->bucket_ctx = NULL;
}

int
painter_paint_bucket(
    struct Painter* painter,
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel)
{
    (void)camera_slevel;

    struct PainterBucketCtx* w = BM(painter);
    struct PaintersTile* tile = NULL;
    struct PaintersElement* element = NULL;
    struct PaintersTile* bridge_underpass_tile = NULL;
    struct TilePaint* tile_paint = NULL;
    struct TilePaint* other_paint = NULL;
    struct ElementPaint* element_paint = NULL;

    buffer->command_count = 0;
    memset(painter->element_paints, 0x00, painter->element_count * sizeof(struct ElementPaint));

    int radius = 25;
    int max_level = 4;

    int max_draw_x = camera_sx + radius;
    int max_draw_z = camera_sz + radius;
    if( max_draw_x >= painter->width )
        max_draw_x = painter->width;
    if( max_draw_z >= painter->height )
        max_draw_z = painter->height;
    if( max_draw_x < 0 )
        max_draw_x = 0;
    if( max_draw_z < 0 )
        max_draw_z = 0;

    int min_draw_x = camera_sx - radius;
    int min_draw_z = camera_sz - radius;
    if( min_draw_x < 0 )
        min_draw_x = 0;
    if( min_draw_z < 0 )
        min_draw_z = 0;
    if( min_draw_x > painter->width )
        min_draw_x = painter->width;
    if( min_draw_z > painter->height )
        min_draw_z = painter->height;

    if( min_draw_x >= max_draw_x || min_draw_z >= max_draw_z )
        return 0;

    painter_clear_tile_paints_region(
        painter, min_draw_x, max_draw_x, min_draw_z, max_draw_z, max_level);

    memset(w->in_heap, 0, (size_t)painter->tile_capacity);

    for( int s = 0; s < max_level && s < painter->levels; s++ )
    {
        for( int z = min_draw_z; z < max_draw_z; z++ )
        {
            for( int x = min_draw_x; x < max_draw_x; x++ )
            {
                int ti = painter_coord_idx(painter, x, z, s);
                struct PaintersTile* t = &painter->tiles[ti];
                int tile_slevel = painters_tile_get_slevel(t);

                w->dist[ti] = abs(x - camera_sx) + abs(z - camera_sz);

                tile_paint = tile_paint_at(painter, x, z, s);
                if( (painters_tile_get_flags(t) & PAINTERS_TILE_FLAG_BRIDGE) != 0 ||
                    tile_slevel > max_level )
                {
                    tile_paint->step = PAINT_STEP_DONE;
                }
                else if( !painter_cullmap_tile_visible(
                             painter, tile_paint, x, z, camera_sx, camera_sz) )
                {
                    tile_paint->step = PAINT_STEP_DONE;
                }
                else
                {
                    tile_paint->step = PAINT_STEP_READY;
                }
            }
        }
    }

    bucket_reset(w);

    for( int s = 0; s < max_level && s < painter->levels; s++ )
    {
        for( int z = min_draw_z; z < max_draw_z; z++ )
        {
            for( int x = min_draw_x; x < max_draw_x; x++ )
            {
                int ti = painter_coord_idx(painter, x, z, s);
                tile_paint = tile_paint_at(painter, x, z, s);
                if( tile_paint->step != PAINT_STEP_READY )
                    continue;
                bucket_push(w, ti);
                w->in_heap[ti] = 1;
            }
        }
    }

    for( ;; )
    {
        int e_tile = bucket_pop(w);
        if( e_tile < 0 )
            break;

        tile = &painter->tiles[e_tile];
        w->in_heap[e_tile] = 0;

        int tile_sx = PAINTER_TILE_X(painter, tile);
        int tile_sz = PAINTER_TILE_Z(painter, tile);
        int tile_slevel = painters_tile_get_slevel(tile);
        tile_paint = tile_paint_at(painter, tile_sx, tile_sz, tile_slevel);

        if( tile_paint->step == PAINT_STEP_DONE )
            continue;

        if( (painters_tile_get_flags(tile) & PAINTERS_TILE_FLAG_BRIDGE) != 0 ||
            tile_slevel > max_level )
        {
            tile_paint->step = PAINT_STEP_DONE;
            continue;
        }

        if( !painter_cullmap_tile_visible(
                painter, tile_paint, tile_sx, tile_sz, camera_sx, camera_sz) )
        {
            tile_paint->step = PAINT_STEP_DONE;
            if( tile_slevel < painter->levels - 1 )
            {
                other_paint = tile_paint_at(painter, tile_sx, tile_sz, tile_slevel + 1);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(
                        w, painter_coord_idx(painter, tile_sx, tile_sz, tile_slevel + 1));
            }
            if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
            {
                other_paint = tile_paint_at(painter, tile_sx + 1, tile_sz, tile_slevel);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(
                        w, painter_coord_idx(painter, tile_sx + 1, tile_sz, tile_slevel));
            }
            if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
            {
                other_paint = tile_paint_at(painter, tile_sx - 1, tile_sz, tile_slevel);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(
                        w, painter_coord_idx(painter, tile_sx - 1, tile_sz, tile_slevel));
            }
            if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
            {
                other_paint = tile_paint_at(painter, tile_sx, tile_sz + 1, tile_slevel);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(
                        w, painter_coord_idx(painter, tile_sx, tile_sz + 1, tile_slevel));
            }
            if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
            {
                other_paint = tile_paint_at(painter, tile_sx, tile_sz - 1, tile_slevel);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(
                        w, painter_coord_idx(painter, tile_sx, tile_sz - 1, tile_slevel));
            }
            continue;
        }

        assert(tile_sx >= min_draw_x);
        assert(tile_sx < max_draw_x);
        assert(tile_sz >= min_draw_z);
        assert(tile_sz < max_draw_z);

        if( tile_paint->step == PAINT_STEP_READY )
        {
            if( tile_slevel > 0 )
            {
                other_paint = tile_paint_at(painter, tile_sx, tile_sz, tile_slevel - 1);
                if( other_paint->step != PAINT_STEP_DONE )
                    continue;
            }

            /* Reference NOT_VISITED deps; span bits match painter_bucket.c / tile->spans. */
            if( bucket_is_north(camera_sz, tile_sz) )
            {
                if( tile_is_south_inbounds(tile_sz, camera_sz, min_draw_z) )
                {
                    other_paint = tile_paint_at(painter, tile_sx, tile_sz - 1, tile_slevel);
                    if( other_paint->step != PAINT_STEP_DONE )
                    {
                        if( (tile->spans & SPAN_FLAG_SOUTH) == 0 )
                            continue;
                    }
                }
            }
            if( bucket_is_east(camera_sx, tile_sx) )
            {
                if( tile_is_east_inbounds(tile_sx, camera_sx, max_draw_x) )
                {
                    other_paint = tile_paint_at(painter, tile_sx + 1, tile_sz, tile_slevel);
                    if( other_paint->step != PAINT_STEP_DONE )
                    {
                        if( (tile->spans & SPAN_FLAG_EAST) == 0 )
                            continue;
                    }
                }
            }
            if( bucket_is_south(camera_sz, tile_sz) )
            {
                if( tile_is_north_inbounds(tile_sz, camera_sz, max_draw_z) )
                {
                    other_paint = tile_paint_at(painter, tile_sx, tile_sz + 1, tile_slevel);
                    if( other_paint->step != PAINT_STEP_DONE )
                    {
                        if( (tile->spans & SPAN_FLAG_NORTH) == 0 )
                            continue;
                    }
                }
            }
            if( bucket_is_west(camera_sx, tile_sx) )
            {
                if( tile_is_west_inbounds(tile_sx, camera_sx, min_draw_x) )
                {
                    other_paint = tile_paint_at(painter, tile_sx - 1, tile_sz, tile_slevel);
                    if( other_paint->step != PAINT_STEP_DONE )
                    {
                        if( (tile->spans & SPAN_FLAG_WEST) == 0 )
                            continue;
                    }
                }
            }

            int far_walls = far_wall_flags(camera_sx, camera_sz, tile_sx, tile_sz);
            tile_paint->near_wall_flags |= (uint8_t)~far_walls;

            if( tile->bridge_tile != -1 )
            {
                bridge_underpass_tile = &painter->tiles[tile->bridge_tile];
                push_command_terrain(
                    buffer,
                    PAINTER_TILE_X(painter, bridge_underpass_tile),
                    PAINTER_TILE_Z(painter, bridge_underpass_tile),
                    painters_tile_get_terrain_slevel(bridge_underpass_tile));

                if( bridge_underpass_tile->wall_a != -1 )
                {
                    element = &painter->elements[bridge_underpass_tile->wall_a];
                    assert(element->kind == PNTRELEM_WALL_A);
                    push_command_entity(buffer, element->_wall.entity);
                }

                for( int32_t sn = bridge_underpass_tile->scenery_head; sn != -1;
                     sn = painter->scenery_pool[sn].next )
                {
                    int scenery_element = painter->scenery_pool[sn].element_idx;
                    element_paint = &painter->element_paints[scenery_element];
                    if( element_paint->drawn )
                        continue;

                    element = &painter->elements[scenery_element];
                    assert(element->kind == PNTRELEM_SCENERY);
                    push_command_entity(buffer, element->_scenery.entity);

                    element_paint->drawn = true;
                }
            }

            push_command_terrain(buffer, tile_sx, tile_sz, painters_tile_get_terrain_slevel(tile));

            if( tile->wall_a != -1 )
            {
                element = &painter->elements[tile->wall_a];
                assert(element->kind == PNTRELEM_WALL_A);
                if( (element->_wall.side & far_walls) != 0 )
                    push_command_entity(buffer, element->_wall.entity);
            }

            if( tile->wall_b != -1 )
            {
                element = &painter->elements[tile->wall_b];
                assert(element->kind == PNTRELEM_WALL_B);
                if( (element->_wall.side & far_walls) != 0 )
                    push_command_entity(buffer, element->_wall.entity);
            }

            if( tile->ground_decor != -1 )
            {
                element = &painter->elements[tile->ground_decor];
                assert(element->kind == PNTRELEM_GROUND_DECOR);
                push_command_entity(buffer, element->_ground_decor.entity);
            }

            if( tile->ground_object_bottom != -1 )
            {
                element = &painter->elements[tile->ground_object_bottom];
                assert(element->kind == PNTRELEM_GROUND_OBJECT);
                push_command_entity(buffer, element->_ground_object.entity);
            }

            if( tile->wall_decor_a != -1 )
            {
                element = &painter->elements[tile->wall_decor_a];
                assert(element->kind == PNTRELEM_WALL_DECOR);
                if( element->_wall_decor._bf_through_wall_flags != 0 )
                {
                    int x_diff = element->sx - camera_sx;
                    int z_diff = element->sz - camera_sz;

                    int x_near = x_diff;
                    if( element->_wall_decor._bf_side == WALL_CORNER_NORTHEAST ||
                        element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST )
                        x_near = -x_diff;

                    int z_near = z_diff;
                    if( element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST ||
                        element->_wall_decor._bf_side == WALL_CORNER_SOUTHWEST )
                        z_near = -z_diff;

                    if( z_near < x_near )
                        push_command_entity(buffer, element->_wall_decor.entity);
                    else if( tile->wall_decor_b != -1 )
                    {
                        element = &painter->elements[tile->wall_decor_b];
                        assert(element->kind == PNTRELEM_WALL_DECOR);
                        push_command_entity(buffer, element->_wall_decor.entity);
                    }
                }
                else if( (element->_wall_decor._bf_side & far_walls) != 0 )
                {
                    push_command_entity(buffer, element->_wall_decor.entity);
                }
            }
            else
            {
                assert(tile->wall_decor_b == -1);
            }

            tile_paint->step = PAINT_STEP_GROUND;
        }

        /* PAINT_STEP_GROUND == reference PAINTER_STEP_BASE for scenery / completion. */
        int visit_sc[64];
        int n_visit = 0;

        for( int32_t sn = tile->scenery_head; sn != -1; sn = painter->scenery_pool[sn].next )
        {
            int si = painter->scenery_pool[sn].element_idx;
            element_paint = &painter->element_paints[si];
            if( element_paint->drawn )
                continue;

            element = &painter->elements[si];
            assert(element->kind == PNTRELEM_SCENERY);

            int el_slevel = (int)element->slevel;
            int min_tile_x = (int)element->sx;
            int min_tile_z = (int)element->sz;
            int max_tile_x = min_tile_x + element->_scenery.size_x - 1;
            int max_tile_z = min_tile_z + element->_scenery.size_z - 1;

            if( max_tile_x > max_draw_x - 1 )
                max_tile_x = max_draw_x - 1;
            if( max_tile_z > max_draw_z - 1 )
                max_tile_z = max_draw_z - 1;
            if( min_tile_x < min_draw_x )
                min_tile_x = min_draw_x;
            if( min_tile_z < min_draw_z )
                min_tile_z = min_draw_z;

            if( min_tile_x > max_tile_x || min_tile_z > max_tile_z )
            {
                element_paint->drawn = true;
                continue;
            }

            int all_base = 1;
            for( int ox = min_tile_x; ox <= max_tile_x; ox++ )
            {
                for( int oz = min_tile_z; oz <= max_tile_z; oz++ )
                {
                    if( ox < 0 || oz < 0 || ox >= painter->width || oz >= painter->height )
                    {
                        all_base = 0;
                        break;
                    }
                    struct TilePaint* u = tile_paint_at(painter, ox, oz, el_slevel);
                    if( u->step < PAINT_STEP_GROUND )
                    {
                        all_base = 0;
                        break;
                    }
                }
                if( !all_base )
                    break;
            }

            if( all_base && n_visit < (int)(sizeof(visit_sc) / sizeof(visit_sc[0])) )
                visit_sc[n_visit++] = si;
        }

        int some_drawn = 0;
        for( int vi = 0; vi < n_visit; vi++ )
        {
            int si = visit_sc[vi];
            element_paint = &painter->element_paints[si];
            element_paint->drawn = true;

            element = &painter->elements[si];
            assert(element->kind == PNTRELEM_SCENERY);
            push_command_entity(buffer, element->_scenery.entity);

            int el_slevel = (int)element->slevel;
            int min_tile_x = (int)element->sx;
            int min_tile_z = (int)element->sz;
            int max_tile_x = min_tile_x + element->_scenery.size_x - 1;
            int max_tile_z = min_tile_z + element->_scenery.size_z - 1;

            if( max_tile_x > max_draw_x - 1 )
                max_tile_x = max_draw_x - 1;
            if( max_tile_z > max_draw_z - 1 )
                max_tile_z = max_draw_z - 1;
            if( min_tile_x < min_draw_x )
                min_tile_x = min_draw_x;
            if( min_tile_z < min_draw_z )
                min_tile_z = min_draw_z;

            if( min_tile_x <= max_tile_x && min_tile_z <= max_tile_z )
            {
                for( int ox = min_tile_x; ox <= max_tile_x; ox++ )
                {
                    for( int oz = min_tile_z; oz <= max_tile_z; oz++ )
                    {
                        if( ox < 0 || oz < 0 || ox >= painter->width || oz >= painter->height )
                            continue;
                        bucket_push_tile(w, painter_coord_idx(painter, ox, oz, el_slevel));
                        some_drawn = 1;
                    }
                }
            }
        }
        if( some_drawn )
            continue;

        int all_sc_done = 1;
        for( int32_t sn = tile->scenery_head; sn != -1; sn = painter->scenery_pool[sn].next )
        {
            int si = painter->scenery_pool[sn].element_idx;
            if( !painter->element_paints[si].drawn )
            {
                all_sc_done = 0;
                break;
            }
        }
        if( !all_sc_done )
            continue;

        if( tile->wall_decor_a != -1 )
        {
            element = &painter->elements[tile->wall_decor_a];
            assert(element->kind == PNTRELEM_WALL_DECOR);

            if( element->_wall_decor._bf_through_wall_flags != 0 )
            {
                int x_diff = element->sx - camera_sx;
                int z_diff = element->sz - camera_sz;

                int x_near = x_diff;
                if( element->_wall_decor._bf_side == WALL_CORNER_NORTHEAST ||
                    element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST )
                    x_near = -x_diff;

                int z_near = z_diff;
                if( element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST ||
                    element->_wall_decor._bf_side == WALL_CORNER_SOUTHWEST )
                    z_near = -z_diff;

                if( z_near >= x_near )
                    push_command_entity(buffer, element->_wall_decor.entity);
                else if( tile->wall_decor_b != -1 )
                {
                    element = &painter->elements[tile->wall_decor_b];
                    assert(element->kind == PNTRELEM_WALL_DECOR);
                    push_command_entity(buffer, element->_wall_decor.entity);
                }
            }
            else if( (element->_wall_decor._bf_side & tile_paint->near_wall_flags) != 0 )
            {
                push_command_entity(buffer, element->_wall_decor.entity);
            }
        }

        if( tile->wall_a != -1 )
        {
            element = &painter->elements[tile->wall_a];
            assert(element->kind == PNTRELEM_WALL_A);
            if( (element->_wall.side & tile_paint->near_wall_flags) != 0 )
                push_command_entity(buffer, element->_wall.entity);
        }

        if( tile->wall_b != -1 )
        {
            element = &painter->elements[tile->wall_b];
            assert(element->kind == PNTRELEM_WALL_B);
            if( (element->_wall.side & tile_paint->near_wall_flags) != 0 )
                push_command_entity(buffer, element->_wall.entity);
        }

        tile_paint->step = PAINT_STEP_DONE;

        if( tile_slevel < painter->levels - 1 )
        {
            other_paint = tile_paint_at(painter, tile_sx, tile_sz, tile_slevel + 1);
            if( other_paint->step != PAINT_STEP_DONE )
                bucket_push_tile(w, painter_coord_idx(painter, tile_sx, tile_sz, tile_slevel + 1));
        }

        if( bucket_is_north(camera_sz, tile_sz) )
        {
            if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
            {
                other_paint = tile_paint_at(painter, tile_sx, tile_sz + 1, tile_slevel);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(
                        w, painter_coord_idx(painter, tile_sx, tile_sz + 1, tile_slevel));
            }
        }
        if( bucket_is_east(camera_sx, tile_sx) )
        {
            if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
            {
                other_paint = tile_paint_at(painter, tile_sx - 1, tile_sz, tile_slevel);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(
                        w, painter_coord_idx(painter, tile_sx - 1, tile_sz, tile_slevel));
            }
        }
        if( bucket_is_south(camera_sz, tile_sz) )
        {
            if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
            {
                other_paint = tile_paint_at(painter, tile_sx, tile_sz - 1, tile_slevel);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(
                        w, painter_coord_idx(painter, tile_sx, tile_sz - 1, tile_slevel));
            }
        }
        if( bucket_is_west(camera_sx, tile_sx) )
        {
            if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
            {
                other_paint = tile_paint_at(painter, tile_sx + 1, tile_sz, tile_slevel);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(
                        w, painter_coord_idx(painter, tile_sx + 1, tile_sz, tile_slevel));
            }
        }
    }

    return 0;
}

#endif /* PAINTERS_BUCKET_U_C */
