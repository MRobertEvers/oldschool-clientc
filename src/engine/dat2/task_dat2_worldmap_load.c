#include "mem/torirs_mem.h"
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
#include "log/torirs_log.h"

/*
 * The world map lives in three archives of table 19: "details" holds one file
 * per map area, "compositemap" holds that area's map element icons, and
 * "compositetexture" holds the overview-pane PNG (clientCode 1401). Details and
 * compositemap list the same file ids so icons attach by id; compositetexture
 * uses those ids too. Loading is all-or-nothing — the scripts ask "is the world
 * map loaded", not "is area N loaded".
 *
 * The frame contract's split (app.h, THE FRAME CONTRACT): this task is on the
 * provider's asset queue (the parallel queue) and only READS the three
 * archives. Decoding them -- every area, its icons, and the overview PNGs, 8.8
 * to 9.4 ms of processor time in one step against the osrs242 cache, which
 * the compute check named and which aborted the map editor under strict the
 * moment its Square map opened -- is handed, with the archives, to ONE task on
 * the provider's compute queue (the background queue), Dat2WorldMapDecode,
 * joined like any fan-out. It decodes an area, then a PNG, a step, stops
 * between them when its slice is spent, and publishes the map on the provider
 * itself (test-worldmap-load).
 */
struct Task_Dat2WorldMapLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    /* Locals do not survive a protothread yield, so earlier archives wait here
     * while later ones load, until they are handed to the decode. */
    struct RSCache_Dat2DiskArchive* details;
    struct RSCache_Dat2DiskArchive* composite;
    struct RSCache_Dat2DiskArchive* texture;
    int composite_archive_id;
    int texture_archive_id;
    /* The decode job, joined. */
    int decoding;
};

/* The CPU half, on the compute queue. It OWNS the archives it is given and
 * everything it makes from them until the map is on the provider, and writes
 * only there: so it needs nothing of the load task's to be alive. */
struct Task_Dat2WorldMapDecode
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct RSCache_Dat2DiskArchive* details;
    struct RSCache_Dat2DiskArchive* composite;
    struct RSCache_Dat2DiskArchive* texture;
    struct RSCache_FileList* details_files;
    struct RSCache_FileList* composite_files;
    struct RSCache_FileList* texture_files;
    /* The areas decoded so far (entries[0..count)), then the map made of them. */
    struct RSCache_WorldMapArea* entries;
    int count;
    struct ToriRS_WorldMapAreas* areas;
    /* The file being decoded, between steps. */
    int next;
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
        if( RSCache_ReferenceTableIdentifier(table, i) == name_hash )
            return i;
    }
    /* OSRS 238+: details/compositemap/compositetexture may be unnamed; fall
     * back to archive id. */
    if( unnamed_fallback_id >= 0 && unnamed_fallback_id < table->archive_count &&
        RSCache_ReferenceTableHasArchive(table, unnamed_fallback_id) )
        return RSCache_ReferenceTableHasArchive(table, unnamed_fallback_id)
                   ? unnamed_fallback_id
                   : -1;
    return -1;
}

/* One file of "details": the area, and its icons from the "compositemap"
 * file of the same id. A file that does not decode is left out. */
static void
task_dat2_worldmap_decode_area(
    struct Task_Dat2WorldMapDecode* self,
    int index,
    int worldmap_flags)
{
    int file_id = self->details->file_ids[index];
    struct RSCache_WorldMapArea* entry = &self->entries[self->count];

    if( !RSCache_WorldMapAreaDecodeInplace(
            entry, file_id, self->details_files->files[index], self->details_files->file_sizes[index]) )
    {
        TORIRS_ERR("worldmap: failed to decode area %d\n", file_id);
        RSCache_WorldMapAreaFreeInplace(entry);
        memset(entry, 0, sizeof(*entry));
        return;
    }
    for( int j = 0; self->composite_files && j < self->composite_files->file_count; j++ )
    {
        if( self->composite->file_ids[j] != file_id )
            continue;
        RSCache_WorldMapAreaDecodeIconsInplace(
            entry, self->composite_files->files[j], self->composite_files->file_sizes[j], worldmap_flags);
        break;
    }
    self->count++;
}

/* One compositetexture PNG, attached to the area of the same file id. The
 * overview pane scale-blits these; missing textures leave overview_pixels NULL
 * and the emit path falls back to the area background fill alone. */
static void
task_dat2_worldmap_attach_texture(
    struct Task_Dat2WorldMapDecode* self,
    int index)
{
    int file_id = self->texture->file_ids[index];
    struct ToriRS_WorldMapArea* area = ToriRS_WorldMapAreasGet(self->areas, file_id);
    int width = 0;
    int height = 0;
    uint32_t* rgb = NULL;
    uint32_t* argb = NULL;
    int pixel_count;

    if( !area )
        return;
    if( !PngDecode_Rgb(
            ToriMem_Frame(), self->texture_files->files[index], self->texture_files->file_sizes[index], &width,
            &height, &rgb) )
    {
        TORIRS_ERR("worldmap: compositetexture %d: PNG decode failed\n", file_id);
        return;
    }
    assert(width > 0 && height > 0 && rgb);
    pixel_count = width * height;
    /* The area owns it; ToriRS_WorldMapAreasFree releases it from ASSET. */
    argb = ToriMem_Alloc(ToriMem_Assets(), (size_t)pixel_count * sizeof(*argb));
    assert(argb);
    /* PngDecode_Rgb drops alpha to 0; overview sprites need opaque ARGB. */
    for( int p = 0; p < pixel_count; p++ )
        argb[p] = 0xFF000000u | (rgb[p] & 0xFFFFFFu);
    ToriMem_Free(ToriMem_Frame(), rgb);

    ToriMem_Free(ToriMem_Assets(), area->overview_pixels);
    area->overview_pixels = argb;
    area->overview_width = width;
    area->overview_height = height;
}

static int
Task_Dat2WorldMapDecode_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat2WorldMapDecode* self = (struct Task_Dat2WorldMapDecode*)task_base;

    PT_BEGIN(&self->pt);

    self->details_files = RSCache_FileListNewFromDecode(
        self->details->data, self->details->data_size, self->details->file_count);
    if( !self->details_files || self->details_files->file_count <= 0 || !self->details->file_ids )
    {
        TORIRS_ERR("worldmap: failed to split the details archive\n");
        PT_EXIT(&self->pt);
    }
    if( self->composite && self->composite->file_ids )
        self->composite_files = RSCache_FileListNewFromDecode(
            self->composite->data, self->composite->data_size, self->composite->file_count);
    self->entries =
        ToriMem_Calloc(ToriMem_Assets(), (size_t)self->details_files->file_count, sizeof(*self->entries));
    assert(self->entries);
    TASK_YIELD_IF_SPENT(&self->task, &self->pt, io);

    /* An area a step. */
    for( self->next = 0; self->next < self->details_files->file_count; self->next++ )
    {
        task_dat2_worldmap_decode_area(
            self, self->next, RSCache_WorldMapFlags(CacheProvider_Profile(&self->bc->base)));
        TASK_YIELD_IF_SPENT(&self->task, &self->pt, io);
    }

    self->areas = ToriRS_WorldMapAreasFromRSCacheDat2(self->entries, self->count);
    for( int i = 0; i < self->count; i++ )
        RSCache_WorldMapAreaFreeInplace(&self->entries[i]);
    ToriMem_Free(ToriMem_Assets(), self->entries);
    self->entries = NULL;
    self->count = 0;
    if( !self->areas )
    {
        TORIRS_LOG("worldmap: no areas decoded\n");
        PT_EXIT(&self->pt);
    }

    /* A PNG a step. */
    if( self->texture && self->texture->file_ids && self->texture->file_count > 0 )
    {
        self->texture_files = RSCache_FileListNewFromDecode(
            self->texture->data, self->texture->data_size, self->texture->file_count);
        for( self->next = 0; self->texture_files && self->next < self->texture_files->file_count; self->next++ )
        {
            TASK_YIELD_IF_SPENT(&self->task, &self->pt, io);
            task_dat2_worldmap_attach_texture(self, self->next);
        }
    }

    CacheProvider_WorldMapSet(&self->bc->base, self->areas);
    self->areas = NULL;

    PT_END(&self->pt);
}

static void
Task_Dat2WorldMapDecode_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2WorldMapDecode* self = (struct Task_Dat2WorldMapDecode*)task_base;

    /* Whatever a decode that ended early, or a queue freed whole, left. */
    for( int i = 0; self->entries && i < self->count; i++ )
        RSCache_WorldMapAreaFreeInplace(&self->entries[i]);
    ToriMem_Free(ToriMem_Assets(), self->entries);
    ToriRS_WorldMapAreasFree(self->areas);
    RSCache_FileListFree(self->texture_files);
    RSCache_FileListFree(self->composite_files);
    RSCache_FileListFree(self->details_files);
    RSCache_Dat2DiskArchiveFree(self->texture);
    RSCache_Dat2DiskArchiveFree(self->composite);
    RSCache_Dat2DiskArchiveFree(self->details);
    ToriMem_Free(ToriMem_Assets(), task_base);
}

static struct ToriRS_TaskVTable Task_Dat2WorldMapDecode_VTable = {
    .run = Task_Dat2WorldMapDecode_Run,
    .free = Task_Dat2WorldMapDecode_Free,
};

/* The decode, taking the load's archives. */
static struct ToriRS_Task*
create_worldmap_decode(struct Task_Dat2WorldMapLoad* load)
{
    struct Task_Dat2WorldMapDecode* task;

    assert(load);
    assert(load->details);
    task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2WorldMapDecode_VTable;
    strcpy(task->task.name, "Dat2WorldMapDecode");
    task->bc = load->bc;
    task->details = load->details;
    task->composite = load->composite;
    task->texture = load->texture;
    load->details = NULL;
    load->composite = NULL;
    load->texture = NULL;
    PT_INIT(&task->pt);
    return &task->task;
}

static int
Task_Dat2WorldMapLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat2WorldMapLoad* task = (struct Task_Dat2WorldMapLoad*)task_base;
    struct RSCache_ReferenceTable* table = NULL;
    int details_archive_id = -1;

    PT_BEGIN(&task->pt);

    /* Table 19 is the world map only in OldSchool; the RS2 branch keeps objs there. Ask
     * before loading, or a 643 cache renders obj configs as map areas. */
    if( !RSCache_IO_ProfileHasDat2Table(
            CacheProvider_Profile(&task->bc->base), RSCACHE_DAT2_TABLE_WORLDMAP) )
    {
        TORIRS_LOG("worldmap: this cache's branch has no world map table\n");
        PT_EXIT(&task->pt);
    }

    if( !dat2_buildcache_reference_table_has(task->bc, RSCACHE_DAT2_TABLE_WORLDMAP) )
    {
        RSCache_IO_Dat2ReferenceTableLoad(io, 0, RSCACHE_DAT2_TABLE_WORLDMAP);
        PT_YIELD(&task->pt);

        table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
        if( !table )
        {
            TORIRS_LOG("worldmap: no reference table (cache has no world map)\n");
            PT_EXIT(&task->pt);
        }
        dat2_buildcache_reference_table_add(task->bc, RSCACHE_DAT2_TABLE_WORLDMAP, table);
    }

    table = dat2_buildcache_reference_table_get(task->bc, RSCACHE_DAT2_TABLE_WORLDMAP);
    assert(table);

    details_archive_id = task_dat2_worldmap_resolve_archive_id(table, "details", 0);
    if( details_archive_id < 0 )
    {
        TORIRS_LOG("worldmap: no \"details\" archive in the world map table\n");
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
        TORIRS_ERR("worldmap: failed to load the \"details\" archive\n");
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
        task->texture = RSCache_IO_Dat2WorldMapArchiveDecode(io, 0);
    }

    /* The decoding is the compute queue's; this task waits for it as for any
     * fan-out. The map is on the provider when it ends (or it said why not). */
    assert(task->bc->base.compute_queue);
    ToriRS_TaskQueue_AddParallelPoolSubTask(
        task->bc->base.compute_queue, create_worldmap_decode(task), &task->decoding);
    TASK_AWAIT_JOIN(&task->task, &task->pt, task->decoding);

    PT_END(&task->pt);
}

static void
Task_Dat2WorldMapLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2WorldMapLoad* task = (struct Task_Dat2WorldMapLoad*)task_base;

    /* Only set when the task is freed between archive loads. */
    RSCache_Dat2DiskArchiveFree(task->details);
    RSCache_Dat2DiskArchiveFree(task->composite);
    RSCache_Dat2DiskArchiveFree(task->texture);
    ToriMem_Free(ToriMem_Assets(), task_base);
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

    task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2WorldMapLoad_VTable;
    strcpy(task->task.name, "Dat2WorldMapLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->composite_archive_id = -1;
    task->texture_archive_id = -1;
    PT_INIT(&task->pt);
    return &task->task;
}
