#include "platform/platform_audio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Silent audio backend.
 *
 * Two jobs. It is what headless runs and tests build against — every command is
 * accounted for, and the last clip is summarised so a test can assert that the
 * right effect reached the platform with plausible PCM, without owning a sound
 * card. And it is the honest answer when a device cannot be opened: the game
 * keeps running and keeps reporting what it wanted to play.
 *
 * "Played" here means accepted, not audible. Nothing else in the interface can
 * distinguish the two, which is the point of having it.
 */

struct PlatformAudio
{
    int sample_rate;
    int volume;
    struct PlatformAudioStats stats;
    /** Last accepted clip, for tests and for TORIRS_AUDIO_DEBUG. */
    int last_sound_id;
    int last_sample_count;
    int last_nonsilent_samples;
    bool trace;
};

struct PlatformAudio*
PlatformAudio_New(void)
{
    struct PlatformAudio* audio = calloc(1, sizeof(*audio));
    assert(audio);
    audio->volume = TORIRS_AUDIO_VOLUME_MAX;
    audio->stats.volume = TORIRS_AUDIO_VOLUME_MAX;
    audio->last_sound_id = -1;
    audio->trace = getenv("TORIRS_AUDIO_DEBUG") != NULL;
    return audio;
}

bool
PlatformAudio_Init(
    struct PlatformAudio* audio,
    int sample_rate)
{
    if( !audio )
        return false;
    audio->sample_rate = sample_rate > 0 ? sample_rate : 22050;
    /* No device, and saying so is the contract — a caller that wants to know
     * whether anything will be heard checks device_open. */
    audio->stats.device_open = false;
    return true;
}

void
PlatformAudio_Free(struct PlatformAudio* audio)
{
    free(audio);
}

void
PlatformAudio_Submit(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* command)
{
    if( !audio || !command )
        return;

    switch( command->kind )
    {
    case TORIRS_AUDIO_CMD_PLAY_PCM:
        audio->stats.submitted++;
        if( !command->pcm || command->sample_count <= 0 || audio->volume <= 0 )
        {
            audio->stats.dropped++;
            break;
        }
        audio->stats.played++;
        audio->last_sound_id = command->sound_id;
        audio->last_sample_count = command->sample_count;
        audio->last_nonsilent_samples = 0;
        for( int i = 0; i < command->sample_count; i++ )
            if( command->pcm[i] != 0x80 )
                audio->last_nonsilent_samples++;
        if( audio->trace )
            fprintf(
                stderr,
                "audio(null): play id=%d samples=%d rate=%d nonsilent=%d\n",
                command->sound_id,
                command->sample_count,
                command->sample_rate,
                audio->last_nonsilent_samples);
        break;
    case TORIRS_AUDIO_CMD_STOP_ALL:
        if( audio->trace )
            fprintf(stderr, "audio(null): stop\n");
        break;
    case TORIRS_AUDIO_CMD_SET_VOLUME:
        audio->volume = command->volume < 0 ? 0
                        : command->volume > TORIRS_AUDIO_VOLUME_MAX
                            ? TORIRS_AUDIO_VOLUME_MAX
                            : command->volume;
        audio->stats.volume = audio->volume;
        if( audio->trace )
            fprintf(stderr, "audio(null): volume=%d\n", audio->volume);
        break;
    case TORIRS_AUDIO_CMD_NONE:
    default:
        break;
    }
}

void
PlatformAudio_SubmitAll(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* commands,
    int count)
{
    if( !audio || !commands )
        return;
    for( int i = 0; i < count; i++ )
        PlatformAudio_Submit(audio, &commands[i]);
}

int
PlatformAudio_QueuedBytes(struct PlatformAudio* audio)
{
    (void)audio;
    return 0;
}

struct PlatformAudioStats
PlatformAudio_Stats(struct PlatformAudio* audio)
{
    struct PlatformAudioStats empty;
    if( !audio )
    {
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    return audio->stats;
}

/* --- test hooks ---------------------------------------------------------- */

/* Declared in platform_audio_null.h rather than the shared interface: only this
 * backend can answer them, and the game must not be able to ask. */

int
PlatformAudioNull_LastSoundId(struct PlatformAudio* audio)
{
    return audio ? audio->last_sound_id : -1;
}

int
PlatformAudioNull_LastSampleCount(struct PlatformAudio* audio)
{
    return audio ? audio->last_sample_count : 0;
}

int
PlatformAudioNull_LastNonSilentSamples(struct PlatformAudio* audio)
{
    return audio ? audio->last_nonsilent_samples : 0;
}
