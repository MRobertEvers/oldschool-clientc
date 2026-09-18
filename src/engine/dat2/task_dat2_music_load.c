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
#include "log/torirs_log.h"

#define RS2012_QBD_SONG_ID 1118
#define RS2012_AWOKEN_SONG_ID 1119
#define RS2012_SAMPLE_SETUP_ID 16000

/*
 * Make a song playable: the track, its instruments, and their samples.
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
 * ## Two dependencies, not forty-four
 *
 * Only two of those arrows are real: the track names the patches, and a patch
 * names its samples. Everything else is a SET, and this used to walk each set
 * an archive at a time -- 44 reads for a track, each one a round trip waited
 * out before the next was asked for, which on the browser is 1.0 to 1.6 s of
 * loading (runner telemetry, 2026-09-18: a MusicLoad chain 44 deep, the
 * longest in the client).
 *
 * So each set goes out as one wave (TORIRS_IOK_CACHE_PREFETCH) and is then
 * read back out of the resident store without touching the wire. Between the
 * two waves sits the survey, which unpacks every patch -- that is what turns
 * the first set into the second. Three round trips, and the walk at the end
 * is the same walk it always was.
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

    /*
     * The survey: what the three prefetch waves need to know, gathered before
     * any of the loads below go out. @see the wave note in the run body.
     *
     * `decoded` holds the patch each survey step unpacked, and the walk below
     * TAKES it (setting the entry NULL) rather than reading the archive a
     * second time -- so a patch is fetched once and decoded once. Whatever the
     * walk never reaches is this task's to free.
     */
    int survey_index;
    int survey_count;
    int* patch_ids;
    int patch_id_count;
    struct RSCache_MusicPatch* decoded[TORIRS_MUSIC_MAX_PATCHES];
    int* sample_music_ids;
    int sample_music_count;
    int* sample_effect_ids;
    int sample_effect_count;

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

/** Append `id` to a survey list unless it is already there. The lists are
 *  tens of entries at their worst, so the linear scan is the cheap half. */
static void
id_list_add(
    int** ids,
    int* count,
    int id)
{
    assert(ids);
    assert(count);
    for( int i = 0; i < *count; i++ )
        if( (*ids)[i] == id )
            return;
    *ids = realloc(*ids, (size_t)(*count + 1) * sizeof(int));
    assert(*ids);
    (*ids)[(*count)++] = id;
}

/**
 * Every sample `patch` needs for the notes THIS song plays on it, named into
 * the two prefetch lists.
 *
 * The same walk next_missing_sample does, without the task's cursors: that one
 * finds the next sample to fetch, this one names the whole set so that one
 * wave can fetch it. The bank test is the same, so a sample an earlier song
 * already decoded is not asked for again.
 */
static void
survey_patch_samples(
    struct Task_Dat2MusicLoad* task,
    int patch_index,
    const struct RSCache_MusicPatch* patch)
{
    assert(task);
    assert(patch);
    for( int note = 0; note < 128; note++ )
    {
        int id;
        int table;

        if( !RSCache_MusicSongPatchUsesNote(&task->song->patches[patch_index], note) )
            continue;
        id = RSCache_MusicPatchNoteSampleId(patch, note);
        if( id < 0 )
            continue;
        table = RSCache_MusicPatchNoteIsMusicSample(patch, note)
                    ? TORIRS_SOUNDBANK_TABLE_MUSIC
                    : TORIRS_SOUNDBANK_TABLE_EFFECTS;
        if( ToriRS_SoundBank_FindSample(&task->player->bank, table, id) )
            continue;
        if( table == TORIRS_SOUNDBANK_TABLE_MUSIC )
            id_list_add(&task->sample_music_ids, &task->sample_music_count, id);
        else
            id_list_add(&task->sample_effect_ids, &task->sample_effect_count, id);
    }
}

/** One prefetch wave: `count` groups of `table` made resident together. Worth
 *  a round trip only for more than one group; a single group is one read
 *  either way, and the read is coming regardless. */
static bool
survey_wave_wanted(
    int count)
{
    return count > 1;
}

static int
Task_Dat2MusicLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
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
        TORIRS_LOG("music: song %d did not unpack\n", task->song_id);
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
            TORIRS_ERR("music: rev-727 setup index 14:%d is absent or invalid for QBD song %d\n",
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
            TORIRS_LOG("music: no Vorbis setup (index 14 archive 0); samples will be absent\n");
    }

    /*
     * 2a-2c. THE WAVES.
     *
     * Everything above named one archive and had to wait for it to name the
     * next: the track names the patches, a patch names its samples. That is
     * genuine sequencing and stays. What was NOT genuine was asking for each
     * of those archives on its own once the set was known -- a song with a
     * dozen instruments walked its way through 44 reads, one round trip after
     * another, which on the browser is 1.0 to 1.6 s of loading for a track
     * (runner telemetry: a MusicLoad chain 44 deep).
     *
     * The set is known in two steps, so it goes out in two waves. First every
     * patch the song names, together; the survey then unpacks each one -- off
     * the resident store, no wire -- and that is what names every sample. Then
     * every sample, together. The walk below is unchanged except that it takes
     * the patch the survey already decoded instead of asking for it again, so
     * each archive is fetched once and decoded once. Three round trips where
     * there were forty-four.
     */
    task->survey_count = task->song->patch_count;
    if( task->survey_count > TORIRS_MUSIC_MAX_PATCHES )
        task->survey_count = TORIRS_MUSIC_MAX_PATCHES;

    for( task->survey_index = 0; task->survey_index < task->survey_count; task->survey_index++ )
    {
        int id = task->song->patches[task->survey_index].patch_id;
        /* Resident from an earlier song: neither fetched nor decoded again.
         * Its notes are still surveyed -- see the borrowed-patch case below,
         * where a later song plays notes the earlier one never touched. */
        if( ToriRS_SoundBank_FindPatch(&task->player->bank, id) )
            continue;
        id_list_add(&task->patch_ids, &task->patch_id_count, id);
    }
    if( survey_wave_wanted(task->patch_id_count) )
    {
        ToriRS_IO_QueueCachePrefetch(
            io,
            0,
            0,
            RSCACHE_DAT2_TABLE_MUSIC_PATCHES,
            TORIRS_IO_CACHE_DAT2,
            task->patch_ids,
            task->patch_id_count);
        PT_YIELD(&task->pt);
        ToriRS_IO_ClearItem(ToriRS_IO_TaskSlot(io, 0));
    }
    free(task->patch_ids);
    task->patch_ids = NULL;

    /* 2b. Unpack each patch and let it name its samples. Every read here is
     * answered out of the store the wave above filled, so the yields cost
     * passes and not round trips. */
    for( task->survey_index = 0; task->survey_index < task->survey_count; task->survey_index++ )
    {
        struct ToriRS_SoundBankPatch* resident = ToriRS_SoundBank_FindPatch(
            &task->player->bank, task->song->patches[task->survey_index].patch_id);

        if( resident )
        {
            survey_patch_samples(task, task->survey_index, resident->patch);
            continue;
        }
        RSCache_IO_Dat2MusicLoad(
            io,
            0,
            RSCACHE_DAT2_TABLE_MUSIC_PATCHES,
            task->song->patches[task->survey_index].patch_id);
        PT_YIELD(&task->pt);
        archive = RSCache_IO_Dat2MusicDecode(io, 0, RSCACHE_DAT2_TABLE_MUSIC_PATCHES);
        if( !archive )
            continue;
        task->decoded[task->survey_index] =
            RSCache_MusicPatchNewDecode(archive->data, archive->data_size);
        RSCache_Dat2DiskArchiveFree(archive);
        if( task->decoded[task->survey_index] )
            survey_patch_samples(
                task, task->survey_index, task->decoded[task->survey_index]);
    }

    /* 2c. Every sample those patches named, in one wave per table. */
    if( survey_wave_wanted(task->sample_music_count) )
    {
        ToriRS_IO_QueueCachePrefetch(
            io,
            0,
            0,
            RSCACHE_DAT2_TABLE_MUSIC_SAMPLES,
            TORIRS_IO_CACHE_DAT2,
            task->sample_music_ids,
            task->sample_music_count);
        PT_YIELD(&task->pt);
        ToriRS_IO_ClearItem(ToriRS_IO_TaskSlot(io, 0));
    }
    if( survey_wave_wanted(task->sample_effect_count) )
    {
        ToriRS_IO_QueueCachePrefetch(
            io,
            0,
            0,
            RSCACHE_DAT2_TABLE_SOUND_EFFECTS,
            TORIRS_IO_CACHE_DAT2,
            task->sample_effect_ids,
            task->sample_effect_count);
        PT_YIELD(&task->pt);
        ToriRS_IO_ClearItem(ToriRS_IO_TaskSlot(io, 0));
    }
    free(task->sample_music_ids);
    task->sample_music_ids = NULL;
    free(task->sample_effect_ids);
    task->sample_effect_ids = NULL;

    /* 3 and 4. Each patch, then each sample its used notes reference. */
    for( task->patch_index = 0;
         !task->failed && task->patch_index < task->survey_count;
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
            /* Taken from the survey, which fetched and unpacked it above. NULL
             * is the same two failures it used to read as here -- the cache
             * has no such patch, or it did not unpack -- and is handled the
             * same way. */
            task->pending_patch = task->decoded[task->patch_index];
            task->decoded[task->patch_index] = NULL;
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
                    TORIRS_ERR("music: rev-727 QBD sample %d is absent or invalid\n",
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

    /* A task killed mid-walk still owns whatever it had decoded: the song, the
     * patch in hand, and every patch the survey unpacked that the walk never
     * reached (a failure part way through, or a song naming more patches than
     * the bank will hold). */
    RSCache_MusicSongFree(task->song);
    if( !task->pending_patch_borrowed )
        RSCache_MusicPatchFree(task->pending_patch);
    for( int i = 0; i < TORIRS_MUSIC_MAX_PATCHES; i++ )
        RSCache_MusicPatchFree(task->decoded[i]);
    free(task->patch_ids);
    free(task->sample_music_ids);
    free(task->sample_effect_ids);
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
