#include "engine/dat2/dat2_tasks.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_hitsplat.h"
#include "world/entity_pathing.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * Load every hitsplat type's sprite id, once, at boot.
 *
 * Same shape and the same reasons as Task_Dat2VarbitLoad: whole-group and
 * eager rather than per-id and lazy, because the consumer reads it inside the
 * per-frame overlay build, where there is nowhere to yield to a load.
 *
 * The group is small — 78 records in cache.osrs230, a few hundred bytes each —
 * so this is a single group read, not a walk. Unlike varbits, hitsplats have no
 * sharded RS2 form to handle: the type predates that layout entirely, and a
 * cache without the group simply keeps drawing bare numbers.
 */

struct Task_Dat2HitsplatLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct RS_Hitsplats* hitsplats;
    /* The sprite-preload walk. Both the cursor and the table have to outlive a
     * PT_YIELD, so neither can be a local. */
    int preload_index;
};

static int
Task_Dat2HitsplatLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2HitsplatLoad* task = (struct Task_Dat2HitsplatLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    int* sprite_ids = NULL;
    int* durations = NULL;
    int* slot_policies = NULL;
    struct RS_HitsplatVariants* variants = NULL;
    int count = 0;
    int decoded = 0;
    int selectors = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_HITSPLAT);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_HITSPLAT);
    if( !archive )
    {
        /* Pre-OldSchool caches keep hitmarks in a named sprite archive instead,
         * which the static-sprite path already handles. Absent is normal. */
        PT_EXIT(&task->pt);
    }

    filelist =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        TORIRS_ERR("hitsplat: failed to split the config group\n");
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

    sprite_ids = malloc((size_t)count * sizeof(*sprite_ids));
    durations = malloc((size_t)count * sizeof(*durations));
    slot_policies = malloc((size_t)count * sizeof(*slot_policies));
    /* calloc, not malloc: a type with no opcode 17/18 must read back as
     * `count == 0` and `ids == NULL`, which is how `ResolveType` tells an
     * ordinary appearance from a selector. */
    variants = calloc((size_t)count, sizeof(*variants));
    assert(sprite_ids);
    assert(durations);
    assert(slot_policies);
    assert(variants);
    /* -1 is "no sprite", and it is a real state rather than a hole: a quarter
     * of cache.osrs230's records genuinely carry no opcode 5. */
    for( int i = 0; i < count; i++ )
    {
        sprite_ids[i] = -1;
        /* The reference's own pre-loop defaults, so a type with no opcode 9 or
         * 12 behaves exactly as it did when both were hardcoded in the client. */
        durations[i] = WORLD_HITMARK_DEFAULT_DURATION;
        slot_policies[i] = WORLD_HITMARK_POLICY_DISCARD;
    }

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigHitsplat entry;

        if( id < 0 || id >= count || filelist->file_sizes[i] <= 0 )
            continue;
        memset(&entry, 0, sizeof(entry));
        /* Group 32 holds hitsplat records only in the OldSchool lineage — the
         * same reason `task_dat2_worldmap_load.c` asks before reading table 19.
         * Off-lineage the flags come back empty and the decoder claims nothing,
         * so the short-decode warning below fires instead of the loader
         * inventing sprite ids out of another type's bytes. */
        RSCache_Dat2ConfigHitsplatDecodeInplace(
            &entry,
            filelist->files[i],
            filelist->file_sizes[i],
            (unsigned)RSCache_Dat2ConfigHitsplatFlags(
                CacheProvider_Profile(&task->bc->base)));
        if( entry._consumed != filelist->file_sizes[i] )
            TORIRS_LOG("hitsplat %d: decode consumed %d of %d bytes\n", id,
                    entry._consumed, filelist->file_sizes[i]);
        sprite_ids[id] = entry.sprite_id;
        durations[id] = entry.duration;
        slot_policies[id] = entry.slot_policy;
        /*
         * The selector, laid out as the reference lays it out: the stream's
         * `variant_count` ids followed by the opcode-18 fallback, so the array
         * IS the `count + 2` one `GetMultiHitmark` indexes and the "last entry
         * is the fallback" rule is the array's length rather than a second rule.
         *
         * Opcode 17 reads no fallback; the decoder leaves `variant_fallback` at
         * -1 and appending that is exactly what the reference does, so a var
         * value out of range on an opcode-17 record draws nothing. cache.osrs239
         * carries no opcode-17 record, so this path is reached only by a cache
         * that does.
         */
        if( entry.variant_count > 0 )
        {
            int const n = entry.variant_count + 1;
            int* ids = malloc((size_t)n * sizeof(*ids));

            assert(ids);
            for( int v = 0; v < entry.variant_count; v++ )
                ids[v] = entry.variants[v];
            ids[n - 1] = entry.variant_fallback;
            variants[id].varbit = entry.variant_varbit;
            variants[id].varp = entry.variant_varp;
            variants[id].ids = ids;
            variants[id].count = n;
            selectors++;
        }
        decoded++;
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    if( !RS_Hitsplats_SetTypes(task->hitsplats, sprite_ids, durations, slot_policies, variants,
                               count) )
    {
        for( int i = 0; i < count; i++ )
            free(variants[i].ids);
        free(variants);
        free(sprite_ids);
        free(durations);
        free(slot_policies);
        PT_EXIT(&task->pt);
    }
    /*
     * `selectors` is worth printing rather than counting silently. A cache whose
     * hitsplat table has been round-tripped through a stale text export loses
     * every one of them — which is not a crash, not a warning, and not visible
     * on screen: settings 5 and 279 simply stop having anything to switch. It
     * was 0 here for as long as the baked cache carried that export.
     */
    TORIRS_LOG("hitsplat load: %d types (%d records, %d var selectors)\n", count, decoded, selectors);

    /*
     * Bring the sprites themselves into residence.
     *
     * `UITreeSceneBridge_EnsureSprite` binds a *resident* sprite into the
     * scene; it does not load one, because it is called from the per-frame
     * overlay build where there is nowhere to yield to. So knowing the ids is
     * not enough — something has to have asked for them, and nothing else ever
     * will. This is the same shape as the texture-wants registry: an
     * event-driven loader only ever has what somebody requested.
     *
     * Fifty-odd small sprites at boot, once. Loading them lazily on the first
     * hit would mean the first hit of a session draws no splat, which is
     * precisely the bug this is fixing.
     */
    for( task->preload_index = 0; task->preload_index < task->hitsplats->count;
         task->preload_index++ )
    {
        if( task->hitsplats->sprite_ids[task->preload_index] < 0 )
            continue;
        /* The _IF form skips a NULL child, which is what CreateTask_SpriteLoad
         * returns for a sprite already resident — and it clears its own child
         * pointer afterwards, which is what makes it safe inside a loop. */
        TASK_AWAITEX_IF(
            &task->task,
            &task->pt,
            io,
            CreateTask_SpriteLoad(&task->bc->base,
                                  task->hitsplats->sprite_ids[task->preload_index]));
    }

    PT_END(&task->pt);
}

static void
Task_Dat2HitsplatLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2HitsplatLoad_VTable = {
    .run = Task_Dat2HitsplatLoad_Run,
    .free = Task_Dat2HitsplatLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2HitsplatLoad(
    struct CacheProvider* provider,
    struct RS_Hitsplats* hitsplats)
{
    assert(provider);
    assert(hitsplats);

    struct Task_Dat2HitsplatLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2HitsplatLoad_VTable;
    strcpy(task->task.name, "Dat2HitsplatLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->hitsplats = hitsplats;
    PT_INIT(&task->pt);
    return &task->task;
}
