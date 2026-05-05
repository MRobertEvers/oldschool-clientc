#ifndef MINIMENU_GAME_H
#define MINIMENU_GAME_H

#include "minimenu.h"
#include "tori_rs_render.h"

#include <stdbool.h>

struct CollisionMap;
struct GGame;
struct GInput;
struct WorldOption;
struct WorldOptionSet;

/* Default row for closed-menu tooltip / crosshair (Client.ts menuOption[menuNumEntries - 1]). */
void
minimenu_game_world_ts_default_row(
    struct WorldOptionSet const* os,
    int mouse_tile_x,
    int mouse_tile_z,
    struct WorldOption* out);

/* ToriRS bridge: expand high-level minimenu commands from `cb` into `out`. `font` must be
 * non-NULL for option text; option rows are re-pooled via `pool_text` (same contract as enqueue). */
void
minimenu_translate_commands(
    struct MinimenuRenderCommandBuffer* cb,
    struct MinimenuState const* mm,
    void* pool_user,
    MinimenuPoolTextFn pool_text,
    struct ToriRSRenderCommandBuffer* out,
    int font_id,
    struct DashPixFont* font,
    int mouse_x,
    int mouse_y);

void
minimenu_game_show(
    struct GGame* game,
    int click_x,
    int click_y);

/** After `uitree_sync_hover_option_set`, builds the same option rows as `minimenu_game_show`. */
int
minimenu_game_collect_lines(
    struct GGame* game,
    struct MinimenuOptionLine* lines_out,
    int max_lines);

/** Sync hover at (cx,cy), take primary row (index 0 after TS reversal), dispatch like menu pick.
 * @return 1 if an option was dispatched or handled (including mode switches), 0 if nothing to do. */
int
minimenu_game_use_primary_at(
    struct GGame* game,
    struct GInput* input,
    int cx,
    int cy);

void
minimenu_game_enqueue(
    struct GGame* game,
    int font_id);

void
minimenu_game_translate_commands(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* out,
    int font_id,
    struct DashPixFont* font);

int
minimenu_game_click_option(
    struct GGame* game,
    int click_x,
    int click_y);

void
minimenu_game_use_option(
    struct GGame* game,
    struct GInput* input,
    int option_index);

bool
minimenu_game_send_move_path_to(
    struct GGame* game,
    struct CollisionMap* cm,
    int src_local_x,
    int src_local_z,
    int dest_local_x,
    int dest_local_z);

bool
minimenu_game_send_move_opclick_to(
    struct GGame* game,
    struct CollisionMap* cm,
    int src_local_x,
    int src_local_z,
    int dest_local_x,
    int dest_local_z);

#endif
