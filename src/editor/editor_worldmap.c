#include "editor/editor_worldmap.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The most pixels a square is rendered at: one per tile. */
#define WORLDMAP_PX_MAX 64

void
Editor_WorldMapInit(struct Editor_WorldMap* map)
{
    assert(map);
    memset(map, 0, sizeof(*map));
    map->area_id = EDITOR_WORLDMAP_AREA_UNASKED;
}

void
Editor_WorldMapFree(struct Editor_WorldMap* map)
{
    if( !map )
        return;
    ToriMem_Free(ToriMem_Editor(), map->pixels);
    ToriMem_Free(ToriMem_Editor(), map->shown);
    map->pixels = NULL;
    map->shown = NULL;
    map->pixels_cap = 0;
    map->shown_cap = 0;
}

int
Editor_WorldMapAvailable(const struct EditorUiAppServices* services)
{
    assert(services);
    return services->worldmap_area && services->worldmap_poll && services->worldmap_render
           && services->image_publish;
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
Editor_WorldMapCompose(
    uint32_t* argb,
    int w,
    int h,
    int px,
    int grid,
    const unsigned char* shown)
{
    int const out_w = w * px;

    assert(argb);
    assert(shown);
    assert(w > 0);
    assert(h > 0);
    assert(px > 0);

    for( int gz = 0; gz < h; gz++ )
    {
        /* Picture row 0 is the north edge: square row h-1. */
        int const top = (h - 1 - gz) * px;
        for( int gx = 0; gx < w; gx++ )
        {
            int const on_page = shown[gz * w + gx] != 0;
            for( int row = 0; row < px; row++ )
            {
                uint32_t* line = &argb[(size_t)(top + row) * (size_t)out_w + (size_t)gx * (size_t)px];
                for( int col = 0; col < px; col++ )
                {
                    uint32_t p = line[col];
                    int const edge = grid && (row == 0 || col == 0);
                    if( (p >> 24) == 0 )
                    {
                        /* Nothing on the map here. */
                        line[col] = edge ? 0xFF1C1C1Cu : 0xFF000000u;
                        continue;
                    }
                    if( !on_page )
                        p = worldmap_scale(p, 100);
                    if( edge )
                        p = worldmap_scale(p, 190);
                    line[col] = p;
                }
            }
        }
    }
}

/* ---- deciding ---------------------------------------------------------------- */

static int
worldmap_key_same(
    const struct Editor_WorldMapKey* a,
    const struct Editor_WorldMapKey* b)
{
    return a->area_id == b->area_id && a->cell == b->cell && a->px == b->px && a->x0 == b->x0
           && a->z0 == b->z0 && a->w == b->w && a->h == b->h && a->atlas_serial == b->atlas_serial
           && a->active_tab == b->active_tab && a->storage_filter == b->storage_filter
           && a->filter_on == b->filter_on;
}

/** The same picture apart from the window, and the window holds `inner`. */
static int
worldmap_key_covers(
    const struct Editor_WorldMapKey* drawn,
    const struct Editor_WorldMapKey* want,
    int vx0,
    int vz0,
    int vw,
    int vh)
{
    return drawn->area_id == want->area_id && drawn->cell == want->cell && drawn->px == want->px
           && drawn->atlas_serial == want->atlas_serial && drawn->active_tab == want->active_tab
           && drawn->storage_filter == want->storage_filter && drawn->filter_on == want->filter_on
           && vx0 >= drawn->x0 && vz0 >= drawn->z0 && vx0 + vw <= drawn->x0 + drawn->w
           && vz0 + vh <= drawn->z0 + drawn->h;
}

/** One axis of the window: the visible span plus a quarter of it each side,
 *  clamped at 0 and to `max_squares`. */
static void
worldmap_axis(
    int v0,
    int vn,
    int max_squares,
    int* out_0,
    int* out_n)
{
    int margin = vn / 4 > 2 ? vn / 4 : 2;
    int lo;
    int hi;

    if( vn + 2 * margin > max_squares )
        margin = (max_squares - vn) / 2 > 0 ? (max_squares - vn) / 2 : 0;
    lo = v0 - margin;
    hi = v0 + vn + margin;
    if( lo < 0 )
        lo = 0;
    *out_0 = lo;
    *out_n = hi - lo;
}

static void
worldmap_clear(
    struct Editor_WorldMap* map,
    struct Editor_Picker* picker,
    int* changed)
{
    if( picker->underlay.scene_id != 0 )
        *changed = 1;
    memset(&picker->underlay, 0, sizeof(picker->underlay));
    map->scene_id = 0;
    map->polled_valid = 0;
    map->missing = 0;
}

static void
worldmap_render(
    struct Editor_WorldMap* map,
    const struct EditorUiAppServices* services,
    struct Editor_Picker* picker,
    const struct Editor_WorldMapKey* key)
{
    size_t const pixels = (size_t)key->w * (size_t)key->px * (size_t)key->h * (size_t)key->px;
    size_t const cells = (size_t)key->w * (size_t)key->h;
    int scene_id;

    if( pixels > map->pixels_cap )
    {
        ToriMem_Free(ToriMem_Editor(), map->pixels);
        map->pixels = ToriMem_Alloc(ToriMem_Editor(), pixels * sizeof(uint32_t));
        assert(map->pixels);
        map->pixels_cap = pixels;
    }
    if( cells > map->shown_cap )
    {
        ToriMem_Free(ToriMem_Editor(), map->shown);
        map->shown = ToriMem_Alloc(ToriMem_Editor(), cells);
        assert(map->shown);
        map->shown_cap = cells;
    }
    memset(map->shown, 0, cells);
    for( int i = 0; i < picker->square_count; i++ )
    {
        const struct Editor_PickerSquare* square = &picker->squares[i];
        int const gx = square->map_x - key->x0;
        int const gz = square->map_z - key->z0;
        if( gx < 0 || gz < 0 || gx >= key->w || gz >= key->h )
            continue;
        if( Editor_PickerSquareVisible(picker, i) )
            map->shown[gz * key->w + gx] = 1;
    }

    map->renders++;
    if( !services->worldmap_render(
            services->user_data, key->area_id, key->x0, key->z0, key->w, key->h, key->px, map->pixels) )
    {
        /* The area went (a cache without it): remembered, not asked again. */
        map->area_id = EDITOR_WORLDMAP_AREA_NONE;
        map->scene_id = 0;
        return;
    }
    Editor_WorldMapCompose(
        map->pixels, key->w, key->h, key->px, key->cell >= EDITOR_WORLDMAP_GRID_CELL && key->px >= 4, map->shown);
    scene_id = services->image_publish(services->user_data, map->slot, map->pixels, key->w * key->px, key->h * key->px);
    map->slot ^= 1;
    map->scene_id = scene_id > 0 ? scene_id : 0;
    map->drawn = *key;
    map->drawn_serial = map->serial;
    map->ticks_since_render = 0;
}

int
Editor_WorldMapUpdate(
    struct Editor_WorldMap* map,
    const struct EditorUiAppServices* services,
    struct Editor_Picker* picker,
    int chrome_scale)
{
    struct Editor_WorldMapKey want;
    const char* name;
    int changed = 0;
    int vx0;
    int vz0;
    int vw;
    int vh;
    int area;

    assert(map);
    assert(services);
    assert(picker);
    assert(chrome_scale > 0);

    if( !Editor_WorldMapAvailable(services) || picker->tab_count == 0
        || picker->tabs[picker->active_tab].area < 0 )
    {
        /* No map for this page: the Unmapped page has no area. */
        worldmap_clear(map, picker, &changed);
        return changed;
    }

    /* The page's area, asked once per page (and again while the cache's
     * areas are still loading). */
    area = picker->tabs[picker->active_tab].area;
    name = picker->areas[area].internal_name;
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

    memset(&want, 0, sizeof(want));
    want.area_id = map->area_id;
    want.cell = picker->cell;
    want.px = picker->cell * chrome_scale < WORLDMAP_PX_MAX ? picker->cell * chrome_scale : WORLDMAP_PX_MAX;
    /* A well wider than the picture may be: fewer pixels a square. */
    if( vw * want.px > EDITOR_WORLDMAP_IMAGE_MAX )
        want.px = EDITOR_WORLDMAP_IMAGE_MAX / vw > 0 ? EDITOR_WORLDMAP_IMAGE_MAX / vw : 1;
    if( vh * want.px > EDITOR_WORLDMAP_IMAGE_MAX )
        want.px = EDITOR_WORLDMAP_IMAGE_MAX / vh > 0 ? EDITOR_WORLDMAP_IMAGE_MAX / vh : 1;
    want.atlas_serial = picker->atlas_serial;
    want.active_tab = picker->active_tab;
    want.storage_filter = picker->storage_filter;
    want.filter_on = picker->filter_on;

    if( map->scene_id > 0 && worldmap_key_covers(&map->drawn, &want, vx0, vz0, vw, vh) )
    {
        /* The picture still holds the view: keep its window. */
        want.x0 = map->drawn.x0;
        want.z0 = map->drawn.z0;
        want.w = map->drawn.w;
        want.h = map->drawn.h;
    }
    else
    {
        worldmap_axis(vx0, vw, EDITOR_WORLDMAP_IMAGE_MAX / want.px, &want.x0, &want.w);
        worldmap_axis(vz0, vh, EDITOR_WORLDMAP_IMAGE_MAX / want.px, &want.z0, &want.h);
    }

    /* Poll the window as soon as it is new, then every few ticks while
     * regions are still arriving. */
    if( !map->polled_valid || !worldmap_key_same(&map->polled, &want)
        || (map->missing > 0 && --map->poll_wait <= 0) )
    {
        int missing = 0;
        int const serial = services->worldmap_poll(
            services->user_data, want.area_id, want.x0, want.z0, want.w, want.h, &missing);
        map->polls++;
        if( serial < 0 )
        {
            map->area_id = EDITOR_WORLDMAP_AREA_NONE;
            worldmap_clear(map, picker, &changed);
            return changed;
        }
        map->serial = serial;
        map->missing = missing;
        map->polled = want;
        map->polled_valid = 1;
        map->poll_wait = EDITOR_WORLDMAP_POLL_TICKS;
    }

    map->ticks_since_render++;
    if( map->scene_id == 0 || !worldmap_key_same(&map->drawn, &want)
        || (map->serial != map->drawn_serial
            && (map->missing == 0 || map->ticks_since_render >= EDITOR_WORLDMAP_LOADING_TICKS)) )
    {
        int const before = map->scene_id;
        worldmap_render(map, services, picker, &want);
        changed = changed || map->scene_id != before || map->scene_id > 0;
    }
    if( map->missing > 0 && services->request_redraw )
        services->request_redraw(services->user_data);

    if( map->scene_id <= 0 )
    {
        worldmap_clear(map, picker, &changed);
        return changed;
    }
    {
        struct Editor_PickerUnderlay next;
        memset(&next, 0, sizeof(next));
        next.scene_id = map->scene_id;
        Editor_PickerCellOrigin(picker, map->drawn.x0, map->drawn.z0 + map->drawn.h - 1, &next.x, &next.y);
        next.w = map->drawn.w * picker->cell;
        next.h = map->drawn.h * picker->cell;
        if( memcmp(&next, &picker->underlay, sizeof(next)) != 0 )
            changed = 1;
        picker->underlay = next;
    }
    return changed;
}
