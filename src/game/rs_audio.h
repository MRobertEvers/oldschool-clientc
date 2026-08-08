#ifndef SRC_GAME_RS_AUDIO_H
#define SRC_GAME_RS_AUDIO_H

#include "audio/torirs_audio.h"
#include "audio/torirs_music.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * The game's audio layer: everything between "the game decided something should
 * be heard" and a command on the platform queue.
 *
 * Four sources of sound, and they are genuinely different problems:
 *
 *   effects   the server says "play effect N, looped L times, in D ticks"
 *             (SYNTH_SOUND), and so do sequence frame sounds and scripts. A
 *             small queue counts each down on the client tick and starts a
 *             one-shot voice when it comes due.
 *   area      locs in the built scene emit continuously. Nothing tells the
 *             client to start them -- it walks the scene it just built (see
 *             World_AreaSound) and owns a looping voice per emitter, whose
 *             volume and pan follow the camera every tick.
 *   music     a song id, synthesised from a MIDI track and a cache soundbank
 *             into a stream. See audio/torirs_music.h.
 *   jingles   a short track that interrupts the song and hands back.
 *
 * ## Assets
 *
 * Clips are ToriDraw_Scene assets. The load path is: the cache task makes a
 * ToriRS_Sound resident in the provider; this layer promotes it into the scene
 * (widening the RS2-era 8-bit render to 16-bit) exactly once; the scene emits a
 * load event; this layer drains that event into an ASSET_LOAD command. From
 * then on a play is a VOICE_START naming the id, and the PCM never crosses the
 * boundary again.
 *
 * The drain has to happen on the client tick, before the render frame consumes
 * and clears the same event queue. That ordering is why RS_Audio_Tick is called
 * from the logic tick rather than from the frame's audio drain.
 *
 * ## Volumes
 *
 * Three independent settings -- effects, music, area -- each mapped onto a
 * mixer bus. The reference has three sliders and the game reads them from
 * different varps, so collapsing them into one number here would make the
 * settings panel unimplementable.
 */

struct CacheProvider;
struct TaskRunner;
struct ToriDraw_Scene;
struct World;

/** Reference cap: waveIds/waveLoops/waveDelay are 50 long and the packet
 *  handler drops anything past that. */
#define RS_AUDIO_QUEUE_MAX 50

/** How long a queued effect waits for its clip, in client ticks (20ms each).
 *  100 = two seconds, far longer than a cache read, so hitting it means the
 *  effect is absent or undecodable rather than slow. */
#define RS_AUDIO_LOAD_WAIT_TICKS 100

/** Reference default (Client.waveVolume), on the 0..255 scale used here. */
#define RS_AUDIO_DEFAULT_VOLUME 128

/** Concurrent area-sound emitters. Beyond this the nearest win. */
#define RS_AUDIO_MAX_AREA_VOICES 12

struct RS_AudioEntry
{
    int sound_id;
    int loops;
    /** Ticks until this plays. Only counted down once the clip is resident. */
    int delay;
    /** Set once ToriDraw_Sound.queue_delay has been folded into `delay`. */
    bool delay_resolved;
    /** Set once a load has been asked for, so it is asked for exactly once. */
    bool load_requested;
    int waited;
    /** Where it should sound from, in scene tiles; -1 for "not positional",
     *  which plays it centred at full volume (UI and self-inflicted sounds). */
    int tile_x;
    int tile_z;
};

/** One live area emitter. */
struct RS_AudioAreaVoice
{
    bool active;
    int voice_id;
    int sound_id;
    int tile_x;
    int tile_z;
    int level;
    int distance;
    /** Random-set emitters re-trigger rather than loop. */
    bool random_set;
    int ticks_until_next;
    int ticks_min;
    int ticks_max;
    /** Index into world->area_sounds this came from, so a rebuild can tell
     *  which voices are still wanted. */
    int source_index;
};

struct RS_Audio
{
    struct RS_AudioEntry queue[RS_AUDIO_QUEUE_MAX];
    int count;

    bool enabled;
    /** 0..TORIRS_AUDIO_VOLUME_MAX, per bus. */
    int effect_volume;
    int area_volume;
    int music_volume;

    /* Overlap rule state (reference lastWave*). Times are client ticks. */
    int last_play_tick;
    int last_play_length_ticks;
    int last_sound_id;
    int last_loops;

    /** Monotonic client tick. */
    int tick;

    /** Next voice id to hand out. Monotonic so a stale VOICE_UPDATE for a
     *  finished voice can never land on a new one. */
    int next_voice_id;

    struct ToriRS_MusicPlayer music;
    /** Set while a music load task is outstanding. */
    bool music_loading;

    struct RS_AudioAreaVoice area[RS_AUDIO_MAX_AREA_VOICES];
    /** The world's area-sound generation this layer last synchronised with. */
    int area_generation;

    /** Cursor into the scene's event queue for the asset drain. Reset whenever
     *  the queue shrinks, which is how a frame boundary is detected without the
     *  scene having to tell us. */
    int scene_event_cursor;

    /* Counters, for tests and TORIRS_AUDIO_DEBUG. */
    int played;
    int dropped_overlap;
    int dropped_absent;
    int dropped_queue_full;
    int assets_published;
};

void
RS_Audio_Init(struct RS_Audio* audio);

/** Release the music player and everything it holds. */
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

/**
 * The same, but from somewhere in the scene: the clip is attenuated and panned
 * by the tile's distance from the camera. Used by sequence frame sounds and by
 * anything the server places rather than aims at the player.
 */
void
RS_Audio_SynthAt(
    struct RS_Audio* audio,
    int synth_id,
    int loops,
    int delay,
    int tile_x,
    int tile_z);

/** MIDI_SONG / MIDI_SONG_V2: play a track from cache index 6. */
void
RS_Audio_Song(
    struct RS_Audio* audio,
    int song_id,
    bool loop,
    int fade_out_ms,
    int fade_in_ms);

/** MIDI_SONG_STOP. */
void
RS_Audio_SongStop(
    struct RS_Audio* audio,
    int fade_out_ms);

/** MIDI_JINGLE: play a jingle from cache index 11, then resume the song. */
void
RS_Audio_Jingle(
    struct RS_Audio* audio,
    int jingle_id,
    int length_ms);

/**
 * Advance one client tick.
 *
 * Drains the scene's sound-asset events, runs the effect queue, synchronises
 * area sounds against the world, and ticks the music player. `provider`,
 * `runner`, `scene` and `world` may be NULL (no cache, no scene yet), in which
 * case the parts that need them are skipped rather than guessed at.
 *
 * `camera_tile_x/z` and `camera_level` place the listener; `feedback` is what
 * the host reported before the frame.
 */
void
RS_Audio_Tick(
    struct RS_Audio* audio,
    struct CacheProvider* provider,
    struct TaskRunner* runner,
    struct ToriDraw_Scene* scene,
    struct World* world,
    int camera_tile_x,
    int camera_tile_z,
    int camera_level,
    const struct ToriRS_AudioFeedback* feedback,
    struct ToriRS_AudioQueue* out);

/**
 * Apply varp clientcode 4 (reference Client.updateVarp): values 0..3 select
 * volume 128/96/64/32 on the reference's 0..128 scale and enable sound; 4
 * disables it. Unknown values are ignored.
 */
void
RS_Audio_SetVolumeLevel(
    struct RS_Audio* audio,
    int level,
    struct ToriRS_AudioQueue* out);

/** Set one bus directly, 0..TORIRS_AUDIO_VOLUME_MAX. */
void
RS_Audio_SetBusVolume(
    struct RS_Audio* audio,
    enum ToriRS_AudioBus bus,
    int volume,
    struct ToriRS_AudioQueue* out);

/** Silence everything and drop every queued effect (used on logout and on a
 *  cache swap). */
void
RS_Audio_StopAll(
    struct RS_Audio* audio,
    struct ToriRS_AudioQueue* out);

#endif
