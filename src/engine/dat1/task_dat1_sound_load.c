#include "engine/cache_provider.h"
#include "mem/torirs_mem.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/torirs_sound_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * Make one dat1 sound effect resident.
 *
 * The bank behind it (`sounds.dat` in the sound-effects jagfile) is decoded once
 * into the buildcache — it has no index, so there is no such thing as reading
 * one record out of it — and the render stays per id, because rendering all ~700
 * effects up front would cost several megabytes of PCM and a visible hitch on
 * the frame that happened to ask for the first sound.
 *
 * ## The read here, the render on the background queue, in steps
 *
 * As the dat2 loader (task_dat2_sound_load.c): this task runs on the asset
 * queue (the App's parallel queue), whose processor time the frame pays, so it
 * only finds the record and trims it. RENDERING it -- an FM synthesiser
 * program run sample by sample, about 1 ms natively for the costliest effect
 * of cache254, several times that in wasm -- is a job on the provider's
 * compute queue (the background queue), Task_Dat1SoundRender, in steps of
 * TORIRS_SOUND_RENDER_STEP_SAMPLES samples of work, checking its slice after
 * every step. The job adds the sound to the provider the moment it is done.
 *
 * The job BORROWS the record: the bank is the provider's, decoded once and
 * freed with it (dat1_buildcache_free), and a render only reads it
 * (RSCache_SoundRenderNew takes it const; RSCache_SoundRenderFree does not
 * touch it). The trim stays where it was, on the bank's record before the
 * render, so the lead-in is what it was when this task rendered whole: the
 * first load of an id takes it, and the trim is idempotent after that.
 */

struct Task_Dat1SoundLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int sound_id;
    /* The background job's join (TASK_AWAIT_JOIN). */
    int rendering;
};

/* ---- the render: a job on the background queue ----------------------------- */

struct Task_Dat1SoundRender
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int sound_id;
    /* The bank's record (borrowed, above), already trimmed. */
    struct RSCache_SoundEffect const* effect;
    struct RSCache_SoundRender* render;
    /* The trim's lead-in, ticks (ToriRS_Sound.queue_delay). */
    int queue_delay;
};

static int
Task_Dat1SoundRender_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat1SoundRender* task = (struct Task_Dat1SoundRender*)task_base;

    PT_BEGIN(&task->pt);

    task->render = RSCache_SoundRenderNew(CacheProvider_Profile(&task->bc->base), task->effect);
    if( !task->render )
    {
        /* An empty program renders to nothing; the reference plays silence. */
        if( getenv("TORIRS_AUDIO_DEBUG") )
            TORIRS_LOG("dat1 sound: effect %d has no audible tone\n", task->sound_id);
        PT_EXIT(&task->pt);
    }
    while( !RSCache_SoundRenderStep(task->render, TORIRS_SOUND_RENDER_STEP_SAMPLES) )
        TASK_YIELD_IF_SPENT(&task->task, &task->pt, io);

    /* Another load may have put it there while this one rendered. */
    if( !CacheProvider_SoundHas(&task->bc->base, task->sound_id) )
    {
        struct RSCache_SoundPcm pcm;
        struct ToriRS_Sound* sound;

        RSCache_SoundRenderTake(task->render, &pcm);
        sound = ToriRS_SoundFromRSCachePcm(task->sound_id, &pcm, task->queue_delay);
        CacheProvider_SoundAdd(&task->bc->base, task->sound_id, sound);
        if( getenv("TORIRS_AUDIO_DEBUG") )
            TORIRS_LOG("dat1 sound: effect %d rendered, %d samples, delay %d ticks\n",
                task->sound_id,
                sound->sample_count,
                sound->queue_delay);
    }

    PT_END(&task->pt);
}

static void
Task_Dat1SoundRender_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat1SoundRender* task = (struct Task_Dat1SoundRender*)task_base;

    /* The record is the bank's: only the render is this job's. */
    RSCache_SoundRenderFree(task->render);
    ToriMem_Free(ToriMem_Assets(), task);
}

static struct ToriRS_TaskVTable Task_Dat1SoundRender_VTable = {
    .run = Task_Dat1SoundRender_Run,
    .free = Task_Dat1SoundRender_Free,
};

static struct ToriRS_Task*
create_render(
    struct Dat1BuildCache* bc,
    int sound_id,
    struct RSCache_SoundEffect const* effect,
    int queue_delay)
{
    struct Task_Dat1SoundRender* task;

    assert(bc);
    assert(effect);
    task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1SoundRender_VTable;
    strcpy(task->task.name, "Dat1SoundRender");
    task->bc = bc;
    task->sound_id = sound_id;
    task->effect = effect;
    task->queue_delay = queue_delay;
    PT_INIT(&task->pt);
    return &task->task;
}

/* ---- the read: on the asset queue ------------------------------------------- */

static int
Task_Dat1SoundLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat1SoundLoad* task = (struct Task_Dat1SoundLoad*)task_base;
    struct RSCache_SoundBank* bank = NULL;

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_sound_bank(task->bc) )
    {
        struct RSCache_FileListDat* jagfile = NULL;

        RSCache_IO_Dat1JagfileLoad(io, 0, RSCACHE_DAT1_CONFIG_SOUND_EFFECTS);
        PT_YIELD(&task->pt);

        jagfile = RSCache_IO_Dat1JagfileDecode(io, 0, RSCACHE_DAT1_CONFIG_SOUND_EFFECTS);
        if( !jagfile )
        {
            TORIRS_LOG("dat1 sound: sound-effects jagfile did not decode\n");
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
            TORIRS_LOG("dat1 sound: no effect %d in this bank\n", task->sound_id);
        PT_EXIT(&task->pt);
    }

    /* Trimmed here, before the render, as ToriRS_SoundFromRSCache orders it
     * (it moves the tone starts, which size the render); rendered on the
     * compute queue in steps, and the job adds the sound. */
    {
        int const queue_delay = RSCache_SoundEffectTrim(bank->effects[task->sound_id]);

        assert(task->bc->base.compute_queue);
        ToriRS_TaskQueue_AddParallelPoolSubTask(
            task->bc->base.compute_queue,
            create_render(task->bc, task->sound_id, bank->effects[task->sound_id], queue_delay),
            &task->rendering);
    }
    TASK_AWAIT_JOIN(&task->task, &task->pt, task->rendering);

    PT_END(&task->pt);
}

static void
Task_Dat1SoundLoad_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Assets(), task_base);
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

    task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1SoundLoad_VTable;
    strcpy(task->task.name, "Dat1SoundLoad");
    task->bc = (struct Dat1BuildCache*)provider;
    task->sound_id = sound_id;
    PT_INIT(&task->pt);
    return &task->task;
}
