#ifndef MINIMENU_H
#define MINIMENU_H

#include "collision_map.h"
#include "game.h"
#include "graphics/dash.h"
#include "tori_rs_render.h"

#include <stdbool.h>

struct ToriRSRenderCommandBuffer;

/* Build menu options from option_set and show at (click_x, click_y) in iface-viewport coords.
 * Client.ts showContextMenu: positions menu, sets menuVisible. */
void
minimenu_show(
    struct GGame* game,
    int click_x,
    int click_y);

/* Emit GFX draw commands (TORIRS_GFX_DRAW_RECT / TORIRS_GFX_DRAW_FONT) into buf.
 * Must be called inside a STATE_BEGIN_2D block.  Text lifetimes are extended via
 * game->ui_frame_text_pool (caller resets the pool at FrameBegin). */
void
minimenu_draw(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* buf,
    int font_id,
    struct DashPixFont* font);

/* Check if (click_x, click_y) hits a menu option. Returns option index or -1.
 * If click outside menu, hides menu and returns -2. */
int
minimenu_click_option(
    struct GGame* game,
    int click_x,
    int click_y);

/* Execute the selected menu option (send packet). Called after minimenu_click_option returns >= 0.
 */
void
minimenu_use_option(
    struct GGame* game,
    int option_index);

/* BFS path to dest and send MOVE_GAMECLICK if path exists. Returns true if waypoints >= 0. */
bool
send_move_path_to(
    struct GGame* game,
    struct CollisionMap* cm,
    int src_local_x,
    int src_local_z,
    int dest_local_x,
    int dest_local_z);

/* BFS path to dest and send MOVE_OPCLICK (tile click) if path exists. Returns true if waypoints >= 0. */
bool
send_move_opclick_to(
    struct GGame* game,
    struct CollisionMap* cm,
    int src_local_x,
    int src_local_z,
    int dest_local_x,
    int dest_local_z);

#endif
