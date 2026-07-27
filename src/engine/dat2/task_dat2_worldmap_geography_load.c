#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/png_decode.h"
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
/*
 * Where a record's tiles live.
 *
 * Up to OSRS 237 the compositemap record names its geography group and file
 * outright. From 238 the pair is gone (RSCache_WorldMapFlags), and the group is
 * addressed by the source region instead: table 18 is a sparse array indexed by
 * (region_x << 8) | region_y, one file per group. Verified against
 * cache.osrs239, whose 15938-slot table has 2101 populated groups: the first is
 * 3872 = region 15,32 and Lumbridge's 49,48 is 12592, both exactly that
 * packing.
 */
static int
worldmap_geography_group(struct ToriRS_WorldMapRegionSource const* source)
{
    if( source->group_id >= 0 )
        return source->group_id;
    return ((source->src_region_x & 0xFF) << 8) | (source->src_region_y & 0xFF);
}

static int
worldmap_geography_file(struct ToriRS_WorldMapRegionSource const* source)
{
    return source->file_id >= 0 ? source->file_id : 0;
}

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
    /* The ground image is per group, so it is fetched once for the first record
     * that names one — a chunk-assembled region shares its neighbours' image. */
    int ground_group;
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
        /* OSRS >= 238 is addressed (see worldmap_geography_group) but not yet
         * read: its headerless tile stream consumes exactly and still decodes to
         * floor ids that do not exist, so the record layout differs in a way
         * this decoder does not model. Drawing it would be drawing wrong data —
         * the surface stays background until the layout is worked out. */
        if( source->group_id < 0 )
            continue;

        RSCache_IO_Dat2WorldMapGeographyLoad(io, 0, worldmap_geography_group(source));
        PT_YIELD(&task->pt);

        /* Re-read after the yield: locals do not survive it. */
        source = &task->sources[task->cursor];
        archive = RSCache_IO_Dat2WorldMapGeographyDecode(io, 0);
        if( getenv("TORIRS_WORLDMAP_DEBUG") )
            fprintf(
                stderr,
                "worldmap geography: group=%d file=%d archive=%s files=%d\n",
                worldmap_geography_group(source),
                worldmap_geography_file(source),
                archive ? "ok" : "MISSING",
                archive ? archive->file_count : -1);
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
            bool derived = source->group_id < 0;
            if( archive->file_ids[i] != worldmap_geography_file(source) )
                continue;
            /* A derived address has no record fields to check the file against,
             * and the file's own marker decides whether it is a region or a
             * chunk — see RSCache_WorldMapGeographyDecodeInplace. */
            if( task->ground_group < 0 )
                task->ground_group = worldmap_geography_group(source);
            if( RSCache_WorldMapGeographyDecodeInplace(
                    task->geography,
                    filelist->files[i],
                    filelist->file_sizes[i],
                    derived ? -1 : source->kind,
                    source->planes,
                    derived ? -1 : source->dst_region_x,
                    derived ? -1 : source->dst_region_y,
                    (!derived && source->kind == 1) ? source->dst_chunk_x : -1,
                    (!derived && source->kind == 1) ? source->dst_chunk_y : -1) )
                task->decoded++;
            break;
        }

        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    /* Ground colours (table 20): a 64x64 PNG of the blended underlay colours the
     * cache pre-rendered. The reference reads exactly this; the flo's flat
     * colour is only the fallback for a cache that ships no ground table. */
    if( task->decoded > 0 && task->ground_group >= 0 )
    {
        RSCache_IO_Dat2WorldMapGroundLoad(io, 0, task->ground_group);
        PT_YIELD(&task->pt);

        archive = RSCache_IO_Dat2WorldMapGroundDecode(io, 0);
        if( archive )
        {
            filelist = RSCache_FileListNewFromDecode(
                archive->data, archive->data_size, archive->file_count);
            if( filelist && filelist->file_count > 0 )
            {
                int png_width = 0;
                int png_height = 0;
                uint32_t* png_pixels = NULL;
                if( PngDecode_Rgb(
                        filelist->files[0], filelist->file_sizes[0], &png_width, &png_height,
                        &png_pixels) )
                {
                    int copy_w = png_width < RSCACHE_WORLDMAP_TILE_COUNT
                                     ? png_width
                                     : RSCACHE_WORLDMAP_TILE_COUNT;
                    int copy_h = png_height < RSCACHE_WORLDMAP_TILE_COUNT
                                     ? png_height
                                     : RSCACHE_WORLDMAP_TILE_COUNT;
                    for( int row = 0; row < copy_h; row++ )
                    {
                        for( int col = 0; col < copy_w; col++ )
                            task->geography->ground[row * RSCACHE_WORLDMAP_TILE_COUNT + col] =
                                png_pixels[(size_t)row * png_width + col];
                    }
                    task->geography->has_ground = true;
                    free(png_pixels);
                }
            }
            RSCache_FileListFree(filelist);
            RSCache_Dat2DiskArchiveFree(archive);
        }
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
    task->ground_group = -1;
    task->sources = calloc((size_t)source_count, sizeof(*task->sources));
    assert(task->sources);
    memcpy(task->sources, sources, (size_t)source_count * sizeof(*task->sources));
    task->source_count = source_count;
    PT_INIT(&task->pt);
    return &task->task;
}
