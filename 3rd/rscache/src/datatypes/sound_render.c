#include "sound_render.h"

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

/**
 * Run the tone's filter over the rendered samples in place.
 *
 * Ported from rt4 SynthInstrument.getSamples' filter block: an IIR sweep whose
 * coefficients are recomputed every 128 samples from the filter envelope, with
 * three phases (priming while there is no history, steady state, then a tail
 * where the look-ahead runs past the end of the buffer).
 */
static void
filter_apply(
    const struct RSCache_SoundTone* tone,
    struct filter_ctx* ctx,
    int32_t* samples,
    int count)
{
    const struct RSCache_SoundFilter* filter = &tone->filter;
    struct env_state envelope_state;
    int level;
    int forward;
    int feedback;
    int index;
    int limit;

    if( filter->pairs[0] <= 0 && filter->pairs[1] <= 0 )
        return;

    env_reset(&envelope_state);
    level = env_eval(&filter->envelope, &envelope_state, count + 1);
    forward = filter_compute(filter, ctx, 0, (float)level / 65536.0f);
    feedback = filter_compute(filter, ctx, 1, (float)level / 65536.0f);
    if( count < forward + feedback )
        return;

    /* Priming pass: fewer output samples exist than the feedback taps want, so
     * the inner feedback sum runs only over what has been written. */
    index = 0;
    limit = feedback > count - forward ? count - forward : feedback;
    while( index < limit )
    {
        int32_t accumulator = mul16(samples[index + forward], ctx->inverse_a0);
        for( int tap = 0; tap < forward; tap++ )
            accumulator += mul16(samples[index + forward - tap - 1], ctx->coefficients[0][tap]);
        for( int tap = 0; tap < index; tap++ )
            accumulator -= mul16(samples[index - tap - 1], ctx->coefficients[1][tap]);
        samples[index] = accumulator;
        level = env_eval(&filter->envelope, &envelope_state, count + 1);
        index++;
    }

    /* Steady state in 128-sample blocks, recomputing the coefficients from the
     * envelope between blocks, then a tail where the look-ahead tap runs past
     * the end of the buffer and only the in-range taps contribute. */
    limit = 128;
    while( true )
    {
        if( limit > count - forward )
            limit = count - forward;
        while( index < limit )
        {
            int32_t accumulator = mul16(samples[index + forward], ctx->inverse_a0);
            for( int tap = 0; tap < forward; tap++ )
                accumulator +=
                    mul16(samples[index + forward - tap - 1], ctx->coefficients[0][tap]);
            for( int tap = 0; tap < feedback; tap++ )
                accumulator -= mul16(samples[index - tap - 1], ctx->coefficients[1][tap]);
            samples[index] = accumulator;
            level = env_eval(&filter->envelope, &envelope_state, count + 1);
            index++;
        }
        if( index >= count - forward )
        {
            while( index < count )
            {
                int32_t accumulator = 0;
                for( int tap = index + forward - count; tap < forward; tap++ )
                    accumulator +=
                        mul16(samples[index + forward - tap - 1], ctx->coefficients[0][tap]);
                for( int tap = 0; tap < feedback; tap++ )
                    accumulator -= mul16(samples[index - tap - 1], ctx->coefficients[1][tap]);
                samples[index] = accumulator;
                env_eval(&filter->envelope, &envelope_state, count + 1);
                index++;
            }
            break;
        }
        forward = filter_compute(filter, ctx, 0, (float)level / 65536.0f);
        feedback = filter_compute(filter, ctx, 1, (float)level / 65536.0f);
        limit += 128;
    }
}

/* --- tone synthesis ------------------------------------------------------ */

/* Reference Tone.generate / SynthInstrument.getSamples. */
static void
tone_render(
    const struct RSCache_SoundTone* tone,
    int sample_count,
    int length_ms,
    int32_t* samples,
    struct filter_ctx* filter_scratch)
{
    struct env_state frequency_state;
    struct env_state amplitude_state;
    struct env_state frequency_mod_rate_state;
    struct env_state frequency_mod_range_state;
    struct env_state amplitude_mod_rate_state;
    struct env_state amplitude_mod_range_state;
    double samples_per_step;
    int frequency_start = 0;
    int frequency_duration = 0;
    int frequency_phase = 0;
    int amplitude_start = 0;
    int amplitude_duration = 0;
    int amplitude_phase = 0;
    int harmonic_phase[RSCACHE_SOUND_HARMONICS];
    int harmonic_delay[RSCACHE_SOUND_HARMONICS];
    int harmonic_scaled_volume[RSCACHE_SOUND_HARMONICS];
    int harmonic_step[RSCACHE_SOUND_HARMONICS];
    int harmonic_base_step[RSCACHE_SOUND_HARMONICS];
    /* The reference's oscillator arrays are five long and its render loop only
     * ever sums five, even though the wire can carry more. */
    const int harmonics = 5;

    memset(samples, 0, (size_t)sample_count * sizeof(int32_t));
    if( length_ms < 10 )
        return;

    samples_per_step = (double)sample_count / (double)length_ms;

    env_reset(&frequency_state);
    env_reset(&amplitude_state);

    if( tone->has_frequency_mod )
    {
        env_reset(&frequency_mod_rate_state);
        env_reset(&frequency_mod_range_state);
        frequency_start =
            (int)((double)(tone->frequency_mod_rate.end - tone->frequency_mod_rate.start) *
                  32.768 / samples_per_step);
        frequency_duration =
            (int)((double)tone->frequency_mod_rate.start * 32.768 / samples_per_step);
    }
    if( tone->has_amplitude_mod )
    {
        env_reset(&amplitude_mod_rate_state);
        env_reset(&amplitude_mod_range_state);
        amplitude_start =
            (int)((double)(tone->amplitude_mod_rate.end - tone->amplitude_mod_rate.start) *
                  32.768 / samples_per_step);
        amplitude_duration =
            (int)((double)tone->amplitude_mod_rate.start * 32.768 / samples_per_step);
    }

    memset(harmonic_phase, 0, sizeof(harmonic_phase));
    memset(harmonic_delay, 0, sizeof(harmonic_delay));
    memset(harmonic_scaled_volume, 0, sizeof(harmonic_scaled_volume));
    memset(harmonic_step, 0, sizeof(harmonic_step));
    memset(harmonic_base_step, 0, sizeof(harmonic_base_step));
    for( int harmonic = 0; harmonic < harmonics; harmonic++ )
    {
        if( tone->harmonic_volume[harmonic] == 0 )
            continue;
        harmonic_phase[harmonic] = 0;
        harmonic_delay[harmonic] =
            (int)((double)tone->harmonic_delay[harmonic] * samples_per_step);
        harmonic_scaled_volume[harmonic] = (tone->harmonic_volume[harmonic] << 14) / 100;
        harmonic_step[harmonic] =
            (int)((double)(tone->frequency_base.end - tone->frequency_base.start) * 32.768 *
                  pow(1.0057929410678534, (double)tone->harmonic_semitone[harmonic]) /
                  samples_per_step);
        harmonic_base_step[harmonic] =
            (int)((double)tone->frequency_base.start * 32.768 / samples_per_step);
    }

    for( int sample = 0; sample < sample_count; sample++ )
    {
        int frequency = env_eval(&tone->frequency_base, &frequency_state, sample_count);
        int amplitude = env_eval(&tone->amplitude_base, &amplitude_state, sample_count);

        if( tone->has_frequency_mod )
        {
            int rate =
                env_eval(&tone->frequency_mod_rate, &frequency_mod_rate_state, sample_count);
            int range =
                env_eval(&tone->frequency_mod_range, &frequency_mod_range_state, sample_count);
            frequency += waveform(range, frequency_phase, tone->frequency_mod_rate.form) >> 1;
            frequency_phase += (rate * frequency_start >> 16) + frequency_duration;
        }
        if( tone->has_amplitude_mod )
        {
            int rate =
                env_eval(&tone->amplitude_mod_rate, &amplitude_mod_rate_state, sample_count);
            int range =
                env_eval(&tone->amplitude_mod_range, &amplitude_mod_range_state, sample_count);
            amplitude = amplitude *
                        ((waveform(range, amplitude_phase, tone->amplitude_mod_rate.form) >> 1) +
                         32768) >>
                        15;
            amplitude_phase += (rate * amplitude_start >> 16) + amplitude_duration;
        }

        for( int harmonic = 0; harmonic < harmonics; harmonic++ )
        {
            int position;
            if( tone->harmonic_volume[harmonic] == 0 )
                continue;
            position = sample + harmonic_delay[harmonic];
            if( position >= sample_count )
                continue;
            samples[position] += waveform(
                amplitude * harmonic_scaled_volume[harmonic] >> 15,
                harmonic_phase[harmonic],
                tone->frequency_base.form);
            harmonic_phase[harmonic] +=
                (frequency * harmonic_step[harmonic] >> 16) + harmonic_base_step[harmonic];
        }
    }

    /* Gate: chop the tone into pulses whose on/off lengths sweep with the
     * release (closed) and attack (open) envelopes. */
    if( tone->has_gate )
    {
        struct env_state release_state;
        struct env_state attack_state;
        int counter = 0;
        bool muted = true;

        env_reset(&release_state);
        env_reset(&attack_state);
        for( int sample = 0; sample < sample_count; sample++ )
        {
            int release_value = env_eval(&tone->release, &release_state, sample_count);
            int attack_value = env_eval(&tone->attack, &attack_state, sample_count);
            int threshold =
                tone->release.start + ((tone->release.end - tone->release.start) *
                                       (muted ? release_value : attack_value) >>
                                       8);
            counter += 256;
            if( counter >= threshold )
            {
                counter = 0;
                muted = !muted;
            }
            if( muted )
                samples[sample] = 0;
        }
    }

    if( tone->reverb_delay > 0 && tone->reverb_volume > 0 )
    {
        int delay = (int)((double)tone->reverb_delay * samples_per_step);
        if( delay > 0 )
            for( int sample = delay; sample < sample_count; sample++ )
                samples[sample] += samples[sample - delay] * tone->reverb_volume / 100;
    }

    if( tone->has_filter )
        filter_apply(tone, filter_scratch, samples, sample_count);

    for( int sample = 0; sample < sample_count; sample++ )
    {
        if( samples[sample] < -32768 )
            samples[sample] = -32768;
        else if( samples[sample] > 32767 )
            samples[sample] = 32767;
    }
}

/* --- effect mixing ------------------------------------------------------- */

static int
ms_to_samples(int milliseconds)
{
    return (int)(((int64_t)milliseconds * RSCACHE_SOUND_SAMPLE_RATE) / 1000);
}

bool
RSCache_SoundEffectRender(
    const struct RSCache* cache,
    const struct RSCache_SoundEffect* effect,
    struct RSCache_SoundPcm* out)
{
    int codec = RSCache_SoundCodecVersion(cache);
    int duration;
    int sample_count;
    int reported_count;
    int allocated_count;
    int loop_start;
    int loop_end;
    bool loop_valid;
    int max_tone_samples = 0;
    int32_t* tone_samples;
    uint8_t* pcm;
    struct filter_ctx filter_scratch;

    if( !out )
        return false;
    memset(out, 0, sizeof(*out));
    out->loop_start = -1;
    out->loop_end = -1;
    if( !effect )
        return false;

    duration = RSCache_SoundEffectDurationMs(effect);
    sample_count = ms_to_samples(duration);
    if( duration <= 0 || sample_count <= 0 )
        return false;

    loop_start = ms_to_samples(effect->loop_begin);
    loop_end = ms_to_samples(effect->loop_end);
    loop_valid =
        loop_start >= 0 && loop_end >= 0 && loop_end <= sample_count && loop_start < loop_end;

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
    reported_count = sample_count;
    if( codec == RSCACHE_CODEC_SOUND_WAVE && !loop_valid )
        reported_count = sample_count - (loop_end - loop_start);
    if( reported_count <= 0 )
        return false;
    allocated_count = reported_count > sample_count ? reported_count : sample_count;

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

    pcm = malloc((size_t)allocated_count);
    tone_samples =
        max_tone_samples > 0 ? malloc((size_t)max_tone_samples * sizeof(int32_t)) : NULL;
    if( !pcm || (max_tone_samples > 0 && !tone_samples) )
    {
        free(pcm);
        free(tone_samples);
        return false;
    }

    /* Silence differs between the generations: WAVE mixes in a u8 buffer biased
     * by 128, SYNTH in a signed one. Both are normalised to u8 on the way out. */
    memset(pcm, codec == RSCACHE_CODEC_SOUND_WAVE ? 0x80 : 0x00, (size_t)allocated_count);
    memset(&filter_scratch, 0, sizeof(filter_scratch));

    for( int slot = 0; slot < RSCACHE_SOUND_TONES; slot++ )
    {
        const struct RSCache_SoundTone* tone = effect->tones[slot];
        int tone_sample_count;
        int start;
        if( !tone )
            continue;
        tone_sample_count = ms_to_samples(tone->length);
        start = ms_to_samples(tone->start);
        if( tone_sample_count <= 0 )
            continue;
        if( start + tone_sample_count > sample_count )
            tone_sample_count = sample_count - start;
        if( tone_sample_count <= 0 )
            continue;

        tone_render(tone, tone_sample_count, tone->length, tone_samples, &filter_scratch);

        if( codec == RSCACHE_CODEC_SOUND_WAVE )
        {
            /* Wrapping 8-bit add, as Client-TS's Uint8Array += does. */
            for( int sample = 0; sample < tone_sample_count; sample++ )
                pcm[sample + start] =
                    (uint8_t)(pcm[sample + start] + (uint8_t)(tone_samples[sample] >> 8));
        }
        else
        {
            /* Clamping signed add (rt4). The buffer holds signed values until
             * the bias is applied below. */
            for( int sample = 0; sample < tone_sample_count; sample++ )
            {
                int mixed = (int8_t)pcm[sample + start] + (tone_samples[sample] >> 8);
                if( mixed < -128 )
                    mixed = -128;
                else if( mixed > 127 )
                    mixed = 127;
                pcm[sample + start] = (uint8_t)(int8_t)mixed;
            }
        }
    }

    if( codec != RSCACHE_CODEC_SOUND_WAVE )
        for( int sample = 0; sample < allocated_count; sample++ )
            pcm[sample] = (uint8_t)(pcm[sample] ^ 0x80); /* signed -> unsigned bias */

    free(tone_samples);

    out->samples = pcm;
    out->sample_count = reported_count;
    out->sample_rate = RSCACHE_SOUND_SAMPLE_RATE;
    if( loop_valid )
    {
        out->loop_start = loop_start;
        out->loop_end = loop_end;
    }
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
    grown = realloc(pcm->samples, (size_t)total);
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
    free(pcm->samples);
    pcm->samples = NULL;
    pcm->sample_count = 0;
}
