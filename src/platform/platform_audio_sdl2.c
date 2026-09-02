#include "platform/platform_audio.h"

#include "platform/platform_audio_capture.h"

#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * SDL2 audio backend — callback mode.
 *
 * The audio callback mixes directly into the device buffer from the audio
 * thread. The game thread submits commands and reads feedback under
 * SDL_LockAudioDevice, which serialises with the callback — no manual lock,
 * no data race, and the mix never stops for a loading stall on the frame
 * loop.
 *
 * The earlier queue-mode design pushed rendered blocks with SDL_QueueAudio
 * from the frame thread. That was simple but tied audio continuity to frame
 * pacing: a 160ms loading spike drained the queue and produced a gap. Callback
 * mode lets the mixer run at the device's own cadence.
 */

/* TORIRS_AUDIO_WAV, in platform_audio_capture.c -- the same tee the Android
 * backend uses. Diagnostics-only memory (~10 MiB stereo) that survives a long
 * live boot frame without dropping the very samples being investigated. */
#define AUDIO_CAPTURE_RING_SECONDS 120

struct PlatformAudio
{
    SDL_AudioDeviceID device;
    struct PlatformAudioCapture* capture;
    int sample_rate;
    bool owns_sdl_audio;
    bool device_open;
    struct ToriRS_Mixer mixer;
    int commands;
    int frames_played;

    int callbacks;
    int callback_underruns;
    uint64_t last_callback_tick;
    double callback_interval_min_ms;
    double callback_interval_max_ms;
    double callback_interval_sum_ms;
    double callback_period_ms;
    double callback_jitter_max_ms;
    double render_max_ms;

    int stream_ring_samples;
    int stream_ring_min_frames;
    int stream_ring_max_frames;
    uint64_t stream_ring_sum_frames;
    int stream_ring_current_frames;
};

static void
sample_stream_ring(struct PlatformAudio* audio)
{
    int buffered = 0;
    bool active = false;

    for( int i = 0; i < TORIRS_MIXER_MAX_STREAMS; i++ )
    {
        if( audio->mixer.streams[i].stream_id < 0 )
            continue;
        buffered += audio->mixer.streams[i].buffered_frames;
        active = true;
    }
    if( !active )
        return;
    if( audio->stream_ring_samples == 0 || buffered < audio->stream_ring_min_frames )
        audio->stream_ring_min_frames = buffered;
    if( buffered > audio->stream_ring_max_frames )
        audio->stream_ring_max_frames = buffered;
    audio->stream_ring_current_frames = buffered;
    audio->stream_ring_sum_frames += (uint64_t)buffered;
    audio->stream_ring_samples++;
}

static void
audio_callback(
    void* userdata,
    Uint8* stream,
    int len)
{
    struct PlatformAudio* audio = userdata;
    int frames = len / (int)(TORIRS_AUDIO_CHANNELS * sizeof(int16_t));
    uint64_t now = SDL_GetPerformanceCounter();
    uint64_t render_start;
    double render_ms;

    if( audio->last_callback_tick != 0 )
    {
        double interval_ms =
            (double)(now - audio->last_callback_tick) * 1000.0 /
            (double)SDL_GetPerformanceFrequency();
        if( audio->callbacks > 1 )
        {
            if( interval_ms < audio->callback_interval_min_ms )
                audio->callback_interval_min_ms = interval_ms;
            if( interval_ms > audio->callback_interval_max_ms )
                audio->callback_interval_max_ms = interval_ms;
        }
        else
        {
            audio->callback_interval_min_ms = interval_ms;
            audio->callback_interval_max_ms = interval_ms;
        }
        audio->callback_interval_sum_ms += interval_ms;
        if( audio->callback_period_ms > 0.0 )
        {
            double jitter_ms = fabs(interval_ms - audio->callback_period_ms);
            if( jitter_ms > audio->callback_jitter_max_ms )
                audio->callback_jitter_max_ms = jitter_ms;
            if( interval_ms > audio->callback_period_ms * 1.5 )
            {
                int missed =
                    (int)floor(interval_ms / audio->callback_period_ms + 0.5) - 1;
                audio->callback_underruns += missed > 0 ? missed : 1;
            }
        }
    }
    audio->last_callback_tick = now;
    audio->callbacks++;

    sample_stream_ring(audio);
    render_start = SDL_GetPerformanceCounter();
    ToriRS_Mixer_Render(&audio->mixer, (int16_t*)stream, frames);
    render_ms =
        (double)(SDL_GetPerformanceCounter() - render_start) * 1000.0 /
        (double)SDL_GetPerformanceFrequency();
    if( render_ms > audio->render_max_ms )
        audio->render_max_ms = render_ms;

    audio->frames_played += frames;

    if( audio->capture )
        PlatformAudioCapture_Push(audio->capture, (const int16_t*)stream, frames);
}

struct PlatformAudio*
PlatformAudio_New(void)
{
    struct PlatformAudio* audio = calloc(1, sizeof(*audio));
    assert(audio);
    ToriRS_Mixer_Init(&audio->mixer, TORIRS_AUDIO_SAMPLE_RATE);
    return audio;
}

bool
PlatformAudio_Init(
    struct PlatformAudio* audio,
    int sample_rate)
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;

    assert(audio);

    audio->sample_rate = sample_rate > 0 ? sample_rate : TORIRS_AUDIO_SAMPLE_RATE;
    ToriRS_Mixer_Init(&audio->mixer, audio->sample_rate);

    if( SDL_WasInit(SDL_INIT_AUDIO) == 0 )
    {
        if( SDL_InitSubSystem(SDL_INIT_AUDIO) != 0 )
        {
            fprintf(stderr, "audio: SDL_InitSubSystem(AUDIO): %s\n", SDL_GetError());
            return false;
        }
        audio->owns_sdl_audio = true;
    }

    memset(&want, 0, sizeof(want));
    want.freq = audio->sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = TORIRS_AUDIO_CHANNELS;
    want.samples = 1024;
    want.callback = audio_callback;
    want.userdata = audio;

    audio->device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if( audio->device == 0 )
    {
        fprintf(stderr, "audio: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return false;
    }
    if( have.freq != audio->sample_rate || have.channels != TORIRS_AUDIO_CHANNELS )
    {
        fprintf(
            stderr,
            "audio: device opened at %dHz x%d, wanted %dHz x%d\n",
            have.freq,
            have.channels,
            audio->sample_rate,
            TORIRS_AUDIO_CHANNELS);
        audio->sample_rate = have.freq;
        ToriRS_Mixer_Init(&audio->mixer, audio->sample_rate);
    }
    fprintf(stderr, "audio: callback mode, %d-sample buffer @ %dHz\n",
            have.samples, have.freq);
    audio->callback_period_ms = (double)have.samples * 1000.0 / (double)have.freq;

    audio->capture =
        PlatformAudioCapture_Open(audio->sample_rate, AUDIO_CAPTURE_RING_SECONDS);
    /* Grow the mixer's accumulator before the real-time thread starts. */
    {
        int16_t* warmup = calloc((size_t)have.samples * TORIRS_AUDIO_CHANNELS, sizeof(int16_t));
        assert(warmup);
        ToriRS_Mixer_Render(&audio->mixer, warmup, have.samples);
        free(warmup);
    }
    SDL_PauseAudioDevice(audio->device, 0);
    audio->device_open = true;
    return true;
}

void
PlatformAudio_Free(struct PlatformAudio* audio)
{
    if( !audio )
        return;
    if( audio->device )
    {
        SDL_PauseAudioDevice(audio->device, 1);
        SDL_CloseAudioDevice(audio->device);
        audio->device = 0;
    }
    PlatformAudioCapture_Close(audio->capture);
    if( audio->owns_sdl_audio )
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
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
    if( audio->device )
        SDL_LockAudioDevice(audio->device);
    audio->commands++;
    ToriRS_Mixer_Apply(&audio->mixer, command);
    if( audio->device )
        SDL_UnlockAudioDevice(audio->device);
}

void
PlatformAudio_SubmitAll(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* commands,
    int count)
{
    assert(audio);
    assert(commands);
    if( audio->device )
        SDL_LockAudioDevice(audio->device);
    for( int i = 0; i < count; i++ )
    {
        audio->commands++;
        ToriRS_Mixer_Apply(&audio->mixer, &commands[i]);
    }
    if( audio->device )
        SDL_UnlockAudioDevice(audio->device);
}

void
PlatformAudio_Update(struct PlatformAudio* audio)
{
    assert(audio);
    if( audio->capture )
        PlatformAudioCapture_Drain(audio->capture);
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
    if( audio->device )
        SDL_LockAudioDevice(audio->device);
    ToriRS_Mixer_Feedback(&audio->mixer, out);
    out->device_open = audio->device_open;
    if( audio->device )
        SDL_UnlockAudioDevice(audio->device);
}

struct PlatformAudioStats
PlatformAudio_Stats(struct PlatformAudio* audio)
{
    struct PlatformAudioStats stats;
    struct ToriRS_MixerStats mixer_stats;

    memset(&stats, 0, sizeof(stats));
    assert(audio);
    if( audio->device )
        SDL_LockAudioDevice(audio->device);
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
    stats.device_open = audio->device_open;
    stats.updates = audio->callbacks;
    stats.underruns = audio->callback_underruns;
    stats.queue_min_frames =
        audio->stream_ring_samples > 0 ? audio->stream_ring_min_frames : 0;
    stats.queue_max_frames = audio->stream_ring_max_frames;
    stats.queue_current_frames = audio->stream_ring_current_frames;
    stats.queue_mean_frames = audio->stream_ring_samples > 0
                                  ? (double)audio->stream_ring_sum_frames /
                                        (double)audio->stream_ring_samples
                                  : 0.0;
    stats.update_interval_min_ms =
        audio->callbacks > 1 ? audio->callback_interval_min_ms : 0.0;
    stats.update_interval_max_ms = audio->callback_interval_max_ms;
    stats.update_interval_mean_ms =
        audio->callbacks > 1
            ? audio->callback_interval_sum_ms / (double)(audio->callbacks - 1)
            : 0.0;
    stats.callback_period_ms = audio->callback_period_ms;
    stats.callback_jitter_max_ms = audio->callback_jitter_max_ms;
    stats.render_max_ms = audio->render_max_ms;
    stats.capture_dropped_frames =
        audio->capture ? PlatformAudioCapture_DroppedFrames(audio->capture) : 0;
    if( audio->device )
        SDL_UnlockAudioDevice(audio->device);
    return stats;
}

int
PlatformAudio_LiveAssetCount(struct PlatformAudio* audio)
{
    return audio ? ToriRS_Mixer_LiveAssetCount(&audio->mixer) : 0;
}
