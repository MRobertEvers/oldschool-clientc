#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_font_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

struct Task_Dat2FontLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int font_id;
    struct RSCache_Dat2DiskArchive* font_archive;
};

static int
Task_Dat2FontLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2FontLoad* task = (struct Task_Dat2FontLoad*)task_base;
    struct RSCache_Dat2DiskArchive* sprite_archive = NULL;
    struct ToriRS_Font* font = NULL;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2FontLoad(io, 0, task->font_id);
    PT_YIELD(&task->pt);

    task->font_archive = RSCache_IO_Dat2FontDecode(io, 0);
    if( !task->font_archive )
    {
        TORIRS_ERR("Failed to decode dat2 font archive %d\n", task->font_id);
        PT_EXIT(&task->pt);
    }

    RSCache_IO_Dat2SpriteLoad(io, 0, task->font_id);
    PT_YIELD(&task->pt);

    sprite_archive = RSCache_IO_Dat2SpriteDecode(io, 0);
    if( !sprite_archive )
    {
        TORIRS_ERR("Failed to decode dat2 sprite archive for font %d\n", task->font_id);
        RSCache_Dat2DiskArchiveFree(task->font_archive);
        task->font_archive = NULL;
        PT_EXIT(&task->pt);
    }

    font = ToriRS_FontFromDat2Archives(
        CacheProvider_Profile(&task->bc->base),
        task->font_archive,
        sprite_archive,
        task->font_id);
    task->font_archive = NULL;
    if( !font )
    {
        TORIRS_ERR("Failed to convert dat2 font %d\n", task->font_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_FontAdd(&task->bc->base, task->font_id, font);

    PT_END(&task->pt);
}

static void
Task_Dat2FontLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2FontLoad* task = (struct Task_Dat2FontLoad*)task_base;
    if( task->font_archive )
        RSCache_Dat2DiskArchiveFree(task->font_archive);
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2FontLoad_VTable = {
    .run = Task_Dat2FontLoad_Run,
    .free = Task_Dat2FontLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2FontLoad(
    struct CacheProvider* provider,
    int font_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2FontLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_FontHas(provider, font_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2FontLoad_VTable;
    strcpy(task->task.name, "Dat2FontLoad");
    task->bc = dat2_buildcache;
    task->font_id = font_id;
    PT_INIT(&task->pt);
    return &task->task;
}
