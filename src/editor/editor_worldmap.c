#include "editor/editor_worldmap.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A shown square with no colour of its own (no floors), where no map is:
 * the picker's own empty-cell grey. */
#define WORLDMAP_SQUARE_RGB 0x4a463c

void
Editor_WorldMapInit(struct Editor_WorldMap* map)
{
    assert(map);
    memset(map, 0, sizeof(*map));
    map->area_id = EDITOR_WORLDMAP_AREA_UNASKED;
    map->pixels = ToriMem_Alloc(ToriMem_Editor(), sizeof(uint32_t) * EDITOR_WORLDMAP_TILE * EDITOR_WORLDMAP_TILE);
    assert(map->pixels);
    map->icons = ToriMem_Calloc(ToriMem_Editor(), EDITOR_WORLDMAP_ICONS_MAX, sizeof(*map->icons));
    assert(map->icons);
}

void
Editor_WorldMapFree(struct Editor_WorldMap* map)
{
    if( !map )
        return;
    ToriMem_Free(ToriMem_Editor(), map->pixels);
    ToriMem_Free(ToriMem_Editor(), map->shown);
    ToriMem_Free(ToriMem_Editor(), map->icons);
    map->pixels = NULL;
    map->shown = NULL;
    map->icons = NULL;
    map->shown_cap = 0;
}

int
Editor_WorldMapAvailable(const struct EditorUiAppServices* services)
{
    assert(services);
    return services->worldmap_area && services->worldmap_poll && services->worldmap_render_tile
           && services->image_publish;
}

int
Editor_WorldMapFade(
    double zoom,
    double from,
    double full)
{
    double t;

    if( zoom >= full )
        return 0;
    if( zoom <= from )
        return 255;
    t = (zoom - from) / (full - from);
    return (int)(255.0 * (1.0 - t));
}

/* ---- the picture ------------------------------------------------------------ */

static uint32_t
worldmap_scale(
    uint32_t argb,
    int num)
{
    uint32_t const r = ((argb >> 16) & 0xFF) * (uint32_t)num / 256u;
    uint32_t const g = ((argb >> 8) & 0xFF) * (uint32_t)num / 256u;
    uint32_t const b = (argb & 0xFF) * (uint32_t)num / 256u;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void
Editor_WorldMapComposeTile(
    uint32_t* argb,
    int size,
    double zoom,
    int gx0,
    int gy0,
    int grid,
    int sx0,
    int r0,
    int sw,
    int sh,
    const uint32_t* cells)
{
    int col_sq[EDITOR_WORLDMAP_TILE];
    int col_edge[EDITOR_WORLDMAP_TILE];

    assert(argb);
    assert(cells || sw == 0 || sh == 0);
    assert(size > 0);
    assert(size <= EDITOR_WORLDMAP_TILE);
    assert(zoom > 0);

    for( int c = 0; c < size; c++ )
    {
        col_sq[c] = Editor_PickerSquareOfPixel(zoom, gx0 + c);
        col_edge[c] = Editor_PickerEdge(zoom, col_sq[c]) == gx0 + c;
    }
    for( int row = 0; row < size; row++ )
    {
        int const r = Editor_PickerSquareOfPixel(zoom, gy0 + row);
        int const row_edge = Editor_PickerEdge(zoom, r) == gy0 + row;
        uint32_t* line = &argb[(size_t)row * (size_t)size];
        for( int c = 0; c < size; c++ )
        {
            uint32_t p = line[c];
            int const edge = grid && (row_edge || col_edge[c]);
            uint32_t cell = 0xFF000000u | WORLDMAP_SQUARE_RGB;
            int on_page = 1;
            if( col_sq[c] >= sx0 && col_sq[c] < sx0 + sw && r >= r0 && r < r0 + sh )
            {
                cell = cells[(r - r0) * sw + (col_sq[c] - sx0)];
                on_page = cell != 0;
            }
            if( (p >> 24) == 0 )
            {
                /* No map here: a square the page shows in its own colour,
                 * anything else black. */
                if( on_page )
                    line[c] = edge ? worldmap_scale(cell, 190) : cell;
                else
                    line[c] = edge ? 0xFF1C1C1Cu : 0xFF000000u;
                continue;
            }
            if( !on_page )
                p = worldmap_scale(p, 100);
            if( edge )
                p = worldmap_scale(p, 190);
            line[c] = p;
        }
    }
}

/* ---- deciding ---------------------------------------------------------------- */

static uint32_t
worldmap_content(const struct Editor_Picker* picker)
{
    uint32_t hash = 2166136261u;
    int const fields[4] = { (int)picker->atlas_serial, picker->active_tab, picker->storage_filter,
        picker->filter_on };
    for( int i = 0; i < 4; i++ )
        hash = (hash ^ (uint32_t)fields[i]) * 16777619u;
    return hash;
}

/** The close-up bake the App draws a zoom from (RS_WorldMapOverview_
 *  BakeScale): a new one needs a poll. */
static int
worldmap_bake_scale(double zoom)
{
    int scale = (int)ceil(zoom / 64.0 - 1e-9);
    return scale < 1 ? 1 : scale > 8 ? 8 : scale;
}

static int
worldmap_floor_div(
    int a,
    int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static void
worldmap_clear(
    struct Editor_WorldMap* map,
    struct Editor_Picker* picker,
    int* changed)
{
    if( picker->underlay.active || picker->underlay.count || picker->underlay.icon_count
        || picker->underlay.label_count )
        *changed = 1;
    memset(&picker->underlay, 0, sizeof(picker->underlay));
    map->polled_valid = 0;
    map->missing = 0;
}

/** The kept tile (area, zoom, tx, ty), or -1. */
static int
worldmap_find(
    const struct Editor_WorldMap* map,
    int area_id,
    double zoom,
    int tx,
    int ty)
{
    for( int i = 0; i < EDITOR_UI_MAP_IMAGE_SLOTS; i++ )
    {
        const struct Editor_WorldMapTile* tile = &map->tiles[i];
        if( tile->used && tile->area_id == area_id && tile->zoom == zoom && tile->tx == tx && tile->ty == ty )
            return i;
    }
    return -1;
}

/** A slot for a new tile: a free one, else the least recently drawn that
 *  this tick has not drawn; -1 when every slot is in this tick's view. */
static int
worldmap_claim(const struct Editor_WorldMap* map)
{
    int best = -1;
    for( int i = 0; i < EDITOR_UI_MAP_IMAGE_SLOTS; i++ )
    {
        const struct Editor_WorldMapTile* tile = &map->tiles[i];
        if( !tile->used )
            return i;
        if( tile->stamp == map->tick )
            continue;
        if( best < 0 || tile->stamp < map->tiles[best].stamp )
            best = i;
    }
    return best;
}

/** Render tile (tx, ty) of the current zoom into slot `slot`. */
static void
worldmap_render_tile(
    struct Editor_WorldMap* map,
    const struct EditorUiAppServices* services,
    const struct Editor_Picker* picker,
    int slot,
    int tx,
    int ty,
    uint32_t content)
{
    int const size = EDITOR_WORLDMAP_TILE;
    int const gx0 = tx * size;
    int const gy0 = ty * size;
    double const zoom = picker->zoom;
    struct Editor_WorldMapTile* tile = &map->tiles[slot];
    int sx0;
    int sx1;
    int r0;
    int r1;
    int sw;
    int sh;
    int scene_id;

    map->renders++;
    if( !services->worldmap_render_tile(services->user_data, map->area_id, zoom, gx0, gy0, size, map->pixels) )
    {
        /* The area went (a cache without it): remembered, not asked again. */
        map->area_id = EDITOR_WORLDMAP_AREA_NONE;
        return;
    }

    /* Which of the tile's squares the page shows. */
    sx0 = Editor_PickerSquareOfPixel(zoom, gx0);
    sx1 = Editor_PickerSquareOfPixel(zoom, gx0 + size - 1);
    r0 = Editor_PickerSquareOfPixel(zoom, gy0);
    r1 = Editor_PickerSquareOfPixel(zoom, gy0 + size - 1);
    sw = sx1 - sx0 + 1;
    sh = r1 - r0 + 1;
    if( (size_t)sw * (size_t)sh > map->shown_cap )
    {
        ToriMem_Free(ToriMem_Editor(), map->shown);
        map->shown_cap = (size_t)sw * (size_t)sh;
        map->shown = ToriMem_Alloc(ToriMem_Editor(), map->shown_cap * sizeof(uint32_t));
        assert(map->shown);
    }
    memset(map->shown, 0, sizeof(uint32_t) * (size_t)sw * (size_t)sh);
    for( int i = 0; i < picker->square_count; i++ )
    {
        const struct Editor_PickerSquare* square = &picker->squares[i];
        int const r = EDITOR_PICKER_WORLD_SQUARES - 1 - square->map_z;
        if( square->map_x < sx0 || square->map_x > sx1 || r < r0 || r > r1 )
            continue;
        /* Its own colour where no area's map covers it. */
        if( Editor_PickerSquareVisible(picker, i) )
            map->shown[(r - r0) * sw + (square->map_x - sx0)] =
                0xFF000000u | (square->rgb ? square->rgb & 0xFFFFFF : WORLDMAP_SQUARE_RGB);
    }
    Editor_WorldMapComposeTile(
        map->pixels, size, zoom, gx0, gy0, zoom >= EDITOR_WORLDMAP_GRID_CELL, sx0, r0, sw, sh, map->shown);
    scene_id = services->image_publish(services->user_data, slot, map->pixels, size, size);
    tile->used = scene_id > 0;
    tile->area_id = map->area_id;
    tile->zoom = zoom;
    tile->tx = tx;
    tile->ty = ty;
    tile->content = content;
    tile->serial = map->serial;
    tile->stale = map->missing > 0;
    tile->scene_id = scene_id;
    tile->version++;
    tile->stamp = map->tick;
}

/** What one tile costs to render: the source tiles it visits. */
static long
worldmap_tile_cost(double zoom)
{
    double const squares = (double)EDITOR_WORLDMAP_TILE / zoom + 1.0;
    double const cost = squares * squares * 4096.0;
    double const pixels = (double)EDITOR_WORLDMAP_TILE * EDITOR_WORLDMAP_TILE;
    return (long)(cost > pixels ? cost : pixels);
}

struct WorldMapWant
{
    int tx;
    int ty;
    int slot;
    long distance;
};

static int
worldmap_want_cmp(
    const void* a,
    const void* b)
{
    const struct WorldMapWant* wa = a;
    const struct WorldMapWant* wb = b;
    if( wa->distance != wb->distance )
        return wa->distance < wb->distance ? -1 : 1;
    if( wa->ty != wb->ty )
        return wa->ty < wb->ty ? -1 : 1;
    return wa->tx < wb->tx ? -1 : wa->tx > wb->tx;
}

static void
worldmap_push_image(
    struct Editor_PickerUnderlay* next,
    const struct Editor_WorldMapTile* tile,
    int x,
    int y,
    int w,
    int h)
{
    struct Editor_PickerImage* image;
    if( next->count >= EDITOR_PICKER_MAP_IMAGES_MAX || w <= 0 || h <= 0 )
        return;
    image = &next->items[next->count++];
    image->scene_id = tile->scene_id;
    image->x = x;
    image->y = y;
    image->w = w;
    image->h = h;
    image->trans = 0;
    image->version = tile->version;
}

/** Icons and labels of the visible squares, placed at the zoom. */
static void
worldmap_place_icons(
    struct Editor_WorldMap* map,
    const struct EditorUiAppServices* services,
    const struct Editor_Picker* picker,
    int vx0,
    int vz0,
    int vw,
    int vh,
    struct Editor_PickerUnderlay* next)
{
    double const zoom = picker->zoom;
    double const k = zoom / 64.0;
    int gox;
    int goy;
    int const icon_trans = Editor_WorldMapFade(zoom, EDITOR_WORLDMAP_ICON_FROM, EDITOR_WORLDMAP_ICON_FULL);

    if( !services->worldmap_icons || zoom < EDITOR_WORLDMAP_LABEL_LARGE_FROM )
        return;
    /* Asked once per window, again while some were cold. */
    if( !map->icons_valid || map->icons_area != map->area_id || map->icons_x0 != vx0 || map->icons_z0 != vz0
        || map->icons_w != vw || map->icons_h != vh || (map->icons_pending > 0 && --map->icons_wait <= 0) )
    {
        int pending = 0;
        map->icon_count = services->worldmap_icons(
            services->user_data, map->area_id, vx0, vz0, vw, vh, map->icons, EDITOR_WORLDMAP_ICONS_MAX, &pending);
        map->icon_asks++;
        map->icons_valid = 1;
        map->icons_area = map->area_id;
        map->icons_x0 = vx0;
        map->icons_z0 = vz0;
        map->icons_w = vw;
        map->icons_h = vh;
        map->icons_pending = pending;
        map->icons_wait = EDITOR_WORLDMAP_ICON_RETRY_TICKS;
    }

    Editor_PickerGlobalOrigin(picker, &gox, &goy);
    for( int i = 0; i < map->icon_count; i++ )
    {
        const struct EditorUiMapIcon* icon = &map->icons[i];
        /* The middle of its tile, in well px. */
        int const cx = (int)floor(((double)icon->tile_x + 0.5) * k) - gox;
        int const cy = (int)floor(((double)(EDITOR_PICKER_WORLD_SQUARES * 64 - 1 - icon->tile_z) + 0.5) * k) - goy;

        if( icon->scene_id > 0 )
        {
            struct Editor_PickerImage* image;
            if( icon_trans >= 255 || next->icon_count >= EDITOR_PICKER_MAP_ICONS_MAX )
                continue;
            if( cx + icon->w < EDITOR_PICKER_LEFT || cy + icon->h < EDITOR_PICKER_TOP
                || cx - icon->w > picker->well_w || cy - icon->h > picker->well_h )
                continue;
            image = &next->icons[next->icon_count++];
            image->scene_id = icon->scene_id;
            image->w = icon->w;
            image->h = icon->h;
            /* Centred on its tile, like every icon in the reference. */
            image->x = cx - icon->w / 2;
            image->y = cy - icon->h / 2;
            image->trans = icon_trans;
            image->version = 0;
        }
        else if( icon->label[0] )
        {
            double const from = icon->text_size >= 2   ? EDITOR_WORLDMAP_LABEL_LARGE_FROM
                                : icon->text_size == 1 ? EDITOR_WORLDMAP_LABEL_MEDIUM_FROM
                                                       : EDITOR_WORLDMAP_LABEL_SMALL_FROM;
            int const fade = Editor_WorldMapFade(zoom, from, from * 1.6);
            int const width = (int)strlen(icon->label) * 6;
            struct Editor_PickerLabel* label;
            uint32_t level;
            if( fade >= 255 || next->label_count >= EDITOR_PICKER_MAP_LABELS_MAX )
                continue;
            if( cx + width / 2 < EDITOR_PICKER_LEFT || cx - width / 2 > picker->well_w || cy < EDITOR_PICKER_TOP
                || cy - 12 > picker->well_h )
                continue;
            /* A fade without alpha: the label's grey rises to white. */
            level = (uint32_t)(255 - fade);
            label = &next->labels[next->label_count++];
            label->x = cx - width / 2;
            label->y = cy + 4;
            label->color = (level << 16) | (level << 8) | level;
            snprintf(label->text, sizeof(label->text), "%s", icon->label);
        }
    }
}

int
Editor_WorldMapUpdate(
    struct Editor_WorldMap* map,
    const struct EditorUiAppServices* services,
    struct Editor_Picker* picker)
{
    struct Editor_PickerUnderlay* next = &map->next;
    struct WorldMapWant wants[EDITOR_UI_MAP_IMAGE_SLOTS];
    int want_count = 0;
    int all_in = 1;
    const char* name;
    uint32_t content;
    int changed = 0;
    int vx0;
    int vz0;
    int vw;
    int vh;
    int area;
    int gox;
    int goy;
    int tx0;
    int tx1;
    int ty0;
    int ty1;

    assert(map);
    assert(services);
    assert(picker);

    map->tick++;
    if( !Editor_WorldMapAvailable(services) || picker->tab_count == 0
        || picker->tabs[picker->active_tab].area == EDITOR_PICKER_TAB_UNMAPPED )
    {
        /* No map for this page: the Unmapped page has no area. */
        worldmap_clear(map, picker, &changed);
        return changed;
    }

    /* The page's area, asked once per page (and again while the cache's
     * areas are still loading). */
    /* The Full map asks for every area at once. */
    area = picker->tabs[picker->active_tab].area;
    name = area == EDITOR_PICKER_TAB_FULL ? "*" : picker->areas[area].internal_name;
    if( strcmp(name, map->area_name) != 0 )
    {
        snprintf(map->area_name, sizeof(map->area_name), "%s", name);
        map->area_id = EDITOR_WORLDMAP_AREA_UNASKED;
        map->area_retry = 0;
    }
    if( map->area_id == EDITOR_WORLDMAP_AREA_UNASKED
        || (map->area_id == EDITOR_WORLDMAP_AREA_PENDING && --map->area_retry <= 0) )
    {
        map->area_id = services->worldmap_area(services->user_data, name);
        if( map->area_id == EDITOR_WORLDMAP_AREA_PENDING )
            map->area_retry = EDITOR_WORLDMAP_AREA_RETRY_TICKS;
    }
    if( map->area_id < 0 )
    {
        if( map->area_id == EDITOR_WORLDMAP_AREA_PENDING && services->request_redraw )
            services->request_redraw(services->user_data);
        worldmap_clear(map, picker, &changed);
        return changed;
    }
    if( !Editor_PickerVisibleSquares(picker, &vx0, &vz0, &vw, &vh) )
    {
        worldmap_clear(map, picker, &changed);
        return changed;
    }

    /* Poll the visible squares as soon as they change, then every tick
     * while regions are still arriving. */
    if( !map->polled_valid || map->polled_area != map->area_id || map->polled_x0 != vx0 || map->polled_z0 != vz0
        || map->polled_w != vw || map->polled_h != vh || map->polled_scale != worldmap_bake_scale(picker->zoom)
        || (map->missing > 0 && --map->poll_wait <= 0) )
    {
        int missing = 0;
        int const serial = services->worldmap_poll(
            services->user_data, map->area_id, vx0, vz0, vw, vh, picker->zoom, &missing);
        map->polls++;
        if( serial < 0 )
        {
            map->area_id = EDITOR_WORLDMAP_AREA_NONE;
            worldmap_clear(map, picker, &changed);
            return changed;
        }
        map->serial = serial;
        map->missing = missing;
        map->polled_valid = 1;
        map->polled_area = map->area_id;
        map->polled_x0 = vx0;
        map->polled_z0 = vz0;
        map->polled_w = vw;
        map->polled_h = vh;
        map->polled_scale = worldmap_bake_scale(picker->zoom);
        map->poll_wait = EDITOR_WORLDMAP_POLL_TICKS;
    }

    /* The tiles the well shows at this zoom. */
    content = worldmap_content(picker);
    Editor_PickerGlobalOrigin(picker, &gox, &goy);
    tx0 = worldmap_floor_div(gox + EDITOR_PICKER_LEFT, EDITOR_WORLDMAP_TILE);
    tx1 = worldmap_floor_div(gox + picker->well_w - 1, EDITOR_WORLDMAP_TILE);
    ty0 = worldmap_floor_div(goy + EDITOR_PICKER_TOP, EDITOR_WORLDMAP_TILE);
    ty1 = worldmap_floor_div(goy + picker->well_h - 1, EDITOR_WORLDMAP_TILE);
    {
        int const mid_x = gox + (EDITOR_PICKER_LEFT + picker->well_w) / 2;
        int const mid_y = goy + (EDITOR_PICKER_TOP + picker->well_h) / 2;
        int const redraw_stale = map->tick - map->last_redraw_tick >= EDITOR_WORLDMAP_LOADING_TICKS;
        for( int ty = ty0; ty <= ty1; ty++ )
            for( int tx = tx0; tx <= tx1 && want_count < EDITOR_UI_MAP_IMAGE_SLOTS; tx++ )
            {
                int const slot = worldmap_find(map, map->area_id, picker->zoom, tx, ty);
                long const dx = (long)tx * EDITOR_WORLDMAP_TILE + EDITOR_WORLDMAP_TILE / 2 - mid_x;
                long const dy = (long)ty * EDITOR_WORLDMAP_TILE + EDITOR_WORLDMAP_TILE / 2 - mid_y;
                if( slot >= 0 )
                {
                    struct Editor_WorldMapTile* tile = &map->tiles[slot];
                    tile->stamp = map->tick;
                    /* Kept and current: nothing to do. A tile drawn for another
                     * page, or before its regions landed, is drawn again (and
                     * shown as it is meanwhile). */
                    if( tile->content == content
                        && !(tile->stale && tile->serial != map->serial && redraw_stale) )
                        continue;
                }
                else
                    all_in = 0;
                wants[want_count].tx = tx;
                wants[want_count].ty = ty;
                wants[want_count].slot = slot;
                wants[want_count].distance = dx * dx + dy * dy;
                want_count++;
            }
    }

    /* Render within the budget, nearest the middle first. */
    if( want_count > 0 )
    {
        long spent = 0;
        long const cost = worldmap_tile_cost(picker->zoom);
        int rendered_stale = 0;
        qsort(wants, (size_t)want_count, sizeof(wants[0]), worldmap_want_cmp);
        for( int i = 0; i < want_count && map->area_id >= 0; i++ )
        {
            int slot = wants[i].slot;
            if( spent > 0 && spent + cost > EDITOR_WORLDMAP_RENDER_BUDGET )
                break;
            if( slot < 0 )
                slot = worldmap_claim(map);
            if( slot < 0 )
                break;
            if( map->tiles[slot].used && map->tiles[slot].tx == wants[i].tx && map->tiles[slot].ty == wants[i].ty
                && map->tiles[slot].zoom == picker->zoom )
                rendered_stale = 1;
            worldmap_render_tile(map, services, picker, slot, wants[i].tx, wants[i].ty, content);
            spent += cost;
        }
        if( rendered_stale )
            map->last_redraw_tick = map->tick;
        if( map->area_id < 0 )
        {
            worldmap_clear(map, picker, &changed);
            return changed;
        }
    }

    /* The picture: the stand-in zoom scaled under this zoom's tiles while
     * any of this zoom's are missing, then this zoom's. */
    memset(next, 0, sizeof(*next));
    next->active = 1;
    all_in = 1;
    for( int ty = ty0; ty <= ty1 && all_in; ty++ )
        for( int tx = tx0; tx <= tx1 && all_in; tx++ )
            if( worldmap_find(map, map->area_id, picker->zoom, tx, ty) < 0 )
                all_in = 0;
    if( all_in )
        map->shown_zoom = picker->zoom;
    else if( map->shown_zoom > 0 )
    {
        double const ratio = picker->zoom / map->shown_zoom;
        for( int i = 0; i < EDITOR_UI_MAP_IMAGE_SLOTS; i++ )
        {
            struct Editor_WorldMapTile* tile = &map->tiles[i];
            int x;
            int y;
            int x1;
            int y1;
            if( !tile->used || tile->area_id != map->area_id || tile->zoom != map->shown_zoom )
                continue;
            x = (int)floor((double)(tile->tx * EDITOR_WORLDMAP_TILE) * ratio) - gox;
            y = (int)floor((double)(tile->ty * EDITOR_WORLDMAP_TILE) * ratio) - goy;
            x1 = (int)floor((double)((tile->tx + 1) * EDITOR_WORLDMAP_TILE) * ratio) - gox;
            y1 = (int)floor((double)((tile->ty + 1) * EDITOR_WORLDMAP_TILE) * ratio) - goy;
            if( x1 <= EDITOR_PICKER_LEFT || y1 <= EDITOR_PICKER_TOP || x >= picker->well_w || y >= picker->well_h )
                continue;
            worldmap_push_image(next, tile, x, y, x1 - x, y1 - y);
        }
    }
    for( int ty = ty0; ty <= ty1; ty++ )
        for( int tx = tx0; tx <= tx1; tx++ )
        {
            int const slot = worldmap_find(map, map->area_id, picker->zoom, tx, ty);
            if( slot < 0 )
                continue;
            worldmap_push_image(next, &map->tiles[slot], tx * EDITOR_WORLDMAP_TILE - gox,
                ty * EDITOR_WORLDMAP_TILE - goy, EDITOR_WORLDMAP_TILE, EDITOR_WORLDMAP_TILE);
        }
    worldmap_place_icons(map, services, picker, vx0, vz0, vw, vh, next);

    if( (map->missing > 0 || !all_in || map->icons_pending > 0) && services->request_redraw )
        services->request_redraw(services->user_data);

    if( memcmp(next, &picker->underlay, sizeof(*next)) != 0 )
    {
        memcpy(&picker->underlay, next, sizeof(*next));
        changed = 1;
    }
    return changed;
}
