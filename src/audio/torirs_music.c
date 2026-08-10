#include "audio/torirs_music.h"

#include "audio/torirs_mixer.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Legacy id kept for the player's own bookkeeping. */
#define MUSIC_STREAM_ID 0

/*
 * Music's asset and voice ids.
 *
 * Both spaces reserve negative values -- an asset slot marks itself free with
 * asset_id -1, and TORIRS_AUDIO_VOICE_ANON is -1 -- so a synthetic id has to be
 * positive and simply far above the real ones. Effect assets are cache effect
 * ids (low thousands) and voices come from the game's own counter, so this is
 * out of reach of both. Same convention as the synthetic sprite ids in the dat1
 * bridge.
 *
 * There is exactly one of each: a generator holds its own playback position, so
 * the mixer refuses a second voice on it.
 */
#define MUSIC_ASSET_ID 0x40000000
#define MUSIC_VOICE_ID 0x40000000

#define MUSIC_TRACE(...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( ToriRS_AudioTraceEnabled() )                                                           \
            fprintf(stderr, __VA_ARGS__);                                                          \
    } while( 0 )

void
ToriRS_Music_Init(struct ToriRS_MusicPlayer* player)
{
    if( !player )
        return;
    memset(player, 0, sizeof(*player));
    ToriRS_SoundBank_Init(&player->bank);
    ToriRS_MidiSynth_Init(&player->synth, &player->bank, TORIRS_AUDIO_SAMPLE_RATE);
    player->current_song = -1;
    player->request_song = -1;
    player->resume_song = -1;
    player->secondary_song = -1;
    player->stream_id = MUSIC_STREAM_ID;
    player->volume = TORIRS_AUDIO_VOLUME_MAX;
    player->state = TORIRS_MUSIC_IDLE;
}

/** Drop the song currently installed and everything it held. */
static void
release_current(struct ToriRS_MusicPlayer* player)
{
    ToriRS_MidiSynth_Stop(&player->synth);
    for( int i = 0; i < player->retained_count; i++ )
        ToriRS_SoundBank_ReleasePatch(&player->bank, player->retained_patches[i]);
    player->retained_count = 0;
    RSCache_MusicSongFree(player->song);
    player->song = NULL;
    player->current_song = -1;
}

void
ToriRS_Music_Free(struct ToriRS_MusicPlayer* player)
{
    if( !player )
        return;
    release_current(player);
    ToriRS_MidiSynth_Free(&player->synth);
    ToriRS_SoundBank_Free(&player->bank);
    RSCache_VorbisSetupFree(player->vorbis_setup);
    player->vorbis_setup = NULL;
    RSCache_VorbisSetupFree(player->rs2012_vorbis_setup);
    player->rs2012_vorbis_setup = NULL;
}

void
ToriRS_Music_Request(
    struct ToriRS_MusicPlayer* player,
    int song_id,
    enum ToriRS_MusicSource source,
    bool loop,
    int fade_out_ms,
    int fade_in_ms)
{
    if( !player )
        return;

    if( source == TORIRS_MUSIC_SOURCE_JINGLE )
    {
        /* Remember what to hand back to. A jingle interrupting a jingle keeps
         * the original song, not the jingle it replaced. */
        if( player->current_source == TORIRS_MUSIC_SOURCE_TRACK && player->current_song >= 0 )
        {
            player->resume_song = player->current_song;
            player->resume_loop = player->current_loop;
        }
    }
    else
    {
        player->resume_song = -1;
    }

    /* Already playing it, and not being asked to restart: nothing to do. The
     * server resends the region's song on every rebuild. */
    if( song_id >= 0 && player->current_song == song_id && player->current_source == source &&
        player->state == TORIRS_MUSIC_PLAYING )
        return;
    if( player->has_request && player->request_song == song_id &&
        player->request_source == source )
        return;

    player->has_request = song_id >= 0;
    player->request_song = song_id;
    player->request_source = source;
    player->request_loop = loop;
    player->request_fade_out_ms = fade_out_ms;
    player->request_fade_in_ms = fade_in_ms;
    player->request_loading = false;
    /* Fades are counted in client ticks; the request carries milliseconds
     * because that is what the wire carries. */
    player->fade_ticks = fade_out_ms > 0 ? (fade_out_ms + 19) / 20 : 0;
    player->state = player->fade_ticks > 0 && player->current_song >= 0 ? TORIRS_MUSIC_FADING
                                                                       : player->state;
    MUSIC_TRACE(
        "music: request song=%d source=%d loop=%d fade %d/%dms\n",
        song_id,
        (int)source,
        (int)loop,
        fade_out_ms,
        fade_in_ms);
}

void
ToriRS_Music_Stop(
    struct ToriRS_MusicPlayer* player,
    int fade_out_ms)
{
    if( !player )
        return;
    player->resume_song = -1;
    ToriRS_Music_Request(player, -1, TORIRS_MUSIC_SOURCE_TRACK, false, fade_out_ms, 0);
}

void
ToriRS_Music_SetSecondary(
    struct ToriRS_MusicPlayer* player,
    int song_id)
{
    if( !player )
        return;
    player->secondary_song = song_id;
}

void
ToriRS_Music_Swap(
    struct ToriRS_MusicPlayer* player,
    int fade_out_ms,
    int fade_in_ms)
{
    int incoming;
    int outgoing;
    int resume_tick;

    if( !player )
        return;
    /*
     * Nothing queued, or nothing to swap out of: the reference no-ops rather
     * than treating this as a plain song change, because a swap carries no id
     * of its own -- there would be nothing to play.
     */
    if( player->secondary_song < 0 || player->current_song < 0 ||
        player->state != TORIRS_MUSIC_PLAYING )
        return;

    incoming = player->secondary_song;
    outgoing = player->current_song;

    /*
     * Read the position before the request, while the outgoing song is still
     * the one loaded in the synth -- this is the whole difference between a
     * swap and a song change.
     */
    resume_tick = player->synth.current_tick;
    ToriRS_Music_Request(
        player, incoming, TORIRS_MUSIC_SOURCE_TRACK, player->current_loop, fade_out_ms, fade_in_ms);

    /*
     * Only arm the resume if the request was actually taken.
     *
     * Request declines a song that is already playing or already pending. If
     * the resume tick were armed unconditionally it would survive that decline
     * and be applied to whatever song changed *next*, which would then start
     * from the middle of itself -- a swap that did nothing leaving a booby trap
     * for an unrelated region change later.
     */
    if( player->has_request && player->request_song == incoming )
    {
        player->request_resume_tick = resume_tick;
        player->secondary_song = outgoing;
    }
}

void
ToriRS_Music_SetVolume(
    struct ToriRS_MusicPlayer* player,
    int volume,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;

    if( !player )
        return;
    if( volume < 0 )
        volume = 0;
    if( volume > TORIRS_AUDIO_VOLUME_MAX )
        volume = TORIRS_AUDIO_VOLUME_MAX;
    player->volume = volume;
    if( !out )
        return;
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_BUS_VOLUME);
    command.target_bus = TORIRS_AUDIO_BUS_MUSIC;
    command.volume = volume;
    ToriRS_AudioQueue_Push(out, &command);
}

bool
ToriRS_Music_TakeLoadRequest(
    struct ToriRS_MusicPlayer* player,
    int* out_song,
    enum ToriRS_MusicSource* out_source)
{
    if( !player || !player->has_request || player->request_loading )
        return false;
    if( player->request_song < 0 )
        return false;
    /* Do not start loading until the fade-out has finished: the loader is
     * allowed to be slow, and starting it early would make the gap longer
     * rather than shorter. */
    if( player->fade_ticks > 0 )
        return false;
    player->request_loading = true;
    player->state = TORIRS_MUSIC_LOADING;
    if( out_song )
        *out_song = player->request_song;
    if( out_source )
        *out_source = player->request_source;
    return true;
}

/*
 * The generator the backend pulls while a song plays.
 *
 * `ctx` is the player itself, borrowed. The synth is only advanced from here,
 * so the song's position is a function of what the device consumed rather than
 * of how many times the frame loop happened to run.
 */
static int
music_source_render(
    void* ctx,
    int16_t* out,
    int frames)
{
    struct ToriRS_MusicPlayer* player = ctx;

    /*
     * Report "finished" only for a song that has genuinely run out. A looping
     * song never reaches this, because the synth restarts its own sequence --
     * which is the whole reason looping belongs to the synth and not to the
     * mixer. Ending here is what lets a jingle hand back to the track it
     * interrupted.
     */
    if( ToriRS_MidiSynth_Finished(&player->synth) )
        return 0;
    ToriRS_MidiSynth_Render(&player->synth, out, frames);
    player->frames_pushed += frames;

    /* Notes that never sound are the tell that the soundbank did not fill: the
     * stream stays open, frames keep flowing, and every one of them is zero. */
    if( player->synth.stats.notes_dropped_no_patch + player->synth.stats.notes_dropped_no_sample >
        player->reported_drops )
    {
        player->reported_drops = player->synth.stats.notes_dropped_no_patch +
                                 player->synth.stats.notes_dropped_no_sample;
        MUSIC_TRACE(
            "music: %d notes started, %d dropped (no patch %d, no sample %d)\n",
            player->synth.stats.notes_started,
            player->reported_drops,
            player->synth.stats.notes_dropped_no_patch,
            player->synth.stats.notes_dropped_no_sample);
    }
    return frames;
}

/** VOICE_START's condition, handed to the sequencer. */
static void
music_source_start(
    void* ctx,
    int loop_count)
{
    struct ToriRS_MusicPlayer* player = ctx;

    if( !player->song )
        return;
    ToriRS_MidiSynth_Play(
        &player->synth, player->song->midi, player->song->midi_size, loop_count != 0);
}

/** The backend has dropped the asset, whoever unloaded it. */
static void
music_source_release(void* ctx)
{
    struct ToriRS_MusicPlayer* player = ctx;

    player->stream_open = false;
}

/*
 * Publish the song as a generator asset and play it.
 *
 * `stream_id` is reused as the asset/voice id: music occupies exactly one of
 * each, and effects are keyed by cache effect id, so a dedicated negative id
 * keeps the two spaces from meeting.
 */
static void
open_stream(
    struct ToriRS_MusicPlayer* player,
    struct ToriRS_AudioQueue* out,
    int fade_in_ms)
{
    struct ToriRS_AudioCommand command;

    if( !out )
        return;
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_LOAD);
    command.asset_id = MUSIC_ASSET_ID;
    command.sample_rate = TORIRS_AUDIO_SAMPLE_RATE;
    command.source.start = music_source_start;
    command.source.render = music_source_render;
    command.source.release = music_source_release;
    command.source.ctx = player;
    ToriRS_AudioQueue_Push(out, &command);

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
    command.asset_id = MUSIC_ASSET_ID;
    command.voice_id = MUSIC_VOICE_ID;
    command.source_id = player->current_song;
    command.bus = TORIRS_AUDIO_BUS_MUSIC;
    command.volume = fade_in_ms > 0 ? 0 : TORIRS_AUDIO_VOLUME_MAX;
    /* The play condition. The synth restarts its own sequence on a loop, so
     * this is the whole of "play once" versus "play forever". */
    command.loop_count = player->current_loop ? -1 : 0;
    ToriRS_AudioQueue_Push(out, &command);
    player->stream_open = true;

    if( fade_in_ms > 0 )
    {
        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_UPDATE);
        command.voice_id = MUSIC_VOICE_ID;
        command.volume = TORIRS_AUDIO_VOLUME_MAX;
        command.pan = TORIRS_AUDIO_PAN_CENTRE;
        command.fade_ms = fade_in_ms;
        ToriRS_AudioQueue_Push(out, &command);
    }
}

static void
close_stream(
    struct ToriRS_MusicPlayer* player,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;

    if( !player->stream_open )
        return;
    if( out )
    {
        /* Unload rather than just stopping the voice: the generator borrows the
         * player's synth, and leaving it resident would let the mixer pull a
         * song that has been released. ASSET_UNLOAD stops the voice first. */
        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_UNLOAD);
        command.asset_id = MUSIC_ASSET_ID;
        ToriRS_AudioQueue_Push(out, &command);
    }
    player->stream_open = false;
}

void
ToriRS_Music_Installed(
    struct ToriRS_MusicPlayer* player,
    int song_id,
    enum ToriRS_MusicSource source,
    struct RSCache_MusicSong* song,
    const int* patch_ids,
    int patch_count,
    struct ToriRS_AudioQueue* out)
{
    if( !player )
    {
        RSCache_MusicSongFree(song);
        return;
    }
    if( !song )
    {
        ToriRS_Music_LoadFailed(player, song_id);
        return;
    }

    release_current(player);
    close_stream(player, out);

    player->song = song;
    player->current_song = song_id;
    player->current_source = source;
    player->current_loop = source == TORIRS_MUSIC_SOURCE_TRACK ? player->request_loop : false;
    player->retained_count = 0;
    for( int i = 0; i < patch_count && i < TORIRS_MUSIC_MAX_PATCHES; i++ )
        player->retained_patches[player->retained_count++] = patch_ids[i];

    if( !ToriRS_MidiSynth_PlayFrom(
            &player->synth,
            song->midi,
            song->midi_size,
            player->current_loop,
            player->request_resume_tick) )
    {
        MUSIC_TRACE("music: song %d is not a MIDI file after unpack\n", song_id);
        release_current(player);
        player->state = TORIRS_MUSIC_IDLE;
        player->has_request = false;
        player->request_loading = false;
        player->songs_failed++;
        return;
    }

    /* Consumed: an ordinary song change after this must start from the top. */
    player->request_resume_tick = 0;

    /*
     * The stream is opened on the next tick rather than here.
     *
     * Install is called from the load task, which has no command queue -- and
     * should not: a task that could queue platform commands would be able to
     * do it from the middle of an async walk, out of order with the tick that
     * owns the audio frame. So the player records what fade it wants and the
     * tick opens the stream with it.
     */
    player->pending_fade_in_ms = player->request_fade_in_ms;
    player->state = TORIRS_MUSIC_PLAYING;
    player->has_request = false;
    player->request_loading = false;
    player->songs_started++;
    /*
     * Check what the loader claims against what the bank holds.
     *
     * A song names its instruments and the loader retains one bank entry per
     * name; if a retained id is not findable, the soundfont did not assemble and
     * every note on that program will be dropped -- which is silence, or worse,
     * a song played entirely on whichever instrument did land. Neither is an
     * error anything downstream can report, so it is checked here, once, where
     * both numbers exist.
     */
    {
        int missing = 0;
        for( int i = 0; i < player->retained_count; i++ )
        {
            if( !ToriRS_SoundBank_FindPatch(&player->bank, player->retained_patches[i]) )
                missing++;
        }
        if( missing > 0 )
            fprintf(
                stderr,
                "music: song %d retained %d patches but %d are absent from the bank -- "
                "the soundfont did not assemble\n",
                song_id,
                player->retained_count,
                missing);
    }
    MUSIC_TRACE(
        "music: playing song %d (source %d, %d tracks, %d/%d patches, %d samples, "
        "%ld KB of PCM, loop %d)\n",
        song_id,
        (int)source,
        song->track_count,
        player->bank.patch_count,
        song->patch_count,
        player->bank.sample_count,
        player->bank.sample_bytes / 1024,
        (int)player->current_loop);
}

void
ToriRS_Music_LoadFailed(
    struct ToriRS_MusicPlayer* player,
    int song_id)
{
    if( !player )
        return;
    MUSIC_TRACE("music: song %d failed to load\n", song_id);
    player->songs_failed++;
    if( player->request_song == song_id )
    {
        player->has_request = false;
        player->request_loading = false;
    }
    if( player->state == TORIRS_MUSIC_LOADING )
        player->state = player->current_song >= 0 ? TORIRS_MUSIC_PLAYING : TORIRS_MUSIC_IDLE;
}

void
ToriRS_Music_Tick(
    struct ToriRS_MusicPlayer* player,
    const struct ToriRS_AudioFeedback* feedback,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;

    /* Kept in the signature because every caller passes it and `device_open`
     * may yet be worth reading here; the pacing fields it used to carry are
     * gone along with the pushing. */
    (void)feedback;

    if( !player )
        return;

    /* Fade-out before a switch. Counted in ticks so it is frame-rate
     * independent; the actual ramp is the stream's, applied by the mixer. */
    if( player->fade_ticks > 0 )
    {
        if( player->fade_ticks == (player->request_fade_out_ms + 19) / 20 && player->stream_open &&
            out )
        {
            ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STREAM_VOLUME);
            command.stream_id = player->stream_id;
            command.volume = 0;
            command.fade_ms = player->request_fade_out_ms;
            ToriRS_AudioQueue_Push(out, &command);
        }
        if( --player->fade_ticks == 0 )
        {
            release_current(player);
            close_stream(player, out);
            if( !player->has_request || player->request_song < 0 )
                player->state = TORIRS_MUSIC_IDLE;
        }
    }

    /* A non-looping song that has run out: hand back to whatever it interrupted. */
    if( player->state == TORIRS_MUSIC_PLAYING && ToriRS_MidiSynth_Finished(&player->synth) )
    {
        int resume = player->resume_song;
        bool resume_loop = player->resume_loop;

        MUSIC_TRACE("music: song %d ended\n", player->current_song);
        release_current(player);
        close_stream(player, out);
        player->state = TORIRS_MUSIC_IDLE;
        player->resume_song = -1;
        if( resume >= 0 )
            ToriRS_Music_Request(player, resume, TORIRS_MUSIC_SOURCE_TRACK, resume_loop, 0, 0);
    }

    if( player->state != TORIRS_MUSIC_PLAYING && player->state != TORIRS_MUSIC_FADING )
        return;
    /*
     * Publish once and stop. Everything after this is the backend pulling the
     * generator for exactly the frames it is about to play -- there is no
     * per-tick synthesis here any more, and therefore no way for a slow frame
     * to leave the device without samples.
     */
    if( !player->stream_open && player->current_song >= 0 )
        open_stream(player, out, player->pending_fade_in_ms);
}
