#ifndef SRC_GAME_RS_AUDIO_H
#define SRC_GAME_RS_AUDIO_H

#include "audio/torirs_audio.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * The game's sound queue — reference Client.soundsDoQueue / SYNTH_SOUND.
 *
 * The server does not say "play now": it says "play effect N, looped L times,
 * after D ticks". So this holds a small queue of pending plays, counts each one
 * down on the client tick, and on expiry pushes a play command onto the platform
 * queue built from the provider's cached PCM. Nothing here touches an audio
 * device; see audio/torirs_audio.h for why that split exists.
 *
 * Two things make this more than a countdown:
 *
 *   - **Effects load asynchronously.** The reference unpacks the whole sound bank
 *     at login, so `Wave.delays[id]` is known the moment a packet arrives. Here a
 *     sound may not be resident yet, and its trim lead-in
 *     (ToriRS_Sound.queue_delay) is part of what the server's delay is relative
 *     to. An entry therefore waits — without counting down — until its effect is
 *     resident, and only then starts the reference's countdown. An entry whose
 *     effect never arrives is dropped after RS_AUDIO_LOAD_WAIT_TICKS rather than
 *     waiting forever.
 *   - **The overlap rule.** The reference refuses to interrupt a longer sound
 *     with a shorter one: a clip only plays if it would still be sounding when
 *     the previous one finishes. That decision is made here, from the lengths
 *     submitted, so no backend has to be asked what it is playing.
 *
 * Song and jingle packets are recorded but not played: there is no MIDI decoder
 * behind them yet (see the note in audio/torirs_audio.h). The fields exist so the
 * packets are not silently discarded and so headless tests can assert they
 * arrived.
 */

/** Reference cap: waveIds/waveLoops/waveDelay are 50 long and the packet
 *  handler drops anything past that. */
#define RS_AUDIO_QUEUE_MAX 50

/** How long an entry waits for its effect to become resident, in client ticks
 *  (20ms each). 100 = two seconds, far longer than a cache read, so hitting it
 *  means the effect is absent or undecodable rather than slow. */
#define RS_AUDIO_LOAD_WAIT_TICKS 100

/** Reference default (Client.waveVolume). */
#define RS_AUDIO_DEFAULT_VOLUME 64

struct CacheProvider;
struct TaskRunner;

struct RS_AudioEntry
{
    int sound_id;
    int loops;
    /** Ticks until this plays. Only counted down once the effect is resident. */
    int delay;
    /** Set once ToriRS_Sound.queue_delay has been folded into `delay`. */
    bool delay_resolved;
    /** Set once a load has been asked for, so it is asked for exactly once. */
    bool load_requested;
    /** Ticks spent waiting for residency. */
    int waited;
};

struct RS_Audio
{
    struct RS_AudioEntry queue[RS_AUDIO_QUEUE_MAX];
    int count;

    /** Reference waveEnabled / waveVolume (0..128). */
    bool enabled;
    int volume;

    /* Overlap rule state (reference lastWave*). Times are client ticks. */
    int last_play_tick;
    int last_play_length_ticks;
    int last_sound_id;
    int last_loops;

    /** Monotonic client tick, advanced by RS_Audio_Tick. */
    int tick;

    /* Music, recorded only — see the header comment. */
    int current_song; /* -1 = none */
    int jingle_id;    /* -1 = none */
    int jingle_delay;

    /* Counters, for tests and TORIRS_AUDIO_DEBUG. */
    int played;
    int dropped_overlap;
    int dropped_absent;
    int dropped_queue_full;

    /** PCM handed to the platform on the last drain. Owned here and freed on the
     *  next tick: the interface lends the bytes until the following drain, and
     *  loop expansion means they cannot simply be the provider's copy. */
    uint8_t* lent[TORIRS_AUDIO_QUEUE_MAX];
    int lent_count;
};

void
RS_Audio_Init(struct RS_Audio* audio);

/** Release the PCM buffers still lent to the platform. */
void
RS_Audio_Shutdown(struct RS_Audio* audio);

/**
 * SYNTH_SOUND: queue effect `synth_id`, looped `loops` times, after `delay`
 * ticks. Silently ignored when sound is disabled or the queue is full, which is
 * what the reference does.
 */
void
RS_Audio_Synth(
    struct RS_Audio* audio,
    int synth_id,
    int loops,
    int delay);

void
RS_Audio_Song(
    struct RS_Audio* audio,
    int song_id);

void
RS_Audio_Jingle(
    struct RS_Audio* audio,
    int jingle_id,
    int delay);

/**
 * Advance one client tick: request loads, count entries down, and push play
 * commands for whatever came due.
 *
 * `provider` and `runner` may be NULL (no cache yet), in which case entries just
 * wait. `out` may be NULL to run the queue with nothing listening.
 */
void
RS_Audio_Tick(
    struct RS_Audio* audio,
    struct CacheProvider* provider,
    struct TaskRunner* runner,
    struct ToriRS_AudioQueue* out);

/**
 * Apply varp clientcode 4 (reference Client.updateVarp): values 0..3 select
 * volume 128/96/64/32 and enable sound; 4 disables it. Pushes a SET_VOLUME
 * command. Unknown values are ignored.
 */
void
RS_Audio_SetVolumeLevel(
    struct RS_Audio* audio,
    int level,
    struct ToriRS_AudioQueue* out);

#endif
