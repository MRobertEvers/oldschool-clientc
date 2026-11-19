/*
 * The renderer's facts and the server's ZBUF choice (render/render_mode.h).
 *
 * What each setter stores, the server's choice forgotten back to -1, and the
 * software frame's two answers: the depth buffer follows the world mode, and
 * the OSRS z-buffer follows the server once it has chosen, the launched
 * renderer until then, and only for a painter-collected world.
 *
 * Run: make -C src test-render-mode
 */

#include "render/render_mode.h"

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

static void
test_setters(void)
{
    struct RenderMode rm;

    memset(&rm, 0, sizeof(rm));
    TEST_ASSERT(RenderMode_World(&rm) == TORIRS_WORLD_PAINTER, "zeroed: painter world");
    TEST_ASSERT(!RenderMode_RendererAnimatesTextures(&rm), "zeroed: CPU animates textures");

    RenderMode_SetWorld(&rm, TORIRS_WORLD_DEPTH);
    TEST_ASSERT(RenderMode_World(&rm) == TORIRS_WORLD_DEPTH, "depth world stored");
    RenderMode_SetWorld(&rm, TORIRS_WORLD_PAINTER);
    TEST_ASSERT(RenderMode_World(&rm) == TORIRS_WORLD_PAINTER, "a fallback restores painter");

    RenderMode_SetRendererAnimatesTextures(&rm, true);
    TEST_ASSERT(RenderMode_RendererAnimatesTextures(&rm), "renderer animates stored");
    RenderMode_SetRendererAnimatesTextures(&rm, false);
    TEST_ASSERT(!RenderMode_RendererAnimatesTextures(&rm), "renderer animates cleared");

    RenderMode_SetServerZbuf(&rm, 1);
    TEST_ASSERT(rm.server_zbuf == 1, "ZBUF stored as sent");
    RenderMode_SetServerZbuf(&rm, 0);
    TEST_ASSERT(rm.server_zbuf == 0, "ZBUF 0 stored as sent");
    RenderMode_ForgetServerZbuf(&rm);
    TEST_ASSERT(rm.server_zbuf == -1, "forgotten: -1, none made");
    TEST_ASSERT(sizeof(rm.server_zbuf) == sizeof(int), "the checkpoint stores an int");
}

static void
test_software_depth(void)
{
    struct RenderMode rm;

    memset(&rm, 0, sizeof(rm));
    RenderMode_ForgetServerZbuf(&rm);
    TEST_ASSERT(!RenderMode_SoftwareDepth(&rm), "painter world: no depth buffer");
    RenderMode_SetWorld(&rm, TORIRS_WORLD_DEPTH);
    TEST_ASSERT(RenderMode_SoftwareDepth(&rm), "depth world: depth buffer");
}

static void
test_software_rszbuffer(void)
{
    struct RenderMode rm;

    memset(&rm, 0, sizeof(rm));
    RenderMode_ForgetServerZbuf(&rm);

    /* No server choice: the launched renderer decides. */
    TEST_ASSERT(!RenderMode_SoftwareRsZBuffer(&rm), "plain software: off");
    RenderMode_SetSoftwareRsZBuffer(&rm, true);
    TEST_ASSERT(RenderMode_SoftwareRsZBuffer(&rm), "OSRS z-buffer renderer: on");

    /* The server's choice, once made, overrides the launch in both directions. */
    RenderMode_SetServerZbuf(&rm, 0);
    TEST_ASSERT(!RenderMode_SoftwareRsZBuffer(&rm), "ZBUF 0 turns the launched one off");
    RenderMode_SetSoftwareRsZBuffer(&rm, false);
    RenderMode_SetServerZbuf(&rm, 1);
    TEST_ASSERT(RenderMode_SoftwareRsZBuffer(&rm), "ZBUF 1 turns plain software on");
    RenderMode_SetServerZbuf(&rm, 7);
    TEST_ASSERT(RenderMode_SoftwareRsZBuffer(&rm), "any non-zero ZBUF is on");

    /* Forgetting it hands the decision back to the launch. */
    RenderMode_ForgetServerZbuf(&rm);
    TEST_ASSERT(!RenderMode_SoftwareRsZBuffer(&rm), "session over: plain software again");

    /* Only a painter-collected world: a depth world never draws it. */
    RenderMode_SetServerZbuf(&rm, 1);
    RenderMode_SetSoftwareRsZBuffer(&rm, true);
    RenderMode_SetWorld(&rm, TORIRS_WORLD_DEPTH);
    TEST_ASSERT(!RenderMode_SoftwareRsZBuffer(&rm), "depth world: never the OSRS z-buffer");
}

int
main(void)
{
    test_setters();
    test_software_depth();
    test_software_rszbuffer();
    if( g_failures )
    {
        printf("render_mode_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("render_mode_test: ok\n");
    return 0;
}
