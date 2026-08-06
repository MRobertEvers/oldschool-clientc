/*
 * tool_prefetch.h -- warm a directory tree in parallel, before something walks
 * it one file at a time.
 *
 * WHY THIS EXISTS, measured rather than assumed:
 *
 * Packing the asset half reads ~111,000 small files, one per archive, and each
 * FIRST touch of a file costs ~12ms on a machine whose NVMe answers a small
 * read in about 0.05ms. The second touch of the same file costs 0.07ms. That
 * signature -- a large fixed cost paid once per file, independent of size -- is
 * a filesystem filter, on Windows typically the on-access virus scan. It is
 * latency, not bandwidth or CPU, so it does not shrink with a faster disk and
 * it does not show up in a profile as time spent in this program.
 *
 * It does, however, overlap almost perfectly. Reading the same files 16 at a
 * time measured 14.7x faster than reading them one at a time (15.96ms/file ->
 * 1.08ms/file). So the fix is not to make the packer's loop concurrent -- that
 * loop appends to one .dat2 and must stay ordered and single-threaded -- but to
 * pay the per-file latency ahead of it, in parallel, and let the loop find
 * every file already warm.
 *
 * This deliberately does nothing with the bytes. It is not a cache and it never
 * hands data back: it reads and discards, so the only thing it leaves behind is
 * the operating system's page cache and the scanner's verdict for that file.
 * That makes it safe to run against any tree, in any order, and correct to skip
 * entirely -- a build with prefetching disabled produces the identical cache,
 * just slower.
 *
 * Portability: POSIX threads throughout. macOS and Linux have them natively,
 * and the MinGW toolchains this repo vendors are built `--enable-threads=posix`
 * (winpthread), so there is one code path and no #ifdef. Link with -lpthread.
 */
#ifndef RSCACHE_TOOLS_PREFETCH_H
#define RSCACHE_TOOLS_PREFETCH_H

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef TOOL_PREFETCH_WORKERS
#define TOOL_PREFETCH_WORKERS 16
#endif

#ifndef TOOL_PREFETCH_MAX_PATHS
/* A ceiling rather than a target. `models` is the largest kind at ~61k files;
 * this is sized so no kind in the tree is truncated, and the array costs one
 * pointer per file. */
#define TOOL_PREFETCH_MAX_PATHS 200000
#endif

struct Tool_Prefetch
{
    char** paths;
    int count;
    int next; /* shared cursor, taken under `lock` */
    pthread_mutex_t lock;
    pthread_t workers[TOOL_PREFETCH_WORKERS];
    int worker_count;
    int started;
};

static void
tool_prefetch_collect(struct Tool_Prefetch* pf, const char* dir, int depth)
{
    DIR* handle;
    struct dirent* entry;

    /* The asset trees nest a couple of levels (models/npc/<name>.model); the
     * bound is only here so a symlink loop cannot spin forever. */
    if( depth > 8 || pf->count >= TOOL_PREFETCH_MAX_PATHS )
        return;

    handle = opendir(dir);
    if( !handle )
        return;

    while( (entry = readdir(handle)) != NULL )
    {
        char path[1600];
        struct stat st;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if( stat(path, &st) != 0 )
            continue;

        if( (st.st_mode & S_IFDIR) != 0 )
        {
            tool_prefetch_collect(pf, path, depth + 1);
            continue;
        }
        if( pf->count >= TOOL_PREFETCH_MAX_PATHS )
            break;
        pf->paths[pf->count] = strdup(path);
        if( pf->paths[pf->count] )
            pf->count++;
    }
    closedir(handle);
}

static void*
tool_prefetch_worker(void* arg)
{
    struct Tool_Prefetch* pf = (struct Tool_Prefetch*)arg;
    /* One page is enough: the point is to touch the file, not to hold it. The
     * scanner reads the whole file itself regardless of how much we ask for. */
    char scratch[4096];

    for( ;; )
    {
        int at;
        FILE* f;

        pthread_mutex_lock(&pf->lock);
        at = pf->next < pf->count ? pf->next++ : -1;
        pthread_mutex_unlock(&pf->lock);
        if( at < 0 )
            break;

        f = fopen(pf->paths[at], "rb");
        if( !f )
            continue;
        while( fread(scratch, 1, sizeof(scratch), f) == sizeof(scratch) )
            ;
        fclose(f);
    }
    return NULL;
}

/*
 * Start warming `dir` in the background and return immediately.
 *
 * The caller keeps working while this runs; it is a race the caller always
 * wins correctness-wise, because losing it just means paying the cold cost for
 * that file as before.
 */
static void
tool_prefetch_begin(struct Tool_Prefetch* pf, const char* dir)
{
    memset(pf, 0, sizeof(*pf));
    if( !dir )
        return;

    pf->paths = (char**)calloc(TOOL_PREFETCH_MAX_PATHS, sizeof(*pf->paths));
    if( !pf->paths )
        return;

    tool_prefetch_collect(pf, dir, 0);
    if( pf->count == 0 )
    {
        free(pf->paths);
        pf->paths = NULL;
        return;
    }

    if( pthread_mutex_init(&pf->lock, NULL) != 0 )
    {
        for( int i = 0; i < pf->count; i++ )
            free(pf->paths[i]);
        free(pf->paths);
        pf->paths = NULL;
        pf->count = 0;
        return;
    }

    pf->started = 1;
    for( int i = 0; i < TOOL_PREFETCH_WORKERS; i++ )
    {
        if( pthread_create(&pf->workers[i], NULL, tool_prefetch_worker, pf) != 0 )
            break; /* fewer workers is slower, not wrong */
        pf->worker_count++;
    }
}

/** Join the workers and release everything. Safe on a prefetch that never
 *  started, so the caller needs no matching condition. */
static void
tool_prefetch_end(struct Tool_Prefetch* pf)
{
    if( !pf->started )
    {
        free(pf->paths);
        memset(pf, 0, sizeof(*pf));
        return;
    }
    for( int i = 0; i < pf->worker_count; i++ )
        pthread_join(pf->workers[i], NULL);
    pthread_mutex_destroy(&pf->lock);
    for( int i = 0; i < pf->count; i++ )
        free(pf->paths[i]);
    free(pf->paths);
    memset(pf, 0, sizeof(*pf));
}

#endif /* RSCACHE_TOOLS_PREFETCH_H */
