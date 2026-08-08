#ifndef SRC_AUDIO_TORIRS_MUSIC_H
#define SRC_AUDIO_TORIRS_MUSIC_H

#include "audio/torirs_audio.h"
#include "audio/torirs_midi_synth.h"
#include "audio/torirs_soundbank.h"

#include <stdbool.h>
#include <stdint.h>

struct RSCache_MusicSong;
struct RSCache_VorbisSetup;

/*
 * The music player: what song is playing, what is loading, and how loud.
 *
 * The server names a song; everything else is the client's problem. Getting
 * from an id to sound means decoding a column-packed MIDI, reading the
 * instrument manifest it carries, loading each of those patches and each sample
 * each patch's *used* notes reference, and only then starting the sequencer.
 * That is a dozen or more cache reads, so it is asynchronous and lives in a
 * task; this holds the state that task fills and the state playback needs.
 *
 * ## Why a stream rather than an asset
 *
 * A four-minute track at 22050 stereo is ~21MB of PCM. Rendering it up front
 * would stall the frame that started it and hold the memory for as long as the
 * song plays. So music is a *stream*: the synth renders a few frames' worth
 * each tick into a scratch block and pushes it, and the backend rings it. The
 * game asks the host how much headroom the ring has (ToriRS_AudioFeedback) and
 * fills exactly that, so latency stays bounded in both directions.
 *
 * ## Jingles
 *
 * A jingle is a short track from a different cache index that interrupts the
 * song and hands back when it ends. The song's id is remembered rather than its
 * state -- the reference restarts the song from the top afterwards, and so does
 * this, because nothing in the song format records a resume point.
 */

enum ToriRS_MusicState
{
    TORIRS_MUSIC_IDLE = 0,
    /** A load task is walking the cache for this song. */
    TORIRS_MUSIC_LOADING,
    TORIRS_MUSIC_PLAYING,
    /** Fading the old song out before the new one starts. */
    TORIRS_MUSIC_FADING,
};

/** Which cache index a song id refers to. */
enum ToriRS_MusicSource
{
    TORIRS_MUSIC_SOURCE_TRACK = 0,
    TORIRS_MUSIC_SOURCE_JINGLE,
};

/** Patch ids retained for the song currently loaded. */
#define TORIRS_MUSIC_MAX_PATCHES 64

struct ToriRS_MusicPlayer
{
    struct ToriRS_SoundBank bank;
    struct ToriRS_MidiSynth synth;

    /** The Vorbis setup shared by every music sample (index 14 archive 0).
     *  Loaded once and kept: it is a few kilobytes and every sample needs it. */
    struct RSCache_VorbisSetup* vorbis_setup;

    enum ToriRS_MusicState state;

    /** What is sounding now. -1 when nothing is. */
    int current_song;
    enum ToriRS_MusicSource current_source;
    bool current_loop;
    /** Owned; the synth reads its `midi` bytes for as long as it plays. */
    struct RSCache_MusicSong* song;
    int retained_patches[TORIRS_MUSIC_MAX_PATCHES];
    int retained_count;

    /** What has been asked for but not started. */
    bool has_request;
    int request_song;
    enum ToriRS_MusicSource request_source;
    bool request_loop;
    int request_fade_out_ms;
    int request_fade_in_ms;
    /** Set while a load task owns the request, so it is not started twice. */
    bool request_loading;

    /** Song to return to when a jingle ends. -1 for none. */
    int resume_song;
    bool resume_loop;

    /** Stream id in the mixer, and whether it is open. */
    int stream_id;
    bool stream_open;
    /** 0..TORIRS_AUDIO_VOLUME_MAX, the music setting. */
    int volume;
    /** Ticks left of a fade-out before the pending request starts. */
    int fade_ticks;
    /** Fade-in the next stream open should use. Recorded at install time
     *  because the loader has no command queue to open the stream with. */
    int pending_fade_in_ms;

    /**
     * Scratch the synth renders into and STREAM_PUSH borrows.
     *
     * Owned here and only rewritten on the next push, which is what makes the
     * "borrowed for the duration of the submit" rule safe: the host copies it
     * during the same frame it was filled.
     */
    int16_t* block;
    int block_frames;

    /* Counters for the debug readout. */
    int reported_drops;
    int songs_started;
    int songs_failed;
    int frames_pushed;
};

void
ToriRS_Music_Init(struct ToriRS_MusicPlayer* player);

/** Free the bank, the synth, the song and the scratch. */
void
ToriRS_Music_Free(struct ToriRS_MusicPlayer* player);

/**
 * Ask for a song.
 *
 * `fade_out_ms` fades whatever is playing before the switch; `fade_in_ms` fades
 * the new one up. Asking for the song already playing is a no-op, which is what
 * makes it safe for the server to resend the same id on every region change.
 */
void
ToriRS_Music_Request(
    struct ToriRS_MusicPlayer* player,
    int song_id,
    enum ToriRS_MusicSource source,
    bool loop,
    int fade_out_ms,
    int fade_in_ms);

/** Stop, with an optional fade. */
void
ToriRS_Music_Stop(
    struct ToriRS_MusicPlayer* player,
    int fade_out_ms);

/** Set the music volume, 0..TORIRS_AUDIO_VOLUME_MAX. */
void
ToriRS_Music_SetVolume(
    struct ToriRS_MusicPlayer* player,
    int volume,
    struct ToriRS_AudioQueue* out);

/** True when a request is waiting for a loader to pick it up. */
bool
ToriRS_Music_TakeLoadRequest(
    struct ToriRS_MusicPlayer* player,
    int* out_song,
    enum ToriRS_MusicSource* out_source);

/**
 * Install a loaded song and start it.
 *
 * Takes ownership of `song`. `patch_ids`/`patch_count` are what the loader
 * retained in the bank, so the player can release them when the song ends.
 */
void
ToriRS_Music_Installed(
    struct ToriRS_MusicPlayer* player,
    int song_id,
    enum ToriRS_MusicSource source,
    struct RSCache_MusicSong* song,
    const int* patch_ids,
    int patch_count,
    struct ToriRS_AudioQueue* out);

/** Report that the load failed, so the request is not retried forever. */
void
ToriRS_Music_LoadFailed(
    struct ToriRS_MusicPlayer* player,
    int song_id);

/**
 * One tick: run fades, synthesise into the stream, retire a finished song.
 *
 * `feedback` says how much room the host has; pass a zeroed struct to
 * synthesise nothing (no device, or a headless run).
 */
void
ToriRS_Music_Tick(
    struct ToriRS_MusicPlayer* player,
    const struct ToriRS_AudioFeedback* feedback,
    struct ToriRS_AudioQueue* out);

#endif
