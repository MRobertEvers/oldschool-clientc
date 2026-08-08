#include "audio/torirs_music.h"

#include "audio/torirs_mixer.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** The mixer stream music uses. Stream 0 is music by convention; nothing else
 *  streams today, and a second one would be a second id here. */
#define MUSIC_STREAM_ID 0

/**
 * Frames rendered per tick, at most.
 *
 * One client tick is 20ms = 441 frames at 22050. Rendering a few ticks ahead
 * absorbs a slow frame without a gap; rendering much further ahead makes a
 * volume change or a song switch audibly late, because the change only takes
 * effect after everything already buffered has played. Four ticks is the
 * compromise: ~80ms of latency, which is below the threshold where a fade feels
 * detached from the action that caused it.
 */
#define MUSIC_TARGET_BUFFER_FRAMES (TORIRS_AUDIO_SAMPLE_RATE / 50 * 4)
#define MUSIC_MAX_RENDER_FRAMES (TORIRS_AUDIO_SAMPLE_RATE / 50 * 8)

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
    free(player->block);
    player->block = NULL;
    player->block_frames = 0;
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

static void
open_stream(
    struct ToriRS_MusicPlayer* player,
    struct ToriRS_AudioQueue* out,
    int fade_in_ms)
{
    struct ToriRS_AudioCommand command;

    if( !out )
        return;
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STREAM_OPEN);
    command.stream_id = player->stream_id;
    command.channels = 2;
    command.sample_rate = TORIRS_AUDIO_SAMPLE_RATE;
    command.bus = TORIRS_AUDIO_BUS_MUSIC;
    command.volume = fade_in_ms > 0 ? 0 : TORIRS_AUDIO_VOLUME_MAX;
    ToriRS_AudioQueue_Push(out, &command);
    player->stream_open = true;

    if( fade_in_ms > 0 )
    {
        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STREAM_VOLUME);
        command.stream_id = player->stream_id;
        command.volume = TORIRS_AUDIO_VOLUME_MAX;
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
        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STREAM_CLOSE);
        command.stream_id = player->stream_id;
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

    if( !ToriRS_MidiSynth_Play(&player->synth, song->midi, song->midi_size, player->current_loop) )
    {
        MUSIC_TRACE("music: song %d is not a MIDI file after unpack\n", song_id);
        release_current(player);
        player->state = TORIRS_MUSIC_IDLE;
        player->has_request = false;
        player->request_loading = false;
        player->songs_failed++;
        return;
    }

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
    MUSIC_TRACE(
        "music: playing song %d (source %d, %d tracks, %d patches, loop %d)\n",
        song_id,
        (int)source,
        song->track_count,
        patch_count,
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

static bool
ensure_block(
    struct ToriRS_MusicPlayer* player,
    int frames)
{
    int16_t* grown;

    if( player->block_frames >= frames )
        return true;
    grown = realloc(player->block, (size_t)frames * 2 * sizeof(int16_t));
    if( !grown )
        return false;
    player->block = grown;
    player->block_frames = frames;
    return true;
}

void
ToriRS_Music_Tick(
    struct ToriRS_MusicPlayer* player,
    const struct ToriRS_AudioFeedback* feedback,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;
    int buffered;
    int headroom;
    int wanted;

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
    if( !player->stream_open && player->current_song >= 0 )
        open_stream(player, out, player->pending_fade_in_ms);
    if( !feedback || !feedback->device_open || !player->stream_open )
        return;

    buffered = feedback->stream_buffered[player->stream_id & 3];
    headroom = feedback->stream_headroom[player->stream_id & 3];
    wanted = MUSIC_TARGET_BUFFER_FRAMES - buffered;
    if( wanted > MUSIC_MAX_RENDER_FRAMES )
        wanted = MUSIC_MAX_RENDER_FRAMES;
    if( wanted > headroom )
        wanted = headroom;
    if( wanted <= 0 )
        return;
    if( !ensure_block(player, wanted) )
        return;

    ToriRS_MidiSynth_Render(&player->synth, player->block, wanted);

    if( out )
    {
        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STREAM_PUSH);
        command.stream_id = player->stream_id;
        command.pcm = player->block;
        command.frame_count = wanted;
        command.channels = 2;
        ToriRS_AudioQueue_Push(out, &command);
        player->frames_pushed += wanted;
    }
}
