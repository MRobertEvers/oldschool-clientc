#ifndef MINIMENU_H
#define MINIMENU_H

#include "collision_map.h"
#include "game.h"
#include "graphics/dash.h"

#include <stdbool.h>

/* Build menu options from hovered_scene_element and show at (click_x, click_y).
 * Client.ts showContextMenu: positions menu, sets menuVisible. */
void
minimenu_show(
    struct GGame* game,
    int click_x,
    int click_y);

/* Draw menu. Client.ts drawMenu: fill rect, "Choose Option", option strings. */
void
minimenu_draw(
    struct GGame* game,
    int* pixel_buffer,
    int stride,
    int clip_l,
    int clip_t,
    int clip_r,
    int clip_b,
    int offset_x,
    int offset_y);

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
