#include "asyncio.h"
#include "cache/rscache_io.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"
#include "datatypes/dat2_component.h"
#include "engine/cs2vm2_script_from_rscache.h"
#include "platform/platform_x_io.h"
#include <3rd/hmap/hmap.h>
#include <3rd/minipt.h>

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_DIR "/Users/matthewevers/Documents/git_repos/3draster/cache.jan2026"
#define CONFIG_DIR "/Users/matthewevers/Documents/git_repos/3draster/config"
#define SCRIPT_DIR "/Users/matthewevers/Documents/git_repos/3draster/script"
#define DUMMY_CLIENTSCRIPT_ID 1000
#define SCRIPT_CACHE_BUFFER_SIZE 65536

struct ScriptEntry
{
    int script_id;
    struct CS2VM2_Script* script;
};

struct DummyHost
{
    int unused;

    struct CS2VM_HostRequest request;
    struct HMap* scripts;
};

static struct HMap*
ScriptCache_New(void)
{
    void* buffer = malloc(SCRIPT_CACHE_BUFFER_SIZE);
    assert(buffer);
    struct HashConfig config = {
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ScriptEntry),
        .buffer = buffer,
        .buffer_size = SCRIPT_CACHE_BUFFER_SIZE,
    };
    struct HMap* scripts = hmap_new(&config, 0);
    assert(scripts);
    return scripts;
}

static void
ScriptCache_Free(struct HMap* scripts)
{
    struct HMapIter* it;
    struct ScriptEntry* entry;

    assert(scripts);
    it = hmap_iter_new(scripts);
    while( (entry = (struct ScriptEntry*)hmap_iter_next(it)) )
    {
        CS2VM2_ScriptFree(entry->script);
        free(entry->script);
    }
    hmap_iter_free(it);
    free(hmap_free(scripts));
}

static struct ScriptEntry*
ScriptCache_Store(
    struct HMap* scripts,
    int script_id,
    struct RSCache_ClientScript* rscache_script)
{
    struct CS2VM2_Script* owned;
    struct ScriptEntry* entry;

    assert(scripts);
    assert(rscache_script);

    owned = malloc(sizeof(*owned));
    assert(owned);
    CS2VM2_ScriptInit(owned);
    CS2VM2_ScriptFromRSCache(&rscache_script->script, owned);
    RSCache_ClientScriptFree(rscache_script);

    entry = (struct ScriptEntry*)hmap_search(scripts, &script_id, HMAP_INSERT);
    assert(entry && "Script must be inserted into hmap");
    entry->script_id = script_id;
    entry->script = owned;
    return entry;
}

static struct ScriptEntry*
ScriptCache_Get(
    struct HMap* scripts,
    int script_id)
{
    struct ScriptEntry* entry = (struct ScriptEntry*)hmap_search(scripts, &script_id, HMAP_FIND);
    return entry;
}

static bool
noop_host_pushes_int(enum CS2VM_HostRequestKind kind)
{
    switch( kind )
    {
    case CS2VM_HOST_REQUEST_INVS_GET_SIZE:
    case CS2VM_HOST_REQUEST_INVS_GET_OBJ:
    case CS2VM_HOST_REQUEST_INVS_GET_NUM:
    case CS2VM_HOST_REQUEST_INVS_GET_TOTAL:
    case CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR:
    case CS2VM_HOST_REQUEST_VARS_READ_VARBIT:
    case CS2VM_HOST_REQUEST_VARS_READ_VARC_INT:
    case CS2VM_HOST_REQUEST_ENUM_LOOKUP:
    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
    case CS2VM_HOST_REQUEST_CC_GETID:
    case CS2VM_HOST_REQUEST_CC_GETX:
    case CS2VM_HOST_REQUEST_CC_GETY:
    case CS2VM_HOST_REQUEST_CC_GETWIDTH:
    case CS2VM_HOST_REQUEST_CC_GETHEIGHT:
    case CS2VM_HOST_REQUEST_CC_GETHIDE:
    case CS2VM_HOST_REQUEST_IF_GETWIDTH:
    case CS2VM_HOST_REQUEST_IF_GETHEIGHT:
    case CS2VM_HOST_REQUEST_IF_GETY:
    case CS2VM_HOST_REQUEST_IF_GETLAYER:
    case CS2VM_HOST_REQUEST_IF_GETTOP:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLX:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLY:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT:
    case CS2VM_HOST_REQUEST_IF_GETHIDE:
    case CS2VM_HOST_REQUEST_CC_FINDROOT:
    case CS2VM_HOST_REQUEST_CC_RESOLVE_PARENT:
    case CS2VM_HOST_REQUEST_CC_GETTEXT:
    case CS2VM_HOST_REQUEST_CC_GETTRANS:
    case CS2VM_HOST_REQUEST_IF_FIND:
    case CS2VM_HOST_REQUEST_IF_GETX:
    case CS2VM_HOST_REQUEST_IF_GETTEXT:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH:
    case CS2VM_HOST_REQUEST_OC_PARAM:
    case CS2VM_HOST_REQUEST_OC_NAME:
    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
    case CS2VM_HOST_REQUEST_PARAHEIGHT:
    case CS2VM_HOST_REQUEST_PARAWIDTH:
    case CS2VM_HOST_REQUEST_OC_INT_PARAM:
    case CS2VM_HOST_REQUEST_CLIENTCLOCK:
        return true;
    default:
        return false;
    }
}

static bool
noop_host_pushes_str(enum CS2VM_HostRequestKind kind)
{
    switch( kind )
    {
    case CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING:
        return true;
    default:
        return false;
    }
}

static int
DummyHostExec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct DummyHost* host = (struct DummyHost*)CS2VM_USER(thread);
    assert(thread);
    assert(request);
    assert(CS2VM_USER(thread));

    if( request->kind == CS2VM_HOST_REQUEST_PUSHSCRIPT )
    {
        struct ScriptEntry* entry =
            ScriptCache_Get(host->scripts, request->u.push_script.script_id);
        if( !entry )
        {
            memcpy(&host->request, request, sizeof(struct CS2VM_HostRequest));
            return CS2VM_EXECNO_YIELD;
        }
        else
        {
            CS2VM2_PushCallScript(thread, entry->script);
            return CS2VM_EXECNO_OK;
        }
    }

    if( noop_host_pushes_int(request->kind) )
        return CS2VM2_PushInt(thread, 0);

    if( noop_host_pushes_str(request->kind) )
        return CS2VM2_PushStr(thread, strdup(""));

    return CS2VM_EXECNO_OK;
}

struct Task_CS2ScriptExec
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CS2VM2 vm;
    struct DummyHost host;
    struct CS2VM2_Script* script;
    struct HMap* scripts;
};

int
Task_CS2ScriptExec_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2ScriptExec* exec = (struct Task_CS2ScriptExec*)task;
    struct RSCache_ClientScript* rscache_script = NULL;
    struct ScriptEntry* entry = NULL;
    struct CS2VM2_Thread* thread = NULL;
    int script_id;
    PT_BEGIN(&(exec->pt));

    exec->host.scripts = exec->scripts;
    g_cs2_trace_mode = 2;
    CS2VM2_Init(&exec->vm);
    CS2VM2_BindHost(&exec->vm, &exec->host, DummyHostExec);

    thread = CS2VM2_ThreadMain(&exec->vm);
    CS2VM2_ThreadSetCanvas(thread, 765, 503);
    CS2VM2_ThreadStart(thread, exec->script);
    enum CS2VM2_ThreadStatus status;
    struct CS2VM2_ThreadError err;
    while( (status = CS2VM2_ThreadRun(thread, &err)) )
    {
        if( status == CS2VM2_THREAD_YIELDED )
        {
            // Handle host request.
            printf("CS2VM2_THREAD_YIELDED\n");
            if( exec->host.request.kind == CS2VM_HOST_REQUEST_PUSHSCRIPT )
            {
                script_id = exec->host.request.u.push_script.script_id;
                entry = (struct ScriptEntry*)hmap_search(exec->scripts, &script_id, HMAP_FIND);
                if( !entry )
                {
                    RSCache_IO_ClientScriptLoad(io, 0, script_id);
                    PT_YIELD(&(exec->pt));
                    script_id = exec->host.request.u.push_script.script_id;
                    rscache_script = RSCache_IO_ClientScriptDecode(io, 0, script_id);
                    entry = ScriptCache_Store(exec->scripts, script_id, rscache_script);
                }
                memset(&exec->host.request, 0, sizeof(exec->host.request));
                thread = CS2VM2_ThreadMain(&exec->vm);
                CS2VM2_PushCallScript(thread, entry->script);
            }
        }
        else if( status == CS2VM2_THREAD_ERROR )
        {
            break;
        }
        else
        {
            break;
        }
    }

done:

    PT_END(&(exec->pt));
    return 0;
}

void
Task_CS2ScriptExec_Free(struct ToriRS_Task* task)
{
    struct Task_CS2ScriptExec* exec = (struct Task_CS2ScriptExec*)task;
    assert(exec);
    free(exec);
}

static struct ToriRS_TaskVTable Task_CS2ScriptExec_VTable = {
    .run = Task_CS2ScriptExec_Run,
    .free = Task_CS2ScriptExec_Free,
};

struct ToriRS_Task*
Task_CS2ScriptExec_New(
    struct CS2VM2_Script* script,
    struct HMap* scripts)
{
    struct Task_CS2ScriptExec* exec = calloc(1, sizeof(*exec));
    assert(exec != NULL);
    exec->task.vtable = &Task_CS2ScriptExec_VTable;
    strcpy(exec->task.name, "CS2ScriptExec");
    exec->script = script;
    exec->scripts = scripts;
    return &exec->task;
}

struct Task_Dummy
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CS2VM2 vm;
    struct DummyHost host;
    struct HMap* scripts;
    struct RSCache_Dat2ComponentPack* pack;
    int pack_index;
    bool vm_started;
};

int
Task_Dummy_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_Dummy* dummy = (struct Task_Dummy*)task;
    struct RSCache_Dat2Component* component;
    struct RSCache_ClientScript* rscache_script;
    struct ScriptEntry* entry;
    int script_id;
    printf("Task_Dummy_Run\n");
    PT_BEGIN(&(dummy->pt));

#define SLOT_MACHINE_IF_ID 26
#define BANK_SLOT_MACHINE_IF_ID 12
#define RESIZEABLE_LAYOUT_IF_ID 161

    // RSCache_IO_ClientScriptLoad(io, 0, DUMMY_CLIENTSCRIPT_ID);
    RSCache_IO_Dat2ComponentPackLoad(io, 0, RESIZEABLE_LAYOUT_IF_ID);

    PT_YIELD(&(dummy->pt));
    // dummy->decoded = RSCache_IO_ClientScriptDecode(io, 0, DUMMY_CLIENTSCRIPT_ID);
    // assert(dummy->decoded != NULL);
    dummy->pack = RSCache_IO_Dat2ComponentPackDecode(io, 0);
    assert(dummy->pack != NULL);

    for( dummy->pack_index = 0; dummy->pack_index < dummy->pack->component_count;
         dummy->pack_index++ )
    {
        component = dummy->pack->components[dummy->pack_index];
        printf("component %d: id=%d\n", dummy->pack_index, component->id);
        if( component->onLoadLen != 0 && component->onLoad != NULL )
        {
            printf("onLoad");
            script_id = component->onLoad[0].value.i;
            entry = (struct ScriptEntry*)hmap_search(dummy->scripts, &script_id, HMAP_FIND);
            if( !entry )
            {
                RSCache_IO_ClientScriptLoad(io, 0, script_id);
                PT_YIELD(&(dummy->pt));

                component = dummy->pack->components[dummy->pack_index];
                script_id = component->onLoad[0].value.i;
                rscache_script = RSCache_IO_ClientScriptDecode(io, 0, script_id);
                entry = ScriptCache_Store(dummy->scripts, script_id, rscache_script);
            }

            TASK_AWAITEX(&(dummy->pt), io, Task_CS2ScriptExec_New(entry->script, dummy->scripts));
        }
    }

    PT_END(&(dummy->pt));
}

void
Task_Dummy_Free(struct ToriRS_Task* task)
{
    struct Task_Dummy* dummy = (struct Task_Dummy*)task;
    if( dummy->vm_started )
        CS2VM2_Free(&dummy->vm);
    if( dummy->scripts )
        ScriptCache_Free(dummy->scripts);
    free(dummy);
}

static struct ToriRS_TaskVTable Task_Dummy_VTable = {
    .run = Task_Dummy_Run,
    .free = Task_Dummy_Free,
};

struct ToriRS_Task*
Task_Dummy_New(struct HMap* scripts)
{
    struct Task_Dummy* task = calloc(1, sizeof(*task));
    assert(task != NULL);
    task->task.vtable = &Task_Dummy_VTable;
    strcpy(task->task.name, "Dummy");
    task->scripts = scripts;
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
    struct HMap* scripts = ScriptCache_New();
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    assert(disk != NULL);
    struct PlatformX_IO* px = PlatformX_IO_New();
    assert(px != NULL);
    PlatformX_IO_InitDat2Disk(px, disk);
    PlatformX_IO_InitConfigPath(px, config_dir);
    PlatformX_IO_InitScriptPath(px, script_dir);

    struct ToriRS_Task* task = Task_Dummy_New(scripts);
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

    return 0;
}
