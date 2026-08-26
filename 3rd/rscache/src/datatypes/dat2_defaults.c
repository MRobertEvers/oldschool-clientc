#include "dat2_defaults.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * Opcode widths follow class11.method235 in the osrs239 deob:
 *
 *   1  one 24-bit value, read and discarded
 *   2  eleven bigsmart sprite ids
 *   3  int[3][5] of 24-bit RGB
 *   5  two int32 model ids
 *   6  as 2, plus one trailing bigsmart, also discarded
 *   0  terminator
 *
 * "bigsmart" is the deob's method13607: high bit set means a 4-byte value
 * masked with 0x7FFFFFFF, otherwise a u16 in which 32767 means -1. That is
 * RSCache_BufferReadBigSmart exactly.
 */

const char* const RSCache_Dat2DefaultsSpriteSlotNames[RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT] = {
    "compass", "mapedge",  "mapscene", "headicons_pk", "headicons_prayer", "headicons_hint",
    "mapmarker", "cross",  "mapdots",  "scrollbar",    "mod_icons",
};

/** Bytes left to read. */
static uint32_t
remaining(const struct RSCache_Buffer* buffer)
{
    return buffer->position >= buffer->size ? 0 : buffer->size - buffer->position;
}

/**
 * Whether a whole bigsmart is still in the buffer.
 *
 * Its width is in its first byte, so this has to peek before it can answer --
 * which is also why it cannot be folded into a plain `remaining() >= n` at the
 * call site.
 */
static int
bigsmart_fits(const struct RSCache_Buffer* buffer)
{
    uint32_t left = remaining(buffer);
    int wide;

    if( left < 1 )
        return 0;
    wide = (buffer->data[buffer->position] & 0x80) != 0;
    return left >= (uint32_t)(wide ? 4 : 2);
}

/** Payload width of one opcode, terminator and opcode byte excluded. */
static uint32_t
opcode_bound(int opcode)
{
    switch( opcode )
    {
    case 1:
        return 3;
    case 2:
        /* Eleven bigsmarts, each at most 4 bytes. */
        return RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT * 4;
    case 3:
        return RSCACHE_DAT2_DEFAULTS_RAMP_ROWS * RSCACHE_DAT2_DEFAULTS_RAMP_STOPS * 3;
    case 5:
        return RSCACHE_DAT2_DEFAULTS_MODEL_COUNT * 4;
    case 6:
        return (RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT + 1) * 4;
    default:
        return 0;
    }
}

int
RSCache_Dat2DefaultsDecode(
    const uint8_t* data,
    int data_size,
    struct RSCache_Dat2Defaults* out)
{
    struct RSCache_Buffer buffer;

    assert(data);
    assert(out);

    /* An empty payload is a legitimate thing to be handed and not this record. */
    if( data_size <= 0 )
        return 0;

    memset(out, 0, sizeof(*out));
    for( int i = 0; i < RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT; i++ )
        out->sprite_ids[i] = -1;

    buffer.data = (uint8_t*)data;
    buffer.size = (uint32_t)data_size;
    buffer.position = 0;
    buffer.owns_data = false;

    for( ;; )
    {
        int opcode;

        if( remaining(&buffer) < 1 )
            return 0; /* ran out before the terminator */
        opcode = g1(&buffer);
        if( opcode == 0 )
            break;

        if( out->opcode_count >= RSCACHE_DAT2_DEFAULTS_MAX_OPCODES )
            return 0;
        out->opcode_order[out->opcode_count++] = (uint8_t)opcode;

        switch( opcode )
        {
        case 1:
            if( remaining(&buffer) < 3 )
                return 0;
            out->legacy_value = g3(&buffer);
            out->has_legacy_value = 1;
            break;

        case 2:
        case 6:
            /* Two id blocks in one record is not a shape this decoder can write
             * back, so decline rather than keep the last one and lose the first. */
            if( out->sprite_opcode != 0 )
                return 0;
            for( int i = 0; i < RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT; i++ )
            {
                if( !bigsmart_fits(&buffer) )
                    return 0;
                out->sprite_ids[i] = gbigsmart(&buffer);
            }
            if( opcode == 6 )
            {
                if( !bigsmart_fits(&buffer) )
                    return 0;
                out->sprite_trailer = gbigsmart(&buffer);
            }
            out->sprite_opcode = opcode;
            break;

        case 3:
            if( remaining(&buffer) <
                RSCACHE_DAT2_DEFAULTS_RAMP_ROWS * RSCACHE_DAT2_DEFAULTS_RAMP_STOPS * 3u )
                return 0;
            for( int row = 0; row < RSCACHE_DAT2_DEFAULTS_RAMP_ROWS; row++ )
            {
                for( int stop = 0; stop < RSCACHE_DAT2_DEFAULTS_RAMP_STOPS; stop++ )
                    out->ramps[row][stop] = g3(&buffer);
            }
            out->has_ramps = 1;
            break;

        case 5:
            if( remaining(&buffer) < RSCACHE_DAT2_DEFAULTS_MODEL_COUNT * 4u )
                return 0;
            for( int i = 0; i < RSCACHE_DAT2_DEFAULTS_MODEL_COUNT; i++ )
                out->model_ids[i] = g4(&buffer);
            out->has_models = 1;
            break;

        default:
            /* An opcode this decoder does not know. Every byte after it is
             * unaligned, so there is nothing to salvage. */
            return 0;
        }
    }

    out->consumed = (int)buffer.position;
    /* Trailing bytes mean the terminator was data, not a terminator. */
    if( out->consumed != data_size )
        return 0;
    return 1;
}

uint32_t
RSCache_Dat2DefaultsEncodeBound(const struct RSCache_Dat2Defaults* defaults)
{
    uint32_t bound = 1; /* terminator */

    assert(defaults);
    for( int i = 0; i < defaults->opcode_count; i++ )
        bound += 1 + opcode_bound(defaults->opcode_order[i]);
    return bound;
}

uint32_t
RSCache_Dat2DefaultsEncode(
    const struct RSCache_Dat2Defaults* defaults,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buffer;

    assert(defaults);
    assert(out);

    if( out_capacity < RSCache_Dat2DefaultsEncodeBound(defaults) )
        return 0;

    buffer.data = out;
    buffer.size = out_capacity;
    buffer.position = 0;
    buffer.owns_data = false;

    for( int i = 0; i < defaults->opcode_count; i++ )
    {
        int opcode = defaults->opcode_order[i];

        p1(&buffer, opcode);
        switch( opcode )
        {
        case 1:
            p3(&buffer, defaults->legacy_value);
            break;

        case 2:
        case 6:
            for( int j = 0; j < RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT; j++ )
                pbigsmart(&buffer, defaults->sprite_ids[j]);
            if( opcode == 6 )
                pbigsmart(&buffer, defaults->sprite_trailer);
            break;

        case 3:
            for( int row = 0; row < RSCACHE_DAT2_DEFAULTS_RAMP_ROWS; row++ )
            {
                for( int stop = 0; stop < RSCACHE_DAT2_DEFAULTS_RAMP_STOPS; stop++ )
                    p3(&buffer, defaults->ramps[row][stop]);
            }
            break;

        case 5:
            for( int j = 0; j < RSCACHE_DAT2_DEFAULTS_MODEL_COUNT; j++ )
                p4(&buffer, defaults->model_ids[j]);
            break;

        default:
            return 0;
        }
    }
    p1(&buffer, 0);
    return buffer.position;
}

int
RSCache_Dat2DefaultsRoundTrips(
    const struct RSCache_Dat2Defaults* defaults,
    const uint8_t* data,
    int data_size)
{
    uint8_t* scratch;
    uint32_t bound;
    uint32_t written;
    int same;

    assert(defaults);
    assert(data);

    if( data_size <= 0 )
        return 0;

    bound = RSCache_Dat2DefaultsEncodeBound(defaults);
    scratch = (uint8_t*)malloc(bound);
    assert(scratch);

    written = RSCache_Dat2DefaultsEncode(defaults, scratch, bound);
    same = written == (uint32_t)data_size && memcmp(scratch, data, (size_t)data_size) == 0;
    free(scratch);
    return same;
}

int
RSCache_Dat2DefaultsColoursDecode(
    const uint8_t* data,
    int data_size,
    struct RSCache_Dat2DefaultsColours* out)
{
    struct RSCache_Buffer buffer;
    int stops;

    assert(data);
    assert(out);

    /* size = 3n + (n-1) = 4n - 1, so anything else is a different record. */
    if( data_size < 3 || (data_size + 1) % 4 != 0 )
        return 0;
    stops = (data_size + 1) / 4;
    if( stops > RSCACHE_DAT2_DEFAULTS_COLOUR_MAX_STOPS )
        return 0;

    memset(out, 0, sizeof(*out));
    out->stop_count = stops;

    buffer.data = (uint8_t*)data;
    buffer.size = (uint32_t)data_size;
    buffer.position = 0;
    buffer.owns_data = false;

    out->colours[0] = g3(&buffer);
    for( int i = 1; i < stops; i++ )
    {
        out->intervals[i - 1] = g1(&buffer);
        out->colours[i] = g3(&buffer);
    }
    return 1;
}

uint32_t
RSCache_Dat2DefaultsColoursEncodeBound(const struct RSCache_Dat2DefaultsColours* colours)
{
    assert(colours);
    assert(colours->stop_count > 0);
    return (uint32_t)(4 * colours->stop_count - 1);
}

uint32_t
RSCache_Dat2DefaultsColoursEncode(
    const struct RSCache_Dat2DefaultsColours* colours,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buffer;

    assert(colours);
    assert(out);
    assert(colours->stop_count > 0);

    if( out_capacity < RSCache_Dat2DefaultsColoursEncodeBound(colours) )
        return 0;

    buffer.data = out;
    buffer.size = out_capacity;
    buffer.position = 0;
    buffer.owns_data = false;

    p3(&buffer, colours->colours[0]);
    for( int i = 1; i < colours->stop_count; i++ )
    {
        p1(&buffer, colours->intervals[i - 1]);
        p3(&buffer, colours->colours[i]);
    }
    return buffer.position;
}

int
RSCache_Dat2DefaultsColoursRoundTrips(
    const struct RSCache_Dat2DefaultsColours* colours,
    const uint8_t* data,
    int data_size)
{
    uint8_t* scratch;
    uint32_t bound;
    uint32_t written;
    int same;

    assert(colours);
    assert(data);

    if( data_size <= 0 )
        return 0;

    bound = RSCache_Dat2DefaultsColoursEncodeBound(colours);
    scratch = (uint8_t*)malloc(bound);
    assert(scratch);

    written = RSCache_Dat2DefaultsColoursEncode(colours, scratch, bound);
    same = written == (uint32_t)data_size && memcmp(scratch, data, (size_t)data_size) == 0;
    free(scratch);
    return same;
}
