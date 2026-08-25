#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_npctype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"
#include "engine/dat2/dat2_group_await.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2NpcLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int npc_id;
    int bas_type_id;
    struct RSCache_RecordAddress addr;

    /* Split config group holding this npc. Owned by the buildcache's LRU,
     * borrowed here for the length of one decode — never freed by us, and
     * not read again after the BasType yield, which may evict it. */
    struct Dat2Group const* group;
    int group_table;
    int group_id;

    /* Ours until the task is freed. The decode is a means to the ToriRS
     * convert, which deep-copies every array and string it keeps, so nothing
     * outlives us by pointing into this. */
    struct RSCache_Dat2ConfigNpc* npc;
};

static void
npc_apply_bas(
    struct RSCache_Dat2ConfigNpc* npc,
    const struct RSCache_Dat2ConfigBas* bas)
{
    assert(npc && bas);
    /* Prefer BasType when present — rs-map-viewer NpcType.getIdleSeqId. */
    if( bas->idle_seq_id >= 0 )
        npc->standing_animation = bas->idle_seq_id;
    if( bas->walk_seq_id >= 0 )
        npc->walking_animation = bas->walk_seq_id;
    if( bas->walk_back_seq_id >= 0 )
        npc->rotate180_animation = bas->walk_back_seq_id;
    if( bas->walk_left_seq_id >= 0 )
        npc->rotate_left_animation = bas->walk_left_seq_id;
    if( bas->walk_right_seq_id >= 0 )
        npc->rotate_right_animation = bas->walk_right_seq_id;
}

static int
Task_Dat2NpcLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2NpcLoad* task = (struct Task_Dat2NpcLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_Npctype* torirs_npc = NULL;
    struct RSCache_Dat2ConfigBas* bas = NULL;

    PT_BEGIN(&task->pt);

    /*
     * Where the npc records live is an era question, so the profile answers it rather than
     * this task assuming the OSRS layout.
     *
     * OSRS keeps every npc as a file in config group 9, so one load covers all of them.
     * RS2 (643) promotes npcs to table 18 and shards them 128 per group, so a load covers
     * only the requested id's group — which is why the whole-group memo below is keyed on
     * having *this* id rather than on having loaded anything at all.
     */
    task->addr = RSCache_RecordAddressFor(
        CacheProvider_Profile(&task->bc->base), RSCACHE_TYPE_NPC);

    /*
     * Both layouts are a (table, group) pair, which is the LRU's key: OSRS
     * keeps every npc in config group 9, RS2 shards them 128 per group into
     * table 18.
     */
    task->group_table = task->addr.group_shift ? task->addr.table
                                               : RSCACHE_DAT2_TABLE_CONFIGS;
    task->group_id = task->addr.group_shift
                         ? (task->npc_id >> task->addr.group_shift)
                         : RSCACHE_DAT2_CONFIG_KIND_NPC;

    /*
     * One record, not the group.
     *
     * This used to decode every npc in the cache on the first npc touched and
     * keep them all in a side map, because splitting a group per lookup cost a
     * malloc per member per lookup and nothing could afford that per id. The
     * split-group LRU removed that reason (dat2_group_cache.h) and the other
     * eleven record loaders moved across; this one did not, so a boot that saw
     * one npc paid for all ~15,000 of them — 5 MB of decoded records that
     * nothing ever reads again, since ToriRS_NpctypeFromRSCacheDat2 copies out
     * everything it keeps and a live region touches a couple hundred distinct
     * types at most.
     *
     * The memo goes with it: CreateTask_Dat2NpcLoad already refuses to queue an
     * id whose *converted* npctype is resident, so the map was only ever read
     * by the task that had just filled it.
     */
    DAT2_GROUP_AWAIT(
        &task->pt,
        io,
        0,
        task->bc->group_cache,
        task->group_table,
        task->group_id,
        task->group);

    if( !task->group )
    {
        fprintf(stderr, "Failed to decode dat2 npc group for npc %d\n", task->npc_id);
        PT_EXIT(&task->pt);
    }

    {
        /* Sharded groups number their members 0..mask within the group, so what
         * is wanted is the id's low bits, not the id. */
        int want_file = task->addr.group_shift
                            ? (task->npc_id & task->addr.file_mask)
                            : task->npc_id;
        int pos = Dat2Group_IndexOf(task->group, want_file);
        if( pos >= 0 && task->group->filelist->files[pos] )
            task->npc = RSCache_Dat2ConfigNpcNewDecodeProfile(
                CacheProvider_Profile(&task->bc->base),
                task->group->filelist->files[pos],
                task->group->filelist->file_sizes[pos]);
    }

    if( !task->npc )
    {
        fprintf(stderr, "Failed to load dat2 npc %d\n", task->npc_id);
        PT_EXIT(&task->pt);
    }

    /* Survives PT_YIELD — stack locals do not. */
    task->bas_type_id = task->npc->bas_type_id;

    /* RS2: resolve BasType idle/walk into the npc anim fields before convert. */
    if( task->bas_type_id >= 0 && !dat2_buildcache_bas_get(task->bc, task->bas_type_id) )
    {
        RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_BAS);
        PT_YIELD(&task->pt);
        archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_BAS);
        if( !archive )
        {
            fprintf(
                stderr,
                "Failed to decode dat2 bas group for npc %d bas %d\n",
                task->npc_id,
                task->bas_type_id);
        }
        else
        {
            dat2_buildcache_bas_init_from_archive(task->bc, archive);
            RSCache_Dat2DiskArchiveFree(archive);
            archive = NULL;
        }
    }

    if( task->bas_type_id >= 0 )
    {
        bas = dat2_buildcache_bas_get(task->bc, task->bas_type_id);
        if( bas )
            npc_apply_bas(task->npc, bas);
        else
            fprintf(
                stderr,
                "Failed to load bas %d for npc %d\n",
                task->bas_type_id,
                task->npc_id);
    }

    torirs_npc = ToriRS_NpctypeFromRSCacheDat2(task->npc_id, task->npc);
    if( !torirs_npc )
    {
        fprintf(stderr, "Failed to convert dat2 npc %d\n", task->npc_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_NpctypeAdd(&task->bc->base, task->npc_id, torirs_npc);

    PT_END(&task->pt);
}

static void
Task_Dat2NpcLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2NpcLoad* task = (struct Task_Dat2NpcLoad*)task_base;

    /* Reached on every exit path, PT_EXIT included, so this is the only place
     * the record needs releasing. */
    RSCache_Dat2ConfigNpcFree(task->npc);
    free(task);
}

static struct ToriRS_TaskVTable Task_Dat2NpcLoad_VTable = {
    .run = Task_Dat2NpcLoad_Run,
    .free = Task_Dat2NpcLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2NpcLoad(
    struct CacheProvider* provider,
    int npc_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2NpcLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_NpctypeHas(provider, npc_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2NpcLoad_VTable;
    strcpy(task->task.name, "Dat2NpcLoad");
    task->bc = dat2_buildcache;
    task->npc_id = npc_id;
    task->bas_type_id = -1;
    PT_INIT(&task->pt);
    return &task->task;
}
