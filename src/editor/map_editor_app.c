#include "editor/map_editor_app.h"

#include "mem/torirs_mem.h"

#include <assert.h>

struct MapEditorApp*
MapEditorApp_New(
    struct Editor* session,
    struct MapEditorConfig const* config)
{
    struct MapEditorApp* app;

    assert(session);
    assert(config);
    /* EDITOR: the map editor's working state; MapEditorApp_Free releases it. */
    app = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*app));
    app->session = session;
    app->config = config;
    MapEditorGhost_Reset(&app->map_ghost);
    return app;
}

void
MapEditorApp_Free(struct MapEditorApp* app)
{
    if( !app )
        return;
    ToriMem_Free(ToriMem_Editor(), app);
}

int
MapEditorApp_PanelShown(struct MapEditorApp const* app)
{
    assert(app);
    return app->editor_panel.visible != 0;
}

int
MapEditorApp_PanelLive(struct MapEditorApp const* app)
{
    assert(app);
    return app->editor_panel.built && app->editor_panel.ui;
}

int
MapEditorApp_PanelOwnsCamera(struct MapEditorApp const* app)
{
    assert(app);
    return app->editor_panel.built && app->editor_panel.visible && app->editor_panel.ui;
}
