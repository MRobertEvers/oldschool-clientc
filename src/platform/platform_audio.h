#ifndef SRC_PLATFORM_PLATFORM_AUDIO_H
#define SRC_PLATFORM_PLATFORM_AUDIO_H

#include "audio/torirs_audio.h"
#include "audio/torirs_mixer.h"

#include <stdbool.h>

/*
 * Audio backend interface — the platform half of audio/torirs_audio.h.
 *
 * One of the platform_audio_*.c files provides these; the host picks at build
 * time, the game never sees them.
 *
 * A backend is deliberately thin. The mix -- the retained asset table, the
 * voices, the stream rings, the bus gains -- is `ToriRS_Mixer`, shared by every
 * backend, so what is left here is opening a device and getting a block of
 * finished PCM to it. That split is what makes the audio path testable: the
 * mixer is driven directly by `make -C src test-audio` with no device at all.
 *
 *   platform_audio_sdl2.c  native (macOS/Linux/Windows) via SDL2's audio queue
 *   platform_audio_null.c  headless and tests: mixes, plays nothing, and keeps
 *                          the rendered block so a test can look at it. Also the
 *                          fallback when a device fails to open, so a machine
 *                          with no sound card still runs.
 *   platform_audio_wasm.c  Emscripten/WebAudio.
 *
 * ## The frame contract
 *
 * Once per frame the host does three things, in this order:
 *
 *   1. PlatformAudio_Feedback  — how much room the music stream has
 *   2. (the game runs, and queues commands)
 *   3. PlatformAudio_SubmitAll — apply them
 *   4. PlatformAudio_Update    — mix and push to the device
 *
 * Step 1 has to come first because the game decides how much music to
 * synthesise from it. Step 4 has to come last because it is what consumes what
 * step 3 queued.
 */

struct PlatformAudio;

struct PlatformAudioStats
{
    /** Commands applied since open. */
    int commands;
    /** Assets currently resident in the backend. */
    int assets_live;
    /** Voices currently sounding. */
    int voices_live;
    int voices_started;
    /** Voices displaced because the pool was full. */
    int voices_stolen;
    /** Voice starts that named an asset the backend does not have. */
    int voices_rejected;
    /** Frames the device has been given. */
    int frames_played;
    /** Frames of music dropped because the ring was full. */
    int stream_dropped_frames;
    /** Frames of silence emitted because the ring was empty. */
    int stream_starved_frames;
    int bus_volume[TORIRS_AUDIO_BUS_COUNT];
    /** False when no device was opened — the null backend, or a failed open. */
    bool device_open;

    /** Real-time path diagnostics (SDL2 backend only, zero elsewhere). */
    int updates;
    int underruns;
    int queue_min_frames;
    int queue_max_frames;
    int queue_current_frames;
    double update_interval_min_ms;
    double update_interval_max_ms;
    double update_interval_mean_ms;
    double render_max_ms;
};

struct PlatformAudio*
PlatformAudio_New(void);

/**
 * Open a device for `sample_rate` stereo 16-bit playback.
 *
 * Returns false when no device could be opened. That is not fatal for the
 * caller: everything below stays safe to call and simply drops, so a machine
 * with no audio behaves like the null backend rather than crashing.
 */
bool
PlatformAudio_Init(
    struct PlatformAudio* audio,
    int sample_rate);

void
PlatformAudio_Free(struct PlatformAudio* audio);

/** Apply one drained command. Safe on a NULL/unopened backend. */
void
PlatformAudio_Submit(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* command);

void
PlatformAudio_SubmitAll(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* commands,
    int count);

/**
 * Mix and hand the device whatever it needs to stay ahead.
 *
 * Called once per frame. A backend that is behind renders more; one that is
 * comfortably buffered renders nothing.
 */
void
PlatformAudio_Update(struct PlatformAudio* audio);

/** What the game needs to know before it decides how much music to render. */
void
PlatformAudio_Feedback(
    struct PlatformAudio* audio,
    struct ToriRS_AudioFeedback* out);

struct PlatformAudioStats
PlatformAudio_Stats(struct PlatformAudio* audio);

/**
 * Assets the backend still holds.
 *
 * A shutdown path asserts on this: every asset the game loaded should have been
 * unloaded, and a non-zero count at exit is a leak in the game's own
 * bookkeeping rather than in the backend's.
 */
int
PlatformAudio_LiveAssetCount(struct PlatformAudio* audio);

#endif
