#include "minimap.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int
minimap_coord_idx(
    struct Minimap* minimap,
    int sx,
    int sz,
    int level)
{
    if( level < 0 )
        level = 0;
    if( level >= minimap->levels )
        level = minimap->levels - 1;
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
    minimap->levels = MINIMAP_LEVELS;

    size_t const tile_bytes =
        (size_t)width * (size_t)height * MINIMAP_LEVELS * sizeof(struct MinimapTile);
    minimap->tiles = malloc(tile_bytes);
    memset(minimap->tiles, 0, tile_bytes);

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
minimap_del_tile_wall(
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
    tile->wall &= (uint16_t)~wall;
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

void
minimap_push_down_tiles(
    struct Minimap* minimap,
    int sx,
    int sz)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

    struct MinimapTile tmp = minimap->tiles[minimap_coord_idx(minimap, sx, sz, 0)];
    for( int level = 0; level < minimap->levels - 1; level++ )
        minimap->tiles[minimap_coord_idx(minimap, sx, sz, level)] =
            minimap->tiles[minimap_coord_idx(minimap, sx, sz, level + 1)];
    minimap->tiles[minimap_coord_idx(minimap, sx, sz, minimap->levels - 1)] = tmp;

    /*
     * A deck that states no floor of its own keeps the underpass colour.
     *
     * Not every LinkBelow column carries its deck as terrain. Lumbridge's
     * bridges do — cache level 1 names a plank overlay flo, that colour lands
     * on level 0 above, and the shift is the whole story. Port Sarim's piers do
     * not: their level-1 flo is the 0xFF00FF "hole" (overlay 42, no texture, no
     * underlay), because the deck is loc geometry — the plank models — and the
     * gaps between the planks are meant to show the sea. The shift then moves an
     * EMPTY tile onto the paint level and the water underneath, the only colour
     * the column ever had, wraps out of sight: every pier bakes as a black hole
     * in the middle of the harbour.
     *
     * The reference does not lose it either. World.pushDown (World.ts:213) hangs
     * the displaced square off the new one as `linkedSquare`, and the renderer
     * draws that link's ground under the deck (World.ts:1533) — which is why the
     * sea is visible through the planks in 3D here too. Keeping the colour is
     * that same link, applied to the plane the map bakes from; the wall lines the
     * shift brought down (the pier railings) stay on top of it.
     */
    {
        struct MinimapTile* base = &minimap->tiles[minimap_coord_idx(minimap, sx, sz, 0)];
        if( base->foreground_rgb == 0 && base->background_rgb == 0 )
        {
            base->foreground_rgb = tmp.foreground_rgb;
            base->background_rgb = tmp.background_rgb;
            base->shape = tmp.shape;
            base->rotation = tmp.rotation;
        }
    }
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
    int level,
    int is_foreground)
{
    assert(sx >= 0 && sx < minimap->width);
    assert(sz >= 0 && sz < minimap->height);

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

    /* Which of the two colours a tile can even show is decided by its overlay
     * shape, before any masking: World.setGround (World.ts:259/275/291) stores a
     * PLAIN tile as a QuickGround carrying ONLY the underlay colour, a DIAGONAL
     * (full-tile overlay) as a QuickGround carrying ONLY the overlay colour, and
     * everything else as a Ground carrying both. The distinction is invisible
     * while both colours are set — the shape masks are all-0 and all-1 — and
     * decides the tile when one is 0. A 0xFF00FF overlay resolves to no colour
     * and must leave the tile empty rather than fall through to the underlay,
     * which is what buried the Inferno's lava under its own floor. */
    if( shape == MINIMAP_TILE_SHAPE_PLAIN || shape == MINIMAP_TILE_SHAPE_DIAGONAL )
    {
        int const rgb = shape == MINIMAP_TILE_SHAPE_PLAIN ? background_rgb : foreground_rgb;
        if( rgb == 0 )
            return;
        for( int i = 0; i < 4; i++ )
        {
            int current_y = y + i;
            if( current_y >= 0 && current_y < clip_height )
            {
                for( int px = 0; px < 4; px++ )
                    if( x + px >= 0 && x + px < clip_width )
                        pixel_buffer[offset + px] = (uint32_t)rgb;
            }
            offset += stride;
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
minimap_draw_wall_bits(
    uint32_t* pixel_buffer,
    int stride,
    int x,
    int y,
    int wall,
    uint32_t rgb,
    int clip_width,
    int clip_height)
{
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

/** Wall lines: WALL_* bits draw white; DOOR_* bits (interactive wall locs —
 * doors) draw the same line positions red (reference drawDetail inactiveRgb /
 * activeRgb, Client.ts:5548/5636). */
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

    minimap_draw_wall_bits(
        pixel_buffer, stride, x, y, wall & 0x3F, 0xFFFFFFFFu, clip_width, clip_height);
    minimap_draw_wall_bits(
        pixel_buffer,
        stride,
        x,
        y,
        (wall >> MINIMAP_DOOR_SHIFT) & 0x3F,
        0xFFEE0000u,
        clip_width,
        clip_height);
}

/** One tile of one source level onto the shared pixel grid. */
static void
minimap_bake_tile(
    struct Minimap* minimap,
    uint32_t* pixels,
    int pw,
    int ph,
    int sx,
    int sz,
    int level)
{
    int rgb_background = minimap_tile_rgb(minimap, sx, sz, level, MINIMAP_BACKGROUND);
    int rgb_foreground = minimap_tile_rgb(minimap, sx, sz, level, MINIMAP_FOREGROUND);
    int tile_x = sx * 4;
    int tile_y = (minimap->height - sz) * 4;
    int wall;

    /* Ground and detail are two independent passes in the reference
     * (minimapBuildBuffer runs render2DGround over the square, then drawDetail
     * over it again, Client.ts:5530/5551), and a tile can take part in the
     * second without the first: World.setWall creates the Square itself, so a
     * wall on a tile with no floor still draws its line. Bailing out of the
     * whole tile when it has no colour dropped every such wall — the Inferno's
     * outer arena wall stands on floorless tiles and vanished entirely. */
    if( rgb_foreground != 0 || rgb_background != 0 )
        minimap_fill_tile(
            pixels,
            pw,
            tile_x,
            tile_y,
            rgb_background,
            rgb_foreground,
            minimap_tile_rotation(minimap, sx, sz, level),
            minimap_tile_shape(minimap, sx, sz, level),
            pw,
            ph);

    wall = minimap_tile_wall(minimap, sx, sz, level);
    if( wall != 0 )
        minimap_draw_wall(pixels, pw, tile_x, tile_y, wall, pw, ph);
}

/* TORIRS_MINIMAP_DUMP=1: per-level tile counts, then the grid as one char per
 * tile — ' ' nothing, '.' underlay only, 'O' overlay, 'W'/'D' a wall/door line
 * (which wins the cell, since a wall is what a bare outline looks like).
 * Separates "no colour reached the minimap" from "the colour is there and the
 * bake or the blit drops it" — the two look identical on screen. */
static void
minimap_dump_level(
    struct Minimap* minimap,
    int level)
{
    int n_bg = 0;
    int n_fg = 0;
    int n_wall = 0;
    uint32_t sample_bg = 0;
    uint32_t sample_fg = 0;
    for( int i = 0; i < minimap->width * minimap->height; i++ )
    {
        struct MinimapTile* tile =
            &minimap->tiles[i + (level * minimap->width * minimap->height)];
        if( tile->background_rgb )
        {
            n_bg++;
            sample_bg = tile->background_rgb;
        }
        if( tile->foreground_rgb )
        {
            n_fg++;
            sample_fg = tile->foreground_rgb;
        }
        if( tile->wall )
            n_wall++;
    }
    fprintf(
        stderr,
        "minimap level=%d: bg=%d (e.g. %06X) fg=%d (e.g. %06X) wall=%d\n",
        level,
        n_bg,
        sample_bg,
        n_fg,
        sample_fg,
        n_wall);

    fprintf(stderr, "minimap dump level=%d %dx%d\n", level, minimap->width, minimap->height);
    for( int sz = minimap->height - 1; sz >= 0; sz-- )
    {
        fprintf(stderr, "%3d ", sz);
        for( int sx = 0; sx < minimap->width; sx++ )
        {
            struct MinimapTile* tile =
                &minimap->tiles[minimap_coord_idx(minimap, sx, sz, level)];
            char cell = ' ';
            if( tile->foreground_rgb )
                cell = 'O';
            else if( tile->background_rgb )
                cell = '.';
            if( tile->wall )
                cell = (tile->wall >> MINIMAP_DOOR_SHIFT) ? 'D' : 'W';
            fputc(cell, stderr);
        }
        fputc('\n', stderr);
    }
}

uint32_t*
minimap_bake_argb(
    struct Minimap* minimap,
    int level,
    uint8_t const* tile_flags,
    int* out_width,
    int* out_height)
{
    assert(minimap);
    assert(out_width);
    assert(out_height);

    const int pw = minimap->width * 4;
    const int ph = minimap->height * 4;
    const int plane = minimap->width * minimap->height;
    uint32_t* pixels = (uint32_t*)malloc((size_t)pw * (size_t)ph * sizeof(uint32_t));
    assert(pixels);

    if( level < 0 )
        level = 0;
    if( level >= minimap->levels )
        level = minimap->levels - 1;

    if( getenv("TORIRS_MINIMAP_DUMP") )
        minimap_dump_level(minimap, level);

    const uint32_t black_argb = 0xFF000000u;
    for( int y = 0; y < ph; y++ )
    {
        uint32_t* row = pixels + y * pw;
        for( int x = 0; x < pw; x++ )
            row[x] = black_argb;
    }

    /* Reference minimapBuildBuffer (Client.ts:5519): a tile of the requested
     * level is drawn unless its land settings say VisBelow/ForceHighDetail (its
     * floor is a hole onto the level below), and the level above is drawn over
     * it wherever *that* level is VisBelow — how an upstairs balcony overhang or
     * a hole in the floor shows the level below on the map you're standing on.
     * A LinkBelow bridge deck is NOT handled here: it is pulled onto this level
     * structurally by the push-down (minimap_push_down_tiles), the same way the
     * reference shifts World tiles before baking. So this composite reads the
     * already-shifted tiles against raw mapl (tile_flags), which is why the deck
     * ends up drawn at level 0.
     * tile_flags is World.tile_flags (may be NULL: then draw the plain level). */
    for( int sx = 0; sx < minimap->width; sx++ )
    {
        for( int sz = 0; sz < minimap->height; sz++ )
        {
            int const idx = sx + sz * minimap->width;
            int const own = tile_flags ? tile_flags[idx + level * plane] : 0;

            if( (own & (MINIMAP_FLAG_VIS_BELOW | MINIMAP_FLAG_FORCE_HIGH_DETAIL)) == 0 )
                minimap_bake_tile(minimap, pixels, pw, ph, sx, sz, level);

            if( level + 1 < minimap->levels && tile_flags &&
                (tile_flags[idx + (level + 1) * plane] & MINIMAP_FLAG_VIS_BELOW) != 0 )
                minimap_bake_tile(minimap, pixels, pw, ph, sx, sz, level + 1);
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
