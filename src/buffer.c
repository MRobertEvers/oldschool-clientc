#include "buffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>
int
read_8(struct Buffer* buffer)
{
    assert(buffer->position + 1 <= buffer->data_size);
    buffer->position += 1;
    return buffer->data[buffer->position - 1] & 0xFF;
}

int
read_24(struct Buffer* buffer)
{
    assert(buffer->position + 3 <= buffer->data_size);
    buffer->position += 3;
    return ((buffer->data[buffer->position - 3] & 0xFF) << 16) |
           ((buffer->data[buffer->position - 2] & 0xFF) << 8) |
           (buffer->data[buffer->position - 1] & 0xFF);
}

int
read_16(struct Buffer* buffer)
{
    assert(buffer->position + 2 <= buffer->data_size);
    buffer->position += 2;
    return ((buffer->data[buffer->position - 2] & 0xFF) << 8) |
           (buffer->data[buffer->position - 1] & 0xFF);
}

int
read_32(struct Buffer* buffer)
{
    assert(buffer->position + 4 <= buffer->data_size);
    buffer->position += 4;
    // print the 4 bytes at the current position

    return ((buffer->data[buffer->position - 4] & 0xFF) << 24) |
           ((buffer->data[buffer->position - 3] & 0xFF) << 16) |
           ((buffer->data[buffer->position - 2] & 0xFF) << 8) |
           (buffer->data[buffer->position - 1] & 0xFF);
}

// {
// 	private static final char[] CHARACTERS = new char[]
// 		{
// 			'\u20ac', '\u0000', '\u201a', '\u0192', '\u201e', '\u2026',
// 			'\u2020', '\u2021', '\u02c6', '\u2030', '\u0160', '\u2039',
// 			'\u0152', '\u0000', '\u017d', '\u0000', '\u0000', '\u2018',
// 			'\u2019', '\u201c', '\u201d', '\u2022', '\u2013', '\u2014',
// 			'\u02dc', '\u2122', '\u0161', '\u203a', '\u0153', '\u0000',
// 			'\u017e', '\u0178'
// 		};
// public String readString()
// {
// 	StringBuilder sb = new StringBuilder();

// 	for (; ; )
// 	{
// 		int ch = this.readUnsignedByte();

// 		if (ch == 0)
// 		{
// 			break;
// 		}

// 		if (ch >= 128 && ch < 160)
// 		{
// 			char var7 = CHARACTERS[ch - 128];
// 			if (0 == var7)
// 			{
// 				var7 = '?';
// 			}

// 			ch = var7;
// 		}

// 		sb.append((char) ch);
// 	}
// 	return sb.toString();
// }

char*
read_string(struct Buffer* buffer)
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
    while( true )
    {
        int ch = read_8(buffer);
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
    while( true )
    {
        int ch = read_8(buffer);
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
readto(char* out, int out_size, int len, struct Buffer* buffer)
{
    assert(buffer->position + len <= buffer->data_size);
    int bytes_read = 0;
    while( len > 0 && buffer->position < buffer->data_size )
    {
        out[bytes_read] = buffer->data[buffer->position];
        len--;
        buffer->position++;
        bytes_read++;
    }

    return bytes_read;
}

void
bump(int count, struct Buffer* buffer)
{
    assert(buffer->position + count <= buffer->data_size);
    buffer->position += count;
}
