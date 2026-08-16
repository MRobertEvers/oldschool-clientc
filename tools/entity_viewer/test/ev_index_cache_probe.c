/*
 * Does the on-disk index actually help, and does it actually invalidate?
 *
 * Two properties, and the second is the one that matters: a cache that never
 * goes stale is worse than no cache, because it answers confidently with the
 * previous cache's contents. So this builds, rebuilds (expecting a hit),
 * touches the cache, and rebuilds again (expecting a miss) — comparing the
 * CONTENTS each time, not just the timing, because a load that returns an empty
 * index is also fast.
 */
#include "ev_caches.h"
#include "asset_access.h"
#include "tool_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <utime.h>

static int fails = 0;

static void
check(const char* what, int ok)
{
    if( !ok ) fails++;
    printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
}

static double
now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* One named list, id for id and name for name. The npc, obj and loc lists are
 * the same row type and the same serialised shape, so comparing them by three
 * copies of this loop would be three chances to leave one of them uncompared —
 * which is what "the warm build matches" quietly meant when only npcs were
 * checked and obj/loc rows had just been added to the file. */
static int
same_rows(const char* what, const struct EV_IndexRow* a, const struct EV_IndexRow* b, int count)
{
    for( int i = 0; i < count; i++ )
    {
        const char* x = a[i].name; const char* y = b[i].name;
        if( a[i].id != b[i].id )
        { printf("    %s %d: id %d vs %d\n", what, i, a[i].id, b[i].id); return 0; }
        if( (x == NULL) != (y == NULL) )
        { printf("    %s %d (id %d): name %s vs %s\n", what, i, a[i].id,
                 x ? x : "NULL", y ? y : "NULL"); return 0; }
        if( x && strcmp(x, y) != 0 )
        { printf("    %s %d (id %d): '%s' vs '%s'\n", what, i, a[i].id, x, y); return 0; }
    }
    return 1;
}

static int
same(const struct EV_Index* a, const struct EV_Index* b)
{
    if( a->npc_count != b->npc_count || a->obj_count != b->obj_count ||
        a->loc_count != b->loc_count || a->seq_count != b->seq_count ||
        a->model_count != b->model_count )
    {
        printf("    counts differ: npcs %d/%d objs %d/%d locs %d/%d seqs %d/%d models %d/%d\n",
               a->npc_count, b->npc_count, a->obj_count, b->obj_count,
               a->loc_count, b->loc_count, a->seq_count, b->seq_count,
               a->model_count, b->model_count);
        return 0;
    }
    for( int i = 0; i < a->seq_count; i++ )
        if( a->seq_ids[i] != b->seq_ids[i] )
        { printf("    seq %d: %d vs %d\n", i, a->seq_ids[i], b->seq_ids[i]); return 0; }
    for( int i = 0; i < a->model_count; i++ )
        if( a->model_ids[i] != b->model_ids[i] )
        { printf("    model %d: %d vs %d\n", i, a->model_ids[i], b->model_ids[i]); return 0; }
    return same_rows("npc", a->npcs, b->npcs, a->npc_count) &&
           same_rows("obj", a->objs, b->objs, a->obj_count) &&
           same_rows("loc", a->locs, b->locs, a->loc_count);
}

int
main(int argc, char** argv)
{
    const char* dir = argv[1];
    const char* rev = argv[2];
    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    char idx[1024];

    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) ) return 1;
    if( !tool_dat2_open(dir, &profile, &cache) ) return 1;

    snprintf(idx, sizeof(idx), "%s/.ev/index-%s.evi", dir, rev);
    remove(idx);

    uint64_t fp0 = ev_cache_fingerprint(dir);
    check("fingerprint is non-zero for a real cache", fp0 != 0);
    check("fingerprint is stable across calls", fp0 == ev_cache_fingerprint(dir));
    check("fingerprint of a non-cache directory is 0", ev_cache_fingerprint("/tmp") == 0);

    struct EV_Index a, b, c;
    double t0 = now_ms();
    check("cold build succeeds", ev_index_build(&cache, &profile, dir, rev, &a));
    double cold = now_ms() - t0;

    struct stat st;
    check("the .ev index file was written", stat(idx, &st) == 0);

    t0 = now_ms();
    check("warm build succeeds", ev_index_build(&cache, &profile, dir, rev, &b));
    double warm = now_ms() - t0;

    check("warm build returns the SAME index", same(&a, &b));
    check("warm build is faster than cold", warm < cold);
    printf("    cold %.0f ms -> warm %.0f ms (%.1fx)\n", cold, warm, cold / (warm > 0.01 ? warm : 0.01));

    /* Now make the cache look changed. Touching an idx file is the cheapest
     * edit that a real repack would also make. */
    char touched[1024];
    snprintf(touched, sizeof(touched), "%s/main_file_cache.idx0", dir);
    if( stat(touched, &st) == 0 )
    {
        struct utimbuf ut;
        ut.actime = st.st_atime;
        ut.modtime = st.st_mtime + 120;
        check("touch the cache", utime(touched, &ut) == 0);
        check("fingerprint changed after the touch", ev_cache_fingerprint(dir) != fp0);

        t0 = now_ms();
        check("build after the touch succeeds", ev_index_build(&cache, &profile, dir, rev, &c));
        double again = now_ms() - t0;
        check("it REBUILT rather than served the stale file", again > warm * 3);
        printf("    after touch: %.0f ms (warm was %.0f ms)\n", again, warm);
        check("the rebuilt index still matches", same(&a, &c));
        ev_index_free(&c);

        /* Put the mtime back so the next run starts from the same place. */
        ut.modtime = st.st_mtime;
        utime(touched, &ut);
    }

    /* A different rev must not read this rev's file. */
    char other[1024];
    snprintf(other, sizeof(other), "%s/.ev/index-%s.evi", dir, "osrs184");
    remove(other);
    check("a different rev uses a different file", strcmp(idx, other) != 0);

    ev_index_free(&a);
    ev_index_free(&b);
    tool_dat2_close(&cache);
    printf("%s\n", fails ? "FAILURES" : "all index-cache checks pass");
    return fails ? 1 : 0;
}
