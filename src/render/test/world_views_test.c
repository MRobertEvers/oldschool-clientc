/*
 * The world views and the frame cursor (render/world_views.h).
 *
 * Init makes exactly one live view, views[0], with the frame on it and its
 * menu pointer at the one passed in, and leaves views[1] alone. The runner is
 * views[0] whatever the split says; the presented view is views[1] only while
 * the split is attached. The cursor can be moved to either and put back.
 *
 * Run: make -C src test-world-views
 */

#include "render/world_views.h"

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

/* Big enough that a stack copy is a bad idea. */
static struct WorldViews g_wv;
static struct UIMinimenu g_menu;

static void
test_init(void)
{
    memset(&g_wv, 0, sizeof(g_wv));
    g_wv.views[1].world_cam_zoom = 123;
    g_wv.emit_valid = 1;
    WorldViews_Init(&g_wv, &g_menu);

    TEST_ASSERT(g_wv.split.view_count == 1, "one live view");
    TEST_ASSERT(g_wv.frame == &g_wv.views[0], "frame on views[0]");
    TEST_ASSERT(g_wv.views[0].minimenu == &g_menu, "runner view's menu is the one passed");
    TEST_ASSERT(g_wv.views[1].minimenu == NULL, "views[1] gets no menu");
    TEST_ASSERT(g_wv.views[1].world_cam_zoom == 123, "views[1] untouched");
    TEST_ASSERT(g_wv.split.attached == 0, "not attached");
    TEST_ASSERT(g_wv.emit_valid == 1, "the emit desc is not Init's");
}

static void
test_selectors(void)
{
    memset(&g_wv, 0, sizeof(g_wv));
    WorldViews_Init(&g_wv, &g_menu);

    TEST_ASSERT(WorldViews_Runner(&g_wv) == &g_wv.views[APP_VIEW_RUNNER], "runner is views[0]");
    TEST_ASSERT(WorldViews_Presented(&g_wv) == &g_wv.views[APP_VIEW_RUNNER], "detached: presented is views[0]");

    g_wv.split.attached = 1;
    g_wv.split.view_count = 2;
    TEST_ASSERT(WorldViews_Runner(&g_wv) == &g_wv.views[0], "attached: runner still views[0]");
    TEST_ASSERT(WorldViews_Presented(&g_wv) == &g_wv.views[APP_VIEW_PLAYER_CLIENT], "attached: presented is views[1]");

    /* A stage moves the cursor and puts it back. */
    g_wv.frame = WorldViews_Presented(&g_wv);
    TEST_ASSERT(g_wv.frame == &g_wv.views[1], "presented stage draws views[1]");
    g_wv.frame = WorldViews_Runner(&g_wv);
    TEST_ASSERT(g_wv.frame == &g_wv.views[0], "and puts views[0] back");

    g_wv.split.attached = 0;
    g_wv.split.view_count = 1;
    TEST_ASSERT(WorldViews_Presented(&g_wv) == &g_wv.views[0], "detached again: presented is views[0]");
}

int
main(void)
{
    test_init();
    test_selectors();
    if( g_failures )
    {
        printf("world_views_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("world_views_test: ok\n");
    return 0;
}
