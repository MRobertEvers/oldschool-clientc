/*
 * The ground-items overlay's dirty set.
 *
 * The overlay is rebuilt per tile by a clientscript, and the list is drained
 * once per tick rather than fired from inside the zone executor -- one OBJ_ADD
 * burst can touch the same tile several times, and a script per packet rebuilds
 * the same overlay three times over.
 *
 * What is asserted, and the last two are the interesting ones:
 *
 *   - dedup, because that is the reason the list exists at all.
 *   - the two ways in are NOT the same. `Mark` overflowing PROMOTES to a
 *     whole-scene walk, because from that many tiles the walk is cheaper.
 *     `Append`, which is what the walk itself uses, overflowing merely STOPS
 *     -- promoting there would restart the walk that is already running, and a
 *     scene with more piles than the list holds would never finish one.
 *   - `Mark` is a no-op while a whole-scene walk is pending, so a burst does
 *     not refill a list that is about to be replaced wholesale.
 *   - the refresh flag is taken and cleared BEFORE the walk, so a mark arriving
 *     during the walk is recorded normally rather than swallowed by a flag that
 *     is about to be reset.
 *
 * Build and run:
 *   make -C src test-ground-items-dirty
 */

#include "game/rs_ground_items_dirty.h"

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

static bool
listed(
    struct RS_GroundItemsDirty const* dirty,
    int coord)
{
    for( int i = 0; i < dirty->count; i++ )
        if( dirty->coords[i] == coord )
            return true;
    return false;
}

static void
test_marking_dedups(void)
{
    struct RS_GroundItemsDirty dirty;

    memset(&dirty, 0, sizeof(dirty));

    RS_GroundItemsDirty_Mark(&dirty, 1000);
    RS_GroundItemsDirty_Mark(&dirty, 1000);
    RS_GroundItemsDirty_Mark(&dirty, 1001);
    CHECK(dirty.count == 2, "count is %d after two distinct tiles and a repeat", dirty.count);
    CHECK(listed(&dirty, 1000) && listed(&dirty, 1001), "a marked tile is missing");
    CHECK(!dirty.refresh_all, "two tiles asked for a whole-scene walk");

    RS_GroundItemsDirty_Clear(&dirty);
    CHECK(dirty.count == 0, "clear left %d", dirty.count);
    CHECK(!dirty.refresh_all, "clear set the refresh flag");
}

static void
test_the_two_overflows_differ(void)
{
    struct RS_GroundItemsDirty dirty;

    /* Mark overflowing PROMOTES: the list is dropped and the whole scene is
     * walked instead, because that is the cheaper answer from here on. */
    memset(&dirty, 0, sizeof(dirty));
    for( int i = 0; i < RS_GROUND_ITEMS_DIRTY_MAX; i++ )
        RS_GroundItemsDirty_Mark(&dirty, 5000 + i);
    CHECK(dirty.count == RS_GROUND_ITEMS_DIRTY_MAX, "the list did not fill");
    CHECK(!dirty.refresh_all, "the list promoted before it was full");

    RS_GroundItemsDirty_Mark(&dirty, 9999);
    CHECK(dirty.refresh_all, "overflowing Mark did not ask for a whole-scene walk");
    CHECK(dirty.count == 0, "overflowing Mark left %d stale entries", dirty.count);

    /* And while that is pending, further marks are no-ops -- the walk will
     * cover them, and refilling a list that is about to be replaced is waste. */
    RS_GroundItemsDirty_Mark(&dirty, 1234);
    CHECK(dirty.count == 0, "a mark during a pending walk queued %d tiles", dirty.count);

    /* Append overflowing merely STOPS. This is the walk itself filling the
     * list; promoting here would restart the walk that is running, and a scene
     * with more piles than the list holds would never finish one. */
    memset(&dirty, 0, sizeof(dirty));
    for( int i = 0; i < RS_GROUND_ITEMS_DIRTY_MAX; i++ )
        CHECK(RS_GroundItemsDirty_Append(&dirty, 7000 + i), "append %d was refused", i);
    CHECK(!RS_GroundItemsDirty_Append(&dirty, 8888), "append past the ceiling was accepted");
    CHECK(!dirty.refresh_all, "an overflowing append asked for another walk");
    CHECK(dirty.count == RS_GROUND_ITEMS_DIRTY_MAX, "an overflowing append changed the count");
    CHECK(!listed(&dirty, 8888), "the refused append was stored anyway");

    /* Append dedups too, and a duplicate is a success rather than a full list
     * -- otherwise the walk would stop at the first tile it had already seen. */
    memset(&dirty, 0, sizeof(dirty));
    CHECK(RS_GroundItemsDirty_Append(&dirty, 1), "the first append was refused");
    CHECK(RS_GroundItemsDirty_Append(&dirty, 1), "a duplicate append reported a full list");
    CHECK(dirty.count == 1, "a duplicate append grew the list to %d", dirty.count);

    /* Append ignores the refresh flag, because it IS the walk that flag asked
     * for. Marking would decline here. */
    memset(&dirty, 0, sizeof(dirty));
    dirty.refresh_all = 1;
    CHECK(RS_GroundItemsDirty_Append(&dirty, 42), "the walk's own append was declined");
    CHECK(listed(&dirty, 42), "the walk's own append was not stored");
    RS_GroundItemsDirty_Mark(&dirty, 43);
    CHECK(!listed(&dirty, 43), "a mark was stored while a walk was pending");
}

static void
test_the_flag_is_taken_before_the_walk(void)
{
    struct RS_GroundItemsDirty dirty;

    memset(&dirty, 0, sizeof(dirty));
    CHECK(!RS_GroundItemsDirty_TakeRefreshAll(&dirty), "an unset flag was taken");

    RS_GroundItemsDirty_MarkAll(&dirty);
    CHECK(dirty.refresh_all, "MarkAll did not set the flag");
    CHECK(RS_GroundItemsDirty_TakeRefreshAll(&dirty), "a set flag was not taken");
    CHECK(!dirty.refresh_all, "taking the flag did not clear it");
    CHECK(!RS_GroundItemsDirty_TakeRefreshAll(&dirty), "the flag was taken twice");

    /* Cleared first, so a tile changing DURING the walk is recorded rather
     * than declined by a flag that is about to be reset anyway. */
    RS_GroundItemsDirty_Mark(&dirty, 77);
    CHECK(listed(&dirty, 77), "a mark after the flag was taken was declined");
}

int
main(void)
{
    test_marking_dedups();
    test_the_two_overflows_differ();
    test_the_flag_is_taken_before_the_walk();

    if( g_failures )
    {
        printf("rs_ground_items_dirty_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_ground_items_dirty_test: OK\n");
    return 0;
}
