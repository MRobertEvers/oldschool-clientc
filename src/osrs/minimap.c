#include "minimap.h"

#include <stdlib.h>
#include <string.h>

static inline int
minimap_coord_idx(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level)
{
    return sx + sz * minimap->width + level * minimap->width * minimap->height;
}

struct MinimapRenderCommandBuffer*
minimap_commands_new(int hint)
{
    struct MinimapRenderCommandBuffer* command_buffer =
        malloc(sizeof(struct MinimapRenderCommandBuffer));
    memset(command_buffer, 0, sizeof(struct MinimapRenderCommandBuffer));

    if( hint < 128 )
        hint = 128;

    command_buffer->commands = malloc(hint * sizeof(struct MinimapRenderCommand));
    memset(command_buffer->commands, 0, hint * sizeof(struct MinimapRenderCommand));

    command_buffer->count = 0;
    command_buffer->capacity = hint;
    return command_buffer;
}

void
minimap_commands_free(struct MinimapRenderCommandBuffer* command_buffer)
{
    free(command_buffer->commands);
    free(command_buffer);
}

struct Minimap*
minimap_new(
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
    int levels)
{
    struct Minimap* minimap = malloc(sizeof(struct Minimap));
    memset(minimap, 0, sizeof(struct Minimap));

    minimap->sw_x = sw_x;
    minimap->sw_z = sw_z;
    minimap->ne_x = ne_x;
    minimap->ne_z = ne_z;

    int width = ne_x - sw_x + 1;
    int height = ne_z - sw_z + 1;
    minimap->width = width;
    minimap->height = height;
    minimap->levels = levels;

    minimap->tiles = malloc(width * height * levels * sizeof(struct MinimapTile));
    memset(minimap->tiles, 0, width * height * levels * sizeof(struct MinimapTile));

    minimap->locs = malloc(1024 * sizeof(struct MinimapLoc));
    minimap->locs_count = 0;
    minimap->locs_capacity = 1024;

    return minimap;
}

void
minimap_free(struct Minimap* minimap)
{
    free(minimap);
}

static void
ensure_loc_capacity(
    struct Minimap* minimap,
    int count)
{
    if( minimap->locs_count + count > minimap->locs_capacity )
    {
        minimap->locs_capacity *= 2;
        minimap->locs = realloc(minimap->locs, minimap->locs_capacity * sizeof(struct MinimapLoc));
    }
}

void
minimap_add_loc(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level,
    enum MinimapLocType type)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    ensure_loc_capacity(minimap, 1);
    assert(minimap->locs_count < minimap->locs_capacity);
    struct MinimapLoc* loc = &minimap->locs[minimap->locs_count++];
    loc->tile_sx = sx;
    loc->tile_sz = sz;
    loc->level = level;
    loc->type = type;
}

void
minimap_add_tile_wall(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level,
    enum MinimapWallFlag wall)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz, level);

    struct MinimapTile* tile = &minimap->tiles[idx];
    tile->wall |= wall;
}

void
minimap_set_tile_color(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level,
    uint32_t color_rgb,
    int is_foreground)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz, level);

    struct MinimapTile* tile = &minimap->tiles[idx];
    if( is_foreground == MINIMAP_FOREGROUND )
        tile->foreground_rgb = color_rgb;
    else
        tile->background_rgb = color_rgb;
}

void
minimap_set_tile_shape(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level,
    int shape,
    int rotation)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz, level);

    struct MinimapTile* tile = &minimap->tiles[idx];
    tile->shape = shape;
    tile->rotation = rotation;
}

static void
ensure_command_capacity(
    struct MinimapRenderCommandBuffer* command_buffer,
    int count)
{
    if( command_buffer->count + count > command_buffer->capacity )
    {
        if( command_buffer->capacity == 0 )
        {
            command_buffer->capacity = 1024;
        }

        command_buffer->capacity *= 2;
        command_buffer->commands = realloc(
            command_buffer->commands,
            command_buffer->capacity * sizeof(struct MinimapRenderCommand));
    }
}

static void
push_tile_command(
    struct MinimapRenderCommandBuffer* command_buffer,
    int sx,
    int sz)
{
    ensure_command_capacity(command_buffer, 1);
    command_buffer->commands[command_buffer->count++] = (struct MinimapRenderCommand){
        ._tile = {
            .kind = MINIMAP_RENDER_COMMAND_TILE,
            .tile_sx = sx,
            .tile_sz = sz,
        },
    };
}

static void
push_loc_command(
    struct MinimapRenderCommandBuffer* command_buffer,
    int idx)
{
    ensure_command_capacity(command_buffer, 1);
    command_buffer->commands[command_buffer->count++] = (struct MinimapRenderCommand){
        ._loc = {
            .kind = MINIMAP_RENDER_COMMAND_LOC,
            .loc_idx = idx,
        },
    };
}

void
minimap_render(
    struct Minimap* minimap,
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
    int level,
    struct MinimapRenderCommandBuffer* command_buffer)
{
    if( sw_x < 0 )
        sw_x = 0;
    if( sw_z < 0 )
        sw_z = 0;
    if( ne_x > minimap->width )
        ne_x = minimap->width;
    if( ne_z > minimap->height )
        ne_z = minimap->height;

    for( int sx = sw_x; sx < ne_x; sx++ )
    {
        for( int sz = sw_z; sz < ne_z; sz++ )
        {
            push_tile_command(command_buffer, sx, sz);
        }
    }

    for( int i = 0; i < minimap->locs_count; i++ )
    {
        if( minimap->locs[i].level != level )
            push_loc_command(command_buffer, i);
    }
}

int
minimap_tile_rgb(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level,
    int is_foreground)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);
    assert(level >= 0 && level < minimap->levels);

    int idx = minimap_coord_idx(minimap, sx, sz, level);
    struct MinimapTile* tile = &minimap->tiles[idx];
    return is_foreground == MINIMAP_FOREGROUND ? tile->foreground_rgb : tile->background_rgb;
}

int
minimap_tile_wall(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);
    assert(level >= 0 && level < minimap->levels);

    int idx = minimap_coord_idx(minimap, sx, sz, level);
    struct MinimapTile* tile = &minimap->tiles[idx];
    return tile->wall;
}
int
minimap_tile_shape(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);
    assert(level >= 0 && level < minimap->levels);

    int idx = minimap_coord_idx(minimap, sx, sz, level);
    struct MinimapTile* tile = &minimap->tiles[idx];
    return tile->shape;
}

int
minimap_tile_rotation(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);
    assert(level >= 0 && level < minimap->levels);

    int idx = minimap_coord_idx(minimap, sx, sz, level);
    struct MinimapTile* tile = &minimap->tiles[idx];
    return tile->rotation;
}

enum MinimapLocType
minimap_loc_type(
    struct Minimap* minimap,
    int idx)
{
    assert(idx >= 0 && idx < minimap->locs_count);
    return minimap->locs[idx].type;
}