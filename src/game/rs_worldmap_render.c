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
/*
 * One baked region per slot.
 *
 * The pool has to hold every region on screen at once, or each frame evicts what
 * the next one asks for and regions blink in and out. The widest case is the
 * lowest zoom: a 765-pixel surface at 1 pixel per tile spans 12 regions across
 * and 8 down, plus the ring a pan crosses into — so 128 slots, with a byte
 * budget doing the real limiting since a slot costs 16 KB at that zoom and 1 MB
 * at the highest.
 */
#define WORLDMAP_REGION_SLOTS 512
/* The reference caps its rendered-region cache at 48 MB; same here. At the
 * lowest zoom a region is 16 KB, so the whole world fits; at the highest a
 * region is 1 MB and about 48 stay resident, which still covers a screen. */
#define WORLDMAP_REGION_BUDGET_BYTES (48 * 1024 * 1024)
/* Bakes started per frame. Zooming out makes hundreds of regions visible at
 * once; baking them all in one frame stalls for seconds, and baking none of
 * them (pool thrash) leaves the surface black. A few per frame fills the view
 * in under a second while the frame stays interactive. */
#define WORLDMAP_BAKES_PER_FRAME 6
/* Reserved scene sprite id range: base | slot. Out of cache range, like the
 * other reserved ids in uitree_scene_bridge.h. */
#define WORLDMAP_REGION_SCENE_BASE 0x60000000
/* The reference's wall colours: interactive locs (doors, gates) draw dark. */
#define WORLDMAP_WALL_DARK 0xcc0000
#define WORLDMAP_WALL_LIGHT 0xcccccc

/* Icons per region. A dense region (a city) runs to a few dozen; the cap keeps
 * the pool flat rather than growable, and overflow only drops icons that would
 * have overlapped anyway. */
#define WORLDMAP_REGION_ICON_MAX 96

struct RS_WorldMapRegionSlot
{
    int key;   /* CacheProvider_WorldMapGeographyKey, -1 when free */
    int scale; /* pixels per tile this was baked at */
    int size;  /* baked edge length in pixels */
    unsigned long stamp;
    /* False when the bake ran before every floor/loc it needed had loaded: the
     * image is missing colours and the icons the locs carry. Such a region is
     * re-baked, once, as soon as the assets arrive — without this the icons
     * simply never appear, since they are collected at bake time. */
    bool complete;
    struct RS_WorldMapRegionIcon icons[WORLDMAP_REGION_ICON_MAX];
    int icon_count;
};

/* A region whose floors are still loading. Bounded so a floor the cache cannot
 * supply (or a tile stream this build misreads) costs a few frames of waiting
 * rather than a load queued on every frame forever. */
struct RS_WorldMapPending
{
    int key;
    int attempts;
};

#define WORLDMAP_FLOOR_WAIT_FRAMES 30

struct RS_WorldMapRender
{
    /* Scene id of the "mapscene" sprite pack (trees, rocks, altars, piers...),
     * resolved by the app from the bridge — ui/ and this renderer cannot reach
     * the static-sprite registry themselves. -1 until set. */
    int mapscene_scene_id;
    struct RS_WorldMapRegionSlot slots[WORLDMAP_REGION_SLOTS];
    struct RS_WorldMapPending pending[WORLDMAP_REGION_SLOTS];
    unsigned long clock;
    size_t baked_bytes;
    int bakes_this_frame;
};

struct RS_WorldMapRender*
RS_WorldMapRender_New(void)
{
    struct RS_WorldMapRender* render = calloc(1, sizeof(*render));
    assert(render);
    render->mapscene_scene_id = -1;
    for( int i = 0; i < WORLDMAP_REGION_SLOTS; i++ )
    {
        render->slots[i].key = -1;
        render->pending[i].key = -1;
    }
    return render;
}

void
RS_WorldMapRender_BeginFrame(struct RS_WorldMapRender* render)
{
    if( render )
        render->bakes_this_frame = 0;
}

void
RS_WorldMapRender_SetMapScenes(
    struct RS_WorldMapRender* render,
    int mapscene_scene_id)
{
    if( render )
        render->mapscene_scene_id = mapscene_scene_id;
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
    render->baked_bytes = 0;
}

/* =========================================================================
 * Tile shapes
 *
 * Shapes 1..8 (plus 9/10/11, which are 1 and 8 at a turned rotation) are masks
 * over the tile square: which pixels take the overlay colour and which keep the
 * underlay. They are what gives a road or a river bank its diagonal edge.
 *
 * Ported as a table because the reference's is one: each shape/rotation pairs
 * its *own* predicate with its *own* row/column walk order, and rotation 0 is
 * not the identity for most of them (shapes 2..5 start on a reversed row). An
 * earlier version here applied one generic rotation transform to a per-shape
 * predicate, which agrees with the reference only for shape 1 — every diagonal
 * edge in the map came out as sawtooth.
 *
 * Reading a row: mask(r, c) = predicate(rows[r], cols[c]), where rows[r] is r
 * ascending or size-1-r descending. Exactly the reference's makeWithOrder.
 * ========================================================================= */

enum WorldMapShapePredicate
{
    WORLDMAP_PRED_COL_LE_ROW,      /* col <= row */
    WORLDMAP_PRED_COL_GE_ROW,      /* col >= row */
    WORLDMAP_PRED_COL_LE_HALFROW,  /* col <= row >> 1 */
    WORLDMAP_PRED_COL_GE_DBLROW,   /* col >= row << 1 */
    WORLDMAP_PRED_COL_GE_HALFROW,  /* col >= row >> 1 */
    WORLDMAP_PRED_COL_LE_DBLROW,   /* col <= row << 1 */
    WORLDMAP_PRED_COL_LE_HALF,     /* col <= size / 2 */
    WORLDMAP_PRED_ROW_LE_HALF,     /* row <= size / 2 */
    WORLDMAP_PRED_COL_GE_HALF,     /* col >= size / 2 */
    WORLDMAP_PRED_ROW_GE_HALF,     /* row >= size / 2 */
    WORLDMAP_PRED_COL_LE_ROW_HALF, /* col <= row - size / 2 */
    WORLDMAP_PRED_COL_GE_ROW_HALF, /* col >= row - size / 2 */
};

struct WorldMapShapeSpec
{
    uint8_t row_desc;
    uint8_t col_desc;
    uint8_t predicate;
};

/* [shape - 1][rotation], transcribed from WorldMapScaleHandler.initTemplates. */
static const struct WorldMapShapeSpec k_worldmap_shapes[8][4] = {
    /* 1 */
    { { 0, 0, WORLDMAP_PRED_COL_LE_ROW },
      { 1, 0, WORLDMAP_PRED_COL_LE_ROW },
      { 0, 0, WORLDMAP_PRED_COL_GE_ROW },
      { 1, 0, WORLDMAP_PRED_COL_GE_ROW } },
    /* 2 */
    { { 1, 0, WORLDMAP_PRED_COL_LE_HALFROW },
      { 0, 0, WORLDMAP_PRED_COL_GE_DBLROW },
      { 0, 1, WORLDMAP_PRED_COL_LE_HALFROW },
      { 1, 1, WORLDMAP_PRED_COL_GE_DBLROW } },
    /* 3 */
    { { 1, 1, WORLDMAP_PRED_COL_LE_HALFROW },
      { 1, 0, WORLDMAP_PRED_COL_GE_DBLROW },
      { 0, 0, WORLDMAP_PRED_COL_LE_HALFROW },
      { 0, 1, WORLDMAP_PRED_COL_GE_DBLROW } },
    /* 4 */
    { { 1, 0, WORLDMAP_PRED_COL_GE_HALFROW },
      { 0, 0, WORLDMAP_PRED_COL_LE_DBLROW },
      { 0, 1, WORLDMAP_PRED_COL_GE_HALFROW },
      { 1, 1, WORLDMAP_PRED_COL_LE_DBLROW } },
    /* 5 */
    { { 1, 1, WORLDMAP_PRED_COL_GE_HALFROW },
      { 1, 0, WORLDMAP_PRED_COL_LE_DBLROW },
      { 0, 0, WORLDMAP_PRED_COL_GE_HALFROW },
      { 0, 1, WORLDMAP_PRED_COL_LE_DBLROW } },
    /* 6 */
    { { 0, 0, WORLDMAP_PRED_COL_LE_HALF },
      { 0, 0, WORLDMAP_PRED_ROW_LE_HALF },
      { 0, 0, WORLDMAP_PRED_COL_GE_HALF },
      { 0, 0, WORLDMAP_PRED_ROW_GE_HALF } },
    /* 7 */
    { { 0, 0, WORLDMAP_PRED_COL_LE_ROW_HALF },
      { 1, 0, WORLDMAP_PRED_COL_LE_ROW_HALF },
      { 1, 1, WORLDMAP_PRED_COL_LE_ROW_HALF },
      { 0, 1, WORLDMAP_PRED_COL_LE_ROW_HALF } },
    /* 8 */
    { { 0, 0, WORLDMAP_PRED_COL_GE_ROW_HALF },
      { 1, 0, WORLDMAP_PRED_COL_GE_ROW_HALF },
      { 1, 1, WORLDMAP_PRED_COL_GE_ROW_HALF },
      { 0, 1, WORLDMAP_PRED_COL_GE_ROW_HALF } },
};

static bool
worldmap_shape_predicate(
    int predicate,
    int row,
    int col,
    int size)
{
    int half = size / 2;
    switch( predicate )
    {
    case WORLDMAP_PRED_COL_LE_ROW:
        return col <= row;
    case WORLDMAP_PRED_COL_GE_ROW:
        return col >= row;
    case WORLDMAP_PRED_COL_LE_HALFROW:
        return col <= (row >> 1);
    case WORLDMAP_PRED_COL_GE_DBLROW:
        return col >= (row << 1);
    case WORLDMAP_PRED_COL_GE_HALFROW:
        return col >= (row >> 1);
    case WORLDMAP_PRED_COL_LE_DBLROW:
        return col <= (row << 1);
    case WORLDMAP_PRED_COL_LE_HALF:
        return col <= half;
    case WORLDMAP_PRED_ROW_LE_HALF:
        return row <= half;
    case WORLDMAP_PRED_COL_GE_HALF:
        return col >= half;
    case WORLDMAP_PRED_ROW_GE_HALF:
        return row >= half;
    case WORLDMAP_PRED_COL_LE_ROW_HALF:
        return col <= row - half;
    default:
        return col >= row - half;
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
    struct WorldMapShapeSpec const* spec;
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

    spec = &k_worldmap_shapes[shape - 1][rotation & 3];
    return worldmap_shape_predicate(
        spec->predicate,
        spec->row_desc ? size - 1 - row : row,
        spec->col_desc ? size - 1 - col : col,
        size);
}

/* =========================================================================
 * Colours
 * ========================================================================= */

/*
 * Ground colour, from table 20's pre-baked 64x64 image like the reference: one
 * already-blended colour per tile, so terrain types fade into each other the way
 * they do in game. The underlay flo's own flat colour is the fallback for a
 * cache with no ground table (or a region missing from it).
 */
static int
worldmap_underlay_rgb(
    struct CacheProvider* provider,
    struct RSCache_WorldMapGeography const* geography,
    int tile_x,
    int tile_y,
    int underlay_id,
    int background)
{
    struct ToriRS_Flotype* underlay;

    if( underlay_id < 0 )
        return background;
    if( geography->has_ground )
    {
        uint32_t rgb = geography->ground[tile_y * RSCACHE_WORLDMAP_TILE_COUNT + tile_x];
        if( rgb != 0 )
            return (int)(rgb & 0xFFFFFF);
    }
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
    {
        hsl = palette_rgb_to_hsl16(overlay->secondary_rgb_color & 0xFFFFFF);
    }
    else if( overlay->texture >= 0 )
    {
        /* Textured overlays — bridges, piers, jetties, cave floors — carry no
         * colour of their own: the flo names a texture and nothing else, so
         * reading rgb_color here gives 0 and the tile bakes black. The reference
         * takes the texture's average instead (getAverageHsl), which is the
         * value the texture decode already computes for us. */
        struct ToriRS_Texture* texture = CacheProvider_TextureGet(provider, overlay->texture);
        if( texture )
            hsl = texture->average_hsl;
        else
            hsl = palette_rgb_to_hsl16(overlay->rgb_color & 0xFFFFFF);
    }
    else if( (overlay->rgb_color & 0xFFFFFF) == 0xFF00FF )
    {
        return background;
    }
    else
    {
        hsl = palette_rgb_to_hsl16(overlay->rgb_color & 0xFFFFFF);
    }

    return g_hsl16_to_rgb_table[terrain_adjust_lightness(hsl, 96) & 0xFFFF] & 0xFFFFFF;
}

/* =========================================================================
 * Bake
 * ========================================================================= */

/*
 * Nearest-neighbour blit of a map scene sprite into the region image, scaled to
 * the box the reference gives it (2 tiles square, whatever the sprite's own
 * size). Transparent pixels are skipped so the terrain shows through — these are
 * cut-out icons, not tiles.
 */
static void
worldmap_blit_scene_sprite(
    uint32_t* pixels,
    int stride,
    struct ToriDraw_Sprite const* sprite,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h)
{
    int src_w = sprite->width;
    int src_h = sprite->height;

    if( src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 || !sprite->pixels_argb )
        return;

    for( int row = 0; row < dst_h; row++ )
    {
        int src_y = (row * src_h) / dst_h;
        int dst_row = dst_y + row;
        if( dst_row < 0 || dst_row >= stride )
            continue;
        for( int col = 0; col < dst_w; col++ )
        {
            int src_x = (col * src_w) / dst_w;
            int dst_col = dst_x + col;
            uint32_t texel;
            if( dst_col < 0 || dst_col >= stride )
                continue;
            texel = sprite->pixels_argb[(size_t)src_y * src_w + src_x];
            /* Cut-out icons: transparent pixels leave the terrain showing. */
            if( (texel >> 24) == 0 )
                continue;
            pixels[(size_t)dst_row * stride + dst_col] = 0xFF000000u | (texel & 0xFFFFFF);
        }
    }
}

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
    uint32_t* pixels,
    struct RS_WorldMapRegionSlot* slot,
    struct ToriDraw_Sprite** map_scenes,
    int map_scene_count)
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

            int underlay_rgb =
                geography->underlay[index] == 0
                    ? background
                    : worldmap_underlay_rgb(
                          provider, geography, tile_x, tile_y, underlay_id, background);
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

                    /* Reference buildIcons: any loc on the tile can carry a map
                     * element, whatever its shape — that is where nearly every
                     * icon on the map comes from, the compositemap's own list
                     * being only the handful with no loc behind them. */
                    loc = CacheProvider_LocationGet(provider, decor->loc_id);
                    if( loc && loc->map_function_id >= 0 && slot &&
                        slot->icon_count < WORLDMAP_REGION_ICON_MAX )
                    {
                        struct RS_WorldMapRegionIcon* icon = &slot->icons[slot->icon_count];
                        bool duplicate = false;
                        for( int seen = 0; seen < slot->icon_count; seen++ )
                        {
                            if( slot->icons[seen].element_id == loc->map_function_id &&
                                slot->icons[seen].tile_x == tile_x &&
                                slot->icons[seen].tile_y == tile_y )
                            {
                                duplicate = true;
                                break;
                            }
                        }
                        if( !duplicate )
                        {
                            icon->element_id = loc->map_function_id;
                            icon->tile_x = tile_x;
                            icon->tile_y = tile_y;
                            slot->icon_count++;
                        }
                    }

                    /* Map scene: a loc that stands for a landmark (tree, rock,
                     * altar, pier head) draws its icon from the mapscene pack
                     * instead of a wall line — reference drawMapScene, shapes
                     * 10/11 (centrepiece) and 22 (ground decor). */
                    if( (decor->shape >= 10 && decor->shape <= 11) || decor->shape == 22 )
                    {
                        int scene_index = loc ? loc->map_scene_id : -1;
                        if( scene_index >= 0 && scene_index < map_scene_count &&
                            map_scenes[scene_index] )
                        {
                            /* The icon is anchored at the loc's north edge, so a
                             * multi-tile loc needs its own depth: the reference
                             * takes sizeY, or sizeX when the rotation turns it. */
                            int size_tiles =
                                (decor->rotation != 1 && decor->rotation != 3) ? loc->size_z
                                                                              : loc->size_x;
                            if( size_tiles < 1 )
                                size_tiles = 1;
                            worldmap_blit_scene_sprite(
                                pixels,
                                stride,
                                map_scenes[scene_index],
                                tile_x * scale,
                                (WORLDMAP_TILES - size_tiles - tile_y) * scale,
                                scale * 2,
                                scale * 2);
                        }
                        continue;
                    }

                    if( !((decor->shape >= 0 && decor->shape <= 3) || decor->shape == 9) )
                        continue;

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

int
RS_WorldMapRender_RegionIcons(
    struct RS_WorldMapRender* render,
    int scene_id,
    struct RS_WorldMapRegionIcon const** out_icons)
{
    int slot = scene_id & ~WORLDMAP_REGION_SCENE_BASE;

    if( !render || (scene_id & WORLDMAP_REGION_SCENE_BASE) != WORLDMAP_REGION_SCENE_BASE )
        return -1;
    if( slot < 0 || slot >= WORLDMAP_REGION_SLOTS || render->slots[slot].key < 0 )
        return -1;

    *out_icons = render->slots[slot].icons;
    return render->slots[slot].icon_count;
}

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

static size_t
worldmap_slot_bytes(struct RS_WorldMapRegionSlot const* slot)
{
    return (size_t)slot->size * slot->size * sizeof(uint32_t);
}

/*
 * Claim a slot, evicting the least recently drawn one when full — and only when
 * full. Evicting on a frame that still needs the victim is what makes regions
 * flicker, so the pool is sized to hold a whole screen and the byte budget below
 * is what actually reclaims, dropping the oldest until the total fits.
 */
static int
worldmap_slot_claim(
    struct RS_WorldMapRender* render,
    struct ToriDraw_Scene* scene,
    size_t incoming_bytes)
{
    int oldest = 0;
    int claimed = -1;

    for( int i = 0; i < WORLDMAP_REGION_SLOTS; i++ )
    {
        if( render->slots[i].key < 0 )
        {
            claimed = i;
            break;
        }
        if( render->slots[i].stamp < render->slots[oldest].stamp )
            oldest = i;
    }

    if( claimed < 0 )
    {
        claimed = oldest;
        render->baked_bytes -= worldmap_slot_bytes(&render->slots[claimed]);
        render->slots[claimed].key = -1;
    }

    /* Budget eviction: never touch a slot drawn this frame (the caller stamps
     * every visible region before any bake), so this can only reclaim regions
     * that have scrolled away. */
    while( render->baked_bytes + incoming_bytes > WORLDMAP_REGION_BUDGET_BYTES )
    {
        int victim = -1;
        for( int i = 0; i < WORLDMAP_REGION_SLOTS; i++ )
        {
            if( i == claimed || render->slots[i].key < 0 )
                continue;
            if( victim < 0 || render->slots[i].stamp < render->slots[victim].stamp )
                victim = i;
        }
        if( victim < 0 )
            break;
        render->baked_bytes -= worldmap_slot_bytes(&render->slots[victim]);
        render->slots[victim].key = -1;
        if( scene )
            ToriDraw_SceneSpriteAdd(scene, WORLDMAP_REGION_SCENE_BASE | victim, NULL, 0);
    }

    return claimed;
}

/*
 * A region bakes once and is then blitted from, so it must not bake before the
 * floor configs it colours with are resident — a bake that ran early would cache
 * a black region for as long as it stays in the pool. Queue whatever is missing
 * and report it; the caller retries next frame.
 */
/* Loads started per region per frame.
 *
 * A region references a few hundred locs and a couple of dozen floors, and a
 * load in flight is not yet "has" — so re-asking every frame queues the same
 * work again and again. Thirty regions doing that buries the task runner in
 * duplicates and nothing ever finishes: the surface stays black while the queue
 * grows. Starting a few per region per frame keeps the queue draining. */
#define WORLDMAP_LOADS_PER_REGION_FRAME 8

static bool
worldmap_floors_ready(
    struct CacheProvider* provider,
    struct ToriRS_TaskQueue* queue,
    struct RSCache_WorldMapGeography const* geography)
{
    bool ready = true;
    int planes = geography->planes > 0 ? geography->planes : 1;
    int queued = 0;

    /* Locs too: they decide wall colour and carry the map element ids the icon
     * pass reads back, and both are baked in — a loc that arrives after the bake
     * would never show. */
    for( int plane = 0; plane < planes; plane++ )
    {
        for( int i = 0; i < RSCACHE_WORLDMAP_TILE_AREA; i++ )
        {
            struct RSCache_WorldMapDecorList const* list = &geography->decor[plane][i];
            for( int item = 0; item < list->count; item++ )
            {
                int loc_id = list->items[item].loc_id;
                if( loc_id < 0 || CacheProvider_LocationHas(provider, loc_id) )
                    continue;
                ready = false;
                if( queued < WORLDMAP_LOADS_PER_REGION_FRAME )
                {
                    struct ToriRS_Task* task = CreateTask_LocLoad(provider, loc_id);
                    if( task && queue )
                    {
                        ToriRS_TaskQueue_Add(queue, task);
                        queued++;
                    }
                }
            }
        }
    }

    for( int i = 0; i < RSCACHE_WORLDMAP_TILE_AREA; i++ )
    {
        int underlay_id = (int)geography->underlay[i] - 1;
        if( underlay_id < 0 || CacheProvider_UnderlayHas(provider, underlay_id) )
            continue;
        ready = false;
        if( queued < WORLDMAP_LOADS_PER_REGION_FRAME )
        {
            struct ToriRS_Task* task = CreateTask_UnderlayLoad(provider, underlay_id);
            if( task && queue )
            {
                ToriRS_TaskQueue_Add(queue, task);
                queued++;
            }
        }
    }

    for( int plane = 0; plane < planes; plane++ )
    {
        for( int i = 0; i < RSCACHE_WORLDMAP_TILE_AREA; i++ )
        {
            int overlay_id = (int)geography->overlay[plane][i] - 1;
            struct ToriRS_Flotype* overlay;
            if( overlay_id < 0 )
                continue;
            if( !CacheProvider_FlotypeHas(provider, overlay_id) )
            {
                ready = false;
                if( queued < WORLDMAP_LOADS_PER_REGION_FRAME )
                {
                    struct ToriRS_Task* task = CreateTask_FlotypeLoad(provider, overlay_id);
                    if( task && queue )
                    {
                        ToriRS_TaskQueue_Add(queue, task);
                        queued++;
                    }
                }
                continue;
            }
            /* A textured overlay is coloured from its texture's average, so the
             * texture has to be resident before the bake, same as the flo. */
            overlay = CacheProvider_FlotypeGet(provider, overlay_id);
            if( !overlay || overlay->texture < 0 ||
                CacheProvider_TextureHas(provider, overlay->texture) )
                continue;
            ready = false;
            if( queued < WORLDMAP_LOADS_PER_REGION_FRAME )
            {
                struct ToriRS_Task* task = CreateTask_TextureLoad(provider, overlay->texture);
                if( task && queue )
                {
                    ToriRS_TaskQueue_Add(queue, task);
                    queued++;
                }
            }
        }
    }

    return ready;
}

/**
 * Should this region keep waiting for its floors? Counts the waits per region
 * and gives up after WORLDMAP_FLOOR_WAIT_FRAMES, baking with whatever loaded —
 * the tiles whose floor never arrived draw as background.
 */
static bool
worldmap_wait_for_floors(
    struct RS_WorldMapRender* render,
    int key)
{
    int free_slot = -1;

    for( int i = 0; i < WORLDMAP_REGION_SLOTS; i++ )
    {
        if( render->pending[i].key == key )
        {
            render->pending[i].attempts++;
            if( render->pending[i].attempts < WORLDMAP_FLOOR_WAIT_FRAMES )
                return true;
            render->pending[i].key = -1;
            return false;
        }
        if( free_slot < 0 && render->pending[i].key < 0 )
            free_slot = i;
    }

    if( free_slot >= 0 )
    {
        render->pending[free_slot].key = key;
        render->pending[free_slot].attempts = 1;
    }
    return true;
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
    int key;
    int slot;
    int size;
    bool complete = false;
    int source_count = 0;
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
        /* Complete, or out of budget this frame: draw what is there. An
         * incomplete region kept its tiles precisely so this can bake again
         * without re-reading the cache — re-requesting them every frame would
         * flood the queue and starve the regions a pan just exposed. */
        if( render->slots[slot].complete ||
            render->bakes_this_frame >= WORLDMAP_BAKES_PER_FRAME )
            return WORLDMAP_REGION_SCENE_BASE | slot;
        geography = CacheProvider_WorldMapGeographyGet(provider, key);
        if( !geography )
        {
            /* Should not happen (an incomplete bake holds its tiles), but if the
             * cache dropped them there is nothing to re-bake from: stop asking. */
            render->slots[slot].complete = true;
            return WORLDMAP_REGION_SCENE_BASE | slot;
        }
        if( !worldmap_floors_ready(provider, queue, geography) &&
            worldmap_wait_for_floors(render, key) )
            return WORLDMAP_REGION_SCENE_BASE | slot;
        /* Assets are in (or the wait ran out): bake over the same slot. */
    }

    if( slot < 0 )
    {
        source_count = worldmap_region_sources(
            area, region_x, region_y, sources, (int)(sizeof(sources) / sizeof(sources[0])));
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

        complete = worldmap_floors_ready(provider, queue, geography);
        if( !complete && worldmap_wait_for_floors(render, key) )
            return -1;

        /* Zooming out asks for hundreds of regions at once. Baking them all in
         * one frame freezes the client for seconds, so only a few start per
         * frame and the rest come in over the next ones. */
        if( render->bakes_this_frame >= WORLDMAP_BAKES_PER_FRAME )
            return -1;
    }
    else
    {
        complete = true; /* the resident-slot path above only falls through when ready */
    }
    render->bakes_this_frame++;

    size = WORLDMAP_TILES * pixels_per_tile;
    if( slot < 0 )
        slot = worldmap_slot_claim(render, scene, (size_t)size * size * sizeof(uint32_t));
    else
        render->baked_bytes -= worldmap_slot_bytes(&render->slots[slot]);
    render->slots[slot].icon_count = 0;

    pixels = calloc((size_t)size * size, sizeof(*pixels));
    assert(pixels);
    {
        struct ToriDraw_Sprite** map_scenes = NULL;
        int map_scene_count = 0;
        if( render->mapscene_scene_id > 0 )
            map_scenes =
                ToriDraw_SceneSpriteGet(scene, render->mapscene_scene_id, &map_scene_count);
        worldmap_bake_region(
            provider,
            geography,
            area->background_colour & 0xFFFFFF,
            pixels_per_tile,
            pixels,
            &render->slots[slot],
            map_scenes,
            map_scenes ? map_scene_count : 0);
    }

    /* A complete bake has no further use for the tiles, and holding both would
     * keep a quarter-megabyte per region alive. An incomplete one keeps them:
     * it is going to bake again as soon as its floors and locs arrive, and a
     * second trip through the cache for the same region is what made a pan
     * starve. */
    if( complete )
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

    /* SceneSpriteAdd frees whatever the slot held, so an evicted region needs no
     * separate teardown. */
    ToriDraw_SceneSpriteAdd(scene, WORLDMAP_REGION_SCENE_BASE | slot, sprite_list, 1);
    render->slots[slot].key = key;
    render->slots[slot].scale = pixels_per_tile;
    render->slots[slot].size = size;
    render->slots[slot].stamp = ++render->clock;
    render->slots[slot].complete = complete;
    render->baked_bytes += worldmap_slot_bytes(&render->slots[slot]);

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
