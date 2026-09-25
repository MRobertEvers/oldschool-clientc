#ifndef TOOLS_RUNESCRIPT_LSP_PLATFORM_H
#define TOOLS_RUNESCRIPT_LSP_PLATFORM_H

/*
 * The three things a language server cannot do portably by itself.
 *
 *   - walking a directory (dirent.h on POSIX, FindFirstFile on Windows);
 *   - keeping stdio in binary mode, which Windows does not do by default and
 *     which the LSP framing requires: the header terminator really is CRLF,
 *     and a text-mode stdout turns every LF into CRLF on the way out, so the
 *     byte count in Content-Length stops matching the bytes that follow it;
 *   - telling a path separator from a character in a name.
 */

#include <stddef.h>

/** Called once per entry. `is_directory` is 1 for a directory. */
typedef void (*Plat_DirEntryFn)(void* context, const char* path, int is_directory);

/** Enumerate `directory`, calling `callback` for each entry. */
void
Plat_ListDirectory(const char* directory, void* context, Plat_DirEntryFn callback);

/** Put stdin and stdout into binary mode. A no-op where they already are. */
void
Plat_UseBinaryStdio(void);

/** The path separator this platform writes. '/' everywhere but Windows. */
char
Plat_PathSeparator(void);

/** True when `c` separates path components here. */
int
Plat_IsPathSeparator(char c);

#endif
