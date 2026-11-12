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
 *
 * On Windows the line is cmd.exe's: `{path}` is "..." (a Windows path holds
 * no '"'), so `code --goto {path}:{line}` gives `code --goto "C:\a b\x.rs2":12`,
 * which a program's argument parsing reads as one word, and the default
 * without VS Code is `start "" {path}`, the shell's own opener
 * (Editor_OpenCommandDefaultWindows).
 *
 * `{folder}` is a folder quoted the same way: the one the caller names (a new
 * quest's folder, opened as VS Code's workspace with its script in view:
 * `code {folder} --goto {path}:{line}`), else the folder `{path}` is in.
 *
 * "Show in Explorer / Finder" is the second command line here, at the foot
 * of the file (Editor_RevealCommandDefault).
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

/** Windows' default template: VS Code when `code` is on PATH, else the
 *  file's associated program. */
static inline const char*
Editor_OpenCommandDefaultWindows(int have_code)
{
    return have_code ? "code --goto {path}:{line}" : "start \"\" {path}";
}

/** What Open in editor starts, in words for the status bar: the user's own
 *  command when TORIRS_EDITOR_OPEN_CMD is set, VS Code when `code` is on
 *  PATH, else the desktop's opener -- which, for a script, is whatever
 *  program the file type happens to be given to. `os`: 'w' Windows, 'm'
 *  macOS, anything else the rest. */
static inline const char*
Editor_OpenCommandWho(
    int from_env,
    int have_code,
    char os)
{
    if( from_env )
        return "your TORIRS_EDITOR_OPEN_CMD command";
    if( have_code )
        return "VS Code";
    return os == 'w'   ? "the program Windows opens that file type with"
           : os == 'm' ? "the app macOS opens that file type with"
                       : "xdg-open";
}

/** The default template for a FOLDER with one file of it in view: VS Code
 *  opens the folder as its workspace and the file at its line; without it
 *  the desktop opens the folder (its file manager). `os` as above. */
static inline const char*
Editor_OpenFolderCommandDefault(
    int have_code,
    char os)
{
    if( have_code )
        return "code {folder} --goto {path}:{line}";
    return os == 'w' ? "start \"\" {folder}" : os == 'm' ? "open {folder}" : "xdg-open {folder}";
}

/** `text` appended at out[n] as one shell word: "..." for cmd.exe, '...'
 *  (a ' inside written '\'') for /bin/sh. Returns the new length, 0 when it
 *  does not fit. */
static inline size_t
Editor_OpenCommandQuote(
    const char* text,
    size_t length,
    char* out,
    size_t n,
    size_t out_size)
{
#ifdef _WIN32
    int const written = snprintf(out + n, out_size - n, "\"%.*s\"", (int)length, text);
    if( written < 0 || (size_t)written >= out_size - n )
        return 0;
    return n + (size_t)written;
#else
    if( n + 1 >= out_size )
        return 0;
    out[n++] = '\'';
    for( size_t i = 0; i < length; i++ )
    {
        if( text[i] == '\'' )
        {
            if( n + 4 >= out_size )
                return 0;
            memcpy(out + n, "'\\''", 4);
            n += 4;
        }
        else
        {
            if( n + 1 >= out_size )
                return 0;
            out[n++] = text[i];
        }
    }
    if( n + 1 >= out_size )
        return 0;
    out[n++] = '\'';
    return n;
#endif
}

/** The command line, with `{folder}` the folder named (NULL: the folder
 *  `path` is in). Returns its length, 0 when it does not fit `out` or the
 *  template is empty. */
static inline size_t
Editor_OpenCommandLineIn(
    const char* tmpl,
    const char* folder,
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
            n = Editor_OpenCommandQuote(path, strlen(path), out, n, out_size);
            if( n == 0 )
                return out[0] = '\0', (size_t)0;
            t += 6;
        }
        else if( strncmp(t, "{folder}", 8) == 0 )
        {
            const char* text = folder;
            size_t length = folder ? strlen(folder) : 0;
            if( !folder )
            {
                /* The folder the file is in: up to its last separator. */
                const char* cut = strrchr(path, '/');
                const char* back = strrchr(path, '\\');
                if( back && (!cut || back > cut) )
                    cut = back;
                text = cut ? path : ".";
                length = cut ? (size_t)(cut - path) : 1;
            }
            n = Editor_OpenCommandQuote(text, length, out, n, out_size);
            if( n == 0 )
                return out[0] = '\0', (size_t)0;
            t += 8;
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

/** Editor_OpenCommandLineIn with `{folder}` the folder `path` is in. */
static inline size_t
Editor_OpenCommandLine(
    const char* tmpl,
    const char* path,
    int line,
    char* out,
    size_t out_size)
{
    return Editor_OpenCommandLineIn(tmpl, NULL, path, line, out, out_size);
}

/* ---- "Show in Explorer" / "Show in Finder" ---------------------------------
 *
 * The command line that shows a file in the desktop's file manager
 * (Editor_HostLocalRevealInFileManager spawns it). Scripts are written in
 * the user's own editor (owner, 2026-11-09), so the map editor's half is
 * getting them from a record to its files: this, Open in editor, and the
 * names and snippets it copies.
 *
 * The template is TORIRS_EDITOR_REVEAL_CMD when set (another file manager; a
 * test's `echo`), else the default below. `{path}` is what is shown -- the
 * file when it is on disk, else its nearest folder that is -- quoted by
 * Editor_OpenCommandLine exactly as Open in editor's `{path}` is.
 */

/** macOS and other POSIX. Finder selects a file in its folder (`open -R`)
 *  and opens a folder; xdg-open selects nothing, so the caller hands it the
 *  folder to open. */
static inline const char*
Editor_RevealCommandDefault(
    int is_folder,
    int apple)
{
    if( !apple )
        return "xdg-open {path}";
    return is_folder ? "open {path}" : "open -R {path}";
}

/** Windows: Explorer with the file selected, or with the folder opened.
 *  `/select,` takes the path straight after its comma, backslashes only. */
static inline const char*
Editor_RevealCommandDefaultWindows(int is_folder)
{
    return is_folder ? "explorer.exe {path}" : "explorer.exe /select,{path}";
}

#endif
