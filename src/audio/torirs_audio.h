#ifndef SRC_AUDIO_TORIRS_AUDIO_H
#define SRC_AUDIO_TORIRS_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The platform <-> game audio interface.
 *
 * The game never touches an audio device. It decides *what* should be heard and
 * puts a command on a queue; the host drains that queue once per frame and hands
 * each command to whichever backend it built (src/platform/platform_audio.h).
 * Same split as rendering, where the game builds a ToriRS_Frame and the host
 * gives it to a renderer — and for the same reason: the game has to run in
 * places that cannot own an audio device, from a headless test to a WebAssembly
 * page where playback may only start inside a user gesture.
 *
 * Everything crossing this boundary is plain data:
 *
 *   - PCM is 8-bit unsigned mono (128 = silence), the format the cache's synth
 *     renders to and a RIFF/WAVE payload as-is. Backends convert if they must,
 *     and none has to parse a container.
 *   - Sample buffers are *borrowed*. A drained command's `pcm` stays valid until
 *     the next drain, which is long enough for a backend to copy or submit it.
 *     Nothing here allocates on the audio path.
 *
 * Deliberately not in this interface: music. Song and jingle packets are
 * recorded by the game (see game/rs_audio.h) but there is no MIDI decoder behind
 * them yet, and an enum value the host can only refuse would be worse than its
 * absence. When there is one, this is where it goes.
 */

#define TORIRS_AUDIO_VOLUME_MAX 128

/**
 * Rate to open a device at.
 *
 * Every cache era renders its sound effects at 22050Hz mono (the reference's
 * AUDIO_SAMPLE_RATE, and RSCACHE_SOUND_SAMPLE_RATE), so this is the rate a
 * backend will actually be fed. Each command still carries its own
 * `sample_rate`, so a backend must not assume this one — it is the device
 * default, not a contract.
 */
#define TORIRS_AUDIO_SAMPLE_RATE 22050

enum ToriRS_AudioCommandKind
{
    TORIRS_AUDIO_CMD_NONE = 0,
    /** Play a PCM clip once, now. Clips may overlap or drop — see the backend. */
    TORIRS_AUDIO_CMD_PLAY_PCM,
    /** Silence anything currently playing and drop anything queued. */
    TORIRS_AUDIO_CMD_STOP_ALL,
    /** Set the effect volume, 0..TORIRS_AUDIO_VOLUME_MAX. */
    TORIRS_AUDIO_CMD_SET_VOLUME,
};

struct ToriRS_AudioCommand
{
    enum ToriRS_AudioCommandKind kind;

    /* PLAY_PCM */
    /** Borrowed until the next drain. 8-bit unsigned mono. */
    const uint8_t* pcm;
    int sample_count;
    int sample_rate;
    int channels;
    /** Which cache effect this is, for logs and tests. -1 when not from one. */
    int sound_id;

    /* SET_VOLUME */
    int volume;
};

/** Ring of pending commands. Sized well past a frame's worth: the reference
 *  caps its own sound queue at 50 and plays at most a handful per tick. */
#define TORIRS_AUDIO_QUEUE_MAX 32

struct ToriRS_AudioQueue
{
    struct ToriRS_AudioCommand commands[TORIRS_AUDIO_QUEUE_MAX];
    int count;
    /** Commands dropped because the queue was full since the last drain. A
     *  non-zero value means the host is not draining, which is a wiring bug
     *  rather than an audio one — so it is counted rather than ignored. */
    int dropped;
};

static inline void
ToriRS_AudioQueue_Reset(struct ToriRS_AudioQueue* queue)
{
    queue->count = 0;
    queue->dropped = 0;
}

/** Returns false when the queue is full (and counts the drop). */
static inline bool
ToriRS_AudioQueue_Push(
    struct ToriRS_AudioQueue* queue,
    const struct ToriRS_AudioCommand* command)
{
    if( !queue || !command )
        return false;
    if( queue->count >= TORIRS_AUDIO_QUEUE_MAX )
    {
        queue->dropped++;
        return false;
    }
    queue->commands[queue->count++] = *command;
    return true;
}

/**
 * Copy up to `max` commands out and clear the queue.
 *
 * The queue is emptied whether or not everything fitted: a command the host had
 * no room for is a command it will never take, and keeping it would play a sound
 * a frame or more late. Returns how many were written.
 */
static inline int
ToriRS_AudioQueue_Drain(
    struct ToriRS_AudioQueue* queue,
    struct ToriRS_AudioCommand* out,
    int max)
{
    int taken;

    if( !queue || !out || max <= 0 )
        return 0;
    taken = queue->count < max ? queue->count : max;
    for( int i = 0; i < taken; i++ )
        out[i] = queue->commands[i];
    queue->count = 0;
    return taken;
}

#endif
