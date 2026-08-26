#include "engine/dat2/dat2_tasks.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_healthbar.h"

#include "asyncio.h"
#include "cache/rscache_io.h"
#include "datatypes/dat2_config_healthbar.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * Load every healthbar type, once, at boot.
 *
 * Same shape and the same reasons as Task_Dat2HitsplatLoad: whole-group and
 * eager, because the consumer reads it inside the per-frame overlay build,
 * where there is nowhere to yield to a load. Eighty-five records of a few dozen
 * bytes each, so this is one group read.
 *
 * The sprite preload at the end matters more here than it does for hitsplats.
 * `UITreeSceneBridge_EnsureSprite` binds a *resident* sprite; a bar whose pair
 * is not resident cannot report its own pixel width, so it silently falls back
 * to the rectangle path at the declared width -- which is the bug this table
 * exists to fix, reappearing as a first-hit-of-the-session flicker.
 */

struct Task_Dat2HealthbarLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct RS_Healthbars* healthbars;
    /* Both the cursor and the half-of-the-pair selector outlive a PT_YIELD. */
    int preload_index;
    int preload_half;
};

static int
Task_Dat2HealthbarLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2HealthbarLoad* task = (struct Task_Dat2HealthbarLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct RS_HealthbarType* types = NULL;
    int count = 0;
    int decoded = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_HEALTHBAR);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_HEALTHBAR);
    if( !archive )
    {
        /* dat1 has no healthbar type at all, and the defaults reproduce the
         * client's own constructor, so an absent group is not an error. */
        PT_EXIT(&task->pt);
    }

    filelist =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        TORIRS_ERR("healthbar: failed to split the config group\n");
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    /* Ids are sparse, so size from the largest rather than from the count. */
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( archive->file_ids[i] + 1 > count )
            count = archive->file_ids[i] + 1;
    }
    if( count <= 0 )
    {
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    types = malloc((size_t)count * sizeof(*types));
    assert(types);
    for( int i = 0; i < count; i++ )
        RS_Healthbars_Defaults(&types[i]);

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigHealthbar entry;

        if( id < 0 || id >= count || filelist->file_sizes[i] <= 0 )
            continue;
        memset(&entry, 0, sizeof(entry));
        RSCache_Dat2ConfigHealthbarDecodeInplace(
            &entry, filelist->files[i], filelist->file_sizes[i]);
        if( entry._consumed != filelist->file_sizes[i] )
            TORIRS_LOG("healthbar %d: decode consumed %d of %d bytes\n", id,
                    entry._consumed, filelist->file_sizes[i]);
        types[id].front_sprite = entry.front_sprite_id;
        types[id].back_sprite = entry.back_sprite_id;
        /* Only an opcode that was actually present overrides a default: the
         * decoder leaves an absent field at 0, and 0 is a legal value for both
         * of these -- opcode 11 carries it explicitly on several records. */
        if( entry.has_width )
            types[id].width = entry.width;
        if( entry.has_persist_cycles )
            types[id].persist_cycles = entry.persist_cycles;
        if( entry.has_fade_threshold )
            types[id].fade_threshold = entry.fade_threshold;
        decoded++;
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    if( !RS_Healthbars_SetTypes(task->healthbars, types, count) )
    {
        free(types);
        PT_EXIT(&task->pt);
    }
    TORIRS_LOG("healthbar load: %d types (%d records)\n", count, decoded);

    /* Both halves of every pair, for the reason in the file comment. Roughly
     * 170 small sprites at boot, once. */
    for( task->preload_index = 0; task->preload_index < task->healthbars->count;
         task->preload_index++ )
    {
        for( task->preload_half = 0; task->preload_half < 2; task->preload_half++ )
        {
            int sprite = task->preload_half == 0
                             ? task->healthbars->types[task->preload_index].front_sprite
                             : task->healthbars->types[task->preload_index].back_sprite;
            if( sprite < 0 )
                continue;
            /* The _IF form skips a NULL child, which is what CreateTask_SpriteLoad
             * returns for a sprite already resident — and it clears its own child
             * pointer afterwards, which is what makes it safe inside a loop. */
            TASK_AWAITEX_IF(
                &task->pt, io, CreateTask_SpriteLoad(&task->bc->base, sprite));
        }
    }

    PT_END(&task->pt);
}

static void
Task_Dat2HealthbarLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2HealthbarLoad_VTable = {
    .run = Task_Dat2HealthbarLoad_Run,
    .free = Task_Dat2HealthbarLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2HealthbarLoad(
    struct CacheProvider* provider,
    struct RS_Healthbars* healthbars)
{
    assert(provider);
    assert(healthbars);

    struct Task_Dat2HealthbarLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2HealthbarLoad_VTable;
    strcpy(task->task.name, "Dat2HealthbarLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->healthbars = healthbars;
    PT_INIT(&task->pt);
    return &task->task;
}
