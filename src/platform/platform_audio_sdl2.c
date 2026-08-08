#include "platform/platform_audio.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * SDL2 audio backend.
 *
 * Queue mode, not a callback: `PlatformAudio_Update` mixes on the frame loop's
 * thread and pushes the block with SDL_QueueAudio. There is no audio thread and
 * therefore no lock, which matters for the same reason it matters to the
 * renderer -- everything the game does stays on one thread, and a mixer that
 * could be re-entered while the game is loading an asset would need one.
 *
 * The cost is latency: the device runs behind by whatever is queued. The target
 * below is what bounds it.
 */

/** Aim to keep this much queued. Below it the device can underrun on a slow
 *  frame; far above it a volume change is audibly late because everything
 *  already queued plays first. 80ms is about four frames. */
#define AUDIO_TARGET_QUEUED_MS 80
/** Never render more than this in one call, so a stall does not turn into a
 *  multi-second render that stalls the next frame too. */
#define AUDIO_MAX_RENDER_MS 200

/*
 * TORIRS_AUDIO_WAV=<path> tees every block the device is given into a WAV.
 *
 * "It sounds wrong" is not a bug report anyone can act on, and the mixer's
 * counters cannot tell a click from a clean play. This makes the actual output
 * inspectable: open it in an editor, or run a spectrum over it. The header's
 * length fields are patched on close.
 */
struct PlatformAudio
{
    SDL_AudioDeviceID device;
    FILE* capture;
    long capture_frames;
    int sample_rate;
    bool owns_sdl_audio;
    bool device_open;
    struct ToriRS_Mixer mixer;
    int16_t* block;
    int block_frames;
    int commands;
    int frames_played;
};

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

    if( !audio )
        return false;

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
    /* Small enough that a clip starts promptly, large enough not to underrun on
     * a 50Hz game loop. */
    want.samples = 512;
    want.callback = NULL;

    audio->device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if( audio->device == 0 )
    {
        fprintf(stderr, "audio: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return false;
    }
    if( have.freq != audio->sample_rate || have.channels != TORIRS_AUDIO_CHANNELS )
    {
        /* We asked for no conversion; if SDL gave us something else the mixer
         * would be producing the wrong rate. Say so rather than play it fast. */
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
    SDL_PauseAudioDevice(audio->device, 0);
    audio->device_open = true;

    if( getenv("TORIRS_AUDIO_WAV") )
    {
        audio->capture = fopen(getenv("TORIRS_AUDIO_WAV"), "wb");
        if( audio->capture )
        {
            unsigned char header[44] = { 0 };
            memcpy(header, "RIFF", 4);
            memcpy(header + 8, "WAVEfmt ", 8);
            header[16] = 16;
            header[20] = 1;
            header[22] = TORIRS_AUDIO_CHANNELS;
            header[24] = (unsigned char)(audio->sample_rate & 0xFF);
            header[25] = (unsigned char)((audio->sample_rate >> 8) & 0xFF);
            header[26] = (unsigned char)((audio->sample_rate >> 16) & 0xFF);
            {
                int byte_rate = audio->sample_rate * TORIRS_AUDIO_CHANNELS * 2;
                header[28] = (unsigned char)(byte_rate & 0xFF);
                header[29] = (unsigned char)((byte_rate >> 8) & 0xFF);
                header[30] = (unsigned char)((byte_rate >> 16) & 0xFF);
            }
            header[32] = TORIRS_AUDIO_CHANNELS * 2;
            header[34] = 16;
            memcpy(header + 36, "data", 4);
            fwrite(header, 1, sizeof(header), audio->capture);
            fprintf(stderr, "audio: capturing to %s\n", getenv("TORIRS_AUDIO_WAV"));
        }
    }
    return true;
}

void
PlatformAudio_Free(struct PlatformAudio* audio)
{
    if( !audio )
        return;
    if( audio->capture )
    {
        /* Patch the two length fields now that the total is known. */
        uint32_t data_bytes = (uint32_t)(audio->capture_frames * TORIRS_AUDIO_CHANNELS * 2);
        uint32_t riff_size = 36 + data_bytes;
        fseek(audio->capture, 4, SEEK_SET);
        fwrite(&riff_size, 4, 1, audio->capture);
        fseek(audio->capture, 40, SEEK_SET);
        fwrite(&data_bytes, 4, 1, audio->capture);
        fclose(audio->capture);
    }
    if( audio->device )
        SDL_CloseAudioDevice(audio->device);
    if( audio->owns_sdl_audio )
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    ToriRS_Mixer_Free(&audio->mixer);
    free(audio->block);
    free(audio);
}

void
PlatformAudio_Submit(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* command)
{
    if( !audio || !command )
        return;
    audio->commands++;
    ToriRS_Mixer_Apply(&audio->mixer, command);
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

static bool
ensure_block(
    struct PlatformAudio* audio,
    int frames)
{
    int16_t* grown;

    if( audio->block_frames >= frames )
        return true;
    grown = realloc(audio->block, (size_t)frames * TORIRS_AUDIO_CHANNELS * sizeof(int16_t));
    if( !grown )
        return false;
    audio->block = grown;
    audio->block_frames = frames;
    return true;
}

void
PlatformAudio_Update(struct PlatformAudio* audio)
{
    int queued_frames;
    int target_frames;
    int wanted;

    if( !audio || !audio->device_open || !audio->device )
        return;

    queued_frames =
        (int)(SDL_GetQueuedAudioSize(audio->device) / (TORIRS_AUDIO_CHANNELS * sizeof(int16_t)));
    target_frames = audio->sample_rate * AUDIO_TARGET_QUEUED_MS / 1000;
    wanted = target_frames - queued_frames;
    if( wanted <= 0 )
        return;
    if( wanted > audio->sample_rate * AUDIO_MAX_RENDER_MS / 1000 )
        wanted = audio->sample_rate * AUDIO_MAX_RENDER_MS / 1000;
    if( !ensure_block(audio, wanted) )
        return;

    ToriRS_Mixer_Render(&audio->mixer, audio->block, wanted);
    if( SDL_QueueAudio(
            audio->device,
            audio->block,
            (uint32_t)wanted * TORIRS_AUDIO_CHANNELS * (uint32_t)sizeof(int16_t)) != 0 )
    {
        fprintf(stderr, "audio: SDL_QueueAudio: %s\n", SDL_GetError());
        return;
    }
    audio->frames_played += wanted;
    if( audio->capture )
    {
        fwrite(audio->block, 2, (size_t)wanted * TORIRS_AUDIO_CHANNELS, audio->capture);
        audio->capture_frames += wanted;
    }
}

void
PlatformAudio_Feedback(
    struct PlatformAudio* audio,
    struct ToriRS_AudioFeedback* out)
{
    if( !out )
        return;
    memset(out, 0, sizeof(*out));
    if( !audio )
        return;
    ToriRS_Mixer_Feedback(&audio->mixer, out);
    out->device_open = audio->device_open;
}

struct PlatformAudioStats
PlatformAudio_Stats(struct PlatformAudio* audio)
{
    struct PlatformAudioStats stats;
    struct ToriRS_MixerStats mixer_stats;

    memset(&stats, 0, sizeof(stats));
    if( !audio )
        return stats;
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
    return stats;
}

int
PlatformAudio_LiveAssetCount(struct PlatformAudio* audio)
{
    return audio ? ToriRS_Mixer_LiveAssetCount(&audio->mixer) : 0;
}
