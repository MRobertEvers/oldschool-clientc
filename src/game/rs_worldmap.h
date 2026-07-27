#ifndef RS_WORLDMAP_H
#define RS_WORLDMAP_H

#include <stdbool.h>

struct CacheProvider;
struct ToriRS_WorldMapArea;

/** Growable int set — the flash / disable lists are a handful of ids at most. */
struct RS_WorldMapIdSet
{
    int* ids;
    int count;
    int capacity;
};

/**
 * View state for the world map interface: which area is shown, where it is
 * looking, and which elements flash or are hidden. The areas themselves are
 * owned by the cache provider; this only borrows them.
 */
struct RS_WorldMapState
{
    struct CacheProvider* provider;
    struct ToriRS_WorldMapAreas* areas;
    struct ToriRS_WorldMapArea* current_area;

    int zoom_percentage;
    /* Centre of the view, in map surface tiles. */
    int display_x;
    int display_y;
    /* Pan target, or -1 when the view is settled. */
    int target_x;
    int target_y;
    /* Size of the map surface widget, in pixels and in tiles. */
    int display_pixel_width;
    int display_pixel_height;
    int display_width;
    int display_height;

    bool elements_enabled;
    bool perpetual_flash;
    int max_flash_count;
    int cycles_per_flash;
    int flash_count;
    int flash_cycle;

    struct RS_WorldMapIdSet disabled_elements;
    struct RS_WorldMapIdSet disabled_categories;
    struct RS_WorldMapIdSet flashing_elements;
    struct RS_WorldMapIdSet flashing_categories;

    /* Cursor for WORLDMAP_LISTELEMENT_START / _NEXT. */
    int icon_index;

    /* What WORLDMAP_ELEMENT / _ELEMENTCOORD report while a map element event
     * script runs. */
    int event_element;
    int event_coord1;
    int event_coord2;
};

struct RS_WorldMapState*
RS_WorldMap_New(struct CacheProvider* provider);

void
RS_WorldMap_Free(struct RS_WorldMapState* state);

/**
 * Adopt whatever the provider has decoded since the last call. Selects the main
 * area and centres on its origin the first time areas appear. Returns true once
 * the world map is usable.
 */
bool
RS_WorldMap_Sync(struct RS_WorldMapState* state);

bool
RS_WorldMap_IsLoaded(struct RS_WorldMapState const* state);

int
RS_WorldMap_CurrentMapId(struct RS_WorldMapState const* state);

/** The area being displayed, or NULL before the areas load. Borrowed. */
struct ToriRS_WorldMapArea const*
RS_WorldMap_CurrentArea(struct RS_WorldMapState const* state);

/** Pixels per map tile at the current zoom (reference getZoomScale). */
int
RS_WorldMap_ZoomScale(struct RS_WorldMapState const* state);

struct ToriRS_WorldMapArea*
RS_WorldMap_Area(
    struct RS_WorldMapState* state,
    int map_id);

void
RS_WorldMap_SetCurrentMapId(
    struct RS_WorldMapState* state,
    int map_id);

/** WORLDMAP_INIT: centre on the main area (no local player to follow yet). */
void
RS_WorldMap_Init(struct RS_WorldMapState* state);

int
RS_WorldMap_Zoom(struct RS_WorldMapState const* state);

void
RS_WorldMap_SetZoom(
    struct RS_WorldMapState* state,
    int zoom_percentage);

/** Pixel size of the map surface widget; drives the tile size scripts read. */
void
RS_WorldMap_SetDisplayPixelSize(
    struct RS_WorldMapState* state,
    int width,
    int height);

void
RS_WorldMap_DisplaySize(
    struct RS_WorldMapState const* state,
    int* width,
    int* height);

void
RS_WorldMap_DisplayPosition(
    struct RS_WorldMapState const* state,
    int* x,
    int* y);

void
RS_WorldMap_JumpToDisplayCoord(
    struct RS_WorldMapState* state,
    int packed_coord,
    bool instant);

void
RS_WorldMap_JumpToSourceCoord(
    struct RS_WorldMapState* state,
    int packed_coord,
    bool instant);

void
RS_WorldMap_JumpToMap(
    struct RS_WorldMapState* state,
    int map_id,
    int packed_fallback_coord,
    bool force_fallback);

/** World coord -> display position. False when the current area misses it. */
bool
RS_WorldMap_SourceToDisplay(
    struct RS_WorldMapState* state,
    int packed_coord,
    int* out_x,
    int* out_y);

/** Display coord -> packed world coord, or -1. */
int
RS_WorldMap_DisplayToSource(
    struct RS_WorldMapState* state,
    int packed_coord);

/** World coord under the view centre. False when there is no current area. */
bool
RS_WorldMap_DisplayCoord(
    struct RS_WorldMapState* state,
    int* out_x,
    int* out_y);

bool
RS_WorldMap_CoordInMap(
    struct RS_WorldMapState* state,
    int map_id,
    int packed_coord);

/** Map area covering a world coord, or -1. */
int
RS_WorldMap_MapAtCoord(
    struct RS_WorldMapState* state,
    int packed_coord);

/** One client tick of panning and flashing. True when something moved. */
bool
RS_WorldMap_Cycle(struct RS_WorldMapState* state);

void
RS_WorldMap_SetElementsEnabled(
    struct RS_WorldMapState* state,
    bool enabled);

bool
RS_WorldMap_ElementsEnabled(struct RS_WorldMapState const* state);

void
RS_WorldMap_SetElementEnabled(
    struct RS_WorldMapState* state,
    int element_id,
    bool enabled);

bool
RS_WorldMap_IsElementEnabled(
    struct RS_WorldMapState const* state,
    int element_id);

void
RS_WorldMap_SetCategoryEnabled(
    struct RS_WorldMapState* state,
    int category_id,
    bool enabled);

bool
RS_WorldMap_IsCategoryEnabled(
    struct RS_WorldMapState const* state,
    int category_id);

void
RS_WorldMap_FlashElement(
    struct RS_WorldMapState* state,
    int element_id);

void
RS_WorldMap_FlashCategory(
    struct RS_WorldMapState* state,
    int category_id);

void
RS_WorldMap_StopCurrentFlashes(struct RS_WorldMapState* state);

void
RS_WorldMap_SetPerpetualFlash(
    struct RS_WorldMapState* state,
    bool enabled);

void
RS_WorldMap_SetMaxFlashCount(
    struct RS_WorldMapState* state,
    int count);

void
RS_WorldMap_ResetMaxFlashCount(struct RS_WorldMapState* state);

void
RS_WorldMap_SetCyclesPerFlash(
    struct RS_WorldMapState* state,
    int cycles);

void
RS_WorldMap_ResetCyclesPerFlash(struct RS_WorldMapState* state);

/**
 * Visible icon iteration. Both report (element, display coord) and return false
 * once exhausted, which the scripts see as (-1, -1).
 */
bool
RS_WorldMap_IconStart(
    struct RS_WorldMapState* state,
    int* out_element,
    int* out_coord);

bool
RS_WorldMap_IconNext(
    struct RS_WorldMapState* state,
    int* out_element,
    int* out_coord);

/** Display coord of the nearest icon of an element to a source coord, or -1. */
int
RS_WorldMap_NearestIcon(
    struct RS_WorldMapState* state,
    int element_id,
    int packed_source_coord);

#endif /* RS_WORLDMAP_H */
