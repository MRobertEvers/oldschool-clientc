/*
 * The map editor: ghost placement, the preview, square loads, and the editor
 * panel drain.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "mem/torirs_mem.h"
#include "mem/torimem_toridraw.h"
#include "app/app_internal.h"
#include "editor/editor_derive.h"
#include "editor/editor_view.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanel_ops.h"
#include "editor/panels/edpanels.h"
#include "engine/world_builder/edit/world_edit.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_map_editor_ghost_forget(struct App* app);
static void
app_map_editor_ghost_remove(struct App* app);
static int
app_map_editor_ghost_is_edit(struct App const* app);
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
    return app->editor_panel.visible && Editor_PanelTool(&app->editor_panel) == EDITOR_TOOL_SELECT;
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
    /* STATE_VIEW_SELF or a camera key is the view's alone; STATE_SELECTION
     * answers 0 here, so the panels still get it. */
    if( EditorView_AppOnState(app, key, values, count) )
        return;
    Editor_PanelApplySharedState(&app->editor_panel, app, key, values, count);
}

/* ---- the EDIT world's flush path ---------------------------------------- */

/*
 * What the EDIT world was last told about one open square: its authored tiles,
 * its loc list, and the EDIT placement handle of every loc in that list.
 *
 * The mirror applies the authority's commands to the document; this is how the
 * per-frame flush finds out which tiles and locs those commands touched, by
 * comparing the document with what it last pushed. Comparing rather than
 * listening keeps the path whole for every way a document changes -- a
 * command, an undo, a resync that re-opens it -- and a square costs a 16k-tile
 * compare, microseconds, only on a frame its square was queued.
 *
 * The invariant that makes it safe: for every square with a shadow, the
 * provider's map records were derived from exactly the shadow, so a window
 * bake (pan, bridge flag, first load) shows what EDIT shows. The seed below
 * and the sync update the two together.
 *
 * One App per process, so the set is file-static rather than an App field.
 * The core below takes what it touches as arguments (struct AppEditCtx) and
 * not the App, so edit_world_test drives the same code through a real
 * embedded ToriRSMapEd without a window.
 */
struct AppMapEditShadow
{
    int used;
    int map_x;
    int map_z;
    struct Editor_Tile* tiles; /* EDITOR_SQUARE_TILES */
    struct Editor_Loc* locs;
    int* handles;
    int loc_count;
    int loc_cap;
};

static struct
{
    struct AppMapEditShadow squares[EDITOR_DOC_MAX_SQUARES];
    /* The EDIT bake the handles belong to; another serial means rebind. */
    uint32_t handle_serial;
    int handles_bound;
} g_app_edit_mirror;

/* What the EDIT sync reads and writes. */
struct AppEditCtx
{
    struct Editor* editor;
    struct CacheProvider* provider;
    struct WorldBuilder* builder;
    struct World* world;
    struct ToriDraw_Scene* scene;
};

void
app_map_editor_free(struct App* app)
{
    assert(app);
    for( int i = 0; i < EDITOR_DOC_MAX_SQUARES; i++ )
    {
        struct AppMapEditShadow* sh = &g_app_edit_mirror.squares[i];
        ToriMem_Free(ToriMem_Editor(), sh->tiles);
        ToriMem_Free(ToriMem_Editor(), sh->locs);
        ToriMem_Free(ToriMem_Editor(), sh->handles);
    }
    memset(&g_app_edit_mirror, 0, sizeof(g_app_edit_mirror));
}

static struct AppMapEditShadow*
app_edit_shadow_find(
    int map_x,
    int map_z)
{
    for( int i = 0; i < EDITOR_DOC_MAX_SQUARES; i++ )
    {
        struct AppMapEditShadow* sh = &g_app_edit_mirror.squares[i];
        if( sh->used && sh->map_x == map_x && sh->map_z == map_z )
            return sh;
    }
    return NULL;
}

static void
app_edit_shadow_reserve(
    struct AppMapEditShadow* sh,
    int loc_count)
{
    if( loc_count <= sh->loc_cap )
        return;
    sh->loc_cap = loc_count + 256;
    sh->locs = ToriMem_Realloc(ToriMem_Editor(), sh->locs, (size_t)sh->loc_cap * sizeof(*sh->locs));
    assert(sh->locs);
    sh->handles =
        ToriMem_Realloc(ToriMem_Editor(), sh->handles, (size_t)sh->loc_cap * sizeof(*sh->handles));
    assert(sh->handles);
}

/* The provider's two records for a square, from the document -- what
 * Editor_LoadSquare seeds, done here so the shadow and the provider are
 * written together. */
static void
app_edit_seed_provider(
    const struct AppEditCtx* ctx,
    const struct Editor_Square* square)
{
    int map_id = CacheProvider_MapId(square->map_x, square->map_z);
    struct ToriRS_MapTerrain* terrain = CacheProvider_MapTerrainGet(ctx->provider, map_id);
    struct ToriRS_MapLocs* locs = CacheProvider_MapSceneryGet(ctx->provider, map_id);

    if( !terrain )
    {
        terrain = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*terrain));
        assert(terrain);
        CacheProvider_MapTerrainAdd(ctx->provider, map_id, terrain);
    }
    Editor_SquareDeriveTerrain(square, ctx->editor->doc.profile, terrain);
    if( !locs )
    {
        locs = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*locs));
        assert(locs);
        CacheProvider_MapSceneryAdd(ctx->provider, map_id, locs);
    }
    ToriMem_Free(ToriMem_Assets(), locs->locs);
    locs->locs = NULL;
    Editor_SquareDeriveLocs(square, locs);
}

static void
app_edit_shadow_take(
    struct AppMapEditShadow* sh,
    const struct Editor_Square* square)
{
    if( !sh->tiles )
    {
        sh->tiles = ToriMem_Alloc(ToriMem_Editor(), sizeof(*sh->tiles) * EDITOR_SQUARE_TILES);
        assert(sh->tiles);
    }
    memcpy(sh->tiles, square->tiles, sizeof(*sh->tiles) * EDITOR_SQUARE_TILES);
    app_edit_shadow_reserve(sh, square->loc_count);
    if( square->loc_count > 0 )
        memcpy(sh->locs, square->locs, sizeof(*sh->locs) * (size_t)square->loc_count);
    for( int i = 0; i < square->loc_count; i++ )
        sh->handles[i] = WORLD_BUILDER_HANDLE_NONE;
    sh->loc_count = square->loc_count;
}

/*
 * A square the world is about to load: give it a shadow, and the provider the
 * records the shadow describes. A square that already has one keeps it -- the
 * provider already holds what it says, edits included. No-op under STATIC.
 *
 * The App-free core of app_map_editor_shadow_seed; edit_world_test calls it.
 */
void
app_map_editor_edit_seed(
    struct Editor* editor,
    struct CacheProvider* provider,
    struct WorldBuilder* builder,
    int map_x,
    int map_z)
{
    struct AppEditCtx ctx = { editor, provider, builder, NULL, NULL };
    struct Editor_Square* square;

    assert(editor);
    assert(provider);
    assert(builder);
    if( WorldBuilder_GetLayout(builder) != WORLD_BUILDER_LAYOUT_EDIT )
        return;
    square = Editor_DocFindSquare(&editor->doc, map_x, map_z);
    if( !square || !square->loaded || app_edit_shadow_find(map_x, map_z) )
        return;
    for( int i = 0; i < EDITOR_DOC_MAX_SQUARES; i++ )
    {
        struct AppMapEditShadow* sh = &g_app_edit_mirror.squares[i];
        if( sh->used )
            continue;
        sh->used = 1;
        sh->map_x = map_x;
        sh->map_z = map_z;
        app_edit_seed_provider(&ctx, square);
        app_edit_shadow_take(sh, square);
        return;
    }
    /* The document holds at most EDITOR_DOC_MAX_SQUARES, and so does this. */
    assert(0 && "app_map_editor: more shadowed squares than the document can hold");
}

/* app_world_load_begin, after Editor_LoadSquare: the square's first shadow,
 * taken with the seed so the flush can tell what later commands changed. */
void
app_map_editor_shadow_seed(
    struct App* app,
    int map_x,
    int map_z)
{
    assert(app);
    assert(app->editor);
    app_map_editor_edit_seed(app->editor, app->provider, app->world_builder, map_x, map_z);
}

/* Squares one world load takes (app_world_load.c's APP_WORLD_MAP_SQUARE_MAX). */
#define APP_MAP_EDITOR_OPEN_MAX 16

/**
 * Start a load the square picker asked for. Runs on the frame boundary with
 * the other editor drains, so a panel click never loads a world mid-tick.
 *
 * Replace opens the one square; add keeps every square the document has open
 * and adds this one (a world load names its whole square list). A square from
 * a lane (or one DOC_CREATE just made) is opened in the mirror by its full
 * key first and its provider records seeded from the document, because the
 * load's own seeding only reads maps/ by coordinates.
 */
void
app_map_editor_open_pending_square(struct App* app)
{
    struct Editor_Panel* panel;
    int chunks[APP_MAP_EDITOR_OPEN_MAX * 2];
    int count = 0;

    assert(app);

    panel = &app->editor_panel;
    if( !app->editor || !panel->sq_open_pending )
        return;
    if( app->world_load_inflight )
        return; /* the request waits for the load in flight */
    panel->sq_open_pending = 0;

    if( panel->sq_open_storage[0] )
    {
        char key[EDITOR_DOC_KEY_MAX];
        snprintf(key, sizeof(key), "%s/m%d_%d", panel->sq_open_storage, panel->sq_open_x, panel->sq_open_z);
        if( !Editor_OpenDoc(app->editor, EDITOR_DOC_SQUARE, key) )
        {
            TORIRS_ERR("editor: cannot open %s\n", key);
            return;
        }
    }

    if( panel->sq_open_add )
    {
        struct Editor_Doc* doc = &app->editor->doc;
        for( int i = 0; i < doc->square_count && count < APP_MAP_EDITOR_OPEN_MAX - 1; i++ )
        {
            if( !doc->squares[i].loaded )
                continue;
            if( doc->squares[i].map_x == panel->sq_open_x && doc->squares[i].map_z == panel->sq_open_z )
                continue;
            chunks[count * 2] = doc->squares[i].map_x;
            chunks[count * 2 + 1] = doc->squares[i].map_z;
            count++;
        }
    }
    chunks[count * 2] = panel->sq_open_x;
    chunks[count * 2 + 1] = panel->sq_open_z;
    count++;

    /* Every square already in the mirror seeds the provider from the
     * document (an open lane square, a created one, unsaved edits). */
    {
        struct AppEditCtx ctx = { app->editor, app->provider, app->world_builder, app->world, app->scene };
        for( int i = 0; i < count; i++ )
        {
            struct Editor_Square* square = Editor_DocFindSquare(&app->editor->doc, chunks[i * 2], chunks[i * 2 + 1]);
            if( square && square->loaded && app->provider )
                app_edit_seed_provider(&ctx, square);
        }
    }

    TORIRS_LOG("editor: opening m%d_%d%s (%d square%s)\n", panel->sq_open_x, panel->sq_open_z,
        panel->sq_open_add ? ", added" : "", count, count == 1 ? "" : "s");
    app->world_load_attempted = 0;
    app_world_load_begin(app, chunks, count);
}

static int
app_edit_scene_of(
    const struct AppEditCtx* ctx,
    int map_x,
    int map_z,
    int local_x,
    int local_z,
    int* out_x,
    int* out_z)
{
    struct World* world = ctx->world;
    *out_x = World_ToSceneX(world, map_x, local_x);
    *out_z = World_ToSceneZ(world, map_z, local_z);
    return *out_x >= 0 && *out_z >= 0 && *out_x < world->_scene_size &&
           *out_z < world->_scene_size;
}

/* After an EDIT bake, every shadowed square in the window learns its
 * placement handles: the bake placed the provider's list, which is the
 * shadow's, in order. */
static void
app_edit_bind_handles(const struct AppEditCtx* ctx)
{
    struct WorldBuilder* wb = ctx->builder;
    uint32_t serial;

    if( !WorldEdit_Ready(wb) )
        return;
    serial = WorldEdit_BakeSerial(wb);
    if( g_app_edit_mirror.handles_bound && g_app_edit_mirror.handle_serial == serial )
        return;
    for( int i = 0; i < EDITOR_DOC_MAX_SQUARES; i++ )
    {
        struct AppMapEditShadow* sh = &g_app_edit_mirror.squares[i];
        int in_window;
        if( !sh->used )
            continue;
        in_window = WorldEdit_SquareInWindow(wb, sh->map_x, sh->map_z);
        for( int k = 0; k < sh->loc_count; k++ )
            sh->handles[k] = in_window ? WorldEdit_SquarePlacement(wb, sh->map_x, sh->map_z, k)
                                       : WORLD_BUILDER_HANDLE_NONE;
    }
    g_app_edit_mirror.handle_serial = serial;
    g_app_edit_mirror.handles_bound = 1;
}

/* A floor the EDIT world can apply now: what it names is resident. A config
 * the provider lacks would mesh differently from the window bake, which loads
 * it -- so that edit goes through a rebake instead. */
static int
app_edit_floor_ready(
    const struct AppEditCtx* ctx,
    const struct ToriRS_MapFloor* floor)
{
    struct CacheProvider* p = ctx->provider;

    if( floor->underlay_id > 0 && !CacheProvider_UnderlayHas(p, floor->underlay_id - 1) )
        return 0;
    if( floor->overlay_id > 0 )
    {
        struct ToriRS_Flotype* flo;
        if( !CacheProvider_FlotypeHas(p, floor->overlay_id - 1) )
            return 0;
        flo = CacheProvider_FlotypeGet(p, floor->overlay_id - 1);
        if( flo && flo->texture >= 0 && !CacheProvider_TextureHas(p, flo->texture) )
            return 0;
    }
    return 1;
}

/* A loc the EDIT world can place now: its config, every model it names, and
 * its animation are resident, and it is no multiloc (whose rungs the window
 * load resolves and fetches as a closure). */
static int
app_edit_loc_ready(
    const struct AppEditCtx* ctx,
    int loc_id)
{
    struct ToriRS_Location* cfg;

    if( !CacheProvider_LocationHas(ctx->provider, loc_id) )
        return 0;
    cfg = CacheProvider_LocationGet(ctx->provider, loc_id);
    if( !cfg )
        return 0;
    if( cfg->transform_count > 0 )
        return 0;
    for( int row = 0; row < cfg->shapes_and_model_count; row++ )
        for( int k = 0; cfg->models && cfg->lengths && k < cfg->lengths[row]; k++ )
            if( cfg->models[row][k] > 0 && !CacheProvider_ModelHas(ctx->provider, cfg->models[row][k]) )
                return 0;
    if( cfg->seq_id >= 0 && !ToriDraw_SceneAnimationGet(ctx->scene, cfg->seq_id) )
        return 0;
    return 1;
}

static int
app_edit_tile_equal(
    const struct Editor_Tile* a,
    const struct Editor_Tile* b)
{
    return a->has_height == b->has_height && a->height == b->height &&
           a->has_overlay == b->has_overlay && a->overlay_id == b->overlay_id &&
           a->shape == b->shape && a->rotation == b->rotation && a->settings == b->settings &&
           a->underlay_id == b->underlay_id;
}

static int
app_edit_loc_equal(
    const struct Editor_Loc* a,
    const struct Editor_Loc* b)
{
    return a->loc_id == b->loc_id && a->shape == b->shape && a->rotation == b->rotation &&
           a->level == b->level && a->x == b->x && a->z == b->z;
}

/*
 * One queued square: the tiles and locs the document changed since the shadow
 * go to the provider and, where the square is in the EDIT window, to the world
 * as tile_set / loc_remove / loc_place. Returns 1 when the window has to be
 * baked again instead (an asset the world does not hold yet).
 */
static int
app_edit_sync_square(
    const struct AppEditCtx* ctx,
    const struct Editor_Square* square,
    struct AppMapEditShadow* sh)
{
    static uint8_t columns[EDITOR_SQUARE_X * EDITOR_SQUARE_Z];
    struct WorldBuilder* wb = ctx->builder;
    int in_window = WorldEdit_SquareInWindow(wb, square->map_x, square->map_z);
    int rebake = 0;
    int map_id = CacheProvider_MapId(square->map_x, square->map_z);
    struct ToriRS_MapTerrain* terrain = CacheProvider_MapTerrainGet(ctx->provider, map_id);
    struct ToriRS_MapLocs* provider_locs = CacheProvider_MapSceneryGet(ctx->provider, map_id);
    int* handles;

    assert(terrain);
    assert(provider_locs);

    /* Tiles: every column with a changed level is derived whole -- a height
     * on level 0 moves every floor stacked on it. */
    memset(columns, 0, sizeof(columns));
    for( int i = 0; i < EDITOR_SQUARE_TILES; i++ )
        if( !app_edit_tile_equal(&square->tiles[i], &sh->tiles[i]) )
            columns[i % (EDITOR_SQUARE_X * EDITOR_SQUARE_Z)] = 1;
    for( int c = 0; c < EDITOR_SQUARE_X * EDITOR_SQUARE_Z; c++ )
    {
        struct ToriRS_MapFloor column[EDITOR_SQUARE_LEVELS];
        int x = c % EDITOR_SQUARE_X;
        int z = c / EDITOR_SQUARE_X;
        int sx;
        int sz;

        if( !columns[c] )
            continue;
        Editor_TileDeriveColumn(square, x, z, column);
        for( int l = 0; l < EDITOR_SQUARE_LEVELS; l++ )
        {
            terrain->tiles_xyz[Editor_TileIndex(x, z, l)] = column[l];
            if( !app_edit_floor_ready(ctx, &column[l]) )
                rebake = 1;
        }
        if( in_window && !rebake &&
            app_edit_scene_of(ctx, square->map_x, square->map_z, x, z, &sx, &sz) )
            for( int l = 0; l < EDITOR_SQUARE_LEVELS; l++ )
                WorldBuilder_TileSet(wb, sx, sz, l, &column[l]);
    }

    /* Locs: the document only ever appends to a square's list and removes from
     * it in place, so walking both lists in step names every removal and the
     * appended tail. (A resync that reorders is still right: it reads as
     * removals and additions.) */
    handles = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * (size_t)(square->loc_count + 1));
    assert(handles);
    {
        int i = 0;
        int j = 0;
        while( i < sh->loc_count )
        {
            if( j < square->loc_count && app_edit_loc_equal(&sh->locs[i], &square->locs[j]) )
            {
                handles[j++] = sh->handles[i++];
                continue;
            }
            if( in_window && !rebake && sh->handles[i] != WORLD_BUILDER_HANDLE_NONE )
                WorldBuilder_LocRemove(wb, sh->handles[i]);
            i++;
        }
        for( ; j < square->loc_count; j++ )
        {
            const struct Editor_Loc* loc = &square->locs[j];
            struct World_LocPlacement placement;

            handles[j] = WORLD_BUILDER_HANDLE_NONE;
            if( !in_window || rebake )
                continue;
            if( !app_edit_loc_ready(ctx, loc->loc_id) )
            {
                rebake = 1;
                continue;
            }
            if( !app_edit_scene_of(
                    ctx, square->map_x, square->map_z, loc->x, loc->z, &placement.scene_x,
                    &placement.scene_z) )
                continue;
            placement.level = loc->level;
            placement.loc_id = loc->loc_id;
            placement.shape = loc->shape;
            placement.angle = loc->rotation;
            handles[j] = WorldBuilder_LocPlace(wb, &placement);
        }
    }
    ToriMem_Free(ToriMem_Assets(), provider_locs->locs);
    provider_locs->locs = NULL;
    Editor_SquareDeriveLocs(square, provider_locs);

    app_edit_shadow_take(sh, square);
    memcpy(sh->handles, handles, sizeof(int) * (size_t)square->loc_count);
    ToriMem_Free(ToriMem_Editor(), handles);
    return rebake;
}

/*
 * The EDIT layout's per-frame step, App-free: every square the mirror queued
 * since the last step reaches the world as tile-local edits, then one
 * WorldBuilder_Flush shows them. Call it after Editor_PumpFacts, with no world
 * load in flight. Returns 1 when the window must be baked again from the
 * provider (which already holds every edit) instead -- an asset the world
 * lacks, a bridge flag, a square it never shadowed.
 */
int
app_map_editor_edit_step(
    struct Editor* editor,
    struct CacheProvider* provider,
    struct WorldBuilder* builder,
    struct World* world,
    struct ToriDraw_Scene* scene)
{
    struct AppEditCtx ctx = { editor, provider, builder, world, scene };
    int rebake = 0;

    assert(editor);
    assert(provider);
    assert(builder);
    assert(world);
    assert(scene);

    app_edit_bind_handles(&ctx);
    for( int i = 0; i < editor->rebuild_count; i++ )
    {
        int map_x = editor->rebuild_queue[i * 2];
        int map_z = editor->rebuild_queue[i * 2 + 1];
        struct Editor_Square* square = Editor_DocFindSquare(&editor->doc, map_x, map_z);
        struct AppMapEditShadow* sh;

        /* A neighbour across the seam the document never opened. */
        if( !square || !square->loaded )
            continue;
        sh = app_edit_shadow_find(map_x, map_z);
        if( !sh )
        {
            /* Never pushed: nothing to compare with, so the provider takes the
             * whole square and, if the world shows it, the window rebakes. */
            app_map_editor_edit_seed(editor, provider, builder, map_x, map_z);
            if( WorldEdit_SquareInWindow(builder, map_x, map_z) )
                rebake = 1;
            continue;
        }
        rebake |= app_edit_sync_square(&ctx, square, sh);
    }
    editor->rebuild_count = 0;

    if( !rebake && WorldBuilder_Flush(builder) == WORLD_BUILDER_FLUSH_REBUILD )
        rebake = 1;
    return rebake;
}

/* Bake the window the world shows again, from the provider. */
static void
app_edit_rebake_window(struct App* app)
{
    struct World* world = app->world;
    int chunks[EDITOR_REBUILD_QUEUE_MAX * 2];
    int count = 0;

    for( int mx = world->_chunk_sw_x; mx <= world->_chunk_ne_x; mx++ )
        for( int mz = world->_chunk_sw_z; mz <= world->_chunk_ne_z; mz++ )
        {
            if( count >= EDITOR_REBUILD_QUEUE_MAX )
                break;
            chunks[count * 2] = mx;
            chunks[count * 2 + 1] = mz;
            count++;
        }
    if( count == 0 )
        return;
    app->world_load_attempted = 0;
    app_world_load_begin(app, chunks, count);
    app->need_redraw = 1;
}

/*
 * The EDIT layout's frame (design #world: Editor_DrainRebuilds becomes
 * Editor_Flush). The document's changes since last frame reach the world as
 * tile-local edits, and one WorldBuilder_Flush shows them all; a square is
 * never remeshed whole.
 */
static void
app_map_editor_flush(struct App* app)
{
    int had_work;

    /* Other clients' edits land here, queued by square like ours. */
    Editor_PumpFacts(app->editor);
    if( app->world_load_inflight )
        return; /* the load lands first; the queue waits for it */
    if( !app->world || !app->world_active )
        return;
    had_work = app->editor->rebuild_count > 0;
    /* The document changed under the ghost: take it down first, so the sync
     * never meets an occupant the ghost has hidden. It is back next frame. */
    if( had_work && app->map_ghost.active )
        app_map_editor_ghost_remove(app);
    if( app_map_editor_edit_step(
            app->editor, app->provider, app->world_builder, app->world, app->scene) )
    {
        app_edit_rebake_window(app);
        return;
    }
    if( had_work )
        app->need_redraw = 1;
}

void
app_map_editor_drain(struct App* app)
{
    int squares[EDITOR_REBUILD_QUEUE_MAX * 2];
    int count;

    assert(app);

    if( !app->editor )
        return;
    /* EDIT: tile-local edits and one flush per frame. STATIC (the fallback,
     * and every game manifest): the queued squares rebuild through the load
     * path, as before. */
    if( app->world_builder && WorldBuilder_GetLayout(app->world_builder) == WORLD_BUILDER_LAYOUT_EDIT )
    {
        app_map_editor_flush(app);
        return;
    }
    if( app->editor->rebuild_count == 0 )
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

/* ---- the world click ---------------------------------------------------- */

/** A shift-click with the select tool: the loc under the cursor joins (or
 *  leaves) the multi-selection, by document address. */
static int
app_map_editor_toggle_loc_at(
    struct App* app,
    int scene_x,
    int scene_z,
    int level)
{
    struct EditorUi* ui = app->editor_panel.ui;
    struct WorldEntity_Scenery* scenery;
    struct Editor_LocRef ref;
    int idx;

    if( !app->world || !ui )
        return 0;
    idx = World_SceneryFindAt(app->world, scene_x, scene_z, level, -1);
    if( idx < 0 )
        return 0;
    scenery = World_EntityPoolGet(&app->world->entities.scenery, idx);
    if( !scenery )
        return 0;
    Editor_LocRefAt(
        &ref, app->world->_base_tile_x + scene_x, app->world->_base_tile_z + scene_z, level,
        scenery->shape, scenery->loc_id, scenery->angle & 3);
    (void)Editor_UiToggleLoc(ui, &ref);
    if( ui->hooks.selection_changed )
        ui->hooks.selection_changed(ui->hooks_user);
    return 1;
}

/** The loc selection's keys: arrows nudge, R rotates, S reshapes, D
 *  duplicates, Delete deletes. Only with a loc selected, so the same keys
 *  still fly the camera otherwise; a key that acted consumes the frame. */
static void
app_map_editor_loc_keys(
    struct App* app,
    struct LibToriRS_Input* input)
{
    struct EditorUi* ui = app->editor_panel.ui;
    int acted = 1;

    if( !ui || ui->tools.loc_count == 0 || app_text_input_focused(app) || app_modelview_focused(app) )
        return;
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_UP) )
        (void)edp_locs_nudge(ui, 0, 1);
    else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_DOWN) )
        (void)edp_locs_nudge(ui, 0, -1);
    else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_LEFT) )
        (void)edp_locs_nudge(ui, -1, 0);
    else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_RIGHT) )
        (void)edp_locs_nudge(ui, 1, 0);
    else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_R) )
        (void)edp_locs_rotate(ui);
    else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_S) )
        (void)edp_locs_reshape(ui);
    else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_D) )
        (void)edp_locs_duplicate(ui);
    else if( LibToriRS_Input_IsKeyDown(input, TORIRSK_DELETE)
             || LibToriRS_Input_IsKeyDown(input, TORIRSK_BACKSPACE) )
        (void)edp_locs_delete(ui);
    else
        acted = 0;
    if( acted )
    {
        app->input_frame_consumed = 1;
        app->need_redraw = 1;
    }
}

/** The world tile under the cursor, absolute. */
static int
app_map_editor_hover_world(
    struct App* app,
    int* out_x,
    int* out_z)
{
    if( !app->world || app->frame_view->world_hover_tile_x < 0 )
        return 0;
    *out_x = app->world->_base_tile_x + app->frame_view->world_hover_tile_x;
    *out_z = app->world->_base_tile_z + app->frame_view->world_hover_tile_z;
    return 1;
}

/** An open drag -- a brush stroke or a rectangle -- runs to the button's
 *  release wherever the pointer wanders, and closes there. */
static void
app_map_editor_drag(
    struct App* app,
    struct LibToriRS_Input* input)
{
    struct Editor_Panel* panel = &app->editor_panel;
    struct EditorUi* ui = panel->ui;
    int const released =
        input->curr.mouse_button_up[TORIRSM_LEFT] || !LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
    int world_x;
    int world_z;

    if( panel->stroke_active )
    {
        int const hx = app->frame_view->world_hover_tile_x;
        int const hz = app->frame_view->world_hover_tile_z;
        if( hx >= 0 && (hx != panel->stroke_last_x || hz != panel->stroke_last_z) )
        {
            Editor_PanelApplyToolAt(panel, app, hx, hz, Editor_PanelEditLevel(panel, app));
            panel->stroke_last_x = hx;
            panel->stroke_last_z = hz;
            app->need_redraw = 1;
        }
        if( released )
        {
            /* One drag, one undo step: the stroke opened on press. */
            ui->tools.stroke_open = 0;
            Editor_StrokeEnd(app->editor);
            panel->stroke_active = 0;
        }
    }
    if( panel->rect_dragging )
    {
        if( app_map_editor_hover_world(app, &world_x, &world_z) )
        {
            ui->tools.rect.x1 = world_x;
            ui->tools.rect.z1 = world_z;
        }
        if( released )
        {
            struct Editor_TileRect r;
            panel->rect_dragging = 0;
            if( Editor_TileRectNormalise(&ui->tools.rect, &r) )
                Editor_UiLog(ui, "editor: rectangle %dx%d at %d,%d level %d", r.x1 - r.x0 + 1,
                    r.z1 - r.z0 + 1, r.x0, r.z0, r.level);
            else
                Editor_UiLog(ui, "editor: that rectangle is larger than 64x64 tiles");
        }
        app->need_redraw = 1;
    }
}

/**
 * A press or click in the world applies the armed tool.
 *
 * Brushes paint from the PRESS and keep painting while the button is held, a
 * tile once per visit, the whole drag one undo step (the stroke opens on
 * press and closes on release). The rectangle tool drags its rectangle the
 * same way. The click tools (select, place, move, rotate, delete) act on the
 * RELEASE, as before.
 *
 * Gated on `input_frame_consumed` so a click that landed on a panel does not
 * also edit the tile behind it, and on the minimenu owning the gesture: both
 * `minimenu.visible` and `interact.swallow_left_click` -- the menu selects on
 * mousedown and hides itself, so the release a frame later must still be
 * recognised as the menu's (see the old note on "Select Object" latching
 * terrain).
 *
 * The tile is the one under the cursor this frame; the level is the one the
 * terrain panel pins (Editor_PanelEditLevel), not the hover's.
 */
void
app_map_editor_world_click(
    struct App* app,
    struct LibToriRS_Input* input)
{
    struct Editor_Panel* panel;
    struct EditorUi* ui;
    enum Editor_Tool tool;
    int level;
    int hx;
    int hz;

    assert(app);
    assert(input);

    panel = &app->editor_panel;
    if( !app->editor || !panel->ui )
        return;
    ui = panel->ui;
    ui->shift_held = LibToriRS_Input_IsKeyHeld(input, TORIRSK_SHIFT) ? 1 : 0;

    if( panel->stroke_active || panel->rect_dragging )
    {
        /* The editor closing mid-drag still closes the stroke: an open
         * stroke would fold every later edit into one undo step. */
        if( !panel->visible )
        {
            if( panel->stroke_active )
            {
                ui->tools.stroke_open = 0;
                Editor_StrokeEnd(app->editor);
            }
            panel->stroke_active = 0;
            panel->rect_dragging = 0;
            return;
        }
        app_map_editor_drag(app, input);
        return;
    }
    if( !panel->visible )
        return;
    if( app->frame_view->minimenu->visible || app->interact.swallow_left_click )
        return;
    if( app->input_frame_consumed )
        return;

    app_map_editor_loc_keys(app, input);
    if( app->input_frame_consumed )
        return;

    tool = ui->tools.tool;
    hx = app->frame_view->world_hover_tile_x;
    hz = app->frame_view->world_hover_tile_z;
    if( hx < 0 )
        return;
    level = Editor_PanelEditLevel(panel, app);

    /* The press opens a drag for the brushes and the rectangle. */
    if( input->curr.mouse_button_down[TORIRSM_LEFT] )
    {
        if( Editor_ToolIsBrush(tool) )
        {
            Editor_StrokeBegin(app->editor);
            ui->tools.stroke_open = 1;
            ui->tools.flatten_valid = 0;
            panel->stroke_active = 1;
            panel->stroke_last_x = hx;
            panel->stroke_last_z = hz;
            Editor_PanelApplyToolAt(panel, app, hx, hz, level);
            app->need_redraw = 1;
            return;
        }
        if( tool == EDITOR_TOOL_RECT_SELECT )
        {
            int world_x;
            int world_z;
            if( app_map_editor_hover_world(app, &world_x, &world_z) )
            {
                ui->tools.rect.valid = 1;
                ui->tools.rect.x0 = ui->tools.rect.x1 = world_x;
                ui->tools.rect.z0 = ui->tools.rect.z1 = world_z;
                ui->tools.rect.level = level;
                panel->rect_dragging = 1;
            }
            return;
        }
    }
    if( !input->curr.mouse_button_up[TORIRSM_LEFT] )
        return;

    /*
     * Modifier accelerators: hold a key to act on a layer without changing
     * the tool first. L places the catalog's loc, K deletes the loc under the
     * cursor, C clears every loc on the tile. They reach the same functions
     * the tools do. Gated on text focus: `L` typed into a search box must not
     * place a loc.
     */
    if( !app_text_input_focused(app) )
    {
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_L) )
        {
            Editor_PanelPlaceLocAt(panel, app, hx, hz, level);
            app->need_redraw = 1;
            return;
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_K) )
        {
            Editor_PanelDeleteLocAt(panel, app, hx, hz, level);
            app->need_redraw = 1;
            return;
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_C) )
        {
            Editor_PanelClearLocsAt(panel, app, hx, hz, level);
            app->need_redraw = 1;
            return;
        }
    }

    if( tool == EDITOR_TOOL_SELECT )
    {
        /* A plain click latches the TILE (unambiguous where a wall, a decor
         * and a ground loc share it; the minimenu's "Select wall" rows pick
         * those exactly). Shift-click adds the loc under it to the
         * multi-selection instead. */
        if( ui->shift_held )
            (void)app_map_editor_toggle_loc_at(app, hx, hz, app->frame_view->world_hover_tile_level);
        else
            Editor_PanelSelectTerrain(panel, app, hx, hz, app->frame_view->world_hover_tile_level);
        app->need_redraw = 1;
        return;
    }
    if( Editor_ToolIsBrush(tool) || tool == EDITOR_TOOL_RECT_SELECT )
        return;

    Editor_PanelApplyToolAt(panel, app, hx, hz, level);

    /* A Place or Move landed where the ghost stood. STATIC: the real add
     * replaced the ghost's element, so the ghost is FORGOTTEN, not removed
     * (removing would delete the placed loc). EDIT: the placed loc arrives
     * through the flush as its own placement, so the ghost is removed and
     * whatever it hid comes back. */
    if( tool == EDITOR_TOOL_LOC_PLACE || tool == EDITOR_TOOL_LOC_MOVE )
    {
        if( app_map_editor_ghost_is_edit(app) )
            app_map_editor_ghost_remove(app);
        else
            app_map_editor_ghost_forget(app);
    }
    app->need_redraw = 1;
}

/* ---- the hover ghost ---------------------------------------------------- */

/*
 * Under the EDIT layout the ghost is a placement of its own, by handle: the
 * occupant of its slot is hidden by removing ITS placement (and put back as
 * a fresh one, whose handle the shadow then adopts), and the ghost is
 * removed by its handle. A runtime App_WorldLocChange would evict the
 * occupant behind EDIT's back and leave its records naming an element that
 * is gone (edit-world's phase-2 note).
 */
static struct
{
    int ghost_handle;
    int occupant_handle;
    /* The bake the handles belong to: after a rebake they name nothing, and
     * the bake already showed the document without the ghost. */
    uint32_t bake_serial;
} g_app_edit_ghost = { WORLD_BUILDER_HANDLE_NONE, WORLD_BUILDER_HANDLE_NONE, 0 };

static int
app_map_editor_ghost_is_edit(struct App const* app)
{
    return app->world_builder
           && WorldBuilder_GetLayout(app->world_builder) == WORLD_BUILDER_LAYOUT_EDIT;
}

/** A shadow loc that named `old_handle` now names `new_handle`. */
static void
app_edit_shadow_rebind(
    int old_handle,
    int new_handle)
{
    if( old_handle == WORLD_BUILDER_HANDLE_NONE )
        return;
    for( int i = 0; i < EDITOR_DOC_MAX_SQUARES; i++ )
    {
        struct AppMapEditShadow* sh = &g_app_edit_mirror.squares[i];
        if( !sh->used )
            continue;
        for( int k = 0; k < sh->loc_count; k++ )
            if( sh->handles[k] == old_handle )
            {
                sh->handles[k] = new_handle;
                return;
            }
    }
}

/** Forget the ghost WITHOUT removing it (STATIC commit). */
static void
app_map_editor_ghost_forget(struct App* app)
{
    assert(app);
    MapEditorGhost_Commit(&app->map_ghost);
}

/** Remove the ghost, put back whatever it displaced, forget. */
static void
app_map_editor_ghost_remove(struct App* app)
{
    struct MapEditorGhostSpec removed;
    struct MapEditorGhostSpec restore;
    bool has_restore = false;

    assert(app);
    if( !MapEditorGhost_Remove(&app->map_ghost, &removed, &has_restore, &restore) )
        return;
    if( app_map_editor_ghost_is_edit(app) )
    {
        struct WorldBuilder* wb = app->world_builder;
        struct World_LocPlacement live;
        int const same_bake =
            WorldEdit_Ready(wb) && WorldEdit_BakeSerial(wb) == g_app_edit_ghost.bake_serial;

        if( !same_bake )
        {
            g_app_edit_ghost.ghost_handle = WORLD_BUILDER_HANDLE_NONE;
            g_app_edit_ghost.occupant_handle = WORLD_BUILDER_HANDLE_NONE;
            return;
        }
        if( g_app_edit_ghost.ghost_handle != WORLD_BUILDER_HANDLE_NONE
            && WorldEdit_PlacementGet(wb, g_app_edit_ghost.ghost_handle, &live) )
            WorldBuilder_LocRemove(wb, g_app_edit_ghost.ghost_handle);
        if( has_restore )
        {
            struct World_LocPlacement place = { restore.scene_x, restore.scene_z, restore.level,
                                                restore.loc_id, restore.shape, restore.angle };
            int const handle = WorldBuilder_LocPlace(wb, &place);
            app_edit_shadow_rebind(g_app_edit_ghost.occupant_handle, handle);
        }
        g_app_edit_ghost.ghost_handle = WORLD_BUILDER_HANDLE_NONE;
        g_app_edit_ghost.occupant_handle = WORLD_BUILDER_HANDLE_NONE;
        (void)WorldBuilder_Flush(wb);
        app->need_redraw = 1;
        return;
    }
    App_WorldLocChange(
        app, removed.scene_x, removed.scene_z, removed.level, -1, removed.shape, removed.angle);
    /* Scene-only, like the ghost itself -- the document never knew about
     * either. */
    if( has_restore )
        App_WorldLocChange(
            app, restore.scene_x, restore.scene_z, restore.level, restore.loc_id, restore.shape,
            restore.angle);
    app->need_redraw = 1;
}

/** Put the ghost up under EDIT: hide the occupant by handle, place the ghost
 *  as its own placement. 0 when EDIT cannot place it this frame. */
static int
app_map_editor_ghost_place_edit(
    struct App* app,
    const struct MapEditorGhostSpec* wanted)
{
    struct WorldBuilder* wb = app->world_builder;
    struct AppEditCtx ctx = { app->editor, app->provider, wb, app->world, app->scene };
    struct MapEditorGhostSpec displaced;
    struct World_LocPlacement place;
    int has_displaced = 0;
    int occupant;

    if( !WorldEdit_Ready(wb) || !app_edit_loc_ready(&ctx, wanted->loc_id) )
    {
        /* Not resident yet: ask for it, show the ghost once it lands. */
        if( app->provider && !CacheProvider_LocationHas(app->provider, wanted->loc_id) )
        {
            struct ToriRS_Task* task = CreateTask_LocLoad(app->provider, wanted->loc_id);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
        }
        return 0;
    }
    occupant = WorldEdit_FindPlacement(wb, wanted->scene_x, wanted->scene_z, wanted->level, wanted->shape);
    if( occupant != WORLD_BUILDER_HANDLE_NONE )
    {
        struct World_LocPlacement held;
        if( WorldEdit_PlacementGet(wb, occupant, &held) )
        {
            displaced = *wanted;
            displaced.loc_id = held.loc_id;
            displaced.shape = held.shape;
            displaced.angle = held.angle;
            has_displaced = 1;
            WorldBuilder_LocRemove(wb, occupant);
        }
    }
    place.scene_x = wanted->scene_x;
    place.scene_z = wanted->scene_z;
    place.level = wanted->level;
    place.loc_id = wanted->loc_id;
    place.shape = wanted->shape;
    place.angle = wanted->angle;
    g_app_edit_ghost.ghost_handle = WorldBuilder_LocPlace(wb, &place);
    g_app_edit_ghost.occupant_handle = has_displaced ? occupant : WORLD_BUILDER_HANDLE_NONE;
    g_app_edit_ghost.bake_serial = WorldEdit_BakeSerial(wb);
    (void)WorldBuilder_Flush(wb);
    MapEditorGhost_Place(&app->map_ghost, wanted, has_displaced ? &displaced : NULL);
    return 1;
}

/**
 * Keep the Place / Move hover ghost current. Once per frame, with the other
 * editor drains.
 */
void
app_map_editor_ghost_update(struct App* app)
{
    struct Editor_Panel* panel;
    struct EditorUi* ui;
    int want = 0;
    int id = -1;
    int shape = 0;
    int angle = 0;
    int level = 0;

    assert(app);

    panel = &app->editor_panel;
    if( !app->editor || !panel->ui )
        return;
    ui = panel->ui;

    /* Place previews the CATALOG pick; Move previews the first selected loc
     * at the tile it would land on (not on its own tile: a translucent double
     * of itself reads as flicker). */
    {
        int const hover_ok = app->frame_view->world_hover_tile_x >= 0
                             && !app->frame_view->minimenu->visible && !app->input_frame_consumed;

        if( panel->visible && hover_ok && ui->tools.tool == EDITOR_TOOL_LOC_PLACE
            && ui->tools.pick_kind == CACHEPROVIDER_CATALOG_LOC && ui->tools.pick_id >= 0 )
        {
            want = 1;
            id = ui->tools.pick_id;
            Editor_PanelGhostSpec(panel, app, &shape, &angle);
            level = Editor_PanelEditLevel(panel, app);
        }
        else if( panel->visible && hover_ok && ui->tools.tool == EDITOR_TOOL_LOC_MOVE
                 && panel->sel_kind == EDITOR_SELECTION_LOC
                 && !(app->frame_view->world_hover_tile_x == panel->sel_scene_x
                      && app->frame_view->world_hover_tile_z == panel->sel_scene_z) )
        {
            want = 1;
            id = panel->sel_loc_id;
            shape = panel->sel_shape;
            angle = panel->sel_angle;
            level = panel->sel_level;
        }
    }

    {
        struct MapEditorGhostSpec const wanted = { .loc_id = id,
                                                   .scene_x = app->frame_view->world_hover_tile_x,
                                                   .scene_z = app->frame_view->world_hover_tile_z,
                                                   .level = level,
                                                   .shape = shape,
                                                   .angle = angle };

        if( !want || !MapEditorGhost_Matches(&app->map_ghost, &wanted) )
            app_map_editor_ghost_remove(app);

        if( want && !app->map_ghost.active )
        {
            if( app_map_editor_ghost_is_edit(app) )
            {
                if( app_map_editor_ghost_place_edit(app, &wanted) )
                    app->need_redraw = 1;
            }
            else
            {
                struct MapEditorGhostSpec displaced;
                bool has_displaced = false;

                /* Whoever holds this slot is about to be replaced; remember
                 * them, read BEFORE the add is queued. */
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
                App_WorldLocChange(app, wanted.scene_x, wanted.scene_z, level, id, shape, angle);
                MapEditorGhost_Place(&app->map_ghost, &wanted, has_displaced ? &displaced : NULL);
                app->need_redraw = 1;
            }
        }
    }

    /* Translucency, once a placement has produced an element. Written onto
     * the ELEMENT's own model (ForWrite: placements of one loc share a model,
     * and fading it in place would ghost every other copy on screen). */
    if( MapEditorGhost_NeedsFade(&app->map_ghost) && app->world && app->scene )
    {
        int const idx = World_SceneryFindAt(
            app->world, app->map_ghost.spec.scene_x, app->map_ghost.spec.scene_z,
            app->map_ghost.spec.level, app->map_ghost.spec.shape);
        if( idx >= 0 )
        {
            struct WorldEntity_Scenery* scenery = World_EntityPoolGet(&app->world->entities.scenery, idx);
            struct ToriDraw_Model* model =
                scenery ? ToriDraw_SceneElementModelForWrite(app->scene, scenery->element_id) : NULL;

            if( model && model->face_count > 0 )
            {
                /* RS face alpha: 0 opaque, higher more transparent. */
                if( !model->face_alphas )
                {
                    /* lib:toridraw: the model's array; ToriDraw_ModelFree frees it. */
                    model->face_alphas = TORIDRAW_MALLOC((size_t)model->face_count);
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
    int cat_kind;
    int cat_view;
    /* Not the pick: while a multiloc VARIANT row is chosen this is that
     * rung's loc, so the well shows the variant the catalog is reading out. */
    int preview_id;

    assert(app);

    if( !app->editor || !panel->visible || !panel->ui )
        return;
    /* The catalog module owns the well and the pick (panels/edpanel_catalog.c). */
    cat_kind = panel->ui->tools.pick_kind;
    cat_view = Editor_Catalog_ModelView(panel->ui);
    if( cat_view < 0 )
        return;
    preview_id = Editor_PanelCatalogPreviewId(panel);
    if( preview_id < 0 )
    {
        ToriRSChrome_ModelViewSet(&app->dbg_ui, cat_view, 0);
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
    if( cat_kind == last_kind && preview_id == last_id )
        return;

    if( cat_kind == CACHEPROVIDER_CATALOG_OBJ )
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
            ToriRSChrome_ModelViewSet(&app->dbg_ui, cat_view, 0);
            last_kind = cat_kind;
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
            struct ToriDraw_Sprite** sprites =
                TORIDRAW_MALLOC(sizeof(*sprites)); /* SceneSpriteAdd frees it */
            assert(sprites);
            sprites[0] = sprite;
            if( ToriDraw_SceneSpriteHas(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID) )
                ToriDraw_SceneSpriteRemove(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID);
            ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID, sprites, 1);
            ToriRSChrome_ModelViewSet(
                &app->dbg_ui, cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID);
            last_kind = cat_kind;
            last_id = preview_id;
        }
        return;
    }

    if( cat_kind == CACHEPROVIDER_CATALOG_LOC )
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
            ToriRSChrome_ModelViewSet(&app->dbg_ui, cat_view, 0);
            last_kind = cat_kind;
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
            struct ToriDraw_Sprite** sprites =
                TORIDRAW_MALLOC(sizeof(*sprites)); /* SceneSpriteAdd frees it */
            assert(sprites);
            sprites[0] = sprite;
            /* One well, re-rendered per pick: the old sprite goes with the
             * old registration. The scene owns what it holds. */
            if( ToriDraw_SceneSpriteHas(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID) )
                ToriDraw_SceneSpriteRemove(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID);
            ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID, sprites, 1);
            ToriRSChrome_ModelViewSet(
                &app->dbg_ui, cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID);
            last_kind = cat_kind;
            last_id = preview_id;
        }
        return;
    }

    if( cat_kind == CACHEPROVIDER_CATALOG_NPC )
    {
        struct ToriRS_Npctype* npc = CacheProvider_NpctypeGet(app->provider, preview_id);
        int missing = 0;

        if( !npc || npc->models_count <= 0 )
        {
            ToriRSChrome_ModelViewSet(&app->dbg_ui, cat_view, 0);
            last_kind = cat_kind;
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
                ToriRSChrome_ModelViewSet(&app->dbg_ui, cat_view, 0);
                last_kind = cat_kind;
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
            struct ToriDraw_Sprite** sprites =
                TORIDRAW_MALLOC(sizeof(*sprites)); /* SceneSpriteAdd frees it */
            assert(sprites);
            sprites[0] = sprite;
            if( ToriDraw_SceneSpriteHas(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID) )
                ToriDraw_SceneSpriteRemove(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID);
            ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_EDITOR_PREVIEW_ID, sprites, 1);
            ToriRSChrome_ModelViewSet(
                &app->dbg_ui, cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID);
            last_kind = cat_kind;
            last_id = preview_id;
        }
        return;
    }

    ToriRSChrome_ModelViewSet(&app->dbg_ui, cat_view, 0);
    last_kind = cat_kind;
    last_id = preview_id;
}

