#include "ui_minimenu.h"

#include "toridraw/toridraw_font.h"

#include <stdio.h>
#include <string.h>

struct UIMinimenuLayout
ui_minimenu_layout_from_line_height(int line_height)
{
    int const h =
        line_height > 0 ? line_height : UI_MINIMENU_DEFAULT_LINE_HEIGHT;

    struct UIMinimenuLayout layout;
    layout.line_height = h;
    layout.row_stride = h + 1;
    layout.header_text_y = h;
    layout.header_bar_h = h + 2;
    layout.separator_y = h + 4;
    layout.option_base_y = 2 * h + 3;
    layout.chrome_h = h + 7;
    layout.hover_above = h - 1;
    layout.hover_below = 3;
    layout.width_pad = 8;
    layout.click_y_bias = h - 3;
    layout.border_inset = h + 5;
    return layout;
}

bool
ui_minimenu_prepare_show(
    struct UIMinimenuState const* menu,
    struct ToriDraw_Font* font,
    struct UIMinimenuLayout* out_layout,
    int* out_content_width)
{
    if( !menu || !out_layout || !out_content_width || menu->option_count <= 0 )
        return false;

    *out_layout = ui_minimenu_layout_from_line_height(font ? font->line_height : 0);

    int max_w = 0;
    if( font )
    {
        max_w = ToriDraw2D_MeasureString(font, "Choose Option");
        for( int i = 0; i < menu->option_count; i++ )
        {
            int const w = ToriDraw2D_MeasureString(font, menu->options[i].text);
            if( w > max_w )
                max_w = w;
        }
    }
    else
    {
        size_t header_len = strlen("Choose Option");
        if( header_len > (size_t)max_w )
            max_w = (int)header_len;
        for( int i = 0; i < menu->option_count; i++ )
        {
            size_t const len = strlen(menu->options[i].text);
            if( len > (size_t)max_w )
                max_w = (int)len;
        }
        max_w *= 6;
    }

    *out_content_width = max_w + out_layout->width_pad;
    return true;
}

int
ui_minimenu_height(
    struct UIMinimenuLayout const* layout,
    int option_count)
{
    if( !layout || option_count < 0 )
        return 0;
    return option_count * layout->row_stride + layout->chrome_h;
}

int
ui_minimenu_option_y(
    struct UIMinimenuState const* menu,
    int option_index)
{
    if( !menu || option_index < 0 || option_index >= menu->option_count )
        return menu ? menu->y : 0;

    int const row =
        (menu->option_count - 1 - option_index) * menu->layout.row_stride;
    return menu->y + row + menu->layout.option_base_y;
}

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
    struct UIMinimenuLayout layout,
    int content_width,
    int click_x,
    int click_y,
    int viewport_w,
    int viewport_h)
{
    if( !menu || menu->option_count <= 0 )
        return;

    int width = content_width > 0 ? content_width : 120;
    int height = ui_minimenu_height(&layout, menu->option_count);
    int x = click_x - (width / 2);
    int y = click_y - layout.click_y_bias;

    if( x + width > viewport_w )
        x = viewport_w - width;
    if( x < 0 )
        x = 0;
    if( y + height > viewport_h )
        y = viewport_h - height;
    if( y < 0 )
        y = 0;

    menu->layout = layout;
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
        int const option_y = ui_minimenu_option_y(menu, i);
        if( click_x > menu->x && click_x < menu->x + menu->width &&
            click_y > option_y - menu->layout.hover_above &&
            click_y < option_y + menu->layout.hover_below )
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
