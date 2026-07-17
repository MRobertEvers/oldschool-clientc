#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_map_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2MapLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int map_x;
    int map_z;
};

static int
task_dat2_map_resolve_archive_id(
    struct RSCache_ReferenceTable* table,
    const char* name_fmt,
    int map_x,
    int map_z)
{
    char name[16];
    int name_hash;

    assert(table);

    snprintf(name, sizeof(name), name_fmt, map_x, map_z);
    name_hash = RSCache_ArchiveNameHashDat2(name);

    for( int i = 0; i < table->archive_count; i++ )
    {
        if( table->archives[i].identifier == name_hash )
            return table->archives[i].index;
    }
    return -1;
}

static int
Task_Dat2MapTerrainLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2MapLoad* task = (struct Task_Dat2MapLoad*)task_base;
    struct RSCache_ReferenceTable* table = NULL;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_MapTerrain* rscache_terrain = NULL;
    struct ToriRS_MapTerrain* torirs_terrain = NULL;
    int map_id = CacheProvider_MapId(task->map_x, task->map_z);
    int archive_id = -1;

    PT_BEGIN(&task->pt);

    if( !dat2_buildcache_reference_table_has(task->bc, RSCACHE_DAT2_DISK_TABLE_MAPS) )
    {
        RSCache_IO_Dat2ReferenceTableLoad(io, 0, RSCACHE_DAT2_DISK_TABLE_MAPS);
        PT_YIELD(&task->pt);

        table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
        if( !table )
        {
            fprintf(stderr, "Failed to load maps reference table\n");
            PT_EXIT(&task->pt);
        }
        dat2_buildcache_reference_table_add(task->bc, RSCACHE_DAT2_DISK_TABLE_MAPS, table);
    }

    table = dat2_buildcache_reference_table_get(task->bc, RSCACHE_DAT2_DISK_TABLE_MAPS);
    assert(table);

    archive_id = task_dat2_map_resolve_archive_id(table, "m%d_%d", task->map_x, task->map_z);
    if( archive_id < 0 )
    {
        fprintf(stderr, "No terrain archive for map (%d,%d)\n", task->map_x, task->map_z);
        PT_EXIT(&task->pt);
    }

    RSCache_IO_Dat2MapArchiveLoad(io, 0, archive_id);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2MapArchiveDecode(io, 0);
    if( !archive )
    {
        fprintf(stderr, "Failed to load terrain archive for map (%d,%d)\n", task->map_x, task->map_z);
        PT_EXIT(&task->pt);
    }

    rscache_terrain = RSCache_MapTerrainNewFromArchive(archive, task->map_x, task->map_z);
    RSCache_Dat2DiskArchiveFree(archive);
    if( !rscache_terrain )
    {
        fprintf(stderr, "Failed to decode terrain for map (%d,%d)\n", task->map_x, task->map_z);
        PT_EXIT(&task->pt);
    }

    dat2_buildcache_map_terrain_add(task->bc, map_id, rscache_terrain);

    torirs_terrain =
        ToriRS_MapTerrainFromRSCache(task->map_x, task->map_z, rscache_terrain);
    CacheProvider_MapTerrainAdd(&task->bc->base, map_id, torirs_terrain);

    PT_END(&task->pt);
}

static int
Task_Dat2MapSceneryLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2MapLoad* task = (struct Task_Dat2MapLoad*)task_base;
    struct RSCache_ReferenceTable* table = NULL;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_MapLocs* rscache_locs = NULL;
    struct ToriRS_MapLocs* torirs_locs = NULL;
    int map_id = CacheProvider_MapId(task->map_x, task->map_z);
    int archive_id = -1;

    PT_BEGIN(&task->pt);

    if( !dat2_buildcache_reference_table_has(task->bc, RSCACHE_DAT2_DISK_TABLE_MAPS) )
    {
        RSCache_IO_Dat2ReferenceTableLoad(io, 0, RSCACHE_DAT2_DISK_TABLE_MAPS);
        PT_YIELD(&task->pt);

        table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
        if( !table )
        {
            fprintf(stderr, "Failed to load maps reference table\n");
            PT_EXIT(&task->pt);
        }
        dat2_buildcache_reference_table_add(task->bc, RSCACHE_DAT2_DISK_TABLE_MAPS, table);
    }

    table = dat2_buildcache_reference_table_get(task->bc, RSCACHE_DAT2_DISK_TABLE_MAPS);
    assert(table);

    archive_id = task_dat2_map_resolve_archive_id(table, "l%d_%d", task->map_x, task->map_z);
    if( archive_id < 0 )
    {
        fprintf(stderr, "No scenery archive for map (%d,%d)\n", task->map_x, task->map_z);
        PT_EXIT(&task->pt);
    }

    RSCache_IO_Dat2MapArchiveLoad(io, 0, archive_id);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2MapArchiveDecode(io, 0);
    if( !archive )
    {
        fprintf(stderr, "Failed to load scenery archive for map (%d,%d)\n", task->map_x, task->map_z);
        PT_EXIT(&task->pt);
    }

    rscache_locs = RSCache_MapLocsNewDecode(archive->data, archive->data_size);
    RSCache_Dat2DiskArchiveFree(archive);
    if( !rscache_locs )
    {
        fprintf(stderr, "Failed to decode scenery for map (%d,%d)\n", task->map_x, task->map_z);
        PT_EXIT(&task->pt);
    }

    rscache_locs->chunk_mapx = task->map_x;
    rscache_locs->chunk_mapz = task->map_z;
    dat2_buildcache_map_scenery_add(task->bc, map_id, rscache_locs);

    torirs_locs = ToriRS_MapLocsFromRSCache(rscache_locs);
    CacheProvider_MapSceneryAdd(&task->bc->base, map_id, torirs_locs);

    PT_END(&task->pt);
}

static void
Task_Dat2MapLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2MapTerrainLoad_VTable = {
    .run = Task_Dat2MapTerrainLoad_Run,
    .free = Task_Dat2MapLoad_Free,
};

static struct ToriRS_TaskVTable Task_Dat2MapSceneryLoad_VTable = {
    .run = Task_Dat2MapSceneryLoad_Run,
    .free = Task_Dat2MapLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2MapTerrainLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2MapLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_MapTerrainHas(provider, CacheProvider_MapId(map_x, map_z)) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2MapTerrainLoad_VTable;
    strcpy(task->task.name, "Dat2MapTerrainLoad");
    task->bc = dat2_buildcache;
    task->map_x = map_x;
    task->map_z = map_z;
    PT_INIT(&task->pt);
    return &task->task;
}

struct ToriRS_Task*
CreateTask_Dat2MapSceneryLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2MapLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_MapSceneryHas(provider, CacheProvider_MapId(map_x, map_z)) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2MapSceneryLoad_VTable;
    strcpy(task->task.name, "Dat2MapSceneryLoad");
    task->bc = dat2_buildcache;
    task->map_x = map_x;
    task->map_z = map_z;
    PT_INIT(&task->pt);
    return &task->task;
}
