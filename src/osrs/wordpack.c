#include "wordpack.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char WORDPACK_TABLE[] = {
    ' ', 'e', 't', 'a', 'o', 'i', 'h', 'n', 's', 'r', 'd', 'l', 'u', 'm',
    'w', 'c', 'y', 'f', 'g', 'p', 'b', 'v', 'k', 'x', 'j', 'q', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    ' ', '!', '?', '.', ',', ':', ';', '(', ')', '-',
    '&', '*', '\\', '\'', '@', '#', '+', '=', (char)0xA3, '$', '%', '"', '[', ']'
};

#define WORDPACK_TABLE_SIZE 64

static int
wordpack_char_index(char c)
{
    for( size_t i = 0; i < WORDPACK_TABLE_SIZE; i++ )
    {
        if( (unsigned char)c == (unsigned char)WORDPACK_TABLE[i] )
            return (int)i;
    }
    return -1;
}

void
wordpack_pack(
    struct RSBuffer* buffer,
    const char* str)
{
    size_t len = str ? strlen(str) : 0;
    if( len > 80 )
        len = 80;

    int carry = -1;
    for( size_t idx = 0; idx < len; idx++ )
    {
        char c = (char)tolower((unsigned char)str[idx]);
        int current_char = wordpack_char_index(c);
        if( current_char < 0 )
            current_char = 0;

        if( current_char > 12 )
            current_char += 195;

        if( carry == -1 )
        {
            if( current_char < 13 )
                carry = current_char;
            else
                p1(buffer, current_char);
        }
        else if( current_char < 13 )
        {
            p1(buffer, (carry << 4) + current_char);
            carry = -1;
        }
        else
        {
            p1(buffer, (carry << 4) + (current_char >> 4));
            carry = current_char & 0xf;
        }
    }
    if( carry != -1 )
        p1(buffer, carry << 4);
}

char*
wordpack_unpack(
    struct RSBuffer* buffer,
    int length)
{
    char* out = malloc(101);
    if( !out )
        return NULL;
    memset(out, 0, 101);

    int pos = 0;
    int carry = -1;
    for( int idx = 0; idx < length && pos < 100; idx++ )
    {
        int value = g1(buffer);
        int nibble = (value >> 4) & 0xf;

        if( carry != -1 )
        {
            int idx2 = (carry << 4) + nibble - 195;
            if( idx2 >= 0 && idx2 < (int)WORDPACK_TABLE_SIZE )
                out[pos++] = WORDPACK_TABLE[idx2];
            carry = -1;
        }
        else if( nibble < 13 )
        {
            out[pos++] = WORDPACK_TABLE[nibble];
        }
        else
        {
            carry = nibble;
        }

        nibble = value & 0xf;
        if( carry != -1 )
        {
            int idx2 = (carry << 4) + nibble - 195;
            if( idx2 >= 0 && idx2 < (int)WORDPACK_TABLE_SIZE )
                out[pos++] = WORDPACK_TABLE[idx2];
            carry = -1;
        }
        else if( nibble < 13 )
        {
            out[pos++] = WORDPACK_TABLE[nibble];
        }
        else
        {
            carry = nibble;
        }
    }

    int uppercase = 1;
    for( int i = 0; i < pos; i++ )
    {
        char c = out[i];
        if( uppercase && c >= 'a' && c <= 'z' )
        {
            out[i] = (char)toupper((unsigned char)c);
            uppercase = 0;
        }
        if( c == '.' || c == '!' )
            uppercase = 1;
    }
    out[pos] = '\0';
    return out;
}
