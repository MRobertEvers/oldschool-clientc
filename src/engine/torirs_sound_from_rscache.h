#ifndef TORIRS_SOUND_FROM_RSCACHE_H
#define TORIRS_SOUND_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

/**
 * Render a decoded cache sound effect into the neutral type the game plays.
 *
 * One adaptor for both containers: the record layout and the synthesis are the
 * same in every era, and `profile` is what tells rscache which flavour of each
 * to use (see sound_synth.h).
 *
 * Mutates `effect`: the lead-in trim (reference Wave.trim / SynthSound.getStart)
 * shifts the tone starts so the render begins at the first audible sample, and
 * the removed lead-in becomes ToriRS_Sound.queue_delay. Callers hand over a
 * decoded record they are done with.
 *
 * Returns NULL for an effect with no audible tone — a real case in every cache,
 * and the same "play nothing" the reference produces for an empty program.
 */
struct ToriRS_Sound*
ToriRS_SoundFromRSCache(
    const struct RSCache* profile,
    int sound_id,
    struct RSCache_SoundEffect* effect);

/**
 * How much of a sound effect's render one step of a background job does
 * (RSCache_SoundRenderStep's budget, in samples of work): a fraction of a
 * millisecond of processor time natively, whatever the effect.
 */
#define TORIRS_SOUND_RENDER_STEP_SAMPLES 4096

/**
 * ToriRS_SoundFromRSCache's last half, for a render made in steps: the sound
 * of an effect rendered to `pcm` (which it takes: `pcm->samples` is the
 * sound's, or freed), whose trim removed `queue_delay` ticks of lead-in.
 */
struct ToriRS_Sound*
ToriRS_SoundFromRSCachePcm(
    int sound_id,
    struct RSCache_SoundPcm* pcm,
    int queue_delay);

/** Transfer an exactly decoded index-14 recorded sample into a playable sound.
 * Ownership of `sample->samples` moves on success. */
struct ToriRS_Sound*
ToriRS_SoundFromRSCacheSample(
    int sound_id,
    struct RSCache_AudioSample* sample);

/**
 * The PCM to hand the platform for one play, with the effect's loop span
 * repeated `loop_count` times (reference Wave.generate's loop expansion, which
 * runs on the rendered bytes — so rendering once per id and expanding per play
 * gives the same bytes as the reference's render-per-play).
 *
 * Always returns a fresh buffer the caller frees, even when no expansion was
 * needed, so the caller has one ownership rule. *out_sample_count is set on
 * success; returns NULL if the sound has no PCM.
 */
uint8_t*
ToriRS_SoundExpandLoops(
    const struct ToriRS_Sound* sound,
    int loop_count,
    int* out_sample_count);

#endif
