/*
 * Render skip's decision: which committed frames are drawn, and whether a
 * skipped one can still be drawn late.
 *
 * Each wrong answer is a run that differs from the same run with skip off: a
 * frame drawn that the host withheld (its emit list may point at freed
 * components), a pick read from a stale frame that should have been caught
 * up, or an owed draw spent on a frame that was never drawn.
 *
 * Run: make -C src test-render-skip
 */

#include "render/render_skip.h"

#include <stdio.h>

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

static void
test_off_draws_what_the_host_draws(void)
{
    struct RenderSkip skip;

    RenderSkip_Reset(&skip);
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 0, 0, 0) == 1, "off: a committed frame draws");
    TEST_ASSERT(RenderSkip_Frame(&skip, 0, 0, 0, 0) == 0, "off: an uncommitted one does not");
    TEST_ASSERT(RenderSkip_FramesDrawn(&skip) == 1, "one drawn");
    TEST_ASSERT(RenderSkip_FramesSkipped(&skip) == 1, "one not drawn");
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_FRESH, "off is always fresh");
    TEST_ASSERT(!RenderSkip_AsksHulls(&skip, 0, 0), "off never asks the hulls");
}

static void
test_on_skips_and_catches_up(void)
{
    struct RenderSkip skip;

    RenderSkip_Reset(&skip);
    RenderSkip_Set(&skip, 1);
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_FRESH, "switched on fresh");
    TEST_ASSERT(RenderSkip_AsksHulls(&skip, 0, 0), "nothing else decides: ask the hulls");
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 0, 0, 0) == 0, "committed and skipped");
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_DRAW, "the skipped frame can be drawn late");
    RenderSkip_NoteCaughtUp(&skip);
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_FRESH, "caught up is fresh");
    TEST_ASSERT(RenderSkip_FramesCaughtUp(&skip) == 1, "counted");

    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 0, 0, 0) == 0, "skipped again");
    TEST_ASSERT(RenderSkip_Frame(&skip, 0, 0, 0, 0) == 0, "then not committed");
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_LOST, "an uncommitted frame loses the catch-up");

    RenderSkip_Reset(&skip);
    RenderSkip_Set(&skip, 1);
    TEST_ASSERT(RenderSkip_Frame(&skip, 0, 0, 0, 0) == 0, "not committed while fresh");
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_FRESH, "a fresh state stays fresh");
}

static void
test_reasons_to_draw(void)
{
    struct RenderSkip skip;

    RenderSkip_Reset(&skip);
    RenderSkip_Set(&skip, 1);
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 1, 0, 0) == 1, "forced draws");
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 0, 1, 0) == 1, "a capture draws");
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 0, 0, 2) == 1, "a hull in the scene draws");
    TEST_ASSERT(!RenderSkip_AsksHulls(&skip, 1, 0), "forced: hulls not asked");
    TEST_ASSERT(!RenderSkip_AsksHulls(&skip, 0, 1), "capture: hulls not asked");
    TEST_ASSERT(RenderSkip_Frame(&skip, 0, 1, 1, 1) == 0, "skip never adds a draw");
}

static void
test_owed_draws_count_drawn_frames(void)
{
    struct RenderSkip skip;

    RenderSkip_Reset(&skip);
    RenderSkip_Set(&skip, 1);
    RenderSkip_RequestDraw(&skip, 2);
    RenderSkip_RequestDraw(&skip, 1);
    TEST_ASSERT(skip.draw_frames == 2, "a larger debt stands");
    TEST_ASSERT(!RenderSkip_AsksHulls(&skip, 0, 0), "owed: hulls not asked");
    TEST_ASSERT(RenderSkip_Frame(&skip, 0, 0, 0, 0) == 0, "an uncommitted frame does not spend the debt");
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 0, 0, 0) == 1, "owed 2 of 2");
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 0, 0, 0) == 1, "owed 1 of 2");
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 0, 0, 0) == 0, "debt paid");
}

static void
test_hold(void)
{
    struct RenderSkip skip;

    RenderSkip_Reset(&skip);
    TEST_ASSERT(RenderSkip_Hold(&skip, 1) == 0, "holding releases nothing");
    TEST_ASSERT(RenderSkip_Held(&skip), "held");
    TEST_ASSERT(!RenderSkip_AsksHulls(&skip, 0, 0), "a refused frame never asks the hulls");
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 0, 0, 0) == 0, "held, skip off: not drawn");
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 0, 1, 0) == 1, "held: a capture still draws");
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 1, 0, 0) == 1, "held: a forced frame draws");
    TEST_ASSERT(RenderSkip_Hold(&skip, 0) == 1, "release says so");
    TEST_ASSERT(skip.draw_frames == 1, "release owes one drawn frame");
    TEST_ASSERT(RenderSkip_Hold(&skip, 0) == 0, "releasing twice says nothing");

    RenderSkip_Reset(&skip);
    RenderSkip_Set(&skip, 1);
    (void)RenderSkip_Hold(&skip, 1);
    TEST_ASSERT(RenderSkip_Frame(&skip, 1, 0, 0, 0) == 0, "held, skip on: not drawn");
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_LOST, "a held frame is not caught up");
}

static void
test_state_lost(void)
{
    struct RenderSkip skip;

    RenderSkip_Reset(&skip);
    RenderSkip_Set(&skip, 1);
    (void)RenderSkip_Frame(&skip, 1, 0, 0, 0);
    RenderSkip_NoteStateLost(&skip);
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_LOST, "a restore loses the catch-up");
    RenderSkip_Set(&skip, 1);
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_LOST, "setting on while on changes nothing");
    RenderSkip_Set(&skip, 0);
    TEST_ASSERT(!RenderSkip_Enabled(&skip), "off");
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_FRESH, "off is fresh");
}

/* The emit list a skipped frame left borrows the tree it was built from (a
 * text command points at the node's own string). A tree that moved after
 * that commit -- a driver verb's script settle at the next frame's start, a
 * task landing between frames -- cannot be drawn under the old list: a late
 * draw read the text a script had just replaced and freed (ASan
 * heap-use-after-free in font_collect_draw_lines, from a camera verb's
 * catch-up right after a tab press, on every relog of r2_relog.lua). */
static void
test_moved_tree_loses_the_catch_up(void)
{
    struct RenderSkip skip;

    RenderSkip_Reset(&skip);
    RenderSkip_Set(&skip, 1);
    RenderSkip_NoteEmit(&skip, 5);
    (void)RenderSkip_Frame(&skip, 1, 0, 0, 0);
    RenderSkip_NoteTree(&skip, 5);
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_DRAW, "the tree its list was built from: drawn late");
    RenderSkip_NoteTree(&skip, 6);
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_LOST, "a tree that moved since: not drawn under the old list");
    RenderSkip_NoteEmit(&skip, 6);
    (void)RenderSkip_Frame(&skip, 1, 0, 0, 0);
    RenderSkip_NoteTree(&skip, 6);
    TEST_ASSERT(RenderSkip_CatchUp(&skip) == RENDER_SKIP_CATCH_UP_DRAW, "the next commit's list is its own tree's again");
}

int
main(void)
{
    test_off_draws_what_the_host_draws();
    test_on_skips_and_catches_up();
    test_reasons_to_draw();
    test_owed_draws_count_drawn_frames();
    test_hold();
    test_state_lost();
    test_moved_tree_loses_the_catch_up();
    if( g_failures )
    {
        printf("render_skip_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("render_skip_test: ok\n");
    return 0;
}
