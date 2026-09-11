#include "engine/dat2/dat2_tasks.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_group_await.h"
#include "engine/torirs_sprite_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * Resolve one graphic-defaults sprite the way the client does: out of the
 * defaults table, by slot.
 *
 * The other path to these eleven sprites is Task_Dat2SpriteLoadByName, which
 * hashes a name and walks the sprites reference table looking for a matching
 * identifier. That works, and it is what every profile did before this task
 * existed, but it is not what the client does and it is not equivalent:
 *
 *   - The client never looks a sprite up by name. Index 17 group 3 stores
 *     eleven ids positionally, `class11.method235` reads them into fields, and
 *     `Statics.java:37160`-`:37299` loads each one by id. Nothing in the record
 *     is a string, and index 17's reference table does not even carry names.
 *   - The name walk depends on index 8 still shipping name hashes. It does at
 *     rev239, so both paths land on the same ids there — compass is 169 either
 *     way. A cache built without that name table would break the name walk and
 *     leave this one working.
 *   - They disagree the moment a revision repoints a slot. The record is the
 *     authority on *which sprite is the compass*; the name is only the sprite's
 *     own label. Reading the record is the difference between asking what the
 *     client draws and asking what something happens to be called.
 *
 * So a profile that says `table=defaults` gets the client's answer, and one
 * that says `table=sprites` keeps the name walk. Both stay supported: dat1 has
 * no defaults table at all, and neither does every dat2 cache.
 *
 * The slot is bound to the profile's section name once resolved, so every later
 * lookup — including the host's static slots — can keep using the one spelling
 * C knows, exactly as the name path does.
 */

#define DAT2_DEFAULTS_NAME_MAX 63

struct Task_Dat2DefaultsSpriteLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int slot;
    /* The await assigns this across a yield, so it cannot be a local. */
    struct Dat2Group const* group;
    /* Resolved before the sprite read, because the decode's locals cannot
     * survive that yield. */
    int sprite_id;
    char name[DAT2_DEFAULTS_NAME_MAX + 1];
};

static int
Task_Dat2DefaultsSpriteLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2DefaultsSpriteLoad* task = (struct Task_Dat2DefaultsSpriteLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_Sprite* sprite = NULL;
    struct RSCache_Dat2Defaults defaults;
    int pos;

    PT_BEGIN(&task->pt);

    DAT2_GROUP_AWAIT(
        &task->pt, io, 0, task->bc->group_cache, RSCACHE_DAT2_TABLE_DEFAULTS,
        RSCACHE_DAT2_DEFAULTS_GROUP_RECORD, task->group);

    if( !task->group )
    {
        /* No defaults table. Legitimate for a cache that predates it, and the
         * profile simply leaves the slot unbound — same outcome as an
         * era-absent archive on the name path. */
        PT_EXIT(&task->pt);
    }

    pos = Dat2Group_IndexOf(task->group, RSCACHE_DAT2_DEFAULTS_RECORD_FILE);
    if( pos < 0 )
    {
        TORIRS_ERR("defaults: group %d has no file %d\n", RSCACHE_DAT2_DEFAULTS_GROUP_RECORD,
                   RSCACHE_DAT2_DEFAULTS_RECORD_FILE);
        PT_EXIT(&task->pt);
    }

    if( !RSCache_Dat2DefaultsDecode(
            (const uint8_t*)task->group->filelist->files[pos],
            task->group->filelist->file_sizes[pos], &defaults) )
    {
        /* A record this decoder does not recognise — the RS2 branch's schema,
         * most likely. Declining beats binding a slot to a misread id. */
        TORIRS_ERR("defaults: group %d file %d is not a record this build reads\n",
                   RSCACHE_DAT2_DEFAULTS_GROUP_RECORD, RSCACHE_DAT2_DEFAULTS_RECORD_FILE);
        PT_EXIT(&task->pt);
    }

    task->sprite_id = defaults.sprite_ids[task->slot];
    if( task->sprite_id < 0 )
    {
        /* -1 is the record's own "unset", and a real state: class11's
         * constructor fills every slot with it and only opcode 2 or 6 overwrites
         * them. A revision that ships fewer than eleven leaves the rest here. */
        PT_EXIT(&task->pt);
    }

    CacheProvider_SpriteNameMapPut(&task->bc->base, task->name, task->sprite_id);

    if( !CacheProvider_SpriteHas(&task->bc->base, task->sprite_id) )
    {
        RSCache_IO_Dat2SpriteLoad(io, 0, task->sprite_id);
        PT_YIELD(&task->pt);

        archive = RSCache_IO_Dat2SpriteDecode(io, 0);
        if( !archive )
        {
            TORIRS_ERR("defaults: failed to decode sprite archive %d for slot %d\n",
                       task->sprite_id, task->slot);
            PT_EXIT(&task->pt);
        }

        sprite = ToriRS_SpriteFromDat2Archive(
            archive, task->sprite_id, CacheProvider_Profile(&task->bc->base));
        if( !sprite )
        {
            TORIRS_ERR("defaults: failed to convert sprite %d for slot %d\n", task->sprite_id,
                       task->slot);
            PT_EXIT(&task->pt);
        }

        CacheProvider_SpriteAdd(&task->bc->base, task->sprite_id, sprite);
    }

    PT_END(&task->pt);
}

static void
Task_Dat2DefaultsSpriteLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2DefaultsSpriteLoad_VTable = {
    .run = Task_Dat2DefaultsSpriteLoad_Run,
    .free = Task_Dat2DefaultsSpriteLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2DefaultsSpriteLoad(
    struct CacheProvider* provider,
    int slot,
    char const* name)
{
    struct Task_Dat2DefaultsSpriteLoad* task;
    int existing_id;

    assert(provider);
    assert(name);
    assert(name[0] != '\0');
    assert(slot >= 0);
    assert(slot < RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT);

    existing_id = CacheProvider_SpriteIdByName(provider, name);
    if( existing_id >= 0 && CacheProvider_SpriteHas(provider, existing_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2DefaultsSpriteLoad_VTable;
    strcpy(task->task.name, "Dat2DefaultsSpriteLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->slot = slot;
    task->sprite_id = -1;
    strncpy(task->name, name, DAT2_DEFAULTS_NAME_MAX);
    task->name[DAT2_DEFAULTS_NAME_MAX] = '\0';
    PT_INIT(&task->pt);
    return &task->task;
}
