#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_group_await.h"
#include "engine/torirs_location_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

struct Task_Dat2LocLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int loc_id;
    struct RSCache_RecordAddress addr;
    /* Borrowed from the buildcache's split-group LRU — never freed here. */
    struct Dat2Group const* group;
    int group_table;
    int group_id;
};

static int
Task_Dat2LocLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2LocLoad* task = (struct Task_Dat2LocLoad*)task_base;
    struct RSCache_Dat2ConfigLoc* rscache_loc = NULL;
    struct ToriRS_Location* torirs_loc = NULL;
    int pos;

    PT_BEGIN(&task->pt);

    /*
     * Where the loc records live is an era question, so the profile answers it rather than
     * this task assuming the OSRS layout.
     *
     * OSRS keeps every loc as a file in config group 6, so one load covers all of them.
     * RS2 (643) promotes locs to table 16 and shards them 256 per group, so a load covers
     * only the requested id's group.
     */
    task->addr = RSCache_RecordAddressFor(
        CacheProvider_Profile(&task->bc->base), RSCACHE_TYPE_LOC);

    /*
     * Only this loc's record is decoded, not the group's.
     *
     * This used to decode every record in the group and keep them all in
     * bc->loc_hmap, because re-reading the group per id meant re-running bzip2
     * on it. It no longer does: the archive is cached decompressed by the IO
     * layer and split by the group LRU, so reaching one record is an array
     * index. Decoding all of them cost 34 MB resident on an osrs230 boot —
     * 56,376 records — to serve the few hundred a scene actually names, and
     * every one of them was dead the moment ToriRS_LocationFromRSCacheDat2
     * (which deep-copies) had run.
     */
    task->group_table = task->addr.group_shift ? task->addr.table
                                               : RSCACHE_DAT2_TABLE_CONFIGS;
    task->group_id = task->addr.group_shift
                         ? (task->loc_id >> task->addr.group_shift)
                         : RSCACHE_DAT2_CONFIG_KIND_LOCS;
    DAT2_GROUP_AWAIT(
        &task->pt, io, 0, task->bc->group_cache,
        task->group_table, task->group_id, task->group);

    if( !task->group )
    {
        TORIRS_ERR("Failed to decode dat2 loc group for loc %d\n", task->loc_id);
        PT_EXIT(&task->pt);
    }

    /* Sharded groups number their files 0..255 within the group, so the id to
     * look up is group-relative; the OSRS layout's file id is the loc id. */
    pos = Dat2Group_IndexOf(
        task->group,
        task->addr.group_shift ? (task->loc_id & task->addr.file_mask)
                               : task->loc_id);
    if( getenv("TORIRS_LOC_MODEL_DEBUG") )
        TORIRS_LOG("locgroup: loc %d -> table %d group %d files %d -> pos %d\n",
            task->loc_id, task->group_table, task->group_id,
            task->group->file_count, pos);
    if( pos >= 0 )
        rscache_loc = RSCache_Dat2ConfigLocNewDecodeProfile(
            CacheProvider_Profile(&task->bc->base),
            task->group->filelist->files[pos],
            task->group->filelist->file_sizes[pos]);
    if( !rscache_loc )
    {
        TORIRS_ERR("Failed to load dat2 loc %d\n", task->loc_id);
        PT_EXIT(&task->pt);
    }
    rscache_loc->_id = task->loc_id;

    torirs_loc = ToriRS_LocationFromRSCacheDat2(task->loc_id, rscache_loc);
    /* The adaptor deep-copies (torirs_location_from_rscache.c dups every array
     * and string), so the source record has no reader after this point. */
    RSCache_Dat2ConfigLocFree(rscache_loc);
    if( !torirs_loc )
    {
        TORIRS_ERR("Failed to convert dat2 loc %d\n", task->loc_id);
        PT_EXIT(&task->pt);
    }

    /*
     * TORIRS_LOC_CFG=<loc_id>: the decoded shape -> model table for one loc.
     *
     * The build picks a model by scanning `shapes[]` for the shape it is
     * placing and taking the models at that index. A misaligned decode there
     * hands back a DIFFERENT OBJECT at the correct position — which on screen
     * reads as "the right loc drawn in the wrong place", not as a decode fault,
     * and no amount of checking the placement arithmetic will show it. Dumping
     * the table is the only way to tell the two apart.
     */
    if( getenv("TORIRS_LOC_CFG") &&
        (int)strtol(getenv("TORIRS_LOC_CFG"), NULL, 0) == task->loc_id )
    {
        TORIRS_LOG("loc_cfg %d: name='%s' size=%dx%d seq=%d shapes=%s groups=%d\n",
            task->loc_id,
            torirs_loc->name ? torirs_loc->name : "",
            torirs_loc->size_x,
            torirs_loc->size_z,
            torirs_loc->seq_id,
            torirs_loc->shapes ? "yes" : "no(implicit shape 10)",
            torirs_loc->shapes_and_model_count);
        TORIRS_LOG("  transforms: count=%d varbit=%d varp=%d ->",
            torirs_loc->transform_count,
            torirs_loc->transform_varbit,
            torirs_loc->transform_varp);
        for( int i = 0; i < torirs_loc->transform_count; i++ )
            TORIRS_LOG(" %d", torirs_loc->transforms[i]);
        TORIRS_LOG("\n");
        for( int i = 0; i < torirs_loc->shapes_and_model_count; i++ )
        {
            TORIRS_LOG("  group %d: shape=%d models(%d):",
                i,
                torirs_loc->shapes ? torirs_loc->shapes[i] : -1,
                torirs_loc->lengths ? torirs_loc->lengths[i] : 0);
            for( int j = 0; torirs_loc->lengths && j < torirs_loc->lengths[i]; j++ )
                TORIRS_LOG(" %d", torirs_loc->models[i][j]);
            TORIRS_LOG("\n");
        }
    }

    CacheProvider_LocationAdd(&task->bc->base, task->loc_id, torirs_loc);

    PT_END(&task->pt);
}

static void
Task_Dat2LocLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2LocLoad_VTable = {
    .run = Task_Dat2LocLoad_Run,
    .free = Task_Dat2LocLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2LocLoad(
    struct CacheProvider* provider,
    int loc_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2LocLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_LocationHas(provider, loc_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2LocLoad_VTable;
    strcpy(task->task.name, "Dat2LocLoad");
    task->bc = dat2_buildcache;
    task->loc_id = loc_id;
    PT_INIT(&task->pt);
    return &task->task;
}
