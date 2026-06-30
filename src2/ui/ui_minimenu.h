#ifndef UI_MINIMENU_H
#define UI_MINIMENU_H

#include "minimenu_pickset.h"
#include "osrs/minimenu_action.h"

#include <stdbool.h>
#include <stdint.h>

#define UI_MINIMENU_MAX_OPTIONS 10
#define UI_MINIMENU_OPTION_LEN 128

struct UIMinimenuOption
{
    char text[UI_MINIMENU_OPTION_LEN];
    enum MinimenuAction action;
    int action_index;
    enum MinimenuPickKind pick_kind;
    int pick_id;
    int pick_secondary_id;
    int pick_tertiary_id;
    int pick_quaternary_id;
};

struct UIMinimenuState
{
    bool visible;
    int x;
    int y;
    int width;
    int height;
    int hovered_option;
    struct UIMinimenuOption options[UI_MINIMENU_MAX_OPTIONS];
    int option_count;
};

void
ui_minimenu_reset(struct UIMinimenuState* menu);

void
ui_minimenu_hide(struct UIMinimenuState* menu);

bool
ui_minimenu_add_option(
    struct UIMinimenuState* menu,
    char const* text,
    enum MinimenuAction action,
    int action_index);

bool
ui_minimenu_add_option_with_pick(
    struct UIMinimenuState* menu,
    char const* text,
    enum MinimenuAction action,
    int action_index,
    enum MinimenuPickKind pick_kind,
    int pick_id,
    int pick_secondary_id,
    int pick_tertiary_id,
    int pick_quaternary_id);

void
ui_minimenu_show_at(
    struct UIMinimenuState* menu,
    int click_x,
    int click_y,
    int viewport_w,
    int viewport_h);

int
ui_minimenu_hit_option(
    struct UIMinimenuState const* menu,
    int click_x,
    int click_y);

void
ui_minimenu_update_hover(
    struct UIMinimenuState* menu,
    int mouse_x,
    int mouse_y);

#endif
