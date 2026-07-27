#include "rs_audio.h"

#include "engine/cache_provider.h"
#include "engine/torirs_sound_from_rscache.h"
#include "task_runner.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool
audio_trace(void)
{
    static int enabled = -1;
    if( enabled < 0 )
        enabled = getenv("TORIRS_AUDIO_DEBUG") != NULL ? 1 : 0;
    return enabled != 0;
}

void
RS_Audio_Init(struct RS_Audio* audio)
{
    assert(audio);
    memset(audio, 0, sizeof(*audio));
    audio->enabled = true;
    audio->volume = RS_AUDIO_DEFAULT_VOLUME;
    audio->current_song = -1;
    audio->jingle_id = -1;
    audio->last_sound_id = -1;
    audio->last_loops = -1;
}

static void
release_lent(struct RS_Audio* audio)
{
    for( int i = 0; i < audio->lent_count; i++ )
    {
        free(audio->lent[i]);
        audio->lent[i] = NULL;
    }
    audio->lent_count = 0;
}

void
RS_Audio_Shutdown(struct RS_Audio* audio)
{
    if( !audio )
        return;
    release_lent(audio);
}

void
RS_Audio_Synth(
    struct RS_Audio* audio,
    int synth_id,
    int loops,
    int delay)
{
    struct RS_AudioEntry* entry;

    assert(audio);
    if( !audio->enabled )
        return;
    if( audio->count >= RS_AUDIO_QUEUE_MAX )
    {
        audio->dropped_queue_full++;
        return;
    }

    entry = &audio->queue[audio->count++];
    memset(entry, 0, sizeof(*entry));
    entry->sound_id = synth_id;
    entry->loops = loops;
    entry->delay = delay;

    if( audio_trace() )
        fprintf(
            stderr, "rs_audio: queued synth id=%d loops=%d delay=%d\n", synth_id, loops, delay);
}

void
RS_Audio_Song(
    struct RS_Audio* audio,
    int song_id)
{
    assert(audio);
    audio->current_song = song_id;
    if( audio_trace() )
        fprintf(stderr, "rs_audio: song id=%d (no midi backend)\n", song_id);
}

void
RS_Audio_Jingle(
    struct RS_Audio* audio,
    int jingle_id,
    int delay)
{
    assert(audio);
    audio->jingle_id = jingle_id;
    audio->jingle_delay = delay;
    if( audio_trace() )
        fprintf(stderr, "rs_audio: jingle id=%d delay=%d (no midi backend)\n", jingle_id, delay);
}

static void
remove_entry(
    struct RS_Audio* audio,
    int index)
{
    for( int i = index; i + 1 < audio->count; i++ )
        audio->queue[i] = audio->queue[i + 1];
    audio->count--;
}

/** Clip length in client ticks (20ms each), for the overlap rule. */
static int
length_in_ticks(
    int sample_count,
    int sample_rate)
{
    if( sample_rate <= 0 )
        return 0;
    return (int)(((int64_t)sample_count * 1000) / ((int64_t)sample_rate * 20));
}

/**
 * Play one due entry, or decline it.
 *
 * The overlap rule is the reference's, restated in ticks: a clip is played only
 * if it would still be sounding when the one already playing ends. Same test,
 * same outcome — the reference compares wall-clock milliseconds against byte
 * counts divided by 22, which is the same ratio at 22050Hz.
 */
static void
play_entry(
    struct RS_Audio* audio,
    const struct RS_AudioEntry* entry,
    const struct ToriRS_Sound* sound,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;
    uint8_t* pcm;
    int sample_count = 0;
    int ticks;

    if( !out || audio->lent_count >= TORIRS_AUDIO_QUEUE_MAX )
    {
        /* Nothing listening, or more plays in one tick than the platform queue
         * can carry. Either way this is not a sound problem — count it and move
         * on rather than allocating a buffer nobody will read. */
        audio->dropped_queue_full++;
        return;
    }

    ticks = length_in_ticks(sound->sample_count, sound->sample_rate);
    if( audio->tick + ticks <= audio->last_play_tick + audio->last_play_length_ticks )
    {
        audio->dropped_overlap++;
        if( audio_trace() )
            fprintf(
                stderr,
                "rs_audio: id=%d skipped, %d-tick clip under the %d ticks still playing\n",
                entry->sound_id,
                ticks,
                audio->last_play_tick + audio->last_play_length_ticks - audio->tick);
        return;
    }

    pcm = ToriRS_SoundExpandLoops(sound, entry->loops, &sample_count);
    if( !pcm )
    {
        audio->dropped_absent++;
        return;
    }
    /* The interface lends the bytes until the next drain, so the buffer is kept
     * here and freed on the following tick. */
    audio->lent[audio->lent_count++] = pcm;

    memset(&command, 0, sizeof(command));
    command.kind = TORIRS_AUDIO_CMD_PLAY_PCM;
    command.pcm = pcm;
    command.sample_count = sample_count;
    command.sample_rate = sound->sample_rate;
    command.channels = sound->channels;
    command.sound_id = entry->sound_id;
    ToriRS_AudioQueue_Push(out, &command);

    audio->last_play_tick = audio->tick;
    audio->last_play_length_ticks = length_in_ticks(sample_count, sound->sample_rate);
    audio->last_sound_id = entry->sound_id;
    audio->last_loops = entry->loops;
    audio->played++;

    if( audio_trace() )
        fprintf(
            stderr,
            "rs_audio: play id=%d loops=%d samples=%d (%d ticks)\n",
            entry->sound_id,
            entry->loops,
            sample_count,
            audio->last_play_length_ticks);
}

void
RS_Audio_Tick(
    struct RS_Audio* audio,
    struct CacheProvider* provider,
    struct TaskRunner* runner,
    struct ToriRS_AudioQueue* out)
{
    assert(audio);

    /* The platform has had its frame to consume last tick's buffers. */
    release_lent(audio);
    audio->tick++;

    for( int index = 0; index < audio->count; index++ )
    {
        struct RS_AudioEntry* entry = &audio->queue[index];
        struct ToriRS_Sound* sound =
            provider ? CacheProvider_SoundGet(provider, entry->sound_id) : NULL;

        if( !sound )
        {
            /* Ask once, then wait. Not counting down while waiting keeps the
             * server's delay meaningful: it is relative to the effect's own
             * start, which is not known until the effect is rendered. */
            if( !entry->load_requested && provider && runner )
            {
                struct ToriRS_Task* task = CreateTask_SoundLoad(provider, entry->sound_id);
                entry->load_requested = true;
                if( task )
                    ToriRS_TaskQueue_Add(runner->queue, task);
            }
            if( ++entry->waited >= RS_AUDIO_LOAD_WAIT_TICKS )
            {
                audio->dropped_absent++;
                if( audio_trace() )
                    fprintf(
                        stderr,
                        "rs_audio: giving up on id=%d (never became resident)\n",
                        entry->sound_id);
                remove_entry(audio, index);
                index--;
            }
            continue;
        }

        if( !entry->delay_resolved )
        {
            /* Reference: waveDelay = delay + Wave.delays[id]. */
            entry->delay += sound->queue_delay;
            entry->delay_resolved = true;
        }

        if( entry->delay > 0 )
        {
            entry->delay--;
            continue;
        }

        play_entry(audio, entry, sound, out);
        remove_entry(audio, index);
        index--;
    }
}

void
RS_Audio_SetVolumeLevel(
    struct RS_Audio* audio,
    int level,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;
    static const int VOLUME_BY_LEVEL[4] = { 128, 96, 64, 32 };

    assert(audio);
    if( level >= 0 && level <= 3 )
    {
        audio->volume = VOLUME_BY_LEVEL[level];
        audio->enabled = true;
    }
    else if( level == 4 )
    {
        audio->enabled = false;
    }
    else
    {
        return;
    }

    memset(&command, 0, sizeof(command));
    command.kind = TORIRS_AUDIO_CMD_SET_VOLUME;
    command.sound_id = -1;
    command.volume = audio->enabled ? audio->volume : 0;
    ToriRS_AudioQueue_Push(out, &command);

    if( audio_trace() )
        fprintf(
            stderr,
            "rs_audio: volume level=%d -> %d (%s)\n",
            level,
            command.volume,
            audio->enabled ? "on" : "off");
}
