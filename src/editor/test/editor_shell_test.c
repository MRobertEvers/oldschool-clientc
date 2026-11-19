/*
 * The editors' working state (editor/editor_shell.h).
 *
 * A created shell is zeroed except for its two resets: the hover ghost has no
 * spawn entity and the preview camera sits at its default angle with a fit
 * pending. (The footprint outline's seed and toggle left with it:
 * editor/test/hover_footprint_test.c.)
 *
 * Run: make -C src test-editor-shell
 */

#include "editor/editor_shell.h"

#include "mem/torirs_mem.h"

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
test_create(void)
{
    struct EditorShell* shell = EditorShell_Create();

    TEST_ASSERT(shell != NULL, "create hands back a record");
    TEST_ASSERT(shell->locedit.panel == 0 && shell->locedit.visible == 0, "no loc editor panel");
    TEST_ASSERT(!shell->map_ghost.active, "no ghost placed");
    TEST_ASSERT(shell->map_ghost.spawn.entity == -1, "the ghost reset ran");
    TEST_ASSERT(shell->preview_camera.pitch == EDITOR_PREVIEW_DEFAULT_PITCH, "camera pitch reset");
    TEST_ASSERT(shell->preview_camera.yaw == EDITOR_PREVIEW_DEFAULT_YAW, "camera yaw reset");
    TEST_ASSERT(shell->preview_camera.fit_pending, "a fit is pending");
    TEST_ASSERT(shell->editor_panel.built == 0, "the map editor panel is not built");
    TEST_ASSERT(shell->editor_panel.ui == NULL, "the map editor panel has no registry");
    EditorShell_Free(shell);
    EditorShell_Free(NULL);
}

int
main(void)
{
    ToriMem_Init();
    test_create();
    ToriMem_Shutdown();
    if( g_failures )
    {
        printf("editor_shell_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("editor_shell_test: ok\n");
    return 0;
}
