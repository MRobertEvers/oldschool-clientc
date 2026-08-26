#include "task_slot_mount.h"

#include "task_pack_assets_load.h"
#include "uitree_builder.h"
#include "uitree_builder_bake.h"
#include "uitree_builder_manifest.h"

#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "ui/uitree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

struct Task_SlotMount
{
    struct ToriRS_Task task;
    struct pt pt;

    struct UITreeBuilder* builder;
    int32_t owner_index;
    int iface_id;
    int packed_id;
};

static int
Task_SlotMount_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_SlotMount* self = (struct Task_SlotMount*)base;
    struct UITree* tree = self->builder->tree;
    (void)io;

    PT_BEGIN(&self->pt);

    assert(self->owner_index >= 0 && (uint32_t)self->owner_index < tree->component_count);

    UITree_ClearChildren(tree, self->owner_index);

    if( self->iface_id > 0 )
    {
        self->packed_id = uibuilder_pack_component_id(self->iface_id);
        PT_TASK_AWAITSELF_IF(CreateTask_ComponentLoad(self->builder->provider, self->packed_id));
        PT_TASK_AWAITSELF_IF(
            CreateTask_PackAssetsLoad(self->builder->provider, self->packed_id >> 16));

        {
            struct ToriRS_ComponentPack* pack =
                CacheProvider_ComponentPackGet(self->builder->provider, self->packed_id >> 16);
            if( pack )
            {
                uitree_builder_bake_pack_under_owner(
                    tree,
                    self->builder,
                    pack,
                    self->owner_index,
                    -1 /* no INI inv binding at runtime, see header */);
            }
            else
            {
                TORIRS_ERR("slot_mount: pack %d missing after load\n", self->iface_id);
            }
        }
    }

    PT_END(&self->pt);
}

static void
Task_SlotMount_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_SlotMount_VTable = {
    .run = Task_SlotMount_Run,
    .free = Task_SlotMount_Free,
};

struct ToriRS_Task*
CreateTask_SlotMount(
    struct UITreeBuilder* builder,
    int32_t owner_index,
    int iface_id)
{
    assert(builder);
    if( owner_index < 0 )
        return NULL;

    struct Task_SlotMount* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_SlotMount_VTable;
    strncpy(task->task.name, "SlotMount", sizeof(task->task.name) - 1);
    task->builder = builder;
    task->owner_index = owner_index;
    task->iface_id = iface_id;
    PT_INIT(&task->pt);
    return &task->task;
}
