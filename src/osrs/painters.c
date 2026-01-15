#include "painters.h"

#include "tables/maps.h"

#include <stdlib.h>
#include <string.h>

// clang-format off
#include "painters_intqueue.u.c"
// clang-format on

static inline void
init_painter_tile(
    struct PaintersTile* tile,
    int sx,
    int sz,
    int slevel)
{
    tile->sx = sx;
    tile->sz = sz;
    tile->slevel = slevel;
    tile->spans = 0;
    tile->wall_a = -1;
    tile->wall_b = -1;
    tile->wall_decor_a = -1;
    tile->wall_decor_b = -1;
    tile->bridge_tile = -1;
    tile->ground_decor = -1;
    tile->ground_object_bottom = -1;
    tile->ground_object_middle = -1;
    tile->ground_object_top = -1;
    tile->flags = 0;
}

enum TilePaintStep
{
    // Do not draw ground until adjacent tiles are done,
    // unless we are spanned by that tile.
    PAINT_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED = 0,
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

struct ElementPaint
{
    uint8_t drawn;
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
    struct ElementPaint* element_paints;
    int element_count;
    int element_capacity;

    struct IntQueue queue;
    struct IntQueue catchup_queue;
};

static inline int
painter_push_element(struct Painter* painter)
{
    int element = painter->element_count++;
    if( element >= painter->element_capacity )
    {
        painter->element_capacity *= 2;
        painter->elements =
            realloc(painter->elements, painter->element_capacity * sizeof(struct PaintersElement));

        painter->element_paints = realloc(
            painter->element_paints, painter->element_capacity * sizeof(struct ElementPaint));
    }
    memset(&painter->elements[element], 0, sizeof(struct PaintersElement));
    return element;
}

static inline int
painter_coord_idx(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel)
{
    assert(sx >= 0);
    assert(sz >= 0);
    assert(slevel >= 0);
    assert(sx < painter->width);
    assert(sz < painter->height);
    assert(slevel < painter->levels);

    return sx + sz * painter->width + slevel * painter->width * painter->height;
}

struct Painter*
painter_new(
    int width,
    int height,
    int levels)
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

    for( int sx = 0; sx < width; sx++ )
    {
        for( int sz = 0; sz < height; sz++ )
            for( int slevel = 0; slevel < levels; slevel++ )
                init_painter_tile(painter_tile_at(painter, sx, sz, slevel), sx, sz, slevel);
    }

    painter->tile_paints = malloc(tile_count * sizeof(struct TilePaint));
    memset(painter->tile_paints, 0, tile_count * sizeof(struct TilePaint));

    painter->elements = malloc(128 * sizeof(struct PaintersElement));
    painter->element_count = 0;
    painter->element_capacity = 128;

    painter->element_paints = malloc(128 * sizeof(struct ElementPaint));
    memset(painter->element_paints, 0, 128 * sizeof(struct ElementPaint));

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
painter_tile_at(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel)
{
    return &painter->tiles[painter_coord_idx(painter, sx, sz, slevel)];
}

struct PaintersElement*
painter_element_at(
    struct Painter* painter, //
    int element)
{
    assert(element < painter->element_count);
    return &painter->elements[element];
}

static struct TilePaint*
tile_paint_at(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel)
{
    return &painter->tile_paints[painter_coord_idx(painter, sx, sz, slevel)];
}

static void
compute_normal_scenery_spans(
    struct Painter* painter,
    int loc_x,
    int loc_z,
    int loc_level,
    int size_x,
    int size_z,
    int element)
{
    struct PaintersTile* tile = NULL;
    int min_tile_x = loc_x;
    int min_tile_z = loc_z;

    // The max tile is not actually included in the span
    int max_tile_exclusive_x = loc_x + size_x - 1;
    int max_tile_exclusive_z = loc_z + size_z - 1;
    if( max_tile_exclusive_x > painter->width - 1 )
        max_tile_exclusive_x = painter->width - 1;
    if( max_tile_exclusive_z > painter->height - 1 )
        max_tile_exclusive_z = painter->height - 1;

    for( int x = min_tile_x; x <= max_tile_exclusive_x; x++ )
    {
        for( int z = min_tile_z; z <= max_tile_exclusive_z; z++ )
        {
            int span_flags = 0;
            if( x > min_tile_x )
            {
                // Block until the west tile underlay is drawn.
                span_flags |= SPAN_FLAG_WEST;
            }

            if( x < max_tile_exclusive_x )
            {
                // Block until the east tile underlay is drawn.
                span_flags |= SPAN_FLAG_EAST;
            }

            if( z > min_tile_z )
            {
                // Block until the south tile underlay is drawn.
                span_flags |= SPAN_FLAG_SOUTH;
            }

            if( z < max_tile_exclusive_z )
            {
                // Block until the north tile underlay is drawn.
                span_flags |= SPAN_FLAG_NORTH;
            }

            tile = painter_tile_at(painter, x, z, loc_level);
            tile->spans |= span_flags;
            assert(tile->scenery_count < MAX_SCENERY_COUNT);
            tile->scenery[tile->scenery_count++] = element;
        }
    }
}

int
painter_add_normal_scenery(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int size_x,
    int size_z)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    int element = painter_push_element(painter);

    compute_normal_scenery_spans(painter, sx, sz, slevel, size_x, size_z, element);

    assert(size_x > 0);
    assert(size_z > 0);
    assert(size_x < 16);
    assert(size_z < 16);
    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_SCENERY,
        .sx = sx,
        .sz = sz,
        .slevel = slevel,
        ._scenery = { .entity = entity, .size_x = size_x, .size_z = size_z },
    };
    return element;
}

int
painter_add_wall(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int wall_ab,
    int side)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    enum PaintersElementKind kind;
    int element = painter_push_element(painter);

    switch( wall_ab )
    {
    case WALL_A:
        assert(tile->wall_a == -1);
        kind = PNTRELEM_WALL_A;
        tile->wall_a = element;
        break;
    case WALL_B:
        assert(tile->wall_b == -1);
        kind = PNTRELEM_WALL_B;
        tile->wall_b = element;
        break;
    default:
        assert(false);
    }

    painter->elements[element] = (struct PaintersElement){
        .kind = kind,
        .sx = sx,
        .sz = sz,
        .slevel = slevel,
        ._wall = { .entity = entity, .side = side },
    };

    return element;
}

int
painter_add_wall_decor(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int wall_ab,
    int side,
    int through_wall_flags)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    int element = painter_push_element(painter);

    switch( wall_ab )
    {
    case WALL_A:
        assert(tile->wall_decor_a == -1);
        tile->wall_decor_a = element;
        break;
    case WALL_B:
        assert(tile->wall_decor_b == -1);
        tile->wall_decor_b = element;
        break;
    default:
        assert(false);
    }

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_WALL_DECOR,
        .sx = sx,
        .sz = sz,
        .slevel = slevel,
        ._wall_decor = { //
            .entity = entity, //
            ._bf_side = side,
            ._bf_through_wall_flags = through_wall_flags,
        },
    };
    return element;
}

int
painter_add_ground_decor(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    int element = painter_push_element(painter);

    assert(tile->ground_decor == -1);
    tile->ground_decor = element;

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_GROUND_DECOR,
        .sx = sx,
        .sz = sz,
        .slevel = slevel,
        ._ground_decor = { .entity = entity },
    };
    return element;
}

int
painter_add_ground_object(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int bottom_middle_top)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
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
        .sx = sx,
        .sz = sz,
        .slevel = slevel,
        ._ground_object = { .entity = entity },
    };
    return element;
}

/**
 * ****
 * The Painters Algorithm
 * ****
 */

static inline void
ensure_command_capacity(
    struct PaintersBuffer* buffer,
    int count)
{
    if( buffer->command_count + count > buffer->command_capacity )
    {
        buffer->command_capacity *= 2;
        buffer->commands = realloc(
            buffer->commands, buffer->command_capacity * sizeof(struct PaintersElementCommand));
    }
}

static inline void
push_command_entity(
    struct PaintersBuffer* buffer,
    int entity)
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
push_command_terrain(
    struct PaintersBuffer* buffer,
    int sx,
    int sz,
    int slevel)
{
    ensure_command_capacity(buffer, 1);
    buffer->commands[buffer->command_count++] = (struct PaintersElementCommand){
        ._terrain = {
            ._bf_kind = PNTR_CMD_TERRAIN,
            ._bf_terrain_x = sx,
            ._bf_terrain_z = sz,
            ._bf_terrain_y = slevel,
        },
    };
}

static inline int
near_wall_flags(
    int camera_sx,
    int camera_sz,
    int sx,
    int sz)
{
    int flags = 0;

    int camera_is_north = sz < camera_sz;
    int camera_is_east = sx < camera_sx;

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

struct PaintersBuffer*
painter_buffer_new()
{
    struct PaintersBuffer* buffer = (struct PaintersBuffer*)malloc(sizeof(struct PaintersBuffer));

    memset(buffer, 0x00, sizeof(struct PaintersBuffer));

    buffer->command_capacity = 512;
    buffer->commands = malloc(sizeof(struct PaintersElementCommand) * 512);
    return buffer;
}

int
painter_paint(
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
        int coord_sx = coord_list_x[i];
        int coord_sz = coord_list_z[i];

        assert(coord_sx >= min_draw_x);
        assert(coord_sx < max_draw_x);
        assert(coord_sz >= min_draw_z);
        assert(coord_sz < max_draw_z);

        int coord_idx = painter_coord_idx(painter, coord_sx, coord_sz, 0);
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

            int tile_sx = tile->sx;
            int tile_sz = tile->sz;
            int tile_slevel = tile->slevel;

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

            if( (tile->flags & PAINTERS_TILE_FLAG_BRIDGE) != 0 || tile_slevel > max_level )
            {
                tile_paint->step = PAINT_STEP_DONE;
                continue;
            }

            assert(tile_sx >= min_draw_x);
            assert(tile_sx < max_draw_x);
            assert(tile_sz >= min_draw_z);
            assert(tile_sz < max_draw_z);

            /**
             * If this is a loc revisit, then we need to verify adjacent tiles are done.
             */

            /**
             * If this is a loc revisit, then we need to verify adjacent tiles are done.
             */

            if( tile_paint->step == PAINT_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED )
            {
                if( tile_slevel > 0 )
                {
                    other_paint = tile_paint_at(painter, tile_sx, tile_sz, tile_slevel - 1);

                    if( other_paint->step != PAINT_STEP_DONE )
                    {
                        continue;
                    }
                }

                if( tile_sx >= camera_sx && tile_sx < max_draw_x )
                {
                    if( tile_sx + 1 < max_draw_x )
                    {
                        other_paint = tile_paint_at(painter, tile_sx + 1, tile_sz, tile_slevel);

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

                if( tile_sx <= camera_sx && tile_sx > min_draw_x )
                {
                    if( tile_sx - 1 >= min_draw_x )
                    {
                        other_paint = tile_paint_at(painter, tile_sx - 1, tile_sz, tile_slevel);

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

                if( tile_sz >= camera_sz && tile_sz < max_draw_z )
                {
                    if( tile_sz + 1 < max_draw_z )
                    {
                        other_paint = tile_paint_at(painter, tile_sx, tile_sz + 1, tile_slevel);

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
                if( tile_sz <= camera_sz && tile_sz > min_draw_z )
                {
                    if( tile_sz - 1 >= min_draw_z )
                    {
                        other_paint = tile_paint_at(painter, tile_sx, tile_sz - 1, tile_slevel);

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

                int near_walls = near_wall_flags(camera_sx, camera_sz, tile_sx, tile_sz);
                int far_walls = ~near_walls;
                tile_paint->near_wall_flags |= near_walls;

                if( tile->bridge_tile != -1 )
                {
                    bridge_underpass_tile = &painter->tiles[tile->bridge_tile];

                    push_command_terrain(buffer, tile_sx, tile_sz, tile_slevel);

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

                push_command_terrain(buffer, tile_sx, tile_sz, tile_slevel);

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

                if( tile_sx < camera_sx )
                {
                    if( tile_sx + 1 < max_draw_x )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_sx + 1, tile_sz, tile_slevel);
                        other_paint = &painter->tile_paints[other_idx];

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

                if( tile_sx > camera_sx )
                {
                    if( tile_sx - 1 >= min_draw_x )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_sx - 1, tile_sz, tile_slevel);
                        other_paint = &painter->tile_paints[other_idx];

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

                if( tile_sz < camera_sz )
                {
                    if( tile_sz + 1 < max_draw_z )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_sx, tile_sz + 1, tile_slevel);
                        other_paint = &painter->tile_paints[other_idx];

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

                if( tile_sz > camera_sz )
                {
                    if( tile_sz - 1 >= min_draw_z )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_sx, tile_sz - 1, tile_slevel);
                        other_paint = &painter->tile_paints[other_idx];

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

            bool waiting_spanning_scenery = false;
            scenery_queue_length = 0;
            if( tile_paint->step == PAINT_STEP_WAIT_ADJACENT_GROUND )
            {
                tile_paint->step = PAINT_STEP_LOCS;

                // Check if all locs are drawable.
                for( int j = 0; j < tile->scenery_count; j++ )
                {
                    int scenery_element = tile->scenery[j];

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
                        for( int other_tile_z = min_tile_z; other_tile_z <= max_tile_z;
                             other_tile_z++ )
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

                    int next_prio = 0;
                    if( element->_scenery.size_x > 1 || element->_scenery.size_z > 1 )
                    {
                        next_prio = element->_scenery.size_x > element->_scenery.size_z
                                        ? element->_scenery.size_x
                                        : element->_scenery.size_z;
                    }

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
                            if( other_tile_x != tile_sx || other_tile_z != tile_sz )
                            {
                                other_paint->queue_count++;
                                if( next_prio == 0 )
                                    int_queue_push_wrap(&painter->queue, other_idx);
                                else
                                    int_queue_push_wrap_prio(
                                        &painter->catchup_queue, other_idx, next_prio - 1);
                            }
                        }
                    }
                }

                if( !waiting_spanning_scenery )
                    tile_paint->step = PAINT_STEP_NOTIFY_ADJACENT_TILES;
                else
                    tile_paint->step = PAINT_STEP_WAIT_ADJACENT_GROUND;
            }

            // Move towards camera if farther away tiles are done.
            if( tile_paint->step == PAINT_STEP_NOTIFY_ADJACENT_TILES )
            {
                if( tile_slevel < painter->levels - 1 )
                {
                    int other_idx = painter_coord_idx(painter, tile_sx, tile_sz, tile_slevel + 1);
                    other_paint = &painter->tile_paints[other_idx];

                    if( other_paint->step != PAINT_STEP_DONE )
                    {
                        other_paint->queue_count++;
                        if( prio == 0 )
                            int_queue_push_wrap(&painter->queue, other_idx);
                        else
                            int_queue_push_wrap_prio(&painter->catchup_queue, other_idx, prio - 1);
                    }
                }

                if( tile_sx < camera_sx )
                {
                    if( tile_sx + 1 < max_draw_x )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_sx + 1, tile_sz, tile_slevel);
                        other_paint = &painter->tile_paints[other_idx];

                        if( other_paint->step != PAINT_STEP_DONE )
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
                if( tile_sx > camera_sx )
                {
                    if( tile_sx - 1 >= min_draw_x )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_sx - 1, tile_sz, tile_slevel);
                        other_paint = &painter->tile_paints[other_idx];
                        if( other_paint->step != PAINT_STEP_DONE )
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

                if( tile_sz < camera_sz )
                {
                    if( tile_sz + 1 < max_draw_z )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_sx, tile_sz + 1, tile_slevel);
                        other_paint = &painter->tile_paints[other_idx];
                        if( other_paint->step != PAINT_STEP_DONE )
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

                if( tile_sz > camera_sz )
                {
                    if( tile_sz - 1 >= min_draw_z )
                    {
                        int other_idx =
                            painter_coord_idx(painter, tile_sx, tile_sz - 1, tile_slevel);
                        other_paint = &painter->tile_paints[other_idx];
                        if( other_paint->step != PAINT_STEP_DONE )
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
        } //  while( painter->queue.length > 0 )
    } // for( int i = 0; i < coord_list_length; i++ )

    return 0;
}