#include "cp_walk.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int
push(struct CP_Walk* walk, const char* path, int rank)
{
    struct CP_WalkFile* slot;
    const char* base;
    const char* dot;

    if( walk->count == walk->capacity )
    {
        int next = walk->capacity ? walk->capacity * 2 : 64;
        struct CP_WalkFile* grown =
            (struct CP_WalkFile*)realloc(walk->files, (size_t)next * sizeof(*grown));

        if( !grown )
            return 0;
        walk->files = grown;
        walk->capacity = next;
    }
    slot = &walk->files[walk->count++];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->path, sizeof(slot->path), "%s", path);
    slot->rank = rank;

    /*
     * The extension is what follows the *final* dot of the basename. Searching the
     * whole path would let a directory called `worldmap/areas` donate an extension
     * to a file that has none.
     */
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    dot = strrchr(base, '.');
    if( dot && dot[1] )
        snprintf(slot->ext, sizeof(slot->ext), "%s", dot + 1);
    return 1;
}

static void
walk_dir(struct CP_Walk* walk, const char* dir, int rank)
{
    DIR* handle = opendir(dir);
    struct dirent* entry;

    if( !handle )
        return;
    while( (entry = readdir(handle)) != NULL )
    {
        char path[CP_WALK_PATH];
        struct stat info;
        int is_dir = -1;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        /* d_type when the platform has it: a stat() per entry is what made the
         * walk pay for every selftest screenshot under server/scripts. */
#ifdef DT_DIR
        if( entry->d_type == DT_DIR )
            is_dir = 1;
        else if( entry->d_type == DT_REG )
            is_dir = 0;
#endif
        if( is_dir < 0 )
        {
            if( stat(path, &info) != 0 )
                continue;
            is_dir = S_ISDIR(info.st_mode) ? 1 : 0;
        }
        if( is_dir )
            walk_dir(walk, path, rank);
        else
            push(walk, path, rank);
    }
    closedir(handle);
}

int
cp_walk_tree(
    struct CP_Walk* walk,
    const char* srcdir,
    const char* const* roots,
    const int* ranks,
    int root_count)
{
    memset(walk, 0, sizeof(*walk));
    for( int i = 0; i < root_count && i < CP_WALK_MAX_ROOTS; i++ )
    {
        char dir[CP_WALK_PATH];

        snprintf(dir, sizeof(dir), "%s/%s", srcdir, roots[i]);
        /* A missing root is not an error: a tree with no `server/` is a normal
         * tree, and so is one exported without configs. */
        walk_dir(walk, dir, ranks[i]);
    }
    return walk->count;
}

static const struct CP_Walk* g_compare_walk;

static int
compare(const void* a, const void* b)
{
    const struct CP_WalkFile* x = &g_compare_walk->files[*(const int*)a];
    const struct CP_WalkFile* y = &g_compare_walk->files[*(const int*)b];

    if( x->rank != y->rank )
        return x->rank - y->rank;
    return strcmp(x->path, y->path);
}

int
cp_walk_find(
    const struct CP_Walk* walk,
    const char* ext,
    const char** out_paths,
    int out_capacity)
{
    /* The order is a cache over a walk that never changes after cp_walk_tree,
     * so building it is not a change a caller can see; hence the cast. */
    struct CP_Walk* cache = (struct CP_Walk*)walk;
    int matched = 0;

    if( walk->count <= 0 )
        return 0;
    /*
     * Rank first, then path: the order a merge depends on. Sorted once per walk
     * now; it was sorted per call, over a copy of every entry, and each match
     * then searched the whole walk for its own path again — a few seconds per
     * server pack once server/scripts held 54,000 files.
     */
    if( !walk->ordered )
    {
        cache->order = (int*)malloc((size_t)walk->count * sizeof(int));
        if( !cache->order )
            return 0;
        for( int i = 0; i < walk->count; i++ )
            cache->order[i] = i;
        g_compare_walk = walk;
        qsort(cache->order, (size_t)walk->count, sizeof(int), compare);
        g_compare_walk = NULL;
        cache->ordered = 1;
    }
    for( int i = 0; i < walk->count && matched < out_capacity; i++ )
    {
        const struct CP_WalkFile* file = &walk->files[walk->order[i]];

        if( strcmp(file->ext, ext) == 0 )
            out_paths[matched++] = file->path;
    }
    return matched;
}

void
cp_walk_free(struct CP_Walk* walk)
{
    free(walk->files);
    free(walk->order);
    memset(walk, 0, sizeof(*walk));
}
