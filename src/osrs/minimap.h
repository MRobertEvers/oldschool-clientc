#ifndef MINIMAP_H
#define MINIMAP_H

#include "osrs/game.h"

#include <stdint.h>

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
    uint16_t wall;
    uint32_t background_rgb;
    uint32_t foreground_rgb;
    int shape;
    int rotation;

    uint16_t locs[8];
    int locs_count;
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
    int level;
    enum MinimapLocType type;
};

struct Minimap
{
    int sw_x;
    int sw_z;
    int ne_x;
    int ne_z;

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
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
    int levels);

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

struct Minimap*
minimap_new(
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
    int levels);

void
minimap_free(struct Minimap* minimap);

void
minimap_add_loc(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level,
    enum MinimapLocType type);

void
minimap_add_tile_wall(
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

void
minimap_render(
    struct Minimap* minimap,
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
    int level,
    struct MinimapRenderCommandBuffer* command_buffer);

#endif