#include "rsbuf.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

void
rsbuf_init(
    struct RSBuffer* buffer,
    int8_t* data,
    int size)
{
    buffer->data = data;
    buffer->size = size;
    buffer->position = 0;
}

int
rsbuf_g1(struct RSBuffer* buffer)
{
    return buffer->data[buffer->position++] & 0xff;
}

void
rsbuf_p1(
    struct RSBuffer* buffer,
    int value)
{
    buffer->data[buffer->position++] = value & 0xff;
}

// signed
int8_t
rsbuf_g1b(struct RSBuffer* buffer)
{
    return buffer->data[buffer->position++];
}

int
rsbuf_g2(struct RSBuffer* buffer)
{
    buffer->position += 2;
    return (buffer->data[buffer->position - 2] & 0xff) << 8 |
           (buffer->data[buffer->position - 1] & 0xff);
}

void
rsbuf_p2(
    struct RSBuffer* buffer,
    int value)
{
    buffer->data[buffer->position++] = value >> 8 & 0xff;
    buffer->data[buffer->position++] = value & 0xff;
}

// signed
int
rsbuf_g2b(struct RSBuffer* buffer)
{
    buffer->position += 2;
    int value = (buffer->data[buffer->position - 2] & 0xff) << 8 |
                (buffer->data[buffer->position - 1] & 0xff);

    if( value > 32767 )
        value -= 65536;
    return value;
}

int
rsbuf_g3(struct RSBuffer* buffer)
{
    buffer->position += 3;
    return (buffer->data[buffer->position - 3] & 0xff) << 16 |
           (buffer->data[buffer->position - 2] & 0xff) << 8 |
           (buffer->data[buffer->position - 1] & 0xff);
}

int
rsbuf_g4(struct RSBuffer* buffer)
{
    buffer->position += 4;
    return (buffer->data[buffer->position - 4] & 0xff) << 24 |
           (buffer->data[buffer->position - 3] & 0xff) << 16 |
           (buffer->data[buffer->position - 2] & 0xff) << 8 |
           (buffer->data[buffer->position - 1] & 0xff);
}

int64_t
rsbuf_g8(struct RSBuffer* buffer)
{
    int64_t high = (int64_t)rsbuf_g4(buffer) & 0xffffffffLL;
    int64_t low = (int64_t)rsbuf_g4(buffer) & 0xffffffffLL;
    return (high << 32) | low;
}

int
rsbuf_read_usmart(struct RSBuffer* buffer)
{
    assert(buffer->position < buffer->size);
    int peek = buffer->data[buffer->position];
    if( peek < 0 )
        return rsbuf_g4(buffer) & 0x7fffffff;
    else
        return rsbuf_g2(buffer) & 0xFFFF;
}

int
rsbuf_read_big_smart(struct RSBuffer* buffer)
{
    //   if (this.getByte(this.offset) < 0) {
    //         return this.readInt() & 0x7fffffff;
    //     } else {
    //         const v = this.readUnsignedShort();
    //         if (v === 32767) {
    //             return -1;
    //         }
    //         return v;
    //     }

    int peek = buffer->data[buffer->position] & 0xFF;
    if( peek < 0 )
        return rsbuf_g4(buffer) & 0x7fffffff;
    else
    {
        int v = rsbuf_g2(buffer);
        if( v == 32767 )
            return -1;
        return v;
    }
}

static inline char*
read_string(
    struct RSBuffer* buffer,
    int stop_char)
{
    static const wchar_t CHARACTERS[] = {
        L'\u20ac', L'\0',     L'\u201a', L'\u0192', L'\u201e', L'\u2026', L'\u2020', L'\u2021',
        L'\u02c6', L'\u2030', L'\u0160', L'\u2039', L'\u0152', L'\0',     L'\u017d', L'\0',
        L'\0',     L'\u2018', L'\u2019', L'\u201c', L'\u201d', L'\u2022', L'\u2013', L'\u2014',
        L'\u02dc', L'\u2122', L'\u0161', L'\u203a', L'\u0153', L'\0',     L'\u017e', L'\u0178'
    };

    // Count string length first
    int pos = buffer->position;
    int length = 0;
    while( 1 )
    {
        int ch = rsbuf_g1(buffer);
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
    while( 1 )
    {
        int ch = rsbuf_g1(buffer);
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
rsbuf_read_string_null_terminated(struct RSBuffer* buffer)
{
    return read_string(buffer, 0x00);
}

char*
rsbuf_read_string_newline_terminated(struct RSBuffer* buffer)
{
    return read_string(buffer, 0x0a);
}

int
rsbuf_read_unsigned_int_smart_short_compat(struct RSBuffer* buffer)
{
    int var1 = 0;
    int var2 = rsbuf_read_unsigned_short_smart(buffer);
    while( var2 == 32767 )
    {
        var1 += 32767;
        var2 = rsbuf_read_unsigned_short_smart(buffer);
    }
    var1 += var2;
    return var1;
}

int
rsbuf_read_short_smart(struct RSBuffer* buffer)
{
    int peek = buffer->data[buffer->position] & 0xFF;
    return peek < 128 ? (rsbuf_g1(buffer) - 64) : (rsbuf_g2(buffer) - 0xC000);
}

int
rsbuf_read_unsigned_short_smart(struct RSBuffer* buffer)
{
    int peek = buffer->data[buffer->position] & 0xFF;
    return peek < 128 ? rsbuf_g1(buffer) : (rsbuf_g2(buffer) - 0x8000);
}

int
rsbuf_readto(
    struct RSBuffer* buffer,
    char* out,
    int out_size,
    int len)
{
    assert(buffer->position + len <= buffer->size);
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
rsbuf_read_params(
    struct RSBuffer* buffer,
    struct Params* params)
{
    if( buffer->position >= buffer->size )
    {
        printf("rsbuf_read_params: Buffer overflow\n");
        return;
    }
    int length = rsbuf_g1(buffer) & 0xFF;

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
        printf("rsbuf_read_params: Failed to allocate params arrays of capacity %d\n", capacity);
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
            printf("rsbuf_read_params: Buffer overflow while reading params at index %d\n", i);
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

        bool is_string = (rsbuf_g1(buffer) & 0xFF) == 1;
        int key = rsbuf_g3(buffer);
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
                    "rsbuf_read_params: Buffer overflow while reading param string at index "
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
                    "rsbuf_read_params: Failed to allocate param string of length %d at "
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
            rsbuf_readto(buffer, value, str_len + 1, str_len + 1);
        }
        else
        {
            if( buffer->position + 3 >= buffer->size )
            {
                printf(
                    "rsbuf_read_params: Buffer overflow while reading param int at index "
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
            *(int*)value = rsbuf_g4(buffer);
        }

        params->keys[params->count] = key;
        params->values[params->count] = value;
        params->is_string[params->count] = is_string;
        params->count++;
    }
}