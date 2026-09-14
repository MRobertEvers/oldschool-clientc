/*
 * The loc editor panel: rows, buttons, and the chrome input routing shared by
 * every developer panel.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_loc_editor_refresh_labels(struct App* app);
static void
app_loc_editor_reselect(struct App* app);
static void
app_loc_editor_deselect(struct App* app);
static void
app_loc_editor_nudge(
    struct App* app,
    int dx,
    int dz);
static void
app_loc_editor_rotate(struct App* app);
static int
app_dbgui_key_edit_from_osrs(int osrs_key);

/*
 * The LOC EDITOR's client half -- selecting a placed loc in the world, showing
 * what it is, and driving the edits the tool makes to it.
 *
 * A unity fragment of app.c, not a module. It is textually part of app.c's
 * translation unit and included at exactly the point it was cut from, so every
 * helper here stays static and every App field it reads stays where it was.
 * The split is for the reader: this is a developer tool and it should not be
 * in the way of the code that runs every frame.
 *
 * It carries the chrome routing it sits between, because that is where the
 * pointer this tool reads is decided -- whether a click belongs to a chrome
 * panel or to the world underneath it, which is the question every selection
 * here starts with.
 *
 * @see src/app.c, src/editor/editor.h
 */

/* Refreshes the panel's readout rows from current selection state. Called
 * after every selection change, deselect, move, and rotate. */
static void
app_loc_editor_refresh_labels(struct App* app)
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
    if( app->locedit_selection.terrain )
    {
        char settings[4 * 6 + 1];
        char meshes[WORLD_MAP_TERRAIN_LEVELS + 1];
        int const cache_level = app->locedit_selection.terrain_level;

        World_TileSettingsText(
            app->world,
            app->locedit_selection.scene_x,
            app->locedit_selection.scene_z,
            settings,
            (int)sizeof(settings));
        World_TerrainMeshLevelsText(
            app->world, app->locedit_selection.scene_x, app->locedit_selection.scene_z, meshes, (int)sizeof(meshes));

        snprintf(text, sizeof(text), "terrain tile, mesh on level %d", cache_level);
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_target, text);
        snprintf(
            text,
            sizeof(text),
            "x=%d z=%d abs(%d,%d)",
            app->locedit_selection.scene_x,
            app->locedit_selection.scene_z,
            app->world ? app->world->_base_tile_x + app->locedit_selection.scene_x : -1,
            app->world ? app->world->_base_tile_z + app->locedit_selection.scene_z : -1);
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_pos, text);
        snprintf(
            text,
            sizeof(text),
            "cache=%d draw=%d paint=%d",
            cache_level,
            World_TerrainDrawLevel(
                app->world, app->locedit_selection.scene_x, app->locedit_selection.scene_z, cache_level),
            World_LocPaintLevel(
                app->world, app->locedit_selection.scene_x, app->locedit_selection.scene_z, cache_level));
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_size, text);
        snprintf(text, sizeof(text), "s[%s] mesh[%s]", settings, meshes);
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_extra, text);
        return;
    }

    if( app->locedit_selection.loc_id < 0 )
    {
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_target, "nothing selected");
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_pos, "");
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_size, "");
        ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_extra, "");
        return;
    }
    snprintf(text, sizeof(text), "loc %d shape %d", app->locedit_selection.loc_id, app->locedit_selection.shape);
    ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_target, text);
    snprintf(
        text,
        sizeof(text),
        "x=%d z=%d level=%d",
        app->locedit_selection.scene_x,
        app->locedit_selection.scene_z,
        app->locedit_selection.level);
    ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_pos, text);
    snprintf(
        text,
        sizeof(text),
        "size %dx%d angle=%d",
        app->locedit_selection.size_x,
        app->locedit_selection.size_z,
        app->locedit_selection.angle);
    ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_size, text);
    /* A baked-map loc usually has no LocType.name resolved client-side (that
     * lives in the config, not the placed entity), so an empty name is the
     * common case -- fall back to whether it can be clicked at all rather
     * than print a blank row. */
    if( app->locedit_selection.name[0] )
        snprintf(
            text,
            sizeof(text),
            "\"%s\" interactive=%d",
            app->locedit_selection.name,
            app->locedit_selection.interactive);
    else
        snprintf(text, sizeof(text), "interactive=%d", app->locedit_selection.interactive);
    ToriRSChrome_SetText(&app->dbg_ui, app->locedit.row_extra, text);
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
app_loc_editor_reselect(struct App* app)
{
    struct WorldEntity_Player* player;
    struct WorldEntity_Scenery* scenery;
    int hover_x;
    int hover_z;
    int idx;

    LocEditorSelection_Clear(&app->locedit_selection);
    if( !app->world ||
        !LocEditorSelection_HoverTile(&app->locedit_selection, &hover_x, &hover_z) )
    {
        app_loc_editor_refresh_labels(app);
        return;
    }
    player = app_local_player(app);
    app->locedit_selection.level = player ? player->grid_position.level : 0;
    /* loc_shape < 0: match the first loc on the tile regardless of layer --
     * a decoration like a bridge is exactly as findable as a wall this way. */
    idx = World_SceneryFindAt(app->world, hover_x, hover_z, app->locedit_selection.level, -1);
    if( idx < 0 )
    {
        app_loc_editor_refresh_labels(app);
        return;
    }
    scenery = World_EntityPoolGet(&app->world->entities.scenery, idx);
    if( !scenery )
    {
        app_loc_editor_refresh_labels(app);
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
        LocEditorSelection_SelectLoc(&app->locedit_selection, &placement);
    }
    app_loc_editor_refresh_labels(app);
}

/* Explicit Deselect: clears the target without touching the world. Clears the
 * tile selection too — one row, both subjects, or Deselect would appear to do
 * nothing while a tile was up. */
static void
app_loc_editor_deselect(struct App* app)
{
    LocEditorSelection_Clear(&app->locedit_selection);
    app_loc_editor_refresh_labels(app);
}

/* Targets an exact scene element -- the "Select" minimenu row's handler.
 * Unlike app_loc_editor_reselect (a tile-only guess, first-loc-regardless-of-
 * layer), this comes from the real pick/classify/dedup pipeline the minimenu
 * itself uses, via the row's UIMinimenuPick.id, so it disambiguates a tile
 * with a wall AND a wall-decor AND a ground loc on it exactly the way a
 * player reading the right-click menu would. */
void
app_loc_editor_select_element(
    struct App* app,
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
        LocEditorSelection_SelectLoc(&app->locedit_selection, &placement);
    }
    app_loc_editor_refresh_labels(app);
}

/* Select the GROUND at a scene tile. `cache_level` is the picked mesh level —
 * the plane the map authored that floor on — which the panel then reads the
 * draw and paint levels off, since those are derived and not stored. */
void
app_loc_editor_select_terrain(
    struct App* app,
    int scene_x,
    int scene_z,
    int cache_level)
{
    if( !app->world )
        return;
    LocEditorSelection_SelectTile(&app->locedit_selection, scene_x, scene_z, cache_level);
    app_loc_editor_refresh_labels(app);
}

/* Client-only reposition: clear the old tile, place at the new one. Both legs
 * go through App_WorldLocChange so this is exactly what a zone LOC_DEL +
 * LOC_ADD_CHANGE pair would produce, just without a server round trip. */
static void
app_loc_editor_nudge(
    struct App* app,
    int dx,
    int dz)
{
    struct LocEditorMove move;

    if( !LocEditorSelection_Nudge(&app->locedit_selection, dx, dz, &move) )
        return;
    App_WorldLocChange(app, move.from_x, move.from_z, move.level, -1, move.shape, move.from_angle);
    /* The scene edit above is client-side only; this records the same move
     * against the authored loc list so it survives a reload and can be saved.
     * Both ends, which is why the move carries them. */
    Editor_PanelRecordLocEdit(
        &app->editor_panel,
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
    App_WorldLocChange(
        app, move.to_x, move.to_z, move.level, move.loc_id, move.shape, move.to_angle);
    app_loc_editor_refresh_labels(app);
}

/* Same-tile change with the next of the 4 config angles (0..3 = W/N/E/S,
 * entity_scenery.h) -- no del needed, App_WorldLocChange already replaces
 * whatever is at scene_x/z. */
static void
app_loc_editor_rotate(struct App* app)
{
    struct LocEditorMove move;

    if( !LocEditorSelection_Rotate(&app->locedit_selection, &move) )
        return;
    /* Same pair as a nudge: the authored record first, then the scene. */
    Editor_PanelRecordLocEdit(
        &app->editor_panel,
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
    App_WorldLocChange(
        app, move.to_x, move.to_z, move.level, move.loc_id, move.shape, move.to_angle);
    app_loc_editor_refresh_labels(app);
}

/**
 * OSRS key code -> the overlay's editing key, or TORIRS_CHROME_KEY_NONE.
 *
 * The overlay deliberately owns no keymap (see uitree_debug_overlay.h), so the
 * translation lives here, where the client's own key codes already are.
 * Printable characters do not come through this at all — they arrive as
 * `key_pressed` and go straight to ToriRSChrome_KeyChar.
 */

static int
app_dbgui_key_edit_from_osrs(int osrs_key)
{
    switch( osrs_key )
    {
    case TORIRS_OSRSKEY_BACKSPACE:
        return TORIRS_CHROME_KEY_BACKSPACE;
    case TORIRS_OSRSKEY_DELETE:
        return TORIRS_CHROME_KEY_DELETE;
    case TORIRS_OSRSKEY_ENTER:
        return TORIRS_CHROME_KEY_ENTER;
    case TORIRS_OSRSKEY_ESCAPE:
        return TORIRS_CHROME_KEY_ESCAPE;
    /* Arrows and home/end have no named constants; the keymap table spells
     * them (src/input/torirs_keymap.c: 96 left, 97 right, 98 up, 99 down,
     * 102 home, 103 end). */
    case 96:
        return TORIRS_CHROME_KEY_LEFT;
    case 97:
        return TORIRS_CHROME_KEY_RIGHT;
    /* Up and down a LINE, for a multiline field. The model ignores them on
     * every other kind, so routing them costs nothing where there is none. */
    case 98:
        return TORIRS_CHROME_KEY_UP;
    case 99:
        return TORIRS_CHROME_KEY_DOWN;
    case 102:
        return TORIRS_CHROME_KEY_HOME;
    case 103:
        return TORIRS_CHROME_KEY_END;
    default:
        return TORIRS_CHROME_KEY_NONE;
    }
}

/**
 * Does chrome DRAWN IN THIS CANVAS own the pointer at (x, y)?
 *
 * The game's interaction pass, the world hittest and the camera all run long
 * after the chrome handled this frame's input, and by then they cannot read
 * "the chrome took it" off `input_frame_consumed` -- the shell sets that on
 * every frame as its own replay fence. They ask this instead, and a press, a
 * wheel or a right click over a panel stops there.
 *
 * The plugin window counts only under the BUFFER executor. Every other
 * presentation draws it somewhere else -- its own SDL window, a DOM, the
 * interface tree -- while the in-canvas model keeps the geometry it was laid
 * out with. That geometry is a ghost: hit-testable at a floating position
 * nothing draws, so honouring it would punch an invisible hole in the game.
 */
int
app_chrome_wants_pointer(
    struct App const* app,
    int x,
    int y)
{
    assert(app);
    if( ToriRSChrome_WantsPointer(&app->dbg_ui, x, y) )
        return 1;
    if( app->plugin_panel_visible && app->plugin_exec_kind == TORIRS_CHROME_EXEC_BUFFER &&
        ToriRSChrome_WantsPointer(&app->plugin_ui, x, y) )
        return 1;
    return 0;
}

/**
 * Feed one frame's pointer and keyboard to one chrome instance.
 *
 * Instance-taking rather than reaching for app->dbg_ui, because the plugin
 * window is a chrome of its own: routing input is the half of "a second chrome
 * is a second of all of this" that genuinely would have been duplicated, and
 * this is the one copy both instances share.
 *
 * Chrome first, then the game (README_DEBUG_OVERLAY.md §6): a click or drag
 * that lands on a panel must not also reach the world's click-to-walk
 * underneath it, which is what `input_frame_consumed` says.
 */
void
app_chrome_route_input(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input)
{
    assert(app);
    assert(ui);
    assert(input);

    if( ToriRSChrome_MouseMove(ui, input->curr.mouse_x, input->curr.mouse_y) )
        app->input_frame_consumed = 1;
    /*
     * The press lands where the button actually went DOWN, not where the
     * pointer finished the frame.
     *
     * curr.mouse_x is the last position any event in this frame's batch
     * carried, and a press is one event in that batch. A finger crossing the
     * drag slop pushes move(landed), down(landed), move(now) in a single batch
     * (input/torirs_touch.c), so reading curr here grabbed a scrollbar a slop's
     * width from where the finger was put -- which pages the list instead of
     * taking the grip when the grip's edge is inside that gap. press_origin is
     * the position the down carried, and for a mouse it is the same number.
     */
    if( input->curr.mouse_button_down[TORIRSM_LEFT] &&
        ToriRSChrome_MouseDown(
            ui, input->press_origin_x[TORIRSM_LEFT], input->press_origin_y[TORIRSM_LEFT]) )
        app->input_frame_consumed = 1;
    if( input->curr.mouse_button_up[TORIRSM_LEFT] &&
        ToriRSChrome_MouseUp(ui, input->curr.mouse_x, input->curr.mouse_y) )
        app->input_frame_consumed = 1;

    /* The wheel, so an open dropdown or a scrolling panel moves. Consumed when
     * the chrome takes it, or the camera would zoom behind it at the same time. */
    if( input->curr.mouse_wheel_y != 0 &&
        ToriRSChrome_MouseWheel(
            ui, input->curr.mouse_x, input->curr.mouse_y, input->curr.mouse_wheel_y) )
        app->input_frame_consumed = 1;

    app_chrome_route_keys(app, ui, input);
}

/*
 * The keyboard half of the routing above, on its own because a browser-backed
 * presentation needs exactly this half: pointer intents arrive through the
 * browser bridge, but its text fields are the model's, so typing still has
 * to reach the model. Routing the MOUSE too would hand clicks to the in-canvas
 * window's ghost -- laid out and hit-testable at its floating position even
 * though nothing draws it there.
 *
 * `key_typed` carries the OSRS key code and `key_pressed` the typed character
 * (see torirs_input.h), so editing keys and printable bytes come off different
 * fields of the same event.
 *
 * Safe to run for any visible panel: with nothing focused the chrome consumes
 * neither, so a panel that is merely on screen -- the developer readout, say
 * -- never swallows a keystroke meant for the game.
 */
void
app_chrome_route_keys(
    struct App* app,
    struct ToriRSChrome* ui,
    struct LibToriRS_Input* input)
{
    assert(app);
    assert(ui);
    assert(input);

    for( int i = 0; i < input->key_event_count; i++ )
    {
        struct LibToriRS_KeyEvent const* ev = &input->key_events[i];
        int consumed = 0;

        if( ev->key_pressed >= 32 && ev->key_pressed < 127 )
            consumed = ToriRSChrome_KeyChar(ui, ev->key_pressed);
        else
        {
            int const edit = app_dbgui_key_edit_from_osrs(ev->key_typed);
            if( edit != TORIRS_CHROME_KEY_NONE )
                consumed = ToriRSChrome_KeyEdit(ui, edit);
        }
        if( consumed )
            app->input_frame_consumed = 1;
    }
}

void
app_loc_editor_tick(
    struct App* app,
    struct LibToriRS_Input* input)
{
    int activated;

    assert(app);
    assert(input);

    /* Same suppression as the developer overlay toggle: a chat line has focus
     * must not also flip debug chrome. */
    if( !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_LOC_EDITOR) )
    {
        /* Toggling visibility only, never the selection -- a target picked
         * with Reselect stays active across a close/reopen. */
        app->locedit.visible = !app->locedit.visible;
        ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->locedit.panel, app->locedit.visible);
    }

    /* Footprint outline, toggled here rather than in its own tick because it
     * is the same loc-inspection tool and wants the same chat suppression.
     * Restores the configured mode instead of a literal 1, so a run started
     * with TORIRS_HOVER_FOOTPRINT=<loc id> keeps outlining that id after an
     * off/on rather than silently downgrading to "the hovered loc". */
    if( !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_HOVER_FOOTPRINT) )
    {
        app->hover_footprint = app->hover_footprint ? 0 : app->hover_footprint_mode;
        app->need_redraw = 1;
        TORIRS_LOG("hover_footprint: %d\n", app->hover_footprint);
    }

    /* Map editor panel. Gated on the session existing, so binding this key in a
     * manifest with no [editor:boot] is inert rather than a panel with nothing
     * behind it. */
    if( app->editor && !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_MAP_EDITOR) )
    {
        Editor_PanelSetVisible(&app->editor_panel, &app->dbg_ui, !app->editor_panel.visible);
        app->need_redraw = 1;
    }

    /* Both tools inspect locs the pick classifier drops by default — walls,
     * fences, gravel, ground decor: everything with no ops on it, which is
     * most of what a placement or footprint question is actually about. Told
     * here, once, from the two toggles that own the state, so neither tool has
     * to reach into the pick path itself. */
    /* The MAP editor makes every loc pickable too: its Select/Delete minimenu
     * rows are built per PICKED element, so a wall or roof with no ops of its
     * own -- invisible to the pick without this flag -- could never grow a
     * "Select Wall" row however the menu was gated. This one line is the
     * difference between "the menu ignores half the tile" and not. */
    WorldEntity_SceneryDebugSetTools(
        app->locedit.visible || app->hover_footprint != 0 ||
        (app->editor && app->editor_panel.visible));

    /* Remember the world tile under the cursor whenever the cursor is NOT
     * over the panel itself. Runs every frame, panel open or not, so the
     * moment Reselect is clicked there is already a last-known-good world
     * hover to read -- the live world_hover_tile_x/z cannot be used at click
     * time because reaching the menu item necessarily moved the cursor onto
     * the panel first. */
    LocEditorSelection_NoteHover(
        &app->locedit_selection,
        ToriRSChrome_HitTest(&app->dbg_ui, input->curr.mouse_x, input->curr.mouse_y) >= 0,
        app->world_hover_tile_x,
        app->world_hover_tile_z);

    /*
     * Overlay input, for ANY visible chrome panel.
     *
     * Asked of the chrome rather than listed here, because the list was the
     * bug: this was gated on the loc editor, then on the loc editor OR the map
     * editor, and every panel added after that -- the plugin settings panel
     * among them -- silently got no clicks. A panel whose checkboxes cannot be
     * ticked reads as broken, not as unrouted, so the gate is now "is anything
     * of this instance on screen" and a new panel needs no gate edit at all.
     */
    if( ToriRSChrome_HasVisiblePanel(&app->dbg_ui) )
        app_chrome_route_input(app, &app->dbg_ui, input);

    if( app->locedit.visible || app->editor_panel.visible )
    {
        /* A chat line stealing W/A/S/D/R/Space/Backspace would make the panel
         * unusable, so force it (and the modal chat variants) closed for as
         * long as this panel is open rather than merely suppressing the
         * toggle key like the developer overlay does. The later chat-focus
         * code (Enter / click-in-chat-region) is itself gated on
         * locedit_visible so it cannot steal focus back mid-session.
         *
         * Deliberately NOT part of the generic routing above: this is an
         * editor's claim on the whole keyboard, and a panel a player may leave
         * open beside the game -- the plugin window -- must not disable chat
         * for as long as it is up. */
        app->chat_input_active = 0;
        app->chat.social_input_open = 0;
        app->chat.dialog_input_open = 0;

        /*
         * A focused model view owns the movement keys: WASD orbits, E/F zooms,
         * arrows orbit too. Every accepted key re-renders with the camera
         * held, consumes the frame, and never reaches the world camera --
         * which also checks this focus itself, for the keys that arrive on
         * frames this block does not see.
         *
         * HELD, not the down edge. The world camera flies for as long as W is
         * down and the preview has to answer the same gesture the same way: on
         * the edge alone, holding a key nudged the model once and then sat
         * there, which is indistinguishable from a control that does not work.
         * The steps are per FRAME because of it -- a fifth of the old edge
         * step, so a press-and-release is still a small turn and a hold is a
         * smooth orbit rather than a spin.
         */
        if( app_modelview_focused(app) )
        {
            int took = 0;
            int const yaw_step = 12;
            int const pitch_step = 6;

            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_A) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT) )
            {
                EditorPreviewCamera_Orbit(&app->preview_camera, 0, -yaw_step);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_D) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT) )
            {
                EditorPreviewCamera_Orbit(&app->preview_camera, 0, yaw_step);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_W) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP) )
            {
                EditorPreviewCamera_Orbit(&app->preview_camera, -pitch_step, 0);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_S) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN) )
            {
                EditorPreviewCamera_Orbit(&app->preview_camera, pitch_step, 0);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_E) )
            {
                EditorPreviewCamera_Zoom(&app->preview_camera, -1);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_F) )
            {
                EditorPreviewCamera_Zoom(&app->preview_camera, 1);
                took = 1;
            }
            if( took )
            {
                /* Orbit and Zoom have already said this is the same model. */
                app->input_frame_consumed = 1;
                app->need_redraw = 1;
            }
        }

        /* The map editor's own key: apply the current tool to the SELECTION,
         * so a subject picked once can be operated on without going back to
         * the world with the cursor. `E` because the camera's up/down moved to
         * R/F, leaving it free next to WASD. */
        if( app->editor && app->editor_panel.visible && !app_text_input_focused(app) &&
            !app_modelview_focused(app) && LibToriRS_Input_IsKeyDown(input, TORIRSK_E) )
        {
            Editor_PanelApplyToSelection(&app->editor_panel, app);
            app->input_frame_consumed = 1;
        }

        /*
         * Keyboard control, the fast path the menu rows advertise. IsKeyDown
         * (edge, not held) so one press moves one tile rather than a nudge
         * repeating every frame a key is held down. Space reselects at the
         * live world_hover_tile_x/z directly -- pressing a key, unlike
         * clicking a menu row, never moves the cursor off the world first, so
         * the live hover is already correct and the remembered
         * locedit_hover_x/z (which Reselect itself reads) is equally valid
         * here since this same tick already refreshed it above.
         *
         * Gated on the LOC editor specifically, not on "any panel is open".
         * The enclosing block widened to route input for the map editor too,
         * and these came along with it -- so with only the map editor open,
         * W/A/S/D nudged whatever loc the loc editor had latched *while also*
         * flying the camera, since a nudge does not consume the frame. Same
         * reasoning as the activation latch below.
         */
        if( app->locedit.visible && LibToriRS_Input_IsKeyDown(input, TORIRSK_D) )
            app_loc_editor_nudge(app, 1, 0);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_A) )
            app_loc_editor_nudge(app, -1, 0);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_W) )
            app_loc_editor_nudge(app, 0, 1);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_S) )
            app_loc_editor_nudge(app, 0, -1);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_R) )
            app_loc_editor_rotate(app);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_SPACE) )
            app_loc_editor_reselect(app);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_BACKSPACE) )
            app_loc_editor_deselect(app);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_ESCAPE) )
        {
            app->locedit.visible = 0;
            ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->locedit.panel, 0);
        }

        /* Only when the LOC editor is open. The block above widened to route
         * input for any visible panel, but this half is loc-editor rows, and
         * draining the shared activation latch here swallowed the map editor's
         * clicks -- its dropdown showed the new value while its tool never
         * changed, because the activation was taken before its tick ran. */
        activated = app->locedit.visible ? ToriRSChrome_TakeActivated(&app->dbg_ui) : -1;
        if( activated >= 0 )
        {
            if( activated == app->locedit.item_xplus )
                app_loc_editor_nudge(app, 1, 0);
            else if( activated == app->locedit.item_xminus )
                app_loc_editor_nudge(app, -1, 0);
            else if( activated == app->locedit.item_zplus )
                app_loc_editor_nudge(app, 0, 1);
            else if( activated == app->locedit.item_zminus )
                app_loc_editor_nudge(app, 0, -1);
            else if( activated == app->locedit.item_rotate )
                app_loc_editor_rotate(app);
            else if( activated == app->locedit.item_reselect )
                app_loc_editor_reselect(app);
            else if( activated == app->locedit.item_deselect )
                app_loc_editor_deselect(app);
            else if( activated == app->locedit.item_close )
            {
                app->locedit.visible = 0;
                ToriRSChrome_PanelSetVisible(&app->dbg_ui, app->locedit.panel, 0);
            }
        }
    }

    if( ToriRSChrome_Build(&app->dbg_ui) )
    {
        app->need_redraw = 1;
        ToriRSChrome_DamageClear(&app->dbg_ui);
    }
}

