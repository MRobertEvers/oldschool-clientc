#ifndef COMPONENT_LOAD_TYPES_H
#define COMPONENT_LOAD_TYPES_H

#include <stdbool.h>

#define RS_COMPONENT_STACK_MAX 1024
#define TASK_RS_COMPONENT_PREFETCH_BATCH 64
#define INV_LOAD_IO_BATCH 64

struct RSComponentLoadCallbacks
{
    void* user;
    void (*on_component)(
        void* user,
        int component_id,
        int parent_id,
        int rel_x,
        int rel_y);
};

struct RSInvLoadCallbacks
{
    void* user;
    void (*on_slot)(
        void* user,
        int slot_index,
        int obj_id);
};

#endif
