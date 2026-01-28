#ifndef PAINTERS_H
#define PAINTERS_H

#include <stdint.h>
/**
 * Coordinate naming convention:
 * c := chunk -> relative to the map chunk, 0-63
 * s := scene -> relative to the scene, however big the scene is
 * w := world -> relative to the world, 0-16k. This is the world coordinate system.
 * m := model -> model coordinates for rendering.
 */

/**
 * Tells the renderer to defer drawing locs until
 * the underlay is drawn for tiles in the direction
 * of the span.
 */
enum SpanFlag
{
    SPAN_FLAG_WEST = 1 << 0,
    SPAN_FLAG_NORTH = 1 << 1,
    SPAN_FLAG_EAST = 1 << 2,
    SPAN_FLAG_SOUTH = 1 << 3,
};

enum PaintersTileFlags
{
    /**
     * The bridge tile is actually a bridge drawn on a different level.
     */
    PAINTERS_TILE_FLAG_BRIDGE = 1 << 0,
};

#define MAX_SCENERY_COUNT 10
struct PaintersTile
{
    // These are only used for normal locs
    int32_t scenery[MAX_SCENERY_COUNT];
    uint16_t scenery_count;

    int32_t wall_a;
    int32_t wall_b;
    int32_t wall_decor_a;
    int32_t wall_decor_b;

    int32_t ground_decor;

    int32_t bridge_tile;

    int32_t ground_object_bottom;
    int32_t ground_object_middle;
    int32_t ground_object_top;

    // Contains directions for which tiles are waiting for us to draw.
    // This is determined by locs that are larger than 1x1.
    // E.g. If a table is 3x1, then the spans for each tile will be:
    // (Assuming the table is going west-east direction)
    // WEST SIDE
    //     SPAN_FLAG_EAST,
    //     SPAN_FLAG_EAST | SPAN_FLAG_WEST,
    //     SPAN_FLAG_WEST
    // EAST SIDE
    //
    // For a 2x2 table, the spans will be:
    // WEST SIDE  <->  EAST SIDE
    //     [SPAN_FLAG_EAST | SPAN_FLAG_SOUTH,    SPAN_FLAG_WEST | SPAN_FLAG_SOUTH]
    //     [SPAN_FLAG_EAST | SPAN_FLAG_NORTH,    SPAN_FLAG_WEST | SPAN_FLAG_NORTH]
    //
    //
    // As the underlays are drawn diagonally inwards from the corner, once each of the
    // underlays is drawn, the loc on top is drawn.
    // The spans are used to determine which tiles are waiting for us to draw.
    uint8_t spans;

    // These are stored on the tile. Sometimes to tile coordinates do not match the
    // index in the array coordinate (e.g. bridges)
    uint16_t sx;
    uint16_t sz;
    uint8_t slevel;

    uint8_t terrain_slevel;

    uint16_t flags;
};

enum PaintersElementKind
{
    PNTRELEM_INVALID = 0,
    PNTRELEM_GROUND,
    PNTRELEM_SCENERY,
    PNTRELEM_WALL_A,
    PNTRELEM_WALL_B,
    PNTRELEM_GROUND_DECOR,
    PNTRELEM_WALL_DECOR,
    PNTRELEM_GROUND_OBJECT,
};

struct NormalScenery
{
    uint16_t entity;
    uint8_t size_x : 4;
    uint8_t size_z : 4;
};

struct GroundObject
{
    uint16_t entity;
};

enum WallSide
{
    WALL_SIDE_WEST = 1 << 0,        // 1
    WALL_SIDE_NORTH = 1 << 1,       // 2
    WALL_SIDE_EAST = 1 << 2,        // 4
    WALL_SIDE_SOUTH = 1 << 3,       // 8
    WALL_CORNER_NORTHWEST = 1 << 4, // 16
    WALL_CORNER_NORTHEAST = 1 << 5, // 32
    WALL_CORNER_SOUTHEAST = 1 << 6, // 64
    WALL_CORNER_SOUTHWEST = 1 << 7, // 128
};

struct Wall
{
    uint16_t entity;

    uint8_t side;
};

struct GroundDecor
{
    int entity;
};

enum ThroughWallFlags
{
    THROUGHWALL = 0x01,
    // THROUGHWALL = 0x100,
    // In OS1 and later, this is removed; only present in 2004scape.
    // THROUGHWALL_OUTSIDE = 0x200,
};

struct WallDecor
{
    uint16_t entity;

    // For throughwall, this specifies which side is the "outside".
    // enum WallSide side;
    struct
    {
        uint8_t _bf_side : 8;
        // In the 2004scape 0x100 is used to indicate an interior decor,
        // and 0x200 to indicate an exterior decor. 0x300 means draw both with same model.
        // In OS1 and deobs, this is removed and 0x100 represents both for throughwall always.
        // Both models are drawn before and after locs.
        // In this case, "side" represents the side of the wall that model_a is facing.
        // model_b faces the opposite side.
        uint8_t _bf_through_wall_flags : 8;
    };

    // int through_wall_flags;
};

struct PaintersElement
{
    enum PaintersElementKind kind;

    // These are stored on the element because sometimes tiles refer to elements that are not on the
    // same tile.
    uint16_t sx;
    uint16_t sz;
    uint8_t slevel;

    union
    {
        struct NormalScenery _scenery;
        struct Wall _wall;
        struct GroundDecor _ground_decor;
        struct WallDecor _wall_decor;
        struct GroundObject _ground_object;
    };
};

enum PaintersCommandKind
{
    PNTR_CMD_INVALID = 0,
    PNTR_CMD_ELEMENT,
    PNTR_CMD_TERRAIN,
};

// Want to pack into 64 bits.
// 16 bits: Need 16 bits for world element idx.
// 18 bits: x,y 9 bits each = 18 bits (512x512)
// 3  bits: level (0-7)
// 4  bits: command (0-15) : scenery or terrain

// Either element or terrain.
// Entity:
// - 4  bits: kind = 1, CMD = Entity
// - 16 bits: world entity idx.
// Terrain:
// - 4  bits: kind = 2, CMD = Terrain
// - 16 bits: terrain x,y,z. (9 bits each)
struct PaintersElementCommand
{
    union
    {
        uint32_t _packed;

        struct
        {
            uint32_t _bf_kind : 4;
        };

        struct
        {
            uint32_t _bf_kind : 4;
            uint32_t _bf_entity : 16;
        } _entity;

        struct
        {
            uint32_t _bf_kind : 4;
            uint32_t _bf_terrain_x : 9;
            uint32_t _bf_terrain_z : 9;
            uint32_t _bf_terrain_y : 4;
        } _terrain;
    };
};

struct Painter;

struct Painter*
painter_new(
    int width, //
    int height,
    int levels);

void
painter_free(
    struct Painter* painter //
);

int
painter_max_levels(struct Painter* painter);

struct PaintersTile*
painter_tile_at(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel);

struct PaintersElement*
painter_element_at(
    struct Painter* painter, //
    int element);

void
painter_tile_set_bridge(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int bridge_tile_sx,
    int bridge_tile_sz,
    int bridge_tile_slevel);

void
painter_tile_set_draw_level(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int draw_level);

void
painter_tile_set_terrain_level(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int terrain_slevel);

void
painter_tile_copyto(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int dest_sx,
    int dest_sz,
    int dest_slevel);

int
painter_add_normal_scenery(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int entity,
    int size_x,
    int size_y);

void
painter_mark_static_count(struct Painter* painter);

void
painter_reset_to_static(struct Painter* painter);

#define WALL_A 0
#define WALL_B 1

int
painter_add_wall(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int entity,
    int wall_ab,
    int side);

int
painter_add_wall_decor(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int wall_ab,
    int side,
    int through_wall_flags);

int
painter_add_ground_decor(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int entity);

#define GROUND_OBJECT_BOTTOM 0
#define GROUND_OBJECT_MIDDLE 1
#define GROUND_OBJECT_TOP 2

int
painter_add_ground_object(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int entity,
    int bottom_middle_top);

struct PaintersBuffer
{
    struct PaintersElementCommand* commands;
    int command_count;
    int command_capacity;
};

struct PaintersBuffer*
painter_buffer_new();

int
painter_paint(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

#endif