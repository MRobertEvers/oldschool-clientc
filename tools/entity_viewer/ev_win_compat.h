/* ev_win_compat.h - force-included (-include) shim so the entity_viewer's
 * native tools build with MinGW. Nothing in the repo is modified; this rides
 * in on the CC variable.
 *
 * Three gaps, all of them POSIX-isms the tools use casually:
 *   M_PI      - not in Windows <math.h> without _USE_MATH_DEFINES
 *   realpath  - POSIX; _fullpath is the Windows spelling
 *   mkdir     - takes a mode on POSIX, one argument on Windows
 *
 * The system headers are pulled in FIRST, before the mkdir macro is defined.
 * Defining a function-like mkdir() macro ahead of <sys/stat.h> would rewrite
 * the header's own declaration of it and fail to compile.
 */
#ifndef EV_WIN_COMPAT_H
#define EV_WIN_COMPAT_H

#ifdef _WIN32

#include <direct.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* _fullpath(out, in, len) is argument-swapped relative to realpath(in, out).
 * Passing a NULL destination makes both of them allocate, so callers that
 * rely on that keep working. */
#define realpath(path, resolved) _fullpath((resolved), (path), _MAX_PATH)

#define mkdir(path, mode) _mkdir(path)

#endif /* _WIN32 */

#endif /* EV_WIN_COMPAT_H */
