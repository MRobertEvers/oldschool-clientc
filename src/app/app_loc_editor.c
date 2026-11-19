/*
 * The loc editor panel: rows and buttons. (The chrome input routing every
 * developer panel shares is app_chrome.c's.)
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
 * The chrome routing it used to carry -- where the pointer this tool reads is
 * decided, whether a click belongs to a chrome panel or to the world
 * underneath it -- serves every panel of both chrome instances, and is
 * app_chrome.c's.
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
    if( app->editor_shell->locedit_selection.terrain )
    {
        char settings[4 * 6 + 1];
        char meshes[WORLD_MAP_TERRAIN_LEVELS + 1];
        int const cache_level = app->editor_shell->locedit_selection.terrain_level;

        World_TileSettingsText(
            app->world,
            app->editor_shell->locedit_selection.scene_x,
            app->editor_shell->locedit_selection.scene_z,
            settings,
            (int)sizeof(settings));
        World_TerrainMeshLevelsText(
            app->world,
            app->editor_shell->locedit_selection.scene_x,
            app->editor_shell->locedit_selection.scene_z,
            meshes,
            (int)sizeof(meshes));

        snprintf(text, sizeof(text), "terrain tile, mesh on level %d", cache_level);
        ToriRSChrome_SetText(&app->dev_overlay->ui, app->editor_shell->locedit.row_target, text);
        snprintf(
            text,
            sizeof(text),
            "x=%d z=%d abs(%d,%d)",
            app->editor_shell->locedit_selection.scene_x,
            app->editor_shell->locedit_selection.scene_z,
            app->world ? app->world->_base_tile_x + app->editor_shell->locedit_selection.scene_x
                       : -1,
            app->world ? app->world->_base_tile_z + app->editor_shell->locedit_selection.scene_z
                       : -1);
        ToriRSChrome_SetText(&app->dev_overlay->ui, app->editor_shell->locedit.row_pos, text);
        snprintf(
            text,
            sizeof(text),
            "cache=%d draw=%d paint=%d",
            cache_level,
            World_TerrainDrawLevel(
                app->world,
                app->editor_shell->locedit_selection.scene_x,
                app->editor_shell->locedit_selection.scene_z,
                cache_level),
            World_LocPaintLevel(
                app->world,
                app->editor_shell->locedit_selection.scene_x,
                app->editor_shell->locedit_selection.scene_z,
                cache_level));
        ToriRSChrome_SetText(&app->dev_overlay->ui, app->editor_shell->locedit.row_size, text);
        snprintf(text, sizeof(text), "s[%s] mesh[%s]", settings, meshes);
        ToriRSChrome_SetText(&app->dev_overlay->ui, app->editor_shell->locedit.row_extra, text);
        return;
    }

    if( app->editor_shell->locedit_selection.loc_id < 0 )
    {
        ToriRSChrome_SetText(
            &app->dev_overlay->ui, app->editor_shell->locedit.row_target, "nothing selected");
        ToriRSChrome_SetText(&app->dev_overlay->ui, app->editor_shell->locedit.row_pos, "");
        ToriRSChrome_SetText(&app->dev_overlay->ui, app->editor_shell->locedit.row_size, "");
        ToriRSChrome_SetText(&app->dev_overlay->ui, app->editor_shell->locedit.row_extra, "");
        return;
    }
    snprintf(
        text,
        sizeof(text),
        "loc %d shape %d",
        app->editor_shell->locedit_selection.loc_id,
        app->editor_shell->locedit_selection.shape);
    ToriRSChrome_SetText(&app->dev_overlay->ui, app->editor_shell->locedit.row_target, text);
    snprintf(
        text,
        sizeof(text),
        "x=%d z=%d level=%d",
        app->editor_shell->locedit_selection.scene_x,
        app->editor_shell->locedit_selection.scene_z,
        app->editor_shell->locedit_selection.level);
    ToriRSChrome_SetText(&app->dev_overlay->ui, app->editor_shell->locedit.row_pos, text);
    snprintf(
        text,
        sizeof(text),
        "size %dx%d angle=%d",
        app->editor_shell->locedit_selection.size_x,
        app->editor_shell->locedit_selection.size_z,
        app->editor_shell->locedit_selection.angle);
    ToriRSChrome_SetText(&app->dev_overlay->ui, app->editor_shell->locedit.row_size, text);
    /* A baked-map loc usually has no LocType.name resolved client-side (that
     * lives in the config, not the placed entity), so an empty name is the
     * common case -- fall back to whether it can be clicked at all rather
     * than print a blank row. */
    if( app->editor_shell->locedit_selection.name[0] )
        snprintf(
            text,
            sizeof(text),
            "\"%s\" interactive=%d",
            app->editor_shell->locedit_selection.name,
            app->editor_shell->locedit_selection.interactive);
    else
        snprintf(
            text, sizeof(text), "interactive=%d", app->editor_shell->locedit_selection.interactive);
    ToriRSChrome_SetText(&app->dev_overlay->ui, app->editor_shell->locedit.row_extra, text);
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

    LocEditorSelection_Clear(&app->editor_shell->locedit_selection);
    if( !app->world ||
        !LocEditorSelection_HoverTile(&app->editor_shell->locedit_selection, &hover_x, &hover_z) )
    {
        app_loc_editor_refresh_labels(app);
        return;
    }
    player = app_local_player(app);
    app->editor_shell->locedit_selection.level = player ? player->grid_position.level : 0;
    /* loc_shape < 0: match the first loc on the tile regardless of layer --
     * a decoration like a bridge is exactly as findable as a wall this way. */
    idx = World_SceneryFindAt(
        app->world, hover_x, hover_z, app->editor_shell->locedit_selection.level, -1);
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
        LocEditorSelection_SelectLoc(&app->editor_shell->locedit_selection, &placement);
    }
    app_loc_editor_refresh_labels(app);
}

/* Explicit Deselect: clears the target without touching the world. Clears the
 * tile selection too — one row, both subjects, or Deselect would appear to do
 * nothing while a tile was up. */
static void
app_loc_editor_deselect(struct App* app)
{
    LocEditorSelection_Clear(&app->editor_shell->locedit_selection);
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
        LocEditorSelection_SelectLoc(&app->editor_shell->locedit_selection, &placement);
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
    LocEditorSelection_SelectTile(
        &app->editor_shell->locedit_selection, scene_x, scene_z, cache_level);
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
    int recorded;
    int const edit_layout =
        app->world_builder && WorldBuilder_GetLayout(app->world_builder) == WORLD_BUILDER_LAYOUT_EDIT;

    if( !LocEditorSelection_Nudge(&app->editor_shell->locedit_selection, dx, dz, &move) )
        return;
    /* The document first: the same move against the authored loc list, so it
     * survives a reload and can be saved. Under the EDIT layout that is the
     * whole edit -- the flush moves the placement -- and a runtime change on
     * top would leave a second, default-lit copy. STATIC still shows it
     * through the zone-packet seam (and so does EDIT when the square is not
     * open in the document, which the record then refused). */
    recorded = Editor_PanelRecordLocEdit(
        &app->editor_shell->editor_panel,
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
    app_loc_editor_refresh_labels(app);
}

/* Same-tile change with the next of the 4 config angles (0..3 = W/N/E/S,
 * entity_scenery.h) -- no del needed, App_WorldLocChange already replaces
 * whatever is at scene_x/z. */
static void
app_loc_editor_rotate(struct App* app)
{
    struct LocEditorMove move;
    int recorded;
    int const edit_layout =
        app->world_builder && WorldBuilder_GetLayout(app->world_builder) == WORLD_BUILDER_LAYOUT_EDIT;

    if( !LocEditorSelection_Rotate(&app->editor_shell->locedit_selection, &move) )
        return;
    /* Same pair as a nudge: the authored record first, then (STATIC, or a
     * square the document does not hold) the scene. */
    recorded = Editor_PanelRecordLocEdit(
        &app->editor_shell->editor_panel,
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
    app_loc_editor_refresh_labels(app);
}

void
app_loc_editor_tick(
    struct App* app,
    struct LibToriRS_Input* input)
{
    int activated;
    int popup_was_open;
    int field_had_keys;

    assert(app);
    assert(input);

    /* A dropdown list, a menu or a colour popup up as this frame's keys
     * arrive: its Escape is the chrome's, which closes the popup with it
     * (ToriRSChrome_KeyEdit) -- and by the time the loc editor's own Escape is
     * read below the popup is already gone, so it is remembered here. One
     * press closes the menu; the next one closes the panel. */
    popup_was_open =
        app->dev_overlay->ui.dropdown_open >= 0 || app->dev_overlay->ui.colorpick_open >= 0;
    /* Likewise a chrome field that is being typed in as the keys arrive: they
     * are its -- Ctrl+A selects its content, Escape leaves it -- and the loc
     * editor's shortcuts on the same keys (Ctrl+A nudges, Escape closes the
     * panel) wait for a frame in which no field has the keyboard. */
    field_had_keys = app_chrome_focus_types(&app->dev_overlay->ui) ||
                     app_chrome_focus_types(&app->plugin_win->ui);

    /* Same suppression as the developer overlay toggle: a chat line has focus
     * must not also flip debug chrome. */
    if( !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_LOC_EDITOR) )
    {
        /* Toggling visibility only, never the selection -- a target picked
         * with Reselect stays active across a close/reopen. */
        app->editor_shell->locedit.visible = !app->editor_shell->locedit.visible;
        ToriRSChrome_PanelSetVisible(
            &app->dev_overlay->ui,
            app->editor_shell->locedit.panel,
            app->editor_shell->locedit.visible);
    }

    /* The side-apps' keys of the loc editor's point (the hover footprint's
     * toggle: app_tool_hover_footprint.c), under the same chat suppression. */
    if( !app_text_input_focused(app) )
        app_side_app_deliver_keys(app, input, SIDE_APP_PHASE_LOC_EDITOR);

    /* Map editor panel. Gated on the session existing, so binding this key in a
     * manifest with no [editor:boot] is inert rather than a panel with nothing
     * behind it. */
    if( app->editor && !app_text_input_focused(app) &&
        app_debug_key_down(app, input, APP_DEBUG_HOTKEY_MAP_EDITOR) )
    {
        Editor_PanelSetVisible(
            &app->editor_shell->editor_panel,
            &app->dev_overlay->ui,
            !app->editor_shell->editor_panel.visible);
        RedrawRequest_Raise(&app->redraw_request);
    }

    /* The loc editor and the side-apps that inspect locs (the hover
     * footprint) see locs the pick classifier drops by default — walls,
     * fences, gravel, ground decor: everything with no ops on it, which is
     * most of what a placement or footprint question is actually about. Told
     * here, once, from the toggles that own the state, so no tool has to
     * reach into the pick path itself. */
    /* The MAP editor makes every loc pickable too: its Select/Delete minimenu
     * rows are built per PICKED element, so a wall or roof with no ops of its
     * own -- invisible to the pick without this flag -- could never grow a
     * "Select Wall" row however the menu was gated. This one line is the
     * difference between "the menu ignores half the tile" and not. */
    WorldEntity_SceneryDebugSetTools(
        app->editor_shell->locedit.visible || SideApps_AnyInspectsScenery(app->side_apps) ||
        (app->editor && app->editor_shell->editor_panel.visible));

    /* Remember the world tile under the cursor whenever the cursor is NOT
     * over the panel itself. Runs every frame, panel open or not, so the
     * moment Reselect is clicked there is already a last-known-good world
     * hover to read -- the live world_hover_tile_x/z cannot be used at click
     * time because reaching the menu item necessarily moved the cursor onto
     * the panel first. */
    LocEditorSelection_NoteHover(
        &app->editor_shell->locedit_selection,
        ToriRSChrome_HitTest(&app->dev_overlay->ui, input->curr.mouse_x, input->curr.mouse_y) >= 0,
        app->world_views.frame->world_hover_tile_x,
        app->world_views.frame->world_hover_tile_z);

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
    if( ToriRSChrome_HasVisiblePanel(&app->dev_overlay->ui) )
        app_chrome_route_input(app, &app->dev_overlay->ui, input);

    if( app->editor_shell->locedit.visible || app->editor_shell->editor_panel.visible )
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
        UIChatLine_SetInputActive(&app->chat_line, 0);
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
                EditorPreviewCamera_Orbit(&app->editor_shell->preview_camera, 0, -yaw_step);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_D) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT) )
            {
                EditorPreviewCamera_Orbit(&app->editor_shell->preview_camera, 0, yaw_step);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_W) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP) )
            {
                EditorPreviewCamera_Orbit(&app->editor_shell->preview_camera, -pitch_step, 0);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_S) ||
                LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN) )
            {
                EditorPreviewCamera_Orbit(&app->editor_shell->preview_camera, pitch_step, 0);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_E) )
            {
                EditorPreviewCamera_Zoom(&app->editor_shell->preview_camera, -1);
                took = 1;
            }
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_F) )
            {
                EditorPreviewCamera_Zoom(&app->editor_shell->preview_camera, 1);
                took = 1;
            }
            if( took )
            {
                /* Orbit and Zoom have already said this is the same model. */
                ToriRS_InputClaimsTakeFrame(&app->input_claims);
                RedrawRequest_Raise(&app->redraw_request);
            }
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
         *
         * The nudges are Ctrl+W/A/S/D (Cmd on a Mac): the bare keys fly the
         * camera and nothing in the world view takes them over (owner,
         * 2026-11-07).
         */
        if( !app->editor_shell->locedit.visible )
        {
            /* The map editor's keys are its hotkey table's (app_editor_hotkeys.c). */
        }
        else if(
            field_had_keys || app_chrome_focus_types(&app->dev_overlay->ui) ||
            app_chrome_focus_types(&app->plugin_win->ui) )
        {
            /* A chrome field holds the keyboard (or did when this frame's
             * keys arrived): the letters, Backspace, Space and the Ctrl
             * chords are text and clipboard there, not loc shortcuts. */
        }
        else if( input->key_held[TORIRSK_CTRL] && LibToriRS_Input_IsKeyDown(input, TORIRSK_D) )
            app_loc_editor_nudge(app, 1, 0);
        else if( input->key_held[TORIRSK_CTRL] && LibToriRS_Input_IsKeyDown(input, TORIRSK_A) )
            app_loc_editor_nudge(app, -1, 0);
        else if( input->key_held[TORIRSK_CTRL] && LibToriRS_Input_IsKeyDown(input, TORIRSK_W) )
            app_loc_editor_nudge(app, 0, 1);
        else if( input->key_held[TORIRSK_CTRL] && LibToriRS_Input_IsKeyDown(input, TORIRSK_S) )
            app_loc_editor_nudge(app, 0, -1);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_R) )
            app_loc_editor_rotate(app);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_SPACE) )
            app_loc_editor_reselect(app);
        else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_BACKSPACE) )
            app_loc_editor_deselect(app);
        else if( !popup_was_open && LibToriRS_Input_IsKeyDown(input, TORIRSK_ESCAPE) )
        {
            app->editor_shell->locedit.visible = 0;
            ToriRSChrome_PanelSetVisible(
                &app->dev_overlay->ui, app->editor_shell->locedit.panel, 0);
        }

        /* Only when the LOC editor is open. The block above widened to route
         * input for any visible panel, but this half is loc-editor rows, and
         * draining the shared activation latch here swallowed the map editor's
         * clicks -- its dropdown showed the new value while its tool never
         * changed, because the activation was taken before its tick ran. */
        activated = app->editor_shell->locedit.visible
                        ? ToriRSChrome_TakeActivated(&app->dev_overlay->ui)
                        : -1;
        if( activated >= 0 )
        {
            if( activated == app->editor_shell->locedit.item_xplus )
                app_loc_editor_nudge(app, 1, 0);
            else if( activated == app->editor_shell->locedit.item_xminus )
                app_loc_editor_nudge(app, -1, 0);
            else if( activated == app->editor_shell->locedit.item_zplus )
                app_loc_editor_nudge(app, 0, 1);
            else if( activated == app->editor_shell->locedit.item_zminus )
                app_loc_editor_nudge(app, 0, -1);
            else if( activated == app->editor_shell->locedit.item_rotate )
                app_loc_editor_rotate(app);
            else if( activated == app->editor_shell->locedit.item_reselect )
                app_loc_editor_reselect(app);
            else if( activated == app->editor_shell->locedit.item_deselect )
                app_loc_editor_deselect(app);
            else if( activated == app->editor_shell->locedit.item_close )
            {
                app->editor_shell->locedit.visible = 0;
                ToriRSChrome_PanelSetVisible(
                    &app->dev_overlay->ui, app->editor_shell->locedit.panel, 0);
            }
        }
    }

    if( ToriRSChrome_Build(&app->dev_overlay->ui) )
    {
        RedrawRequest_Raise(&app->redraw_request);
        ToriRSChrome_DamageClear(&app->dev_overlay->ui);
    }
}

