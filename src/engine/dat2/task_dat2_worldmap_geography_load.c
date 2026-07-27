#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/torirs_types.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * One map surface region's tiles (cache table 18).
 *
 * A region either comes whole from a single compositemap record, or is stitched
 * from up to 64 chunk records — each naming its own group/file. They decode into
 * one geography struct, so this task walks the region's records in order and
 * loads each file the first time its group is needed. Groups repeat across
 * records (a chunk-built region usually draws several chunks from one group),
 * and the IO layer caches archives, so the repeat load is a hash lookup rather
 * than a disk read.
 *
 * The whole region is one task rather than one task per file: the renderer
 * cannot draw a half-decoded region, and the caller has one thing to wait on.
 */
struct Task_Dat2WorldMapGeographyLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int key;
    /* Owned copy: the area's array can be freed while this task is in flight. */
    struct ToriRS_WorldMapRegionSource* sources;
    int source_count;
    int cursor;
    struct RSCache_WorldMapGeography* geography;
    int decoded;
};

static int
Task_Dat2WorldMapGeographyLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2WorldMapGeographyLoad* task =
        (struct Task_Dat2WorldMapGeographyLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct ToriRS_WorldMapRegionSource const* source = NULL;

    PT_BEGIN(&task->pt);

    task->geography = calloc(1, sizeof(*task->geography));
    assert(task->geography);

    for( task->cursor = 0; task->cursor < task->source_count; task->cursor++ )
    {
        source = &task->sources[task->cursor];
        if( source->group_id < 0 || source->file_id < 0 )
            continue;

        RSCache_IO_Dat2WorldMapGeographyLoad(io, 0, source->group_id);
        PT_YIELD(&task->pt);

        /* Re-read after the yield: locals do not survive it. */
        source = &task->sources[task->cursor];
        archive = RSCache_IO_Dat2WorldMapGeographyDecode(io, 0);
        if( !archive )
            continue;

        filelist =
            RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        if( !filelist || !archive->file_ids )
        {
            RSCache_FileListFree(filelist);
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }

        for( int i = 0; i < filelist->file_count; i++ )
        {
            if( archive->file_ids[i] != source->file_id )
                continue;
            if( RSCache_WorldMapGeographyDecodeInplace(
                    task->geography,
                    filelist->files[i],
                    filelist->file_sizes[i],
                    source->kind,
                    source->planes,
                    source->dst_region_x,
                    source->dst_region_y,
                    source->kind == 1 ? source->dst_chunk_x : -1,
                    source->kind == 1 ? source->dst_chunk_y : -1) )
                task->decoded++;
            break;
        }

        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    if( task->decoded == 0 )
    {
        /* Nothing decoded: still publish the empty geography, so the renderer
         * bakes a background-coloured region once instead of asking again every
         * frame for a region this cache has no tiles for. */
        if( getenv("TORIRS_WORLDMAP_DEBUG") )
            fprintf(stderr, "worldmap geography: key 0x%08x decoded nothing\n", task->key);
    }

    CacheProvider_WorldMapGeographyAdd(&task->bc->base, task->key, task->geography);
    task->geography = NULL;

    PT_END(&task->pt);
}

static void
Task_Dat2WorldMapGeographyLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2WorldMapGeographyLoad* task =
        (struct Task_Dat2WorldMapGeographyLoad*)task_base;
    if( task->geography )
    {
        RSCache_WorldMapGeographyFreeInplace(task->geography);
        free(task->geography);
    }
    free(task->sources);
    free(task);
}

static struct ToriRS_TaskVTable Task_Dat2WorldMapGeographyLoad_VTable = {
    .run = Task_Dat2WorldMapGeographyLoad_Run,
    .free = Task_Dat2WorldMapGeographyLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2WorldMapGeographyLoad(
    struct CacheProvider* provider,
    int key,
    struct ToriRS_WorldMapRegionSource const* sources,
    int source_count)
{
    struct Task_Dat2WorldMapGeographyLoad* task;

    assert(provider);

    if( !sources || source_count <= 0 )
        return NULL;
    if( CacheProvider_WorldMapGeographyHas(provider, key) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2WorldMapGeographyLoad_VTable;
    strcpy(task->task.name, "Dat2WorldMapGeographyLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->key = key;
    task->sources = calloc((size_t)source_count, sizeof(*task->sources));
    assert(task->sources);
    memcpy(task->sources, sources, (size_t)source_count * sizeof(*task->sources));
    task->source_count = source_count;
    PT_INIT(&task->pt);
    return &task->task;
}
