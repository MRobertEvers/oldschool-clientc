#include "engine/torirs_sound_from_rscache.h"

#include <stdlib.h>
#include <string.h>

struct ToriRS_Sound*
ToriRS_SoundFromRSCache(
    const struct RSCache* profile,
    int sound_id,
    struct RSCache_SoundEffect* effect)
{
    struct ToriRS_Sound* sound;
    struct RSCache_SoundPcm pcm;
    int queue_delay;

    if( !profile || !effect )
        return NULL;

    /* Trim before rendering: it moves the tone starts, which is what sizes the
     * render. Doing it after would render the silent lead-in and then throw the
     * offset away. */
    queue_delay = RSCache_SoundEffectTrim(effect);

    if( !RSCache_SoundEffectRender(profile, effect, &pcm) )
        return NULL;

    sound = calloc(1, sizeof(*sound));
    if( !sound )
    {
        RSCache_SoundPcmRelease(&pcm);
        return NULL;
    }

    sound->id = sound_id;
    sound->pcm = pcm.samples; /* ownership moves to the ToriRS type */
    sound->sample_count = pcm.sample_count;
    sound->sample_rate = pcm.sample_rate;
    sound->channels = 1;
    sound->loop_start = pcm.loop_start;
    sound->loop_end = pcm.loop_end;
    sound->queue_delay = queue_delay;
    return sound;
}

struct ToriRS_Sound*
ToriRS_SoundFromRSCacheSample(
    int sound_id,
    struct RSCache_AudioSample* sample)
{
    struct ToriRS_Sound* sound;

    if( !sample || !sample->samples || sample->sample_count <= 0 || sample->sample_rate <= 0 )
        return NULL;
    sound = calloc(1, sizeof(*sound));
    if( !sound )
        return NULL;
    sound->id = sound_id;
    sound->pcm16 = sample->samples;
    sample->samples = NULL;
    sound->sample_count = sample->sample_count;
    sound->sample_rate = sample->sample_rate;
    sound->channels = 1;
    sound->loop_start = sample->loop ? sample->loop_start : -1;
    sound->loop_end = sample->loop ? sample->loop_end : -1;
    sound->ping_pong = sample->loop;
    sound->queue_delay = 0;
    return sound;
}

uint8_t*
ToriRS_SoundExpandLoops(
    const struct ToriRS_Sound* sound,
    int loop_count,
    int* out_sample_count)
{
    struct RSCache_SoundPcm pcm;
    uint8_t* copy;

    if( !sound || !sound->pcm || sound->sample_count <= 0 )
        return NULL;

    copy = malloc((size_t)sound->sample_count);
    if( !copy )
        return NULL;
    memcpy(copy, sound->pcm, (size_t)sound->sample_count);

    memset(&pcm, 0, sizeof(pcm));
    pcm.samples = copy;
    pcm.sample_count = sound->sample_count;
    pcm.sample_rate = sound->sample_rate;
    pcm.loop_start = sound->loop_start;
    pcm.loop_end = sound->loop_end;

    if( !RSCache_SoundPcmExpandLoops(&pcm, loop_count) )
    {
        /* Expansion failed to grow the buffer (and so left it untouched); the
         * un-looped copy is still a correct single play. */
        if( out_sample_count )
            *out_sample_count = pcm.sample_count;
        return pcm.samples;
    }

    if( out_sample_count )
        *out_sample_count = pcm.sample_count;
    return pcm.samples;
}
