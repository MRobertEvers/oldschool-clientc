#ifndef MINIMENU_H
#define MINIMENU_H

#include "minimenu_regions.h"
#include "minimenu_state.h"

#include <stdint.h>

/** Built-in minimenu layout (matches Client.ts). */
#define MINIMENU_ROW_HEIGHT 15
#define MINIMENU_HEADER_HEIGHT 18

/** One row from the host (e.g. game) before `minimenu_show` copies into `MinimenuState`. */
struct MinimenuOptionLine
{
    const char* text; /* UTF-8; copied into state; must live until `minimenu_show` returns */
    int action;
    int param_a;
    int param_b;
    int param_c;
};

/** Host provides UTF-8 pinning for draw commands; pointer valid for the rest of the frame. */
typedef const uint8_t* (*MinimenuPoolTextFn)(
    void* pool_user,
    const char* utf8);

enum MinimenuRenderCommandKind
{
    MINIMENU_RENDER_COMMAND_RECT = 0,
    MINIMENU_RENDER_COMMAND_TEXT_HEADER,
    MINIMENU_RENDER_COMMAND_TEXT_OPTION,
};

struct MinimenuRenderCommand
{
    enum MinimenuRenderCommandKind kind;
    union
    {
        struct
        {
            int x;
            int y;
            int w;
            int h;
            int color_rgb;
            uint8_t fill;
        } rect;
        struct
        {
            int font_id;
            const uint8_t* text;
            int x;
            int y;
            int color_rgb;
        } text_header;
        struct
        {
            int font_id;
            int option_index;
            int x;
            int y;
        } text_option;
    } u;
};

struct MinimenuRenderCommandBuffer
{
    struct MinimenuRenderCommand* commands;
    int count;
    int capacity;
};

struct MinimenuRenderCommandBuffer*
minimenu_commands_new(int hint);

void
minimenu_commands_free(struct MinimenuRenderCommandBuffer* command_buffer);

void
minimenu_commands_reset(struct MinimenuRenderCommandBuffer* command_buffer);

/* Record minimenu geometry/text into `cb`. Strings must live until translate; pool via `pool_text`.
 */
void
minimenu_enqueue(
    struct MinimenuRenderCommandBuffer* cb,
    struct MinimenuState const* mm,
    void* pool_user,
    MinimenuPoolTextFn pool_text,
    int font_id);

/* Copy `lines[0..line_count-1]` into state (host supplies full list, e.g. world options + Cancel).
 */
void
minimenu_show(
    struct MinimenuState* mm,
    struct MinimenuOptionLine const* lines,
    int line_count,
    int iface_viewport_w,
    int iface_viewport_h,
    const struct MinimenuIniRegions* regions,
    int click_x,
    int click_y);

/* Check if (click_x, click_y) hits a menu option. Returns option index or -1.
 * If click outside menu, sets mm->visible = 0 and returns -2. */
int
minimenu_click_option(
    struct MinimenuState* mm,
    int click_x,
    int click_y);

#endif
