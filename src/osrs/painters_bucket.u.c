#ifndef PAINTERS_BUCKET_U_C
#define PAINTERS_BUCKET_U_C

#include "painter_tile_iter.u.c"
#include "painters_i.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Manhattan distance from camera to any tile in a 128-wide grid is in [0, 2*127]. */
#define BUCKET_DIST_RANGE (2 * 128 + 1)

struct BucketSeed
{
    uint8_t phase;
    int32_t tile_idx;
};

struct PainterBucketCtx
{
    int* bucket_next; /* [tile_capacity]; intrusive list per bucket, -1 = end */
    int* dist;        /* [tile_capacity] Manhattan distance key */
    uint8_t* in_heap;
    int bucket_heads[BUCKET_DIST_RANGE];
    int bucket_max;
    int n_in_queue;   /* live count of tiles currently in the bucket queue */
    struct BucketSeed* seeds;
    int seeds_cap;
    uint8_t* seed_seen;
    int seed_seen_nbytes;
};

#define BM(P) ((struct PainterBucketCtx*)(P)->bucket_ctx)

static void
bucket_reset(struct PainterBucketCtx* w)
{
    for( int i = 0; i < BUCKET_DIST_RANGE; i++ )
        w->bucket_heads[i] = -1;
    w->bucket_max = -1;
    w->n_in_queue = 0;
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
    w->n_in_queue++;
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
        w->n_in_queue--;
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

static void
bucket_emit_seed(
    struct Painter* painter,
    struct PainterBucketCtx* w,
    uint8_t phase,
    int sx,
    int sz,
    int slevel,
    int* seeds_len)
{
    if( sx < 0 || sz < 0 || sx >= painter->width || sz >= painter->height )
        return;
    int b = sz * painter->width + sx;
    int bi = b / 8;
    int bb = 1 << (b % 8);
    if( (w->seed_seen[bi] & (uint8_t)bb) != 0 )
        return;
    w->seed_seen[bi] |= (uint8_t)bb;
    assert(*seeds_len < w->seeds_cap);
    w->seeds[*seeds_len].phase = phase;
    w->seeds[*seeds_len].tile_idx = painter_coord_idx(painter, sx, sz, slevel);
    (*seeds_len)++;
}

static void
bucket_build_seeds(
    struct Painter* painter,
    struct PainterBucketCtx* w,
    int eye_ix,
    int eye_iz,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int radius,
    int* out_len)
{
    *out_len = 0;
    int L = painter->levels;
    int R = radius;
    if( R < 1 )
        R = 1;

    for( int phase = 1; phase <= 2; phase++ )
    {
        for( int level = 0; level < L; level++ )
        {
            for( int dx = -R; dx <= 0; dx++ )
            {
                int right_x = eye_ix + dx;
                int left_x = eye_ix - dx;
                if( right_x < min_draw_x && left_x >= max_draw_x )
                    continue;

                for( int dz = -R; dz <= 0; dz++ )
                {
                    int fwd_z = eye_iz + dz;
                    int bwd_z = eye_iz - dz;

                    memset(w->seed_seen, 0, (size_t)w->seed_seen_nbytes);

                    if( right_x >= min_draw_x )
                    {
                        if( fwd_z >= min_draw_z )
                            bucket_emit_seed(
                                painter, w, (uint8_t)phase, right_x, fwd_z, level, out_len);
                        if( bwd_z < max_draw_z )
                            bucket_emit_seed(
                                painter, w, (uint8_t)phase, right_x, bwd_z, level, out_len);
                    }
                    if( left_x < max_draw_x )
                    {
                        if( fwd_z >= min_draw_z )
                            bucket_emit_seed(
                                painter, w, (uint8_t)phase, left_x, fwd_z, level, out_len);
                        if( bwd_z < max_draw_z )
                            bucket_emit_seed(
                                painter, w, (uint8_t)phase, left_x, bwd_z, level, out_len);
                    }
                }
            }
        }
    }
}

static int
bucket_queue_empty(struct PainterBucketCtx* w)
{
    return w->n_in_queue == 0;
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
    int seed_cap = 8 * painter->levels * (painter->width + 4) * (painter->height + 4) + 2048;
    if( seed_cap < 8192 )
        seed_cap = 8192;
    w->seeds = (struct BucketSeed*)malloc((size_t)seed_cap * sizeof(struct BucketSeed));
    w->seeds_cap = seed_cap;
    w->seed_seen_nbytes = (painter->width * painter->height + 7) / 8 + 1;
    w->seed_seen = (uint8_t*)calloc((size_t)w->seed_seen_nbytes, 1);
    if( !w->bucket_next || !w->dist || !w->in_heap || !w->seeds || !w->seed_seen )
    {
        free(w->bucket_next);
        free(w->dist);
        free(w->in_heap);
        free(w->seeds);
        free(w->seed_seen);
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
    free(w->seeds);
    free(w->seed_seen);
    free(w);
    painter->bucket_ctx = NULL;
}

#include "painters_bucket_simd.u.c"

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

    uint8_t draw_mask = painter->level_mask ? painter->level_mask : 0xFu;
    /* Iterate all grid stack levels; per-tile draw_mask uses packed slevel (VisBelow). */
    int min_level = 0;
    int max_level = painter->levels;

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

    painter_cullmap_refresh_camera_key(painter);

    painter_clear_tile_paints_region(
        painter, min_draw_x, max_draw_x, min_draw_z, max_draw_z, max_level);

    for( int s = min_level; s < max_level; s++ )
    {
        for( int z = min_draw_z; z < max_draw_z; z++ )
        {
            int base = painter_coord_idx(painter, min_draw_x, z, s);
            memset(&w->in_heap[base], 0, (size_t)(max_draw_x - min_draw_x) * sizeof(uint8_t));
        }
    }

    bucket_fill_distances(
        w,
        painter,
        camera_sx,
        camera_sz,
        min_draw_x,
        max_draw_x,
        min_draw_z,
        max_draw_z,
        min_level,
        max_level);

    struct TileIter tile_iter;
    tile_iter_init(
        &tile_iter, painter, min_draw_x, max_draw_x, min_draw_z, max_draw_z, min_level, max_level);
    for( int ti, x, z; (ti = tile_iter_next(&tile_iter, &x, &z)) >= 0; )
    {
        struct PaintersTile* t = &painter->tiles[ti];
        int tile_slevel = painters_tile_get_slevel(t);

        tile_paint = tile_paint_at_idx(painter, ti);
        if( tile_excluded_by_bridge_or_draw_mask(
                painters_tile_get_flags(t), tile_slevel, draw_mask) )
        {
            tile_paint->step = PAINT_STEP_DONE;
        }
        else if( !painter_cullmap_tile_visible(painter, tile_paint, x, z, camera_sx, camera_sz) )
        {
            tile_paint->step = PAINT_STEP_DONE;
        }
        else
        {
            tile_paint->step = PAINT_STEP_READY;
        }
    }

    bucket_reset(w);

    /* Count ready tiles; do NOT bulk-push them all. The seed list drives the cascade
     * lazily, avoiding O(tiles) queue fill and reducing duplicate dependency retries. */
    int tiles_remaining = 0;
    tile_iter_init(
        &tile_iter, painter, min_draw_x, max_draw_x, min_draw_z, max_draw_z, min_level, max_level);
    for( int ti; (ti = tile_iter_next(&tile_iter, NULL, NULL)) >= 0; )
    {
        tile_paint = tile_paint_at_idx(painter, ti);
        if( tile_paint->step == PAINT_STEP_READY )
            tiles_remaining++;
    }

    /* Seed list is built on first queue drain; preallocate but defer the work. */
    int seeds_len = 0;
    int seed_idx = 0;
    int seeds_built = 0;
    int check_adjacent = 1;

    for( ;; )
    {
        if( bucket_queue_empty(w) )
        {
            if( tiles_remaining == 0 )
                break;
            /* Build the perimeter seed list on first drain (deferred to avoid paying the
             * O(R^2*levels) memset cost when the cascade already covers all tiles). */
            if( !seeds_built )
            {
                bucket_build_seeds(
                    painter,
                    w,
                    camera_sx,
                    camera_sz,
                    min_draw_x,
                    max_draw_x,
                    min_draw_z,
                    max_draw_z,
                    radius,
                    &seeds_len);
                seeds_built = 1;
            }
            int seeded = 0;
            while( seed_idx < seeds_len )
            {
                struct BucketSeed* sd = &w->seeds[seed_idx++];
                tile_paint = tile_paint_at_idx(painter, sd->tile_idx);
                if( tile_paint->step == PAINT_STEP_READY )
                {
                    bucket_push_tile(w, sd->tile_idx);
                    check_adjacent = (sd->phase == 1);
                    seeded = 1;
                    break;
                }
            }
            if( !seeded )
                break;
        }

        int e_tile = bucket_pop(w);
        if( e_tile < 0 )
            continue;

        tile = &painter->tiles[e_tile];
        w->in_heap[e_tile] = 0;
        
        tile_paint = tile_paint_at_idx(painter, e_tile);
        int tile_sx = tile->sx;
        int tile_sz = tile->sz;
        int grid_level = painters_tile_get_grid_level(tile);
        int tile_slevel = painters_tile_get_slevel(tile);

        if( tile_paint->step == PAINT_STEP_DONE )
            continue;

        if( tile_excluded_by_bridge_or_draw_mask(
                painters_tile_get_flags(tile), tile_slevel, draw_mask) )
        {
            tile_paint->step = PAINT_STEP_DONE;
            if( tiles_remaining > 0 )
                tiles_remaining--;
            continue;
        }

        if( !painter_cullmap_tile_visible(
                painter, tile_paint, tile_sx, tile_sz, camera_sx, camera_sz) )
        {
            tile_paint->step = PAINT_STEP_DONE;
            if( tiles_remaining > 0 )
                tiles_remaining--;
            if( grid_level < painter->levels - 1 )
            {
                int nidx = step_idx_up(painter, e_tile);
                other_paint = tile_paint_at_idx(painter, nidx);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(w, nidx);
            }

            if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
            {
                int nidx = step_idx_east(painter, e_tile);
                other_paint = tile_paint_at_idx(painter, nidx);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(w, nidx);
            }

            if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
            {
                int nidx = step_idx_west(painter, e_tile);
                other_paint = tile_paint_at_idx(painter, nidx);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(w, nidx);
            }
            if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
            {
                int nidx = step_idx_north(painter, e_tile);
                other_paint = tile_paint_at_idx(painter, nidx);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(w, nidx);
            }
            if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
            {
                int nidx = step_idx_south(painter, e_tile);
                other_paint = tile_paint_at_idx(painter, nidx);
                if( other_paint->step != PAINT_STEP_DONE )
                    bucket_push_tile(w, nidx);
            }
            continue;
        }

        assert(tile_sx >= min_draw_x);
        assert(tile_sx < max_draw_x);
        assert(tile_sz >= min_draw_z);
        assert(tile_sz < max_draw_z);

        if( tile_paint->step == PAINT_STEP_READY )
        {
            if( check_adjacent )
            {
                if( grid_level > 0 )
                {
                    other_paint = tile_paint_at_idx(painter, step_idx_down(painter, e_tile));
                    if( other_paint->step != PAINT_STEP_DONE )
                        continue;
                }

                /* Match painter_paint_world3d draw_front adjacent deps. */
                if( tile_is_west_inbounds(tile_sx, camera_sx, min_draw_x) )
                {
                    other_paint = tile_paint_at_idx(painter, step_idx_west(painter, e_tile));
                    if( other_paint->step != PAINT_STEP_DONE &&
                        (other_paint->step == PAINT_STEP_READY ||
                         (tile->spans & SPAN_FLAG_WEST) == 0) )
                        continue;
                }
                if( tile_is_east_inbounds(tile_sx, camera_sx, max_draw_x) )
                {
                    other_paint = tile_paint_at_idx(painter, step_idx_east(painter, e_tile));
                    if( other_paint->step != PAINT_STEP_DONE &&
                        (other_paint->step == PAINT_STEP_READY ||
                         (tile->spans & SPAN_FLAG_EAST) == 0) )
                        continue;
                }
                if( tile_is_south_inbounds(tile_sz, camera_sz, min_draw_z) )
                {
                    other_paint = tile_paint_at_idx(painter, step_idx_south(painter, e_tile));
                    if( other_paint->step != PAINT_STEP_DONE &&
                        (other_paint->step == PAINT_STEP_READY ||
                         (tile->spans & SPAN_FLAG_SOUTH) == 0) )
                        continue;
                }
                if( tile_is_north_inbounds(tile_sz, camera_sz, max_draw_z) )
                {
                    other_paint = tile_paint_at_idx(painter, step_idx_north(painter, e_tile));
                    if( other_paint->step != PAINT_STEP_DONE &&
                        (other_paint->step == PAINT_STEP_READY ||
                         (tile->spans & SPAN_FLAG_NORTH) == 0) )
                        continue;
                }
            }
            else
            {
                check_adjacent = 1;
            }

            int far_walls = far_wall_flags(camera_sx, camera_sz, tile_sx, tile_sz);
            tile_paint->near_wall_flags |= (uint8_t)~far_walls;

            if( tile->bridge_tile != -1 )
            {
                bridge_underpass_tile = &painter->tiles[tile->bridge_tile];
                push_command_terrain(
                    buffer,
                    bridge_underpass_tile->sx,
                    bridge_underpass_tile->sz,
                    painters_tile_get_terrain_level(bridge_underpass_tile));

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

            push_command_terrain(buffer, tile_sx, tile_sz, painters_tile_get_terrain_level(tile));

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

            unsigned spans = tile->spans;
            if( spans )
            {
                if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) &&
                    (spans & SPAN_FLAG_EAST) )
                {
                    int nidx = step_idx_east(painter, e_tile);
                    other_paint = tile_paint_at_idx(painter, nidx);
                    if( other_paint->step != PAINT_STEP_DONE )
                        bucket_push_tile(w, nidx);
                }
                if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) &&
                    (spans & SPAN_FLAG_NORTH) )
                {
                    int nidx = step_idx_north(painter, e_tile);
                    other_paint = tile_paint_at_idx(painter, nidx);
                    if( other_paint->step != PAINT_STEP_DONE )
                        bucket_push_tile(w, nidx);
                }
                if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) &&
                    (spans & SPAN_FLAG_WEST) )
                {
                    int nidx = step_idx_west(painter, e_tile);
                    other_paint = tile_paint_at_idx(painter, nidx);
                    if( other_paint->step != PAINT_STEP_DONE )
                        bucket_push_tile(w, nidx);
                }
                if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) &&
                    (spans & SPAN_FLAG_SOUTH) )
                {
                    int nidx = step_idx_south(painter, e_tile);
                    other_paint = tile_paint_at_idx(painter, nidx);
                    if( other_paint->step != PAINT_STEP_DONE )
                        bucket_push_tile(w, nidx);
                }
            }
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
        tiles_remaining--;

        if( grid_level < painter->levels - 1 )
        {
            int nidx = step_idx_up(painter, e_tile);
            other_paint = tile_paint_at_idx(painter, nidx);
            if( other_paint->step != PAINT_STEP_DONE )
                bucket_push_tile(w, nidx);
        }

        if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
        {
            int nidx = step_idx_north(painter, e_tile);
            other_paint = tile_paint_at_idx(painter, nidx);
            if( other_paint->step != PAINT_STEP_DONE )
                bucket_push_tile(w, nidx);
        }
        if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
        {
            int nidx = step_idx_west(painter, e_tile);
            other_paint = tile_paint_at_idx(painter, nidx);
            if( other_paint->step != PAINT_STEP_DONE )
                bucket_push_tile(w, nidx);
        }
        if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
        {
            int nidx = step_idx_south(painter, e_tile);
            other_paint = tile_paint_at_idx(painter, nidx);
            if( other_paint->step != PAINT_STEP_DONE )
                bucket_push_tile(w, nidx);
        }
        if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
        {
            int nidx = step_idx_east(painter, e_tile);
            other_paint = tile_paint_at_idx(painter, nidx);
            if( other_paint->step != PAINT_STEP_DONE )
                bucket_push_tile(w, nidx);
        }
    }

    return 0;
}

#endif /* PAINTERS_BUCKET_U_C */
