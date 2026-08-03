#ifndef PAINTERS_BUCKET_U_C
#define PAINTERS_BUCKET_U_C

#include "painters_i.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Manhattan distance from camera to any tile in a 128-wide grid is in [0, 2*127]. */
#define BUCKET_DIST_RANGE (2 * 128 + 1)

struct PainterBucketCtx
{
    int bucket_heads[BUCKET_DIST_RANGE];
    int bucket_max;
    int n_in_queue; /* live count of tiles currently in the bucket queue */
};

#define BM(P) ((struct PainterBucketCtx*)(P)->bucket_ctx)

static inline void
bucket_reset(struct PainterBucketCtx* w)
{
    for( int i = 0; i < BUCKET_DIST_RANGE; i++ )
        w->bucket_heads[i] = -1;
    w->bucket_max = -1;
    w->n_in_queue = 0;
}

static inline void
bucket_push(
    struct PainterBucketCtx* w,
    struct TilePaint* paints,
    int ti,
    int dist)
{
    assert(dist >= 0 && dist < BUCKET_DIST_RANGE);
    paints[ti].queue_next = w->bucket_heads[dist];
    w->bucket_heads[dist] = ti;
    paints[ti].in_queue = 1;
    if( dist > w->bucket_max )
        w->bucket_max = dist;
    w->n_in_queue++;
}

/* Pop farthest distance first; LIFO within a bucket (matches reference). */
static inline int
bucket_pop(
    struct PainterBucketCtx* w,
    struct TilePaint* paints)
{
    while( w->bucket_max >= 0 )
    {
        int head = w->bucket_heads[w->bucket_max];
        if( head < 0 )
        {
            w->bucket_max--;
            continue;
        }
        w->bucket_heads[w->bucket_max] = paints[head].queue_next;
        paints[head].in_queue = 0;
        w->n_in_queue--;
        return head;
    }
    return -1;
}

static inline int
bucket_push_if_active(
    struct PainterBucketCtx* w,
    struct TilePaint* paints,
    int ti,
    int dist)
{
    if( paints[ti].in_queue )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_PUSH_DEDUP, 1);
        return 0;
    }
    if( paints[ti].step == PAINT_STEP_DONE )
        return 0;
    bucket_push(w, paints, ti, dist);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_PUSHES, 1);
    return 1;
}

static inline int
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
    painter->bucket_ctx = w;
    return 0;
}

static void
bucket_ctx_free(struct Painter* painter)
{
    struct PainterBucketCtx* w = BM(painter);
    if( !w )
        return;
    free(w);
    painter->bucket_ctx = NULL;
}

static inline void
bucket_emit_entity(
    struct PaintersElementCommand** cur,
    struct PaintersElementCommand* end,
    int entity)
{
    assert(*cur < end);
    **cur = (struct PaintersElementCommand){
        ._entity = {
            ._bf_kind = PNTR_CMD_ELEMENT,
            ._bf_entity = (uint32_t)entity,
        },
    };
    (*cur)++;
}

static inline void
bucket_emit_terrain(
    struct PaintersElementCommand** cur,
    struct PaintersElementCommand* end,
    int sx,
    int sz,
    int slevel)
{
    assert(*cur < end);
    **cur = (struct PaintersElementCommand){
        ._terrain = {
            ._bf_kind = PNTR_CMD_TERRAIN,
            ._bf_terrain_x = (uint32_t)sx,
            ._bf_terrain_z = (uint32_t)sz,
            ._bf_terrain_y = (uint32_t)slevel,
        },
    };
    (*cur)++;
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

    assert(painter);
    assert(buffer);
    struct PainterBucketCtx* w = BM(painter);
    assert(w);

    /* Hoisted fields — setup/main store through tile_paints, so the compiler
     * cannot otherwise keep these in registers across iterations. */
    int width = painter->width;
    int height = painter->height;
    int levels = painter->levels;
    int level_stride = width * height;
    struct PaintersTile* tiles = painter->tiles;
    struct TilePaint* paints = painter->tile_paints;
    struct PaintersElement* elements = painter->elements;
    struct ElementPaint* element_paints = painter->element_paints;
    struct SceneryNode* scenery_pool = painter->scenery_pool;
    const struct PaintersCullMap* cullmap = painter->cullmap;
    size_t cull_camera_key = painter->cull_camera_key;
    int cull_all_visible = (cullmap == NULL || cullmap->all_visible);
    int cull_radius = 0;
    int cull_grid_side = 0;
    const uint8_t* cull_vis = NULL;
    if( !cull_all_visible )
    {
        cull_radius = cullmap->radius;
        cull_grid_side = cullmap->grid_side;
        cull_vis = cullmap->visibility;
    }

    buffer->command_count = 0;
    memset(element_paints, 0x00, (size_t)painter->element_count * sizeof(struct ElementPaint));

    int radius = 25;

    uint8_t draw_mask = painter->level_mask ? painter->level_mask : 0xFu;
    /* Iterate all grid stack levels; per-tile draw_mask uses packed visible_gte_level (VisBelow). */
    int min_level = 0;
    int max_level = levels;

    int max_draw_x = camera_sx + radius;
    int max_draw_z = camera_sz + radius;
    if( max_draw_x >= width )
        max_draw_x = width;
    if( max_draw_z >= height )
        max_draw_z = height;
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
    if( min_draw_x > width )
        min_draw_x = width;
    if( min_draw_z > height )
        min_draw_z = height;

    if( min_draw_x >= max_draw_x || min_draw_z >= max_draw_z )
        return 0;

    painter_cullmap_refresh_camera_key(painter);
    cull_camera_key = painter->cull_camera_key;

    /* Contiguous row setup: reset + classify + count. Distance is derived at push. */
    int tiles_remaining = 0;
    int tiles_in_box = 0;
    for( int s = min_level; s < max_level; s++ )
    {
        for( int z = min_draw_z; z < max_draw_z; z++ )
        {
            int row = min_draw_x + z * width + s * level_stride;
            int adz = abs(z - camera_sz);
            (void)adz;
            for( int x = min_draw_x, ti = row; x < max_draw_x; x++, ti++ )
            {
                tiles_in_box++;
                struct PaintersTile* t = &tiles[ti];
                struct TilePaint* tp = &paints[ti];

                tp->near_wall_flags = 0;
                tp->in_queue = 0;

                int tile_visible_gte_level = painters_tile_get_visible_gte_level(t);
                if( tile_excluded_by_bridge_or_draw_mask(
                        painters_tile_get_flags(t), tile_visible_gte_level, draw_mask) )
                {
                    tp->step = PAINT_STEP_DONE;
                    continue;
                }

                if( !cull_all_visible )
                {
                    int dx = x - camera_sx;
                    int dz = z - camera_sz;
                    if( dx < -cull_radius || dx >= cull_radius || dz < -cull_radius ||
                        dz >= cull_radius )
                    {
                        tp->step = PAINT_STEP_DONE;
                        continue;
                    }
                    int ix = dx + cull_radius;
                    int iz = dz + cull_radius;
                    size_t bidx =
                        cull_camera_key + (size_t)ix * (size_t)cull_grid_side + (size_t)iz;
                    if( !pcull_bit_get(cull_vis, bidx) )
                    {
                        tp->step = PAINT_STEP_DONE;
                        continue;
                    }
                }

                tp->step = PAINT_STEP_READY;
                tiles_remaining++;
            }
        }
    }

    /* Hard upper bound: each wall can emit twice (far + near); terrain once per box
     * tile; scenery/ground/decor at most once each. */
    int need_cmds = 2 * painter->element_count + 2 * tiles_in_box + 16;
    if( need_cmds < 16 )
        need_cmds = 16;
    if( buffer->command_capacity < need_cmds )
    {
        int cap = buffer->command_capacity > 0 ? buffer->command_capacity : 128;
        while( cap < need_cmds )
            cap *= 2;
        buffer->commands = realloc(
            buffer->commands, (size_t)cap * sizeof(struct PaintersElementCommand));
        assert(buffer->commands);
        buffer->command_capacity = cap;
    }
    struct PaintersElementCommand* cmd_base = buffer->commands;
    struct PaintersElementCommand* cmd_cur = cmd_base;
    struct PaintersElementCommand* cmd_end = cmd_base + buffer->command_capacity;

    bucket_reset(w);

    /* Incremental seed generator — initialized lazily on the first queue drain so frames
     * where the cascade covers all tiles pay zero seed-iteration cost. */
    struct PainterSeedGen seed_gen;
    int seed_gen_initialized = 0;
    int check_adjacent = 1;

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_TILES_REMAINING_SET, tiles_remaining);

    for( ;; )
    {
        if( bucket_queue_empty(w) )
        {
            if( tiles_remaining == 0 )
                break;
            if( !seed_gen_initialized )
            {
                seed_gen_init(
                    &seed_gen,
                    camera_sx,
                    camera_sz,
                    min_draw_x,
                    max_draw_x,
                    min_draw_z,
                    max_draw_z,
                    levels,
                    radius);
                seed_gen_initialized = 1;
            }
            int seeded = 0;
            int sx, sz, level, phase;
            while( seed_gen_next(&seed_gen, &sx, &sz, &level, &phase) )
            {
                int tidx = sx + sz * width + level * level_stride;
                if( paints[tidx].step == PAINT_STEP_READY )
                {
                    int dist = abs(sx - camera_sx) + abs(sz - camera_sz);
                    bucket_push_if_active(w, paints, tidx, dist);
                    check_adjacent = (phase == 1);
                    seeded = 1;
                    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_DRAIN_EVENTS, 1);
                    break;
                }
            }
            if( !seeded )
                break;
        }

        int e_tile = bucket_pop(w, paints);
        if( e_tile < 0 )
            continue;
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_POPS, 1);

        struct PaintersTile* tile = &tiles[e_tile];
        struct TilePaint* tile_paint = &paints[e_tile];
        if( tile_paint->step == PAINT_STEP_DONE )
            continue;

        int tile_sx = tile->sx;
        int tile_sz = tile->sz;
        int paintgrid_level = painters_tile_get_paintgrid_level(tile);
        int adx = abs(tile_sx - camera_sx);
        int adz = abs(tile_sz - camera_sz);
        int tile_dist = adx + adz;

        if( tile_paint->step == PAINT_STEP_READY )
        {
            if( check_adjacent )
            {
                if( paintgrid_level > 0 )
                {
                    if( paints[e_tile - level_stride].step != PAINT_STEP_DONE )
                    {
                        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_GATE_REJECTS, 1);
                        continue;
                    }
                }

                /* Match painter_paint_world3d draw_front adjacent deps. */
                if( tile_is_west_inbounds(tile_sx, camera_sx, min_draw_x) )
                {
                    struct TilePaint* other = &paints[e_tile - 1];
                    if( other->step != PAINT_STEP_DONE &&
                        (other->step == PAINT_STEP_READY || (tile->spans & SPAN_FLAG_WEST) == 0) )
                    {
                        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_GATE_REJECTS, 1);
                        continue;
                    }
                }
                if( tile_is_east_inbounds(tile_sx, camera_sx, max_draw_x) )
                {
                    struct TilePaint* other = &paints[e_tile + 1];
                    if( other->step != PAINT_STEP_DONE &&
                        (other->step == PAINT_STEP_READY || (tile->spans & SPAN_FLAG_EAST) == 0) )
                    {
                        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_GATE_REJECTS, 1);
                        continue;
                    }
                }
                if( tile_is_south_inbounds(tile_sz, camera_sz, min_draw_z) )
                {
                    struct TilePaint* other = &paints[e_tile - width];
                    if( other->step != PAINT_STEP_DONE &&
                        (other->step == PAINT_STEP_READY ||
                         (tile->spans & SPAN_FLAG_SOUTH) == 0) )
                    {
                        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_GATE_REJECTS, 1);
                        continue;
                    }
                }
                if( tile_is_north_inbounds(tile_sz, camera_sz, max_draw_z) )
                {
                    struct TilePaint* other = &paints[e_tile + width];
                    if( other->step != PAINT_STEP_DONE &&
                        (other->step == PAINT_STEP_READY ||
                         (tile->spans & SPAN_FLAG_NORTH) == 0) )
                    {
                        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_GATE_REJECTS, 1);
                        continue;
                    }
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
                struct PaintersTile* bridge_underpass_tile = &tiles[tile->bridge_tile];
                bucket_emit_terrain(
                    &cmd_cur,
                    cmd_end,
                    bridge_underpass_tile->sx,
                    bridge_underpass_tile->sz,
                    painters_tile_get_mesh_level(bridge_underpass_tile));

                if( bridge_underpass_tile->wall_a != -1 )
                {
                    struct PaintersElement* element = &elements[bridge_underpass_tile->wall_a];
                    assert(element->kind == PNTRELEM_WALL_A);
                    bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
                }

                for( int32_t sn = bridge_underpass_tile->scenery_head; sn != -1;
                     sn = scenery_pool[sn].next )
                {
                    int scenery_element = scenery_pool[sn].element_idx;
                    struct ElementPaint* ep = &element_paints[scenery_element];
                    if( ep->drawn )
                        continue;

                    struct PaintersElement* element = &elements[scenery_element];
                    assert(element->kind == PNTRELEM_SCENERY);
                    bucket_emit_entity(&cmd_cur, cmd_end, element->_scenery.entity);

                    ep->drawn = true;
                }
            }

            bucket_emit_terrain(
                &cmd_cur, cmd_end, tile_sx, tile_sz, painters_tile_get_mesh_level(tile));

            if( tile->wall_a != -1 )
            {
                struct PaintersElement* element = &elements[tile->wall_a];
                assert(element->kind == PNTRELEM_WALL_A);
                if( (element->_wall.side & far_walls) != 0 )
                    bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
            }

            if( tile->wall_b != -1 )
            {
                struct PaintersElement* element = &elements[tile->wall_b];
                assert(element->kind == PNTRELEM_WALL_B);
                if( (element->_wall.side & far_walls) != 0 )
                    bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
            }

            if( tile->ground_decor != -1 )
            {
                struct PaintersElement* element = &elements[tile->ground_decor];
                assert(element->kind == PNTRELEM_GROUND_DECOR);
                bucket_emit_entity(&cmd_cur, cmd_end, element->_ground_decor.entity);
            }

            if( tile->ground_object_bottom != -1 )
            {
                struct PaintersElement* element = &elements[tile->ground_object_bottom];
                assert(element->kind == PNTRELEM_GROUND_OBJECT);
                bucket_emit_entity(&cmd_cur, cmd_end, element->_ground_object.entity);
            }

            if( tile->wall_decor_a != -1 )
            {
                struct PaintersElement* element = &elements[tile->wall_decor_a];
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
                        bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
                    else if( tile->wall_decor_b != -1 )
                    {
                        element = &elements[tile->wall_decor_b];
                        assert(element->kind == PNTRELEM_WALL_DECOR);
                        bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
                    }
                }
                else if( (element->_wall_decor._bf_side & far_walls) != 0 )
                {
                    bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
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
                    bucket_push_if_active(
                        w, paints, e_tile + 1, abs(tile_sx + 1 - camera_sx) + adz);
                }
                if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) &&
                    (spans & SPAN_FLAG_NORTH) )
                {
                    bucket_push_if_active(
                        w, paints, e_tile + width, adx + abs(tile_sz + 1 - camera_sz));
                }
                if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) &&
                    (spans & SPAN_FLAG_WEST) )
                {
                    bucket_push_if_active(
                        w, paints, e_tile - 1, abs(tile_sx - 1 - camera_sx) + adz);
                }
                if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) &&
                    (spans & SPAN_FLAG_SOUTH) )
                {
                    bucket_push_if_active(
                        w, paints, e_tile - width, adx + abs(tile_sz - 1 - camera_sz));
                }
            }
        }

        /* PAINT_STEP_GROUND == reference PAINTER_STEP_BASE for scenery / completion. */
        int visit_sc[64];
        int n_visit = 0;
        int blocked_undrawn = 0;

        for( int32_t sn = tile->scenery_head; sn != -1; sn = scenery_pool[sn].next )
        {
            int si = scenery_pool[sn].element_idx;
            struct ElementPaint* ep = &element_paints[si];
            if( ep->drawn )
                continue;

            struct PaintersElement* element = &elements[si];
            assert(element->kind == PNTRELEM_SCENERY);

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
                ep->drawn = true;
                continue;
            }

            /* Use the current tile's paintgrid_level for the footprint readiness check, matching
             * world3d's approach. This avoids a circular dependency when bridge level-shifting
             * places elements (with their original source_level) into a lower paintgrid_level
             * tile's scenery_head: checking at source_level would stall on tiles that wait on the
             * current tile to be DONE, creating a deadlock. */
            int all_base = 1;
            int row0 = min_tile_x + min_tile_z * width + paintgrid_level * level_stride;
            for( int oz = min_tile_z, row = row0; oz <= max_tile_z; oz++, row += width )
            {
                for( int ox = min_tile_x, ti = row; ox <= max_tile_x; ox++, ti++ )
                {
                    if( paints[ti].step < PAINT_STEP_GROUND )
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
            else
                blocked_undrawn = 1;
        }

        int some_drawn = 0;
        for( int vi = 0; vi < n_visit; vi++ )
        {
            int si = visit_sc[vi];
            struct ElementPaint* ep = &element_paints[si];
            ep->drawn = true;

            struct PaintersElement* element = &elements[si];
            assert(element->kind == PNTRELEM_SCENERY);
            bucket_emit_entity(&cmd_cur, cmd_end, element->_scenery.entity);

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
                int row0 = min_tile_x + min_tile_z * width + paintgrid_level * level_stride;
                for( int oz = min_tile_z, row = row0; oz <= max_tile_z; oz++, row += width )
                {
                    for( int ox = min_tile_x, ti = row; ox <= max_tile_x; ox++, ti++ )
                    {
                        int ndist = abs(ox - camera_sx) + abs(oz - camera_sz);
                        bucket_push_if_active(w, paints, ti, ndist);
                        some_drawn = 1;
                    }
                }
            }
        }
        /* Emitting scenery defers near-wall completion until a later visit (world3d parity). */
        if( some_drawn || n_visit > 0 )
            continue;

        if( blocked_undrawn )
            continue;

        if( tile->wall_decor_a != -1 )
        {
            struct PaintersElement* element = &elements[tile->wall_decor_a];
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
                    bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
                else if( tile->wall_decor_b != -1 )
                {
                    element = &elements[tile->wall_decor_b];
                    assert(element->kind == PNTRELEM_WALL_DECOR);
                    bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
                }
            }
            else if( (element->_wall_decor._bf_side & tile_paint->near_wall_flags) != 0 )
            {
                bucket_emit_entity(&cmd_cur, cmd_end, element->_wall_decor.entity);
            }
        }

        if( tile->wall_a != -1 )
        {
            struct PaintersElement* element = &elements[tile->wall_a];
            assert(element->kind == PNTRELEM_WALL_A);
            if( (element->_wall.side & tile_paint->near_wall_flags) != 0 )
                bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
        }

        if( tile->wall_b != -1 )
        {
            struct PaintersElement* element = &elements[tile->wall_b];
            assert(element->kind == PNTRELEM_WALL_B);
            if( (element->_wall.side & tile_paint->near_wall_flags) != 0 )
                bucket_emit_entity(&cmd_cur, cmd_end, element->_wall.entity);
        }

        tile_paint->step = PAINT_STEP_DONE;
        tiles_remaining--;

        if( paintgrid_level < levels - 1 )
        {
            bucket_push_if_active(w, paints, e_tile + level_stride, tile_dist);
        }

        if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
        {
            bucket_push_if_active(
                w, paints, e_tile + width, adx + abs(tile_sz + 1 - camera_sz));
        }
        if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
        {
            bucket_push_if_active(
                w, paints, e_tile - 1, abs(tile_sx - 1 - camera_sx) + adz);
        }
        if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
        {
            bucket_push_if_active(
                w, paints, e_tile - width, adx + abs(tile_sz - 1 - camera_sz));
        }
        if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
        {
            bucket_push_if_active(
                w, paints, e_tile + 1, abs(tile_sx + 1 - camera_sx) + adz);
        }
    }

    buffer->command_count = (int)(cmd_cur - cmd_base);
    TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_PAINTER_COMMANDS, buffer->command_count);

    painter_dump_command_order(painter, buffer);
    return 0;
}

#endif /* PAINTERS_BUCKET_U_C */
