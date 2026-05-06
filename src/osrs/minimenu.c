#include "minimenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t*
mm_pool_text(
    void* pool_user,
    MinimenuPoolTextFn pool_text,
    const char* text)
{
    if( !text || !pool_text )
        return NULL;
    return pool_text(pool_user, text);
}

static int
mm_parse_rect4(const char* s, int* a, int* b, int* c, int* d)
{
    if( !s || !s[0] )
        return 0;
    return sscanf(s, "%d,%d,%d,%d", a, b, c, d) == 4;
}

static int
mm_parse_pair(const char* s, int* x, int* y)
{
    if( !s || !s[0] )
        return 0;
    return sscanf(s, "%d,%d", x, y) == 2;
}

void
minimenu_regions_default(struct MinimenuIniRegions* r)
{
    if( !r )
        return;
    r->viewport_x0   = 4;
    r->viewport_y0   = 4;
    r->viewport_x1   = 516;
    r->viewport_y1   = 338;
    r->sidebar_x0    = 553;
    r->sidebar_y0    = 205;
    r->sidebar_x1    = 743;
    r->sidebar_y1    = 466;
    r->chat_x0       = 17;
    r->chat_y0       = 357;
    r->chat_x1       = 496;
    r->chat_y1       = 453;
    r->place_vp_max_x = 512;
    r->place_vp_max_y = 334;
    r->place_sb_max_x = 190;
    r->place_sb_max_y = 261;
    r->place_ch_max_x = 479;
    r->place_ch_max_y = 96;
}

void
minimenu_regions_merge_ini(
    struct MinimenuIniRegions* r,
    const char* region_viewport,
    const char* region_sidebar,
    const char* region_chat,
    const char* place_viewport_max,
    const char* place_sidebar_max,
    const char* place_chat_max)
{
    if( !r )
        return;
    minimenu_regions_default(r);
    int a, b, c, d;
    if( mm_parse_rect4(region_viewport, &a, &b, &c, &d) )
    {
        r->viewport_x0 = a;
        r->viewport_y0 = b;
        r->viewport_x1 = c;
        r->viewport_y1 = d;
    }
    if( mm_parse_rect4(region_sidebar, &a, &b, &c, &d) )
    {
        r->sidebar_x0 = a;
        r->sidebar_y0 = b;
        r->sidebar_x1 = c;
        r->sidebar_y1 = d;
    }
    if( mm_parse_rect4(region_chat, &a, &b, &c, &d) )
    {
        r->chat_x0 = a;
        r->chat_y0 = b;
        r->chat_x1 = c;
        r->chat_y1 = d;
    }
    if( mm_parse_pair(place_viewport_max, &a, &b) )
    {
        r->place_vp_max_x = a;
        r->place_vp_max_y = b;
    }
    if( mm_parse_pair(place_sidebar_max, &a, &b) )
    {
        r->place_sb_max_x = a;
        r->place_sb_max_y = b;
    }
    if( mm_parse_pair(place_chat_max, &a, &b) )
    {
        r->place_ch_max_x = a;
        r->place_ch_max_y = b;
    }
}

static int
mm_pt_in_open_rect(int px, int py, int x0, int y0, int x1, int y1)
{
    return px > x0 && py > y0 && px < x1 && py < y1;
}

/** Baseline Y for option row storage index s (0 = top row under header, n-1 = bottom). */
static int
minimenu_option_baseline_y(int menu_y_canvas, int storage_index)
{
    return menu_y_canvas + storage_index * MINIMENU_ROW_HEIGHT + 31;
}

struct MinimenuRenderCommandBuffer*
minimenu_commands_new(int hint)
{
    struct MinimenuRenderCommandBuffer* command_buffer =
        malloc(sizeof(struct MinimenuRenderCommandBuffer));
    if( !command_buffer )
        return NULL;
    memset(command_buffer, 0, sizeof(struct MinimenuRenderCommandBuffer));
    if( hint < 8 )
        hint = 8;
    command_buffer->commands = malloc((size_t)hint * sizeof(struct MinimenuRenderCommand));
    if( !command_buffer->commands )
    {
        free(command_buffer);
        return NULL;
    }
    memset(command_buffer->commands, 0, (size_t)hint * sizeof(struct MinimenuRenderCommand));
    command_buffer->capacity = hint;
    command_buffer->count    = 0;
    return command_buffer;
}

void
minimenu_commands_free(struct MinimenuRenderCommandBuffer* command_buffer)
{
    if( !command_buffer )
        return;
    free(command_buffer->commands);
    free(command_buffer);
}

void
minimenu_commands_reset(struct MinimenuRenderCommandBuffer* command_buffer)
{
    if( !command_buffer )
        return;
    command_buffer->count = 0;
}

static void
mm_ensure_capacity(
    struct MinimenuRenderCommandBuffer* cb,
    int extra)
{
    if( !cb || extra <= 0 )
        return;
    if( cb->count + extra <= cb->capacity )
        return;
    int cap = cb->capacity ? cb->capacity : 8;
    while( cb->count + extra > cap )
        cap *= 2;
    struct MinimenuRenderCommand* nb =
        realloc(cb->commands, (size_t)cap * sizeof(struct MinimenuRenderCommand));
    if( !nb )
        return;
    cb->commands  = nb;
    cb->capacity = cap;
}

static void
mm_push_rect(
    struct MinimenuRenderCommandBuffer* cb,
    int x,
    int y,
    int w,
    int h,
    int color_rgb,
    uint8_t fill)
{
    mm_ensure_capacity(cb, 1);
    struct MinimenuRenderCommand* c = &cb->commands[cb->count++];
    c->kind = MINIMENU_RENDER_COMMAND_RECT;
    c->u.rect.x         = x;
    c->u.rect.y         = y;
    c->u.rect.w         = w;
    c->u.rect.h         = h;
    c->u.rect.color_rgb = color_rgb;
    c->u.rect.fill      = fill;
}

static void
mm_push_text_header(
    struct MinimenuRenderCommandBuffer* cb,
    int font_id,
    const uint8_t* text,
    int x,
    int y,
    int color_rgb)
{
    if( !text )
        return;
    mm_ensure_capacity(cb, 1);
    struct MinimenuRenderCommand* c = &cb->commands[cb->count++];
    c->kind = MINIMENU_RENDER_COMMAND_TEXT_HEADER;
    c->u.text_header.font_id    = font_id;
    c->u.text_header.text       = text;
    c->u.text_header.x          = x;
    c->u.text_header.y          = y;
    c->u.text_header.color_rgb  = color_rgb;
}

static void
mm_push_text_option(
    struct MinimenuRenderCommandBuffer* cb,
    int font_id,
    int option_index,
    int x,
    int y)
{
    mm_ensure_capacity(cb, 1);
    struct MinimenuRenderCommand* c = &cb->commands[cb->count++];
    c->kind = MINIMENU_RENDER_COMMAND_TEXT_OPTION;
    c->u.text_option.font_id      = font_id;
    c->u.text_option.option_index = option_index;
    c->u.text_option.x            = x;
    c->u.text_option.y            = y;
}

void
minimenu_show(
    struct MinimenuState* mm,
    struct MinimenuOptionLine const* lines,
    int line_count,
    int iface_viewport_w,
    int iface_viewport_h,
    const struct MinimenuIniRegions* regions_in,
    int click_x,
    int click_y)
{
    if( !mm || !lines || line_count <= 0 )
        return;

    struct MinimenuIniRegions def;
    const struct MinimenuIniRegions* reg = regions_in;
    if( !reg )
    {
        minimenu_regions_default(&def);
        reg = &def;
    }

    mm->option_count = 0;
    mm->visible      = 0;

    int n = line_count;
    if( n > MINIMENU_MAX_OPTIONS )
        n = MINIMENU_MAX_OPTIONS;

    for( int i = 0; i < n; i++ )
    {
        struct MinimenuOptionLine const* src = &lines[i];
        const char* t = src->text ? src->text : "";
        snprintf(mm->options[i], MINIMENU_OPTION_LEN, "%s", t);
        mm->option_action[i]  = src->action;
        mm->option_param_a[i] = src->param_a;
        mm->option_param_b[i] = src->param_b;
        mm->option_param_c[i] = src->param_c;
    }

    mm->option_count = n;

    int width = 120;
    for( int i = 0; i < n; i++ )
    {
        int w = (int)strlen(mm->options[i]) * 8 + 12;
        if( w > width )
            width = w;
    }
    width += 8;

    const int height = n * MINIMENU_ROW_HEIGHT + 21;

    int menu_area_class = -1;
    if( mm_pt_in_open_rect(
            click_x,
            click_y,
            reg->viewport_x0,
            reg->viewport_y0,
            reg->viewport_x1,
            reg->viewport_y1) )
        menu_area_class = 0;
    else if( mm_pt_in_open_rect(
                 click_x,
                 click_y,
                 reg->sidebar_x0,
                 reg->sidebar_y0,
                 reg->sidebar_x1,
                 reg->sidebar_y1) )
        menu_area_class = 1;
    else if( mm_pt_in_open_rect(
                 click_x,
                 click_y,
                 reg->chat_x0,
                 reg->chat_y0,
                 reg->chat_x1,
                 reg->chat_y1) )
        menu_area_class = 2;

    mm->menu_area = menu_area_class >= 0 ? menu_area_class : 0;

    if( menu_area_class == 1 )
    {
        mm->origin_x = reg->sidebar_x0;
        mm->origin_y = reg->sidebar_y0;
    }
    else if( menu_area_class == 2 )
    {
        mm->origin_x = reg->chat_x0;
        mm->origin_y = reg->chat_y0;
    }
    else
    {
        mm->origin_x = reg->viewport_x0;
        mm->origin_y = reg->viewport_y0;
    }

    int vp_w = iface_viewport_w > 0 ? iface_viewport_w : 765;
    int vp_h = iface_viewport_h > 0 ? iface_viewport_h : 503;

    int x = 0;
    int y = 0;

    if( menu_area_class == 0 )
    {
        x = click_x - (width / 2) - reg->viewport_x0;
        if( x + width > reg->place_vp_max_x )
            x = reg->place_vp_max_x - width;
        if( x < 0 )
            x = 0;

        y = click_y - reg->viewport_y0;
        if( y + height > reg->place_vp_max_y )
            y = reg->place_vp_max_y - height;
        if( y < 0 )
            y = 0;
    }
    else if( menu_area_class == 1 )
    {
        x = click_x - (width / 2) - reg->sidebar_x0;
        if( x < 0 )
            x = 0;
        else if( x + width > reg->place_sb_max_x )
            x = reg->place_sb_max_x - width;

        y = click_y - reg->sidebar_y0;
        if( y < 0 )
            y = 0;
        else if( y + height > reg->place_sb_max_y )
            y = reg->place_sb_max_y - height;
        x += reg->sidebar_x0;
        y += reg->sidebar_y0;
    }
    else if( menu_area_class == 2 )
    {
        x = click_x - (width / 2) - reg->chat_x0;
        if( x < 0 )
            x = 0;
        else if( x + width > reg->place_ch_max_x )
            x = reg->place_ch_max_x - width;

        y = click_y - reg->chat_y0;
        if( y < 0 )
            y = 0;
        else if( y + height > reg->place_ch_max_y )
            y = reg->place_ch_max_y - height;
        x += reg->chat_x0;
        y += reg->chat_y0;
    }
    else
    {
        /* Rare: click outside TS openMenu rects — legacy full-viewport clamp. */
        x = click_x - width / 2;
        if( x + width > vp_w )
            x = vp_w - width;
        if( x < 0 )
            x = 0;
        y = click_y - 11;
        if( y + height > vp_h )
            y = vp_h - height;
        if( y < 0 )
            y = 0;
    }

    mm->visible = 1;
    mm->x       = x;
    mm->y       = y;
    mm->width   = width;
    mm->height  = n * MINIMENU_ROW_HEIGHT + 22;
}

void
minimenu_enqueue(
    struct MinimenuRenderCommandBuffer* cb,
    struct MinimenuState const* mm,
    void* pool_user,
    MinimenuPoolTextFn pool_text,
    int font_id)
{
    if( !cb || !mm || !mm->visible || mm->option_count <= 0 || !pool_text )
        return;

    int x = mm->x;
    int y = mm->y;
    int w = mm->width;
    int h = mm->height;
    int n = mm->option_count;

    int color_bg    = 0x5d5447;
    int color_black = 0x000000;

    mm_push_rect(cb, x, y, w, h, color_bg, 1);
    mm_push_rect(cb, x + 1, y + 1, w - 2, MINIMENU_HEADER_HEIGHT - 2, color_black, 1);
    mm_push_rect(
        cb,
        x + 1,
        y + MINIMENU_HEADER_HEIGHT,
        w - 2,
        h - MINIMENU_HEADER_HEIGHT - 1,
        color_black,
        0);

    const uint8_t* header_text =
        mm_pool_text(pool_user, pool_text, "Choose Option");
    /* Client.ts drawMinimenu: drawString('Choose Option', x + 3, y + 14, ...) */
    mm_push_text_header(cb, font_id, header_text, x + 3, y + 14, color_bg);

    for( int s = 0; s < n; s++ )
    {
        int opt_y = minimenu_option_baseline_y(y, s);
        mm_push_text_option(cb, font_id, s, x + 3, opt_y);
    }
}

int
minimenu_click_option(
    struct MinimenuState* mm,
    int click_x,
    int click_y)
{
    if( !mm || !mm->visible )
        return -1;

    int menu_x = mm->x;
    int menu_y = mm->y;
    int menu_w = mm->width;
    int menu_h = mm->height;
    int n      = mm->option_count;

    if( click_x < menu_x || click_x >= menu_x + menu_w || click_y < menu_y ||
        click_y >= menu_y + menu_h )
    {
        mm->visible = 0;
        return -2;
    }

    if( click_y >= menu_y && click_y < menu_y + MINIMENU_HEADER_HEIGHT )
        return -1;

    int ox = mm->origin_x;
    (void)mm->origin_y;

    int lx = click_x - ox;
    int mx0 = menu_x - ox;

    for( int s = 0; s < n; s++ )
    {
        int opt_y = minimenu_option_baseline_y(menu_y, s);
        /* TS drawMinimenu: clickY > optionY - 13 && clickY < optionY + 3 (canvas Y matches optionY). */
        if( lx > mx0 && lx < mx0 + menu_w && click_y > opt_y - 13 && click_y < opt_y + 3 )
            return s;
    }

    mm->visible = 0;
    return -2;
}
