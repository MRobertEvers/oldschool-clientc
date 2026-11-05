#include "editor/editor_worldmap.h"

#include "log/torirs_log.h"
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
    /* What comes next is a cold open again: the middle first. */
    map->full_valid = 0;
    map->core_valid = 0;
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
    struct Editor_PickerLod lod;

    /* Icons and names belong to the closest band only: below it a crowd of
     * icons is what hid the map (the design's "icons wait"). */
    Editor_PickerLodAt(zoom, &lod);
    if( !services->worldmap_icons || !(lod.icons || lod.names) )
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
            if( !lod.icons || next->icon_count >= EDITOR_PICKER_MAP_ICONS_MAX )
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
            image->trans = 0;
            image->version = 0;
        }
        else if( icon->label[0] )
        {
            double const from = icon->text_size >= 2   ? EDITOR_WORLDMAP_LABEL_LARGE_FROM
                                : icon->text_size == 1 ? EDITOR_WORLDMAP_LABEL_MEDIUM_FROM
                                                       : EDITOR_WORLDMAP_LABEL_SMALL_FROM;
            int const fade = Editor_WorldMapFade(zoom, from * 0.75, from);
            int const width = (int)strlen(icon->label) * 6;
            struct Editor_PickerLabel* label;
            uint32_t level;
            if( !lod.names || fade >= 255 || next->label_count >= EDITOR_PICKER_MAP_LABELS_MAX )
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

/* ---- visible first ------------------------------------------------------------------- */

void
Editor_WorldMapCore(
    int x0,
    int z0,
    int w,
    int h,
    int* out_x0,
    int* out_z0,
    int* out_w,
    int* out_h)
{
    assert(w > 0);
    assert(h > 0);
    assert(out_x0);
    assert(out_z0);
    assert(out_w);
    assert(out_h);
    *out_w = w / 2 > 0 ? w / 2 : 1;
    *out_h = h / 2 > 0 ? h / 2 : 1;
    *out_x0 = x0 + (w - *out_w) / 2;
    *out_z0 = z0 + (h - *out_h) / 2;
}

int
Editor_WorldMapNextSlice(
    const uint32_t* slices,
    uint32_t tick,
    int min_x,
    int max_x,
    int min_z,
    int max_z,
    int vx0,
    int vz0,
    int vw,
    int vh)
{
    int best = -1;
    long best_distance = 0;
    int sx0;
    int sx1;
    int sz0;
    int sz1;
    /* Twice the window's middle, so the distances stay whole. */
    long const mid_x2 = 2L * vx0 + vw;
    long const mid_z2 = 2L * vz0 + vh;

    assert(slices);
    if( min_x > max_x || min_z > max_z )
        return -1;
    sx0 = min_x < 0 ? 0 : min_x / EDITOR_WORLDMAP_SLICE;
    sz0 = min_z < 0 ? 0 : min_z / EDITOR_WORLDMAP_SLICE;
    sx1 = max_x / EDITOR_WORLDMAP_SLICE;
    sz1 = max_z / EDITOR_WORLDMAP_SLICE;
    if( sx1 >= EDITOR_WORLDMAP_SLICES )
        sx1 = EDITOR_WORLDMAP_SLICES - 1;
    if( sz1 >= EDITOR_WORLDMAP_SLICES )
        sz1 = EDITOR_WORLDMAP_SLICES - 1;
    for( int sz = sz0; sz <= sz1; sz++ )
        for( int sx = sx0; sx <= sx1; sx++ )
        {
            uint32_t const asked = slices[sx + sz * EDITOR_WORLDMAP_SLICES];
            int const x = sx * EDITOR_WORLDMAP_SLICE;
            int const z = sz * EDITOR_WORLDMAP_SLICE;
            long dx;
            long dz;
            long distance;
            if( asked == EDITOR_WORLDMAP_SLICE_DONE )
                continue;
            if( asked != 0 && tick - asked < EDITOR_WORLDMAP_PREFETCH_RETRY_TICKS )
                continue;
            /* Wholly in view: the visible poll has it. */
            if( x >= vx0 && x + EDITOR_WORLDMAP_SLICE <= vx0 + vw && z >= vz0
                && z + EDITOR_WORLDMAP_SLICE <= vz0 + vh )
                continue;
            dx = 2L * x + EDITOR_WORLDMAP_SLICE - mid_x2;
            dz = 2L * z + EDITOR_WORLDMAP_SLICE - mid_z2;
            distance = dx * dx + dz * dz;
            if( best < 0 || distance < best_distance )
            {
                best = sx + sz * EDITOR_WORLDMAP_SLICES;
                best_distance = distance;
            }
        }
    return best;
}

/** Whether window (x0, z0, w, h) shares at least half its squares with the
 *  last whole window polled: a pan (the edges come with the whole window)
 *  rather than a jump (the middle first). */
static int
worldmap_overlaps_last(
    const struct Editor_WorldMap* map,
    int x0,
    int z0,
    int w,
    int h)
{
    int ox;
    int oz;
    if( !map->full_valid )
        return 0;
    ox = (x0 + w < map->full_x0 + map->full_w ? x0 + w : map->full_x0 + map->full_w)
         - (x0 > map->full_x0 ? x0 : map->full_x0);
    oz = (z0 + h < map->full_z0 + map->full_h ? z0 + h : map->full_z0 + map->full_h)
         - (z0 > map->full_z0 ? z0 : map->full_z0);
    if( ox <= 0 || oz <= 0 )
        return 0;
    return 2L * ox * oz >= (long)w * h;
}

/** One background slice of the page, when the view is in and the frame has
 *  room: at one pixel a tile (no close-up bakes for what is not seen). */
static void
worldmap_prefetch(
    struct Editor_WorldMap* map,
    const struct EditorUiAppServices* services,
    const struct Editor_Picker* picker,
    uint32_t content,
    int vx0,
    int vz0,
    int vw,
    int vh)
{
    const struct Editor_PickerTab* tab = &picker->tabs[picker->active_tab];
    uint64_t t0 = 0;
    int missing = 0;
    int slice;
    int serial;

    if( !map->slices_valid || map->slices_area != map->area_id || map->slices_content != content )
    {
        memset(map->slices, 0, sizeof(map->slices));
        map->slices_valid = 1;
        map->slices_area = map->area_id;
        map->slices_content = content;
        map->prefetch_wait = 0;
    }
    if( map->prefetch_wait > 0 )
    {
        map->prefetch_wait--;
        return;
    }
    slice = Editor_WorldMapNextSlice(map->slices, (uint32_t)map->tick, tab->min_x, tab->max_x, tab->min_z,
        tab->max_z, vx0, vz0, vw, vh);
    if( slice < 0 )
        return;
    if( services->now_us )
        t0 = services->now_us(services->user_data);
    serial = services->worldmap_poll(services->user_data, map->area_id,
        (slice % EDITOR_WORLDMAP_SLICES) * EDITOR_WORLDMAP_SLICE,
        (slice / EDITOR_WORLDMAP_SLICES) * EDITOR_WORLDMAP_SLICE, EDITOR_WORLDMAP_SLICE, EDITOR_WORLDMAP_SLICE,
        picker->zoom < 64.0 ? picker->zoom : 64.0, &missing);
    map->polls++;
    map->prefetch_polls++;
    /* Its serial is not the view's: the view's tiles were drawn complete. */
    (void)serial;
    map->slices[slice] = missing > 0 ? (uint32_t)map->tick : EDITOR_WORLDMAP_SLICE_DONE;
    if( services->now_us )
    {
        uint64_t const spent = services->now_us(services->user_data) - t0;
        /* Over the frame's share: sit out the excess, a share a tick. */
        if( spent > EDITOR_WORLDMAP_PREFETCH_US )
            map->prefetch_wait = (int)(spent / EDITOR_WORLDMAP_PREFETCH_US);
    }
    if( services->request_redraw )
        services->request_redraw(services->user_data);
}

/** The cold open, measured: every frame from the first poll of a cold area
 *  to the view's last region, against the frame budget. */
static void
worldmap_measure(
    struct Editor_WorldMap* map,
    const struct EditorUiAppServices* services)
{
    uint64_t frame;
    if( !map->cold_active || !services->last_frame_us )
        return;
    frame = services->last_frame_us(services->user_data);
    if( frame == 0 )
        return;
    map->cold_frames++;
    if( frame > map->cold_slowest_us )
        map->cold_slowest_us = frame;
    if( frame > EDITOR_WORLDMAP_FRAME_US_MAX )
        map->cold_over++;
    if( map->missing > 0 || !map->polled_valid )
        return;
    map->cold_active = 0;
    /* TORIRS_WORLDMAP_STATS=1: the 6.4 check, asked for. */
    if( getenv("TORIRS_WORLDMAP_STATS") )
        TORIRS_REPORT("worldmap picker: cold open %d frames, slowest %.1f ms, %d over %d ms\n", map->cold_frames,
            (double)map->cold_slowest_us / 1000.0, map->cold_over, EDITOR_WORLDMAP_FRAME_US_MAX / 1000);
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
    worldmap_measure(map, services);
    if( !Editor_WorldMapAvailable(services) || picker->tab_count == 0
        || picker->tabs[picker->active_tab].area == EDITOR_PICKER_TAB_UNMAPPED
        || picker->tabs[picker->active_tab].area == EDITOR_PICKER_TAB_INSTANCE )
    {
        /* No map for this page: Unmapped and Instance space have no area. */
        worldmap_clear(map, picker, &changed);
        return changed;
    }
    {
        /* Below 12 px a square the picker draws the atlas colours: nothing
         * to ask for (and nothing kept drawing a picture it would hide). */
        struct Editor_PickerLod lod;
        Editor_PickerLodAt(picker->zoom, &lod);
        if( !lod.pictures )
        {
            worldmap_clear(map, picker, &changed);
            return changed;
        }
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
        map->full_valid = 0;
        map->core_valid = 0;
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
     * while regions are still arriving. A jump (a cold open, a search, a
     * zoom out) polls the window's middle half first, so its loads queue
     * ahead of the edges'; a pan polls the whole window at once. One poll a
     * tick: the renderer's frame budget is per poll. */
    content = worldmap_content(picker);
    if( !map->polled_valid || map->polled_area != map->area_id || map->polled_x0 != vx0 || map->polled_z0 != vz0
        || map->polled_w != vw || map->polled_h != vh || map->polled_scale != worldmap_bake_scale(picker->zoom)
        || (map->missing > 0 && --map->poll_wait <= 0) )
    {
        int missing = 0;
        int serial;
        int const window_new = !map->polled_valid || map->polled_area != map->area_id || map->polled_x0 != vx0
                               || map->polled_z0 != vz0 || map->polled_w != vw || map->polled_h != vh;
        int const core_done = map->core_valid && map->core_area == map->area_id && map->core_x0 == vx0
                              && map->core_z0 == vz0 && map->core_w == vw && map->core_h == vh;
        if( window_new && !core_done && vw * vh > 4 && !worldmap_overlaps_last(map, vx0, vz0, vw, vh) )
        {
            int cx0;
            int cz0;
            int cw;
            int ch;
            Editor_WorldMapCore(vx0, vz0, vw, vh, &cx0, &cz0, &cw, &ch);
            serial = services->worldmap_poll(
                services->user_data, map->area_id, cx0, cz0, cw, ch, picker->zoom, &missing);
            map->polls++;
            map->core_polls++;
            map->core_valid = 1;
            map->core_area = map->area_id;
            map->core_x0 = vx0;
            map->core_z0 = vz0;
            map->core_w = vw;
            map->core_h = vh;
            if( !map->full_valid )
            {
                map->cold_active = 1;
                map->cold_frames = 0;
                map->cold_slowest_us = 0;
                map->cold_over = 0;
            }
            if( serial < 0 )
            {
                map->area_id = EDITOR_WORLDMAP_AREA_NONE;
                worldmap_clear(map, picker, &changed);
                return changed;
            }
            /* The edges are still to be asked for: the window is not
             * polled yet, and is again next tick. */
            map->serial = serial;
            map->missing = missing > 0 ? missing : 1;
            map->polled_valid = 0;
        }
        else
        {
            serial = services->worldmap_poll(
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
            map->full_valid = 1;
            map->full_x0 = vx0;
            map->full_z0 = vz0;
            map->full_w = vw;
            map->full_h = vh;
        }
    }
    else if( map->polled_valid && map->missing == 0 )
        /* The view is in and nothing else asked this tick: the rest of the
         * page, a slice. */
        worldmap_prefetch(map, services, picker, content, vx0, vz0, vw, vh);

    /* The tiles the well shows at this zoom. */
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
