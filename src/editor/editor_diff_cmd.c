/*
 * "Diff against base" (editor_diff_cmd.h): starting the user's own diff tool
 * on two files. Spawned the way Open in editor is (editor_host_local.c,
 * Editor_HostLocalOpenInEditor): through the user's shell, backgrounded, never
 * waited on -- the tool is never this process's.
 */
#include "editor_diff_cmd.h"

#include <assert.h>
#include <stdlib.h>

#if defined(__EMSCRIPTEN__)

/* A browser tab has no editor to hand two paths to. */
enum EditorHost_Status
Editor_DiffInEditor(
    const char* base,
    const char* path,
    char* out_cmdline,
    size_t out_size)
{
    assert(base);
    assert(path);
    if( out_cmdline && out_size > 0 )
        out_cmdline[0] = '\0';
    return EDITOR_HOST_UNSUPPORTED;
}

#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum EditorHost_Status
Editor_DiffInEditor(
    const char* base,
    const char* path,
    char* out_cmdline,
    size_t out_size)
{
    const char* tmpl = getenv("TORIRS_EDITOR_DIFF_CMD");
    char command[2600];
    char cmd_line[2700];
    char found[MAX_PATH];
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;

    assert(base);
    assert(path);
    if( out_cmdline && out_size > 0 )
        out_cmdline[0] = '\0';
    /* VS Code's launcher on PATH is code.cmd. */
    if( !tmpl || !tmpl[0] )
        tmpl = Editor_DiffCommandDefault(SearchPathA(NULL, "code.cmd", NULL, sizeof(found), found, NULL) != 0);
    if( !tmpl )
        return EDITOR_HOST_ABSENT;
    if( Editor_DiffCommandLine(tmpl, base, path, command, sizeof(command)) == 0 )
        return EDITOR_HOST_REFUSED;
    if( out_cmdline && out_size > 0 )
        snprintf(out_cmdline, out_size, "%s", command);
    /* Through cmd.exe, as the user's own command line (a .cmd, a redirect),
     * with no console and not waited on. */
    snprintf(cmd_line, sizeof(cmd_line), "cmd.exe /d /c %s", command);
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    memset(&process, 0, sizeof(process));
    if( !CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &startup,
            &process) )
        return EDITOR_HOST_IO_ERROR;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return EDITOR_HOST_OK;
}

#else

#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

/** `name` is an executable in a PATH directory. */
static int
diff_on_path(const char* name)
{
    const char* path = getenv("PATH");
    char candidate[1200];

    while( path && *path )
    {
        const char* colon = strchr(path, ':');
        size_t const length = colon ? (size_t)(colon - path) : strlen(path);
        if( length > 0 && length < 1000 )
        {
            snprintf(candidate, sizeof(candidate), "%.*s/%s", (int)length, path, name);
            if( access(candidate, X_OK) == 0 )
                return 1;
        }
        path = colon ? colon + 1 : NULL;
    }
    return 0;
}

enum EditorHost_Status
Editor_DiffInEditor(
    const char* base,
    const char* path,
    char* out_cmdline,
    size_t out_size)
{
    const char* tmpl = getenv("TORIRS_EDITOR_DIFF_CMD");
    char command[2600];
    char shell_line[2700];
    char* argv[4];
    pid_t pid;
    int status = 0;

    assert(base);
    assert(path);
    if( out_cmdline && out_size > 0 )
        out_cmdline[0] = '\0';
    if( !tmpl || !tmpl[0] )
        tmpl = Editor_DiffCommandDefault(diff_on_path("code"));
    if( !tmpl )
        return EDITOR_HOST_ABSENT;
    if( Editor_DiffCommandLine(tmpl, base, path, command, sizeof(command)) == 0 )
        return EDITOR_HOST_REFUSED;
    if( out_cmdline && out_size > 0 )
        snprintf(out_cmdline, out_size, "%s", command);
    /* Backgrounded inside the shell, which exits at once: waiting on it is
     * waiting on nothing, and the tool is never this process's child. */
    snprintf(shell_line, sizeof(shell_line), "( %s ) >/dev/null 2>&1 &", command);
    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = shell_line;
    argv[3] = NULL;
    if( posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ) != 0 )
        return EDITOR_HOST_IO_ERROR;
    while( waitpid(pid, &status, 0) < 0 )
    {
        if( errno != EINTR )
            break;
    }
    return EDITOR_HOST_OK;
}

#endif
