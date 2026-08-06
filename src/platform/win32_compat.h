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

<<<<<<< HEAD
/* XP's system msvcrt.dll exports _putenv, but not the Vista-era _putenv_s.
 * Keep the successful assignment allocation alive: that is safe for both
 * CRTs which copy the string and putenv implementations which retain it. */
static __inline int
torirs_win32_putenv(const char* name, const char* value)
{
    size_t name_len;
    size_t value_len;
    char* assignment;
    int result;

    if( !name || !name[0] || !value || strchr(name, '=') )
        return -1;

    name_len = strlen(name);
    value_len = strlen(value);
    if( name_len > (size_t)-1 - value_len - 2u )
        return -1;

    assignment = (char*)malloc(name_len + value_len + 2u);
    if( !assignment )
        return -1;

    memcpy(assignment, name, name_len);
    assignment[name_len] = '=';
    memcpy(assignment + name_len + 1u, value, value_len + 1u);

    result = _putenv(assignment);
    if( result != 0 )
        free(assignment);
    return result == 0 ? 0 : -1;
}

/* POSIX setenv over XP's CRT. overwrite is honoured: when 0 and the name
 * already exists, leave it. Returns 0 on success, -1 on error. */
=======
/* POSIX setenv over the CRT's _putenv_s. overwrite is honoured: when 0 and the
 * name already exists, leave it. Returns 0 on success, -1 on error. */
>>>>>>> 5cc78a2898eaf81842f0042a51fce58c1e512f0c
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
<<<<<<< HEAD
    return torirs_win32_putenv(name, value);
=======
    return _putenv_s(name, value) == 0 ? 0 : -1;
>>>>>>> 5cc78a2898eaf81842f0042a51fce58c1e512f0c
}

static __inline int
unsetenv(const char* name)
{
<<<<<<< HEAD
    return torirs_win32_putenv(name, "");
=======
    if( !name )
        return -1;
    return _putenv_s(name, "") == 0 ? 0 : -1;
>>>>>>> 5cc78a2898eaf81842f0042a51fce58c1e512f0c
}
#endif /* _WIN32 */

#endif /* SRC_PLATFORM_WIN32_COMPAT_H */
