#ifndef SRC_AUDIO_TORIRS_MIXER_H
#define SRC_AUDIO_TORIRS_MIXER_H

#include "audio/torirs_audio.h"
#include "audio/torirs_pcm.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * The software mixer every backend runs.
 *
 * Backends differ in how they get PCM to a device -- SDL2 queues it, WebAudio
 * posts it, the null backend counts it -- but they do not differ in what the
 * mix should be. So the retained asset table, the voice pool, the stream rings
 * and the bus gains live here, once, and a backend is reduced to "open a
 * device, call Render, hand the block over".
 *
 * That also makes the mix testable. `make -C src test-audio` drives this
 * directly with no device at all and inspects the samples it produces, which is
 * the only way to check a fade, a loop point, or an asset lifetime without
 * listening to it.
 *
 * ## Threading
 *
 * None. Render is called from the frame loop, like everything else in this
 * client; there is no audio thread and no lock. A backend that needs a callback
 * thread must own the synchronisation itself and is expected not to -- SDL2
 * queue mode exists precisely so it does not have to.
 *
 * ## Lifetimes
 *
 * Assets own their PCM copy. Unloading one stops every voice on it first, so
 * there is never a voice pointing at freed samples; that ordering is the whole
 * reason ASSET_UNLOAD is a command rather than a direct free. `ToriRS_Mixer_Free`
 * releases everything, and `ToriRS_Mixer_LiveAssetCount` lets a shutdown path
 * assert the game unloaded what it loaded.
 */

#define TORIRS_MIXER_MAX_ASSETS 512
#define TORIRS_MIXER_MAX_VOICES 64
#define TORIRS_MIXER_MAX_STREAMS TORIRS_AUDIO_STREAM_MAX
/** ~1.5s of stereo at 22050. Music only needs a few frames of slack; the extra
 *  room is what lets a stalled frame not become a gap. */
#define TORIRS_MIXER_STREAM_FRAMES 32768

struct ToriRS_MixerAsset
{
    int asset_id; /* -1 when the slot is free */
    struct ToriRS_PcmSound sound;
    int16_t* samples; /* owned copy; NULL for a generator */
    /**
     * A generator-backed asset: the mixer pulls its PCM instead of holding it.
     *
     * This is how music is an asset at all. A four-minute track is ~21MB
     * rendered, so it cannot be resident -- but everything else about it is
     * asset-shaped, so the difference is confined to where the samples come
     * from. `render` is NULL for an ordinary resident asset.
     *
     * A generator is stateful, so unlike resident PCM it backs exactly one
     * voice at a time; a second VOICE_START on it is rejected.
     */
    struct ToriRS_AudioSource source;
};

struct ToriRS_MixerVoice
{
    int voice_id; /* -1 when the slot is free */
    int asset_id;
    enum ToriRS_AudioBus bus;
    struct ToriRS_PcmVoice voice;
    /** For logs: which cache effect this is. */
    int source_id;
    /** Playing a generator asset: pull each block rather than read samples. */
    bool generator;
};

struct ToriRS_MixerStream
{
    int stream_id; /* -1 when closed */
    enum ToriRS_AudioBus bus;
    int channels;
    int sample_rate;
    int16_t* ring; /* TORIRS_MIXER_STREAM_FRAMES * channels */
    int write_index;
    int read_index;
    int buffered_frames;
    /** Own gain, ramped: this is how a song fades without touching the bus.
     *  Held in 8.8 fixed point so a multi-second fade does not quantise into
     *  audible steps -- at 22050Hz a 3s fade is 66,150 frames and an integer
     *  gain would move once every 260 of them. */
    int gain_fixed;
    int target_gain;
    int gain_step;
    int gain_frames;
    int dropped_frames;
    int starved_frames;
};

struct ToriRS_MixerStats
{
    int assets_live;
    int assets_loaded;
    int assets_unloaded;
    int voices_live;
    int voices_started;
    int voices_stolen;
    int voices_rejected;
    int frames_rendered;
    int stream_dropped_frames;
    int stream_starved_frames;
    int bus_volume[TORIRS_AUDIO_BUS_COUNT];
};

struct ToriRS_Mixer
{
    int sample_rate;
    struct ToriRS_MixerAsset assets[TORIRS_MIXER_MAX_ASSETS];
    struct ToriRS_MixerVoice voices[TORIRS_MIXER_MAX_VOICES];
    struct ToriRS_MixerStream streams[TORIRS_MIXER_MAX_STREAMS];
    int bus_volume[TORIRS_AUDIO_BUS_COUNT];
    int32_t* accumulator; /* frames * 2 int32, grown on demand */
    int accumulator_frames;
    /** Staging for a pull source's block, grown on demand like the
     *  accumulator. Shared across streams: sources render one at a time. */
    int16_t* source_scratch;
    int source_scratch_frames;
    struct ToriRS_MixerStats stats;
};

void
ToriRS_Mixer_Init(
    struct ToriRS_Mixer* mixer,
    int sample_rate);

/** Free every asset, voice and stream. Safe on a zeroed mixer. */
void
ToriRS_Mixer_Free(struct ToriRS_Mixer* mixer);

/** Apply one drained command. */
void
ToriRS_Mixer_Apply(
    struct ToriRS_Mixer* mixer,
    const struct ToriRS_AudioCommand* command);

void
ToriRS_Mixer_ApplyAll(
    struct ToriRS_Mixer* mixer,
    const struct ToriRS_AudioCommand* commands,
    int count);

/**
 * Render `frames` frames of interleaved 16-bit stereo into `out`.
 *
 * `out` must hold `frames * 2` samples. Voices that end during the block are
 * reaped. Always fills the whole block -- silence where there is nothing to
 * play -- so a backend can hand it straight to a device.
 */
void
ToriRS_Mixer_Render(
    struct ToriRS_Mixer* mixer,
    int16_t* out,
    int frames);

/** Fill `feedback` with per-stream headroom so the game knows how much music
 *  to synthesise this frame. */
void
ToriRS_Mixer_Feedback(
    const struct ToriRS_Mixer* mixer,
    struct ToriRS_AudioFeedback* feedback);

int
ToriRS_Mixer_LiveAssetCount(const struct ToriRS_Mixer* mixer);

int
ToriRS_Mixer_LiveVoiceCount(const struct ToriRS_Mixer* mixer);

struct ToriRS_MixerStats
ToriRS_Mixer_Stats(const struct ToriRS_Mixer* mixer);

/** True when TORIRS_AUDIO_TRACE is set — shared by the mixer and the backends
 *  so one switch turns on the whole audio path's tracing. */
bool
ToriRS_AudioTraceEnabled(void);

#endif
