#include "music_patch.h"

#include <stdlib.h>
#include <string.h>

/*
 * A direct port of the reference's MusicPatch constructor.
 *
 * Kept in the reference's order and with its variable roles intact, because the
 * format is not self-describing: every read depends on where the last one left
 * the cursor, and there is no field the decoder could re-sync on. Reordering
 * "for clarity" is how a patch ends up plausible and wrong.
 */

struct patch_reader
{
    const uint8_t* data;
    int size;
    int pos;
    bool overrun;
};

static int
pr_u8(struct patch_reader* r)
{
    if( r->pos < 0 || r->pos >= r->size )
    {
        r->overrun = true;
        return 0;
    }
    return r->data[r->pos++];
}

static int
pr_s8(struct patch_reader* r)
{
    return (int8_t)pr_u8(r);
}

/** MIDI-style variable-length quantity (reference Buffer.readVarInt). */
static int
pr_varint(struct patch_reader* r)
{
    int value = 0;
    int byte = pr_s8(r);

    while( byte < 0 )
    {
        value = (value | (byte & 127)) << 7;
        byte = pr_s8(r);
        if( r->overrun )
            return 0;
    }
    return value | byte;
}

/** Length of the NUL-terminated run at the cursor, without consuming it. */
static int
pr_run_length(struct patch_reader* r)
{
    int length = 0;

    while( r->pos + length < r->size && r->data[r->pos + length] != 0 )
        length++;
    if( r->pos + length >= r->size )
    {
        r->overrun = true;
        return 0;
    }
    return length;
}

void
RSCache_MusicPatchFree(struct RSCache_MusicPatch* patch)
{
    if( !patch )
        return;
    for( int i = 0; i < patch->envelope_count; i++ )
    {
        free(patch->envelopes[i].amplitude);
        free(patch->envelopes[i].release);
    }
    free(patch->envelopes);
    free(patch);
}

/**
 * Apply one of the two trailing ramps.
 *
 * The ramp is a list of (note, value) breakpoints; between breakpoints the
 * value is interpolated with the reference's exact integer stepping, which
 * rounds toward zero and biases by half a step. `scale_volume` picks between
 * the two consumers: volume multiplies by value/64, pan adds 2*value and
 * clamps to 0..128.
 */
static void
apply_ramp(
    const uint8_t* ramp,
    int ramp_len,
    void* target,
    bool scale_volume)
{
    int8_t* volume = (int8_t*)target;
    uint8_t* pan = (uint8_t*)target;
    int from;
    int value;

    if( !ramp || ramp_len < 2 )
        return;
    from = ramp[0];
    value = scale_volume ? (int8_t)ramp[1] : ((int8_t)ramp[1]) << 1;
    if( from > 128 )
        from = 128;

    for( int note = 0; note < from; note++ )
    {
        if( scale_volume )
            volume[note] = (int8_t)((volume[note] * value + 32) >> 6);
        else
        {
            int updated = pan[note] + value;
            pan[note] = (uint8_t)(updated < 0 ? 0 : updated > 128 ? 128 : updated);
        }
    }

    for( int i = 2; i + 1 < ramp_len; i += 2 )
    {
        int to = ramp[i];
        int to_value = scale_volume ? (int8_t)ramp[i + 1] : ((int8_t)ramp[i + 1]) << 1;
        int span = to - from;
        int accumulator;

        if( to > 128 )
            to = 128;
        span = to - from;
        if( span <= 0 )
        {
            from = to;
            value = to_value;
            continue;
        }
        accumulator = span / 2 + span * value;
        for( int note = from; note < to; note++ )
        {
            int sign = (int)((uint32_t)accumulator >> 31);
            int stepped = (accumulator + sign) / span - sign;
            if( scale_volume )
            {
                volume[note] = (int8_t)((volume[note] * stepped + 32) >> 6);
            }
            else
            {
                int updated = pan[note] + stepped;
                pan[note] = (uint8_t)(updated < 0 ? 0 : updated > 128 ? 128 : updated);
            }
            accumulator += to_value - value;
        }
        from = to;
        value = to_value;
    }

    for( int note = from; note < 128; note++ )
    {
        if( scale_volume )
            volume[note] = (int8_t)((volume[note] * value + 32) >> 6);
        else
        {
            int updated = pan[note] + value;
            pan[note] = (uint8_t)(updated < 0 ? 0 : updated > 128 ? 128 : updated);
        }
    }
}

struct RSCache_MusicPatch*
RSCache_MusicPatchNewDecode(
    const char* data,
    int data_size)
{
    struct RSCache_MusicPatch* patch;
    struct patch_reader reader;
    int8_t* group_counts = NULL;
    int8_t* pan_counts = NULL;
    int8_t* envelope_counts = NULL;
    int8_t* sample_counts = NULL;
    uint8_t* envelope_tree = NULL;
    uint8_t* volume_ramp = NULL;
    uint8_t* pan_ramp = NULL;
    int group_count_len = 0;
    int pan_count_len = 0;
    int envelope_count_len = 0;
    int sample_count_len = 0;
    int group_values;
    int pan_values;
    int envelope_set_count = 0;
    int volume_ramp_len = 0;
    int pan_ramp_len = 0;

    if( !data || data_size <= 0 )
        return NULL;
    patch = calloc(1, sizeof(*patch));
    if( !patch )
        return NULL;
    for( int i = 0; i < 128; i++ )
    {
        patch->envelope_index[i] = -1;
        patch->note_group[i] = -1;
    }

    reader.data = (const uint8_t*)data;
    reader.size = data_size;
    reader.pos = 0;
    reader.overrun = false;

    /* Three (counts, values) stream pairs. The counts are NUL-terminated and
     * read here; the values are the same length and are indexed in place. */
    group_count_len = pr_run_length(&reader);
    if( reader.overrun )
        goto fail;
    group_counts = malloc((size_t)(group_count_len + 1));
    if( !group_counts )
        goto fail;
    for( int i = 0; i < group_count_len; i++ )
        group_counts[i] = (int8_t)pr_s8(&reader);
    reader.pos++;
    group_count_len++;
    group_values = reader.pos;
    reader.pos += group_count_len;

    pan_count_len = pr_run_length(&reader);
    if( reader.overrun )
        goto fail;
    pan_counts = malloc((size_t)(pan_count_len + 1));
    if( !pan_counts )
        goto fail;
    for( int i = 0; i < pan_count_len; i++ )
        pan_counts[i] = (int8_t)pr_s8(&reader);
    reader.pos++;
    pan_count_len++;
    pan_values = reader.pos;
    reader.pos += pan_count_len;

    envelope_count_len = pr_run_length(&reader);
    if( reader.overrun )
        goto fail;
    envelope_counts = malloc((size_t)(envelope_count_len + 1));
    if( !envelope_counts )
        goto fail;
    for( int i = 0; i < envelope_count_len; i++ )
        envelope_counts[i] = (int8_t)pr_s8(&reader);
    reader.pos++;
    envelope_count_len++;

    /*
     * The envelope tree: for each run, which distinct envelope set it uses.
     * Coded as "0 = a new set, otherwise the index of a previous one, biased
     * past the current". Run 0 is implicit and run 1 always uses set 1, which
     * is why the walk starts at 2 with `last = 1` and `count = 2`.
     */
    envelope_tree = calloc((size_t)(envelope_count_len > 0 ? envelope_count_len : 1), 1);
    if( !envelope_tree )
        goto fail;
    if( envelope_count_len > 1 )
    {
        int last = 1;
        envelope_tree[1] = 1;
        envelope_set_count = 2;
        for( int i = 2; i < envelope_count_len; i++ )
        {
            int coded = pr_u8(&reader);
            if( coded == 0 )
            {
                last = envelope_set_count++;
            }
            else
            {
                if( coded <= last )
                    coded--;
                last = coded;
            }
            envelope_tree[i] = (uint8_t)last;
        }
    }
    else
    {
        envelope_set_count = envelope_count_len;
    }
    if( reader.overrun || envelope_set_count < 0 )
        goto fail;

    patch->envelope_count = envelope_set_count;
    if( envelope_set_count > 0 )
    {
        patch->envelopes =
            calloc((size_t)envelope_set_count, sizeof(struct RSCache_MusicPatchEnvelope));
        if( !patch->envelopes )
            goto fail;
    }
    for( int i = 0; i < envelope_set_count; i++ )
    {
        struct RSCache_MusicPatchEnvelope* envelope = &patch->envelopes[i];
        int amplitude_pairs = pr_u8(&reader);
        int release_pairs;

        if( amplitude_pairs > 0 )
        {
            envelope->amplitude_len = amplitude_pairs * 2;
            envelope->amplitude = calloc((size_t)envelope->amplitude_len, 1);
            if( !envelope->amplitude )
                goto fail;
        }
        release_pairs = pr_u8(&reader);
        if( release_pairs > 0 )
        {
            envelope->release_len = release_pairs * 2 + 2;
            envelope->release = calloc((size_t)envelope->release_len, 1);
            if( !envelope->release )
                goto fail;
            envelope->release[1] = 64;
        }
    }
    if( reader.overrun )
        goto fail;

    volume_ramp_len = pr_u8(&reader) * 2;
    if( volume_ramp_len > 0 )
    {
        volume_ramp = calloc((size_t)volume_ramp_len, 1);
        if( !volume_ramp )
            goto fail;
    }
    pan_ramp_len = pr_u8(&reader) * 2;
    if( pan_ramp_len > 0 )
    {
        pan_ramp = calloc((size_t)pan_ramp_len, 1);
        if( !pan_ramp )
            goto fail;
    }

    sample_count_len = pr_run_length(&reader);
    if( reader.overrun )
        goto fail;
    sample_counts = malloc((size_t)(sample_count_len + 1));
    if( !sample_counts )
        goto fail;
    for( int i = 0; i < sample_count_len; i++ )
        sample_counts[i] = (int8_t)pr_s8(&reader);
    reader.pos++;
    sample_count_len++;

    /* Pitch offsets: the low byte and the high byte are two independent delta
     * runs over all 128 notes. */
    {
        int accumulator = 0;
        for( int note = 0; note < 128; note++ )
        {
            accumulator += pr_u8(&reader);
            patch->pitch_offset[note] = (int16_t)accumulator;
        }
        accumulator = 0;
        for( int note = 0; note < 128; note++ )
        {
            accumulator += pr_u8(&reader);
            patch->pitch_offset[note] = (int16_t)(patch->pitch_offset[note] + (accumulator << 8));
        }
    }

    /* Sample references, run-length coded by `sample_counts`. */
    {
        int run = 0;
        int cursor = 0;
        int reference = 0;
        for( int note = 0; note < 128; note++ )
        {
            if( run == 0 )
            {
                run = cursor < sample_count_len - 1 ? sample_counts[cursor++] : -1;
                reference = pr_varint(&reader);
            }
            patch->pitch_offset[note] =
                (int16_t)(patch->pitch_offset[note] + (((reference - 1) & 2) << 14));
            patch->sample_ref[note] = reference;
            run--;
        }
    }

    /* Exclusive groups, run-length coded by `group_counts`, values inline. */
    {
        int run = 0;
        int cursor = 0;
        int value = 0;
        for( int note = 0; note < 128; note++ )
        {
            if( patch->sample_ref[note] == 0 )
                continue;
            if( run == 0 )
            {
                run = cursor < group_count_len - 1 ? group_counts[cursor++] : -1;
                if( group_values >= reader.size )
                    goto fail;
                value = (int8_t)reader.data[group_values++] - 1;
            }
            patch->note_group[note] = (int8_t)value;
            run--;
        }
    }

    /* Pan, run-length coded by `pan_counts`, values inline. */
    {
        int run = 0;
        int cursor = 0;
        int value = 0;
        for( int note = 0; note < 128; note++ )
        {
            if( patch->sample_ref[note] == 0 )
                continue;
            if( run == 0 )
            {
                run = cursor < pan_count_len - 1 ? pan_counts[cursor++] : -1;
                if( pan_values >= reader.size )
                    goto fail;
                value = ((int8_t)reader.data[pan_values++] + 16) << 2;
            }
            patch->pan[note] = (uint8_t)value;
            run--;
        }
    }

    /* Envelope assignment, run-length coded by `envelope_counts` through the
     * tree built above. */
    {
        int run = 0;
        int cursor = 0;
        int selected = -1;
        for( int note = 0; note < 128; note++ )
        {
            if( patch->sample_ref[note] == 0 )
                continue;
            if( run == 0 )
            {
                int tree_index = cursor < envelope_count_len ? envelope_tree[cursor] : 0;
                selected = tree_index < envelope_set_count ? tree_index : -1;
                run = cursor < envelope_count_len - 1 ? envelope_counts[cursor++] : -1;
            }
            patch->envelope_index[note] = selected;
            run--;
        }
    }

    /* Volume, run-length coded by `sample_counts` again with its own cursor. */
    {
        int run = 0;
        int cursor = 0;
        int value = 0;
        for( int note = 0; note < 128; note++ )
        {
            if( run == 0 )
            {
                run = cursor < sample_count_len - 1 ? sample_counts[cursor++] : -1;
                if( patch->sample_ref[note] > 0 )
                    value = pr_u8(&reader) + 1;
            }
            patch->volume[note] = (int8_t)value;
            run--;
        }
    }

    patch->volume_scale = pr_u8(&reader) + 1;
    if( reader.overrun )
        goto fail;

    /* Envelope levels, then ramp levels, then envelope times. The split is the
     * reference's: all levels first, all times after, each delta+1 coded. */
    for( int i = 0; i < envelope_set_count; i++ )
    {
        struct RSCache_MusicPatchEnvelope* envelope = &patch->envelopes[i];
        if( envelope->amplitude )
        {
            for( int j = 1; j < envelope->amplitude_len; j += 2 )
                envelope->amplitude[j] = (uint8_t)pr_s8(&reader);
        }
        if( envelope->release )
        {
            for( int j = 3; j < envelope->release_len - 2; j += 2 )
                envelope->release[j] = (uint8_t)pr_s8(&reader);
        }
    }
    if( volume_ramp )
    {
        for( int j = 1; j < volume_ramp_len; j += 2 )
            volume_ramp[j] = (uint8_t)pr_s8(&reader);
    }
    if( pan_ramp )
    {
        for( int j = 1; j < pan_ramp_len; j += 2 )
            pan_ramp[j] = (uint8_t)pr_s8(&reader);
    }

    for( int i = 0; i < envelope_set_count; i++ )
    {
        struct RSCache_MusicPatchEnvelope* envelope = &patch->envelopes[i];
        int accumulator = 0;
        if( !envelope->release )
            continue;
        for( int j = 2; j < envelope->release_len; j += 2 )
        {
            accumulator = accumulator + 1 + pr_u8(&reader);
            envelope->release[j] = (uint8_t)accumulator;
        }
    }
    for( int i = 0; i < envelope_set_count; i++ )
    {
        struct RSCache_MusicPatchEnvelope* envelope = &patch->envelopes[i];
        int accumulator = 0;
        if( !envelope->amplitude )
            continue;
        for( int j = 2; j < envelope->amplitude_len; j += 2 )
        {
            accumulator = accumulator + 1 + pr_u8(&reader);
            envelope->amplitude[j] = (uint8_t)accumulator;
        }
    }

    if( volume_ramp )
    {
        int accumulator = pr_u8(&reader);
        volume_ramp[0] = (uint8_t)accumulator;
        for( int j = 2; j < volume_ramp_len; j += 2 )
        {
            accumulator = accumulator + 1 + pr_u8(&reader);
            volume_ramp[j] = (uint8_t)accumulator;
        }
        apply_ramp(volume_ramp, volume_ramp_len, patch->volume, true);
    }
    if( pan_ramp )
    {
        int accumulator = pr_u8(&reader);
        pan_ramp[0] = (uint8_t)accumulator;
        for( int j = 2; j < pan_ramp_len; j += 2 )
        {
            accumulator = accumulator + 1 + pr_u8(&reader);
            pan_ramp[j] = (uint8_t)accumulator;
        }
        apply_ramp(pan_ramp, pan_ramp_len, patch->pan, false);
    }

    for( int i = 0; i < envelope_set_count; i++ )
        patch->envelopes[i].decay = pr_u8(&reader);
    for( int i = 0; i < envelope_set_count; i++ )
    {
        struct RSCache_MusicPatchEnvelope* envelope = &patch->envelopes[i];
        if( envelope->amplitude )
            envelope->amplitude_rate = pr_u8(&reader);
        if( envelope->release )
            envelope->release_rate = pr_u8(&reader);
        if( envelope->decay > 0 )
            envelope->decay_rate = pr_u8(&reader);
    }
    for( int i = 0; i < envelope_set_count; i++ )
        patch->envelopes[i].vibrato_speed = pr_u8(&reader);
    for( int i = 0; i < envelope_set_count; i++ )
    {
        if( patch->envelopes[i].vibrato_speed > 0 )
            patch->envelopes[i].vibrato_depth = pr_u8(&reader);
    }
    for( int i = 0; i < envelope_set_count; i++ )
    {
        if( patch->envelopes[i].vibrato_depth > 0 )
            patch->envelopes[i].vibrato_delay = pr_u8(&reader);
    }

    if( reader.overrun )
        goto fail;
    patch->_consumed = reader.pos;

    free(group_counts);
    free(pan_counts);
    free(envelope_counts);
    free(sample_counts);
    free(envelope_tree);
    free(volume_ramp);
    free(pan_ramp);
    return patch;

fail:
    free(group_counts);
    free(pan_counts);
    free(envelope_counts);
    free(sample_counts);
    free(envelope_tree);
    free(volume_ramp);
    free(pan_ramp);
    RSCache_MusicPatchFree(patch);
    return NULL;
}
