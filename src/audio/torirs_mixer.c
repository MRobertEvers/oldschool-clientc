#include "audio/torirs_mixer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool
ToriRS_AudioTraceEnabled(void)
{
    static int enabled = -1;
    if( enabled < 0 )
        enabled = (getenv("TORIRS_AUDIO_TRACE") || getenv("TORIRS_AUDIO_DEBUG")) ? 1 : 0;
    return enabled != 0;
}

#define MIXER_TRACE(...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( ToriRS_AudioTraceEnabled() )                                                           \
            fprintf(stderr, __VA_ARGS__);                                                          \
    } while( 0 )

void
ToriRS_Mixer_Init(
    struct ToriRS_Mixer* mixer,
    int sample_rate)
{
    if( !mixer )
        return;
    memset(mixer, 0, sizeof(*mixer));
    mixer->sample_rate = sample_rate > 0 ? sample_rate : TORIRS_AUDIO_SAMPLE_RATE;
    for( int i = 0; i < TORIRS_MIXER_MAX_ASSETS; i++ )
        mixer->assets[i].asset_id = -1;
    for( int i = 0; i < TORIRS_MIXER_MAX_VOICES; i++ )
        mixer->voices[i].voice_id = -1;
    for( int i = 0; i < TORIRS_MIXER_MAX_STREAMS; i++ )
        mixer->streams[i].stream_id = -1;
    for( int i = 0; i < TORIRS_AUDIO_BUS_COUNT; i++ )
        mixer->bus_volume[i] = TORIRS_AUDIO_VOLUME_MAX;
}

static void
free_asset_slot(struct ToriRS_MixerAsset* asset)
{
    free(asset->samples);
    memset(asset, 0, sizeof(*asset));
    asset->asset_id = -1;
}

static void
free_stream_slot(struct ToriRS_MixerStream* stream)
{
    free(stream->ring);
    memset(stream, 0, sizeof(*stream));
    stream->stream_id = -1;
}

void
ToriRS_Mixer_Free(struct ToriRS_Mixer* mixer)
{
    if( !mixer )
        return;
    for( int i = 0; i < TORIRS_MIXER_MAX_ASSETS; i++ )
        free_asset_slot(&mixer->assets[i]);
    for( int i = 0; i < TORIRS_MIXER_MAX_STREAMS; i++ )
        free_stream_slot(&mixer->streams[i]);
    free(mixer->accumulator);
    mixer->accumulator = NULL;
    mixer->accumulator_frames = 0;
    for( int i = 0; i < TORIRS_MIXER_MAX_VOICES; i++ )
        mixer->voices[i].voice_id = -1;
}

static struct ToriRS_MixerAsset*
find_asset(
    struct ToriRS_Mixer* mixer,
    int asset_id)
{
    if( asset_id < 0 )
        return NULL;
    for( int i = 0; i < TORIRS_MIXER_MAX_ASSETS; i++ )
    {
        if( mixer->assets[i].asset_id == asset_id )
            return &mixer->assets[i];
    }
    return NULL;
}

static struct ToriRS_MixerVoice*
find_voice(
    struct ToriRS_Mixer* mixer,
    int voice_id)
{
    if( voice_id == TORIRS_AUDIO_VOICE_ANON )
        return NULL;
    for( int i = 0; i < TORIRS_MIXER_MAX_VOICES; i++ )
    {
        if( mixer->voices[i].voice_id == voice_id && mixer->voices[i].voice.active )
            return &mixer->voices[i];
    }
    return NULL;
}

static struct ToriRS_MixerStream*
find_stream(
    struct ToriRS_Mixer* mixer,
    int stream_id)
{
    if( stream_id < 0 )
        return NULL;
    for( int i = 0; i < TORIRS_MIXER_MAX_STREAMS; i++ )
    {
        if( mixer->streams[i].stream_id == stream_id )
            return &mixer->streams[i];
    }
    return NULL;
}

/** Stop and free every voice playing `asset_id`. */
static void
stop_voices_on_asset(
    struct ToriRS_Mixer* mixer,
    int asset_id)
{
    for( int i = 0; i < TORIRS_MIXER_MAX_VOICES; i++ )
    {
        if( mixer->voices[i].voice.active && mixer->voices[i].asset_id == asset_id )
        {
            mixer->voices[i].voice.active = false;
            mixer->voices[i].voice.sound = NULL;
            mixer->voices[i].voice_id = -1;
        }
    }
}

static void
asset_load(
    struct ToriRS_Mixer* mixer,
    const struct ToriRS_AudioCommand* command)
{
    struct ToriRS_MixerAsset* slot;
    int16_t* copy;

    if( command->asset_id < 0 || !command->pcm || command->sample_count <= 0 )
        return;

    copy = malloc((size_t)command->sample_count * sizeof(int16_t));
    if( !copy )
        return;
    memcpy(copy, command->pcm, (size_t)command->sample_count * sizeof(int16_t));

    slot = find_asset(mixer, command->asset_id);
    if( slot )
    {
        /* Replacing in place: every voice on the old samples has to go before
         * the samples do. */
        stop_voices_on_asset(mixer, command->asset_id);
        free(slot->samples);
    }
    else
    {
        for( int i = 0; i < TORIRS_MIXER_MAX_ASSETS; i++ )
        {
            if( mixer->assets[i].asset_id < 0 )
            {
                slot = &mixer->assets[i];
                break;
            }
        }
        if( !slot )
        {
            free(copy);
            MIXER_TRACE("audio: asset table full, dropping asset %d\n", command->asset_id);
            return;
        }
        mixer->stats.assets_live++;
    }

    slot->asset_id = command->asset_id;
    slot->samples = copy;
    slot->sound.samples = copy;
    slot->sound.sample_count = command->sample_count;
    slot->sound.sample_rate = command->sample_rate > 0 ? command->sample_rate : mixer->sample_rate;
    slot->sound.loop_start = command->loop_start;
    slot->sound.loop_end = command->loop_end;
    slot->sound.ping_pong = false;
    mixer->stats.assets_loaded++;
    MIXER_TRACE(
        "audio: asset %d loaded (%d samples @%dHz, loop %d..%d), %d live\n",
        command->asset_id,
        command->sample_count,
        slot->sound.sample_rate,
        command->loop_start,
        command->loop_end,
        mixer->stats.assets_live);
}

static void
asset_unload(
    struct ToriRS_Mixer* mixer,
    int asset_id)
{
    struct ToriRS_MixerAsset* slot = find_asset(mixer, asset_id);

    if( !slot )
        return;
    stop_voices_on_asset(mixer, asset_id);
    free_asset_slot(slot);
    mixer->stats.assets_live--;
    mixer->stats.assets_unloaded++;
    MIXER_TRACE("audio: asset %d unloaded, %d live\n", asset_id, mixer->stats.assets_live);
}

static void
asset_clear(struct ToriRS_Mixer* mixer)
{
    for( int i = 0; i < TORIRS_MIXER_MAX_VOICES; i++ )
    {
        mixer->voices[i].voice.active = false;
        mixer->voices[i].voice.sound = NULL;
        mixer->voices[i].voice_id = -1;
    }
    for( int i = 0; i < TORIRS_MIXER_MAX_ASSETS; i++ )
    {
        if( mixer->assets[i].asset_id >= 0 )
        {
            free_asset_slot(&mixer->assets[i]);
            mixer->stats.assets_unloaded++;
        }
    }
    mixer->stats.assets_live = 0;
    MIXER_TRACE("audio: all assets cleared\n");
}

/**
 * Pick a slot for a new voice.
 *
 * Prefers a free slot, then the same voice id (a restart), then steals the
 * quietest live voice. Stealing rather than refusing matches the reference,
 * which drops sounds silently when the mixer is saturated -- a missing effect
 * is better than a missing *newer* effect, because the newest one is the one
 * the player just caused.
 */
static struct ToriRS_MixerVoice*
acquire_voice(
    struct ToriRS_Mixer* mixer,
    int voice_id)
{
    struct ToriRS_MixerVoice* quietest = NULL;
    int quietest_gain = 0;

    if( voice_id != TORIRS_AUDIO_VOICE_ANON )
    {
        struct ToriRS_MixerVoice* existing = find_voice(mixer, voice_id);
        if( existing )
            return existing;
    }
    for( int i = 0; i < TORIRS_MIXER_MAX_VOICES; i++ )
    {
        if( !mixer->voices[i].voice.active )
            return &mixer->voices[i];
    }
    for( int i = 0; i < TORIRS_MIXER_MAX_VOICES; i++ )
    {
        int gain = mixer->voices[i].voice.left_gain + mixer->voices[i].voice.right_gain;
        if( !quietest || gain < quietest_gain )
        {
            quietest = &mixer->voices[i];
            quietest_gain = gain;
        }
    }
    mixer->stats.voices_stolen++;
    return quietest;
}

static void
voice_start(
    struct ToriRS_Mixer* mixer,
    const struct ToriRS_AudioCommand* command)
{
    struct ToriRS_MixerAsset* asset = find_asset(mixer, command->asset_id);
    struct ToriRS_MixerVoice* slot;
    int rate_numerator;

    if( !asset )
    {
        mixer->stats.voices_rejected++;
        MIXER_TRACE(
            "audio: voice %d wants absent asset %d\n", command->voice_id, command->asset_id);
        return;
    }
    slot = acquire_voice(mixer, command->voice_id);
    if( !slot )
        return;

    /* Playback rate combines the asset's own rate with the requested pitch. */
    rate_numerator = (int)(((int64_t)asset->sound.sample_rate * command->rate) /
                           (int64_t)TORIRS_AUDIO_RATE_UNITY);
    slot->voice_id = command->voice_id;
    slot->asset_id = command->asset_id;
    slot->bus = command->bus >= 0 && command->bus < TORIRS_AUDIO_BUS_COUNT
                    ? command->bus
                    : TORIRS_AUDIO_BUS_EFFECTS;
    slot->source_id = command->source_id;
    ToriRS_PcmVoice_Start(
        &slot->voice,
        &asset->sound,
        rate_numerator,
        mixer->sample_rate,
        command->volume << TORIRS_PCM_VOLUME_SHIFT,
        command->pan,
        command->loop_count);
    mixer->stats.voices_started++;
    MIXER_TRACE(
        "audio: voice %d start asset %d (source %d) vol %d pan %d loops %d bus %d\n",
        command->voice_id,
        command->asset_id,
        command->source_id,
        command->volume,
        command->pan,
        command->loop_count,
        (int)slot->bus);
}

/** Milliseconds to frames at the mixer's rate. */
static int
fade_frames(
    const struct ToriRS_Mixer* mixer,
    int fade_ms)
{
    if( fade_ms <= 0 )
        return 0;
    return (int)(((int64_t)fade_ms * mixer->sample_rate) / 1000);
}

void
ToriRS_Mixer_Apply(
    struct ToriRS_Mixer* mixer,
    const struct ToriRS_AudioCommand* command)
{
    if( !mixer || !command )
        return;

    switch( command->kind )
    {
    case TORIRS_AUDIO_CMD_ASSET_LOAD:
        asset_load(mixer, command);
        break;
    case TORIRS_AUDIO_CMD_ASSET_UNLOAD:
        asset_unload(mixer, command->asset_id);
        break;
    case TORIRS_AUDIO_CMD_ASSET_CLEAR:
        asset_clear(mixer);
        break;

    case TORIRS_AUDIO_CMD_VOICE_START:
        voice_start(mixer, command);
        break;
    case TORIRS_AUDIO_CMD_VOICE_UPDATE:
    {
        struct ToriRS_MixerVoice* slot = find_voice(mixer, command->voice_id);
        if( !slot )
            break; /* already finished; not an error */
        ToriRS_PcmVoice_SetGain(
            &slot->voice,
            command->volume << TORIRS_PCM_VOLUME_SHIFT,
            command->pan,
            fade_frames(mixer, command->fade_ms));
        if( command->rate > 0 && slot->voice.sound )
            ToriRS_PcmVoice_SetRate(
                &slot->voice,
                (int)(((int64_t)slot->voice.sound->sample_rate * command->rate) /
                      TORIRS_AUDIO_RATE_UNITY),
                mixer->sample_rate);
        break;
    }
    case TORIRS_AUDIO_CMD_VOICE_STOP:
    {
        struct ToriRS_MixerVoice* slot = find_voice(mixer, command->voice_id);
        if( !slot )
            break;
        ToriRS_PcmVoice_Stop(&slot->voice, fade_frames(mixer, command->fade_ms));
        if( !slot->voice.active )
            slot->voice_id = -1;
        MIXER_TRACE("audio: voice %d stop (fade %dms)\n", command->voice_id, command->fade_ms);
        break;
    }

    case TORIRS_AUDIO_CMD_STREAM_OPEN:
    {
        struct ToriRS_MixerStream* stream = find_stream(mixer, command->stream_id);
        int channels = command->channels == 2 ? 2 : 1;

        if( !stream )
        {
            for( int i = 0; i < TORIRS_MIXER_MAX_STREAMS; i++ )
            {
                if( mixer->streams[i].stream_id < 0 )
                {
                    stream = &mixer->streams[i];
                    break;
                }
            }
        }
        if( !stream )
            break;
        free(stream->ring);
        memset(stream, 0, sizeof(*stream));
        stream->ring =
            calloc((size_t)TORIRS_MIXER_STREAM_FRAMES * (size_t)channels, sizeof(int16_t));
        if( !stream->ring )
        {
            stream->stream_id = -1;
            break;
        }
        stream->stream_id = command->stream_id;
        stream->channels = channels;
        stream->sample_rate =
            command->sample_rate > 0 ? command->sample_rate : mixer->sample_rate;
        stream->bus = command->bus >= 0 && command->bus < TORIRS_AUDIO_BUS_COUNT
                          ? command->bus
                          : TORIRS_AUDIO_BUS_MUSIC;
        stream->gain_fixed = command->volume << 8;
        stream->target_gain = command->volume;
        MIXER_TRACE(
            "audio: stream %d open (%d ch @%dHz, gain %d)\n",
            command->stream_id,
            channels,
            stream->sample_rate,
            stream->gain_fixed >> 8);
        break;
    }
    case TORIRS_AUDIO_CMD_STREAM_PUSH:
    {
        struct ToriRS_MixerStream* stream = find_stream(mixer, command->stream_id);
        int frames;

        if( !stream || !stream->ring || !command->pcm || command->frame_count <= 0 )
            break;
        frames = command->frame_count;
        if( frames > TORIRS_MIXER_STREAM_FRAMES - stream->buffered_frames )
        {
            int dropped = frames - (TORIRS_MIXER_STREAM_FRAMES - stream->buffered_frames);
            frames = TORIRS_MIXER_STREAM_FRAMES - stream->buffered_frames;
            stream->dropped_frames += dropped;
            mixer->stats.stream_dropped_frames += dropped;
        }
        for( int i = 0; i < frames; i++ )
        {
            for( int c = 0; c < stream->channels; c++ )
                stream->ring[stream->write_index * stream->channels + c] =
                    command->pcm[i * stream->channels + c];
            stream->write_index = (stream->write_index + 1) % TORIRS_MIXER_STREAM_FRAMES;
        }
        stream->buffered_frames += frames;
        break;
    }
    case TORIRS_AUDIO_CMD_STREAM_CLOSE:
    {
        struct ToriRS_MixerStream* stream = find_stream(mixer, command->stream_id);
        if( !stream )
            break;
        MIXER_TRACE("audio: stream %d close\n", command->stream_id);
        free_stream_slot(stream);
        break;
    }
    case TORIRS_AUDIO_CMD_STREAM_VOLUME:
    {
        struct ToriRS_MixerStream* stream = find_stream(mixer, command->stream_id);
        int frames = fade_frames(mixer, command->fade_ms);
        if( !stream )
            break;
        stream->target_gain = command->volume;
        if( frames <= 0 )
        {
            stream->gain_fixed = command->volume << 8;
            stream->gain_frames = 0;
        }
        else
        {
            stream->gain_frames = frames;
            stream->gain_step = ((command->volume << 8) - stream->gain_fixed) / frames;
        }
        break;
    }

    case TORIRS_AUDIO_CMD_BUS_VOLUME:
        if( command->target_bus >= 0 && command->target_bus < TORIRS_AUDIO_BUS_COUNT )
        {
            int volume = command->volume;
            if( volume < 0 )
                volume = 0;
            if( volume > TORIRS_AUDIO_VOLUME_MAX )
                volume = TORIRS_AUDIO_VOLUME_MAX;
            mixer->bus_volume[command->target_bus] = volume;
            mixer->stats.bus_volume[command->target_bus] = volume;
            MIXER_TRACE("audio: bus %d volume %d\n", (int)command->target_bus, volume);
        }
        break;

    case TORIRS_AUDIO_CMD_STOP_ALL:
        for( int i = 0; i < TORIRS_MIXER_MAX_VOICES; i++ )
        {
            mixer->voices[i].voice.active = false;
            mixer->voices[i].voice.sound = NULL;
            mixer->voices[i].voice_id = -1;
        }
        for( int i = 0; i < TORIRS_MIXER_MAX_STREAMS; i++ )
        {
            struct ToriRS_MixerStream* stream = &mixer->streams[i];
            if( stream->stream_id < 0 )
                continue;
            stream->buffered_frames = 0;
            stream->read_index = 0;
            stream->write_index = 0;
        }
        MIXER_TRACE("audio: stop all\n");
        break;

    case TORIRS_AUDIO_CMD_NONE:
    default:
        break;
    }
}

void
ToriRS_Mixer_ApplyAll(
    struct ToriRS_Mixer* mixer,
    const struct ToriRS_AudioCommand* commands,
    int count)
{
    if( !mixer || !commands )
        return;
    for( int i = 0; i < count; i++ )
        ToriRS_Mixer_Apply(mixer, &commands[i]);
}

static bool
ensure_accumulator(
    struct ToriRS_Mixer* mixer,
    int frames)
{
    int32_t* grown;

    if( mixer->accumulator_frames >= frames )
        return true;
    grown = realloc(mixer->accumulator, (size_t)frames * 2 * sizeof(int32_t));
    if( !grown )
        return false;
    mixer->accumulator = grown;
    mixer->accumulator_frames = frames;
    return true;
}

/** Mix one stream's buffered frames in, applying its ramped gain and bus. */
static void
render_stream(
    struct ToriRS_Mixer* mixer,
    struct ToriRS_MixerStream* stream,
    int32_t* accumulator,
    int frames)
{
    int bus_gain = mixer->bus_volume[stream->bus];

    for( int i = 0; i < frames; i++ )
    {
        int left;
        int right;
        int gain;

        if( stream->gain_frames > 0 )
        {
            stream->gain_fixed += stream->gain_step;
            if( --stream->gain_frames == 0 )
                stream->gain_fixed = stream->target_gain << 8;
        }
        gain = stream->gain_fixed >> 8;
        if( gain < 0 )
            gain = 0;
        if( gain > TORIRS_AUDIO_VOLUME_MAX )
            gain = TORIRS_AUDIO_VOLUME_MAX;

        if( stream->buffered_frames <= 0 )
        {
            stream->starved_frames++;
            mixer->stats.stream_starved_frames++;
            continue;
        }
        if( stream->channels == 2 )
        {
            left = stream->ring[stream->read_index * 2];
            right = stream->ring[stream->read_index * 2 + 1];
        }
        else
        {
            left = stream->ring[stream->read_index];
            right = left;
        }
        stream->read_index = (stream->read_index + 1) % TORIRS_MIXER_STREAM_FRAMES;
        stream->buffered_frames--;

        accumulator[i * 2] += (left * gain * bus_gain) / (255 * 255);
        accumulator[i * 2 + 1] += (right * gain * bus_gain) / (255 * 255);
    }
}

void
ToriRS_Mixer_Render(
    struct ToriRS_Mixer* mixer,
    int16_t* out,
    int frames)
{
    int32_t* accumulator;

    if( !mixer || !out || frames <= 0 )
        return;
    if( !ensure_accumulator(mixer, frames) )
    {
        memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }
    accumulator = mixer->accumulator;
    memset(accumulator, 0, (size_t)frames * 2 * sizeof(int32_t));

    for( int i = 0; i < TORIRS_MIXER_MAX_VOICES; i++ )
    {
        struct ToriRS_MixerVoice* slot = &mixer->voices[i];
        int bus_gain;

        if( !slot->voice.active )
            continue;
        bus_gain = mixer->bus_volume[slot->bus];
        slot->voice.bus_gain = bus_gain * TORIRS_PCM_BUS_ONE / TORIRS_AUDIO_VOLUME_MAX;
        if( bus_gain <= 0 )
        {
            /* Muted, but still ageing: a one-shot muted mid-flight must still
             * end, or its slot never comes back. */
            ToriRS_PcmVoice_Skip(&slot->voice, frames);
        }
        else
        {
            ToriRS_PcmVoice_Mix(&slot->voice, accumulator, frames);
        }
        if( !slot->voice.active )
        {
            slot->voice_id = -1;
            slot->voice.sound = NULL;
        }
    }

    for( int i = 0; i < TORIRS_MIXER_MAX_STREAMS; i++ )
    {
        if( mixer->streams[i].stream_id >= 0 && mixer->streams[i].ring )
            render_stream(mixer, &mixer->streams[i], accumulator, frames);
    }

    for( int i = 0; i < frames * 2; i++ )
    {
        int32_t value = accumulator[i];
        if( value > 32767 )
            value = 32767;
        if( value < -32768 )
            value = -32768;
        out[i] = (int16_t)value;
    }
    mixer->stats.frames_rendered += frames;
}

void
ToriRS_Mixer_Feedback(
    const struct ToriRS_Mixer* mixer,
    struct ToriRS_AudioFeedback* feedback)
{
    if( !feedback )
        return;
    memset(feedback, 0, sizeof(*feedback));
    if( !mixer )
        return;
    feedback->device_open = true;
    /* Feedback precedes STREAM_OPEN by a frame. Advertise the capacity a new
     * stream will have so OPEN and its first PUSH can land in the same tick;
     * zero here otherwise guarantees one empty callback at every song start. */
    for( int i = 0; i < 4; i++ )
        feedback->stream_headroom[i] = TORIRS_MIXER_STREAM_FRAMES;
    for( int i = 0; i < TORIRS_MIXER_MAX_STREAMS && i < 4; i++ )
    {
        const struct ToriRS_MixerStream* stream = &mixer->streams[i];
        if( stream->stream_id < 0 )
            continue;
        feedback->stream_buffered[stream->stream_id & 3] = stream->buffered_frames;
        feedback->stream_headroom[stream->stream_id & 3] =
            TORIRS_MIXER_STREAM_FRAMES - stream->buffered_frames;
    }
}

int
ToriRS_Mixer_LiveAssetCount(const struct ToriRS_Mixer* mixer)
{
    int count = 0;

    if( !mixer )
        return 0;
    for( int i = 0; i < TORIRS_MIXER_MAX_ASSETS; i++ )
    {
        if( mixer->assets[i].asset_id >= 0 )
            count++;
    }
    return count;
}

int
ToriRS_Mixer_LiveVoiceCount(const struct ToriRS_Mixer* mixer)
{
    int count = 0;

    if( !mixer )
        return 0;
    for( int i = 0; i < TORIRS_MIXER_MAX_VOICES; i++ )
    {
        if( mixer->voices[i].voice.active )
            count++;
    }
    return count;
}

struct ToriRS_MixerStats
ToriRS_Mixer_Stats(const struct ToriRS_Mixer* mixer)
{
    struct ToriRS_MixerStats empty;

    if( !mixer )
    {
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    empty = mixer->stats;
    empty.assets_live = ToriRS_Mixer_LiveAssetCount(mixer);
    empty.voices_live = ToriRS_Mixer_LiveVoiceCount(mixer);
    return empty;
}
