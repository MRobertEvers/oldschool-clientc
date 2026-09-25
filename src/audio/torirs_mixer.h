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
 * The mixer takes no locks of its own. Whether anything else is running while
 * Render does is the backend's answer, and there are two:
 *
 *   - **SDL2 and Android render on a device thread.** SDL's audio thread and
 *     OpenSL ES's callback thread call Render at the device's cadence, so the
 *     mix survives a frame stall. Each owns the exclusion the other side has to
 *     hold -- SDL2 the device lock, Android a recursive mutex -- and publishes
 *     it through PlatformAudio_Exclusion.
 *   - **Web and null render on the frame loop.** There is no second thread and
 *     nothing to exclude; their exclusion handle is zeroed.
 *
 * The rule the first group imposes is wider than "the backend locks its own
 * entry points", because Render reaches past the mixer: a pull source's
 * `render` runs inside it with the game's own ctx. So:
 *
 *   **Everything the device thread can reach is mutated only under that
 *   backend's exclusion** -- the mixer's tables, and every live pull source's
 *   state. The music player is the one that matters today; see
 *   `struct ToriRS_AudioExclusion` in torirs_audio.h.
 *
 * Two consequences worth stating, because both were bugs:
 *
 *   - Render must not allocate: it is reached from a real-time callback. The
 *     scratch it would grow is sized by ToriRS_Mixer_Reserve at Init instead.
 *   - Applying a command must not allocate *inside* the lock either, or a
 *     scene rebuild's worth of ASSET_LOADs holds the device out for every
 *     memcpy. ToriRS_Mixer_Stage does the copying first.
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

/**
 * A command's borrowed PCM, copied ahead of a backend's lock.
 *
 * ASSET_LOAD is the one command that carries a buffer, and copying it is the
 * expensive part of applying one -- a scene rebuild loads dozens of area sounds
 * at once. A backend with a device thread holds the render out for the whole
 * batch it applies, so doing the copies inside that region is an underrun
 * mechanism. Stage first, on the game thread, then apply.
 */
struct ToriRS_MixerStaged
{
    /** Owned by the caller until ApplyStaged takes it; NULL when the command
     *  carries nothing to copy. */
    int16_t* pcm;
};

/** Copy whatever `command` borrows. Call outside the lock. */
void
ToriRS_Mixer_Stage(
    const struct ToriRS_AudioCommand* command,
    struct ToriRS_MixerStaged* staged);

/** Apply, taking ownership of `staged->pcm`. Call inside the lock. */
void
ToriRS_Mixer_ApplyStaged(
    struct ToriRS_Mixer* mixer,
    const struct ToriRS_AudioCommand* command,
    int16_t* staged);

/** Release a staged copy that was never applied. */
void
ToriRS_Mixer_StageFree(struct ToriRS_MixerStaged* staged);

/**
 * Grow the render scratch to `frames` now, so Render never reallocs later.
 *
 * Call from a backend's Init, before the device thread starts, with the largest
 * block that thread will ask for. Render still grows on demand if something
 * asks for more -- this only keeps the common case off the device thread.
 */
void
ToriRS_Mixer_Reserve(
    struct ToriRS_Mixer* mixer,
    int frames);

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
