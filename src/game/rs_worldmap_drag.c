#include "game/rs_worldmap_drag.h"

#include <assert.h>

enum UIWorldMapDragResult
UIWorldMapDrag_Tick(
    struct UIWorldMapDrag* drag,
    struct UIWorldMapDragInput const* input,
    struct RS_WorldMapState* map)
{
    assert(drag);
    assert(input);

    /* Idle frames skip the work; an in-progress drag still reaches its release
     * handling below. */
    if( !drag->active && !input->left_down )
        return UI_WORLDMAP_DRAG_NONE;

    if( !input->surface_live || drag->box_w <= 0 || drag->box_h <= 0 || !map )
    {
        drag->active = 0;
        return UI_WORLDMAP_DRAG_NONE;
    }

    /* The grab. A clickable component under the pointer owns the press, which
     * is how the map's own chrome keeps its clicks. */
    if( !drag->active && !input->pointer_consumed && !input->minimenu_visible &&
        input->hover_component_id < 0 && input->left_down &&
        input->mouse_x >= drag->box_x && input->mouse_x < drag->box_x + drag->box_w &&
        input->mouse_y >= drag->box_y && input->mouse_y < drag->box_y + drag->box_h )
    {
        int display_x = 0;
        int display_y = 0;

        RS_WorldMap_DisplayPosition(map, &display_x, &display_y);
        if( display_x < 0 || display_y < 0 )
            return UI_WORLDMAP_DRAG_NONE;
        drag->active = 1;
        drag->grab_x = input->mouse_x;
        drag->grab_y = input->mouse_y;
        drag->origin_x = display_x;
        drag->origin_y = display_y;
        drag->moved = 0;
    }

    if( !drag->active )
        return UI_WORLDMAP_DRAG_NONE;

    if( !input->left_held || input->left_up )
    {
        int const was_a_click = !drag->moved;

        drag->active = 0;
        return was_a_click ? UI_WORLDMAP_DRAG_CLICKED : UI_WORLDMAP_DRAG_NONE;
    }

    {
        int scale_fp = RS_WorldMap_ZoomScaleFp(map);
        int dx = input->mouse_x - drag->grab_x;
        int dy = input->mouse_y - drag->grab_y;
        int next_x;
        int next_y;
        int current_x = 0;
        int current_y = 0;

        if( scale_fp <= 0 )
            scale_fp = RS_WORLDMAP_ZOOM_SCALE_ONE;
        /* Screen y grows downward, map y northward, and the map moves opposite
         * the pointer -- the tile under the cursor stays under it.
         *
         * Both terms are against the ORIGIN, not the previous frame: that is
         * what keeps the sub-tile remainder from being dropped once per frame
         * and the map from sliding behind a long drag. */
        next_x = drag->origin_x - dx * RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;
        next_y = drag->origin_y + dy * RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;

        RS_WorldMap_DisplayPosition(map, &current_x, &current_y);
        if( next_x == current_x && next_y == current_y )
            return UI_WORLDMAP_DRAG_NONE;

        RS_WorldMap_SetDisplayPosition(map, next_x, next_y);
        drag->moved = 1;
        return UI_WORLDMAP_DRAG_PANNED;
    }
}
