#include "platform/platform_audio.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * SDL2 audio backend.
 *
 * Deliberately the simplest thing that is correct: one queue-mode device (no
 * callback, no mixer) that clips are pushed into with SDL_QueueAudio. That is
 * what the reference does — Client3's platform_play_wave queues onto one device
 * and skips the clip if anything is still playing — and it means there is no
 * audio thread to synchronise with, which matters for the same reason it matters
 * to the renderer: everything the game does stays on one thread.
 *
 * The consequence is that sound effects do not overlap. That is authentic
 * behaviour rather than a shortcut (the 2004 client is monophonic for effects),
 * and the game's own overlap rule already decides which clip wins before it gets
 * here.
 *
 * Volume is applied on the way in rather than by the device, because an 8-bit
 * queue device has no gain: each byte is scaled about the 128 midpoint, which is
 * exactly what Client3 does.
 */

#define PLATFORM_AUDIO_MAX_CLIP_BYTES (4 * 1024 * 1024)

static bool
audio_trace(void)
{
    static int enabled = -1;
    if( enabled < 0 )
        enabled = getenv("TORIRS_AUDIO_DEBUG") != NULL ? 1 : 0;
    return enabled != 0;
}

struct PlatformAudio
{
    SDL_AudioDeviceID device;
    int sample_rate;
    int volume;
    bool owns_sdl_audio;
    struct PlatformAudioStats stats;
    /** Scratch for the volume-scaled copy; grown as needed, never per clip. */
    uint8_t* scaled;
    int scaled_capacity;
};

struct PlatformAudio*
PlatformAudio_New(void)
{
    struct PlatformAudio* audio = calloc(1, sizeof(*audio));
    assert(audio);
    audio->volume = TORIRS_AUDIO_VOLUME_MAX;
    audio->stats.volume = TORIRS_AUDIO_VOLUME_MAX;
    return audio;
}

bool
PlatformAudio_Init(
    struct PlatformAudio* audio,
    int sample_rate)
{
    SDL_AudioSpec want;

    if( !audio )
        return false;

    audio->sample_rate = sample_rate > 0 ? sample_rate : 22050;

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
    want.format = AUDIO_U8;
    want.channels = 1;
    /* Small enough that a clip starts promptly, large enough not to underrun on
     * a 50Hz game loop. */
    want.samples = 512;
    want.callback = NULL;

    audio->device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if( audio->device == 0 )
    {
        fprintf(stderr, "audio: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(audio->device, 0);
    audio->stats.device_open = true;
    return true;
}

void
PlatformAudio_Free(struct PlatformAudio* audio)
{
    if( !audio )
        return;
    if( audio->device )
        SDL_CloseAudioDevice(audio->device);
    if( audio->owns_sdl_audio )
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    free(audio->scaled);
    free(audio);
}

/** The clip, scaled to the current volume. Returns the original when no scaling
 *  is needed, or NULL if the scratch buffer could not grow. */
static const uint8_t*
apply_volume(
    struct PlatformAudio* audio,
    const uint8_t* pcm,
    int count)
{
    if( audio->volume >= TORIRS_AUDIO_VOLUME_MAX )
        return pcm;
    if( audio->scaled_capacity < count )
    {
        uint8_t* grown = realloc(audio->scaled, (size_t)count);
        if( !grown )
            return NULL;
        audio->scaled = grown;
        audio->scaled_capacity = count;
    }
    for( int i = 0; i < count; i++ )
        audio->scaled[i] =
            (uint8_t)((((int)pcm[i] - 128) * audio->volume / TORIRS_AUDIO_VOLUME_MAX) + 128);
    return audio->scaled;
}

static void
play_pcm(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* command)
{
    const uint8_t* pcm;

    audio->stats.submitted++;
    if( !audio->device || !command->pcm || command->sample_count <= 0 )
    {
        audio->stats.dropped++;
        return;
    }
    if( command->sample_count > PLATFORM_AUDIO_MAX_CLIP_BYTES )
    {
        /* A clip this long is a mis-decoded effect, not a sound. */
        fprintf(stderr, "audio: refusing %d-sample clip (id %d)\n",
                command->sample_count, command->sound_id);
        audio->stats.dropped++;
        return;
    }
    if( audio->volume <= 0 )
    {
        audio->stats.dropped++;
        return;
    }
    /* Monophonic, like the reference: a clip arriving while another is still
     * playing is dropped rather than mixed. */
    if( SDL_GetQueuedAudioSize(audio->device) > 0 )
    {
        audio->stats.dropped++;
        return;
    }

    pcm = apply_volume(audio, command->pcm, command->sample_count);
    if( !pcm )
    {
        audio->stats.dropped++;
        return;
    }
    if( SDL_QueueAudio(audio->device, pcm, (uint32_t)command->sample_count) != 0 )
    {
        fprintf(stderr, "audio: SDL_QueueAudio: %s\n", SDL_GetError());
        audio->stats.dropped++;
        return;
    }
    audio->stats.played++;
    if( audio_trace() )
        fprintf(
            stderr,
            "audio(sdl2): queued id=%d samples=%d rate=%d volume=%d\n",
            command->sound_id,
            command->sample_count,
            command->sample_rate,
            audio->volume);
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
        play_pcm(audio, command);
        break;
    case TORIRS_AUDIO_CMD_STOP_ALL:
        if( audio->device )
            SDL_ClearQueuedAudio(audio->device);
        break;
    case TORIRS_AUDIO_CMD_SET_VOLUME:
        audio->volume = command->volume < 0 ? 0
                        : command->volume > TORIRS_AUDIO_VOLUME_MAX
                            ? TORIRS_AUDIO_VOLUME_MAX
                            : command->volume;
        audio->stats.volume = audio->volume;
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
    if( !audio || !audio->device )
        return 0;
    return (int)SDL_GetQueuedAudioSize(audio->device);
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
