/*
 * win32_compat.h -- tiny POSIX shims for the native Windows (win32/GDI) build.
 *
 * Force-included by the win32 build (-include). Provides the handful of POSIX
 * conveniences the embedded rev-230 server reaches for that MinGW's headers
 * don't. Guarded to _WIN32 so it is inert on the SDL/native and web builds.
 *
 * NOTE: this covers only what a header can. dirent's d_type (mock230_content.c)
 * and BSD sockets (mock230_ws.c) need in-file #ifdef _WIN32 branches -- a header
 * cannot add a struct member or swap a socket API.
 */
#ifndef SRC_PLATFORM_WIN32_COMPAT_H
#define SRC_PLATFORM_WIN32_COMPAT_H

#if defined(_WIN32)
#include <stdlib.h>
#include <string.h>

/* POSIX setenv over the CRT's _putenv_s. overwrite is honoured: when 0 and the
 * name already exists, leave it. Returns 0 on success, -1 on error. */
static __inline int
setenv(const char* name, const char* value, int overwrite)
{
    if( !name || !value )
        return -1;
    if( !overwrite )
    {
        const char* existing = getenv(name);
        if( existing )
            return 0;
    }
    return _putenv_s(name, value) == 0 ? 0 : -1;
}

static __inline int
unsetenv(const char* name)
{
    if( !name )
        return -1;
    return _putenv_s(name, "") == 0 ? 0 : -1;
}
#endif /* _WIN32 */

#endif /* SRC_PLATFORM_WIN32_COMPAT_H */
