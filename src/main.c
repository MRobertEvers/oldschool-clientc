#include "3rd/minipt.h"
#include "asyncio.h"
#include "cache/rscache_io.h"
#include "platform/platform_x_io.h"

#include <rscache.h>
#include <stdio.h>

#define CACHE_DIR "/Users/matthewevers/Documents/git_repos/3draster/cache.jan2026"
#define CONFIG_DIR "/Users/matthewevers/Documents/git_repos/3draster/config"
#define SCRIPT_DIR "/Users/matthewevers/Documents/git_repos/3draster/script"

struct Task_Dummy
{
    struct ToriRS_Task task;
    struct pt pt;
};

int
Task_Dummy_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_Dummy* dummy = (struct Task_Dummy*)task;
    struct RSCache_Model* model = NULL;
    struct RSCache_Dat2ComponentPack* component = NULL;
    printf("Task_Dummy_Run\n");
    PT_BEGIN(&(dummy->pt));
    RSCache_IO_ModelLoad(io, 0, 1);
    RSCache_IO_Dat2ComponentPackLoad(io, 1, 26);
    printf("Task_Dummy_Run 1\n");
    PT_YIELD(&(dummy->pt));
    model = RSCache_IO_ModelDecode(io, 0);
    component = RSCache_IO_Dat2ComponentPackDecode(io, 1);
    printf("model: %p\n", model);
    printf("component: %p\n", component);
    printf("Task_Dummy_Run 2\n");
    PT_YIELD(&(dummy->pt));
    printf("Task_Dummy_Run 3\n");
    PT_END(&(dummy->pt));
}
void
Task_Dummy_Free(struct ToriRS_Task* task)
{}

static struct ToriRS_TaskVTable Task_Dummy_VTable = {
    .run = Task_Dummy_Run,
    .free = Task_Dummy_Free,
};

struct ToriRS_Task*
Task_Dummy_New(void)
{
    struct ToriRS_Task* task = malloc(sizeof(struct ToriRS_Task));
    memset(task, 0, sizeof(struct ToriRS_Task));
    assert(task != NULL);
    task->vtable = &Task_Dummy_VTable;
    return task;
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