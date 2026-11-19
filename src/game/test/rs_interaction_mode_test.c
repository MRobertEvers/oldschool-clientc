/*
 * What a click may do in each world view: a view's own pair, the defaults it
 * falls back to, and the reset that puts it back on them.
 *
 * A view that keeps its pair after a reset carries one hull's modes to the
 * next hull given the same slot; a view that ignores the defaults answers a
 * world -2 packet with the boot value. Both only show as a menu missing
 * "Walk here", or offering it where the server took it away.
 *
 *   make -C src test-rs-interaction-mode
 */
#include "game/rs_interaction_mode.h"

#include <stdio.h>

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
test_boot_value(void)
{
    struct RS_InteractionMode modes;

    printf("TEST: after a reset every view answers 1/1\n");

    RS_InteractionMode_Reset(&modes);
    for( int view = 0; view < WORLDVIEW_MAX; view++ )
    {
        CHECK(RS_InteractionMode_Tile(&modes, view) == 1, "view %d tile %d", view,
            RS_InteractionMode_Tile(&modes, view));
        CHECK(RS_InteractionMode_Entity(&modes, view) == 1, "view %d entity %d", view,
            RS_InteractionMode_Entity(&modes, view));
    }
}

static void
test_a_view_pair_wins_over_the_defaults(void)
{
    struct RS_InteractionMode modes;

    printf("TEST: a view's own pair wins; the others follow the defaults\n");

    RS_InteractionMode_Reset(&modes);
    RS_InteractionMode_SetView(&modes, 3, 0, 2);
    RS_InteractionMode_SetDefaults(&modes, 2, 3);
    CHECK(RS_InteractionMode_Tile(&modes, 3) == 0, "view 3 tile %d",
        RS_InteractionMode_Tile(&modes, 3));
    CHECK(RS_InteractionMode_Entity(&modes, 3) == 2, "view 3 entity %d",
        RS_InteractionMode_Entity(&modes, 3));
    CHECK(RS_InteractionMode_Tile(&modes, 0) == 2, "root tile %d did not follow the defaults",
        RS_InteractionMode_Tile(&modes, 0));
    CHECK(RS_InteractionMode_Entity(&modes, 0) == 3, "root entity %d did not follow the defaults",
        RS_InteractionMode_Entity(&modes, 0));
}

static void
test_a_mode_of_zero_is_a_mode(void)
{
    struct RS_InteractionMode modes;

    printf("TEST: a view given 0 answers 0, not the default\n");

    RS_InteractionMode_Reset(&modes);
    RS_InteractionMode_SetView(&modes, WORLDVIEW_MAX - 1, 0, 0);
    CHECK(RS_InteractionMode_Tile(&modes, WORLDVIEW_MAX - 1) == 0, "tile %d",
        RS_InteractionMode_Tile(&modes, WORLDVIEW_MAX - 1));
    CHECK(RS_InteractionMode_Entity(&modes, WORLDVIEW_MAX - 1) == 0, "entity %d",
        RS_InteractionMode_Entity(&modes, WORLDVIEW_MAX - 1));
}

static void
test_reset_view_follows_the_defaults_again(void)
{
    struct RS_InteractionMode modes;

    printf("TEST: a reset view follows the defaults again, and only that view\n");

    RS_InteractionMode_Reset(&modes);
    RS_InteractionMode_SetView(&modes, 1, 2, 0);
    RS_InteractionMode_SetView(&modes, 2, 2, 0);
    RS_InteractionMode_SetDefaults(&modes, 0, 1);
    RS_InteractionMode_ResetView(&modes, 1);
    CHECK(RS_InteractionMode_Tile(&modes, 1) == 0, "reset view 1 tile %d",
        RS_InteractionMode_Tile(&modes, 1));
    CHECK(RS_InteractionMode_Entity(&modes, 1) == 1, "reset view 1 entity %d",
        RS_InteractionMode_Entity(&modes, 1));
    CHECK(RS_InteractionMode_Tile(&modes, 2) == 2, "view 2 lost its tile mode: %d",
        RS_InteractionMode_Tile(&modes, 2));

    RS_InteractionMode_Reset(&modes);
    CHECK(RS_InteractionMode_Tile(&modes, 2) == 1, "a full reset left view 2 at %d",
        RS_InteractionMode_Tile(&modes, 2));
}

static void
test_a_pair_for_a_view_not_created_yet_is_held(void)
{
    struct RS_InteractionMode modes;

    printf("TEST: a pair for a view not created yet is kept by its wire id\n");

    RS_InteractionMode_Reset(&modes);
    CHECK(RS_InteractionMode_Hold(&modes, 2009, 2, 3) == 0, "room to hold");
    CHECK(RS_InteractionMode_Hold(&modes, 2009, 0, 1) == 0, "a newer pair replaces");
    CHECK(modes.held_count == 1, "one pair per id, %d held", modes.held_count);
    CHECK(RS_InteractionMode_TakeHeld(&modes, 2010, 3) == 0, "another id takes nothing");
    CHECK(RS_InteractionMode_Tile(&modes, 3) == 1, "and follows the defaults");
    CHECK(RS_InteractionMode_TakeHeld(&modes, 2009, 3) == 1, "the view made for it takes it");
    CHECK(RS_InteractionMode_Tile(&modes, 3) == 0, "the newer tile mode %d",
        RS_InteractionMode_Tile(&modes, 3));
    CHECK(RS_InteractionMode_Entity(&modes, 3) == 1, "the newer entity mode %d",
        RS_InteractionMode_Entity(&modes, 3));
    CHECK(modes.held_count == 0, "nothing left held");

    RS_InteractionMode_Hold(&modes, 7, 2, 2);
    RS_InteractionMode_ForgetHeld(&modes, 7);
    CHECK(RS_InteractionMode_TakeHeld(&modes, 7, 4) == 0, "a forgotten pair is not taken");

    /* Full, the oldest makes room; the newest are kept. */
    for( int id = 0; id < RS_INTERACTION_MODE_HELD_MAX; id++ )
        CHECK(RS_InteractionMode_Hold(&modes, 1000 + id, 2, 0) == 0, "id %d fits", id);
    CHECK(RS_InteractionMode_Hold(&modes, 5000, 0, 3) == 1, "the next one evicts");
    CHECK(RS_InteractionMode_TakeHeld(&modes, 1000, 5) == 0, "the oldest went");
    CHECK(RS_InteractionMode_TakeHeld(&modes, 1001, 5) == 1, "the second oldest stayed");
    CHECK(RS_InteractionMode_TakeHeld(&modes, 5000, 6) == 1, "the newest stayed");

    RS_InteractionMode_Hold(&modes, 9, 2, 2);
    RS_InteractionMode_Reset(&modes);
    CHECK(modes.held_count == 0, "a session end forgets every held pair");
}

int
main(void)
{
    test_boot_value();
    test_a_view_pair_wins_over_the_defaults();
    test_a_mode_of_zero_is_a_mode();
    test_reset_view_follows_the_defaults_again();
    test_a_pair_for_a_view_not_created_yet_is_held();

    if( g_failures )
    {
        printf("rs_interaction_mode_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_interaction_mode_test: OK\n");
    return 0;
}
