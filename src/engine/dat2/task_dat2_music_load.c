#include "audio/torirs_music.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RS2012_QBD_SONG_ID 1118
#define RS2012_AWOKEN_SONG_ID 1119
#define RS2012_SAMPLE_SETUP_ID 16000

/*
 * Make a song playable: the track, its instruments, and their samples.
 *
 * This is the longest load chain in the client, and it is a chain rather than a
 * fan-out because each step names the next:
 *
 *   1. the track archive (index 6, or 11 for a jingle) unpacks to a MIDI file
 *      *and* an instrument manifest -- which banked programs it uses and which
 *      notes it plays on each.
 *   2. the shared Vorbis setup (index 14 archive 0), once per session. Every
 *      music sample is decoded against it; without it they are all silence.
 *   3. each patch named by the manifest (index 15).
 *   4. each sample each patch's *used* notes reference -- from index 14 for
 *      pitched instruments and from index 4 (the sound-effect table) for
 *      percussion, which is why the soundbank keys samples by table and id.
 *
 * Step 4 is where the "used notes" matter. A patch has 128 notes and a song
 * typically plays a dozen of them; loading all 128 would be several times the
 * memory and a much longer wait before the first bar.
 *
 * Every loop counter lives in the task struct: a protothread's locals do not
 * survive a yield, and this yields once per cache read.
 */

struct Task_Dat2MusicLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct ToriRS_MusicPlayer* player;

    int song_id;
    enum ToriRS_MusicSource source;
    int song_table;

    struct RSCache_MusicSong* song;

    /*
     * Walk state.
     *
     * `patch_id` is here and not a loop local for the reason the header states
     * and this file got wrong once: a protothread resumes by jumping to a label
     * *inside* the loop body, so a declaration-with-initialiser above the yield
     * is skipped and the variable holds whatever was on the stack. Every patch
     * then installs under one garbage id, the soundbank collapses to a single
     * slot, and the song plays with one instrument or none at all.
     */
    int patch_index;
    int patch_id;
    int note_index;
    int pending_sample_table;
    int pending_sample_id;
    struct RSCache_MusicPatch* pending_patch;
    bool pending_patch_borrowed;

    /** These two raw rev-727 tracks must decode against setup 14:16000. */
    bool rs2012_audio;

    int retained[TORIRS_MUSIC_MAX_PATCHES];
    int retained_count;

    bool failed;
};

/** Widen a sound-effect render (8-bit unsigned) into the bank's 16-bit form. */
static int16_t*
widen_effect(
    const struct RSCache_SoundPcm* pcm)
{
    int16_t* out = malloc((size_t)pcm->sample_count * sizeof(int16_t));

    assert(out);
    for( int i = 0; i < pcm->sample_count; i++ )
        out[i] = (int16_t)(((int)pcm->samples[i] - 128) << 8);
    return out;
}

/** Decode the percussion sample at `id` out of the sound-effect table. */
static bool
add_effect_sample(
    struct Task_Dat2MusicLoad* task,
    struct RSCache_Dat2DiskArchive* archive,
    int id)
{
    const struct RSCache* profile = CacheProvider_Profile(&task->bc->base);
    struct RSCache_SoundEffect* effect;
    struct RSCache_SoundPcm pcm;
    int16_t* widened;
    bool ok = false;

    assert(archive);
    effect = RSCache_SoundEffectNewDecode(profile, archive->data, archive->data_size);
    if( !effect )
        return false;
    memset(&pcm, 0, sizeof(pcm));
    if( RSCache_SoundEffectRender(profile, effect, &pcm) && pcm.sample_count > 0 )
    {
        widened = widen_effect(&pcm);
        if( widened )
            ok = ToriRS_SoundBank_AddSample(
                     &task->player->bank,
                     TORIRS_SOUNDBANK_TABLE_EFFECTS,
                     id,
                     widened,
                     pcm.sample_count,
                     pcm.sample_rate,
                     0,
                     pcm.sample_count,
                     false) != NULL;
    }
    RSCache_SoundPcmRelease(&pcm);
    RSCache_SoundEffectFree(effect);
    return ok;
}

/** Decode a Vorbis music sample against the shared setup. */
static bool
add_music_sample(
    struct Task_Dat2MusicLoad* task,
    struct RSCache_Dat2DiskArchive* archive,
    int id)
{
    struct RSCache_AudioSample* sample;
    bool ok;

    const struct RSCache_VorbisSetup* setup = task->rs2012_audio
                                                  ? task->player->rs2012_vorbis_setup
                                                  : task->player->vorbis_setup;

    if( !archive || !setup )
        return false;
    sample = RSCache_VorbisSampleNewDecode(
        setup, archive->data, archive->data_size);
    if( !sample )
        return false;
    ok = ToriRS_SoundBank_AddSample(
             &task->player->bank,
             TORIRS_SOUNDBANK_TABLE_MUSIC,
             id,
             sample->samples,
             sample->sample_count,
             sample->sample_rate,
             sample->loop_start,
             sample->loop_end,
             sample->loop) != NULL;
    if( ok )
        sample->samples = NULL; /* ownership moved into the bank */
    RSCache_AudioSampleFree(sample);
    return ok;
}

/**
 * Next note of the current patch that still needs a sample.
 *
 * Returns false when the patch is fully covered. Writes the table and id of the
 * sample to fetch.
 */
static bool
next_missing_sample(
    struct Task_Dat2MusicLoad* task,
    const struct RSCache_MusicSong* song,
    const struct RSCache_MusicPatch* patch,
    int* out_table,
    int* out_id)
{
    for( ; task->note_index < 128; task->note_index++ )
    {
        int note = task->note_index;
        int id;
        int table;

        if( !RSCache_MusicSongPatchUsesNote(&song->patches[task->patch_index], note) )
            continue;
        id = RSCache_MusicPatchNoteSampleId(patch, note);
        if( id < 0 )
            continue;
        table = RSCache_MusicPatchNoteIsMusicSample(patch, note)
                    ? TORIRS_SOUNDBANK_TABLE_MUSIC
                    : TORIRS_SOUNDBANK_TABLE_EFFECTS;
        if( ToriRS_SoundBank_FindSample(&task->player->bank, table, id) )
            continue;
        *out_table = table;
        *out_id = id;
        return true;
    }
    return false;
}

static int
Task_Dat2MusicLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2MusicLoad* task = (struct Task_Dat2MusicLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;

    PT_BEGIN(&task->pt);

    /* 1. the track itself. */
    RSCache_IO_Dat2MusicLoad(io, 0, task->song_table, task->song_id);
    PT_YIELD(&task->pt);
    archive = RSCache_IO_Dat2MusicDecode(io, 0, task->song_table);
    if( !archive )
    {
        task->failed = true;
        goto done;
    }
    task->song = RSCache_MusicSongNewDecode(archive->data, archive->data_size);
    RSCache_Dat2DiskArchiveFree(archive);
    if( !task->song )
    {
        fprintf(stderr, "music: song %d did not unpack\n", task->song_id);
        task->failed = true;
        goto done;
    }

    /* 2. the matching shared Vorbis setup, once per session. Foreign samples
     * cannot be decoded with OSRS archive 0 and must never replace it. */
    if( task->rs2012_audio && !task->player->rs2012_vorbis_setup )
    {
        RSCache_IO_Dat2MusicLoad(
            io, 0, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, RS2012_SAMPLE_SETUP_ID);
        PT_YIELD(&task->pt);
        archive = RSCache_IO_Dat2MusicDecode(io, 0, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES);
        if( archive )
        {
            task->player->rs2012_vorbis_setup =
                RSCache_VorbisSetupNewDecode(archive->data, archive->data_size);
            RSCache_Dat2DiskArchiveFree(archive);
        }
        if( !task->player->rs2012_vorbis_setup )
        {
            fprintf(stderr,
                    "music: rev-727 setup index 14:%d is absent or invalid for QBD song %d\n",
                    RS2012_SAMPLE_SETUP_ID, task->song_id);
            task->failed = true;
            goto done;
        }
    }
    else if( !task->rs2012_audio && !task->player->vorbis_setup )
    {
        RSCache_IO_Dat2MusicLoad(io, 0, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, 0);
        PT_YIELD(&task->pt);
        archive = RSCache_IO_Dat2MusicDecode(io, 0, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES);
        if( archive )
        {
            task->player->vorbis_setup =
                RSCache_VorbisSetupNewDecode(archive->data, archive->data_size);
            RSCache_Dat2DiskArchiveFree(archive);
        }
        if( !task->player->vorbis_setup )
            fprintf(stderr, "music: no Vorbis setup (index 14 archive 0); samples will be absent\n");
    }

    /* 3 and 4. Each patch, then each sample its used notes reference. */
    for( task->patch_index = 0;
         !task->failed && task->patch_index < task->song->patch_count;
         task->patch_index++ )
    {
        task->patch_id = task->song->patches[task->patch_index].patch_id;

        if( task->retained_count >= TORIRS_MUSIC_MAX_PATCHES )
            break;
        struct ToriRS_SoundBankPatch* resident =
            ToriRS_SoundBank_FindPatch(&task->player->bank, task->patch_id);
        task->pending_patch_borrowed = resident != NULL;
        if( resident )
        {
            /* A later song can use different notes of the same patch. Walk its
             * note manifest again and load any samples the earlier song did not
             * need; QBD 1119 -> 1118 is exactly this case. */
            task->pending_patch = resident->patch;
        }
        else
        {
            RSCache_IO_Dat2MusicLoad(io, 0, RSCACHE_DAT2_TABLE_MUSIC_PATCHES, task->patch_id);
            PT_YIELD(&task->pt);
            archive = RSCache_IO_Dat2MusicDecode(io, 0, RSCACHE_DAT2_TABLE_MUSIC_PATCHES);
            if( !archive )
            {
                if( task->rs2012_audio ) task->failed = true;
                continue;
            }
            task->pending_patch = RSCache_MusicPatchNewDecode(archive->data, archive->data_size);
            RSCache_Dat2DiskArchiveFree(archive);
            if( !task->pending_patch )
            {
                if( task->rs2012_audio ) task->failed = true;
                continue;
            }
        }

        for( task->note_index = 0; !task->failed && task->note_index < 128; )
        {
            if( !next_missing_sample(
                    task,
                    task->song,
                    task->pending_patch,
                    &task->pending_sample_table,
                    &task->pending_sample_id) )
                break;

            RSCache_IO_Dat2MusicLoad(
                io,
                0,
                task->pending_sample_table == TORIRS_SOUNDBANK_TABLE_MUSIC
                    ? RSCACHE_DAT2_TABLE_MUSIC_SAMPLES
                    : RSCACHE_DAT2_TABLE_SOUND_EFFECTS,
                task->pending_sample_id);
            PT_YIELD(&task->pt);
            archive = RSCache_IO_Dat2MusicDecode(
                io,
                0,
                task->pending_sample_table == TORIRS_SOUNDBANK_TABLE_MUSIC
                    ? RSCACHE_DAT2_TABLE_MUSIC_SAMPLES
                    : RSCACHE_DAT2_TABLE_SOUND_EFFECTS);
            if( task->pending_sample_table == TORIRS_SOUNDBANK_TABLE_MUSIC )
            {
                bool added = add_music_sample(task, archive, task->pending_sample_id);
                if( task->rs2012_audio && !added )
                {
                    fprintf(stderr,
                            "music: rev-727 QBD sample %d is absent or invalid\n",
                            task->pending_sample_id);
                    task->failed = true;
                }
            }
            else
                add_effect_sample(task, archive, task->pending_sample_id);
            RSCache_Dat2DiskArchiveFree(archive);
            /* Whether or not it landed, move past this note: a sample the cache
             * does not have would otherwise be requested forever. */
            task->note_index++;
        }

        /* A required foreign sample failed after the patch was decoded (or
         * borrowed).  Do not retain or publish that incomplete patch: doing
         * so would make a later retry appear resident while one of its notes
         * is still unresolved. */
        if( task->failed )
        {
            if( !task->pending_patch_borrowed )
                RSCache_MusicPatchFree(task->pending_patch);
            task->pending_patch = NULL;
            task->pending_patch_borrowed = false;
            break;
        }

        if( task->pending_patch_borrowed )
        {
            struct ToriRS_SoundBankPatch* slot =
                ToriRS_SoundBank_FindPatch(&task->player->bank, task->patch_id);
            ToriRS_SoundBank_ResolvePatch(&task->player->bank, slot);
            ToriRS_SoundBank_RetainPatch(&task->player->bank, task->patch_id);
            task->retained[task->retained_count++] = task->patch_id;
        }
        else if( ToriRS_SoundBank_AddPatch(
                     &task->player->bank, task->patch_id, task->pending_patch) )
        {
            struct ToriRS_SoundBankPatch* slot =
                ToriRS_SoundBank_FindPatch(&task->player->bank, task->patch_id);
            ToriRS_SoundBank_ResolvePatch(&task->player->bank, slot);
            ToriRS_SoundBank_RetainPatch(&task->player->bank, task->patch_id);
            task->retained[task->retained_count++] = task->patch_id;
        }
        task->pending_patch = NULL;
        task->pending_patch_borrowed = false;
    }

done:
    if( task->failed || !task->song )
    {
        ToriRS_Music_LoadFailed(task->player, task->song_id);
    }
    else
    {
        /* The queue the player pushes stream commands onto is the app's; the
         * install itself only needs to hand over the song and the retained
         * patch list, and the next tick opens the stream. */
        ToriRS_Music_Installed(
            task->player,
            task->song_id,
            task->source,
            task->song,
            task->retained,
            task->retained_count,
            NULL);
        task->song = NULL;
    }

    PT_END(&task->pt);
}

static void
Task_Dat2MusicLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2MusicLoad* task = (struct Task_Dat2MusicLoad*)task_base;

    /* A task killed mid-walk still owns whatever it had decoded. */
    RSCache_MusicSongFree(task->song);
    if( !task->pending_patch_borrowed )
        RSCache_MusicPatchFree(task->pending_patch);
    free(task);
}

static struct ToriRS_TaskVTable Task_Dat2MusicLoad_VTable = {
    .run = Task_Dat2MusicLoad_Run,
    .free = Task_Dat2MusicLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2MusicLoad(
    struct CacheProvider* provider,
    struct ToriRS_MusicPlayer* player,
    int song_id,
    int source)
{
    struct Task_Dat2MusicLoad* task;

    assert(provider);
    assert(player);
    if( song_id < 0 )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2MusicLoad_VTable;
    strcpy(task->task.name, "Dat2MusicLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->player = player;
    task->song_id = song_id;
    task->source = (enum ToriRS_MusicSource)source;
    task->song_table = source == TORIRS_MUSIC_SOURCE_JINGLE
                           ? RSCACHE_DAT2_TABLE_MUSIC_JINGLES
                           : RSCACHE_DAT2_TABLE_MUSIC_TRACKS;
    task->rs2012_audio = task->song_table == RSCACHE_DAT2_TABLE_MUSIC_TRACKS &&
                         (song_id == RS2012_QBD_SONG_ID || song_id == RS2012_AWOKEN_SONG_ID);
    PT_INIT(&task->pt);
    return &task->task;
}
