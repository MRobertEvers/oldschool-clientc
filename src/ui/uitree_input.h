#ifndef SRC_UITREE_INPUT_H
#define SRC_UITREE_INPUT_H

#include "uitree.h"
#include "uitree_host.h"
#include "uitree_scroll.h"

#include <stdbool.h>
#include <stdint.h>

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
UITree_PointInComponent(
    struct UITreeElemPosition const* position,
    int px,
    int py);

int32_t
UITree_HitTestRecursive(
    struct UITree const* tree,
    int32_t node_index,
    int px,
    int py);

int32_t
UITree_HitTest(
    struct UITree const* tree,
    int px,
    int py);

bool
UITree_ComponentIsPassThrough(
    struct UITreeComponent const* component,
    struct UITreeHost const* host);

int32_t
UITree_HitTestInteractive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int px,
    int py);

struct UIInputResult
UITree_InputUpdate(
    struct UIInputState* state,
    struct UITree const* tree,
    struct UIInputEvent event);

#endif
