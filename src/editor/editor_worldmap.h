#ifndef SRC_EDITOR_EDITOR_WORLDMAP_H
#define SRC_EDITOR_EDITOR_WORLDMAP_H

/*
 * The world map under the square picker, drawn and zoomed the way the CS2
 * world map (interface 595) is: the page's area as the client's own world-map
 * renderer draws it, at whatever zoom the picker is at, with the map element
 * icons and area labels over it.
 *
 * The App renders (EditorUiAppServices worldmap_area / _poll / _render_tile /
 * image_publish / _icons, src/app/app_map_editor.c over
 * game/rs_worldmap_overview.h); this decides what to ask for and when, and
 * composes what comes back into the picker's underlay:
 *
 *   tiles      the map is cut into EDITOR_WORLDMAP_TILE px squares of the
 *              zoom's global pixel space, each rendered once for its zoom,
 *              area and page and kept in one of EDITOR_UI_MAP_IMAGE_SLOTS
 *              scene sprites (least recently drawn reused). Every picture is
 *              the same size, so a renderer that packs sprites into an atlas
 *              reuses the picture's place instead of filling the atlas (a
 *              picture that changed size on every zoom is what blanked the
 *              GL map at close zooms). A pan renders only the tiles it brings
 *              into the well.
 *   budget     tiles are rendered nearest the well's middle first, up to
 *              EDITOR_WORLDMAP_RENDER_BUDGET source tiles of work a tick (at
 *              least one tile); until a zoom's tiles are in, the last zoom
 *              whose tiles were all in is drawn scaled under them -- the
 *              transition -- so a zoom never flashes black and never stalls
 *              a frame.
 *   loading    the visible squares are polled every tick while regions are
 *              still arriving; a tile drawn before then is redrawn when the
 *              serial moves (at most every EDITOR_WORLDMAP_LOADING_TICKS).
 *   picture    the map; squares the page does not show (no map in any
 *              storage, another page, filtered out) dimmed, black where the
 *              map has nothing, a faint grid from EDITOR_WORLDMAP_GRID_CELL px
 *              a square.
 *   icons      from EDITOR_WORLDMAP_ICON_FROM px a square the map element
 *              icons fade in (fully drawn from EDITOR_WORLDMAP_ICON_FULL);
 *              labels by their text size, large ones first.
 *
 * The pictures are rendered at LOGICAL pixels and drawn at the chrome's
 * scale: a renderer's sprite atlas (2048 square on GL3, shared by every UI
 * sprite) cannot hold a device-pixel map of a large well.
 *
 * Without the services (the headless harness, the web) nothing is drawn and
 * the picker fills its cells as before.
 */

#include "editor/editor_ui.h"
#include "editor/panels/edpanel_picker.h"

#include <stddef.h>
#include <stdint.h>

/** A picture's side, px. */
#define EDITOR_WORLDMAP_TILE 128
/** The grid is drawn from this many px a square. */
#define EDITOR_WORLDMAP_GRID_CELL 8
/** Work a tick may spend rendering tiles: source tiles visited. */
#define EDITOR_WORLDMAP_RENDER_BUDGET (3 * 1024 * 1024)
/** Ticks between redraws of tiles drawn while regions were still loading. */
#define EDITOR_WORLDMAP_LOADING_TICKS 10
/** Ticks between polls while regions are still loading. */
#define EDITOR_WORLDMAP_POLL_TICKS 1
/** Ticks before asking again for areas the cache has not loaded. */
#define EDITOR_WORLDMAP_AREA_RETRY_TICKS 30
/** Ticks before asking again for icons whose config or sprite was cold. */
#define EDITOR_WORLDMAP_ICON_RETRY_TICKS 6
/** Map element icons: from this zoom faint, fully drawn from one pixel a
 *  tile (595's farthest zoom). */
#define EDITOR_WORLDMAP_ICON_FROM 40.0
#define EDITOR_WORLDMAP_ICON_FULL 64.0
/** Area labels by text size (0 small, 1 medium, 2 large): the zoom each
 *  starts at; fully drawn at 1.6 times it. */
#define EDITOR_WORLDMAP_LABEL_LARGE_FROM 6.0
#define EDITOR_WORLDMAP_LABEL_MEDIUM_FROM 16.0
#define EDITOR_WORLDMAP_LABEL_SMALL_FROM 40.0
/** Icons asked for at once. */
#define EDITOR_WORLDMAP_ICONS_MAX 1200

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

/** One kept picture: tile (tx, ty) of `zoom`'s global pixel space. */
struct Editor_WorldMapTile
{
    int used;
    int area_id;
    double zoom;
    int tx;
    int ty;
    /** What the page showed when it was drawn (atlas, page, filters). */
    uint32_t content;
    /** The bake serial it was drawn at, and whether regions were still to
     *  come then (so it is drawn again when they land). */
    int serial;
    int stale;
    int scene_id;
    uint32_t version;
    /** The tick it was last drawn on (the least recent is reused). */
    unsigned long stamp;
};

struct Editor_WorldMap
{
    /** The page the area id answers for, by the atlas area's internal name. */
    char area_name[EDITOR_PICKER_NAME_MAX];
    int area_id;
    int area_retry;

    struct Editor_WorldMapTile tiles[EDITOR_UI_MAP_IMAGE_SLOTS];
    unsigned long tick;
    /** The last zoom whose visible tiles were all in: the stand-in. */
    double shown_zoom;
    unsigned long last_redraw_tick;

    /** The squares last polled, the overview's serial and what it lacks. */
    int polled_valid;
    int polled_area;
    int polled_x0;
    int polled_z0;
    int polled_w;
    int polled_h;
    int polled_scale;
    int serial;
    int missing;
    int poll_wait;

    /** The icons of the squares last asked about. */
    struct EditorUiMapIcon* icons;
    int icon_count;
    int icons_valid;
    int icons_area;
    int icons_x0;
    int icons_z0;
    int icons_w;
    int icons_h;
    int icons_pending;
    int icons_wait;

    uint32_t* pixels;
    uint32_t* shown;
    size_t shown_cap;
    /** The underlay being built this tick (compared with the picker's). */
    struct Editor_PickerUnderlay next;

    /** Calls made, for the tests. */
    int polls;
    int renders;
    int icon_asks;
};

void
Editor_WorldMapInit(struct Editor_WorldMap* map);

void
Editor_WorldMapFree(struct Editor_WorldMap* map);

/** Whether `services` can draw a map at all (area, poll, render_tile and
 *  image_publish). */
int
Editor_WorldMapAvailable(const struct EditorUiAppServices* services);

/**
 * Once per tick, before the picker draws: settle the area, poll, render the
 * tiles the view lacks within the budget, and fill `picker->underlay`
 * (inactive when there is no map). Returns 1 when the underlay changed.
 */
int
Editor_WorldMapUpdate(
    struct Editor_WorldMap* map,
    const struct EditorUiAppServices* services,
    struct Editor_Picker* picker);

/**
 * Compose one rendered picture into what the picker draws (pure; tested).
 * `argb` is size x size pixels of `zoom`'s global pixel space from (gx0,
 * gy0). `cells` says, for squares x in [sx0, sx0 + sw) and north-based rows
 * r (r = EDITOR_PICKER_WORLD_SQUARES - 1 - map_z) in [r0, r0 + sh), at
 * cells[(r - r0) * sw + (x - sx0)]: 0 when the page does not show that
 * square, else 0xFFRRGGBB its colour. A square not shown is dimmed; a pixel
 * the map has nothing at takes its square's colour when shown and is opaque
 * black when not (a square no area maps stays a plain cell); with `grid`
 * each square's top row and left column are darkened.
 */
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
    const uint32_t* cells);

/** How faint something that starts at zoom `from` and is fully drawn at
 *  `full` is at `zoom`: 255 invisible .. 0 opaque. */
int
Editor_WorldMapFade(
    double zoom,
    double from,
    double full);

#endif
