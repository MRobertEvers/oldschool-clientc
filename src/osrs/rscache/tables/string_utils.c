#include "string_utils.h"

#include <ctype.h>
#include <string.h>

#if !defined(_WIN32)
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
#endif

void
str_ascii_toupper(
    char* str,
    int len)
{
    /* Check i < len before str[i]: callers may pass a 1-byte buffer (e.g. &c, 1). */
    for( int i = 0; i < len && str[i] != '\0'; i++ )
        str[i] = (char)toupper((unsigned char)str[i]);
}

void
strncpy_trimmed(
    char* dest,
    const char* src,
    size_t n,
    char const* trim_chars)
{
    size_t i = 0;
    size_t j = 0;

    // Find the first non-trim character
    while( src[i] != '\0' && strchr(trim_chars, src[i]) != NULL )
        i++;

    // Copy the non-trim characters to the
    // destination buffer, ensuring we don't exceed the limit
    while( src[i] != '\0' && j < n - 1 )
        dest[j++] = src[i++];

    // Go backwards trim trailing trim characters
    while( j > 0 && strchr(trim_chars, dest[j - 1]) != NULL )
        j--;

    // Null-terminate the destination buffer
    dest[j] = '\0';
}
