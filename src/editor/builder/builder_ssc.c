#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "builder_ssc.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__EMSCRIPTEN__)
#define BUILDER_SSC_NATIVE 0
#else
#define BUILDER_SSC_NATIVE 1
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char** environ;
#endif

static int
copy_path(
    char* out,
    size_t out_size,
    const char* text)
{
    int written = snprintf(out, out_size, "%s", text);
    return written >= 0 && (size_t)written < out_size;
}

/* ---- the parser (portable) ----------------------------------------------- */

static int
contains(
    const char* text,
    size_t length,
    const char* needle)
{
    size_t needle_length = strlen(needle);

    for( size_t i = 0; i + needle_length <= length; i++ )
    {
        if( memcmp(text + i, needle, needle_length) == 0 )
            return 1;
    }
    return 0;
}

void
Builder_SscParseOutput(
    const char* stage,
    const char* output,
    size_t length,
    struct Builder_SscResult* result)
{
    const char* at = output;
    const char* end = output + length;
    size_t stage_length;

    assert(stage);
    assert(output || length == 0);
    assert(result);

    stage_length = strlen(stage);
    while( at < end )
    {
        const char* line_end = memchr(at, '\n', (size_t)(end - at));
        const char* line;
        size_t line_length;
        const char* colon;

        if( !line_end )
            line_end = end;
        line = at;
        line_length = (size_t)(line_end - at);
        at = line_end < end ? line_end + 1 : end;

        if( line_length > 11 && memcmp(line, "sscompile: ", 11) == 0 )
        {
            /* "sscompile: 3184 compiled, ..." is the summary; anything else
             * with that prefix is a note or a located warning. */
            if( contains(line, line_length, " compiled, ") )
            {
                size_t copy = line_length - 11 < sizeof(result->summary) - 1
                                  ? line_length - 11
                                  : sizeof(result->summary) - 1;
                memcpy(result->summary, line + 11, copy);
                result->summary[copy] = '\0';
                continue;
            }
            line += 11;
            line_length -= 11;
        }

        /* <file>:<line>: <message> */
        colon = memchr(line, ':', line_length);
        while( colon )
        {
            const char* digits = colon + 1;
            const char* after = digits;
            while( after < line + line_length && *after >= '0' && *after <= '9' )
                after++;
            if( after > digits && after + 1 < line + line_length && after[0] == ':' && after[1] == ' ' )
            {
                struct Builder_SscDiag* diag;
                const char* file = line;
                size_t file_length = (size_t)(colon - line);
                const char* message = after + 2;
                size_t message_length = (size_t)(line + line_length - message);

                if( result->diag_count >= BUILDER_SSC_DIAGS_MAX )
                {
                    result->diags_dropped++;
                    break;
                }
                diag = &result->diags[result->diag_count++];
                memset(diag, 0, sizeof(*diag));
                if( file_length > stage_length && memcmp(file, stage, stage_length) == 0
                    && file[stage_length] == '/' )
                {
                    file += stage_length + 1;
                    file_length -= stage_length + 1;
                }
                if( file_length >= sizeof(diag->file) )
                    file_length = sizeof(diag->file) - 1;
                memcpy(diag->file, file, file_length);
                diag->line = atoi(digits);
                if( message_length >= 9 && memcmp(message, "warning: ", 9) == 0 )
                {
                    diag->warning = 1;
                    message += 9;
                    message_length -= 9;
                }
                if( message_length >= sizeof(diag->message) )
                    message_length = sizeof(diag->message) - 1;
                memcpy(diag->message, message, message_length);
                break;
            }
            colon = memchr(colon + 1, ':', (size_t)(line + line_length - colon - 1));
        }
    }
}

int
Builder_SscErrorsFor(
    const struct Builder_SscResult* result,
    const char* rel_path)
{
    int count = 0;

    assert(result);
    assert(rel_path);
    for( int i = 0; i < result->diag_count; i++ )
    {
        if( !result->diags[i].warning && strcmp(result->diags[i].file, rel_path) == 0 )
            count++;
    }
    return count;
}

#if BUILDER_SSC_NATIVE

/* ---- paths --------------------------------------------------------------- */

/** realpath() of `path`, or of its longest existing ancestor with the rest
 *  appended -- what the path WILL be once created. */
static int
resolve_future(
    const char* path,
    char* out,
    size_t out_size)
{
    char probe[PATH_MAX];
    char resolved[PATH_MAX];
    size_t cut;

    if( !copy_path(probe, sizeof(probe), path) )
        return 0;
    cut = strlen(probe);
    for( ;; )
    {
        if( realpath(probe, resolved) )
        {
            int written = snprintf(out, out_size, "%s%s", resolved, path + cut);
            return written >= 0 && (size_t)written < out_size;
        }
        while( cut > 0 && probe[cut - 1] != '/' )
            cut--;
        if( cut == 0 )
        {
            /* Relative with no existing part: resolve against the cwd. */
            if( !getcwd(resolved, sizeof(resolved)) )
                return 0;
            {
                int written = snprintf(out, out_size, "%s/%s", resolved, path);
                return written >= 0 && (size_t)written < out_size;
            }
        }
        cut--;
        probe[cut] = '\0';
        if( cut == 0 )
        {
            probe[0] = '/';
            probe[1] = '\0';
            cut = 1;
            if( realpath(probe, resolved) )
            {
                int written = snprintf(out, out_size, "%s", path);
                return written >= 0 && (size_t)written < out_size;
            }
            return 0;
        }
    }
}

static int
path_inside(
    const char* path,
    const char* root)
{
    size_t length = strlen(root);
    return strncmp(path, root, length) == 0 && (path[length] == '/' || path[length] == '\0');
}

static int
make_dirs(const char* path)
{
    char dir[PATH_MAX];
    size_t length;

    if( !copy_path(dir, sizeof(dir), path) )
        return 0;
    length = strlen(dir);
    for( size_t i = 1; i <= length; i++ )
    {
        if( dir[i] == '/' || dir[i] == '\0' )
        {
            char saved = dir[i];
            dir[i] = '\0';
            if( mkdir(dir, 0755) != 0 && errno != EEXIST )
                return 0;
            dir[i] = saved;
        }
    }
    return 1;
}

static int
make_parent_dirs(const char* file_path)
{
    char dir[PATH_MAX];
    char* slash;

    if( !copy_path(dir, sizeof(dir), file_path) )
        return 0;
    slash = strrchr(dir, '/');
    if( !slash )
        return 1;
    *slash = '\0';
    return make_dirs(dir);
}

int
Builder_SscInit(
    struct Builder_Ssc* ssc,
    const char* sscompile,
    const char* content_root,
    const char* work_dir)
{
    char content_real[PATH_MAX];
    char work_real[PATH_MAX];

    assert(ssc);
    assert(sscompile);
    assert(content_root);
    assert(work_dir);

    memset(ssc, 0, sizeof(*ssc));
    if( !realpath(content_root, content_real) )
        return 0;
    if( !resolve_future(work_dir, work_real, sizeof(work_real)) )
        return 0;
    /* The one rule this module exists to keep: nothing it writes is in the
     * content tree (MAPED_IMPLEMENTATION.md section 0). */
    if( path_inside(work_real, content_real) || path_inside(content_real, work_real) )
        return 0;
    if( !copy_path(ssc->sscompile, sizeof(ssc->sscompile), sscompile)
        || !copy_path(ssc->content_root, sizeof(ssc->content_root), content_real)
        || !copy_path(ssc->work_dir, sizeof(ssc->work_dir), work_real) )
        return 0;
    if( snprintf(ssc->stage, sizeof(ssc->stage), "%s/scripts", work_real) >= (int)sizeof(ssc->stage)
        || snprintf(ssc->out, sizeof(ssc->out), "%s/out", work_real) >= (int)sizeof(ssc->out) )
        return 0;
    return 1;
}

/* ---- staging ------------------------------------------------------------- */

#define STAGE_SKIP_MAX 32

struct stage_walk
{
    struct Builder_Ssc* ssc;
    char source_root[PATH_MAX];
    /** Lane script directories, relative to server/scripts. */
    char skip[STAGE_SKIP_MAX][256];
    int skip_count;
};

static int
staged_extension(const char* name)
{
    static const char* const k_extensions[] = { ".rs2",    ".constant", ".dbtable", ".varp",
                                                ".varbit", ".pack",     ".compack", ".alloc" };
    size_t length = strlen(name);

    for( size_t i = 0; i < sizeof(k_extensions) / sizeof(k_extensions[0]); i++ )
    {
        size_t extension = strlen(k_extensions[i]);
        if( length > extension && strcmp(name + length - extension, k_extensions[i]) == 0 )
            return 1;
    }
    return 0;
}

static void
read_lane_skips(struct stage_walk* walk)
{
    char ported[PATH_MAX];
    DIR* dir;
    struct dirent* entry;

    snprintf(ported, sizeof(ported), "%s/ported", walk->ssc->content_root);
    dir = opendir(ported);
    if( !dir )
        return;
    while( (entry = readdir(dir)) != NULL )
    {
        char ini[PATH_MAX];
        char line[512];
        FILE* file;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(ini, sizeof(ini), "%s/%s/lane.ini", ported, entry->d_name);
        file = fopen(ini, "r");
        if( !file )
            continue;
        while( fgets(line, sizeof(line), file) )
        {
            static const char k_key[] = "scripts=server/scripts/";
            size_t length = strlen(line);
            while( length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'
                                  || line[length - 1] == ' ') )
                line[--length] = '\0';
            if( strncmp(line, k_key, sizeof(k_key) - 1) == 0 && walk->skip_count < STAGE_SKIP_MAX )
                copy_path(walk->skip[walk->skip_count++], sizeof(walk->skip[0]),
                          line + sizeof(k_key) - 1);
        }
        fclose(file);
    }
    closedir(dir);
}

static int
skipped_dir(
    const char* name,
    int top)
{
    if( name[0] == '.' )
        return 1;
    /* server/scripts/build*: compiler outputs, gigabytes, never inputs. */
    return top && strncmp(name, "build", 5) == 0;
}

/** A lane's script directory: its sources are subtracted from a lane-less
 *  compile by their REAL path, which a stage copy does not have, so they are
 *  not staged at all. Its .constant files are, because the constant walk
 *  reads every one under --src whether the lane compiles or not. */
static int
lane_dir(
    const struct stage_walk* walk,
    const char* rel)
{
    for( int i = 0; i < walk->skip_count; i++ )
    {
        if( strcmp(rel, walk->skip[i]) == 0 )
            return 1;
    }
    return 0;
}

static int
is_put(
    const struct Builder_Ssc* ssc,
    const char* rel)
{
    for( int i = 0; i < ssc->put_count; i++ )
    {
        if( strcmp(ssc->puts[i], rel) == 0 )
            return 1;
    }
    return 0;
}

static int
copy_file(
    const char* from,
    const char* to,
    const struct stat* info)
{
    FILE* in = fopen(from, "rb");
    FILE* out;
    char buffer[65536];
    size_t got;
    int ok = 1;
    struct timeval times[2];

    if( !in )
        return 0;
    if( !make_parent_dirs(to) )
    {
        fclose(in);
        return 0;
    }
    out = fopen(to, "wb");
    if( !out )
    {
        fclose(in);
        return 0;
    }
    while( (got = fread(buffer, 1, sizeof(buffer), in)) > 0 )
    {
        if( fwrite(buffer, 1, got, out) != got )
        {
            ok = 0;
            break;
        }
    }
    fclose(in);
    if( fclose(out) != 0 )
        ok = 0;
    /* The copy carries the source's mtime, so the next stage can tell an
     * unchanged file by size and mtime alone. */
    times[0].tv_sec = info->st_atime;
    times[0].tv_usec = 0;
    times[1].tv_sec = info->st_mtime;
    times[1].tv_usec = 0;
    utimes(to, times);
    return ok;
}

static void
stage_tree(
    struct stage_walk* walk,
    const char* rel,
    int in_lane)
{
    char source_dir[PATH_MAX];
    DIR* dir;
    struct dirent* entry;
    struct Builder_Ssc* ssc = walk->ssc;

    snprintf(source_dir, sizeof(source_dir), "%s%s%s", walk->source_root, rel[0] ? "/" : "", rel);
    dir = opendir(source_dir);
    if( !dir )
        return;
    while( (entry = readdir(dir)) != NULL )
    {
        char child_rel[PATH_MAX];
        char source[PATH_MAX];
        char target[PATH_MAX];
        struct stat info;
        struct stat staged;

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        snprintf(child_rel, sizeof(child_rel), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name);
        snprintf(source, sizeof(source), "%s/%s", walk->source_root, child_rel);
        if( stat(source, &info) != 0 )
            continue;
        if( S_ISDIR(info.st_mode) )
        {
            if( !skipped_dir(entry->d_name, rel[0] == '\0') )
                stage_tree(walk, child_rel, in_lane || lane_dir(walk, child_rel));
            continue;
        }
        if( !S_ISREG(info.st_mode) || !staged_extension(entry->d_name) || is_put(ssc, child_rel) )
            continue;
        if( in_lane && strcmp(entry->d_name + strlen(entry->d_name) - 4, ".rs2") == 0 )
            continue;
        snprintf(target, sizeof(target), "%s/%s", ssc->stage, child_rel);
        if( stat(target, &staged) == 0 && staged.st_size == info.st_size
            && staged.st_mtime == info.st_mtime )
        {
            ssc->last_kept++;
            continue;
        }
        if( copy_file(source, target, &info) )
            ssc->last_copied++;
    }
    closedir(dir);
}

static void
drop_stale(
    struct stage_walk* walk,
    const char* rel,
    int in_lane)
{
    char stage_dir[PATH_MAX];
    DIR* dir;
    struct dirent* entry;
    struct Builder_Ssc* ssc = walk->ssc;

    snprintf(stage_dir, sizeof(stage_dir), "%s%s%s", ssc->stage, rel[0] ? "/" : "", rel);
    dir = opendir(stage_dir);
    if( !dir )
        return;
    while( (entry = readdir(dir)) != NULL )
    {
        char child_rel[PATH_MAX];
        char staged_path[PATH_MAX];
        char source[PATH_MAX];
        struct stat info;

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        snprintf(child_rel, sizeof(child_rel), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name);
        snprintf(staged_path, sizeof(staged_path), "%s/%s", ssc->stage, child_rel);
        if( stat(staged_path, &info) != 0 )
            continue;
        if( S_ISDIR(info.st_mode) )
        {
            drop_stale(walk, child_rel, in_lane || lane_dir(walk, child_rel));
            continue;
        }
        snprintf(source, sizeof(source), "%s/%s", walk->source_root, child_rel);
        if( is_put(ssc, child_rel) )
            continue;
        if( stat(source, &info) == 0
            && !(in_lane && strlen(entry->d_name) > 4
                 && strcmp(entry->d_name + strlen(entry->d_name) - 4, ".rs2") == 0) )
            continue;
        if( unlink(staged_path) == 0 )
            ssc->last_dropped++;
    }
    closedir(dir);
}

int
Builder_SscStage(struct Builder_Ssc* ssc)
{
    struct stage_walk walk;

    assert(ssc);
    assert(ssc->stage[0]);

    memset(&walk, 0, sizeof(walk));
    walk.ssc = ssc;
    snprintf(walk.source_root, sizeof(walk.source_root), "%s/server/scripts", ssc->content_root);
    ssc->last_copied = 0;
    ssc->last_dropped = 0;
    ssc->last_kept = 0;
    if( !make_dirs(ssc->stage) || !make_dirs(ssc->out) )
        return 0;
    read_lane_skips(&walk);
    stage_tree(&walk, "", 0);
    drop_stale(&walk, "", 0);
    ssc->staged = 1;
    return 1;
}

int
Builder_SscPut(
    struct Builder_Ssc* ssc,
    const char* rel_path,
    const char* text,
    size_t length)
{
    char target[PATH_MAX];
    FILE* file;
    int ok;

    assert(ssc);
    assert(rel_path);
    assert(text || length == 0);

    if( rel_path[0] == '\0' || rel_path[0] == '/' || strstr(rel_path, "..") || strlen(rel_path) >= 256 )
        return 0;
    if( snprintf(target, sizeof(target), "%s/%s", ssc->stage, rel_path) >= (int)sizeof(target) )
        return 0;
    if( !make_parent_dirs(target) )
        return 0;
    file = fopen(target, "wb");
    if( !file )
        return 0;
    ok = length == 0 || fwrite(text, 1, length, file) == length;
    if( fclose(file) != 0 )
        ok = 0;
    if( ok && !is_put(ssc, rel_path) && ssc->put_count < BUILDER_SSC_PUTS_MAX )
        copy_path(ssc->puts[ssc->put_count++], sizeof(ssc->puts[0]), rel_path);
    return ok;
}

static double
now_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

enum Builder_SscStatus
Builder_SscRun(
    struct Builder_Ssc* ssc,
    enum Builder_SscMode mode,
    struct Builder_SscResult* result)
{
    char pack[PATH_MAX];
    char configs[PATH_MAX];
    char* argv[16];
    int argc = 0;
    posix_spawn_file_actions_t actions;
    pid_t pid;
    int status = 0;
    double start;
    FILE* log;

    assert(ssc);
    assert(result);
    assert(ssc->stage[0]);

    memset(result, 0, sizeof(*result));
    result->status = BUILDER_SSC_NOT_RUN;
    if( access(ssc->sscompile, X_OK) != 0 )
        return result->status;
    if( !ssc->staged && !Builder_SscStage(ssc) )
        return result->status;
    snprintf(result->log_path, sizeof(result->log_path), "%s/sscompile.log", ssc->work_dir);
    snprintf(pack, sizeof(pack), "%s/pack", ssc->content_root);
    snprintf(configs, sizeof(configs), "%s/configs", ssc->content_root);

    /* --pack twice is exactly the two defaults sscompile derives from
     * --src, which here would point inside the work directory. */
    argv[argc++] = ssc->sscompile;
    argv[argc++] = "--src";
    argv[argc++] = ssc->stage;
    argv[argc++] = "--out";
    argv[argc++] = ssc->out;
    argv[argc++] = "--content-root";
    argv[argc++] = ssc->content_root;
    argv[argc++] = "--pack";
    argv[argc++] = pack;
    argv[argc++] = "--pack";
    argv[argc++] = configs;
    if( mode == BUILDER_SSC_DRY_RUN )
        argv[argc++] = "--dry-run";
    argv[argc] = NULL;

    if( posix_spawn_file_actions_init(&actions) != 0 )
        return result->status;
    posix_spawn_file_actions_addopen(&actions, 1, result->log_path, O_WRONLY | O_CREAT | O_TRUNC,
                                     0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    start = now_seconds();
    if( posix_spawn(&pid, ssc->sscompile, &actions, NULL, argv, environ) != 0 )
    {
        posix_spawn_file_actions_destroy(&actions);
        return result->status;
    }
    posix_spawn_file_actions_destroy(&actions);
    while( waitpid(pid, &status, 0) < 0 )
    {
        if( errno != EINTR )
            return result->status;
    }
    result->seconds = now_seconds() - start;
    result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result->status = result->exit_code == 0 ? BUILDER_SSC_OK : BUILDER_SSC_FAILED;

    log = fopen(result->log_path, "rb");
    if( log )
    {
        long size;
        char* text;

        fseek(log, 0, SEEK_END);
        size = ftell(log);
        fseek(log, 0, SEEK_SET);
        if( size > 0 )
        {
            text = ToriMem_Alloc(ToriMem_Editor(), (size_t)size + 1);
            assert(text);
            size = (long)fread(text, 1, (size_t)size, log);
            text[size] = '\0';
            Builder_SscParseOutput(ssc->stage, text, (size_t)size, result);
            ToriMem_Free(ToriMem_Editor(), text);
        }
        fclose(log);
    }
    return result->status;
}

#else /* !BUILDER_SSC_NATIVE */

int
Builder_SscInit(
    struct Builder_Ssc* ssc,
    const char* sscompile,
    const char* content_root,
    const char* work_dir)
{
    assert(ssc);
    assert(sscompile);
    assert(content_root);
    assert(work_dir);
    memset(ssc, 0, sizeof(*ssc));
    return 0;
}

int
Builder_SscStage(struct Builder_Ssc* ssc)
{
    assert(ssc);
    return 0;
}

int
Builder_SscPut(
    struct Builder_Ssc* ssc,
    const char* rel_path,
    const char* text,
    size_t length)
{
    assert(ssc);
    assert(rel_path);
    assert(text || length == 0);
    return 0;
}

enum Builder_SscStatus
Builder_SscRun(
    struct Builder_Ssc* ssc,
    enum Builder_SscMode mode,
    struct Builder_SscResult* result)
{
    assert(ssc);
    assert(result);
    (void)mode;
    memset(result, 0, sizeof(*result));
    result->status = BUILDER_SSC_UNSUPPORTED;
    return result->status;
}

#endif
