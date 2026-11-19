/*
 * When the three tile-highlight refreshers fire again.
 *
 * The first ask after a reset reports a change for every value, -1 included:
 * "no tile" is a state the refreshers must run for once. After that a value
 * reports a change only when it differs from the one last asked about, and
 * each of the three is remembered on its own.
 *
 *   make -C src test-rs-tile-highlight-watch
 */
#include "game/rs_tile_highlight_watch.h"

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
test_first_ask_after_reset_fires_even_for_no_tile(void)
{
    struct RS_TileHighlightWatch watch;

    memset(&watch, 0, sizeof(watch));
    RS_TileHighlightWatch_Reset(&watch);
    CHECK(RS_TileHighlightWatch_HoverChanged(&watch, -1), "no hovered tile did not fire once");
    CHECK(RS_TileHighlightWatch_RouteChanged(&watch, -1), "a still route did not fire once");
    CHECK(RS_TileHighlightWatch_DestChanged(&watch, -1), "no destination did not fire once");

    CHECK(!RS_TileHighlightWatch_HoverChanged(&watch, -1), "no hovered tile fired twice");
    CHECK(!RS_TileHighlightWatch_RouteChanged(&watch, -1), "a still route fired twice");
    CHECK(!RS_TileHighlightWatch_DestChanged(&watch, -1), "no destination fired twice");

    /* 0 is a coord like any other; a zeroed watch that was never reset would
     * have swallowed it. */
    RS_TileHighlightWatch_Reset(&watch);
    CHECK(RS_TileHighlightWatch_HoverChanged(&watch, 0), "coord 0 did not fire after reset");
}

static void
test_each_value_is_its_own_edge(void)
{
    struct RS_TileHighlightWatch watch;

    RS_TileHighlightWatch_Reset(&watch);
    (void)RS_TileHighlightWatch_HoverChanged(&watch, 1000);
    (void)RS_TileHighlightWatch_RouteChanged(&watch, 2000);
    (void)RS_TileHighlightWatch_DestChanged(&watch, 3000);

    CHECK(RS_TileHighlightWatch_HoverChanged(&watch, 1001), "a new hovered tile did not fire");
    CHECK(!RS_TileHighlightWatch_RouteChanged(&watch, 2000), "the route fired with the hover");
    CHECK(!RS_TileHighlightWatch_DestChanged(&watch, 3000), "the destination fired with the hover");

    /* A value equal to ANOTHER one's last value is still a change for this
     * one. */
    CHECK(RS_TileHighlightWatch_DestChanged(&watch, 1001), "the destination shared the hover's edge");

    /* Going back to an earlier value is a change too: only the last one is
     * remembered. */
    CHECK(RS_TileHighlightWatch_HoverChanged(&watch, 1000), "returning to a tile did not fire");
    CHECK(!RS_TileHighlightWatch_HoverChanged(&watch, 1000), "a held tile fired again");
}

int
main(void)
{
    test_first_ask_after_reset_fires_even_for_no_tile();
    test_each_value_is_its_own_edge();

    if( g_failures )
    {
        printf("rs_tile_highlight_watch_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_tile_highlight_watch_test: OK\n");
    return 0;
}
