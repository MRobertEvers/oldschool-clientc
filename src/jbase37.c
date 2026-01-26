#include "base37.h"

static const char* BASE37_LOOKUP[] = {                               //
    '_',                                                             //
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', //
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', //
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
};

/**
 * Think of the uint64_t as an 8 byte buffer.
 * We are packing 12 characters into 8 bytes.
 *
 *
 *
 * @param s
 * @return uint64_t
 */
uint64_t
strtobase37(const char* s)
{
    uint64_t l = 0;

    for( int i = 0; i < 12; i++ )
    {
        if( *s == '\0' )
            break;

        unsigned char c = (unsigned char)*s++;
        l *= 37;

        if( c >= 'A' && c <= 'Z' )
        {
            l += (c - 'A' + 1);
        }
        else if( c >= 'a' && c <= 'z' )
        {
            l += (c - 'a' + 1);
        }
        else if( c >= '0' && c <= '9' )
        {
            l += (c - '0' + 27);
        }
    }

    return l;
}

void
base37tostr(
    uint64_t l,
    char* buffer,
    int buffer_size)
{
    // must be able to hold "invalid_name" + null
    if( buffer_size <= 0 )
        return;

    if( l == 0 || l >= MAX_BASE37 || (l % 37) == 0 )
    {
        strncpy(buffer, "invalid_name", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    char tmp[12];
    int len = 0;

    while( l != 0 && len < 12 )
    {
        uint64_t prev = l;
        l /= 37;

        uint64_t remainder = prev - l * 37;
        tmp[11 - len++] = BASE37_LOOKUP[remainder];
    }

    int out_len = len;

    if( out_len + 1 > buffer_size )
    {
        // not enough space
        strncpy(buffer, "invalid_name", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    memcpy(buffer, &tmp[12 - len], out_len);
    buffer[out_len] = '\0';
}