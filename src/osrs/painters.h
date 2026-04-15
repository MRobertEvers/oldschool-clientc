#ifndef PAINTERS_H
#define PAINTERS_H

#include <stddef.h>
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
     * Bridge underpass slot after LinkBelow push-down: holds the former level-0 tile at grid
     * level 3, drawn via bridge_tile before the surface tile. Skipped in normal level passes.
     */
    PAINTERS_TILE_FLAG_BRIDGE = 1 << 0,
};

/**
 * Linked-list node in the painter's scenery pool (one node per tile footprint slot).
 * Tile grid position is implicit in the PaintersTile pointer; element_idx indexes
 * painter->elements.
 */
struct SceneryNode
{
    int16_t element_idx;
    uint8_t span;
    int32_t next; /* pool index, or -1 */
};

struct PaintersTile
{
    int16_t wall_a;
    int16_t wall_b;
    int16_t wall_decor_a;
    int16_t wall_decor_b;

    int16_t ground_decor;

    int16_t ground_object_bottom;
    int16_t ground_object_middle;
    int16_t ground_object_top;

    int32_t bridge_tile;
    int32_t scenery_head; /* pool index, or -1 */

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

    /* Combined span flags for all scenery on this tile (see enum SpanFlag). */
    uint8_t spans;

    /*
     * packed_meta layout (uint16_t):
     *   bits 0-2:   draw level (slevel), 0-7
     *   bits 3-5:   grid level (grid_level), 0-7
     *   bits 6-8:   terrain level (terrain_level), 0-7
     *   bits 9-15:  PaintersTileFlags
     *
     * grid_level: The fixed position of this tile in the vertical grid stack (0 = ground,
     *   1 = first floor, etc.). Set once at init and never changes. Used for array indexing
     *   (painter_coord_idx, step_idx_up/down) and vertical neighbor stepping.
     *
     * terrain_level: The cache level whose terrain mesh belongs to this tile — the level index
     *   passed to push_command_terrain and used by the renderer (e.g. world_tile_entity_at(x, z,
     *   terrain_level)). World terrain entities stay indexed by original cache level and are not
     *   shifted when bridge push-down (LinkBelow) moves painter tiles between grid slots (e.g.
     *   cache 1 content into grid 0, old grid 0 into grid 3). So grid_level (current slot) can
     *   diverge from the slot that holds the geometry. terrain_level is set to match grid_level
     *   at init; painter_tile_copyto intentionally does not reset it so copies retain the
     *   source's terrain index. On non-bridge columns, terrain_level always equals grid_level.
     *
     * draw level (slevel): The visibility level this tile participates in, checked against the
     *   UI draw_mask to decide whether the tile is drawn or skipped. Normally equals grid_level,
     *   but VisBelow (0x08 floor flag) lowers it so upper-floor tiles become visible when only
     *   lower levels are enabled in draw_mask — e.g. a tile on grid_level=1 can have slevel=0.
     */
    uint16_t packed_meta;
};

#define PAINTERS_TILE_META_SLEVEL_MASK 0x7u
#define PAINTERS_TILE_META_GRID_LEVEL_SHIFT 3
#define PAINTERS_TILE_META_GRID_LEVEL_MASK (0x7u << PAINTERS_TILE_META_GRID_LEVEL_SHIFT)
#define PAINTERS_TILE_META_TERRAIN_LEVEL_SHIFT 6
#define PAINTERS_TILE_META_TERRAIN_LEVEL_MASK (0x7u << PAINTERS_TILE_META_TERRAIN_LEVEL_SHIFT)
#define PAINTERS_TILE_META_FLAGS_SHIFT 9

static inline uint8_t
painters_tile_get_slevel(const struct PaintersTile* t)
{
    return (uint8_t)(t->packed_meta & PAINTERS_TILE_META_SLEVEL_MASK);
}

static inline void
painters_tile_set_slevel(
    struct PaintersTile* t,
    uint8_t v)
{
    t->packed_meta = (uint16_t)((t->packed_meta & ~PAINTERS_TILE_META_SLEVEL_MASK) | (v & 7u));
}

static inline uint8_t
painters_tile_get_grid_level(const struct PaintersTile* t)
{
    return (uint8_t)((t->packed_meta & PAINTERS_TILE_META_GRID_LEVEL_MASK) >>
                     PAINTERS_TILE_META_GRID_LEVEL_SHIFT);
}

static inline void
painters_tile_set_grid_level(
    struct PaintersTile* t,
    uint8_t v)
{
    t->packed_meta = (uint16_t)((t->packed_meta & ~PAINTERS_TILE_META_GRID_LEVEL_MASK) |
                                ((uint16_t)(v & 7u) << PAINTERS_TILE_META_GRID_LEVEL_SHIFT));
}

static inline uint8_t
painters_tile_get_terrain_level(const struct PaintersTile* t)
{
    return (uint8_t)((t->packed_meta & PAINTERS_TILE_META_TERRAIN_LEVEL_MASK) >>
                     PAINTERS_TILE_META_TERRAIN_LEVEL_SHIFT);
}

static inline void
painters_tile_set_terrain_level(
    struct PaintersTile* t,
    uint8_t v)
{
    t->packed_meta = (uint16_t)((t->packed_meta & ~PAINTERS_TILE_META_TERRAIN_LEVEL_MASK) |
                                ((uint16_t)(v & 7u) << PAINTERS_TILE_META_TERRAIN_LEVEL_SHIFT));
}

static inline uint16_t
painters_tile_get_flags(const struct PaintersTile* t)
{
    return (uint16_t)(t->packed_meta >> PAINTERS_TILE_META_FLAGS_SHIFT);
}

static inline void
painters_tile_set_flags(
    struct PaintersTile* t,
    uint16_t f)
{
    t->packed_meta = (uint16_t)((t->packed_meta & ((1u << PAINTERS_TILE_META_FLAGS_SHIFT) - 1u)) |
                                (f << PAINTERS_TILE_META_FLAGS_SHIFT));
}

static inline void
painters_tile_or_flags(
    struct PaintersTile* t,
    uint16_t f)
{
    painters_tile_set_flags(t, (uint16_t)(painters_tile_get_flags(t) | f));
}

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

struct PaintersCullMap;

/** Build cullmap at runtime (CPU bake). */
struct PaintersCullMap*
painters_cullmap_build(
    int radius,
    int near_clip_z,
    int screen_width,
    int screen_height);

struct PaintersCullMap*
painters_cullmap_new_nocull(void);

/** Load from packed visibility bytes (e.g. file read by caller). Caller frees with
 * painters_cullmap_free. */
struct PaintersCullMap*
painters_cullmap_from_blob(
    const uint8_t* data,
    size_t nbytes,
    int radius);

void
painters_cullmap_free(struct PaintersCullMap* cm);

void
painter_set_cullmap(
    struct Painter* painter,
    struct PaintersCullMap* cm);

void
painter_set_camera_angles(
    struct Painter* painter,
    int pitch,
    int yaw);

/** Bitmask of levels to draw (bits 0-3 for levels 0-3). Default 0xF = all levels. */
void
painter_set_level_mask(
    struct Painter* painter,
    uint8_t mask);

/** Draw only levels lo through hi inclusive (0-based). */
void
painter_set_level_range(
    struct Painter* painter,
    int lo,
    int hi);

/** Bitmask: which scratch contexts painter_new allocates up front (see painters_bucket / world3d /
 * distancemetric). */
enum PainterNewContextFlags
{
    PAINTER_NEW_CTX_BUCKET = 1u << 0,
    PAINTER_NEW_CTX_WORLD3D = 1u << 1,
    PAINTER_NEW_CTX_DISTMETRIC = 1u << 2,
};

struct Painter*
painter_new(
    int width, //
    int height,
    int levels,
    uint32_t init_contexts);

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
painter_tile_set_grid_level(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int grid_level);

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

int
painter_paint3(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

int
painter_paint4(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

/**
 * Like painter_paint4 (distance-bucket queue + insertion-sort scenery), but uses a full
 * tile_paints memset on native and targeted bbox clear on Emscripten/WASM.
 */
int
painter_paint4_1(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

int
painter_paint_bucket(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

int
painter_paint_world3d(
    struct Painter* painter, //
    struct PaintersBuffer* buffer,
    int camera_sx,
    int camera_sz,
    int camera_slevel);

#endif