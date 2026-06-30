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

struct UIClickResult
{
    bool handled;
    bool show_minimenu;
    int minimenu_option;
};

void
uitree_inv_hit_test_slot(
    struct StaticUIComponent const* component,
    int px,
    int py,
    int* out_slot);

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
ui_click_build_minimenu_from_pickset(
    struct GameRunescape* game,
    struct MinimenuPickSet const* picks,
    bool include_walk,
    struct UIMinimenuState* menu);

void
ui_click_use_minimenu_option(
    struct GameRunescape* game,
    int option_index);

#endif
