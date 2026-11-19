/*
 * The pending index (game/rs_pending.h) over bare task headers.
 *
 * Pins: a pending task is found by its kind and key, the newest one when
 * several are listed, never a cancelled one; a key's generation is part of
 * the key; cancel by key and by every scope, counting only what it cancelled;
 * "an earlier task for this key is pending" ignores other keys, other kinds,
 * later tasks and cancelled ones, and holds across the order counter's wrap;
 * many headers in one bucket unlist cleanly in any order.
 *
 * Run: make -C src test-pending
 */

#include "game/rs_pending.h"

#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

enum
{
    KIND_A = RS_PENDING_KINDS_TEST_FIRST,
    KIND_B,
};

static void
test_find_newest_not_cancelled(void)
{
    struct RS_Pending* index = RS_Pending_New();
    struct RS_ParallelTask t1 = { 0 };
    struct RS_ParallelTask t2 = { 0 };
    struct RS_ParallelTask t3 = { 0 };
    struct RS_PendingKey tile = RS_PendingKey_View(0, 10, 20, 0, 2, 7);

    TEST_ASSERT(RS_Pending_Find(index, KIND_A, tile) == NULL, "nothing pending yet");
    RS_Pending_List(index, &t1, KIND_A, tile, 0, 5, 100);
    TEST_ASSERT(t1.listed && !t1.cancelled && t1.join == 0, "listed, live, not waiting");
    TEST_ASSERT(t1.view == 0 && t1.scene_serial == 5 && t1.arrived_cycle == 100,
        "the header carries its view, scene and arrival");
    TEST_ASSERT(RS_Pending_Find(index, KIND_A, tile) == &t1, "found by kind and key");
    TEST_ASSERT(RS_Pending_Find(index, KIND_B, tile) == NULL, "another kind is another task");
    TEST_ASSERT(RS_Pending_Find(index, KIND_A, RS_PendingKey_View(0, 10, 20, 0, 1, 7)) == NULL,
        "another layer is another target");
    TEST_ASSERT(RS_Pending_Find(index, KIND_A, RS_PendingKey_View(0, 10, 20, 0, 2, 8)) == NULL,
        "a later generation of the view is another target");

    RS_Pending_List(index, &t2, KIND_A, tile, 0, 5, 101);
    TEST_ASSERT(RS_Pending_Find(index, KIND_A, tile) == &t2, "the newest of two");
    TEST_ASSERT(RS_Pending_CancelKey(index, KIND_A, tile) == 2, "a newer packet cancels both");
    TEST_ASSERT(RS_Pending_CancelKey(index, KIND_A, tile) == 0, "and a second cancel counts none");
    TEST_ASSERT(RS_Pending_Find(index, KIND_A, tile) == NULL, "a cancelled task is not pending");
    TEST_ASSERT(RS_Pending_Listed(index) == 2 && RS_Pending_Count(index, 0) == 0,
        "still listed until they exit, but not counted");
    RS_Pending_List(index, &t3, KIND_A, tile, 0, 5, 102);
    TEST_ASSERT(RS_Pending_Find(index, KIND_A, tile) == &t3, "the superseding task is found");

    RS_Pending_Unlist(index, &t1);
    RS_Pending_Unlist(index, &t3);
    RS_Pending_Unlist(index, &t2);
    TEST_ASSERT(!t1.listed && !t2.listed && !t3.listed, "unlisted");
    TEST_ASSERT(RS_Pending_Listed(index) == 0, "nothing left");
    TEST_ASSERT(RS_Pending_Find(index, KIND_A, tile) == NULL, "and nothing found");
    RS_Pending_Free(index);
}

static void
test_scopes(void)
{
    struct RS_Pending* index = RS_Pending_New();
    struct RS_ParallelTask scene_old = { 0 }, scene_new = { 0 }, other_view = { 0 };
    struct RS_ParallelTask npc = { 0 }, npc_later_spawn = { 0 }, player = { 0 };
    struct RS_ParallelTask widget = { 0 }, other_group = { 0 }, song = { 0 };

    RS_Pending_List(index, &scene_old, KIND_A, RS_PendingKey_View(0, 1, 1, 0, 0, 1), 0, 5, 0);
    RS_Pending_List(index, &scene_new, KIND_A, RS_PendingKey_View(0, 1, 1, 0, 0, 1), 0, 6, 0);
    RS_Pending_List(index, &other_view, KIND_A, RS_PendingKey_View(3, 1, 1, 0, 0, 1), 3, 5, 0);
    RS_Pending_List(index, &npc, KIND_B, RS_PendingKey_Entity(RS_PENDING_ENTITY_NPC, 12, 1), 0, 6, 0);
    RS_Pending_List(
        index, &npc_later_spawn, KIND_B, RS_PendingKey_Entity(RS_PENDING_ENTITY_NPC, 12, 2), 0, 6, 0);
    RS_Pending_List(
        index, &player, KIND_B, RS_PendingKey_Entity(RS_PENDING_ENTITY_PLAYER, 12, 1), 0, 6, 0);
    RS_Pending_List(index, &widget, KIND_A, RS_PendingKey_Component((149 << 16) | 3, 4), -1, 0, 0);
    RS_Pending_List(
        index, &other_group, KIND_A, RS_PendingKey_Component((150 << 16) | 3, 4), -1, 0, 0);
    RS_Pending_List(index, &song, KIND_B, RS_PendingKey_Id(77, 0), -1, 0, 0);
    TEST_ASSERT(RS_Pending_Count(index, 0) == 9, "nine pending");
    TEST_ASSERT(RS_Pending_Count(index, KIND_B) == 4, "four of kind B");

    TEST_ASSERT(RS_Pending_Cancel(index, RS_PendingScope_Scene(0, 5)) == 1,
        "a rebuild cancels its own view's old scene only");
    TEST_ASSERT(scene_old.cancelled && !scene_new.cancelled && !other_view.cancelled,
        "not the new scene, not another view's");
    TEST_ASSERT(RS_Pending_Cancel(index, RS_PendingScope_Entity(RS_PENDING_ENTITY_NPC, 12)) == 2,
        "a removed npc cancels every spawn serial of its slot");
    TEST_ASSERT(npc.cancelled && npc_later_spawn.cancelled && !player.cancelled,
        "never the player of the same index");
    TEST_ASSERT(RS_Pending_Cancel(index, RS_PendingScope_Group(149)) == 1, "a closed interface");
    TEST_ASSERT(widget.cancelled && !other_group.cancelled, "only its own components");
    TEST_ASSERT(RS_Pending_Cancel(index, RS_PendingScope_View(3)) == 1, "a torn-down view");
    TEST_ASSERT(other_view.cancelled, "every scene of it");
    TEST_ASSERT(RS_Pending_Cancel(index, RS_PendingScope_Kind(KIND_B)) == 2, "a kind: the rest");
    TEST_ASSERT(player.cancelled && song.cancelled, "player and song");
    TEST_ASSERT(RS_Pending_Cancel(index, RS_PendingScope_All()) == 2, "a logout: what is left");
    TEST_ASSERT(scene_new.cancelled && other_group.cancelled, "everything");
    TEST_ASSERT(RS_Pending_Count(index, 0) == 0 && RS_Pending_Listed(index) == 9,
        "nothing pending, all listed until they exit");

    {
        struct RS_ParallelTask* all[] = { &scene_old, &scene_new, &other_view, &npc, &npc_later_spawn,
                                          &player, &widget, &other_group, &song };
        for( size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++ )
            RS_Pending_Unlist(index, all[i]);
    }
    RS_Pending_Free(index);
}

static void
test_earlier_pending(void)
{
    struct RS_Pending* index = RS_Pending_New();
    struct RS_ParallelTask add = { 0 }, del = { 0 }, other_tile = { 0 }, other_kind = { 0 };
    struct RS_PendingKey tile = RS_PendingKey_View(0, 4, 4, 1, 2, 1);

    RS_Pending_List(index, &add, KIND_A, tile, 0, 1, 0);
    RS_Pending_List(index, &other_tile, KIND_A, RS_PendingKey_View(0, 5, 4, 1, 2, 1), 0, 1, 0);
    RS_Pending_List(index, &other_kind, KIND_B, tile, 0, 1, 0);
    RS_Pending_List(index, &del, KIND_A, tile, 0, 1, 0);
    TEST_ASSERT(!RS_Pending_EarlierPending(index, &add), "the first for its key has its turn");
    TEST_ASSERT(RS_Pending_EarlierPending(index, &del), "the second waits for the first");
    TEST_ASSERT(!RS_Pending_EarlierPending(index, &other_tile), "another tile does not wait");
    TEST_ASSERT(!RS_Pending_EarlierPending(index, &other_kind), "another kind does not wait");
    add.cancelled = 1;
    TEST_ASSERT(!RS_Pending_EarlierPending(index, &del), "a cancelled earlier task never applies");
    add.cancelled = 0;
    RS_Pending_Unlist(index, &add);
    TEST_ASSERT(!RS_Pending_EarlierPending(index, &del), "its turn once the first is gone");
    RS_Pending_Unlist(index, &del);
    RS_Pending_Unlist(index, &other_tile);
    RS_Pending_Unlist(index, &other_kind);
    RS_Pending_Free(index);
}

/* The order counter wraps; "earlier" and "newest" are signed differences. */
static void
test_order_wraps(void)
{
    struct RS_Pending* index = RS_Pending_New();
    struct RS_ParallelTask before = { 0 }, after = { 0 };
    struct RS_PendingKey key = RS_PendingKey_Id(1, 0);
    struct RS_ParallelTask filler = { 0 };

    /* Walk the counter to just below the wrap, one listing at a time would
     * take four billion: list one to read the order, then fake it. */
    RS_Pending_List(index, &before, KIND_A, key, -1, 0, 0);
    before.order = 0xFFFFFFFFu;
    RS_Pending_List(index, &after, KIND_A, key, -1, 0, 0);
    after.order = 0x00000001u;
    TEST_ASSERT(RS_Pending_EarlierPending(index, &after), "0xFFFFFFFF is before 1");
    TEST_ASSERT(!RS_Pending_EarlierPending(index, &before), "and 1 is after it");
    TEST_ASSERT(RS_Pending_Find(index, KIND_A, key) == &after, "the newest across the wrap");
    RS_Pending_Unlist(index, &before);
    RS_Pending_Unlist(index, &after);
    RS_Pending_List(index, &filler, KIND_A, key, -1, 0, 0);
    RS_Pending_Unlist(index, &filler);
    RS_Pending_Free(index);
}

/* More headers than buckets: chains share buckets and unlist in any order. */
static void
test_many(void)
{
    enum
    {
        N = 2000
    };
    static struct RS_ParallelTask tasks[N];
    struct RS_Pending* index = RS_Pending_New();
    int ok = 1;

    memset(tasks, 0, sizeof(tasks));
    for( int i = 0; i < N; i++ )
        RS_Pending_List(index, &tasks[i], KIND_A + (i & 1), RS_PendingKey_Id(i / 2, 0), -1, 0, 0);
    for( int i = 0; i < N; i++ )
        if( RS_Pending_Find(index, KIND_A + (i & 1), RS_PendingKey_Id(i / 2, 0)) != &tasks[i] )
            ok = 0;
    TEST_ASSERT(ok, "every one of 2000 found by its kind and key");
    /* Unlist the odd ones, then the evens from the back. */
    for( int i = 1; i < N; i += 2 )
        RS_Pending_Unlist(index, &tasks[i]);
    TEST_ASSERT(RS_Pending_Count(index, KIND_B) == 0 && RS_Pending_Count(index, KIND_A) == N / 2,
        "the odd ones are gone");
    ok = 1;
    for( int i = 0; i < N; i += 2 )
        if( RS_Pending_Find(index, KIND_A, RS_PendingKey_Id(i / 2, 0)) != &tasks[i] )
            ok = 0;
    TEST_ASSERT(ok, "the even ones are still found");
    for( int i = N - 2; i >= 0; i -= 2 )
        RS_Pending_Unlist(index, &tasks[i]);
    TEST_ASSERT(RS_Pending_Listed(index) == 0, "empty");
    RS_Pending_Free(index);
}

int
main(void)
{
    ToriMem_Init();
    test_find_newest_not_cancelled();
    test_scopes();
    test_earlier_pending();
    test_order_wraps();
    test_many();
    if( g_failures )
    {
        printf("rs_pending_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("rs_pending_test: all passed\n");
    ToriMem_Shutdown();
    return 0;
}
