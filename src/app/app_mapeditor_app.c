/*
 * The map editor as the App holds it: its record (editor/map_editor_app.h,
 * App.map_editor), the queries the rest of the client asks it through, and
 * the side-app it registers as.
 *
 * App.map_editor is NULL in every boot that is not an editor boot, and in an
 * editor boot whose ToriRSMapEd could not be reached. Every query here tests
 * for it once and answers no / zero / "no override" without it; each keeps
 * the exact test its sites made before the record existed, so two sites that
 * tested different things ask different queries.
 *
 * The side-app (registered in an editor boot only, app_tools.c) reaches the
 * frame through the registry:
 *   - key `map_editor_toggle` (CTRL held, not a key the editor claimed, the
 *     dev panels' point with no text input focused, after the loc editor's
 *     and the hover footprint's toggles): show or hide the command panel;
 *   - frame, at SIDE_APP_PHASE_FIRST_KEYS: the editor's hotkeys, before
 *     every other key reader; at SIDE_APP_PHASE_POINTER_LATCHED: the editor's
 *     frame work, in the order it always ran;
 *   - world_overlay: its previews, grids and selection
 *     (app_mapeditor_overlay.c), after the hover footprint's marks;
 *   - inspects_scenery: while the panel is shown the pick sees every loc;
 *   - menu_flags / menu_action: the SELECT tool's rows and the previewed
 *     spawn's (mapedit_select_active, RS_MINIMENU_ACTION_MAPEDIT_SELECT*);
 *   - world_load: the previews come down at a load's start, and the squares
 *     it takes are seeded from the content tree.
 * Its `self` is the App: what it keeps is App.map_editor, which the App
 * makes and frees, and the editor's own units.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "app/app_catalog_palette.h"
#include "app/app_mapeditor_state.h"
#include "app/app_record_thumbs.h"
#include "perf/frame_stats.h"

/* Private to this unit, declared up front so definition order is free. */
static bool
app_mapedit_select_active(struct App const* app);
static void
app_map_editor_side_app_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args);
static void
app_map_editor_side_app_frame(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase,
    struct LibToriRS_Input* input);
static void
app_map_editor_side_app_pointer_latched(
    struct App* app,
    struct LibToriRS_Input* input);
static void
app_map_editor_side_app_world_overlay(
    void* self,
    struct SideAppHost* host);
static int
app_map_editor_side_app_inspects_scenery(void const* self);
static void
app_map_editor_side_app_menu_flags(
    void const* self,
    void* build_ctx);
static int
app_map_editor_side_app_menu_action(
    void* self,
    struct SideAppHost* host,
    int action,
    void const* option);
static void
app_map_editor_side_app_world_load(
    void* self,
    struct SideAppHost* host,
    enum SideAppWorldLoadPoint point,
    int const* squares_xz,
    int square_count);

/* ---- the record ---------------------------------------------------------- */

void
app_map_editor_app_open(struct App* app)
{
    struct Editor* session;

    assert(app);
    assert(!app->map_editor);
    session = ToriMem_Alloc(ToriMem_Process(), sizeof(*session));
    assert(session);
    app->map_editor = MapEditorApp_New(session, &app->cfg.map_editor);
    app->map_editor->state = app_mapeditor_state_new();
}

void
app_map_editor_app_close(struct App* app)
{
    assert(app);
    assert(app->map_editor);
    app_mapeditor_state_free(app->map_editor->state);
    app->map_editor->state = NULL;
    ToriMem_Free(ToriMem_Process(), app->map_editor->session);
    MapEditorApp_Free(app->map_editor);
    app->map_editor = NULL;
}

struct MapEditorState*
app_mapeditor_state_new(void)
{
    /* EDITOR: the editor units' working state; app_mapeditor_state_free
     * releases it with the app. */
    struct MapEditorState* st = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*st));

    /* What the file statics started at, where that was not zero. */
    st->edit.ghost.ghost_handle = WORLD_BUILDER_HANDLE_NONE;
    st->edit.ghost.occupant_handle = WORLD_BUILDER_HANDLE_NONE;
    st->edit.ghost.occupant_next = WORLD_BUILDER_HANDLE_NONE;
    st->spawns.ghost_loads.npc_requested = -1;
    st->spawns.ghost_loads.npc_resolved = -1;
    st->spawns.ghost_loads.obj_requested = -1;
    st->spawns.ghost_loads.model_requested = -1;
    st->spawns.player_ghost.entity = -1;
    st->spawns.player_ghost.look_serial = -1;
    st->spawns.player_preview.entity = -1;
    st->spawns.player_preview.element_id = -1;
    st->spawns.player_preview.look_serial = -1;
    st->thumbs.strip.last_loc = -1;
    st->thumbs.strip.requested_loc = -1;
    st->preview_well.last_kind = -1;
    st->preview_well.last_id = -1;
    st->preview_well.last_w = -1;
    st->preview_well.last_h = -1;
    st->preview_well.subject_kind = -1;
    st->preview_well.subject_id = -1;
    st->preview_well.requested_kind = -1;
    st->preview_well.requested_id = -1;
    st->preview_well.models_kind = -1;
    st->preview_well.models_id = -1;
    return st;
}

void
app_mapeditor_state_free(struct MapEditorState* state)
{
    if( !state )
        return;
    ToriMem_Free(ToriMem_Editor(), state);
}

struct MapEditorState*
app_mapeditor_state(struct App const* app)
{
    assert(app);
    assert(app->map_editor);
    assert(app->map_editor->state);
    return app->map_editor->state;
}

/* ---- the queries --------------------------------------------------------- */

struct MapEditorApp*
App_MapEditor(struct App const* app)
{
    assert(app);
    return app->map_editor;
}

int
App_MapEditorOpen(struct App const* app)
{
    assert(app);
    return app->map_editor != NULL;
}

int
App_MapEditorUnsavedCount(struct App const* app)
{
    assert(app);
    if( !app->map_editor )
        return 0;
    return Editor_DocUnsavedCount(&app->map_editor->session->doc);
}

int
App_MapEditorChromeScaleFor(
    struct App const* app,
    int q,
    int touch)
{
    assert(app);
    if( !app->map_editor || !MapEditorApp_PanelLive(app->map_editor) )
        return q;
    return Editor_UiChromeScaleFor(app->map_editor->editor_panel.ui, q, touch);
}

int
App_MapEditorTextSmooth(struct App const* app)
{
    assert(app);
    if( !app->map_editor || !MapEditorApp_PanelLive(app->map_editor) )
        return 0;
    return Editor_UiTextSmooth(app->map_editor->editor_panel.ui);
}

int
App_MapEditorKeepsWindowBox(struct App const* app)
{
    assert(app);
    return app->map_editor && MapEditorApp_PanelLive(app->map_editor);
}

void
App_MapEditorSetWindowBox(
    struct App* app,
    int x,
    int y,
    int w,
    int h)
{
    assert(app);
    assert(App_MapEditorKeepsWindowBox(app));
    Editor_UiSetWindowBox(app->map_editor->editor_panel.ui, x, y, w, h);
}

int
app_map_editor_panel_shown(struct App const* app)
{
    assert(app);
    return app->map_editor && MapEditorApp_PanelShown(app->map_editor);
}

unsigned
app_map_editor_vis_level_mask(struct App const* app)
{
    assert(app);
    if( !app->map_editor )
        return 0;
    return Editor_PanelVisLevelMask(&app->map_editor->editor_panel);
}

unsigned
app_map_editor_view_level_mask(struct App const* app)
{
    assert(app);
    if( !app->map_editor )
        return 0;
    return EditorView_AppLevelMask(app);
}

int
app_map_editor_record_loc_edit(
    struct App* app,
    int from_scene_x,
    int from_scene_z,
    int level,
    int loc_id,
    int shape,
    int from_angle,
    int to_scene_x,
    int to_scene_z,
    int to_angle)
{
    assert(app);
    if( !app->map_editor )
        return 0;
    return Editor_PanelRecordLocEdit(
        &app->map_editor->editor_panel,
        app,
        from_scene_x,
        from_scene_z,
        level,
        loc_id,
        shape,
        from_angle,
        to_scene_x,
        to_scene_z,
        to_angle);
}

/**
 * Whether the map editor's SELECT tool is the thing the minimenu should be
 * offering "Select wall/object/decor/terrain" rows for -- panel closed or a
 * paint tool active both mean no such row belongs on the menu, same as
 * the open loc editor's menu flag gates its own Select row
 * (app_tool_loc_editor.c).
 */
static bool
app_mapedit_select_active(struct App const* app)
{
    assert(app);
    return app->map_editor && app->map_editor->editor_panel.visible &&
           Editor_PanelTool(&app->map_editor->editor_panel) == EDITOR_TOOL_SELECT;
}

/* ---- the side-app -------------------------------------------------------- */

enum
{
    APP_MAP_EDITOR_TOGGLE
};

static struct SideAppAction const k_app_map_editor_actions[] = {
    [APP_MAP_EDITOR_TOGGLE] = { "map_editor_toggle", SIDE_APP_PHASE_DEV_PANELS, SIDE_APP_GATE_CTRL,
                                SIDE_APP_KEY_PRESS, 0 },
};

static struct SideAppHooks const k_app_map_editor_hooks = {
    .key = app_map_editor_side_app_key,
    .frame = app_map_editor_side_app_frame,
    .world_overlay = app_map_editor_side_app_world_overlay,
    .inspects_scenery = app_map_editor_side_app_inspects_scenery,
    .menu_flags = app_map_editor_side_app_menu_flags,
    .menu_action = app_map_editor_side_app_menu_action,
    .world_load = app_map_editor_side_app_world_load,
};

void
app_map_editor_side_app_register(struct App* app)
{
    assert(app);
    assert(app->side_apps);
    assert(MapEditorConfig_IsEditorBoot(&app->cfg.map_editor));
    SideApps_Register(
        app->side_apps,
        "map_editor",
        app,
        &k_app_map_editor_hooks,
        k_app_map_editor_actions,
        (int)(sizeof(k_app_map_editor_actions) / sizeof(k_app_map_editor_actions[0])));
}

/* Map editor panel. Gated on the session existing, so binding this key in a
 * manifest whose ToriRSMapEd did not answer is inert rather than a panel with
 * nothing behind it. */
static void
app_map_editor_side_app_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    struct App* app = app_from_side_app_host(host);

    assert(self == app);
    assert(action == APP_MAP_EDITOR_TOGGLE);
    (void)self;
    (void)action;
    (void)args;
    if( !app->map_editor )
        return;
    Editor_PanelSetVisible(
        &app->map_editor->editor_panel,
        &app->dev_overlay->ui,
        !app->map_editor->editor_panel.visible);
    RedrawRequest_Raise(&app->redraw_request);
}

static void
app_map_editor_side_app_frame(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase,
    struct LibToriRS_Input* input)
{
    struct App* app = app_from_side_app_host(host);

    assert(self == app);
    assert(input);
    (void)self;
    /* The map editor's hotkeys before every other key reader: a chord its
     * table claims (Ctrl+S) must be withheld from the manifest's debug
     * hotkeys the overlay, the loc editor and the camera read below, and its
     * keys are marked consumed for the world hotkeys (app_editor_hotkeys.c). */
    if( phase == SIDE_APP_PHASE_FIRST_KEYS )
        app_editor_hotkeys_frame(app, input);
    else if( phase == SIDE_APP_PHASE_POINTER_LATCHED && app->map_editor )
        app_map_editor_side_app_pointer_latched(app, input);
}

/* Map editor panel after the loc editor, for the same reason and in the
 * same order it is drawn: both read this frame's hover, and the map editor
 * acts on activations the overlay latched during the dev chrome's ticks. */
static void
app_map_editor_side_app_pointer_latched(
    struct App* app,
    struct LibToriRS_Input* input)
{
    /* A viewer window's toolbar, camera read-back and broadcast; it takes
     * the frame's activation only while the command panels are hidden. */
    EditorView_AppFrame(app);
    /* The gesture frames before the editor's tick: the chrome's events
     * they queue (a picker zoom, a Preview orbit) drain in it. */
    app_map_editor_gestures(app, input);
    Editor_PanelTick(&app->map_editor->editor_panel, app);
    app_map_editor_world_click(app, input);
    {
        uint64_t const t_hover = FrameStats_Now();
        app_map_editor_ghost_update(app);
        app_map_editor_preview_update(app);
        /* The small pictures of records (app_record_thumbs.c: the Gallery's
         * tiles, the Catalog's npc rows) and the Catalog palette's slots. */
        app_record_thumbs_update(app);
        app_catalog_palette_update(app);
        FrameStats_Span(FRAME_STATS_HOVER_GHOSTS, t_hover);
    }
    app_map_editor_open_pending_square(app);
    app_map_editor_drain(app);
    /*
     * The chrome built once more, AFTER the editor's tick. The build in
     * app_dev_panels_tick ran before it, on this frame's pointer, so
     * everything the editor's tick then did to the chrome -- a well sized
     * to the panel it sits in, a label, a group's window shown -- would
     * otherwise be drawn a frame late, and geometry a frame late is
     * visible: a resized panel's bottom trailing its top. A build on a
     * frame where the tick changed nothing is a no-op (the dirty flag).
     */
    if( ToriRSChrome_Build(&app->dev_overlay->ui) )
    {
        RedrawRequest_Raise(&app->redraw_request);
        ToriRSChrome_DamageClear(&app->dev_overlay->ui);
    }
}

/* Whatever tool is active, while the panel is shown
 * (app_overlay_build_editor_selection tests it). */
static void
app_map_editor_side_app_world_overlay(
    void* self,
    struct SideAppHost* host)
{
    struct App* app = app_from_side_app_host(host);

    assert(self == app);
    (void)self;
    app_overlay_build_editor_selection(app);
}

/* The MAP editor makes every loc pickable too: its Select/Delete minimenu
 * rows are built per PICKED element, so a wall or roof with no ops of its
 * own -- invisible to the pick without this flag -- could never grow a
 * "Select Wall" row however the menu was gated. */
static int
app_map_editor_side_app_inspects_scenery(void const* self)
{
    struct App const* app = self;

    assert(app);
    return app_map_editor_panel_shown(app);
}

static void
app_map_editor_side_app_menu_flags(
    void const* self,
    void* build_ctx)
{
    struct App const* app = self;
    struct RS_MinimenuBuildCtx* ctx = build_ctx;

    assert(app);
    assert(ctx);
    ctx->mapedit_select_active = app_mapedit_select_active(app);
}

/*
 * The SELECT tool's pair of the loc editor's two rows -- same shape, a
 * different panel's latch. The `app_mapedit_select_active` re-check guards a
 * row clicked in the one frame the tool closed or switched off SELECT. And a
 * previewed spawn's row: any tool, the Spawns selection takes it. Each is
 * handled locally; no CS2 task is dispatched.
 */
static int
app_map_editor_side_app_menu_action(
    void* self,
    struct SideAppHost* host,
    int action,
    void const* option)
{
    struct App* app = app_from_side_app_host(host);
    struct UIMinimenuOption const* opt = option;

    assert(self == app);
    assert(opt);
    (void)self;
    if( action == RS_MINIMENU_ACTION_MAPEDIT_SELECT )
    {
        if( app_mapedit_select_active(app) )
            Editor_PanelSelectLoc(&app->map_editor->editor_panel, app, opt->pick.id);
        return 1;
    }
    if( action == RS_MINIMENU_ACTION_MAPEDIT_SELECT_TERRAIN )
    {
        if( app_mapedit_select_active(app) )
            Editor_PanelSelectTerrain(
                &app->map_editor->editor_panel,
                app,
                opt->pick.secondary_id,
                opt->pick.tertiary_id,
                opt->pick.quaternary_id);
        return 1;
    }
    if( action == RS_MINIMENU_ACTION_MAPEDIT_SELECT_SPAWN )
    {
        /* Only the editor previews spawns, so only an open editor has such a
         * row to answer. */
        if( app->map_editor )
            app_map_editor_spawn_preview_select_element(app, opt->pick.id);
        return 1;
    }
    return 0;
}

/*
 * BEGIN, ahead of the drain, so what it removes goes with this scene.
 *
 * SQUARES: editor boots seed the provider from the content tree before the
 * load runs, so what gets meshed is the `.jm2`/`.jl2` text being edited
 * rather than the last bake. Task_WorldLoad skips a square the provider
 * already holds, so the text wins simply by being there first -- the editor
 * never has to invalidate or race the cache path, and an unsaved edit is
 * visible without a bake. A square the content tree does not carry is left
 * alone and loads from the cache as usual, which is what lets an editor
 * session sit at the edge of authored content and still see the world around
 * it.
 */
static void
app_map_editor_side_app_world_load(
    void* self,
    struct SideAppHost* host,
    enum SideAppWorldLoadPoint point,
    int const* squares_xz,
    int square_count)
{
    struct App* app = app_from_side_app_host(host);

    assert(self == app);
    (void)self;
    if( !app->map_editor )
        return;
    if( point == SIDE_APP_WORLD_LOAD_BEGIN )
    {
        app_map_editor_world_load_begin(app);
        return;
    }
    for( int i = 0; i < square_count; i++ )
    {
        Editor_LoadSquare(app->map_editor->session, app->provider, squares_xz[i * 2], squares_xz[i * 2 + 1]);
        /* EDIT layout: the square's first shadow, taken with the seed so
         * the flush can tell what later commands changed. A no-op under
         * STATIC and for a square that already has one. */
        app_map_editor_shadow_seed(app, squares_xz[i * 2], squares_xz[i * 2 + 1]);
    }
}
