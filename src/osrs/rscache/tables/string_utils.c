#include "string_utils.h"

#include <ctype.h>

int
strcasecmp(
    const char* s1,
    const char* s2)
{
    if( s1 == NULL && s2 == NULL )
        return 0;
    if( s1 == NULL )
        return -1;
    if( s2 == NULL )
        return 1;

    while( *s1 && *s2 )
    {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);

        if( c1 != c2 )
        {
            return c1 - c2;
        }

        s1++;
        s2++;
    }

    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int
strncasecmp(
    const char* s1,
    const char* s2,
    size_t n)
{
    if( n == 0 )
        return 0;
    if( s1 == NULL && s2 == NULL )
        return 0;
    if( s1 == NULL )
        return -1;
    if( s2 == NULL )
        return 1;

    while( n > 0 && *s1 && *s2 )
    {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);

        if( c1 != c2 )
        {
            return c1 - c2;
        }

        s1++;
        s2++;
        n--;
    }

    if( n == 0 )
        return 0;
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

void
str_ascii_toupper(
    char* str,
    int len)
{
    for( int i = 0; str[i] != '\0' && i < len; i++ )
        str[i] = toupper(str[i]);
}