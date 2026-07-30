#ifndef RSCACHE_TOOLS_CACHEPACK_CP_WALK_H
#define RSCACHE_TOOLS_CACHEPACK_CP_WALK_H

/*
 * Finding content by extension, the way LostCity's packer does
 * (`engine/tools/pack/config/PackShared.ts:89` `readDirTree`, then `findFiles`
 * per extension).
 *
 * **Declared roots, not the whole tree.** LostCity walks one root because its
 * content tree is small. Ours is not: models 61,615 files, sprites 20,266, maps
 * 17,599, scripts 12,654 — about 112,000 in total, and none of them is found this
 * way. Assets are addressed through their pack file and stay that way. The roots
 * that matter here are tiny by comparison: `configs/` is 40 files and
 * `server/scripts/` is 64.
 *
 * **Rank is the layer.** A root carries a rank, and a later rank overlays an
 * earlier one. `configs/` (rank 0) is the machine export; `server/scripts/`
 * (rank 1) is what a person authored on top of it. Ordering by rank, then by path,
 * is what makes a merge reproducible — never `readdir` order, which is the lesson
 * `cp_assets.h` already records for archive members.
 *
 * **Matching is last-dot equality, not a suffix test.** `all.npc` is a `.npc` file;
 * `all.npc.compack` is a `.compack` file and not a `.npc` one. A suffix test
 * happens to give the same answers for every name in this tree today — that was
 * checked, not assumed — but it says something weaker than what is meant, and the
 * coincidence is not worth depending on.
 */

#include <stdbool.h>

enum
{
    CP_WALK_MAX_ROOTS = 4,
    /** Longest path the walk will record. */
    CP_WALK_PATH = 1024,
};

struct CP_WalkFile
{
    char path[CP_WALK_PATH];
    /** Extension after the final `.`, without the dot. Empty when there is none. */
    char ext[32];
    /** Which root found it; lower is more machine-owned. */
    int rank;
};

struct CP_Walk
{
    struct CP_WalkFile* files;
    int count;
    int capacity;
};

/**
 * Walk `<srcdir>/<dir>` for each declared root, recursively.
 *
 * Returns the number of files recorded. A root that does not exist is skipped
 * silently: a tree that carries no `server/` is a normal tree.
 */
int
cp_walk_tree(
    struct CP_Walk* walk,
    const char* srcdir,
    const char* const* roots,
    const int* ranks,
    int root_count);

/**
 * Paths whose extension equals `ext`, in rank then path order.
 *
 * `out_paths` is filled with borrowed pointers into `walk`; the caller does not
 * free them. Returns how many matched.
 */
int
cp_walk_find(
    const struct CP_Walk* walk,
    const char* ext,
    const char** out_paths,
    int out_capacity);

void
cp_walk_free(struct CP_Walk* walk);

#endif
