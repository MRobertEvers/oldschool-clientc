#ifndef UI_MINIMENU_H
#define UI_MINIMENU_H

#include "minimenu_pickset.h"
#include "osrs/minimenu_action.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Font;

#define UI_MINIMENU_MAX_OPTIONS 10
#define UI_MINIMENU_OPTION_LEN 128
#define UI_MINIMENU_DEFAULT_LINE_HEIGHT 14

struct UIMinimenuLayout
{
    int line_height;
    int row_stride;
    int header_text_y;
    int header_bar_h;
    int separator_y;
    int option_base_y;
    int chrome_h;
    int hover_above;
    int hover_below;
    int width_pad;
    int click_y_bias;
    int border_inset;
};

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
    struct UIMinimenuLayout layout;
    struct UIMinimenuOption options[UI_MINIMENU_MAX_OPTIONS];
    int option_count;
};

struct UIMinimenuLayout
ui_minimenu_layout_from_line_height(int line_height);

bool
ui_minimenu_prepare_show(
    struct UIMinimenuState const* menu,
    struct ToriDraw_Font* font,
    struct UIMinimenuLayout* out_layout,
    int* out_content_width);

int
ui_minimenu_height(
    struct UIMinimenuLayout const* layout,
    int option_count);

int
ui_minimenu_option_y(
    struct UIMinimenuState const* menu,
    int option_index);

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
    struct UIMinimenuLayout layout,
    int content_width,
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
