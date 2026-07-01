#ifndef UI_CLICK_H
#define UI_CLICK_H

#include "interaction_state.h"
#include "minimenu_pickset.h"
#include "ui_minimenu.h"
#include "uitree.h"

#include <stdbool.h>
#include <stdint.h>

struct GameRunescape;
struct LibToriRS_Input;
struct ToriAuxLibCache;
struct UITreeHost;
struct UITreeScrollState;

struct UIClickResult
{
    bool handled;
    bool show_minimenu;
    int minimenu_option;
};

struct UITreeInvPick
{
    int32_t component_index;
    int inv_index;
    int slot;
    int obj_id;
};

void
uitree_inv_hit_test_slot(
    struct StaticUIComponent const* component,
    int px,
    int py,
    int scroll_off_x,
    int scroll_off_y,
    int* out_slot);

bool
uitree_inv_pick_at_point(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    struct UIInventoryPool const* inv_pool,
    int px,
    int py,
    struct UITreeInvPick* out);

void
ui_click_handle_left(
    struct GameRunescape* game,
    struct LibToriRS_Input* input,
    int click_x,
    int click_y);

void
ui_click_handle_right(
    struct GameRunescape* game,
    struct LibToriRS_Input* input,
    int click_x,
    int click_y);

void
ui_click_build_ui_minimenu_at_point(
    struct GameRunescape* game,
    int click_x,
    int click_y,
    int32_t hit_idx,
    struct UIMinimenuState* menu);

void
ui_click_build_minimenu_from_pickset(
    struct GameRunescape* game,
    struct MinimenuPickSet const* picks,
    bool include_walk,
    struct UIMinimenuState* menu);

void
ui_click_use_minimenu_option(
    struct GameRunescape* game,
    int option_index,
    int click_x,
    int click_y);

#endif
