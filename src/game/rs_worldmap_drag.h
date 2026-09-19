#ifndef SRC_GAME_RS_WORLDMAP_DRAG_H
#define SRC_GAME_RS_WORLDMAP_DRAG_H

/**
 * Drag to pan the world map.
 *
 * The map surface is a builtin with no widget-level drag of its own, and the
 * pan lives in the CS2 world map state, so the press is picked up from the box
 * the emit walk recorded rather than from the widget tree.
 *
 * Anchored, like the reference (OsrsClient.updateWorldMapDrag): the grab
 * records where the view WAS, and every frame sets the view to that origin
 * plus the TOTAL pointer delta converted to tiles. Accumulating per-frame
 * deltas instead loses the sub-tile remainder on every step, so the map slides
 * out from under the pointer over a long drag.
 *
 * Unclamped, also like the reference: dragging past the edge of the map is
 * allowed and dragging back brings it straight back. Clamping the centre parks
 * the view in a corner of the area, where most of the surface is legitimately
 * off-map -- and a map showing nothing looks like a map that stopped loading.
 *
 * Whether the pointer is over the SURFACE is not the same question as whether
 * it is over the MAP: the map's own chrome sits inside the surface box -- the
 * close button, the key panel, the search field, the zoom buttons. The caller
 * answers that with `hover_component_id`, which is -1 over bare map and a real
 * id over anything else. Without it, closing the map also teleports the player
 * to whatever tile the close button was drawn over.
 */

#include "game/rs_worldmap.h"

#include <stdbool.h>

struct UIWorldMapDrag
{
    /** The surface's box in canvas pixels, as the emit walk recorded it. */
    int box_x;
    int box_y;
    int box_w;
    int box_h;

    int active;
    /** Pointer position when the drag was grabbed. */
    int grab_x;
    int grab_y;
    /** Map display position when the drag was grabbed -- the anchor. */
    int origin_x;
    int origin_y;
    /** Set once the view has actually moved, so a release is not also a click. */
    int moved;
};

/** Everything about this frame's pointer the drag needs to decide. */
struct UIWorldMapDragInput
{
    int mouse_x;
    int mouse_y;
    /** The press edge, the held state, and the release edge, separately. */
    int left_down;
    int left_held;
    int left_up;
    /** Something earlier in the frame already took this press. */
    int pointer_consumed;
    int minimenu_visible;
    /** -1 over bare map; any component id means the map's chrome owns it. */
    int hover_component_id;
    /** The surface exists, is mounted, and no ancestor hides it. */
    int surface_live;
};

enum UIWorldMapDragResult
{
    /** Nothing happened, or a pan that moved the view by no whole tile. */
    UI_WORLDMAP_DRAG_NONE = 0,
    /** The view moved; the caller should redraw. */
    UI_WORLDMAP_DRAG_PANNED,
    /**
     * Released without ever panning: this was a CLICK on the map, at
     * `mouse_x`/`mouse_y`.
     *
     * What a click there means is the server's to decide (the reference's
     * ClickWorldMap -- a teleport for staff, ignored for everyone else), which
     * is why this reports the click rather than acting on it.
     */
    UI_WORLDMAP_DRAG_CLICKED
};

/**
 * Advance the drag by one frame.
 *
 * `map` is the CS2 world map state the pan is applied to; it is read for the
 * current display position and zoom and written when the view moves.
 */
enum UIWorldMapDragResult
UIWorldMapDrag_Tick(
    struct UIWorldMapDrag* drag,
    struct UIWorldMapDragInput const* input,
    struct RS_WorldMapState* map);

#endif /* SRC_GAME_RS_WORLDMAP_DRAG_H */
