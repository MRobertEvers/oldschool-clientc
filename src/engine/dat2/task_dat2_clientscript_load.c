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
#include "log/torirs_log.h"

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
    struct ToriRS_IOBatch* io)
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
        TORIRS_ERR("Failed to decode dat2 clientscript %d\n", task->script_id);
        PT_EXIT(&task->pt);
    }

    vm_script = calloc(1, sizeof(*vm_script));
    assert(vm_script);
    if( !CS2VM2_ScriptCopyFromRSCache(
            &rscache_script->script,
            vm_script,
            CS2_OpcodeDialectForCache(CacheProvider_Profile(&task->bc->base)) ) )
    {
        TORIRS_ERR("Failed to convert dat2 clientscript %d\n", task->script_id);
        free(vm_script);
        RSCache_ClientScriptFree(rscache_script);
        PT_EXIT(&task->pt);
    }

    /*
     * The decode is consumed here and nothing else reads it — the same shape
     * the raw model store had (task_dat2_model_load.c). It used to be stashed
     * in dat2_buildcache_clientscript_add, whose only reader
     * (dat2_buildcache_clientscript_get) has no caller in src: that kept a
     * second, wire-format copy of every script the session ever loaded until
     * shutdown, and a script re-loaded after the derived cache dropped it
     * overwrote the entry and leaked the previous copy on top. The conversion
     * above COPIES (CS2VM2_ScriptCopyFromRSCache, not the Move sibling), so the
     * VM script is independent and this releases the whole decode.
     */
    RSCache_ClientScriptFree(rscache_script);
    rscache_script = NULL;

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
