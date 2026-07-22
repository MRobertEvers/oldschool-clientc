#include "rsbuffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

void
RSCache_BufferInit(
    struct RSCache_Buffer* buffer,
    uint8_t* data,
    uint32_t size)
{
    buffer->data = data;
    buffer->size = size;
    buffer->position = 0;
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
    assert(buffer->position < buffer->size);
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

float
RSCache_BufferReadFloat(struct RSCache_Buffer* buffer)
{
    uint32_t bits = (uint32_t)RSCache_BufferG4(buffer);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
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

int
RSCache_BufferReadUsmart(struct RSCache_Buffer* buffer)
{
    assert(buffer->position < buffer->size);
    int peek = buffer->data[buffer->position] & 0xFF;
    if( peek < 128 )
        return RSCache_BufferG2(buffer) & 0xFFFF;
    return RSCache_BufferG4(buffer) & 0x7fffffff;
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

int
RSCache_BufferReadShortSmart(struct RSCache_Buffer* buffer)
{
    int peek = buffer->data[buffer->position] & 0xFF;
    return peek < 128 ? (RSCache_BufferG1(buffer) - 64) : (RSCache_BufferG2(buffer) - 0xC000);
}

int
RSCache_BufferReadUnsignedShortSmart(struct RSCache_Buffer* buffer)
{
    int peek = buffer->data[buffer->position] & 0xFF;
    return peek < 128 ? RSCache_BufferG1(buffer) : (RSCache_BufferG2(buffer) - 0x8000);
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
RSCache_BufferPjstr(
    struct RSCache_Buffer* buffer,
    const char* str,
    int terminator)
{
    int len = strlen(str);
    for( int i = 0; i < len; i++ )
    {
        unsigned char c = (unsigned char)str[i];
        if( c >= 'A' && c <= 'Z' )
        {
            RSCache_BufferP1(buffer, c);
        }
        else if( c >= 'a' && c <= 'z' )
        {
            RSCache_BufferP1(buffer, c);
        }
        else if( c >= '0' && c <= '9' )
        {
            RSCache_BufferP1(buffer, c);
        }
        else
        {
            RSCache_BufferP1(buffer, c); // Pass through other chars
        }
    }
    RSCache_BufferP1(buffer, terminator); // Null terminator
}

void
RSCache_BufferPwrite(
    struct RSCache_Buffer* buffer,
    const uint8_t* data,
    int data_size)
{
    assert(buffer->position + data_size <= buffer->size);
    memcpy(buffer->data + buffer->position, data, data_size);
    buffer->position += data_size;
}