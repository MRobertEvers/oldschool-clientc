#include "rsbuf.h"

#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>

void
rsbuf_init(struct RSBuffer* buffer, uint8_t* data, int size)
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

// signed
char
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

char*
rsbuf_read_string(struct RSBuffer* buffer)
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
        if( ch == 0 )
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
        if( ch == 0 )
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

int
rsbuf_read_unsigned_int_smart_short_compat(struct RSBuffer* buffer)
{
    // int var1 = 0;

    // int var2;
    // for (var2 = this.readUnsignedShortSmart(); var2 == 32767; var2 =
    // this.readUnsignedShortSmart())
    // {
    // 	var1 += 32767;
    // }

    // var1 += var2;
    // return var1;

    int var1 = 0;
    int var2;
    for( var2 = rsbuf_read_unsigned_short_smart(buffer); var2 == 32767;
         var2 = rsbuf_read_unsigned_short_smart(buffer) )
    {
        var1 += 32767;
    }
    var1 += var2;
    return var1;
}

int
rsbuf_read_unsigned_short_smart(struct RSBuffer* buffer)
{
    int peek = buffer->data[buffer->position] & 0xFF;
    return peek < 128 ? rsbuf_g1(buffer) : rsbuf_g2(buffer) - 0x8000;
}