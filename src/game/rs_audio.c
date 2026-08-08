#include "rs_audio.h"

#include "audio/torirs_mixer.h"
#include "engine/cache_provider.h"
#include "task_runner.h"
#include "world/world.h"

#include <toridraw.h>
#include <toridraw_scene.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_TRACE(...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( ToriRS_AudioTraceEnabled() )                                                           \
            fprintf(stderr, __VA_ARGS__);                                                          \
    } while( 0 )

/**
 * Tiles at which a positional sound has faded to nothing when the loc did not
 * say. The reference's default audible radius for area sounds.
 */
#define AUDIO_DEFAULT_DISTANCE 12

void
RS_Audio_Init(struct RS_Audio* audio)
{
    assert(audio);
    memset(audio, 0, sizeof(*audio));
    audio->enabled = true;
    audio->effect_volume = RS_AUDIO_DEFAULT_VOLUME;
    audio->area_volume = RS_AUDIO_DEFAULT_VOLUME;
    audio->music_volume = RS_AUDIO_DEFAULT_VOLUME;
    audio->last_sound_id = -1;
    audio->last_loops = -1;
    audio->next_voice_id = 1;
    audio->area_generation = -1;
    ToriRS_Music_Init(&audio->music);
    ToriRS_Music_SetVolume(&audio->music, audio->music_volume, NULL);
}

void
RS_Audio_Shutdown(struct RS_Audio* audio)
{
    if( !audio )
        return;
    ToriRS_Music_Free(&audio->music);
}

static int
next_voice_id(struct RS_Audio* audio)
{
    int id = audio->next_voice_id++;
    if( audio->next_voice_id <= 0 )
        audio->next_voice_id = 1;
    return id;
}

/* --- effect queue --------------------------------------------------------- */

static void
queue_effect(
    struct RS_Audio* audio,
    int synth_id,
    int loops,
    int delay,
    int tile_x,
    int tile_z)
{
    struct RS_AudioEntry* entry;

    assert(audio);
    if( !audio->enabled || synth_id < 0 )
        return;
    if( audio->count >= RS_AUDIO_QUEUE_MAX )
    {
        audio->dropped_queue_full++;
        return;
    }

    entry = &audio->queue[audio->count++];
    memset(entry, 0, sizeof(*entry));
    entry->sound_id = synth_id;
    entry->loops = loops;
    entry->delay = delay;
    entry->tile_x = tile_x;
    entry->tile_z = tile_z;

    AUDIO_TRACE(
        "rs_audio: queued effect %d loops=%d delay=%d at (%d,%d)\n",
        synth_id,
        loops,
        delay,
        tile_x,
        tile_z);
}

void
RS_Audio_Synth(
    struct RS_Audio* audio,
    int synth_id,
    int loops,
    int delay)
{
    queue_effect(audio, synth_id, loops, delay, -1, -1);
}

void
RS_Audio_SynthAt(
    struct RS_Audio* audio,
    int synth_id,
    int loops,
    int delay,
    int tile_x,
    int tile_z)
{
    queue_effect(audio, synth_id, loops, delay, tile_x, tile_z);
}

static void
remove_entry(
    struct RS_Audio* audio,
    int index)
{
    for( int i = index; i + 1 < audio->count; i++ )
        audio->queue[i] = audio->queue[i + 1];
    audio->count--;
}

/** Clip length in client ticks (20ms each), for the overlap rule. */
static int
length_in_ticks(
    int sample_count,
    int sample_rate)
{
    if( sample_rate <= 0 )
        return 0;
    return (int)(((int64_t)sample_count * 1000) / ((int64_t)sample_rate * 20));
}

/* --- positional attenuation ----------------------------------------------- */

/**
 * Volume and pan for a sound at a tile, heard from the camera's tile.
 *
 * Linear falloff to zero at `distance` tiles, which is what the reference does
 * for area sounds; pan is the signed east-west offset over the same radius, so
 * a sound directly north of the listener is centred rather than silent.
 *
 * The camera's *yaw* is deliberately not used. The reference pans area sounds by
 * world offset and not by facing, and rotating the pan with the camera makes a
 * waterfall swing across the stereo field every time the player turns, which is
 * far more distracting than the fixed field being slightly wrong.
 */
static void
positional_gain(
    int tile_x,
    int tile_z,
    int camera_x,
    int camera_z,
    int distance,
    int base_volume,
    int* out_volume,
    int* out_pan)
{
    int dx = tile_x - camera_x;
    int dz = tile_z - camera_z;
    int magnitude;
    int falloff;

    if( distance <= 0 )
        distance = AUDIO_DEFAULT_DISTANCE;
    magnitude = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
    if( magnitude >= distance )
    {
        *out_volume = 0;
        *out_pan = TORIRS_AUDIO_PAN_CENTRE;
        return;
    }
    falloff = base_volume * (distance - magnitude) / distance;
    *out_volume = falloff;

    if( dx <= -distance )
        *out_pan = 0;
    else if( dx >= distance )
        *out_pan = TORIRS_AUDIO_PAN_MAX;
    else
        *out_pan = TORIRS_AUDIO_PAN_CENTRE + (dx * TORIRS_AUDIO_PAN_CENTRE) / distance;
}

/* --- asset publication ---------------------------------------------------- */

static void
drain_scene_events(
    struct RS_Audio* audio,
    struct ToriDraw_Scene* scene,
    struct ToriRS_AudioQueue* out);

/**
 * Make sure the scene holds a clip for `sound_id`, promoting the provider's
 * decode if it has one.
 *
 * The provider renders RS2-era effects to 8-bit unsigned (that is what the era's
 * synth produced); the scene and the mixer are 16-bit. Widening here, once per
 * id, is what lets everything downstream have one sample format.
 *
 * Returns the scene's clip, or NULL when nothing is resident yet.
 */
static struct ToriDraw_Sound*
publish_sound(
    struct RS_Audio* audio,
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    struct ToriRS_AudioQueue* out,
    int sound_id)
{
    struct ToriDraw_Sound* published;
    struct ToriRS_Sound* decoded;
    int16_t* widened;

    if( !scene )
        return NULL;
    published = ToriDraw_SceneSoundGet(scene, sound_id);
    if( published )
        return published;
    if( !provider )
        return NULL;
    decoded = CacheProvider_SoundGet(provider, sound_id);
    if( !decoded || !decoded->pcm || decoded->sample_count <= 0 )
        return NULL;

    widened = malloc((size_t)decoded->sample_count * sizeof(int16_t));
    if( !widened )
        return NULL;
    for( int i = 0; i < decoded->sample_count; i++ )
        widened[i] = (int16_t)(((int)decoded->pcm[i] - 128) << 8);

    ToriDraw_SceneSoundAdd(
        scene,
        sound_id,
        ToriDraw_SoundNew(
            widened,
            decoded->sample_count,
            decoded->sample_rate,
            decoded->loop_start,
            decoded->loop_end,
            decoded->queue_delay));
    audio->assets_published++;
    /*
     * Drain immediately, not on the next tick.
     *
     * A clip that becomes resident with no trim and no server delay is played
     * on this very tick, and the backend has to already hold the asset when
     * that VOICE_START arrives. Draining here is what makes "publish then play"
     * hold within a single tick; leaving it to the top of the next one made
     * every zero-delay dat1 effect a rejected voice -- silently, because the
     * game had done everything right and only the order was wrong.
     */
    drain_scene_events(audio, scene, out);
    return ToriDraw_SceneSoundGet(scene, sound_id);
}

/**
 * Turn the scene's sound load/unload events into asset commands.
 *
 * The scene's event queue is shared with the renderer, which clears it at the
 * end of its frame. This runs on the logic tick, before that, and keeps its own
 * cursor; a queue that shrank means the renderer cleared it and the cursor
 * restarts. Events the renderer does not understand (ours) it skips, and vice
 * versa, so the two consumers do not interfere.
 */
static void
drain_scene_events(
    struct RS_Audio* audio,
    struct ToriDraw_Scene* scene,
    struct ToriRS_AudioQueue* out)
{
    struct ToriDraw_EventQueue* events;
    struct ToriRS_AudioCommand command;

    if( !scene || !out )
        return;
    events = ToriDraw_SceneEvents(scene);
    if( !events )
        return;
    if( audio->scene_event_cursor > events->count )
        audio->scene_event_cursor = 0;

    while( audio->scene_event_cursor < events->count )
    {
        const struct ToriDraw_Event* event = &events->events[audio->scene_event_cursor++];

        if( event->kind == TORIDRAW_EVENT_SOUND_LOAD && event->sound )
        {
            ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_LOAD);
            command.asset_id = event->sound_id;
            command.pcm = event->sound->samples;
            command.sample_count = event->sound->sample_count;
            command.sample_rate = event->sound->sample_rate;
            command.loop_start = event->sound->loop_start;
            command.loop_end = event->sound->loop_end;
            command.source_id = event->sound_id;
            ToriRS_AudioQueue_Push(out, &command);
        }
        else if( event->kind == TORIDRAW_EVENT_SOUND_UNLOAD )
        {
            ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_UNLOAD);
            command.asset_id = event->sound_id;
            ToriRS_AudioQueue_Push(out, &command);
        }
        else if( event->kind == TORIDRAW_EVENT_SCENE_RESET )
        {
            ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_CLEAR);
            ToriRS_AudioQueue_Push(out, &command);
        }
    }
}

/* --- playing one due effect ----------------------------------------------- */

/**
 * Start a due entry, or decline it.
 *
 * The overlap rule is the reference's, restated in ticks: a clip is played only
 * if it would still be sounding when the one already playing ends. That keeps a
 * short effect from cutting off a long one, which is audible on the combat
 * sounds it was written for.
 */
static void
play_entry(
    struct RS_Audio* audio,
    const struct RS_AudioEntry* entry,
    const struct ToriDraw_Sound* sound,
    int camera_x,
    int camera_z,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;
    int ticks;
    int volume = audio->effect_volume;
    int pan = TORIRS_AUDIO_PAN_CENTRE;

    if( !out )
        return;

    ticks = length_in_ticks(sound->sample_count, sound->sample_rate);
    if( audio->tick + ticks <= audio->last_play_tick + audio->last_play_length_ticks )
    {
        audio->dropped_overlap++;
        AUDIO_TRACE(
            "rs_audio: effect %d skipped, %d-tick clip under the %d still playing\n",
            entry->sound_id,
            ticks,
            audio->last_play_tick + audio->last_play_length_ticks - audio->tick);
        return;
    }

    if( entry->tile_x >= 0 )
    {
        positional_gain(
            entry->tile_x,
            entry->tile_z,
            camera_x,
            camera_z,
            AUDIO_DEFAULT_DISTANCE,
            audio->effect_volume,
            &volume,
            &pan);
        if( volume <= 0 )
        {
            /* Out of earshot. Still counts against the overlap rule the way the
             * reference does, because the queue entry is consumed either way. */
            audio->dropped_absent++;
            return;
        }
    }

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
    command.asset_id = entry->sound_id;
    command.voice_id = next_voice_id(audio);
    command.bus = TORIRS_AUDIO_BUS_EFFECTS;
    command.volume = volume;
    command.pan = pan;
    command.loop_count = entry->loops > 1 ? entry->loops - 1 : 0;
    command.source_id = entry->sound_id;
    ToriRS_AudioQueue_Push(out, &command);

    audio->last_play_tick = audio->tick;
    audio->last_play_length_ticks = ticks * (entry->loops > 0 ? entry->loops : 1);
    audio->last_sound_id = entry->sound_id;
    audio->last_loops = entry->loops;
    audio->played++;

    AUDIO_TRACE(
        "rs_audio: play effect %d loops=%d vol=%d pan=%d (%d ticks)\n",
        entry->sound_id,
        entry->loops,
        volume,
        pan,
        audio->last_play_length_ticks);
}

static void
tick_effect_queue(
    struct RS_Audio* audio,
    struct CacheProvider* provider,
    struct TaskRunner* runner,
    struct ToriDraw_Scene* scene,
    int camera_x,
    int camera_z,
    struct ToriRS_AudioQueue* out)
{
    for( int index = 0; index < audio->count; index++ )
    {
        struct RS_AudioEntry* entry = &audio->queue[index];
        struct ToriDraw_Sound* sound =
            publish_sound(audio, provider, scene, out, entry->sound_id);

        if( !sound )
        {
            /* Ask once, then wait. Not counting down while waiting keeps the
             * server's delay meaningful: it is relative to the effect's own
             * start, which is not known until the effect is rendered. */
            if( !entry->load_requested && provider && runner )
            {
                struct ToriRS_Task* task = CreateTask_SoundLoad(provider, entry->sound_id);
                entry->load_requested = true;
                if( task )
                    ToriRS_TaskQueue_Add(runner->queue, task);
            }
            if( ++entry->waited >= RS_AUDIO_LOAD_WAIT_TICKS )
            {
                audio->dropped_absent++;
                AUDIO_TRACE(
                    "rs_audio: giving up on effect %d (never became resident)\n",
                    entry->sound_id);
                remove_entry(audio, index);
                index--;
            }
            continue;
        }

        if( !entry->delay_resolved )
        {
            /* Reference: waveDelay = delay + Wave.delays[id]. */
            entry->delay += sound->queue_delay;
            entry->delay_resolved = true;
        }
        if( entry->delay > 0 )
        {
            entry->delay--;
            continue;
        }

        play_entry(audio, entry, sound, camera_x, camera_z, out);
        remove_entry(audio, index);
        index--;
    }
}

/* --- area sounds ---------------------------------------------------------- */

static void
stop_area_voice(
    struct RS_Audio* audio,
    struct RS_AudioAreaVoice* voice,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;

    if( !voice->active )
        return;
    if( out && voice->voice_id > 0 )
    {
        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_STOP);
        command.voice_id = voice->voice_id;
        /* A short fade rather than a cut: an area loop stopped dead clicks, and
         * the scene rebuild that stops it is already a busy frame. */
        command.fade_ms = 60;
        ToriRS_AudioQueue_Push(out, &command);
    }
    memset(voice, 0, sizeof(*voice));
}

/**
 * Keep the live area voices matching the scene.
 *
 * Rebuilding the world replaces the emitter list wholesale, so every voice is
 * dropped and the nearest emitters are re-acquired. Within a generation the
 * voices persist and only their gain moves, which is what stops a waterfall
 * restarting every time the player takes a step.
 */
static void
tick_area_sounds(
    struct RS_Audio* audio,
    struct CacheProvider* provider,
    struct TaskRunner* runner,
    struct ToriDraw_Scene* scene,
    struct World* world,
    int camera_x,
    int camera_z,
    int camera_level,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;

    if( !world || !scene )
        return;

    if( audio->area_generation != world->area_sound_generation )
    {
        for( int i = 0; i < RS_AUDIO_MAX_AREA_VOICES; i++ )
            stop_area_voice(audio, &audio->area[i], out);
        audio->area_generation = world->area_sound_generation;
    }

    /* Acquire: fill free slots with the nearest audible emitters not already
     * playing. Nearest-first because the slot count is the budget and a distant
     * emitter contributes almost nothing. */
    for( int slot = 0; slot < RS_AUDIO_MAX_AREA_VOICES; slot++ )
    {
        int best = -1;
        int best_distance = 0;

        if( audio->area[slot].active )
            continue;
        for( int i = 0; i < world->area_sound_count; i++ )
        {
            const struct World_AreaSound* source = &world->area_sounds[i];
            int dx;
            int dz;
            int magnitude;
            bool taken = false;

            if( source->level != camera_level )
                continue;
            for( int j = 0; j < RS_AUDIO_MAX_AREA_VOICES; j++ )
            {
                if( audio->area[j].active && audio->area[j].source_index == i )
                    taken = true;
            }
            if( taken )
                continue;
            dx = source->x - camera_x;
            dz = source->z - camera_z;
            magnitude = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
            if( magnitude >= (source->distance > 0 ? source->distance : AUDIO_DEFAULT_DISTANCE) )
                continue;
            if( best < 0 || magnitude < best_distance )
            {
                best = i;
                best_distance = magnitude;
            }
        }
        if( best < 0 )
            break;

        {
            const struct World_AreaSound* source = &world->area_sounds[best];
            struct RS_AudioAreaVoice* voice = &audio->area[slot];
            memset(voice, 0, sizeof(*voice));
            voice->active = true;
            voice->source_index = best;
            voice->tile_x = source->x;
            voice->tile_z = source->z;
            voice->level = source->level;
            voice->distance = source->distance;
            voice->ticks_min = source->ticks_min;
            voice->ticks_max = source->ticks_max;
            voice->random_set = source->sound_id < 0 && source->sound_id_count > 0;
            voice->sound_id =
                voice->random_set ? source->sound_ids[0] : source->sound_id;
            voice->voice_id = 0;
            voice->ticks_until_next = 0;
        }
    }

    /* Drive: publish the clip, start or update the voice, retrigger the random
     * ones on their own schedule. */
    for( int slot = 0; slot < RS_AUDIO_MAX_AREA_VOICES; slot++ )
    {
        struct RS_AudioAreaVoice* voice = &audio->area[slot];
        struct ToriDraw_Sound* sound;
        int volume;
        int pan;

        if( !voice->active )
            continue;
        if( voice->sound_id < 0 )
        {
            stop_area_voice(audio, voice, out);
            continue;
        }

        sound = publish_sound(audio, provider, scene, out, voice->sound_id);
        if( !sound )
        {
            if( provider && runner )
            {
                struct ToriRS_Task* task = CreateTask_SoundLoad(provider, voice->sound_id);
                if( task )
                    ToriRS_TaskQueue_Add(runner->queue, task);
            }
            continue;
        }

        positional_gain(
            voice->tile_x,
            voice->tile_z,
            camera_x,
            camera_z,
            voice->distance,
            TORIRS_AUDIO_VOLUME_MAX,
            &volume,
            &pan);

        if( voice->random_set )
        {
            if( voice->ticks_until_next > 0 )
            {
                voice->ticks_until_next--;
                continue;
            }
            /*
             * Deterministic rather than random: the gap cycles through the
             * declared range. A pseudo-random gap would make a headless run
             * non-reproducible for no audible benefit -- the point of the range
             * is that repeated plays are not metronomic, and cycling achieves
             * that.
             */
            {
                int span = voice->ticks_max - voice->ticks_min;
                voice->ticks_until_next =
                    voice->ticks_min + (span > 0 ? (audio->tick % (span + 1)) : 0);
                if( voice->ticks_until_next <= 0 )
                    voice->ticks_until_next = 50;
            }
            if( volume <= 0 || !out )
                continue;
            ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
            command.asset_id = voice->sound_id;
            command.voice_id = next_voice_id(audio);
            command.bus = TORIRS_AUDIO_BUS_AREA;
            command.volume = volume;
            command.pan = pan;
            command.loop_count = 0;
            command.source_id = voice->sound_id;
            ToriRS_AudioQueue_Push(out, &command);
            continue;
        }

        if( voice->voice_id == 0 )
        {
            if( !out )
                continue;
            voice->voice_id = next_voice_id(audio);
            ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
            command.asset_id = voice->sound_id;
            command.voice_id = voice->voice_id;
            command.bus = TORIRS_AUDIO_BUS_AREA;
            command.volume = volume;
            command.pan = pan;
            command.loop_count = -1;
            command.source_id = voice->sound_id;
            ToriRS_AudioQueue_Push(out, &command);
            AUDIO_TRACE(
                "rs_audio: area sound %d at (%d,%d) vol=%d pan=%d\n",
                voice->sound_id,
                voice->tile_x,
                voice->tile_z,
                volume,
                pan);
        }
        else if( out )
        {
            ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_UPDATE);
            command.voice_id = voice->voice_id;
            command.volume = volume;
            command.pan = pan;
            /* Ramp over a tick so walking past an emitter is a slide, not a
             * staircase of 20ms steps. */
            command.fade_ms = 20;
            command.rate = 0;
            ToriRS_AudioQueue_Push(out, &command);
        }
    }
}

/* --- music ---------------------------------------------------------------- */

void
RS_Audio_Song(
    struct RS_Audio* audio,
    int song_id,
    bool loop,
    int fade_out_ms,
    int fade_in_ms)
{
    if( !audio )
        return;
    ToriRS_Music_Request(
        &audio->music, song_id, TORIRS_MUSIC_SOURCE_TRACK, loop, fade_out_ms, fade_in_ms);
}

void
RS_Audio_SongStop(
    struct RS_Audio* audio,
    int fade_out_ms)
{
    if( !audio )
        return;
    ToriRS_Music_Stop(&audio->music, fade_out_ms);
}

void
RS_Audio_Jingle(
    struct RS_Audio* audio,
    int jingle_id,
    int length_ms)
{
    if( !audio )
        return;
    /* `length_ms` is the server's hint at how long the jingle runs. It is not
     * needed to play one -- the sequencer knows when the track ends -- so it is
     * only traced. Trusting it would cut a jingle short on a slow load. */
    AUDIO_TRACE("rs_audio: jingle %d (server says %dms)\n", jingle_id, length_ms);
    ToriRS_Music_Request(&audio->music, jingle_id, TORIRS_MUSIC_SOURCE_JINGLE, false, 0, 0);
}

/* --- tick ----------------------------------------------------------------- */

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
    struct ToriRS_AudioQueue* out)
{
    assert(audio);

    audio->tick++;

    /* Assets first: a voice started this tick must name an asset the backend
     * already has, and commands are applied in the order they are queued. */
    drain_scene_events(audio, scene, out);

    tick_effect_queue(audio, provider, runner, scene, camera_tile_x, camera_tile_z, out);
    tick_area_sounds(
        audio,
        provider,
        runner,
        scene,
        world,
        camera_tile_x,
        camera_tile_z,
        camera_level,
        out);
    ToriRS_Music_Tick(&audio->music, feedback, out);
}

/* --- volume --------------------------------------------------------------- */

void
RS_Audio_SetBusVolume(
    struct RS_Audio* audio,
    enum ToriRS_AudioBus bus,
    int volume,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;

    if( !audio )
        return;
    if( volume < 0 )
        volume = 0;
    if( volume > TORIRS_AUDIO_VOLUME_MAX )
        volume = TORIRS_AUDIO_VOLUME_MAX;

    switch( bus )
    {
    case TORIRS_AUDIO_BUS_EFFECTS:
        audio->effect_volume = volume;
        break;
    case TORIRS_AUDIO_BUS_AREA:
        audio->area_volume = volume;
        break;
    case TORIRS_AUDIO_BUS_MUSIC:
        audio->music_volume = volume;
        ToriRS_Music_SetVolume(&audio->music, volume, out);
        return; /* the music player pushes its own bus command */
    default:
        return;
    }

    if( !out )
        return;
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_BUS_VOLUME);
    command.target_bus = bus;
    command.volume = volume;
    ToriRS_AudioQueue_Push(out, &command);
}

void
RS_Audio_SetVolumeLevel(
    struct RS_Audio* audio,
    int level,
    struct ToriRS_AudioQueue* out)
{
    /* The reference's four steps, on its 0..128 scale, doubled onto ours. */
    static const int VOLUME_BY_LEVEL[4] = { 255, 191, 128, 64 };
    int volume;

    assert(audio);
    if( level >= 0 && level <= 3 )
    {
        volume = VOLUME_BY_LEVEL[level];
        audio->enabled = true;
    }
    else if( level == 4 )
    {
        volume = 0;
        audio->enabled = false;
    }
    else
    {
        return;
    }

    RS_Audio_SetBusVolume(audio, TORIRS_AUDIO_BUS_EFFECTS, volume, out);
    RS_Audio_SetBusVolume(audio, TORIRS_AUDIO_BUS_AREA, volume, out);
    AUDIO_TRACE("rs_audio: effect volume level=%d -> %d\n", level, volume);
}

void
RS_Audio_StopAll(
    struct RS_Audio* audio,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;

    if( !audio )
        return;
    audio->count = 0;
    for( int i = 0; i < RS_AUDIO_MAX_AREA_VOICES; i++ )
        memset(&audio->area[i], 0, sizeof(audio->area[i]));
    audio->area_generation = -1;
    ToriRS_Music_Stop(&audio->music, 0);
    if( !out )
        return;
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STOP_ALL);
    ToriRS_AudioQueue_Push(out, &command);
}
