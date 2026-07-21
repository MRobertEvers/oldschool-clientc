#include "uitree_minimenu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct UIMinimenuLayout
UIMinimenu_LayoutFromLineHeight(int line_height)
{
    int const h = line_height > 0 ? line_height : UITREE_MINIMENU_DEFAULT_LINE_HEIGHT;

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

void
UIMinimenu_Reset(struct UIMinimenu* menu)
{
    assert(menu);
    memset(menu, 0, sizeof(*menu));
    menu->hovered_option = -1;
    menu->font_id = -1;
}

void
UIMinimenu_Hide(struct UIMinimenu* menu)
{
    assert(menu);
    menu->visible = false;
    menu->option_count = 0;
    menu->hovered_option = -1;
}

bool
UIMinimenu_AddOption(
    struct UIMinimenu* menu,
    char const* text,
    int action,
    int action_index,
    struct UIMinimenuPick pick)
{
    assert(menu);
    assert(text);
    if( menu->option_count >= UITREE_MINIMENU_MAX_OPTIONS )
        return false;

    int idx = menu->option_count++;
    snprintf(menu->options[idx].text, sizeof(menu->options[idx].text), "%s", text);
    menu->options[idx].action = action;
    menu->options[idx].action_index = action_index;
    menu->options[idx].pick = pick;
    return true;
}

void
UIMinimenu_SortPriorityActions(struct UIMinimenu* menu)
{
    assert(menu);
    if( menu->option_count < 2 )
        return;

    bool sorted = false;
    while( !sorted )
    {
        sorted = true;
        for( int i = 0; i < menu->option_count - 1; i++ )
        {
            if( menu->options[i].action < 1000 && menu->options[i + 1].action > 1000 )
            {
                struct UIMinimenuOption tmp = menu->options[i];
                menu->options[i] = menu->options[i + 1];
                menu->options[i + 1] = tmp;
                sorted = false;
            }
        }
    }
}

bool
UIMinimenu_PrepareShow(
    struct UIMinimenu const* menu,
    int line_height,
    UIMinimenuMeasureFn measure,
    void* measure_ud,
    struct UIMinimenuLayout* out_layout,
    int* out_content_width)
{
    assert(menu);
    if( !out_layout || !out_content_width || menu->option_count <= 0 )
        return false;

    *out_layout = UIMinimenu_LayoutFromLineHeight(line_height);

    int max_w = 0;
    if( measure )
    {
        max_w = measure(measure_ud, menu->font_id, "Choose Option");
        for( int i = 0; i < menu->option_count; i++ )
        {
            int const w = measure(measure_ud, menu->font_id, menu->options[i].text);
            if( w > max_w )
                max_w = w;
        }
    }
    if( max_w <= 0 )
    {
        size_t max_len = strlen("Choose Option");
        for( int i = 0; i < menu->option_count; i++ )
        {
            size_t const len = strlen(menu->options[i].text);
            if( len > max_len )
                max_len = len;
        }
        max_w = (int)max_len * 6;
    }

    *out_content_width = max_w + out_layout->width_pad;
    return true;
}

int
UIMinimenu_Height(struct UIMinimenuLayout const* layout, int option_count)
{
    assert(layout);
    if( option_count < 0 )
        return 0;
    return option_count * layout->row_stride + layout->chrome_h;
}

int
UIMinimenu_OptionY(struct UIMinimenu const* menu, int option_index)
{
    assert(menu);
    if( option_index < 0 || option_index >= menu->option_count )
        return menu->y;

    int const row = (menu->option_count - 1 - option_index) * menu->layout.row_stride;
    return menu->y + row + menu->layout.option_base_y;
}

void
UIMinimenu_ShowAt(
    struct UIMinimenu* menu,
    struct UIMinimenuLayout layout,
    int content_width,
    int click_x,
    int click_y,
    int viewport_w,
    int viewport_h)
{
    assert(menu);
    if( menu->option_count <= 0 )
        return;

    int width = content_width > 0 ? content_width : 120;
    int height = UIMinimenu_Height(&layout, menu->option_count);
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
UIMinimenu_HitOption(struct UIMinimenu const* menu, int click_x, int click_y)
{
    assert(menu);
    if( !menu->visible )
        return -1;

    for( int i = 0; i < menu->option_count; i++ )
    {
        int const option_y = UIMinimenu_OptionY(menu, i);
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

bool
UIMinimenu_UpdateHover(struct UIMinimenu* menu, int mouse_x, int mouse_y)
{
    assert(menu);
    if( !menu->visible )
        return false;

    int hovered = UIMinimenu_HitOption(menu, mouse_x, mouse_y);
    if( hovered < 0 )
        hovered = -1;
    if( hovered == menu->hovered_option )
        return false;
    menu->hovered_option = hovered;
    return true;
}
