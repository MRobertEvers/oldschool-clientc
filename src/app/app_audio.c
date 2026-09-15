/*
 * The audio entry points: a thin forward to the RS_Audio engine.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

void
App_PlaySound(
    struct App* app,
    int sound_id,
    int loops,
    int delay)
{
    assert(app);
    if( getenv("TORIRS_SOUND_DEBUG") )
        TORIRS_LOG("sound: synth=%d loops=%d delay=%d\n", sound_id, loops, delay);
    RS_Audio_Synth(&app->audio, sound_id, loops, delay);
}

void
App_PlaySoundAt(
    struct App* app,
    int sound_id,
    int loops,
    int delay,
    int tile_x,
    int tile_z,
    int radius,
    int inner)
{
    assert(app);
    RS_Audio_SynthAt(&app->audio, sound_id, loops, delay, tile_x, tile_z, radius, inner);
}

void
App_SetAudioFeedback(
    struct App* app,
    const struct ToriRS_AudioFeedback* feedback)
{
    assert(app);
    if( feedback )
        app->audio_feedback = *feedback;
    else
        memset(&app->audio_feedback, 0, sizeof(app->audio_feedback));
}

void
App_SetAudioDevicePresent(
    struct App* app,
    bool present)
{
    assert(app);
    RS_Audio_SetDevicePresent(&app->audio, present);
}

void
App_PlaySong(
    struct App* app,
    int song_id,
    bool loop,
    int fade_out_ms,
    int fade_in_ms)
{
    assert(app);
    RS_Audio_Song(&app->audio, song_id, loop, fade_out_ms, fade_in_ms);
}

void
App_PlaySongWithSecondary(
    struct App* app,
    int primary_id,
    int secondary_id,
    int fade_out_ms,
    int fade_in_ms)
{
    assert(app);
    RS_Audio_SongWithSecondary(&app->audio, primary_id, secondary_id, fade_out_ms, fade_in_ms);
}

void
App_SwapSong(
    struct App* app,
    int fade_out_ms,
    int fade_in_ms)
{
    assert(app);
    RS_Audio_SongSwap(&app->audio, fade_out_ms, fade_in_ms);
}

void
App_PlayJingle(
    struct App* app,
    int jingle_id,
    int length_ms)
{
    assert(app);
    RS_Audio_Jingle(&app->audio, jingle_id, length_ms);
}

void
App_StopSong(
    struct App* app,
    int fade_out_ms)
{
    assert(app);
    RS_Audio_SongStop(&app->audio, fade_out_ms);
}

void
App_SetAmbientSound(
    struct App* app,
    int sound_id,
    int fade_ms)
{
    assert(app);
    RS_Audio_SetAmbient(&app->audio, sound_id, fade_ms);
}

int
App_DrainAudio(
    struct App* app,
    struct ToriRS_AudioCommand* out,
    int max)
{
    assert(app);
    return ToriRS_AudioQueue_Drain(&app->audio_out, out, max);
}
