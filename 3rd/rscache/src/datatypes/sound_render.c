#include "sound_render.h"
#include "../rscache_alloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Waveform lookup tables, shared by every render. 32768 entries each, exactly
 * as the reference builds them at static-init time. */
#define SOUND_TABLE_SIZE 32768

static int g_sound_sine[SOUND_TABLE_SIZE];
static int g_sound_noise[SOUND_TABLE_SIZE];
static bool g_sound_tables_ready = false;

/* java.util.Random, seeded with 0 — the sequence rt4's noise table is filled
 * from. Reproduced rather than approximated so a render is byte-stable across
 * platforms and runs (see the header's note on this deviation). */
struct java_random
{
    uint64_t seed;
};

static void
java_random_init(struct java_random* rng, uint64_t seed)
{
    rng->seed = (seed ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1);
}

static int32_t
java_random_next_int(struct java_random* rng)
{
    rng->seed = (rng->seed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
    return (int32_t)(uint32_t)(rng->seed >> 16);
}

static void
sound_tables_init(void)
{
    struct java_random rng;
    if( g_sound_tables_ready )
        return;
    java_random_init(&rng, 0);
    for( int i = 0; i < SOUND_TABLE_SIZE; i++ )
        g_sound_noise[i] = (java_random_next_int(&rng) & 0x2) - 1;
    for( int i = 0; i < SOUND_TABLE_SIZE; i++ )
        g_sound_sine[i] = (int)(sin((double)i / 5215.1903) * 16384.0);
    g_sound_tables_ready = true;
}

/* --- envelope playback --------------------------------------------------- */

/**
 * The mutable half of an envelope during a render. The reference keeps this on
 * the envelope object itself; separating it keeps decoded records immutable and
 * lets one record be rendered more than once.
 */
struct env_state
{
    int threshold;
    int position;
    int delta;
    int amplitude;
    int ticks;
};

static void
env_reset(struct env_state* state)
{
    memset(state, 0, sizeof(*state));
}

/* Reference Envelope.evaluate / SynthEnvelope.nextLevel. */
static int
env_eval(
    const struct RSCache_SoundEnvelope* envelope,
    struct env_state* state,
    int total)
{
    if( envelope->stage_count <= 0 )
        return 0;
    if( state->ticks >= state->threshold )
    {
        state->amplitude = envelope->levels[state->position++] << 15;
        if( state->position >= envelope->stage_count )
            state->position = envelope->stage_count - 1;
        state->threshold =
            (int)((double)envelope->times[state->position] / 65536.0 * (double)total);
        if( state->threshold > state->ticks )
            state->delta = ((envelope->levels[state->position] << 15) - state->amplitude) /
                           (state->threshold - state->ticks);
    }
    state->amplitude += state->delta;
    state->ticks++;
    return (state->amplitude - state->delta) >> 15;
}

/* Reference Tone.generate's inner `generate` / SynthInstrument.method3504. */
static int
waveform(
    int amplitude,
    int phase,
    int form)
{
    switch( form )
    {
    case 1:
        return (phase & 0x7FFF) < 16384 ? amplitude : -amplitude;
    case 2:
        return (g_sound_sine[phase & 0x7FFF] * amplitude) >> 14;
    case 3:
        return (((phase & 0x7FFF) * amplitude) >> 14) - amplitude;
    case 4:
        return g_sound_noise[(phase / 2607) & 0x7FFF] * amplitude;
    default:
        return 0;
    }
}

/* --- filter -------------------------------------------------------------- */

/* Reference SynthFilter's static coefficient scratch. Held per render rather
 * than globally; the two compute() calls share it and the sample loop reads
 * what they left, so it must persist across both. */
struct filter_ctx
{
    float float_coefficients[2][8];
    int32_t coefficients[2][8];
    float float_inverse_a0;
    int32_t inverse_a0;
};

static float
filter_octave_phase(float octaves)
{
    float frequency = (float)pow(2.0, (double)octaves) * 32.703197f;
    return frequency * 3.1415927f / 11025.0f;
}

static float
filter_amplitude(
    const struct RSCache_SoundFilter* filter,
    int direction,
    int pair,
    float alpha)
{
    float gain = (float)filter->gain[direction][0][pair] +
                 alpha * (float)(filter->gain[direction][1][pair] -
                                 filter->gain[direction][0][pair]);
    float decibels = gain * 0.0015258789f;
    return 1.0f - (float)pow(10.0, (double)(-decibels / 20.0f));
}

static float
filter_phase(
    const struct RSCache_SoundFilter* filter,
    int direction,
    int pair,
    float alpha)
{
    float octave = (float)filter->octaves[direction][0][pair] +
                   alpha * (float)(filter->octaves[direction][1][pair] -
                                   filter->octaves[direction][0][pair]);
    return filter_octave_phase(octave * 1.2207031e-4f);
}

/* Reference SynthFilter.compute: builds the polynomial coefficients for one
 * direction at sweep position `alpha`, returning how many there are. */
static int
filter_compute(
    const struct RSCache_SoundFilter* filter,
    struct filter_ctx* ctx,
    int direction,
    float alpha)
{
    float amplitude;

    if( direction == 0 )
    {
        float gain = (float)filter->inverse_gain[0] +
                     (float)(filter->inverse_gain[1] - filter->inverse_gain[0]) * alpha;
        float decibels = gain * 0.0030517578f;
        ctx->float_inverse_a0 = (float)pow(0.1, (double)(decibels / 20.0f));
        ctx->inverse_a0 = (int32_t)(ctx->float_inverse_a0 * 65536.0f);
    }
    if( filter->pairs[direction] == 0 )
        return 0;

    amplitude = filter_amplitude(filter, direction, 0, alpha);
    ctx->float_coefficients[direction][0] =
        -2.0f * amplitude * (float)cos((double)filter_phase(filter, direction, 0, alpha));
    ctx->float_coefficients[direction][1] = amplitude * amplitude;

    for( int pair = 1; pair < filter->pairs[direction]; pair++ )
    {
        float linear;
        float quadratic;
        float* coefficients = ctx->float_coefficients[direction];
        amplitude = filter_amplitude(filter, direction, pair, alpha);
        linear =
            -2.0f * amplitude * (float)cos((double)filter_phase(filter, direction, pair, alpha));
        quadratic = amplitude * amplitude;
        coefficients[(pair * 2) + 1] = coefficients[(pair * 2) - 1] * quadratic;
        coefficients[pair * 2] = (coefficients[(pair * 2) - 1] * linear) +
                                 (coefficients[(pair * 2) - 2] * quadratic);
        for( int i = (pair * 2) - 1; i >= 2; i-- )
            coefficients[i] +=
                (coefficients[i - 1] * linear) + (coefficients[i - 2] * quadratic);
        coefficients[1] += (coefficients[0] * linear) + quadratic;
        coefficients[0] += linear;
    }

    if( direction == 0 )
        for( int i = 0; i < filter->pairs[0] * 2; i++ )
            ctx->float_coefficients[0][i] *= ctx->float_inverse_a0;
    for( int i = 0; i < filter->pairs[direction] * 2; i++ )
        ctx->coefficients[direction][i] =
            (int32_t)(ctx->float_coefficients[direction][i] * 65536.0f);
    return filter->pairs[direction] * 2;
}

static int32_t
mul16(
    int32_t sample,
    int32_t coefficient)
{
    return (int32_t)(((int64_t)sample * (int64_t)coefficient) >> 16);
}

/* --- tone synthesis ------------------------------------------------------ */

/*
 * One tone's render, kept between steps (tone_run_begin, tone_run_step).
 *
 * The reference renders a tone in one call (rt4 SynthInstrument.getSamples /
 * Client-TS Tone.generate): the oscillators sample by sample, then the gate,
 * the reverb, the filter and the clamp, each a pass over the tone's samples.
 * A long tone with a filter is tens of milliseconds of processor time, so a
 * caller that must stop between bounded pieces of work (a background job that
 * checks its clock) needs to stop INSIDE a tone. Every pass here is that
 * loop with its variables kept in the run instead of in locals, and a step
 * does at most `*budget` samples of work, across passes, before it returns.
 * The arithmetic is the one-call loops' own, in their order, so the samples
 * are the same however the work is cut (RSCache_SoundEffectRender is these
 * steps with an unbounded budget).
 */
enum tone_pass
{
    TONE_SYNTH,
    TONE_GATE,
    TONE_REVERB,
    TONE_FILTER,
    TONE_CLAMP,
    TONE_DONE,
};

/* The filter's own phases (filter_apply in the reference's three loops). */
enum filter_pass
{
    FILTER_BEGIN,
    FILTER_PRIME,
    FILTER_BLOCK,
    FILTER_STEADY,
    FILTER_TAIL,
};

struct tone_run
{
    const struct RSCache_SoundTone* tone;
    int32_t* samples;
    struct filter_ctx* filter;
    int sample_count;
    int pass;
    int cursor;

    /* The oscillators. */
    double samples_per_step;
    struct env_state frequency_state;
    struct env_state amplitude_state;
    struct env_state frequency_mod_rate_state;
    struct env_state frequency_mod_range_state;
    struct env_state amplitude_mod_rate_state;
    struct env_state amplitude_mod_range_state;
    int frequency_start;
    int frequency_duration;
    int frequency_phase;
    int amplitude_start;
    int amplitude_duration;
    int amplitude_phase;
    int harmonic_phase[RSCACHE_SOUND_HARMONICS];
    int harmonic_delay[RSCACHE_SOUND_HARMONICS];
    int harmonic_scaled_volume[RSCACHE_SOUND_HARMONICS];
    int harmonic_step[RSCACHE_SOUND_HARMONICS];
    int harmonic_base_step[RSCACHE_SOUND_HARMONICS];

    /* The gate. */
    struct env_state release_state;
    struct env_state attack_state;
    int gate_counter;
    bool muted;

    /* The reverb's delay, in samples. */
    int reverb_delay;

    /* The filter. */
    int filter_pass;
    struct env_state filter_state;
    int level;
    int forward;
    int feedback;
    int limit;
};

/* The reference's oscillator arrays are five long and its render loop only
 * ever sums five, even though the wire can carry more. */
#define TONE_HARMONICS 5

/* The pass after `pass` that this tone has (a gate, a reverb and a filter
 * only when it carries one), with that pass's state set up. */
static void
tone_run_next(struct tone_run* r)
{
    const struct RSCache_SoundTone* tone = r->tone;

    r->cursor = 0;
    if( r->pass < TONE_GATE && tone->has_gate )
    {
        r->pass = TONE_GATE;
        env_reset(&r->release_state);
        env_reset(&r->attack_state);
        r->gate_counter = 0;
        r->muted = true;
        return;
    }
    if( r->pass < TONE_REVERB && tone->reverb_delay > 0 && tone->reverb_volume > 0 )
    {
        int const delay = (int)((double)tone->reverb_delay * r->samples_per_step);

        if( delay > 0 )
        {
            r->pass = TONE_REVERB;
            r->reverb_delay = delay;
            r->cursor = delay;
            return;
        }
    }
    if( r->pass < TONE_FILTER && tone->has_filter )
    {
        r->pass = TONE_FILTER;
        r->filter_pass = FILTER_BEGIN;
        return;
    }
    r->pass = r->pass < TONE_CLAMP ? TONE_CLAMP : TONE_DONE;
}

/* Reference Tone.generate / SynthInstrument.getSamples' set-up. */
static void
tone_run_begin(
    struct tone_run* r,
    const struct RSCache_SoundTone* tone,
    int sample_count,
    int length_ms,
    int32_t* samples,
    struct filter_ctx* filter_scratch)
{
    memset(r, 0, sizeof(*r));
    r->tone = tone;
    r->samples = samples;
    r->filter = filter_scratch;
    r->sample_count = sample_count;
    memset(samples, 0, (size_t)sample_count * sizeof(int32_t));
    if( length_ms < 10 )
    {
        r->pass = TONE_DONE;
        return;
    }

    r->samples_per_step = (double)sample_count / (double)length_ms;
    if( tone->has_frequency_mod )
    {
        r->frequency_start =
            (int)((double)(tone->frequency_mod_rate.end - tone->frequency_mod_rate.start) *
                  32.768 / r->samples_per_step);
        r->frequency_duration =
            (int)((double)tone->frequency_mod_rate.start * 32.768 / r->samples_per_step);
    }
    if( tone->has_amplitude_mod )
    {
        r->amplitude_start =
            (int)((double)(tone->amplitude_mod_rate.end - tone->amplitude_mod_rate.start) *
                  32.768 / r->samples_per_step);
        r->amplitude_duration =
            (int)((double)tone->amplitude_mod_rate.start * 32.768 / r->samples_per_step);
    }
    for( int harmonic = 0; harmonic < TONE_HARMONICS; harmonic++ )
    {
        if( tone->harmonic_volume[harmonic] == 0 )
            continue;
        r->harmonic_delay[harmonic] =
            (int)((double)tone->harmonic_delay[harmonic] * r->samples_per_step);
        r->harmonic_scaled_volume[harmonic] = (tone->harmonic_volume[harmonic] << 14) / 100;
        r->harmonic_step[harmonic] =
            (int)((double)(tone->frequency_base.end - tone->frequency_base.start) * 32.768 *
                  pow(1.0057929410678534, (double)tone->harmonic_semitone[harmonic]) /
                  r->samples_per_step);
        r->harmonic_base_step[harmonic] =
            (int)((double)tone->frequency_base.start * 32.768 / r->samples_per_step);
    }
    r->pass = TONE_SYNTH;
}

/* The oscillators over samples [cursor, end). */
static void
tone_synth(
    struct tone_run* r,
    int end)
{
    const struct RSCache_SoundTone* tone = r->tone;
    int32_t* samples = r->samples;
    int const sample_count = r->sample_count;

    for( int sample = r->cursor; sample < end; sample++ )
    {
        int frequency = env_eval(&tone->frequency_base, &r->frequency_state, sample_count);
        int amplitude = env_eval(&tone->amplitude_base, &r->amplitude_state, sample_count);

        if( tone->has_frequency_mod )
        {
            int rate =
                env_eval(&tone->frequency_mod_rate, &r->frequency_mod_rate_state, sample_count);
            int range =
                env_eval(&tone->frequency_mod_range, &r->frequency_mod_range_state, sample_count);
            frequency += waveform(range, r->frequency_phase, tone->frequency_mod_rate.form) >> 1;
            r->frequency_phase += (rate * r->frequency_start >> 16) + r->frequency_duration;
        }
        if( tone->has_amplitude_mod )
        {
            int rate =
                env_eval(&tone->amplitude_mod_rate, &r->amplitude_mod_rate_state, sample_count);
            int range =
                env_eval(&tone->amplitude_mod_range, &r->amplitude_mod_range_state, sample_count);
            amplitude = amplitude *
                        ((waveform(range, r->amplitude_phase, tone->amplitude_mod_rate.form) >> 1) +
                         32768) >>
                        15;
            r->amplitude_phase += (rate * r->amplitude_start >> 16) + r->amplitude_duration;
        }

        for( int harmonic = 0; harmonic < TONE_HARMONICS; harmonic++ )
        {
            int position;
            if( tone->harmonic_volume[harmonic] == 0 )
                continue;
            position = sample + r->harmonic_delay[harmonic];
            if( position >= sample_count )
                continue;
            samples[position] += waveform(
                amplitude * r->harmonic_scaled_volume[harmonic] >> 15,
                r->harmonic_phase[harmonic],
                tone->frequency_base.form);
            r->harmonic_phase[harmonic] +=
                (frequency * r->harmonic_step[harmonic] >> 16) + r->harmonic_base_step[harmonic];
        }
    }
    r->cursor = end;
}

/* Gate: chop the tone into pulses whose on/off lengths sweep with the
 * release (closed) and attack (open) envelopes. Samples [cursor, end). */
static void
tone_gate(
    struct tone_run* r,
    int end)
{
    const struct RSCache_SoundTone* tone = r->tone;

    for( int sample = r->cursor; sample < end; sample++ )
    {
        int release_value = env_eval(&tone->release, &r->release_state, r->sample_count);
        int attack_value = env_eval(&tone->attack, &r->attack_state, r->sample_count);
        int threshold = tone->release.start + ((tone->release.end - tone->release.start) *
                                                   (r->muted ? release_value : attack_value) >>
                                               8);
        r->gate_counter += 256;
        if( r->gate_counter >= threshold )
        {
            r->gate_counter = 0;
            r->muted = !r->muted;
        }
        if( r->muted )
            r->samples[sample] = 0;
    }
    r->cursor = end;
}

/*
 * The tone's filter, at most `*budget` output samples of it.
 *
 * Ported from rt4 SynthInstrument.getSamples' filter block: an IIR sweep whose
 * coefficients are recomputed every 128 samples from the filter envelope, with
 * three phases (priming while there is no history, steady state, then a tail
 * where the look-ahead runs past the end of the buffer). `cursor` is the
 * reference's `index`. Returns true once the filter is done.
 */
static bool
tone_filter(
    struct tone_run* r,
    int* budget)
{
    const struct RSCache_SoundFilter* filter = &r->tone->filter;
    struct filter_ctx* ctx = r->filter;
    int32_t* samples = r->samples;
    int const count = r->sample_count;

    while( *budget > 0 )
    {
        if( r->filter_pass == FILTER_BEGIN )
        {
            if( filter->pairs[0] <= 0 && filter->pairs[1] <= 0 )
                return true;
            env_reset(&r->filter_state);
            r->level = env_eval(&filter->envelope, &r->filter_state, count + 1);
            r->forward = filter_compute(filter, ctx, 0, (float)r->level / 65536.0f);
            r->feedback = filter_compute(filter, ctx, 1, (float)r->level / 65536.0f);
            if( count < r->forward + r->feedback )
                return true;
            r->cursor = 0;
            r->limit = r->feedback > count - r->forward ? count - r->forward : r->feedback;
            r->filter_pass = FILTER_PRIME;
            continue;
        }
        if( r->filter_pass == FILTER_PRIME )
        {
            /* Fewer output samples exist than the feedback taps want, so the
             * inner feedback sum runs only over what has been written. */
            while( r->cursor < r->limit && *budget > 0 )
            {
                int const index = r->cursor;
                int32_t accumulator = mul16(samples[index + r->forward], ctx->inverse_a0);
                for( int tap = 0; tap < r->forward; tap++ )
                    accumulator +=
                        mul16(samples[index + r->forward - tap - 1], ctx->coefficients[0][tap]);
                for( int tap = 0; tap < index; tap++ )
                    accumulator -= mul16(samples[index - tap - 1], ctx->coefficients[1][tap]);
                samples[index] = accumulator;
                r->level = env_eval(&filter->envelope, &r->filter_state, count + 1);
                r->cursor++;
                (*budget)--;
            }
            if( r->cursor < r->limit )
                return false;
            r->limit = 128;
            r->filter_pass = FILTER_BLOCK;
            continue;
        }
        if( r->filter_pass == FILTER_BLOCK )
        {
            /* A 128-sample block of the steady state, cut short where the
             * look-ahead tap would run past the end. */
            if( r->limit > count - r->forward )
                r->limit = count - r->forward;
            r->filter_pass = FILTER_STEADY;
            continue;
        }
        if( r->filter_pass == FILTER_STEADY )
        {
            while( r->cursor < r->limit && *budget > 0 )
            {
                int const index = r->cursor;
                int32_t accumulator = mul16(samples[index + r->forward], ctx->inverse_a0);
                for( int tap = 0; tap < r->forward; tap++ )
                    accumulator +=
                        mul16(samples[index + r->forward - tap - 1], ctx->coefficients[0][tap]);
                for( int tap = 0; tap < r->feedback; tap++ )
                    accumulator -= mul16(samples[index - tap - 1], ctx->coefficients[1][tap]);
                samples[index] = accumulator;
                r->level = env_eval(&filter->envelope, &r->filter_state, count + 1);
                r->cursor++;
                (*budget)--;
            }
            if( r->cursor < r->limit )
                return false;
            if( r->cursor >= count - r->forward )
            {
                r->filter_pass = FILTER_TAIL;
                continue;
            }
            /* The next block's coefficients, from the envelope. */
            r->forward = filter_compute(filter, ctx, 0, (float)r->level / 65536.0f);
            r->feedback = filter_compute(filter, ctx, 1, (float)r->level / 65536.0f);
            r->limit += 128;
            r->filter_pass = FILTER_BLOCK;
            continue;
        }
        /* FILTER_TAIL: only the in-range look-ahead taps contribute. */
        while( r->cursor < count && *budget > 0 )
        {
            int const index = r->cursor;
            int32_t accumulator = 0;
            for( int tap = index + r->forward - count; tap < r->forward; tap++ )
                accumulator +=
                    mul16(samples[index + r->forward - tap - 1], ctx->coefficients[0][tap]);
            for( int tap = 0; tap < r->feedback; tap++ )
                accumulator -= mul16(samples[index - tap - 1], ctx->coefficients[1][tap]);
            samples[index] = accumulator;
            env_eval(&filter->envelope, &r->filter_state, count + 1);
            r->cursor++;
            (*budget)--;
        }
        return r->cursor >= count;
    }
    return false;
}

/* At most `*budget` samples of the tone's passes. True once it is rendered. */
static bool
tone_run_step(
    struct tone_run* r,
    int* budget)
{
    while( r->pass != TONE_DONE && *budget > 0 )
    {
        int const count = r->sample_count;
        int n;
        int end;

        if( r->pass == TONE_FILTER )
        {
            if( !tone_filter(r, budget) )
                return false;
            tone_run_next(r);
            continue;
        }
        n = count - r->cursor;
        if( n <= 0 )
        {
            tone_run_next(r);
            continue;
        }
        if( n > *budget )
            n = *budget;
        end = r->cursor + n;
        *budget -= n;
        if( r->pass == TONE_SYNTH )
            tone_synth(r, end);
        else if( r->pass == TONE_GATE )
            tone_gate(r, end);
        else if( r->pass == TONE_REVERB )
        {
            for( int sample = r->cursor; sample < end; sample++ )
                r->samples[sample] +=
                    r->samples[sample - r->reverb_delay] * r->tone->reverb_volume / 100;
            r->cursor = end;
        }
        else
        {
            assert(r->pass == TONE_CLAMP);
            for( int sample = r->cursor; sample < end; sample++ )
            {
                if( r->samples[sample] < -32768 )
                    r->samples[sample] = -32768;
                else if( r->samples[sample] > 32767 )
                    r->samples[sample] = 32767;
            }
            r->cursor = end;
        }
        if( r->cursor >= count )
            tone_run_next(r);
    }
    return r->pass == TONE_DONE;
}

/* --- effect mixing ------------------------------------------------------- */

static int
ms_to_samples(int milliseconds)
{
    return (int)(((int64_t)milliseconds * RSCACHE_SOUND_SAMPLE_RATE) / 1000);
}

/*
 * An effect's render in steps (RSCache_SoundRenderNew): what the one-call
 * render keeps in locals across its tone loop, kept here between steps.
 */
struct RSCache_SoundRender
{
    const struct RSCache_SoundEffect* effect; /* borrowed */
    int codec;
    int sample_count;
    int reported_count;
    int allocated_count;
    int loop_start;
    int loop_end;
    bool loop_valid;
    int32_t* tone_samples;
    uint8_t* pcm;
    struct filter_ctx filter_scratch;
    /* The next tone slot to render. */
    int slot;
    /* The tone being rendered (`tone_active`), where it mixes in, and how far
     * its mix has got. */
    struct tone_run tone;
    bool tone_active;
    int tone_start;
    int tone_sample_count;
    int mix_cursor;
    /* Every tone is in: the bias pass (SYNTH only) up to here. */
    bool tones_done;
    int bias_cursor;
    bool finished;
};

void
RSCache_SoundRenderFree(struct RSCache_SoundRender* render)
{
    if( !render )
        return;
    RSCACHE_FREE(render->pcm);
    RSCACHE_FREE(render->tone_samples);
    RSCACHE_FREE(render);
}

struct RSCache_SoundRender*
RSCache_SoundRenderNew(
    const struct RSCache* cache,
    const struct RSCache_SoundEffect* effect)
{
    struct RSCache_SoundRender* render;
    int duration;
    int max_tone_samples = 0;

    assert(effect);
    render = RSCACHE_CALLOC(1, sizeof(*render));
    if( !render )
        return NULL;
    render->effect = effect;
    render->codec = RSCache_SoundCodecVersion(cache);

    duration = RSCache_SoundEffectDurationMs(effect);
    render->sample_count = ms_to_samples(duration);
    if( duration <= 0 || render->sample_count <= 0 )
    {
        RSCache_SoundRenderFree(render);
        return NULL;
    }

    render->loop_start = ms_to_samples(effect->loop_begin);
    render->loop_end = ms_to_samples(effect->loop_end);
    render->loop_valid = render->loop_start >= 0 && render->loop_end >= 0 &&
                         render->loop_end <= render->sample_count &&
                         render->loop_start < render->loop_end;

    /*
     * The WAVE generation's output is not always the natural length.
     *
     * Its length is `sampleCount + span * (loopCount - 1)`, and an unusable loop
     * range forces loopCount to 0 — which leaves `sampleCount - span`. So an
     * effect whose loop end runs past its own duration comes out *shorter* than
     * its tones (id 221 in cache254.lostcity loses 200ms of tail), and one whose
     * loop bounds are reversed comes out *longer*, padded with silence (id 383
     * gains 1.2s). Both are what the client plays, so both are reproduced: nine
     * of the 696 effects in that cache depend on it, and skipping it means every
     * one of the nine is a different length from the reference.
     *
     * The modern generation has no equivalent — it hands its loop points to the
     * mixer and always returns the natural length.
     */
    render->reported_count = render->sample_count;
    if( render->codec == RSCACHE_CODEC_SOUND_WAVE && !render->loop_valid )
        render->reported_count = render->sample_count - (render->loop_end - render->loop_start);
    if( render->reported_count <= 0 )
    {
        RSCache_SoundRenderFree(render);
        return NULL;
    }
    render->allocated_count = render->reported_count > render->sample_count
                                  ? render->reported_count
                                  : render->sample_count;

    sound_tables_init();

    for( int slot = 0; slot < RSCACHE_SOUND_TONES; slot++ )
    {
        int need;
        if( !effect->tones[slot] )
            continue;
        need = ms_to_samples(effect->tones[slot]->length);
        if( need > max_tone_samples )
            max_tone_samples = need;
    }

    render->pcm = RSCACHE_MALLOC((size_t)render->allocated_count);
    render->tone_samples =
        max_tone_samples > 0 ? RSCACHE_MALLOC((size_t)max_tone_samples * sizeof(int32_t)) : NULL;
    if( !render->pcm || (max_tone_samples > 0 && !render->tone_samples) )
    {
        RSCache_SoundRenderFree(render);
        return NULL;
    }

    /* Silence differs between the generations: WAVE mixes in a u8 buffer biased
     * by 128, SYNTH in a signed one. Both are normalised to u8 on the way out. */
    memset(
        render->pcm,
        render->codec == RSCACHE_CODEC_SOUND_WAVE ? 0x80 : 0x00,
        (size_t)render->allocated_count);
    memset(&render->filter_scratch, 0, sizeof(render->filter_scratch));
    return render;
}

/* Mix samples [mix_cursor, end) of the current tone into the PCM. */
static void
render_mix(
    struct RSCache_SoundRender* render,
    int end)
{
    uint8_t* pcm = render->pcm;
    int32_t const* tone_samples = render->tone_samples;
    int const start = render->tone_start;

    if( render->codec == RSCACHE_CODEC_SOUND_WAVE )
    {
        /* Wrapping 8-bit add, as Client-TS's Uint8Array += does. */
        for( int sample = render->mix_cursor; sample < end; sample++ )
            pcm[sample + start] = (uint8_t)(pcm[sample + start] + (uint8_t)(tone_samples[sample] >> 8));
    }
    else
    {
        /* Clamping signed add (rt4). The buffer holds signed values until
         * the bias is applied at the end. */
        for( int sample = render->mix_cursor; sample < end; sample++ )
        {
            int mixed = (int8_t)pcm[sample + start] + (tone_samples[sample] >> 8);
            if( mixed < -128 )
                mixed = -128;
            else if( mixed > 127 )
                mixed = 127;
            pcm[sample + start] = (uint8_t)(int8_t)mixed;
        }
    }
    render->mix_cursor = end;
}

/* Start the next tone slot that renders anything; false when none is left. */
static bool
render_next_tone(struct RSCache_SoundRender* render)
{
    const struct RSCache_SoundEffect* effect = render->effect;

    while( render->slot < RSCACHE_SOUND_TONES )
    {
        const struct RSCache_SoundTone* tone = effect->tones[render->slot++];
        int tone_sample_count;
        int start;
        if( !tone )
            continue;
        tone_sample_count = ms_to_samples(tone->length);
        start = ms_to_samples(tone->start);
        if( tone_sample_count <= 0 )
            continue;
        if( start + tone_sample_count > render->sample_count )
            tone_sample_count = render->sample_count - start;
        if( tone_sample_count <= 0 )
            continue;
        tone_run_begin(
            &render->tone, tone, tone_sample_count, tone->length, render->tone_samples,
            &render->filter_scratch);
        render->tone_active = true;
        render->tone_start = start;
        render->tone_sample_count = tone_sample_count;
        render->mix_cursor = 0;
        return true;
    }
    return false;
}

bool
RSCache_SoundRenderStep(
    struct RSCache_SoundRender* render,
    int budget)
{
    assert(render);
    assert(budget > 0);
    while( !render->finished && budget > 0 )
    {
        if( !render->tones_done )
        {
            int n;

            if( !render->tone_active && !render_next_tone(render) )
            {
                render->tones_done = true;
                continue;
            }
            if( !tone_run_step(&render->tone, &budget) )
                continue;
            n = render->tone_sample_count - render->mix_cursor;
            if( n > budget )
                n = budget;
            budget -= n;
            render_mix(render, render->mix_cursor + n);
            if( render->mix_cursor >= render->tone_sample_count )
                render->tone_active = false;
            continue;
        }
        if( render->codec == RSCACHE_CODEC_SOUND_WAVE )
        {
            render->finished = true;
            break;
        }
        {
            /* signed -> unsigned bias */
            int n = render->allocated_count - render->bias_cursor;

            if( n > budget )
                n = budget;
            budget -= n;
            for( int sample = render->bias_cursor; sample < render->bias_cursor + n; sample++ )
                render->pcm[sample] = (uint8_t)(render->pcm[sample] ^ 0x80);
            render->bias_cursor += n;
            if( render->bias_cursor >= render->allocated_count )
                render->finished = true;
        }
    }
    return render->finished;
}

void
RSCache_SoundRenderTake(
    struct RSCache_SoundRender* render,
    struct RSCache_SoundPcm* out)
{
    assert(render);
    assert(render->finished);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->loop_start = -1;
    out->loop_end = -1;
    out->samples = render->pcm;
    render->pcm = NULL;
    out->sample_count = render->reported_count;
    out->sample_rate = RSCACHE_SOUND_SAMPLE_RATE;
    if( render->loop_valid )
    {
        out->loop_start = render->loop_start;
        out->loop_end = render->loop_end;
    }
}

bool
RSCache_SoundEffectRender(
    const struct RSCache* cache,
    const struct RSCache_SoundEffect* effect,
    struct RSCache_SoundPcm* out)
{
    struct RSCache_SoundRender* render;

    if( !out )
        return false;
    memset(out, 0, sizeof(*out));
    out->loop_start = -1;
    out->loop_end = -1;
    if( !effect )
        return false;
    render = RSCache_SoundRenderNew(cache, effect);
    if( !render )
        return false;
    while( !RSCache_SoundRenderStep(render, INT_MAX) )
        ;
    RSCache_SoundRenderTake(render, out);
    RSCache_SoundRenderFree(render);
    return true;
}

bool
RSCache_SoundPcmExpandLoops(
    struct RSCache_SoundPcm* pcm,
    int loop_count)
{
    int span;
    int total;
    int end_offset;
    uint8_t* grown;

    if( !pcm || !pcm->samples )
        return false;
    if( pcm->loop_start < 0 || pcm->loop_end <= pcm->loop_start )
        return true; /* no usable span; the render already accounted for it */

    span = pcm->loop_end - pcm->loop_start;

    /* The reference's length is `count + span * (loopCount - 1)`, so a repeat
     * count of zero *shortens* the clip by one span rather than playing it once.
     * Servers send at least 1, but reproducing it costs nothing and removes the
     * last place this differs from the reference by choice. */
    if( loop_count <= 0 )
    {
        pcm->sample_count = pcm->sample_count - span;
        if( pcm->sample_count < 0 )
            pcm->sample_count = 0;
        return true;
    }
    if( loop_count == 1 )
        return true;
    total = pcm->sample_count + span * (loop_count - 1);
    grown = RSCACHE_REALLOC(pcm->samples, (size_t)total);
    if( !grown )
        return false;
    pcm->samples = grown;

    /* Move the post-loop tail to the end, then stamp the loop body in behind
     * it (reference Wave.generate's expansion, descending so the growing copy
     * never overwrites a source byte it still needs). */
    end_offset = total - pcm->sample_count;
    for( int sample = pcm->sample_count - 1; sample >= pcm->loop_end; sample-- )
        grown[sample + end_offset] = grown[sample];
    for( int loop = 1; loop < loop_count; loop++ )
    {
        int offset = span * loop;
        for( int sample = pcm->loop_start; sample < pcm->loop_end; sample++ )
            grown[sample + offset] = grown[sample];
    }

    pcm->sample_count = total;
    return true;
}

uint32_t
RSCache_SoundPcmWriteWav(
    const struct RSCache_SoundPcm* pcm,
    uint8_t* out,
    uint32_t out_capacity)
{
    uint32_t needed;
    uint32_t offset = 0;

    if( !pcm || !pcm->samples || !out )
        return 0;
    needed = 44u + (uint32_t)pcm->sample_count;
    if( out_capacity < needed )
        return 0;

#define WAV_P4BE(v)                                                                                \
    do                                                                                             \
    {                                                                                              \
        out[offset++] = (uint8_t)((v) >> 24);                                                       \
        out[offset++] = (uint8_t)((v) >> 16);                                                       \
        out[offset++] = (uint8_t)((v) >> 8);                                                        \
        out[offset++] = (uint8_t)(v);                                                               \
    } while( 0 )
#define WAV_P4LE(v)                                                                                \
    do                                                                                             \
    {                                                                                              \
        out[offset++] = (uint8_t)(v);                                                               \
        out[offset++] = (uint8_t)((v) >> 8);                                                        \
        out[offset++] = (uint8_t)((v) >> 16);                                                       \
        out[offset++] = (uint8_t)((v) >> 24);                                                       \
    } while( 0 )
#define WAV_P2LE(v)                                                                                \
    do                                                                                             \
    {                                                                                              \
        out[offset++] = (uint8_t)(v);                                                               \
        out[offset++] = (uint8_t)((v) >> 8);                                                        \
    } while( 0 )

    WAV_P4BE(0x52494646u);                    /* "RIFF" */
    WAV_P4LE((uint32_t)pcm->sample_count + 36u);
    WAV_P4BE(0x57415645u);                    /* "WAVE" */
    WAV_P4BE(0x666D7420u);                    /* "fmt " */
    WAV_P4LE(16u);
    WAV_P2LE(1u);                             /* PCM */
    WAV_P2LE(1u);                             /* mono */
    WAV_P4LE((uint32_t)pcm->sample_rate);
    WAV_P4LE((uint32_t)pcm->sample_rate);     /* byte rate = rate * 1 * 1 */
    WAV_P2LE(1u);                             /* block align */
    WAV_P2LE(8u);                             /* bits per sample */
    WAV_P4BE(0x64617461u);                    /* "data" */
    WAV_P4LE((uint32_t)pcm->sample_count);
    memcpy(out + offset, pcm->samples, (size_t)pcm->sample_count);
    offset += (uint32_t)pcm->sample_count;

#undef WAV_P4BE
#undef WAV_P4LE
#undef WAV_P2LE
    return offset;
}

void
RSCache_SoundPcmRelease(struct RSCache_SoundPcm* pcm)
{
    if( !pcm )
        return;
    RSCACHE_FREE(pcm->samples);
    pcm->samples = NULL;
    pcm->sample_count = 0;
}
