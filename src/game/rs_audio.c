#include "rs_audio.h"

#include "rs_soundscape.h"

#include "audio/torirs_mixer.h"
#include "engine/cache_provider.h"
#include "task_runner.h"
#include "features/features.h"
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
    audio->master_volume = RS_AUDIO_DEFAULT_VOLUME;
    audio->effect_volume = RS_AUDIO_DEFAULT_VOLUME;
    audio->area_volume = RS_AUDIO_DEFAULT_VOLUME;
    audio->music_volume = RS_AUDIO_DEFAULT_VOLUME;
    audio->last_sound_id = -1;
    audio->last_loops = -1;
    audio->ambient_sound_id = -1;
    audio->ambient_started = true;
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

void
RS_Audio_SetFeatures(
    struct RS_Audio* audio,
    struct ToriRS_FeatureTable const* features)
{
    if( !audio || !features )
        return;
    audio->effects_monophonic = features->effects_monophonic != 0;
    AUDIO_TRACE(
        "rs_audio: era '%s', effects %s\n",
        features->name ? features->name : "?",
        audio->effects_monophonic ? "monophonic" : "polyphonic");
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
    int tile_z,
    int radius,
    int inner)
{
    struct RS_AudioEntry* entry;

    assert(audio);
    if( !audio->enabled || synth_id < 0 )
        return;
    /*
     * Zero repeats is not "play once" -- the reference refuses the entry
     * outright (`Message.queueSoundEffect` requires `var1 != 0`), because the
     * count it eventually hands the mixer is `loops - 1` and zero means the
     * caller asked for nothing.
     */
    if( loops == 0 )
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
    entry->radius = radius;
    entry->inner = inner;

    AUDIO_TRACE(
        "rs_audio: queued effect %d loops=%d delay=%d at (%d,%d) radius=%d\n",
        synth_id,
        loops,
        delay,
        tile_x,
        tile_z,
        radius);
}

void
RS_Audio_Synth(
    struct RS_Audio* audio,
    int synth_id,
    int loops,
    int delay)
{
    queue_effect(audio, synth_id, loops, delay, -1, -1, 0, 0);
}

void
RS_Audio_SynthAt(
    struct RS_Audio* audio,
    int synth_id,
    int loops,
    int delay,
    int tile_x,
    int tile_z,
    int radius,
    int inner)
{
    queue_effect(audio, synth_id, loops, delay, tile_x, tile_z, radius, inner);
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

/** Fine units per tile. The reference works in these throughout; a tile centre
 *  is `tile * 128 + 64`, and an emitter's box spans `[x*128, (x+size)*128)`. */
#define AUDIO_FINE 128

/**
 * Distance from the listener to an emitter's footprint box, in fine units.
 *
 * The reference (`Statics.method10796`, osrs239 `deob/Statics.java:15309`, and
 * `AreaSoundManager.redraw` in rt4) clamps the listener against the box on each
 * axis independently and **sums** the two overshoots, then subtracts half a tile
 * and floors at zero.
 *
 * Two things about it are easy to get wrong and were both wrong here before.
 * It is a *box*, not a point: a four-tile waterfall is at distance zero from
 * anywhere along its length, so it does not get quieter as you walk past the
 * middle of it. And the metric is Manhattan, not Chebyshev -- an earlier change
 * to Chebyshev was really compensating for the missing box and the missing
 * half-tile dead zone, which between them are worth more than the metric.
 */
static int
box_distance_fine(
    int listener_x_fine,
    int listener_z_fine,
    int min_x_fine,
    int min_z_fine,
    int max_x_fine,
    int max_z_fine)
{
    int distance = 0;

    if( listener_x_fine < min_x_fine )
        distance += min_x_fine - listener_x_fine;
    else if( listener_x_fine > max_x_fine )
        distance += listener_x_fine - max_x_fine;

    if( listener_z_fine < min_z_fine )
        distance += min_z_fine - listener_z_fine;
    else if( listener_z_fine > max_z_fine )
        distance += listener_z_fine - max_z_fine;

    distance -= AUDIO_FINE / 2;
    return distance < 0 ? 0 : distance;
}

/**
 * Volume for a distance, given the audible radius and the full-volume inner
 * radius (all fine units).
 *
 * Reference (`class91.method3002`): flat at full volume out to `inner`, then
 * `(radius - d) / (radius - inner)` from there to `radius`, then silent. rt4 is
 * the same expression with `inner = 0`, which is what every era before OldSchool
 * 220 does and what a loc that declares no inner radius gets.
 *
 * There is deliberately **no floor**. An earlier version clamped this at a
 * quarter of full volume, which put a step exactly at the radius -- audible at
 * 25% one tile in, silent one tile out. The inner radius is the reference's
 * answer to "it should not fade the whole way in", and 54 osrs239 locs use it.
 */
static int
falloff_volume(
    int distance_fine,
    int radius_fine,
    int inner_fine,
    int base_volume)
{
    if( radius_fine <= 0 || distance_fine > radius_fine )
        return 0;
    if( inner_fine >= radius_fine || distance_fine <= inner_fine )
        return base_volume;
    return base_volume * (radius_fine - distance_fine) / (radius_fine - inner_fine);
}

/**
 * Stereo placement for an emitter, from its horizontal offset.
 *
 * The camera's *yaw* is deliberately not used. The reference pans by world
 * offset and not by facing, and rotating the pan with the camera makes a
 * waterfall swing across the stereo field every time the player turns, which is
 * far more distracting than the fixed field being slightly wrong.
 */
static int
pan_for_offset(
    int dx_fine,
    int radius_fine)
{
    if( radius_fine <= 0 )
        return TORIRS_AUDIO_PAN_CENTRE;
    if( dx_fine <= -radius_fine )
        return 0;
    if( dx_fine >= radius_fine )
        return TORIRS_AUDIO_PAN_MAX;
    return TORIRS_AUDIO_PAN_CENTRE + (dx_fine * TORIRS_AUDIO_PAN_CENTRE) / radius_fine;
}

/**
 * Volume and pan for a point emitter one tile across -- a frame sound, or a
 * SOUND_AREA. The box degenerates to the single tile it stands on, which is
 * what `addSequenceSoundEffect` does (it packs one tile, not a footprint).
 */
static void
positional_gain(
    int tile_x,
    int tile_z,
    int camera_x,
    int camera_z,
    int radius_tiles,
    int inner_tiles,
    int base_volume,
    int* out_volume,
    int* out_pan)
{
    int listener_x = camera_x * AUDIO_FINE + AUDIO_FINE / 2;
    int listener_z = camera_z * AUDIO_FINE + AUDIO_FINE / 2;
    int min_x = tile_x * AUDIO_FINE;
    int min_z = tile_z * AUDIO_FINE;
    int radius_fine;
    int distance;

    if( radius_tiles <= 0 )
        radius_tiles = AUDIO_DEFAULT_DISTANCE;
    radius_fine = radius_tiles * AUDIO_FINE;
    distance =
        box_distance_fine(listener_x, listener_z, min_x, min_z, min_x + AUDIO_FINE, min_z + AUDIO_FINE);

    *out_volume = falloff_volume(distance, radius_fine, inner_tiles * AUDIO_FINE, base_volume);
    *out_pan = pan_for_offset(min_x + AUDIO_FINE / 2 - listener_x, radius_fine);
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
    int volume = TORIRS_AUDIO_VOLUME_MAX;
    int pan = TORIRS_AUDIO_PAN_CENTRE;

    if( !out )
        return;

    ticks = length_in_ticks(sound->sample_count, sound->sample_rate);
    if( audio->effects_monophonic &&
        audio->tick + ticks <= audio->last_play_tick + audio->last_play_length_ticks )
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
            entry->radius,
            entry->inner,
            TORIRS_AUDIO_VOLUME_MAX,
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
    /*
     * The reference's loop, in its order, because the order *is* the behaviour
     * (HealthBarUpdate.method1769):
     *
     *     delays[i]--;                       // always, before anything else
     *     if (delays[i] >= -10) {
     *         if (clip == null) {
     *             clip = read(id); if (clip == null) continue;
     *             delays[i] += clip.calculateDelay();
     *         }
     *         if (delays[i] < 0) { play(i); delays[i] = -100; }
     *     } else remove(i);
     *
     * Decrementing first is not a detail. Holding the countdown until the clip
     * arrives makes every millisecond of load latency a millisecond of lateness,
     * which is heard as the first swing of a weapon landing behind its animation
     * and every later swing being fine.
     */
    for( int index = 0; index < audio->count; index++ )
    {
        struct RS_AudioEntry* entry = &audio->queue[index];
        struct ToriDraw_Sound* sound;

        entry->delay--;

        if( entry->delay < -RS_AUDIO_LATE_LIMIT_TICKS )
        {
            audio->dropped_absent++;
            AUDIO_TRACE(
                "rs_audio: dropping effect %d, %d ticks late\n",
                entry->sound_id,
                -entry->delay);
            remove_entry(audio, index);
            index--;
            continue;
        }

        sound = publish_sound(audio, provider, scene, out, entry->sound_id);
        if( !sound )
        {
            /* Ask once, then let the countdown carry on without it. */
            if( !entry->load_requested && provider && runner )
            {
                struct ToriRS_Task* task = CreateTask_SoundLoad(provider, entry->sound_id);
                entry->load_requested = true;
                if( task )
                    ToriRS_TaskQueue_Add(runner->queue, task);
            }
            continue;
        }

        if( !entry->delay_resolved )
        {
            /* Reference: `delays[i] += clip.calculateDelay()` -- the silent
             * lead-in the render trimmed off the front, added back as ticks so
             * the audible onset lands where the server asked for it. */
            entry->delay += sound->queue_delay;
            entry->delay_resolved = true;
        }
        if( entry->delay >= 0 )
            continue;

        play_entry(audio, entry, sound, camera_x, camera_z, out);
        remove_entry(audio, index);
        index--;
    }
}

/* --- area sounds ---------------------------------------------------------- */

/**
 * Per-emitter PRNG.
 *
 * The reference picks the random set's next sound and its next gap with
 * `Math.random()`. Using the global tick instead (which is what this did) makes
 * every emitter in the scene fire on the same schedule and always pick
 * `sound_ids[0]`, so a forest is one bird repeating. A tiny LCG seeded from the
 * emitter's identity keeps them independent *and* keeps a headless run
 * reproducible, which `Math.random()` would not.
 */
static uint32_t
area_rand(struct RS_AudioAreaVoice* voice)
{
    voice->rng = voice->rng * 1664525u + 1013904223u;
    return voice->rng >> 16;
}

/** The same, for a soundscape's random sets. */
static uint32_t
ambient_rand(struct RS_AudioAmbientSet* set)
{
    set->rng = set->rng * 1664525u + 1013904223u;
    return set->rng >> 16;
}

static void
stop_area_stream(
    int* voice_id,
    struct ToriRS_AudioQueue* out,
    int fade_ms)
{
    struct ToriRS_AudioCommand command;

    if( *voice_id <= 0 )
        return;
    if( out )
    {
        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_STOP);
        command.voice_id = *voice_id;
        command.fade_ms = fade_ms;
        ToriRS_AudioQueue_Push(out, &command);
    }
    *voice_id = 0;
}

static void
stop_area_voice(
    struct RS_AudioAreaVoice* voice,
    struct ToriRS_AudioQueue* out)
{
    if( !voice->active )
        return;
    /* A short fade rather than a cut: an area loop stopped dead clicks, and the
     * scene rebuild that stops it is already a busy frame. The reference ramps
     * to zero over 150ms when an emitter goes out of range. */
    stop_area_stream(&voice->primary_voice_id, out, RS_AUDIO_AREA_FADE_MS);
    memset(voice, 0, sizeof(*voice));
}

/** Volume and pan for an emitter's footprint box, from the camera's tile. */
static void
area_gain(
    const struct RS_AudioAreaVoice* voice,
    int camera_x,
    int camera_z,
    int* out_volume,
    int* out_pan)
{
    int listener_x = camera_x * AUDIO_FINE + AUDIO_FINE / 2;
    int listener_z = camera_z * AUDIO_FINE + AUDIO_FINE / 2;
    int min_x = voice->min_x * AUDIO_FINE;
    int min_z = voice->min_z * AUDIO_FINE;
    int max_x = voice->max_x * AUDIO_FINE;
    int max_z = voice->max_z * AUDIO_FINE;
    int radius_fine = (voice->radius > 0 ? voice->radius : AUDIO_DEFAULT_DISTANCE) * AUDIO_FINE;
    int inner_fine = voice->inner * AUDIO_FINE;
    int distance = box_distance_fine(listener_x, listener_z, min_x, min_z, max_x, max_z);
    int centre_x = (min_x + max_x) / 2;

    *out_volume = falloff_volume(distance, radius_fine, inner_fine, TORIRS_AUDIO_VOLUME_MAX);
    *out_pan = pan_for_offset(centre_x - listener_x, radius_fine);
}

/** Distance in tiles from the camera to an emitter's box, for slot ranking. */
static int
source_distance_tiles(
    const struct World_AreaSound* source,
    int camera_x,
    int camera_z)
{
    return box_distance_fine(
               camera_x * AUDIO_FINE + AUDIO_FINE / 2,
               camera_z * AUDIO_FINE + AUDIO_FINE / 2,
               source->x * AUDIO_FINE,
               source->z * AUDIO_FINE,
               (source->x + source->size_x) * AUDIO_FINE,
               (source->z + source->size_z) * AUDIO_FINE) /
           AUDIO_FINE;
}

/** Is this live voice the same emitter as this scene record? */
static bool
voice_matches_source(
    const struct RS_AudioAreaVoice* voice,
    const struct World_AreaSound* source)
{
    return voice->loc_id == source->loc_id && voice->level == source->level &&
           voice->min_x == source->x && voice->min_z == source->z;
}

static void
bind_voice(
    struct RS_AudioAreaVoice* voice,
    const struct World_AreaSound* source,
    int source_index)
{
    voice->source_index = source_index;
    voice->loc_id = source->loc_id;
    voice->min_x = source->x;
    voice->min_z = source->z;
    voice->max_x = source->x + (source->size_x > 0 ? source->size_x : 1);
    voice->max_z = source->z + (source->size_z > 0 ? source->size_z : 1);
    voice->level = source->level;
    voice->radius = source->distance;
    voice->inner = source->inner;
    voice->primary_sound = source->sound_id;
    voice->set_ids = source->sound_ids;
    voice->set_count = source->sound_id_count;
    voice->ticks_min = source->ticks_min;
    voice->ticks_max = source->ticks_max;
}

/**
 * Keep the live area voices matching the scene.
 *
 * A rebuild replaces the emitter list wholesale, but the *world* mostly did not
 * change: crossing a chunk boundary re-emits the same waterfall at the same
 * place. Voices are therefore re-bound to the emitter they already match rather
 * than stopped and restarted, which is what the reference does (it keys loc
 * emitters by level/x/z/locType.id and removes only what actually left).
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
        /* Re-bind what survived, stop what did not. `source_index` is only
         * meaningful within a generation, so every voice is re-resolved here. */
        for( int slot = 0; slot < RS_AUDIO_MAX_AREA_VOICES; slot++ )
        {
            struct RS_AudioAreaVoice* voice = &audio->area[slot];
            int found = -1;

            if( !voice->active )
                continue;
            for( int i = 0; i < world->area_sound_count && found < 0; i++ )
            {
                if( voice_matches_source(voice, &world->area_sounds[i]) )
                    found = i;
            }
            if( found < 0 )
            {
                stop_area_voice(voice, out);
                continue;
            }
            /*
             * Re-bind rather than re-create: a multiloc that changed state
             * points at a different sound now, and the reference's
             * `AreaSound.update()` stops the primary stream exactly when the id
             * changes and leaves it alone when it did not.
             */
            if( voice->primary_sound != world->area_sounds[found].sound_id )
                stop_area_stream(&voice->primary_voice_id, out, RS_AUDIO_AREA_FADE_MS);
            bind_voice(voice, &world->area_sounds[found], found);
        }
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
            int distance;
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
            distance = source_distance_tiles(source, camera_x, camera_z);
            if( distance >= (source->distance > 0 ? source->distance : AUDIO_DEFAULT_DISTANCE) )
                continue;
            if( best < 0 || distance < best_distance )
            {
                best = i;
                best_distance = distance;
            }
        }
        if( best < 0 )
            break;

        {
            const struct World_AreaSound* source = &world->area_sounds[best];
            struct RS_AudioAreaVoice* voice = &audio->area[slot];
            memset(voice, 0, sizeof(*voice));
            voice->active = true;
            bind_voice(voice, source, best);
            /* Seed from the emitter's place in the world, not from the slot: the
             * same waterfall re-acquired into a different slot keeps its
             * rhythm. */
            voice->rng = (uint32_t)(source->x * 73856093) ^ (uint32_t)(source->z * 19349663) ^
                         (uint32_t)(source->loc_id * 83492791);
            voice->ticks_until_next = 0;
        }
    }

    /* Drive: publish the clips, hold the continuous stream's gain on the camera,
     * and fire the random set on its own schedule. Both streams run at once --
     * `bgsound` and `bgsounds` are independent fields and a loc that declares
     * both hums *and* clanks. */
    for( int slot = 0; slot < RS_AUDIO_MAX_AREA_VOICES; slot++ )
    {
        struct RS_AudioAreaVoice* voice = &audio->area[slot];
        int volume;
        int pan;

        if( !voice->active )
            continue;
        if( voice->primary_sound < 0 && voice->set_count <= 0 )
        {
            stop_area_voice(voice, out);
            continue;
        }

        area_gain(voice, camera_x, camera_z, &volume, &pan);

        /* Out of range: the reference ramps both streams to zero over 150ms and
         * keeps the emitter, rather than dropping it and restarting on the way
         * back in. */
        if( volume <= 0 || voice->level != camera_level )
        {
            stop_area_stream(&voice->primary_voice_id, out, RS_AUDIO_AREA_FADE_MS);
            continue;
        }

        /* --- the continuous stream --- */
        if( voice->primary_sound >= 0 )
        {
            struct ToriDraw_Sound* sound =
                publish_sound(audio, provider, scene, out, voice->primary_sound);

            if( !sound )
            {
                if( provider && runner )
                {
                    struct ToriRS_Task* task =
                        CreateTask_SoundLoad(provider, voice->primary_sound);
                    if( task )
                        ToriRS_TaskQueue_Add(runner->queue, task);
                }
            }
            else if( voice->primary_voice_id == 0 && out )
            {
                voice->primary_voice_id = next_voice_id(audio);
                ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
                command.asset_id = voice->primary_sound;
                command.voice_id = voice->primary_voice_id;
                command.bus = TORIRS_AUDIO_BUS_AREA;
                command.volume = volume;
                command.pan = pan;
                command.loop_count = -1;
                command.source_id = voice->primary_sound;
                command.fade_ms = RS_AUDIO_AREA_FADE_MS;
                ToriRS_AudioQueue_Push(out, &command);
                AUDIO_TRACE(
                    "rs_audio: area loop %d box=(%d,%d)-(%d,%d) r=%d inner=%d vol=%d pan=%d\n",
                    voice->primary_sound,
                    voice->min_x,
                    voice->min_z,
                    voice->max_x,
                    voice->max_z,
                    voice->radius,
                    voice->inner,
                    volume,
                    pan);
            }
            else if( voice->primary_voice_id != 0 && out )
            {
                ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_UPDATE);
                command.voice_id = voice->primary_voice_id;
                command.volume = volume;
                command.pan = pan;
                /* Ramp over a tick so walking past an emitter is a slide, not a
                 * staircase of 20ms steps. */
                command.fade_ms = 20;
                command.rate = 0;
                ToriRS_AudioQueue_Push(out, &command);
            }
        }

        /* --- the random set --- */
        if( voice->set_count > 0 && voice->set_ids )
        {
            int pick;
            struct ToriDraw_Sound* sound;

            if( voice->ticks_until_next > 0 )
            {
                voice->ticks_until_next--;
                continue;
            }

            pick = voice->set_ids[area_rand(voice) % (uint32_t)voice->set_count];
            sound = publish_sound(audio, provider, scene, out, pick);
            if( !sound )
            {
                /* Ask for it and try again next tick. Not rescheduling here is
                 * deliberate: the gap is measured from the last *play*. */
                if( provider && runner )
                {
                    struct ToriRS_Task* task = CreateTask_SoundLoad(provider, pick);
                    if( task )
                        ToriRS_TaskQueue_Add(runner->queue, task);
                }
                continue;
            }

            {
                int span = voice->ticks_max - voice->ticks_min;
                voice->ticks_until_next =
                    voice->ticks_min + (span > 0 ? (int)(area_rand(voice) % (uint32_t)span) : 0);
                if( voice->ticks_until_next <= 0 )
                    voice->ticks_until_next = RS_AUDIO_AREA_SET_FALLBACK_TICKS;
            }
            if( !out )
                continue;
            ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
            command.asset_id = pick;
            command.voice_id = next_voice_id(audio);
            command.bus = TORIRS_AUDIO_BUS_AREA;
            command.volume = volume;
            command.pan = pan;
            command.loop_count = 0;
            command.source_id = pick;
            ToriRS_AudioQueue_Push(out, &command);
            AUDIO_TRACE(
                "rs_audio: area one-shot %d, next in %d ticks\n", pick, voice->ticks_until_next);
        }
    }
}

/* --- the region's background bed ------------------------------------------ */

void
RS_Audio_SetSoundscapes(
    struct RS_Audio* audio,
    struct RS_Soundscapes const* soundscapes)
{
    if( !audio )
        return;
    audio->soundscapes = soundscapes;
    /* A bed already playing was resolved against the old table -- which, at
     * boot, is the empty one. Re-resolve it rather than leaving the region
     * humming the fallback for as long as the player stays in it. */
    audio->ambient_started = false;
}

void
RS_Audio_SetAmbient(
    struct RS_Audio* audio,
    int id,
    int fade_ms)
{
    if( !audio )
        return;
    if( audio->ambient_sound_id == id )
        return;
    audio->ambient_sound_id = id;
    audio->ambient_fade_ms = fade_ms;
    /* Clear the started flag so the tick re-acquires: the clips may not be
     * resident yet, and the tick is where loads are requested. */
    audio->ambient_started = false;
    AUDIO_TRACE("rs_audio: ambient soundscape %d (fade %dms)\n", id, fade_ms);
}

/**
 * Keep the background bed sounding.
 *
 * Unpositioned and unattenuated -- it is the region's bed, not a thing standing
 * somewhere. It shares the area bus with the loc emitters so one setting covers
 * both, which is how the reference's "area sound" slider behaves.
 */
static void
stop_ambient_streams(
    struct RS_Audio* audio,
    struct ToriRS_AudioQueue* out)
{
    stop_area_stream(&audio->ambient_legacy_voice_id, out, audio->ambient_fade_ms);
    for( int i = 0; i < audio->ambient_loop_count; i++ )
        stop_area_stream(&audio->ambient_loop_voice_id[i], out, audio->ambient_fade_ms);
    audio->ambient_loop_count = 0;
    audio->ambient_set_count = 0;
}

/**
 * Bind the soundscape record `id` names, or fall back to the legacy reading.
 *
 * Returns true when a soundscape was bound. False means either "no table" (a
 * cache older than OldSchool 231) or "no such record", and both take the
 * pre-231 path: the id is a sound-effect id and the bed is that one clip
 * looping.
 */
static bool
bind_ambient_soundscape(
    struct RS_Audio* audio,
    int id)
{
    const struct RS_Soundscape* scape = RS_Soundscapes_Get(audio->soundscapes, id);

    if( !scape )
        return false;

    audio->ambient_loop_count =
        scape->loop_count < RS_AUDIO_MAX_AMBIENT_LOOPS ? scape->loop_count
                                                       : RS_AUDIO_MAX_AMBIENT_LOOPS;
    for( int i = 0; i < audio->ambient_loop_count; i++ )
    {
        audio->ambient_loop_sound_id[i] = scape->loop_ids[i];
        audio->ambient_loop_voice_id[i] = 0;
    }

    audio->ambient_set_count =
        scape->set_count < RS_AUDIO_MAX_AMBIENT_SETS ? scape->set_count
                                                     : RS_AUDIO_MAX_AMBIENT_SETS;
    for( int i = 0; i < audio->ambient_set_count; i++ )
    {
        struct RS_AudioAmbientSet* set = &audio->ambient_sets[i];
        set->ids = scape->sets[i].ids;
        set->id_count = scape->sets[i].id_count;
        /* The cache states the gap in milliseconds; the tick is 20ms. */
        set->ticks_min = scape->sets[i].min_ms / 20;
        set->ticks_max = scape->sets[i].max_ms / 20;
        /* Seeded per (soundscape, set) so the sets do not fire in lockstep and
         * the same region sounds the same on a headless replay. */
        set->rng = (uint32_t)(id * 2654435761u) ^ (uint32_t)((i + 1) * 40503u);
        /* Staggered first fire: without this every set in a soundscape speaks
         * on the tick the region loads, which is the one moment they should
         * not all speak at once. */
        set->ticks_until_next =
            set->ticks_min +
            (set->ticks_max > set->ticks_min
                 ? (int)(ambient_rand(set) % (uint32_t)(set->ticks_max - set->ticks_min))
                 : 0);
    }

    if( scape->fade_in_ms > 0 )
        audio->ambient_fade_ms = scape->fade_in_ms;
    return true;
}

/**
 * The region's background bed.
 *
 * Unpositioned and unattenuated -- it is the region's bed, not a thing standing
 * somewhere. It shares the area bus with the loc emitters so one setting covers
 * both, which is how the reference's "area sound" slider behaves.
 *
 * Unlike the loc emitters this runs every tick rather than once: a soundscape's
 * random sets each have their own countdown, and a loop whose clip was not yet
 * resident has to be retried.
 */
static void
tick_ambient_bed(
    struct RS_Audio* audio,
    struct CacheProvider* provider,
    struct TaskRunner* runner,
    struct ToriDraw_Scene* scene,
    struct ToriRS_AudioQueue* out)
{
    struct ToriRS_AudioCommand command;

    /* A new id: tear the old bed down and resolve the new one, once. */
    if( !audio->ambient_started )
    {
        stop_ambient_streams(audio, out);
        audio->ambient_started = true;
        if( audio->ambient_sound_id < 0 )
            return;
        if( !bind_ambient_soundscape(audio, audio->ambient_sound_id) )
        {
            /* Pre-231 reading: the id is one looping sound effect. Modelled as a
             * single-loop soundscape so the drive loop below is the only place
             * that starts a voice. */
            audio->ambient_loop_count = 1;
            audio->ambient_loop_sound_id[0] = audio->ambient_sound_id;
            audio->ambient_loop_voice_id[0] = 0;
            audio->ambient_set_count = 0;
        }
        AUDIO_TRACE(
            "rs_audio: ambient bed %d -> %d loops, %d sets\n",
            audio->ambient_sound_id,
            audio->ambient_loop_count,
            audio->ambient_set_count);
    }

    if( audio->ambient_sound_id < 0 )
        return;

    /* --- the continuous loops --- */
    for( int i = 0; i < audio->ambient_loop_count; i++ )
    {
        struct ToriDraw_Sound* sound;

        if( audio->ambient_loop_voice_id[i] > 0 )
            continue;
        sound = publish_sound(audio, provider, scene, out, audio->ambient_loop_sound_id[i]);
        if( !sound )
        {
            if( provider && runner )
            {
                struct ToriRS_Task* task =
                    CreateTask_SoundLoad(provider, audio->ambient_loop_sound_id[i]);
                if( task )
                    ToriRS_TaskQueue_Add(runner->queue, task);
            }
            continue; /* try again next tick */
        }
        if( !out )
            continue;

        audio->ambient_loop_voice_id[i] = next_voice_id(audio);
        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
        command.asset_id = audio->ambient_loop_sound_id[i];
        command.voice_id = audio->ambient_loop_voice_id[i];
        command.bus = TORIRS_AUDIO_BUS_AREA;
        command.volume = TORIRS_AUDIO_VOLUME_MAX;
        command.pan = TORIRS_AUDIO_PAN_CENTRE;
        command.loop_count = -1;
        command.source_id = audio->ambient_loop_sound_id[i];
        command.fade_ms = audio->ambient_fade_ms;
        ToriRS_AudioQueue_Push(out, &command);
        AUDIO_TRACE("rs_audio: ambient loop %d playing\n", audio->ambient_loop_sound_id[i]);
    }

    /* --- the timed random sets --- */
    for( int i = 0; i < audio->ambient_set_count; i++ )
    {
        struct RS_AudioAmbientSet* set = &audio->ambient_sets[i];
        struct ToriDraw_Sound* sound;
        int pick;

        if( set->id_count <= 0 || !set->ids )
            continue;
        if( set->ticks_until_next > 0 )
        {
            set->ticks_until_next--;
            continue;
        }

        /* Uniform over the list -- the format weights by *repeating* an id, so
         * a set listing silence six times and a drip three times drips a third
         * of the time. Weighting the pick as well would square that. */
        pick = set->ids[ambient_rand(set) % (uint32_t)set->id_count];
        sound = publish_sound(audio, provider, scene, out, pick);
        if( !sound )
        {
            if( provider && runner )
            {
                struct ToriRS_Task* task = CreateTask_SoundLoad(provider, pick);
                if( task )
                    ToriRS_TaskQueue_Add(runner->queue, task);
            }
            continue;
        }

        {
            int span = set->ticks_max - set->ticks_min;
            set->ticks_until_next =
                set->ticks_min + (span > 0 ? (int)(ambient_rand(set) % (uint32_t)span) : 0);
            if( set->ticks_until_next <= 0 )
                set->ticks_until_next = RS_AUDIO_AREA_SET_FALLBACK_TICKS;
        }
        if( !out )
            continue;
        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
        command.asset_id = pick;
        command.voice_id = next_voice_id(audio);
        command.bus = TORIRS_AUDIO_BUS_AREA;
        command.volume = TORIRS_AUDIO_VOLUME_MAX;
        command.pan = TORIRS_AUDIO_PAN_CENTRE;
        command.loop_count = 0;
        command.source_id = pick;
        ToriRS_AudioQueue_Push(out, &command);
        AUDIO_TRACE(
            "rs_audio: ambient one-shot %d, next in %d ticks\n", pick, set->ticks_until_next);
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
RS_Audio_SongWithSecondary(
    struct RS_Audio* audio,
    int primary_id,
    int secondary_id,
    int fade_out_ms,
    int fade_in_ms)
{
    if( !audio )
        return;
    ToriRS_Music_Request(
        &audio->music, primary_id, TORIRS_MUSIC_SOURCE_TRACK, true, fade_out_ms, fade_in_ms);
    /* After the request: an ordinary track request is what clears any variant
     * left over from a previous region, and this one has to survive it. */
    ToriRS_Music_SetSecondary(&audio->music, secondary_id);
}

void
RS_Audio_SongSwap(
    struct RS_Audio* audio,
    int fade_out_ms,
    int fade_in_ms)
{
    if( !audio )
        return;
    ToriRS_Music_Swap(&audio->music, fade_out_ms, fade_in_ms);
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
    tick_ambient_bed(audio, provider, runner, scene, out);
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
    int effective;

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
        audio->enabled = volume > 0;
        break;
    case TORIRS_AUDIO_BUS_AREA:
        audio->area_volume = volume;
        break;
    case TORIRS_AUDIO_BUS_MUSIC:
        audio->music_volume = volume;
        effective = volume * audio->master_volume / TORIRS_AUDIO_VOLUME_MAX;
        ToriRS_Music_SetVolume(&audio->music, effective, out);
        return; /* the music player pushes its own bus command */
    default:
        return;
    }

    if( !out )
        return;
    effective = volume * audio->master_volume / TORIRS_AUDIO_VOLUME_MAX;
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_BUS_VOLUME);
    command.target_bus = bus;
    command.volume = effective;
    ToriRS_AudioQueue_Push(out, &command);
}

void
RS_Audio_SetMasterVolume(
    struct RS_Audio* audio,
    int volume,
    struct ToriRS_AudioQueue* out)
{
    int effects;
    int area;
    int music;

    if( !audio )
        return;
    if( volume < 0 )
        volume = 0;
    if( volume > TORIRS_AUDIO_VOLUME_MAX )
        volume = TORIRS_AUDIO_VOLUME_MAX;
    effects = audio->effect_volume;
    area = audio->area_volume;
    music = audio->music_volume;
    audio->master_volume = volume;
    RS_Audio_SetBusVolume(audio, TORIRS_AUDIO_BUS_EFFECTS, effects, out);
    RS_Audio_SetBusVolume(audio, TORIRS_AUDIO_BUS_AREA, area, out);
    RS_Audio_SetBusVolume(audio, TORIRS_AUDIO_BUS_MUSIC, music, out);
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
    audio->ambient_sound_id = -1;
    stop_ambient_streams(audio, NULL);
    audio->ambient_started = true;
    for( int i = 0; i < RS_AUDIO_MAX_AREA_VOICES; i++ )
        memset(&audio->area[i], 0, sizeof(audio->area[i]));
    audio->area_generation = -1;
    ToriRS_Music_Stop(&audio->music, 0);
    if( !out )
        return;
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STOP_ALL);
    ToriRS_AudioQueue_Push(out, &command);
}
