#ifndef CS2_TRIGGER_ARGS_H
#define CS2_TRIGGER_ARGS_H

#include "cs2vm.h"

#include <stdbool.h>
#include <stdint.h>

#define CS2_TRIGGER_ARGV_MAX 16
#define CS2_TRIGGER_COUNT_MAX 8

struct CS2TriggerArgs
{
    int script_id;
    int argc;
    int argv[CS2_TRIGGER_ARGV_MAX];
    int trigger_count;
    int triggers[CS2_TRIGGER_COUNT_MAX];
    bool is_clear;
    bool ok;
};

/** Pop OSRS trigger-hook stack layout (signature, optional Y-triggers, typed args, scriptId). */
bool
cs2_trigger_args_parse(
    struct CS2_InvokeCtx* ctx,
    struct CS2TriggerArgs* out);

#endif
