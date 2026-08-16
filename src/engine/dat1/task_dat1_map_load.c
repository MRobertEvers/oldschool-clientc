#include "engine/dat1/dat1_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/torirs_map_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Dat1 map chunk loads. Unlike dat2 there is no reference table to consult:
 * the region -> archive mapping lives in the versionlist "map_index" the disk
 * decoded at open, so the IO layer resolves it (see load_cache_item_dat1) and
 * these tasks only decode.
 */

struct Task_Dat1MapLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int map_x;
    int map_z;
};

static int
Task_Dat1MapTerrainLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1MapLoad* task = (struct Task_Dat1MapLoad*)task_base;
    struct RSCache_MapTerrain* rscache_terrain = NULL;
    struct ToriRS_MapTerrain* torirs_terrain = NULL;
    int map_id = CacheProvider_MapId(task->map_x, task->map_z);

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat1MapTerrainLoad(io, 0, task->map_x, task->map_z);
    PT_YIELD(&task->pt);

    rscache_terrain = RSCache_IO_Dat1MapTerrainDecode(io, 0, task->map_x, task->map_z);
    if( !rscache_terrain )
    {
        fprintf(
            stderr,
            "Failed to load dat1 terrain for map (%d,%d)\n",
            task->map_x,
            task->map_z);
        PT_EXIT(&task->pt);
    }

    dat1_buildcache_map_terrain_add(task->bc, map_id, rscache_terrain);

    torirs_terrain = ToriRS_MapTerrainFromRSCache(task->map_x, task->map_z, rscache_terrain);
    CacheProvider_MapTerrainAdd(&task->bc->base, map_id, torirs_terrain);

    PT_END(&task->pt);
}

static int
Task_Dat1MapSceneryLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1MapLoad* task = (struct Task_Dat1MapLoad*)task_base;
    struct RSCache_MapLocs* rscache_locs = NULL;
    struct ToriRS_MapLocs* torirs_locs = NULL;
    int map_id = CacheProvider_MapId(task->map_x, task->map_z);

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat1MapSceneryLoad(io, 0, task->map_x, task->map_z);
    PT_YIELD(&task->pt);

    rscache_locs = RSCache_IO_Dat1MapSceneryDecode(io, 0);
    if( !rscache_locs )
    {
        fprintf(
            stderr,
            "Failed to load dat1 scenery for map (%d,%d)\n",
            task->map_x,
            task->map_z);
        PT_EXIT(&task->pt);
    }

    /* The loc stream carries only chunk-relative positions; the world builder
     * places the chunk from these (same fixup the dat2 task does). */
    rscache_locs->chunk_mapx = task->map_x;
    rscache_locs->chunk_mapz = task->map_z;
    dat1_buildcache_map_scenery_add(task->bc, map_id, rscache_locs);

    torirs_locs = ToriRS_MapLocsFromRSCache(rscache_locs);
    CacheProvider_MapSceneryAdd(&task->bc->base, map_id, torirs_locs);

    PT_END(&task->pt);
}

static void
Task_Dat1MapLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1MapTerrainLoad_VTable = {
    .run = Task_Dat1MapTerrainLoad_Run,
    .free = Task_Dat1MapLoad_Free,
};

static struct ToriRS_TaskVTable Task_Dat1MapSceneryLoad_VTable = {
    .run = Task_Dat1MapSceneryLoad_Run,
    .free = Task_Dat1MapLoad_Free,
};

static struct Task_Dat1MapLoad*
task_dat1_map_load_new(
    struct Dat1BuildCache* bc,
    struct ToriRS_TaskVTable* vtable,
    char const* name,
    int map_x,
    int map_z)
{
    struct Task_Dat1MapLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = vtable;
    strncpy(task->task.name, name, sizeof(task->task.name) - 1);
    task->bc = bc;
    task->map_x = map_x;
    task->map_z = map_z;
    PT_INIT(&task->pt);
    return task;
}

struct ToriRS_Task*
CreateTask_Dat1MapTerrainLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z)
{
    assert(provider);

    if( CacheProvider_MapTerrainHas(provider, CacheProvider_MapId(map_x, map_z)) )
        return NULL;

    return &task_dat1_map_load_new(
                (struct Dat1BuildCache*)provider,
                &Task_Dat1MapTerrainLoad_VTable,
                "Dat1MapTerrainLoad",
                map_x,
                map_z)
                ->task;
}

struct ToriRS_Task*
CreateTask_Dat1MapSceneryLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z)
{
    assert(provider);

    if( CacheProvider_MapSceneryHas(provider, CacheProvider_MapId(map_x, map_z)) )
        return NULL;

    return &task_dat1_map_load_new(
                (struct Dat1BuildCache*)provider,
                &Task_Dat1MapSceneryLoad_VTable,
                "Dat1MapSceneryLoad",
                map_x,
                map_z)
                ->task;
}
