#include "game/task_cs2_script_exec.h"

#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_script.h"
#include "engine/cache_provider.h"

#include <3rd/minipt.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRIPT_CACHE_BUFFER_SIZE 65536

struct DummyHost
{
    int unused;

    struct CS2VM_HostRequest request;
    struct CacheProvider* provider;
};

struct HMap*
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

void
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

struct ScriptEntry*
ScriptCache_Get(
    struct HMap* scripts,
    int script_id)
{
    struct ScriptEntry* entry = (struct ScriptEntry*)hmap_search(scripts, &script_id, HMAP_FIND);
    return entry;
}

struct ScriptEntry*
ScriptCache_StoreCopy(
    struct HMap* scripts,
    int script_id,
    const struct CS2VM2_Script* vm_script)
{
    struct CS2VM2_Script* owned;
    struct ScriptEntry* entry;

    assert(scripts);
    assert(vm_script);

    owned = malloc(sizeof(*owned));
    assert(owned);
    CS2VM2_ScriptCopy(vm_script, owned);

    entry = (struct ScriptEntry*)hmap_search(scripts, &script_id, HMAP_INSERT);
    assert(entry && "Script must be inserted into hmap");
    entry->script_id = script_id;
    entry->script = owned;
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
    case CS2VM_HOST_REQUEST_ENTITY_OVERLAY:
    case CS2VM_HOST_REQUEST_CC_RESOLVE_PARENT:
    case CS2VM_HOST_REQUEST_CC_GETTEXT:
    case CS2VM_HOST_REQUEST_CC_GETCOMPONENTPARAM:
    case CS2VM_HOST_REQUEST_CC_GETTRANS:
    case CS2VM_HOST_REQUEST_IF_FIND:
    case CS2VM_HOST_REQUEST_IF_GETX:
    case CS2VM_HOST_REQUEST_IF_GETTEXT:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH:
    case CS2VM_HOST_REQUEST_OC_PARAM:
    case CS2VM_HOST_REQUEST_OC_NAME:
    case CS2VM_HOST_REQUEST_NC_NAME:
    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
    case CS2VM_HOST_REQUEST_PARAHEIGHT:
    case CS2VM_HOST_REQUEST_PARAWIDTH:
    case CS2VM_HOST_REQUEST_OC_INT_PARAM:
    case CS2VM_HOST_REQUEST_CLIENTCLOCK:
    case CS2VM_HOST_REQUEST_MOUSE_GETX:
    case CS2VM_HOST_REQUEST_MOUSE_GETY:
        return true;
    case CS2VM_HOST_REQUEST_CC_CREATE:
        printf("CC_CREATE\n");
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
    case CS2VM_HOST_REQUEST_CC_GETOP:
    case CS2VM_HOST_REQUEST_IF_GETOP:
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
        struct CS2VM2_Script* script = CacheProvider_ClientScriptGet(
            host->provider, request->u.push_script.script_id);
        if( !script )
        {
            memcpy(&host->request, request, sizeof(struct CS2VM_HostRequest));
            return CS2VM_EXECNO_YIELD;
        }

        CS2VM2_PushCallScript(thread, script);
        return CS2VM_EXECNO_OK;
    }

    if( noop_host_pushes_int(request->kind) )
        return CS2VM2_PushInt(thread, 0);

    if( noop_host_pushes_str(request->kind) )
        return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));

    return CS2VM_EXECNO_OK;
}

struct Task_CS2ScriptExec
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CS2VM2 vm;
    struct DummyHost host;
    struct CS2VM2_Script* script;
    struct CacheProvider* provider;
    /* Must live here, not as a Run() local: it's set before a TASK_AWAITEX
     * yield and read again after resume, and locals don't survive a yield
     * (the protothread macros return out of Run() on each yield). */
    int script_id;
};

static int
Task_CS2ScriptExec_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2ScriptExec* exec = (struct Task_CS2ScriptExec*)task;
    struct CS2VM2_Script* vm_script = NULL;
    struct CS2VM2_Thread* thread = NULL;
    PT_BEGIN(&(exec->pt));

    exec->host.provider = exec->provider;
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
            printf("CS2VM2_THREAD_YIELDED\n");
            if( exec->host.request.kind == CS2VM_HOST_REQUEST_PUSHSCRIPT )
            {
                exec->script_id = exec->host.request.u.push_script.script_id;
                if( !CacheProvider_ClientScriptHas(exec->provider, exec->script_id) )
                {
                    TASK_AWAITEX(
                        &(exec->pt),
                        io,
                        CreateTask_ClientScriptLoad(exec->provider, exec->script_id));
                }
                vm_script = CacheProvider_ClientScriptGet(exec->provider, exec->script_id);
                if( !vm_script )
                {
                    memset(&exec->host.request, 0, sizeof(exec->host.request));
                    break;
                }
                memset(&exec->host.request, 0, sizeof(exec->host.request));
                thread = CS2VM2_ThreadMain(&exec->vm);
                CS2VM2_PushCallScript(thread, vm_script);
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

    PT_END(&(exec->pt));
    return 0;
}

static void
Task_CS2ScriptExec_Free(struct ToriRS_Task* task)
{
    struct Task_CS2ScriptExec* exec = (struct Task_CS2ScriptExec*)task;
    assert(exec);
    /* Releases the threads' string pools. Safe even if Run never got as far as
     * CS2VM2_Init: Task_CS2ScriptExec_New callocs, so the pools read as empty. */
    CS2VM2_Free(&exec->vm);
    free(exec);
}

static struct ToriRS_TaskVTable Task_CS2ScriptExec_VTable = {
    .run = Task_CS2ScriptExec_Run,
    .free = Task_CS2ScriptExec_Free,
};

struct ToriRS_Task*
Task_CS2ScriptExec_New(
    struct CS2VM2_Script* script,
    struct CacheProvider* provider)
{
    struct Task_CS2ScriptExec* exec = calloc(1, sizeof(*exec));
    assert(exec != NULL);
    assert(provider);
    exec->task.vtable = &Task_CS2ScriptExec_VTable;
    strcpy(exec->task.name, "CS2ScriptExec");
    exec->script = script;
    exec->provider = provider;
    return &exec->task;
}
