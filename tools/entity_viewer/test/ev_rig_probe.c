/*
 * Does a cache with no catalog produce animation lists, and does switching
 * caches produce the RIGHT ones?
 *
 * The bug this exists for had no error attached: rig matching came only from a
 * prebuilt catalog, so rs634 and rs727 — which have none — listed zero
 * animations for every npc, and every cache switch did the same to the caches
 * that did have one. "Zero rig matches" is indistinguishable from "this cache's
 * npcs genuinely share no rigs" by looking at the page, so it is checked here
 * as a number.
 *
 * The second half is the one that needs a probe rather than a glance: the walk
 * runs on a worker thread, and a second cache switch has to abandon the first
 * walk rather than let it publish. A stale publish is silent and looks exactly
 * like a correct one — a full npc list, sensible counts, all describing the
 * cache you just left.
 *
 *   ev_rig_probe <cache_dir> <rev> [<other_cache_dir> <other_rev>]
 */
#include "ev_rigs.h"

#include "asset_access.h"
#include "tool_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static int fails = 0;

static void
check(const char* what, int ok)
{
    if( !ok )
        fails++;
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
}

static double
now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/**
 * Poll until a walk publishes something, or until it fails.
 *
 * `complete` selects which publish: 0 takes the sequence half as soon as it
 * lands, 1 waits for the npc pass behind it.
 */
static const struct EV_RigIndex*
wait_for(int complete, struct EV_RigStatus* status)
{
    double t0 = now_ms();
    while( now_ms() - t0 < 120000 )
    {
        ev_rigs_collect();
        const struct EV_RigIndex* index = ev_rigs_current();
        if( index && (!complete || index->npcs_complete) )
            return index;
        ev_rigs_status(status);
        if( status->state == EV_RIG_FAILED )
            return NULL;
        struct timespec nap = { 0, 5 * 1000 * 1000 };
        nanosleep(&nap, NULL);
    }
    return NULL;
}

static struct EV_RigIndex*
build(const char* dir, const char* rev)
{
    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) )
        return NULL;
    if( !tool_dat2_open(dir, &profile, &cache) )
        return NULL;
    struct EV_RigIndex* index = ev_rigs_build(&cache, NULL, NULL);
    tool_dat2_close(&cache);
    return index;
}

/** Sequences on this npc's rigs, counted the way ev_server lists them. */
static int
listed(const struct EV_RigIndex* index, const struct EV_RigNpc* npc)
{
    int n = 0;
    for( int i = 0; i < index->seq_count; i++ )
        for( int k = 0; k < npc->framemap_count; k++ )
            if( index->seqs[i].framemap_id == npc->framemaps[k] )
            {
                n++;
                break;
            }
    return n;
}

static void
report(const char* label, const struct EV_RigIndex* index)
{
    int with_rig = 0;
    int with_seqs = 0;
    int most = 0;
    int most_npc = -1;
    for( int i = 0; i < index->npc_count; i++ )
    {
        if( index->npcs[i].framemap_count > 0 )
            with_rig++;
        if( index->npcs[i].seq_count > 0 )
            with_seqs++;
        if( index->npcs[i].seq_count > most )
        {
            most = index->npcs[i].seq_count;
            most_npc = index->npcs[i].npc_id;
        }
    }
    printf(
        "  %s: %d seqs, %d distinct rigs (%d ids unified), %d npcs, "
        "%d with a rig, %d with matches, most is npc %d with %d [%d ms]\n",
        label,
        index->seq_count,
        index->distinct_rigs,
        index->alias_ids,
        index->npc_count,
        with_rig,
        with_seqs,
        most_npc,
        most,
        index->build_ms);

    check("the cache has sequences", index->seq_count > 0);
    check("the cache has npcs", index->npc_count > 0);
    /* The symptom, as a number. Half is a deliberately loose floor: some npcs
     * genuinely name no animation at all. Zero is the failure. */
    check("most npcs reach a rig", with_rig * 2 > index->npc_count);
    check("npcs with a rig have matching sequences", with_rig == with_seqs);

    /* The precomputed count and the list the server actually emits have to
     * agree: the badge on the npc row comes from one and the rows in the panel
     * from the other, and a mismatch reads as animations that vanish when you
     * click. */
    int checked = 0;
    int agreed = 0;
    for( int i = 0; i < index->npc_count && checked < 25; i++ )
    {
        if( index->npcs[i].framemap_count == 0 )
            continue;
        checked++;
        if( listed(index, &index->npcs[i]) == index->npcs[i].seq_count )
            agreed++;
    }
    check("the precomputed count equals the emitted list", checked > 0 && agreed == checked);
}

int
main(int argc, char** argv)
{
    if( argc < 3 )
    {
        fprintf(stderr, "usage: %s <cache_dir> <rev> [<other_cache_dir> <other_rev>]\n", argv[0]);
        return 2;
    }
    const char* dir = argv[1];
    const char* rev = argv[2];
    const char* other_dir = argc > 4 ? argv[3] : NULL;
    const char* other_rev = argc > 4 ? argv[4] : NULL;

    printf("== %s as %s\n", dir, rev);
    struct EV_RigIndex* direct = build(dir, rev);
    if( !direct )
    {
        printf("  FAIL: could not walk %s as %s\n", dir, rev);
        return 1;
    }
    report("walk", direct);

    /* The background half. */
    printf("== background walk\n");
    ev_rigs_start(dir, rev);
    check("nothing is current while the sequence sweep runs", ev_rigs_current() == NULL);

    struct EV_RigStatus status;
    const struct EV_RigIndex* async = wait_for(0, &status);
    check("the sequence half published before the npc pass finished", async != NULL);
    if( async )
    {
        check("it found the same sequences as the direct walk",
              async->seq_count == direct->seq_count);

        /*
         * The point of publishing early: one npc can be answered from the
         * sequence half alone. If this needed the npc pass, rs727 would show no
         * animations for the first nine seconds after every cache switch.
         */
        struct RSCache profile;
        struct Tool_Dat2Cache cache;
        int opened = tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) &&
                     tool_dat2_open(dir, &profile, &cache);
        check("the cache reopens for the on-demand path", opened);
        if( opened )
        {
            /* An npc the direct walk found rigs for, asked of the partial. */
            const struct EV_RigNpc* want = NULL;
            for( int i = 0; i < direct->npc_count; i++ )
                if( direct->npcs[i].seq_count > 0 )
                {
                    want = &direct->npcs[i];
                    break;
                }
            struct EV_RigNpc got;
            check("an npc resolves before the npc pass has run",
                  want && ev_rigs_npc_lookup(async, &cache, want->npc_id, &got));
            if( want )
                check("on demand agrees with the batch pass",
                      got.framemap_count == want->framemap_count &&
                          got.seq_count == want->seq_count &&
                          got.skeletal_count == want->skeletal_count);
            tool_dat2_close(&cache);
        }
    }

    async = wait_for(1, &status);
    check("the npc pass published", async != NULL && async->npcs_complete);
    if( async )
    {
        check("it found the same npcs as the direct walk",
              async->npc_count == direct->npc_count);
        ev_rigs_status(&status);
        check("the status says ready", status.state == EV_RIG_READY);
    }

    /*
     * Switching mid-walk. The first walk must not publish, however far along it
     * is — its npc ids mean something else in the cache that is now open.
     */
    if( other_dir )
    {
        printf("== switch from %s to %s mid-walk\n", dir, other_dir);
        struct EV_RigIndex* other_direct = build(other_dir, other_rev);
        if( !other_direct )
        {
            printf("  FAIL: could not walk %s as %s\n", other_dir, other_rev);
            fails++;
        }
        else
        {
            check("the two caches differ in size (so a swap is visible)",
                  other_direct->seq_count != direct->seq_count ||
                      other_direct->npc_count != direct->npc_count);

            ev_rigs_start(dir, rev);
            ev_rigs_start(other_dir, other_rev);
            check("the abandoned walk's index is gone", ev_rigs_current() == NULL);

            const struct EV_RigIndex* got = wait_for(1, &status);
            check("a walk published", got != NULL);
            if( got )
            {
                check("it is the SECOND cache's walk, not the abandoned one",
                      got->seq_count == other_direct->seq_count &&
                          got->npc_count == other_direct->npc_count);
            }

            /* And the abandoned walk must not arrive late and overwrite it. */
            struct timespec nap = { 2, 0 };
            nanosleep(&nap, NULL);
            ev_rigs_collect();
            const struct EV_RigIndex* after = ev_rigs_current();
            check("nothing replaced it afterwards",
                  after && after->seq_count == other_direct->seq_count &&
                      after->npc_count == other_direct->npc_count);

            ev_rigs_free(other_direct);
        }
    }

    ev_rigs_free(direct);
    printf("%s\n", fails ? "FAILURES" : "all rig checks pass");
    return fails ? 1 : 0;
}
