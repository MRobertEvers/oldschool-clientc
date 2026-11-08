#ifndef SRC_EDITOR_EDITOR_PANEL_REGION_H
#define SRC_EDITOR_EDITOR_PANEL_REGION_H

/**
 * The Region tools in the world (editor/editor_region.h plans them,
 * editor/panels/edpanel_region.h makes their commands):
 *
 *   Region brush (O)  a disc swept along the drag, in continuous tile space
 *                     (the point under the pointer, not the hovered tile);
 *                     laid on release as one undo step. Alt at the press
 *                     takes the overlay off instead.
 *   Lasso (J)         a drag draws a loop, laid on release; a click without
 *                     a drag drops a corner, and a click on the first corner
 *                     or a double-click closes the polygon. Alt at the first
 *                     press subtracts. Escape drops it.
 *   Tidy (Y)          a click on an overlay refits the run of it the tile
 *                     belongs to, its steps smoothed.
 *
 * With the Region section's target on Underlay, the brush and the lasso
 * paint the palette's underlay instead, the rim dithered.
 */

#include "editor/editor_region.h"
#include "editor/panels/edpanel_region.h"

#include <stdint.h>

struct App;
struct Editor_Panel;
struct EditorUi;

/** Points one stroke or loop keeps. */
#define EDITOR_REGION_STROKE_POINTS 2048

struct Editor_RegionStroke
{
    /** A brush drag, or a lasso, is open. */
    int active;
    /** EDITOR_TOOL_REGION or EDITOR_TOOL_LASSO. */
    int tool;
    int level;
    /** Alt was held at the press: the overlay comes off. */
    int subtract;
    /** The button went down on this stroke and is still held. */
    int held;
    /** Lasso: 0 the first press is pending (a click or a drag?), 1 a
     *  freehand drag, 2 a polygon of clicked corners. */
    int lasso_mode;
    /** Lasso: the last corner click, for the double-click. */
    uint32_t last_click_ms;
    /** The points, absolute world tiles (continuous). */
    int count;
    double xs[EDITOR_REGION_STROKE_POINTS];
    double zs[EDITOR_REGION_STROKE_POINTS];
    /** The pointer's point this frame (the brush outline, the lasso's
     *  rubber band). */
    int hover_valid;
    double hover_x;
    double hover_z;
    int hover_level;
    /** The preview's plan, kept while nothing it reads changed. */
    uint32_t preview_key;
    int preview_count;
    struct Editor_RegionStats preview_stats;
};

/** The point under the pointer in world tiles (continuous), on the level
 *  the tools edit: the hovered tile's two triangles as drawn, the pointer
 *  placed in whichever holds it. 0 off the world. */
int
Editor_PanelRegionHoverPoint(
    struct Editor_Panel* panel,
    struct App* app,
    double* out_x,
    double* out_z,
    int* out_level);

/** Once a frame with a Region tool armed: tracks the pointer, extends an
 *  open drag. Returns 1 when the preview moved. */
int
Editor_PanelRegionTrack(
    struct Editor_Panel* panel,
    struct App* app);

/** The left button went down with the Region brush or the Lasso armed. */
int
Editor_PanelRegionPress(
    struct Editor_Panel* panel,
    struct App* app,
    int alt);

/** The left button came up: a brush stroke or a freehand loop is laid, a
 *  lasso click drops a corner or closes the polygon. Returns the tiles
 *  changed. */
int
Editor_PanelRegionRelease(
    struct Editor_Panel* panel,
    struct App* app);

/** Escape, a tool change or the editor hiding: drop the open stroke. 1 when
 *  there was one. */
int
Editor_PanelRegionCancel(
    struct Editor_Panel* panel,
    const char* why);

/** Tidy at a scene tile. Returns the tiles changed. */
int
Editor_PanelTidyAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level);

/**
 * The preview: what a release now would write (an overlay plan; for the
 * underlay, the tiles the dither paints as shape 0). *out_tiles points into
 * a buffer the panel keeps until the next call. Returns the count; 0 with
 * nothing open.
 */
int
Editor_PanelRegionPreview(
    struct Editor_Panel* panel,
    struct App* app,
    const struct Editor_RegionTile** out_tiles,
    struct Editor_RegionStats* out_stats);

/**
 * The outline to draw, world tiles: the brush's disc round the pointer (and
 * the swept points of an open stroke as one polyline), or the lasso's loop
 * with the rubber band to the pointer. Writes up to `max` points; *out_closed
 * is 1 for a closed loop. Returns the count.
 */
int
Editor_PanelRegionOutline(
    struct Editor_Panel* panel,
    double* xs,
    double* zs,
    int max,
    int* out_closed);

#endif
