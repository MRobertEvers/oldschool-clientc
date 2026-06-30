#include "minimap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static inline int
minimap_coord_idx(
    struct Minimap* minimap,
    int sx,
    int sz)
{
    return sx + sz * minimap->width;
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

void
minimap_commands_reset(struct MinimapRenderCommandBuffer* command_buffer)
{
    if( command_buffer )
        command_buffer->count = 0;
}

struct Minimap*
minimap_new(
    int width,
    int height)
{
    struct Minimap* minimap = malloc(sizeof(struct Minimap));
    memset(minimap, 0, sizeof(struct Minimap));

    minimap->width = width;
    minimap->height = height;

    minimap->tiles = malloc(width * height * sizeof(struct MinimapTile));
    memset(minimap->tiles, 0, width * height * sizeof(struct MinimapTile));

    minimap->locs = malloc(1024 * sizeof(struct MinimapLoc));
    minimap->locs_count = 0;
    minimap->locs_capacity = 1024;

    return minimap;
}

void
minimap_free(struct Minimap* minimap)
{
    if( !minimap )
        return;
    free(minimap->tiles);
    free(minimap->locs);
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
    enum MinimapLocType type)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    ensure_loc_capacity(minimap, 1);
    assert(minimap->locs_count < minimap->locs_capacity);
    struct MinimapLoc* loc = &minimap->locs[minimap->locs_count++];
    loc->tile_sx = sx;
    loc->tile_sz = sz;
    loc->type = type;
}

void
minimap_add_tile_wall(
    struct Minimap* minimap,
    int sx,
    int sz,
    enum MinimapWallFlag wall)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);

    struct MinimapTile* tile = &minimap->tiles[idx];
    tile->wall |= wall;
}

void
minimap_set_tile_color(
    struct Minimap* minimap,
    int sx,
    int sz,
    uint32_t color_rgb,
    int is_foreground)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);

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
    int shape,
    int rotation)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);

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
minimap_render_static_tiles(
    struct Minimap* minimap,
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
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
}

void
minimap_render_dynamic(
    struct Minimap* minimap,
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
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

    for( int i = 0; i < minimap->locs_count; i++ )
    {
        int lx = minimap->locs[i].tile_sx;
        int lz = minimap->locs[i].tile_sz;
        if( lx < sw_x || lx >= ne_x || lz < sw_z || lz >= ne_z )
            continue;
        push_loc_command(command_buffer, i);
    }
}

void
minimap_render(
    struct Minimap* minimap,
    int sw_x,
    int sw_z,
    int ne_x,
    int ne_z,
    struct MinimapRenderCommandBuffer* command_buffer)
{
    minimap_render_static_tiles(minimap, sw_x, sw_z, ne_x, ne_z, command_buffer);
    minimap_render_dynamic(minimap, sw_x, sw_z, ne_x, ne_z, command_buffer);
}

int
minimap_tile_rgb(
    struct Minimap* minimap,
    int sx,
    int sz,
    int is_foreground)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);
    struct MinimapTile* tile = &minimap->tiles[idx];
    return is_foreground == MINIMAP_FOREGROUND ? tile->foreground_rgb : tile->background_rgb;
}

int
minimap_tile_wall(
    struct Minimap* minimap,
    int sx,
    int sz)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);
    struct MinimapTile* tile = &minimap->tiles[idx];
    return tile->wall;
}
int
minimap_tile_shape(
    struct Minimap* minimap,
    int sx,
    int sz)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);
    struct MinimapTile* tile = &minimap->tiles[idx];
    return tile->shape;
}

int
minimap_tile_rotation(
    struct Minimap* minimap,
    int sx,
    int sz)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);
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

static int g_minimap_tile_rotation_map[4][16] = {
    { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15 },
    { 12, 8,  4,  0,  13, 9,  5,  1,  14, 10, 6,  2,  15, 11, 7,  3  },
    { 15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0  },
    { 3,  7,  11, 15, 2,  6,  10, 14, 1,  5,  9,  13, 0,  4,  8,  12 },
};

static int g_minimap_tile_mask[16][16] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1 },
    { 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 },
    { 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1 },
    { 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1 }
};

static void
minimap_fill_tile(
    uint32_t* pixel_buffer,
    int stride,
    int x,
    int y,
    int background_rgb,
    int foreground_rgb,
    int angle,
    int shape,
    int clip_width,
    int clip_height)
{
    assert(shape >= 0 && shape < 16);
    assert(angle >= 0 && angle < 4);

    if( x + 3 < 0 || x >= clip_width || y + 3 < 0 || y >= clip_height )
        return;

    int* mask = g_minimap_tile_mask[shape];
    int* rotation = g_minimap_tile_rotation_map[angle];

    int offset = x + y * stride;
    if( foreground_rgb == 0 )
    {
        if( background_rgb != 0 )
        {
            for( int i = 0; i < 4; i++ )
            {
                int current_y = y + i;
                if( current_y >= 0 && current_y < clip_height )
                {
                    if( x >= 0 && x < clip_width )
                        pixel_buffer[offset] = (uint32_t)background_rgb;
                    if( x + 1 >= 0 && x + 1 < clip_width )
                        pixel_buffer[offset + 1] = (uint32_t)background_rgb;
                    if( x + 2 >= 0 && x + 2 < clip_width )
                        pixel_buffer[offset + 2] = (uint32_t)background_rgb;
                    if( x + 3 >= 0 && x + 3 < clip_width )
                        pixel_buffer[offset + 3] = (uint32_t)background_rgb;
                }
                offset += stride;
            }
        }
        return;
    }

    int shape_vertex_index = 0;
    if( background_rgb != 0 )
    {
        for( int i = 0; i < 4; i++ )
        {
            int current_y = y + i;
            if( current_y >= 0 && current_y < clip_height )
            {
                int color0 =
                    mask[rotation[shape_vertex_index++]] == 0 ? background_rgb : foreground_rgb;
                int color1 =
                    mask[rotation[shape_vertex_index++]] == 0 ? background_rgb : foreground_rgb;
                int color2 =
                    mask[rotation[shape_vertex_index++]] == 0 ? background_rgb : foreground_rgb;
                int color3 =
                    mask[rotation[shape_vertex_index++]] == 0 ? background_rgb : foreground_rgb;

                if( x >= 0 && x < clip_width )
                    pixel_buffer[offset] = (uint32_t)color0;
                if( x + 1 >= 0 && x + 1 < clip_width )
                    pixel_buffer[offset + 1] = (uint32_t)color1;
                if( x + 2 >= 0 && x + 2 < clip_width )
                    pixel_buffer[offset + 2] = (uint32_t)color2;
                if( x + 3 >= 0 && x + 3 < clip_width )
                    pixel_buffer[offset + 3] = (uint32_t)color3;
            }
            else
            {
                shape_vertex_index += 4;
            }
            offset += stride;
        }
        return;
    }

    for( int i = 0; i < 4; i++ )
    {
        int current_y = y + i;
        if( current_y >= 0 && current_y < clip_height )
        {
            if( mask[rotation[shape_vertex_index++]] != 0 )
            {
                if( x >= 0 && x < clip_width )
                    pixel_buffer[offset] = (uint32_t)foreground_rgb;
            }
            if( mask[rotation[shape_vertex_index++]] != 0 )
            {
                if( x + 1 >= 0 && x + 1 < clip_width )
                    pixel_buffer[offset + 1] = (uint32_t)foreground_rgb;
            }
            if( mask[rotation[shape_vertex_index++]] != 0 )
            {
                if( x + 2 >= 0 && x + 2 < clip_width )
                    pixel_buffer[offset + 2] = (uint32_t)foreground_rgb;
            }
            if( mask[rotation[shape_vertex_index++]] != 0 )
            {
                if( x + 3 >= 0 && x + 3 < clip_width )
                    pixel_buffer[offset + 3] = (uint32_t)foreground_rgb;
            }
        }
        else
        {
            shape_vertex_index += 4;
        }
        offset += stride;
    }
}

static void
minimap_draw_wall(
    uint32_t* pixel_buffer,
    int stride,
    int x,
    int y,
    int wall,
    int clip_width,
    int clip_height)
{
    if( x + 3 < 0 || x >= clip_width || y + 3 < 0 || y >= clip_height )
        return;

    uint32_t rgb = 0xFFFFFFFFu;
    int offset = y * stride + x;

    if( wall & MINIMAP_WALL_WEST )
    {
        for( int p = 0; p < 4; p++ )
        {
            int current_y = y + p;
            if( current_y >= 0 && current_y < clip_height && x >= 0 && x < clip_width )
                pixel_buffer[offset + p * stride] = rgb;
        }
    }

    if( wall & MINIMAP_WALL_NORTH )
    {
        for( int p = 0; p < 4; p++ )
        {
            int current_x = x + p;
            if( current_x >= 0 && current_x < clip_width && y >= 0 && y < clip_height )
                pixel_buffer[offset + p] = rgb;
        }
    }

    if( wall & MINIMAP_WALL_EAST )
    {
        for( int p = 0; p < 4; p++ )
        {
            int current_y = y + p;
            int current_x = x + 3;
            if( current_y >= 0 && current_y < clip_height && current_x >= 0 &&
                current_x < clip_width )
                pixel_buffer[offset + 3 + p * stride] = rgb;
        }
    }

    if( wall & MINIMAP_WALL_SOUTH )
    {
        for( int p = 0; p < 4; p++ )
        {
            int current_x = x + p;
            int current_y = y + 3;
            if( current_x >= 0 && current_x < clip_width && current_y >= 0 &&
                current_y < clip_height )
                pixel_buffer[offset + p + 3 * stride] = rgb;
        }
    }

    if( wall & MINIMAP_WALL_NORTHEAST_SOUTHWEST )
    {
        for( int p = 0; p < 4; p++ )
        {
            int current_x = x + (3 - p);
            int current_y = y + p;
            if( current_x >= 0 && current_x < clip_width && current_y >= 0 &&
                current_y < clip_height )
                pixel_buffer[offset + p * stride + (3 - p)] = rgb;
        }
    }

    if( wall & MINIMAP_WALL_NORTHWEST_SOUTHEAST )
    {
        for( int p = 0; p < 4; p++ )
        {
            int current_x = x + p;
            int current_y = y + p;
            if( current_x >= 0 && current_x < clip_width && current_y >= 0 &&
                current_y < clip_height )
                pixel_buffer[offset + p * stride + p] = rgb;
        }
    }
}

uint32_t*
minimap_bake_argb(
    struct Minimap* minimap,
    int* out_width,
    int* out_height)
{
    if( !minimap || !out_width || !out_height )
        return NULL;

    const int pw = minimap->width * 4;
    const int ph = minimap->height * 4;
    uint32_t* pixels = (uint32_t*)malloc((size_t)pw * (size_t)ph * sizeof(uint32_t));
    if( !pixels )
        return NULL;

    const uint32_t black_argb = 0xFF000000u;
    for( int y = 0; y < ph; y++ )
    {
        uint32_t* row = pixels + y * pw;
        for( int x = 0; x < pw; x++ )
            row[x] = black_argb;
    }

    const int ne_z = minimap->height;
    for( int sx = 0; sx < minimap->width; sx++ )
    {
        for( int sz = 0; sz < minimap->height; sz++ )
        {
            int shape = minimap_tile_shape(minimap, sx, sz);
            int angle = minimap_tile_rotation(minimap, sx, sz);
            int rgb_background = minimap_tile_rgb(minimap, sx, sz, MINIMAP_BACKGROUND);
            int rgb_foreground = minimap_tile_rgb(minimap, sx, sz, MINIMAP_FOREGROUND);
            if( rgb_foreground == 0 && rgb_background == 0 )
                continue;

            int tile_x = sx * 4;
            int tile_y = (ne_z - sz) * 4;
            minimap_fill_tile(
                pixels,
                pw,
                tile_x,
                tile_y,
                rgb_background,
                rgb_foreground,
                angle,
                shape,
                pw,
                ph);

            int wall = minimap_tile_wall(minimap, sx, sz);
            if( wall != 0 )
            {
                minimap_draw_wall(pixels, pw, tile_x, tile_y, wall, pw, ph);
            }
        }
    }

    *out_width = pw;
    *out_height = ph;
    return pixels;
}

void
minimap_compute_camera_src_anchor(
    int camera_world_x,
    int camera_world_z,
    int sprite_w,
    int sprite_h,
    int map_tile_w,
    int map_tile_h,
    int* out_src_anchor_x,
    int* out_src_anchor_y)
{
    if( out_src_anchor_x )
        *out_src_anchor_x = 0;
    if( out_src_anchor_y )
        *out_src_anchor_y = 0;
    if( !out_src_anchor_x || !out_src_anchor_y || sprite_w <= 0 || sprite_h <= 0 || map_tile_w <= 0 ||
        map_tile_h <= 0 )
        return;

    int const px_per_tile_x = sprite_w / map_tile_w;
    int const px_per_tile_z = sprite_h / map_tile_h;
    *out_src_anchor_x = (camera_world_x * px_per_tile_x) / 128;
    *out_src_anchor_y = sprite_h - (camera_world_z * px_per_tile_z) / 128;
}
