#ifndef CS2_TRIGGER_ARGS_H
#define CS2_TRIGGER_ARGS_H

#include "cs2vm2.h"

#include <stdbool.h>
#include <stdint.h>

/* Wide enough for the whole cache: see CS2VM_SETON_INT_ARG_MAX. A short cap
 * here does not merely drop the tail — the pop loop is bounded by it too, so
 * the un-popped arguments stay on the VM stack and every value the rest of the
 * script reads is shifted. */
#define CS2_TRIGGER_ARGV_MAX CS2VM_SETON_INT_ARG_MAX
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

/** Pop OSRS trigger-hook stack layout from a CS2VM2 int/string stack. */
bool
CS2VM2_TriggerArgsParse(
    struct CS2VM2_Thread* thread,
    struct CS2TriggerArgs* out);

#endif
