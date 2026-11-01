#ifndef SRC_EDITOR_EDITOR_OPEN_CMD_H
#define SRC_EDITOR_EDITOR_OPEN_CMD_H

/**
 * "Open in editor": the command line that hands a file and a line to the
 * user's own editor (the Triggers panel, the catalog). Header-only and pure,
 * so editor_host_local.c (which spawns it, Editor_HostLocalOpenInEditor) and
 * the tests share it without a link dependency.
 *
 * The template is TORIRS_EDITOR_OPEN_CMD (the manifest has no such key yet:
 * src/bootmanifest is not the editor's), else Editor_OpenCommandDefault.
 * `{path}` becomes the path quoted for /bin/sh -- '...', a ' inside written
 * '\'' -- and `{line}` the 1-based line number; so `code --goto {path}:{line}`
 * gives `code --goto '/a b/x.rs2':12`, which the shell reads as one word.
 * Any other text is copied as written (the user's own shell syntax).
 */

#include <stdio.h>
#include <string.h>

/** The default template: VS Code when `code` is on PATH, else the
 *  platform's opener. */
static inline const char*
Editor_OpenCommandDefault(
    int have_code,
    int apple)
{
    if( have_code )
        return "code --goto {path}:{line}";
    return apple ? "open {path}" : "xdg-open {path}";
}

/** The command line. Returns its length, 0 when it does not fit `out` or
 *  the template is empty. */
static inline size_t
Editor_OpenCommandLine(
    const char* tmpl,
    const char* path,
    int line,
    char* out,
    size_t out_size)
{
    size_t n = 0;

    if( !tmpl || !path || !out || out_size == 0 || !tmpl[0] )
        return 0;
    if( line < 1 )
        line = 1;
    for( const char* t = tmpl; *t; )
    {
        if( strncmp(t, "{path}", 6) == 0 )
        {
            if( n + 1 >= out_size )
                return out[0] = '\0', (size_t)0;
            out[n++] = '\'';
            for( const char* p = path; *p; p++ )
            {
                if( *p == '\'' )
                {
                    if( n + 4 >= out_size )
                        return out[0] = '\0', (size_t)0;
                    memcpy(out + n, "'\\''", 4);
                    n += 4;
                }
                else
                {
                    if( n + 1 >= out_size )
                        return out[0] = '\0', (size_t)0;
                    out[n++] = *p;
                }
            }
            if( n + 1 >= out_size )
                return out[0] = '\0', (size_t)0;
            out[n++] = '\'';
            t += 6;
        }
        else if( strncmp(t, "{line}", 6) == 0 )
        {
            int const written = snprintf(out + n, out_size - n, "%d", line);
            if( written < 0 || (size_t)written >= out_size - n )
                return out[0] = '\0', (size_t)0;
            n += (size_t)written;
            t += 6;
        }
        else
        {
            if( n + 1 >= out_size )
                return out[0] = '\0', (size_t)0;
            out[n++] = *t++;
        }
    }
    out[n] = '\0';
    return n;
}

#endif
