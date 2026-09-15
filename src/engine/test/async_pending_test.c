/*
 * The two "asked for it, waiting for it" lists.
 *
 * Both fail in ways nothing else in the frame notices. A duplicate request
 * queues a second decoder for work already in flight. An overflow drops a
 * request, and the model it belonged to renders untextured forever. A stale
 * entry binds somebody else's animation onto whoever inherited a recycled
 * scene element id -- the wrong creature suddenly playing the wrong sequence.
 *
 * What is asserted:
 *
 *   - the texture list refuses duplicates, and says which of the two reasons
 *     it refused for. `Add` returning false has to mean "not added", whether
 *     that was a duplicate or a full list, because the caller's only job with
 *     that answer is to say so.
 *   - it fills to its ceiling exactly and refuses past it -- not one short,
 *     not one over.
 *   - the seq-bind list does NOT reject duplicates, and that is deliberate:
 *     the same element can be re-bound to a different sequence, or to the same
 *     one at a later cycle, and both entries mean something.
 *   - dropping an element removes EVERY entry for it and preserves the order
 *     of the rest. Order matters because the poll binds the first that lands.
 *   - the compaction both lists use keeps a prefix, which is the shape the
 *     callers' walk-and-keep loops produce.
 *
 * Build and run:
 *   make -C src test-async-pending
 */

#include "engine/async_pending.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_textures_refuse_duplicates_and_overflow(void)
{
    struct AsyncPendingTextures pending;

    memset(&pending, 0, sizeof(pending));

    CHECK(!AsyncPendingTextures_Has(&pending, 7), "an empty list held 7");
    CHECK(AsyncPendingTextures_Add(&pending, 7), "the first add was refused");
    CHECK(AsyncPendingTextures_Has(&pending, 7), "7 was added but is not held");
    CHECK(pending.count == 1, "count is %d after one add", pending.count);

    /* The rebuild case: the same id asked for again while the first request is
     * still in flight. */
    CHECK(!AsyncPendingTextures_Add(&pending, 7), "a duplicate was accepted");
    CHECK(pending.count == 1, "a duplicate grew the list to %d", pending.count);

    /* Id 0 is a real texture id, and so is a negative one as far as this list
     * is concerned -- it stores what it is given and lets the caller judge. */
    CHECK(AsyncPendingTextures_Add(&pending, 0), "id 0 was refused");
    CHECK(AsyncPendingTextures_Has(&pending, 0), "id 0 was added but is not held");
    CHECK(!AsyncPendingTextures_Has(&pending, 1), "an unadded neighbour is held");

    /* Fill to the ceiling exactly, then one past it. */
    memset(&pending, 0, sizeof(pending));
    for( int i = 0; i < ASYNC_PENDING_TEXTURE_MAX; i++ )
        CHECK(AsyncPendingTextures_Add(&pending, i), "add %d of the ceiling was refused", i);
    CHECK(pending.count == ASYNC_PENDING_TEXTURE_MAX, "count is %d at the ceiling", pending.count);
    CHECK(
        !AsyncPendingTextures_Add(&pending, ASYNC_PENDING_TEXTURE_MAX),
        "one past the ceiling was accepted");
    CHECK(
        pending.count == ASYNC_PENDING_TEXTURE_MAX,
        "a refused add changed the count to %d",
        pending.count);

    /* Compaction: the callers walk and keep a prefix. */
    AsyncPendingTextures_Keep(&pending, 3);
    CHECK(pending.count == 3, "keep(3) left %d", pending.count);
    CHECK(AsyncPendingTextures_Has(&pending, 2), "keep(3) lost the third entry");
    CHECK(!AsyncPendingTextures_Has(&pending, 3), "keep(3) kept a fourth entry");
    AsyncPendingTextures_Keep(&pending, 0);
    CHECK(pending.count == 0, "keep(0) left %d", pending.count);
}

static void
test_seq_binds_keep_duplicates_but_drop_dead_elements(void)
{
    struct AsyncPendingSeqBinds pending;

    memset(&pending, 0, sizeof(pending));

    CHECK(AsyncPendingSeqBinds_Add(&pending, 10, 100, 5), "the first bind was refused");
    CHECK(pending.count == 1, "count is %d after one bind", pending.count);
    CHECK(pending.items[0].element_id == 10, "element id was not stored");
    CHECK(pending.items[0].seq_id == 100, "seq id was not stored");
    CHECK(pending.items[0].start_cycle == 5, "start cycle was not stored");

    /*
     * Deliberately NOT deduplicated, unlike the texture list. The same element
     * re-bound to a different sequence is a real second entry, and re-bound to
     * the SAME sequence at a later cycle is too -- the cycle is what stops an
     * async load resetting a DynamicObject's clock, so two cycles are two
     * different requests.
     */
    CHECK(AsyncPendingSeqBinds_Add(&pending, 10, 200, 6), "a second seq for one element was refused");
    CHECK(AsyncPendingSeqBinds_Add(&pending, 10, 100, 9), "the same seq at a later cycle was refused");
    CHECK(pending.count == 3, "count is %d after three binds", pending.count);

    /* The recycled-id case. Every entry for the dying element goes, and the
     * order of what is left is preserved -- the poll binds the first that
     * lands, so reordering changes which sequence wins. */
    CHECK(AsyncPendingSeqBinds_Add(&pending, 11, 300, 7), "a bind for a second element was refused");
    CHECK(AsyncPendingSeqBinds_Add(&pending, 12, 400, 8), "a bind for a third element was refused");
    AsyncPendingSeqBinds_DropElement(&pending, 10);
    CHECK(pending.count == 2, "dropping element 10 left %d entries", pending.count);
    CHECK(pending.items[0].element_id == 11, "the surviving order changed");
    CHECK(pending.items[1].element_id == 12, "the surviving order changed");

    /* Dropping an element with nothing pending is ordinary: every despawn
     * calls this, and almost none of them have a bind waiting. */
    AsyncPendingSeqBinds_DropElement(&pending, 999);
    CHECK(pending.count == 2, "dropping an absent element changed the count");

    /* The ceiling, and compaction. */
    memset(&pending, 0, sizeof(pending));
    for( int i = 0; i < ASYNC_PENDING_SEQ_BIND_MAX; i++ )
        CHECK(AsyncPendingSeqBinds_Add(&pending, i, i, 0), "bind %d of the ceiling was refused", i);
    CHECK(
        !AsyncPendingSeqBinds_Add(&pending, 9999, 1, 0),
        "one past the seq-bind ceiling was accepted");
    CHECK(
        pending.count == ASYNC_PENDING_SEQ_BIND_MAX,
        "a refused bind changed the count to %d",
        pending.count);

    AsyncPendingSeqBinds_Keep(&pending, 2);
    CHECK(pending.count == 2, "keep(2) left %d", pending.count);
    CHECK(pending.items[1].element_id == 1, "keep(2) did not keep the first two");
}

int
main(void)
{
    test_textures_refuse_duplicates_and_overflow();
    test_seq_binds_keep_duplicates_but_drop_dead_elements();

    if( g_failures )
    {
        printf("async_pending_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("async_pending_test: OK\n");
    return 0;
}
