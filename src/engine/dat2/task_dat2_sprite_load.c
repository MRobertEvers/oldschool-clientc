#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_sprite_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2SpriteLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int sprite_id;
};

static int
Task_Dat2SpriteLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2SpriteLoad* task = (struct Task_Dat2SpriteLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_Sprite* sprite = NULL;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2SpriteLoad(io, 0, task->sprite_id);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2SpriteDecode(io, 0);
    if( !archive )
    {
        fprintf(stderr, "Failed to decode dat2 sprite archive %d\n", task->sprite_id);
        PT_EXIT(&task->pt);
    }

    sprite = ToriRS_SpriteFromDat2Archive(archive, task->sprite_id);
    if( !sprite )
    {
        fprintf(stderr, "Failed to convert dat2 sprite %d\n", task->sprite_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_SpriteAdd(&task->bc->base, task->sprite_id, sprite);

    PT_END(&task->pt);
}

static void
Task_Dat2SpriteLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2SpriteLoad_VTable = {
    .run = Task_Dat2SpriteLoad_Run,
    .free = Task_Dat2SpriteLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2SpriteLoad(
    struct CacheProvider* provider,
    int sprite_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2SpriteLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_SpriteHas(provider, sprite_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2SpriteLoad_VTable;
    strcpy(task->task.name, "Dat2SpriteLoad");
    task->bc = dat2_buildcache;
    task->sprite_id = sprite_id;
    PT_INIT(&task->pt);
    return &task->task;
}
