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
 * from up to 64 chunk records. On OSRS >= 238 the archive is addressed by the
 * *destination* region ((dst_rx << 8) | dst_ry) and one file (keyed by area id)
 * holds every zone for that display region: kind=0 is a 4096-tile x-major
 * stream; kind=1 is consecutive 64-tile zone blocks in compositemap record
 * order. The task walks the region's records in that order and advances one
 * shared cursor through the file.
 *
 * The whole region is one task rather than one task per file: the renderer
 * cannot draw a half-decoded region, and the caller has one thing to wait on.
 */
/*
 * Where a record's tiles live.
 *
 * Up to OSRS 237 the compositemap record names its geography group and file
 * outright. From 238 the pair is gone (RSCache_WorldMapFlags), and the group is
 * addressed by the destination region: table 18 is a sparse array indexed by
 * (dst_region_x << 8) | dst_region_y, one file per group. Verified against
 * cache.osrs239 / compositemap.wmc: every kind=1 group's file holds exactly
 * 64 * record_count tiles when keyed by dst region (0 mismatches); keying by
 * src region leaves 46 mismatched groups and 29 missing. The reference client
 * (class184.method5887) loads the same key from the display region's own
 * coords.
 *
 * The file *content* at that address is a headerless tile stream (EXCEPTIONS.md
 * B21): loc ids are plain u32, not BigSmart — see dat2_worldmap_geography.c's
 * worldmap_read_tile.
 */
static int
worldmap_geography_group(struct ToriRS_WorldMapRegionSource const* source)
{
    if( source->group_id >= 0 )
        return source->group_id;
    return ((source->dst_region_x & 0xFF) << 8) | (source->dst_region_y & 0xFF);
}

/*
 * Which file inside the geography group to read.
 *
 * Named composemap records (pre-238) carry file_id outright. Derived addressing
 * (OSRS >= 238, group_id < 0) used to hardcode 0, which only works for the main
 * map (area id 0): every other area's single-file group stores the *area id* as
 * the file id, so want=0 skipped the only file, Decode never ran, and the
 * renderer baked a solid background_colour — black. Measured:
 *   braindeath (area 4) -> skip file_id=4 want=0
 *   ancient_cavern (area 1) -> skip file_id=1 want=0
 */
static int
worldmap_geography_file(
    struct ToriRS_WorldMapRegionSource const* source,
    int area_id)
{
    if( source->file_id >= 0 )
        return source->file_id;
    return area_id & 0xFF;
}

enum
{
    WORLDMAP_GEO_FILE_CACHE_CAP = 8,
};

/*
 * Owned copy of one headerless geography file plus a cursor. Sibling chunk
 * records for the same destination region advance this cursor; the archive
 * and filelist are freed each loop iteration, so the bytes must be copied.
 */
struct WorldMapGeoFileCacheEntry
{
    int group_id;
    int file_id;
    uint8_t* data;
    int data_size;
    struct RSCache_WorldMapGeographyReader reader;
};

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
    struct WorldMapGeoFileCacheEntry file_cache[WORLDMAP_GEO_FILE_CACHE_CAP];
    int file_cache_count;
};

static void
worldmap_geo_file_cache_clear(struct Task_Dat2WorldMapGeographyLoad* task)
{
    for( int i = 0; i < task->file_cache_count; i++ )
    {
        free(task->file_cache[i].data);
        task->file_cache[i].data = NULL;
        task->file_cache[i].data_size = 0;
    }
    task->file_cache_count = 0;
}

static struct WorldMapGeoFileCacheEntry*
worldmap_geo_file_cache_find(
    struct Task_Dat2WorldMapGeographyLoad* task,
    int group_id,
    int file_id)
{
    for( int i = 0; i < task->file_cache_count; i++ )
    {
        if( task->file_cache[i].group_id == group_id && task->file_cache[i].file_id == file_id )
            return &task->file_cache[i];
    }
    return NULL;
}

static struct WorldMapGeoFileCacheEntry*
worldmap_geo_file_cache_put(
    struct Task_Dat2WorldMapGeographyLoad* task,
    int group_id,
    int file_id,
    const void* data,
    int data_size)
{
    struct WorldMapGeoFileCacheEntry* entry;
    uint8_t* copy;

    if( task->file_cache_count >= WORLDMAP_GEO_FILE_CACHE_CAP )
        return NULL;
    if( !data || data_size <= 0 )
        return NULL;

    copy = malloc((size_t)data_size);
    assert(copy);
    memcpy(copy, data, (size_t)data_size);

    entry = &task->file_cache[task->file_cache_count++];
    entry->group_id = group_id;
    entry->file_id = file_id;
    entry->data = copy;
    entry->data_size = data_size;
    RSCache_WorldMapGeographyReaderInit(&entry->reader, copy, data_size);
    return entry;
}

static bool
worldmap_read_source_from_entry(
    struct RSCache_WorldMapGeography* dst,
    struct WorldMapGeoFileCacheEntry* entry,
    struct ToriRS_WorldMapRegionSource const* source)
{
    assert(dst);
    assert(entry);
    assert(source);

    if( source->kind == 0 )
        return RSCache_WorldMapGeographyReadRegion(dst, &entry->reader, source->planes);
    assert(source->kind == 1);
    assert(source->dst_chunk_x >= 0 && source->dst_chunk_x < 8);
    assert(source->dst_chunk_y >= 0 && source->dst_chunk_y < 8);
    return RSCache_WorldMapGeographyReadChunk(
        dst, &entry->reader, source->planes, source->dst_chunk_x, source->dst_chunk_y);
}

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
        /* Area id is the high byte of the geography cache key
         * (CacheProvider_WorldMapGeographyKey); derived addressing picks the
         * file by that id. Checked before IO so sibling chunk records that
         * share one file skip the archive round-trip and only advance the
         * cursor. */
        source = &task->sources[task->cursor];
        {
            int const area_id = (task->key >> 24) & 0xFF;
            int const group_id = worldmap_geography_group(source);
            int const want_file = worldmap_geography_file(source, area_id);
            bool const derived = source->group_id < 0;

            if( derived )
            {
                struct WorldMapGeoFileCacheEntry* cached =
                    worldmap_geo_file_cache_find(task, group_id, want_file);
                if( cached )
                {
                    if( worldmap_read_source_from_entry(task->geography, cached, source) )
                        task->decoded++;
                    else if( getenv("TORIRS_WORLDMAP_DEBUG") )
                        fprintf(
                            stderr,
                            "worldmap geography: cached read FAILED group=%d file=%d "
                            "kind=%d dst_chunk=%d,%d remaining=%u\n",
                            group_id,
                            want_file,
                            source->kind,
                            source->dst_chunk_x,
                            source->dst_chunk_y,
                            RSCache_WorldMapGeographyReaderRemaining(&cached->reader));
                    continue;
                }
            }
        }

        RSCache_IO_Dat2WorldMapGeographyLoad(io, 0, worldmap_geography_group(source));
        PT_YIELD(&task->pt);

        /* Re-read after the yield: locals do not survive it. */
        source = &task->sources[task->cursor];
        int const area_id = (task->key >> 24) & 0xFF;
        int const group_id = worldmap_geography_group(source);
        int const want_file = worldmap_geography_file(source, area_id);
        bool const derived = source->group_id < 0;

        archive = RSCache_IO_Dat2WorldMapGeographyDecode(io, 0);
        if( getenv("TORIRS_WORLDMAP_DEBUG") )
            fprintf(
                stderr,
                "worldmap geography: group=%d file=%d archive=%s files=%d\n",
                group_id,
                want_file,
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
            if( archive->file_ids[i] != want_file )
            {
                if( getenv("TORIRS_WORLDMAP_DEBUG") )
                    fprintf(
                        stderr,
                        "worldmap geography: skip file_id=%d want=%d (group=%d)\n",
                        archive->file_ids[i],
                        want_file,
                        group_id);
                continue;
            }
            if( task->ground_group < 0 )
                task->ground_group = group_id;

            if( derived )
            {
                struct WorldMapGeoFileCacheEntry* cached = worldmap_geo_file_cache_put(
                    task, group_id, want_file, filelist->files[i], filelist->file_sizes[i]);
                if( !cached )
                {
                    /* Cache full: decode this record directly without a cursor. */
                    if( RSCache_WorldMapGeographyDecodeInplace(
                            task->geography,
                            filelist->files[i],
                            filelist->file_sizes[i],
                            true,
                            source->kind,
                            source->planes,
                            -1,
                            -1,
                            source->dst_chunk_x,
                            source->dst_chunk_y) )
                        task->decoded++;
                    else if( getenv("TORIRS_WORLDMAP_DEBUG") )
                        fprintf(
                            stderr,
                            "worldmap geography: decode FAILED group=%d file=%d size=%d "
                            "kind=%d derived=1 dst_chunk=%d,%d\n",
                            group_id,
                            want_file,
                            filelist->file_sizes[i],
                            source->kind,
                            source->dst_chunk_x,
                            source->dst_chunk_y);
                }
                else if( worldmap_read_source_from_entry(task->geography, cached, source) )
                {
                    task->decoded++;
                }
                else if( getenv("TORIRS_WORLDMAP_DEBUG") )
                    fprintf(
                        stderr,
                        "worldmap geography: read FAILED group=%d file=%d size=%d "
                        "kind=%d dst_chunk=%d,%d\n",
                        group_id,
                        want_file,
                        filelist->file_sizes[i],
                        source->kind,
                        source->dst_chunk_x,
                        source->dst_chunk_y);
            }
            else if( RSCache_WorldMapGeographyDecodeInplace(
                         task->geography,
                         filelist->files[i],
                         filelist->file_sizes[i],
                         false,
                         source->kind,
                         source->planes,
                         source->dst_region_x,
                         source->dst_region_y,
                         source->dst_chunk_x,
                         source->dst_chunk_y) )
            {
                task->decoded++;
            }
            else if( getenv("TORIRS_WORLDMAP_DEBUG") )
                fprintf(
                    stderr,
                    "worldmap geography: decode FAILED group=%d file=%d size=%d "
                    "kind=%d derived=0 dst_chunk=%d,%d\n",
                    group_id,
                    want_file,
                    filelist->file_sizes[i],
                    source->kind,
                    source->dst_chunk_x,
                    source->dst_chunk_y);
            break;
        }

        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    if( getenv("TORIRS_WORLDMAP_DEBUG") )
    {
        for( int i = 0; i < task->file_cache_count; i++ )
        {
            uint32_t left =
                RSCache_WorldMapGeographyReaderRemaining(&task->file_cache[i].reader);
            if( left != 0 )
                fprintf(
                    stderr,
                    "worldmap geography: leftover %u bytes group=%d file=%d size=%d\n",
                    left,
                    task->file_cache[i].group_id,
                    task->file_cache[i].file_id,
                    task->file_cache[i].data_size);
        }
    }

    /* Ground colours (table 20): a 64x64 PNG of the blended underlay colours the
     * cache pre-rendered. The reference reads exactly this; the flo's flat
     * colour is only the fallback for a cache that ships no ground table.
     * Same destination-region key as geography. */
    if( task->decoded > 0 && task->ground_group >= 0 )
    {
        RSCache_IO_Dat2WorldMapGroundLoad(io, 0, task->ground_group);
        PT_YIELD(&task->pt);

        archive = RSCache_IO_Dat2WorldMapGroundDecode(io, 0);
        if( archive )
        {
            int const area_id = (task->key >> 24) & 0xFF;
            int file_index = 0;
            filelist = RSCache_FileListNewFromDecode(
                archive->data, archive->data_size, archive->file_count);
            if( filelist && filelist->file_count > 0 )
            {
                if( archive->file_ids )
                {
                    for( int i = 0; i < filelist->file_count; i++ )
                    {
                        if( archive->file_ids[i] == (area_id & 0xFF) )
                        {
                            file_index = i;
                            break;
                        }
                    }
                }
                int png_width = 0;
                int png_height = 0;
                uint32_t* png_pixels = NULL;
                if( PngDecode_Rgb(
                        filelist->files[file_index],
                        filelist->file_sizes[file_index],
                        &png_width,
                        &png_height,
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

    worldmap_geo_file_cache_clear(task);

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
    worldmap_geo_file_cache_clear(task);
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

    if( source_count <= 0 )
        return NULL;
    assert(sources);
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
