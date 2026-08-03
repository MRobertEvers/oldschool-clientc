#include "game/rs_worldmap.h"

#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "engine/torirs_worldmap_from_rscache.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RS_WORLDMAP_DEFAULT_ZOOM 100
#define RS_WORLDMAP_DEFAULT_MAX_FLASH_COUNT 3
#define RS_WORLDMAP_DEFAULT_CYCLES_PER_FLASH 50
/* Reference default surface size, used until a widget reports its own. */
#define RS_WORLDMAP_DEFAULT_DISPLAY_WIDTH 512
#define RS_WORLDMAP_DEFAULT_DISPLAY_HEIGHT 334
/* Panning covers the remaining distance in at most this many ticks. */
#define RS_WORLDMAP_PAN_STEPS 8

static bool
id_set_contains(
    struct RS_WorldMapIdSet const* set,
    int id)
{
    for( int i = 0; i < set->count; i++ )
    {
        if( set->ids[i] == id )
            return true;
    }
    return false;
}

static void
id_set_add(
    struct RS_WorldMapIdSet* set,
    int id)
{
    if( id_set_contains(set, id) )
        return;
    if( set->count == set->capacity )
    {
        int capacity = set->capacity > 0 ? set->capacity * 2 : 8;
        int* ids = realloc(set->ids, (size_t)capacity * sizeof(*ids));
        assert(ids);
        set->ids = ids;
        set->capacity = capacity;
    }
    set->ids[set->count++] = id;
}

static void
id_set_remove(
    struct RS_WorldMapIdSet* set,
    int id)
{
    for( int i = 0; i < set->count; i++ )
    {
        if( set->ids[i] != id )
            continue;
        set->ids[i] = set->ids[set->count - 1];
        set->count--;
        return;
    }
}

static void
id_set_clear(struct RS_WorldMapIdSet* set)
{
    set->count = 0;
}

static void
id_set_free(struct RS_WorldMapIdSet* set)
{
    free(set->ids);
    set->ids = NULL;
    set->count = 0;
    set->capacity = 0;
}

/* Only these percentages exist in the client; anything else is the 200% step. */
static int
normalize_zoom(int zoom_percentage)
{
    switch( zoom_percentage )
    {
    case 25:
    case 37:
    case 50:
    case 75:
    case 100:
        return zoom_percentage;
    default:
        return 200;
    }
}

static int
zoom_scale_pixels_per_tile(int zoom_percentage)
{
    switch( zoom_percentage )
    {
    case 25:
        return 1;
    case 37:
        /* 1.5px per tile; the tile counts below round up either way. */
        return 2;
    case 50:
        return 2;
    case 75:
        return 3;
    case 100:
        return 4;
    default:
        return 8;
    }
}

static void
recompute_display_size(struct RS_WorldMapState* state)
{
    int pixels_per_tile = zoom_scale_pixels_per_tile(state->zoom_percentage);
    int width = state->display_pixel_width;
    int height = state->display_pixel_height;

    if( width <= 0 )
        width = RS_WORLDMAP_DEFAULT_DISPLAY_WIDTH;
    if( height <= 0 )
        height = RS_WORLDMAP_DEFAULT_DISPLAY_HEIGHT;

    state->display_width = (width + pixels_per_tile - 1) / pixels_per_tile;
    state->display_height = (height + pixels_per_tile - 1) / pixels_per_tile;
}

static void
set_display_position(
    struct RS_WorldMapState* state,
    int x,
    int y)
{
    state->display_x = x;
    state->display_y = y;
    state->target_x = -1;
    state->target_y = -1;
}

/* Centre on a world coord, falling back to the area's own origin when the area
 * does not actually cover it (the origin of a dungeon map is often outside its
 * own sections). */
static void
jump_to_source_or_origin_instant(
    struct RS_WorldMapState* state,
    int plane,
    int x,
    int y)
{
    struct ToriRS_WorldMapArea* area = state->current_area;
    int origin_plane;
    int origin_x;
    int origin_y;
    int out_x;
    int out_y;

    if( !area )
        return;

    if( ToriRS_WorldMapArea_Position(area, plane, x, y, &out_x, &out_y) )
    {
        set_display_position(state, out_x, out_y);
        return;
    }

    ToriRS_WorldMapUnpackCoord(area->origin, &origin_plane, &origin_x, &origin_y);
    if( ToriRS_WorldMapArea_Position(area, origin_plane, origin_x, origin_y, &out_x, &out_y) )
    {
        set_display_position(state, out_x, out_y);
        return;
    }
    /* Last resort: centre on the area's own display bounds. Storing raw world
     * coords as display coords can land outside region_low..high, invert the
     * visit clamp, and paint only background_colour (black for most areas). */
    {
        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;

        ToriRS_WorldMapArea_Bounds(area, &min_x, &min_y, &max_x, &max_y);
        set_display_position(state, (min_x + max_x) / 2, (min_y + max_y) / 2);
    }
}

static void
select_area(
    struct RS_WorldMapState* state,
    struct ToriRS_WorldMapArea* area)
{
    int plane;
    int x;
    int y;

    if( !area )
        return;

    state->current_area = area;
    state->icon_index = 0;
    if( area->zoom > 0 )
        RS_WorldMap_SetZoom(state, area->zoom);
    ToriRS_WorldMapUnpackCoord(area->origin, &plane, &x, &y);
    jump_to_source_or_origin_instant(state, plane, x, y);
}

struct RS_WorldMapState*
RS_WorldMap_New(struct CacheProvider* provider)
{
    struct RS_WorldMapState* state = calloc(1, sizeof(*state));

    assert(state);
    state->provider = provider;
    state->zoom_percentage = RS_WORLDMAP_DEFAULT_ZOOM;
    state->target_x = -1;
    state->target_y = -1;
    state->elements_enabled = true;
    state->max_flash_count = RS_WORLDMAP_DEFAULT_MAX_FLASH_COUNT;
    state->cycles_per_flash = RS_WORLDMAP_DEFAULT_CYCLES_PER_FLASH;
    state->flash_count = -1;
    state->flash_cycle = -1;
    state->event_element = -1;
    state->event_coord1 = -1;
    state->event_coord2 = -1;
    recompute_display_size(state);
    return state;
}

void
RS_WorldMap_Free(struct RS_WorldMapState* state)
{
    if( !state )
        return;
    id_set_free(&state->disabled_elements);
    id_set_free(&state->disabled_categories);
    id_set_free(&state->flashing_elements);
    id_set_free(&state->flashing_categories);
    free(state);
}

bool
RS_WorldMap_Sync(struct RS_WorldMapState* state)
{
    struct ToriRS_WorldMapAreas* areas;

    assert(state);
    if( !state->provider )
        return false;

    areas = CacheProvider_WorldMapGet(state->provider);
    if( !areas )
        return false;
    if( areas == state->areas )
        return true;

    state->areas = areas;
    state->current_area = NULL;
    for( int i = 0; i < areas->count; i++ )
    {
        if( !areas->areas[i].is_main )
            continue;
        select_area(state, &areas->areas[i]);
        break;
    }
    if( !state->current_area && areas->count > 0 )
        select_area(state, &areas->areas[0]);

    if( getenv("TORIRS_WORLDMAP_DEBUG") )
    {
        fprintf(
            stderr,
            "worldmap: %d areas, current=%d (%s) display=(%d,%d) zoom=%d size=%dx%d tiles "
            "regions x=%d..%d y=%d..%d\n",
            areas->count,
            RS_WorldMap_CurrentMapId(state),
            state->current_area && state->current_area->internal_name
                ? state->current_area->internal_name
                : "?",
            state->display_x,
            state->display_y,
            state->zoom_percentage,
            state->display_width,
            state->display_height,
            state->current_area ? state->current_area->region_low_x : -1,
            state->current_area ? state->current_area->region_high_x : -1,
            state->current_area ? state->current_area->region_low_y : -1,
            state->current_area ? state->current_area->region_high_y : -1);
    }
    return state->current_area != NULL;
}

bool
RS_WorldMap_IsLoaded(struct RS_WorldMapState const* state)
{
    return state && state->areas && state->areas->count > 0;
}

int
RS_WorldMap_CurrentMapId(struct RS_WorldMapState const* state)
{
    if( !state || !state->current_area )
        return -1;
    return state->current_area->id;
}

struct ToriRS_WorldMapArea*
RS_WorldMap_Area(
    struct RS_WorldMapState* state,
    int map_id)
{
    if( !state )
        return NULL;
    return ToriRS_WorldMapAreasGet(state->areas, map_id);
}

void
RS_WorldMap_SetCurrentMapId(
    struct RS_WorldMapState* state,
    int map_id)
{
    select_area(state, RS_WorldMap_Area(state, map_id));
}

void
RS_WorldMap_Init(struct RS_WorldMapState* state)
{
    assert(state);
    if( !state->areas )
        return;
    for( int i = 0; i < state->areas->count; i++ )
    {
        if( !state->areas->areas[i].is_main )
            continue;
        select_area(state, &state->areas->areas[i]);
        return;
    }
    if( state->areas->count > 0 )
        select_area(state, &state->areas->areas[0]);
}

void
RS_WorldMap_SetDisplayPosition(
    struct RS_WorldMapState* state,
    int x,
    int y)
{
    assert(state);
    set_display_position(state, x, y);
}

struct ToriRS_WorldMapArea const*
RS_WorldMap_CurrentArea(struct RS_WorldMapState const* state)
{
    return state ? state->current_area : NULL;
}

int
RS_WorldMap_ZoomScale(struct RS_WorldMapState const* state)
{
    return state ? zoom_scale_pixels_per_tile(state->zoom_percentage) : 4;
}

int
RS_WorldMap_Zoom(struct RS_WorldMapState const* state)
{
    return state ? state->zoom_percentage : RS_WORLDMAP_DEFAULT_ZOOM;
}

void
RS_WorldMap_SetZoom(
    struct RS_WorldMapState* state,
    int zoom_percentage)
{
    assert(state);
    state->zoom_percentage = normalize_zoom(zoom_percentage);
    recompute_display_size(state);
}

void
RS_WorldMap_SetDisplayPixelSize(
    struct RS_WorldMapState* state,
    int width,
    int height)
{
    assert(state);
    state->display_pixel_width = width > 0 ? width : 0;
    state->display_pixel_height = height > 0 ? height : 0;
    recompute_display_size(state);
}

void
RS_WorldMap_DisplaySize(
    struct RS_WorldMapState const* state,
    int* width,
    int* height)
{
    if( width )
        *width = state ? state->display_width : 0;
    if( height )
        *height = state ? state->display_height : 0;
}

void
RS_WorldMap_DisplayPosition(
    struct RS_WorldMapState const* state,
    int* x,
    int* y)
{
    if( x )
        *x = state ? state->display_x : -1;
    if( y )
        *y = state ? state->display_y : -1;
}

void
RS_WorldMap_JumpToDisplayCoord(
    struct RS_WorldMapState* state,
    int packed_coord,
    bool instant)
{
    int plane;
    int x;
    int y;

    assert(state);
    ToriRS_WorldMapUnpackCoord(packed_coord, &plane, &x, &y);

    if( instant )
    {
        if( state->current_area )
            set_display_position(state, x, y);
        return;
    }
    /* A target outside the surface would pan into nothing. */
    if( !ToriRS_WorldMapArea_ContainsPosition(state->current_area, x, y) )
        return;
    state->target_x = x;
    state->target_y = y;
}

void
RS_WorldMap_JumpToSourceCoord(
    struct RS_WorldMapState* state,
    int packed_coord,
    bool instant)
{
    int plane;
    int x;
    int y;
    int display_x;
    int display_y;

    assert(state);
    ToriRS_WorldMapUnpackCoord(packed_coord, &plane, &x, &y);
    if( !ToriRS_WorldMapArea_Position(state->current_area, plane, x, y, &display_x, &display_y) )
        return;

    if( instant )
    {
        set_display_position(state, display_x, display_y);
        return;
    }
    state->target_x = display_x;
    state->target_y = display_y;
}

void
RS_WorldMap_JumpToMap(
    struct RS_WorldMapState* state,
    int map_id,
    int packed_fallback_coord,
    bool force_fallback)
{
    struct ToriRS_WorldMapArea* area;
    int plane;
    int x;
    int y;

    assert(state);
    area = RS_WorldMap_Area(state, map_id);
    if( !area )
        return;

    state->current_area = area;
    state->icon_index = 0;
    if( area->zoom > 0 )
        RS_WorldMap_SetZoom(state, area->zoom);

    /* There is no local player to prefer yet, so the fallback coord always
     * wins — same result as the reference when the player is off this map. */
    (void)force_fallback;
    ToriRS_WorldMapUnpackCoord(packed_fallback_coord, &plane, &x, &y);
    jump_to_source_or_origin_instant(state, plane, x, y);
}

bool
RS_WorldMap_SourceToDisplay(
    struct RS_WorldMapState* state,
    int packed_coord,
    int* out_x,
    int* out_y)
{
    int plane;
    int x;
    int y;

    assert(state);
    ToriRS_WorldMapUnpackCoord(packed_coord, &plane, &x, &y);
    return ToriRS_WorldMapArea_Position(state->current_area, plane, x, y, out_x, out_y);
}

int
RS_WorldMap_DisplayToSource(
    struct RS_WorldMapState* state,
    int packed_coord)
{
    int plane;
    int x;
    int y;
    int out_plane;
    int out_x;
    int out_y;

    assert(state);
    ToriRS_WorldMapUnpackCoord(packed_coord, &plane, &x, &y);
    if( !ToriRS_WorldMapArea_Coord(state->current_area, x, y, &out_plane, &out_x, &out_y) )
        return -1;
    return ToriRS_WorldMapPackCoord(out_plane, out_x, out_y);
}

bool
RS_WorldMap_DisplayCoord(
    struct RS_WorldMapState* state,
    int* out_x,
    int* out_y)
{
    int plane;

    assert(state);
    if( !state->current_area )
        return false;
    if( ToriRS_WorldMapArea_Coord(
            state->current_area, state->display_x, state->display_y, &plane, out_x, out_y) )
        return true;

    /* Off-surface centre: report the raw display position, as the reference
     * does, so callers still get a usable pair. */
    if( out_x )
        *out_x = state->display_x;
    if( out_y )
        *out_y = state->display_y;
    return true;
}

bool
RS_WorldMap_CoordInMap(
    struct RS_WorldMapState* state,
    int map_id,
    int packed_coord)
{
    int plane;
    int x;
    int y;

    assert(state);
    ToriRS_WorldMapUnpackCoord(packed_coord, &plane, &x, &y);
    return ToriRS_WorldMapArea_ContainsCoord(RS_WorldMap_Area(state, map_id), plane, x, y);
}

int
RS_WorldMap_MapAtCoord(
    struct RS_WorldMapState* state,
    int packed_coord)
{
    int plane;
    int x;
    int y;

    assert(state);
    if( !state->areas )
        return -1;

    ToriRS_WorldMapUnpackCoord(packed_coord, &plane, &x, &y);
    for( int i = 0; i < state->areas->count; i++ )
    {
        if( ToriRS_WorldMapArea_ContainsCoord(&state->areas->areas[i], plane, x, y) )
            return state->areas->areas[i].id;
    }
    return -1;
}

static bool
has_active_flashes(struct RS_WorldMapState const* state)
{
    return state->flashing_elements.count > 0 || state->flashing_categories.count > 0;
}

static bool
cycle_flash(struct RS_WorldMapState* state)
{
    if( !has_active_flashes(state) )
        return false;

    state->flash_cycle = (state->flash_cycle < 0 ? 0 : state->flash_cycle) + 1;
    if( state->cycles_per_flash > 0 && state->flash_cycle % state->cycles_per_flash == 0 )
    {
        state->flash_count = (state->flash_count < 0 ? 0 : state->flash_count) + 1;
        if( state->flash_count >= state->max_flash_count && !state->perpetual_flash )
            RS_WorldMap_StopCurrentFlashes(state);
    }
    return true;
}

bool
RS_WorldMap_Cycle(struct RS_WorldMapState* state)
{
    int delta_x;
    int delta_y;
    int step_x;
    int step_y;
    bool changed;

    assert(state);
    changed = cycle_flash(state);
    if( state->target_x == -1 || state->target_y == -1 )
        return changed;

    delta_x = state->target_x - state->display_x;
    delta_y = state->target_y - state->display_y;
    step_x = delta_x;
    step_y = delta_y;
    if( delta_x != 0 )
    {
        int span = delta_x < 0 ? -delta_x : delta_x;
        step_x = delta_x / (span < RS_WORLDMAP_PAN_STEPS ? span : RS_WORLDMAP_PAN_STEPS);
    }
    if( delta_y != 0 )
    {
        int span = delta_y < 0 ? -delta_y : delta_y;
        step_y = delta_y / (span < RS_WORLDMAP_PAN_STEPS ? span : RS_WORLDMAP_PAN_STEPS);
    }

    state->display_x += step_x;
    state->display_y += step_y;
    if( state->display_x == state->target_x && state->display_y == state->target_y )
    {
        state->target_x = -1;
        state->target_y = -1;
    }
    return changed || step_x != 0 || step_y != 0;
}

void
RS_WorldMap_SetElementsEnabled(
    struct RS_WorldMapState* state,
    bool enabled)
{
    assert(state);
    state->elements_enabled = enabled;
}

bool
RS_WorldMap_ElementsEnabled(struct RS_WorldMapState const* state)
{
    return state && state->elements_enabled;
}

void
RS_WorldMap_SetElementEnabled(
    struct RS_WorldMapState* state,
    int element_id,
    bool enabled)
{
    assert(state);
    if( enabled )
        id_set_remove(&state->disabled_elements, element_id);
    else
        id_set_add(&state->disabled_elements, element_id);
}

bool
RS_WorldMap_IsElementEnabled(
    struct RS_WorldMapState const* state,
    int element_id)
{
    return state && !id_set_contains(&state->disabled_elements, element_id);
}

void
RS_WorldMap_SetCategoryEnabled(
    struct RS_WorldMapState* state,
    int category_id,
    bool enabled)
{
    assert(state);
    if( enabled )
        id_set_remove(&state->disabled_categories, category_id);
    else
        id_set_add(&state->disabled_categories, category_id);
}

bool
RS_WorldMap_IsCategoryEnabled(
    struct RS_WorldMapState const* state,
    int category_id)
{
    return state && !id_set_contains(&state->disabled_categories, category_id);
}

bool
RS_WorldMap_IconVisible(
    struct RS_WorldMapState const* state,
    int element_id,
    int category_id)
{
    if( !state )
        return false;
    /* An element with no category (-1) is only ever gated by its own id and by
     * the global switch — the reference short-circuits the category test the
     * same way, so "hide category -1" cannot hide every uncategorised icon. */
    return state->elements_enabled && RS_WorldMap_IsElementEnabled(state, element_id) &&
           (category_id < 0 || RS_WorldMap_IsCategoryEnabled(state, category_id));
}

bool
RS_WorldMap_ShouldFlashIcon(
    struct RS_WorldMapState const* state,
    int element_id,
    int category_id)
{
    if( !state || !has_active_flashes(state) )
        return false;
    if( state->flash_cycle < 0 || state->cycles_per_flash <= 0 )
        return false;
    /* On for the first half of every cycle, off for the second — the blink is
     * the cycle counter, which RS_WorldMap_Cycle advances once per client tick. */
    if( state->flash_cycle % state->cycles_per_flash >= state->cycles_per_flash / 2 )
        return false;
    if( id_set_contains(&state->flashing_elements, element_id) )
        return true;
    return category_id >= 0 && id_set_contains(&state->flashing_categories, category_id);
}

void
RS_WorldMap_FlashElement(
    struct RS_WorldMapState* state,
    int element_id)
{
    assert(state);
    id_set_clear(&state->flashing_elements);
    id_set_clear(&state->flashing_categories);
    id_set_add(&state->flashing_elements, element_id);
    state->flash_count = 0;
    state->flash_cycle = 0;
}

void
RS_WorldMap_FlashCategory(
    struct RS_WorldMapState* state,
    int category_id)
{
    assert(state);
    id_set_clear(&state->flashing_elements);
    id_set_clear(&state->flashing_categories);
    id_set_add(&state->flashing_categories, category_id);
    state->flash_count = 0;
    state->flash_cycle = 0;
}

void
RS_WorldMap_StopCurrentFlashes(struct RS_WorldMapState* state)
{
    assert(state);
    id_set_clear(&state->flashing_elements);
    id_set_clear(&state->flashing_categories);
    state->flash_count = -1;
    state->flash_cycle = -1;
}

void
RS_WorldMap_SetPerpetualFlash(
    struct RS_WorldMapState* state,
    bool enabled)
{
    assert(state);
    state->perpetual_flash = enabled;
}

void
RS_WorldMap_SetMaxFlashCount(
    struct RS_WorldMapState* state,
    int count)
{
    assert(state);
    if( count >= 1 )
        state->max_flash_count = count;
}

void
RS_WorldMap_ResetMaxFlashCount(struct RS_WorldMapState* state)
{
    assert(state);
    state->max_flash_count = RS_WORLDMAP_DEFAULT_MAX_FLASH_COUNT;
}

void
RS_WorldMap_SetCyclesPerFlash(
    struct RS_WorldMapState* state,
    int cycles)
{
    assert(state);
    if( cycles >= 1 )
        state->cycles_per_flash = cycles;
}

void
RS_WorldMap_ResetCyclesPerFlash(struct RS_WorldMapState* state)
{
    assert(state);
    state->cycles_per_flash = RS_WORLDMAP_DEFAULT_CYCLES_PER_FLASH;
}

/* An element whose config has not been loaded yet resolves to "no category",
 * i.e. only its own id can hide it. Categories only ever narrow visibility, so
 * the worst case is an icon staying visible for a tick longer than it should. */
static int
icon_category(
    struct RS_WorldMapState* state,
    int element_id)
{
    struct ToriRS_MapElement* element;

    if( !state->provider )
        return -1;
    element = CacheProvider_MapElementGet(state->provider, element_id);
    return element ? element->category : -1;
}

static bool
icon_visible(
    struct RS_WorldMapState* state,
    struct ToriRS_WorldMapIcon const* icon)
{
    int category;
    int plane;
    int x;
    int y;

    if( !state->elements_enabled )
        return false;
    if( icon->element < 0 )
        return false;
    if( !RS_WorldMap_IsElementEnabled(state, icon->element) )
        return false;

    category = icon_category(state, icon->element);
    if( category >= 0 && !RS_WorldMap_IsCategoryEnabled(state, category) )
        return false;

    ToriRS_WorldMapUnpackCoord(icon->coord, &plane, &x, &y);
    return ToriRS_WorldMapArea_ContainsCoord(state->current_area, plane, x, y);
}

/* Icons carry source coords; the scripts want where they land on the surface. */
static int
icon_display_coord(
    struct RS_WorldMapState* state,
    struct ToriRS_WorldMapIcon const* icon)
{
    int plane;
    int x;
    int y;
    int display_x;
    int display_y;

    ToriRS_WorldMapUnpackCoord(icon->coord, &plane, &x, &y);
    if( !ToriRS_WorldMapArea_Position(state->current_area, plane, x, y, &display_x, &display_y) )
        return icon->coord;
    return ToriRS_WorldMapPackCoord(plane, display_x, display_y);
}

bool
RS_WorldMap_IconNext(
    struct RS_WorldMapState* state,
    int* out_element,
    int* out_coord)
{
    struct ToriRS_WorldMapArea* area;

    assert(state);
    area = state->current_area;
    if( !area )
        return false;

    while( state->icon_index < area->icon_count )
    {
        struct ToriRS_WorldMapIcon const* icon = &area->icons[state->icon_index++];
        if( !icon_visible(state, icon) )
            continue;
        if( out_element )
            *out_element = icon->element;
        if( out_coord )
            *out_coord = icon_display_coord(state, icon);
        return true;
    }
    return false;
}

bool
RS_WorldMap_IconStart(
    struct RS_WorldMapState* state,
    int* out_element,
    int* out_coord)
{
    assert(state);
    state->icon_index = 0;
    return RS_WorldMap_IconNext(state, out_element, out_coord);
}

int
RS_WorldMap_NearestIcon(
    struct RS_WorldMapState* state,
    int element_id,
    int packed_source_coord)
{
    struct ToriRS_WorldMapArea* area;
    struct ToriRS_WorldMapIcon const* nearest = NULL;
    long long nearest_distance = 0;
    int plane;
    int source_x;
    int source_y;

    assert(state);
    area = state->current_area;
    if( !area )
        return -1;

    ToriRS_WorldMapUnpackCoord(packed_source_coord, &plane, &source_x, &source_y);
    if( !ToriRS_WorldMapArea_ContainsPosition(area, source_x, source_y) )
        return -1;

    for( int i = 0; i < area->icon_count; i++ )
    {
        struct ToriRS_WorldMapIcon const* icon = &area->icons[i];
        int icon_plane;
        int icon_x;
        int icon_y;
        long long dx;
        long long dy;
        long long distance;

        if( icon->element != element_id || !icon_visible(state, icon) )
            continue;

        ToriRS_WorldMapUnpackCoord(
            icon_display_coord(state, icon), &icon_plane, &icon_x, &icon_y);
        dx = icon_x - source_x;
        dy = icon_y - source_y;
        distance = dx * dx + dy * dy;
        if( nearest && distance >= nearest_distance )
            continue;
        nearest = icon;
        nearest_distance = distance;
    }

    return nearest ? icon_display_coord(state, nearest) : -1;
}
