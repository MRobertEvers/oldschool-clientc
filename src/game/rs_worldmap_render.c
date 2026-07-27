#include "game/rs_worldmap_render.h"

#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "engine/uitree_scene_bridge.h"
#include "engine/world_builder/world_decode_tile.h"

#include "asyncio.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graphics/shared_tables.h"
#include "osrs/palette.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"

#define WORLDMAP_TILES 64
/* One baked region per slot. 32 covers a full screen of regions at any zoom
 * (the widest view is ~7x5 regions) with room for the ring the pan crosses
 * into, and caps the pool at a few MB. */
#define WORLDMAP_REGION_SLOTS 32
/* Reserved scene sprite id range: base | slot. Out of cache range, like the
 * other reserved ids in uitree_scene_bridge.h. */
#define WORLDMAP_REGION_SCENE_BASE 0x60000000
/* The reference's wall colours: interactive locs (doors, gates) draw dark. */
#define WORLDMAP_WALL_DARK 0xcc0000
#define WORLDMAP_WALL_LIGHT 0xcccccc

struct RS_WorldMapRegionSlot
{
    int key;   /* CacheProvider_WorldMapGeographyKey, -1 when free */
    int scale; /* pixels per tile this was baked at */
    int size;  /* baked edge length in pixels */
    unsigned long stamp;
};

struct RS_WorldMapRender
{
    struct RS_WorldMapRegionSlot slots[WORLDMAP_REGION_SLOTS];
    unsigned long clock;
};

struct RS_WorldMapRender*
RS_WorldMapRender_New(void)
{
    struct RS_WorldMapRender* render = calloc(1, sizeof(*render));
    assert(render);
    for( int i = 0; i < WORLDMAP_REGION_SLOTS; i++ )
        render->slots[i].key = -1;
    return render;
}

void
RS_WorldMapRender_Free(struct RS_WorldMapRender* render)
{
    free(render);
}

void
RS_WorldMapRender_Clear(
    struct RS_WorldMapRender* render,
    struct ToriDraw_Scene* scene)
{
    if( !render )
        return;
    for( int i = 0; i < WORLDMAP_REGION_SLOTS; i++ )
    {
        if( render->slots[i].key >= 0 && scene )
            ToriDraw_SceneSpriteAdd(scene, WORLDMAP_REGION_SCENE_BASE | i, NULL, 0);
        render->slots[i].key = -1;
    }
}

/* =========================================================================
 * Tile shapes
 *
 * The overlay shapes (1..8, plus 9/10/11 which are rotations of 1 and 8) are
 * masks over the tile square saying which pixels take the overlay colour and
 * which keep the underlay. The reference builds one mask set per zoom level by
 * walking the square in a row/column order chosen per rotation; the same is done
 * here, evaluated per pixel instead of cached, because a bake touches each tile
 * once and the mask is a comparison rather than a lookup.
 * ========================================================================= */

static bool
worldmap_shape_fill(
    int shape,
    int row,
    int col,
    int size)
{
    int half = size / 2;
    switch( shape )
    {
    case 1:
        return col <= row;
    case 2:
        return col <= (row >> 1);
    case 3:
        return col <= (row >> 1);
    case 4:
        return col >= (row >> 1);
    case 5:
        return col >= (row >> 1);
    case 6:
        return col <= half;
    case 7:
        return col <= row - half;
    case 8:
        return col >= row - half;
    default:
        return true;
    }
}

/* Rotation is applied by walking the source square in a different order, which
 * is what the reference's makeWithOrder does. */
static void
worldmap_shape_source(
    int rotation,
    int row,
    int col,
    int size,
    int* out_row,
    int* out_col)
{
    int last = size - 1;
    switch( rotation & 3 )
    {
    case 1:
        *out_row = last - row;
        *out_col = col;
        break;
    case 2:
        *out_row = last - row;
        *out_col = last - col;
        break;
    case 3:
        *out_row = row;
        *out_col = last - col;
        break;
    default:
        *out_row = row;
        *out_col = col;
        break;
    }
}

static bool
worldmap_shape_pixel(
    int shape,
    int rotation,
    int row,
    int col,
    int size)
{
    int source_row = row;
    int source_col = col;
    int original = shape;

    /* Reference getTemplate: 9/10/11 are 1 and 8 at a turned rotation. */
    if( shape == 9 )
        rotation = (rotation + 1) & 3;
    if( shape == 10 || shape == 11 )
        rotation = (rotation + 3) & 3;
    if( original == 9 || original == 10 )
        shape = 1;
    else if( original == 11 )
        shape = 8;
    if( shape < 1 || shape > 8 )
        return true;

    worldmap_shape_source(rotation, row, col, size, &source_row, &source_col);
    return worldmap_shape_fill(shape, source_row, source_col, size);
}

/* =========================================================================
 * Colours
 * ========================================================================= */

/*
 * Ground colour. The reference reads it from table 20's pre-baked 64x64 image
 * (one blended colour per tile, PNG or JPEG). This build has no image decoder,
 * so the underlay flo's own colour is used instead: same hue per terrain type,
 * without the neighbour blending that softens the boundaries between them.
 */
static int
worldmap_underlay_rgb(
    struct CacheProvider* provider,
    int underlay_id,
    int background)
{
    struct ToriRS_Flotype* underlay;

    if( underlay_id < 0 )
        return background;
    underlay = CacheProvider_UnderlayGet(provider, underlay_id);
    if( !underlay )
        return background;
    return underlay->rgb_color & 0xFFFFFF;
}

/* Reference getFloorOverlayColor. */
static int
worldmap_overlay_rgb(
    struct CacheProvider* provider,
    int overlay_id,
    int background)
{
    struct ToriRS_Flotype* overlay;
    int hsl;

    if( overlay_id < 0 )
        return background;
    overlay = CacheProvider_FlotypeGet(provider, overlay_id);
    if( !overlay )
        return background;

    if( overlay->secondary_rgb_color >= 0 )
        hsl = palette_rgb_to_hsl16(overlay->secondary_rgb_color & 0xFFFFFF);
    else if( overlay->texture >= 0 )
        /* The reference averages the texture; the flo's own colour is the same
         * value the terrain builder falls back to for an unloaded texture. */
        hsl = palette_rgb_to_hsl16(overlay->rgb_color & 0xFFFFFF);
    else if( (overlay->rgb_color & 0xFFFFFF) == 0xFF00FF )
        return background;
    else
        hsl = palette_rgb_to_hsl16(overlay->rgb_color & 0xFFFFFF);

    return g_hsl16_to_rgb_table[terrain_adjust_lightness(hsl, 96) & 0xFFFF] & 0xFFFFFF;
}

/* =========================================================================
 * Bake
 * ========================================================================= */

static void
worldmap_fill_tile(
    uint32_t* pixels,
    int stride,
    int px,
    int py,
    int size,
    int rgb)
{
    for( int row = 0; row < size; row++ )
    {
        uint32_t* out = &pixels[(size_t)(py + row) * stride + px];
        for( int col = 0; col < size; col++ )
            out[col] = 0xFF000000u | (uint32_t)rgb;
    }
}

static void
worldmap_fill_tile_shaped(
    uint32_t* pixels,
    int stride,
    int px,
    int py,
    int size,
    int underlay_rgb,
    int overlay_rgb,
    int shape,
    int rotation)
{
    for( int row = 0; row < size; row++ )
    {
        uint32_t* out = &pixels[(size_t)(py + row) * stride + px];
        for( int col = 0; col < size; col++ )
        {
            bool overlay = worldmap_shape_pixel(shape, rotation, row, col, size);
            out[col] = 0xFF000000u | (uint32_t)(overlay ? overlay_rgb : underlay_rgb);
        }
    }
}

/* Wall lines: a loc of shape 0-3 or 9 draws as an edge of its tile. */
static void
worldmap_draw_wall(
    uint32_t* pixels,
    int stride,
    int px,
    int py,
    int size,
    int shape,
    int rotation,
    int rgb)
{
    uint32_t colour = 0xFF000000u | (uint32_t)(rgb & 0xFFFFFF);

    if( shape == 3 )
    {
        /* Corner post: one pixel of the tile. */
        int cx = ((rotation & 3) == 1 || (rotation & 3) == 2) ? px + size - 1 : px;
        int cy = ((rotation & 3) >= 2) ? py + size - 1 : py;
        pixels[(size_t)cy * stride + cx] = colour;
        return;
    }

    if( shape == 9 )
    {
        for( int i = 0; i < size; i++ )
        {
            int row = (rotation & 1) == 0 ? size - 1 - i : i;
            pixels[(size_t)(py + row) * stride + px + i] = colour;
        }
        return;
    }

    switch( rotation & 3 )
    {
    case 0:
        for( int i = 0; i < size; i++ )
            pixels[(size_t)(py + i) * stride + px] = colour;
        break;
    case 1:
        for( int i = 0; i < size; i++ )
            pixels[(size_t)py * stride + px + i] = colour;
        break;
    case 2:
        for( int i = 0; i < size; i++ )
            pixels[(size_t)(py + i) * stride + px + size - 1] = colour;
        break;
    default:
        for( int i = 0; i < size; i++ )
            pixels[(size_t)(py + size - 1) * stride + px + i] = colour;
        break;
    }
}

static void
worldmap_bake_region(
    struct CacheProvider* provider,
    struct RSCache_WorldMapGeography const* geography,
    int background,
    int scale,
    uint32_t* pixels)
{
    int stride = WORLDMAP_TILES * scale;
    int planes = geography->planes > 0 ? geography->planes : 1;

    for( int tile_x = 0; tile_x < WORLDMAP_TILES; tile_x++ )
    {
        for( int tile_y = 0; tile_y < WORLDMAP_TILES; tile_y++ )
        {
            int index = RSCache_WorldMapTileIndex(tile_x, tile_y);
            int underlay_id = (int)geography->underlay[index] - 1;
            int overlay_id = (int)geography->overlay[0][index] - 1;
            /* North is up: tile row 63 is the top of the image. */
            int px = tile_x * scale;
            int py = (WORLDMAP_TILES - 1 - tile_y) * scale;

            if( underlay_id == -1 && overlay_id == -1 )
            {
                worldmap_fill_tile(pixels, stride, px, py, scale, background);
                continue;
            }

            int shape = geography->overlay_shape[0][index];
            int overlay_rgb = worldmap_overlay_rgb(provider, overlay_id, background);
            if( overlay_id > -1 && shape == 0 )
            {
                worldmap_fill_tile(pixels, stride, px, py, scale, overlay_rgb);
                continue;
            }

            int underlay_rgb = geography->underlay[index] == 0
                                   ? background
                                   : worldmap_underlay_rgb(provider, underlay_id, background);
            if( overlay_id == -1 )
            {
                worldmap_fill_tile(pixels, stride, px, py, scale, underlay_rgb);
                continue;
            }

            worldmap_fill_tile_shaped(
                pixels,
                stride,
                px,
                py,
                scale,
                underlay_rgb,
                overlay_rgb,
                shape,
                geography->overlay_rotation[0][index]);
        }
    }

    /* Overlays on the upper planes paint over the ground, then the locs on every
     * plane draw their wall lines — same two passes as the reference. */
    for( int plane = 1; plane < planes; plane++ )
    {
        for( int tile_x = 0; tile_x < WORLDMAP_TILES; tile_x++ )
        {
            for( int tile_y = 0; tile_y < WORLDMAP_TILES; tile_y++ )
            {
                int index = RSCache_WorldMapTileIndex(tile_x, tile_y);
                int overlay_id = (int)geography->overlay[plane][index] - 1;
                if( overlay_id <= -1 )
                    continue;
                int px = tile_x * scale;
                int py = (WORLDMAP_TILES - 1 - tile_y) * scale;
                int shape = geography->overlay_shape[plane][index];
                int overlay_rgb = worldmap_overlay_rgb(provider, overlay_id, background);
                if( shape == 0 )
                    worldmap_fill_tile(pixels, stride, px, py, scale, overlay_rgb);
                else
                    worldmap_fill_tile_shaped(
                        pixels,
                        stride,
                        px,
                        py,
                        scale,
                        overlay_rgb,
                        overlay_rgb,
                        shape,
                        geography->overlay_rotation[plane][index]);
            }
        }
    }

    for( int plane = 0; plane < planes; plane++ )
    {
        for( int tile_x = 0; tile_x < WORLDMAP_TILES; tile_x++ )
        {
            for( int tile_y = 0; tile_y < WORLDMAP_TILES; tile_y++ )
            {
                int index = RSCache_WorldMapTileIndex(tile_x, tile_y);
                struct RSCache_WorldMapDecorList const* list = &geography->decor[plane][index];
                int px = tile_x * scale;
                int py = (WORLDMAP_TILES - 1 - tile_y) * scale;

                for( int i = 0; i < list->count; i++ )
                {
                    struct RSCache_WorldMapDecor const* decor = &list->items[i];
                    struct ToriRS_Location* loc;
                    int rgb;

                    if( !((decor->shape >= 0 && decor->shape <= 3) || decor->shape == 9) )
                        continue;

                    loc = CacheProvider_LocationGet(provider, decor->loc_id);
                    /* An unloaded loc still draws: the line is the wall, and the
                     * loc only decides light vs dark. Light is the common case. */
                    rgb = (loc && loc->is_interactive) ? WORLDMAP_WALL_DARK : WORLDMAP_WALL_LIGHT;

                    if( decor->shape == 2 )
                    {
                        worldmap_draw_wall(
                            pixels, stride, px, py, scale, 0, decor->rotation,
                            WORLDMAP_WALL_LIGHT);
                        worldmap_draw_wall(
                            pixels, stride, px, py, scale, 0, decor->rotation + 1, rgb);
                        continue;
                    }
                    worldmap_draw_wall(
                        pixels, stride, px, py, scale, decor->shape, decor->rotation, rgb);
                }
            }
        }
    }
}

/* =========================================================================
 * Slots
 * ========================================================================= */

static int
worldmap_slot_find(
    struct RS_WorldMapRender* render,
    int key,
    int scale)
{
    for( int i = 0; i < WORLDMAP_REGION_SLOTS; i++ )
    {
        if( render->slots[i].key == key && render->slots[i].scale == scale )
            return i;
    }
    return -1;
}

static int
worldmap_slot_claim(struct RS_WorldMapRender* render)
{
    int oldest = 0;
    for( int i = 0; i < WORLDMAP_REGION_SLOTS; i++ )
    {
        if( render->slots[i].key < 0 )
            return i;
        if( render->slots[i].stamp < render->slots[oldest].stamp )
            oldest = i;
    }
    return oldest;
}

/*
 * A region bakes once and is then blitted from, so it must not bake before the
 * floor configs it colours with are resident — a bake that ran early would cache
 * a black region for as long as it stays in the pool. Queue whatever is missing
 * and report it; the caller retries next frame.
 */
static bool
worldmap_floors_ready(
    struct CacheProvider* provider,
    struct ToriRS_TaskQueue* queue,
    struct RSCache_WorldMapGeography const* geography)
{
    bool ready = true;
    int planes = geography->planes > 0 ? geography->planes : 1;

    for( int i = 0; i < RSCACHE_WORLDMAP_TILE_AREA; i++ )
    {
        int underlay_id = (int)geography->underlay[i] - 1;
        if( underlay_id < 0 || CacheProvider_UnderlayHas(provider, underlay_id) )
            continue;
        {
            struct ToriRS_Task* task = CreateTask_UnderlayLoad(provider, underlay_id);
            if( task && queue )
                ToriRS_TaskQueue_Add(queue, task);
        }
        ready = false;
    }

    for( int plane = 0; plane < planes; plane++ )
    {
        for( int i = 0; i < RSCACHE_WORLDMAP_TILE_AREA; i++ )
        {
            int overlay_id = (int)geography->overlay[plane][i] - 1;
            if( overlay_id < 0 || CacheProvider_FlotypeHas(provider, overlay_id) )
                continue;
            {
                struct ToriRS_Task* task = CreateTask_FlotypeLoad(provider, overlay_id);
                if( task && queue )
                    ToriRS_TaskQueue_Add(queue, task);
            }
            ready = false;
        }
    }

    return ready;
}

/** Every compositemap record that lands on this map surface region. */
static int
worldmap_region_sources(
    struct ToriRS_WorldMapArea const* area,
    int region_x,
    int region_y,
    struct ToriRS_WorldMapRegionSource* out,
    int out_capacity)
{
    int count = 0;
    for( int i = 0; i < area->region_source_count && count < out_capacity; i++ )
    {
        struct ToriRS_WorldMapRegionSource const* source = &area->region_sources[i];
        if( source->dst_region_x != region_x || source->dst_region_y != region_y )
            continue;
        out[count++] = *source;
    }
    return count;
}

int
RS_WorldMapRender_RegionSprite(
    struct RS_WorldMapRender* render,
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_WorldMapArea const* area,
    int region_x,
    int region_y,
    int pixels_per_tile,
    int* out_size)
{
    struct RSCache_WorldMapGeography* geography;
    /* A region assembled from chunks has at most 8x8 records. */
    struct ToriRS_WorldMapRegionSource sources[64];
    int source_count;
    int key;
    int slot;
    int size;
    uint32_t* pixels;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** sprite_list;

    assert(render);
    assert(provider);
    assert(scene);

    if( !area )
        return -1;

    if( pixels_per_tile < 1 )
        pixels_per_tile = 1;
    if( pixels_per_tile > 8 )
        pixels_per_tile = 8;

    key = CacheProvider_WorldMapGeographyKey(area->id, region_x, region_y);
    slot = worldmap_slot_find(render, key, pixels_per_tile);
    if( slot >= 0 )
    {
        render->slots[slot].stamp = ++render->clock;
        if( out_size )
            *out_size = render->slots[slot].size;
        return WORLDMAP_REGION_SCENE_BASE | slot;
    }

    source_count =
        worldmap_region_sources(area, region_x, region_y, sources, (int)(sizeof(sources) / sizeof(sources[0])));
    if( source_count == 0 )
        return -1;

    geography = CacheProvider_WorldMapGeographyGet(provider, key);
    if( !geography )
    {
        struct ToriRS_Task* task =
            CreateTask_WorldMapGeographyLoad(provider, key, sources, source_count);
        if( task && queue )
            ToriRS_TaskQueue_Add(queue, task);
        return -1;
    }

    if( !worldmap_floors_ready(provider, queue, geography) )
        return -1;

    size = WORLDMAP_TILES * pixels_per_tile;
    pixels = calloc((size_t)size * size, sizeof(*pixels));
    assert(pixels);
    worldmap_bake_region(
        provider, geography, area->background_colour & 0xFFFFFF, pixels_per_tile, pixels);

    /* The tiles have served their purpose; the baked image is what gets drawn,
     * and holding both would keep a quarter-megabyte per region alive. */
    CacheProvider_WorldMapGeographyRelease(provider, key);

    sprite = ToriDraw_SpriteNewFromArgbOwned(pixels, size, size);
    if( !sprite )
    {
        free(pixels);
        return -1;
    }
    sprite_list = malloc(sizeof(*sprite_list));
    if( !sprite_list )
    {
        ToriDraw_SpriteFree(sprite);
        return -1;
    }
    sprite_list[0] = sprite;

    slot = worldmap_slot_claim(render);
    /* SceneSpriteAdd frees whatever the slot held, so an evicted region needs no
     * separate teardown. */
    ToriDraw_SceneSpriteAdd(scene, WORLDMAP_REGION_SCENE_BASE | slot, sprite_list, 1);
    render->slots[slot].key = key;
    render->slots[slot].scale = pixels_per_tile;
    render->slots[slot].size = size;
    render->slots[slot].stamp = ++render->clock;

    if( getenv("TORIRS_WORLDMAP_DEBUG") )
        fprintf(
            stderr,
            "worldmap: baked region %d,%d area=%d scale=%d slot=%d\n",
            region_x,
            region_y,
            area->id,
            pixels_per_tile,
            slot);

    if( out_size )
        *out_size = size;
    return WORLDMAP_REGION_SCENE_BASE | slot;
}
