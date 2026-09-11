#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/title_panel.h"
#include "engine/torirs_types.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * The modern lane's title backdrop.
 *
 * Same picture and same assembly as the old lane -- half a screen, mirrored --
 * but a different address: OldSchool keeps it in the BINARY table under the
 * name `title.jpg` rather than as a member of a jagfile. That difference is
 * the entire reason this task exists beside the dat1 one; the composite itself
 * is shared (engine/title_panel.h), because the reference builds it the same
 * way in both eras.
 */

struct Task_Dat2TitlePanelLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    /** Binary-table archive name, from revconfig `archive=`. */
    char archive_name[64];
    /** Provider name the composited panel is registered under. */
    char sprite_name[64];
    int archive_id;
};

static int
resolve_by_name(
    struct RSCache_ReferenceTable* table,
    char const* name)
{
    char buf[64];
    int name_hash;

    assert(table);
    assert(name);
    snprintf(buf, sizeof(buf), "%s", name);
    name_hash = RSCache_ArchiveNameHashDat2(buf);
    for( int i = 0; i < table->archive_count; i++ )
    {
        if( RSCache_ReferenceTableIdentifier(table, i) == name_hash )
            return i;
    }
    return -1;
}

static int
Task_Dat2TitlePanelLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2TitlePanelLoad* task = (struct Task_Dat2TitlePanelLoad*)task_base;
    struct RSCache_ReferenceTable* table = NULL;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_Sprite* sprite = NULL;
    int sprite_id;

    PT_BEGIN(&task->pt);

    if( !dat2_buildcache_reference_table_has(task->bc, RSCACHE_DAT2_TABLE_BINARY) )
    {
        RSCache_IO_Dat2ReferenceTableLoad(io, 0, RSCACHE_DAT2_TABLE_BINARY);
        PT_YIELD(&task->pt);

        table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
        if( !table )
        {
            TORIRS_ERR("title panel: no binary reference table in this cache\n");
            PT_EXIT(&task->pt);
        }
        dat2_buildcache_reference_table_add(task->bc, RSCACHE_DAT2_TABLE_BINARY, table);
    }

    table = dat2_buildcache_reference_table_get(task->bc, RSCACHE_DAT2_TABLE_BINARY);
    assert(table);

    task->archive_id = resolve_by_name(table, task->archive_name);
    if( task->archive_id < 0 )
    {
        TORIRS_ERR("title panel: binary table has no '%s'\n", task->archive_name);
        PT_EXIT(&task->pt);
    }

    RSCache_IO_Dat2RecordGroupLoad(io, 0, RSCACHE_DAT2_TABLE_BINARY, task->archive_id);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2RecordGroupDecode(io, 0, RSCACHE_DAT2_TABLE_BINARY);
    if( !archive )
    {
        TORIRS_ERR("title panel: '%s' did not load\n", task->archive_name);
        PT_EXIT(&task->pt);
    }

    sprite = ToriRS_TitlePanelFromJpeg(archive->data, archive->data_size);
    RSCache_Dat2DiskArchiveFree(archive);
    if( !sprite )
    {
        TORIRS_ERR("title panel: '%s' did not decode as a JPEG\n", task->archive_name);
        PT_EXIT(&task->pt);
    }

    strncpy(sprite->name, task->sprite_name, sizeof(sprite->name) - 1);
    sprite_id = CACHE_PROVIDER_SPRITE_TITLE_PANEL;
    CacheProvider_SpriteAdd(&task->bc->base, sprite_id, sprite);
    CacheProvider_SpriteNameMapPut(&task->bc->base, task->sprite_name, sprite_id);

    PT_END(&task->pt);
}

static void
Task_Dat2TitlePanelLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2TitlePanelLoad_VTable = {
    .run = Task_Dat2TitlePanelLoad_Run,
    .free = Task_Dat2TitlePanelLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2TitlePanelLoad(
    struct CacheProvider* provider,
    char const* archive_name,
    char const* sprite_name)
{
    struct Task_Dat2TitlePanelLoad* task;

    assert(provider);
    assert(archive_name && archive_name[0]);
    assert(sprite_name && sprite_name[0]);

    if( CacheProvider_SpriteIdByName(provider, sprite_name) >= 0 )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2TitlePanelLoad_VTable;
    strcpy(task->task.name, "Dat2TitlePanelLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    strncpy(task->archive_name, archive_name, sizeof(task->archive_name) - 1);
    strncpy(task->sprite_name, sprite_name, sizeof(task->sprite_name) - 1);
    task->archive_id = -1;
    PT_INIT(&task->pt);
    return &task->task;
}
