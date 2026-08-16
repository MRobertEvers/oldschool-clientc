#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/torirs_sound_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Make one dat1 sound effect resident.
 *
 * The bank behind it (`sounds.dat` in the sound-effects jagfile) is decoded once
 * into the buildcache — it has no index, so there is no such thing as reading
 * one record out of it — and the render stays per id, because rendering all ~700
 * effects up front would cost several megabytes of PCM and a visible hitch on
 * the frame that happened to ask for the first sound.
 */

struct Task_Dat1SoundLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int sound_id;
};

static int
Task_Dat1SoundLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1SoundLoad* task = (struct Task_Dat1SoundLoad*)task_base;
    struct RSCache_SoundBank* bank = NULL;
    struct ToriRS_Sound* sound = NULL;

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_sound_bank(task->bc) )
    {
        struct RSCache_FileListDat* jagfile = NULL;

        RSCache_IO_Dat1JagfileLoad(io, 0, RSCACHE_DAT1_CONFIG_SOUND_EFFECTS);
        PT_YIELD(&task->pt);

        jagfile = RSCache_IO_Dat1JagfileDecode(io, 0, RSCACHE_DAT1_CONFIG_SOUND_EFFECTS);
        if( !jagfile )
        {
            fprintf(stderr, "dat1 sound: sound-effects jagfile did not decode\n");
            PT_EXIT(&task->pt);
        }
        dat1_buildcache_set_sounds_jagfile(task->bc, jagfile);
    }

    bank = dat1_buildcache_get_sound_bank(task->bc);
    if( !bank || task->sound_id < 0 || task->sound_id >= bank->count ||
        !bank->effects[task->sound_id] )
    {
        /* Not an error: servers reference ids a given cache does not carry, and
         * the reference client plays nothing for them. */
        if( getenv("TORIRS_AUDIO_DEBUG") )
            fprintf(stderr, "dat1 sound: no effect %d in this bank\n", task->sound_id);
        PT_EXIT(&task->pt);
    }

    sound = ToriRS_SoundFromRSCache(
        CacheProvider_Profile(&task->bc->base),
        task->sound_id,
        bank->effects[task->sound_id]);
    if( !sound )
    {
        /* An empty program renders to nothing; the reference plays silence. */
        if( getenv("TORIRS_AUDIO_DEBUG") )
            fprintf(stderr, "dat1 sound: effect %d has no audible tone\n", task->sound_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_SoundAdd(&task->bc->base, task->sound_id, sound);
    if( getenv("TORIRS_AUDIO_DEBUG") )
        fprintf(
            stderr,
            "dat1 sound: effect %d rendered, %d samples, delay %d ticks\n",
            task->sound_id,
            sound->sample_count,
            sound->queue_delay);

    PT_END(&task->pt);
}

static void
Task_Dat1SoundLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1SoundLoad_VTable = {
    .run = Task_Dat1SoundLoad_Run,
    .free = Task_Dat1SoundLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat1SoundLoad(
    struct CacheProvider* provider,
    int sound_id)
{
    struct Task_Dat1SoundLoad* task;

    assert(provider);

    if( CacheProvider_SoundHas(provider, sound_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1SoundLoad_VTable;
    strcpy(task->task.name, "Dat1SoundLoad");
    task->bc = (struct Dat1BuildCache*)provider;
    task->sound_id = sound_id;
    PT_INIT(&task->pt);
    return &task->task;
}
