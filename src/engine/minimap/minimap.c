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

struct Minimap*
minimap_new(
    int width,
    int height)
{
    assert(width > 0);
    assert(height > 0);

    struct Minimap* minimap = malloc(sizeof(struct Minimap));
    assert(minimap);
    memset(minimap, 0, sizeof(struct Minimap));

    minimap->width = width;
    minimap->height = height;

    minimap->tiles = malloc((size_t)width * (size_t)height * sizeof(struct MinimapTile));
    assert(minimap->tiles);
    memset(minimap->tiles, 0, (size_t)width * (size_t)height * sizeof(struct MinimapTile));

    return minimap;
}

void
minimap_free(struct Minimap* minimap)
{
    if( !minimap )
        return;
    free(minimap->tiles);
    free(minimap);
}

void
minimap_add_tile_wall(
    struct Minimap* minimap,
    int sx,
    int sz,
    enum MinimapWallFlag wall)
{
    assert(minimap);
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);
    struct MinimapTile* tile = &minimap->tiles[idx];
    tile->wall |= (uint16_t)wall;
}

void
minimap_set_tile_color(
    struct Minimap* minimap,
    int sx,
    int sz,
    uint32_t color_rgb,
    int is_foreground)
{
    assert(minimap);
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
    assert(minimap);
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);
    struct MinimapTile* tile = &minimap->tiles[idx];
    tile->shape = (uint8_t)shape;
    tile->rotation = (uint8_t)rotation;
}

int
minimap_tile_rgb(
    struct Minimap* minimap,
    int sx,
    int sz,
    int is_foreground)
{
    assert(minimap);
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);
    struct MinimapTile* tile = &minimap->tiles[idx];
    return is_foreground == MINIMAP_FOREGROUND ? (int)tile->foreground_rgb
                                               : (int)tile->background_rgb;
}

int
minimap_tile_wall(
    struct Minimap* minimap,
    int sx,
    int sz)
{
    assert(minimap);
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);
    return minimap->tiles[idx].wall;
}

int
minimap_tile_shape(
    struct Minimap* minimap,
    int sx,
    int sz)
{
    assert(minimap);
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);
    return minimap->tiles[idx].shape;
}

int
minimap_tile_rotation(
    struct Minimap* minimap,
    int sx,
    int sz)
{
    assert(minimap);
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    int idx = minimap_coord_idx(minimap, sx, sz);
    return minimap->tiles[idx].rotation;
}

static int g_minimap_tile_rotation_map[4][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 12, 8, 4, 0, 13, 9, 5, 1, 14, 10, 6, 2, 15, 11, 7, 3 },
    { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 },
    { 3, 7, 11, 15, 2, 6, 10, 14, 1, 5, 9, 13, 0, 4, 8, 12 },
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
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1 },
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

static uint32_t*
minimap_bake_argb(
    struct Minimap* minimap,
    int* out_width,
    int* out_height)
{
    assert(minimap);
    assert(out_width);
    assert(out_height);

    const int pw = minimap->width * 4;
    const int ph = minimap->height * 4;
    uint32_t* pixels = (uint32_t*)malloc((size_t)pw * (size_t)ph * sizeof(uint32_t));
    assert(pixels);

    const uint32_t black_argb = 0xFF000000u;
    for( int y = 0; y < ph; y++ )
    {
        uint32_t* row = pixels + y * pw;
        for( int x = 0; x < pw; x++ )
            row[x] = black_argb;
    }

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
            int tile_y = (minimap->height - 1 - sz) * 4;
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
                minimap_draw_wall(pixels, pw, tile_x, tile_y, wall, pw, ph);
        }
    }

    *out_width = pw;
    *out_height = ph;
    return pixels;
}

struct ToriRS_Sprite*
minimap_render_to_sprite(struct Minimap* minimap)
{
    assert(minimap);

    int pw = 0;
    int ph = 0;
    uint32_t* pixels = minimap_bake_argb(minimap, &pw, &ph);
    if( !pixels )
        return NULL;

    struct ToriRS_Sprite* sprite = calloc(1, sizeof(*sprite));
    assert(sprite);

    sprite->frames = calloc(1, sizeof(*sprite->frames));
    assert(sprite->frames);

    sprite->frame_count = 1;
    sprite->frames[0].pixels_argb = pixels;
    sprite->frames[0].width = pw;
    sprite->frames[0].height = ph;
    sprite->frames[0].crop_x = 0;
    sprite->frames[0].crop_y = 0;
    sprite->frames[0].crop_width = pw;
    sprite->frames[0].crop_height = ph;
    return sprite;
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
    assert(out_src_anchor_x);
    assert(out_src_anchor_y);
    if( sprite_w <= 0 || sprite_h <= 0 || map_tile_w <= 0 || map_tile_h <= 0 )
        return;

    *out_src_anchor_x = 0;
    *out_src_anchor_y = 0;

    int const px_per_tile_x = sprite_w / map_tile_w;
    int const px_per_tile_z = sprite_h / map_tile_h;
    *out_src_anchor_x = (camera_world_x * px_per_tile_x) / 128;
    *out_src_anchor_y = sprite_h - (camera_world_z * px_per_tile_z) / 128;
}
