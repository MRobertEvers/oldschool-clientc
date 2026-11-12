#ifndef SRC_EDITOR_EDITOR_DIFF_CMD_H
#define SRC_EDITOR_EDITOR_DIFF_CMD_H

/**
 * "Diff against base": the command line that hands two files to the user's
 * own diff tool (the Lanes panel's Changes list: a lane's file against a
 * copy of its base's, torirsmaped/torirs_maped_lanes.h
 * ToriRSMapEdLanes_DiffBase). Scripts and maps are read and written in the
 * user's own editor (owner, 2026-11-09), so the map editor shows no diff of
 * its own: it starts theirs.
 *
 * The template is TORIRS_EDITOR_DIFF_CMD when set (`{base}` the base's copy,
 * `{path}` the lane's file, each quoted exactly as Open in editor's `{path}`
 * is, editor_open_cmd.h; a test's `echo`), else `code --diff {base} {path}`
 * when VS Code's launcher is on PATH. There is no other default: a file
 * manager or `start` opens one file, and showing one side of a diff as if it
 * were the diff is worse than saying what is missing.
 *
 * The pure half (the line) is here for the tests; Editor_DiffInEditor
 * (editor_diff_cmd.c) spawns it.
 */

#include "editor_host.h"
#include "editor_open_cmd.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** The default template: VS Code's two-file diff, NULL without VS Code. */
static inline const char*
Editor_DiffCommandDefault(int have_code)
{
    return have_code ? "code --diff {base} {path}" : NULL;
}

/** The command line. Returns its length, 0 when it does not fit `out` or
 *  the template is empty. */
static inline size_t
Editor_DiffCommandLine(
    const char* tmpl,
    const char* base,
    const char* path,
    char* out,
    size_t out_size)
{
    char quoted_base[1100];
    char quoted_path[1100];
    size_t n = 0;

    assert(tmpl);
    assert(base);
    assert(path);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( !tmpl[0] )
        return 0;
    /* Open in editor's quoting, one spelling of it: '...' for /bin/sh, "..."
     * for cmd.exe. */
    if( Editor_OpenCommandLine("{path}", base, 1, quoted_base, sizeof(quoted_base)) == 0
        || Editor_OpenCommandLine("{path}", path, 1, quoted_path, sizeof(quoted_path)) == 0 )
        return 0;
    for( const char* t = tmpl; *t; )
    {
        const char* piece = NULL;

        if( strncmp(t, "{base}", 6) == 0 )
            piece = quoted_base;
        else if( strncmp(t, "{path}", 6) == 0 )
            piece = quoted_path;
        if( piece )
        {
            size_t const length = strlen(piece);
            if( n + length >= out_size )
                return out[0] = '\0', (size_t)0;
            memcpy(out + n, piece, length);
            n += length;
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

/**
 * Start the user's diff tool on `base` and `path` (absolute, on this
 * machine), never waited on; the command line goes to `out_cmdline` (may be
 * NULL) for the Console. OK when it was started; ABSENT when there is no
 * diff tool to start (no TORIRS_EDITOR_DIFF_CMD and no VS Code on PATH);
 * UNSUPPORTED on the web; REFUSED for a command that does not fit; IO_ERROR
 * when it could not be started.
 */
enum EditorHost_Status
Editor_DiffInEditor(
    const char* base,
    const char* path,
    char* out_cmdline,
    size_t out_size);

#endif
