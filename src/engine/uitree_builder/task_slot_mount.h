#ifndef TASK_SLOT_MOUNT_H
#define TASK_SLOT_MOUNT_H

#include "asyncio.h"

#include <stdint.h>

struct UITreeBuilder;

/**
 * Mount an interface pack under a slot owner node at runtime (reference
 * IF_OPENMAIN / IF_OPENSIDE / IF_OPENCHAT / IF_SETTAB): loads the pack and
 * its assets, clears the owner's children, and re-bakes through the same
 * path the initial RevConfig build used. iface_id <= 0 just clears the slot.
 *
 * Runtime mounts carry no INI inv= binding; INV widgets in the pack keep
 * their pack inv sources and fill via UPDATE_INV state sync.
 */
struct ToriRS_Task*
CreateTask_SlotMount(
    struct UITreeBuilder* builder,
    int32_t owner_index,
    int iface_id);

#endif
