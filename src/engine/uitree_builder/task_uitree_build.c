#include "uitree_builder.h"
#include "uitree_builder_bake.h"
#include "uitree_builder_manifest.h"

#include "game/rs_cs2_host.h"
#include "game/task_cs2_run.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_UITreeBuild
{
    struct ToriRS_Task task;
    struct pt pt;

    struct UITreeBuilder* builder;
    struct UIBuilderManifest manifest;
    int i;
    int script_id;
};

static int
Task_UITreeBuild_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_UITreeBuild* self = (struct Task_UITreeBuild*)base;
    assert(self->builder);
    assert(self->builder->tree);
    assert(self->builder->invs);
    assert(self->builder->ini_path[0] != '\0');

    PT_BEGIN(&self->pt);

    /* 1. Parse RevConfig INI(s) into a manifest (local file, no cache IO). */
    uibuilder_manifest_from_revconfig_inis(
        &self->manifest,
        self->builder->ini_path,
        self->builder->cache_ini_path[0] ? self->builder->cache_ini_path : NULL);

    /* 2. Load every cache asset the manifest needs. */
    TASK_AWAITSELF(CreateTask_UIBuilderAssetsLoad(self->builder, &self->manifest));

    /* 3. Bake the tree (synchronous; provider is fully populated). */
    uitree_builder_bake(
        self->builder->tree, self->builder, &self->manifest, self->builder->invs);
    UITree_LayoutResolve(
        self->builder->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    /* 4. Run IF3 on_load hooks collected during bake. */
    if( self->builder->host )
    {
        for( self->i = 0; self->i < self->builder->onload_count; self->i++ )
        {
            struct UIBuilderOnLoadEntry const* hook = &self->builder->onloads[self->i];
            int const* args;
            int arg_count;

            self->script_id = hook->script_id;
            if( self->script_id <= 0 )
                continue;

            /* on_load.argv[0] is the script id; remaining entries are frame locals. */
            if( hook->argc > 1 )
            {
                args = hook->argv + 1;
                arg_count = hook->argc - 1;
            }
            else
            {
                args = NULL;
                arg_count = 0;
            }

            TASK_AWAITSELF_IF(CreateTask_CS2Run(
                self->builder->host,
                self->script_id,
                hook->component_id,
                hook->component_id,
                args,
                arg_count));
        }

        /* 5. Dispatch initial inv-transmit hooks registered by on_load. */
        TASK_AWAITSELF_IF(CreateTask_CS2InvTransmitDispatch(self->builder->host, -1));
    }

    PT_END(&self->pt);
}

static void
Task_UITreeBuild_Free(struct ToriRS_Task* base)
{
    struct Task_UITreeBuild* self = (struct Task_UITreeBuild*)base;
    uibuilder_manifest_free(&self->manifest);
    free(self);
}

static struct ToriRS_TaskVTable Task_UITreeBuild_VTable = {
    .run = Task_UITreeBuild_Run,
    .free = Task_UITreeBuild_Free,
};

struct ToriRS_Task*
CreateTask_UITreeBuild(struct UITreeBuilder* builder)
{
    assert(builder);

    struct Task_UITreeBuild* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_UITreeBuild_VTable;
    strncpy(task->task.name, "UITreeBuild", sizeof(task->task.name) - 1);
    task->builder = builder;
    uibuilder_manifest_init(&task->manifest);
    PT_INIT(&task->pt);
    return &task->task;
}
