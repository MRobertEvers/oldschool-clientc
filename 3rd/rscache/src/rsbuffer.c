#include "rsbuffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

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
    static const wchar_t CHARACTERS[] = {
        L'\u20ac', L'\0',     L'\u201a', L'\u0192', L'\u201e', L'\u2026', L'\u2020', L'\u2021',
        L'\u02c6', L'\u2030', L'\u0160', L'\u2039', L'\u0152', L'\0',     L'\u017d', L'\0',
        L'\0',     L'\u2018', L'\u2019', L'\u201c', L'\u201d', L'\u2022', L'\u2013', L'\u2014',
        L'\u02dc', L'\u2122', L'\u0161', L'\u203a', L'\u0153', L'\0',     L'\u017e', L'\u0178'
    };

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

    // Read string with character mapping
    while( buffer->position < buffer->size )
    {
        int ch = RSCache_BufferG1(buffer);
        if( ch == stop_char )
        {
            break;
        }

        if( ch >= 128 && ch < 160 )
        {
            wchar_t mapped = CHARACTERS[ch - 128];
            if( mapped == 0 )
            {
                mapped = L'?';
            }
            ch = (char)mapped;
        }

        string[i++] = ch;
    }
    string[length] = '\0';
    return string;
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

    // Initialize params with next power of 2 size
    int capacity = 1;
    while( capacity < length )
    {
        capacity <<= 1;
    }

    params->keys = malloc(capacity * sizeof(int));
    params->values = malloc(capacity * sizeof(void*));
    params->is_string = malloc(capacity * sizeof(bool));

    if( !params->keys || !params->values || !params->is_string )
    {
        printf(
            "RSCache_BufferReadParams: Failed to allocate params arrays of capacity %d\n",
            capacity);
        if( params->keys )
            free(params->keys);
        if( params->values )
            free(params->values);
        if( params->is_string )
            free(params->is_string);
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
            // Cleanup on error
            for( int j = 0; j < params->count; j++ )
            {
                if( params->values[j] )
                {
                    free(params->values[j]);
                }
            }
            free(params->keys);
            free(params->values);
            free(params->is_string);
            params->keys = NULL;
            params->values = NULL;
            params->is_string = NULL;
            params->count = 0;
            params->capacity = 0;
            return;
        }

        bool is_string = (RSCache_BufferG1(buffer) & 0xFF) == 1;
        int key = RSCache_BufferG3(buffer);
        void* value;

        if( is_string )
        {
            // Read string length first
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
                    "index "
                    "%d\n",
                    i);
                // Cleanup on error
                for( int j = 0; j < params->count; j++ )
                {
                    if( params->values[j] )
                    {
                        free(params->values[j]);
                    }
                }
                free(params->keys);
                free(params->values);
                free(params->is_string);
                params->keys = NULL;
                params->values = NULL;
                params->is_string = NULL;
                params->count = 0;
                params->capacity = 0;
                return;
            }
            value = malloc(str_len + 1);
            if( !value )
            {
                printf(
                    "RSCache_BufferReadParams: Failed to allocate param string of length %d at "
                    "index %d\n",
                    str_len,
                    i);
                // Cleanup on error
                for( int j = 0; j < params->count; j++ )
                {
                    if( params->values[j] )
                    {
                        free(params->values[j]);
                    }
                }
                free(params->keys);
                free(params->values);
                free(params->is_string);
                params->keys = NULL;
                params->values = NULL;
                params->is_string = NULL;
                params->count = 0;
                params->capacity = 0;
                return;
            }
            RSCache_BufferReadto(buffer, value, str_len + 1, str_len + 1);
        }
        else
        {
            if( buffer->position + 3 >= buffer->size )
            {
                printf(
                    "RSCache_BufferReadParams: Buffer overflow while reading param int at index "
                    "%d\n",
                    i);
                // Cleanup on error
                for( int j = 0; j < params->count; j++ )
                {
                    if( params->values[j] )
                    {
                        free(params->values[j]);
                    }
                }
                free(params->keys);
                free(params->values);
                free(params->is_string);
                params->keys = NULL;
                params->values = NULL;
                params->is_string = NULL;
                params->count = 0;
                params->capacity = 0;
                return;
            }
            value = malloc(sizeof(int));
            if( !value )
            {
                printf("decode_npc_type: Failed to allocate param int at index %d\n", i);
                // Cleanup on error
                for( int j = 0; j < params->count; j++ )
                {
                    if( params->values[j] )
                    {
                        free(params->values[j]);
                    }
                }
                free(params->keys);
                free(params->values);
                free(params->is_string);
                params->keys = NULL;
                params->values = NULL;
                params->is_string = NULL;
                params->count = 0;
                params->capacity = 0;
                return;
            }
            *(int*)value = RSCache_BufferG4(buffer);
        }

        params->keys[params->count] = key;
        params->values[params->count] = value;
        params->is_string[params->count] = is_string;
        params->count++;
    }
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
        bool is_string = params->is_string[i];
        RSCache_BufferP1(buffer, is_string ? 1 : 0);
        RSCache_BufferP3(buffer, params->keys[i]);
        if( is_string )
        {
            const char* str = params->values[i] ? (const char*)params->values[i] : "";
            RSCache_BufferPjstr(buffer, str, RSCACHE_JSTR_TERMINATOR_NULL);
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
    /* Byte-transparent on purpose. The matching reader maps input bytes
     * 128..159 through a Unicode table and then truncates each result to
     * (char), which is not invertible: the 32 truncated values collide with 14
     * printable ASCII bytes (space ! " & 0 9 : R S ` a x } ~) and two source
     * bytes (149, 153) both land on 0x22. Applying the inverse map here would
     * corrupt any ordinary string containing a space or an 'a'. */
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