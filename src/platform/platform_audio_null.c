#include "platform/platform_audio_null.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Silent audio backend.
 *
 * Two jobs. It is what headless runs and tests build against -- every command
 * is accounted for and the mix is still *computed*, so a test can assert that
 * what the game asked for would actually have been audible without owning a
 * sound card. And it is the honest answer when a device cannot be opened: the
 * game keeps running and keeps reporting what it wanted to play.
 *
 * It runs the same `ToriRS_Mixer` every other backend does. Mixing and throwing
 * the result away costs a fraction of a millisecond and is what makes the silent
 * path exercise the same code the audible one does -- a null backend that
 * skipped the mix would let a mixer crash reach only the machines with speakers.
 *
 * ## Exactly one block per Update, and why it stays that way
 *
 * This is the third of the three clock shapes, not a worse copy of one of the
 * other two:
 *
 *   - SDL2 and Android are *pulled* by a device thread at its own cadence.
 *   - WebAudio is *pushed* from the frame loop and has to refill to a lead,
 *     because how much it owes depends on how long the last frame took.
 *   - This one is *stepped*. There is no device and therefore no clock to keep
 *     up with, so "how much does it owe" has no answer -- and the tests are the
 *     caller. `PlatformAudioNull_LastPeak` and `LastVoiceSource` report the
 *     block this Update rendered, and rs_audio_test counts Updates to decide
 *     when a clip became audible. Refilling to a lead here would make the peak
 *     "the last of however many blocks" and shift every frame count in the
 *     suite, for no gain: nothing is listening.
 *
 * So the fixed block below is deliberate. `PlatformAudioStats.underruns` is
 * structurally zero for the same reason -- there is no device to starve.
 */

/** Frames rendered per Update, mirroring a 50Hz tick at the mixer's rate. */
#define NULL_AUDIO_FRAMES_PER_UPDATE (TORIRS_AUDIO_SAMPLE_RATE / 50)

struct PlatformAudio
{
    int sample_rate;
    struct ToriRS_Mixer mixer;
    int commands;
    int frames_played;
    int16_t block[NULL_AUDIO_FRAMES_PER_UPDATE * TORIRS_AUDIO_CHANNELS];
    /** Summary of the most recent rendered block, for tests. */
    int last_peak;
    long last_energy;
    int last_voice_start_source;
    bool trace;
};

struct PlatformAudio*
PlatformAudio_New(void)
{
    struct PlatformAudio* audio = calloc(1, sizeof(*audio));
    assert(audio);
    audio->sample_rate = TORIRS_AUDIO_SAMPLE_RATE;
    ToriRS_Mixer_Init(&audio->mixer, audio->sample_rate);
    audio->last_voice_start_source = -1;
    audio->trace = ToriRS_AudioTraceEnabled();
    return audio;
}

bool
PlatformAudio_Init(
    struct PlatformAudio* audio,
    int sample_rate)
{
    assert(audio);
    audio->sample_rate = sample_rate > 0 ? sample_rate : TORIRS_AUDIO_SAMPLE_RATE;
    ToriRS_Mixer_Init(&audio->mixer, audio->sample_rate);
    /* Deliberately false: there is no device. The game uses this to skip
     * synthesising music nobody will hear. */
    return false;
}

void
PlatformAudio_Free(struct PlatformAudio* audio)
{
    if( !audio )
        return;
    ToriRS_Mixer_Free(&audio->mixer);
    free(audio);
}

void
PlatformAudio_Submit(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* command)
{
    assert(audio);
    assert(command);
    audio->commands++;
    if( command->kind == TORIRS_AUDIO_CMD_VOICE_START )
        audio->last_voice_start_source = command->source_id;
    ToriRS_Mixer_Apply(&audio->mixer, command);
}

void
PlatformAudio_SubmitAll(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* commands,
    int count)
{
    assert(audio);
    assert(commands);
    for( int i = 0; i < count; i++ )
        PlatformAudio_Submit(audio, &commands[i]);
}

void
PlatformAudio_Update(struct PlatformAudio* audio)
{
    int peak = 0;
    long energy = 0;

    assert(audio);
    ToriRS_Mixer_Render(&audio->mixer, audio->block, NULL_AUDIO_FRAMES_PER_UPDATE);
    for( int i = 0; i < NULL_AUDIO_FRAMES_PER_UPDATE * TORIRS_AUDIO_CHANNELS; i++ )
    {
        int magnitude = audio->block[i] < 0 ? -audio->block[i] : audio->block[i];
        energy += magnitude;
        if( magnitude > peak )
            peak = magnitude;
    }
    audio->last_peak = peak;
    audio->last_energy = energy;
    audio->frames_played += NULL_AUDIO_FRAMES_PER_UPDATE;
}

int
PlatformAudio_BlockFrames(struct PlatformAudio* audio)
{
    return audio ? NULL_AUDIO_FRAMES_PER_UPDATE : 0;
}

struct ToriRS_AudioExclusion
PlatformAudio_Exclusion(struct PlatformAudio* audio)
{
    struct ToriRS_AudioExclusion exclusion;

    /*
     * Nothing to exclude: this backend renders from the frame loop, on the same
     * thread the game mutates from. The zeroed handle's acquire and release are
     * no-ops, so the game locks unconditionally and pays nothing here.
     */
    (void)audio;
    memset(&exclusion, 0, sizeof(exclusion));
    return exclusion;
}

void
PlatformAudio_Feedback(
    struct PlatformAudio* audio,
    struct ToriRS_AudioFeedback* out)
{
    assert(out);
    memset(out, 0, sizeof(*out));
    if( !audio )
        return;
    ToriRS_Mixer_Feedback(&audio->mixer, out);
    /*
     * Reported closed even though the mix runs. The game reads this to decide
     * whether to synthesise music, and there is no reason to spend a
     * millisecond a frame on a MIDI render that will be discarded -- while a
     * test that *wants* the render drives ToriRS_MidiSynth directly.
     */
    out->device_open = false;
}

struct PlatformAudioStats
PlatformAudio_Stats(struct PlatformAudio* audio)
{
    struct PlatformAudioStats stats;
    struct ToriRS_MixerStats mixer_stats;

    memset(&stats, 0, sizeof(stats));
    assert(audio);
    mixer_stats = ToriRS_Mixer_Stats(&audio->mixer);
    stats.commands = audio->commands;
    stats.assets_live = mixer_stats.assets_live;
    stats.voices_live = mixer_stats.voices_live;
    stats.voices_started = mixer_stats.voices_started;
    stats.voices_stolen = mixer_stats.voices_stolen;
    stats.voices_rejected = mixer_stats.voices_rejected;
    stats.frames_played = audio->frames_played;
    stats.stream_dropped_frames = mixer_stats.stream_dropped_frames;
    stats.stream_starved_frames = mixer_stats.stream_starved_frames;
    for( int i = 0; i < TORIRS_AUDIO_BUS_COUNT; i++ )
        stats.bus_volume[i] = mixer_stats.bus_volume[i];
    stats.device_open = false;
    return stats;
}

int
PlatformAudio_LiveAssetCount(struct PlatformAudio* audio)
{
    return audio ? ToriRS_Mixer_LiveAssetCount(&audio->mixer) : 0;
}

/* --- inspection hooks ----------------------------------------------------- */

int
PlatformAudioNull_LastVoiceSource(struct PlatformAudio* audio)
{
    return audio ? audio->last_voice_start_source : -1;
}

int
PlatformAudioNull_LastPeak(struct PlatformAudio* audio)
{
    return audio ? audio->last_peak : 0;
}

long
PlatformAudioNull_LastEnergy(struct PlatformAudio* audio)
{
    return audio ? audio->last_energy : 0;
}
