#ifndef UI_INPUT_H
#define UI_INPUT_H

#include "uitree.h"

#include <stdbool.h>
#include <stdint.h>

struct UITreeHost;
struct UITreeScrollState;

struct UIInputState
{
    int32_t hovered;
    int32_t pressed;
};

enum UIInputEventKind
{
    UI_INPUT_MOVE = 0,
    UI_INPUT_DOWN,
    UI_INPUT_UP,
};

struct UIInputEvent
{
    enum UIInputEventKind kind;
    int x;
    int y;
    int button;
};

struct UIInputResult
{
    int32_t hovered;
    int32_t prev_hovered;
    int32_t clicked;
    bool hover_changed;
};

bool
uitree_point_in_component(
    struct StaticUIElemPosition const* position,
    int px,
    int py);

int32_t
uitree_hit_test_recursive(
    struct UITree const* tree,
    int32_t node_index,
    int px,
    int py);

int32_t
uitree_hit_test(
    struct UITree const* tree,
    int px,
    int py);

bool
uitree_component_is_pass_through(
    struct StaticUIComponent const* component,
    struct UITreeHost const* host);

int32_t
uitree_hit_test_interactive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int px,
    int py);

struct UIInputResult
uitree_input_update(
    struct UIInputState* state,
    struct UITree const* tree,
    struct UIInputEvent event);

#endif
