#ifndef PAINTERS_WORLD3D_U_C
#define PAINTERS_WORLD3D_U_C

#include "painters_i.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct W3dPaint
{
    int32_t ll_prev;
    int32_t ll_next;
    uint8_t draw_front;
    uint8_t draw_back;
    uint8_t draw_primaries;
};

struct W3dSeed
{
    uint8_t phase;
    int32_t tile_idx;
};

struct PainterW3dCtx
{
    struct W3dPaint* paints;
    int sentinel_idx;
    struct W3dSeed* seeds;
    int seeds_cap;
    uint8_t* seed_seen;
    int seed_seen_nbytes;
};

#define W3(P) ((struct PainterW3dCtx*)(P)->w3d_ctx)

static int
w3d_ctx_init(struct Painter* painter)
{
    int tile_count = painter->tile_capacity;
    struct PainterW3dCtx* w = (struct PainterW3dCtx*)calloc(1, sizeof(struct PainterW3dCtx));
    if( !w )
        return -1;
    w->paints = malloc((size_t)(tile_count + 1) * sizeof(struct W3dPaint));
    w->sentinel_idx = tile_count;
    int seed_cap = 8 * painter->levels * (painter->width + 4) * (painter->height + 4) + 2048;
    if( seed_cap < 8192 )
        seed_cap = 8192;
    w->seeds = malloc((size_t)seed_cap * sizeof(struct W3dSeed));
    w->seeds_cap = seed_cap;
    w->seed_seen_nbytes = (painter->width * painter->height + 7) / 8 + 1;
    w->seed_seen = calloc((size_t)w->seed_seen_nbytes, 1);
    if( !w->paints || !w->seeds || !w->seed_seen )
    {
        free(w->paints);
        free(w->seeds);
        free(w->seed_seen);
        free(w);
        return -1;
    }
    painter->w3d_ctx = w;
    return 0;
}

static void
w3d_ctx_free(struct Painter* painter)
{
    struct PainterW3dCtx* w = W3(painter);
    if( !w )
        return;
    free(w->paints);
    free(w->seeds);
    free(w->seed_seen);
    free(w);
    painter->w3d_ctx = NULL;
}

static void
w3d_link_unlink(
    struct Painter* p,
    int idx)
{
    struct W3dPaint* s = &W3(p)->paints[idx];
    if( s->ll_prev < 0 )
        return;
    int pr = s->ll_prev;
    int nx = s->ll_next;
    W3(p)->paints[pr].ll_next = nx;
    W3(p)->paints[nx].ll_prev = pr;
    s->ll_prev = -1;
    s->ll_next = -1;
}

static void
w3d_link_push(
    struct Painter* p,
    int idx)
{
    int sent = W3(p)->sentinel_idx;
    w3d_link_unlink(p, idx);
    struct W3dPaint* s = &W3(p)->paints[idx];
    int tail = W3(p)->paints[sent].ll_prev;
    s->ll_prev = tail;
    s->ll_next = sent;
    W3(p)->paints[tail].ll_next = idx;
    W3(p)->paints[sent].ll_prev = idx;
}

static int
w3d_link_pop(struct Painter* p)
{
    int sent = W3(p)->sentinel_idx;
    int head = W3(p)->paints[sent].ll_next;
    if( head == sent )
        return -1;
    w3d_link_unlink(p, head);
    return head;
}

static int
w3d_link_is_empty(struct Painter* p)
{
    int sent = W3(p)->sentinel_idx;
    return W3(p)->paints[sent].ll_next == sent;
}

static void
w3d_link_init_sentinel(struct Painter* p)
{
    int s = W3(p)->sentinel_idx;
    W3(p)->paints[s].ll_prev = s;
    W3(p)->paints[s].ll_next = s;
}

static void
w3d_emit_seed(
    struct Painter* p,
    uint8_t phase,
    int sx,
    int sz,
    int slevel,
    int* seeds_len)
{
    if( sx < 0 || sz < 0 || sx >= p->width || sz >= p->height )
        return;
    int b = sz * p->width + sx;
    int bi = b / 8;
    int bb = 1 << (b % 8);
    if( (W3(p)->seed_seen[bi] & (uint8_t)bb) != 0 )
        return;
    W3(p)->seed_seen[bi] |= (uint8_t)bb;
    assert(*seeds_len < W3(p)->seeds_cap);
    W3(p)->seeds[*seeds_len].phase = phase;
    W3(p)->seeds[*seeds_len].tile_idx = painter_coord_idx(p, sx, sz, slevel);
    (*seeds_len)++;
}

static void
w3d_build_seeds(
    struct Painter* p,
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
    int gs_w = p->width;
    int gs_h = p->height;
    int L = p->levels;
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

                    memset(W3(p)->seed_seen, 0, (size_t)W3(p)->seed_seen_nbytes);

                    if( right_x >= min_draw_x )
                    {
                        if( fwd_z >= min_draw_z )
                            w3d_emit_seed(p, (uint8_t)phase, right_x, fwd_z, level, out_len);
                        if( bwd_z < max_draw_z )
                            w3d_emit_seed(p, (uint8_t)phase, right_x, bwd_z, level, out_len);
                    }
                    if( left_x < max_draw_x )
                    {
                        if( fwd_z >= min_draw_z )
                            w3d_emit_seed(p, (uint8_t)phase, left_x, fwd_z, level, out_len);
                        if( bwd_z < max_draw_z )
                            w3d_emit_seed(p, (uint8_t)phase, left_x, bwd_z, level, out_len);
                    }
                }
            }
        }
    }
    (void)gs_h;
    (void)gs_w;
}

static void
painter_w3d_emit_ground_pass(
    struct Painter* painter,
    struct PaintersBuffer* buffer,
    struct PaintersTile* tile,
    int tile_sx,
    int tile_sz,
    int camera_sx,
    int camera_sz,
    struct TilePaint* tile_paint)
{
    struct PaintersElement* element = NULL;
    struct PaintersTile* bridge_underpass_tile = NULL;
    struct ElementPaint* element_paint = NULL;

    int far_walls = far_wall_flags(camera_sx, camera_sz, tile_sx, tile_sz);
    tile_paint->near_wall_flags |= ~far_walls;

    if( tile->bridge_tile != -1 )
    {
        bridge_underpass_tile = &painter->tiles[tile->bridge_tile];

        push_command_terrain(
            buffer,
            PAINTER_TILE_X(painter, bridge_underpass_tile),
            PAINTER_TILE_Z(painter, bridge_underpass_tile),
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
            {
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
}

static void
painter_w3d_emit_near_wall_pass(
    struct Painter* painter,
    struct PaintersBuffer* buffer,
    struct PaintersTile* tile,
    int camera_sx,
    int camera_sz,
    struct TilePaint* tile_paint)
{
    struct PaintersElement* element = NULL;

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
            {
                push_command_entity(buffer, element->_wall_decor.entity);
            }
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
}

int
painter_paint_world3d(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel)
{
    (void)camera_slevel;
    if( !painter->w3d_ctx && w3d_ctx_init(painter) != 0 )
        return -1;
    if( !W3(painter)->paints || !W3(painter)->seeds || !W3(painter)->seed_seen )
        return -1;

    struct PaintersTile* tile = NULL;
    struct PaintersElement* element = NULL;
    struct TilePaint* tile_paint = NULL;
    struct ElementPaint* element_paint = NULL;

    buffer->command_count = 0;
    memset(painter->element_paints, 0x00, painter->element_count * sizeof(struct ElementPaint));

    int radius = 25;
    uint8_t draw_mask = painter->level_mask ? painter->level_mask : 0xFu;

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

    /* CHEB_OPT_CLEAR_BBOX_TILES-style: only reset state inside the draw rect (not full map). */
    painter_clear_tile_paints_region(
        painter, min_draw_x, max_draw_x, min_draw_z, max_draw_z, painter->levels);

    for( int s = 0; s < painter->levels; s++ )
    {
        for( int z = min_draw_z; z < max_draw_z; z++ )
        {
            for( int x = min_draw_x; x < max_draw_x; x++ )
            {
                int i = painter_coord_idx(painter, x, z, s);
                W3(painter)->paints[i].ll_prev = -1;
                W3(painter)->paints[i].ll_next = -1;
                W3(painter)->paints[i].draw_front = 0;
                W3(painter)->paints[i].draw_back = 0;
                W3(painter)->paints[i].draw_primaries = 0;
            }
        }
    }
    w3d_link_init_sentinel(painter);

    int eye_ix = camera_sx;
    int eye_iz = camera_sz;

    int tiles_remaining = 0;
    for( int level = 0; level < painter->levels; level++ )
    {
        for( int x = min_draw_x; x < max_draw_x; x++ )
        {
            for( int z = min_draw_z; z < max_draw_z; z++ )
            {
                int idx = painter_coord_idx(painter, x, z, level);
                tile = &painter->tiles[idx];
                tile_paint = &painter->tile_paints[idx];
                {
                    uint16_t tile_flags = painters_tile_get_flags(tile);
                    if( tile_excluded_by_bridge_or_draw_mask(
                            tile_flags, painters_tile_get_slevel(tile), draw_mask) )
                        continue;
                }
                if( !painter_cullmap_tile_visible(painter, tile_paint, x, z, camera_sx, camera_sz) )
                {
                    tile_paint->step = PAINT_STEP_DONE;
                    continue;
                }
                W3(painter)->paints[idx].draw_front = 1;
                W3(painter)->paints[idx].draw_back = 1;
                W3(painter)->paints[idx].draw_primaries = (tile->scenery_head != -1) ? 1u : 0u;
                tiles_remaining++;
            }
        }
    }

    int seeds_len = 0;
    w3d_build_seeds(
        painter,
        eye_ix,
        eye_iz,
        min_draw_x,
        max_draw_x,
        min_draw_z,
        max_draw_z,
        radius,
        &seeds_len);

    int seed_idx = 0;
    int check_adjacent = 1;

    for( ;; )
    {
        if( w3d_link_is_empty(painter) )
        {
            if( tiles_remaining == 0 )
                break;
            int seeded = 0;
            while( seed_idx < seeds_len )
            {
                struct W3dSeed* sd = &W3(painter)->seeds[seed_idx++];
                if( W3(painter)->paints[sd->tile_idx].draw_front )
                {
                    w3d_link_push(painter, sd->tile_idx);
                    check_adjacent = (sd->phase == 1);
                    seeded = 1;
                    break;
                }
            }
            if( !seeded )
                break;
        }

        int tile_idx = w3d_link_pop(painter);
        if( tile_idx < 0 )
            break;
        struct W3dPaint* wp = &W3(painter)->paints[tile_idx];
        if( !wp->draw_back )
            continue;

        tile = &painter->tiles[tile_idx];
        int tile_sx = PAINTER_TILE_X(painter, tile);
        int tile_sz = PAINTER_TILE_Z(painter, tile);
        int tile_slevel = painters_tile_get_slevel(tile);
        int grid_level = painters_tile_get_grid_level(tile);
        tile_paint = &painter->tile_paints[tile_idx];

        {
            uint16_t tile_flags = painters_tile_get_flags(tile);
            if( tile_excluded_by_bridge_or_draw_mask(tile_flags, tile_slevel, draw_mask) )
            {
                wp->draw_front = 0;
                wp->draw_back = 0;
                wp->draw_primaries = 0;
                continue;
            }
        }

        if( !painter_cullmap_tile_visible(
                painter, tile_paint, tile_sx, tile_sz, camera_sx, camera_sz) )
        {
            tile_paint->step = PAINT_STEP_DONE;
            wp->draw_front = 0;
            wp->draw_back = 0;
            wp->draw_primaries = 0;
            if( grid_level < painter->levels - 1 )
            {
                int other_idx = step_idx_up(painter, tile_idx);
                if( W3(painter)->paints[other_idx].draw_back )
                    w3d_link_push(painter, other_idx);
            }
            if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
            {
                int other_idx = step_idx_east(painter, tile_idx);
                if( W3(painter)->paints[other_idx].draw_back )
                    w3d_link_push(painter, other_idx);
            }
            if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
            {
                int other_idx = step_idx_west(painter, tile_idx);
                if( W3(painter)->paints[other_idx].draw_back )
                    w3d_link_push(painter, other_idx);
            }
            if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
            {
                int other_idx = step_idx_north(painter, tile_idx);
                if( W3(painter)->paints[other_idx].draw_back )
                    w3d_link_push(painter, other_idx);
            }
            if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
            {
                int other_idx = step_idx_south(painter, tile_idx);
                if( W3(painter)->paints[other_idx].draw_back )
                    w3d_link_push(painter, other_idx);
            }
            continue;
        }

        if( wp->draw_front )
        {
            if( check_adjacent )
            {
                if( grid_level > 0 )
                {
                    int below_idx = step_idx_down(painter, tile_idx);
                    if( W3(painter)->paints[below_idx].draw_back )
                        continue;
                }

                if( tile_is_west_inbounds(tile_sx, camera_sx, min_draw_x) )
                {
                    int adj = step_idx_west(painter, tile_idx);
                    struct W3dPaint* awp = &W3(painter)->paints[adj];
                    if( awp->draw_back && (awp->draw_front || (tile->spans & SPAN_FLAG_WEST) == 0) )
                        continue;
                }
                if( tile_is_east_inbounds(tile_sx, camera_sx, max_draw_x) )
                {
                    int adj = step_idx_east(painter, tile_idx);
                    struct W3dPaint* awp = &W3(painter)->paints[adj];
                    if( awp->draw_back && (awp->draw_front || (tile->spans & SPAN_FLAG_EAST) == 0) )
                        continue;
                }
                if( tile_is_south_inbounds(tile_sz, camera_sz, min_draw_z) )
                {
                    int adj = step_idx_south(painter, tile_idx);
                    struct W3dPaint* awp = &W3(painter)->paints[adj];
                    if( awp->draw_back &&
                        (awp->draw_front || (tile->spans & SPAN_FLAG_SOUTH) == 0) )
                        continue;
                }
                if( tile_is_north_inbounds(tile_sz, camera_sz, max_draw_z) )
                {
                    int adj = step_idx_north(painter, tile_idx);
                    struct W3dPaint* awp = &W3(painter)->paints[adj];
                    if( awp->draw_back &&
                        (awp->draw_front || (tile->spans & SPAN_FLAG_NORTH) == 0) )
                        continue;
                }
            }
            else
            {
                check_adjacent = 1;
            }

            wp->draw_front = 0;
            painter_w3d_emit_ground_pass(
                painter, buffer, tile, tile_sx, tile_sz, camera_sx, camera_sz, tile_paint);

            unsigned spans = tile->spans;
            if( spans )
            {
                if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) &&
                    (spans & SPAN_FLAG_EAST) )
                {
                    int adj = step_idx_east(painter, tile_idx);
                    if( W3(painter)->paints[adj].draw_back )
                        w3d_link_push(painter, adj);
                }
                if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) &&
                    (spans & SPAN_FLAG_NORTH) )
                {
                    int adj = step_idx_north(painter, tile_idx);
                    if( W3(painter)->paints[adj].draw_back )
                        w3d_link_push(painter, adj);
                }
                if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) &&
                    (spans & SPAN_FLAG_WEST) )
                {
                    int adj = step_idx_west(painter, tile_idx);
                    if( W3(painter)->paints[adj].draw_back )
                        w3d_link_push(painter, adj);
                }
                if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) &&
                    (spans & SPAN_FLAG_SOUTH) )
                {
                    int adj = step_idx_south(painter, tile_idx);
                    if( W3(painter)->paints[adj].draw_back )
                        w3d_link_push(painter, adj);
                }
            }
        }

        if( wp->draw_primaries )
        {
            wp->draw_primaries = 0;
            int buf_si[100];
            int buf_n = 0;

            for( int32_t sn = tile->scenery_head; sn != -1; sn = painter->scenery_pool[sn].next )
            {
                int si = painter->scenery_pool[sn].element_idx;
                element_paint = &painter->element_paints[si];
                if( element_paint->drawn )
                    continue;

                element = &painter->elements[si];
                assert(element->kind == PNTRELEM_SCENERY);

                int blocked = 0;
                int fp_min_x = element->sx;
                int fp_min_z = element->sz;
                int fp_max_x = fp_min_x + element->_scenery.size_x - 1;
                int fp_max_z = fp_min_z + element->_scenery.size_z - 1;
                if( fp_max_x > max_draw_x - 1 )
                    fp_max_x = max_draw_x - 1;
                if( fp_max_z > max_draw_z - 1 )
                    fp_max_z = max_draw_z - 1;
                if( fp_min_x < min_draw_x )
                    fp_min_x = min_draw_x;
                if( fp_min_z < min_draw_z )
                    fp_min_z = min_draw_z;
                if( fp_max_x > painter->width - 1 )
                    fp_max_x = painter->width - 1;
                if( fp_max_z > painter->height - 1 )
                    fp_max_z = painter->height - 1;
                if( fp_min_x < 0 )
                    fp_min_x = 0;
                if( fp_min_z < 0 )
                    fp_min_z = 0;
                if( fp_min_x > fp_max_x || fp_min_z > fp_max_z )
                    continue;
                for( int lx = fp_min_x; lx <= fp_max_x && !blocked; lx++ )
                {
                    for( int lz = fp_min_z; lz <= fp_max_z; lz++ )
                    {
                        int oidx = painter_coord_idx(painter, lx, lz, grid_level);
                        if( W3(painter)->paints[oidx].draw_front )
                        {
                            wp->draw_primaries = 1;
                            blocked = 1;
                            break;
                        }
                    }
                }
                if( !blocked )
                {
                    int dist_x = (eye_ix - fp_min_x) > (fp_max_x - eye_ix) ? (eye_ix - fp_min_x)
                                                                           : (fp_max_x - eye_ix);
                    int dz_a = eye_iz - fp_min_z;
                    int dz_b = fp_max_z - eye_iz;
                    int dz = dz_a > dz_b ? dz_a : dz_b;
                    (void)dist_x;
                    (void)dz;
                    if( buf_n < 100 )
                    {
                        buf_si[buf_n] = si;
                        buf_n++;
                    }
                }
            }

            while( buf_n > 0 )
            {
                int best_i = -1;
                int best_d = -999999;
                for( int i = 0; i < buf_n; i++ )
                {
                    int si = buf_si[i];
                    element = &painter->elements[si];
                    int min_x = element->sx;
                    int min_z = element->sz;
                    int max_x = min_x + element->_scenery.size_x - 1;
                    int max_z = min_z + element->_scenery.size_z - 1;
                    int dist_x =
                        (eye_ix - min_x) > (max_x - eye_ix) ? (eye_ix - min_x) : (max_x - eye_ix);
                    int dz_a = eye_iz - min_z;
                    int dz_b = max_z - eye_iz;
                    int dz = dz_a > dz_b ? dz_a : dz_b;
                    int d = dist_x + dz;
                    if( painter->element_paints[si].drawn )
                        continue;
                    if( d > best_d )
                    {
                        best_d = d;
                        best_i = i;
                    }
                }
                if( best_i < 0 )
                    break;
                int si = buf_si[best_i];
                buf_si[best_i] = buf_si[buf_n - 1];
                buf_n--;

                element_paint = &painter->element_paints[si];
                element_paint->drawn = true;
                element = &painter->elements[si];
                assert(element->kind == PNTRELEM_SCENERY);
                push_command_entity(buffer, element->_scenery.entity);

                int occ_min_x = element->sx;
                int occ_min_z = element->sz;
                int occ_max_x = occ_min_x + element->_scenery.size_x - 1;
                int occ_max_z = occ_min_z + element->_scenery.size_z - 1;
                if( occ_max_x > max_draw_x - 1 )
                    occ_max_x = max_draw_x - 1;
                if( occ_max_z > max_draw_z - 1 )
                    occ_max_z = max_draw_z - 1;
                if( occ_min_x < min_draw_x )
                    occ_min_x = min_draw_x;
                if( occ_min_z < min_draw_z )
                    occ_min_z = min_draw_z;
                if( occ_max_x > painter->width - 1 )
                    occ_max_x = painter->width - 1;
                if( occ_max_z > painter->height - 1 )
                    occ_max_z = painter->height - 1;
                if( occ_min_x < 0 )
                    occ_min_x = 0;
                if( occ_min_z < 0 )
                    occ_min_z = 0;
                if( occ_min_x <= occ_max_x && occ_min_z <= occ_max_z )
                {
                    for( int lx = occ_min_x; lx <= occ_max_x; lx++ )
                    {
                        for( int lz = occ_min_z; lz <= occ_max_z; lz++ )
                        {
                            int occ = painter_coord_idx(painter, lx, lz, grid_level);
                            if( occ != tile_idx && W3(painter)->paints[occ].draw_back )
                                w3d_link_push(painter, occ);
                        }
                    }
                }
            }

            if( wp->draw_primaries )
                continue;
        }

        if( !wp->draw_back )
            continue;

        if( tile_is_west_inbounds(tile_sx, camera_sx, min_draw_x) )
        {
            int adj = step_idx_west(painter, tile_idx);
            if( W3(painter)->paints[adj].draw_back )
                continue;
        }
        if( tile_is_east_inbounds(tile_sx, camera_sx, max_draw_x) )
        {
            int adj = step_idx_east(painter, tile_idx);
            if( W3(painter)->paints[adj].draw_back )
                continue;
        }
        if( tile_is_south_inbounds(tile_sz, camera_sz, min_draw_z) )
        {
            int adj = step_idx_south(painter, tile_idx);
            if( W3(painter)->paints[adj].draw_back )
                continue;
        }
        if( tile_is_north_inbounds(tile_sz, camera_sz, max_draw_z) )
        {
            int adj = step_idx_north(painter, tile_idx);
            if( W3(painter)->paints[adj].draw_back )
                continue;
        }

        wp->draw_back = 0;
        tiles_remaining--;

        painter_w3d_emit_near_wall_pass(painter, buffer, tile, camera_sx, camera_sz, tile_paint);
        tile_paint->step = PAINT_STEP_DONE;

        if( grid_level < painter->levels - 1 )
        {
            int above = step_idx_up(painter, tile_idx);
            if( W3(painter)->paints[above].draw_back )
                w3d_link_push(painter, above);
        }
        if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
        {
            int adj = step_idx_east(painter, tile_idx);
            if( W3(painter)->paints[adj].draw_back )
                w3d_link_push(painter, adj);
        }
        if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
        {
            int adj = step_idx_north(painter, tile_idx);
            if( W3(painter)->paints[adj].draw_back )
                w3d_link_push(painter, adj);
        }
        if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
        {
            int adj = step_idx_west(painter, tile_idx);
            if( W3(painter)->paints[adj].draw_back )
                w3d_link_push(painter, adj);
        }
        if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
        {
            int adj = step_idx_south(painter, tile_idx);
            if( W3(painter)->paints[adj].draw_back )
                w3d_link_push(painter, adj);
        }
    }

    return 0;
}

#endif
