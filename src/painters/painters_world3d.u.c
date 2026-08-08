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

struct PainterW3dCtx
{
    struct W3dPaint* paints;
    int sentinel_idx;
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
    if( !w->paints )
    {
        free(w->paints);
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
painter_w3d_emit_ground_pass(
    struct Painter* painter,
    struct PaintersBuffer* buffer,
    struct PaintersTile* tile,
    int tile_sx,
    int tile_sz,
    int camera_sx,
    int camera_sz,
    int camera_slevel,
    struct TilePaint* tile_paint)
{
    struct PaintersElement* element = NULL;
    struct PaintersTile* bridge_underpass_tile = NULL;
    struct ElementPaint* element_paint = NULL;

    int far_walls = far_wall_flags(camera_sx, camera_sz, tile_sx, tile_sz);
    tile_paint->near_wall_flags |= ~far_walls;

    /* Occlusion uses mesh_level = reference originalLevel (survives push-down). */
    int occlusion_level = painters_tile_get_mesh_level(tile);
    struct SceneOccluders* occ = painter->occluders;
    int ground_hidden =
        painter_tile_ground_hidden(occ, tile_paint, occlusion_level, tile_sx, tile_sz);

    if( tile->bridge_tile != -1 )
    {
        bridge_underpass_tile = &painter->tiles[tile->bridge_tile];

        /* Reference groundOccluded(0, ...) on the linked underpass square. */
        if( !(occ &&
              scene_occluders_ground_tile_hidden(
                  occ,
                  painters_tile_get_mesh_level(bridge_underpass_tile),
                  bridge_underpass_tile->sx,
                  bridge_underpass_tile->sz)) )
        {
            push_command_terrain(
                buffer,
                bridge_underpass_tile->sx,
                bridge_underpass_tile->sz,
                painters_tile_get_mesh_level(bridge_underpass_tile));
        }

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

    {
            /* See PaintersTile::terrain_levels. */
            unsigned set = tile->terrain_levels;
            for( int ml = 0; ml < 4; ml++ )
                if( set & (1u << ml) )
                {
                    if( !ground_hidden )
                        push_command_terrain(buffer, tile_sx, tile_sz, ml);
                    else if( camera_slevel >= 0 && ml <= camera_slevel )
                        push_command_terrain_pick_only(buffer, tile_sx, tile_sz, ml);
                }
        }

    if( tile->wall_a != -1 )
    {
        element = &painter->elements[tile->wall_a];
        assert(element->kind == PNTRELEM_WALL_A);

        if( (element->_wall.side & far_walls) != 0 &&
            !(occ && scene_occluders_wall_hidden(
                         occ, occlusion_level, tile_sx, tile_sz, element->_wall.side)) )
            push_command_entity(buffer, element->_wall.entity);
    }

    if( tile->wall_b != -1 )
    {
        element = &painter->elements[tile->wall_b];
        assert(element->kind == PNTRELEM_WALL_B);

        if( (element->_wall.side & far_walls) != 0 &&
            !(occ && scene_occluders_wall_hidden(
                         occ, occlusion_level, tile_sx, tile_sz, element->_wall.side)) )
            push_command_entity(buffer, element->_wall.entity);
    }

    if( tile->ground_decor != -1 && painter_ground_decor_enabled() )
    {
        element = &painter->elements[tile->ground_decor];
        assert(element->kind == PNTRELEM_GROUND_DECOR);
        if( !(occ && scene_occluders_column_hidden(occ, occlusion_level, tile_sx, tile_sz, 0)) )
            push_command_entity(buffer, element->_ground_decor.entity);
    }

    if( tile->ground_object_bottom != -1 )
    {
        element = &painter->elements[tile->ground_object_bottom];
        assert(element->kind == PNTRELEM_GROUND_OBJECT);
        if( !(occ && scene_occluders_column_hidden(occ, occlusion_level, tile_sx, tile_sz, 0)) )
            push_command_entity(buffer, element->_ground_object.entity);
    }

    if( tile->wall_decor_a != -1 )
    {
        element = &painter->elements[tile->wall_decor_a];
        assert(element->kind == PNTRELEM_WALL_DECOR);
        int decor_hidden =
            occ &&
            scene_occluders_column_hidden(
                occ, occlusion_level, tile_sx, tile_sz, element->_wall_decor.model_height);
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
                if( !decor_hidden )
                    push_command_entity(buffer, element->_wall_decor.entity);
            }
            else if( tile->wall_decor_b != -1 )
            {
                element = &painter->elements[tile->wall_decor_b];
                assert(element->kind == PNTRELEM_WALL_DECOR);
                if( !decor_hidden )
                    push_command_entity(buffer, element->_wall_decor.entity);
            }
        }
        else if( (element->_wall_decor._bf_side & far_walls) != 0 )
        {
            if( !decor_hidden )
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
    int occlusion_level = painters_tile_get_mesh_level(tile);
    int tile_sx = tile->sx;
    int tile_sz = tile->sz;
    struct SceneOccluders* occ = painter->occluders;
    int decor_hidden = 0;
    if( tile->wall_decor_a != -1 )
    {
        element = &painter->elements[tile->wall_decor_a];
        decor_hidden =
            occ &&
            scene_occluders_column_hidden(
                occ, occlusion_level, tile_sx, tile_sz, element->_wall_decor.model_height);
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

            if( z_near >= x_near )
            {
                if( !decor_hidden )
                    push_command_entity(buffer, element->_wall_decor.entity);
            }
            else if( tile->wall_decor_b != -1 )
            {
                element = &painter->elements[tile->wall_decor_b];
                assert(element->kind == PNTRELEM_WALL_DECOR);

                if( !decor_hidden )
                    push_command_entity(buffer, element->_wall_decor.entity);
            }
        }
        else if( (element->_wall_decor._bf_side & tile_paint->near_wall_flags) != 0 )
        {
            if( !decor_hidden )
                push_command_entity(buffer, element->_wall_decor.entity);
        }
    }

    if( tile->wall_a != -1 )
    {
        element = &painter->elements[tile->wall_a];
        assert(element->kind == PNTRELEM_WALL_A);

        if( (element->_wall.side & tile_paint->near_wall_flags) != 0 &&
            !(occ && scene_occluders_wall_hidden(
                         occ, occlusion_level, tile_sx, tile_sz, element->_wall.side)) )
            push_command_entity(buffer, element->_wall.entity);
    }

    if( tile->wall_b != -1 )
    {
        element = &painter->elements[tile->wall_b];
        assert(element->kind == PNTRELEM_WALL_B);

        if( (element->_wall.side & tile_paint->near_wall_flags) != 0 &&
            !(occ && scene_occluders_wall_hidden(
                         occ, occlusion_level, tile_sx, tile_sz, element->_wall.side)) )
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
    if( !painter->w3d_ctx && w3d_ctx_init(painter) != 0 )
        return -1;
    if( !W3(painter)->paints )
        return -1;

    struct PaintersTile* tile = NULL;
    struct PaintersElement* element = NULL;
    struct TilePaint* tile_paint = NULL;
    struct ElementPaint* element_paint = NULL;

    buffer->command_count = 0;
    memset(painter->element_paints, 0x00, painter->element_count * sizeof(struct ElementPaint));

    int radius = painter->draw_distance;
    uint8_t draw_mask = painter->level_mask ? painter->level_mask : 0xFu;

    int draw_center_sx;
    int draw_center_sz;
    int min_draw_x;
    int max_draw_x;
    int min_draw_z;
    int max_draw_z;
    painter_resolve_draw_box(
        painter,
        camera_sx,
        camera_sz,
        radius,
        &draw_center_sx,
        &draw_center_sz,
        &min_draw_x,
        &max_draw_x,
        &min_draw_z,
        &max_draw_z);
    (void)draw_center_sx;
    (void)draw_center_sz;

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
                painter->tile_paints[i].occlusion = TILE_OCCLUSION_UNKNOWN;
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
                            tile_flags, painters_tile_get_visible_gte_level(tile), draw_mask) )
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

    struct PainterSeedGen seed_gen;
    seed_gen_init(
        &seed_gen,
        eye_ix,
        eye_iz,
        min_draw_x,
        max_draw_x,
        min_draw_z,
        max_draw_z,
        painter->levels,
        painter_seed_radius_for_box(
            eye_ix, eye_iz, min_draw_x, max_draw_x, min_draw_z, max_draw_z, radius));

    int check_adjacent = 1;

    for( ;; )
    {
        if( w3d_link_is_empty(painter) )
        {
            if( tiles_remaining == 0 )
                break;
            int seeded = 0;
            int sx, sz, level, phase;
            while( seed_gen_next(&seed_gen, &sx, &sz, &level, &phase) )
            {
                int tidx = painter_coord_idx(painter, sx, sz, level);
                if( W3(painter)->paints[tidx].draw_front )
                {
                    w3d_link_push(painter, tidx);
                    check_adjacent = (phase == 1);
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
        int tile_sx = tile->sx;
        int tile_sz = tile->sz;
        int paintgrid_level = painters_tile_get_paintgrid_level(tile);
        int occlusion_level = painters_tile_get_mesh_level(tile);
        tile_paint = &painter->tile_paints[tile_idx];

        if( wp->draw_front )
        {
            if( check_adjacent )
            {
                if( paintgrid_level > 0 )
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
                painter, buffer, tile, tile_sx, tile_sz, camera_sx, camera_sz, camera_slevel,
                tile_paint);

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
            int some_drawn = 0;

            for( int32_t sn = tile->scenery_head; sn != -1; sn = painter->scenery_pool[sn].next )
            {
                int si = painter->scenery_pool[sn].element_idx;
                element_paint = &painter->element_paints[si];
                if( element_paint->drawn )
                    continue;

                element = &painter->elements[si];
                assert(element->kind == PNTRELEM_SCENERY);

                /* RAISED ground items emit at tile completion (Client-TS
                 * GroundObject.height != 0), not in the scenery pass. */
                if( scenery_is_raised(element) )
                    continue;

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
                        int oidx = painter_coord_idx(painter, lx, lz, paintgrid_level);
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
                int best_dsq = -1;
                for( int i = 0; i < buf_n; i++ )
                {
                    int si = buf_si[i];
                    int min_x;
                    int min_z;
                    int max_x;
                    int max_z;
                    int dist_x;
                    int dz_a;
                    int dz_b;
                    int dz;
                    int d;
                    int mid_x;
                    int mid_z;
                    int dx;
                    int dz_w;
                    int dsq;
                    if( painter->element_paints[si].drawn )
                        continue;
                    element = &painter->elements[si];
                    min_x = element->sx;
                    min_z = element->sz;
                    max_x = min_x + element->_scenery.size_x - 1;
                    max_z = min_z + element->_scenery.size_z - 1;
                    dist_x =
                        (eye_ix - min_x) > (max_x - eye_ix) ? (eye_ix - min_x) : (max_x - eye_ix);
                    dz_a = eye_iz - min_z;
                    dz_b = max_z - eye_iz;
                    dz = dz_a > dz_b ? dz_a : dz_b;
                    d = dist_x + dz;
                    /* Modern deob tie-break: equal key → larger squared XZ dist. */
                    mid_x = min_x + max_x;
                    mid_z = min_z + max_z;
                    dx = mid_x - 2 * eye_ix;
                    dz_w = mid_z - 2 * eye_iz;
                    dsq = dx * dx + dz_w * dz_w;
                    if( d > best_d || (d == best_d && dsq > best_dsq) )
                    {
                        best_d = d;
                        best_dsq = dsq;
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
                some_drawn = 1;
                element = &painter->elements[si];
                assert(element->kind == PNTRELEM_SCENERY);
                if( !(painter->occluders &&
                      scene_occluders_footprint_hidden(
                          painter->occluders,
                          occlusion_level,
                          (int)element->sx,
                          (int)element->sz,
                          element->_scenery.size_x,
                          element->_scenery.size_z,
                          element->_scenery.model_height)) )
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
                            int occ = painter_coord_idx(painter, lx, lz, paintgrid_level);
                            if( occ != tile_idx && W3(painter)->paints[occ].draw_back )
                                w3d_link_push(painter, occ);
                        }
                    }
                }
            }

            if( wp->draw_primaries )
            {
                /* Containment deferral: a STACK_BASE drawn on this tile does not
                 * re-queue tile_idx itself. Push so the blocked contained loc
                 * retries in the same wave. */
                if( some_drawn )
                    w3d_link_push(painter, tile_idx);
                continue;
            }
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

        /* Elevated ground items (RAISED): after all locs, before near walls —
         * Client-TS World.fill elevated GroundObject at tile completion. */
        for( int32_t sn = tile->scenery_head; sn != -1; sn = painter->scenery_pool[sn].next )
        {
            int si = painter->scenery_pool[sn].element_idx;
            element_paint = &painter->element_paints[si];
            if( element_paint->drawn )
                continue;
            element = &painter->elements[si];
            if( !scenery_is_raised(element) )
                continue;
            element_paint->drawn = true;
            push_command_entity(buffer, element->_scenery.entity);
        }

        painter_w3d_emit_near_wall_pass(painter, buffer, tile, camera_sx, camera_sz, tile_paint);
        tile_paint->step = PAINT_STEP_DONE;

        if( paintgrid_level < painter->levels - 1 )
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
