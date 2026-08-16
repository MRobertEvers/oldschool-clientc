#ifndef MINIMAP_H
#define MINIMAP_H

#include "engine/torirs_types.h"

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
    uint8_t shape;
    uint8_t rotation;
};

struct Minimap
{
    int width;
    int height;
    struct MinimapTile* tiles;
};

#define MINIMAP_FOREGROUND 1
#define MINIMAP_BACKGROUND 0

struct Minimap*
minimap_new(
    int width,
    int height);

void
minimap_free(struct Minimap* minimap);

void
minimap_add_tile_wall(
    struct Minimap* minimap,
    int sx,
    int sz,
    enum MinimapWallFlag wall);

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

/** Bake static tiles+walls to a 1-frame sprite (width*4 x height*4). Caller owns; free with
 * ToriRS_SpriteFree. NULL on alloc failure. */
struct ToriRS_Sprite*
minimap_render_to_sprite(struct Minimap* minimap);

/** Map-texture pivot for the camera position (matches minimap_render_to_sprite tile layout). */
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
