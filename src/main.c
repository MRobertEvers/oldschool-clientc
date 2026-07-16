#include "3rd/minipt.h"
#include "asyncio.h"
#include "cache/rscache_io.h"
#include "cs2vm2/cs2vm2.h"
#include "engine/cs2vm2_script_from_rscache.h"
#include "platform/platform_x_io.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_DIR "/Users/matthewevers/Documents/git_repos/3draster/cache.jan2026"
#define CONFIG_DIR "/Users/matthewevers/Documents/git_repos/3draster/config"
#define SCRIPT_DIR "/Users/matthewevers/Documents/git_repos/3draster/script"
#define DUMMY_CLIENTSCRIPT_ID 1000

struct DummyHost
{
    int unused;
};

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
    assert(thread);
    assert(request);
    assert(CS2VM_USER(thread));

    if( request->kind == CS2VM_HOST_REQUEST_PUSHSCRIPT )
        return CS2VM_EXECNO_OK;

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
};

int
Task_CS2ScriptExec_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2ScriptExec* exec = (struct Task_CS2ScriptExec*)task;
    PT_BEGIN(&(exec->pt));

    g_cs2_trace_mode = 2;
    CS2VM2_Init(&exec->vm);
    CS2VM2_BindHost(&exec->vm, &exec->host, DummyHostExec);

    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&exec->vm);
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
            goto done;
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
Task_CS2ScriptExec_New(struct CS2VM2_Script* script)
{
    struct Task_CS2ScriptExec* exec = calloc(1, sizeof(*exec));
    assert(exec != NULL);
    exec->task.vtable = &Task_CS2ScriptExec_VTable;
    strcpy(exec->task.name, "CS2ScriptExec");
    exec->script = script;
    return &exec->task;
}

struct Task_Dummy
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CS2VM2 vm;
    struct DummyHost host;
    struct CS2VM2_Script script;
    struct RSCache_ClientScript* decoded;
    bool script_ready;
    bool vm_started;
};

int
Task_Dummy_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_Dummy* dummy = (struct Task_Dummy*)task;
    printf("Task_Dummy_Run\n");
    PT_BEGIN(&(dummy->pt));

    RSCache_IO_ClientScriptLoad(io, 0, DUMMY_CLIENTSCRIPT_ID);
    PT_YIELD(&(dummy->pt));

    dummy->decoded = RSCache_IO_ClientScriptDecode(io, 0, DUMMY_CLIENTSCRIPT_ID);
    assert(dummy->decoded != NULL);

    assert(CS2VM2_ScriptFromRSCache(&dummy->decoded->script, &dummy->script));
    dummy->script_ready = true;
    printf(
        "clientscript %d decoded: op_count=%d signature=%s\n",
        DUMMY_CLIENTSCRIPT_ID,
        dummy->script.op_count,
        dummy->script.signature ? dummy->script.signature : "(null)");

    TASK_AWAITEX(&(dummy->pt), io, Task_CS2ScriptExec_New(&dummy->script));

    PT_END(&(dummy->pt));
}

void
Task_Dummy_Free(struct ToriRS_Task* task)
{
    struct Task_Dummy* dummy = (struct Task_Dummy*)task;
    if( dummy->vm_started )
        CS2VM2_Free(&dummy->vm);
    if( dummy->script_ready )
        CS2VM2_ScriptFree(&dummy->script);
    if( dummy->decoded )
        RSCache_ClientScriptFree(dummy->decoded);
    free(dummy);
}

static struct ToriRS_TaskVTable Task_Dummy_VTable = {
    .run = Task_Dummy_Run,
    .free = Task_Dummy_Free,
};

struct ToriRS_Task*
Task_Dummy_New(void)
{
    struct Task_Dummy* task = calloc(1, sizeof(*task));
    assert(task != NULL);
    task->task.vtable = &Task_Dummy_VTable;
    strcpy(task->task.name, "Dummy");
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
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    assert(disk != NULL);
    struct PlatformX_IO* px = PlatformX_IO_New();
    assert(px != NULL);
    PlatformX_IO_InitDat2Disk(px, disk);
    PlatformX_IO_InitConfigPath(px, config_dir);
    PlatformX_IO_InitScriptPath(px, script_dir);

    struct ToriRS_Task* task = Task_Dummy_New();
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
