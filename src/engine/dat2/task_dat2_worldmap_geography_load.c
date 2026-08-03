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
 *
 * Headerless kind=1 records often share one full 4096-tile geography file
 * across all 64 chunks of a region (Ardent Ocean Underground, Ancient Cavern).
 * Decoding that stream once per task and blitting each src_chunk→dst_chunk
 * avoids reparsing the same bytes 64 times.
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
 *
 * The file *content* at that address is now read too (EXCEPTIONS.md B21): it
 * is the same headerless tile stream as everywhere else in this era, with one
 * field width difference (a loc's id is a plain u32, not a BigSmart) — see
 * dat2_worldmap_geography.c's worldmap_read_tile.
 */
static int
worldmap_geography_group(struct ToriRS_WorldMapRegionSource const* source)
{
    if( source->group_id >= 0 )
        return source->group_id;
    return ((source->src_region_x & 0xFF) << 8) | (source->src_region_y & 0xFF);
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

struct WorldMapGeoFileCacheEntry
{
    int group_id;
    int file_id;
    struct RSCache_WorldMapGeography* full;
    bool is_chunk;
    int tile_count;
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
    /* Decoded headerless source files reused across chunk records. */
    struct WorldMapGeoFileCacheEntry file_cache[WORLDMAP_GEO_FILE_CACHE_CAP];
    int file_cache_count;
};

static void
worldmap_geo_file_cache_clear(struct Task_Dat2WorldMapGeographyLoad* task)
{
    for( int i = 0; i < task->file_cache_count; i++ )
    {
        if( task->file_cache[i].full )
        {
            RSCache_WorldMapGeographyFreeInplace(task->file_cache[i].full);
            free(task->file_cache[i].full);
            task->file_cache[i].full = NULL;
        }
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
    int data_size,
    int planes)
{
    struct WorldMapGeoFileCacheEntry* entry;
    struct RSCache_WorldMapGeography* full;
    bool is_chunk = false;
    int tile_count;

    if( task->file_cache_count >= WORLDMAP_GEO_FILE_CACHE_CAP )
        return NULL;

    full = calloc(1, sizeof(*full));
    assert(full);
    tile_count =
        RSCache_WorldMapGeographyDecodeHeaderlessFile(full, data, data_size, planes, &is_chunk);
    if( tile_count < 0 )
    {
        RSCache_WorldMapGeographyFreeInplace(full);
        free(full);
        return NULL;
    }

    entry = &task->file_cache[task->file_cache_count++];
    entry->group_id = group_id;
    entry->file_id = file_id;
    entry->full = full;
    entry->is_chunk = is_chunk;
    entry->tile_count = tile_count;
    return entry;
}

static bool
worldmap_apply_cached_chunk(
    struct RSCache_WorldMapGeography* dst,
    struct WorldMapGeoFileCacheEntry const* entry,
    struct ToriRS_WorldMapRegionSource const* source)
{
    int src_cx = source->src_chunk_x;
    int src_cy = source->src_chunk_y;
    int dst_cx = source->dst_chunk_x;
    int dst_cy = source->dst_chunk_y;

    if( entry->is_chunk )
        return RSCache_WorldMapGeographyBlitChunk(dst, entry->full, 0, 0, dst_cx, dst_cy);

    {
        int columns = entry->tile_count / RSCACHE_WORLDMAP_TILE_COUNT;
        if( src_cx < 0 || src_cy < 0 || src_cx >= 8 || src_cy >= 8 )
            return false;
        if( src_cx * 8 + 8 > columns )
            return false;
    }
    return RSCache_WorldMapGeographyBlitChunk(
        dst, entry->full, src_cx, src_cy, dst_cx, dst_cy);
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
         * share one full-region file skip the archive round-trip. */
        source = &task->sources[task->cursor];
        {
            int const area_id = (task->key >> 24) & 0xFF;
            int const group_id = worldmap_geography_group(source);
            int const want_file = worldmap_geography_file(source, area_id);
            bool const derived = source->group_id < 0;

            if( derived && source->kind == 1 )
            {
                struct WorldMapGeoFileCacheEntry* cached =
                    worldmap_geo_file_cache_find(task, group_id, want_file);
                if( cached )
                {
                    if( worldmap_apply_cached_chunk(task->geography, cached, source) )
                        task->decoded++;
                    else if( getenv("TORIRS_WORLDMAP_DEBUG") )
                        fprintf(
                            stderr,
                            "worldmap geography: cached blit FAILED group=%d file=%d "
                            "src_chunk=%d,%d dst_chunk=%d,%d\n",
                            group_id,
                            want_file,
                            source->src_chunk_x,
                            source->src_chunk_y,
                            source->dst_chunk_x,
                            source->dst_chunk_y);
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

            if( derived && source->kind == 1 )
            {
                struct WorldMapGeoFileCacheEntry* cached = worldmap_geo_file_cache_put(
                    task,
                    group_id,
                    want_file,
                    filelist->files[i],
                    filelist->file_sizes[i],
                    source->planes);
                if( cached &&
                    worldmap_apply_cached_chunk(task->geography, cached, source) )
                {
                    task->decoded++;
                }
                else if( !cached &&
                         RSCache_WorldMapGeographyDecodeInplace(
                             task->geography,
                             filelist->files[i],
                             filelist->file_sizes[i],
                             true,
                             source->kind,
                             source->planes,
                             -1,
                             -1,
                             source->src_chunk_x,
                             source->src_chunk_y,
                             source->dst_chunk_x,
                             source->dst_chunk_y) )
                {
                    /* Cache full: still decode this record directly. */
                    task->decoded++;
                }
                else if( getenv("TORIRS_WORLDMAP_DEBUG") )
                    fprintf(
                        stderr,
                        "worldmap geography: decode FAILED group=%d file=%d size=%d "
                        "kind=%d derived=%d src_chunk=%d,%d dst_chunk=%d,%d\n",
                        group_id,
                        want_file,
                        filelist->file_sizes[i],
                        source->kind,
                        (int)derived,
                        source->src_chunk_x,
                        source->src_chunk_y,
                        source->dst_chunk_x,
                        source->dst_chunk_y);
            }
            else if( RSCache_WorldMapGeographyDecodeInplace(
                         task->geography,
                         filelist->files[i],
                         filelist->file_sizes[i],
                         derived,
                         source->kind,
                         source->planes,
                         derived ? -1 : source->dst_region_x,
                         derived ? -1 : source->dst_region_y,
                         source->src_chunk_x,
                         source->src_chunk_y,
                         source->dst_chunk_x,
                         source->dst_chunk_y) )
            {
                task->decoded++;
            }
            else if( getenv("TORIRS_WORLDMAP_DEBUG") )
                fprintf(
                    stderr,
                    "worldmap geography: decode FAILED group=%d file=%d size=%d "
                    "kind=%d derived=%d src_chunk=%d,%d dst_chunk=%d,%d\n",
                    group_id,
                    want_file,
                    filelist->file_sizes[i],
                    source->kind,
                    (int)derived,
                    source->src_chunk_x,
                    source->src_chunk_y,
                    source->dst_chunk_x,
                    source->dst_chunk_y);
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
