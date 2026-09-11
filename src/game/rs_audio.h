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
 * mixer bus, plus the master slider multiplied into all three. The reference
 * reads them from distinct options, so collapsing them into one number here
 * would make the settings panel unimplementable.
 */

struct CacheProvider;
struct TaskRunner;
struct ToriRS_FeatureTable;
struct ToriDraw_Scene;
struct World;
struct RS_Soundscapes;

/** Reference cap: waveIds/waveLoops/waveDelay are 50 long and the packet
 *  handler drops anything past that. */
#define RS_AUDIO_QUEUE_MAX 50

/**
 * How late an effect may be and still be worth playing, in client ticks (20ms).
 *
 * The reference's `delays[i] >= -10` window. It is the whole answer to "the clip
 * was not resident when it came due": keep counting down, and once the entry is
 * ten ticks past its moment, discard it. A swing sound that arrives after the
 * swing is worse than no swing sound -- it lands on the *next* attack.
 *
 * This is not a load timeout. The countdown runs whether or not the clip is
 * resident, exactly as the reference's does, so an effect that is still loading
 * when it comes due plays the instant it arrives (if it arrives inside the
 * window) rather than `delay` ticks after it arrives.
 *
 * The consequence, stated because it is a real behaviour change: a cold clip
 * whose load takes longer than 10 ticks is now silent rather than late. The next
 * request for the same effect finds it resident and plays on time, so what this
 * costs is the first play of a never-heard sound, which is what the reference
 * costs too (its archive reads are synchronous and in-memory, so it simply never
 * hits the window).
 */
#define RS_AUDIO_LATE_LIMIT_TICKS 10

/**
 * Reference default (Client.waveVolume = 128 on its 0..128 scale), on the
 * 0..255 scale used here, is *full* -- not half. Starting at 128 made
 * everything 6dB quieter than the game plays it, on every bus, before any
 * setting was touched. RS_AUDIO_REFERENCE_VOLUME keeps that number, because
 * it is the one to scale against when anything asks "how loud is full?".
 */
#define RS_AUDIO_REFERENCE_VOLUME 255

/**
 * What a fresh install actually starts at: muted.
 *
 * A deliberate product default, not an audio-fidelity claim. This client is
 * launched constantly from harnesses, capture runs and dev loops where sound
 * is noise, and the reference's own full-volume start is the wrong first
 * impression for that. Only the *initial* device state is affected: the
 * volume settings persist (RS_Settings writes them through the IO queue), so
 * a player who turns sound up keeps it up across sessions.
 */
#define RS_AUDIO_DEFAULT_VOLUME 0

/** Concurrent area-sound emitters. Beyond this the nearest win. */
#define RS_AUDIO_MAX_AREA_VOICES 12

/** Ramp applied when an area emitter starts, stops, or goes out of range. The
 *  reference's `method12534(0, …, 150, …)` -- a cut clicks. */
#define RS_AUDIO_AREA_FADE_MS 150

/** Gap used when a random-set loc declares a degenerate one (min == max == 0).
 *  Ten seconds: audible as "occasionally", which is what the field means. */
#define RS_AUDIO_AREA_SET_FALLBACK_TICKS 500

/** Soundscape caps, matching the cache type's own. */
#define RS_AUDIO_MAX_AMBIENT_LOOPS 8
#define RS_AUDIO_MAX_AMBIENT_SETS 8

/**
 * One of a soundscape's timed random sets, live.
 *
 * `ids` is borrowed from the soundscape table, which outlives the bed: the
 * table is loaded once at boot and never rewritten.
 */
struct RS_AudioAmbientSet
{
    const int* ids;
    int id_count;
    int ticks_min;
    int ticks_max;
    int ticks_until_next;
    uint32_t rng;
};

struct RS_AudioEntry
{
    int sound_id;
    int loops;
    /**
     * Ticks until this plays, counted down every tick regardless of whether the
     * clip is resident -- see RS_AUDIO_LATE_LIMIT_TICKS. Goes negative: the
     * effect fires on the tick it first reads below zero, and is discarded once
     * it passes -RS_AUDIO_LATE_LIMIT_TICKS.
     */
    int delay;
    /** Set once ToriDraw_Sound.queue_delay has been folded into `delay`. */
    bool delay_resolved;
    /** Set once a load has been asked for, so it is asked for exactly once. */
    bool load_requested;
    /** Where it should sound from, in scene tiles; -1 for "not positional",
     *  which plays it centred at full volume (UI and self-inflicted sounds).
     *  SYNTH_SOUND is deliberately in that class: the reference queues it with
     *  a zero location, which its mixer reads as "full volume, no pan". */
    int tile_x;
    int tile_z;
    /** Audible radius in tiles, 0 for "none stated" (falls back to
     *  AUDIO_DEFAULT_DISTANCE). A sequence frame sound carries its own in the
     *  low nibble of the record, and SOUND_AREA carries one on the wire. */
    int radius;
    /** Radius within which it is at full volume, as for area emitters. Frame
     *  sounds state none (0); SOUND_AREA carries one from rev 221 on. */
    int inner;
};

/**
 * One live area emitter.
 *
 * An emitter can sound *two* ways at once, and the reference runs both streams
 * off one record: `primary` is a single sound looping forever, `secondary` fires
 * one shot picked at random out of a set every few seconds. A loc that declares
 * both hums and clanks; modelling them as alternatives silences the clank.
 */
struct RS_AudioAreaVoice
{
    bool active;

    /** The continuous loop. `primary_sound < 0` when the loc declares no
     *  continuous sound, in which case no primary voice is ever started. */
    int primary_voice_id;
    int primary_sound;

    /** The random set. Borrowed from the loc config, which outlives the scene. */
    const int* set_ids;
    int set_count;
    int ticks_until_next;
    int ticks_min;
    int ticks_max;
    /** Per-emitter PRNG state, so two identical waterfalls do not fire in
     *  lockstep and a headless run is still reproducible. */
    uint32_t rng;

    /** Footprint box in scene tiles: [min_x, max_x) x [min_z, max_z). */
    int min_x;
    int min_z;
    int max_x;
    int max_z;
    int level;
    /** Audible radius and the full-volume inner radius, in tiles. */
    int radius;
    int inner;

    /** Identity of the loc this came from, so a scene rebuild can keep a voice
     *  that is still in the new scene instead of restarting it. */
    int loc_id;
    /** Index into world->area_sounds for the current generation. */
    int source_index;
};

struct RS_Audio
{
    struct RS_AudioEntry queue[RS_AUDIO_QUEUE_MAX];
    int count;

    bool enabled;
    /**
     * A mixer sink exists.
     *
     * Distinct from `enabled`, which is the player's effect-volume setting and
     * is restored by any later volume command. This one is the boot-time fact
     * that the platform found no output device, and nothing in the session
     * flips it back. While it is false every clip decode, area-sound scene walk
     * and Vorbis frame below would render into a sink that discards them, so
     * none of it runs -- the server still gets told what it would have played.
     */
    bool device_present;
    /**
     * Refuse a short effect while a longer one is sounding (the 2004 client's
     * monophony). Set from the era feature table; off for OldSchool, whose
     * mixer is polyphonic and where the rule would swallow most of a combat
     * tick.
     */
    bool effects_monophonic;
    /** 0..TORIRS_AUDIO_VOLUME_MAX. Per-bus values are pre-master settings. */
    int master_volume;
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

    /**
     * The region's background bed (AMBIENTSOUND_START): an *unpositioned* set of
     * sounds the server names with one id. Distinct from the loc-driven area
     * emitters below, which have positions and are found by walking the scene --
     * an area routinely has both, and this is the one that makes a place sound
     * like somewhere when nothing in it is making a noise.
     *
     * The id is a **soundscape config id**, not a sound-effect id: it names a
     * record holding several continuous loops plus up to eight independently
     * timed random sets. `soundscapes` is the table it resolves through, loaded
     * once at boot and borrowed here. When that table is empty -- every cache
     * before OldSchool 231 -- the id is treated as a single looping effect,
     * which is all the older revisions can have meant by it.
     */
    int ambient_sound_id;
    int ambient_fade_ms;
    bool ambient_started;
    struct RS_Soundscapes const* soundscapes;

    /** The bed's live streams. `legacy_voice_id` is the pre-231 single-effect
     *  form; `loop_voice_id[]` and `sets[]` are the soundscape form. */
    int ambient_legacy_voice_id;
    int ambient_loop_voice_id[RS_AUDIO_MAX_AMBIENT_LOOPS];
    int ambient_loop_sound_id[RS_AUDIO_MAX_AMBIENT_LOOPS];
    int ambient_loop_count;
    struct RS_AudioAmbientSet ambient_sets[RS_AUDIO_MAX_AMBIENT_SETS];
    int ambient_set_count;

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

/** Apply the era's audio behaviour. Safe with NULL (leaves the defaults). */
void
RS_Audio_SetFeatures(
    struct RS_Audio* audio,
    struct ToriRS_FeatureTable const* features);

/**
 * Say whether the platform opened an output device.
 *
 * Call once, after PlatformAudio_Init reports. Defaults to present, so a
 * caller that never opens a device and never calls this keeps the old
 * behaviour of decoding everything into a discarding sink.
 */
void
RS_Audio_SetDevicePresent(struct RS_Audio* audio, bool present);

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
 * by the tile's distance from the camera, and dropped outright beyond `radius`.
 * Used by sequence frame sounds and by anything the server places rather than
 * aims at the player (SOUND_AREA).
 *
 * `radius` is in tiles; 0 means the caller has no radius to state, which falls
 * back to AUDIO_DEFAULT_DISTANCE. Both real sources do state one -- a frame
 * sound in the low nibble of its record, SOUND_AREA in the high nibble of its
 * second byte -- so 0 is a fallback, not the normal case.
 */
void
RS_Audio_SynthAt(
    struct RS_Audio* audio,
    int synth_id,
    int loops,
    int delay,
    int tile_x,
    int tile_z,
    int radius,
    int inner);

/**
 * AMBIENTSOUND_START / _STOP: set the region's background bed, or -1 to stop it.
 *
 * `id` is a soundscape config id when a soundscape table has been bound and
 * holds it, and a bare sound-effect id otherwise. Either way the bed plays at
 * full volume on the area bus: it is a bed, not a thing in the world, so it is
 * neither attenuated nor panned.
 */
void
RS_Audio_SetAmbient(
    struct RS_Audio* audio,
    int id,
    int fade_ms);

/** Bind the soundscape table AMBIENTSOUND_START ids resolve through. Borrowed,
 *  not owned; pass NULL to unbind. */
void
RS_Audio_SetSoundscapes(
    struct RS_Audio* audio,
    struct RS_Soundscapes const* soundscapes);

/** MIDI_SONG / MIDI_SONG_V2: play a track from cache index 6. */
void
RS_Audio_Song(
    struct RS_Audio* audio,
    int song_id,
    bool loop,
    int fade_out_ms,
    int fade_in_ms);

/** MIDI_SONG_STOP. */
/**
 * Play `primary_id` and pre-queue `secondary_id` as its tonal variant.
 *
 * The variant is not loaded here -- only remembered -- because the server sends
 * this on entering a region and may never send the swap.
 */
void
RS_Audio_SongWithSecondary(
    struct RS_Audio* audio,
    int primary_id,
    int secondary_id,
    int fade_out_ms,
    int fade_in_ms);

/** Crossfade to the pre-queued variant without restarting the phrase. */
void
RS_Audio_SongSwap(
    struct RS_Audio* audio,
    int fade_out_ms,
    int fade_in_ms);

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

/** Set the master gain and re-emit all three effective bus gains. */
void
RS_Audio_SetMasterVolume(
    struct RS_Audio* audio,
    int volume,
    struct ToriRS_AudioQueue* out);

/** Silence everything and drop every queued effect (used on logout and on a
 *  cache swap). */
void
RS_Audio_StopAll(
    struct RS_Audio* audio,
    struct ToriRS_AudioQueue* out);

#endif
