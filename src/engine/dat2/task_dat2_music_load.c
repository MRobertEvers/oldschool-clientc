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
 * ## Three tasks, because there are three things
 *
 * Only two of those arrows are dependencies: the track names the instruments,
 * an instrument names its samples. The rest are SETS, and a set is one
 * sub-task per member (ToriRS_TaskQueue_AddParallelPoolSubTask) waited for
 * once at the end (PT_TASK_JOIN). So this file is a song task that fans out
 * instruments, an instrument task that fans out samples, and a sample task.
 *
 * That is also why none of the three carries a walk cursor. A loop that waits
 * in the middle of itself has to keep every loop variable in the task, because
 * a protothread forgets its locals at each yield -- which is where this file's
 * previous 28 fields came from, first as a frame cursor and then as a survey
 * with prefetch waves bolted beside it. The loops below queue tasks and do not
 * wait, so their variables are ordinary locals and the structs stay small.
 *
 * Speed is unchanged by that: every sibling of a fan-out queues its read in
 * the same pass, so all of an instrument's samples go onto the wire in one
 * batch exactly as a prefetch wave did. A cold twelve-instrument track is four
 * round trips -- track, setup, all instruments, all samples -- against the
 * forty-four it once walked one at a time.
 */

/* --- one sample ---------------------------------------------------------- */

struct Task_Dat2MusicSampleLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct ToriRS_MusicPlayer* player;
    /** TORIRS_SOUNDBANK_TABLE_MUSIC (pitched) or _EFFECTS (percussion). */
    int table;
    int id;
    bool rs2012_audio;
    /* The song's flag, shared down the fan-out exactly as the join count is:
     * a required sample that does not land fails the whole track, and only
     * the song can refuse to install. The song outlives us -- it is parked on
     * the join that counts us -- so the pointer is good for our whole life. */
    bool* failed;
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
    struct Task_Dat2MusicSampleLoad* task,
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
    struct Task_Dat2MusicSampleLoad* task,
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

static int
Task_Dat2MusicSampleLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat2MusicSampleLoad* task = (struct Task_Dat2MusicSampleLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive;
    int const table = task->table == TORIRS_SOUNDBANK_TABLE_MUSIC
                          ? RSCACHE_DAT2_TABLE_MUSIC_SAMPLES
                          : RSCACHE_DAT2_TABLE_SOUND_EFFECTS;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2MusicLoad(io, 0, table, task->id);
    PT_YIELD(&task->pt);
    archive = RSCache_IO_Dat2MusicDecode(io, 0, table);

    if( task->table == TORIRS_SOUNDBANK_TABLE_MUSIC )
    {
        if( !add_music_sample(task, archive, task->id) && task->rs2012_audio )
        {
            TORIRS_ERR("music: rev-727 QBD sample %d is absent or invalid\n", task->id);
            *task->failed = true;
        }
    }
    else
        add_effect_sample(task, archive, task->id);
    RSCache_Dat2DiskArchiveFree(archive);

    PT_END(&task->pt);
}

static struct ToriRS_TaskVTable Task_Dat2MusicSampleLoad_VTable = {
    .run = Task_Dat2MusicSampleLoad_Run,
    .free = NULL,
};

static struct ToriRS_Task*
create_sample_load(
    struct Dat2BuildCache* bc,
    struct ToriRS_MusicPlayer* player,
    int table,
    int id,
    bool rs2012_audio,
    bool* failed)
{
    struct Task_Dat2MusicSampleLoad* task;

    assert(bc);
    assert(player);
    assert(failed);
    /* Already decoded by an earlier song, or by the instrument next door:
     * NULL is what AddParallelPoolSubTask takes for "nothing to do". */
    if( id < 0 || ToriRS_SoundBank_FindSample(&player->bank, table, id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2MusicSampleLoad_VTable;
    strcpy(task->task.name, "Dat2MusicSampleLoad");
    task->bc = bc;
    task->player = player;
    task->table = table;
    task->id = id;
    task->rs2012_audio = rs2012_audio;
    task->failed = failed;
    PT_INIT(&task->pt);
    return &task->task;
}

/* --- one instrument ------------------------------------------------------ */

struct Task_Dat2MusicPatchLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct ToriRS_MusicPlayer* player;
    /** The song's manifest, for which notes THIS track plays on this patch.
     *  Borrowed: the song is parked on the join that counts us. */
    const struct RSCache_MusicSong* song;
    int patch_index;
    int patch_id;
    struct RSCache_MusicPatch* patch;
    /** The patch was already in the bank; we neither own nor install it. A
     *  later song can still play notes the earlier one never touched, which
     *  is why a resident patch is walked again rather than skipped. */
    bool borrowed;
    bool rs2012_audio;
    bool* failed;
};

static int
Task_Dat2MusicPatchLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat2MusicPatchLoad* task = (struct Task_Dat2MusicPatchLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive;
    struct ToriRS_SoundBankPatch* slot;

    PT_BEGIN(&task->pt);

    slot = ToriRS_SoundBank_FindPatch(&task->player->bank, task->patch_id);
    if( slot )
    {
        task->borrowed = true;
        task->patch = slot->patch;
    }
    else
    {
        RSCache_IO_Dat2MusicLoad(io, 0, RSCACHE_DAT2_TABLE_MUSIC_PATCHES, task->patch_id);
        PT_YIELD(&task->pt);
        archive = RSCache_IO_Dat2MusicDecode(io, 0, RSCACHE_DAT2_TABLE_MUSIC_PATCHES);
        if( archive )
        {
            task->patch = RSCache_MusicPatchNewDecode(archive->data, archive->data_size);
            RSCache_Dat2DiskArchiveFree(archive);
        }
        if( !task->patch )
        {
            /* A song naming a patch this cache does not carry is ordinary;
             * for the two raw rev-727 tracks it is not. */
            if( task->rs2012_audio )
                *task->failed = true;
            PT_EXIT(&task->pt);
        }
    }

    if( task->borrowed )
        PT_EXIT(&task->pt);

    if( ToriRS_SoundBank_AddPatch(&task->player->bank, task->patch_id, task->patch) )
        task->patch = NULL; /* the bank owns it now; our Free must not */

    PT_END(&task->pt);
}

static void
Task_Dat2MusicPatchLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2MusicPatchLoad* task = (struct Task_Dat2MusicPatchLoad*)task_base;

    /* Only what we decoded ourselves and never handed over. */
    if( !task->borrowed )
        RSCache_MusicPatchFree(task->patch);
    free(task);
}

static struct ToriRS_TaskVTable Task_Dat2MusicPatchLoad_VTable = {
    .run = Task_Dat2MusicPatchLoad_Run,
    .free = Task_Dat2MusicPatchLoad_Free,
};

static struct ToriRS_Task*
create_patch_load(
    struct Dat2BuildCache* bc,
    struct ToriRS_MusicPlayer* player,
    const struct RSCache_MusicSong* song,
    int patch_index,
    bool rs2012_audio,
    bool* failed)
{
    struct Task_Dat2MusicPatchLoad* task;

    assert(bc);
    assert(player);
    assert(song);
    assert(failed);
    assert(patch_index >= 0);
    assert(patch_index < song->patch_count);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2MusicPatchLoad_VTable;
    strcpy(task->task.name, "Dat2MusicPatchLoad");
    task->bc = bc;
    task->player = player;
    task->song = song;
    task->patch_index = patch_index;
    task->patch_id = song->patches[patch_index].patch_id;
    task->rs2012_audio = rs2012_audio;
    task->failed = failed;
    PT_INIT(&task->pt);
    return &task->task;
}

/* --- one song ------------------------------------------------------------ */

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

    /** These two raw rev-727 tracks must decode against setup 14:16000. */
    bool rs2012_audio;
    bool failed;

    /** Instruments still loading. */
    int pending;

    int retained[TORIRS_MUSIC_MAX_PATCHES];
    int retained_count;
};

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
     * 3. Every instrument the song names.
     *
     * Bounded by what the bank will hold: a song naming more patches than
     * TORIRS_MUSIC_MAX_PATCHES cannot retain them all, and queuing loads for
     * instruments that can never be installed is work thrown away.
     */
    for( int i = 0; i < task->song->patch_count && i < TORIRS_MUSIC_MAX_PATCHES; i++ )
        ToriRS_TaskQueue_AddParallelPoolSubTask(
            task->bc->base.asset_queue,
            create_patch_load(
                task->bc, task->player, task->song, i, task->rs2012_audio, &task->failed),
            &task->pending);
    /* PT_TASK_JOIN's shape, written out because that macro names its task
     * `self` and this file names it `task`. @see task_dat2_healthbar_load.c. */
    while( task->pending > 0 )
    {
        task->task.blocked = 1;
        PT_YIELD(&task->pt);
    }

    /*
     * 4. Every sample those instruments play, fetched together.
     *
     * Fanned out HERE and not from each instrument, which is what a first cut
     * did: instruments share samples, and siblings all queue in the same pass
     * before any of them lands, so each one asked for the shared sample again.
     * Measured on a cold track that was 58 fetches where 13 were wanted. The
     * song is the only place that sees the whole set, so it is the only place
     * that can ask for each sample once.
     *
     * `seen` is a local, and may be: nothing below it yields until the fan-out
     * is finished and the list is freed. That is the same reason the loop's
     * counters are locals -- see the note at the top of this file.
     */
    {
        int const max_ids = task->song->patch_count * 128;
        int* seen = malloc((size_t)(max_ids > 0 ? max_ids : 1) * sizeof(int));
        int seen_count = 0;

        assert(seen);
        for( int i = 0; i < task->song->patch_count && i < TORIRS_MUSIC_MAX_PATCHES; i++ )
        {
            struct ToriRS_SoundBankPatch* slot =
                ToriRS_SoundBank_FindPatch(&task->player->bank, task->song->patches[i].patch_id);
            if( !slot )
                continue;
            for( int note = 0; note < 128; note++ )
            {
                int id;
                int table;
                int key;
                bool dup = false;

                if( !RSCache_MusicSongPatchUsesNote(&task->song->patches[i], note) )
                    continue;
                id = RSCache_MusicPatchNoteSampleId(slot->patch, note);
                if( id < 0 )
                    continue;
                table = RSCache_MusicPatchNoteIsMusicSample(slot->patch, note)
                            ? TORIRS_SOUNDBANK_TABLE_MUSIC
                            : TORIRS_SOUNDBANK_TABLE_EFFECTS;
                /* One key over both tables: percussion and pitched samples
                 * share an id space only by accident. */
                key = (table << 24) | id;
                for( int k = 0; k < seen_count; k++ )
                    if( seen[k] == key )
                        dup = true;
                if( dup )
                    continue;
                seen[seen_count++] = key;
                ToriRS_TaskQueue_AddParallelPoolSubTask(
                    task->bc->base.asset_queue,
                    create_sample_load(
                        task->bc, task->player, table, id, task->rs2012_audio, &task->failed),
                    &task->pending);
            }
        }
        free(seen);
    }
    while( task->pending > 0 )
    {
        task->task.blocked = 1;
        PT_YIELD(&task->pt);
    }

    /*
     * 5. What actually landed. The instruments installed themselves and their
     * samples are in now, so each one can be bound and held; the song only
     * has to hold what it is playing, which it can read back out of the bank
     * because it has known the ids since step 1.
     */
    for( int i = 0; i < task->song->patch_count && i < TORIRS_MUSIC_MAX_PATCHES; i++ )
    {
        int const patch_id = task->song->patches[i].patch_id;
        struct ToriRS_SoundBankPatch* slot =
            ToriRS_SoundBank_FindPatch(&task->player->bank, patch_id);
        if( !slot )
        {
            /* Ordinary for an OSRS cache that simply lacks the record; fatal
             * for the two rev-727 tracks, which have no silent-instrument
             * rendition worth playing. */
            if( task->rs2012_audio )
                task->failed = true;
            continue;
        }
        if( task->retained_count >= TORIRS_MUSIC_MAX_PATCHES )
            break;
        ToriRS_SoundBank_ResolvePatch(&task->player->bank, slot);
        ToriRS_SoundBank_RetainPatch(&task->player->bank, patch_id);
        task->retained[task->retained_count++] = patch_id;
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

    /* A task killed mid-load still owns the song it decoded. Its instruments
     * own nothing of ours: each one freed whatever it held when the queue
     * reaped it. */
    RSCache_MusicSongFree(task->song);
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
