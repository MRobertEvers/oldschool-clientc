#ifndef PAINTERS_DISTANCEMETRIC_U_C
#define PAINTERS_DISTANCEMETRIC_U_C

#include "painters_i.h"

#include <stdlib.h>

/* Distance-queue metric for painter_paint_chebyshev / paint3 / paint4 (override with -D).
 * Must match PAINTER_DIST_QUEUE_BUCKETS below (or omit buckets to derive from metric). */
#define PAINTER_PAINT_DIST_CHEBYSHEV 0 /* L_inf; max key 25 → buckets 0..25 */
#define PAINTER_PAINT_DIST_MANHATTAN 1 /* L1; max key 50 */
#define PAINTER_PAINT_DIST_EUCLIDEAN 2 /* d^2 buckets; max 25*25+25*25 = 1250 */

#ifndef PAINTER_PAINT_DIST_METRIC
#define PAINTER_PAINT_DIST_METRIC PAINTER_PAINT_DIST_MANHATTAN
#endif

/* Farthest-first bucket array size (radius 25). Override with -D if you change draw radius. */
#ifndef PAINTER_DIST_QUEUE_BUCKETS
#if PAINTER_PAINT_DIST_METRIC == PAINTER_PAINT_DIST_CHEBYSHEV
#define PAINTER_DIST_QUEUE_BUCKETS 26
#elif PAINTER_PAINT_DIST_METRIC == PAINTER_PAINT_DIST_MANHATTAN
#define PAINTER_DIST_QUEUE_BUCKETS 51
#elif PAINTER_PAINT_DIST_METRIC == PAINTER_PAINT_DIST_EUCLIDEAN
#define PAINTER_DIST_QUEUE_BUCKETS 1251
#else
#error "PAINTER_PAINT_DIST_METRIC must be PAINTER_PAINT_DIST_CHEBYSHEV, MANHATTAN, or EUCLIDEAN"
#endif
#endif


/* painter_paint_distancemetric option bits */
#define CHEB_OPT_CLEAR_BBOX_TILES (1u << 0)
#define CHEB_OPT_SCENERY_INSERTION_SORT (1u << 1)

struct DistMetricCtx {
    struct IntQueue distance_queues[PAINTER_DIST_QUEUE_BUCKETS];
    int max_active_dist;
    int current_camera_sx;
    int current_camera_sz;
};

#define DM(P) ((struct DistMetricCtx*)(P)->distmetric_ctx)

static int
distmetric_ctx_init(struct Painter* painter)
{
    struct DistMetricCtx* d = (struct DistMetricCtx*)calloc(1, sizeof(struct DistMetricCtx));
    if( !d )
        return -1;
    for( int i = 0; i < PAINTER_DIST_QUEUE_BUCKETS; i++ )
        int_queue_init(&d->distance_queues[i], 1024);
    d->max_active_dist = 0;
    painter->distmetric_ctx = d;
    return 0;
}

static void
distmetric_ctx_free(struct Painter* painter)
{
    struct DistMetricCtx* d = DM(painter);
    if( !d )
        return;
    for( int i = 0; i < PAINTER_DIST_QUEUE_BUCKETS; i++ )
        int_queue_free(&d->distance_queues[i]);
    free(d);
    painter->distmetric_ctx = NULL;
}

static void
painter_dist_queue_reset(struct Painter* painter)
{
    struct DistMetricCtx* d = DM(painter);
    for( int i = 0; i < PAINTER_DIST_QUEUE_BUCKETS; i++ )
    {
        d->distance_queues[i].head = 0;
        d->distance_queues[i].tail = 0;
        d->distance_queues[i].length = 0;
    }
    d->max_active_dist = 0;
}

static inline int
calc_distance_chebyshev(
    int dx,
    int dz)
{
    dx = abs(dx);
    dz = abs(dz);
    return dx > dz ? dx : dz;
}

static inline int
calc_distance_manhattan(
    int dx,
    int dz)
{
    return abs(dx) + abs(dz);
}

/* Bucket key monotonic with true Euclidean distance (compare d^2, avoid sqrt). */
static inline int
calc_distance_euclid(
    int dx,
    int dz)
{
    return dx * dx + dz * dz;
}

static inline int
calc_distance(
    int dx,
    int dz)
{
#if PAINTER_PAINT_DIST_METRIC == PAINTER_PAINT_DIST_CHEBYSHEV
    return calc_distance_chebyshev(dx, dz);
#elif PAINTER_PAINT_DIST_METRIC == PAINTER_PAINT_DIST_MANHATTAN
    return calc_distance_manhattan(dx, dz);
#elif PAINTER_PAINT_DIST_METRIC == PAINTER_PAINT_DIST_EUCLIDEAN
    return calc_distance_euclid(dx, dz);
#else
#error "PAINTER_PAINT_DIST_METRIC must be PAINTER_PAINT_DIST_CHEBYSHEV, MANHATTAN, or EUCLIDEAN"
#endif
}

static inline void
painter_push_queue_dist(
    struct Painter* painter,
    int tile_idx)
{
    struct DistMetricCtx* d = DM(painter);
    struct PaintersTile* tile = &painter->tiles[tile_idx];
    int tile_sx = PAINTER_TILE_X(painter, tile);
    int tile_sz = PAINTER_TILE_Z(painter, tile);

    int dx = tile_sx - d->current_camera_sx;
    int dz = tile_sz - d->current_camera_sz;
    int dist = calc_distance(dx, dz);

    if( dist >= PAINTER_DIST_QUEUE_BUCKETS )
        dist = PAINTER_DIST_QUEUE_BUCKETS - 1;

    if( dist > d->max_active_dist )
        d->max_active_dist = dist;

    struct TilePaint* tile_paint = tile_paint_at_idx(painter, tile_idx);
    tile_paint->queue_count++;

    int_queue_push_wrap(&d->distance_queues[dist], tile_idx);
}

static inline int
painter_queue_pop_dist(struct Painter* painter)
{
    struct DistMetricCtx* d = DM(painter);
    int dist = d->max_active_dist;

    while( dist >= 0 && d->distance_queues[dist].length == 0 )
        dist--;

    if( dist < 0 )
    {
        d->max_active_dist = 0;
        return -1;
    }

    d->max_active_dist = dist;

    return int_queue_pop(&d->distance_queues[dist]) >> 8;
}

static int
painter_paint_distancemetric(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel,
    unsigned cheb_opts)
{
    struct PaintersTile* tile = NULL;
    struct PaintersElement* element = NULL;
    struct TilePaint* tile_paint = NULL;
    struct TilePaint* other_paint = NULL;
    struct PaintersTile* bridge_underpass_tile = NULL;
    struct ElementPaint* element_paint = NULL;

    int scenery_queue[100];
    int scenery_queue_length = 0;
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

    if( (cheb_opts & CHEB_OPT_CLEAR_BBOX_TILES) != 0 )
    {
        if( min_draw_x >= max_draw_x || min_draw_z >= max_draw_z )
            return 0;
        painter_clear_tile_paints_region(
            painter, min_draw_x, max_draw_x, min_draw_z, max_draw_z, max_level);
    }
    else
    {
        memset(painter->tile_paints, 0x00, painter->tile_capacity * sizeof(struct TilePaint));
        if( min_draw_x >= max_draw_x || min_draw_z >= max_draw_z )
            return 0;
    }

    painter_cullmap_refresh_camera_key(painter);

    painter_dist_queue_reset(painter);
    DM(painter)->current_camera_sx = camera_sx;
    DM(painter)->current_camera_sz = camera_sz;

    /*
     * Distance buckets use floor (sx,sz) only; key from calc_distance (L_inf / L1 / d^2).
     * Stacked slevels at the same column share a bucket; PAINT_STEP_READY still waits on the
     * level below. Large locs no longer use catchup prio; ordering is bucket FIFO plus
     * queue_count revisits.
     *
     * Seed the full axis-aligned border of [min_draw,max_draw) (half-open in x,z). For L_inf,
     * a virtual ring at exactly |dx|==radius || |dz|==radius misses the +x/+z edges because
     * valid tiles stop at max_draw-1, and the northeast corner (max-1,max-1) has L_inf
     * distance radius-1 so it never lies on that ring — nothing would seed it.
     */
    for( int coord_sx = min_draw_x; coord_sx < max_draw_x; coord_sx++ )
    {
        int coord_idx = painter_coord_idx(painter, coord_sx, min_draw_z, 0);
        tile_paint = tile_paint_at_idx(painter, coord_idx);
        if( tile_paint->queue_count == 0 )
            painter_push_queue_dist(painter, coord_idx);

        if( max_draw_z - 1 != min_draw_z )
        {
            coord_idx = painter_coord_idx(painter, coord_sx, max_draw_z - 1, 0);
            tile_paint = tile_paint_at_idx(painter, coord_idx);
            if( tile_paint->queue_count == 0 )
                painter_push_queue_dist(painter, coord_idx);
        }
    }
    for( int coord_sz = min_draw_z + 1; coord_sz < max_draw_z - 1; coord_sz++ )
    {
        int coord_idx = painter_coord_idx(painter, min_draw_x, coord_sz, 0);
        tile_paint = tile_paint_at_idx(painter, coord_idx);
        if( tile_paint->queue_count == 0 )
            painter_push_queue_dist(painter, coord_idx);

        if( max_draw_x - 1 != min_draw_x )
        {
            coord_idx = painter_coord_idx(painter, max_draw_x - 1, coord_sz, 0);
            tile_paint = tile_paint_at_idx(painter, coord_idx);
            if( tile_paint->queue_count == 0 )
                painter_push_queue_dist(painter, coord_idx);
        }
    }

    int tile_idx;
    while( (tile_idx = painter_queue_pop_dist(painter)) != -1 )
    {
        tile = &painter->tiles[tile_idx];

        int tile_sx = PAINTER_TILE_X(painter, tile);
        int tile_sz = PAINTER_TILE_Z(painter, tile);
        int tile_slevel = painters_tile_get_slevel(tile);

        tile_paint = tile_paint_at_idx(painter, tile_idx);
        assert(tile_paint->queue_count > 0);
        tile_paint->queue_count -= 1;

        if( g_trap_x != -1 && g_trap_z != -1 && tile_sx == g_trap_x && tile_sz == g_trap_z )
        {
            printf("tile_idx: %d\n", tile_idx);

            // __builtin_debugtrap();
        }
        // https://discord.com/channels/788652898904309761/1069689552052166657/1172452179160870922
        // Dane discovered this also.
        // The issue turned out to be...a nuance of the DoublyLinkedList and Node
        // implementations. In 317 anything that extends Node will be automatically unlinked
        // from a list when inserted into a list, even if it's already in that same list. I had
        // overlooked this and used a standard FIFO list in my scene tile queue. This meant that
        // tiles which were supposed to move in the render queue were actually just occupying >1
        // spot in the list.
        // It's also not a very standard way to implement doubly linked lists.. but it does save
        // allocations since now the next/prev is built into the tile. luckily the solution is
        // super simple. Add the next and prev fields to the Tile struct and use this queue:
        if( tile_paint->queue_count > 0 )
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
            /* Same inward neighbor pushes as end of PAINT_STEP_LOCS (propagation). */
            if( tile_slevel < painter->levels - 1 )
            {
                int other_idx = step_idx_up(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);
                if( other_paint->step != PAINT_STEP_DONE )
                    painter_push_queue_dist(painter, other_idx);
            }
            if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
            {
                int other_idx = step_idx_east(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);
                if( other_paint->step != PAINT_STEP_DONE )
                    painter_push_queue_dist(painter, other_idx);
            }
            if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
            {
                int other_idx = step_idx_west(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);
                if( other_paint->step != PAINT_STEP_DONE )
                    painter_push_queue_dist(painter, other_idx);
            }
            if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
            {
                int other_idx = step_idx_north(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);
                if( other_paint->step != PAINT_STEP_DONE )
                    painter_push_queue_dist(painter, other_idx);
            }
            if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
            {
                int other_idx = step_idx_south(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);
                if( other_paint->step != PAINT_STEP_DONE )
                    painter_push_queue_dist(painter, other_idx);
            }
            continue;
        }

        assert(tile_sx >= min_draw_x);
        assert(tile_sx < max_draw_x);
        assert(tile_sz >= min_draw_z);
        assert(tile_sz < max_draw_z);

        /**
         * If this is a loc revisit, then we need to verify adjacent tiles are done.
         */

        if( tile_paint->step == PAINT_STEP_READY )
        {
            if( tile_slevel > 0 )
            {
                other_paint = tile_paint_at_idx(painter, step_idx_down(painter, tile_idx));

                if( other_paint->step != PAINT_STEP_DONE )
                {
                    continue;
                }
            }

            if( tile_is_east_inbounds(tile_sx, camera_sx, max_draw_x) )
            {
                other_paint = tile_paint_at_idx(painter, step_idx_east(painter, tile_idx));

                // If we are not spanned by the tile, then we need to verify it is done.
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    if( (tile->spans & SPAN_FLAG_EAST) == 0 ||
                        other_paint->step != PAINT_STEP_WAIT_ADJACENT_GROUND )
                    {
                        continue;
                    }
                }
            }

            if( tile_is_west_inbounds(tile_sx, camera_sx, min_draw_x) )
            {
                other_paint = tile_paint_at_idx(painter, step_idx_west(painter, tile_idx));

                if( other_paint->step != PAINT_STEP_DONE )
                {
                    if( (tile->spans & SPAN_FLAG_WEST) == 0 ||
                        other_paint->step != PAINT_STEP_WAIT_ADJACENT_GROUND )
                    {
                        goto done;
                    }
                }
            }

            if( tile_is_north_inbounds(tile_sz, camera_sz, max_draw_z) )
            {
                other_paint = tile_paint_at_idx(painter, step_idx_north(painter, tile_idx));

                if( other_paint->step != PAINT_STEP_DONE )
                {
                    if( (tile->spans & SPAN_FLAG_NORTH) == 0 ||
                        other_paint->step != PAINT_STEP_WAIT_ADJACENT_GROUND )
                    {
                        goto done;
                    }
                }
            }
            if( tile_is_south_inbounds(tile_sz, camera_sz, min_draw_z) )
            {
                other_paint = tile_paint_at_idx(painter, step_idx_south(painter, tile_idx));

                if( other_paint->step != PAINT_STEP_DONE )
                {
                    if( (tile->spans & SPAN_FLAG_SOUTH) == 0 ||
                        other_paint->step != PAINT_STEP_WAIT_ADJACENT_GROUND )
                    {
                        goto done;
                    }
                }
            }

            tile_paint->step = PAINT_STEP_GROUND;
        }

        if( tile_paint->step == PAINT_STEP_GROUND )
        {
            tile_paint->step = PAINT_STEP_WAIT_ADJACENT_GROUND;

            int far_walls = far_wall_flags(camera_sx, camera_sz, tile_sx, tile_sz);
            tile_paint->near_wall_flags |= ~far_walls;

            if( tile->bridge_tile != -1 )
            {
                bridge_underpass_tile = &painter->tiles[tile->bridge_tile];

                // other_paint = tile_paint_at(
                //     painter,
                //     bridge_underpass_tile->sx,
                //     bridge_underpass_tile->sz,
                //     bridge_underpass_tile->terrain_slevel);
                // other_paint->step = PAINT_STEP_DONE;
                // The bridge floor is always stored on level 3.
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

                    // TODO: Document what this is doing.
                    int x_near = x_diff;
                    if( element->_wall_decor._bf_side == WALL_CORNER_NORTHEAST ||
                        element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST )
                        x_near = -x_diff;

                    int z_near = z_diff;
                    if( element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST ||
                        element->_wall_decor._bf_side == WALL_CORNER_SOUTHWEST )
                        z_near = -z_diff;

                    if( z_near < x_near )
                    {
                        // Draw model a
                        push_command_entity(buffer, element->_wall_decor.entity);
                    }
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

            if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
            {
                int other_idx = step_idx_east(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);

                if( other_paint->step != PAINT_STEP_DONE &&
                    (tile->spans & SPAN_FLAG_EAST) != 0 )
                {
                    painter_push_queue_dist(painter, other_idx);
                }
            }

            if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
            {
                int other_idx = step_idx_west(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);

                if( other_paint->step != PAINT_STEP_DONE &&
                    (tile->spans & SPAN_FLAG_WEST) != 0 )
                {
                    painter_push_queue_dist(painter, other_idx);
                }
            }

            if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
            {
                int other_idx = step_idx_north(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);

                if( other_paint->step != PAINT_STEP_DONE &&
                    (tile->spans & SPAN_FLAG_NORTH) != 0 )
                {
                    painter_push_queue_dist(painter, other_idx);
                }
            }

            if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
            {
                int other_idx = step_idx_south(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);

                if( other_paint->step != PAINT_STEP_DONE &&
                    (tile->spans & SPAN_FLAG_SOUTH) != 0 )
                {
                    painter_push_queue_dist(painter, other_idx);
                }
            }
        }

        bool waiting_spanning_scenery = false;
        scenery_queue_length = 0;
        if( tile_paint->step == PAINT_STEP_WAIT_ADJACENT_GROUND )
        {
            tile_paint->step = PAINT_STEP_LOCS;

            // Check if all locs are drawable.
            for( int32_t sn = tile->scenery_head; sn != -1; sn = painter->scenery_pool[sn].next )
            {
                int scenery_element = painter->scenery_pool[sn].element_idx;

                element_paint = &painter->element_paints[scenery_element];
                if( element_paint->drawn )
                    continue;

                element = &painter->elements[scenery_element];
                assert(element->kind == PNTRELEM_SCENERY);

                int min_tile_x = element->sx;
                int min_tile_z = element->sz;
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

                for( int other_tile_x = min_tile_x; other_tile_x <= max_tile_x; other_tile_x++ )
                {
                    for( int other_tile_z = min_tile_z; other_tile_z <= max_tile_z; other_tile_z++ )
                    {
                        other_paint = tile_paint_at_idx(
                            painter,
                            painter_coord_idx(painter, other_tile_x, other_tile_z, tile_slevel));

                        if( other_paint->step <= PAINT_STEP_GROUND )
                        {
                            waiting_spanning_scenery = true;
                            goto step_scenery;
                        }
                    }
                }

                scenery_queue[scenery_queue_length++] = scenery_element;

            step_scenery:;
            }
        }

        if( tile_paint->step == PAINT_STEP_LOCS )
        {
            if( (cheb_opts & CHEB_OPT_SCENERY_INSERTION_SORT) != 0 )
                scenery_queue_insertion_sort(
                    scenery_queue, scenery_queue_length, painter, camera_sx, camera_sz);
            else
            {
                s_scenery_sort_painter = painter;
                s_scenery_sort_camera_sx = camera_sx;
                s_scenery_sort_camera_sz = camera_sz;
                qsort(
                    scenery_queue,
                    (size_t)scenery_queue_length,
                    sizeof(scenery_queue[0]),
                    scenery_distance_compare);
            }

            for( int j = 0; j < scenery_queue_length; j++ )
            {
                int scenery_element = scenery_queue[j];

                element_paint = &painter->element_paints[scenery_element];
                if( element_paint->drawn )
                    continue;

                element_paint->drawn = true;

                element = &painter->elements[scenery_element];
                assert(element->kind == PNTRELEM_SCENERY);

                push_command_entity(buffer, element->_scenery.entity);

                int min_tile_x = element->sx;
                int min_tile_z = element->sz;
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

                int step_x = element->sx <= camera_sx ? 1 : -1;
                int step_z = element->sz <= camera_sz ? 1 : -1;

                int start_x = min_tile_x;
                int start_z = min_tile_z;
                int end_x = max_tile_x;
                int end_z = max_tile_z;

                if( step_x < 0 )
                {
                    int tmp = start_x;
                    start_x = end_x;
                    end_x = tmp;
                }

                if( step_z < 0 )
                {
                    int tmp = start_z;
                    start_z = end_z;
                    end_z = tmp;
                }

                for( int other_tile_x = start_x; other_tile_x != end_x + step_x;
                     other_tile_x += step_x )
                {
                    for( int other_tile_z = start_z; other_tile_z != end_z + step_z;
                         other_tile_z += step_z )
                    {
                        int other_idx =
                            painter_coord_idx(painter, other_tile_x, other_tile_z, tile_slevel);
                        other_paint = tile_paint_at_idx(painter, other_idx);

                        painter_push_queue_dist(painter, other_idx);
                    }
                }
            }

            if( scenery_queue_length != 0 )
            {
                tile_paint->step = PAINT_STEP_WAIT_ADJACENT_GROUND;
                goto done;
            }

            if( waiting_spanning_scenery )
            {
                tile_paint->step = PAINT_STEP_WAIT_ADJACENT_GROUND;
                goto done;
            }

            if( tile_slevel < painter->levels - 1 )
            {
                int other_idx = step_idx_up(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);

                if( other_paint->step != PAINT_STEP_DONE )
                {
                    painter_push_queue_dist(painter, other_idx);
                }
            }

            if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
            {
                int other_idx = step_idx_east(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);

                if( other_paint->step != PAINT_STEP_DONE )
                {
                    painter_push_queue_dist(painter, other_idx);
                }
            }
            if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
            {
                int other_idx = step_idx_west(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    painter_push_queue_dist(painter, other_idx);
                }
            }

            if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
            {
                int other_idx = step_idx_north(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    painter_push_queue_dist(painter, other_idx);
                }
            }

            if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
            {
                int other_idx = step_idx_south(painter, tile_idx);
                other_paint = tile_paint_at_idx(painter, other_idx);
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    painter_push_queue_dist(painter, other_idx);
                }
            }

            tile_paint->step = PAINT_STEP_NEAR_WALL;
        }

        if( tile_paint->step == PAINT_STEP_NEAR_WALL )
        {
            if( tile->wall_decor_a != -1 )
            {
                element = &painter->elements[tile->wall_decor_a];
                assert(element->kind == PNTRELEM_WALL_DECOR);

                if( element->_wall_decor._bf_through_wall_flags != 0 )
                {
                    int x_diff = element->sx - camera_sx;
                    int z_diff = element->sz - camera_sz;

                    // TODO: Document what this is doing.
                    int x_near = x_diff;
                    if( element->_wall_decor._bf_side == WALL_CORNER_NORTHEAST ||
                        element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST )
                        x_near = -x_diff;

                    int z_near = z_diff;
                    if( element->_wall_decor._bf_side == WALL_CORNER_SOUTHEAST ||
                        element->_wall_decor._bf_side == WALL_CORNER_SOUTHWEST )
                        z_near = -z_diff;

                    // The deobs and official clients calculate the nearest quadrant.
                    // Notice a line goes from SW to NE with y = x.
                    if( z_near >= x_near )
                    {
                        // Draw model a
                        push_command_entity(buffer, element->_wall_decor.entity);
                    }
                    else if( tile->wall_decor_b != -1 )
                    {
                        element = &painter->elements[tile->wall_decor_b];
                        assert(element->kind == PNTRELEM_WALL_DECOR);

                        // Draw model b
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
        }

        if( tile_paint->step == PAINT_STEP_DONE )
        {
        }

    done:;
    } // while( distance queues )

    for( int qi = 0; qi < PAINTER_DIST_QUEUE_BUCKETS; qi++ )
        assert(DM(painter)->distance_queues[qi].length == 0);

    return 0;
}

int
painter_paint3(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel)
{
    return painter_paint_distancemetric(painter, buffer, camera_sx, camera_sz, camera_slevel, 0u);
}

int
painter_paint4(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel)
{
    return painter_paint_distancemetric(
        painter,
        buffer,
        camera_sx,
        camera_sz,
        camera_slevel,
        CHEB_OPT_CLEAR_BBOX_TILES | CHEB_OPT_SCENERY_INSERTION_SORT);
}

int
painter_paint4_1(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel)
{
    unsigned cheb_opts = CHEB_OPT_SCENERY_INSERTION_SORT;
#ifdef __EMSCRIPTEN__
    cheb_opts |= CHEB_OPT_CLEAR_BBOX_TILES;
#endif
    return painter_paint_distancemetric(painter, buffer, camera_sx, camera_sz, camera_slevel, cheb_opts);
}


#endif
