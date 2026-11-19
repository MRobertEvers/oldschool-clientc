/*
 * The editors' working state (editor/editor_shell.h).
 *
 * A created shell is zeroed except for its one reset: the hover ghost has no
 * spawn entity. (The footprint outline's seed and toggle left it:
 * editor/test/hover_footprint_test.c; the loc editor is a side-app,
 * app/app_tool_loc_editor.c; the model-view camera is the developer
 * overlay's, ui/test/dev_overlay_test.c.)
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
    TEST_ASSERT(!shell->map_ghost.active, "no ghost placed");
    TEST_ASSERT(shell->map_ghost.spawn.entity == -1, "the ghost reset ran");
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
