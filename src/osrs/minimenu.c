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
    int click_x,
    int click_y)
{
    if( !mm || !lines || line_count <= 0 )
        return;

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
    int height = n * MINIMENU_ROW_HEIGHT + MINIMENU_HEADER_HEIGHT + 4;

    int vp_w = iface_viewport_w > 0 ? iface_viewport_w : 765;
    int vp_h = iface_viewport_h > 0 ? iface_viewport_h : 503;

    int x = click_x - width / 2;
    if( x + width > vp_w )
        x = vp_w - width;
    if( x < 0 )
        x = 0;

    int y = click_y - 11;
    if( y + height > vp_h )
        y = vp_h - height;
    if( y < 0 )
        y = 0;

    mm->visible = 1;
    mm->x       = x;
    mm->y       = y;
    mm->width   = width;
    mm->height  = height;
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

    int color_bg    = 0x5F5000;
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
        1);

    const uint8_t* header_text =
        mm_pool_text(pool_user, pool_text, "Choose Option");
    mm_push_text_header(cb, font_id, header_text, x + 3, y + 3, color_bg);

    for( int i = 0; i < n; i++ )
    {
        int row_top = y + MINIMENU_HEADER_HEIGHT + i * MINIMENU_ROW_HEIGHT;
        mm_push_text_option(cb, font_id, i, x + 3, row_top + 2);
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
    int n      = mm->option_count;

    for( int i = 0; i < n; i++ )
    {
        int row_top = menu_y + MINIMENU_HEADER_HEIGHT + i * MINIMENU_ROW_HEIGHT;
        int row_bot = row_top + MINIMENU_ROW_HEIGHT;
        if( click_x >= menu_x && click_x < menu_x + menu_w && click_y >= row_top &&
            click_y < row_bot )
        {
            return i;
        }
    }

    if( click_x >= menu_x && click_x < menu_x + menu_w && click_y >= menu_y &&
        click_y < menu_y + MINIMENU_HEADER_HEIGHT )
    {
        return -1;
    }

    mm->visible = 0;
    return -2;
}
