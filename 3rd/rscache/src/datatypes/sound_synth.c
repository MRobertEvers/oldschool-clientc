#include "sound_synth.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
RSCache_SoundCodecVersion(const struct RSCache* cache)
{
    /* dat1 254 and earlier have no filter; dat1 377 and every dat2 cache do.
     * See the header for the corpus measurements behind the boundary. There is
     * no archive-revision fallback: the sound table carries no per-group
     * revision this library records, so an unidentified profile takes the
     * modern layout (every dat2 cache in the corpus, and the only container
     * this can be asked about without a stated epoch). */
    if( RSCache_IsDat1(cache) )
        return RSCache_RevisionAtLeastRs2(
                   cache, RSCACHE_TYPE_SOUND, 377, RSCACHE_GROUP_REVISION_UNKNOWN, false)
                   ? RSCACHE_CODEC_SOUND_SYNTH
                   : RSCACHE_CODEC_SOUND_WAVE;
    return RSCACHE_CODEC_SOUND_SYNTH;
}

int
RSCache_SoundFlags(const struct RSCache* cache)
{
    return RSCache_SoundCodecVersion(cache) == RSCACHE_CODEC_SOUND_SYNTH
               ? RSCACHE_SOUND_HAS_FILTER
               : 0;
}

enum RSCache_SoundSampleKind
RSCache_SoundSampleKindOf(
    const char* data,
    int data_size)
{
    /* Jagex's compressed-audio container. The magic is "BCV" + a version byte;
     * its offset differs by table (6 in the OldSchool sound-effects table, 2 in
     * the music-samples table), so the first 16 bytes are scanned rather than a
     * fixed offset assumed. A synth record cannot collide: byte 0 of a synth
     * record is a tone's envelope form (0..4) or a 0 skip byte, and no synth
     * record in the corpus contains this sequence in its first 16 bytes. */
    if( !data || data_size < 12 )
        return RSCACHE_SOUND_SAMPLE_NONE;
    for( int i = 0; i + 4 <= 16 && i + 4 <= data_size; i++ )
    {
        if( data[i] == 'B' && data[i + 1] == 'C' && data[i + 2] == 'V' )
            return RSCACHE_SOUND_SAMPLE_BCV;
    }
    return RSCACHE_SOUND_SAMPLE_NONE;
}

bool
RSCache_SoundIsSampleBlob(
    const char* data,
    int data_size)
{
    return RSCache_SoundSampleKindOf(data, data_size) != RSCACHE_SOUND_SAMPLE_NONE;
}

/* Every read below is guarded: a truncated or misframed record must fail the
 * decode, not walk off the archive. */
#define SOUND_NEED(buffer, n)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( RSCache_BufferRemaining(buffer) < (uint32_t)(n) )                                       \
            return false;                                                                          \
    } while( 0 )

/** Bytes an unsigned short-smart at the cursor will consume, or 0 if truncated. */
static int
usmart_width(struct RSCache_Buffer* buffer)
{
    if( RSCache_BufferRemaining(buffer) < 1 )
        return 0;
    int peek = buffer->data[buffer->position] & 0xFF;
    if( peek < 128 )
        return 1;
    return RSCache_BufferRemaining(buffer) >= 2 ? 2 : 0;
}

/* Only the stage list, which is all the filter's sweep envelope carries
 * (reference SynthEnvelope.decodeStages). */
static bool
decode_envelope_stages(
    struct RSCache_SoundEnvelope* envelope,
    struct RSCache_Buffer* buffer)
{
    SOUND_NEED(buffer, 1);
    envelope->stage_count = g1(buffer);
    SOUND_NEED(buffer, envelope->stage_count * 4);
    if( envelope->stage_count > 0 )
    {
        envelope->times = malloc((size_t)envelope->stage_count * sizeof(int));
        envelope->levels = malloc((size_t)envelope->stage_count * sizeof(int));
        if( !envelope->times || !envelope->levels )
            return false;
        for( int i = 0; i < envelope->stage_count; i++ )
        {
            envelope->times[i] = g2(buffer);
            envelope->levels[i] = g2(buffer);
        }
    }
    return true;
}

static bool
decode_envelope(
    struct RSCache_SoundEnvelope* envelope,
    struct RSCache_Buffer* buffer)
{
    memset(envelope, 0, sizeof(*envelope));
    SOUND_NEED(buffer, 10);
    envelope->form = g1(buffer);
    envelope->start = g4(buffer);
    envelope->end = g4(buffer);
    return decode_envelope_stages(envelope, buffer);
}

static void
free_envelope(struct RSCache_SoundEnvelope* envelope)
{
    free(envelope->times);
    free(envelope->levels);
    envelope->times = NULL;
    envelope->levels = NULL;
    envelope->stage_count = 0;
}

/**
 * An optional envelope pair. The wire marks absence with a single zero byte
 * (a present envelope starts with a non-zero form), which the reference
 * consumes; a present one is rewound over and decoded.
 */
static bool
decode_optional_pair(
    struct RSCache_SoundEnvelope* first,
    struct RSCache_SoundEnvelope* second,
    bool* out_present,
    struct RSCache_Buffer* buffer)
{
    *out_present = false;
    SOUND_NEED(buffer, 1);
    if( g1(buffer) == 0 )
        return true;
    buffer->position--;
    if( !decode_envelope(first, buffer) )
        return false;
    if( !decode_envelope(second, buffer) )
        return false;
    *out_present = true;
    return true;
}

static bool
decode_filter(
    struct RSCache_SoundFilter* filter,
    struct RSCache_Buffer* buffer)
{
    memset(filter, 0, sizeof(*filter));
    SOUND_NEED(buffer, 1);
    int packed = g1(buffer);
    int declared[2] = { packed >> 4, packed & 0xF };
    filter->pairs[0] = declared[0] > RSCACHE_SOUND_FILTER_PAIRS ? RSCACHE_SOUND_FILTER_PAIRS
                                                               : declared[0];
    filter->pairs[1] = declared[1] > RSCACHE_SOUND_FILTER_PAIRS ? RSCACHE_SOUND_FILTER_PAIRS
                                                               : declared[1];
    filter->pairs_dropped = (declared[0] - filter->pairs[0]) + (declared[1] - filter->pairs[1]);
    /* No pairs at all: the reference returns before reading anything else. */
    if( packed == 0 )
        return true;

    SOUND_NEED(buffer, 5);
    filter->inverse_gain[0] = g2(buffer);
    filter->inverse_gain[1] = g2(buffer);
    filter->migrated = g1(buffer);

    for( int dir = 0; dir < 2; dir++ )
    {
        for( int pair = 0; pair < declared[dir]; pair++ )
        {
            SOUND_NEED(buffer, 4);
            int octave = g2(buffer);
            int gain = g2(buffer);
            if( pair < RSCACHE_SOUND_FILTER_PAIRS )
            {
                filter->octaves[dir][0][pair] = octave;
                filter->gain[dir][0][pair] = gain;
            }
        }
    }
    for( int dir = 0; dir < 2; dir++ )
    {
        for( int pair = 0; pair < declared[dir]; pair++ )
        {
            bool distinct = (filter->migrated & ((1 << (dir * 4)) << pair)) != 0;
            int octave;
            int gain;
            if( distinct )
            {
                SOUND_NEED(buffer, 4);
                octave = g2(buffer);
                gain = g2(buffer);
            }
            else if( pair < RSCACHE_SOUND_FILTER_PAIRS )
            {
                octave = filter->octaves[dir][0][pair];
                gain = filter->gain[dir][0][pair];
            }
            else
            {
                continue;
            }
            if( pair < RSCACHE_SOUND_FILTER_PAIRS )
            {
                filter->octaves[dir][1][pair] = octave;
                filter->gain[dir][1][pair] = gain;
            }
        }
    }

    if( filter->migrated != 0 || filter->inverse_gain[1] != filter->inverse_gain[0] )
    {
        if( !decode_envelope_stages(&filter->envelope, buffer) )
            return false;
        filter->has_envelope = true;
        return true;
    }

    /* Nothing on the wire: the reference's filter envelope keeps the values its
     * constructor set — a two-stage 0..65535 ramp — and the render sweeps the
     * filter across it anyway. Leaving it empty would freeze the sweep at zero.
     * Encode writes no stage list for this case, so the round trip is unaffected. */
    filter->envelope.stage_count = 2;
    filter->envelope.times = malloc(2 * sizeof(int));
    filter->envelope.levels = malloc(2 * sizeof(int));
    if( !filter->envelope.times || !filter->envelope.levels )
        return false;
    filter->envelope.times[0] = 0;
    filter->envelope.times[1] = 65535;
    filter->envelope.levels[0] = 0;
    filter->envelope.levels[1] = 65535;
    return true;
}

static bool
decode_tone(
    struct RSCache_SoundTone* tone,
    struct RSCache_Buffer* buffer,
    int flags)
{
    memset(tone, 0, sizeof(*tone));
    tone->reverb_volume = 100;
    tone->length = 500;

    if( !decode_envelope(&tone->frequency_base, buffer) )
        return false;
    if( !decode_envelope(&tone->amplitude_base, buffer) )
        return false;
    if( !decode_optional_pair(
            &tone->frequency_mod_rate,
            &tone->frequency_mod_range,
            &tone->has_frequency_mod,
            buffer) )
        return false;
    if( !decode_optional_pair(
            &tone->amplitude_mod_rate,
            &tone->amplitude_mod_range,
            &tone->has_amplitude_mod,
            buffer) )
        return false;
    if( !decode_optional_pair(&tone->release, &tone->attack, &tone->has_gate, buffer) )
        return false;

    for( int harmonic = 0; harmonic < RSCACHE_SOUND_HARMONICS; harmonic++ )
    {
        int volume;
        if( !usmart_width(buffer) )
            return false;
        volume = gushortsmart(buffer);
        if( volume == 0 )
            break;
        tone->harmonic_volume[harmonic] = volume;
        if( !usmart_width(buffer) )
            return false;
        tone->harmonic_semitone[harmonic] = gshortsmart(buffer);
        if( !usmart_width(buffer) )
            return false;
        tone->harmonic_delay[harmonic] = gushortsmart(buffer);
        tone->harmonic_count = harmonic + 1;
    }

    if( !usmart_width(buffer) )
        return false;
    tone->reverb_delay = gushortsmart(buffer);
    if( !usmart_width(buffer) )
        return false;
    tone->reverb_volume = gushortsmart(buffer);
    SOUND_NEED(buffer, 4);
    tone->length = g2(buffer);
    tone->start = g2(buffer);

    if( flags & RSCACHE_SOUND_HAS_FILTER )
    {
        if( !decode_filter(&tone->filter, buffer) )
            return false;
        tone->has_filter = true;
    }
    return true;
}

static void
free_tone(struct RSCache_SoundTone* tone)
{
    if( !tone )
        return;
    free_envelope(&tone->frequency_base);
    free_envelope(&tone->amplitude_base);
    free_envelope(&tone->frequency_mod_rate);
    free_envelope(&tone->frequency_mod_range);
    free_envelope(&tone->amplitude_mod_rate);
    free_envelope(&tone->amplitude_mod_range);
    free_envelope(&tone->release);
    free_envelope(&tone->attack);
    free_envelope(&tone->filter.envelope);
    free(tone);
}

static bool
decode_effect(
    struct RSCache_SoundEffect* effect,
    struct RSCache_Buffer* buffer,
    int flags)
{
    for( int slot = 0; slot < RSCACHE_SOUND_TONES; slot++ )
    {
        SOUND_NEED(buffer, 1);
        if( g1(buffer) == 0 )
            continue;
        buffer->position--;
        effect->tones[slot] = malloc(sizeof(struct RSCache_SoundTone));
        if( !effect->tones[slot] )
            return false;
        if( !decode_tone(effect->tones[slot], buffer, flags) )
            return false;
    }
    SOUND_NEED(buffer, 4);
    effect->loop_begin = g2(buffer);
    effect->loop_end = g2(buffer);
    return true;
}

struct RSCache_SoundEffect*
RSCache_SoundEffectNewDecode(
    const struct RSCache* cache,
    char* data,
    int data_size)
{
    struct RSCache_SoundEffect* effect;
    struct RSCache_Buffer buffer;
    int flags = RSCache_SoundFlags(cache);

    if( !data || data_size <= 0 )
        return NULL;

    effect = malloc(sizeof(*effect));
    if( !effect )
        return NULL;
    memset(effect, 0, sizeof(*effect));
    effect->id = -1;

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    if( !decode_effect(effect, &buffer, flags) )
    {
        RSCache_SoundEffectFree(effect);
        return NULL;
    }
    effect->_consumed = (int)buffer.position;
    return effect;
}

struct RSCache_SoundBank*
RSCache_SoundBankNewDecode(
    const struct RSCache* cache,
    char* data,
    int data_size)
{
    struct RSCache_SoundBank* bank;
    struct RSCache_Buffer buffer;
    int flags = RSCache_SoundFlags(cache);
    int capacity = 0;
    int order_capacity = 0;

    if( !data || data_size <= 0 )
        return NULL;

    bank = malloc(sizeof(*bank));
    if( !bank )
        return NULL;
    memset(bank, 0, sizeof(*bank));

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    while( true )
    {
        struct RSCache_SoundEffect* effect;
        int id;

        if( RSCache_BufferRemaining(&buffer) < 2 )
            goto error; /* ran out before the 65535 terminator */
        id = g2(&buffer);
        if( id == 65535 )
            break;

        if( id >= bank->count )
        {
            int want = id + 1;
            if( want > capacity )
            {
                int grow = capacity ? capacity * 2 : 64;
                struct RSCache_SoundEffect** grown;
                if( grow < want )
                    grow = want;
                grown = realloc(bank->effects, (size_t)grow * sizeof(*grown));
                if( !grown )
                    goto error;
                memset(grown + capacity, 0, (size_t)(grow - capacity) * sizeof(*grown));
                bank->effects = grown;
                capacity = grow;
            }
            bank->count = want;
        }
        /* A duplicate id would leak the earlier record; the reference simply
         * overwrites, so free first and keep the last one. */
        if( bank->effects[id] )
        {
            RSCache_SoundEffectFree(bank->effects[id]);
            bank->effects[id] = NULL;
        }

        effect = malloc(sizeof(*effect));
        if( !effect )
            goto error;
        memset(effect, 0, sizeof(*effect));
        effect->id = id;
        if( !decode_effect(effect, &buffer, flags) )
        {
            RSCache_SoundEffectFree(effect);
            goto error;
        }
        effect->_consumed = 0;
        bank->effects[id] = effect;

        if( bank->order_count == order_capacity )
        {
            int grow = order_capacity ? order_capacity * 2 : 64;
            int* grown = realloc(bank->order, (size_t)grow * sizeof(int));
            if( !grown )
                goto error;
            bank->order = grown;
            order_capacity = grow;
        }
        bank->order[bank->order_count++] = id;
    }

    bank->_consumed = (int)buffer.position;
    return bank;

error:
    RSCache_SoundBankFree(bank);
    return NULL;
}

/* --- encode ------------------------------------------------------------- */

static void
encode_envelope_stages(
    const struct RSCache_SoundEnvelope* envelope,
    struct RSCache_Buffer* buffer)
{
    p1(buffer, envelope->stage_count);
    for( int i = 0; i < envelope->stage_count; i++ )
    {
        p2(buffer, envelope->times[i]);
        p2(buffer, envelope->levels[i]);
    }
}

static void
encode_envelope(
    const struct RSCache_SoundEnvelope* envelope,
    struct RSCache_Buffer* buffer)
{
    p1(buffer, envelope->form);
    p4(buffer, envelope->start);
    p4(buffer, envelope->end);
    encode_envelope_stages(envelope, buffer);
}

static void
encode_filter(
    const struct RSCache_SoundFilter* filter,
    struct RSCache_Buffer* buffer)
{
    int packed = (filter->pairs[0] << 4) | (filter->pairs[1] & 0xF);
    p1(buffer, packed);
    if( packed == 0 )
        return;
    p2(buffer, filter->inverse_gain[0]);
    p2(buffer, filter->inverse_gain[1]);
    p1(buffer, filter->migrated);
    for( int dir = 0; dir < 2; dir++ )
        for( int pair = 0; pair < filter->pairs[dir]; pair++ )
        {
            p2(buffer, filter->octaves[dir][0][pair]);
            p2(buffer, filter->gain[dir][0][pair]);
        }
    for( int dir = 0; dir < 2; dir++ )
        for( int pair = 0; pair < filter->pairs[dir]; pair++ )
            if( (filter->migrated & ((1 << (dir * 4)) << pair)) != 0 )
            {
                p2(buffer, filter->octaves[dir][1][pair]);
                p2(buffer, filter->gain[dir][1][pair]);
            }
    if( filter->has_envelope )
        encode_envelope_stages(&filter->envelope, buffer);
}

static void
encode_tone(
    const struct RSCache_SoundTone* tone,
    struct RSCache_Buffer* buffer,
    int flags)
{
    encode_envelope(&tone->frequency_base, buffer);
    encode_envelope(&tone->amplitude_base, buffer);
    if( tone->has_frequency_mod )
    {
        encode_envelope(&tone->frequency_mod_rate, buffer);
        encode_envelope(&tone->frequency_mod_range, buffer);
    }
    else
    {
        p1(buffer, 0);
    }
    if( tone->has_amplitude_mod )
    {
        encode_envelope(&tone->amplitude_mod_rate, buffer);
        encode_envelope(&tone->amplitude_mod_range, buffer);
    }
    else
    {
        p1(buffer, 0);
    }
    if( tone->has_gate )
    {
        encode_envelope(&tone->release, buffer);
        encode_envelope(&tone->attack, buffer);
    }
    else
    {
        p1(buffer, 0);
    }

    for( int harmonic = 0; harmonic < tone->harmonic_count; harmonic++ )
    {
        pushortsmart(buffer, tone->harmonic_volume[harmonic]);
        pshortsmart(buffer, tone->harmonic_semitone[harmonic]);
        pushortsmart(buffer, tone->harmonic_delay[harmonic]);
    }
    /* The reader's loop runs at most RSCACHE_SOUND_HARMONICS times, so a full
     * list needs no terminator — it stops on the count alone. */
    if( tone->harmonic_count < RSCACHE_SOUND_HARMONICS )
        pushortsmart(buffer, 0);

    pushortsmart(buffer, tone->reverb_delay);
    pushortsmart(buffer, tone->reverb_volume);
    p2(buffer, tone->length);
    p2(buffer, tone->start);

    if( flags & RSCACHE_SOUND_HAS_FILTER )
        encode_filter(&tone->filter, buffer);
}

static void
encode_effect(
    const struct RSCache_SoundEffect* effect,
    struct RSCache_Buffer* buffer,
    int flags)
{
    for( int slot = 0; slot < RSCACHE_SOUND_TONES; slot++ )
    {
        if( !effect->tones[slot] )
        {
            p1(buffer, 0);
            continue;
        }
        encode_tone(effect->tones[slot], buffer, flags);
    }
    p2(buffer, effect->loop_begin);
    p2(buffer, effect->loop_end);
}

static uint32_t
envelope_bound(const struct RSCache_SoundEnvelope* envelope)
{
    return 10u + (uint32_t)envelope->stage_count * 4u;
}

static uint32_t
tone_bound(const struct RSCache_SoundTone* tone)
{
    uint32_t size = 1u;
    size += envelope_bound(&tone->frequency_base);
    size += envelope_bound(&tone->amplitude_base);
    size += 1u + envelope_bound(&tone->frequency_mod_rate) +
            envelope_bound(&tone->frequency_mod_range);
    size += 1u + envelope_bound(&tone->amplitude_mod_rate) +
            envelope_bound(&tone->amplitude_mod_range);
    size += 1u + envelope_bound(&tone->release) + envelope_bound(&tone->attack);
    size += (uint32_t)RSCACHE_SOUND_HARMONICS * 6u + 2u;
    size += 4u + 4u;
    /* filter: header + gains + two passes of pairs + a stage list */
    size += 6u + ((uint32_t)(2 * RSCACHE_SOUND_FILTER_PAIRS) * 8u) + 1u +
            ((uint32_t)tone->filter.envelope.stage_count * 4u);
    return size;
}

uint32_t
RSCache_SoundEffectEncodeBound(const struct RSCache_SoundEffect* effect)
{
    uint32_t size = 4u;
    if( !effect )
        return 0;
    for( int slot = 0; slot < RSCACHE_SOUND_TONES; slot++ )
        size += effect->tones[slot] ? tone_bound(effect->tones[slot]) : 1u;
    return size;
}

uint32_t
RSCache_SoundEffectEncode(
    const struct RSCache* cache,
    const struct RSCache_SoundEffect* effect,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buffer;
    if( !effect || !out )
        return 0;
    if( out_capacity < RSCache_SoundEffectEncodeBound(effect) )
        return 0;
    RSCache_BufferInit(&buffer, out, out_capacity);
    encode_effect(effect, &buffer, RSCache_SoundFlags(cache));
    return RSCache_BufferLength(&buffer);
}

uint32_t
RSCache_SoundBankEncodeBound(const struct RSCache_SoundBank* bank)
{
    uint32_t size = 2u;
    if( !bank )
        return 0;
    for( int id = 0; id < bank->count; id++ )
        if( bank->effects[id] )
            size += 2u + RSCache_SoundEffectEncodeBound(bank->effects[id]);
    return size;
}

uint32_t
RSCache_SoundBankEncode(
    const struct RSCache* cache,
    const struct RSCache_SoundBank* bank,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buffer;
    int flags;
    if( !bank || !out )
        return 0;
    if( out_capacity < RSCache_SoundBankEncodeBound(bank) )
        return 0;
    flags = RSCache_SoundFlags(cache);
    RSCache_BufferInit(&buffer, out, out_capacity);
    /* Wire order, not id order — see RSCache_SoundBank.order. */
    for( int index = 0; index < bank->order_count; index++ )
    {
        int sound_id = bank->order[index];
        if( sound_id < 0 || sound_id >= bank->count || !bank->effects[sound_id] )
            continue;
        p2(&buffer, sound_id);
        encode_effect(bank->effects[sound_id], &buffer, flags);
    }
    p2(&buffer, 65535);
    return RSCache_BufferLength(&buffer);
}

/* --- derived quantities -------------------------------------------------- */

int
RSCache_SoundEffectDurationMs(const struct RSCache_SoundEffect* effect)
{
    int duration = 0;
    if( !effect )
        return 0;
    for( int slot = 0; slot < RSCACHE_SOUND_TONES; slot++ )
    {
        const struct RSCache_SoundTone* tone = effect->tones[slot];
        if( tone && tone->length + tone->start > duration )
            duration = tone->length + tone->start;
    }
    return duration;
}

int
RSCache_SoundEffectTrim(struct RSCache_SoundEffect* effect)
{
    int start = 9999999;

    if( !effect )
        return 0;
    for( int slot = 0; slot < RSCACHE_SOUND_TONES; slot++ )
    {
        const struct RSCache_SoundTone* tone = effect->tones[slot];
        if( tone && tone->start / 20 < start )
            start = tone->start / 20;
    }
    if( effect->loop_begin < effect->loop_end && effect->loop_begin / 20 < start )
        start = effect->loop_begin / 20;

    if( start == 9999999 || start == 0 )
        return 0;

    for( int slot = 0; slot < RSCACHE_SOUND_TONES; slot++ )
        if( effect->tones[slot] )
            effect->tones[slot]->start -= start * 20;
    if( effect->loop_begin < effect->loop_end )
    {
        effect->loop_begin -= start * 20;
        effect->loop_end -= start * 20;
    }
    return start;
}

void
RSCache_SoundEffectFree(struct RSCache_SoundEffect* effect)
{
    if( !effect )
        return;
    for( int slot = 0; slot < RSCACHE_SOUND_TONES; slot++ )
        free_tone(effect->tones[slot]);
    free(effect);
}

void
RSCache_SoundBankFree(struct RSCache_SoundBank* bank)
{
    if( !bank )
        return;
    for( int id = 0; id < bank->count; id++ )
        RSCache_SoundEffectFree(bank->effects[id]);
    free(bank->effects);
    free(bank->order);
    free(bank);
}
