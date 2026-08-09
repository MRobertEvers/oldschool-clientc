#include "audio/torirs_pcm.h"

#include <math.h>
#include <string.h>

void
ToriRS_PcmGains(
    int volume,
    int pan,
    int* out_left,
    int* out_right)
{
    double normalised;

    if( volume < 0 )
        volume = 0;
    if( volume > TORIRS_PCM_VOLUME_UNITY )
        volume = TORIRS_PCM_VOLUME_UNITY;
    if( pan < 0 )
        pan = 0;
    if( pan > TORIRS_PCM_PAN_MAX )
        pan = TORIRS_PCM_PAN_MAX;

    /* Reference: left = vol * sqrt((PAN_MAX - pan) / (PAN_MAX/2)),
     *            right = vol * sqrt(pan / (PAN_MAX/2)).
     * Both are 1 at centre, so unity volume is unity gain on both channels. */
    normalised = (double)volume / (double)TORIRS_PCM_VOLUME_UNITY * (double)TORIRS_PCM_GAIN_ONE;
    *out_left =
        (int)(normalised * sqrt((double)(TORIRS_PCM_PAN_MAX - pan) / (TORIRS_PCM_PAN_MAX / 2)) +
              0.5);
    *out_right = (int)(normalised * sqrt((double)pan / (TORIRS_PCM_PAN_MAX / 2)) + 0.5);
}

/** Playback step in 8.8, clamped so a wild rate cannot make the loop spin. */
static int
step_for_rate(
    int rate_numerator,
    int rate_denominator)
{
    int64_t step;

    if( rate_denominator <= 0 )
        return TORIRS_PCM_POSITION_ONE;
    step = ((int64_t)rate_numerator * TORIRS_PCM_POSITION_ONE) / rate_denominator;
    if( step < 1 )
        step = 1;
    if( step > 64 * TORIRS_PCM_POSITION_ONE )
        step = 64 * TORIRS_PCM_POSITION_ONE;
    return (int)step;
}

void
ToriRS_PcmVoice_Start(
    struct ToriRS_PcmVoice* voice,
    const struct ToriRS_PcmSound* sound,
    int rate_numerator,
    int rate_denominator,
    int volume,
    int pan,
    int loop_count)
{
    if( !voice )
        return;
    memset(voice, 0, sizeof(*voice));
    if( !sound || !sound->samples || sound->sample_count <= 0 )
        return;

    voice->sound = sound;
    voice->position = 0;
    voice->step = step_for_rate(rate_numerator, rate_denominator);
    voice->loops_left = loop_count;
    voice->volume = volume;
    voice->pan = pan;
    ToriRS_PcmGains(volume, pan, &voice->left_gain, &voice->right_gain);
    voice->target_left_gain = voice->left_gain;
    voice->target_right_gain = voice->right_gain;
    voice->bus_gain = TORIRS_PCM_BUS_ONE;
    voice->active = true;
}

/** Largest of the absolute differences, which is what bounds a ramp. */
static int
ramp_span(
    int a,
    int b,
    int c)
{
    int span = a < 0 ? -a : a;
    int magnitude_b = b < 0 ? -b : b;
    int magnitude_c = c < 0 ? -c : c;

    if( magnitude_b > span )
        span = magnitude_b;
    if( magnitude_c > span )
        span = magnitude_c;
    return span;
}

static void
apply_gain_now(
    struct ToriRS_PcmVoice* voice,
    int left,
    int right)
{
    voice->left_gain = left;
    voice->right_gain = right;
    voice->target_left_gain = left;
    voice->target_right_gain = right;
    voice->ramp_frames = 0;
    voice->ramp_to_stop = false;
}

void
ToriRS_PcmVoice_SetGain(
    struct ToriRS_PcmVoice* voice,
    int volume,
    int pan,
    int ramp_frames)
{
    int left;
    int right;
    int span;

    if( !voice || !voice->active )
        return;
    voice->volume = volume;
    voice->pan = pan;
    ToriRS_PcmGains(volume, pan, &left, &right);
    if( ramp_frames <= 0 || (left == voice->left_gain && right == voice->right_gain) )
    {
        apply_gain_now(voice, left, right);
        return;
    }

    /*
     * Cap the ramp at the largest gain step it has to make (reference
     * method946's `if (arg0 > var6) arg0 = var6`).
     *
     * Without it, `(target - current) / ramp_frames` truncates to **zero** for
     * any change smaller than the ramp length -- and the synth re-ramps every
     * envelope step, which is 220 frames at 22050Hz. The gain then sits still
     * for 10ms and jumps at the end of each one: a 100Hz staircase riding on
     * every voice, which is heard as a buzz over the music rather than as a
     * fault in any single note. Clamping makes the ramp finish sooner instead
     * of moving in steps of zero.
     */
    span = ramp_span(
        volume - voice->volume, left - voice->left_gain, right - voice->right_gain);
    if( ramp_frames > span )
        ramp_frames = span;
    if( ramp_frames <= 0 )
    {
        apply_gain_now(voice, left, right);
        return;
    }

    voice->target_left_gain = left;
    voice->target_right_gain = right;
    voice->ramp_frames = ramp_frames;
    voice->ramp_to_stop = false;
    voice->left_step = (left - voice->left_gain) / ramp_frames;
    voice->right_step = (right - voice->right_gain) / ramp_frames;
}

void
ToriRS_PcmVoice_SetRate(
    struct ToriRS_PcmVoice* voice,
    int rate_numerator,
    int rate_denominator)
{
    int magnitude;

    if( !voice || !voice->active )
        return;
    magnitude = step_for_rate(rate_numerator, rate_denominator);
    /* Direction is loop state, not a rate property: a ping-pong voice halfway
     * through its backward pass must stay backward across a pitch bend. */
    voice->step = voice->step < 0 ? -magnitude : magnitude;
}

void
ToriRS_PcmVoice_Stop(
    struct ToriRS_PcmVoice* voice,
    int ramp_frames)
{
    if( !voice || !voice->active )
        return;
    if( ramp_frames <= 0 || (voice->left_gain == 0 && voice->right_gain == 0) )
    {
        voice->active = false;
        return;
    }
    /* Same clamp as SetGain, for the same reason (reference method998). */
    {
        int span = ramp_span(voice->volume, voice->left_gain, voice->right_gain);
        if( ramp_frames > span )
            ramp_frames = span;
        if( ramp_frames <= 0 )
        {
            voice->active = false;
            return;
        }
    }
    voice->target_left_gain = 0;
    voice->target_right_gain = 0;
    voice->ramp_frames = ramp_frames;
    voice->ramp_to_stop = true;
    voice->left_step = -voice->left_gain / ramp_frames;
    voice->right_step = -voice->right_gain / ramp_frames;
}

void
ToriRS_PcmVoice_Seek(
    struct ToriRS_PcmVoice* voice,
    int sample_position)
{
    if( !voice || !voice->sound )
        return;
    if( sample_position < 0 )
        sample_position = 0;
    if( sample_position > voice->sound->sample_count )
        sample_position = voice->sound->sample_count;
    voice->position = sample_position << TORIRS_PCM_POSITION_SHIFT;
}

void
ToriRS_PcmVoice_SeekFixed(
    struct ToriRS_PcmVoice* voice,
    int fixed_position)
{
    int limit;

    if( !voice || !voice->sound )
        return;
    limit = voice->sound->sample_count << TORIRS_PCM_POSITION_SHIFT;
    if( fixed_position < 0 )
        fixed_position = 0;
    if( fixed_position > limit )
        fixed_position = limit;
    voice->position = fixed_position;
}

void
ToriRS_PcmVoice_SetStep(
    struct ToriRS_PcmVoice* voice,
    int step)
{
    if( !voice )
        return;
    if( step < 1 )
        step = 1;
    if( step > 64 * TORIRS_PCM_POSITION_ONE )
        step = 64 * TORIRS_PCM_POSITION_ONE;
    voice->step = voice->step < 0 ? -step : step;
}

void
ToriRS_PcmVoice_SetBackwards(
    struct ToriRS_PcmVoice* voice,
    bool backwards)
{
    int magnitude;

    if( !voice )
        return;
    magnitude = voice->step < 0 ? -voice->step : voice->step;
    voice->step = backwards ? -magnitude : magnitude;
}

bool
ToriRS_PcmVoice_Exhausted(const struct ToriRS_PcmVoice* voice)
{
    if( !voice || !voice->sound )
        return true;
    return voice->position < 0 ||
           voice->position >= (voice->sound->sample_count << TORIRS_PCM_POSITION_SHIFT);
}

/**
 * The bounds playback is confined to *right now*.
 *
 * This is the reference's rule and it is not "the loop span": a voice is bounded
 * by the loop span only while it still has passes left. Once the count is
 * exhausted it plays out to the end of the whole clip -- the reference falls out
 * of its loop branch into a final pass bounded by `samples.length`.
 *
 * Getting this wrong is not subtle in the way it sounds. A sound effect whose
 * loop span ends before its samples do gets cut off mid-waveform, which is a
 * step discontinuity: a click on every play, on every effect that has a loop
 * span. It is also invisible to a test whose fixture loops over the whole clip.
 */
static void
loop_bounds(
    const struct ToriRS_PcmVoice* voice,
    int* out_start,
    int* out_end)
{
    const struct ToriRS_PcmSound* sound = voice->sound;
    int start = sound->loop_start;
    int end = sound->loop_end;

    if( voice->loops_left == 0 || end <= start || end > sound->sample_count )
    {
        start = 0;
        end = sound->sample_count;
    }
    if( start < 0 )
        start = 0;
    *out_start = start;
    *out_end = end;
}

/**
 * Advance one frame, applying the loop rule.
 *
 * Returns false when the voice has run off the end and has no loop left, which
 * is what ends it. Ping-pong reflects the position about the bound and flips
 * the step, which is the reference's `position = bound + bound - 1 - position`.
 */
static bool
advance(struct ToriRS_PcmVoice* voice)
{
    const struct ToriRS_PcmSound* sound = voice->sound;
    int loop_start;
    int loop_end;
    int limit_low;
    int limit_high;

    voice->position += voice->step;
    loop_bounds(voice, &loop_start, &loop_end);
    limit_low = loop_start << TORIRS_PCM_POSITION_SHIFT;
    limit_high = loop_end << TORIRS_PCM_POSITION_SHIFT;

    if( voice->step >= 0 )
    {
        if( voice->position < limit_high )
            return true;
        if( voice->loops_left == 0 )
            return false;
        if( voice->loops_left > 0 )
            voice->loops_left--;
        if( sound->ping_pong )
        {
            voice->position = limit_high + limit_high - 1 - voice->position;
            voice->step = -voice->step;
            if( voice->position < limit_low )
                voice->position = limit_low;
        }
        else
        {
            int span = limit_high - limit_low;
            if( span <= 0 )
                return false;
            voice->position = limit_low + (voice->position - limit_low) % span;
        }
        return true;
    }

    if( voice->position >= limit_low )
        return true;
    if( voice->loops_left == 0 )
        return false;
    if( voice->loops_left > 0 )
        voice->loops_left--;
    if( sound->ping_pong )
    {
        voice->position = limit_low + limit_low - 1 - voice->position;
        voice->step = -voice->step;
        if( voice->position >= limit_high )
            voice->position = limit_high - 1;
    }
    else
    {
        int span = limit_high - limit_low;
        if( span <= 0 )
            return false;
        voice->position = limit_high - 1 - ((limit_high - 1 - voice->position) % span);
    }
    return true;
}

/** Linearly interpolated sample at the voice's 8.8 position. */
static int
sample_at(const struct ToriRS_PcmVoice* voice)
{
    const struct ToriRS_PcmSound* sound = voice->sound;
    int index = voice->position >> TORIRS_PCM_POSITION_SHIFT;
    int fraction = voice->position & (TORIRS_PCM_POSITION_ONE - 1);
    int current;
    int next;

    if( index < 0 )
        index = 0;
    if( index >= sound->sample_count )
        index = sound->sample_count - 1;
    current = sound->samples[index];
    next = index + 1 < sound->sample_count ? sound->samples[index + 1] : current;
    return current + (((next - current) * fraction) >> TORIRS_PCM_POSITION_SHIFT);
}

/** One ramp step; ends the voice when a fade-to-stop lands. */
static void
step_ramp(struct ToriRS_PcmVoice* voice)
{
    if( voice->ramp_frames <= 0 )
        return;
    voice->left_gain += voice->left_step;
    voice->right_gain += voice->right_step;
    if( --voice->ramp_frames == 0 )
    {
        voice->left_gain = voice->target_left_gain;
        voice->right_gain = voice->target_right_gain;
        if( voice->ramp_to_stop )
            voice->active = false;
    }
}

void
ToriRS_PcmVoice_StartExternal(
    struct ToriRS_PcmVoice* voice,
    int volume,
    int pan)
{
    if( !voice )
        return;
    memset(voice, 0, sizeof(*voice));
    voice->volume = volume;
    voice->pan = pan;
    ToriRS_PcmGains(volume, pan, &voice->left_gain, &voice->right_gain);
    voice->target_left_gain = voice->left_gain;
    voice->target_right_gain = voice->right_gain;
    voice->bus_gain = TORIRS_PCM_BUS_ONE;
    voice->active = true;
}

bool
ToriRS_PcmVoice_MixExternal(
    struct ToriRS_PcmVoice* voice,
    const int16_t* stereo,
    int available,
    int32_t* out,
    int frames)
{
    if( !voice || !voice->active || !out )
        return voice && voice->active;

    for( int i = 0; i < frames; i++ )
    {
        int left = (voice->left_gain * voice->bus_gain) >> TORIRS_PCM_BUS_SHIFT;
        int right = (voice->right_gain * voice->bus_gain) >> TORIRS_PCM_BUS_SHIFT;

        /*
         * The ramp advances across the whole block, including any tail the
         * generator did not fill. A fade that stalled where the audio ran out
         * would leave `ramp_to_stop` unresolved and the voice pinned live.
         */
        if( stereo && i < available )
        {
            out[i * 2] += (stereo[i * 2] * left) >> TORIRS_PCM_GAIN_SHIFT;
            out[i * 2 + 1] += (stereo[i * 2 + 1] * right) >> TORIRS_PCM_GAIN_SHIFT;
        }
        step_ramp(voice);
        if( !voice->active )
            return false;
    }
    return true;
}

bool
ToriRS_PcmVoice_Mix(
    struct ToriRS_PcmVoice* voice,
    int32_t* out,
    int frames)
{
    if( !voice || !voice->active || !voice->sound || !out )
        return voice && voice->active;

    for( int i = 0; i < frames; i++ )
    {
        int value = sample_at(voice);
        /*
         * The bus gain is applied here rather than folded into left/right_gain,
         * because those are ramp state: scaling them before a block and
         * restoring them after would throw away the ramp's progress every
         * frame, and a fade under a non-unity bus would never actually fade.
         */
        int left = (voice->left_gain * voice->bus_gain) >> TORIRS_PCM_BUS_SHIFT;
        int right = (voice->right_gain * voice->bus_gain) >> TORIRS_PCM_BUS_SHIFT;

        out[i * 2] += (value * left) >> TORIRS_PCM_GAIN_SHIFT;
        out[i * 2 + 1] += (value * right) >> TORIRS_PCM_GAIN_SHIFT;
        step_ramp(voice);
        if( !voice->active )
            return false;
        if( !advance(voice) )
        {
            voice->active = false;
            return false;
        }
    }
    return true;
}

void
ToriRS_PcmVoice_Skip(
    struct ToriRS_PcmVoice* voice,
    int frames)
{
    if( !voice || !voice->active || !voice->sound )
        return;
    for( int i = 0; i < frames; i++ )
    {
        step_ramp(voice);
        if( !voice->active )
            return;
        if( !advance(voice) )
        {
            voice->active = false;
            return;
        }
    }
}
