#ifndef UI_CHAT_MINIMENU_H
#define UI_CHAT_MINIMENU_H

#include <stddef.h>

#include "ui/ui_minimenu.h"
#include "ui/uitree.h"

struct GameRunescape;
struct LibToriRS_RenderCommand;
struct StaticUIChatMinimenuConfig;
struct StaticUIComponent;

void
ui_chat_minimenu_add_private_strip(
    struct GameRunescape* game,
    int mouse_x,
    int mouse_y,
    struct UIMinimenuState* menu);

void
ui_chat_minimenu_add_main_box(
    struct GameRunescape* game,
    int mouse_x,
    int mouse_y,
    struct StaticUIChatMinimenuConfig const* config,
    struct UIMinimenuState* menu);

int32_t
uitree_find_chat_builtin_node(struct UITree const* tree);

bool
ui_chat_button_emit(
    struct GameRunescape* game,
    struct StaticUIComponent const* component,
    int step,
    struct LibToriRS_RenderCommand* command,
    char* text_scratch,
    size_t text_scratch_size);

int
ui_chat_button_emit_step_count(struct StaticUIComponent const* component);

void
ui_chat_button_handle_click(
    struct GameRunescape* game,
    struct StaticUIComponent const* component);

#endif
