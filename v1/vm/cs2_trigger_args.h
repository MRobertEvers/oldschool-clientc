#ifndef CS2_TRIGGER_ARGS_H
#define CS2_TRIGGER_ARGS_H

#include "cs2vmx.h"

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

/** Pop OSRS trigger-hook stack layout from a CS2VMX int/string stack. */
bool
cs2_trigger_args_parse(
    struct CS2VMX* vm,
    struct CS2TriggerArgs* out);

#endif
