/*
 * The map editor: ghost placement, the preview, square loads, and the editor panel drain.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_map_editor_ghost_forget(struct App* app);
static void
app_map_editor_ghost_remove(struct App* app);
static struct ToriDraw_Sprite*
app_preview_raster(
    struct App* app,
    struct ToriDraw_ModelHandle hnd);

/**
 * Whether the map editor's SELECT tool is the thing the minimenu should be
 * offering "Select wall/object/decor/terrain" rows for -- panel closed or a
 * paint tool active both mean no such row belongs on the menu, same as
 * `app->locedit.visible` gates the loc editor's own Select row.
 */
bool
app_mapedit_select_active(struct App const* app)
{
    assert(app);
    return app->editor_panel.visible && app->editor_panel.tool == EDITOR_TOOL_SELECT;
}

/** Keyboard belongs to the catalog's model view? (Focused via a click; the
 *  chrome holds the focus, the app routes the keys.) */
int
app_modelview_focused(struct App const* app)
{
    int const f = app->dbg_ui.focus;
    return f >= 0 && f < app->dbg_ui.widget_count &&
           app->dbg_ui.widgets[f].kind == TORIRS_CHROME_W_MODELVIEW;
}

void
App_EditorPlaceSpawn(
    struct App* app,
    int is_obj,
    int id,
    int scene_x,
    int scene_z,
    int level)
{
    char args[32];

    assert(app);

    snprintf(args, sizeof(args), "id=%d", id);
    if( is_obj )
        app_world_spawn_obj(app, scene_x, scene_z, level, args);
    else
        app_world_spawn_npc(app, scene_x, scene_z, level, args);
}

/** Start a load the square browser asked for. Runs on the frame boundary with
 *  the other editor drains, so a panel click never loads a world mid-tick. */
void
app_map_editor_open_pending_square(struct App* app)
{
    int chunks[2];

    assert(app);

    if( !app->editor || !app->editor_panel.sq_open_pending )
        return;
    app->editor_panel.sq_open_pending = 0;
    if( app->world_load_inflight )
        return;

    chunks[0] = app->editor_panel.sq_open_x;
    chunks[1] = app->editor_panel.sq_open_z;
    TORIRS_LOG("editor: opening m%d_%d\n", chunks[0], chunks[1]);
    app_world_load_begin(app, chunks, 1);
}

/** Shared-state facts from this connection's Client land on the panel — the
 *  receiving half of the selection relay. Registered at editor construction;
 *  fires from Editor_PumpFacts inside the per-frame drain below, and for the
 *  common single-connection boot that includes this panel's own echoes,
 *  which apply idempotently. */
void
app_editor_on_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct App* app = user_data;

    assert(app);
    Editor_PanelApplySharedState(&app->editor_panel, app, key, values, count);
}

void
app_map_editor_drain(struct App* app)
{
    int squares[EDITOR_REBUILD_QUEUE_MAX * 2];
    int count;

    assert(app);

    if( !app->editor || app->editor->rebuild_count == 0 )
        return;
    if( app->world_load_inflight )
        return; /* A load is already rewriting the scene; let it land first. */

    count = Editor_DrainRebuilds(app->editor, app->provider, squares, EDITOR_REBUILD_QUEUE_MAX);
    if( count <= 0 )
        return;

    /* The chunklist rebuild path -- the same one an offline world load uses,
     * given only the squares whose meshes the edit invalidated. */
    app->world_load_attempted = 0;
    app_world_load_begin(app, squares, count);
    app->need_redraw = 1;
}
/*
 * The MAP EDITOR's client half -- placing and previewing map edits against the
 * live scene, and the ghost placements that show what an edit will look like
 * before it is committed.
 *
 * A unity fragment of app.c, not a module. It is textually part of app.c's
 * translation unit and included at exactly the point it was cut from, so every
 * helper here stays static and every App field it reads stays where it was.
 * The split is for the reader: this is a developer tool, it is off in every
 * player-facing build, and it should not be in the way of the code that runs
 * every frame.
 *
 * The document and the wire are elsewhere -- editor_doc.c owns the edit model
 * and editor_host_*.c the local and remote hosts. What stays here is the part
 * that needs a scene: which tile the pointer is on, what a ghost looks like
 * standing in it, and how to rebuild the preview when it moves.
 *
 * @see src/app.c, src/editor/editor.h
 */

/**
 * A click in the world applies the current tool, as one undoable edit.
 *
 * Gated on `input_frame_consumed` so a click that landed on the panel does not
 * also paint the tile behind it -- the overlay sets that flag when it takes a
 * press, and this runs after it for exactly that reason.
 *
 * ALSO gated on the minimenu owning this gesture, which is TWO conditions and
 * not one. `input_frame_consumed` covers neither: this runs early in the frame
 * (before UITree_InteractFrame), so nothing has classified the click yet.
 *
 *   - `minimenu.visible` -- a menu is on screen, so the world is not taking
 *     clicks at all.
 *   - `interact.swallow_left_click` -- the menu already consumed the PRESS
 *     edge of this click and this is the matching RELEASE.
 *
 * The second is the one that actually bites, and checking only the first is
 * why "Select Object" still latched terrain after it was supposedly fixed:
 * the minimenu selects on mousedown and hides itself immediately, so by the
 * time the mouse-up arrives -- the edge THIS function triggers on, a frame
 * later -- `minimenu.visible` is already 0 and the gate opens. The latch is
 * still set at that instant because interact_frame retires it further down
 * the same frame, after this ran. Reading it here is not a race: this runs
 * before the retire by construction, which is the same ordering that made
 * the bug.
 *
 * The tile is the one under the cursor THIS frame. The pickset and hover are
 * refreshed by the render pass, so they are at most one frame stale, which at
 * mouse speed is the tile the user is looking at.
 */
void
app_map_editor_world_click(
    struct App* app,
    struct LibToriRS_Input* input)
{
    assert(app);
    assert(input);

    if( !app->editor || !app->editor_panel.visible )
        return;
    if( app->interact.minimenu.visible || app->interact.swallow_left_click )
    {
        if( getenv("TORIRS_EDIT_DEBUG") && input->curr.mouse_button_up[TORIRSM_LEFT] )
            TORIRS_LOG(
                "edit: click belongs to the minimenu (visible=%d swallow=%d)\n",
                app->interact.minimenu.visible,
                app->interact.swallow_left_click);
        return;
    }
    if( app->input_frame_consumed )
    {
        if( getenv("TORIRS_EDIT_DEBUG") && input->curr.mouse_button_up[TORIRSM_LEFT] )
            TORIRS_LOG("edit: click swallowed (input_frame_consumed)\n");
        return;
    }
    if( !input->curr.mouse_button_up[TORIRSM_LEFT] )
        return;
    if( getenv("TORIRS_EDIT_DEBUG") )
        TORIRS_LOG(
            "edit: click tool=%d consumed=%d hover=%d,%d\n",
            (int)app->editor_panel.tool,
            app->input_frame_consumed,
            app->world_hover_tile_x,
            app->world_hover_tile_z);
    if( app->world_hover_tile_x < 0 )
        return;

    /*
     * Modifier accelerators: hold a key to act on a layer without changing the
     * tool dropdown first.
     *
     *   L  place the catalog's picked loc      (the tool's Place loc)
     *   K  delete the loc under the cursor     (the tool's Delete loc)
     *   C  clear every loc on the tile
     *
     * Deliberately resolving to the SAME functions the tool rows call rather
     * than to a parallel path, so the readout, the undo step and the document
     * write are identical however the edit was asked for. Gated on
     * app_text_input_focused via the caller's chain -- without that, `L` typed
     * into the catalog's search box would place a loc.
     */
    if( !app_text_input_focused(app) )
    {
        int const level = Editor_PanelEditLevel(&app->editor_panel, app);
        int const hx = app->world_hover_tile_x;
        int const hz = app->world_hover_tile_z;

        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_L) )
        {
            Editor_PanelPlaceLocAt(&app->editor_panel, app, hx, hz, level);
            app->need_redraw = 1;
            return;
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_K) )
        {
            Editor_PanelDeleteLocAt(&app->editor_panel, app, hx, hz, level);
            app->need_redraw = 1;
            return;
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_C) )
        {
            Editor_PanelClearLocsAt(&app->editor_panel, app, hx, hz, level);
            app->need_redraw = 1;
            return;
        }
    }

    /* SELECT latches the plain-click default: the hovered TILE, unambiguous
     * even where a wall, a wall-decor and a ground loc share it. Picking one
     * of those exactly is what the minimenu's "Select wall/object/decor" rows
     * are for (app_minimenu_run_option) -- this is the one-click fallback for
     * "just the ground". */
    if( app->editor_panel.tool == EDITOR_TOOL_SELECT )
    {
        Editor_PanelSelectTerrain(
            &app->editor_panel,
            app,
            app->world_hover_tile_x,
            app->world_hover_tile_z,
            app->world_hover_tile_level);
        app->need_redraw = 1;
        return;
    }

    /* One click is one undo step. A drag would open the stroke on press and
     * close it on release; this is the single-click case, which is a stroke of
     * one and needs no bracketing.
     *
     * The level the PANEL says to edit, not the one the pick happened to
     * return: a pinned plane is the whole point of the Level row, and reading
     * the hover here would silently ignore it. */
    Editor_PanelApplyToolAt(
        &app->editor_panel,
        app,
        app->world_hover_tile_x,
        app->world_hover_tile_z,
        Editor_PanelEditLevel(&app->editor_panel, app));

    /* A Place or Move click landed on the ghost's tile: the real add just
     * replaced the ghost's element, so the ghost must be FORGOTTEN, not
     * removed -- removing now would delete the loc that was just placed. */
    if( app->editor_panel.tool == EDITOR_TOOL_LOC_PLACE ||
        app->editor_panel.tool == EDITOR_TOOL_LOC_MOVE )
        app_map_editor_ghost_forget(app);

    /*
     * The subject follows the work -- BY KIND.
     *
     * A tile tool's click latches the tile it painted, so the readout
     * describes what just happened and Apply repeats there. The LOC tools do
     * NOT latch terrain: their subject is a loc, and stamping a terrain latch
     * after every place/move wiped the loc selection the user was working
     * with -- Place selects what it placed (inside PlaceLocAt), Move keeps
     * the selection riding the loc, and Delete's handler latches the vacated
     * tile itself. Switching tools never touches the selection at all.
     */
    app->need_redraw = 1;
}

/**
 * Push the frame's edits back into the provider and rebuild what they changed.
 *
 * Once per frame, not once per edit: a brush drag produces a command per tile,
 * and remeshing a square for each of them would spend the frame rebuilding
 * terrain nobody has seen yet. Draining here coalesces them, so a drag costs
 * one rebuild per square per frame however fast the mouse moves.
 */
/** Forget the ghost WITHOUT removing it from the scene -- for the commit
 *  click, whose real placement just replaced the ghost's element on the same
 *  tile and layer. Removing would delete the loc that was just placed. */
static void
app_map_editor_ghost_forget(struct App* app)
{
    assert(app);
    MapEditorGhost_Commit(&app->map_ghost);
}

/** Remove the ghost from the scene, put back whatever it displaced, forget.
 *  Which of those happens is MapEditorGhost's; the placements are ours. */
static void
app_map_editor_ghost_remove(struct App* app)
{
    struct MapEditorGhostSpec removed;
    struct MapEditorGhostSpec restore;
    bool has_restore = false;

    assert(app);
    if( !MapEditorGhost_Remove(&app->map_ghost, &removed, &has_restore, &restore) )
        return;
    App_WorldLocChange(
        app, removed.scene_x, removed.scene_z, removed.level, -1, removed.shape, removed.angle);
    /* Scene-only, like the ghost itself -- the document never knew about
     * either. */
    if( has_restore )
        App_WorldLocChange(
            app,
            restore.scene_x,
            restore.scene_z,
            restore.level,
            restore.loc_id,
            restore.shape,
            restore.angle);
    app->need_redraw = 1;
}

/**
 * Keep the Place-loc hover ghost current. Once per frame, with the other
 * editor drains.
 */
void
app_map_editor_ghost_update(struct App* app)
{
    int want;
    int id = -1;
    int shape = 0;
    int angle = 0;
    int level;

    assert(app);

    if( !app->editor )
        return;

    /* Two tools ghost: Place previews the CATALOG pick, Move previews the
     * SELECTED loc at the tile it would land on. Move skips the selection's
     * own tile -- ghosting a loc onto itself replaces it with its own
     * translucent double, which reads as flicker, not preview. */
    {
        struct Editor_Panel const* panel = &app->editor_panel;
        int const hover_ok = app->world_hover_tile_x >= 0 && !app->interact.minimenu.visible &&
                             !app->input_frame_consumed;

        want = 0;
        if( panel->visible && hover_ok && panel->tool == EDITOR_TOOL_LOC_PLACE &&
            panel->cat_picked_id >= 0 && panel->cat_kind == CACHEPROVIDER_CATALOG_LOC )
        {
            want = 1;
            id = panel->cat_picked_id;
            Editor_PanelGhostSpec(&app->editor_panel, app, &shape, &angle);
            level = Editor_PanelEditLevel(panel, app);
        }
        else if(
            panel->visible && hover_ok && panel->tool == EDITOR_TOOL_LOC_MOVE &&
            panel->sel_kind == EDITOR_SELECTION_LOC &&
            !(app->world_hover_tile_x == panel->sel_scene_x &&
              app->world_hover_tile_z == panel->sel_scene_z) )
        {
            want = 1;
            id = panel->sel_loc_id;
            shape = panel->sel_shape;
            angle = panel->sel_angle;
            /* A move keeps its plane; the Level row is for edits, not this. */
            level = panel->sel_level;
        }
        else
            level = Editor_PanelEditLevel(panel, app);
    }

    {
        struct MapEditorGhostSpec const wanted = { .loc_id = id,
                                                   .scene_x = app->world_hover_tile_x,
                                                   .scene_z = app->world_hover_tile_z,
                                                   .level = level,
                                                   .shape = shape,
                                                   .angle = angle };

        /* The ghost follows the hover; any change of tile, loc or pose is a
         * remove + add. Same tile and spec: nothing to do but the alpha pass. */
        if( !want || !MapEditorGhost_Matches(&app->map_ghost, &wanted) )
            app_map_editor_ghost_remove(app);

        if( want && !app->map_ghost.active )
        {
            struct MapEditorGhostSpec displaced;
            bool has_displaced = false;

            /* Whoever holds this tile's slot in the ghost's layer is about to
             * be replaced by the add below; remember them for the restore.
             * Read BEFORE the add is queued -- the capture must see the
             * pre-ghost scene. */
            if( app->world )
            {
                int const occ = World_SceneryFindAt(
                    app->world, wanted.scene_x, wanted.scene_z, level, shape);
                if( occ >= 0 )
                {
                    struct WorldEntity_Scenery const* occupant =
                        World_EntityPoolGet(&app->world->entities.scenery, occ);
                    if( occupant )
                    {
                        has_displaced = true;
                        displaced = wanted;
                        displaced.loc_id = occupant->loc_id;
                        displaced.shape = occupant->shape;
                        displaced.angle = occupant->angle;
                    }
                }
            }

            App_WorldLocChange(
                app, wanted.scene_x, wanted.scene_z, level, id, shape, angle);
            MapEditorGhost_Place(
                &app->map_ghost, &wanted, has_displaced ? &displaced : NULL);
            app->need_redraw = 1;
        }
    }

    /* Translucency, once the async add has produced an element. The fade is
     * written onto the ELEMENT's own model rather than the loc's, so only the
     * placement under the cursor goes translucent. */
    if( MapEditorGhost_NeedsFade(&app->map_ghost) && app->world && app->scene )
    {
        int const idx = World_SceneryFindAt(
            app->world,
            app->map_ghost.spec.scene_x,
            app->map_ghost.spec.scene_z,
            app->map_ghost.spec.level,
            app->map_ghost.spec.shape);
        if( idx >= 0 )
        {
            struct WorldEntity_Scenery* scenery =
                World_EntityPoolGet(&app->world->entities.scenery, idx);
            /* ForWrite, not Get: placements of one loc share a single model
             * (a ToriDraw_SharedModel), and fading it in place would
             * ghost every other one of the same fence on screen. It also
             * answers the tagged-union question -- only a full model carries
             * faces to fade, and a sprite billboard comes back NULL. */
            struct ToriDraw_Model* model =
                scenery ? ToriDraw_SceneElementModelForWrite(app->scene, scenery->element_id)
                        : NULL;

            if( model && model->face_count > 0 )
            {
                /* RS face alpha: 0 opaque, higher more transparent. */
                if( !model->face_alphas )
                {
                    model->face_alphas = malloc((size_t)model->face_count);
                    assert(model->face_alphas);
                }
                memset(model->face_alphas, 150, (size_t)model->face_count);
                MapEditorGhost_NoteFaded(&app->map_ghost);
                app->need_redraw = 1;
            }
        }
    }
}

/**
 * Render the catalog's picked entry into its model-view well.
 *
 * Objs ride the inventory-icon pipeline unchanged. Locs have no equivalent --
 * loc models are composed per shape by the world builder, privately -- so this
 * picks the models for the DEFAULT shape (the catalog previews "what is this",
 * not a placement) and rasterises the first through the same
 * ModelFromToriRS -> light -> raster route the icons take. Models not resident
 * yet are queued and retried: the updater latches its key only once a render
 * lands, so a miss this frame is a retry next frame, not a permanent blank.
 */
/**
 * Raster a preview model with the preview camera, fitting the zoom on demand.
 *
 * The fit reads the model's bounds cylinder and scales the raster distance so
 * the larger dimension fills most of the well -- a candle and a castle gate
 * both arrive framed, instead of one vanishing and the other cropping to a
 * wall of pixels. The constant is calibrated against the obj-icon pipeline
 * (zoom 2000 frames a typical item in ~30px) and clamped so a degenerate
 * bounds cannot zoom to infinity.
 */
static struct ToriDraw_Sprite*
app_preview_raster(
    struct App* app,
    struct ToriDraw_ModelHandle hnd)
{
    {
        /* A model with no bounds is one that has not finished loading. 128 is
         * a middling size, so the well shows something framed rather than a
         * dot or a wall while it waits. */
        struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);

        (void)EditorPreviewCamera_TakeFit(
            &app->preview_camera,
            bounds ? bounds->radius : 64,
            bounds ? bounds->min_y : 0,
            bounds ? bounds->max_y : 128);
    }
    return ToriDraw_SpriteNewFromModelRaster(
        app->scene,
        hnd,
        app->preview_camera.zoom,
        app->preview_camera.pitch,
        app->preview_camera.yaw,
        120,
        96,
        false);
}

void
app_map_editor_preview_update(struct App* app)
{
    static int last_kind = -1;
    static int last_id = -1;
    struct Editor_Panel* panel = &app->editor_panel;
    struct ToriDraw_Sprite* sprite = NULL;
    /* Not the pick: while a multiloc VARIANT row is chosen this is that
     * rung's loc, so the well shows the variant the catalog is reading out. */
    int preview_id;

    assert(app);

    if( !app->editor || !panel->visible || panel->cat_view < 0 )
        return;
    preview_id = Editor_PanelCatalogPreviewId(panel);
    if( preview_id < 0 )
    {
        ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
        last_kind = -1;
        last_id = -1;
        return;
    }
    /* A key moved the camera: re-render the same pick. Taking the render is
     * also what resets the framing for a NEW one, so the two cannot disagree
     * about whether this is the same model. */
    if( EditorPreviewCamera_TakeRender(&app->preview_camera) )
    {
        last_kind = -1;
        last_id = -1;
    }
    if( panel->cat_kind == last_kind && preview_id == last_id )
        return;

    if( panel->cat_kind == CACHEPROVIDER_CATALOG_OBJ )
    {
        /*
         * The obj's own model, rastered with the preview camera -- NOT the
         * inventory icon.
         *
         * The icon was the obvious thing to reach for (it is already cached,
         * one call) and it is the one thing in this well that cannot be
         * turned: an icon is baked at the objtype's authored xan2d/yan2d/zoom2d
         * and handed back from an id-keyed cache, so every camera key was a
         * no-op for the whole obj kind while loc and npc orbited fine. Building
         * the model here costs a raster per nudge and answers the keys.
         *
         * The resize/recolour order is ObjModelLoader's (resize first, 128 ==
         * 1.0), so the preview is the item the game builds.
         */
        struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, preview_id);
        struct ToriDraw_Model* model;
        struct ToriDraw_ModelHandle hnd;

        if( !obj || (obj->inventory_model_id > 0 &&
                     !CacheProvider_ModelHas(app->provider, obj->inventory_model_id)) )
        {
            /* Objtype or model still to come. Task_ObjModelLoad fetches the
             * objtype, its count variant, the inventory model and that model's
             * textures together, so ask it once rather than per piece. */
            if( ObjModelLoad_NeedsWork(app->provider, preview_id, 1) )
            {
                int const ids[1] = { preview_id };
                int const counts[1] = { 1 };
                struct ToriRS_Task* task = CreateTask_ObjModelLoad(app->provider, ids, counts, 1);
                if( task )
                    ToriRS_TaskQueue_Add(app->runner.queue, task);
            }
            return; /* retry next frame once it lands */
        }
        if( obj->inventory_model_id <= 0 )
        {
            /* A real answer, not a pending one: a bank note or placeholder
             * has no model of its own. Cached, so this does not re-ask. */
            ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
            last_kind = panel->cat_kind;
            last_id = preview_id;
            return;
        }

        {
            struct ToriRS_Model* rs_model =
                CacheProvider_ModelGet(app->provider, obj->inventory_model_id);

            assert(rs_model);
            model = ToriDraw_ModelFromToriRS(rs_model);
            assert(model);
            if( obj->resize_x != 128 || obj->resize_y != 128 || obj->resize_z != 128 )
                ToriDraw_ModelScale(model, obj->resize_x, obj->resize_z, obj->resize_y);
            for( int i = 0; i < obj->recolor_count; i++ )
                ToriDraw_ModelRecolor(model, obj->recolors_from[i], obj->recolors_to[i]);
            ToriDraw_ModelSetBoundsCylinder(model);
            ToriDraw_ModelDropNonSdTextures(app->provider, model);
            memset(&hnd, 0, sizeof(hnd));
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = model;
            ToriDraw_LightModelScene(hnd, obj->contrast, obj->ambient);
            sprite = app_preview_raster(app, hnd);
            ToriDraw_ModelFree(model);
        }

        if( sprite )
        {
            struct ToriDraw_Sprite** sprites = malloc(sizeof(*sprites));
            assert(sprites);
            sprites[0] = sprite;
            if( ToriDraw_SceneSpriteHas(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID) )
                ToriDraw_SceneSpriteRemove(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID);
            ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID, sprites, 1);
            ToriRSChrome_ModelViewSet(
                &app->dbg_ui, panel->cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID);
            last_kind = panel->cat_kind;
            last_id = preview_id;
        }
        return;
    }

    if( panel->cat_kind == CACHEPROVIDER_CATALOG_LOC )
    {
        struct ToriRS_Location* cfg = CacheProvider_LocationGet(app->provider, preview_id);
        int model_id = -1;

        if( !cfg )
            return;
        /* The default-shape model set: no shapes array means one set; with
         * one, prefer the centrepiece (10) row, else the first row. */
        if( cfg->shapes_and_model_count > 0 && cfg->models && cfg->lengths )
        {
            int row = 0;
            if( cfg->shapes )
                for( int i = 0; i < cfg->shapes_and_model_count; i++ )
                    if( cfg->shapes[i] == RSCACHE_LOC_SHAPE_SCENERY )
                    {
                        row = i;
                        break;
                    }
            if( cfg->lengths[row] > 0 )
                model_id = cfg->models[row][0];
        }
        if( model_id <= 0 )
        {
            ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
            last_kind = panel->cat_kind;
            last_id = preview_id;
            return;
        }

        if( !CacheProvider_ModelHas(app->provider, model_id) )
        {
            struct ToriRS_Task* task = CreateTask_ModelLoad(app->provider, model_id);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
            return; /* retry next frame once it lands */
        }

        {
            struct ToriRS_Model* rs_model = CacheProvider_ModelGet(app->provider, model_id);
            struct ToriDraw_Model* model;
            struct ToriDraw_ModelHandle hnd;

            assert(rs_model);
            model = ToriDraw_ModelFromToriRS(rs_model);
            assert(model);
            for( int i = 0; i < cfg->recolor_count; i++ )
                ToriDraw_ModelRecolor(model, cfg->recolors_from[i], cfg->recolors_to[i]);
            ToriDraw_ModelSetBoundsCylinder(model);
            ToriDraw_ModelDropNonSdTextures(app->provider, model);
            memset(&hnd, 0, sizeof(hnd));
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = model;
            ToriDraw_LightModelScene(hnd, cfg->contrast, cfg->ambient);
            sprite = app_preview_raster(app, hnd);
            ToriDraw_ModelFree(model);
        }

        if( sprite )
        {
            struct ToriDraw_Sprite** sprites = malloc(sizeof(*sprites));
            assert(sprites);
            sprites[0] = sprite;
            /* One well, re-rendered per pick: the old sprite goes with the
             * old registration. The scene owns what it holds. */
            if( ToriDraw_SceneSpriteHas(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID) )
                ToriDraw_SceneSpriteRemove(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID);
            ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID, sprites, 1);
            ToriRSChrome_ModelViewSet(
                &app->dbg_ui, panel->cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID);
            last_kind = panel->cat_kind;
            last_id = preview_id;
        }
        return;
    }

    if( panel->cat_kind == CACHEPROVIDER_CATALOG_NPC )
    {
        struct ToriRS_Npctype* npc = CacheProvider_NpctypeGet(app->provider, preview_id);
        int missing = 0;

        if( !npc || npc->models_count <= 0 )
        {
            ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
            last_kind = panel->cat_kind;
            last_id = preview_id;
            return;
        }
        for( int i = 0; i < npc->models_count; i++ )
            if( npc->models[i] > 0 && !CacheProvider_ModelHas(app->provider, npc->models[i]) )
            {
                struct ToriRS_Task* task = CreateTask_ModelLoad(app->provider, npc->models[i]);
                if( task )
                    ToriRS_TaskQueue_Add(app->runner.queue, task);
                missing = 1;
            }
        if( missing )
            return; /* retry once the parts land */

        {
            /* An npc body is its PARTS MERGED -- QBD is two models, and a
             * first-part-only render shows a torso and reads as corruption.
             * Merge exactly as the entity path does, then raster the merge. */
            struct ToriDraw_Model* parts[16];
            struct ToriDraw_Model* merged = NULL;
            struct ToriDraw_ModelHandle hnd;
            int part_count = 0;

            for( int i = 0; i < npc->models_count && part_count < 16; i++ )
            {
                struct ToriRS_Model* rs =
                    npc->models[i] > 0 ? CacheProvider_ModelGet(app->provider, npc->models[i])
                                       : NULL;
                if( !rs )
                    continue;
                parts[part_count] = ToriDraw_ModelFromToriRS(rs);
                assert(parts[part_count]);
                part_count++;
            }
            if( part_count == 0 )
            {
                ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
                last_kind = panel->cat_kind;
                last_id = preview_id;
                return;
            }
            merged = part_count == 1 ? parts[0] : ToriDraw_ModelNewMerge(parts, part_count);
            assert(merged);
            for( int i = 0; i < npc->recolor_count; i++ )
                ToriDraw_ModelRecolor(merged, npc->recolors_from[i], npc->recolors_to[i]);
            ToriDraw_ModelSetBoundsCylinder(merged);
            ToriDraw_ModelDropNonSdTextures(app->provider, merged);
            memset(&hnd, 0, sizeof(hnd));
            hnd.kind = TORIDRAWMK_MODEL;
            hnd.u.model.model = merged;
            ToriDraw_LightModelScene(hnd, npc->contrast, npc->ambient);
            sprite = app_preview_raster(app, hnd);
            if( part_count > 1 )
                for( int i = 0; i < part_count; i++ )
                    ToriDraw_ModelFree(parts[i]);
            ToriDraw_ModelFree(merged);
        }

        if( sprite )
        {
            struct ToriDraw_Sprite** sprites = malloc(sizeof(*sprites));
            assert(sprites);
            sprites[0] = sprite;
            if( ToriDraw_SceneSpriteHas(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID) )
                ToriDraw_SceneSpriteRemove(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID);
            ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID, sprites, 1);
            ToriRSChrome_ModelViewSet(
                &app->dbg_ui, panel->cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID);
            last_kind = panel->cat_kind;
            last_id = preview_id;
        }
        return;
    }

    ToriRSChrome_ModelViewSet(&app->dbg_ui, panel->cat_view, 0);
    last_kind = panel->cat_kind;
    last_id = preview_id;
}

