#include "game/task_cs2_script_exec.h"

#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_opcode_stack.gen.h"
#include "cs2vm2/cs2vm2_script.h"
#include "engine/cache_provider.h"
#include "engine/torirs_types.h"

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

/*
 * The dummy host does not implement game state, but it must preserve the VM's
 * stack shape. Host-request kinds are the originating opcode values, so use
 * that opcode's exact output signature instead of maintaining another list of
 * aliases such as WIDGET or ENUM_LOOKUP.
 */
static int
dummy_host_push_results(
    struct CS2VM2_Thread* thread,
    enum CS2VM_HostRequestKind kind)
{
    int opcode = (int)kind;
    int result;

    if( opcode < 0 || opcode >= CS2VM2_OPCODE_STACK_MAX )
        return CS2VM_EXECNO_OK;

    for( int i = 0; i < g_cs2vm2_opcode_stack[opcode].int_out; i++ )
    {
        result = CS2VM2_PushInt(thread, 0);
        if( result != CS2VM_EXECNO_OK )
            return result;
    }
    for( int i = 0; i < g_cs2vm2_opcode_stack[opcode].str_out; i++ )
    {
        result = CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));
        if( result != CS2VM_EXECNO_OK )
            return result;
    }
    return CS2VM_EXECNO_OK;
}

static int
dummy_host_push_param(
    struct DummyHost* host,
    struct CS2VM2_Thread* thread,
    int param_id)
{
    struct ToriRS_ParamType* param =
        host->provider ? CacheProvider_ParamGet(host->provider, param_id) : NULL;
    if( param && param->is_string )
        return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));
    return CS2VM2_PushInt(thread, 0);
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

    if( request->kind == CS2VM_HOST_REQUEST_GOSUB_WITH_PARAMS )
    {
        struct CS2VM2_Script* script = CacheProvider_ClientScriptGet(
            host->provider, request->u.GOSUB_WITH_PARAMS.payload.script_id);
        if( !script )
        {
            memcpy(&host->request, request, sizeof(struct CS2VM_HostRequest));
            return CS2VM_EXECNO_YIELD;
        }

        CS2VM2_PushCallScript(thread, script);
        return CS2VM_EXECNO_OK;
    }

    if( request->kind == CS2VM_HOST_REQUEST_ENUM_STRING ||
        (request->kind == CS2VM_HOST_REQUEST_ENUM &&
         request->u.ENUM.payload.output_type == (int)'s') )
        return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_CC_GETPARAM:
        return dummy_host_push_param(host, thread, request->u.CC_GETPARAM.payload.param_id);
    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
        return dummy_host_push_param(host, thread, request->u.STRUCT_PARAM.payload.param_id);
    case CS2VM_HOST_REQUEST_NC_PARAM:
        return dummy_host_push_param(host, thread, request->u.NC_PARAM.payload.param_id);
    case CS2VM_HOST_REQUEST_LC_PARAM:
        return dummy_host_push_param(host, thread, request->u.LC_PARAM.payload.param_id);
    case CS2VM_HOST_REQUEST_OC_PARAM:
        return dummy_host_push_param(host, thread, request->u.OC_PARAM.payload.param_id);
    default:
        break;
    }

    return dummy_host_push_results(thread, request->kind);
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
            if( exec->host.request.kind == CS2VM_HOST_REQUEST_GOSUB_WITH_PARAMS )
            {
                exec->script_id =
                    exec->host.request.u.GOSUB_WITH_PARAMS.payload.script_id;
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
