/*
 * tool_posix_compat.h -- the POSIX pieces MinGW does not ship, for the
 * cachepack / port_lostcity tools.
 *
 * These tools were written against glibc and Apple libc. Building them with
 * MinGW (the i686 toolchain the win32 client lane uses) fails in three ways,
 * and only the third is a real gap:
 *
 *   1. <sys/stat.h> was reached only through the #else arm of an `#ifdef
 *      _WIN32` that exists to pick _mkdir over mkdir. MinGW *has* <sys/stat.h>,
 *      struct stat, stat() and S_ISDIR -- they were simply never included on
 *      the Windows path. Same for <dirent.h>. So this header includes both
 *      unconditionally and the callers stop caring.
 *   2. mkdir() takes one argument on Windows and two on POSIX. tool_mkdir()
 *      below is the arity-neutral spelling.
 *   3. fmemopen() and getline() genuinely do not exist in MinGW. Those two are
 *      implemented here.
 *
 * Include this instead of <sys/stat.h> / <dirent.h> in a tool that needs them.
 */
#ifndef RSCACHE_TOOLS_POSIX_COMPAT_H
#define RSCACHE_TOOLS_POSIX_COMPAT_H

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)

#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define tool_mkdir(p) _mkdir(p)

/*
 * getline -- read one line, growing *lineptr as needed. Returns the byte count
 * including the trailing newline, or -1 at EOF with nothing read. Ownership
 * matches POSIX: the caller frees *lineptr.
 */
static __inline ssize_t
tool_compat_getline(char** lineptr, size_t* n, FILE* stream)
{
    size_t pos = 0;
    int c;

    if( !lineptr || !n || !stream )
        return -1;

    if( !*lineptr || *n == 0 )
    {
        char* buf = (char*)realloc(*lineptr, 128);
        if( !buf )
            return -1;
        *lineptr = buf;
        *n = 128;
    }

    for( ;; )
    {
        c = fgetc(stream);
        if( c == EOF )
        {
            if( pos == 0 )
                return -1;
            break;
        }

        /* +1 so the NUL below always has room. */
        if( pos + 1 >= *n )
        {
            size_t cap = *n * 2;
            char* buf = (char*)realloc(*lineptr, cap);
            if( !buf )
                return -1;
            *lineptr = buf;
            *n = cap;
        }

        (*lineptr)[pos++] = (char)c;
        if( c == '\n' )
            break;
    }

    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

/*
 * fmemopen -- only the read-only form these tools use ("rb" over a buffer the
 * caller already holds). Windows has no memory stream, so spill to a tmpfile
 * and hand back a rewound handle. Callers read sequentially and fclose, which
 * this satisfies exactly; it is NOT a general fmemopen (no write mode, and the
 * buffer is copied rather than aliased).
 */
static __inline FILE*
tool_compat_fmemopen(void* buf, size_t size, const char* mode)
{
    FILE* f;

    (void)mode;
    f = tmpfile();
    if( !f )
        return NULL;

    if( size && fwrite(buf, 1, size, f) != size )
    {
        fclose(f);
        return NULL;
    }

    rewind(f);
    return f;
}

#define getline(lineptr, n, stream) tool_compat_getline((lineptr), (n), (stream))
#define fmemopen(buf, size, mode) tool_compat_fmemopen((buf), (size), (mode))

#else /* !_WIN32 */

#define tool_mkdir(p) mkdir((p), 0755)

#endif /* _WIN32 */

#endif /* RSCACHE_TOOLS_POSIX_COMPAT_H */
