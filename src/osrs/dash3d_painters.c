#include "dash3d_painters.h"

#include "dash3d.h"
#include "dash3d_intqueue.u.c"
#include "dash3d_painters_elements.h"
#include "scene.h"

#define TILE_SIZE 128

static inline int
near_wall_flags(int camera_tile_x, int camera_tile_z, int loc_x, int loc_z)
{
    int flags = 0;

    int camera_is_north = loc_z < camera_tile_z;
    int camera_is_east = loc_x < camera_tile_x;

    if( camera_is_north )
        flags |= WALL_SIDE_NORTH | WALL_CORNER_NORTHWEST | WALL_CORNER_NORTHEAST;

    if( !camera_is_north )
        flags |= WALL_SIDE_SOUTH | WALL_CORNER_SOUTHEAST | WALL_CORNER_SOUTHWEST;

    if( camera_is_east )
        flags |= WALL_SIDE_EAST | WALL_CORNER_NORTHEAST | WALL_CORNER_SOUTHEAST;

    if( !camera_is_east )
        flags |= WALL_SIDE_WEST | WALL_CORNER_NORTHWEST | WALL_CORNER_SOUTHWEST;

    return flags;
}

/**
 * Painters algorithm
 *
 * 1. Draw Bridge Underlay (the water, not the surface)
 * 2. Draw Bridge Wall (the arches holding the bridge up)
 * 3. Draw bridge locs
 * 4. Draw tile underlay
 * 5. Draw far wall
 * 6. Draw far wall decor (i.e. facing the camera)
 * 7. Draw ground decor
 * 8. Draw ground items on ground
 * 9. Draw locs
 * 10. Draw ground items on table
 * 11. Draw near decor (i.e. facing away from the camera on the near wall)
 * 12. Draw the near wall.
 */
static int
scene_painters_algorithm(
    struct Scene* scene,
    struct IntQueue* queue,
    struct IntQueue* catchup_queue,
    struct PaintersElement* elements,
    int camera_x,
    int camera_y,
    int camera_z,
    struct Dash3DPainterOp* __restrict__ __attribute__((aligned(16))) ops,
    int op_capacity)
{
    struct GridTile* grid_tile = NULL;
    struct GridTile* bridge_underpass_tile = NULL;
    int loc_buffer_length = 0;
    int loc_buffer[64];
    memset(loc_buffer, 0, sizeof(loc_buffer));

    struct Loc* loc = NULL;

    for( int i = 0; i < scene->locs_length; i++ )
    {
        scene->locs[i].__drawn = false;
    }

    int op_count = 0;

    // int op_capacity = scene->grid_tiles_length * 11;
    // struct Dash3DPainterOp* ops = (struct Dash3DPainterOp*)malloc(op_capacity * sizeof(struct
    // Dash3DPainterOp)); memset(ops, 0, op_capacity * sizeof(struct Dash3DPainterOp)); struct
    // SceneElement* elements =
    //     (struct SceneElement*)malloc(scene->grid_tiles_length * sizeof(struct SceneElement));

    for( int i = 0; i < scene->grid_tiles_length; i++ )
    {
        struct PaintersElement* element = &elements[i];
        element->step = E_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED;
        element->remaining_locs = scene->grid_tiles[i].locs_length;
        element->near_wall_flags = 0;
        element->generation = 0;
        element->q_count = 0;
    }

    // Generate painter's algorithm coordinate list - farthest to nearest
    int radius = 25;
    int coord_list_x[4];
    int coord_list_z[4];
    int max_level = 0;

    int coord_list_length = 0;

    int camera_tile_x = camera_x / TILE_SIZE;
    int camera_tile_z = camera_z / TILE_SIZE;
    int camera_tile_y = camera_y / 240;

    int max_draw_x = camera_tile_x + radius;
    int max_draw_z = camera_tile_z + radius;
    if( max_draw_x >= MAP_TERRAIN_X )
        max_draw_x = MAP_TERRAIN_X;
    if( max_draw_z >= MAP_TERRAIN_Y )
        max_draw_z = MAP_TERRAIN_Y;
    if( max_draw_x < 0 )
        max_draw_x = 0;
    if( max_draw_z < 0 )
        max_draw_z = 0;

    int min_draw_x = camera_tile_x - radius;
    int min_draw_z = camera_tile_z - radius;
    if( min_draw_x < 0 )
        min_draw_x = 0;
    if( min_draw_z < 0 )
        min_draw_z = 0;
    if( min_draw_x > MAP_TERRAIN_X )
        min_draw_x = MAP_TERRAIN_X;
    if( min_draw_z > MAP_TERRAIN_Y )
        min_draw_z = MAP_TERRAIN_Y;

    if( min_draw_x >= max_draw_x )
        return 0;
    if( min_draw_z >= max_draw_z )
        return 0;

    struct PaintersElement* element = NULL;
    struct PaintersElement* other = NULL;

    coord_list_length = 4;
    coord_list_x[0] = min_draw_x;
    coord_list_z[0] = min_draw_z;

    coord_list_x[1] = min_draw_x;
    coord_list_z[1] = max_draw_z - 1;

    coord_list_x[2] = max_draw_x - 1;
    coord_list_z[2] = min_draw_z;

    coord_list_x[3] = max_draw_x - 1;
    coord_list_z[3] = max_draw_z - 1;

    // Render tiles in painter's algorithm order (farthest to nearest)
    for( int i = 0; i < coord_list_length; i++ )
    {
        int _coord_x = coord_list_x[i];
        int _coord_z = coord_list_z[i];

        assert(_coord_x >= min_draw_x);
        assert(_coord_x < max_draw_x);
        assert(_coord_z >= min_draw_z);
        assert(_coord_z < max_draw_z);

        int _coord_idx = MAP_TILE_COORD(_coord_x, _coord_z, 0);
        int_queue_push_wrap(&queue, _coord_idx);

        int gen = 0;

        while( queue->length > 0 )
        {
            int val;

            if( catchup_queue->length > 0 )
            {
                val = int_queue_pop(&catchup_queue);
            }
            else
            {
                val = int_queue_pop(&queue);
            }

            int tile_coord = val >> 8;
            int prio = val & 0xFF;

            grid_tile = &scene->grid_tiles[tile_coord];

            int tile_x = grid_tile->x;
            int tile_z = grid_tile->z;
            int tile_level = grid_tile->level;

            element = &elements[tile_coord];
            element->generation = gen++;
            element->q_count--;

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
            if( element->q_count > 0 )
                continue;

            if( (grid_tile->flags & GRID_TILE_FLAG_BRIDGE) != 0 || tile_level > max_level )
            {
                element = &elements[tile_coord];
                element->step = E_STEP_DONE;
                continue;
            }

            // printf("Tile %d, %d Coord %d\n", tile_x, tile_z, tile_coord);
            // printf("Min %d, %d\n", min_draw_x, min_draw_z);
            // printf("Max %d, %d\n", max_draw_x, max_draw_z);

            assert(tile_x >= min_draw_x);
            assert(tile_x < max_draw_x);
            assert(tile_z >= min_draw_z);
            assert(tile_z < max_draw_z);

            element = &elements[tile_coord];

            /**
             * If this is a loc revisit, then we need to verify adjacent tiles are done.
             *
             */

            if( element->step == E_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED )
            {
                if( tile_level > 0 )
                {
                    other = &elements[MAP_TILE_COORD(tile_x, tile_z, tile_level - 1)];

                    if( other->step != E_STEP_DONE )
                    {
                        goto done;
                    }
                }

                if( tile_x >= camera_tile_x && tile_x < max_draw_x )
                {
                    if( tile_x + 1 < max_draw_x )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x + 1, tile_z, tile_level)];

                        // If we are not spanned by the tile, then we need to verify it is done.
                        if( other->step != E_STEP_DONE )
                        {
                            if( (grid_tile->spans & SPAN_FLAG_EAST) == 0 ||
                                other->step != E_STEP_WAIT_ADJACENT_GROUND )
                            {
                                goto done;
                            }
                        }
                    }
                }

                if( tile_x <= camera_tile_x && tile_x > min_draw_x )
                {
                    if( tile_x - 1 >= min_draw_x )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x - 1, tile_z, tile_level)];
                        if( other->step != E_STEP_DONE )
                        {
                            if( (grid_tile->spans & SPAN_FLAG_WEST) == 0 ||
                                other->step != E_STEP_WAIT_ADJACENT_GROUND )
                            {
                                goto done;
                            }
                        }
                    }
                }

                if( tile_z >= camera_tile_z && tile_z < max_draw_z )
                {
                    if( tile_z + 1 < max_draw_z )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x, tile_z + 1, tile_level)];
                        if( other->step != E_STEP_DONE )
                        {
                            if( (grid_tile->spans & SPAN_FLAG_NORTH) == 0 ||
                                other->step != E_STEP_WAIT_ADJACENT_GROUND )
                            {
                                goto done;
                            }
                        }
                    }
                }
                if( tile_z <= camera_tile_z && tile_z > min_draw_z )
                {
                    if( tile_z - 1 >= min_draw_z )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x, tile_z - 1, tile_level)];
                        if( other->step != E_STEP_DONE )
                        {
                            if( (grid_tile->spans & SPAN_FLAG_SOUTH) == 0 ||
                                other->step != E_STEP_WAIT_ADJACENT_GROUND )
                            {
                                goto done;
                            }
                        }
                    }
                }

                element->step = E_STEP_GROUND;
            }

            if( element->step == E_STEP_GROUND )
            {
                element->step = E_STEP_WAIT_ADJACENT_GROUND;

                int near_walls = near_wall_flags(camera_tile_x, camera_tile_z, tile_x, tile_z);
                int far_walls = ~near_walls;
                element->near_wall_flags |= near_walls;

                if( grid_tile->bridge_tile != -1 )
                {
                    bridge_underpass_tile = &scene->grid_tiles[grid_tile->bridge_tile];
                    ops[op_count++] = (struct Dash3DPainterOp){
                        .op = DASH3D_PAINTERS_DRAW_GROUND,
                        .x = tile_x,
                        .z = tile_z,

                        .level = bridge_underpass_tile->level,
                    };

                    if( bridge_underpass_tile->wall != -1 )
                    {
                        loc = &scene->locs[bridge_underpass_tile->wall];
                        ops[op_count++] = (struct Dash3DPainterOp){
                            .op = DASH3D_PAINTERS_DRAW_WALL,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall = { .loc_index = bridge_underpass_tile->wall,
                                      .is_wall_a = true },
                        };
                    }

                    for( int i = 0; i < bridge_underpass_tile->locs_length; i++ )
                    {
                        int loc_index = bridge_underpass_tile->locs[i];
                        loc = &scene->locs[loc_index];
                        ops[op_count++] = (struct Dash3DPainterOp){
                            .op = DASH3D_PAINTERS_DRAW_LOC,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._ground_decor = { .loc_index = loc_index },
                        };
                    }
                }

                ops[op_count++] = (struct Dash3DPainterOp){
                    .op = DASH3D_PAINTERS_DRAW_GROUND,
                    .x = tile_x,
                    .z = tile_z,
                    .level = tile_level,
                };

                if( grid_tile->wall != -1 )
                {
                    loc = &scene->locs[grid_tile->wall];

                    if( (loc->_wall.side_a & far_walls) != 0 )
                        ops[op_count++] = (struct Dash3DPainterOp){
                            .op = DASH3D_PAINTERS_DRAW_WALL,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall = { .loc_index = grid_tile->wall, .is_wall_a = true },
                        };
                    if( (loc->_wall.side_b & far_walls) != 0 )
                        ops[op_count++] = (struct Dash3DPainterOp){
                            .op = DASH3D_PAINTERS_DRAW_WALL,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall = { .loc_index = grid_tile->wall, .is_wall_a = false },
                        };
                }

                if( grid_tile->ground_decor != -1 )
                {
                    loc = &scene->locs[grid_tile->ground_decor];

                    ops[op_count++] = (struct Dash3DPainterOp){
                        .op = DASH3D_PAINTERS_DRAW_GROUND_DECOR,
                        .x = loc->chunk_pos_x,
                        .z = loc->chunk_pos_y,
                        .level = loc->chunk_pos_level,
                        ._ground_decor = { .loc_index = grid_tile->ground_decor },
                    };
                }

                if( grid_tile->ground_object_bottom != -1 )
                {
                    loc = &scene->locs[grid_tile->ground_object_bottom];
                    ops[op_count++] = (struct Dash3DPainterOp){
                        .op = DASH3D_PAINTERS_DRAW_GROUND_OBJECT,
                        .x = loc->chunk_pos_x,
                        .z = loc->chunk_pos_y,
                        .level = loc->chunk_pos_level,
                        ._ground_object = { .num = 0 },
                    };
                }

                if( grid_tile->wall_decor != -1 )
                {
                    loc = &scene->locs[grid_tile->wall_decor];
                    if( loc->_wall_decor.through_wall_flags != 0 )
                    {
                        int x_diff = loc->chunk_pos_x - camera_tile_x;
                        int y_diff = loc->chunk_pos_y - camera_tile_z;

                        // TODO: Document what this is doing.
                        int x_near = x_diff;
                        if( loc->_wall_decor.side == WALL_CORNER_NORTHEAST ||
                            loc->_wall_decor.side == WALL_CORNER_SOUTHEAST )
                            x_near = -x_diff;

                        int y_near = y_diff;
                        if( loc->_wall_decor.side == WALL_CORNER_SOUTHEAST ||
                            loc->_wall_decor.side == WALL_CORNER_SOUTHWEST )
                            y_near = -y_diff;

                        if( y_near < x_near )
                        {
                            // Draw model a
                            ops[op_count++] = (struct Dash3DPainterOp){
                                .op = DASH3D_PAINTERS_DRAW_WALL_DECOR,
                                .x = loc->chunk_pos_x,
                                .z = loc->chunk_pos_y,
                                .level = loc->chunk_pos_level,
                                ._wall_decor = { .loc_index = grid_tile->wall_decor,
                                                .is_wall_a = true,
                                                .__rotation = loc->_wall_decor.side,    
                                               },
                            };
                        }

                        else if( loc->_wall_decor.model_b != -1 )
                        {
                            // Draw model b
                            ops[op_count++] = (struct Dash3DPainterOp){
                                .op = DASH3D_PAINTERS_DRAW_WALL_DECOR,
                                .x = loc->chunk_pos_x,
                                .z = loc->chunk_pos_y,
                                .level = loc->chunk_pos_level,
                                ._wall_decor = { .loc_index = grid_tile->wall_decor,
                                                .is_wall_a = false,
                                                .__rotation = loc->_wall_decor.side,
                                           },
                            };
                        }
                    }
                    else if( (loc->_wall_decor.side & far_walls) != 0 )
                    {
                        ops[op_count++] = (struct Dash3DPainterOp){
                            .op = DASH3D_PAINTERS_DRAW_WALL_DECOR,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall_decor = { .loc_index = grid_tile->wall_decor },
                        };
                    }
                }

                if( tile_x < camera_tile_x )
                {
                    if( tile_x + 1 < max_draw_x )
                    {
                        int idx = MAP_TILE_COORD(tile_x + 1, tile_z, tile_level);
                        other = &elements[idx];

                        if( other->step != E_STEP_DONE && (grid_tile->spans & SPAN_FLAG_EAST) != 0 )
                        {
                            other->q_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }
                if( tile_x > camera_tile_x )
                {
                    if( tile_x - 1 >= min_draw_x )
                    {
                        int idx = MAP_TILE_COORD(tile_x - 1, tile_z, tile_level);
                        other = &elements[idx];

                        if( other->step != E_STEP_DONE && (grid_tile->spans & SPAN_FLAG_WEST) != 0 )
                        {
                            other->q_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }

                if( tile_z < camera_tile_z )
                {
                    if( tile_z + 1 < max_draw_z )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_z + 1, tile_level);
                        other = &elements[idx];

                        if( other->step != E_STEP_DONE &&
                            (grid_tile->spans & SPAN_FLAG_NORTH) != 0 )
                        {
                            other->q_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }

                if( tile_z > camera_tile_z )
                {
                    if( tile_z - 1 >= min_draw_z )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_z - 1, tile_level);
                        other = &elements[idx];

                        if( other->step != E_STEP_DONE &&
                            (grid_tile->spans & SPAN_FLAG_SOUTH) != 0 )
                        {
                            other->q_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }
            }

            loc_buffer_length = 0;
            bool contains_locs = false;
            memset(loc_buffer, 0, sizeof(loc_buffer));
            if( element->step == E_STEP_WAIT_ADJACENT_GROUND )
            {
                element->step = E_STEP_LOCS;

                // Check if all locs are drawable.
                for( int j = 0; j < grid_tile->locs_length; j++ )
                {
                    int loc_index = grid_tile->locs[j];

                    loc = &scene->locs[loc_index];
                    if( scene->locs[loc_index].__drawn )
                        continue;

                    int min_tile_x = loc->chunk_pos_x;
                    int min_tile_z = loc->chunk_pos_y;
                    int max_tile_x = min_tile_x + loc->size_x - 1;
                    int max_tile_z = min_tile_z + loc->size_y - 1;

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
                        for( int other_tile_z = min_tile_z; other_tile_z <= max_tile_z;
                             other_tile_z++ )
                        {
                            other =
                                &elements[MAP_TILE_COORD(other_tile_x, other_tile_z, tile_level)];
                            if( other->step <= E_STEP_GROUND )
                            {
                                contains_locs = true;
                                goto step_locs;
                            }
                        }
                    }

                    loc_buffer[loc_buffer_length++] = loc_index;

                step_locs:;
                }
            }

            if( element->step == E_STEP_LOCS )
            {
                // TODO: Draw farthest first.
                for( int j = 0; j < loc_buffer_length; j++ )
                {
                    int loc_index = loc_buffer[j];
                    if( scene->locs[loc_index].__drawn )
                        continue;
                    scene->locs[loc_index].__drawn = true;
                    loc = &scene->locs[loc_index];

                    ops[op_count++] = (struct Dash3DPainterOp){
                        .op = DASH3D_PAINTERS_DRAW_LOC,
                        .x = loc->chunk_pos_x,
                        .z = loc->chunk_pos_y,
                        .level = loc->chunk_pos_level,
                        ._loc = { .loc_index = loc_index },
                    };

                    int min_tile_x = loc->chunk_pos_x;
                    int min_tile_z = loc->chunk_pos_y;
                    int max_tile_x = min_tile_x + loc->size_x - 1;
                    int max_tile_z = min_tile_z + loc->size_y - 1;

                    int next_prio = 0;
                    if( loc->size_x > 1 || loc->size_y > 1 )
                    {
                        next_prio = loc->size_x > loc->size_y ? loc->size_x : loc->size_y;
                    }

                    if( max_tile_x > max_draw_x - 1 )
                        max_tile_x = max_draw_x - 1;
                    if( max_tile_z > max_draw_z - 1 )
                        max_tile_z = max_draw_z - 1;
                    if( min_tile_x < min_draw_x )
                        min_tile_x = min_draw_x;
                    if( min_tile_z < min_draw_z )
                        min_tile_z = min_draw_z;

                    int step_x = tile_x <= camera_tile_x ? 1 : -1;
                    int step_y = tile_z <= camera_tile_z ? 1 : -1;

                    int start_x = min_tile_x;
                    int start_y = min_tile_z;
                    int end_x = max_tile_x;
                    int end_y = max_tile_z;

                    if( step_x < 0 )
                    {
                        int tmp = start_x;
                        start_x = end_x;
                        end_x = tmp;
                    }

                    if( step_y < 0 )
                    {
                        int tmp = start_y;
                        start_y = end_y;
                        end_y = tmp;
                    }

                    for( int other_tile_x = start_x; other_tile_x != end_x + step_x;
                         other_tile_x += step_x )
                    {
                        for( int other_tile_z = start_y; other_tile_z != end_y + step_y;
                             other_tile_z += step_y )
                        {
                            int idx = MAP_TILE_COORD(other_tile_x, other_tile_z, tile_level);
                            other = &elements[idx];
                            if( other_tile_x != tile_x || other_tile_z != tile_z )
                            {
                                other->q_count++;
                                if( next_prio == 0 )
                                    int_queue_push_wrap(&queue, idx);
                                else
                                    int_queue_push_wrap_prio(&catchup_queue, idx, next_prio - 1);
                            }
                        }
                    }
                }

                if( !contains_locs )
                    element->step = E_STEP_NOTIFY_ADJACENT_TILES;
                else
                {
                    element->step = E_STEP_WAIT_ADJACENT_GROUND;
                }
            }

            // Move towards camera if farther away tiles are done.
            if( element->step == E_STEP_NOTIFY_ADJACENT_TILES )
            {
                if( tile_level < MAP_TERRAIN_Z - 1 )
                {
                    int idx = MAP_TILE_COORD(tile_x, tile_z, tile_level + 1);
                    other = &elements[idx];

                    if( other->step != E_STEP_DONE )
                    {
                        other->q_count++;
                        if( prio == 0 )
                            int_queue_push_wrap(&queue, idx);
                        else
                            int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                    }
                }

                if( tile_x < camera_tile_x )
                {
                    if( tile_x + 1 < max_draw_x )
                    {
                        int idx = MAP_TILE_COORD(tile_x + 1, tile_z, tile_level);
                        other = &elements[idx];

                        if( other->step != E_STEP_DONE )
                        {
                            other->q_count++;
                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }
                if( tile_x > camera_tile_x )
                {
                    if( tile_x - 1 >= min_draw_x )
                    {
                        int idx = MAP_TILE_COORD(tile_x - 1, tile_z, tile_level);
                        other = &elements[idx];
                        if( other->step != E_STEP_DONE )
                        {
                            other->q_count++;
                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }

                if( tile_z < camera_tile_z )
                {
                    if( tile_z + 1 < max_draw_z )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_z + 1, tile_level);
                        other = &elements[idx];
                        if( other->step != E_STEP_DONE )
                        {
                            other->q_count++;
                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }

                if( tile_z > camera_tile_z )
                {
                    if( tile_z - 1 >= min_draw_z )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_z - 1, tile_level);
                        other = &elements[idx];
                        if( other->step != E_STEP_DONE )
                        {
                            other->q_count++;
                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }

                element->step = E_STEP_NEAR_WALL;
            }

            if( element->step == E_STEP_NEAR_WALL )
            {
                if( grid_tile->wall_decor != -1 )
                {
                    loc = &scene->locs[grid_tile->wall_decor];

                    if( loc->_wall_decor.through_wall_flags != 0 )
                    {
                        int x_diff = loc->chunk_pos_x - camera_tile_x;
                        int y_diff = loc->chunk_pos_y - camera_tile_z;

                        // TODO: Document what this is doing.
                        int x_near = x_diff;
                        if( loc->_wall_decor.side == WALL_CORNER_NORTHEAST ||
                            loc->_wall_decor.side == WALL_CORNER_SOUTHEAST )
                            x_near = -x_diff;

                        int y_near = y_diff;
                        if( loc->_wall_decor.side == WALL_CORNER_SOUTHEAST ||
                            loc->_wall_decor.side == WALL_CORNER_SOUTHWEST )
                            y_near = -y_diff;

                        // The deobs and official clients calculate the nearest quadrant.
                        // Notice a line goes from SW to NE with y = x.
                        if( y_near >= x_near )
                        {
                            // Draw model a
                            ops[op_count++] = (struct Dash3DPainterOp){
                                .op = DASH3D_PAINTERS_DRAW_WALL_DECOR,
                                .x = loc->chunk_pos_x,
                                .z = loc->chunk_pos_y,
                                .level = loc->chunk_pos_level,
                                ._wall_decor = { .loc_index = grid_tile->wall_decor,
                                                .is_wall_a = true,
                                                .__rotation = loc->_wall_decor.side,
                                          },
                            };
                        }
                        else if( loc->_wall_decor.model_b != -1 )
                        {
                            // Draw model b
                            ops[op_count++] = (struct Dash3DPainterOp){
                                .op = DASH3D_PAINTERS_DRAW_WALL_DECOR,
                                .x = loc->chunk_pos_x,
                                .z = loc->chunk_pos_y,
                                .level = loc->chunk_pos_level,
                                ._wall_decor = { .loc_index = grid_tile->wall_decor,
                                                .is_wall_a = false,
                                                .__rotation = loc->_wall_decor.side,
                                          },
                            };
                        }
                    }
                    else if( (loc->_wall_decor.side & element->near_wall_flags) != 0 )
                    {
                        ops[op_count++] = (struct Dash3DPainterOp){
                            .op = DASH3D_PAINTERS_DRAW_WALL_DECOR,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall_decor = { .loc_index = grid_tile->wall_decor },
                        };
                    }
                }

                if( grid_tile->wall != -1 )
                {
                    int near_walls = element->near_wall_flags;

                    loc = &scene->locs[grid_tile->wall];

                    if( (loc->_wall.side_a & near_walls) != 0 )
                        ops[op_count++] = (struct Dash3DPainterOp){
                            .op = DASH3D_PAINTERS_DRAW_WALL,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall = { .loc_index = grid_tile->wall, .is_wall_a = true },
                        };
                    if( (loc->_wall.side_b & near_walls) != 0 )
                        ops[op_count++] = (struct Dash3DPainterOp){
                            .op = DASH3D_PAINTERS_DRAW_WALL,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall = { .loc_index = grid_tile->wall, .is_wall_a = false },
                        };
                }

                element->step = E_STEP_DONE;
            }

            if( element->step == E_STEP_DONE )
            {
            }

        done:;
        }
    }

    return op_count;
}
