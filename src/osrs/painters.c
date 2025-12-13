#include "painters.h"

#include "tables/maps.h"

#include <stdlib.h>
#include <string.h>

// clang-format off
#include "painters_intqueue.u.c"
// clang-format on

static inline void
init_painter_tile(struct PaintersTile* tile, int wx, int wz, int wlevel)
{
    tile->wx = wx;
    tile->wz = wz;
    tile->wlevel = wlevel;
    tile->spans = 0;
    tile->wall_a = -1;
    tile->wall_b = -1;
    tile->wall_decor_a = -1;
    tile->wall_decor_b = -1;
    tile->bridge_tile = -1;
    tile->ground_object_bottom = -1;
    tile->ground_object_middle = -1;
    tile->ground_object_top = -1;
    tile->flags = 0;
}

enum TilePaintStep
{
    // Do not draw ground until adjacent tiles are done,
    // unless we are spanned by that tile.
    PAINT_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED,
    PAINT_STEP_GROUND,
    PAINT_STEP_WAIT_ADJACENT_GROUND,
    PAINT_STEP_LOCS,
    PAINT_STEP_NOTIFY_ADJACENT_TILES,
    PAINT_STEP_NEAR_WALL,
    PAINT_STEP_DONE,
};

struct TilePaint
{
    uint8_t step;
    uint8_t queue_count;
    uint8_t near_wall_flags;
};

struct Painter
{
    int width;
    int height;
    int levels;

    struct PaintersTile* tiles;
    struct TilePaint* tile_paints;
    int tile_count;
    int tile_capacity;

    struct PaintersElement* elements;
    int element_count;
    int element_capacity;

    struct IntQueue queue;
    struct IntQueue catchup_queue;
};

static inline int
painter_push_element(struct Painter* painter)
{
    int element = painter->element_count++;
    if( element < painter->element_capacity )
    {
        painter->element_capacity *= 2;
        painter->elements =
            realloc(painter->elements, painter->element_capacity * sizeof(struct PaintersElement));
    }
    memset(&painter->elements[element], 0, sizeof(struct PaintersElement));
    return element;
}

static inline int
painter_coord_idx(struct Painter* painter, int wx, int wz, int wlevel)
{
    assert(wx >= 0);
    assert(wz >= 0);
    assert(wlevel >= 0);
    assert(wx < painter->width);
    assert(wz < painter->height);
    assert(wlevel < painter->levels);

    return MAP_TILE_COORD(wx, wz, wlevel);
}

struct Painter*
painter_new(int width, int height, int levels)
{
    struct Painter* painter = malloc(sizeof(struct Painter));
    memset(painter, 0, sizeof(struct Painter));

    painter->width = width;
    painter->height = height;
    painter->levels = levels;

    int tile_count = width * height * levels;

    painter->tiles = malloc(tile_count * sizeof(struct PaintersTile));
    memset(painter->tiles, 0, tile_count * sizeof(struct PaintersTile));
    painter->tile_count = 0;
    painter->tile_capacity = tile_count;

    for( int wx = 0; wx < width; wx++ )
    {
        for( int wz = 0; wz < height; wz++ )
            for( int wlevel = 0; wlevel < levels; wlevel++ )
                init_painter_tile(painter_tile_at(painter, wx, wz, wlevel), wx, wz, wlevel);
    }

    painter->tile_paints = malloc(tile_count * sizeof(struct TilePaint));
    memset(painter->tile_paints, 0, tile_count * sizeof(struct TilePaint));

    painter->elements = malloc(128 * sizeof(struct PaintersElement));
    painter->element_count = 0;
    painter->element_capacity = 128;

    int_queue_init(&painter->queue, 512);
    int_queue_init(&painter->catchup_queue, 512);

    return painter;
}

void
painter_free(struct Painter* painter)
{
    free(painter->tiles);
    free(painter);
}

struct PaintersTile*
painter_tile_at(struct Painter* painter, int wx, int wz, int wlevel)
{
    int coord = MAP_TILE_COORD(wx, wz, wlevel);
    assert(coord < painter->tile_capacity);
    return &painter->tiles[coord];
}

struct PaintersElement*
painter_element_at(
    struct Painter* painter, //
    int element)
{
    assert(element < painter->element_count);
    return &painter->elements[element];
}

void
painter_add_normal_scenery(
    struct Painter* painter, int wx, int wz, int wlevel, int entity, int size_x, int size_y)
{
    struct PaintersTile* tile = painter_tile_at(painter, wx, wz, wlevel);
    int element = painter_push_element(painter);

    assert(tile->scenery_count < 10);
    tile->scenery[tile->scenery_count++] = element;

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_SCENERY,
        .wx = wx,
        .wz = wz,
        .wlevel = wlevel,
        ._scenery = { .entity = entity, .size_x = size_x, .size_y = size_y },
    };
}

void
painter_add_wall(
    struct Painter* painter, int wx, int wz, int wlevel, int entity, int wall_ab, int side)
{
    struct PaintersTile* tile = painter_tile_at(painter, wx, wz, wlevel);
    int element = painter_push_element(painter);

    assert(tile->wall_a == -1);
    assert(tile->wall_b == -1);

    switch( wall_ab )
    {
    case WALL_A:
        assert(tile->wall_a == -1);
        tile->wall_a = element;
        break;
    case WALL_B:
        assert(tile->wall_b == -1);
        tile->wall_b = element;
        break;
    default:
        assert(false);
    }

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_WALL_A,
        .wx = wx,
        .wz = wz,
        .wlevel = wlevel,
        ._wall = { .entity = entity, .side = side },
    };
}

void
painter_add_wall_decor(
    struct Painter* painter,
    int wx,
    int wz,
    int wlevel,
    int entity,
    int wall_ab,
    int side,
    int through_wall_flags)
{
    struct PaintersTile* tile = painter_tile_at(painter, wx, wz, wlevel);
    int element = painter_push_element(painter);

    assert(tile->wall_decor_a == -1);
    assert(tile->wall_decor_b == -1);

    switch( wall_ab )
    {
    case WALL_A:
        tile->wall_decor_a = element;
        break;
    case WALL_B:
        tile->wall_decor_b = element;
        break;
    default:
        assert(false);
    }

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_WALL_DECOR,
        .wx = wx,
        .wz = wz,
        .wlevel = wlevel,
        ._wall_decor = { //
            .entity = entity, //
            ._bf_side = side,
            ._bf_through_wall_flags = through_wall_flags,
        },
    };
}

void
painter_add_ground_decor(struct Painter* painter, int wx, int wz, int wlevel, int entity)
{
    struct PaintersTile* tile = painter_tile_at(painter, wx, wz, wlevel);
    int element = painter_push_element(painter);

    assert(tile->ground_decor == -1);
    tile->ground_decor = element;

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_GROUND_DECOR,
        .wx = wx,
        .wz = wz,
        .wlevel = wlevel,
        ._ground_decor = { .entity = entity },
    };
}

void
painter_add_ground_object(
    struct Painter* painter, int wx, int wz, int wlevel, int entity, int bottom_middle_top)
{
    struct PaintersTile* tile = painter_tile_at(painter, wx, wz, wlevel);
    int element = painter_push_element(painter);

    switch( bottom_middle_top )
    {
    case GROUND_OBJECT_BOTTOM:
        assert(tile->ground_object_bottom == -1);
        tile->ground_object_bottom = element;
        break;
    case GROUND_OBJECT_MIDDLE:
        assert(tile->ground_object_middle == -1);
        tile->ground_object_middle = element;
        break;
    case GROUND_OBJECT_TOP:
        assert(tile->ground_object_top == -1);
        tile->ground_object_top = element;
        break;
    default:
        assert(false);
    }

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_GROUND_OBJECT,
        .wx = wx,
        .wz = wz,
        .wlevel = wlevel,
        ._ground_object = { .entity = entity },
    };
}

/**
 * ****
 * The Painters Algorithm
 * ****
 */

static inline void
ensure_command_capacity(struct PaintersBuffer* buffer, int count)
{
    if( buffer->command_count + count > buffer->command_capacity )
    {
        buffer->command_capacity *= 2;
        buffer->commands = realloc(
            buffer->commands, buffer->command_capacity * sizeof(struct PaintersElementCommand));
    }
}

static inline void
push_command_entity(struct PaintersBuffer* buffer, int entity)
{
    ensure_command_capacity(buffer, 1);
    buffer->commands[buffer->command_count++] = (struct PaintersElementCommand){
        ._entity = {
            ._bf_kind = PNTR_CMD_ELEMENT,
            ._bf_entity = entity,
        },
    };
}

static inline void
push_command_terrain(struct PaintersBuffer* buffer, int wx, int wz, int wlevel)
{
    ensure_command_capacity(buffer, 1);
    buffer->commands[buffer->command_count++] = (struct PaintersElementCommand){
        ._terrain = {
            ._bf_kind = PNTR_CMD_TERRAIN,
            ._bf_terrain_x = wx,
            ._bf_terrain_y = wz,
            ._bf_terrain_z = wlevel,
        },
    };
}

static inline int
near_wall_flags(int camera_wx, int camera_wz, int wx, int wz)
{
    int flags = 0;

    int camera_is_north = wz < camera_wz;
    int camera_is_east = wx < camera_wx;

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

int
painter_paint(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_wx,
    int camera_wz,
    int camera_wlevel)
{
    struct PaintersTile* tile = NULL;
    struct PaintersElement* element = NULL;
    struct TilePaint* tile_paint = NULL;
    struct TilePaint* other_paint = NULL;
    struct PaintersTile* bridge_underpass_tile = NULL;

    memset(painter->tile_paints, 0, painter->tile_count * sizeof(struct TilePaint));

    // Generate painter's algorithm coordinate list - farthest to nearest
    int radius = 25;
    int coord_list_x[4];
    int coord_list_z[4];
    int max_level = 0;

    int coord_list_length = 0;

    int max_draw_x = camera_wx + radius;
    int max_draw_z = camera_wz + radius;
    if( max_draw_x >= painter->width )
        max_draw_x = painter->width;
    if( max_draw_z >= painter->height )
        max_draw_z = painter->height;
    if( max_draw_x < 0 )
        max_draw_x = 0;
    if( max_draw_z < 0 )
        max_draw_z = 0;

    int min_draw_x = camera_wx - radius;
    int min_draw_z = camera_wz - radius;
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
    // Starting at the corners
    for( int i = 0; i < coord_list_length; i++ )
    {
        int coord_wx = coord_list_x[i];
        int coord_wz = coord_list_z[i];

        assert(coord_wx >= min_draw_x);
        assert(coord_wx < max_draw_x);
        assert(coord_wz >= min_draw_z);
        assert(coord_wz < max_draw_z);

        int coord_idx = painter_coord_idx(painter, coord_wx, coord_wz, 0);
        int_queue_push_wrap(&painter->queue, coord_idx);

        tile_paint = &painter->tile_paints[coord_idx];
        tile_paint->queue_count += 1;

        while( painter->queue.length > 0 )
        {
            int val;
            if( painter->catchup_queue.length > 0 )
                val = int_queue_pop(&painter->catchup_queue);
            else
                val = int_queue_pop(&painter->queue);

            int tile_idx = val >> 8;
            int prio = val & 0xFF;

            tile = &painter->tiles[tile_idx];

            int tile_wx = tile->wx;
            int tile_wz = tile->wz;
            int tile_wlevel = tile->wlevel;

            tile_paint = &painter->tile_paints[tile_idx];
            tile_paint->queue_count -= 1;

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

            if( (tile->flags & PAINTERS_TILE_FLAG_BRIDGE) != 0 || tile_wlevel > max_level )
            {
                tile_paint->step = PAINT_STEP_DONE;
                continue;
            }

            assert(tile_wx >= min_draw_x);
            assert(tile_wx < max_draw_x);
            assert(tile_wz >= min_draw_z);
            assert(tile_wz < max_draw_z);

            /**
             * If this is a loc revisit, then we need to verify adjacent tiles are done.
             */

            /**
             * If this is a loc revisit, then we need to verify adjacent tiles are done.
             */

            if( tile_paint->step == PAINT_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED )
            {
                if( tile_wlevel > 0 )
                {
                    other_paint =
                        &painter
                             ->tiles[painter_coord_idx(painter, tile_wx, tile_wz, tile_wlevel - 1)];

                    if( other_paint->step != PAINT_STEP_DONE )
                    {
                        continue;
                    }
                }

                if( tile_wx >= camera_wx && tile_wx < max_draw_x )
                {
                    if( tile_wx + 1 < max_draw_x )
                    {
                        other_paint = &painter->tiles[painter_coord_idx(
                            painter, tile_wx + 1, tile_wz, tile_wlevel)];

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
                }

                if( tile_wx <= camera_wx && tile_wx > min_draw_x )
                {
                    if( tile_wx - 1 >= min_draw_x )
                    {
                        other_paint = &painter->tiles[painter_coord_idx(
                            painter, tile_wx - 1, tile_wz, tile_wlevel)];
                        if( other_paint->step != PAINT_STEP_DONE )
                        {
                            if( (tile->spans & SPAN_FLAG_WEST) == 0 ||
                                other_paint->step != PAINT_STEP_WAIT_ADJACENT_GROUND )
                            {
                                goto done;
                            }
                        }
                    }
                }

                if( tile_wz >= camera_wz && tile_wz < max_draw_z )
                {
                    if( tile_wz + 1 < max_draw_z )
                    {
                        other_paint = &painter->tiles[painter_coord_idx(
                            painter, tile_wx, tile_wz + 1, tile_wlevel)];
                        if( other_paint->step != PAINT_STEP_DONE )
                        {
                            if( (tile->spans & SPAN_FLAG_NORTH) == 0 ||
                                other_paint->step != PAINT_STEP_WAIT_ADJACENT_GROUND )
                            {
                                goto done;
                            }
                        }
                    }
                }
                if( tile_wz <= camera_wz && tile_wz > min_draw_z )
                {
                    if( tile_wz - 1 >= min_draw_z )
                    {
                        other_paint = &painter->tiles[painter_coord_idx(
                            painter, tile_wx, tile_wz - 1, tile_wlevel)];
                        if( other_paint->step != PAINT_STEP_DONE )
                        {
                            if( (tile->spans & SPAN_FLAG_SOUTH) == 0 ||
                                other_paint->step != PAINT_STEP_WAIT_ADJACENT_GROUND )
                            {
                                goto done;
                            }
                        }
                    }
                }

                tile_paint->step = PAINT_STEP_GROUND;
            }

            if( tile_paint->step == PAINT_STEP_GROUND )
            {
                tile_paint->step = PAINT_STEP_WAIT_ADJACENT_GROUND;

                int near_walls = near_wall_flags(camera_wx, camera_wz, tile_wx, tile_wz);
                int far_walls = ~near_walls;
                tile_paint->near_wall_flags |= near_walls;

                if( tile->bridge_tile != -1 )
                {
                    bridge_underpass_tile = &painter->tiles[tile->bridge_tile];

                    push_command_terrain(buffer, tile_wx, tile_wz, tile_wlevel);

                    if( bridge_underpass_tile->wall_a != -1 )
                    {
                        element = &painter->elements[bridge_underpass_tile->wall_a];
                        assert(element->kind == PNTRELEM_WALL_A);
                        push_command_entity(buffer, element->_wall.entity);
                    }

                    for( int i = 0; i < bridge_underpass_tile->scenery_count; i++ )
                    {
                        int scenery_element = bridge_underpass_tile->scenery[i];
                        element = &painter->elements[scenery_element];
                        assert(element->kind == PNTRELEM_SCENERY);
                        push_command_entity(buffer, element->_scenery.entity);
                    }
                }

                push_command_terrain(buffer, tile_wx, tile_wz, tile_wlevel);

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
                        int x_diff = element->wx - camera_wx;
                        int z_diff = element->wz - camera_wz;

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

                if( tile_wx < camera_wx )
                {
                    if( tile_wx + 1 < max_draw_x )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_wx + 1, tile_wz, tile_wlevel);
                        other_paint = &painter->tiles[other_idx];

                        if( other_paint->step != PAINT_STEP_DONE &&
                            (tile->spans & SPAN_FLAG_EAST) != 0 )
                        {
                            other_paint->queue_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&painter->queue, other_idx);
                            else
                                int_queue_push_wrap_prio(
                                    &painter->catchup_queue, other_idx, prio - 1);
                        }
                    }
                }
                if( tile_wx > camera_wx )
                {
                    if( tile_wx - 1 >= min_draw_x )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_wx - 1, tile_wz, tile_wlevel);
                        other_paint = &painter->tiles[other_idx];

                        if( other_paint->step != PAINT_STEP_DONE &&
                            (tile->spans & SPAN_FLAG_WEST) != 0 )
                        {
                            other_paint->queue_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&painter->queue, other_idx);
                            else
                                int_queue_push_wrap_prio(
                                    &painter->catchup_queue, other_idx, prio - 1);
                        }
                    }
                }

                if( tile_wz < camera_wz )
                {
                    if( tile_wz + 1 < max_draw_z )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_wx, tile_wz + 1, tile_wlevel);
                        other_paint = &painter->tiles[other_idx];

                        if( other_paint->step != PAINT_STEP_DONE &&
                            (tile->spans & SPAN_FLAG_NORTH) != 0 )
                        {
                            other_paint->queue_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&painter->queue, other_idx);
                            else
                                int_queue_push_wrap_prio(
                                    &painter->catchup_queue, other_idx, prio - 1);
                        }
                    }
                }

                if( tile_wz > camera_wz )
                {
                    if( tile_wz - 1 >= min_draw_z )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_wx, tile_wz - 1, tile_wlevel);
                        other_paint = &painter->tiles[other_idx];

                        if( other_paint->step != PAINT_STEP_DONE &&
                            (tile->spans & SPAN_FLAG_SOUTH) != 0 )
                        {
                            other_paint->queue_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&painter->queue, other_idx);
                            else
                                int_queue_push_wrap_prio(
                                    &painter->catchup_queue, other_idx, prio - 1);
                        }
                    }
                }
            }

        done:;
        } //  while( painter->queue.length > 0 )
    } // for( int i = 0; i < coord_list_length; i++ )

    return 0;
}