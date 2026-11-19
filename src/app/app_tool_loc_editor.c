/*
 * The loc editor: a side-app (sideapp/side_app.h).
 *
 * Selecting a placed loc (or a terrain tile) in the world, showing what it is,
 * and driving the client-side edits the tool makes to it: a
 * TORIRS_CHROME_PANEL_MENU in the developer chrome, opened at the loc under
 * the cursor, whose "Move"/"Rotate" rows re-place it client-side only, so the
 * readout gives exact scene coords to hand-copy into a script without a
 * server round trip. Under the EDIT layout a move is recorded into the map
 * editor document first (Editor_PanelRecordLocEdit).
 *
 * Everything it keeps is its own struct below: the panel's widget ids and the
 * selection. It reaches the client through the registry's hooks only:
 *   - build_chrome: its panel, in the developer chrome right after the
 *     frame-time panel's (registration order, app_tools.c);
 *   - key `loc_editor_toggle` (CTRL held, not a key the map editor claimed
 *     this frame, the dev panels' point with no text input focused): show or
 *     hide the panel, never touching the selection;
 *   - frame, at the dev panels' points (app_dev_panels_tick, app_chrome.c):
 *     BEGIN remembers whether a chrome popup or field had the keys as this
 *     frame's arrived; HOVER notes the world tile under the cursor; INPUT runs
 *     the panel's shortcuts and rows while it is open;
 *   - inspects_scenery: while open the pick sees every loc;
 *   - holds_keyboard: while open the chat line is kept closed and takes no
 *     keys;
 *   - menu_flags / menu_action: the right-click menu's Select rows
 *     (locedit_active, RS_MINIMENU_ACTION_LOCEDIT_SELECT[_TERRAIN]).
 *
 * The model-view camera it shares with the map editor's Catalog is the
 * developer overlay's (ui/dev_overlay.h); the model-view keys are the dev
 * panels' tick's.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "mem/torirs_mem.h"
#include "app/app_internal.h"
#include "editor/loc_editor_selection.h"

/** The loc editor panel's chrome handles: the panel, its four readout rows and
 *  its eight buttons. Ids the chrome hands back, nothing more. */
struct LocEditorWidgets
{
    int panel;
    int visible;
    int row_target; /* "loc <id> shape <n>" or "no loc selected" */
    int row_pos;    /* "x=.. z=.. level=.." */
    int row_size;   /* "size AxB angle=N" */
    int row_extra;  /* loc name, or "interactive=0/1" when unnamed */
    int item_xplus;
    int item_xminus;
    int item_zplus;
    int item_zminus;
    int item_rotate;
    int item_reselect;
    int item_deselect;
    int item_close;
};

struct AppToolLocEditor
{
    /** The panel in the developer chrome. */
    struct LocEditorWidgets widgets;
    /** What the panel is pointed at, and what moving it means. Set only by an
     *  explicit Reselect/Deselect: opening or closing the panel never changes
     *  it. @see editor/loc_editor_selection.h */
    struct LocEditorSelection selection;
    /** Noted at the dev panels' BEGIN point for this frame's INPUT point: a
     *  dropdown list, menu or colour popup was up, and a chrome field had the
     *  keyboard, as this frame's keys arrived. */
    int popup_was_open;
    int field_had_keys;
};

/* Private to this unit, declared up front so definition order is free. */
static void
app_loc_editor_refresh_labels(
    struct App* app,
    struct AppToolLocEditor* tool);
static void
app_loc_editor_reselect(
    struct App* app,
    struct AppToolLocEditor* tool);
static void
app_loc_editor_deselect(
    struct App* app,
    struct AppToolLocEditor* tool);
static void
app_loc_editor_select_element(
    struct App* app,
    struct AppToolLocEditor* tool,
    int element_id);
static void
app_loc_editor_select_terrain(
    struct App* app,
    struct AppToolLocEditor* tool,
    int scene_x,
    int scene_z,
    int cache_level);
static void
app_loc_editor_nudge(
    struct App* app,
    struct AppToolLocEditor* tool,
    int dx,
    int dz);
static void
app_loc_editor_rotate(
    struct App* app,
    struct AppToolLocEditor* tool);
static void
app_loc_editor_set_visible(
    struct App* app,
    struct AppToolLocEditor* tool,
    int visible);
static void
app_loc_editor_input(
    struct App* app,
    struct AppToolLocEditor* tool,
    struct LibToriRS_Input* input);
static void
app_tool_loc_editor_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args);
static void
app_tool_loc_editor_frame(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase,
    struct LibToriRS_Input* input);
static void
app_tool_loc_editor_build_chrome(
    void* self,
    struct SideAppHost* host);
static int
app_tool_loc_editor_is_open(void const* self);
static void
app_tool_loc_editor_menu_flags(
    void const* self,
    void* build_ctx);
static int
app_tool_loc_editor_menu_action(
    void* self,
    struct SideAppHost* host,
    int action,
    void const* option);
static void
app_tool_loc_editor_free(void* self);

enum
{
    APP_TOOL_LOC_EDITOR_TOGGLE
};

static struct SideAppAction const k_app_tool_loc_editor_actions[] = {
    [APP_TOOL_LOC_EDITOR_TOGGLE] = { "loc_editor_toggle", SIDE_APP_PHASE_DEV_PANELS, SIDE_APP_GATE_CTRL,
                                     SIDE_APP_KEY_PRESS, 0 },
};

static struct SideAppHooks const k_app_tool_loc_editor_hooks = {
    .key = app_tool_loc_editor_key,
    .free = app_tool_loc_editor_free,
    .frame = app_tool_loc_editor_frame,
    .build_chrome = app_tool_loc_editor_build_chrome,
    .inspects_scenery = app_tool_loc_editor_is_open,
    .holds_keyboard = app_tool_loc_editor_is_open,
    .menu_flags = app_tool_loc_editor_menu_flags,
    .menu_action = app_tool_loc_editor_menu_action,
};

void
app_tool_loc_editor_register(struct App* app)
{
    struct AppToolLocEditor* tool;

    assert(app);
    assert(app->side_apps);
    tool = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*tool));
    SideApps_Register(
        app->side_apps,
        "loc_editor",
        tool,
        &k_app_tool_loc_editor_hooks,
        k_app_tool_loc_editor_actions,
        (int)(sizeof(k_app_tool_loc_editor_actions) / sizeof(k_app_tool_loc_editor_actions[0])));
}

/* Refreshes the panel's readout rows from current selection state. Called
 * after every selection change, deselect, move, and rotate. */
static void
app_loc_editor_refresh_labels(
    struct App* app,
    struct AppToolLocEditor* tool)
{
    char text[TORIRS_CHROME_INPUT_MAX];

    /*
     * A selected TILE, which answers a different set of questions than a loc.
     *
     * The three levels are all different on exactly the columns where ground
     * misbehaves, so all three are shown rather than one "level":
     *   cache  — the plane the map authored this floor on (the mesh level).
     *   draw   — the plane it is culled and picked against; VIS_BELOW makes
     *            that 0, and a LinkBelow column's upper planes one lower.
     *   paint  — where the build's push-down parked the tile in the painter.
     * A flat column reads the same number three times; a bridge deck reads
     * 1/0/0, and that spread is the readout's whole reason to exist.
     */
    if( tool->selection.terrain )
    {
        char settings[4 * 6 + 1];
        char meshes[WORLD_MAP_TERRAIN_LEVELS + 1];
        int const cache_level = tool->selection.terrain_level;

        World_TileSettingsText(
            app->world,
            tool->selection.scene_x,
            tool->selection.scene_z,
            settings,
            (int)sizeof(settings));
        World_TerrainMeshLevelsText(
            app->world,
            tool->selection.scene_x,
            tool->selection.scene_z,
            meshes,
            (int)sizeof(meshes));

        snprintf(text, sizeof(text), "terrain tile, mesh on level %d", cache_level);
        ToriRSChrome_SetText(&app->dev_overlay->ui, tool->widgets.row_target, text);
        snprintf(
            text,
            sizeof(text),
            "x=%d z=%d abs(%d,%d)",
            tool->selection.scene_x,
            tool->selection.scene_z,
            app->world ? app->world->_base_tile_x + tool->selection.scene_x
                       : -1,
            app->world ? app->world->_base_tile_z + tool->selection.scene_z
                       : -1);
        ToriRSChrome_SetText(&app->dev_overlay->ui, tool->widgets.row_pos, text);
        snprintf(
            text,
            sizeof(text),
            "cache=%d draw=%d paint=%d",
            cache_level,
            World_TerrainDrawLevel(
                app->world,
                tool->selection.scene_x,
                tool->selection.scene_z,
                cache_level),
            World_LocPaintLevel(
                app->world,
                tool->selection.scene_x,
                tool->selection.scene_z,
                cache_level));
        ToriRSChrome_SetText(&app->dev_overlay->ui, tool->widgets.row_size, text);
        snprintf(text, sizeof(text), "s[%s] mesh[%s]", settings, meshes);
        ToriRSChrome_SetText(&app->dev_overlay->ui, tool->widgets.row_extra, text);
        return;
    }

    if( tool->selection.loc_id < 0 )
    {
        ToriRSChrome_SetText(
            &app->dev_overlay->ui, tool->widgets.row_target, "nothing selected");
        ToriRSChrome_SetText(&app->dev_overlay->ui, tool->widgets.row_pos, "");
        ToriRSChrome_SetText(&app->dev_overlay->ui, tool->widgets.row_size, "");
        ToriRSChrome_SetText(&app->dev_overlay->ui, tool->widgets.row_extra, "");
        return;
    }
    snprintf(
        text,
        sizeof(text),
        "loc %d shape %d",
        tool->selection.loc_id,
        tool->selection.shape);
    ToriRSChrome_SetText(&app->dev_overlay->ui, tool->widgets.row_target, text);
    snprintf(
        text,
        sizeof(text),
        "x=%d z=%d level=%d",
        tool->selection.scene_x,
        tool->selection.scene_z,
        tool->selection.level);
    ToriRSChrome_SetText(&app->dev_overlay->ui, tool->widgets.row_pos, text);
    snprintf(
        text,
        sizeof(text),
        "size %dx%d angle=%d",
        tool->selection.size_x,
        tool->selection.size_z,
        tool->selection.angle);
    ToriRSChrome_SetText(&app->dev_overlay->ui, tool->widgets.row_size, text);
    /* A baked-map loc usually has no LocType.name resolved client-side (that
     * lives in the config, not the placed entity), so an empty name is the
     * common case -- fall back to whether it can be clicked at all rather
     * than print a blank row. */
    if( tool->selection.name[0] )
        snprintf(
            text,
            sizeof(text),
            "\"%s\" interactive=%d",
            tool->selection.name,
            tool->selection.interactive);
    else
        snprintf(
            text, sizeof(text), "interactive=%d", tool->selection.interactive);
    ToriRSChrome_SetText(&app->dev_overlay->ui, tool->widgets.row_extra, text);
}

/* Targets whatever loc sits at locedit_hover_x/z -- the last tile the cursor
 * hovered while NOT over the panel -- on the local player's current level (a
 * loc editor has no reason to reach across planes). Clears the selection
 * (loc_id -1) when there is no loc there or nothing was ever hovered. Only
 * ever called from an explicit Reselect click -- opening the panel does NOT
 * call this, so a selection stays active across a close/reopen.
 *
 * Deliberately reads locedit_hover_x/z, not the live world_hover_tile_x/z:
 * clicking "Reselect" necessarily moves the cursor onto the panel first, and
 * by the time the click lands, the live hover reflects the panel, not
 * whatever loc the player was actually pointing at. */
static void
app_loc_editor_reselect(
    struct App* app,
    struct AppToolLocEditor* tool)
{
    struct WorldEntity_Player* player;
    struct WorldEntity_Scenery* scenery;
    int hover_x;
    int hover_z;
    int idx;

    LocEditorSelection_Clear(&tool->selection);
    if( !app->world ||
        !LocEditorSelection_HoverTile(&tool->selection, &hover_x, &hover_z) )
    {
        app_loc_editor_refresh_labels(app, tool);
        return;
    }
    player = app_local_player(app);
    tool->selection.level = player ? player->grid_position.level : 0;
    /* loc_shape < 0: match the first loc on the tile regardless of layer --
     * a decoration like a bridge is exactly as findable as a wall this way. */
    idx = World_SceneryFindAt(
        app->world, hover_x, hover_z, tool->selection.level, -1);
    if( idx < 0 )
    {
        app_loc_editor_refresh_labels(app, tool);
        return;
    }
    scenery = World_EntityPoolGet(&app->world->entities.scenery, idx);
    if( !scenery )
    {
        app_loc_editor_refresh_labels(app, tool);
        return;
    }
    {
        struct LocEditorLocPlacement placement;
        placement.loc_id = scenery->loc_id;
        placement.shape = scenery->shape;
        placement.angle = scenery->angle;
        placement.size_x = scenery->size_x;
        placement.size_z = scenery->size_z;
        placement.interactive = scenery->interactive;
        placement.name = scenery->info->name;
        placement.scene_x = scenery->grid_position.x;
        placement.scene_z = scenery->grid_position.z;
        placement.level = scenery->grid_position.level;
        LocEditorSelection_SelectLoc(&tool->selection, &placement);
    }
    app_loc_editor_refresh_labels(app, tool);
}

/* Explicit Deselect: clears the target without touching the world. Clears the
 * tile selection too — one row, both subjects, or Deselect would appear to do
 * nothing while a tile was up. */
static void
app_loc_editor_deselect(
    struct App* app,
    struct AppToolLocEditor* tool)
{
    LocEditorSelection_Clear(&tool->selection);
    app_loc_editor_refresh_labels(app, tool);
}

/* Targets an exact scene element -- the "Select" minimenu row's handler.
 * Unlike app_loc_editor_reselect (a tile-only guess, first-loc-regardless-of-
 * layer), this comes from the real pick/classify/dedup pipeline the minimenu
 * itself uses, via the row's UIMinimenuPick.id, so it disambiguates a tile
 * with a wall AND a wall-decor AND a ground loc on it exactly the way a
 * player reading the right-click menu would. */
static void
app_loc_editor_select_element(
    struct App* app,
    struct AppToolLocEditor* tool,
    int element_id)
{
    struct WorldEntity_Scenery* scenery;

    if( !app->world )
        return;
    scenery = World_SceneryGetByElementId(app->world, element_id);
    if( !scenery )
        return;
    {
        struct LocEditorLocPlacement placement;
        placement.loc_id = scenery->loc_id;
        placement.shape = scenery->shape;
        placement.angle = scenery->angle;
        placement.size_x = scenery->size_x;
        placement.size_z = scenery->size_z;
        placement.interactive = scenery->interactive;
        placement.name = scenery->info->name;
        placement.scene_x = scenery->grid_position.x;
        placement.scene_z = scenery->grid_position.z;
        placement.level = scenery->grid_position.level;
        LocEditorSelection_SelectLoc(&tool->selection, &placement);
    }
    app_loc_editor_refresh_labels(app, tool);
}

/* Select the GROUND at a scene tile. `cache_level` is the picked mesh level —
 * the plane the map authored that floor on — which the panel then reads the
 * draw and paint levels off, since those are derived and not stored. */
static void
app_loc_editor_select_terrain(
    struct App* app,
    struct AppToolLocEditor* tool,
    int scene_x,
    int scene_z,
    int cache_level)
{
    if( !app->world )
        return;
    LocEditorSelection_SelectTile(
        &tool->selection, scene_x, scene_z, cache_level);
    app_loc_editor_refresh_labels(app, tool);
}

/* Client-only reposition: clear the old tile, place at the new one. Both legs
 * go through App_WorldLocChange so this is exactly what a zone LOC_DEL +
 * LOC_ADD_CHANGE pair would produce, just without a server round trip. */
static void
app_loc_editor_nudge(
    struct App* app,
    struct AppToolLocEditor* tool,
    int dx,
    int dz)
{
    struct LocEditorMove move;
    int recorded;
    int const edit_layout =
        app->world_builder && WorldBuilder_GetLayout(app->world_builder) == WORLD_BUILDER_LAYOUT_EDIT;

    if( !LocEditorSelection_Nudge(&tool->selection, dx, dz, &move) )
        return;
    /* The document first: the same move against the authored loc list, so it
     * survives a reload and can be saved. Under the EDIT layout that is the
     * whole edit -- the flush moves the placement -- and a runtime change on
     * top would leave a second, default-lit copy. STATIC still shows it
     * through the zone-packet seam (and so does EDIT when the square is not
     * open in the document, which the record then refused). */
    recorded = app_map_editor_record_loc_edit(
        app,
        move.from_x,
        move.from_z,
        move.level,
        move.loc_id,
        move.shape,
        move.from_angle,
        move.to_x,
        move.to_z,
        move.to_angle);
    if( !edit_layout || !recorded )
    {
        App_WorldLocChange(app, move.from_x, move.from_z, move.level, -1, move.shape, move.from_angle);
        App_WorldLocChange(
            app, move.to_x, move.to_z, move.level, move.loc_id, move.shape, move.to_angle);
    }
    app_loc_editor_refresh_labels(app, tool);
}

/* Same-tile change with the next of the 4 config angles (0..3 = W/N/E/S,
 * entity_scenery.h) -- no del needed, App_WorldLocChange already replaces
 * whatever is at scene_x/z. */
static void
app_loc_editor_rotate(
    struct App* app,
    struct AppToolLocEditor* tool)
{
    struct LocEditorMove move;
    int recorded;
    int const edit_layout =
        app->world_builder && WorldBuilder_GetLayout(app->world_builder) == WORLD_BUILDER_LAYOUT_EDIT;

    if( !LocEditorSelection_Rotate(&tool->selection, &move) )
        return;
    /* Same pair as a nudge: the authored record first, then (STATIC, or a
     * square the document does not hold) the scene. */
    recorded = app_map_editor_record_loc_edit(
        app,
        move.from_x,
        move.from_z,
        move.level,
        move.loc_id,
        move.shape,
        move.from_angle,
        move.to_x,
        move.to_z,
        move.to_angle);
    if( !edit_layout || !recorded )
        App_WorldLocChange(
            app, move.to_x, move.to_z, move.level, move.loc_id, move.shape, move.to_angle);
    app_loc_editor_refresh_labels(app, tool);
}


/* Show or hide the panel: visibility only, never the selection -- a target
 * picked with Reselect stays active across a close/reopen. */
static void
app_loc_editor_set_visible(
    struct App* app,
    struct AppToolLocEditor* tool,
    int visible)
{
    tool->widgets.visible = visible;
    ToriRSChrome_PanelSetVisible(&app->dev_overlay->ui, tool->widgets.panel, tool->widgets.visible);
}

/* The panel, hidden, in the developer chrome; the selection empty. */
static void
app_tool_loc_editor_build_chrome(
    void* self,
    struct SideAppHost* host)
{
    struct AppToolLocEditor* tool = self;
    struct App* app = app_from_side_app_host(host);
    struct ToriRSChrome* ui;

    assert(tool);
    ui = &app->dev_overlay->ui;
    tool->widgets.panel = ToriRSChrome_PanelAdd(ui, TORIRS_CHROME_PANEL_MENU, 8, 40, 0, "Loc Editor");
    tool->widgets.row_target = ToriRSChrome_MenuItem(ui, tool->widgets.panel, "nothing selected");
    tool->widgets.row_pos = ToriRSChrome_MenuItem(ui, tool->widgets.panel, "");
    tool->widgets.row_size = ToriRSChrome_MenuItem(ui, tool->widgets.panel, "");
    tool->widgets.row_extra = ToriRSChrome_MenuItem(ui, tool->widgets.panel, "");
    ToriRSChrome_Separator(ui, tool->widgets.panel);
    /* Rows double as the key reference: chat input is forced off while this
     * panel is open (holds_keyboard), so these letters are always free to use
     * without a message box eating them. Still clickable too -- the key is
     * the fast path, the click is the discoverable one. */
    tool->widgets.item_xplus = ToriRSChrome_MenuItem(ui, tool->widgets.panel, "Move X+1  [D]");
    tool->widgets.item_xminus = ToriRSChrome_MenuItem(ui, tool->widgets.panel, "Move X-1  [A]");
    tool->widgets.item_zplus = ToriRSChrome_MenuItem(ui, tool->widgets.panel, "Move Z+1  [W]");
    tool->widgets.item_zminus = ToriRSChrome_MenuItem(ui, tool->widgets.panel, "Move Z-1  [S]");
    tool->widgets.item_rotate = ToriRSChrome_MenuItem(ui, tool->widgets.panel, "Rotate  [R]");
    tool->widgets.item_reselect =
        ToriRSChrome_MenuItem(ui, tool->widgets.panel, "Reselect (under cursor)  [Space]");
    tool->widgets.item_deselect = ToriRSChrome_MenuItem(ui, tool->widgets.panel, "Deselect  [Backspace]");
    tool->widgets.item_close = ToriRSChrome_MenuItem(ui, tool->widgets.panel, "Close  [9 / Esc]");
    ToriRSChrome_PanelSetVisible(ui, tool->widgets.panel, 0);
    tool->widgets.visible = 0;
    LocEditorSelection_Reset(&tool->selection);
}

/* The toggle key: show or hide the panel. */
static void
app_tool_loc_editor_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    struct AppToolLocEditor* tool = self;
    struct App* app = app_from_side_app_host(host);

    (void)action;
    (void)args;
    assert(tool);
    assert(action == APP_TOOL_LOC_EDITOR_TOGGLE);
    assert(args);
    app_loc_editor_set_visible(app, tool, !tool->widgets.visible);
}

static void
app_tool_loc_editor_frame(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase,
    struct LibToriRS_Input* input)
{
    struct AppToolLocEditor* tool = self;
    struct App* app = app_from_side_app_host(host);

    assert(tool);
    assert(input);
    if( phase == SIDE_APP_PHASE_DEV_PANELS_BEGIN )
    {
        /* A dropdown list, a menu or a colour popup up as this frame's keys
         * arrive: its Escape is the chrome's, which closes the popup with it
         * (ToriRSChrome_KeyEdit) -- and by the time the loc editor's own
         * Escape is read at the INPUT point the popup is already gone, so it
         * is remembered here. One press closes the menu; the next one closes
         * the panel. */
        tool->popup_was_open =
            app->dev_overlay->ui.dropdown_open >= 0 || app->dev_overlay->ui.colorpick_open >= 0;
        /* Likewise a chrome field that is being typed in as the keys arrive:
         * they are its -- Ctrl+A selects its content, Escape leaves it -- and
         * the loc editor's shortcuts on the same keys (Ctrl+A nudges, Escape
         * closes the panel) wait for a frame in which no field has the
         * keyboard. */
        tool->field_had_keys = app_chrome_focus_types(&app->dev_overlay->ui) ||
                               app_chrome_focus_types(&app->plugin_win->ui);
    }
    else if( phase == SIDE_APP_PHASE_DEV_PANELS_HOVER )
    {
        /* Remember the world tile under the cursor whenever the cursor is NOT
         * over the panel itself. Runs every frame, panel open or not, so the
         * moment Reselect is clicked there is already a last-known-good world
         * hover to read -- the live world_hover_tile_x/z cannot be used at
         * click time because reaching the menu item necessarily moved the
         * cursor onto the panel first. */
        LocEditorSelection_NoteHover(
            &tool->selection,
            ToriRSChrome_HitTest(&app->dev_overlay->ui, input->curr.mouse_x, input->curr.mouse_y) >= 0,
            app->world_views.frame->world_hover_tile_x,
            app->world_views.frame->world_hover_tile_z);
    }
    else if( phase == SIDE_APP_PHASE_DEV_PANELS_INPUT )
    {
        if( tool->widgets.visible )
            app_loc_editor_input(app, tool, input);
    }
}

/*
 * The open panel's keys and rows, after the chrome routed this frame's input.
 *
 * Keyboard control, the fast path the menu rows advertise. IsKeyDown (edge,
 * not held) so one press moves one tile rather than a nudge repeating every
 * frame a key is held down. Space reselects at the live world_hover_tile_x/z
 * directly -- pressing a key, unlike clicking a menu row, never moves the
 * cursor off the world first, so the live hover is already correct and the
 * remembered hover (which Reselect itself reads) is equally valid here since
 * this same tick already refreshed it.
 *
 * Only while the LOC editor is open, never merely because some panel is: with
 * only the map editor open, W/A/S/D once nudged whatever loc the loc editor
 * had latched *while also* flying the camera, since a nudge does not consume
 * the frame.
 *
 * The nudges are Ctrl+W/A/S/D (Cmd on a Mac): the bare keys fly the camera and
 * nothing in the world view takes them over (owner, 2026-11-07).
 */
static void
app_loc_editor_input(
    struct App* app,
    struct AppToolLocEditor* tool,
    struct LibToriRS_Input* input)
{
    int activated;

    if( tool->field_had_keys || app_chrome_focus_types(&app->dev_overlay->ui) ||
        app_chrome_focus_types(&app->plugin_win->ui) )
    {
        /* A chrome field holds the keyboard (or did when this frame's keys
         * arrived): the letters, Backspace, Space and the Ctrl chords are text
         * and clipboard there, not loc shortcuts. */
    }
    else if( input->key_held[TORIRSK_CTRL] && LibToriRS_Input_IsKeyDown(input, TORIRSK_D) )
        app_loc_editor_nudge(app, tool, 1, 0);
    else if( input->key_held[TORIRSK_CTRL] && LibToriRS_Input_IsKeyDown(input, TORIRSK_A) )
        app_loc_editor_nudge(app, tool, -1, 0);
    else if( input->key_held[TORIRSK_CTRL] && LibToriRS_Input_IsKeyDown(input, TORIRSK_W) )
        app_loc_editor_nudge(app, tool, 0, 1);
    else if( input->key_held[TORIRSK_CTRL] && LibToriRS_Input_IsKeyDown(input, TORIRSK_S) )
        app_loc_editor_nudge(app, tool, 0, -1);
    else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_R) )
        app_loc_editor_rotate(app, tool);
    else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_SPACE) )
        app_loc_editor_reselect(app, tool);
    else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_BACKSPACE) )
        app_loc_editor_deselect(app, tool);
    else if( !tool->popup_was_open && LibToriRS_Input_IsKeyDown(input, TORIRSK_ESCAPE) )
        app_loc_editor_set_visible(app, tool, 0);

    /* Only when the LOC editor is open (re-read: Escape may just have closed
     * it). Draining the shared activation latch for any open panel swallowed
     * the map editor's clicks -- its dropdown showed the new value while its
     * tool never changed, because the activation was taken before its tick
     * ran. */
    activated = tool->widgets.visible ? ToriRSChrome_TakeActivated(&app->dev_overlay->ui) : -1;
    if( activated >= 0 )
    {
        if( activated == tool->widgets.item_xplus )
            app_loc_editor_nudge(app, tool, 1, 0);
        else if( activated == tool->widgets.item_xminus )
            app_loc_editor_nudge(app, tool, -1, 0);
        else if( activated == tool->widgets.item_zplus )
            app_loc_editor_nudge(app, tool, 0, 1);
        else if( activated == tool->widgets.item_zminus )
            app_loc_editor_nudge(app, tool, 0, -1);
        else if( activated == tool->widgets.item_rotate )
            app_loc_editor_rotate(app, tool);
        else if( activated == tool->widgets.item_reselect )
            app_loc_editor_reselect(app, tool);
        else if( activated == tool->widgets.item_deselect )
            app_loc_editor_deselect(app, tool);
        else if( activated == tool->widgets.item_close )
            app_loc_editor_set_visible(app, tool, 0);
    }
}

/* Open: the pick sees every loc (walls, fences, gravel and ground decor with
 * no ops, which it drops by default -- most of what a placement question is
 * about), and the keyboard is the panel's (the chat line is kept closed and
 * takes no keys, so W/A/S/D/R/Space/Backspace reach the shortcuts). */
static int
app_tool_loc_editor_is_open(void const* self)
{
    struct AppToolLocEditor const* tool = self;

    assert(tool);
    return tool->widgets.visible != 0;
}

/* While open, every loc that earns an Examine row also earns a Select row. */
static void
app_tool_loc_editor_menu_flags(
    void const* self,
    void* build_ctx)
{
    struct AppToolLocEditor const* tool = self;
    struct RS_MinimenuBuildCtx* ctx = build_ctx;

    assert(tool);
    assert(ctx);
    ctx->locedit_active = tool->widgets.visible != 0;
}

/*
 * The "Select" rows (rs_minimenu_world.c, gated on locedit_active): entirely
 * client-side, taken whether or not the panel is still open -- the re-check
 * guards a row clicked in the one frame the tool closed on, and the row must
 * not reach the OPLOC dispatch either way.
 */
static int
app_tool_loc_editor_menu_action(
    void* self,
    struct SideAppHost* host,
    int action,
    void const* option)
{
    struct AppToolLocEditor* tool = self;
    struct App* app = app_from_side_app_host(host);
    struct UIMinimenuOption const* opt = option;

    assert(tool);
    assert(opt);
    if( action == RS_MINIMENU_ACTION_LOCEDIT_SELECT )
    {
        if( tool->widgets.visible )
            app_loc_editor_select_element(app, tool, opt->pick.id);
        return 1;
    }
    /* Its ground twin. The pick already carries the exact tile the terrain hit
     * named -- secondary/tertiary are scene x/z and quaternary the mesh level
     * -- so nothing is re-derived here and the panel cannot describe a
     * different tile than the row that was clicked. */
    if( action == RS_MINIMENU_ACTION_LOCEDIT_SELECT_TERRAIN )
    {
        if( tool->widgets.visible )
            app_loc_editor_select_terrain(
                app, tool, opt->pick.secondary_id, opt->pick.tertiary_id, opt->pick.quaternary_id);
        return 1;
    }
    return 0;
}

static void
app_tool_loc_editor_free(void* self)
{
    ToriMem_Free(ToriMem_Process(), self);
}
