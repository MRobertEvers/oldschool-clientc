/*
 * The settings save debounce: when a store that keeps changing is quiet long
 * enough to be written.
 *
 * A write that falls due too early is a file rewritten on every tick of a
 * slider drag; one that never falls due is a setting lost at the next launch
 * unless the shutdown flush catches it.
 *
 *   make -C src test-save-debounce
 */
#include "game/rs_save_debounce.h"

#include <stdint.h>
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

#define SETTLE 25u

static void
test_a_clean_store_is_never_due(void)
{
    struct RS_SaveDebounce d;

    printf("TEST: a store with nothing marked is never due\n");
    memset(&d, 0, sizeof(d));
    CHECK(!RS_SaveDebounce_Due(&d, 0, SETTLE), "due at cycle 0");
    CHECK(!RS_SaveDebounce_Due(&d, 1000000, SETTLE), "due at cycle 1000000");
}

static void
test_a_mark_falls_due_after_the_settle(void)
{
    struct RS_SaveDebounce d;

    printf("TEST: a mark falls due at exactly settle cycles, not before\n");
    memset(&d, 0, sizeof(d));
    RS_SaveDebounce_Mark(&d, 100);
    CHECK(!RS_SaveDebounce_Due(&d, 100, SETTLE), "due on the cycle it was marked");
    CHECK(!RS_SaveDebounce_Due(&d, 124, SETTLE), "due one cycle early");
    CHECK(RS_SaveDebounce_Due(&d, 125, SETTLE), "not due after the settle");
    CHECK(RS_SaveDebounce_Due(&d, 5000, SETTLE), "not due long after the settle");
}

static void
test_a_later_change_moves_the_mark(void)
{
    struct RS_SaveDebounce d;

    printf("TEST: a change during the settle moves the mark\n");
    memset(&d, 0, sizeof(d));
    RS_SaveDebounce_Mark(&d, 100);
    RS_SaveDebounce_Mark(&d, 120);
    CHECK(!RS_SaveDebounce_Due(&d, 125, SETTLE), "due from the first mark");
    CHECK(RS_SaveDebounce_Due(&d, 145, SETTLE), "not due from the second mark");
}

static void
test_clear_ends_the_wait(void)
{
    struct RS_SaveDebounce d;

    printf("TEST: a cleared mark is not due\n");
    memset(&d, 0, sizeof(d));
    RS_SaveDebounce_Mark(&d, 100);
    RS_SaveDebounce_Clear(&d);
    CHECK(!RS_SaveDebounce_Due(&d, 200, SETTLE), "due after Clear");
}

static void
test_a_mark_at_cycle_zero_is_clean(void)
{
    struct RS_SaveDebounce d;

    printf("TEST: a change marked at cycle 0 leaves the store looking clean\n");
    memset(&d, 0, sizeof(d));
    RS_SaveDebounce_Mark(&d, 7);
    RS_SaveDebounce_Mark(&d, 0);
    CHECK(!RS_SaveDebounce_Due(&d, SETTLE, SETTLE), "a cycle-0 mark fell due");
    CHECK(!RS_SaveDebounce_Due(&d, 1000, SETTLE), "a cycle-0 mark fell due later");
    CHECK(!RS_SaveDebounce_Take(&d, false), "a cycle-0 mark wants a flush");
}

static void
test_a_settle_of_zero_is_due_at_once(void)
{
    struct RS_SaveDebounce d;

    printf("TEST: a settle of 0 is due on the marked cycle\n");
    memset(&d, 0, sizeof(d));
    RS_SaveDebounce_Mark(&d, 9);
    CHECK(RS_SaveDebounce_Due(&d, 9, 0), "not due on the marked cycle with no settle");
}

static void
test_a_cycle_behind_the_mark_wraps(void)
{
    struct RS_SaveDebounce d;

    printf("TEST: a cycle behind the mark is a huge unsigned gap, so due\n");
    memset(&d, 0, sizeof(d));
    RS_SaveDebounce_Mark(&d, 100);
    CHECK(RS_SaveDebounce_Due(&d, 99, SETTLE), "a cycle behind the mark was not due");
}

static void
test_take_wants_a_change_or_a_mark_and_clears(void)
{
    struct RS_SaveDebounce d;

    printf("TEST: Take wants a write for a change now or a mark, and clears the mark\n");
    memset(&d, 0, sizeof(d));
    CHECK(!RS_SaveDebounce_Take(&d, false), "clean and unchanged wants a write");
    CHECK(RS_SaveDebounce_Take(&d, true), "a change now does not want a write");
    CHECK(d.dirty_cycle == 0, "Take left a mark");

    RS_SaveDebounce_Mark(&d, 40);
    CHECK(RS_SaveDebounce_Take(&d, false), "a pending mark does not want a write");
    CHECK(d.dirty_cycle == 0, "Take did not clear the mark");
    CHECK(!RS_SaveDebounce_Due(&d, 1000, SETTLE), "due after Take");

    RS_SaveDebounce_Mark(&d, 40);
    CHECK(RS_SaveDebounce_Take(&d, true), "a change and a mark do not want a write");
    CHECK(d.dirty_cycle == 0, "Take did not clear the mark");
}

int
main(void)
{
    test_a_clean_store_is_never_due();
    test_a_mark_falls_due_after_the_settle();
    test_a_later_change_moves_the_mark();
    test_clear_ends_the_wait();
    test_a_mark_at_cycle_zero_is_clean();
    test_a_settle_of_zero_is_due_at_once();
    test_a_cycle_behind_the_mark_wraps();
    test_take_wants_a_change_or_a_mark_and_clears();
    if( g_failures )
    {
        printf("rs_save_debounce_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_save_debounce_test: all passed\n");
    return 0;
}
