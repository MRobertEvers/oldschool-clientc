#ifndef MINIMAP_H
#define MINIMAP_H

#include <stdint.h>

struct Minimap;

/* DOOR_* are the same six line positions as WALL_* shifted by
 * MINIMAP_DOOR_SHIFT: an interactive (active) wall loc — a door — draws its
 * line red instead of white (reference minimapBuildBuffer activeRgb,
 * Client.ts:5548 / drawDetail rgb pick). */
#define MINIMAP_DOOR_SHIFT 6
enum MinimapWallFlag
{
    MINIMAP_WALL_NONE = 0,
    MINIMAP_WALL_NORTH = 1 << 0,
    MINIMAP_WALL_EAST = 1 << 1,
    MINIMAP_WALL_SOUTH = 1 << 2,
    MINIMAP_WALL_WEST = 1 << 3,
    MINIMAP_WALL_NORTHEAST_SOUTHWEST = 1 << 4,
    MINIMAP_WALL_NORTHWEST_SOUTHEAST = 1 << 5,
    MINIMAP_DOOR_NORTH = MINIMAP_WALL_NORTH << MINIMAP_DOOR_SHIFT,
    MINIMAP_DOOR_EAST = MINIMAP_WALL_EAST << MINIMAP_DOOR_SHIFT,
    MINIMAP_DOOR_SOUTH = MINIMAP_WALL_SOUTH << MINIMAP_DOOR_SHIFT,
    MINIMAP_DOOR_WEST = MINIMAP_WALL_WEST << MINIMAP_DOOR_SHIFT,
    MINIMAP_DOOR_NORTHEAST_SOUTHWEST = MINIMAP_WALL_NORTHEAST_SOUTHWEST << MINIMAP_DOOR_SHIFT,
    MINIMAP_DOOR_NORTHWEST_SOUTHEAST = MINIMAP_WALL_NORTHWEST_SOUTHEAST << MINIMAP_DOOR_SHIFT,
};

struct MinimapTile
{
    uint32_t background_rgb;
    uint32_t foreground_rgb;
    uint16_t wall;
    uint16_t locs[8];
    uint8_t shape;
    uint8_t rotation;
    uint8_t locs_count;
};

enum MinimapLocType
{
    MINIMAP_LOC_TYPE_NONE = 0,
    MINIMAP_LOC_TYPE_PLAYER = 1,
    MINIMAP_LOC_TYPE_NPC = 2,
    MINIMAP_LOC_TYPE_OBJECT = 3,
};

struct MinimapLoc
{
    int tile_sx;
    int tile_sz;
    enum MinimapLocType type;
};

/** Cache land-settings bits the bake composition reads (Client-TS MapFlag);
 * the same values RSCACHE_FLOFLAG_* uses, redeclared so this stays leaf. */
enum MinimapTileFlag
{
    MINIMAP_FLAG_VIS_BELOW = 0x08,
    MINIMAP_FLAG_FORCE_HIGH_DETAIL = 0x10,
};

/** Terrain levels stored per tile (matches WORLD_MAP_TERRAIN_LEVELS). */
#define MINIMAP_LEVELS 4

/** The two overlay shapes the reference stores as a single-colour QuickGround
 * rather than a two-colour Ground (Client-TS TerrainOverlayShape / setGround).
 * A tile's shape is 0 with no overlay, else the map's stored shape + 1. */
#define MINIMAP_TILE_SHAPE_PLAIN 0
#define MINIMAP_TILE_SHAPE_DIAGONAL 1

struct Minimap
{
    int width;
    int height;
    int levels;

    struct MinimapTile* tiles;
    int tiles_count;
    int tiles_capacity;

    struct MinimapLoc* locs;
    int locs_count;
    int locs_capacity;
};

struct Minimap*
minimap_new(
    int width,
    int height);

enum MinimapRenderCommandKind
{
    MINIMAP_RENDER_COMMAND_TILE,
    MINIMAP_RENDER_COMMAND_LOC,
};

struct MinimapRenderCommand
{
    union
    {
        uint32_t kind : 2;

        struct
        {
            uint16_t kind : 2;
            uint16_t tile_sx : 10;
            uint16_t tile_sz : 10;
        } _tile;

        struct
        {
            uint16_t kind : 2;
            uint16_t loc_idx : 10;
        } _loc;
    };
};

struct MinimapRenderCommandBuffer
{
    struct MinimapRenderCommand* commands;
    int count;
    int capacity;
};

struct MinimapRenderCommandBuffer*
minimap_commands_new(int hint);

void
minimap_commands_free(struct MinimapRenderCommandBuffer* command_buffer);

void
minimap_commands_reset(struct MinimapRenderCommandBuffer* command_buffer);

void
minimap_free(struct Minimap* minimap);

void
minimap_add_loc(
    struct Minimap* minimap,
    int sx,
    int sz,
    enum MinimapLocType type);

void
minimap_add_tile_wall(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level,
    enum MinimapWallFlag wall);

/** Clear wall/door line bits on a tile (exact inverse of add: AND-NOT). Used
 * by the runtime loc change so an opened door's old line disappears. */
void
minimap_del_tile_wall(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level,
    enum MinimapWallFlag wall);

#define MINIMAP_FOREGROUND 1
#define MINIMAP_BACKGROUND 0

int
minimap_tile_rgb(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level,
    int is_foreground);

int
minimap_tile_wall(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level);

int
minimap_tile_shape(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level);

int
minimap_tile_rotation(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level);

enum MinimapLocType
minimap_loc_type(
    struct Minimap* minimap,
    int idx);

void
minimap_set_tile_color(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level,
    uint32_t color_rgb,
    int is_foreground);

void
minimap_set_tile_shape(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level,
    int shape,
    int rotation);

/** Plane-shift one column's tiles down a level, the way the geometry painter
 * and reference World.pushDown treat a LinkBelow bridge column: level 0 takes
 * the former cache level 1 (the deck), 1←2, 2←3, and the old level 0 (the
 * underpass floor) wraps to the top plane. The land-settings (tile_flags) are
 * intentionally NOT shifted — the bake's VisBelow composite reads raw mapl, so
 * once the deck tile sits at level 0 it draws on the player's map.
 *
 * A deck that states no floor of its own (a pier whose planks are loc geometry
 * over the 0xFF00FF hole flo) keeps the displaced underpass colour rather than
 * blanking the column — the reference's `linkedSquare`, see the function. */
void
minimap_push_down_tiles(
    struct Minimap* minimap,
    int sx,
    int sz);

void
minimap_render(
    struct Minimap* minimap,
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
    struct MinimapRenderCommandBuffer* command_buffer);

void
minimap_render_static_tiles(
    struct Minimap* minimap,
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
    struct MinimapRenderCommandBuffer* command_buffer);

void
minimap_render_dynamic(
    struct Minimap* minimap,
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
    struct MinimapRenderCommandBuffer* command_buffer);

/** Returns a malloc'd (width*4 x height*4) ARGB buffer of the whole map for one
 * level; caller owns. tile_flags is the per-level land-settings plane
 * (World.tile_flags, indexed x + z*width + level*width*height) used for the
 * reference VisBelow composition; NULL bakes the level plainly. */
uint32_t*
minimap_bake_argb(
    struct Minimap* minimap,
    int level,
    uint8_t const* tile_flags,
    int* out_width,
    int* out_height);

/** Map-texture pivot for the camera position (matches minimap_bake_argb tile layout). */
void
minimap_compute_camera_src_anchor(
    int camera_world_x,
    int camera_world_z,
    int sprite_w,
    int sprite_h,
    int map_tile_w,
    int map_tile_h,
    int* out_src_anchor_x,
    int* out_src_anchor_y);

#endif
