#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/torirs_sound_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 */

struct Task_Dat2SoundLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int sound_id;
};

static int
Task_Dat2SoundLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2SoundLoad* task = (struct Task_Dat2SoundLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* group = NULL;
    struct RSCache_SoundEffect* effect = NULL;
    struct RSCache_AudioSample* sample = NULL;
    struct ToriRS_Sound* sound = NULL;
    char* record = NULL;
    int record_size = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2SoundLoad(io, 0, task->sound_id);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2SoundDecode(io, 0);
    if( !archive )
    {
        if( task->sound_id < RS2012_SAMPLE_ID_MIN )
        {
            if( getenv("TORIRS_AUDIO_DEBUG") )
                fprintf(stderr, "dat2 sound: no archive for effect %d\n", task->sound_id);
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
                task->bc->rs2012_vorbis_setup =
                    RSCache_VorbisSetupNewDecode(archive->data, archive->data_size);
                RSCache_Dat2DiskArchiveFree(archive);
                archive = NULL;
            }
            if( !task->bc->rs2012_vorbis_setup )
            {
                fprintf(stderr,
                        "dat2 sound: rev-727 Vorbis setup index 14:%d is absent or invalid\n",
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
            fprintf(stderr,
                    "dat2 sound: high id %d is absent from effects and rev-727 samples\n",
                    task->sound_id);
            PT_EXIT(&task->pt);
        }
        sample = RSCache_VorbisSampleNewDecode(
            task->bc->rs2012_vorbis_setup, archive->data, archive->data_size);
        RSCache_Dat2DiskArchiveFree(archive);
        archive = NULL;
        if( !sample )
        {
            fprintf(stderr, "dat2 sound: rev-727 sample %d did not decode\n", task->sound_id);
            PT_EXIT(&task->pt);
        }
        sound = ToriRS_SoundFromRSCacheSample(task->sound_id, sample);
        RSCache_AudioSampleFree(sample);
        sample = NULL;
        if( !sound )
        {
            fprintf(stderr, "dat2 sound: rev-727 sample %d decoded empty\n", task->sound_id);
            PT_EXIT(&task->pt);
        }
        CacheProvider_SoundAdd(&task->bc->base, task->sound_id, sound);
        if( getenv("TORIRS_AUDIO_DEBUG") )
            fprintf(stderr,
                    "dat2 sound: rev-727 sample %d decoded exactly, %d samples @ %d Hz\n",
                    task->sound_id, sound->sample_count, sound->sample_rate);
        PT_EXIT(&task->pt);
    }
    /* file_count / file_ids arrive already filled: the platform layer holds the
     * open cache and resolves the archive's reference-table entry there. */

    record = archive->data;
    record_size = archive->data_size;
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
            fprintf(
                stderr,
                "dat2 sound: effect %d is a compressed sample, not decoded\n",
                task->sound_id);
        goto done;
    }

    effect = RSCache_SoundEffectNewDecode(
        CacheProvider_Profile(&task->bc->base), record, record_size);
    if( !effect )
    {
        fprintf(stderr, "dat2 sound: effect %d did not decode\n", task->sound_id);
        goto done;
    }
    if( effect->_consumed != record_size )
        fprintf(
            stderr,
            "dat2 sound: effect %d consumed %d of %d bytes\n",
            task->sound_id,
            effect->_consumed,
            record_size);

    sound =
        ToriRS_SoundFromRSCache(CacheProvider_Profile(&task->bc->base), task->sound_id, effect);
    if( sound )
    {
        CacheProvider_SoundAdd(&task->bc->base, task->sound_id, sound);
        if( getenv("TORIRS_AUDIO_DEBUG") )
            fprintf(
                stderr,
                "dat2 sound: effect %d rendered, %d samples, delay %d ticks\n",
                task->sound_id,
                sound->sample_count,
                sound->queue_delay);
    }
    else if( getenv("TORIRS_AUDIO_DEBUG") )
    {
        fprintf(stderr, "dat2 sound: effect %d has no audible tone\n", task->sound_id);
    }

done:
    RSCache_SoundEffectFree(effect);
    if( group )
        RSCache_FileListFree(group);
    RSCache_Dat2DiskArchiveFree(archive);

    PT_END(&task->pt);
}

static void
Task_Dat2SoundLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
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

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2SoundLoad_VTable;
    strcpy(task->task.name, "Dat2SoundLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->sound_id = sound_id;
    PT_INIT(&task->pt);
    return &task->task;
}
