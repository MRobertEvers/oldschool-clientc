#ifndef MINIMAP_H
#define MINIMAP_H

#include <stdint.h>

struct Minimap;

enum MinimapWallFlag
{
    MINIMAP_WALL_NONE = 0,
    MINIMAP_WALL_NORTH = 1 << 0,
    MINIMAP_WALL_EAST = 1 << 1,
    MINIMAP_WALL_SOUTH = 1 << 2,
    MINIMAP_WALL_WEST = 1 << 3,
    MINIMAP_WALL_NORTHEAST_SOUTHWEST = 1 << 4,
    MINIMAP_WALL_NORTHWEST_SOUTHEAST = 1 << 5,
    MINIMAP_DOOR_NORTH = 1 << 5,
    MINIMAP_DOOR_EAST = 1 << 6,
    MINIMAP_DOOR_SOUTH = 1 << 7,
    MINIMAP_DOOR_WEST = 1 << 8,
    MINIMAP_DOOR_NORTHEAST_SOUTHWEST = 1 << 9,
    MINIMAP_DOOR_NORTHWEST_SOUTHEAST = 1 << 10,
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

struct Minimap
{
    int width;
    int height;

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
    enum MinimapWallFlag wall);

#define MINIMAP_FOREGROUND 1
#define MINIMAP_BACKGROUND 0

int
minimap_tile_rgb(
    struct Minimap* minimap,
    int sx,
    int sz,
    int is_foreground);

int
minimap_tile_wall(
    struct Minimap* minimap,
    int sx,
    int sz);

int
minimap_tile_shape(
    struct Minimap* minimap,
    int sx,
    int sz);

int
minimap_tile_rotation(
    struct Minimap* minimap,
    int sx,
    int sz);

enum MinimapLocType
minimap_loc_type(
    struct Minimap* minimap,
    int idx);

void
minimap_set_tile_color(
    struct Minimap* minimap,
    int sx,
    int sz,
    uint32_t color_rgb,
    int is_foreground);

void
minimap_set_tile_shape(
    struct Minimap* minimap,
    int sx,
    int sz,
    int shape,
    int rotation);

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

#endif
