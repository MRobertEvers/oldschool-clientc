#include "rsbuffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RSCACHE_BUFFER_DEFAULT_CAPACITY 256

void
RSCache_BufferInit(
    struct RSCache_Buffer* buffer,
    uint8_t* data,
    uint32_t size)
{
    buffer->data = data;
    buffer->size = size;
    buffer->position = 0;
    buffer->owns_data = false;
}

bool
RSCache_BufferInitAlloc(
    struct RSCache_Buffer* buffer,
    uint32_t initial_capacity)
{
    if( initial_capacity == 0 )
        initial_capacity = RSCACHE_BUFFER_DEFAULT_CAPACITY;

    buffer->data = malloc(initial_capacity);
    if( !buffer->data )
    {
        buffer->size = 0;
        buffer->position = 0;
        buffer->owns_data = false;
        return false;
    }

    buffer->size = initial_capacity;
    buffer->position = 0;
    buffer->owns_data = true;
    return true;
}

void
RSCache_BufferRelease(struct RSCache_Buffer* buffer)
{
    if( !buffer )
        return;
    if( buffer->owns_data )
        free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
    buffer->position = 0;
    buffer->owns_data = false;
}

uint8_t*
RSCache_BufferDetach(
    struct RSCache_Buffer* buffer,
    uint32_t* out_size)
{
    if( !buffer || !buffer->owns_data )
    {
        if( out_size )
            *out_size = 0;
        return NULL;
    }

    uint8_t* data = buffer->data;
    if( out_size )
        *out_size = buffer->position;

    buffer->data = NULL;
    buffer->size = 0;
    buffer->position = 0;
    buffer->owns_data = false;
    return data;
}

bool
RSCache_BufferEnsure(
    struct RSCache_Buffer* buffer,
    uint32_t extra)
{
    uint32_t needed = buffer->position + extra;
    /* Wrap of the cursor itself is a caller bug, not a growth request. */
    if( needed < buffer->position )
        return false;
    if( needed <= buffer->size )
        return true;
    if( !buffer->owns_data )
        return false;

    uint32_t capacity = buffer->size ? buffer->size : RSCACHE_BUFFER_DEFAULT_CAPACITY;
    while( capacity < needed )
    {
        uint32_t doubled = capacity * 2;
        /* Saturate to the exact requirement rather than wrap on a huge length. */
        capacity = doubled > capacity ? doubled : needed;
    }

    uint8_t* data = realloc(buffer->data, capacity);
    if( !data )
        return false;

    buffer->data = data;
    buffer->size = capacity;
    return true;
}

/* Every "p" routes through this: grow when owned, assert when borrowed and out
 * of room. Borrowed overflow already asserted before owned mode existed, so the
 * net stack's behaviour is unchanged. */
static inline void
buffer_reserve(
    struct RSCache_Buffer* buffer,
    uint32_t extra)
{
    bool room = RSCache_BufferEnsure(buffer, extra);
    assert(room && "RSCache_Buffer write past the end of a borrowed buffer");
    (void)room;
}

int
RSCache_BufferG1(struct RSCache_Buffer* buffer)
{
    if( buffer->position >= buffer->size )
        return 0;
    return buffer->data[buffer->position++] & 0xff;
}

void
RSCache_BufferP1(
    struct RSCache_Buffer* buffer,
    int value)
{
    assert(value < 256);
    buffer_reserve(buffer, 1);
    buffer->data[buffer->position++] = value & 0xff;
}

// signed
int8_t
RSCache_BufferG1b(struct RSCache_Buffer* buffer)
{
    if( buffer->position >= buffer->size )
        return 0;
    return buffer->data[buffer->position++];
}

void
RSCache_BufferP1b(
    struct RSCache_Buffer* buffer,
    int value)
{
    assert(value >= -128 && value <= 127);
    buffer_reserve(buffer, 1);
    buffer->data[buffer->position++] = (uint8_t)(value & 0xff);
}

int
RSCache_BufferG2(struct RSCache_Buffer* buffer)
{
    if( buffer->position + 2u > buffer->size )
        return 0;
    int value =
        (buffer->data[buffer->position] & 0xff) << 8 | (buffer->data[buffer->position + 1] & 0xff);
    buffer->position += 2;
    return value;
}

void
RSCache_BufferP2(
    struct RSCache_Buffer* buffer,
    int value)
{
    buffer_reserve(buffer, 2);
    buffer->data[buffer->position++] = value >> 8 & 0xff;
    buffer->data[buffer->position++] = value & 0xff;
}

// signed
int
RSCache_BufferG2b(struct RSCache_Buffer* buffer)
{
    if( buffer->position + 2u > buffer->size )
        return 0;
    int value =
        (buffer->data[buffer->position] & 0xff) << 8 | (buffer->data[buffer->position + 1] & 0xff);
    buffer->position += 2;

    if( value > 32767 )
        value -= 65536;
    return value;
}

void
RSCache_BufferP2b(
    struct RSCache_Buffer* buffer,
    int value)
{
    assert(value >= -32768 && value <= 32767);
    RSCache_BufferP2(buffer, value & 0xffff);
}

int
RSCache_BufferG3(struct RSCache_Buffer* buffer)
{
    if( buffer->position + 3u > buffer->size )
        return 0;
    int value = (buffer->data[buffer->position] & 0xff) << 16 |
                (buffer->data[buffer->position + 1] & 0xff) << 8 |
                (buffer->data[buffer->position + 2] & 0xff);
    buffer->position += 3;
    return value;
}

void
RSCache_BufferP3(
    struct RSCache_Buffer* buffer,
    int value)
{
    buffer_reserve(buffer, 3);
    buffer->data[buffer->position++] = value >> 16 & 0xff;
    buffer->data[buffer->position++] = value >> 8 & 0xff;
    buffer->data[buffer->position++] = value & 0xff;
}

int
RSCache_BufferG4(struct RSCache_Buffer* buffer)
{
    if( buffer->position + 4u > buffer->size )
        return 0;
    int value = (buffer->data[buffer->position] & 0xff) << 24 |
                (buffer->data[buffer->position + 1] & 0xff) << 16 |
                (buffer->data[buffer->position + 2] & 0xff) << 8 |
                (buffer->data[buffer->position + 3] & 0xff);
    buffer->position += 4;
    return value;
}

void
RSCache_BufferP4(
    struct RSCache_Buffer* buffer,
    int value)
{
    buffer_reserve(buffer, 4);
    buffer->data[buffer->position++] = value >> 24 & 0xff;
    buffer->data[buffer->position++] = value >> 16 & 0xff;
    buffer->data[buffer->position++] = value >> 8 & 0xff;
    buffer->data[buffer->position++] = value & 0xff;
}

int64_t
RSCache_BufferG8(struct RSCache_Buffer* buffer)
{
    int64_t high = (int64_t)RSCache_BufferG4(buffer) & 0xffffffffLL;
    int64_t low = (int64_t)RSCache_BufferG4(buffer) & 0xffffffffLL;
    return (high << 32) | low;
}

void
RSCache_BufferP8(
    struct RSCache_Buffer* buffer,
    int64_t value)
{
    RSCache_BufferP4(buffer, (int)((uint64_t)value >> 32));
    RSCache_BufferP4(buffer, (int)((uint64_t)value & 0xffffffffULL));
}

int
RSCache_BufferReadVarInt2(struct RSCache_Buffer* buffer)
{
    int value = 0;
    int bits = 0;
    int byte;
    do
    {
        if( buffer->position >= buffer->size )
            break;
        byte = RSCache_BufferG1(buffer);
        value |= (byte & 0x7f) << bits;
        bits += 7;
    } while( byte > 127 );
    return value;
}

void
RSCache_BufferWriteVarInt2(
    struct RSCache_Buffer* buffer,
    int value)
{
    /* Unsigned so a negative input emits the five groups the reader needs to
     * rebuild the same bit pattern, instead of shifting a sign bit forever. */
    uint32_t bits = (uint32_t)value;
    while( bits > 0x7fu )
    {
        RSCache_BufferP1(buffer, (int)((bits & 0x7fu) | 0x80u));
        bits >>= 7;
    }
    RSCache_BufferP1(buffer, (int)(bits & 0x7fu));
}

float
RSCache_BufferReadFloat(struct RSCache_Buffer* buffer)
{
    uint32_t bits = (uint32_t)RSCache_BufferG4(buffer);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

void
RSCache_BufferWriteFloat(
    struct RSCache_Buffer* buffer,
    float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    RSCache_BufferP4(buffer, (int)bits);
}

int
RSCache_BufferG1At(
    const uint8_t* data,
    int* offset)
{
    return data[(*offset)++] & 0xff;
}

int
RSCache_BufferG2At(
    const uint8_t* data,
    int* offset)
{
    int value = (data[*offset] << 8) | data[*offset + 1];
    *offset += 2;
    return value & 0xffff;
}

int
RSCache_BufferG4At(
    const uint8_t* data,
    int* offset)
{
    int value = (data[*offset] << 24) | (data[*offset + 1] << 16) | (data[*offset + 2] << 8) |
                data[*offset + 3];
    *offset += 4;
    return value;
}

int
RSCache_BufferReadShortSmartAt(
    const uint8_t* data,
    int* offset)
{
    int peek = data[*offset] & 0xff;
    if( peek < 128 )
        return RSCache_BufferG1At(data, offset) - 64;
    return RSCache_BufferG2At(data, offset) - 0xC000;
}

void
RSCache_BufferP1At(
    uint8_t* data,
    int* offset,
    int value)
{
    data[(*offset)++] = (uint8_t)(value & 0xff);
}

void
RSCache_BufferP2At(
    uint8_t* data,
    int* offset,
    int value)
{
    data[(*offset)++] = (uint8_t)(value >> 8 & 0xff);
    data[(*offset)++] = (uint8_t)(value & 0xff);
}

void
RSCache_BufferP4At(
    uint8_t* data,
    int* offset,
    int value)
{
    data[(*offset)++] = (uint8_t)(value >> 24 & 0xff);
    data[(*offset)++] = (uint8_t)(value >> 16 & 0xff);
    data[(*offset)++] = (uint8_t)(value >> 8 & 0xff);
    data[(*offset)++] = (uint8_t)(value & 0xff);
}

void
RSCache_BufferWriteShortSmartAt(
    uint8_t* data,
    int* offset,
    int value)
{
    if( value >= -64 && value <= 63 )
        RSCache_BufferP1At(data, offset, value + 64);
    else
        RSCache_BufferP2At(data, offset, (value + 0xC000) & 0xffff);
}

int
RSCache_BufferReadUsmart(struct RSCache_Buffer* buffer)
{
    assert(buffer->position < buffer->size);
    int peek = buffer->data[buffer->position] & 0xFF;
    if( peek < 128 )
        return RSCache_BufferG2(buffer) & 0xFFFF;
    return RSCache_BufferG4(buffer) & 0x7fffffff;
}

void
RSCache_BufferWriteUsmart(
    struct RSCache_Buffer* buffer,
    int value)
{
    /* The reader picks the width off the top bit of the first byte, so the
     * 2-byte form is only reachable while the value fits in 15 bits. */
    assert(value >= 0);
    if( value < 0x8000 )
        RSCache_BufferP2(buffer, value);
    else
        RSCache_BufferP4(buffer, value | (int)0x80000000);
}

int
RSCache_BufferReadBigSmart(struct RSCache_Buffer* buffer)
{
    int peek = buffer->data[buffer->position] & 0xFF;
    if( peek < 128 )
    {
        int v = RSCache_BufferG2(buffer);
        if( v == 32767 )
            return -1;
        return v;
    }
    return RSCache_BufferG4(buffer) & 0x7fffffff;
}

void
RSCache_BufferWriteBigSmart(
    struct RSCache_Buffer* buffer,
    int value)
{
    if( value == -1 )
    {
        RSCache_BufferP2(buffer, 32767);
        return;
    }
    assert(value >= 0);
    /* 32767 is spoken for by -1, so the 2-byte form stops one short of it. */
    if( value < 32767 )
        RSCache_BufferP2(buffer, value);
    else
        RSCache_BufferP4(buffer, value | (int)0x80000000);
}

static inline char*
read_string(
    struct RSCache_Buffer* buffer,
    int stop_char)
{
    /* Byte-transparent: wire bytes are windows-1252 and stay that way in
     * memory. Unicode conversion is a consumer concern — see
     * RSCache_Cp1252ToUtf8 / RSCache_Utf8ToCp1252. */

    // Count string length first. Stop at end-of-buffer too: G1 past the end
    // returns 0 without advancing, which would otherwise spin forever when
    // the terminator is missing (e.g. a truncated network packet).
    int pos = buffer->position;
    int length = 0;
    while( buffer->position < buffer->size )
    {
        int ch = RSCache_BufferG1(buffer);
        if( ch == stop_char )
        {
            break;
        }
        length++;
    }

    // Reset position and allocate string
    buffer->position = pos;
    char* string = malloc(length + 1);
    int i = 0;

    while( buffer->position < buffer->size )
    {
        int ch = RSCache_BufferG1(buffer);
        if( ch == stop_char )
        {
            break;
        }
        string[i++] = (char)ch;
    }
    string[length] = '\0';
    return string;
}

/* Windows-1252 specials for bytes 0x80..0x9F. Undefined slots are 0; those
 * round-trip as U+0081 / U+008D / U+008F / U+0090 / U+009D (same numeric value
 * as the source byte) so Cp1252ToUtf8 / Utf8ToCp1252 form a bijection. */
static const uint16_t RSCACHE_CP1252_SPECIAL[32] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178
};

static uint16_t
cp1252_byte_to_codepoint(uint8_t b)
{
    if( b < 128 || b >= 160 )
        return (uint16_t)b;
    uint16_t special = RSCACHE_CP1252_SPECIAL[b - 128];
    return special != 0 ? special : (uint16_t)b;
}

static int
utf8_encode_codepoint(
    uint16_t cp,
    char* dst,
    int dst_cap)
{
    if( cp < 0x80 )
    {
        if( dst && dst_cap >= 1 )
            dst[0] = (char)cp;
        return 1;
    }
    if( cp < 0x800 )
    {
        if( dst && dst_cap >= 2 )
        {
            dst[0] = (char)(0xC0 | (cp >> 6));
            dst[1] = (char)(0x80 | (cp & 0x3F));
        }
        return 2;
    }
    if( dst && dst_cap >= 3 )
    {
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
    }
    return 3;
}

static int
utf8_decode_codepoint(
    const char* src,
    int src_remaining,
    uint16_t* out_cp)
{
    assert(src && out_cp && src_remaining > 0);
    unsigned char b0 = (unsigned char)src[0];
    if( b0 < 0x80 )
    {
        *out_cp = b0;
        return 1;
    }
    if( (b0 & 0xE0) == 0xC0 && src_remaining >= 2 )
    {
        unsigned char b1 = (unsigned char)src[1];
        if( (b1 & 0xC0) == 0x80 )
        {
            *out_cp = (uint16_t)(((b0 & 0x1F) << 6) | (b1 & 0x3F));
            return 2;
        }
    }
    if( (b0 & 0xF0) == 0xE0 && src_remaining >= 3 )
    {
        unsigned char b1 = (unsigned char)src[1];
        unsigned char b2 = (unsigned char)src[2];
        if( (b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 )
        {
            *out_cp = (uint16_t)(((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F));
            return 3;
        }
    }
    /* Malformed: consume one byte as '?'. */
    *out_cp = (uint16_t)'?';
    return 1;
}

static uint8_t
codepoint_to_cp1252(uint16_t cp)
{
    if( (cp > 0 && cp < 128) || (cp >= 160 && cp <= 255) )
        return (uint8_t)cp;
    /* Undefined C1 controls that we emit for undefined cp1252 slots. */
    if( cp == 0x81 || cp == 0x8D || cp == 0x8F || cp == 0x90 || cp == 0x9D )
        return (uint8_t)cp;
    for( int i = 0; i < 32; i++ )
    {
        if( RSCACHE_CP1252_SPECIAL[i] == cp )
            return (uint8_t)(128 + i);
    }
    return (uint8_t)'?';
}

int
RSCache_Cp1252ToUtf8(
    const char* src,
    char* dst,
    int dst_size)
{
    assert(src);
    assert(dst || dst_size == 0);

    int needed = 0;
    int written = 0;
    for( const unsigned char* p = (const unsigned char*)src; *p; p++ )
    {
        uint16_t cp = cp1252_byte_to_codepoint(*p);
        int rem = dst_size > 0 ? dst_size - 1 - written : 0;
        char* out = (dst && rem > 0) ? dst + written : NULL;
        int n = utf8_encode_codepoint(cp, out, rem);
        needed += n;
        if( out && rem >= n )
            written += n;
    }
    if( dst && dst_size > 0 )
        dst[written] = '\0';
    return needed;
}

int
RSCache_Utf8ToCp1252(
    const char* src,
    char* dst,
    int dst_size)
{
    assert(src);
    assert(dst || dst_size == 0);

    int needed = 0;
    int written = 0;
    int src_len = (int)strlen(src);
    int i = 0;
    while( i < src_len )
    {
        uint16_t cp;
        int n = utf8_decode_codepoint(src + i, src_len - i, &cp);
        i += n;
        uint8_t b = codepoint_to_cp1252(cp);
        needed++;
        if( dst && dst_size > 0 && written < dst_size - 1 )
            dst[written++] = (char)b;
    }
    if( dst && dst_size > 0 )
        dst[written] = '\0';
    return needed;
}

char*
RSCache_BufferReadStringNullTerminated(struct RSCache_Buffer* buffer)
{
    return read_string(buffer, 0x00);
}

char*
RSCache_BufferReadStringNewlineTerminated(struct RSCache_Buffer* buffer)
{
    return read_string(buffer, 0x0a);
}

int
RSCache_BufferReadUnsignedIntSmartShortCompat(struct RSCache_Buffer* buffer)
{
    int var1 = 0;
    int var2 = RSCache_BufferReadUnsignedShortSmart(buffer);
    while( var2 == 32767 )
    {
        var1 += 32767;
        var2 = RSCache_BufferReadUnsignedShortSmart(buffer);
    }
    var1 += var2;
    return var1;
}

void
RSCache_BufferWriteUnsignedIntSmartShortCompat(
    struct RSCache_Buffer* buffer,
    int value)
{
    assert(value >= 0);
    /* 32767 is the "keep going" escape, so a value that is an exact multiple of
     * it still needs a trailing remainder or the reader eats what follows. */
    while( value >= 32767 )
    {
        RSCache_BufferWriteUnsignedShortSmart(buffer, 32767);
        value -= 32767;
    }
    RSCache_BufferWriteUnsignedShortSmart(buffer, value);
}

int
RSCache_BufferReadShortSmart(struct RSCache_Buffer* buffer)
{
    int peek = buffer->data[buffer->position] & 0xFF;
    return peek < 128 ? (RSCache_BufferG1(buffer) - 64) : (RSCache_BufferG2(buffer) - 0xC000);
}

void
RSCache_BufferWriteShortSmart(
    struct RSCache_Buffer* buffer,
    int value)
{
    assert(value >= -16384 && value <= 16383);
    if( value >= -64 && value <= 63 )
        RSCache_BufferP1(buffer, value + 64);
    else
        RSCache_BufferP2(buffer, (value + 0xC000) & 0xffff);
}

int
RSCache_BufferReadUnsignedShortSmart(struct RSCache_Buffer* buffer)
{
    int peek = buffer->data[buffer->position] & 0xFF;
    return peek < 128 ? RSCache_BufferG1(buffer) : (RSCache_BufferG2(buffer) - 0x8000);
}

void
RSCache_BufferWriteUnsignedShortSmart(
    struct RSCache_Buffer* buffer,
    int value)
{
    assert(value >= 0 && value <= 32767);
    if( value < 128 )
        RSCache_BufferP1(buffer, value);
    else
        RSCache_BufferP2(buffer, value + 0x8000);
}

int
RSCache_BufferReadto(
    struct RSCache_Buffer* buffer,
    char* out,
    int out_size,
    int len)
{
    (void)out_size;
    assert(buffer->position + (uint32_t)len <= buffer->size);
    int bytes_read = 0;
    while( len > 0 && buffer->position < buffer->size )
    {
        out[bytes_read] = buffer->data[buffer->position];
        len--;
        buffer->position++;
        bytes_read++;
    }

    return bytes_read;
}

static void
params_cleanup_partial(struct RSCache_Params* params)
{
    if( !params )
        return;
    for( int j = 0; j < params->count; j++ )
    {
        if( params->values[j] )
            free(params->values[j]);
    }
    free(params->keys);
    free(params->values);
    free(params->kinds);
    params->keys = NULL;
    params->values = NULL;
    params->kinds = NULL;
    params->count = 0;
    params->capacity = 0;
}

void
RSCache_BufferReadParams(
    struct RSCache_Buffer* buffer,
    struct RSCache_Params* params)
{
    if( buffer->position >= buffer->size )
    {
        printf("RSCache_BufferReadParams: Buffer overflow\n");
        return;
    }
    int length = RSCache_BufferG1(buffer) & 0xFF;

    int capacity = 1;
    while( capacity < length )
        capacity <<= 1;

    params->keys = malloc(capacity * sizeof(int));
    params->values = malloc(capacity * sizeof(void*));
    params->kinds = malloc(capacity * sizeof(uint8_t));

    if( !params->keys || !params->values || !params->kinds )
    {
        printf(
            "RSCache_BufferReadParams: Failed to allocate params arrays of capacity %d\n",
            capacity);
        free(params->keys);
        free(params->values);
        free(params->kinds);
        params->keys = NULL;
        params->values = NULL;
        params->kinds = NULL;
        return;
    }

    params->count = 0;
    params->capacity = capacity;

    for( int i = 0; i < length; i++ )
    {
        if( buffer->position >= buffer->size )
        {
            printf(
                "RSCache_BufferReadParams: Buffer overflow while reading params at index %d\n", i);
            params_cleanup_partial(params);
            return;
        }

        int type_byte = RSCache_BufferG1(buffer) & 0xFF;
        int key = RSCache_BufferG3(buffer);
        void* value;
        uint8_t kind;

        if( type_byte == RSCACHE_PARAM_STRING )
        {
            kind = RSCACHE_PARAM_STRING;
            int str_len = 0;
            while( buffer->position + str_len < buffer->size &&
                   buffer->data[buffer->position + str_len] != '\0' )
            {
                str_len++;
            }
            if( buffer->position + str_len >= buffer->size )
            {
                printf(
                    "RSCache_BufferReadParams: Buffer overflow while reading param string at "
                    "index %d\n",
                    i);
                params_cleanup_partial(params);
                return;
            }
            value = malloc((size_t)str_len + 1);
            if( !value )
            {
                printf(
                    "RSCache_BufferReadParams: Failed to allocate param string of length %d at "
                    "index %d\n",
                    str_len,
                    i);
                params_cleanup_partial(params);
                return;
            }
            RSCache_BufferReadto(buffer, value, str_len + 1, str_len + 1);
        }
        else if( type_byte == RSCACHE_PARAM_LONG )
        {
            kind = RSCACHE_PARAM_LONG;
            if( buffer->position + 7 >= buffer->size )
            {
                printf(
                    "RSCache_BufferReadParams: Buffer overflow while reading param long at index "
                    "%d\n",
                    i);
                params_cleanup_partial(params);
                return;
            }
            value = malloc(sizeof(int64_t));
            if( !value )
            {
                printf(
                    "RSCache_BufferReadParams: Failed to allocate param long at index %d\n", i);
                params_cleanup_partial(params);
                return;
            }
            *(int64_t*)value = RSCache_BufferG8(buffer);
        }
        else
        {
            kind = RSCACHE_PARAM_INT;
            if( buffer->position + 3 >= buffer->size )
            {
                printf(
                    "RSCache_BufferReadParams: Buffer overflow while reading param int at index "
                    "%d\n",
                    i);
                params_cleanup_partial(params);
                return;
            }
            value = malloc(sizeof(int));
            if( !value )
            {
                printf(
                    "RSCache_BufferReadParams: Failed to allocate param int at index %d\n", i);
                params_cleanup_partial(params);
                return;
            }
            *(int*)value = RSCache_BufferG4(buffer);
        }

        params->keys[params->count] = key;
        params->values[params->count] = value;
        params->kinds[params->count] = kind;
        params->count++;
    }
}

uint32_t
RSCache_BufferParamsBound(const struct RSCache_Params* params)
{
    int count = params ? params->count : 0;
    /* The count byte itself. */
    uint32_t need = 1u;

    for( int i = 0; i < count; i++ )
    {
        uint8_t kind = params->kinds ? params->kinds[i] : (uint8_t)RSCACHE_PARAM_INT;

        /* kind + 3-byte key, then the payload. */
        need += 1u + 3u;
        if( kind == RSCACHE_PARAM_STRING )
        {
            const char* str = params->values && params->values[i]
                                  ? (const char*)params->values[i]
                                  : "";

            need += (uint32_t)strlen(str) + 1u;
        }
        else if( kind == RSCACHE_PARAM_LONG )
        {
            need += 8u;
        }
        else
        {
            need += 4u;
        }
    }
    return need;
}

void
RSCache_BufferWriteParams(
    struct RSCache_Buffer* buffer,
    const struct RSCache_Params* params)
{
    int count = params ? params->count : 0;
    /* The count is a single byte on the wire. */
    assert(count >= 0 && count <= 255);

    RSCache_BufferP1(buffer, count);
    for( int i = 0; i < count; i++ )
    {
        uint8_t kind = params->kinds ? params->kinds[i] : (uint8_t)RSCACHE_PARAM_INT;
        RSCache_BufferP1(buffer, kind);
        RSCache_BufferP3(buffer, params->keys[i]);
        if( kind == RSCACHE_PARAM_STRING )
        {
            const char* str = params->values[i] ? (const char*)params->values[i] : "";
            RSCache_BufferPjstr(buffer, str, RSCACHE_JSTR_TERMINATOR_NULL);
        }
        else if( kind == RSCACHE_PARAM_LONG )
        {
            int64_t v = params->values[i] ? *(const int64_t*)params->values[i] : 0;
            RSCache_BufferP8(buffer, v);
        }
        else
        {
            RSCache_BufferP4(buffer, params->values[i] ? *(const int*)params->values[i] : 0);
        }
    }
}

void
RSCache_BufferPjstr(
    struct RSCache_Buffer* buffer,
    const char* str,
    int terminator)
{
    /* Byte-transparent: decoded strings are windows-1252 wire bytes and are
     * written back unchanged. See RSCache_Cp1252ToUtf8 for Unicode. */
    if( str )
    {
        size_t len = strlen(str);
        RSCache_BufferPwrite(buffer, (const uint8_t*)str, (int)len);
    }
    RSCache_BufferP1(buffer, terminator);
}

void
RSCache_BufferPwrite(
    struct RSCache_Buffer* buffer,
    const uint8_t* data,
    int data_size)
{
    if( data_size <= 0 )
        return;
    buffer_reserve(buffer, (uint32_t)data_size);
    memcpy(buffer->data + buffer->position, data, (size_t)data_size);
    buffer->position += (uint32_t)data_size;
}