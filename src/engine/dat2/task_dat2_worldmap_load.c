#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/png_decode.h"
#include "engine/torirs_types.h"
#include "engine/torirs_worldmap_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The world map lives in three archives of table 19: "details" holds one file
 * per map area, "compositemap" holds that area's map element icons, and
 * "compositetexture" holds the overview-pane PNG (clientCode 1401). Details and
 * compositemap list the same file ids so icons attach by id; compositetexture
 * uses those ids too. Loading is all-or-nothing — the scripts ask "is the world
 * map loaded", not "is area N loaded".
 */
struct Task_Dat2WorldMapLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    /* Locals do not survive a protothread yield, so earlier archives wait here
     * while later ones load. */
    struct RSCache_Dat2DiskArchive* details;
    struct RSCache_Dat2DiskArchive* composite;
    int composite_archive_id;
    int texture_archive_id;
};

static int
task_dat2_worldmap_resolve_archive_id(
    struct RSCache_ReferenceTable* table,
    char const* name,
    int unnamed_fallback_id)
{
    int name_hash;

    assert(table);

    name_hash = RSCache_ArchiveNameHashDat2((char*)name);
    for( int i = 0; i < table->archive_count; i++ )
    {
        if( table->archives[i].identifier == name_hash )
            return table->archives[i].index;
    }
    /* OSRS 238+: details/compositemap/compositetexture may be unnamed; fall
     * back to archive id. */
    if( unnamed_fallback_id >= 0 && unnamed_fallback_id < table->archive_count &&
        table->archives[unnamed_fallback_id].index >= 0 )
        return table->archives[unnamed_fallback_id].index;
    return -1;
}

static struct ToriRS_WorldMapAreas*
task_dat2_worldmap_decode(
    struct RSCache_Dat2DiskArchive* details,
    struct RSCache_Dat2DiskArchive* composite,
    int worldmap_flags)
{
    struct RSCache_FileList* details_files = NULL;
    struct RSCache_FileList* composite_files = NULL;
    struct RSCache_WorldMapArea* entries = NULL;
    struct ToriRS_WorldMapAreas* areas = NULL;
    int count = 0;

    assert(details);

    details_files =
        RSCache_FileListNewFromDecode(details->data, details->data_size, details->file_count);
    if( !details_files || details_files->file_count <= 0 || !details->file_ids )
    {
        fprintf(stderr, "worldmap: failed to split the details archive\n");
        RSCache_FileListFree(details_files);
        return NULL;
    }

    if( composite && composite->file_ids )
    {
        composite_files = RSCache_FileListNewFromDecode(
            composite->data, composite->data_size, composite->file_count);
    }

    entries = calloc((size_t)details_files->file_count, sizeof(*entries));
    assert(entries);

    for( int i = 0; i < details_files->file_count; i++ )
    {
        int file_id = details->file_ids[i];

        if( !RSCache_WorldMapAreaDecodeInplace(
                &entries[count], file_id, details_files->files[i], details_files->file_sizes[i]) )
        {
            fprintf(stderr, "worldmap: failed to decode area %d\n", file_id);
            RSCache_WorldMapAreaFreeInplace(&entries[count]);
            continue;
        }

        if( composite_files )
        {
            for( int j = 0; j < composite_files->file_count; j++ )
            {
                if( composite->file_ids[j] != file_id )
                    continue;
                RSCache_WorldMapAreaDecodeIconsInplace(
                    &entries[count],
                    composite_files->files[j],
                    composite_files->file_sizes[j],
                    worldmap_flags);
                break;
            }
        }
        count++;
    }

    areas = ToriRS_WorldMapAreasFromRSCacheDat2(entries, count);

    for( int i = 0; i < count; i++ )
        RSCache_WorldMapAreaFreeInplace(&entries[i]);
    free(entries);
    RSCache_FileListFree(composite_files);
    RSCache_FileListFree(details_files);
    return areas;
}

/* Attach each compositetexture PNG to the matching area (same file id). The
 * overview pane scale-blits these; missing textures leave overview_pixels NULL
 * and the emit path falls back to the area background fill alone. */
static void
task_dat2_worldmap_attach_compositetextures(
    struct ToriRS_WorldMapAreas* areas,
    struct RSCache_Dat2DiskArchive* texture)
{
    struct RSCache_FileList* files = NULL;

    assert(areas);
    if( !texture || !texture->file_ids || texture->file_count <= 0 )
        return;

    files = RSCache_FileListNewFromDecode(texture->data, texture->data_size, texture->file_count);
    if( !files || files->file_count <= 0 )
    {
        RSCache_FileListFree(files);
        return;
    }

    for( int i = 0; i < files->file_count; i++ )
    {
        int file_id = texture->file_ids[i];
        struct ToriRS_WorldMapArea* area;
        int width = 0;
        int height = 0;
        uint32_t* rgb = NULL;
        uint32_t* argb = NULL;
        int pixel_count;

        area = ToriRS_WorldMapAreasGet(areas, file_id);
        if( !area )
            continue;
        if( !PngDecode_Rgb(files->files[i], files->file_sizes[i], &width, &height, &rgb) )
        {
            fprintf(stderr, "worldmap: compositetexture %d: PNG decode failed\n", file_id);
            continue;
        }
        assert(width > 0 && height > 0 && rgb);
        pixel_count = width * height;
        argb = malloc((size_t)pixel_count * sizeof(*argb));
        if( !argb )
        {
            free(rgb);
            continue;
        }
        /* PngDecode_Rgb drops alpha to 0; overview sprites need opaque ARGB. */
        for( int p = 0; p < pixel_count; p++ )
            argb[p] = 0xFF000000u | (rgb[p] & 0xFFFFFFu);
        free(rgb);

        free(area->overview_pixels);
        area->overview_pixels = argb;
        area->overview_width = width;
        area->overview_height = height;
    }

    RSCache_FileListFree(files);
}

static int
Task_Dat2WorldMapLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2WorldMapLoad* task = (struct Task_Dat2WorldMapLoad*)task_base;
    struct RSCache_ReferenceTable* table = NULL;
    struct RSCache_Dat2DiskArchive* texture = NULL;
    struct ToriRS_WorldMapAreas* areas = NULL;
    int details_archive_id = -1;

    PT_BEGIN(&task->pt);

    /* Table 19 is the world map only in OldSchool; the RS2 branch keeps objs there. Ask
     * before loading, or a 643 cache renders obj configs as map areas. */
    if( !RSCache_IO_ProfileHasDat2Table(
            CacheProvider_Profile(&task->bc->base), RSCACHE_DAT2_TABLE_WORLDMAP) )
    {
        fprintf(stderr, "worldmap: this cache's branch has no world map table\n");
        PT_EXIT(&task->pt);
    }

    if( !dat2_buildcache_reference_table_has(task->bc, RSCACHE_DAT2_TABLE_WORLDMAP) )
    {
        RSCache_IO_Dat2ReferenceTableLoad(io, 0, RSCACHE_DAT2_TABLE_WORLDMAP);
        PT_YIELD(&task->pt);

        table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
        if( !table )
        {
            fprintf(stderr, "worldmap: no reference table (cache has no world map)\n");
            PT_EXIT(&task->pt);
        }
        dat2_buildcache_reference_table_add(task->bc, RSCACHE_DAT2_TABLE_WORLDMAP, table);
    }

    table = dat2_buildcache_reference_table_get(task->bc, RSCACHE_DAT2_TABLE_WORLDMAP);
    assert(table);

    details_archive_id = task_dat2_worldmap_resolve_archive_id(table, "details", 0);
    if( details_archive_id < 0 )
    {
        fprintf(stderr, "worldmap: no \"details\" archive in the world map table\n");
        PT_EXIT(&task->pt);
    }
    task->composite_archive_id = task_dat2_worldmap_resolve_archive_id(table, "compositemap", 1);
    task->texture_archive_id =
        task_dat2_worldmap_resolve_archive_id(table, "compositetexture", 2);

    RSCache_IO_Dat2WorldMapArchiveLoad(io, 0, details_archive_id);
    PT_YIELD(&task->pt);

    task->details = RSCache_IO_Dat2WorldMapArchiveDecode(io, 0);
    if( !task->details )
    {
        fprintf(stderr, "worldmap: failed to load the \"details\" archive\n");
        PT_EXIT(&task->pt);
    }

    /* Icons are optional: an area without them still pans and reports bounds. */
    if( task->composite_archive_id >= 0 )
    {
        RSCache_IO_Dat2WorldMapArchiveLoad(io, 0, task->composite_archive_id);
        PT_YIELD(&task->pt);
        task->composite = RSCache_IO_Dat2WorldMapArchiveDecode(io, 0);
    }

    /* Overview textures are optional the same way: the pane still opens and
     * CS2 still draws the red viewport rects without them. */
    if( task->texture_archive_id >= 0 )
    {
        RSCache_IO_Dat2WorldMapArchiveLoad(io, 0, task->texture_archive_id);
        PT_YIELD(&task->pt);
        texture = RSCache_IO_Dat2WorldMapArchiveDecode(io, 0);
    }

    areas = task_dat2_worldmap_decode(
        task->details,
        task->composite,
        RSCache_WorldMapFlags(CacheProvider_Profile(&task->bc->base)));
    if( areas && texture )
        task_dat2_worldmap_attach_compositetextures(areas, texture);

    RSCache_Dat2DiskArchiveFree(texture);
    RSCache_Dat2DiskArchiveFree(task->composite);
    task->composite = NULL;
    RSCache_Dat2DiskArchiveFree(task->details);
    task->details = NULL;

    if( !areas )
    {
        fprintf(stderr, "worldmap: no areas decoded\n");
        PT_EXIT(&task->pt);
    }

    CacheProvider_WorldMapSet(&task->bc->base, areas);

    PT_END(&task->pt);
}

static void
Task_Dat2WorldMapLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2WorldMapLoad* task = (struct Task_Dat2WorldMapLoad*)task_base;

    /* Only set when the task is killed between archive loads. */
    RSCache_Dat2DiskArchiveFree(task->details);
    RSCache_Dat2DiskArchiveFree(task->composite);
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2WorldMapLoad_VTable = {
    .run = Task_Dat2WorldMapLoad_Run,
    .free = Task_Dat2WorldMapLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2WorldMapLoad(struct CacheProvider* provider)
{
    struct Task_Dat2WorldMapLoad* task;

    assert(provider);

    if( CacheProvider_WorldMapGet(provider) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2WorldMapLoad_VTable;
    strcpy(task->task.name, "Dat2WorldMapLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->composite_archive_id = -1;
    task->texture_archive_id = -1;
    PT_INIT(&task->pt);
    return &task->task;
}
