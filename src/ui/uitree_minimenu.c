#include "uitree_minimenu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct UIMinimenuLayout
UIMinimenu_LayoutFromLineBox(int line_box)
{
    return UIMinimenu_LayoutFromLineBoxStyled(line_box, UI_MINIMENU_STYLE_DESKTOP);
}

/*
 * The touch geometry, derived from the desktop one so the two cannot drift:
 * the desktop numbers below are all functions of the glyph line box, and the
 * touch popup is the same functions of a taller row band and a taller title
 * bar, with the text centred in whichever band it sits in.
 */
static struct UIMinimenuLayout
minimenu_layout_touch(int box)
{
    /* About twice the desktop stride: a 16-box font's rows go from 15 to 31
     * canvas pixels, which at a phone's 150-200% UI scale is the 48-60 device
     * pixel target a finger needs. The desktop stride is box - 1. */
    int const row_stride = 2 * box - 1;
    int const header_bar_h = box + 8;
    struct UIMinimenuLayout layout;

    layout.line_height = box - 2;
    layout.row_stride = row_stride;
    layout.header_text_y = header_bar_h - 2;
    layout.header_bar_h = header_bar_h;
    layout.separator_y = header_bar_h + 2;
    /* Desktop: option_base_y = separator_y + (row_stride - 2). */
    layout.option_base_y = layout.separator_y + row_stride - 2;
    layout.hover_above = row_stride - 2;
    layout.hover_below = 3;
    /* Desktop: chrome_h = option_base_y + hover_below + 2 - row_stride, the
     * two being the bottom border. */
    layout.chrome_h = layout.option_base_y + layout.hover_below + 2 - row_stride;
    layout.width_pad = 24;
    /* Keeps the finger on the title bar, as the desktop bias keeps the mouse
     * there: box - 5 is header/2 + 3 for a 16-box header. */
    layout.click_y_bias = header_bar_h / 2 + 3;
    layout.border_inset = layout.separator_y + 1;
    layout.text_inset_x = 8;
    layout.header_text_top = 2 + (header_bar_h - box) / 2;
    /* The band is hover_above + hover_below = row_stride + 1 tall; the text
     * box is box + 1, centred. */
    layout.row_text_box_h = box + 1;
    layout.row_text_offset_y = (row_stride + 1 - layout.row_text_box_h) / 2 + 1;
    return layout;
}

struct UIMinimenuLayout
UIMinimenu_LayoutFromLineBoxStyled(int line_box, enum UIMinimenuStyle style)
{
    int const box = line_box > 0 ? line_box : UITREE_MINIMENU_DEFAULT_LINE_BOX;

    if( style == UI_MINIMENU_STYLE_TOUCH )
        return minimenu_layout_touch(box);
    assert(style == UI_MINIMENU_STYLE_DESKTOP);

    struct UIMinimenuLayout layout;
    /* box - 2 matches the historical "line height" the reference chrome was
     * written against (b12 glyph line box 16 -> 14). */
    layout.line_height = box - 2;
    layout.row_stride = box - 1;
    layout.header_text_y = box - 2;
    layout.header_bar_h = box;
    layout.separator_y = box + 2;
    layout.option_base_y = 2 * box - 1;
    layout.chrome_h = box + 5;
    layout.hover_above = box - 3;
    layout.hover_below = 3;
    /* Reference allowance: rows draw at x+3, so 8 leaves a three-pixel inset
     * left and five (four after the one-pixel shadow) right. It was widened to
     * 16 once to stop long rows spilling out of the popup; that spill was a
     * measure returning nothing, not a pad too small, and the real fix is in
     * app_minimenu_open — so this stays on the reference number. */
    layout.width_pad = 8;
    layout.click_y_bias = box - 5;
    layout.border_inset = box + 3;
    /* Reference: rows and title draw at x + 3, the title box starts just under
     * the border, and a row's text box starts one pixel into its band and is
     * row_stride + 2 tall (so box + 1). */
    layout.text_inset_x = 3;
    layout.header_text_top = 2;
    layout.row_text_offset_y = 1;
    layout.row_text_box_h = box + 1;
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
    int line_box,
    UIMinimenuMeasureFn measure,
    void* measure_ud,
    struct UIMinimenuLayout* out_layout,
    int* out_content_width)
{
    return UIMinimenu_PrepareShowStyled(
        menu,
        line_box,
        UI_MINIMENU_STYLE_DESKTOP,
        measure,
        measure_ud,
        out_layout,
        out_content_width);
}

bool
UIMinimenu_PrepareShowStyled(
    struct UIMinimenu const* menu,
    int line_box,
    enum UIMinimenuStyle style,
    UIMinimenuMeasureFn measure,
    void* measure_ud,
    struct UIMinimenuLayout* out_layout,
    int* out_content_width)
{
    assert(menu);
    if( menu->option_count <= 0 )
        return false;
    assert(out_layout);
    assert(out_content_width);

    *out_layout = UIMinimenu_LayoutFromLineBoxStyled(line_box, style);

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
    /* No measure (or a font the caller could not resolve): guess from the
     * character count. The reference never guesses, so this is only ever a
     * degraded path — err wide. b12/p12 average close to seven pixels a glyph
     * and colour tags in a row are free, so the old six-per-character estimate
     * came out narrower than the text it was sizing and the rows drew past the
     * border; eight keeps the popup around the text instead. */
    if( max_w <= 0 )
    {
        size_t max_len = strlen("Choose Option");
        for( int i = 0; i < menu->option_count; i++ )
        {
            size_t const len = strlen(menu->options[i].text);
            if( len > max_len )
                max_len = len;
        }
        max_w = (int)max_len * 8;
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
UIMinimenu_RowTextBox(
    struct UIMinimenu const* menu,
    int option_index,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    assert(menu);
    assert(option_index >= 0);
    assert(option_index < menu->option_count);
    assert(out_x);
    assert(out_y);
    assert(out_w);
    assert(out_h);

    {
        int const band_top = UIMinimenu_OptionY(menu, option_index) - menu->layout.hover_above;
        *out_x = menu->x + menu->layout.text_inset_x;
        *out_y = band_top + menu->layout.row_text_offset_y;
        *out_w = menu->width - 2 * menu->layout.text_inset_x;
        *out_h = menu->layout.row_text_box_h;
    }
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

void
UIMinimenu_AfterimageShow(struct UIMinimenu* menu, int option_index)
{
    struct UIMinimenuAfterimage* image;

    assert(menu);
    assert(menu->visible);
    assert(option_index >= 0);
    assert(option_index < menu->option_count);

    image = &menu->afterimage;
    memset(image, 0, sizeof(*image));
    image->active = true;
    /* The band the tap hit, plus the popup's one-pixel border all round. The
     * band's bottom edge is exclusive (HitOption tests < option y + below), so
     * the box is the band's height plus two. */
    {
        int const band_top =
            UIMinimenu_OptionY(menu, option_index) - menu->layout.hover_above;
        int const band_h = menu->layout.hover_above + menu->layout.hover_below;
        image->x = menu->x;
        image->y = band_top - 1;
        image->w = menu->width;
        image->h = band_h + 2;
    }
    UIMinimenu_RowTextBox(
        menu, option_index, &image->text_x, &image->text_y, &image->text_w, &image->text_h);
    image->font_id = menu->font_id;
    image->color = menu->hovered_option == option_index ? 0xFFFF00 : 0xFFFFFF;
    snprintf(image->text, sizeof(image->text), "%s", menu->options[option_index].text);
    image->cycle_ms = 0;
}

void
UIMinimenu_AfterimageTick(struct UIMinimenu* menu, int delta_ms)
{
    assert(menu);
    assert(delta_ms >= 0);
    if( !menu->afterimage.active )
        return;
    menu->afterimage.cycle_ms += delta_ms;
    if( menu->afterimage.cycle_ms >=
        UITREE_MINIMENU_AFTERIMAGE_HOLD_MS + UITREE_MINIMENU_AFTERIMAGE_FADE_MS )
        menu->afterimage.active = false;
}

bool
UIMinimenu_AfterimageActive(struct UIMinimenu const* menu)
{
    assert(menu);
    return menu->afterimage.active;
}

int
UIMinimenu_AfterimageTrans(struct UIMinimenu const* menu)
{
    int faded;

    assert(menu);
    if( !menu->afterimage.active )
        return 255;
    faded = menu->afterimage.cycle_ms - UITREE_MINIMENU_AFTERIMAGE_HOLD_MS;
    if( faded <= 0 )
        return 0;
    if( faded >= UITREE_MINIMENU_AFTERIMAGE_FADE_MS )
        return 255;
    return faded * 255 / UITREE_MINIMENU_AFTERIMAGE_FADE_MS;
}
