#include "asyncio.h"
#include "cache/rscache_io.h"
#include "engine/cache_provider.h"
#include "engine/cs2vm2_script_from_rscache.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2ClientScriptLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int script_id;
};

static int
Task_Dat2ClientScriptLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2ClientScriptLoad* task = (struct Task_Dat2ClientScriptLoad*)task_base;
    struct RSCache_ClientScript* rscache_script = NULL;
    struct CS2VM2_Script* vm_script = NULL;

    PT_BEGIN(&task->pt);

    RSCache_IO_ClientScriptLoad(io, 0, task->script_id);
    PT_YIELD(&task->pt);

    rscache_script = RSCache_IO_ClientScriptDecode(io, 0, task->script_id);
    if( !rscache_script )
    {
        fprintf(stderr, "Failed to decode dat2 clientscript %d\n", task->script_id);
        PT_EXIT(&task->pt);
    }

    dat2_buildcache_clientscript_add(task->bc, task->script_id, rscache_script);

    vm_script = calloc(1, sizeof(*vm_script));
    assert(vm_script);
    if( !CS2VM2_ScriptCopyFromRSCache(
            &rscache_script->script,
            vm_script,
            CS2_OpcodeDialectForCache(CacheProvider_Profile(&task->bc->base)) ) )
    {
        fprintf(stderr, "Failed to convert dat2 clientscript %d\n", task->script_id);
        free(vm_script);
        PT_EXIT(&task->pt);
    }

    CacheProvider_ClientScriptAdd(&task->bc->base, task->script_id, vm_script);

    PT_END(&task->pt);
}

static void
Task_Dat2ClientScriptLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2ClientScriptLoad_VTable = {
    .run = Task_Dat2ClientScriptLoad_Run,
    .free = Task_Dat2ClientScriptLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2ClientScriptLoad(
    struct CacheProvider* provider,
    int script_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2ClientScriptLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_ClientScriptHas(provider, script_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2ClientScriptLoad_VTable;
    strcpy(task->task.name, "Dat2ClientScriptLoad");
    task->bc = dat2_buildcache;
    task->script_id = script_id;
    PT_INIT(&task->pt);
    return &task->task;
}
