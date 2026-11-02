/*
 * The main window's App face: builds the panel registry (editor_ui.h) and
 * its modules (panels/), implements the hooks they ask of the window (open a
 * square, floor colours, bake, the view window), keeps the SELECT latch the
 * overlay and minimenu read in step with the document-address selection, and
 * turns the world actions the minimenu and the loc editor invoke into
 * commands.
 *
 * Every edit here is a command (panels/edpanel_ops.h, panels/edpanel_actions.c),
 * never a runtime scene change: the world shows the document through the EDIT
 * flush (app_map_editor.c), and a runtime App_WorldLocChange on top would put
 * a second, default-lit copy in the scene.
 */

#include "editor_panel.h"

#include "app.h"
#include "app/app_internal.h"
#include "builder/builder_panels.h"
#include "editor_view.h"
#include "engine/world_builder/world_builder.h"
#include "log/torirs_log.h"
#include "mem/torirs_mem.h"
#include "panels/edpanel_common.h"
#include "panels/edpanel_ops.h"
#include "panels/edpanels.h"
#include "torirsmaped/torirs_maped.h"
#include "ui/uitree_debug_overlay.h"
#include "world/world.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/*
 * The selection relay speaks ABSOLUTE WORLD TILES, never scene coordinates:
 * a scene coordinate is an offset from one load region's base, meaningless to
 * a viewer showing another region and to a controller with no world. These
 * two converters are the only place a scene coordinate crosses that line.
 */
static int
panel_scene_to_world(
    struct App const* app,
    int scene_x,
    int scene_z,
    int* out_world_x,
    int* out_world_z)
{
    assert(app);
    assert(out_world_x);
    assert(out_world_z);

    if( !app->world )
        return 0;
    *out_world_x = app->world->_base_tile_x + scene_x;
    *out_world_z = app->world->_base_tile_z + scene_z;
    return *out_world_x >= 0 && *out_world_z >= 0;
}

static int
panel_world_to_scene(
    struct App const* app,
    int world_x,
    int world_z,
    int* out_scene_x,
    int* out_scene_z)
{
    int scene_x;
    int scene_z;
    int span;

    assert(app);
    assert(out_scene_x);
    assert(out_scene_z);

    if( !app->world )
        return 0;
    scene_x = world_x - app->world->_base_tile_x;
    scene_z = world_z - app->world->_base_tile_z;
    span = app->world->_scene_size;
    if( scene_x < 0 || scene_z < 0 || (span > 0 && (scene_x >= span || scene_z >= span)) )
        return 0;
    *out_scene_x = scene_x;
    *out_scene_z = scene_z;
    return 1;
}

/** Announce the primary latch (TORIRSMAPED_STATE_SELECTION, absolute tiles). */
static void
panel_publish_selection(
    struct Editor_Panel* panel,
    struct App const* app)
{
    int32_t values[7];
    int world_x = 0;
    int world_z = 0;

    assert(panel);
    assert(app);

    if( !panel->editor || panel->applying_shared_state )
        return;
    if( panel->sel_kind != EDITOR_SELECTION_NONE
        && !panel_scene_to_world(app, panel->sel_scene_x, panel->sel_scene_z, &world_x, &world_z) )
        return;
    values[0] = (int32_t)panel->sel_kind;
    values[1] = world_x;
    values[2] = world_z;
    values[3] = panel->sel_level;
    values[4] = panel->sel_loc_id;
    values[5] = panel->sel_shape;
    values[6] = panel->sel_angle;
    Editor_StateSet(panel->editor, TORIRSMAPED_STATE_SELECTION, values, 7);
}

/** The primary latch from the multi-selection's first loc. */
static void
panel_sync_primary(
    struct Editor_Panel* panel,
    struct App* app)
{
    struct EditorUi* ui = panel->ui;

    if( ui->tools.loc_count > 0 )
    {
        const struct Editor_LocRef* ref = &ui->tools.locs[0];
        int world_x;
        int world_z;
        int scene_x = 0;
        int scene_z = 0;

        Editor_LocRefWorld(ref, &world_x, &world_z);
        if( app->world )
            (void)panel_world_to_scene(app, world_x, world_z, &scene_x, &scene_z);
        panel->sel_kind = EDITOR_SELECTION_LOC;
        panel->sel_scene_x = scene_x;
        panel->sel_scene_z = scene_z;
        panel->sel_level = ref->level;
        panel->sel_loc_id = ref->loc_id;
        panel->sel_shape = ref->shape;
        panel->sel_angle = ref->rotation;
        /* Healed by the overlay from tile and shape once the flush placed it. */
        panel->sel_element_id = -1;
    }
    else if( panel->sel_kind == EDITOR_SELECTION_LOC )
    {
        /* The loc went; its tile stays the subject. */
        panel->sel_kind = ui->tools.tile_valid ? EDITOR_SELECTION_TERRAIN : EDITOR_SELECTION_NONE;
        panel->sel_element_id = -1;
        panel->sel_loc_id = -1;
    }
    app->need_redraw = 1;
    panel_publish_selection(panel, app);
}

/* ---- the window's hooks ----------------------------------------------------- */

static void
hook_open_square(
    void* user,
    int map_x,
    int map_z,
    const char* storage,
    int add)
{
    struct App* app = user;
    struct Editor_Panel* panel = &app->editor_panel;

    assert(app);
    panel->sq_open_pending = 1;
    panel->sq_open_x = map_x;
    panel->sq_open_z = map_z;
    panel->sq_open_add = add;
    snprintf(panel->sq_open_storage, sizeof(panel->sq_open_storage), "%s", storage ? storage : "");
}

static void
hook_open_in_new_view(
    void* user,
    int map_x,
    int map_z)
{
    struct App* app = user;
    struct EditorViewCamera camera;

    assert(app);
    assert(app->editor); /* the panels tick only with a session */
    /* Looking down at the square's middle from the default orbit
     * (editor/editor_view.h: absolute fine units, yaw, pitch, distance). */
    memset(&camera, 0, sizeof(camera));
    camera.x = (map_x * 64 + 32) * 128;
    camera.z = (map_z * 64 + 32) * 128;
    camera.level = 0;
    camera.yaw = 0;
    camera.pitch = 383;
    camera.zoom = 2600;
    if( EditorView_AppSpawn(app, &camera) )
        Editor_UiLog(app->editor_panel.ui, "editor: opening a new view of m%d_%d", map_x, map_z);
    else
        Editor_UiLog(app->editor_panel.ui, "editor: Open in new view (m%d_%d) could not start one; see the log",
            map_x, map_z);
}

static int
hook_floor_rgb(
    void* user,
    int is_overlay,
    int id,
    uint32_t* out_rgb)
{
    struct App* app = user;
    struct ToriRS_Flotype* flo;

    assert(app);
    assert(out_rgb);
    if( !app->provider || id <= 0 )
        return 0;
    /* A `.jm2` id is the config id + 1 (0 is "none"). */
    flo = is_overlay ? CacheProvider_FlotypeGet(app->provider, id - 1)
                     : CacheProvider_UnderlayGet(app->provider, id - 1);
    if( !flo )
        return 0;
    *out_rgb = (uint32_t)flo->rgb_color & 0xffffff;
    return 1;
}

static void
hook_close_editor(void* user)
{
    struct App* app = user;
    assert(app);
    Editor_PanelSetVisible(&app->editor_panel, &app->dbg_ui, 0);
}

static void
hook_bake_progress(
    void* user_data,
    const char* line)
{
    (void)user_data;
    TORIRS_LOG("bake: %s\n", line);
}

static void
hook_bake(void* user)
{
    struct App* app = user;
    assert(app);
    if( !app->editor )
        return;
    Editor_UiLog(app->editor_panel.ui, "editor: baking, this takes a while");
    Editor_UiLog(app->editor_panel.ui, "editor: bake %s",
        Editor_Bake(app->editor, hook_bake_progress, NULL) ? "finished" : "FAILED");
}

static int
hook_view_window(
    void* user,
    int* out_map_x,
    int* out_map_z,
    int* out_w,
    int* out_h)
{
    struct App* app = user;
    struct World* world;

    assert(app);
    world = app->world;
    if( !world || !app->world_active )
        return 0;
    if( world->_chunk_ne_x < world->_chunk_sw_x || world->_chunk_ne_z < world->_chunk_sw_z )
        return 0;
    *out_map_x = world->_chunk_sw_x;
    *out_map_z = world->_chunk_sw_z;
    *out_w = world->_chunk_ne_x - world->_chunk_sw_x + 1;
    *out_h = world->_chunk_ne_z - world->_chunk_sw_z + 1;
    return 1;
}

static void
hook_selection_changed(void* user)
{
    struct App* app = user;
    assert(app);
    panel_sync_primary(&app->editor_panel, app);
}

/* ---- lifetime ---------------------------------------------------------------- */

void
Editor_PanelInit(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui)
{
    struct App* app;

    assert(panel);
    assert(ui);

    /* The App embeds the panel: its address is the App's, by container. */
    app = (struct App*)((char*)panel - offsetof(struct App, editor_panel));

    memset(panel, 0, sizeof(*panel));
    panel->sel_element_id = -1;
    panel->sel_loc_id = -1;
    panel->pin_hover_level = -1;

    panel->ui = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*panel->ui));
    assert(panel->ui);
    Editor_UiInit(panel->ui, ui, app->editor);
    panel->ui->app = app;
    panel->ui->hooks_user = app;
    panel->ui->hooks.open_square = hook_open_square;
    panel->ui->hooks.open_in_new_view = hook_open_in_new_view;
    panel->ui->hooks.floor_rgb = hook_floor_rgb;
    panel->ui->hooks.close_editor = hook_close_editor;
    panel->ui->hooks.bake = hook_bake;
    panel->ui->hooks.view_window = hook_view_window;
    panel->ui->hooks.selection_changed = hook_selection_changed;
    /* What the App-bound modules ask of the App, before they register. */
    app_map_editor_ui_services(app, &panel->ui->app_services);
    Editor_UiRegisterToolsPanels(panel->ui);
    /* The builders' Check button compiles against a stage of the content
     * tree's scripts with this checkout's sscompile (builder_ssc.h); the
     * TORIRS_BUILDER_* env vars do the same where the manifest names neither. */
    if( app->cfg.editor_content_dir && app->cfg.editor_content_dir[0] && app->cfg.editor_repo_root
        && app->cfg.editor_repo_root[0] )
    {
        char ssc[1024];
        char work[1024];
        snprintf(ssc, sizeof(ssc), "%s/src/build_opt/sscompile", app->cfg.editor_repo_root);
        snprintf(work, sizeof(work), "%s/src/build_opt/tmp/builders", app->cfg.editor_repo_root);
        Builder_PanelsSetCompile(panel->ui, ssc, app->cfg.editor_content_dir, work);
        /* The Triggers panel's Check: the same compiler, its own stage. */
        snprintf(work, sizeof(work), "%s/src/build_opt/tmp/triggers_check", app->cfg.editor_repo_root);
        (void)Editor_Triggers_SetPaths(panel->ui, app->cfg.editor_content_dir, ssc, work);
    }
    else if( app->cfg.editor_content_dir && app->cfg.editor_content_dir[0] )
        (void)Editor_Triggers_SetPaths(panel->ui, app->cfg.editor_content_dir, NULL, NULL);
    panel->built = 1;
}

void
Editor_PanelFree(struct Editor_Panel* panel)
{
    if( !panel || !panel->ui )
        return;
    Editor_UiFree(panel->ui);
    ToriMem_Free(ToriMem_Editor(), panel->ui);
    panel->ui = NULL;
    panel->built = 0;
}

void
Editor_PanelSetVisible(
    struct Editor_Panel* panel,
    struct ToriRSChrome* ui,
    int visible)
{
    assert(panel);
    assert(ui);
    (void)ui;

    if( !panel->built )
        return;
    panel->visible = visible ? 1 : 0;
    Editor_UiSetVisible(panel->ui, panel->visible);
}

void
Editor_PanelTick(
    struct Editor_Panel* panel,
    struct App* app)
{
    assert(panel);
    assert(app);

    if( !panel->built || !app->editor )
        return;
    /* The session can arrive after Init (app.c sets it); so can the editor. */
    panel->ui->editor = app->editor;
    if( !panel->visible )
    {
        /* A viewer (or this window with its panels closed) still frames its
         * window in every picker. */
        Editor_UiPublishView(panel->ui);
        return;
    }
    panel->ui->shift_held = app->view_split.physical_input.key_held[TORIRSK_SHIFT] != 0;
    /* A viewer that pressed "Open panels" shares the frame's one activation
     * between its thin toolbar and the panels: the toolbar is offered it
     * first, and what it does not claim goes back for the registry. */
    {
        int const activated = ToriRSChrome_TakeActivated(panel->ui->chrome);
        if( activated >= 0 && !EditorView_AppActivate(app, activated) )
            panel->ui->chrome->activated = activated;
    }
    Editor_UiTick(panel->ui);
}

enum Editor_Tool
Editor_PanelTool(const struct Editor_Panel* panel)
{
    assert(panel);
    if( !panel->ui )
        return EDITOR_TOOL_SELECT;
    return panel->ui->tools.tool;
}

/* ---- the latch -------------------------------------------------------------- */

void
Editor_PanelSelectTerrain(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    int world_x;
    int world_z;

    assert(panel);
    assert(app);

    /* An armed spawn move takes this window's own click (not a relayed
     * selection): the Spawns panel re-tiles its selection there. */
    if( !panel->applying_shared_state && panel->ui && Editor_Spawns_MoveArmed(panel->ui)
        && panel_scene_to_world(app, scene_x, scene_z, &world_x, &world_z) )
    {
        (void)Editor_Spawns_MoveTo(panel->ui, world_x, world_z, level);
        return;
    }

    panel->sel_kind = EDITOR_SELECTION_TERRAIN;
    panel->sel_scene_x = scene_x;
    panel->sel_scene_z = scene_z;
    panel->sel_level = level;
    panel->sel_element_id = -1;
    panel->sel_loc_id = -1;
    if( panel->ui )
    {
        panel->ui->tools.loc_count = 0;
        if( panel_scene_to_world(app, scene_x, scene_z, &world_x, &world_z) )
        {
            panel->ui->tools.tile_valid = 1;
            panel->ui->tools.tile_x = world_x;
            panel->ui->tools.tile_z = world_z;
            panel->ui->tools.tile_level = level;
        }
        else if( !app->world )
        {
            /* A controller with no world stores the absolute tile it was
             * told (Editor_PanelApplySharedState passes it through). */
            panel->ui->tools.tile_valid = 1;
            panel->ui->tools.tile_x = scene_x;
            panel->ui->tools.tile_z = scene_z;
            panel->ui->tools.tile_level = level;
        }
    }
    panel_publish_selection(panel, app);
}

/** The loc under a scene tile as a document ref; `shape` -1 is any layer. */
static int
panel_loc_ref_at(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int shape,
    struct Editor_LocRef* out)
{
    struct WorldEntity_Scenery* scenery;
    int world_x;
    int world_z;
    int idx;

    if( !app->world || !panel_scene_to_world(app, scene_x, scene_z, &world_x, &world_z) )
        return 0;
    idx = World_SceneryFindAt(app->world, scene_x, scene_z, level, shape);
    if( idx < 0 )
        return 0;
    scenery = World_EntityPoolGet(&app->world->entities.scenery, idx);
    if( !scenery )
        return 0;
    Editor_LocRefAt(out, world_x, world_z, level, scenery->shape, scenery->loc_id, scenery->angle & 3);
    return 1;
}

/**
 * The loc the pointer is ON: the front-most scenery model the last rendered
 * frame hittested under the pointer, on `level` -- what the user sees and
 * clicks. A Delete or Rotate click on a tree's canopy names the tree, not
 * whatever loc is filed first on the terrain tile behind it (a flower patch
 * under the tree's corner, which was being deleted while the tree stayed).
 *
 * Ground decor (shape 22) is flat on the floor and its hit box reaches out
 * from under anything standing over it, so it is taken only when no other
 * model is under the pointer. With no model at all, the first loc on the
 * hovered tile.
 */
static int
panel_loc_ref_pointed(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    struct Editor_LocRef* out)
{
    struct World_PickSet const* picks;
    struct WorldEntity_Scenery* best = NULL;

    if( !app->world )
        return 0;
    picks = &app->frame_view->world_pickset;
    if( picks->mouse_valid && picks->mouse_x == app->frame_view->world_mouse_x
        && picks->mouse_y == app->frame_view->world_mouse_y )
    {
        /* Hits arrive back to front: the last is nearest. */
        for( int i = picks->count - 1; i >= 0; i-- )
        {
            struct World_Picked const* hit = &picks->items[i];
            struct WorldEntity_Scenery* scenery;
            if( hit->type != WORLD_PICK_SCENERY || hit->view_id != 0 )
                continue;
            scenery = World_SceneryGetByElementId(app->world, hit->element_id);
            if( !scenery || scenery->grid_position.level != level )
                continue;
            if( scenery->shape != RSCACHE_LOC_SHAPE_FLOOR_DECORATION )
            {
                best = scenery;
                break;
            }
            if( !best )
                best = scenery;
        }
    }
    if( best )
    {
        int world_x;
        int world_z;
        if( panel_scene_to_world(app, best->grid_position.x, best->grid_position.z, &world_x, &world_z) )
        {
            Editor_LocRefAt(out, world_x, world_z, level, best->shape, best->loc_id, best->angle & 3);
            return 1;
        }
    }
    return panel_loc_ref_at(app, scene_x, scene_z, level, -1, out);
}

void
Editor_PanelSelectLoc(
    struct Editor_Panel* panel,
    struct App* app,
    int element_id)
{
    struct WorldEntity_Scenery* scenery;
    struct Editor_LocRef ref;
    int world_x;
    int world_z;

    assert(panel);
    assert(app);

    if( !app->world || !panel->ui )
        return;
    scenery = World_SceneryGetByElementId(app->world, element_id);
    if( !scenery )
        return;
    if( !panel_scene_to_world(app, scenery->grid_position.x, scenery->grid_position.z, &world_x, &world_z) )
        return;
    Editor_LocRefAt(
        &ref, world_x, world_z, scenery->grid_position.level, scenery->shape, scenery->loc_id,
        scenery->angle & 3);
    if( panel->ui->shift_held )
        (void)Editor_UiToggleLoc(panel->ui, &ref);
    else
        Editor_UiSelectLoc(panel->ui, &ref);
    panel_sync_primary(panel, app);
    if( panel->ui->tools.loc_count > 0 && panel->sel_loc_id == scenery->loc_id )
        panel->sel_element_id = element_id;
    /* The catalog follows the selection: selecting a maploc picks that loc. */
    Editor_Catalog_Adopt(panel->ui, scenery->loc_id);
}

void
Editor_PanelClearSelection(
    struct Editor_Panel* panel,
    struct App* app)
{
    assert(panel);
    assert(app);

    panel->sel_kind = EDITOR_SELECTION_NONE;
    panel->sel_element_id = -1;
    panel->sel_loc_id = -1;
    if( panel->ui )
    {
        panel->ui->tools.loc_count = 0;
        panel->ui->tools.tile_valid = 0;
    }
    panel_publish_selection(panel, app);
}

int
Editor_PanelEditLevel(
    struct Editor_Panel const* panel,
    struct App const* app)
{
    int hover_level;

    assert(panel);
    assert(app);
    hover_level = app->world ? app->frame_view->world_hover_tile_level : 0;
    if( !panel->ui )
        return hover_level;
    return edp_level_click(panel->ui, hover_level);
}

int
Editor_PanelHoverTile(
    struct Editor_Panel const* panel,
    struct App const* app,
    int* out_scene_x,
    int* out_scene_z)
{
    assert(panel);
    assert(app);
    assert(out_scene_x);
    assert(out_scene_z);
    if( !app->world )
        return 0;
    if( panel->ui && panel->ui->tools.edit_level >= 0 )
    {
        if( !panel->pin_hover_valid || panel->pin_hover_level != panel->ui->tools.edit_level )
            return 0;
        *out_scene_x = panel->pin_hover_x;
        *out_scene_z = panel->pin_hover_z;
        return 1;
    }
    if( app->frame_view->world_hover_tile_x < 0 || app->frame_view->world_hover_tile_z < 0 )
        return 0;
    *out_scene_x = app->frame_view->world_hover_tile_x;
    *out_scene_z = app->frame_view->world_hover_tile_z;
    return 1;
}

uint8_t
Editor_PanelVisLevelMask(struct Editor_Panel const* panel)
{
    int level;

    assert(panel);
    if( !panel->ui || panel->ui->tools.vis_level < 0 )
        return 0;
    level = panel->ui->tools.vis_level;
    if( panel->ui->tools.vis_solo )
        return (uint8_t)(1u << level);
    /* Cumulative, as the game's view floor builds it: VIS_BELOW tiles read
     * right only against levels 0..cap. */
    return (uint8_t)((1u << (level + 1)) - 1);
}

void
Editor_PanelGhostSpec(
    struct Editor_Panel* panel,
    struct App* app,
    int* out_shape,
    int* out_angle)
{
    assert(panel);
    assert(app);
    (void)app;
    assert(out_shape);
    assert(out_angle);
    *out_shape = panel->ui ? panel->ui->tools.place_shape : 10;
    *out_angle = panel->ui ? panel->ui->tools.place_rotation & 3 : 0;
}

/* ---- world actions ---------------------------------------------------------- */

int
Editor_PanelPlaceLocAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    struct EditorUi* ui;
    int world_x;
    int world_z;

    assert(panel);
    assert(app);
    ui = panel->ui;
    if( !ui || !panel_scene_to_world(app, scene_x, scene_z, &world_x, &world_z) )
        return 0;
    if( ui->tools.pick_kind == 1 || ui->tools.pick_kind == 2 )
        return Editor_Locs_PlaceSpawn(ui, world_x, world_z, level);
    if( ui->tools.pick_kind != 0 )
    {
        Editor_UiLog(ui, "editor: pick a loc in the catalog first");
        return 0;
    }
    return edp_locs_place(
        ui, ui->tools.pick_id, ui->tools.place_shape, ui->tools.place_rotation, world_x, world_z, level);
}

int
Editor_PanelDeleteLocAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    struct Editor_LocRef ref;
    struct Editor_Cmd command;
    struct Editor_LocOpResult result;

    assert(panel);
    assert(app);
    if( !panel->ui || !app->editor || !panel_loc_ref_pointed(app, scene_x, scene_z, level, &ref) )
        return 0;
    if( !Editor_LocOpsDelete(&app->editor->doc, &ref, 1, &command, 1, &result) )
        return 0;
    return edp_apply(panel->ui, &command, 1, "delete loc");
}

int
Editor_PanelClearLocsAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    struct Editor_LocRef ref;
    struct Editor_Square* square;
    struct Editor_Cmd commands[16];
    int count = 0;

    assert(panel);
    assert(app);
    if( !panel->ui || !app->editor )
        return 0;
    if( !panel_scene_to_world(app, scene_x, scene_z, &ref.x, &ref.z) )
        return 0;
    Editor_LocRefAt(&ref, ref.x, ref.z, level, 0, -1, 0);
    square = Editor_DocFindSquare(&app->editor->doc, ref.map_x, ref.map_z);
    if( !square || !square->loaded )
        return 0;
    /* Every layer on the tile, from the document: one undo step. */
    for( int i = 0; i < square->loc_count && count < 16; i++ )
    {
        const struct Editor_Loc* loc = &square->locs[i];
        if( loc->level != level || loc->x != ref.x || loc->z != ref.z )
            continue;
        Editor_CmdInitSquare(&commands[count], EDITOR_CMD_LOC, ref.map_x, ref.map_z);
        commands[count].has_before = 1;
        commands[count].u.loc.before = *loc;
        count++;
    }
    return edp_apply(panel->ui, commands, count, "clear tile locs");
}

int
Editor_PanelDeleteSelection(
    struct Editor_Panel* panel,
    struct App* app)
{
    assert(panel);
    assert(app);
    (void)app;
    if( !panel->ui )
        return 0;
    return edp_locs_delete(panel->ui) > 0;
}

int
Editor_PanelDeleteLocByElement(
    struct Editor_Panel* panel,
    struct App* app,
    int element_id)
{
    struct WorldEntity_Scenery* scenery;
    struct Editor_LocRef ref;
    struct Editor_Cmd command;
    struct Editor_LocOpResult result;
    int world_x;
    int world_z;

    assert(panel);
    assert(app);
    if( !panel->ui || !app->world || !app->editor )
        return 0;
    scenery = World_SceneryGetByElementId(app->world, element_id);
    if( !scenery )
        return 0;
    if( !panel_scene_to_world(app, scenery->grid_position.x, scenery->grid_position.z, &world_x, &world_z) )
        return 0;
    Editor_LocRefAt(&ref, world_x, world_z, scenery->grid_position.level, scenery->shape, scenery->loc_id,
        scenery->angle & 3);
    if( !Editor_LocOpsDelete(&app->editor->doc, &ref, 1, &command, 1, &result) )
    {
        Editor_UiLog(panel->ui, "editor: that loc is not in an open square");
        return 0;
    }
    if( !edp_apply(panel->ui, &command, 1, "delete loc") )
        return 0;
    if( Editor_UiFindLoc(panel->ui, &ref) >= 0 )
    {
        (void)Editor_UiToggleLoc(panel->ui, &ref);
        panel_sync_primary(panel, app);
    }
    return 1;
}

int
Editor_PanelRecordLocEdit(
    struct Editor_Panel* panel,
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
    struct Editor_LocRef ref;
    struct Editor_Cmd commands[2];
    struct Editor_LocOpResult result;
    int from_x;
    int from_z;
    int to_x;
    int to_z;
    int count;

    assert(panel);
    assert(app);
    if( !panel->ui || !app->editor )
        return 0;
    if( !panel_scene_to_world(app, from_scene_x, from_scene_z, &from_x, &from_z)
        || !panel_scene_to_world(app, to_scene_x, to_scene_z, &to_x, &to_z) )
        return 0;
    Editor_LocRefAt(&ref, from_x, from_z, level, shape, loc_id, from_angle & 3);
    if( to_x != from_x || to_z != from_z )
        count = Editor_LocOpsNudge(&app->editor->doc, &ref, 1, to_x - from_x, to_z - from_z, commands, 2, &result);
    else
        count = Editor_LocOpsRotate(
            &app->editor->doc, &ref, 1, (to_angle - from_angle) & 3, commands, 2, &result);
    return edp_apply(panel->ui, commands, count, "loc edit") > 0;
}

int
Editor_PanelApplyToolAt(
    struct Editor_Panel* panel,
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    struct EditorUi* ui;
    int world_x;
    int world_z;

    assert(panel);
    assert(app);
    ui = panel->ui;
    if( !ui || !app->editor || !panel_scene_to_world(app, scene_x, scene_z, &world_x, &world_z) )
        return 0;

    if( Editor_ToolIsBrush(ui->tools.tool) )
    {
        int capacity;
        struct Editor_Cmd* commands = edp_scratch(&capacity);
        struct Editor_BrushOp op;
        int count;

        memset(&op, 0, sizeof(op));
        op.tool = ui->tools.tool;
        op.world_x = world_x;
        op.world_z = world_z;
        op.level = level < 0 ? 0 : level > 3 ? 3 : level;
        op.brush = ui->tools.brush;
        if( ui->tools.tool == EDITOR_TOOL_FLATTEN )
        {
            if( !ui->tools.flatten_valid
                && Editor_OpsDerivedHeight(&app->editor->doc, world_x, world_z, op.level, &ui->tools.flatten_height) )
                ui->tools.flatten_valid = 1;
            if( !ui->tools.flatten_valid )
                return 0;
            op.flatten_height = ui->tools.flatten_height;
        }
        op.underlay_id = ui->tools.underlay_id;
        op.overlay_id = ui->tools.overlay_id;
        op.overlay_none = ui->tools.overlay_none;
        op.overlay_shape = ui->tools.overlay_shape;
        op.overlay_rotation = ui->tools.overlay_rotation;
        op.flags_value = ui->tools.flags_value;
        count = Editor_BrushCommands(&app->editor->doc, &op, commands, capacity);
        if( count == 0 && !Editor_DocFindSquare(&app->editor->doc, world_x / 64, world_z / 64) )
            Editor_UiLog(ui, "editor: m%d_%d is not open in the editor", world_x / 64, world_z / 64);
        return edp_apply(ui, commands, count, Editor_ToolName(ui->tools.tool));
    }

    /* An armed spawn move takes the click whatever tool is up. */
    if( Editor_Spawns_MoveArmed(ui) )
        return Editor_Spawns_MoveTo(ui, world_x, world_z, level);

    switch( ui->tools.tool )
    {
    case EDITOR_TOOL_LOC_PLACE:
        return Editor_PanelPlaceLocAt(panel, app, scene_x, scene_z, level);
    case EDITOR_TOOL_LOC_MOVE:
        return edp_locs_move_to(ui, world_x, world_z);
    case EDITOR_TOOL_LOC_ROTATE:
    case EDITOR_TOOL_LOC_DELETE:
    {
        struct Editor_LocRef ref;
        if( !panel_loc_ref_pointed(app, scene_x, scene_z, level, &ref) )
        {
            Editor_UiLog(ui, "editor: no loc on that tile");
            return 0;
        }
        Editor_UiSelectLoc(ui, &ref);
        panel_sync_primary(panel, app);
        return ui->tools.tool == EDITOR_TOOL_LOC_ROTATE ? edp_locs_rotate(ui) : edp_locs_delete(ui);
    }
    default:
        return 0;
    }
}

int
Editor_PanelApplyToSelection(
    struct Editor_Panel* panel,
    struct App* app)
{
    int scene_x;
    int scene_z;

    assert(panel);
    assert(app);
    if( !panel->ui )
        return 0;
    if( !Editor_ToolIsBrush(panel->ui->tools.tool) || !panel->ui->tools.tile_valid )
    {
        Editor_UiLog(panel->ui, "editor: latch a tile and arm a brush; E applies it there");
        return 0;
    }
    if( !panel_world_to_scene(app, panel->ui->tools.tile_x, panel->ui->tools.tile_z, &scene_x, &scene_z) )
        return 0;
    panel->ui->tools.flatten_valid = 0;
    return Editor_PanelApplyToolAt(panel, app, scene_x, scene_z, panel->ui->tools.tile_level);
}

/* ---- the selection relay ---------------------------------------------------- */

void
Editor_PanelApplySharedState(
    struct Editor_Panel* panel,
    struct App* app,
    uint32_t key,
    const int32_t* values,
    int count)
{
    assert(panel);
    assert(app);
    assert(values || count == 0);

    if( panel->ui )
        Editor_UiApplyState(panel->ui, key, values, count);
    if( key != TORIRSMAPED_STATE_SELECTION || count < 7 || !panel->ui )
        return;

    panel->applying_shared_state = 1;
    if( values[0] == EDITOR_SELECTION_NONE )
    {
        Editor_PanelClearSelection(panel, app);
        panel->applying_shared_state = 0;
        return;
    }

    /* A viewer showing another region keeps its own selection; a controller
     * with no world stores the absolute tile. */
    {
        int scene_x = values[1];
        int scene_z = values[2];

        if( app->world && !panel_world_to_scene(app, values[1], values[2], &scene_x, &scene_z) )
        {
            panel->applying_shared_state = 0;
            return;
        }
        if( values[0] == EDITOR_SELECTION_TERRAIN )
            Editor_PanelSelectTerrain(panel, app, scene_x, scene_z, values[3]);
        else if( values[0] == EDITOR_SELECTION_LOC )
        {
            struct Editor_LocRef ref;
            if( values[1] >= 0 && values[2] >= 0 )
            {
                Editor_LocRefAt(&ref, values[1], values[2], values[3], values[5], values[4], values[6] & 3);
                Editor_UiSelectLoc(panel->ui, &ref);
                panel_sync_primary(panel, app);
            }
        }
        else if( values[0] == EDITOR_SELECTION_NPC || values[0] == EDITOR_SELECTION_OBJ )
        {
            panel->sel_kind = (enum Editor_SelectionKind)values[0];
            panel->sel_scene_x = scene_x;
            panel->sel_scene_z = scene_z;
            panel->sel_level = values[3];
            panel->sel_element_id = -1;
        }
    }
    panel->applying_shared_state = 0;
}
