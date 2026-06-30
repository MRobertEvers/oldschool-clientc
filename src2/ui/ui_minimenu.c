#include "ui_minimenu.h"

#include <stdio.h>
#include <string.h>

void
ui_minimenu_reset(struct UIMinimenuState* menu)
{
    if( !menu )
        return;
    memset(menu, 0, sizeof(*menu));
    menu->hovered_option = -1;
}

void
ui_minimenu_hide(struct UIMinimenuState* menu)
{
    if( !menu )
        return;
    menu->visible = false;
    menu->option_count = 0;
    menu->hovered_option = -1;
}

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
    int pick_quaternary_id)
{
    if( !menu || !text || menu->option_count >= UI_MINIMENU_MAX_OPTIONS )
        return false;

    int idx = menu->option_count++;
    snprintf(menu->options[idx].text, sizeof(menu->options[idx].text), "%s", text);
    menu->options[idx].action = action;
    menu->options[idx].action_index = action_index;
    menu->options[idx].pick_kind = pick_kind;
    menu->options[idx].pick_id = pick_id;
    menu->options[idx].pick_secondary_id = pick_secondary_id;
    menu->options[idx].pick_tertiary_id = pick_tertiary_id;
    menu->options[idx].pick_quaternary_id = pick_quaternary_id;
    return true;
}

bool
ui_minimenu_add_option(
    struct UIMinimenuState* menu,
    char const* text,
    enum MinimenuAction action,
    int action_index)
{
    return ui_minimenu_add_option_with_pick(
        menu, text, action, action_index, MINIMENU_PICK_NONE, 0, 0, 0, 0);
}

void
ui_minimenu_show_at(
    struct UIMinimenuState* menu,
    int click_x,
    int click_y,
    int viewport_w,
    int viewport_h)
{
    if( !menu || menu->option_count <= 0 )
        return;

    int width = 120;
    for( int i = 0; i < menu->option_count; i++ )
    {
        int approx = (int)strlen(menu->options[i].text) * 6 + 12;
        if( approx > width )
            width = approx;
    }

    int height = menu->option_count * 15 + 21;
    int x = click_x - (width / 2);
    int y = click_y - 11;

    if( x + width > viewport_w )
        x = viewport_w - width;
    if( x < 0 )
        x = 0;
    if( y + height > viewport_h )
        y = viewport_h - height;
    if( y < 0 )
        y = 0;

    menu->visible = true;
    menu->x = x;
    menu->y = y;
    menu->width = width;
    menu->height = height;
    menu->hovered_option = -1;
}

int
ui_minimenu_hit_option(
    struct UIMinimenuState const* menu,
    int click_x,
    int click_y)
{
    if( !menu || !menu->visible )
        return -1;

    for( int i = 0; i < menu->option_count; i++ )
    {
        int row_top = menu->y + 19 + i * 15;
        int row_bot = row_top + 15;
        if( click_x > menu->x && click_x < menu->x + menu->width && click_y > row_top &&
            click_y < row_bot )
            return i;
    }

    if( click_x < menu->x - 10 || click_x > menu->x + menu->width + 10 ||
        click_y < menu->y - 10 || click_y > menu->y + menu->height + 10 )
        return -2;

    return -1;
}

void
ui_minimenu_update_hover(
    struct UIMinimenuState* menu,
    int mouse_x,
    int mouse_y)
{
    if( !menu || !menu->visible )
        return;

    menu->hovered_option = ui_minimenu_hit_option(menu, mouse_x, mouse_y);
    if( menu->hovered_option < 0 )
        menu->hovered_option = -1;
}
