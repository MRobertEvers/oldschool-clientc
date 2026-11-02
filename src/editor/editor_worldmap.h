#ifndef SRC_EDITOR_EDITOR_WORLDMAP_H
#define SRC_EDITOR_EDITOR_WORLDMAP_H

/*
 * The world map under the square picker: the picture the CS2 world map
 * (interface 595) shows of the active page's area, drawn by the client's own
 * world-map renderer and handed to the picker as one GRAPH image
 * (Editor_PickerUnderlay).
 *
 * The App does the rendering (EditorUiAppServices worldmap_area / _poll /
 * _render / image_publish, src/app/app_map_editor.c over
 * game/rs_worldmap_overview.h); this decides what to ask for and when, and
 * composes what comes back:
 *
 *   the window   the visible squares plus a quarter of the view on every
 *                side, clamped at square 0 and to EDITOR_WORLDMAP_IMAGE_MAX
 *                pixels a side; a pan inside it moves the picture, nothing
 *                is rendered
 *   the key      area, cell, pixels per square, the window, and what the
 *                picker shows (its atlas serial, page, storage filter): the
 *                picture is rendered again only when one of these changes,
 *                or while regions are still arriving (at most every
 *                EDITOR_WORLDMAP_LOADING_TICKS ticks)
 *   the picture  the map; squares the page does not show (no map in any
 *                storage, or on another page) dimmed, black where the map
 *                has nothing; a faint grid from EDITOR_WORLDMAP_GRID_CELL
 *                px a square
 *   the image    two scene sprites taken in turn, so the GRAPH's item hash
 *                sees each new picture
 *
 * Without the four services (the headless harness, the web) nothing is drawn
 * and the picker fills its cells as before.
 */

#include "editor/editor_ui.h"
#include "editor/panels/edpanel_picker.h"

#include <stddef.h>
#include <stdint.h>

/** The picture's largest side, pixels. */
#define EDITOR_WORLDMAP_IMAGE_MAX 2048
/** The grid is drawn from this many logical px a square: below it the
 *  lines would be most of the picture. */
#define EDITOR_WORLDMAP_GRID_CELL 8
/** Ticks between pictures while regions are still loading. */
#define EDITOR_WORLDMAP_LOADING_TICKS 10
/** Ticks between polls while regions are still loading. */
#define EDITOR_WORLDMAP_POLL_TICKS 1
/** Ticks before asking again for areas the cache has not loaded. */
#define EDITOR_WORLDMAP_AREA_RETRY_TICKS 30

/** The area lookup's answers, beside a real id (>= 0). */
enum
{
    /** The cache has no such area. Remembered. */
    EDITOR_WORLDMAP_AREA_NONE = -1,
    /** The cache's areas are not loaded yet: ask again. */
    EDITOR_WORLDMAP_AREA_PENDING = -2,
    /** Not asked yet for this page. */
    EDITOR_WORLDMAP_AREA_UNASKED = -3,
};

struct Editor_WorldMapKey
{
    int area_id;
    int cell;
    int px;
    int x0;
    int z0;
    int w;
    int h;
    uint32_t atlas_serial;
    int active_tab;
    int storage_filter;
    int filter_on;
};

struct Editor_WorldMap
{
    /** The page the area id answers for, by the atlas area's internal name. */
    char area_name[EDITOR_PICKER_NAME_MAX];
    int area_id;
    int area_retry;

    /** What the picture shows; valid while `scene_id` is set. */
    struct Editor_WorldMapKey drawn;
    int drawn_serial;
    int scene_id;
    int slot;

    /** The overview's bake serial and what it still lacks, from the last
     *  poll of `polled`. */
    struct Editor_WorldMapKey polled;
    int polled_valid;
    int serial;
    int missing;
    int poll_wait;
    int ticks_since_render;

    uint32_t* pixels;
    size_t pixels_cap;
    unsigned char* shown;
    size_t shown_cap;

    /** Calls made, for the tests. */
    int polls;
    int renders;
};

void
Editor_WorldMapInit(struct Editor_WorldMap* map);

void
Editor_WorldMapFree(struct Editor_WorldMap* map);

/** Whether `services` can draw a map at all (all four members). */
int
Editor_WorldMapAvailable(const struct EditorUiAppServices* services);

/**
 * Once per tick, before the picker draws: settle the area, poll and render
 * as the key says, and point `picker->underlay` at the picture (cleared when
 * there is none). `chrome_scale` is device pixels per logical pixel, so the
 * picture is rendered at the size it is shown. Returns 1 when the picker's
 * picture changed (a new image, or the old one moved or went).
 */
int
Editor_WorldMapUpdate(
    struct Editor_WorldMap* map,
    const struct EditorUiAppServices* services,
    struct Editor_Picker* picker,
    int chrome_scale);

/**
 * Compose the map into the picture the picker draws (pure; tested): a
 * square whose `shown` byte is 0 is dimmed, a pixel the map has nothing at
 * becomes opaque black, and with `grid` each square's top row and left
 * column are darkened. `shown` is w x h, row 0 the SOUTH row (z0); the
 * picture is (w * px) x (h * px), row 0 the north edge.
 */
void
Editor_WorldMapCompose(
    uint32_t* argb,
    int w,
    int h,
    int px,
    int grid,
    const unsigned char* shown);

#endif
