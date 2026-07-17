#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "game/task_cs2_script_exec.h"
#include "platform/platform_x_io.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_DIR "/Users/matthewevers/Documents/git_repos/3draster/cache.jan2026"
#define CONFIG_DIR "/Users/matthewevers/Documents/git_repos/3draster/config"
#define SCRIPT_DIR "/Users/matthewevers/Documents/git_repos/3draster/script"

struct Task_Dummy
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CacheProvider* provider;
    int pack_index;
    struct ToriRS_ComponentPack* pack;
};

static int
Component_OnLoadScriptId(const struct ToriRS_Component* component)
{
    assert(component);
    if( component->on_load.argc <= 0 )
        return 0;
    if( component->on_load.argv[0] <= 0 )
        return 0;
    return component->on_load.argv[0];
}

static int
Task_Dummy_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_Dummy* self = (struct Task_Dummy*)task;
    struct ToriRS_Component* component;
    struct CS2VM2_Script* vm_script;
    int script_id;
    printf("Task_Dummy_Run\n");
    PT_BEGIN(&(self->pt));

#define SLOT_MACHINE_IF_ID 26
#define BANK_SLOT_MACHINE_IF_ID 12
#define RESIZEABLE_LAYOUT_IF_ID 161

    TASK_AWAITSELF(CreateTask_ComponentPackLoad(self->provider, RESIZEABLE_LAYOUT_IF_ID));

    self->pack = CacheProvider_ComponentPackGet(self->provider, RESIZEABLE_LAYOUT_IF_ID);

    assert(self->pack != NULL);

    for( self->pack_index = 0; self->pack_index < self->pack->component_count; self->pack_index++ )
    {
        component = &self->pack->components[self->pack_index];
        printf("component %d: id=%d\n", self->pack_index, component->id);
        script_id = Component_OnLoadScriptId(component);
        if( script_id == 0 )
            continue;

        printf("onLoad script_id=%d\n", script_id);
        if( !CacheProvider_ClientScriptHas(self->provider, script_id) )
        {
            TASK_AWAITSELF(CreateTask_ClientScriptLoad(self->provider, script_id));

            component = &self->pack->components[self->pack_index];
            script_id = Component_OnLoadScriptId(component);
            assert(script_id != 0);
        }

        vm_script = CacheProvider_ClientScriptGet(self->provider, script_id);
        if( !vm_script )
            continue;

        TASK_AWAITEX(&(self->pt), io, Task_CS2ScriptExec_New(vm_script, self->provider));
    }

    PT_END(&(self->pt));
}

static void
Task_Dummy_Free(struct ToriRS_Task* task)
{
    struct Task_Dummy* dummy = (struct Task_Dummy*)task;
    free(dummy);
}

static struct ToriRS_TaskVTable Task_Dummy_VTable = {
    .run = Task_Dummy_Run,
    .free = Task_Dummy_Free,
};

static struct ToriRS_Task*
Task_Dummy_New(struct CacheProvider* provider)
{
    struct Task_Dummy* task = calloc(1, sizeof(*task));
    assert(task != NULL);
    assert(provider);
    task->task.vtable = &Task_Dummy_VTable;
    strcpy(task->task.name, "Dummy");
    task->provider = provider;
    return &task->task;
}

int
main(
    int argc,
    char** argv)
{
    const char* cache_dir = CACHE_DIR;
    const char* config_dir = CONFIG_DIR;
    const char* script_dir = SCRIPT_DIR;

    struct ToriRS_IO* io = ToriRS_IO_New();
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    assert(disk != NULL);
    struct PlatformX_IO* px = PlatformX_IO_New();
    assert(px != NULL);
    PlatformX_IO_InitDat2Disk(px, disk);
    PlatformX_IO_InitConfigPath(px, config_dir);
    PlatformX_IO_InitScriptPath(px, script_dir);

    struct ToriRS_Task* task = Task_Dummy_New(provider);
    assert(task != NULL);
    ToriRS_TaskQueue_Add(queue, task);

    while( ToriRS_TaskQueue_Run(queue, io) == TORIRS_ASYNCIO_STAT_YIELD )
    {
        PlatformX_IO_Process(px, io);
    }

    printf("Task_Dummy_Run done\n");
    ToriRS_TaskQueue_Free(queue);
    ToriRS_IO_Free(io);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    dat2_buildcache_free(bc);

    (void)argc;
    (void)argv;

    return 0;
}
