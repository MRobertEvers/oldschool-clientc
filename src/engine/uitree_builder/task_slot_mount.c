#include "task_slot_mount.h"
#include "mem/torirs_mem.h"

#include "task_group_mount.h"
#include "engine/asset_requests.h"
#include "uitree_builder.h"
#include "uitree_builder_bake.h"
#include "uitree_builder_manifest.h"

#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "plugin/torirs_plugin_drive.h"
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

    struct App* app;
    struct UITreeBuilder* builder;
    int32_t owner_index;
    int iface_id;
    int packed_id;
    /* Records asked of the assets table and not yet ended. */
    int join;
};

static int
Task_SlotMount_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_SlotMount* self = (struct Task_SlotMount*)base;
    struct UITree* tree = self->builder->tree;
    (void)io;

    PT_BEGIN(&self->pt);

    assert(self->owner_index >= 0 && (uint32_t)self->owner_index < tree->component_count);

    /* The models the slot's outgoing nodes still wait for go with them. */
    App_SlotVisualsCancel(self->app, self->owner_index);
    UITree_ClearChildren(tree, self->owner_index);

    if( self->iface_id > 0 )
    {
        self->packed_id = uibuilder_pack_component_id(self->iface_id);
        /* The ComponentPack, then what a later packet or script reads of it
         * (the models are not), through the assets table. */
        (void)Assets_Await(
            self->builder->provider->assets,
            AssetKey_Make(ASSET_KIND_IF_GROUP, self->packed_id >> 16, 0),
            &self->join);
        TASK_AWAIT_ASSETS(&self->task, &self->pt, self->join);
        if( CacheProvider_ComponentPackHas(self->builder->provider, self->packed_id >> 16) )
            GroupMount_AskComponentPackParts(
                self->builder->provider, self->packed_id >> 16, &self->join);
        TASK_AWAIT_ASSETS(&self->task, &self->pt, self->join);

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
                /* The bake bound the models that are here; the rest are
                 * parallel tasks, and the mount goes on. */
                (void)App_SlotVisualsAdd(self->app, self->owner_index);
            }
            else
            {
                TORIRS_ERR("slot_mount: ComponentPack %d missing after load\n", self->iface_id);
            }
        }
    }

    /* DRIVE_STAMP: slot_mounted -- a=owner_index b=iface_id. The dat1 lane's
     * mount fence; ui.await_open/close ride it there the way they ride
     * sub_mounted on the cache lane. Unconditional (unlike sub_mounted on the
     * cache lane): a non-positive iface_id here means the slot was cleared,
     * not that nothing to report -- close rides this same event. */
    App_DriveEvent(self->app, RS_EVENT_SLOT_MOUNTED, self->owner_index, self->iface_id, 0, 0);
    PT_END(&self->pt);
}

static void
Task_SlotMount_Free(struct ToriRS_Task* base)
{
    struct Task_SlotMount* self = (struct Task_SlotMount*)base;
    /* Freed while it waits on the table: stop counting into it. */
    if( self->join > 0 )
        Assets_Unjoin(self->builder->provider->assets, &self->join);
    ToriMem_Free(ToriMem_Ui(), base);
}

static struct ToriRS_TaskVTable Task_SlotMount_VTable = {
    .run = Task_SlotMount_Run,
    .free = Task_SlotMount_Free,
};

struct ToriRS_Task*
CreateTask_SlotMount(
    struct App* app,
    struct UITreeBuilder* builder,
    int32_t owner_index,
    int iface_id)
{
    assert(app);
    assert(builder);
    if( owner_index < 0 )
        return NULL;

    struct Task_SlotMount* task = ToriMem_Calloc(ToriMem_Ui(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_SlotMount_VTable;
    strncpy(task->task.name, "SlotMount", sizeof(task->task.name) - 1);
    task->app = app;
    task->builder = builder;
    task->owner_index = owner_index;
    task->iface_id = iface_id;
    PT_INIT(&task->pt);
    return &task->task;
}
