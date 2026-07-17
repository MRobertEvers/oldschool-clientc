#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_texture_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2TextureLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int texture_id;
};

static int
Task_Dat2TextureLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2TextureLoad* task = (struct Task_Dat2TextureLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_Dat2Texture* rscache_texture = NULL;
    struct ToriRS_Texture* torirs_texture = NULL;
    int wanted_id = task->texture_id;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2TextureGroupLoad(io, 0);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2TextureGroupDecode(io, 0);
    if( !archive )
    {
        fprintf(stderr, "Failed to decode dat2 texture group for texture %d\n", task->texture_id);
        PT_EXIT(&task->pt);
    }

    dat2_buildcache_textures_init_from_archive(task->bc, archive, &wanted_id, 1);
    RSCache_Dat2DiskArchiveFree(archive);

    rscache_texture = dat2_buildcache_texture_get(task->bc, task->texture_id);
    if( !rscache_texture )
    {
        fprintf(stderr, "Failed to load dat2 texture %d\n", task->texture_id);
        PT_EXIT(&task->pt);
    }

    torirs_texture = ToriRS_TextureFromRSCache(task->texture_id, rscache_texture);
    if( !torirs_texture )
    {
        fprintf(stderr, "Failed to convert dat2 texture %d\n", task->texture_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_TextureAdd(&task->bc->base, task->texture_id, torirs_texture);

    PT_END(&task->pt);
}

static void
Task_Dat2TextureLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2TextureLoad_VTable = {
    .run = Task_Dat2TextureLoad_Run,
    .free = Task_Dat2TextureLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2TextureLoad(
    struct CacheProvider* provider,
    int texture_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2TextureLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_TextureHas(provider, texture_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2TextureLoad_VTable;
    strcpy(task->task.name, "Dat2TextureLoad");
    task->bc = dat2_buildcache;
    task->texture_id = texture_id;
    PT_INIT(&task->pt);
    return &task->task;
}
