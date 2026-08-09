#ifndef SRC_AUDIO_TORIRS_AUDIO_H
#define SRC_AUDIO_TORIRS_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The platform <-> game audio interface.
 *
 * The game never touches an audio device. It decides *what* should be heard and
 * puts commands on a queue; the host drains that queue once per frame and hands
 * each command to whichever backend it built (src/platform/platform_audio.h).
 * Same split as rendering, where the game builds a ToriRS_Frame and the host
 * gives it to a renderer -- and for the same reason: the game has to run in
 * places that cannot own an audio device, from a headless test to a WebAssembly
 * page where playback may only start inside a user gesture and where the heap
 * moves underneath any pointer the game handed out.
 *
 * ## Retained, like the renderer
 *
 * This interface is *retained*. Clips are **assets**: loaded once under a
 * handle, referenced by handle from every play, and unloaded explicitly. They
 * live in the ToriDraw_Scene's asset registry next to models, sprites and fonts
 * (ToriDraw_SceneSoundAdd), and the scene's own load/unload events become
 * ASSET_LOAD/ASSET_UNLOAD commands the same way sprite events become
 * TORIRSRC_SPRITE_LOAD. Nothing on the audio path allocates per play.
 *
 * That replaces an earlier immediate-mode shape where each play lent the
 * backend a PCM buffer "until the next drain". It could not express a sound
 * that outlives a frame -- an area loop, a song -- and it re-expanded the same
 * clip's loops into a fresh malloc on every play.
 *
 * Three kinds of thing cross the boundary:
 *
 *   assets   immutable PCM, 16-bit signed mono, with the sample rate and loop
 *            points the cache gave them. The backend copies on load and owns
 *            the copy until unload.
 *   voices   one playback of an asset: volume, pan, playback rate, loop count.
 *            A voice has a game-chosen id so the game can move it (an area
 *            sound follows the camera) or stop it. One-shots use the same
 *            mechanism and simply never refer to the id again.
 *   streams  PCM the game generates rather than loads -- music, which is
 *            synthesised from a MIDI track and a cache soundbank and would be
 *            tens of megabytes if rendered up front. The game pushes blocks
 *            and the backend rings them.
 *
 * ## Buses
 *
 * Every voice and stream names a bus, and each bus has its own volume. The game
 * has three independent volume settings (effects, music, area) and they are
 * separate settings in the real client too, so mixing them into one number here
 * would make the settings panel unimplementable.
 *
 * ## Ownership
 *
 * ASSET_LOAD and STREAM_PUSH carry a pointer. It is borrowed **only for the
 * duration of the submit call** -- the backend copies immediately. Everything
 * else in a command is a plain value. Nothing the backend receives outlives the
 * call, so a moving heap cannot invalidate it.
 */

#define TORIRS_AUDIO_VOLUME_MAX 255

/**
 * Rate the mixer runs at.
 *
 * Every era's sound effects and every music sample render at 22050Hz
 * (RSCACHE_SOUND_SAMPLE_RATE, and the reference's AUDIO_SAMPLE_RATE), so
 * resampling only happens for pitch, never for format. Assets still carry their
 * own rate and are resampled into this one.
 */
#define TORIRS_AUDIO_SAMPLE_RATE 22050

/** The mixer is stereo so pan means something. */
#define TORIRS_AUDIO_CHANNELS 2

/** Pan: 0 hard left, 8192 centre, 16384 hard right (the reference's range). */
#define TORIRS_AUDIO_PAN_CENTRE 8192
#define TORIRS_AUDIO_PAN_MAX 16384

/** Playback rate is fixed-point: this many units == the asset's natural rate. */
#define TORIRS_AUDIO_RATE_UNITY 256

/** A voice id the game does not intend to refer to again. */
#define TORIRS_AUDIO_VOICE_ANON (-1)

enum ToriRS_AudioBus
{
    /** Combat, interaction, interface: everything the server or a script fires. */
    TORIRS_AUDIO_BUS_EFFECTS = 0,
    /** Songs and jingles. */
    TORIRS_AUDIO_BUS_MUSIC,
    /** Looping scenery sound: waterfalls, furnaces, machinery. */
    TORIRS_AUDIO_BUS_AREA,
    TORIRS_AUDIO_BUS_COUNT
};

enum ToriRS_AudioCommandKind
{
    TORIRS_AUDIO_CMD_NONE = 0,

    /** Copy PCM into the backend under `asset_id`. Replaces any asset already
     *  there (which is how a re-decoded clip updates in place). */
    TORIRS_AUDIO_CMD_ASSET_LOAD,
    /** Drop an asset. Voices still playing it are stopped first -- a backend
     *  must never be left pointing at freed samples. */
    TORIRS_AUDIO_CMD_ASSET_UNLOAD,
    /** Drop every asset and stop every voice. Sent on a cache/scene reset. */
    TORIRS_AUDIO_CMD_ASSET_CLEAR,

    /** Start `asset_id` on `voice_id`. Starting a voice id that is already
     *  playing replaces it. */
    TORIRS_AUDIO_CMD_VOICE_START,
    /** Retarget a live voice's volume/pan/rate, optionally ramped. Ignored for
     *  a voice that has finished, which is not an error: an area sound updates
     *  its pan every frame and may have been reaped in between. */
    TORIRS_AUDIO_CMD_VOICE_UPDATE,
    /** Stop a voice, optionally with a fade so it does not click. */
    TORIRS_AUDIO_CMD_VOICE_STOP,

    /** Create (or reset) a push stream, fed by STREAM_PUSH. */
    TORIRS_AUDIO_CMD_STREAM_OPEN,
    /** Append interleaved PCM for `channels` channels. Frames past the ring's
     *  capacity are dropped and counted. */
    TORIRS_AUDIO_CMD_STREAM_PUSH,
    TORIRS_AUDIO_CMD_STREAM_CLOSE,
    /** Ramp a stream's own gain -- how music fades in and out. */
    TORIRS_AUDIO_CMD_STREAM_VOLUME,

    /** Set a bus volume, 0..TORIRS_AUDIO_VOLUME_MAX. */
    TORIRS_AUDIO_CMD_BUS_VOLUME,
    /** Silence everything currently sounding; assets and streams stay loaded. */
    TORIRS_AUDIO_CMD_STOP_ALL,
};

/**
 * An asset whose PCM the backend pulls instead of holding.
 *
 * This is what makes music an ordinary asset. A four-minute track is ~21MB
 * rendered, so it cannot be resident -- but load / unload / play-with-
 * conditions is exactly the right vocabulary for it, and the only thing that
 * has to differ is where the samples come from. Attach one of these to
 * ASSET_LOAD instead of `pcm` and every other command works unchanged:
 * VOICE_START plays it, `loop_count` says how, VOICE_STOP fades it,
 * ASSET_UNLOAD retires it.
 *
 * Before this, music was pushed a tick at a time by the game, which made every
 * gap in the frame loop a gap in the audio. Pulling removes the buffer that was
 * being under-filled: the backend asks for exactly the frames it is about to
 * play, at the moment it plays them.
 *
 * `render` runs inside the backend's mix, on the frame thread like everything
 * else -- so its cost is frame time. It must not do IO, must not block, and
 * should not allocate: whatever it needs is resident before the asset loads.
 */
struct ToriRS_AudioSource
{
    /**
     * Begin, or restart from the top. `loop_count` is VOICE_START's, verbatim:
     * 0 plays once, -1 repeats forever, n > 0 repeats n times.
     *
     * The condition belongs here rather than in the mixer because for a
     * sequenced source looping means restarting the *sequence*; repeating the
     * rendered PCM would loop whichever bar was in the buffer. Optional.
     */
    void (*start)(void* ctx, int loop_count);
    /**
     * Fill up to `frames` interleaved stereo frames at the mixer's rate.
     *
     * Returns the frames actually produced. A short return means finished, and
     * the voice ends; a looping source simply never returns short.
     */
    int (*render)(void* ctx, int16_t* out, int frames);
    /** Optional. Called when the asset is replaced, unloaded or freed. The
     *  source is borrowed, so this is a notification, not a transfer. */
    void (*release)(void* ctx);
    void* ctx;
};

struct ToriRS_AudioCommand
{
    enum ToriRS_AudioCommandKind kind;

    /* ASSET_LOAD / ASSET_UNLOAD, and the asset a VOICE_START refers to. */
    int asset_id;
    /** ASSET_LOAD: a generator to pull from instead of `pcm`. Set `render` to
     *  use it; borrowed, so the game must ASSET_UNLOAD before tearing `ctx`
     *  down. Leave zeroed for an ordinary resident asset. */
    struct ToriRS_AudioSource source;
    /** Borrowed for the duration of the submit call only. 16-bit signed mono. */
    const int16_t* pcm;
    int sample_count;
    int sample_rate;
    /** Loop span in samples. `loop_end > loop_start` makes a voice with
     *  loop_count != 0 repeat that span rather than the whole clip. */
    int loop_start;
    int loop_end;

    /* VOICE_* */
    int voice_id;
    enum ToriRS_AudioBus bus;
    /** 0..TORIRS_AUDIO_VOLUME_MAX. */
    int volume;
    /** 0..TORIRS_AUDIO_PAN_MAX, TORIRS_AUDIO_PAN_CENTRE is centre. */
    int pan;
    /** Playback rate relative to TORIRS_AUDIO_RATE_UNITY. */
    int rate;
    /** 0 = play once, -1 = loop forever, n > 0 = play the loop span n times. */
    int loop_count;
    /** Ramp length in milliseconds for UPDATE and STOP. 0 is immediate. */
    int fade_ms;

    /* STREAM_* */
    int stream_id;
    int channels;
    int frame_count;

    /* BUS_VOLUME */
    enum ToriRS_AudioBus target_bus;

    /** Which cache effect/song this came from, for logs and tests. -1 when it
     *  is not from one. Never used to make a decision. */
    int source_id;
};

/**
 * Ring of pending commands.
 *
 * Sized for a busy frame: a scene rebuild can unload and reload every area
 * sound at once, which is a few dozen commands, and the queue must not be the
 * thing that drops them.
 */
#define TORIRS_AUDIO_QUEUE_MAX 256

struct ToriRS_AudioQueue
{
    struct ToriRS_AudioCommand commands[TORIRS_AUDIO_QUEUE_MAX];
    int count;
    /** Commands dropped because the queue was full since the last drain. A
     *  non-zero value means the host is not draining, which is a wiring bug
     *  rather than an audio one -- so it is counted rather than ignored. */
    int dropped;
};

/**
 * What the host tells the game before it builds a frame's audio.
 *
 * Deliberately almost empty. It used to carry per-stream headroom, because the
 * game synthesised music and had to be told how far ahead it was allowed to
 * run. That made the game a real-time producer feeding a consumer on a
 * different clock, and it is exactly the coupling SOURCE_OPEN removes: a pull
 * source is asked for what is needed when it is needed, so there is no budget
 * to communicate. If anything ever needs a headroom number back here again,
 * something has gone back to pushing.
 */
struct ToriRS_AudioFeedback
{
    /** True when a device is open. When false the game may skip work entirely
     *  rather than prepare audio nobody will hear. */
    bool device_open;
};

#define TORIRS_AUDIO_STREAM_MAX 4

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

/** Zero a command and give it the defaults every kind shares. */
static inline void
ToriRS_AudioCommand_Init(
    struct ToriRS_AudioCommand* command,
    enum ToriRS_AudioCommandKind kind)
{
    if( !command )
        return;
    for( unsigned i = 0; i < sizeof(*command); i++ )
        ((unsigned char*)command)[i] = 0;
    command->kind = kind;
    command->asset_id = -1;
    command->voice_id = TORIRS_AUDIO_VOICE_ANON;
    command->stream_id = -1;
    command->source_id = -1;
    command->volume = TORIRS_AUDIO_VOLUME_MAX;
    command->pan = TORIRS_AUDIO_PAN_CENTRE;
    command->rate = TORIRS_AUDIO_RATE_UNITY;
    command->channels = 1;
}

/**
 * Copy up to `max` commands out and clear the queue.
 *
 * The queue is emptied whether or not everything fitted: a command the host had
 * no room for is a command it will never take, and keeping it would apply a
 * volume change or start a sound a frame or more late. Returns how many were
 * written.
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
