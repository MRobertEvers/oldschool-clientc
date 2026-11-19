/*
 * Which sequence loads have already been asked for.
 *
 * An id is claimed once and then refused until it is evicted. Add records
 * nothing once the table is full; ClaimEvicting drops the oldest id instead,
 * so a full table still dedupes and an evicted id can be claimed again.
 *
 *   make -C src test-seq-load-tracker
 */
#include "engine/seq_load_tracker.h"

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
test_claim_once_then_refused(void)
{
    struct SeqLoadTracker tracker;

    memset(&tracker, 0, sizeof(tracker));
    CHECK(!SeqLoadTracker_Has(&tracker, 0), "an empty tracker has seq 0");
    CHECK(SeqLoadTracker_ClaimEvicting(&tracker, 0), "seq 0 was not claimed");
    CHECK(SeqLoadTracker_Has(&tracker, 0), "a claimed seq is not recorded");
    CHECK(!SeqLoadTracker_ClaimEvicting(&tracker, 0), "seq 0 was claimed twice");
    CHECK(tracker.count == 1, "a refused claim changed the count: %d", tracker.count);
    CHECK(SeqLoadTracker_ClaimEvicting(&tracker, 812), "a second seq was refused");
    CHECK(tracker.count == 2, "two claims left count %d", tracker.count);
}

static void
test_full_table_evicts_oldest(void)
{
    struct SeqLoadTracker tracker;

    memset(&tracker, 0, sizeof(tracker));
    for( int i = 0; i < SEQ_LOAD_TRACKER_MAX; i++ )
        CHECK(SeqLoadTracker_ClaimEvicting(&tracker, 1000 + i), "seq %d was refused", 1000 + i);
    CHECK(tracker.count == SEQ_LOAD_TRACKER_MAX, "a full table counts %d", tracker.count);

    /* Full: the next new id pushes the oldest out and still dedupes. */
    CHECK(SeqLoadTracker_ClaimEvicting(&tracker, 5000), "a full table refused a new seq");
    CHECK(tracker.count == SEQ_LOAD_TRACKER_MAX, "eviction changed the count: %d", tracker.count);
    CHECK(!SeqLoadTracker_Has(&tracker, 1000), "the oldest seq was not evicted");
    CHECK(SeqLoadTracker_Has(&tracker, 1001), "the second-oldest seq was evicted");
    CHECK(tracker.seq_ids[SEQ_LOAD_TRACKER_MAX - 1] == 5000, "the new seq is not the newest");
    CHECK(!SeqLoadTracker_ClaimEvicting(&tracker, 5000), "a full table claimed a seq twice");
    CHECK(!SeqLoadTracker_ClaimEvicting(&tracker, 1001), "a kept seq was claimed again");

    /* An evicted id is new again. */
    CHECK(SeqLoadTracker_ClaimEvicting(&tracker, 1000), "an evicted seq could not be claimed");
    CHECK(!SeqLoadTracker_Has(&tracker, 1001), "the next oldest seq was not evicted");
}

static void
test_add_stops_when_full(void)
{
    struct SeqLoadTracker tracker;

    memset(&tracker, 0, sizeof(tracker));
    for( int i = 0; i < SEQ_LOAD_TRACKER_MAX; i++ )
        SeqLoadTracker_Add(&tracker, 2000 + i);
    SeqLoadTracker_Add(&tracker, 9000);
    CHECK(tracker.count == SEQ_LOAD_TRACKER_MAX, "Add grew past the table: %d", tracker.count);
    CHECK(!SeqLoadTracker_Has(&tracker, 9000), "Add recorded into a full table");
    CHECK(SeqLoadTracker_Has(&tracker, 2000), "Add evicted the oldest seq");
}

int
main(void)
{
    test_claim_once_then_refused();
    test_full_table_evicts_oldest();
    test_add_stops_when_full();

    if( g_failures )
    {
        printf("seq_load_tracker_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("seq_load_tracker_test: OK\n");
    return 0;
}
