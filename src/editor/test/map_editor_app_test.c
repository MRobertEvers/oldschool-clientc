/*
 * The map editor's app record (editor/map_editor_app.h).
 *
 * A new record is zeroed except for what it was given and its one reset: the
 * hover ghost has no spawn entity. The panel-state rules each test what their
 * name says.
 *
 * Run: make -C src test-map-editor-app
 */

#include "editor/map_editor_app.h"

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
test_new(void)
{
    struct MapEditorConfig config = { 0 };
    /* Never dereferenced: the record only holds it. */
    struct Editor* session = (struct Editor*)&config;
    struct MapEditorApp* app = MapEditorApp_New(session, &config);

    TEST_ASSERT(app != NULL, "new hands back a record");
    TEST_ASSERT(app->session == session, "the session is held");
    TEST_ASSERT(app->config == &config, "the launch options are held");
    TEST_ASSERT(!app->map_ghost.active, "no ghost placed");
    TEST_ASSERT(app->map_ghost.spawn.entity == -1, "the ghost reset ran");
    TEST_ASSERT(app->editor_panel.built == 0, "the panel is not built");
    TEST_ASSERT(app->editor_panel.ui == NULL, "the panel has no registry");
    TEST_ASSERT(!MapEditorApp_PanelShown(app), "a new panel is hidden");
    TEST_ASSERT(!MapEditorApp_PanelLive(app), "a new panel is not live");
    TEST_ASSERT(!MapEditorApp_PanelOwnsCamera(app), "a new panel does not own the camera");
    MapEditorApp_Free(app);
    MapEditorApp_Free(NULL);
}

static void
test_panel_rules(void)
{
    struct MapEditorConfig config = { 0 };
    struct MapEditorApp* app = MapEditorApp_New((struct Editor*)&config, &config);
    /* A stand-in registry: the rules only test it for presence. */
    struct EditorUi* ui = (struct EditorUi*)&config;

    for( int built = 0; built < 2; built++ )
        for( int visible = 0; visible < 2; visible++ )
            for( int has_ui = 0; has_ui < 2; has_ui++ )
            {
                app->editor_panel.built = built;
                app->editor_panel.visible = visible;
                app->editor_panel.ui = has_ui ? ui : NULL;
                TEST_ASSERT(MapEditorApp_PanelShown(app) == visible, "shown is visible alone");
                TEST_ASSERT(MapEditorApp_PanelLive(app) == (built && has_ui), "live is built with a registry");
                TEST_ASSERT(
                    MapEditorApp_PanelOwnsCamera(app) == (built && visible && has_ui),
                    "the camera is the panel's when built, shown and with a registry");
            }
    app->editor_panel.ui = NULL;
    MapEditorApp_Free(app);
}

int
main(void)
{
    ToriMem_Init();
    test_new();
    test_panel_rules();
    ToriMem_Shutdown();
    if( g_failures )
    {
        printf("map_editor_app_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("map_editor_app_test: ok\n");
    return 0;
}
