#include "plattools.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static wchar_t*
plattools_utf8_to_wide(const char* text)
{
    int count;
    size_t text_length = strlen(text);
    wchar_t* result;

    if( text_length > (size_t)INT_MAX )
    {
        errno = ENAMETOOLONG;
        return NULL;
    }

    /* A zero flag also works on Windows XP.  CP_UTF8 still preserves valid
     * UTF-8 paths instead of routing them through the active ANSI code page. */
    count = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if( count == 0 )
    {
        errno = EINVAL;
        return NULL;
    }

    if( (size_t)count > SIZE_MAX / sizeof(*result) )
    {
        errno = ENOMEM;
        return NULL;
    }

    result = (wchar_t*)malloc((size_t)count * sizeof(*result));
    if( !result )
        return NULL;

    if( MultiByteToWideChar(CP_UTF8, 0, text, -1, result, count) == 0 )
    {
        free(result);
        errno = EINVAL;
        return NULL;
    }

    return result;
}

static char*
plattools_wide_to_utf8(const wchar_t* text)
{
    int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    char* result;

    if( count == 0 )
    {
        errno = EINVAL;
        return NULL;
    }

    result = (char*)malloc((size_t)count);
    if( !result )
        return NULL;

    if( WideCharToMultiByte(CP_UTF8, 0, text, -1, result, count, NULL, NULL) == 0 )
    {
        free(result);
        errno = EINVAL;
        return NULL;
    }

    return result;
}

char*
plattools_full_path(const char* path)
{
    wchar_t* input;
    wchar_t* full;
    char* result;
    DWORD capacity;
    DWORD length;

    if( !path || !path[0] )
    {
        errno = EINVAL;
        return NULL;
    }

    input = plattools_utf8_to_wide(path);
    if( !input )
        return NULL;

    capacity = GetFullPathNameW(input, 0, NULL, NULL);
    if( capacity == 0 )
    {
        free(input);
        errno = EINVAL;
        return NULL;
    }
#if SIZE_MAX <= UINT32_MAX
    if( (size_t)capacity > SIZE_MAX / sizeof(*full) )
    {
        free(input);
        errno = ENOMEM;
        return NULL;
    }
#endif

    full = (wchar_t*)malloc((size_t)capacity * sizeof(*full));
    if( !full )
    {
        free(input);
        return NULL;
    }

    length = GetFullPathNameW(input, capacity, full, NULL);
    free(input);
    if( length == 0 || length >= capacity )
    {
        free(full);
        errno = length == 0 ? EINVAL : ERANGE;
        return NULL;
    }

    result = plattools_wide_to_utf8(full);
    free(full);
    return result;
}

#else

#include <unistd.h>

static char*
plattools_getcwd_alloc(void)
{
    size_t capacity = 256;

    for( ;; )
    {
        char* cwd = (char*)malloc(capacity);
        int error;

        if( !cwd )
            return NULL;
        if( getcwd(cwd, capacity) )
            return cwd;

        error = errno;
        free(cwd);
        if( error != ERANGE )
        {
            errno = error;
            return NULL;
        }
        if( capacity > SIZE_MAX / 2 )
        {
            errno = ENOMEM;
            return NULL;
        }
        capacity *= 2;
    }
}

static void
plattools_normalize_posix(char* path)
{
    size_t read_at = 0;
    size_t write_at = 1;

    path[0] = '/';
    while( path[read_at] == '/' )
        read_at++;

    while( path[read_at] )
    {
        size_t segment_at = read_at;
        size_t segment_length;

        while( path[read_at] && path[read_at] != '/' )
            read_at++;
        segment_length = read_at - segment_at;
        while( path[read_at] == '/' )
            read_at++;

        if( segment_length == 1 && path[segment_at] == '.' )
            continue;

        if( segment_length == 2 && path[segment_at] == '.' && path[segment_at + 1] == '.' )
        {
            while( write_at > 1 && path[write_at - 1] != '/' )
                write_at--;
            if( write_at > 1 )
                write_at--;
            continue;
        }

        if( write_at > 1 )
            path[write_at++] = '/';
        memmove(path + write_at, path + segment_at, segment_length);
        write_at += segment_length;
    }

    path[write_at] = '\0';
}

#if defined(__EMSCRIPTEN__)
static int
plattools_ascii_equal_nocase(
    const char* left,
    const char* right,
    size_t length)
{
    size_t i;

    for( i = 0; i < length; i++ )
    {
        unsigned char a = (unsigned char)left[i];
        unsigned char b = (unsigned char)right[i];

        if( a >= 'A' && a <= 'Z' )
            a = (unsigned char)(a + ('a' - 'A'));
        if( b >= 'A' && b <= 'Z' )
            b = (unsigned char)(b + ('a' - 'A'));
        if( a != b )
            return 0;
    }
    return 1;
}

static const char*
plattools_web_storage_scheme(
    const char* path,
    size_t* consumed)
{
    static const char* const schemes[] = { "idb", "localstorage", "sessionstorage" };
    size_t i;

    for( i = 0; i < sizeof(schemes) / sizeof(schemes[0]); i++ )
    {
        size_t length = strlen(schemes[i]);

        if( strlen(path) > length && plattools_ascii_equal_nocase(path, schemes[i], length) &&
            path[length] == ':' )
        {
            const char* suffix = path + length + 1;
            while( *suffix == '/' )
                suffix++;
            *consumed = (size_t)(suffix - path);
            return schemes[i];
        }
    }
    return NULL;
}

static char*
plattools_web_storage_path(const char* path)
{
    size_t consumed = 0;
    const char* scheme = plattools_web_storage_scheme(path, &consumed);
    const char* suffix;
    size_t scheme_length;
    size_t suffix_length;
    char* normalized;
    char* result;

    if( !scheme )
        return NULL;

    suffix = path + consumed;
    scheme_length = strlen(scheme);
    suffix_length = strlen(suffix);
    normalized = (char*)malloc(suffix_length + 2);
    if( !normalized )
        return NULL;
    normalized[0] = '/';
    memcpy(normalized + 1, suffix, suffix_length + 1);
    plattools_normalize_posix(normalized);

    /* `scheme://` plus the normalized key without its synthetic leading '/'. */
    if( scheme_length > SIZE_MAX - strlen(normalized) - 3 )
    {
        free(normalized);
        errno = ENOMEM;
        return NULL;
    }
    result = (char*)malloc(scheme_length + strlen(normalized) + 3);
    if( !result )
    {
        free(normalized);
        return NULL;
    }
    memcpy(result, scheme, scheme_length);
    memcpy(result + scheme_length, "://", 3);
    strcpy(result + scheme_length + 3, normalized + 1);
    free(normalized);
    return result;
}
#endif

char*
plattools_full_path(const char* path)
{
    size_t path_length;
    char* result;

    if( !path || !path[0] )
    {
        errno = EINVAL;
        return NULL;
    }

#if defined(__EMSCRIPTEN__)
    {
        size_t ignored;
        if( plattools_web_storage_scheme(path, &ignored) )
            return plattools_web_storage_path(path);
    }
#endif

    path_length = strlen(path);
    if( path[0] == '/' )
    {
        result = (char*)malloc(path_length + 1);
        if( !result )
            return NULL;
        memcpy(result, path, path_length + 1);
    }
    else
    {
        char* cwd = plattools_getcwd_alloc();
        size_t cwd_length;

        if( !cwd )
            return NULL;
        cwd_length = strlen(cwd);
        if( cwd_length > SIZE_MAX - path_length - 2 )
        {
            free(cwd);
            errno = ENOMEM;
            return NULL;
        }

        result = (char*)malloc(cwd_length + path_length + 2);
        if( !result )
        {
            free(cwd);
            return NULL;
        }
        memcpy(result, cwd, cwd_length);
        result[cwd_length] = '/';
        memcpy(result + cwd_length + 1, path, path_length + 1);
        free(cwd);
    }

    plattools_normalize_posix(result);
    return result;
}

#endif
