#ifndef UI_CHAT_MINIMENU_H
#define UI_CHAT_MINIMENU_H

#include "ui/ui_minimenu.h"
#include "ui/uitree.h"

struct GameRunescape;
struct StaticUIChatMinimenuConfig;

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

#endif
