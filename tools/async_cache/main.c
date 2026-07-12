#define HMAP_IMPLEMENTATION

#include "../src2/core/tapi/tapi_dat2.h"
#include "../src2/ioqueue/libtorirs_ioqueue.h"
#include "../src2/platforms/platform_x/cachelib_platform.h"
#include "../src2/platforms/platform_x_io_reactor.h"
#include <3rd/minipt.h>
#include <datastruct/hmap.h>

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define LIBTORIRS_TASK_RUNNER_MAX_TASKS 1024

struct CacheDat2_Model_Entry
{
    int id;
    void* data;
};

struct CacheDat2
{
    struct HMap* map;
};

static void
CacheDat2_Init(struct CacheDat2* cache)
{
    memset(cache, 0, sizeof(struct CacheDat2));
    int capacity = 1024;
    void* buffer = malloc(capacity);
    struct HashConfig config = {
        .key_size = sizeof(int),
        .entry_size = sizeof(struct CacheDat2_Model_Entry),
        .buffer = buffer,
        .buffer_size = capacity,
    };

    cache->map = hmap_new(&config, 0);
}

static void
CacheDat2_Model_Add(
    struct CacheDat2* cache,
    int id,
    struct RSCacheDat2A_Model* model)
{
    struct CacheDat2_Model_Entry* entry;

    entry = hmap_search(cache->map, &id, HMAP_INSERT);
    assert(entry && "Must have an entry");
    entry->id = id;
    entry->data = model;
}

typedef int (*LibToriRS_TaskRunnerFunction)(
    struct LibToriRS_IOContext* ctx,
    void* user);

struct LibToriRS_Task
{
    LibToriRS_TaskRunnerFunction function;
    void* user;
};

struct LibToriRS_TaskRunner
{
    struct LibToriRS_IOQueue* io;
    struct LibToriRS_Task tasks[LIBTORIRS_TASK_RUNNER_MAX_TASKS];
    int count;
    int head;
    int tail;
    int wait_run;
};

void
LibToriRS_TaskRunner_Init(
    struct LibToriRS_TaskRunner* runner,
    struct LibToriRS_IOQueue* io)
{
    memset(runner, 0, sizeof(struct LibToriRS_TaskRunner));
    runner->io = io;
    runner->wait_run = -1;
}

void
LibToriRS_TaskRunner_Add(
    struct LibToriRS_TaskRunner* runner,
    LibToriRS_TaskRunnerFunction function,
    void* user)
{
    assert(runner->count < LIBTORIRS_TASK_RUNNER_MAX_TASKS);

    struct LibToriRS_Task task = { 0 };
    task.function = function;
    task.user = user;

    runner->tasks[runner->tail] = task;
    runner->tail++;
    if( runner->tail == LIBTORIRS_TASK_RUNNER_MAX_TASKS )
        runner->tail = 0;
    runner->count++;
}

static void
LibToriRS_TaskRunner_Pop(struct LibToriRS_TaskRunner* runner)
{
    assert(runner->count > 0);

    runner->tasks[runner->head] = (struct LibToriRS_Task){ 0 };
    runner->head++;
    if( runner->head == LIBTORIRS_TASK_RUNNER_MAX_TASKS )
        runner->head = 0;
    runner->count--;
    runner->wait_run = -1;
}

bool
LibToriRS_TaskRunner_Run(struct LibToriRS_TaskRunner* runner)
{
    assert(runner && "Runner must be valid");

    if( runner->wait_run >= 0 && !LibToriRS_IOQueueRunComplete(runner->io, runner->wait_run) )
        return true;

    struct LibToriRS_IOContext ctx = { 0 };
    ctx.io = runner->io;

    struct LibToriRS_Task* task = &runner->tasks[runner->head];
    int run = LibToriRS_IOQueueBeginRun(runner->io);
    int res = task->function(&ctx, task->user);

    switch( res )
    {
    case PT_YIELDED:
        runner->wait_run = run;
        break;
    case PT_EXITED:
    case PT_ENDED:
        LibToriRS_TaskRunner_Pop(runner);
        break;
    default:
        break;
    }

    return runner->count > 0;
}

struct Task_AsyncCacheDat2_Model_Load
{
    struct pt pt;
    int id;
    struct CacheDat2* cachedat2;
};

struct Task_AsyncCacheDat2_Model_Load*
Task_AsyncCacheDat2_Model_Load_New(
    int id,
    struct CacheDat2* cachedat2)
{
    struct Task_AsyncCacheDat2_Model_Load* task =
        malloc(sizeof(struct Task_AsyncCacheDat2_Model_Load));
    memset(task, 0, sizeof(struct Task_AsyncCacheDat2_Model_Load));
    task->id = id;
    task->cachedat2 = cachedat2;
    PT_INIT(&task->pt);
    return task;
}

int
Task_AsyncCacheDat2_Model_Load_Run(
    struct LibToriRS_IOContext* ctx,
    void* user)
{
    struct Task_AsyncCacheDat2_Model_Load* task;
    struct RSCacheDat2A_Model* model;

    task = (struct Task_AsyncCacheDat2_Model_Load*)user;

    PT_BEGIN(&task->pt);

    TAPIDat2_FetchModel(ctx, task->id);
    PT_YIELD(&task->pt);
    model = TAPIDat2_DecodeModel(ctx, 0);

    CacheDat2_Model_Add(task->cachedat2, task->id, model);

    PT_END(&task->pt);
}

int
main(void)
{
    struct LibToriRS_IOQueue queue = { 0 };

    const char* cache_dir = "/Users/matthewevers/Documents/git_repos/3draster/cache";
    const int cache_mode = CACHE_MODE_DAT2;

    struct RSCacheDat2DiskLib* cachedisk = NULL;
    cachedisk = cachelib_new(cache_mode);
    if( !cachedisk )
    {
        fprintf(stderr, "cachelib_new failed\n");
        return 1;
    }
    if( cachelib_platform_init(cachedisk, cache_dir) != 1 )
    {
        fprintf(stderr, "cachelib_platform_init failed for: %s\n", cache_dir);
        cachelib_free(cachedisk);
        return 1;
    }

    struct LibToriPlatformX_IOReactor* reactor = LibToriPlatformX_IOReactorNew(cachedisk);
    assert(reactor && "Must have a reactor");

    struct LibToriRS_TaskRunner runner = { 0 };
    LibToriRS_TaskRunner_Init(&runner, &queue);

    struct CacheDat2 cache = { 0 };
    CacheDat2_Init(&cache);

    struct Task_AsyncCacheDat2_Model_Load* task = Task_AsyncCacheDat2_Model_Load_New(0, &cache);

    LibToriRS_TaskRunner_Add(&runner, Task_AsyncCacheDat2_Model_Load_Run, task);

    while( LibToriRS_TaskRunner_Run(&runner) )
        LibToriPlatformX_IOReactorProcess(reactor, &queue);

    LibToriPlatformX_IOReactorFree(reactor);

    return 0;
}
