#ifndef UI_INPUT_ADAPTER_H
#define UI_INPUT_ADAPTER_H

#include "ui_behavior.h"
#include "ui_input.h"

#include <stdbool.h>
#include <stdint.h>

struct LibToriRS_Input;
struct ToriAuxLibVM;
struct CS2VM;

struct UIInputAdapter
{
    struct UIInputState state;
    int32_t hovered_node;
};

void
ui_input_adapter_init(struct UIInputAdapter* adapter);

struct UIInputResult
ui_input_adapter_process(
    struct UIInputAdapter* adapter,
    struct UITree* tree,
    struct LibToriRS_Input* input,
    struct UITreeBehaviorHost* host);

void
ui_input_adapter_init_behavior_host(
    struct UITreeBehaviorHost* host,
    struct ToriAuxLibVM* vm);

void
ui_input_adapter_init_behavior_host_ex(
    struct UITreeBehaviorHost* host,
    struct ToriAuxLibVM* vm,
    struct CS2VM* cs2vm);

#endif
