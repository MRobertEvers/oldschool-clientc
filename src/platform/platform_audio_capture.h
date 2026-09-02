#ifndef SRC_PLATFORM_PLATFORM_AUDIO_CAPTURE_H
#define SRC_PLATFORM_PLATFORM_AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * TORIRS_AUDIO_WAV=<path> — tee every block the device is given into a WAV.
 *
 * "It sounds wrong" is not a bug report anyone can act on, and the mixer's
 * counters cannot tell a click from a clean play. This makes the actual output
 * inspectable: open it in an editor, or run a spectrum over it.
 *
 * It lives beside the backends rather than inside one because every backend
 * that owns a real device needs it and needs it identically -- and the lane
 * where it matters MOST is the one where you cannot put an editor on the
 * output: a phone.
 *
 * The write does not happen where the samples arrive. Push runs on the device's
 * real-time thread and only moves bytes into a ring; Drain runs on the frame
 * thread and does the fwrite. A capture that blocked the audio callback on a
 * filesystem would produce a recording of the underruns it caused.
 */

struct PlatformAudioCapture;

/**
 * Open the tee named by TORIRS_AUDIO_WAV.
 *
 * Returns NULL when the variable is unset — the normal case — so a caller
 * tests the handle rather than a separate "enabled" flag. `ring_seconds` sizes
 * the hand-off ring: it must cover the longest stall between two frames, and
 * costs `sample_rate * channels * 2 * ring_seconds` bytes of diagnostics-only
 * memory while the capture is open.
 */
struct PlatformAudioCapture*
PlatformAudioCapture_Open(
    int sample_rate,
    int ring_seconds);

/** Hand over one mixed block. Real-time safe: no allocation, no IO, no lock. */
void
PlatformAudioCapture_Push(
    struct PlatformAudioCapture* capture,
    const int16_t* pcm,
    int frames);

/** Write out whatever Push has handed over. Frame thread, once per frame. */
void
PlatformAudioCapture_Drain(struct PlatformAudioCapture* capture);

/** Frames Push had to throw away because Drain was not keeping up. */
int
PlatformAudioCapture_DroppedFrames(struct PlatformAudioCapture* capture);

/** Final drain, patch the header's length fields, close. NULL is fine. */
void
PlatformAudioCapture_Close(struct PlatformAudioCapture* capture);

#endif
