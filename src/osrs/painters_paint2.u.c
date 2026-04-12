#ifndef PAINTERS_PAINT2_U_C
#define PAINTERS_PAINT2_U_C

#include <stdlib.h>

#define PAINTER_WF_WAVE_CAP 96

struct Paint2Ctx {
    struct IntQueue wf_wave_q[PAINTER_WF_WAVE_CAP];
    int wf_min_wave;
};

#define P2(P) ((struct Paint2Ctx*)(P)->paint2_ctx)

static int
paint2_ctx_init(struct Painter* painter)
{
    struct Paint2Ctx* c = (struct Paint2Ctx*)calloc(1, sizeof(struct Paint2Ctx));
    if( !c )
        return -1;
    for( int i = 0; i < PAINTER_WF_WAVE_CAP; i++ )
        int_queue_init(&c->wf_wave_q[i], 4096);
    c->wf_min_wave = 0;
    painter->paint2_ctx = c;
    return 0;
}

static void
paint2_ctx_free(struct Painter* painter)
{
    struct Paint2Ctx* c = P2(painter);
    if( !c )
        return;
    for( int i = 0; i < PAINTER_WF_WAVE_CAP; i++ )
        int_queue_free(&c->wf_wave_q[i]);
    free(c);
    painter->paint2_ctx = NULL;
}

static void
wf_queue_reset(struct Painter* painter)
{
    for( int i = 0; i < PAINTER_WF_WAVE_CAP; i++ )
    {
        P2(painter)->wf_wave_q[i].head = 0;
        P2(painter)->wf_wave_q[i].tail = 0;
        P2(painter)->wf_wave_q[i].length = 0;
    }
    P2(painter)->wf_min_wave = 0;
}

static bool
wf_queue_is_empty(struct Painter* painter)
{
    for( int i = 0; i < PAINTER_WF_WAVE_CAP; i++ )
    {
        if( P2(painter)->wf_wave_q[i].length > 0 )
            return false;
    }
    return true;
}

static void
wf_queue_push(
    struct Painter* painter,
    int tile_idx,
    int wave)
{
    assert(wave >= 0);
    assert(wave < PAINTER_WF_WAVE_CAP);
    if( wave < P2(painter)->wf_min_wave )
        P2(painter)->wf_min_wave = wave;
    int_queue_push_wrap(&P2(painter)->wf_wave_q[wave], tile_idx);
}

static int
wf_queue_pop(struct Painter* painter)
{
    int w = P2(painter)->wf_min_wave;
    while( w < PAINTER_WF_WAVE_CAP && P2(painter)->wf_wave_q[w].length == 0 )
        w++;
    assert(w < PAINTER_WF_WAVE_CAP);
    P2(painter)->wf_min_wave = w;
    int val = int_queue_pop(&P2(painter)->wf_wave_q[w]);
    int tile_idx = val >> 8;
    return (tile_idx << 8) | w;
}

static inline void
wf_paint2_push(
    struct Painter* painter,
    int tile_idx,
    int wave)
{
    painter->tile_paints[tile_idx].queue_count++;
    wf_queue_push(painter, tile_idx, wave);
}

int
painter_paint2(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel)
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

    memset(painter->tile_paints, 0x00, painter->tile_capacity * sizeof(struct TilePaint));
    memset(painter->element_paints, 0x00, painter->element_count * sizeof(struct ElementPaint));
    wf_queue_reset(painter);

    // Generate painter's algorithm coordinate list - farthest to nearest
    int radius = 25;
    int coord_list_x[4];
    int coord_list_z[4];
    int max_level = 4;

    int coord_list_length = 0;

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

    if( min_draw_x >= max_draw_x )
        return 0;
    if( min_draw_z >= max_draw_z )
        return 0;

    coord_list_x[0] = min_draw_x;
    coord_list_z[0] = min_draw_z;

    coord_list_x[1] = min_draw_x;
    coord_list_z[1] = max_draw_z - 1;

    coord_list_x[2] = max_draw_x - 1;
    coord_list_z[2] = min_draw_z;

    coord_list_x[3] = max_draw_x - 1;
    coord_list_z[3] = max_draw_z - 1;
    coord_list_length = 4;

    // for( int x = min_draw_x; x < max_draw_x; x++ )
    // {
    //     for( int z = min_draw_z; z < max_draw_z; z++ )
    //     {
    //         tile_paint = tile_paint_at(painter, x, z, 0);
    //         tile_paint->step = PAINT_STEP_READY;
    //     }
    // }

    // for( int x = min_draw_x; x < max_draw_x; x++ )
    // {
    //     tile_paint = tile_paint_at(painter, x, min_draw_z, 1);
    //     tile_paint->step = PAINT_STEP_READY;

    //     tile_paint = tile_paint_at(painter, x, max_draw_z - 1, 1);
    //     tile_paint->step = PAINT_STEP_READY;
    // }

    // for( int z = min_draw_z; z < max_draw_z; z++ )
    // {
    //     tile_paint = tile_paint_at(painter, min_draw_x, z, 1);
    //     tile_paint->step = PAINT_STEP_READY;

    //     tile_paint = tile_paint_at(painter, max_draw_x - 1, z, 1);
    //     tile_paint->step = PAINT_STEP_READY;
    // }

    // for( int level = 0; level < painter->levels; level++ )
    // {
    //     for( int dx = -radius; dx <= 0; dx++ )
    //     {
    //         int xleft = camera_sx + dx;
    //         int xright = camera_sx - dx;
    //         if( xleft < min_draw_x && xright > max_draw_x )
    //             continue;
    //         for( int dz = -radius; dz <= 0; dz++ )
    //         {
    //             int ztop = camera_sz + dz;
    //             int zbottom = camera_sz - dz;

    //             if( xleft >= min_draw_x && xleft < max_draw_x )
    //             {
    //                 if( ztop >= min_draw_z && ztop < max_draw_z )
    //                 {
    //                     coord_list_x[coord_list_length] = xleft;
    //                     coord_list_z[coord_list_length] = ztop;
    //                     coord_list_level[coord_list_length] = level;
    //                     coord_list_length++;
    //                 }
    //                 if( zbottom >= min_draw_z && zbottom < max_draw_z )
    //                 {
    //                     coord_list_x[coord_list_length] = xleft;
    //                     coord_list_z[coord_list_length] = zbottom;
    //                     coord_list_level[coord_list_length] = level;
    //                     coord_list_length++;
    //                 }
    //             }
    //             if( xright >= min_draw_x && xright < max_draw_x )
    //             {
    //                 if( ztop >= min_draw_z && ztop < max_draw_z )
    //                 {
    //                     coord_list_x[coord_list_length] = xright;
    //                     coord_list_z[coord_list_length] = ztop;
    //                     coord_list_level[coord_list_length] = level;
    //                     coord_list_length++;
    //                 }
    //                 if( zbottom >= min_draw_z && zbottom < max_draw_z )
    //                 {
    //                     coord_list_x[coord_list_length] = xright;
    //                     coord_list_z[coord_list_length] = zbottom;
    //                     coord_list_level[coord_list_length] = level;
    //                     coord_list_length++;
    //                 }
    //             }
    //         }
    //     }
    // }

    // Render tiles in painter's algorithm order (farthest to nearest)
    // Starting at the corners
    for( int i = 0; i < coord_list_length; i++ )
    {
        int coord_sx = coord_list_x[i];
        int coord_sz = coord_list_z[i];
        // int coord_level = coord_list_level[i];

        assert(coord_sx >= min_draw_x);
        assert(coord_sx < max_draw_x);
        assert(coord_sz >= min_draw_z);
        assert(coord_sz < max_draw_z);

        int coord_idx = painter_coord_idx(painter, coord_sx, coord_sz, 0);
        wf_paint2_push(painter, coord_idx, 0);
    } // for( int i = 0; i < coord_list_length; i++ )

    while( !wf_queue_is_empty(painter) )
    {
        int val = wf_queue_pop(painter);

        int tile_idx = val >> 8;
        int prio = val & 0xFF;

        tile = &painter->tiles[tile_idx];

        int tile_sx = PAINTER_TILE_X(painter, tile);
        int tile_sz = PAINTER_TILE_Z(painter, tile);
        int tile_slevel = painters_tile_get_slevel(tile);

        tile_paint = &painter->tile_paints[tile_idx];
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
                other_paint = &painter->tile_paints[other_idx];
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    assert(prio + 1 < PAINTER_WF_WAVE_CAP);
                    wf_paint2_push(painter, other_idx, prio + 1);
                }
            }
            if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
            {
                int other_idx = step_idx_east(painter, tile_idx);
                other_paint = &painter->tile_paints[other_idx];
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    assert(prio + 1 < PAINTER_WF_WAVE_CAP);
                    wf_paint2_push(painter, other_idx, prio + 1);
                }
            }
            if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
            {
                int other_idx = step_idx_west(painter, tile_idx);
                other_paint = &painter->tile_paints[other_idx];
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    assert(prio + 1 < PAINTER_WF_WAVE_CAP);
                    wf_paint2_push(painter, other_idx, prio + 1);
                }
            }
            if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
            {
                int other_idx = step_idx_north(painter, tile_idx);
                other_paint = &painter->tile_paints[other_idx];
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    assert(prio + 1 < PAINTER_WF_WAVE_CAP);
                    wf_paint2_push(painter, other_idx, prio + 1);
                }
            }
            if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
            {
                int other_idx = step_idx_south(painter, tile_idx);
                other_paint = &painter->tile_paints[other_idx];
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    assert(prio + 1 < PAINTER_WF_WAVE_CAP);
                    wf_paint2_push(painter, other_idx, prio + 1);
                }
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
                other_paint = tile_paint_at(painter, tile_sx, tile_sz, tile_slevel - 1);

                if( other_paint->step != PAINT_STEP_DONE )
                {
                    continue;
                }
            }

            if( tile_is_east_inbounds(tile_sx, camera_sx, max_draw_x) )
            {
                other_paint = &painter->tile_paints[step_idx_east(painter, tile_idx)];

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
                other_paint = &painter->tile_paints[step_idx_west(painter, tile_idx)];

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
                other_paint = &painter->tile_paints[step_idx_north(painter, tile_idx)];

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
                other_paint = &painter->tile_paints[step_idx_south(painter, tile_idx)];

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
                other_paint = &painter->tile_paints[other_idx];

                if( other_paint->step != PAINT_STEP_DONE &&
                    (tile->spans & SPAN_FLAG_EAST) != 0 )
                {
                    wf_paint2_push(painter, other_idx, prio);
                }
            }

            if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
            {
                int other_idx = step_idx_west(painter, tile_idx);
                other_paint = &painter->tile_paints[other_idx];

                if( other_paint->step != PAINT_STEP_DONE &&
                    (tile->spans & SPAN_FLAG_WEST) != 0 )
                {
                    wf_paint2_push(painter, other_idx, prio);
                }
            }

            if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
            {
                int other_idx = step_idx_north(painter, tile_idx);
                other_paint = &painter->tile_paints[other_idx];

                if( other_paint->step != PAINT_STEP_DONE &&
                    (tile->spans & SPAN_FLAG_NORTH) != 0 )
                {
                    wf_paint2_push(painter, other_idx, prio);
                }
            }

            if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
            {
                int other_idx = step_idx_south(painter, tile_idx);
                other_paint = &painter->tile_paints[other_idx];

                if( other_paint->step != PAINT_STEP_DONE &&
                    (tile->spans & SPAN_FLAG_SOUTH) != 0 )
                {
                    wf_paint2_push(painter, other_idx, prio);
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
                        other_paint =
                            tile_paint_at(painter, other_tile_x, other_tile_z, tile_slevel);

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
            s_scenery_sort_painter = painter;
            s_scenery_sort_camera_sx = camera_sx;
            s_scenery_sort_camera_sz = camera_sz;
            qsort(
                scenery_queue,
                (size_t)scenery_queue_length,
                sizeof(scenery_queue[0]),
                scenery_distance_compare);

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
                        other_paint = &painter->tile_paints[other_idx];

                        wf_paint2_push(painter, other_idx, prio);
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
                other_paint = &painter->tile_paints[other_idx];

                if( other_paint->step != PAINT_STEP_DONE )
                {
                    assert(prio + 1 < PAINTER_WF_WAVE_CAP);
                    wf_paint2_push(painter, other_idx, prio + 1);
                }
            }

            if( tile_inward_east_inbounds(tile_sx, camera_sx, max_draw_x) )
            {
                int other_idx = step_idx_east(painter, tile_idx);
                other_paint = &painter->tile_paints[other_idx];

                if( other_paint->step != PAINT_STEP_DONE )
                {
                    assert(prio + 1 < PAINTER_WF_WAVE_CAP);
                    wf_paint2_push(painter, other_idx, prio + 1);
                }
            }
            if( tile_inward_west_inbounds(tile_sx, camera_sx, min_draw_x) )
            {
                int other_idx = step_idx_west(painter, tile_idx);
                other_paint = &painter->tile_paints[other_idx];
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    assert(prio + 1 < PAINTER_WF_WAVE_CAP);
                    wf_paint2_push(painter, other_idx, prio + 1);
                }
            }

            if( tile_inward_north_inbounds(tile_sz, camera_sz, max_draw_z) )
            {
                int other_idx = step_idx_north(painter, tile_idx);
                other_paint = &painter->tile_paints[other_idx];
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    assert(prio + 1 < PAINTER_WF_WAVE_CAP);
                    wf_paint2_push(painter, other_idx, prio + 1);
                }
            }

            if( tile_inward_south_inbounds(tile_sz, camera_sz, min_draw_z) )
            {
                int other_idx = step_idx_south(painter, tile_idx);
                other_paint = &painter->tile_paints[other_idx];
                if( other_paint->step != PAINT_STEP_DONE )
                {
                    assert(prio + 1 < PAINTER_WF_WAVE_CAP);
                    wf_paint2_push(painter, other_idx, prio + 1);
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
    } // while( !wf_queue_is_empty(painter) )

    for( int wi = 0; wi < PAINTER_WF_WAVE_CAP; wi++ )
        assert(P2(painter)->wf_wave_q[wi].length == 0);

    return 0;
}

#endif
