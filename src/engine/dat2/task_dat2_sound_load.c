#include "engine/cache_provider.h"
#include "mem/torirs_mem.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/torirs_sound_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/* The imported rev-727 lane keeps its incompatible index-14 setup here and
 * assigns recorded effects only in this high namespace. Ordinary OSRS effects
 * are always tried first, which preserves the native table-4 contract. */
#define RS2012_SAMPLE_SETUP_ID 16000
#define RS2012_SAMPLE_ID_MIN 14000

/*
 * Make one dat2 sound effect resident.
 *
 * One archive per effect id, so unlike dat1 this is a genuine per-id load. Two
 * shapes have to be handled:
 *
 *   - single file: the synth record is the whole archive (every cache up to
 *     OldSchool 230, and both RS2 caches).
 *   - two files: modern OldSchool pairs the synth record (file 0) with a
 *     Jagex-compressed sample (file 1). The sample is not decoded — see
 *     rscache's EXCEPTIONS.md B20 — so the synth record is used, which is the
 *     same effect at lower fidelity rather than silence.
 *
 * A handful of modern ids are sample-only, with no synth record at all. Those
 * are skipped: playing the compressed bytes as PCM would be noise.
 *
 * ## The read here, the render on the background queue, in steps
 *
 * This task runs on the asset queue (the App's parallel queue), which is
 * drained every frame and pays its processor time out of that frame: it reads
 * the archive and decodes the record (a small parse). RENDERING the record is
 * not small -- an FM synthesiser program run sample by sample, up to 7.9 ms of
 * processor time natively for one osrs242 effect (id 8787), over 4 ms for 24
 * of them, several times that in wasm -- so it is a job on the provider's
 * compute queue (the
 * background queue: given time, time-sliced), Task_Dat2SoundRender, cut into
 * steps of TORIRS_SOUND_RENDER_STEP_SAMPLES samples of work (inside a tone:
 * RSCache_SoundRenderStep), checking its slice after every step. A rev-727
 * recorded sample's Vorbis decode is the same kind of work and gets the same
 * treatment (Task_Dat2SoundSampleDecode, SOUND_SAMPLE_DECODE_FRAMES frames a
 * step). The job adds the sound to the provider itself the moment it is done,
 * so the effect is resident at the end of the slice that finishes it: an
 * effect whose render fits one slice is resident the frame it was read, as
 * when it was rendered here, and a long one as soon as its render allows (the
 * audio queue's countdown runs meanwhile, rs_audio.h). This task joins the
 * job, so the assets table holds the one load until the sound is in.
 */

/* Frames of a rev-727 sample's Vorbis decode a background step does (as the
 * music decode's). */
#define SOUND_SAMPLE_DECODE_FRAMES 4096

struct Task_Dat2SoundLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int sound_id;
    /* The background job's join (TASK_AWAIT_JOIN). */
    int rendering;
};

/* ---- the render: a job on the background queue ----------------------------- */

/*
 * An effect's render, stepped. It OWNS the decoded record (handed over by
 * the read) and writes only into the provider (the sound, at its end), so it
 * needs nothing of the read to be alive.
 */
struct Task_Dat2SoundRender
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int sound_id;
    struct RSCache_SoundEffect* effect;
    struct RSCache_SoundRender* render;
    /* The trim's lead-in, ticks (ToriRS_Sound.queue_delay). */
    int queue_delay;
};

static int
Task_Dat2SoundRender_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat2SoundRender* task = (struct Task_Dat2SoundRender*)task_base;

    PT_BEGIN(&task->pt);

    /* Trim before rendering: it moves the tone starts, which is what sizes the
     * render (ToriRS_SoundFromRSCache's order). */
    task->queue_delay = RSCache_SoundEffectTrim(task->effect);
    task->render = RSCache_SoundRenderNew(CacheProvider_Profile(&task->bc->base), task->effect);
    if( !task->render )
    {
        if( getenv("TORIRS_AUDIO_DEBUG") )
            TORIRS_LOG("dat2 sound: effect %d has no audible tone\n", task->sound_id);
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
            TORIRS_LOG("dat2 sound: effect %d rendered, %d samples, delay %d ticks\n",
                task->sound_id,
                sound->sample_count,
                sound->queue_delay);
    }

    PT_END(&task->pt);
}

static void
Task_Dat2SoundRender_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2SoundRender* task = (struct Task_Dat2SoundRender*)task_base;

    RSCache_SoundRenderFree(task->render);
    RSCache_SoundEffectFree(task->effect);
    ToriMem_Free(ToriMem_Assets(), task);
}

static struct ToriRS_TaskVTable Task_Dat2SoundRender_VTable = {
    .run = Task_Dat2SoundRender_Run,
    .free = Task_Dat2SoundRender_Free,
};

/* The render job for `effect`, which it takes. */
static struct ToriRS_Task*
create_render(
    struct Dat2BuildCache* bc,
    int sound_id,
    struct RSCache_SoundEffect* effect)
{
    struct Task_Dat2SoundRender* task;

    assert(bc);
    assert(effect);
    task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2SoundRender_VTable;
    strcpy(task->task.name, "Dat2SoundRender");
    task->bc = bc;
    task->sound_id = sound_id;
    task->effect = effect;
    PT_INIT(&task->pt);
    return &task->task;
}

/* ---- a rev-727 recorded sample's decode: the same, for a Vorbis sample ------- */

struct Task_Dat2SoundSampleDecode
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int sound_id;
    /* The sample's archive, which the decoder reads from between steps. */
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_VorbisSampleDecode* decode;
};

static int
Task_Dat2SoundSampleDecode_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat2SoundSampleDecode* task = (struct Task_Dat2SoundSampleDecode*)task_base;
    struct RSCache_AudioSample* sample;
    struct ToriRS_Sound* sound;

    PT_BEGIN(&task->pt);

    /* The setup is the provider's, read and decoded by the load before it
     * handed this over; it lives as long as the provider. */
    assert(task->bc->rs2012_vorbis_setup);
    task->decode = RSCache_VorbisSampleDecodeNew(
        task->bc->rs2012_vorbis_setup, task->archive->data, task->archive->data_size);
    if( !task->decode )
    {
        TORIRS_LOG("dat2 sound: rev-727 sample %d did not decode\n", task->sound_id);
        PT_EXIT(&task->pt);
    }
    while( !RSCache_VorbisSampleDecodeStep(task->decode, SOUND_SAMPLE_DECODE_FRAMES) )
        TASK_YIELD_IF_SPENT(&task->task, &task->pt, io);

    sample = RSCache_VorbisSampleDecodeTake(task->decode);
    if( !sample )
    {
        TORIRS_LOG("dat2 sound: rev-727 sample %d did not decode\n", task->sound_id);
        PT_EXIT(&task->pt);
    }
    sound = ToriRS_SoundFromRSCacheSample(task->sound_id, sample);
    RSCache_AudioSampleFree(sample);
    if( !sound )
    {
        TORIRS_LOG("dat2 sound: rev-727 sample %d decoded empty\n", task->sound_id);
        PT_EXIT(&task->pt);
    }
    /* Another load may have put it there while this one decoded. */
    if( CacheProvider_SoundHas(&task->bc->base, task->sound_id) )
    {
        ToriRS_SoundFree(sound);
        PT_EXIT(&task->pt);
    }
    CacheProvider_SoundAdd(&task->bc->base, task->sound_id, sound);
    if( getenv("TORIRS_AUDIO_DEBUG") )
        TORIRS_LOG("dat2 sound: rev-727 sample %d decoded exactly, %d samples @ %d Hz\n",
                task->sound_id, sound->sample_count, sound->sample_rate);

    PT_END(&task->pt);
}

static void
Task_Dat2SoundSampleDecode_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2SoundSampleDecode* task = (struct Task_Dat2SoundSampleDecode*)task_base;

    RSCache_VorbisSampleDecodeFree(task->decode);
    RSCache_Dat2DiskArchiveFree(task->archive);
    ToriMem_Free(ToriMem_Assets(), task);
}

static struct ToriRS_TaskVTable Task_Dat2SoundSampleDecode_VTable = {
    .run = Task_Dat2SoundSampleDecode_Run,
    .free = Task_Dat2SoundSampleDecode_Free,
};

/* The decode job for the sample in `archive`, which it takes. */
static struct ToriRS_Task*
create_sample_decode(
    struct Dat2BuildCache* bc,
    int sound_id,
    struct RSCache_Dat2DiskArchive* archive)
{
    struct Task_Dat2SoundSampleDecode* task;

    assert(bc);
    assert(archive);
    task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2SoundSampleDecode_VTable;
    strcpy(task->task.name, "Dat2SoundSampleDecode");
    task->bc = bc;
    task->sound_id = sound_id;
    task->archive = archive;
    PT_INIT(&task->pt);
    return &task->task;
}

/* ---- the read: on the asset queue ------------------------------------------- */

/* The synth record in `archive` (file 0 of a two-file archive), decoded; NULL
 * when there is none to render (a sample-only id, or a record that would not
 * decode). Small: a parse. */
static struct RSCache_SoundEffect*
decode_effect(
    struct Task_Dat2SoundLoad* task,
    struct RSCache_Dat2DiskArchive* archive)
{
    struct RSCache_FileList* group = NULL;
    struct RSCache_SoundEffect* effect = NULL;
    char* record = archive->data;
    int record_size = archive->data_size;

    /* file_count / file_ids arrive already filled: the platform layer holds the
     * open cache and resolves the archive's reference-table entry there. */
    if( archive->file_count > 1 )
    {
        group =
            RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        if( group && group->file_count > 0 )
        {
            record = group->files[0];
            record_size = group->file_sizes[0];
        }
    }

    if( RSCache_SoundIsSampleBlob(record, record_size) )
    {
        if( getenv("TORIRS_AUDIO_DEBUG") )
            TORIRS_LOG("dat2 sound: effect %d is a compressed sample, not decoded\n",
                task->sound_id);
    }
    else
    {
        effect = RSCache_SoundEffectNewDecode(
            CacheProvider_Profile(&task->bc->base), record, record_size);
        if( !effect )
            TORIRS_LOG("dat2 sound: effect %d did not decode\n", task->sound_id);
        else if( effect->_consumed != record_size )
            TORIRS_LOG("dat2 sound: effect %d consumed %d of %d bytes\n",
                task->sound_id,
                effect->_consumed,
                record_size);
    }
    if( group )
        RSCache_FileListFree(group);
    return effect;
}

static int
Task_Dat2SoundLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat2SoundLoad* task = (struct Task_Dat2SoundLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_SoundEffect* effect = NULL;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2SoundLoad(io, 0, task->sound_id);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2SoundDecode(io, 0);
    if( archive )
    {
        effect = decode_effect(task, archive);
        RSCache_Dat2DiskArchiveFree(archive);
        archive = NULL;
        if( !effect )
            PT_EXIT(&task->pt);
        /* Rendered on the compute queue, in steps (the note at the top of
         * this file); the job takes the record and adds the sound. */
        assert(task->bc->base.compute_queue);
        ToriRS_TaskQueue_AddParallelPoolSubTask(
            task->bc->base.compute_queue, create_render(task->bc, task->sound_id, effect),
            &task->rendering);
        TASK_AWAIT_JOIN(&task->task, &task->pt, task->rendering);
        PT_EXIT(&task->pt);
    }

    if( task->sound_id < RS2012_SAMPLE_ID_MIN )
    {
        if( getenv("TORIRS_AUDIO_DEBUG") )
            TORIRS_LOG("dat2 sound: no archive for effect %d\n", task->sound_id);
        PT_EXIT(&task->pt);
    }

    if( !task->bc->rs2012_vorbis_setup )
    {
        RSCache_IO_Dat2MusicLoad(
            io, 0, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, RS2012_SAMPLE_SETUP_ID);
        PT_YIELD(&task->pt);
        archive = RSCache_IO_Dat2MusicDecode(
            io, 0, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES);
        if( archive )
        {
            /* The setup is small (0.2 ms natively): decoded where it is read,
             * as the music load's. */
            task->bc->rs2012_vorbis_setup =
                RSCache_VorbisSetupNewDecode(archive->data, archive->data_size);
            RSCache_Dat2DiskArchiveFree(archive);
            archive = NULL;
        }
        if( !task->bc->rs2012_vorbis_setup )
        {
            TORIRS_ERR("dat2 sound: rev-727 Vorbis setup index 14:%d is absent or invalid\n",
                    RS2012_SAMPLE_SETUP_ID);
            PT_EXIT(&task->pt);
        }
    }

    RSCache_IO_Dat2MusicLoad(
        io, 0, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, task->sound_id);
    PT_YIELD(&task->pt);
    archive = RSCache_IO_Dat2MusicDecode(io, 0, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES);
    if( !archive )
    {
        TORIRS_LOG("dat2 sound: high id %d is absent from effects and rev-727 samples\n",
                task->sound_id);
        PT_EXIT(&task->pt);
    }
    /* Decoded on the compute queue, in steps; the job takes the archive. */
    assert(task->bc->base.compute_queue);
    ToriRS_TaskQueue_AddParallelPoolSubTask(
        task->bc->base.compute_queue, create_sample_decode(task->bc, task->sound_id, archive),
        &task->rendering);
    archive = NULL;
    TASK_AWAIT_JOIN(&task->task, &task->pt, task->rendering);

    PT_END(&task->pt);
}

static void
Task_Dat2SoundLoad_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Assets(), task_base);
}

static struct ToriRS_TaskVTable Task_Dat2SoundLoad_VTable = {
    .run = Task_Dat2SoundLoad_Run,
    .free = Task_Dat2SoundLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2SoundLoad(
    struct CacheProvider* provider,
    int sound_id)
{
    struct Task_Dat2SoundLoad* task;

    assert(provider);

    if( CacheProvider_SoundHas(provider, sound_id) )
        return NULL;

    task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2SoundLoad_VTable;
    strcpy(task->task.name, "Dat2SoundLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->sound_id = sound_id;
    PT_INIT(&task->pt);
    return &task->task;
}
