#ifndef SRC_PLATFORM_PLATFORM_AUDIO_H
#define SRC_PLATFORM_PLATFORM_AUDIO_H

#include "audio/torirs_audio.h"

#include <stdbool.h>

/*
 * Audio backend interface — the platform half of audio/torirs_audio.h.
 *
 * One of the platform_audio_*.c files provides these; the host picks at build
 * time, the game never sees them. All of them are free to do less than asked:
 * dropping a sound because the device is busy is normal (the reference client
 * does exactly that), and reporting it honestly through PlatformAudio_Stats is
 * more useful than queueing sounds that will play late.
 *
 *   platform_audio_sdl2.c  native (macOS/Linux/Windows) via SDL2's audio queue
 *   platform_audio_null.c  headless and tests: records what was asked, plays
 *                          nothing. Also the fallback when a device fails to
 *                          open, so a machine with no sound card still runs.
 *   platform_audio_wasm.c  Emscripten/WebAudio. Compiled only under
 *                          __EMSCRIPTEN__, and no WebAssembly build of src/
 *                          exists yet — it is here so this interface is the one
 *                          a web host will implement, not a native-only shape
 *                          that has to be renegotiated later.
 */

struct PlatformAudio;

struct PlatformAudioStats
{
    /** PLAY_PCM commands submitted. */
    int submitted;
    /** ...of those, ones the backend actually started. */
    int played;
    /** ...and ones it dropped (device busy, too large, no device). */
    int dropped;
    int volume;
    /** False when no device was opened — the null backend, or a failed open. */
    bool device_open;
};

struct PlatformAudio*
PlatformAudio_New(void);

/**
 * Open a device for `sample_rate` mono 8-bit playback.
 *
 * Returns false when no device could be opened. That is not fatal for the
 * caller: Submit stays safe to call and simply drops, so a machine with no audio
 * behaves like the null backend rather than crashing.
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

/**
 * Apply a whole drained batch.
 *
 * The host's one call per frame — the loop is here so every host does it the
 * same way, and so a backend that can batch (submitting one mixed buffer rather
 * than N) has a place to.
 */
void
PlatformAudio_SubmitAll(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* commands,
    int count);

/**
 * Bytes still queued for playback, or 0 when nothing is playing.
 *
 * The game uses this for nothing — its overlap rule is computed from the clip
 * lengths it submitted, exactly as the reference does — so a backend that cannot
 * answer may return 0.
 */
int
PlatformAudio_QueuedBytes(struct PlatformAudio* audio);

struct PlatformAudioStats
PlatformAudio_Stats(struct PlatformAudio* audio);

#endif
