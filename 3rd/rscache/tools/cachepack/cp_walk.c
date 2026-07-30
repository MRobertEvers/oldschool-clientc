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

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if( stat(path, &info) != 0 )
            continue;
        if( S_ISDIR(info.st_mode) )
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

static int
compare(const void* a, const void* b)
{
    const struct CP_WalkFile* x = (const struct CP_WalkFile*)a;
    const struct CP_WalkFile* y = (const struct CP_WalkFile*)b;

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
    struct CP_WalkFile* ordered;
    int matched = 0;

    if( walk->count <= 0 )
        return 0;
    /*
     * Sorted per call rather than once, because the set is tiny (104 files across
     * the two roots that use this) and a sort here cannot be forgotten by a caller
     * that adds a root later. Rank first, then path: the order a merge depends on.
     */
    ordered = (struct CP_WalkFile*)malloc((size_t)walk->count * sizeof(*ordered));
    if( !ordered )
        return 0;
    memcpy(ordered, walk->files, (size_t)walk->count * sizeof(*ordered));
    qsort(ordered, (size_t)walk->count, sizeof(*ordered), compare);

    for( int i = 0; i < walk->count && matched < out_capacity; i++ )
    {
        if( strcmp(ordered[i].ext, ext) != 0 )
            continue;
        /* Borrowed from `walk`, not from `ordered`, which is about to go away. */
        for( int j = 0; j < walk->count; j++ )
        {
            if( strcmp(walk->files[j].path, ordered[i].path) == 0 )
            {
                out_paths[matched++] = walk->files[j].path;
                break;
            }
        }
    }
    free(ordered);
    return matched;
}

void
cp_walk_free(struct CP_Walk* walk)
{
    free(walk->files);
    memset(walk, 0, sizeof(*walk));
}
