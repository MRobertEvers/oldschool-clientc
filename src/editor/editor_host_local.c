/**
 * The desktop EditorHost: the content tree, reached directly.
 *
 * No daemon and no socket — on this binding the seam collapses to a function
 * call, which is the whole point of putting it behind a vtable rather than
 * behind a process boundary that the desktop case would then have to pay for.
 */

#include "editor_host.h"
#include "editor_cfg.h"
#include "editor_doc.h"
#include "editor_if.h"
#include "editor_open_cmd.h"
#include "editor_spawn.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "log/torirs_log.h"

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <signal.h>
#endif
#if defined(__APPLE__)
#include <crt_externs.h>
#include <libproc.h>
#endif

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define editor_mkdir(path) _mkdir(path)
#else
#define editor_mkdir(path) mkdir(path, 0755)
#endif

/* Replace `path` with `temp` in one step. POSIX rename does; Win32's rename
 * refuses an existing target, so MoveFileEx with REPLACE_EXISTING (as
 * platform_x_io.c and torirs_server_savefile.c do it). 0 on success. */
static int
editor_replace_file(
    const char* temp,
    const char* path)
{
#ifdef _WIN32
    if( MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) )
        return 0;
    errno = EACCES;
    return -1;
#else
    return rename(temp, path);
#endif
}

/* The scripted spawns' walk (every .spawn under server/scripts that is not
 * the generated areas/world one or the edited one) runs on a worker thread
 * where the platform has one, as the lane scan and the catalog's warm build
 * do; the web and win32 lanes walk inline. The walk allocates through
 * ToriMem_Editor(), a HEAP group, which the catalog's thread does too. */
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#include <pthread.h>
#define LOCAL_SPAWNS_THREAD 1
#else
#define LOCAL_SPAWNS_THREAD 0
#endif

/** A snapshot older than this starts a new walk on the next ask (answered
 *  from the snapshot as it stands; the walk lands for the ask after). */
#define LOCAL_SPAWNS_REFRESH_S 30

/**
 * The scripted spawns, every row of every quest's and skill's .spawn file,
 * as one snapshot the "spawns:m<x>_<z>" documents are cut from. The walk is
 * a stat of every entry under server/scripts (58k on an OSRS tree, 2-3 s on
 * a lane's symlink mirror), and until 2026-11-08 every square open ran it on
 * the UI thread; now the first open joins the walk the host's open started,
 * and every later one answers from the snapshot.
 */
struct scripted_spawns
{
    /** The snapshot answered from; `built` once a walk has landed. */
    struct Editor_SpawnFile rows;
    int built;
    time_t walked_at;
    /** A walk in flight fills `fresh`; the landing swaps it into `rows`. */
    struct Editor_SpawnFile fresh;
    int walking;
#if LOCAL_SPAWNS_THREAD
    pthread_t thread;
    int thread_live;
#endif
    int done;
};

struct local_host
{
    char content_dir[1024];
    /** Empty when baking is disabled for this session. */
    char repo_root[1024];
    /** Path of the lockfile while this session holds it; empty when it does not. */
    char lock_path[1100];
    /** Who holds the tree when the last acquire found it taken
     *  ("<content_dir> is held by pid N (...)"); empty otherwise. */
    char lock_holder[1024];
    struct scripted_spawns spawns;
};

static void
scripted_spawns_land(
    struct local_host* host,
    int wait);

static void
square_path(
    const struct local_host* host,
    char* out,
    size_t out_size,
    int map_x,
    int map_z,
    const char* extension)
{
    assert(host);
    assert(out);
    assert(extension);

    snprintf(out, out_size, "%s/maps/m%d_%d.%s", host->content_dir, map_x, map_z, extension);
}

/*
 * Line endings. Every file this host reads and writes is a text document, and
 * a content tree may be CRLF: Git for Windows checks text out that way by
 * default (core.autocrlf), and a tree copied from such a checkout keeps it.
 * The parsers and the documents work in "\n" only, so a CRLF file is read as
 * LF, and a save writes "\r\n" again when the file it replaces is CRLF. An
 * untouched document then writes back byte for byte, and a one-tile edit is a
 * one-line change on every checkout, not a rewrite of every line.
 */

/** Every "\r\n" in `blob` becomes "\n", in place (a lone '\r' is data). */
static void
text_crlf_to_lf(struct EditorHost_Blob* blob)
{
    size_t out = 0;

    assert(blob);
    for( size_t at = 0; at < blob->size; at++ )
    {
        if( blob->data[at] == '\r' && at + 1 < blob->size && blob->data[at + 1] == '\n' )
            continue;
        blob->data[out++] = blob->data[at];
    }
    blob->data[out] = '\0';
    blob->size = out;
}

static enum EditorHost_Status
read_whole_file(
    const char* path,
    struct EditorHost_Blob* out_blob)
{
    FILE* file;
    long size;
    char* data;

    assert(path);
    assert(out_blob);

    out_blob->data = NULL;
    out_blob->size = 0;

    file = fopen(path, "rb");
    if( !file )
        return EDITOR_HOST_ABSENT;

    if( fseek(file, 0, SEEK_END) != 0 )
    {
        fclose(file);
        return EDITOR_HOST_IO_ERROR;
    }
    size = ftell(file);
    if( size < 0 )
    {
        fclose(file);
        return EDITOR_HOST_IO_ERROR;
    }
    rewind(file);

    data = ToriMem_Alloc(ToriMem_Editor(), (size_t)size + 1);

    if( size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        ToriMem_Free(ToriMem_Editor(), data);
        fclose(file);
        return EDITOR_HOST_IO_ERROR;
    }
    fclose(file);

    data[size] = '\0';
    out_blob->data = data;
    out_blob->size = (size_t)size;
    text_crlf_to_lf(out_blob);
    return EDITOR_HOST_OK;
}

/** 1 when `path` exists and its first line ends "\r\n": the file is a CRLF
 *  file and its rewrite keeps that. */
static int
file_is_crlf(const char* path)
{
    FILE* file = fopen(path, "rb");
    char head[4096];
    size_t got;
    char const* newline;

    if( !file )
        return 0;
    got = fread(head, 1, sizeof(head), file);
    fclose(file);
    newline = memchr(head, '\n', got);
    return newline && newline > head && newline[-1] == '\r';
}

/**
 * Write via a temporary beside the target, then rename.
 *
 * The rename is what makes this atomic: a reader sees either the old file or
 * the new one. Writing in place would leave a truncated square on disk if the
 * process died mid-write, and a truncated `.jm2` is not a recoverable state —
 * the tiles it lost are simply gone.
 *
 * The temporary sits in the same directory so the rename stays within one
 * filesystem, where it is atomic.
 */
static enum EditorHost_Status
write_file_atomic(
    const char* path,
    const char* text,
    size_t length)
{
    char temp_path[1200];
    FILE* file;
    int written_ok = 1;

    assert(path);
    assert(text || length == 0);

    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);

    /* Read before the temporary exists: the target's own line ending. */
    int const crlf = file_is_crlf(path);
    file = fopen(temp_path, "wb");
    if( !file )
    {
        TORIRS_ERR("editor: cannot open %s: %s\n", temp_path, strerror(errno));
        return EDITOR_HOST_IO_ERROR;
    }
    if( crlf )
    {
        /* Each "\n" goes out as "\r\n" (one already preceded by '\r' as is). */
        size_t run = 0;
        for( size_t at = 0; at < length && written_ok; at++ )
        {
            if( text[at] != '\n' || (at > 0 && text[at - 1] == '\r') )
                continue;
            written_ok = fwrite(text + run, 1, at - run, file) == at - run && fwrite("\r\n", 1, 2, file) == 2;
            run = at + 1;
        }
        if( written_ok && run < length )
            written_ok = fwrite(text + run, 1, length - run, file) == length - run;
    }
    else if( length > 0 )
        written_ok = fwrite(text, 1, length, file) == length;
    if( !written_ok )
    {
        TORIRS_LOG("editor: short write to %s\n", temp_path);
        fclose(file);
        remove(temp_path);
        return EDITOR_HOST_IO_ERROR;
    }
    if( fclose(file) != 0 )
    {
        TORIRS_ERR("editor: cannot close %s: %s\n", temp_path, strerror(errno));
        remove(temp_path);
        return EDITOR_HOST_IO_ERROR;
    }
    if( editor_replace_file(temp_path, path) != 0 )
    {
        TORIRS_ERR("editor: cannot rename onto %s: %s\n", path, strerror(errno));
        remove(temp_path);
        return EDITOR_HOST_IO_ERROR;
    }
    return EDITOR_HOST_OK;
}

static enum EditorHost_Status
local_square_list(
    void* user_data,
    int* out_coords,
    int max,
    int* out_count)
{
    struct local_host* host = user_data;
    char maps_dir[1100];
    DIR* dir;
    struct dirent* entry;
    int found = 0;

    assert(host);
    assert(out_count);
    assert(out_coords || max == 0);

    snprintf(maps_dir, sizeof(maps_dir), "%s/maps", host->content_dir);
    dir = opendir(maps_dir);
    if( !dir )
        return EDITOR_HOST_IO_ERROR;

    while( (entry = readdir(dir)) != NULL )
    {
        int map_x;
        int map_z;
        size_t name_length = strlen(entry->d_name);

        if( name_length < 5 || strcmp(entry->d_name + name_length - 4, ".jm2") != 0 )
            continue;
        if( sscanf(entry->d_name, "m%d_%d.jm2", &map_x, &map_z) != 2 )
            continue;

        if( found < max )
        {
            out_coords[found * 2] = map_x;
            out_coords[found * 2 + 1] = map_z;
        }
        found++;
    }
    closedir(dir);

    *out_count = found;
    return EDITOR_HOST_OK;
}

static enum EditorHost_Status
local_square_load(
    void* user_data,
    int map_x,
    int map_z,
    struct EditorHost_Blob* out_jm2,
    struct EditorHost_Blob* out_jl2)
{
    struct local_host* host = user_data;
    char path[1200];
    enum EditorHost_Status status;

    assert(host);
    assert(out_jm2);
    assert(out_jl2);

    square_path(host, path, sizeof(path), map_x, map_z, "jm2");
    status = read_whole_file(path, out_jm2);
    if( status != EDITOR_HOST_OK )
        return status;

    /* A square with no scenery ships no `.jl2`, so an absent one is data, not
     * a failure — it comes back as an empty blob and parses to zero locs. */
    square_path(host, path, sizeof(path), map_x, map_z, "jl2");
    read_whole_file(path, out_jl2);

    return EDITOR_HOST_OK;
}

static enum EditorHost_Status
local_square_save(
    void* user_data,
    int map_x,
    int map_z,
    const char* jm2_text,
    size_t jm2_length,
    const char* jl2_text,
    size_t jl2_length)
{
    struct local_host* host = user_data;
    char path[1200];
    enum EditorHost_Status status;

    assert(host);

    if( host->lock_path[0] == '\0' )
    {
        TORIRS_ERR("editor: refusing to save without the session lock\n");
        return EDITOR_HOST_LOCKED;
    }

    if( jm2_text )
    {
        square_path(host, path, sizeof(path), map_x, map_z, "jm2");
        status = write_file_atomic(path, jm2_text, jm2_length);
        if( status != EDITOR_HOST_OK )
            return status;
    }
    if( jl2_text )
    {
        square_path(host, path, sizeof(path), map_x, map_z, "jl2");
        status = write_file_atomic(path, jl2_text, jl2_length);
        if( status != EDITOR_HOST_OK )
            return status;
    }
    return EDITOR_HOST_OK;
}

static enum EditorHost_Status
local_bake(
    void* user_data,
    EditorHost_ProgressFn on_progress,
    void* progress_user_data)
{
    struct local_host* host = user_data;
    char command[2200];
    FILE* pipe;
    char line[512];
    int exit_status;

    assert(host);

    if( host->repo_root[0] == '\0' )
    {
        TORIRS_LOG("editor: baking is disabled for this session\n");
        return EDITOR_HOST_BAKE_FAILED;
    }

    /* The project's own target, not a hand-assembled cachepack line: it knows
     * about the base cache, the asset flags and the CS2 name tables, and a
     * second copy of that here would drift from the one the build uses. */
    snprintf(command, sizeof(command), "cd '%s' && make -C src torirsserver-cache 2>&1", host->repo_root);

    pipe = popen(command, "r");
    if( !pipe )
        return EDITOR_HOST_BAKE_FAILED;

    while( fgets(line, sizeof(line), pipe) )
    {
        size_t length = strlen(line);
        if( length > 0 && line[length - 1] == '\n' )
            line[length - 1] = '\0';
        if( on_progress )
            on_progress(progress_user_data, line);
    }

    exit_status = pclose(pipe);
    return exit_status == 0 ? EDITOR_HOST_OK : EDITOR_HOST_BAKE_FAILED;
}

/**
 * Single-writer lock over the content tree.
 *
 * Exclusive-create on a lockfile: whoever makes it owns the tree. This is what
 * keeps two editor sessions — or an editor and a content-watching rebake —
 * from interleaving writes into the same squares. A session that cannot take
 * it still runs; it just cannot save.
 *
 * The file says who holds it, one `key value` line each:
 *
 *     pid 43893
 *     since 1762100533
 *     cmd /Users/me/3draster/src/torirs --manifest manifests/...
 *
 * so a session that finds it taken can name the holder, and a holder that
 * died without releasing it (a crash, a kill -9) does not lock the tree
 * forever: a lock whose pid no longer runs is moved aside and taken
 * (2026-11-02: a dead editor's lock left the owner's window read-only, and
 * the RUN message named nobody).
 */

/** This process's command line, space-joined, "" when it cannot be read. */
static void
lock_own_command(
    char* out,
    size_t out_size)
{
    size_t at = 0;

    out[0] = '\0';
#if defined(__APPLE__)
    {
        int const argc = *_NSGetArgc();
        char** const argv = *_NSGetArgv();
        for( int i = 0; i < argc && argv && argv[i]; i++ )
        {
            int const wrote = snprintf(out + at, out_size - at, "%s%s", i ? " " : "", argv[i]);
            if( wrote < 0 || (size_t)wrote >= out_size - at )
                break;
            at += (size_t)wrote;
        }
    }
#elif defined(__linux__)
    {
        FILE* file = fopen("/proc/self/cmdline", "rb");
        if( file )
        {
            size_t got = fread(out, 1, out_size - 1, file);
            fclose(file);
            for( size_t i = 0; i < got; i++ )
            {
                if( out[i] == '\0' )
                    out[i] = ' ';
            }
            while( got > 0 && out[got - 1] == ' ' )
                got--;
            out[got] = '\0';
        }
    }
#elif defined(_WIN32)
    (void)at;
    snprintf(out, out_size, "%s", GetCommandLineA());
#else
    (void)at;
    (void)out_size;
#endif
    /* One line in the lockfile: a newline in an argument would end it. */
    for( char* p = out; *p; p++ )
    {
        if( *p == '\n' || *p == '\r' )
            *p = ' ';
    }
}

/** What runs as `pid` (its executable), for a lockfile that names no cmd. */
static void
lock_process_command(
    long pid,
    char* out,
    size_t out_size)
{
    out[0] = '\0';
#if defined(__APPLE__)
    {
        char path[PROC_PIDPATHINFO_MAXSIZE];
        if( proc_pidpath((int)pid, path, sizeof(path)) > 0 )
            snprintf(out, out_size, "%s", path);
    }
#elif defined(__linux__)
    {
        char path[64];
        FILE* file;
        snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid);
        file = fopen(path, "rb");
        if( file )
        {
            size_t got = fread(out, 1, out_size - 1, file);
            fclose(file);
            for( size_t i = 0; i < got; i++ )
            {
                if( out[i] == '\0' )
                    out[i] = ' ';
            }
            out[got] = '\0';
        }
    }
#elif defined(_WIN32)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
        if( process )
        {
            DWORD length = (DWORD)out_size;
            if( !QueryFullProcessImageNameA(process, 0, out, &length) )
                out[0] = '\0';
            CloseHandle(process);
        }
    }
#else
    (void)pid;
    (void)out_size;
#endif
}

/** The holder a lockfile's text names. `pid` 0 when it names none. */
struct lock_holder
{
    long pid;
    long long since;
    char cmd[400];
};

static void
lock_parse(
    const char* text,
    struct lock_holder* out)
{
    const char* line = text;

    memset(out, 0, sizeof(*out));
    while( line && *line )
    {
        const char* end = strchr(line, '\n');
        size_t const length = end ? (size_t)(end - line) : strlen(line);
        if( length > 4 && strncmp(line, "pid ", 4) == 0 )
            out->pid = strtol(line + 4, NULL, 10);
        else if( length > 6 && strncmp(line, "since ", 6) == 0 )
            out->since = strtoll(line + 6, NULL, 10);
        else if( length > 4 && strncmp(line, "cmd ", 4) == 0 )
            snprintf(out->cmd, sizeof(out->cmd), "%.*s", (int)(length - 4), line + 4);
        line = end ? end + 1 : NULL;
    }
}

/** Read the lockfile at `path` (at most a few hundred bytes are meaningful).
 *  0 when it cannot be read. */
static int
lock_read(
    const char* path,
    struct lock_holder* out)
{
    char text[1024];
    FILE* file = fopen(path, "rb");
    size_t got;

    memset(out, 0, sizeof(*out));
    if( !file )
        return 0;
    got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    lock_parse(text, out);
    return 1;
}

/** 1 when `pid` names a running process (a process of another user counts:
 *  it runs, we just may not signal it). */
static int
lock_pid_alive(long pid)
{
#if defined(_WIN32)
    HANDLE process;
    DWORD code = 0;
    int alive;

    if( pid <= 0 )
        return 1; /* names nobody: not ours to judge */
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if( !process )
        return GetLastError() == ERROR_ACCESS_DENIED; /* runs, as another user */
    alive = !GetExitCodeProcess(process, &code) || code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#elif defined(__EMSCRIPTEN__)
    (void)pid;
    return 1; /* no way to ask: never call a lock stale */
#else
    if( pid <= 0 )
        return 1; /* names nobody: not ours to judge */
    if( kill((pid_t)pid, 0) == 0 )
        return 1;
    return errno != ESRCH;
#endif
}

/** "pid 43893 (torirs --manifest ..., since 16:22)" for a person. */
static void
lock_describe(
    const struct lock_holder* holder,
    char* out,
    size_t out_size)
{
    char cmd[400];
    char since[48];
    const char* shown;

    snprintf(cmd, sizeof(cmd), "%s", holder->cmd);
    if( !cmd[0] && holder->pid > 0 )
        lock_process_command(holder->pid, cmd, sizeof(cmd));
    /* The executable's directory says nothing a person needs: torirs, not
     * /Users/.../src/torirs. */
    shown = cmd;
    {
        const char* space = strchr(cmd, ' ');
        const char* slash = NULL;
        for( const char* p = cmd; *p && (!space || p < space); p++ )
        {
            if( *p == '/' )
                slash = p;
        }
        if( slash )
            shown = slash + 1;
    }
    since[0] = '\0';
    if( holder->since > 0 )
    {
        time_t const at = (time_t)holder->since;
        time_t const now = time(NULL);
        struct tm then_tm;
        struct tm now_tm;
#if defined(_WIN32)
        localtime_s(&then_tm, &at);
        localtime_s(&now_tm, &now);
#else
        localtime_r(&at, &then_tm);
        localtime_r(&now, &now_tm);
#endif
        if( then_tm.tm_year == now_tm.tm_year && then_tm.tm_yday == now_tm.tm_yday )
            strftime(since, sizeof(since), "since %H:%M", &then_tm);
        else
            strftime(since, sizeof(since), "since %b %d %H:%M", &then_tm);
    }
    if( holder->pid <= 0 )
        snprintf(out, out_size, "an unnamed holder (the lockfile names no pid)");
    else if( shown[0] && since[0] )
        snprintf(out, out_size, "pid %ld (%.160s%s, %s)", holder->pid, shown,
            strlen(shown) > 160 ? "..." : "", since);
    else if( shown[0] )
        snprintf(out, out_size, "pid %ld (%.160s%s)", holder->pid, shown, strlen(shown) > 160 ? "..." : "");
    else if( since[0] )
        snprintf(out, out_size, "pid %ld (%s)", holder->pid, since);
    else
        snprintf(out, out_size, "pid %ld", holder->pid);
}

/** Make the lockfile. 1 taken, 0 it exists (errno EEXIST) or cannot be made. */
static int
lock_create(const char* path)
{
    char note[600];
    char cmd[440];
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
    int length;

    if( fd < 0 )
        return 0;
    lock_own_command(cmd, sizeof(cmd));
    length = snprintf(note, sizeof(note), "pid %ld\nsince %lld\ncmd %s\n", (long)getpid(),
        (long long)time(NULL), cmd);
    if( length > 0 )
        (void)!write(fd, note, (size_t)(length < (int)sizeof(note) ? length : (int)sizeof(note) - 1));
    close(fd);
    return 1;
}

static enum EditorHost_Status
local_session(
    void* user_data,
    int acquire)
{
    struct local_host* host = user_data;
    char path[1100];
    struct lock_holder holder;

    assert(host);

    snprintf(path, sizeof(path), "%s/.editor-session.lock", host->content_dir);

    if( !acquire )
    {
        if( host->lock_path[0] != '\0' )
        {
            remove(host->lock_path);
            host->lock_path[0] = '\0';
        }
        return EDITOR_HOST_OK;
    }

    host->lock_holder[0] = '\0';
    if( lock_create(path) )
    {
        snprintf(host->lock_path, sizeof(host->lock_path), "%s", path);
        return EDITOR_HOST_OK;
    }

    if( !lock_read(path, &holder) )
    {
        /* Gone between the create and the read (its holder released it), or
         * unreadable: one more try, then report what is there. */
        if( lock_create(path) )
        {
            snprintf(host->lock_path, sizeof(host->lock_path), "%s", path);
            return EDITOR_HOST_OK;
        }
        snprintf(host->lock_holder, sizeof(host->lock_holder),
            "%s is held (%s exists and cannot be read)", host->content_dir, path);
        return EDITOR_HOST_LOCKED;
    }

    if( holder.pid > 0 && !lock_pid_alive(holder.pid) )
    {
        /* Its holder is dead. Move it aside rather than remove it, and check
         * the moved file is the one judged: a second session reclaiming the
         * same stale lock may already have replaced it with its own live one,
         * which must go back. */
        char aside[1200];
        struct lock_holder moved;
        char who[512];
        snprintf(aside, sizeof(aside), "%s.stale.%ld", path, (long)getpid());
        lock_describe(&holder, who, sizeof(who));
        if( rename(path, aside) == 0 )
        {
            if( lock_read(aside, &moved) && moved.pid == holder.pid && moved.since == holder.since )
            {
                remove(aside);
                TORIRS_LOG("editor: %s held a stale lock (%s no longer runs); removed it\n",
                    host->content_dir, who);
                if( lock_create(path) )
                {
                    snprintf(host->lock_path, sizeof(host->lock_path), "%s", path);
                    return EDITOR_HOST_OK;
                }
            }
            else
                (void)rename(aside, path);
        }
        if( !lock_read(path, &holder) )
            memset(&holder, 0, sizeof(holder));
    }

    {
        char who[512];
        lock_describe(&holder, who, sizeof(who));
        snprintf(host->lock_holder, sizeof(host->lock_holder), "%s is held by %s", host->content_dir, who);
    }
    return EDITOR_HOST_LOCKED;
}

static void
local_free(void* user_data)
{
    struct local_host* host = user_data;

    if( !host )
        return;

    /* Never strand the lock: a session that dies holding it would lock every
     * later one out of a tree nobody is editing. */
    if( host->lock_path[0] != '\0' )
        remove(host->lock_path);
    /* A walk in flight owns `fresh` until it lands. */
    scripted_spawns_land(host, 1);
    if( host->spawns.built )
        Editor_SpawnFree(&host->spawns.rows);
    ToriMem_Free(ToriMem_Editor(), host);
}

static enum EditorHost_Status
local_spawn_save(
    void* user_data,
    int map_x,
    int map_z,
    const char* text,
    size_t length)
{
    struct local_host* host = user_data;
    char dir[1200];
    char path[1300];

    assert(host);

    if( host->lock_path[0] == '\0' )
    {
        TORIRS_ERR("editor: refusing to save without the session lock\n");
        return EDITOR_HOST_LOCKED;
    }

    /* mkdir -p the edited lane; every component may already exist, and EEXIST
     * is success for this purpose. */
    {
        static char const* const legs[] = {
            "server", "server/scripts", "server/scripts/areas", "server/scripts/areas/edited",
            "server/scripts/areas/edited/configs"
        };
        for( size_t i = 0; i < sizeof(legs) / sizeof(legs[0]); i++ )
        {
            snprintf(dir, sizeof(dir), "%s/%s", host->content_dir, legs[i]);
            if( editor_mkdir(dir) != 0 && errno != EEXIST )
            {
                TORIRS_ERR("editor: cannot create %s\n", dir);
                return EDITOR_HOST_IO_ERROR;
            }
        }
    }

    snprintf(
        path, sizeof(path), "%s/server/scripts/areas/edited/configs/m%d_%d.spawn",
        host->content_dir, map_x, map_z);
    if( !text || length == 0 )
    {
        remove(path); /* absent == no edited spawns; missing is already that */
        return EDITOR_HOST_OK;
    }
    return write_file_atomic(path, text, length);
}

/* ---- documents by (kind, key) -------------------------------------------- */

int
Editor_HostDocPath(
    enum Editor_DocKind kind,
    const char* key,
    int part,
    char* out,
    size_t out_size)
{
    int written;

    assert(key);
    assert(out);
    assert(out_size > 0);

    out[0] = '\0';
    switch( kind )
    {
    case EDITOR_DOC_SQUARE:
    {
        char storage[EDITOR_DOC_STORAGE_MAX];
        char bare[EDITOR_DOC_KEY_MAX];
        static char const* const extensions[] = { "jm2", "jl2", "filepack" };

        /* A scratch square has no file: it is placed, never saved. */
        if( part < 0 || part > 2 || !Editor_DocSplitSquareKey(key, storage, bare)
            || Editor_DocStorageIsScratch(storage) )
            return 0;
        written = snprintf(
            out, out_size, "%s/%s.%s", storage[0] ? storage : "maps", bare, extensions[part]);
        break;
    }
    case EDITOR_DOC_SPAWN:
        if( part != 0 || !Editor_DocKeyValid(kind, key) )
            return 0;
        written = snprintf(out, out_size, "server/scripts/areas/edited/configs/%s.spawn", key);
        break;
    case EDITOR_DOC_CFG:
    {
        const char* colon = strchr(key, ':');
        const char* overlay;
        if( part != 0 || !Editor_DocKeyValid(kind, key) || !colon )
            return 0;
        /* "<type>:<name>@<overlay>": the block lives in that server overlay. */
        overlay = Editor_CfgKeyOverlay(key);
        if( overlay )
            written = snprintf(out, out_size, "server/scripts/%s", overlay);
        else
            written = snprintf(out, out_size, "configs/all.%.*s", (int)(colon - key), key);
        break;
    }
    case EDITOR_DOC_IFACE:
        if( part != 0 || !Editor_DocKeyValid(kind, key) )
            return 0;
        written = snprintf(out, out_size, "interfaces/%s.if", key + 3);
        break;
    case EDITOR_DOC_AREAS:
        /* The named areas and square collections: in the content tree so
         * the team shares them, outside every pack input (editor_areas.h). */
        if( part != 0 || !Editor_DocKeyValid(kind, key) )
            return 0;
        written = snprintf(out, out_size, "editor/areas.txt");
        break;
    case EDITOR_DOC_PALETTES:
        /* The catalog palettes: beside the areas, for the same reasons
         * (editor_palette.h). */
        if( part != 0 || !Editor_DocKeyValid(kind, key) )
            return 0;
        written = snprintf(out, out_size, "editor/palettes.txt");
        break;
    case EDITOR_DOC_SCENES:
        /* The scenes: beside the areas and the palettes (editor_scenes.h). */
        if( part != 0 || !Editor_DocKeyValid(kind, key) )
            return 0;
        written = snprintf(out, out_size, "editor/scenes.txt");
        break;
    case EDITOR_DOC_TEXT:
        if( part != 0 || !Editor_DocKeyValid(kind, key) )
            return 0;
        if( strncmp(key, "rs2:", 4) == 0 )
            written = snprintf(out, out_size, "server/scripts/%s", key + 4);
        else if( strncmp(key, "cs2:", 4) == 0 )
            written = snprintf(out, out_size, "scripts/%s.cs2", key + 4);
        else if( strncmp(key, "pack:", 5) == 0 )
            written = snprintf(out, out_size, "pack/%s", key + 5);
        else if( strncmp(key, "compack:", 8) == 0 )
            written = snprintf(out, out_size, "configs/all.%s.compack", key + 8);
        else if( strncmp(key, "fields:", 7) == 0 )
            written = snprintf(out, out_size, "fields/%s", key + 7);
        else if( strncmp(key, "editor:", 7) == 0 )
            /* editor/favourites.txt, outside every pack input like the
             * areas and the palettes (editor_favourites.h). */
            written = snprintf(out, out_size, "editor/%s", key + 7);
        else if( strncmp(key, "stamp:", 6) == 0 )
            written = snprintf(out, out_size, "editor/stamps/%s.stamp", key + 6);
        else if( strcmp(key, "stamps:index") == 0 )
            written = snprintf(out, out_size, "editor/stamps/index.txt");
        else if( strncmp(key, "wma:", 4) == 0 )
            /* A world-map area file (editor_wma.h), where the catalog's
             * atlas reader looks for every .wma. */
            written = snprintf(out, out_size, "worldmap/areas/%s.wma", key + 4);
        else if( strncmp(key, "stats:", 6) == 0 )
            /* An npc's stats ledger (editor_npcstats.h). */
            written = snprintf(out, out_size, "npc_stats/%s.stats", key + 6);
        else
            return 0;
        break;
    default:
        return 0;
    }
    return written > 0 && (size_t)written < out_size;
}

/** The `[name]` block of a CFG key inside the file text: from its header line
 *  to the next header (or EOF). Returns 0 when the record is not there. */
static int
cfg_block_extent(
    const char* text,
    size_t length,
    const char* key,
    size_t* out_start,
    size_t* out_end)
{
    const char* name = strchr(key, ':') + 1;
    /* An overlay key's record name stops at its '@'. */
    size_t name_length = strchr(name, '@') ? (size_t)(strchr(name, '@') - name) : strlen(name);
    size_t at = 0;

    while( at < length )
    {
        const char* newline = memchr(text + at, '\n', length - at);
        size_t stop = newline ? (size_t)(newline - text) : length;
        size_t content = stop;

        if( content > at && text[content - 1] == '\r' )
            content--;
        if( content - at == name_length + 2 && text[at] == '[' && text[content - 1] == ']'
            && memcmp(text + at + 1, name, name_length) == 0 )
        {
            size_t next = newline ? stop + 1 : length;
            while( next < length )
            {
                const char* nl = memchr(text + next, '\n', length - next);
                if( text[next] == '[' )
                    break;
                next = nl ? (size_t)(nl - text) + 1 : length;
            }
            *out_start = at;
            *out_end = next;
            return 1;
        }
        at = newline ? stop + 1 : length;
    }
    return 0;
}

/*
 * "spawns:m<x>_<z>": the rows of the square from every .spawn file that is
 * neither the generated areas/world one nor the edited one -- the files quests
 * and skills keep in their own configs/. A read-only document cut from the
 * snapshot (struct scripted_spawns) of the tree as it stands, so a new file
 * needs no index. Suppression rows in those files are not listed: they hide a
 * spawn, they are not one.
 */
static void
scripted_spawns_walk(
    const struct local_host* host,
    const char* rel,
    struct Editor_SpawnFile* out)
{
    char dir_path[2200];
    DIR* dir;
    struct dirent* entry;

    snprintf(dir_path, sizeof(dir_path), "%s/server/scripts/%s", host->content_dir, rel);
    dir = opendir(dir_path);
    if( !dir )
        return;
    while( (entry = readdir(dir)) != NULL )
    {
        char child[1024];
        char path[2600];
        struct stat st;
        size_t name_length = strlen(entry->d_name);

        if( entry->d_name[0] == '.' )
            continue;
        if( snprintf(child, sizeof(child), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name) >= (int)sizeof(child) )
            continue;
        if( strcmp(child, "areas/world") == 0 || strcmp(child, "areas/edited") == 0 )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if( stat(path, &st) != 0 )
            continue;
        if( S_ISDIR(st.st_mode) )
        {
            scripted_spawns_walk(host, child, out);
            continue;
        }
        if( name_length > 6 && strcmp(entry->d_name + name_length - 6, ".spawn") == 0 )
        {
            struct EditorHost_Blob blob = { NULL, 0 };
            struct Editor_SpawnFile file;

            if( read_whole_file(path, &blob) != EDITOR_HOST_OK )
                continue;
            Editor_SpawnInit(&file);
            if( Editor_SpawnParse(&file, blob.data, blob.size).ok )
            {
                for( int i = 0; i < file.line_count; i++ )
                {
                    const struct Editor_SpawnRow* row = &file.lines[i].row;
                    if( file.lines[i].kind == EDITOR_SPAWN_LINE_ROW && !row->suppress )
                        (void)Editor_SpawnInsert(out, row);
                }
            }
            else
                TORIRS_ERR("editor: %s does not parse; its spawns are not previewed\n", path);
            Editor_SpawnFree(&file);
            Editor_HostBlobFree(&blob);
        }
    }
    closedir(dir);
}

static void
scripted_spawns_run(struct local_host* host)
{
    struct timespec t0;
    struct timespec t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    scripted_spawns_walk(host, "", &host->spawns.fresh);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if( getenv("TORIRS_EDITOR_HOST_TRACE") )
        fprintf(stderr, "editor: scripted spawns walk of %s took %.1f ms (%d lines)\n", host->content_dir,
            (double)(t1.tv_sec - t0.tv_sec) * 1000.0 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6,
            host->spawns.fresh.line_count);
    /* Release: every write of the walk is visible to the landing's acquire. */
    __atomic_store_n(&host->spawns.done, 1, __ATOMIC_RELEASE);
}

#if LOCAL_SPAWNS_THREAD
static void*
scripted_spawns_thread_main(void* arg)
{
    scripted_spawns_run(arg);
    return NULL;
}
#endif

/** Start a walk, unless one is in flight. */
static void
scripted_spawns_start(struct local_host* host)
{
    struct scripted_spawns* spawns = &host->spawns;

    if( spawns->walking )
        return;
    Editor_SpawnInit(&spawns->fresh);
    spawns->walking = 1;
    spawns->done = 0;
#if LOCAL_SPAWNS_THREAD
    if( pthread_create(&spawns->thread, NULL, scripted_spawns_thread_main, host) == 0 )
    {
        spawns->thread_live = 1;
        return;
    }
#endif
    scripted_spawns_run(host);
}

/** A walk that has landed (or, with `wait`, the one in flight, joined)
 *  becomes the snapshot. */
static void
scripted_spawns_land(
    struct local_host* host,
    int wait)
{
    struct scripted_spawns* spawns = &host->spawns;

    if( !spawns->walking )
        return;
    if( !wait && !__atomic_load_n(&spawns->done, __ATOMIC_ACQUIRE) )
        return;
#if LOCAL_SPAWNS_THREAD
    if( spawns->thread_live )
    {
        pthread_join(spawns->thread, NULL);
        spawns->thread_live = 0;
    }
#endif
    assert(__atomic_load_n(&spawns->done, __ATOMIC_ACQUIRE));
    if( spawns->built )
        Editor_SpawnFree(&spawns->rows);
    spawns->rows = spawns->fresh;
    memset(&spawns->fresh, 0, sizeof(spawns->fresh));
    spawns->built = 1;
    spawns->walked_at = time(NULL);
    spawns->walking = 0;
}

static enum EditorHost_Status
scripted_spawns_load(
    struct local_host* host,
    const char* key,
    struct EditorHost_Blob* out_blob)
{
    struct scripted_spawns* spawns = &host->spawns;
    struct Editor_SpawnFile out;
    int map_x;
    int map_z;
    size_t size;

    if( sscanf(key + 7, "m%d_%d", &map_x, &map_z) != 2 )
        return EDITOR_HOST_REFUSED;
    /* The snapshot: the first ask waits for the walk the open started (or
     * runs it); a later one answers from what stands and, once the snapshot
     * is old enough, starts the next walk for the asks after it. */
    scripted_spawns_land(host, 0);
    if( !spawns->built )
    {
        scripted_spawns_start(host);
        scripted_spawns_land(host, 1);
    }
    else if( !spawns->walking && time(NULL) - spawns->walked_at >= LOCAL_SPAWNS_REFRESH_S )
        scripted_spawns_start(host);
    Editor_SpawnInit(&out);
    for( int i = 0; i < spawns->rows.line_count; i++ )
    {
        const struct Editor_SpawnRow* row = &spawns->rows.lines[i].row;
        if( spawns->rows.lines[i].kind == EDITOR_SPAWN_LINE_ROW && row->abs_x / EDITOR_SQUARE_X == map_x
            && row->abs_z / EDITOR_SQUARE_Z == map_z )
            (void)Editor_SpawnInsert(&out, row);
    }
    size = Editor_SpawnEmit(&out, NULL, 0);
    out_blob->data = ToriMem_Alloc(ToriMem_Editor(), size + 1);
    assert(out_blob->data);
    Editor_SpawnEmit(&out, out_blob->data, size + 1);
    out_blob->size = size;
    Editor_SpawnFree(&out);
    return EDITOR_HOST_OK;
}

static enum EditorHost_Status
local_doc_load(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key,
    int part,
    struct EditorHost_Blob* out_blob)
{
    struct local_host* host = user_data;
    char relative[1024];
    char path[2200];
    enum EditorHost_Status status;

    assert(host);
    assert(key);
    assert(out_blob);

    out_blob->data = NULL;
    out_blob->size = 0;
    if( kind == EDITOR_DOC_TEXT && part == 0 && Editor_DocKeyValid(kind, key) && strncmp(key, "spawns:", 7) == 0 )
        return scripted_spawns_load(host, key, out_blob);
    if( !Editor_HostDocPath(kind, key, part, relative, sizeof(relative)) )
        return EDITOR_HOST_REFUSED;
    snprintf(path, sizeof(path), "%s/%s", host->content_dir, relative);

    status = read_whole_file(path, out_blob);
    if( status != EDITOR_HOST_OK || kind != EDITOR_DOC_CFG )
        return status;

    /* A config record is its block, not the whole all.<type> — the largest of
     * those files is several times the wire's frame bound. */
    {
        size_t start;
        size_t end;
        char* block;

        if( !cfg_block_extent(out_blob->data, out_blob->size, key, &start, &end) )
        {
            Editor_HostBlobFree(out_blob);
            return EDITOR_HOST_ABSENT;
        }
        block = ToriMem_Alloc(ToriMem_Editor(), end - start + 1);
        memcpy(block, out_blob->data + start, end - start);
        block[end - start] = '\0';
        Editor_HostBlobFree(out_blob);
        out_blob->data = block;
        out_blob->size = end - start;
    }
    return EDITOR_HOST_OK;
}

/** mkdir -p for the directories of `relative` under the content root. */
static enum EditorHost_Status
make_parents(
    const struct local_host* host,
    const char* relative)
{
    char dir[2200];
    size_t base;

    base = (size_t)snprintf(dir, sizeof(dir), "%s/", host->content_dir);
    for( const char* at = relative; *at; at++ )
    {
        if( *at != '/' )
            continue;
        snprintf(dir + base, sizeof(dir) - base, "%.*s", (int)(at - relative), relative);
        if( editor_mkdir(dir) != 0 && errno != EEXIST )
        {
            TORIRS_ERR("editor: cannot create %s: %s\n", dir, strerror(errno));
            return EDITOR_HOST_IO_ERROR;
        }
    }
    return EDITOR_HOST_OK;
}

/* An interface saves its .compack beside it. Block positions are file ids
 * while the compack is positional, and a structure edit renumbers them
 * (editor_if.h, Editor_IfCompackForSave). */
static enum EditorHost_Status
local_iface_save(
    const char* path,
    const char* text,
    size_t length)
{
    struct EditorHost_Blob old_if = { NULL, 0 };
    struct EditorHost_Blob compack = { NULL, 0 };
    char compack_path[2300];
    char* rewritten = NULL;
    size_t rewritten_length = 0;
    size_t const path_length = strlen(path);
    enum EditorHost_Status status;

    assert(path_length > 3); /* Editor_HostDocPath names every IFACE "<name>.if" */
    assert(strcmp(path + path_length - 3, ".if") == 0);
    snprintf(compack_path, sizeof(compack_path), "%.*s.compack", (int)(path_length - 3), path);
    status = read_whole_file(path, &old_if);
    if( status == EDITOR_HOST_IO_ERROR )
        return status;
    status = read_whole_file(compack_path, &compack);
    if( status == EDITOR_HOST_IO_ERROR )
    {
        Editor_HostBlobFree(&old_if);
        return status;
    }
    if( !Editor_IfCompackForSave(compack.data ? compack.data : "",
            compack.size,
            old_if.data ? old_if.data : "",
            old_if.size,
            text,
            length,
            &rewritten,
            &rewritten_length) )
    {
        TORIRS_ERR("editor: %s: its components changed but the .compack is not positional; "
                   "not saved\n",
            path);
        status = EDITOR_HOST_REFUSED;
    }
    else
    {
        status = write_file_atomic(path, text, length);
        if( status == EDITOR_HOST_OK && rewritten )
            status = write_file_atomic(compack_path, rewritten, rewritten_length);
    }
    ToriMem_Free(ToriMem_Editor(), rewritten);
    Editor_HostBlobFree(&old_if);
    Editor_HostBlobFree(&compack);
    return status;
}

static enum EditorHost_Status
local_doc_save(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key,
    int part,
    const char* text,
    size_t length)
{
    struct local_host* host = user_data;
    char relative[1024];
    char path[2200];
    enum EditorHost_Status status;

    assert(host);
    assert(key);
    assert(text || length == 0);

    if( host->lock_path[0] == '\0' )
    {
        TORIRS_ERR("editor: refusing to save without the session lock\n");
        return EDITOR_HOST_LOCKED;
    }
    if( !Editor_HostDocPath(kind, key, part, relative, sizeof(relative)) )
        return EDITOR_HOST_REFUSED;
    snprintf(path, sizeof(path), "%s/%s", host->content_dir, relative);

    status = make_parents(host, relative);
    if( status != EDITOR_HOST_OK )
        return status;

    if( kind == EDITOR_DOC_IFACE && text )
        return local_iface_save(path, text, length);
    if( kind != EDITOR_DOC_CFG )
    {
        if( !text )
        {
            remove(path); /* absent is the state asked for; missing is it already */
            return EDITOR_HOST_OK;
        }
        return write_file_atomic(path, text, length);
    }

    /* A record save rewrites one block of all.<type> and no other byte. */
    {
        struct EditorHost_Blob file = { NULL, 0 };
        size_t start = 0;
        size_t end = 0;
        size_t total;
        size_t at = 0;
        char* merged;
        int found;

        status = read_whole_file(path, &file);
        if( status == EDITOR_HOST_IO_ERROR )
            return status;
        found = file.data && cfg_block_extent(file.data, file.size, key, &start, &end);
        if( !found )
        {
            start = file.size;
            end = file.size;
        }

        total = file.size - (end - start) + (text ? length : 0) + 2;
        merged = ToriMem_Alloc(ToriMem_Editor(), total);
        if( start > 0 )
            memcpy(merged, file.data, start);
        at = start;
        if( !found && text && length > 0 && file.size > 0 )
        {
            /* A new record goes at the end, after a blank separator line. */
            if( file.data[file.size - 1] != '\n' )
                merged[at++] = '\n';
            if( file.size < 2 || file.data[file.size - 2] != '\n'
                || file.data[file.size - 1] != '\n' )
                merged[at++] = '\n';
        }
        if( text && length > 0 )
        {
            memcpy(merged + at, text, length);
            at += length;
        }
        if( file.size > end )
        {
            memcpy(merged + at, file.data + end, file.size - end);
            at += file.size - end;
        }
        status = write_file_atomic(path, merged, at);
        ToriMem_Free(ToriMem_Editor(), merged);
        Editor_HostBlobFree(&file);
        return status;
    }
}

static const struct EditorHost_VTable local_vtable = {
    local_square_list, local_square_load, local_square_save, local_spawn_save,
    local_bake,        local_session,     local_free,        local_doc_load,
    local_doc_save,
};

int
Editor_HostOpenLocal(
    struct EditorHost* host,
    const char* content_dir,
    const char* repo_root)
{
    struct local_host* local;

    assert(host);
    assert(content_dir);

    local = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*local));

    snprintf(local->content_dir, sizeof(local->content_dir), "%s", content_dir);
    if( repo_root )
        snprintf(local->repo_root, sizeof(local->repo_root), "%s", repo_root);

    host->vtable = &local_vtable;
    host->user_data = local;
    /* The scripted spawns' walk starts now, so the first square open (the
     * world's, seconds from now) finds its snapshot landed or nearly so. */
    scripted_spawns_start(local);
    return 1;
}

const char*
Editor_HostLocalLockHolder(const struct EditorHost* host)
{
    assert(host);
    assert(host->vtable == &local_vtable);
    return ((const struct local_host*)host->user_data)->lock_holder;
}

/* ---- open in the user's editor ---------------------------------------------- */

#if defined(__EMSCRIPTEN__)

enum EditorHost_Status
Editor_HostLocalOpenInEditor(
    const char* path,
    int line,
    char* out_cmdline,
    size_t out_size)
{
    assert(path);
    (void)line;
    if( out_cmdline && out_size > 0 )
        out_cmdline[0] = '\0';
    return EDITOR_HOST_UNSUPPORTED;
}

#elif defined(_WIN32)

enum EditorHost_Status
Editor_HostLocalOpenInEditor(
    const char* path,
    int line,
    char* out_cmdline,
    size_t out_size)
{
    const char* tmpl = getenv("TORIRS_EDITOR_OPEN_CMD");
    char command[2048];
    char cmd_line[2200];
    char found[MAX_PATH];
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;

    assert(path);
    if( out_cmdline && out_size > 0 )
        out_cmdline[0] = '\0';
    /* VS Code's launcher on PATH is code.cmd. */
    if( !tmpl || !tmpl[0] )
        tmpl = Editor_OpenCommandDefaultWindows(SearchPathA(NULL, "code.cmd", NULL, sizeof(found), found, NULL) != 0);
    if( Editor_OpenCommandLine(tmpl, path, line, command, sizeof(command)) == 0 )
        return EDITOR_HOST_REFUSED;
    if( out_cmdline && out_size > 0 )
        snprintf(out_cmdline, out_size, "%s", command);
    /* Through cmd.exe, as the user's own command line (start, a .cmd), with
     * no console and not waited on: the editor is never this process's. */
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

#include <spawn.h>
#include <sys/wait.h>
extern char** environ;

/** `name` is an executable in a PATH directory. */
static int
on_path(const char* name)
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
Editor_HostLocalOpenInEditor(
    const char* path,
    int line,
    char* out_cmdline,
    size_t out_size)
{
    const char* tmpl = getenv("TORIRS_EDITOR_OPEN_CMD");
    char command[2048];
    char shell_line[2100];
    char* argv[4];
    pid_t pid;
    int status = 0;

    assert(path);
    if( out_cmdline && out_size > 0 )
        out_cmdline[0] = '\0';
    if( !tmpl || !tmpl[0] )
#if defined(__APPLE__)
        tmpl = Editor_OpenCommandDefault(on_path("code"), 1);
#else
        tmpl = Editor_OpenCommandDefault(on_path("code"), 0);
#endif
    if( Editor_OpenCommandLine(tmpl, path, line, command, sizeof(command)) == 0 )
        return EDITOR_HOST_REFUSED;
    if( out_cmdline && out_size > 0 )
        snprintf(out_cmdline, out_size, "%s", command);
    /* Backgrounded inside the shell, which exits at once: waiting on it is
     * waiting on nothing, and the editor is never this process's child. */
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
