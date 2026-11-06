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
#include "editor/builder/builder_shell.h"
#include "editor/editor_derive.h"
#include "editor/accounts/accounts_panel.h"
#include "editor/editor_view.h"
#include "app/app_camera_clamp.h"
#include "editor/editor_world_gesture.h"
#include "input/torirs_gesture.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanel_ops.h"
#include "editor/panels/edpanel_ops_stamps.h"
#include "editor/loc_editor_selection.h"
#include "editor/panels/edpanels.h"
#include "editor/task_editor_layout_io.h"
#include "editor/task_editor_prefs_io.h"
#include "engine/world_builder/edit/world_edit.h"
#include "game/rs_worldmap_overview.h"
#include "graphics/pixel_format.h"
#include "toridraw.h"
#include "toridraw_alloc.h"
#include "toridraw_math.h"
#include "toridraw_raster_kernel.h"
#include "toridraw_sprite.h"

#include <ctype.h>
#include <time.h>

#if defined(__EMSCRIPTEN__)
#include "ui/torirs_chrome_exec_web.h"
#endif

/* Private to this unit, declared up front so definition order is free. */
static void
app_map_editor_ghost_forget(struct App* app);
static void
app_map_editor_ghost_remove(struct App* app);
static int
app_map_editor_ghost_is_edit(struct App const* app);
static void
app_map_editor_spawn_ghost_remove(struct App* app);
/* The move gizmo's press and drag (workbench 6.3, the gizmo section). */
static struct AppMapEditorGizmo
{
    int active;
    struct LocEditorGizmoDrag drag;
    /** The handle under the pointer (R5: hover tells), and the one the
     *  status bar last named; the quarters it last said for a ring drag. */
    int hot;
    int said;
    int said_quarters;
} g_app_gizmo;
static int
app_map_editor_gizmo_press(
    struct App* app,
    struct LibToriRS_Input* input);
static void
app_map_editor_gizmo_drag(
    struct App* app,
    struct LibToriRS_Input* input);
static int
app_map_editor_hover_env_tile(
    int* out_world_x,
    int* out_world_z);
static struct ToriDraw_Sprite*
app_preview_raster(
    struct App* app,
    struct ToriDraw_ModelHandle hnd,
    int width,
    int height);

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
    /* The var preview is the world's too: every shell keyed on a listed var
     * resolves with its value (the panels still hear it below). */
    if( key == TORIRSMAPED_STATE_VAR_PREVIEW )
        (void)app_map_editor_var_preview(&app->varps, values, count);
    Editor_PanelApplySharedState(&app->editor_panel, app, key, values, count);
}

/*
 * STATE_VAR_PREVIEW (the shell editor's layout, builder_shell.h) into the
 * varps the world resolves multilocs and multinpcs with, App-free so
 * edit_world_test drives it: VarPManager_SetPreview takes the listed vars,
 * and its change callback (app_varp_change in the client) re-resolves every
 * placed loc and npc that depends on one, so scrubbing the shell editor's
 * slider changes the placed multiloc's child. A malformed field clears the
 * preview rather than leave a stale one. Returns how many vars are
 * previewed now.
 */
int
app_map_editor_var_preview(
    struct VarPManager* varps,
    const int32_t* values,
    int count)
{
    struct Builder_PreviewVar vars[BUILDER_PREVIEW_MAX];
    struct VarPManagerPreview preview[VARP_MANAGER_PREVIEW_MAX];
    int n;

    assert(varps);
    assert(values || count == 0);
    n = Builder_PreviewDecode(values, count, vars, BUILDER_PREVIEW_MAX);
    if( n > VARP_MANAGER_PREVIEW_MAX )
        n = VARP_MANAGER_PREVIEW_MAX;
    for( int i = 0; i < n; i++ )
    {
        preview[i].is_varbit = vars[i].kind == BUILDER_PREVIEW_VARBIT;
        preview[i].id = vars[i].id;
        preview[i].value = vars[i].value;
    }
    (void)VarPManager_SetPreview(varps, preview, n);
    return n;
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

/* The editor's shutdown, in the order it needs: the dock layout and hotkeys
 * files are written while the panel still exists, the panel (and every
 * registered module) is freed, then the EDIT shadows. App_Shutdown calls this
 * before it closes the session. */
void
app_map_editor_shutdown(struct App* app)
{
    assert(app);
    assert(app->editor);
    app_map_editor_layout_flush(app);
    /* The var preview is the editor's: the world resolves with the
     * player's vars again once it closes. */
    (void)VarPManager_SetPreview(&app->varps, NULL, 0);
    /* The embedded server's loopback listener, before the session it serves
     * goes (app/app_editor_view.c). */
    EditorView_AppShutdown(app);
    Editor_PanelFree(&app->editor_panel);
    app_map_editor_free(app);
}

/* ---- the App-bound panels' services (editor_ui.h struct EditorUiAppServices)
 *
 * The catalog, inspector and areas modules name no App: what they need of it
 * is these, with the App as user_data. */

static struct ToriRS_Location*
ui_service_loc_record(
    void* user_data,
    int loc_id)
{
    struct App* app = user_data;
    assert(app);
    if( !app->provider )
        return NULL;
    return CacheProvider_LocationGet(app->provider, loc_id);
}

static void
ui_service_loc_request(
    void* user_data,
    int loc_id)
{
    struct App* app = user_data;
    struct ToriRS_Task* task;

    assert(app);
    if( !app->provider )
        return;
    task = CreateTask_LocLoad(app->provider, loc_id);
    if( task )
        ToriRS_TaskQueue_Add(app->runner.queue, task);
}

static int
ui_service_hover_tile(
    void* user_data,
    int* out_world_x,
    int* out_world_z,
    int* out_level)
{
    struct App* app = user_data;
    int scene_x;
    int scene_z;

    assert(app);
    assert(out_world_x);
    assert(out_world_z);
    assert(out_level);
    /* A photograph's parked hover (TORIRS_EDITOR_HOVER) is the pointer. */
    if( app_map_editor_hover_env_tile(out_world_x, out_world_z) )
    {
        *out_level = Editor_PanelEditLevel(&app->editor_panel, app);
        return 1;
    }
    if( !app->world || !Editor_PanelHoverTile(&app->editor_panel, app, &scene_x, &scene_z) )
        return 0;
    *out_world_x = scene_x + app->world->_base_tile_x;
    *out_world_z = scene_z + app->world->_base_tile_z;
    *out_level = Editor_PanelEditLevel(&app->editor_panel, app);
    return 1;
}

static int
ui_service_scene_window(
    void* user_data,
    int* out_base_x,
    int* out_base_z,
    int* out_size)
{
    struct App* app = user_data;

    assert(app);
    assert(out_base_x);
    assert(out_base_z);
    assert(out_size);
    if( !app->world )
        return 0;
    *out_base_x = app->world->_base_tile_x;
    *out_base_z = app->world->_base_tile_z;
    *out_size = app->world->_scene_size;
    return 1;
}

static void
ui_service_select_terrain(
    void* user_data,
    int world_x,
    int world_z,
    int level)
{
    struct App* app = user_data;

    assert(app);
    if( !app->world )
        return;
    Editor_PanelSelectTerrain(
        &app->editor_panel, app, world_x - app->world->_base_tile_x, world_z - app->world->_base_tile_z, level);
}

/* The areas panel's Go to: over the tile, the current yaw, the pitch clamped
 * (383 when the camera has none yet), the eye 2600 units out. */
static int
ui_service_camera_to_tile(
    void* user_data,
    int tile_x,
    int tile_z,
    int level)
{
    struct App* app = user_data;
    struct App_WorldView* view;
    struct EditorViewCamera pose;
    int scene_x;
    int scene_z;
    int extent;

    assert(app);
    if( !app->world || !app->frame_view )
        return 0;
    view = app->frame_view;
    scene_x = (tile_x - app->world->_base_tile_x) * 128 + 64;
    scene_z = (tile_z - app->world->_base_tile_z) * 128 + 64;
    extent = app->world->_scene_size * 128;
    if( scene_x < 0 || scene_z < 0 || scene_x >= extent || scene_z >= extent )
        return 0;
    memset(&pose, 0, sizeof(pose));
    pose.x = tile_x * 128 + 64;
    pose.z = tile_z * 128 + 64;
    pose.level = level;
    pose.yaw = view->world_camera.yaw & 0x7ff;
    pose.pitch = app_world_clamp_pitch(app, view->world_camera.pitch > 0 ? view->world_camera.pitch : 383);
    pose.zoom = 2600;
    EditorView_PoseEye(&pose, app->world->_base_tile_x * 128, app->world->_base_tile_z * 128,
        app_world_height(app, scene_x, scene_z, level), &view->world_camera_pos);
    view->world_camera.pitch = pose.pitch;
    view->camera_unlocked = 1;
    app->need_redraw = 1;
    return 1;
}

static void
ui_service_preview_invalidate(void* user_data)
{
    struct App* app = user_data;
    assert(app);
    EditorPreviewCamera_Invalidate(&app->preview_camera, false);
}

static void
ui_service_preview_orbit(
    void* user_data,
    int pitch_step,
    int yaw_step)
{
    struct App* app = user_data;
    assert(app);
    EditorPreviewCamera_Orbit(&app->preview_camera, pitch_step, yaw_step);
    app->need_redraw = 1;
}

static void
ui_service_preview_zoom(
    void* user_data,
    int ratio_q16)
{
    struct App* app = user_data;
    assert(app);
    EditorPreviewCamera_ZoomBy(&app->preview_camera, ratio_q16);
    app->need_redraw = 1;
}

static int
ui_service_world_busy(void* user_data)
{
    struct App* app = user_data;
    assert(app);
    return app->world_load_inflight || app->editor_panel.sq_open_pending || app->editor_panel.sq_close_pending;
}

static void
ui_service_request_redraw(void* user_data)
{
    struct App* app = user_data;
    assert(app);
    app->need_redraw = 1;
}

/* ---- the square picker's world map (editor/editor_worldmap.h) ----------
 *
 * The overview's bakes live for the process: one App per process, and the
 * App holds no field for them (check-app-boundary). Created on first use.
 * On the web too since workbench 6.4: every picture also goes to the DOM
 * executor (ToriRSChromeExecWeb_GraphImage), which draws the well's IMAGE
 * items from it. */

_Static_assert(
    EDITOR_UI_MAP_IMAGE_SLOTS == UITREE_SCENE_EDITOR_PICKER_MAP_SLOTS,
    "the picker's map slots are the scene's reserved picker range");

static struct RS_WorldMapOverview* g_picker_worldmap;
static int g_picker_worldmap_load_asked;

static struct ToriRS_WorldMapArea*
ui_service_worldmap_area_of(
    struct App* app,
    int area_id)
{
    struct ToriRS_WorldMapAreas* areas;

    if( !app->provider || area_id < 0 || area_id == EDITOR_UI_MAP_AREA_ALL )
        return NULL;
    areas = CacheProvider_WorldMapGet(app->provider);
    if( !areas )
        return NULL;
    return ToriRS_WorldMapAreasGet(areas, area_id);
}

static int
ui_service_worldmap_area(
    void* user_data,
    const char* internal_name)
{
    struct App* app = user_data;
    struct ToriRS_WorldMapAreas* areas;

    assert(app);
    assert(internal_name);
    if( !app->provider )
        return -1;
    areas = CacheProvider_WorldMapGet(app->provider);
    if( !areas )
    {
        /* Asked for once; a cache with no world map never publishes any,
         * and the picker keeps asking every half second, harmlessly. */
        if( !g_picker_worldmap_load_asked )
        {
            struct ToriRS_Task* task = CreateTask_WorldMapLoad(app->provider);
            g_picker_worldmap_load_asked = 1;
            if( !task )
                return -1;
            ToriRS_TaskQueue_Add(app->runner.queue, task);
        }
        return -2;
    }
    if( strcmp(internal_name, "*") == 0 )
        return EDITOR_UI_MAP_AREA_ALL;
    for( int i = 0; i < areas->count; i++ )
    {
        /* The atlas's names are the content tree's .wma blocks, written
         * from these; compared without case all the same. */
        const char* a = areas->areas[i].internal_name;
        const char* b = internal_name;
        if( !a )
            continue;
        while( *a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b) )
        {
            a++;
            b++;
        }
        if( *a == '\0' && *b == '\0' )
            return areas->areas[i].id;
    }
    return -1;
}

static int
ui_service_worldmap_poll(
    void* user_data,
    int area_id,
    int square_x0,
    int square_z0,
    int squares_w,
    int squares_h,
    double px_per_square,
    int* out_missing)
{
    struct App* app = user_data;
    struct ToriRS_WorldMapArea* area;
    struct RS_WorldMapOverviewWindow window;
    struct timespec poll_t0;

    assert(app);
    assert(out_missing);
    area = ui_service_worldmap_area_of(app, area_id);
    if( !area && !(area_id == EDITOR_UI_MAP_AREA_ALL && app->provider && CacheProvider_WorldMapGet(app->provider)) )
        return -1;
    if( !g_picker_worldmap )
        g_picker_worldmap = RS_WorldMapOverview_New();
    window.square_x0 = square_x0;
    window.square_z0 = square_z0;
    window.squares_w = squares_w;
    window.squares_h = squares_h;
    clock_gettime(CLOCK_MONOTONIC, &poll_t0);
    if( area_id == EDITOR_UI_MAP_AREA_ALL )
        *out_missing = RS_WorldMapOverview_PollAll(g_picker_worldmap, app->provider, app->scene, app->runner.queue,
            UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPSCENE),
            CacheProvider_WorldMapGet(app->provider), &window, px_per_square);
    else
        *out_missing = RS_WorldMapOverview_Poll(g_picker_worldmap, app->provider, app->scene, app->runner.queue,
            UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPSCENE), area, &window,
            px_per_square);
    /* TORIRS_WORLDMAP_STATS=1: how long a window takes to arrive, from its
     * first poll to the one that finds nothing missing (stderr in every
     * build: it is asked for). */
    if( getenv("TORIRS_WORLDMAP_STATS") )
    {
        static struct timespec started;
        static int timing;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if( !timing && *out_missing > 0 )
        {
            started = now;
            timing = 1;
        }
        if( timing )
            TORIRS_ERR("worldmap picker: area %d squares %d,%d %dx%d: %d regions to come, %ld ms (poll %ld us)\n",
                area_id, square_x0, square_z0, squares_w, squares_h, *out_missing,
                (long)((now.tv_sec - started.tv_sec) * 1000 + (now.tv_nsec - started.tv_nsec) / 1000000),
                (long)((now.tv_sec - poll_t0.tv_sec) * 1000000 + (now.tv_nsec - poll_t0.tv_nsec) / 1000));
        if( timing && *out_missing == 0 )
            timing = 0;
    }
    return RS_WorldMapOverview_Serial(g_picker_worldmap);
}

static int
ui_service_worldmap_render_tile(
    void* user_data,
    int area_id,
    double px_per_square,
    int pixel_x0,
    int pixel_y0,
    int size,
    uint32_t* out_argb)
{
    struct App* app = user_data;
    struct ToriRS_WorldMapArea* area;
    struct RS_WorldMapOverviewTile tile;

    assert(app);
    assert(out_argb);
    assert(size > 0);
    area = ui_service_worldmap_area_of(app, area_id);
    if( !area && !(area_id == EDITOR_UI_MAP_AREA_ALL && app->provider && CacheProvider_WorldMapGet(app->provider)) )
        return 0;
    if( !g_picker_worldmap )
        g_picker_worldmap = RS_WorldMapOverview_New();
    tile.pixels_per_square = px_per_square;
    tile.pixel_x0 = pixel_x0;
    tile.pixel_y0 = pixel_y0;
    tile.width = size;
    tile.height = size;
    if( area_id == EDITOR_UI_MAP_AREA_ALL )
        RS_WorldMapOverview_RenderTileAll(g_picker_worldmap, CacheProvider_WorldMapGet(app->provider), &tile, out_argb);
    else
        RS_WorldMapOverview_RenderTile(g_picker_worldmap, area, &tile, out_argb);
    return 1;
}

static int
ui_service_worldmap_icons(
    void* user_data,
    int area_id,
    int square_x0,
    int square_z0,
    int squares_w,
    int squares_h,
    struct EditorUiMapIcon* out,
    int capacity,
    int* out_pending)
{
    struct App* app = user_data;
    struct ToriRS_WorldMapArea* area;
    struct RS_WorldMapOverviewWindow window;
    struct RS_WorldMapOverviewIcon found[1536];
    int found_count;
    int count = 0;

    assert(app);
    assert(out);
    assert(out_pending);
    *out_pending = 0;
    area = ui_service_worldmap_area_of(app, area_id);
    if( !g_picker_worldmap )
        return 0;
    if( !area && !(area_id == EDITOR_UI_MAP_AREA_ALL && app->provider && CacheProvider_WorldMapGet(app->provider)) )
        return 0;
    window.square_x0 = square_x0;
    window.square_z0 = square_z0;
    window.squares_w = squares_w;
    window.squares_h = squares_h;
    found_count = area_id == EDITOR_UI_MAP_AREA_ALL
                      ? RS_WorldMapOverview_IconsAll(g_picker_worldmap, CacheProvider_WorldMapGet(app->provider),
                            &window, found, (int)(sizeof(found) / sizeof(found[0])))
                      : RS_WorldMapOverview_Icons(
                            g_picker_worldmap, area, &window, found, (int)(sizeof(found) / sizeof(found[0])));
    for( int i = 0; i < found_count && count < capacity; i++ )
    {
        struct ToriRS_MapElement* element = NULL;
        /* The element's config and sprite, asked for when cold (the same
         * binding the 595 map's icons go through). */
        int const scene_id = app_mapfunction_scene_id(app, found[i].element_id, &element);
        struct EditorUiMapIcon* icon = &out[count];

        if( !element )
        {
            (*out_pending)++;
            continue;
        }
        memset(icon, 0, sizeof(*icon));
        icon->tile_x = found[i].tile_x;
        icon->tile_z = found[i].tile_z;
        icon->text_size = element->text_size;
        if( scene_id > 0 )
        {
            int frame_count = 0;
            struct ToriDraw_Sprite** frames = ToriDraw_SceneSpriteGet(app->scene, scene_id, &frame_count);
            if( !frames || frame_count <= 0 || !frames[0] )
            {
                (*out_pending)++;
                continue;
            }
            icon->scene_id = scene_id;
            icon->w = frames[0]->crop_width > 0 ? frames[0]->crop_width : frames[0]->width;
            icon->h = frames[0]->crop_height > 0 ? frames[0]->crop_height : frames[0]->height;
        }
        else if( element->sprite_id >= 0 )
        {
            /* An icon whose sprite is still loading. */
            (*out_pending)++;
            continue;
        }
        else if( element->name && element->name[0] )
            snprintf(icon->label, sizeof(icon->label), "%s", element->name);
        else
            continue;
        count++;
    }
    return count;
}

static int
ui_service_image_publish(
    void* user_data,
    int slot,
    const uint32_t* argb,
    int w,
    int h)
{
    struct App* app = user_data;
    int const id = UITREE_SCENE_EDITOR_PICKER_MAP_BASE + slot;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** sprites;
    uint32_t* copy;

    assert(app);
    assert(argb);
    assert(w > 0);
    assert(h > 0);
    assert(slot >= 0);
    assert(slot < UITREE_SCENE_EDITOR_PICKER_MAP_SLOTS);
    if( !app->scene )
        return 0;
    copy = TORIDRAW_MALLOC((size_t)w * (size_t)h * sizeof(*copy)); /* the sprite owns it */
    assert(copy);
    memcpy(copy, argb, (size_t)w * (size_t)h * sizeof(*copy));
    sprite = ToriDraw_SpriteNewFromArgbOwned(copy, w, h);
    assert(sprite);
    sprites = TORIDRAW_MALLOC(sizeof(*sprites)); /* SceneSpriteAdd frees it */
    assert(sprites);
    sprites[0] = sprite;
    if( ToriDraw_SceneSpriteHas(app->scene, id) )
        ToriDraw_SceneSpriteRemove(app->scene, id);
    ToriDraw_SceneSpriteAdd(app->scene, id, sprites, 1);
#if defined(__EMSCRIPTEN__)
    {
        /* The DOM well has no scene: the picture goes to it by id. A page
         * without the hook keeps the in-canvas sprite only. */
        static uint32_t versions[UITREE_SCENE_EDITOR_PICKER_MAP_SLOTS];
        versions[slot]++;
        (void)ToriRSChromeExecWeb_GraphImage(id, versions[slot], w, h, argb);
    }
#endif
    app->need_redraw = 1;
    return id;
}

#if !defined(__EMSCRIPTEN__)
/** The quest board's screenshot: the PNG decoded (engine/png_decode.h) and
 *  box-filtered down to fit max_w x max_h, as scene sprite
 *  UITREE_SCENE_EDITOR_SHOT_BASE + slot. One decode a click: the board asks
 *  only when a ledger row is picked. */
static int
ui_service_png_publish(
    void* user_data,
    int slot,
    const char* path,
    int max_w,
    int max_h,
    int* out_w,
    int* out_h)
{
    struct App* app = user_data;
    int const id = UITREE_SCENE_EDITOR_SHOT_BASE + slot;
    FILE* f;
    long size;
    uint8_t* data;
    uint32_t* pixels = NULL;
    uint32_t* fitted;
    int w = 0;
    int h = 0;
    int fw;
    int fh;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** sprites;

    assert(app);
    assert(path);
    assert(slot >= 0);
    assert(slot < UITREE_SCENE_EDITOR_SHOT_SLOTS);
    assert(max_w > 0);
    assert(max_h > 0);
    assert(out_w);
    assert(out_h);
    if( !app->scene )
        return 0;
    f = fopen(path, "rb");
    if( !f )
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if( size <= 0 || size > 64L * 1024 * 1024 )
    {
        fclose(f);
        return 0;
    }
    data = ToriMem_Alloc(ToriMem_Editor(), (size_t)size);
    assert(data);
    if( fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        fclose(f);
        ToriMem_Free(ToriMem_Editor(), data);
        return 0;
    }
    fclose(f);
    if( !PngDecode_Argb(ToriMem_Editor(), data, (int)size, &w, &h, &pixels) )
    {
        ToriMem_Free(ToriMem_Editor(), data);
        return 0;
    }
    ToriMem_Free(ToriMem_Editor(), data);
    /* Fit, never enlarge: a source pixel box per target pixel, averaged. */
    fw = w;
    fh = h;
    if( fw > max_w || fh > max_h )
    {
        if( (long)w * max_h > (long)h * max_w )
        {
            fw = max_w;
            fh = (int)((long)h * max_w / w);
        }
        else
        {
            fh = max_h;
            fw = (int)((long)w * max_h / h);
        }
        if( fw < 1 )
            fw = 1;
        if( fh < 1 )
            fh = 1;
    }
    fitted = TORIDRAW_MALLOC((size_t)fw * (size_t)fh * sizeof(*fitted)); /* the sprite owns it */
    assert(fitted);
    for( int y = 0; y < fh; y++ )
    {
        int const y0 = (int)((long)y * h / fh);
        int const y1 = (int)((long)(y + 1) * h / fh) > y0 ? (int)((long)(y + 1) * h / fh) : y0 + 1;
        for( int x = 0; x < fw; x++ )
        {
            int const x0 = (int)((long)x * w / fw);
            int const x1 = (int)((long)(x + 1) * w / fw) > x0 ? (int)((long)(x + 1) * w / fw) : x0 + 1;
            uint32_t sum[4] = { 0, 0, 0, 0 };
            uint32_t n = 0;
            for( int sy = y0; sy < y1; sy++ )
                for( int sx = x0; sx < x1; sx++ )
                {
                    uint32_t const p = pixels[sy * w + sx];
                    sum[0] += p >> 24;
                    sum[1] += (p >> 16) & 0xFF;
                    sum[2] += (p >> 8) & 0xFF;
                    sum[3] += p & 0xFF;
                    n++;
                }
            fitted[y * fw + x] = ((sum[0] / n) << 24) | ((sum[1] / n) << 16) | ((sum[2] / n) << 8) | (sum[3] / n);
        }
    }
    ToriMem_Free(ToriMem_Editor(), pixels);
    sprite = ToriDraw_SpriteNewFromArgbOwned(fitted, fw, fh);
    assert(sprite);
    sprites = TORIDRAW_MALLOC(sizeof(*sprites)); /* SceneSpriteAdd frees it */
    assert(sprites);
    sprites[0] = sprite;
    if( ToriDraw_SceneSpriteHas(app->scene, id) )
        ToriDraw_SceneSpriteRemove(app->scene, id);
    ToriDraw_SceneSpriteAdd(app->scene, id, sprites, 1);
    app->need_redraw = 1;
    *out_w = fw;
    *out_h = fh;
    return id;
}
#endif

/** A monotonic clock for the picker's background loads. */
static uint64_t
ui_service_now_us(void* user_data)
{
    struct timespec now;
    (void)user_data;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

/** The last frame's work (App_LastFrameUs), what the picker measures its
 *  cold open against. */
static uint64_t
ui_service_last_frame_us(void* user_data)
{
    struct App* app = user_data;
    assert(app);
    return App_LastFrameUs(app);
}

#if !defined(__EMSCRIPTEN__)
static int
ui_service_loc_thumbnail(
    void* user_data,
    int loc_id,
    int shape);
#endif

void
app_map_editor_ui_services(
    struct App* app,
    struct EditorUiAppServices* out)
{
    assert(app);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->user_data = app;
    out->loc_record = ui_service_loc_record;
    out->loc_request = ui_service_loc_request;
    out->hover_tile = ui_service_hover_tile;
    out->scene_window = ui_service_scene_window;
    out->select_terrain = ui_service_select_terrain;
    out->camera_to_tile = ui_service_camera_to_tile;
    out->preview_invalidate = ui_service_preview_invalidate;
    out->preview_orbit = ui_service_preview_orbit;
    out->preview_zoom = ui_service_preview_zoom;
    out->world_busy = ui_service_world_busy;
    out->request_redraw = ui_service_request_redraw;
    /* The square picker's world map, native and web: the web's DOM well
     * draws the pictures image_publish hands the page (graph.image,
     * src/plugin_chrome/runtime-source.js). */
    out->worldmap_area = ui_service_worldmap_area;
    out->worldmap_poll = ui_service_worldmap_poll;
    out->worldmap_render_tile = ui_service_worldmap_render_tile;
    out->image_publish = ui_service_image_publish;
    out->worldmap_icons = ui_service_worldmap_icons;
    out->now_us = ui_service_now_us;
    out->last_frame_us = ui_service_last_frame_us;
#if !defined(__EMSCRIPTEN__)
    /* The Catalog's row thumbnails: scene sprites, which a DOM page has no
     * pixels for (its cells list draws the empty cell). */
    out->loc_thumbnail = ui_service_loc_thumbnail;
    /* The quest board's screenshots: decoded from disk, which a page has no
     * path to (its board lists the paths). */
    out->png_publish = ui_service_png_publish;
#endif
    /* Pop out (app/app_editor_view.c). */
    out->world_camera = EditorView_AppWorldCamera;
    /* A scene's Enter (editor/panels/edpanel_scenes.c). */
    out->world_camera_set = EditorView_AppWorldCameraSet;
    out->spawn_panel = EditorView_AppSpawnPanel;
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
 * Close one square of the window's world (the picker's open-set strip,
 * Shift+click, past the save guard): the world reloads with every other
 * square it shows now. The document keeps the square -- its unsaved edits stay
 * in the session until saved or undone -- it is only no longer in this
 * window's world. Refused, with the reason in the status bar, when it is the
 * only square shown (a window always shows one) or not shown at all.
 */
static void
app_map_editor_close_square(
    struct App* app,
    struct Editor_Panel* panel)
{
    struct Editor_Doc* doc = &app->editor->doc;
    int chunks[APP_MAP_EDITOR_OPEN_MAX * 2];
    int count = 0;
    int shown = 0;

    if( !app->world_builder )
    {
        if( panel->ui )
            Editor_UiStatus(panel->ui, "Close refused: this window has no world yet");
        return;
    }
    for( int i = 0; i < doc->square_count && count < APP_MAP_EDITOR_OPEN_MAX; i++ )
    {
        struct Editor_Square const* square = &doc->squares[i];
        if( !square->loaded || !WorldEdit_SquareInWindow(app->world_builder, square->map_x, square->map_z) )
            continue;
        if( square->map_x == panel->sq_close_x && square->map_z == panel->sq_close_z )
        {
            shown = 1;
            continue;
        }
        chunks[count * 2] = square->map_x;
        chunks[count * 2 + 1] = square->map_z;
        count++;
    }
    if( !shown )
    {
        if( panel->ui )
            Editor_UiStatus(panel->ui, "Close refused: m%d_%d is not in this window's world", panel->sq_close_x,
                panel->sq_close_z);
        return;
    }
    if( count == 0 )
    {
        if( panel->ui )
            Editor_UiStatus(panel->ui, "Close refused: m%d_%d is the only square this window shows; open another first",
                panel->sq_close_x, panel->sq_close_z);
        return;
    }
    TORIRS_LOG("editor: closing m%d_%d (%d square%s left)\n", panel->sq_close_x, panel->sq_close_z, count,
        count == 1 ? "" : "s");
    if( panel->ui )
        Editor_UiStatus(panel->ui, "Closed m%d_%d: %d square%s left in this window", panel->sq_close_x,
            panel->sq_close_z, count, count == 1 ? "" : "s");
    app->world_load_attempted = 0;
    app_world_load_begin(app, chunks, count);
}

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
    if( !app->editor )
        return;
    /* The camera move an Open asked for, once the square's load has landed.
     * Cleared whether or not the tile turned out to be in the scene, so a
     * square the load could not bring does not keep a stale request. */
    if( panel->sq_goto_pending && !app->world_load_inflight && app->world )
    {
        panel->sq_goto_pending = 0;
        if( ui_service_camera_to_tile(app, panel->sq_goto_x, panel->sq_goto_z, 0) )
            app->need_redraw = 1;
        else if( panel->ui )
            Editor_UiLog(panel->ui, "editor: m%d_%d is not in the scene after its open", panel->sq_goto_x / 64,
                panel->sq_goto_z / 64);
    }
    /* A close waits for the load in flight and for an open asked before it. */
    if( panel->sq_close_pending && !panel->sq_open_pending && !app->world_load_inflight )
    {
        panel->sq_close_pending = 0;
        app_map_editor_close_square(app, panel);
        return;
    }
    if( !panel->sq_open_pending )
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
    /* Open brings the camera; Add to open set keeps the view where it is. */
    if( !panel->sq_open_add )
    {
        panel->sq_goto_pending = 1;
        panel->sq_goto_x = panel->sq_open_x * 64 + 32;
        panel->sq_goto_z = panel->sq_open_z * 64 + 32;
    }
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

/** A bucket for a loc by value (app_edit_loc_equal's fields); `buckets` is a
 *  power of two. */
static int
app_edit_loc_bucket(
    const struct Editor_Loc* loc,
    int buckets)
{
    uint32_t h = 2166136261u;
    int const fields[6] = { loc->loc_id, loc->shape, loc->rotation, loc->level, loc->x, loc->z };

    for( int k = 0; k < 6; k++ )
    {
        h ^= (uint32_t)fields[k];
        h *= 16777619u;
    }
    return (int)(h & (uint32_t)(buckets - 1));
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

    /* Locs: each document loc takes the handle of an equal shadow loc, so
     * only what really went is removed and only what really came is placed.
     * The document inserts in cache order (an undone delete lands back in the
     * middle of the list), so the lists are matched by value, not walked in
     * step -- a walk would read every loc after an insertion as gone and come
     * back, re-placing the rest of the square and renumbering its elements.
     * Removals go first: a replace frees its tile+shape before the new loc
     * lands there. */
    handles = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * (size_t)(square->loc_count + 1));
    assert(handles);
    {
        int const buckets = 1024;
        int* head = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * (size_t)buckets);
        int* next = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * (size_t)(sh->loc_count + 1));
        unsigned char* taken = ToriMem_Calloc(ToriMem_Editor(), (size_t)(sh->loc_count + 1), 1);
        unsigned char* matched = ToriMem_Calloc(ToriMem_Editor(), (size_t)(square->loc_count + 1), 1);

        assert(head);
        assert(next);
        assert(taken);
        assert(matched);
        for( int b = 0; b < buckets; b++ )
            head[b] = -1;
        /* Chained newest-first; walked below in the shadow's order. */
        for( int i = sh->loc_count - 1; i >= 0; i-- )
        {
            int const b = app_edit_loc_bucket(&sh->locs[i], buckets);
            next[i] = head[b];
            head[b] = i;
        }
        for( int j = 0; j < square->loc_count; j++ )
        {
            handles[j] = WORLD_BUILDER_HANDLE_NONE;
            for( int i = head[app_edit_loc_bucket(&square->locs[j], buckets)]; i >= 0; i = next[i] )
                if( !taken[i] && app_edit_loc_equal(&sh->locs[i], &square->locs[j]) )
                {
                    taken[i] = 1;
                    matched[j] = 1;
                    handles[j] = sh->handles[i];
                    break;
                }
        }
        for( int i = 0; i < sh->loc_count; i++ )
            if( !taken[i] && in_window && !rebake && sh->handles[i] != WORLD_BUILDER_HANDLE_NONE )
                WorldBuilder_LocRemove(wb, sh->handles[i]);
        /* Last first, each ahead of the loc after it in the list (matched,
         * or placed a step ago): EDIT files a placement in map order, and the
         * list's order is the order a fresh bake would place them in. */
        for( int j = square->loc_count - 1; j >= 0; j-- )
        {
            const struct Editor_Loc* loc = &square->locs[j];
            struct World_LocPlacement placement;
            int before = WORLD_BUILDER_HANDLE_NONE;

            if( matched[j] || !in_window || rebake )
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
            for( int k = j + 1; k < square->loc_count && before == WORLD_BUILDER_HANDLE_NONE; k++ )
                before = handles[k];
            handles[j] = WorldEdit_LocPlaceBefore(wb, &placement, before);
        }
        ToriMem_Free(ToriMem_Editor(), matched);
        ToriMem_Free(ToriMem_Editor(), taken);
        ToriMem_Free(ToriMem_Editor(), next);
        ToriMem_Free(ToriMem_Editor(), head);
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

/* ---- the World panel and the saved layout --------------------------------- */

/**
 * The world's screen rect follows the editor's World panel.
 *
 * Called by app_update_world_viewport (app_viewport.c) right after it latched
 * the UI tree's WORLD emit into `world_emit_desc`, and before the projection
 * scale is derived from it. Every consumer of the world rect reads that one
 * desc -- the 3D paint and the renderers' viewport, app_world_mouse_gate and
 * the hover pick, the overlays (app_overlay.c: brush footprint, editor ghosts,
 * selection marks project through app_world_project.c on the same desc), the
 * minimenu -- so rewriting it here is what makes all of them follow the
 * panel. While the editor's panels show and the World panel is hidden, the
 * world is not drawn and not picked (world_view_valid = 0).
 *
 * Untouched while the panels are hidden (a viewer window, panel=tab, the
 * editor closed) and before the first layout: the world keeps the game's own
 * rect, the whole frame.
 */
/** The editor owns the world rect now: its panels show, and laid out. */
static struct EditorUi const*
app_map_editor_world_owner(struct App const* app)
{
    if( !app->editor || !app->editor_panel.built || !app->editor_panel.visible || !app->editor_panel.ui )
        return NULL;
    if( app->editor_panel.ui->layout_surface_w <= 0 )
        return NULL;
    return app->editor_panel.ui;
}

/** The World body cut to the widget box (surface pixels); 0 when nothing of
 *  it is left (the panel hidden, or dragged off the box). */
static int
app_map_editor_world_cut(
    struct App const* app,
    struct EditorUi const* ui,
    int box_x,
    int box_y,
    int box_w,
    int box_h,
    struct EditorUiRect* out)
{
    struct EditorUiRect const r = Editor_UiWorldRect(ui);
    int const x0 = r.x > box_x ? r.x : box_x;
    int const y0 = r.y > box_y ? r.y : box_y;
    int const x1 = r.x + r.w < box_x + box_w ? r.x + r.w : box_x + box_w;
    int const y1 = r.y + r.h < box_y + box_h ? r.y + r.h : box_y + box_h;

    (void)app;
    if( r.w <= 0 || r.h <= 0 || x1 <= x0 || y1 <= y0 )
        return 0;
    out->x = x0;
    out->y = y0;
    out->w = x1 - x0;
    out->h = y1 - y0;
    return 1;
}

/**
 * The world widget's letterbox while the editor owns the world
 * (UITREE_HOST_GET_WORLD_LETTERBOX, answered from app_host_request.c): the
 * reference's own mechanism for drawing the world into PART of its widget
 * (VIEWPORT_CLAMPFOV, class159.method5357) -- the emit desc becomes the cut
 * rectangle, the rest of the widget's box is painted black by the frame
 * (torirs_frame.c frame_world_letterbox_fill), and every reader of the world
 * rect sees the cut. The cut here is the World panel's body. Hidden World:
 * a 1x1 cut just past the box, so the frame paints only the band (black) and
 * the 3D pass's clip is empty; App_MapEditorWorldView also drops the world
 * (no paint, no pick). The box's origin is last frame's (the request carries
 * only its size); the editor's world widget is the whole canvas.
 *
 * Returns 0 when the editor does not own the world (the reference's answer
 * stands).
 */
int
App_MapEditorWorldLetterbox(
    struct App const* app,
    int box_w,
    int box_h,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    struct EditorUi const* ui;
    struct EditorUiRect cut;
    int box_x;
    int box_y;

    assert(app);
    assert(out_x);
    assert(out_y);
    assert(out_w);
    assert(out_h);
    ui = app_map_editor_world_owner(app);
    if( !ui )
        return 0;
    box_x = app->world_emit_desc.world_box_w > 0 ? app->world_emit_desc.world_box_x : 0;
    box_y = app->world_emit_desc.world_box_h > 0 ? app->world_emit_desc.world_box_y : 0;
    if( !app_map_editor_world_cut(app, ui, box_x, box_y, box_w, box_h, &cut) )
    {
        *out_x = box_w;
        *out_y = 0;
        *out_w = 1;
        *out_h = 1;
        return 1;
    }
    *out_x = cut.x - box_x;
    *out_y = cut.y - box_y;
    *out_w = cut.w;
    *out_h = cut.h;
    return 1;
}

/** What the letterbox above depends on, for the emit retain gate's camera
 *  signature (app_ui_host.c): a moved or resized World re-walks the emit. */
int
App_MapEditorWorldSignature(struct App const* app)
{
    struct EditorUi const* ui;
    struct EditorUiRect r;

    assert(app);
    ui = app_map_editor_world_owner(app);
    if( !ui )
        return 0;
    r = Editor_UiWorldRect(ui);
    return 1 + (int)(((uint32_t)r.x * 2654435761u) ^ ((uint32_t)r.y * 40503u) ^ ((uint32_t)r.w << 16)
                     ^ (uint32_t)r.h);
}

void
App_MapEditorWorldView(struct App* app)
{
    struct EditorUi const* ui;
    struct UITreeEmitDesc* desc;
    struct EditorUiRect cut;

    assert(app);
    ui = app_map_editor_world_owner(app);
    if( !ui || !app->world_view_valid )
        return;
    desc = &app->world_emit_desc;
    if( !app_map_editor_world_cut(app, ui, desc->world_box_w > 0 ? desc->world_box_x : desc->x,
            desc->world_box_h > 0 ? desc->world_box_y : desc->y, desc->world_box_w > 0 ? desc->world_box_w : desc->w,
            desc->world_box_h > 0 ? desc->world_box_h : desc->h, &cut) )
    {
        app->world_view_valid = 0;
        return;
    }
    /* The emit walk later this frame cuts the same rectangle (the letterbox
     * above); this desc is last frame's walk, so restate it now and the
     * picks, the overlays and the paint agree with what is drawn. */
    desc->x = cut.x;
    desc->y = cut.y;
    desc->w = cut.w;
    desc->h = cut.h;
}

/* Frames the layout must hold still before it is written: one drag is one
 * write, and a burst of View toggles is one too. */
#define APP_EDITOR_LAYOUT_SETTLE_FRAMES 6

/* The saved layout's state for this process (one editor window per process).
 * File-static, never freed: a load task in flight at shutdown still has
 * somewhere to land. */
static struct
{
    /* 0 not started, 1 reading, 2 loaded (saving on change), 3 off. */
    int state;
    const char* path;
    struct EditorLayoutLoad load;
    /* What was last queued for writing, and the last frame's digest. */
    int saved_valid;
    uint32_t saved;
    uint32_t last;
    int still_frames;
} g_app_editor_layout;

/**
 * Once per frame: read the layout file at boot (holding the default layout
 * until it lands), then write it whenever the layout settles in a new shape
 * -- a drag or resize released, a View toggle, Reset layout, Snap to grid, a
 * surface resize -- and once after boot so the file states this surface.
 * Both trips go through the App's task queue and the platform IO queue
 * (task_editor_layout_io.h), so the desktop writes a file and the web an
 * IndexedDB record through the same code.
 */
static void
app_map_editor_layout_tick(struct App* app)
{
    struct EditorUi* ui = app->editor_panel.ui;

    if( !app->editor_panel.built || !ui || g_app_editor_layout.state == 3 )
        return;
    if( g_app_editor_layout.state == 0 )
    {
        g_app_editor_layout.path = EditorLayout_Path();
        if( !g_app_editor_layout.path || !app->runner.queue )
        {
            g_app_editor_layout.state = 3;
            return;
        }
        Editor_UiLayoutHold(ui);
        ToriRS_TaskQueue_Add(
            app->runner.queue, CreateTask_EditorLayoutLoad(&g_app_editor_layout.load, g_app_editor_layout.path));
        g_app_editor_layout.state = 1;
        return;
    }
    if( g_app_editor_layout.state == 1 )
    {
        if( !g_app_editor_layout.load.done )
            return;
        Editor_UiLayoutLoaded(ui, g_app_editor_layout.load.found ? &g_app_editor_layout.load.file : NULL);
        if( g_app_editor_layout.load.found )
            TORIRS_LOG("editor: layout from %s (saved at %dx%d, scale %d)\n", g_app_editor_layout.path,
                g_app_editor_layout.load.file.surface_w, g_app_editor_layout.load.file.surface_h,
                g_app_editor_layout.load.file.scale);
        g_app_editor_layout.state = 2;
        return;
    }
    if( Editor_UiLayoutBusy(ui) )
    {
        g_app_editor_layout.still_frames = 0;
        return;
    }
    {
        uint32_t const now = Editor_UiLayoutFingerprint(ui);
        if( now != g_app_editor_layout.last )
        {
            g_app_editor_layout.last = now;
            g_app_editor_layout.still_frames = 0;
            return;
        }
        if( ++g_app_editor_layout.still_frames < APP_EDITOR_LAYOUT_SETTLE_FRAMES )
            return;
        if( g_app_editor_layout.saved_valid && now == g_app_editor_layout.saved )
            return;
        {
            struct EditorLayoutFile file;
            Editor_UiLayoutCapture(ui, &file);
            ToriRS_TaskQueue_Add(app->runner.queue, CreateTask_EditorLayoutSave(&file, g_app_editor_layout.path));
        }
        g_app_editor_layout.saved = now;
        g_app_editor_layout.saved_valid = 1;
    }
}

/**
 * At shutdown: write the layout one last time. The tick above waits for the
 * layout to hold still for APP_EDITOR_LAYOUT_SETTLE_FRAMES before it writes,
 * so a drag released just before quitting would otherwise be lost; and a save
 * the tick queued in the last frame may still sit in the task queue, which is
 * about to be freed, not stepped. So this does not trust the digest: it runs
 * a save by hand on its own IO batch, the way app_prefs_flush does. One small
 * file per quit is the cost. A drag still in progress is not written -- the
 * panel is not where the user left it yet.
 */
void
app_map_editor_layout_flush(struct App* app)
{
    struct EditorUi* ui;
    struct EditorLayoutFile file;
    struct ToriRS_IOBatch* io;
    struct ToriRS_Task* task;
    int guard = 0;

    assert(app);
    /* The hotkeys file rides the same shutdown (src/app.c calls this). */
    app_editor_hotkeys_flush(app);
    ui = app->editor_panel.ui;
    if( g_app_editor_layout.state != 2 || !ui )
        return;
    if( Editor_UiLayoutBusy(ui) )
        return;
    Editor_UiLayoutCapture(ui, &file);
    io = ToriRS_IOBatch_New();
    task = CreateTask_EditorLayoutSave(&file, g_app_editor_layout.path);
    io->task = task;
    while( task_run(task, io) == PT_YIELDED && guard++ < 8 )
        Platform_IO_Process(app->runner.px, io);
    task_free(task);
    ToriRS_IOBatch_Free(io);
    g_app_editor_layout.saved = Editor_UiLayoutFingerprint(ui);
    g_app_editor_layout.saved_valid = 1;
}

/*
 * editor_prefs.ini (editor/editor_prefs.h): View > Chrome scale and View >
 * Text size, this device's and not the Client's. Read once at boot -- a
 * window popped out of this one reads the same file at ITS boot, which is how
 * it inherits the leader's values -- and written whenever the user changes
 * either (EditorUi::prefs_serial moved), at once: a change is one click, never
 * a stream, so there is nothing to settle. Same IO queue as the layout file.
 */
static struct
{
    /* 0 not started, 1 reading, 2 loaded (saving on change), 3 off. */
    int state;
    const char* path;
    struct EditorPrefsLoad load;
    uint32_t saved_serial;
} g_app_editor_prefs;

static void
app_map_editor_prefs_tick(struct App* app)
{
    struct EditorUi* ui = app->editor_panel.ui;

    if( !app->editor_panel.built || !ui || g_app_editor_prefs.state == 3 )
        return;
    if( g_app_editor_prefs.state == 0 )
    {
        g_app_editor_prefs.path = Editor_PrefsPath();
        if( !g_app_editor_prefs.path || !app->runner.queue )
        {
            g_app_editor_prefs.state = 3;
            return;
        }
        ToriRS_TaskQueue_Add(
            app->runner.queue, CreateTask_EditorPrefsLoad(&g_app_editor_prefs.load, g_app_editor_prefs.path));
        g_app_editor_prefs.state = 1;
        return;
    }
    if( g_app_editor_prefs.state == 1 )
    {
        if( !g_app_editor_prefs.load.done )
            return;
        /* A choice the user made while the read was in flight stands. */
        if( ui->prefs_serial == 0 )
            Editor_UiApplyPrefs(ui, &g_app_editor_prefs.load.prefs);
        if( g_app_editor_prefs.load.found )
        {
            char scale[16];
            if( ui->prefs.chrome_scale_q == EDITOR_PREFS_SCALE_AUTO )
                snprintf(scale, sizeof(scale), "Auto");
            else
                snprintf(scale, sizeof(scale), "%d %%", ui->prefs.chrome_scale_q * 100 / EDITOR_UI_SCALEQ_ONE);
            TORIRS_LOG("editor: view preferences from %s (chrome scale %s, text %s)%s\n", g_app_editor_prefs.path,
                scale, ui->prefs.text_large ? "Large" : "Normal",
                g_app_editor_prefs.load.skipped ? "; malformed lines skipped" : "");
        }
        g_app_editor_prefs.state = 2;
        g_app_editor_prefs.saved_serial = 0;
        return;
    }
    if( ui->prefs_serial == g_app_editor_prefs.saved_serial )
        return;
    ToriRS_TaskQueue_Add(app->runner.queue, CreateTask_EditorPrefsSave(&ui->prefs, g_app_editor_prefs.path));
    g_app_editor_prefs.saved_serial = ui->prefs_serial;
}

void
app_map_editor_drain(struct App* app)
{
    int squares[EDITOR_REBUILD_QUEUE_MAX * 2];
    int count;

    assert(app);

    if( !app->editor )
        return;
    app_map_editor_layout_tick(app);
    app_map_editor_prefs_tick(app);
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

/** The world tile under the cursor, absolute: on the pinned plane when a
 *  level is pinned (Editor_PanelHoverTile). */
static int
app_map_editor_hover_world(
    struct App* app,
    int* out_x,
    int* out_z)
{
    int scene_x;
    int scene_z;

    if( !Editor_PanelHoverTile(&app->editor_panel, app, &scene_x, &scene_z) )
        return 0;
    *out_x = app->world->_base_tile_x + scene_x;
    *out_z = app->world->_base_tile_z + scene_z;
    return 1;
}

/* ---- the active level: the pinned plane under the pointer, keys, camera --- */

/** How far (tiles, Chebyshev) from the pick's tile the pinned plane is
 *  searched. Level 3 sits 720 units over the ground; at the flattest free-
 *  camera pitch that is ~14 tiles of slant. */
#define APP_PIN_HOVER_REACH 16

/** (px, py) inside or on triangle a b c (either winding); 0 for a
 *  degenerate one, which would otherwise contain every point. */
static int
app_pin_in_tri(
    int px,
    int py,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy)
{
    long long const area = (long long)(bx - ax) * (cy - ay) - (long long)(by - ay) * (cx - ax);
    long long const d1 = (long long)(bx - ax) * (py - ay) - (long long)(by - ay) * (px - ax);
    long long const d2 = (long long)(cx - bx) * (py - by) - (long long)(cy - by) * (px - bx);
    long long const d3 = (long long)(ax - cx) * (py - cy) - (long long)(ay - cy) * (px - cx);

    if( area == 0 )
        return 0;
    if( area > 0 )
        return d1 >= 0 && d2 >= 0 && d3 >= 0;
    return d1 <= 0 && d2 <= 0 && d3 <= 0;
}

/** Does scene tile (tx, tz) of `level`'s height field cover screen point
 *  (mx, my)? Its four corners projected at that level's heights, as the
 *  terrain is drawn. */
static int
app_pin_tile_covers(
    struct App* app,
    int tx,
    int tz,
    int level,
    int mx,
    int my)
{
    static const int k_corner[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int sx[4];
    int sy[4];

    for( int k = 0; k < 4; k++ )
    {
        int const fx = (tx + k_corner[k][0]) << 7;
        int const fz = (tz + k_corner[k][1]) << 7;
        if( !app_world_project_at(app, fx, fz, app_world_height(app, fx, fz, level), &sx[k], &sy[k]) )
            return 0;
    }
    return app_pin_in_tri(mx, my, sx[0], sy[0], sx[1], sy[1], sx[2], sy[2])
           || app_pin_in_tri(mx, my, sx[0], sy[0], sx[2], sy[2], sx[3], sy[3]);
}

/**
 * With a level pinned, the tile of THAT plane under the pointer, into
 * panel->pin_hover_*. The pick reports the first surface drawn: the pinned
 * plane where it has floor, but over an empty upper floor (a flat roof to
 * be, a new storey) the ground beneath, which at a slant is tiles away from
 * where the pinned plane passes under the pointer. A pick on the pinned
 * level is taken as it is; otherwise the tiles around it are tested ring by
 * ring against the pinned level's height field (what an empty level
 * derives to: the plane below, raised). Once per frame, before anything
 * reads the hover.
 */
static void
app_map_editor_pin_hover(
    struct App* app,
    struct Editor_Panel* panel)
{
    int const level = panel->ui ? panel->ui->tools.edit_level : -1;
    int const cx = app->frame_view->world_hover_tile_x;
    int const cz = app->frame_view->world_hover_tile_z;
    int const mx = app->frame_view->world_mouse_x;
    int const my = app->frame_view->world_mouse_y;
    int size;

    panel->pin_hover_valid = 0;
    panel->pin_hover_level = level;
    if( level < 0 || !app->world || !app->world_view_valid || cx < 0 || cz < 0 )
        return;
    if( app->frame_view->world_hover_tile_level == level )
    {
        panel->pin_hover_valid = 1;
        panel->pin_hover_x = cx;
        panel->pin_hover_z = cz;
        return;
    }
    size = app->world->_scene_size;
    for( int r = 0; r <= APP_PIN_HOVER_REACH; r++ )
        for( int dz = -r; dz <= r; dz++ )
            for( int dx = -r; dx <= r; dx++ )
            {
                int const tx = cx + dx;
                int const tz = cz + dz;
                if( (dx != -r && dx != r && dz != -r && dz != r) || tx < 1 || tz < 1 || tx >= size - 1
                    || tz >= size - 1 )
                    continue;
                if( !app_pin_tile_covers(app, tx, tz, level, mx, my) )
                    continue;
                panel->pin_hover_valid = 1;
                panel->pin_hover_x = tx;
                panel->pin_hover_z = tz;
                return;
            }
}


/*
 * The camera keeps the pinned plane where it was on screen: pinning level 2
 * from level 0 lifts the eye by the height between the two planes under it,
 * so the floor the tools now edit sits where the ground did and the tile
 * picked is the one under the pointer. Auto leaves the eye alone (and
 * remembers the plane it was framed for); a pinned level owns the height
 * until the next pin. The free camera's eye is absolute
 * (frame_view->world_camera_pos), so this is a shift of its y.
 */
static struct
{
    int framed;
} g_app_level_camera;

static void
app_map_editor_level_camera(
    struct App* app,
    struct EditorUi* ui)
{
    int const level = ui->tools.edit_level;
    int extent;
    int ex;
    int ez;

    if( level < 0 || level == g_app_level_camera.framed )
        return;
    if( app->world && app->frame_view )
    {
        extent = app->world->_scene_size * 128;
        ex = app->frame_view->world_camera_pos.x;
        ez = app->frame_view->world_camera_pos.z;
        ex = ex < 128 ? 128 : ex > extent - 129 ? extent - 129 : ex;
        ez = ez < 128 ? 128 : ez > extent - 129 ? extent - 129 : ez;
        app->frame_view->world_camera_pos.y +=
            app_world_height(app, ex, ez, level) - app_world_height(app, ex, ez, g_app_level_camera.framed);
        app->need_redraw = 1;
    }
    g_app_level_camera.framed = level;
}

/** How a Select click or marquee joins the selection: the modifiers held
 *  (Shift adds, Ctrl toggles, Alt subtracts; none replaces). */
static enum Editor_LocSelectMode
app_map_editor_select_mode(
    struct App* app,
    struct LibToriRS_Input* input)
{
    return Editor_LocSelectModeOf(app->editor_panel.ui->shift_held, LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL),
        LibToriRS_Input_IsKeyHeld(input, TORIRSK_ALT));
}

/** A Place (a click, or a run's release) landed where the ghost stood.
 *  STATIC: the real add replaced the ghost's element, so the ghost is
 *  FORGOTTEN, not removed (removing would delete the placed loc). EDIT: the
 *  placed loc arrives through the flush as its own placement, so the ghost
 *  is removed and whatever it hid comes back. */
static void
app_map_editor_place_landed(struct App* app)
{
    if( app_map_editor_ghost_is_edit(app) )
        app_map_editor_ghost_remove(app);
    else
        app_map_editor_ghost_forget(app);
}

/** A second click on the same loc within this is the double-click. */
#define APP_EDITOR_DOUBLE_CLICK_MS 400

/** An open drag -- a brush stroke, a rectangle, a marquee, a Place run --
 *  runs to the button's release wherever the pointer wanders, and closes
 *  there. Returns 1 when the release ends a press that never became a drag
 *  (a Place press or a press on empty ground that stayed on its tile): the
 *  caller then handles it as the click it is. */
static int
app_map_editor_drag(
    struct App* app,
    struct LibToriRS_Input* input)
{
    struct Editor_Panel* panel = &app->editor_panel;
    struct EditorUi* ui = panel->ui;
    int const released =
        input->curr.mouse_button_up[TORIRSM_LEFT] || !LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
    int click = 0;
    int world_x;
    int world_z;

    if( panel->run.active )
    {
        int hx;
        int hz;
        /* Each new tile re-plans the run (the overlay draws the plan); a
         * frame on the same tile costs nothing. */
        /* Shift keeps the run to one axis; with the diagonal wall picked it
         * keeps it to a diagonal instead (a diagonal fence, corner to corner). */
        int const constrain = app_editor_hotkeys_active(app, EDITOR_HK_PLACE_CONSTRAIN)
                                  ? (ui->tools.place_shape == 9 ? 2 : 1)
                                  : 0;
        if( Editor_PanelHoverTile(panel, app, &hx, &hz) && Editor_PanelRunTo(panel, app, hx, hz, constrain) )
            app->need_redraw = 1;
        if( released )
        {
            if( Editor_PanelRunEnd(panel, app) < 0 )
                click = 1;
            else
                app_map_editor_place_landed(app);
            app->need_redraw = 1;
        }
    }
    if( panel->path.active )
    {
        int hx;
        int hz;
        if( Editor_PanelHoverTile(panel, app, &hx, &hz) && Editor_PanelPathTo(panel, app, hx, hz) )
            app->need_redraw = 1;
        if( released )
        {
            (void)Editor_PanelPathEnd(panel, app);
            app->need_redraw = 1;
        }
    }
    if( panel->marquee_pending )
    {
        /* Off its first tile, the press on empty ground is a marquee: the
         * Rect tool's rectangle, dragged from there. */
        if( app_map_editor_hover_world(app, &world_x, &world_z)
            && (world_x != panel->marquee_x || world_z != panel->marquee_z) )
        {
            ui->tools.rect.valid = 1;
            ui->tools.rect.x0 = panel->marquee_x;
            ui->tools.rect.z0 = panel->marquee_z;
            ui->tools.rect.x1 = world_x;
            ui->tools.rect.z1 = world_z;
            ui->tools.rect.level = panel->marquee_level;
            panel->marquee_pending = 0;
            panel->marquee_active = 1;
            panel->rect_dragging = 1;
        }
        else if( released )
        {
            panel->marquee_pending = 0;
            click = 1;
        }
    }

    if( panel->stroke_active )
    {
        int hx = -1;
        int hz = -1;
        if( !Editor_PanelHoverTile(panel, app, &hx, &hz) )
            hx = -1;
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
            if( panel->marquee_active )
            {
                /* The marquee selects what it touches; the rectangle stays
                 * for copy, fill and clear. */
                panel->marquee_active = 0;
                (void)Editor_PanelMarqueeSelect(panel, app, app_map_editor_select_mode(app, input));
            }
            else if( Editor_TileRectNormalise(&ui->tools.rect, &r) )
                Editor_UiLog(ui, "editor: rectangle %dx%d at %d,%d level %d", r.x1 - r.x0 + 1,
                    r.z1 - r.z0 + 1, r.x0, r.z0, r.level);
            else
                Editor_UiLog(ui, "editor: that rectangle is larger than 64x64 tiles");
        }
        app->need_redraw = 1;
    }
    return click && input->curr.mouse_button_up[TORIRSM_LEFT];
}

/* ---- gesture frames (workbench 6.1, design section 14) -------------------- */

int
App_EditorOwnsTouch(struct App const* app)
{
    assert(app);
    return app->editor && app->editor_panel.visible;
}

/** The World's camera is the free one, which the editor's gestures move:
 *  offline, in a cutscene, or unlocked online -- app_camera.c's own test for
 *  the free camera's keys. Online and locked it is the follow camera, whose
 *  anchor is the player's. */
static int
app_map_editor_free_camera(struct App const* app)
{
    return !app->net || app->cam_script.scripted || (app->frame_view && app->frame_view->camera_unlocked);
}

int
App_EditorOwnsGestureFrames(struct App const* app)
{
    assert(app);
    return App_EditorOwnsTouch(app) && app_map_editor_free_camera(app);
}

/** The ground's height under canvas (x, y) for the World's camera: the
 *  pinned level's (level 0 while Auto), sampled where the pointer's ray
 *  meets the ground under the eye -- one refinement, which on the editor's
 *  hills is within a few units of the true hit. */
static int
app_map_editor_gesture_ground(
    struct App* app,
    int x,
    int y)
{
    struct App_WorldView* view = app->frame_view;
    int const extent = app->world->_scene_size * 128;
    int const level = app->editor_panel.ui && app->editor_panel.ui->tools.edit_level >= 0
                          ? app->editor_panel.ui->tools.edit_level
                          : 0;
    int ground;
    double hit_x;
    double hit_z;

    assert(extent > 0);
    ground = app_world_height(app, app_camera_clamp_axis(view->world_camera_pos.x, 0, extent - 1),
        app_camera_clamp_axis(view->world_camera_pos.z, 0, extent - 1), level);
    if( ToriRS_WorldUnprojectPlane(&view->world_camera, &view->world_camera_pos, app->world_emit_desc.x,
            app->world_emit_desc.y, app->world_emit_desc.w, app->world_emit_desc.h, x, y, ground, &hit_x, &hit_z) )
    {
        int const fx = app_camera_clamp_axis((int)hit_x, 0, extent - 1);
        int const fz = app_camera_clamp_axis((int)hit_z, 0, extent - 1);
        ground = app_world_height(app, fx, fz, level);
    }
    return ground;
}

/** The World's share of a frame the chrome did not take: the free camera,
 *  over the scene, with the editor's panels up. */
static void
app_map_editor_world_gesture(
    struct App* app,
    const struct LibToriRS_Gesture* frame)
{
    struct EditorWorldGestureView view;

    if( !app->editor_panel.visible || !app->world || !app->world_view_valid || !app->frame_view )
        return;
    if( app->world->_scene_size <= 0 || app->world_load_inflight )
        return;
    /* The orbit follow camera's anchor is the player's (online, locked, out
     * of a cutscene): the World's gestures move the free camera only. With
     * the follow camera up the touch layer is not making these frames
     * anyway (App_EditorOwnsGestureFrames): its wheel notches and arrow
     * keys turn and zoom that camera, as they do in the game. */
    if( !app_map_editor_free_camera(app) )
        return;
    if( app->frame_view->minimenu->visible )
        return;
    if( !app_world_mouse_gate(app, frame->x, frame->y) )
        return;
    view.viewport_x = app->world_emit_desc.x;
    view.viewport_y = app->world_emit_desc.y;
    view.viewport_w = app->world_emit_desc.w;
    view.viewport_h = app->world_emit_desc.h;
    if( view.viewport_w <= 0 || view.viewport_h <= 0 )
        return;
    view.ground_y = app_map_editor_gesture_ground(app, frame->x, frame->y);
    if( EditorWorldGesture_Apply(&app->frame_view->world_camera, &app->frame_view->world_camera_pos, &view, frame) )
        app->need_redraw = 1;
}

void
app_map_editor_gestures(
    struct App* app,
    struct LibToriRS_Input* input)
{
    const struct LibToriRS_Gesture* frames = NULL;
    struct EditorUi* ui;
    int count;

    assert(app);
    assert(input);
    if( !app->editor || !app->editor_panel.ui )
        return;
    ui = app->editor_panel.ui;
    /* The touch flag: the hint strip spells gestures while it is up. */
    Editor_UiSetTouchInput(ui, ToriRS_InputLastWasTouch(input));
    count = ToriRS_InputGestures(input, &frames);
    if( count <= 0 )
        return;
    assert(frames);
    /* Chrome first, then the World: a pinch over the picker's well zooms
     * the picker and never the camera behind it. */
    for( int i = 0; i < count; i++ )
    {
        /* A trackpad scroll, a ctrl+wheel, a browser pinch: the same motion
         * ALSO reached this frame as the wheel / TORIRS_CMD_INPUT_SCROLL /
         * TORIRS_CMD_INPUT_PINCH, which the chrome (ToriRSChrome_WheelFrame,
         * in app_loc_editor_tick, before this) and the camera's wheel
         * already took. Taking the frame too would scroll a list twice and
         * pan the World while the wheel zooms it, so these stay the legacy
         * route's; the touchscreen's frames (and a touchpad's twist) are not
         * flagged and come here. */
        if( frames[i].flags & LIBTORIRS_GESTURE_FLAG_ALSO_LEGACY )
            continue;
        if( Editor_UiGesture(ui, &frames[i]) )
        {
            app->input_frame_consumed = 1;
            app->need_redraw = 1;
            continue;
        }
        app_map_editor_world_gesture(app, &frames[i]);
    }
}

/**
 * A press or click in the world applies the armed tool.
 *
 * Brushes paint from the PRESS and keep painting while the button is held, a
 * tile once per visit, the whole drag one undo step (the stroke opens on
 * press and closes on release). The rectangle tool drags its rectangle the
 * same way. The click tools (select, place, move, rotate, delete) act on the
 * RELEASE, as before -- except that a Place press with a loc pick opens a
 * RUN (a wall run, or the pose along the tiles crossed; previewed while
 * held, placed on release as one undo step), and a Select press on empty
 * ground becomes the MARQUEE once the pointer leaves its tile (workbench 6.3,
 * design #placing). A press that stays on its tile is still the click.
 *
 * Gated on `input_frame_consumed` so a click that landed on a panel does not
 * also edit the tile behind it, and on the minimenu owning the gesture: both
 * `minimenu.visible` and `interact.swallow_left_click` -- the menu selects on
 * mousedown and hides itself, so the release a frame later must still be
 * recognised as the menu's (see the old note on "Select Object" latching
 * terrain).
 *
 * The tile is the one under the cursor this frame; the level is the one the
 * Level row pins (Editor_PanelEditLevel), not the hover's -- for EVERY tool,
 * select included -- and with a level pinned the tile is that plane's under
 * the pointer (app_map_editor_pin_hover), not the first surface the pick met.
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
    /* ui->shift_held is set by the hotkey dispatcher (app_editor_hotkeys.c),
     * the one reader of the editor's keys. */
    /* First: every reader below (and the previews after) takes the pinned
     * plane's tile from here. */
    app_map_editor_level_camera(app, ui);
    app_map_editor_pin_hover(app, panel);

    /* A gizmo drag holds the pointer until its release (workbench 6.3). */
    if( g_app_gizmo.active )
    {
        if( !panel->visible )
        {
            g_app_gizmo.active = 0;
            MapEditorPieceGhost_Clear(&app->map_ghost.pieces);
            return;
        }
        app_map_editor_gizmo_drag(app, input);
        return;
    }

    if( panel->stroke_active || panel->rect_dragging || panel->run.active || panel->marquee_pending
        || panel->path.active )
    {
        /* The editor closing mid-drag still closes the stroke: an open
         * stroke would fold every later edit into one undo step. A run or a
         * marquee placed nothing yet: it is dropped. */
        if( !panel->visible )
        {
            if( panel->stroke_active )
            {
                ui->tools.stroke_open = 0;
                Editor_StrokeEnd(app->editor);
            }
            panel->stroke_active = 0;
            panel->rect_dragging = 0;
            panel->marquee_pending = 0;
            panel->marquee_active = 0;
            panel->path.active = 0;
            edp_run_cancel(&panel->run);
            return;
        }
        /* A press that never left its tile is the click it was: on. */
        if( !app_map_editor_drag(app, input) )
            return;
    }
    if( !panel->visible )
        return;
    if( app->frame_view->minimenu->visible || app->interact.swallow_left_click )
        return;
    if( app->input_frame_consumed )
        return;
    /* The gizmo is hit-tested before the world click: a press on one of
     * its handles is the gizmo's, never also a tile latch or a marquee. */
    if( input->curr.mouse_button_down[TORIRSM_LEFT] && !app->chrome_pointer_owned
        && app_map_editor_gizmo_press(app, input) )
        return;

    tool = ui->tools.tool;
    if( !Editor_PanelHoverTile(panel, app, &hx, &hz) )
        return;
    level = Editor_PanelEditLevel(panel, app);

    /* The press opens a drag for the brushes and the rectangle. */
    if( input->curr.mouse_button_down[TORIRSM_LEFT] )
    {
        /* The Path tool: the drag is the path, laid on release. */
        if( tool == EDITOR_TOOL_PAINT_PATH )
        {
            if( Editor_PanelPathBegin(panel, app, hx, hz, level) )
                app->need_redraw = 1;
            return;
        }
        if( Editor_ToolIsBrush(tool) )
        {
            Editor_StrokeBegin(app->editor);
            /* The History panel's row says which brush made the stroke. */
            Editor_HistoryName(app->editor, Editor_ToolName(tool));
            ui->tools.stroke_open = 1;
            ui->tools.flatten_valid = 0;
            /* Scatter with Alt held erases; every Roughen and Scatter stroke
             * draws from a fresh seed, so a second drag over the same ground
             * is not the first one again. */
            panel->scatter_erase = tool == EDITOR_TOOL_SCATTER && LibToriRS_Input_IsKeyHeld(input, TORIRSK_ALT);
            if( tool == EDITOR_TOOL_NOISE )
                ui->tools.noise_seed = ui->tools.noise_seed * 1664525u + 1013904223u;
            if( tool == EDITOR_TOOL_SCATTER && !panel->scatter_erase )
                ui->tools.scatter_seed = ui->tools.scatter_seed * 1664525u + 1013904223u;
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
        /* The held click accelerators act on the release, on one tile; so
         * does a click whose press and release land in one frame. */
        if( !input->curr.mouse_button_up[TORIRSM_LEFT] && !app_editor_hotkeys_active(app, EDITOR_HK_HOLD_PLACE)
            && !app_editor_hotkeys_active(app, EDITOR_HK_HOLD_DELETE)
            && !app_editor_hotkeys_active(app, EDITOR_HK_HOLD_CLEAR) )
        {
            /* Place with a loc pick: the press opens a run (design #placing,
             * "Drag along tiles"); a release on the same tile is the click. */
            if( tool == EDITOR_TOOL_LOC_PLACE && ui->tools.pick_kind == 0
                && Editor_PanelRunBegin(panel, app, hx, hz, level) )
                return;
            /* Select on empty ground: a marquee once the pointer moves off
             * the tile. Over a loc the press stays a click. */
            if( tool == EDITOR_TOOL_SELECT )
            {
                struct Editor_LocRef under;
                int world_x;
                int world_z;
                if( !Editor_PanelPointedLoc(panel, app, hx, hz, level, &under)
                    && app_map_editor_hover_world(app, &world_x, &world_z) )
                {
                    panel->marquee_pending = 1;
                    panel->marquee_x = world_x;
                    panel->marquee_z = world_z;
                    panel->marquee_level = level < 0 ? 0 : level > 3 ? 3 : level;
                    return;
                }
            }
        }
    }
    if( !input->curr.mouse_button_up[TORIRSM_LEFT] )
        return;

    /*
     * Click accelerators: hold a key to act on a layer without changing the
     * tool first -- the hotkey table's held actions (L places the catalog's
     * loc, K deletes the loc under the cursor, C clears every loc on the
     * tile; rebindable, docs/MAPED_HOTKEYS.md). They reach the same functions
     * the tools do. The dispatcher's gates apply: `L` typed into a search box
     * is never active.
     */
    if( app_editor_hotkeys_active(app, EDITOR_HK_HOLD_PLACE) )
    {
        Editor_PanelPlaceLocAt(panel, app, hx, hz, level);
        app->need_redraw = 1;
        return;
    }
    if( app_editor_hotkeys_active(app, EDITOR_HK_HOLD_DELETE) )
    {
        Editor_PanelDeleteLocAt(panel, app, hx, hz, level);
        app->need_redraw = 1;
        return;
    }
    if( app_editor_hotkeys_active(app, EDITOR_HK_HOLD_CLEAR) )
    {
        Editor_PanelClearLocsAt(panel, app, hx, hz, level);
        app->need_redraw = 1;
        return;
    }

    if( tool == EDITOR_TOOL_SELECT )
    {
        /* A plain click latches the TILE (unambiguous where a wall, a decor
         * and a ground loc share it; the minimenu's "Select wall" rows pick
         * those exactly). With a modifier the loc under the pointer joins the
         * selection: Shift adds, Ctrl toggles, Alt takes it out. A second
         * click on the same loc soon after is the double-click: select same,
         * every loc of its id on the edit level. */
        enum Editor_LocSelectMode const mode = app_map_editor_select_mode(app, input);
        struct Editor_LocRef under;
        int const pointed = Editor_PanelPointedLoc(panel, app, hx, hz, level, &under);
        int64_t const now = ui->chrome->now_ms;

        if( pointed && panel->click_valid && now - panel->click_ms <= APP_EDITOR_DOUBLE_CLICK_MS
            && under.map_x == panel->click_ref.map_x && under.map_z == panel->click_ref.map_z
            && under.x == panel->click_ref.x && under.z == panel->click_ref.z
            && under.level == panel->click_ref.level && under.shape == panel->click_ref.shape )
        {
            panel->click_valid = 0;
            (void)Editor_PanelSelectSameAt(panel, app, hx, hz, level, mode);
            app->need_redraw = 1;
            return;
        }
        panel->click_valid = pointed;
        panel->click_ms = now;
        if( pointed )
            panel->click_ref = under;
        if( mode == EDITOR_LOC_SELECT_REPLACE )
            Editor_PanelSelectTerrain(panel, app, hx, hz, level);
        else if( !Editor_PanelSelectPointed(panel, app, hx, hz, level, mode) )
            Editor_UiStatus(ui, "Select refused: no loc under the pointer to add, toggle or take out");
        app->need_redraw = 1;
        return;
    }
    if( Editor_ToolIsBrush(tool) || tool == EDITOR_TOOL_RECT_SELECT )
        return;

    /* Place player: the lane's play account onto the tile, through the
     * Accounts panel's server request (::tele online, the save offline; the
     * answer lands in the console). Nothing of the document changes. */
    if( tool == EDITOR_TOOL_PLACE_PLAYER )
    {
        int world_x;
        int world_z;
        if( app_map_editor_hover_world(app, &world_x, &world_z) )
            (void)Editor_Accounts_PlacePlayer(ui, world_x, world_z, level);
        app->need_redraw = 1;
        return;
    }

    /* Place with an npc / obj pick commits what the spawn ghost showed: a row
     * in the square's edited .spawn. The ghost comes down first -- it is a
     * scene entity of its own, not the spawn. */
    if( tool == EDITOR_TOOL_LOC_PLACE && (ui->tools.pick_kind == 1 || ui->tools.pick_kind == 2)
        && ui->tools.pick_id >= 0 )
    {
        int world_x;
        int world_z;
        app_map_editor_spawn_ghost_remove(app);
        if( app_map_editor_hover_world(app, &world_x, &world_z) )
            (void)Editor_Locs_PlaceSpawn(ui, world_x, world_z, level);
        app->need_redraw = 1;
        return;
    }

    Editor_PanelApplyToolAt(panel, app, hx, hz, level);

    /* A Place or Move landed where the ghost stood. */
    if( tool == EDITOR_TOOL_LOC_PLACE || tool == EDITOR_TOOL_LOC_MOVE )
        app_map_editor_place_landed(app);
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
    /* The placement after the occupant in its square's map order: the
     * occupant goes back ahead of it, where the document lists it, not at
     * the end -- the shade replay follows map order. */
    int occupant_next;
    /* The bake the handles belong to: after a rebake they name nothing, and
     * the bake already showed the document without the ghost. */
    uint32_t bake_serial;
} g_app_edit_ghost = { WORLD_BUILDER_HANDLE_NONE, WORLD_BUILDER_HANDLE_NONE, WORLD_BUILDER_HANDLE_NONE, 0 };

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
            g_app_edit_ghost.occupant_next = WORLD_BUILDER_HANDLE_NONE;
            return;
        }
        if( g_app_edit_ghost.ghost_handle != WORLD_BUILDER_HANDLE_NONE
            && WorldEdit_PlacementGet(wb, g_app_edit_ghost.ghost_handle, &live) )
            WorldBuilder_LocRemove(wb, g_app_edit_ghost.ghost_handle);
        if( has_restore )
        {
            struct World_LocPlacement place = { restore.scene_x, restore.scene_z, restore.level,
                                                restore.loc_id, restore.shape, restore.angle };
            struct World_LocPlacement next_live;
            int next = g_app_edit_ghost.occupant_next;
            int handle;
            if( next != WORLD_BUILDER_HANDLE_NONE && !WorldEdit_PlacementGet(wb, next, &next_live) )
                next = WORLD_BUILDER_HANDLE_NONE;
            handle = WorldEdit_LocPlaceBefore(wb, &place, next);
            app_edit_shadow_rebind(g_app_edit_ghost.occupant_handle, handle);
        }
        g_app_edit_ghost.ghost_handle = WORLD_BUILDER_HANDLE_NONE;
        g_app_edit_ghost.occupant_handle = WORLD_BUILDER_HANDLE_NONE;
        g_app_edit_ghost.occupant_next = WORLD_BUILDER_HANDLE_NONE;
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
    g_app_edit_ghost.occupant_next = WORLD_BUILDER_HANDLE_NONE;
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
            g_app_edit_ghost.occupant_next = WorldEdit_PlacementNext(wb, occupant);
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

/* ---- the pointer, and the spawn ghost ------------------------------------ */

/*
 * TORIRS_EDITOR_HOVER=<x>,<z>[,<tool>[,npc|obj|loc,<id>]] -- a headless
 * photograph has no pointer, so this parks the editor's hover on ABSOLUTE
 * tile (x, z) for every frame, and once, at the first frame with panels,
 * arms the named tool (Editor_ToolName, case-insensitive, spaces as '_':
 * raise, paint_underlay, place, move) and the catalog pick. Only the
 * previews (the brush footprint, the loc / spawn ghosts, the move
 * footprints) and the Inspector's hovered tile read it; clicks still need a
 * real pointer. Read once per process.
 */
static struct
{
    int resolved;
    int on;
    int abs_x;
    int abs_z;
    int armed;
    char tool[32];
    char pick_kind[8];
    int pick_id;
    int logged;
} g_app_editor_hover_env;

static void
app_map_editor_hover_env_resolve(void)
{
    const char* value;

    if( g_app_editor_hover_env.resolved )
        return;
    g_app_editor_hover_env.resolved = 1;
    g_app_editor_hover_env.pick_id = -1;
    value = getenv("TORIRS_EDITOR_HOVER");
    if( !value || !value[0] )
        return;
    if( sscanf(value, "%d,%d,%31[^,],%7[^,],%d", &g_app_editor_hover_env.abs_x, &g_app_editor_hover_env.abs_z,
            g_app_editor_hover_env.tool, g_app_editor_hover_env.pick_kind, &g_app_editor_hover_env.pick_id)
        >= 2 )
        g_app_editor_hover_env.on = 1;
}

/** TORIRS_EDITOR_HOVER's parked tile, ABSOLUTE: 1 when one is parked. */
static int
app_map_editor_hover_env_tile(
    int* out_world_x,
    int* out_world_z)
{
    assert(out_world_x);
    assert(out_world_z);
    app_map_editor_hover_env_resolve();
    if( !g_app_editor_hover_env.on )
        return 0;
    *out_world_x = g_app_editor_hover_env.abs_x;
    *out_world_z = g_app_editor_hover_env.abs_z;
    return 1;
}

/** Arm TORIRS_EDITOR_HOVER's tool and pick, once. */
static void
app_map_editor_hover_env_arm(struct EditorUi* ui)
{
    if( !g_app_editor_hover_env.on || g_app_editor_hover_env.armed )
        return;
    g_app_editor_hover_env.armed = 1;
    if( g_app_editor_hover_env.tool[0] )
    {
        for( int t = 0; t < EDITOR_TOOL_COUNT; t++ )
        {
            const char* name = Editor_ToolName((enum Editor_Tool)t);
            int i = 0;
            for( ; name[i] && g_app_editor_hover_env.tool[i]; i++ )
            {
                char a = name[i] == ' ' ? '_' : (char)(name[i] | 0x20);
                char b = g_app_editor_hover_env.tool[i] == '_' ? '_' : (char)(g_app_editor_hover_env.tool[i] | 0x20);
                if( a != b )
                    break;
            }
            if( !name[i] && !g_app_editor_hover_env.tool[i] )
            {
                Editor_UiSetTool(ui, (enum Editor_Tool)t);
                break;
            }
        }
    }
    if( g_app_editor_hover_env.pick_id >= 0 )
    {
        ui->tools.pick_kind = strcmp(g_app_editor_hover_env.pick_kind, "npc") == 0   ? 1
                              : strcmp(g_app_editor_hover_env.pick_kind, "obj") == 0 ? 2
                                                                                      : 0;
        ui->tools.pick_id = g_app_editor_hover_env.pick_id;
    }
}

/*
 * TORIRS_EDITOR_LEVEL=<level>[,<x>,<z>[,<yaw>,<pitch>,<zoom>]] -- a headless
 * photograph of a pinned level: once, at the first frame with panels, pins
 * <level> (-1 = Auto) as the Level row would; with a tile, also puts the free
 * camera on ABSOLUTE tile (x, z) of that level (defaults yaw 0, pitch 400,
 * zoom 1800 fine units) once the scene holds it. Read once per process.
 */
static struct
{
    int resolved;
    int on;
    int level;
    int has_camera;
    struct EditorViewCamera camera;
    int level_done;
    int camera_done;
} g_app_editor_level_env;

static void
app_map_editor_level_env(
    struct App* app,
    struct EditorUi* ui)
{
    if( !g_app_editor_level_env.resolved )
    {
        const char* value = getenv("TORIRS_EDITOR_LEVEL");
        int x = 0;
        int z = 0;
        int yaw = 0;
        int pitch = 400;
        int zoom = 1800;
        int n;

        g_app_editor_level_env.resolved = 1;
        if( value && value[0] )
        {
            n = sscanf(value, "%d,%d,%d,%d,%d,%d", &g_app_editor_level_env.level, &x, &z, &yaw, &pitch, &zoom);
            g_app_editor_level_env.on = n >= 1 && g_app_editor_level_env.level >= EDP_LEVEL_AUTO
                                        && g_app_editor_level_env.level < EDITOR_SQUARE_LEVELS;
            g_app_editor_level_env.has_camera = n >= 3;
            g_app_editor_level_env.camera.x = x * 128 + 64;
            g_app_editor_level_env.camera.z = z * 128 + 64;
            g_app_editor_level_env.camera.level = g_app_editor_level_env.level < 0 ? 0 : g_app_editor_level_env.level;
            g_app_editor_level_env.camera.yaw = yaw & 0x7ff;
            g_app_editor_level_env.camera.pitch = pitch;
            g_app_editor_level_env.camera.zoom = zoom;
        }
    }
    if( !g_app_editor_level_env.on )
        return;
    if( !g_app_editor_level_env.level_done )
    {
        g_app_editor_level_env.level_done = 1;
        edp_level_set(ui, g_app_editor_level_env.level);
        TORIRS_REPORT("editor: TORIRS_EDITOR_LEVEL pins level %d\n", g_app_editor_level_env.level);
    }
    if( g_app_editor_level_env.has_camera && !g_app_editor_level_env.camera_done && app->world && app->frame_view )
    {
        struct EditorViewCamera pose = g_app_editor_level_env.camera;
        int const base_x = app->world->_base_tile_x * 128;
        int const base_z = app->world->_base_tile_z * 128;
        int const sx = pose.x - base_x;
        int const sz = pose.z - base_z;
        int const extent = app->world->_scene_size * 128;

        if( sx < 128 || sz < 128 || sx >= extent - 128 || sz >= extent - 128 )
            return;
        pose.pitch = app_world_clamp_pitch(app, pose.pitch);
        EditorView_PoseEye(
            &pose, base_x, base_z, app_world_height(app, sx, sz, pose.level), &app->frame_view->world_camera_pos);
        app->frame_view->world_camera.pitch = pose.pitch;
        app->frame_view->world_camera.yaw = pose.yaw;
        app->frame_view->camera_unlocked = 1;
        /* Framed for that level: the level camera has nothing to add. */
        g_app_level_camera.framed = pose.level;
        g_app_editor_level_env.camera_done = 1;
        app->need_redraw = 1;
        TORIRS_REPORT("editor: TORIRS_EDITOR_LEVEL camera at %d,%d level %d\n", pose.x / 128, pose.z / 128,
            pose.level);
    }
}

/** This frame's pointer for the previews: the hover tile (or the env's), the
 *  level the next edit writes. Not valid over a panel or the minimenu. */
static void
app_map_editor_hover_update(
    struct App* app,
    struct Editor_Panel* panel)
{
    struct MapEditorHover* hover = &app->map_ghost.hover;

    app_map_editor_hover_env_resolve();
    memset(hover, 0, sizeof(*hover));
    if( !app->world || !panel->visible )
        return;
    if( g_app_editor_hover_env.on )
    {
        hover->scene_x = g_app_editor_hover_env.abs_x - app->world->_base_tile_x;
        hover->scene_z = g_app_editor_hover_env.abs_z - app->world->_base_tile_z;
        hover->valid = hover->scene_x >= 0 && hover->scene_z >= 0 && hover->scene_x < app->world->_scene_size
                       && hover->scene_z < app->world->_scene_size;
        if( g_app_editor_hover_env.logged != hover->valid + 1 )
        {
            /* Once per change: a shot whose preview is missing says why. */
            g_app_editor_hover_env.logged = hover->valid + 1;
            TORIRS_REPORT("editor: TORIRS_EDITOR_HOVER parks at %d,%d (scene %d,%d of %d): %s\n",
                g_app_editor_hover_env.abs_x, g_app_editor_hover_env.abs_z, hover->scene_x, hover->scene_z,
                app->world->_scene_size, hover->valid ? "in the scene" : "outside the scene");
        }
    }
    else
    {
        hover->valid = Editor_PanelHoverTile(panel, app, &hover->scene_x, &hover->scene_z)
                       && !app->frame_view->minimenu->visible && !app->input_frame_consumed;
    }
    hover->level = Editor_PanelEditLevel(panel, app);
}

/** Take the spawn ghost down: despawn exactly the entity it became. */
static void
app_map_editor_spawn_ghost_remove(struct App* app)
{
    struct MapEditorSpawnSpec spec;
    int entity;

    if( !MapEditorSpawnGhost_Remove(&app->map_ghost.spawn, app->world ? app->world->load_seq : 0, &spec, &entity) )
        return;
    if( entity < 0 || !app->world )
        return;
    if( spec.kind == MAP_EDITOR_SPAWN_NPC )
        World_NpcDespawn(app->world, entity);
    else
        App_WorldObjStackDel(app, spec.scene_x, spec.scene_z, spec.level, spec.id);
    app->need_redraw = 1;
}

/*
 * Ask once per id for what a spawn ghost needs resident: the npc's config and
 * body (through the multinpc loader the spawn task uses), the obj's config
 * and ground model. A request outlives the frame that made it, so the out
 * slot it writes is file-static.
 */
static struct
{
    int npc_requested;
    int npc_resolved;
    int obj_requested;
    int model_requested;
} g_app_spawn_ghost_loads = { -1, -1, -1, -1 };

/** Place the pending spawn ghost if its assets are resident; 1 when landed. */
static int
app_map_editor_spawn_ghost_land(struct App* app)
{
    struct MapEditorSpawnGhost* ghost = &app->map_ghost.spawn;
    struct MapEditorSpawnSpec const* spec = &ghost->spec;

    if( !app->world || !app->provider )
        return 0;
    if( spec->kind == MAP_EDITOR_SPAWN_NPC )
    {
        struct ToriRS_Npctype* npctype;
        struct WorldEntity_NPC* npc;
        int idx;

        if( g_app_spawn_ghost_loads.npc_requested != spec->id )
        {
            struct ToriRS_Task* task;
            g_app_spawn_ghost_loads.npc_requested = spec->id;
            g_app_spawn_ghost_loads.npc_resolved = -1;
            task = CreateTask_NpcMultiLoad(app, spec->id, &g_app_spawn_ghost_loads.npc_resolved);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
        }
        npctype = CacheProvider_NpctypeGet(app->provider, spec->id);
        if( !npctype || !app_world_npc_models_resident(app, npctype) )
            return 0;
        idx = app_world_spawn_npc_now(app, spec->id, -1, spec->scene_x, spec->scene_z, spec->level);
        npc = idx >= 0 ? World_EntityPoolGet(&app->world->entities.npc, idx) : NULL;
        if( !npc )
            return 0;
        /* Translucent through the npc fade the frame loop already applies
         * (rev-241 transparency): it survives the idle animation re-posing
         * the body every frame, which a one-off write would not. */
        WorldEntityTransparency_Set(&npc->transparency, app->world->cycle, app->world->cycle,
            app->world->cycle, 150, 150, 1);
        MapEditorSpawnGhost_Land(ghost, idx, app->world->load_seq, npc->size);
        ghost->faded = true;
        app->need_redraw = 1;
        return 1;
    }
    {
        struct ToriRS_Objtype* obj;
        int idx;

        if( g_app_spawn_ghost_loads.obj_requested != spec->id )
        {
            struct ToriRS_Task* task = CreateTask_ObjLoad(app->provider, spec->id);
            g_app_spawn_ghost_loads.obj_requested = spec->id;
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
        }
        obj = CacheProvider_ObjtypeGet(app->provider, spec->id);
        if( !obj )
            return 0;
        if( obj->inventory_model_id > 0 && !CacheProvider_ModelHas(app->provider, obj->inventory_model_id) )
        {
            if( g_app_spawn_ghost_loads.model_requested != obj->inventory_model_id )
            {
                struct ToriRS_Task* task = CreateTask_ModelLoad(app->provider, obj->inventory_model_id);
                g_app_spawn_ghost_loads.model_requested = obj->inventory_model_id;
                if( task )
                    ToriRS_TaskQueue_Add(app->runner.queue, task);
            }
            return 0;
        }
        idx = App_WorldObjStackAdd(app, spec->scene_x, spec->scene_z, spec->level, spec->id, 1);
        if( idx < 0 )
            return 0;
        MapEditorSpawnGhost_Land(ghost, idx, app->world->load_seq, 1);
        app->need_redraw = 1;
        return 1;
    }
}

/** The obj ghost's fade: written once onto the stack element's own model
 *  (ForWrite: the obj's models are shared). */
static void
app_map_editor_spawn_ghost_fade(struct App* app)
{
    struct MapEditorSpawnGhost* ghost = &app->map_ghost.spawn;
    struct WorldEntity_ObjStack* stack;
    struct ToriDraw_Model* model;

    if( !ghost->active || ghost->entity < 0 || ghost->faded || !app->world || !app->scene )
        return;
    stack = World_EntityPoolGet(&app->world->entities.obj_stack, ghost->entity);
    model = stack ? ToriDraw_SceneElementModelForWrite(app->scene, stack->element_id) : NULL;
    if( !model || model->face_count <= 0 )
        return;
    if( !model->face_alphas )
    {
        /* lib:toridraw: the model's array; ToriDraw_ModelFree frees it. */
        model->face_alphas = TORIDRAW_MALLOC((size_t)model->face_count);
        assert(model->face_alphas);
    }
    memset(model->face_alphas, 150, (size_t)model->face_count);
    ghost->faded = true;
    app->need_redraw = 1;
}

/**
 * The Place tool with an NPC or OBJ catalog pick: a translucent npc (its
 * idle pose, its size) or ground item on the hovered tile, scene only -- the
 * document hears nothing until the click places the row.
 */
static void
app_map_editor_spawn_ghost_update(
    struct App* app,
    struct EditorUi* ui)
{
    struct MapEditorHover const* hover = &app->map_ghost.hover;
    struct MapEditorSpawnSpec want;
    int const wanted = hover->valid && ui->tools.tool == EDITOR_TOOL_LOC_PLACE
                       && (ui->tools.pick_kind == 1 || ui->tools.pick_kind == 2) && ui->tools.pick_id >= 0;

    memset(&want, 0, sizeof(want));
    if( wanted )
    {
        want.kind = ui->tools.pick_kind == 1 ? MAP_EDITOR_SPAWN_NPC : MAP_EDITOR_SPAWN_OBJ;
        want.id = ui->tools.pick_id;
        want.scene_x = hover->scene_x;
        want.scene_z = hover->scene_z;
        want.level = hover->level;
    }
    /* A rebuild freed what the ghost became: forget it, place it again. */
    if( app->map_ghost.spawn.active && app->map_ghost.spawn.entity >= 0
        && (!app->world || app->map_ghost.spawn.load_seq != app->world->load_seq) )
        app_map_editor_spawn_ghost_remove(app);
    if( !wanted || !MapEditorSpawnGhost_Matches(&app->map_ghost.spawn, &want) )
        app_map_editor_spawn_ghost_remove(app);
    if( wanted && !app->map_ghost.spawn.active )
        MapEditorSpawnGhost_Place(&app->map_ghost.spawn, &want);
    if( MapEditorSpawnGhost_Pending(&app->map_ghost.spawn) )
        (void)app_map_editor_spawn_ghost_land(app);
    app_map_editor_spawn_ghost_fade(app);
}

/* ---- the move gizmo and the multi-piece ghost (workbench 6.3) ----------------
 *
 * The gizmo (editor/loc_editor_selection.h) stands on the primary selected
 * loc under Select or Move: its points are computed in world units there and
 * projected through this window's camera every frame, the overlay draws what
 * this projects, and a press is hit-tested against the same points BEFORE
 * the world click, so a handle never also latches the tile under it. A drag
 * proposes; the release is one action, one undo step (edp_locs_nudge for an
 * arrow or the centre -- the swap rule applies --, edp_locs_duplicate_by for
 * Alt on an arrow, edp_locs_rotate_by for the ring). While it is held the
 * multi-piece ghost shows each selected loc's destination, orange where its
 * key is held by a loc the move does not carry.
 *
 * The same ghost shows an armed stamp at the hovered tile (its LOC pieces,
 * turned as Q left it), orange where a piece would replace a loc.
 */

/* (g_app_gizmo is declared at the top of this unit: the world click reads
 * it.) */

/** A loc's PLACED footprint from the cache: centrepieces only (a wall or a
 *  decor is one tile), turned with the loc; 1x1 while it is not resident. */
static void
app_gizmo_loc_size(
    struct App* app,
    const struct Editor_LocRef* ref,
    int* out_x,
    int* out_z)
{
    struct ToriRS_Location* cfg;

    *out_x = 1;
    *out_z = 1;
    if( ref->shape != 10 && ref->shape != 11 )
        return;
    cfg = app->provider ? CacheProvider_LocationGet(app->provider, ref->loc_id) : NULL;
    if( !cfg )
        return;
    *out_x = (ref->rotation & 1) ? cfg->size_z : cfg->size_x;
    *out_z = (ref->rotation & 1) ? cfg->size_x : cfg->size_z;
    if( *out_x < 1 )
        *out_x = 1;
    if( *out_z < 1 )
        *out_z = 1;
}

static int
app_gizmo_project(
    struct App* app,
    int fine_x,
    int fine_z,
    int level,
    int* out_x,
    int* out_y)
{
    return app_world_project_at(app, fine_x, fine_z, app_world_height(app, fine_x, fine_z, level), out_x, out_y);
}

int
App_MapEditorGizmoScreen(
    struct App* app,
    struct LocEditorGizmoScreen* out,
    int* out_hot)
{
    struct Editor_Panel* panel;
    struct EditorUi* ui;
    struct LocEditorGizmoWorld world;
    const struct Editor_LocRef* ref;
    int size_x;
    int size_z;
    int sx;
    int sz;

    assert(app);
    assert(out);
    assert(out_hot);
    memset(out, 0, sizeof(*out));
    *out_hot = g_app_gizmo.active ? (int)g_app_gizmo.drag.handle : g_app_gizmo.hot;
    panel = &app->editor_panel;
    ui = panel->ui;
    if( !panel->visible || !ui || !app->world || ui->tools.loc_count == 0 )
        return 0;
    if( ui->tools.tool != EDITOR_TOOL_SELECT && ui->tools.tool != EDITOR_TOOL_LOC_MOVE )
        return 0;
    ref = &ui->tools.locs[0];
    sx = ref->map_x * 64 + ref->x - app->world->_base_tile_x;
    sz = ref->map_z * 64 + ref->z - app->world->_base_tile_z;
    if( sx < 0 || sz < 0 )
        return 0;
    app_gizmo_loc_size(app, ref, &size_x, &size_z);
    LocEditorGizmo_World(sx, sz, size_x, size_z, &world);
    if( !app_gizmo_project(app, world.centre_x, world.centre_z, ref->level, &out->centre_x, &out->centre_y) )
        return 0;
    for( int a = 0; a < 4; a++ )
        out->arrow_ok[a] = app_gizmo_project(app, world.arrow_base_x[a], world.arrow_base_z[a], ref->level,
                               &out->arrow_x0[a], &out->arrow_y0[a])
                           && app_gizmo_project(app, world.arrow_tip_x[a], world.arrow_tip_z[a], ref->level,
                               &out->arrow_x1[a], &out->arrow_y1[a]);
    for( int i = 0; i < LOC_EDITOR_GIZMO_RING_POINTS; i++ )
    {
        /* 2048 a turn, the engine's own sine table. */
        int const angle = i * 2048 / LOC_EDITOR_GIZMO_RING_POINTS;
        int const fx = world.centre_x + ((ToriDraw_Cos(angle) * world.ring_radius) >> 16);
        int const fz = world.centre_z + ((ToriDraw_Sin(angle) * world.ring_radius) >> 16);
        if( app_gizmo_project(app, fx, fz, ref->level, &out->ring_x[out->ring_count], &out->ring_y[out->ring_count]) )
            out->ring_count++;
    }
    out->valid = 1;
    return 1;
}

/** The status sentence for the handle under the pointer, once per change. */
static void
app_gizmo_say_hot(
    struct EditorUi* ui,
    int hot)
{
    static const char* const k_arrow[4] = { "north", "south", "west", "east" };

    if( hot == g_app_gizmo.said )
        return;
    g_app_gizmo.said = hot;
    if( hot >= LOC_EDITOR_GIZMO_ARROW_N && hot <= LOC_EDITOR_GIZMO_ARROW_E )
        Editor_UiStatus(ui, "Gizmo: drag to move the selection %s a tile at a time; Alt+drag copies it",
            k_arrow[hot - LOC_EDITOR_GIZMO_ARROW_N]);
    else if( hot == LOC_EDITOR_GIZMO_RING )
        Editor_UiStatus(ui, "Gizmo: click to turn the selection a quarter; drag round for more");
    else if( hot == LOC_EDITOR_GIZMO_CENTRE )
        Editor_UiStatus(ui, "Gizmo: drag to move the selection to the tile under the pointer");
}

/** The drag's destinations as the multi-piece ghost. */
static void
app_gizmo_preview(
    struct App* app,
    struct EditorUi* ui)
{
    struct LocEditorGizmoResult result;
    struct MapEditorPieceGhost* ghost = &app->map_ghost.pieces;

    LocEditorGizmo_DragResult(&g_app_gizmo.drag, &result);
    if( result.action != LOC_EDITOR_GIZMO_DO_MOVE && result.action != LOC_EDITOR_GIZMO_DO_DUPLICATE )
    {
        MapEditorPieceGhost_Clear(ghost);
        if( result.action == LOC_EDITOR_GIZMO_DO_ROTATE && result.quarters != g_app_gizmo.said_quarters )
        {
            g_app_gizmo.said_quarters = result.quarters;
            Editor_UiStatus(ui, "Turn %d degrees: release to apply", result.quarters * 90);
        }
        return;
    }
    if( MapEditorPieceGhost_Matches(ghost, MAP_EDITOR_PIECE_GHOST_DRAG, result.dx, result.dz, 0,
            (int)result.action, (unsigned)ui->tools.loc_count) )
        return;
    MapEditorPieceGhost_Begin(
        ghost, MAP_EDITOR_PIECE_GHOST_DRAG, result.dx, result.dz, 0, (int)result.action, (unsigned)ui->tools.loc_count);
    for( int i = 0; i < ui->tools.loc_count; i++ )
    {
        const struct Editor_LocRef* ref = &ui->tools.locs[i];
        struct MapEditorPieceGhostPiece piece;
        struct Editor_Square* square;
        int const wx = ref->map_x * 64 + ref->x + result.dx;
        int const wz = ref->map_z * 64 + ref->z + result.dz;
        int held = -1;

        if( wx < 0 || wz < 0 )
            continue;
        memset(&piece, 0, sizeof(piece));
        piece.scene_x = wx - app->world->_base_tile_x;
        piece.scene_z = wz - app->world->_base_tile_z;
        piece.level = ref->level;
        app_gizmo_loc_size(app, ref, &piece.size_x, &piece.size_z);
        square = Editor_DocFindSquare(&app->editor->doc, wx / 64, wz / 64);
        if( square && square->loaded )
            held = Editor_SquareLocFind(square, ref->level, wx % 64, wz % 64, ref->shape);
        /* Held by a loc the move carries along is free: it leaves first. */
        if( held >= 0 && result.action == LOC_EDITOR_GIZMO_DO_MOVE )
        {
            struct Editor_LocRef at;
            Editor_LocRefAt(&at, wx, wz, ref->level, ref->shape, -1, 0);
            if( Editor_UiFindLoc(ui, &at) >= 0 )
                held = -1;
        }
        piece.taken = held >= 0 || !square || !square->loaded;
        (void)MapEditorPieceGhost_Add(ghost, &piece);
    }
    app->need_redraw = 1;
}

/** A press on a handle starts its drag (and takes the press from the world
 *  click). 0 when the press is not on the gizmo. */
static int
app_map_editor_gizmo_press(
    struct App* app,
    struct LibToriRS_Input* input)
{
    struct Editor_Panel* panel = &app->editor_panel;
    struct LocEditorGizmoScreen screen;
    enum LocEditorGizmoHandle handle;
    int hot;
    int hx;
    int hz;

    if( !App_MapEditorGizmoScreen(app, &screen, &hot) )
        return 0;
    handle = LocEditorGizmo_HitTest(
        &screen, app->frame_view->world_mouse_x, app->frame_view->world_mouse_y, LOC_EDITOR_GIZMO_SLOP);
    if( handle == LOC_EDITOR_GIZMO_NONE )
        return 0;
    if( !Editor_PanelHoverTile(panel, app, &hx, &hz) )
    {
        hx = panel->ui->tools.locs[0].map_x * 64 + panel->ui->tools.locs[0].x - app->world->_base_tile_x;
        hz = panel->ui->tools.locs[0].map_z * 64 + panel->ui->tools.locs[0].z - app->world->_base_tile_z;
    }
    LocEditorGizmo_DragBegin(&g_app_gizmo.drag, handle, hx, hz, screen.centre_x, screen.centre_y,
        app->frame_view->world_mouse_x, app->frame_view->world_mouse_y,
        LibToriRS_Input_IsKeyHeld(input, TORIRSK_ALT));
    g_app_gizmo.active = 1;
    g_app_gizmo.said_quarters = -1;
    app_gizmo_preview(app, panel->ui);
    app->need_redraw = 1;
    return 1;
}

/** The held drag: follow the pointer; on the release, act once. */
static void
app_map_editor_gizmo_drag(
    struct App* app,
    struct LibToriRS_Input* input)
{
    struct Editor_Panel* panel = &app->editor_panel;
    struct EditorUi* ui = panel->ui;
    int const released =
        input->curr.mouse_button_up[TORIRSM_LEFT] || !LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
    struct LocEditorGizmoResult result;
    int hx;
    int hz;

    /* Off the world (the sky, a panel) the tile stays where it was; the
     * ring still follows the pointer. */
    if( !Editor_PanelHoverTile(panel, app, &hx, &hz) )
    {
        hx = g_app_gizmo.drag.start_x + g_app_gizmo.drag.dx;
        hz = g_app_gizmo.drag.start_z + g_app_gizmo.drag.dz;
    }
    if( LocEditorGizmo_DragUpdate(
            &g_app_gizmo.drag, hx, hz, app->frame_view->world_mouse_x, app->frame_view->world_mouse_y) )
        app_gizmo_preview(app, ui);
    if( !released )
        return;
    g_app_gizmo.active = 0;
    MapEditorPieceGhost_Clear(&app->map_ghost.pieces);
    app->need_redraw = 1;
    /* The selection may have gone while the button was held (an undo from
     * another window): nothing to act on is not an edit. */
    if( ui->tools.loc_count == 0 )
        return;
    LocEditorGizmo_DragResult(&g_app_gizmo.drag, &result);
    if( result.action == LOC_EDITOR_GIZMO_DO_MOVE )
        (void)edp_locs_nudge(ui, result.dx, result.dz);
    else if( result.action == LOC_EDITOR_GIZMO_DO_DUPLICATE )
        (void)edp_locs_duplicate_by(ui, result.dx, result.dz);
    else if( result.action == LOC_EDITOR_GIZMO_DO_ROTATE )
        (void)edp_locs_rotate_by(ui, result.quarters);
}

/** The armed stamp's pieces at the hovered tile, and the handle under the
 *  pointer; once per frame. */
static void
app_map_editor_piece_ghost_update(
    struct App* app,
    struct EditorUi* ui)
{
    struct Editor_Panel* panel = &app->editor_panel;
    struct MapEditorPieceGhost* ghost = &app->map_ghost.pieces;
    const struct Editor_Stamp* stamp = edp_stamp_armed(ui);
    const struct Editor_History* history = Editor_GetHistory(app->editor);
    unsigned serial;
    int wx;
    int wz;
    int level;

    /* The hover tells (R5): which handle a click would take. */
    if( !g_app_gizmo.active )
    {
        struct LocEditorGizmoScreen screen;
        int hot = LOC_EDITOR_GIZMO_NONE;
        if( App_MapEditorGizmoScreen(app, &screen, &hot) && !app->chrome_pointer_owned )
            hot = (int)LocEditorGizmo_HitTest(
                &screen, app->frame_view->world_mouse_x, app->frame_view->world_mouse_y, LOC_EDITOR_GIZMO_SLOP);
        else
            hot = LOC_EDITOR_GIZMO_NONE;
        if( hot != g_app_gizmo.hot )
        {
            g_app_gizmo.hot = hot;
            app->need_redraw = 1;
        }
        app_gizmo_say_hot(ui, hot);
        if( ghost->source == MAP_EDITOR_PIECE_GHOST_DRAG )
            MapEditorPieceGhost_Clear(ghost);
    }
    if( g_app_gizmo.active )
        return;
    if( !stamp || !panel->visible || !app->map_ghost.hover.valid || !app->world )
    {
        if( ghost->source == MAP_EDITOR_PIECE_GHOST_STAMP )
        {
            MapEditorPieceGhost_Clear(ghost);
            app->need_redraw = 1;
        }
        return;
    }
    wx = app->world->_base_tile_x + app->map_ghost.hover.scene_x;
    wz = app->world->_base_tile_z + app->map_ghost.hover.scene_z;
    level = Editor_PanelEditLevel(panel, app);
    /* Rebuilt when the tile, the turn or the document moves. */
    serial = (unsigned)history->applied * 2654435761u ^ (unsigned)history->count;
    if( MapEditorPieceGhost_Matches(ghost, MAP_EDITOR_PIECE_GHOST_STAMP, wx, wz, level, stamp->turned, serial) )
        return;
    MapEditorPieceGhost_Begin(ghost, MAP_EDITOR_PIECE_GHOST_STAMP, wx, wz, level, stamp->turned, serial);
    for( int i = 0; i < stamp->count; i++ )
    {
        struct MapEditorPieceGhostPiece piece;
        int px;
        int pz;
        int pl;
        int holder;
        if( stamp->pieces[i].kind != EDITOR_STAMP_LOC
            || !Editor_StampPieceLanding(&app->editor->doc, stamp, i, wx, wz, level, &px, &pz, &pl, &holder) )
            continue;
        memset(&piece, 0, sizeof(piece));
        piece.scene_x = px - app->world->_base_tile_x;
        piece.scene_z = pz - app->world->_base_tile_z;
        piece.level = pl;
        piece.size_x = stamp->pieces[i].size_x;
        piece.size_z = stamp->pieces[i].size_z;
        piece.taken = holder >= 0;
        (void)MapEditorPieceGhost_Add(ghost, &piece);
    }
    app->need_redraw = 1;
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
    app_map_editor_hover_update(app, panel);
    app_map_editor_hover_env_arm(ui);
    app_map_editor_level_env(app, ui);
    app_map_editor_spawn_ghost_update(app, ui);
    app_map_editor_piece_ghost_update(app, ui);

    /* Place previews the CATALOG pick; Move previews the first selected loc
     * at the tile it would land on (not on its own tile: a translucent double
     * of itself reads as flicker). An armed stamp is the multi-piece ghost's,
     * and so is a gizmo drag. */
    {
        int const hover_ok = app->map_ghost.hover.valid && !g_app_gizmo.active;

        if( panel->visible && hover_ok && ui->tools.tool == EDITOR_TOOL_LOC_PLACE && !edp_stamp_armed(ui)
            && ui->tools.pick_kind == CACHEPROVIDER_CATALOG_LOC && ui->tools.pick_id >= 0 )
        {
            want = 1;
            id = ui->tools.pick_id;
            Editor_PanelGhostSpec(panel, app, &shape, &angle);
            level = Editor_PanelEditLevel(panel, app);
            /* At the ghost's own tile (the hover a photograph parks too):
             * with Fit walls to neighbours on, the pose that joins the wall
             * line there -- what the click will place. */
            if( app->world )
                edp_place_pose_at(ui, app->world->_base_tile_x + app->map_ghost.hover.scene_x,
                    app->world->_base_tile_z + app->map_ghost.hover.scene_z, level, &shape, &angle);
        }
        else if( panel->visible && hover_ok && ui->tools.tool == EDITOR_TOOL_LOC_MOVE
                 && panel->sel_kind == EDITOR_SELECTION_LOC
                 && !(app->map_ghost.hover.scene_x == panel->sel_scene_x
                      && app->map_ghost.hover.scene_z == panel->sel_scene_z) )
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
                                                   .scene_x = app->map_ghost.hover.scene_x,
                                                   .scene_z = app->map_ghost.hover.scene_z,
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
 * Render the Preview panel's subject (the catalog's pick, else the selected
 * loc: Editor_Preview_Subject) into its model-view well.
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
 * Raster a preview model with the preview camera for a `width` x `height`
 * well, fitting the zoom on demand. The sprite is always
 * EDITOR_PREVIEW_SPRITE_W x _H (one atlas tile for the life of the window,
 * whatever the well does) with the model at its middle; the well shows it
 * centred (ToriRSChrome_ModelViewSetSprite).
 *
 * The fit reads the model's bounds cylinder and scales the raster distance so
 * the larger dimension fills most of the reference 120 x 96 well -- a candle
 * and a castle gate both arrive framed, instead of one vanishing and the other
 * cropping to a wall of pixels. The constant is calibrated against the
 * obj-icon pipeline (zoom 2000 frames a typical item in ~30px) and clamped so
 * a degenerate bounds cannot zoom to infinity.
 *
 * The Preview panel's well is any size, so this is ToriDraw_SpriteNewFromModelRaster's
 * camera with the PROJECTION scaled to the well
 * (EditorPreviewCamera_ProjectionScale): one framing, drawn bigger in a bigger
 * well. Scaling the distance instead would change the perspective. The model's
 * vertical middle is put on the view's: that raster's own placement
 * (`sin_pitch - height / 2 + model_height / 2`, a pixel count read as model
 * units) lifts a model by a quarter of a 96 px well and grows with the well's
 * pixels, which walked a tall well's model off its top.
 */
static struct ToriDraw_Sprite*
app_preview_raster(
    struct App* app,
    struct ToriDraw_ModelHandle hnd,
    int width,
    int height)
{
    struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
    struct ToriDraw_ViewPort view_port;
    struct ToriDraw_Camera camera;
    struct ToriDraw_Position position;
    toripixel_t* pixels;
    uint32_t* argb;
    int const sprite_w = EDITOR_PREVIEW_SPRITE_W;
    int const sprite_h = EDITOR_PREVIEW_SPRITE_H;
    size_t const pixel_count = (size_t)sprite_w * (size_t)sprite_h;
    int zoom;
    int middle_y;

    assert(width > 0);
    assert(height > 0);
    /* A model with no bounds is one that has not finished loading. 128 is a
     * middling size, so the well shows something framed rather than a dot or
     * a wall while it waits. */
    (void)EditorPreviewCamera_TakeFit(
        &app->preview_camera, bounds ? bounds->radius : 64, bounds ? bounds->min_y : 0, bounds ? bounds->max_y : 128);
    middle_y = bounds ? (bounds->min_y + bounds->max_y) / 2 : 0;
    /* After the fit: a new model's first render is at the zoom it chose. */
    zoom = app->preview_camera.zoom > 0 ? app->preview_camera.zoom : 2000;

    /* Only the part the well shows is rastered: the sprite's middle. */
    if( width > sprite_w )
        width = sprite_w;
    if( height > sprite_h )
        height = sprite_h;
    memset(&view_port, 0, sizeof(view_port));
    view_port.width = sprite_w;
    view_port.height = sprite_h;
    view_port.clip_left = (sprite_w - width) / 2;
    view_port.clip_top = (sprite_h - height) / 2;
    view_port.clip_right = view_port.clip_left + width;
    view_port.clip_bottom = view_port.clip_top + height;
    view_port.x_center = sprite_w / 2;
    view_port.y_center = sprite_h / 2;
    view_port.stride = sprite_w;

    memset(&camera, 0, sizeof(camera));
    camera.pitch = app->preview_camera.pitch;
    camera.projection_mode = TORIDRAW_PROJECTION_MODE_SCALE;
    camera.projection_scale = EditorPreviewCamera_ProjectionScale(width, height);
    camera.near_plane_z = 1;

    memset(&position, 0, sizeof(position));
    position.yaw = app->preview_camera.yaw;
    position.y = ((ToriDraw_Sin(app->preview_camera.pitch) * zoom) >> 16) - middle_y;
    position.z = (ToriDraw_Cos(app->preview_camera.pitch) * zoom) >> 16;

    pixels = TORIDRAW_CALLOC(pixel_count, sizeof(toripixel_t));
    assert(pixels);
    ToriDraw_RenderModelWithTable(hnd, app->scene, &position, &view_port, &camera, pixels,
        ToriDraw_KernelGetSpriteBaker());
    argb = TORIDRAW_MALLOC(pixel_count * sizeof(uint32_t));
    assert(argb);
    for( size_t i = 0; i < pixel_count; i++ )
    {
        uint32_t const rgb = toripixel_to_argb8888(pixels[i]) & 0xFFFFFFu;
        argb[i] = rgb ? (rgb | 0xFF000000u) : 0u;
    }
    TORIDRAW_FREE(pixels);
    return ToriDraw_SpriteNewFromArgbOwned(argb, sprite_w, sprite_h);
}

/* ---- the Locs panel's shape strip: one thumbnail per shape the loc has ---- */

/* One scene sprite for the whole strip, the shapes side by side. 0x400001xx
 * is free in uitree_scene_bridge.h's registry (PREVIEW is 0x4000000A, the
 * scaled fonts start at 0x40001000). */
#define APP_EDITOR_SHAPE_STRIP_SPRITE_ID 0x40000100
#define APP_EDITOR_SHAPE_THUMB_H 44
#define APP_EDITOR_SHAPE_STRIP_W 224

/** Raster `count` models (one placement's parts) into the slot
 *  [slot_x, slot_x + slot_w) of an ARGB strip `stride` wide, framed by their
 *  joint bounds, from the Preview's default angle (+ `yaw`) at `pitch` (the
 *  Preview's default, or straight down for the Catalog's thumbnails). */
static void
app_shape_thumb_raster(
    struct App* app,
    struct ToriDraw_ModelHandle const* parts,
    int count,
    toripixel_t* pixels,
    int stride,
    int slot_x,
    int slot_w,
    int slot_h,
    int yaw,
    int pitch)
{
    struct ToriDraw_ViewPort view_port;
    struct ToriDraw_Camera camera;
    struct ToriDraw_Position position;
    int radius = 0;
    int min_y = 0;
    int max_y = 0;
    int size;
    int zoom;

    for( int i = 0; i < count; i++ )
    {
        struct ToriDraw_BoundsCylinder* b = ToriDraw_ModelGetBoundsCylinder(parts[i]);
        if( !b )
            continue;
        if( b->radius > radius )
            radius = b->radius;
        if( b->min_y < min_y )
            min_y = b->min_y;
        if( b->max_y > max_y )
            max_y = b->max_y;
    }
    /* EditorPreviewCamera_TakeFit's framing, at the strip's size. */
    size = 2 * radius > max_y - min_y ? 2 * radius : max_y - min_y;
    zoom = (size * 9) / 2;
    if( zoom < EDITOR_PREVIEW_FIT_ZOOM_MIN )
        zoom = EDITOR_PREVIEW_FIT_ZOOM_MIN;
    if( zoom > EDITOR_PREVIEW_FIT_ZOOM_MAX )
        zoom = EDITOR_PREVIEW_FIT_ZOOM_MAX;

    memset(&view_port, 0, sizeof(view_port));
    view_port.width = stride;
    view_port.height = slot_h;
    view_port.clip_left = slot_x;
    view_port.clip_top = 0;
    view_port.clip_right = slot_x + slot_w;
    view_port.clip_bottom = slot_h;
    view_port.x_center = slot_x + slot_w / 2;
    view_port.y_center = slot_h / 2;
    view_port.stride = stride;

    memset(&camera, 0, sizeof(camera));
    camera.pitch = pitch;
    camera.projection_mode = TORIDRAW_PROJECTION_MODE_SCALE;
    camera.projection_scale = EditorPreviewCamera_ProjectionScale(slot_w, slot_h);
    camera.near_plane_z = 1;

    memset(&position, 0, sizeof(position));
    position.yaw = (EDITOR_PREVIEW_DEFAULT_YAW + yaw) & 2047;
    position.y = ((ToriDraw_Sin(camera.pitch) * zoom) >> 16) - (min_y + max_y) / 2;
    position.z = (ToriDraw_Cos(camera.pitch) * zoom) >> 16;
    for( int i = 0; i < count; i++ )
        ToriDraw_RenderModelWithTable(
            parts[i], app->scene, &position, &view_port, &camera, pixels, ToriDraw_KernelGetSpriteBaker());
}

#if !defined(__EMSCRIPTEN__)
/* ---- the Catalog's row thumbnails (workbench 6.2) -----------------------------
 *
 * A 16 x 16 top-down picture of one loc at one shape, rastered through the
 * Preview's pipeline (app_shape_thumb_raster, looking straight down) and
 * registered as a scene sprite in a block of its own. Cached by (id, shape)
 * for the process (the App holds no field for them: check-app-boundary);
 * the Catalog asks for the rows on screen only, so rows past the viewport
 * never raster. A loc whose config or models are still loading is asked for
 * once and answers 0 until they land; at most APP_EDITOR_THUMB_PER_SLICE are
 * rastered per 16 ms, so a fast scroll spreads its pictures over frames. */

#define APP_EDITOR_THUMB_SPRITE_BASE 0x40002000
#define APP_EDITOR_THUMB_SLOTS 256
#define APP_EDITOR_THUMB_PX 16
#define APP_EDITOR_THUMB_PER_SLICE 4
/** Straight down: the yaw's quarter turn of pitch (2048 a turn). */
#define APP_EDITOR_THUMB_PITCH 512

enum AppLocThumbState
{
    APP_THUMB_FREE = 0,
    /** Its config or a model was asked for. */
    APP_THUMB_LOADING,
    APP_THUMB_READY,
    /** No model at that shape: the empty cell, remembered. */
    APP_THUMB_NONE,
};

struct AppLocThumb
{
    int loc_id;
    int shape;
    enum AppLocThumbState state;
    /** The config / the models were asked for: once, not once a frame. */
    int config_asked;
    int models_asked;
    uint32_t used;
};

static struct AppLocThumb g_loc_thumbs[APP_EDITOR_THUMB_SLOTS];
static uint32_t g_loc_thumb_clock;
static double g_loc_thumb_slice_ms;
static int g_loc_thumb_slice_count;

static double
app_thumb_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/** The slot for (loc_id, shape): its own, a free one, or the least recently
 *  asked that is not loading. */
static struct AppLocThumb*
app_thumb_slot(
    int loc_id,
    int shape,
    int* out_index)
{
    int found = -1;
    int oldest = -1;

    for( int i = 0; i < APP_EDITOR_THUMB_SLOTS; i++ )
    {
        struct AppLocThumb* t = &g_loc_thumbs[i];
        if( t->state != APP_THUMB_FREE && t->loc_id == loc_id && t->shape == shape )
        {
            *out_index = i;
            return t;
        }
        if( t->state == APP_THUMB_FREE && found < 0 )
            found = i;
        if( t->state != APP_THUMB_LOADING && (oldest < 0 || t->used < g_loc_thumbs[oldest].used) )
            oldest = i;
    }
    if( found < 0 )
        found = oldest >= 0 ? oldest : 0;
    memset(&g_loc_thumbs[found], 0, sizeof(g_loc_thumbs[found]));
    g_loc_thumbs[found].loc_id = loc_id;
    g_loc_thumbs[found].shape = shape;
    g_loc_thumbs[found].state = APP_THUMB_LOADING;
    *out_index = found;
    return &g_loc_thumbs[found];
}

static int
ui_service_loc_thumbnail(
    void* user_data,
    int loc_id,
    int shape)
{
    struct App* app = user_data;
    struct AppLocThumb* thumb;
    struct ToriRS_Location* cfg;
    struct ToriDraw_ModelHandle parts[10];
    struct ToriDraw_Model* models[10];
    int const model_shape = shape >= 0 && shape < EDITOR_LOC_SHAPE_COUNT ? Editor_LocShapeModelShape(shape) : 10;
    int const id_base = APP_EDITOR_THUMB_SPRITE_BASE;
    toripixel_t* pixels;
    uint32_t* argb;
    int index;
    int n = 0;
    int missing = 0;

    assert(app);
    assert(loc_id >= 0);
    if( !app->provider || !app->scene )
        return 0;
    thumb = app_thumb_slot(loc_id, shape, &index);
    thumb->used = ++g_loc_thumb_clock;
    if( thumb->state == APP_THUMB_READY )
        return id_base + index;
    if( thumb->state == APP_THUMB_NONE )
        return 0;

    cfg = CacheProvider_LocationGet(app->provider, loc_id);
    if( !cfg )
    {
        if( !thumb->config_asked )
        {
            struct ToriRS_Task* task = CreateTask_LocLoad(app->provider, loc_id);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
            thumb->config_asked = 1;
        }
        return 0;
    }
    for( int row = 0; row < cfg->shapes_and_model_count; row++ )
    {
        if( cfg->shapes ? cfg->shapes[row] != model_shape : row != 0 )
            continue;
        for( int k = 0; cfg->models && cfg->lengths && k < cfg->lengths[row]; k++ )
            if( cfg->models[row][k] > 0 && !CacheProvider_ModelHas(app->provider, cfg->models[row][k]) )
            {
                if( !thumb->models_asked )
                {
                    struct ToriRS_Task* task = CreateTask_ModelLoad(app->provider, cfg->models[row][k]);
                    if( task )
                        ToriRS_TaskQueue_Add(app->runner.queue, task);
                }
                missing = 1;
            }
    }
    if( missing )
    {
        thumb->models_asked = 1;
        return 0;
    }
    {
        double const now = app_thumb_now_ms();
        if( now - g_loc_thumb_slice_ms > 16.0 )
        {
            g_loc_thumb_slice_ms = now;
            g_loc_thumb_slice_count = 0;
        }
        if( g_loc_thumb_slice_count >= APP_EDITOR_THUMB_PER_SLICE )
            return 0;
        g_loc_thumb_slice_count++;
    }

    /* The shape's own models, as the shape strip builds them. */
    for( int row = 0; row < cfg->shapes_and_model_count; row++ )
    {
        if( cfg->shapes ? cfg->shapes[row] != model_shape : row != 0 )
            continue;
        for( int k = 0; cfg->models && cfg->lengths && k < cfg->lengths[row] && n < 10; k++ )
        {
            struct ToriRS_Model* rs_model;
            if( cfg->models[row][k] <= 0 )
                continue;
            rs_model = CacheProvider_ModelGet(app->provider, cfg->models[row][k]);
            assert(rs_model);
            models[n] = ToriDraw_ModelFromToriRS(rs_model);
            assert(models[n]);
            for( int c = 0; c < cfg->recolor_count; c++ )
                ToriDraw_ModelRecolor(models[n], cfg->recolors_from[c], cfg->recolors_to[c]);
            ToriDraw_ModelSetBoundsCylinder(models[n]);
            ToriDraw_ModelDropNonSdTextures(app->provider, models[n]);
            memset(&parts[n], 0, sizeof(parts[n]));
            parts[n].kind = TORIDRAWMK_MODEL;
            parts[n].u.model.model = models[n];
            ToriDraw_LightModelScene(parts[n], cfg->contrast, cfg->ambient);
            n++;
        }
    }
    if( n == 0 )
    {
        thumb->state = APP_THUMB_NONE;
        return 0;
    }
    pixels = TORIDRAW_CALLOC((size_t)APP_EDITOR_THUMB_PX * APP_EDITOR_THUMB_PX, sizeof(toripixel_t));
    assert(pixels);
    app_shape_thumb_raster(app, parts, n, pixels, APP_EDITOR_THUMB_PX, 0, APP_EDITOR_THUMB_PX, APP_EDITOR_THUMB_PX,
        shape == 11 ? 256 : 0, APP_EDITOR_THUMB_PITCH);
    for( int k = 0; k < n; k++ )
        ToriDraw_ModelFree(models[k]);
    argb = TORIDRAW_MALLOC((size_t)APP_EDITOR_THUMB_PX * APP_EDITOR_THUMB_PX * sizeof(uint32_t));
    assert(argb);
    for( int i = 0; i < APP_EDITOR_THUMB_PX * APP_EDITOR_THUMB_PX; i++ )
    {
        uint32_t const rgb = toripixel_to_argb8888(pixels[i]) & 0xFFFFFFu;
        argb[i] = rgb ? (rgb | 0xFF000000u) : 0u;
    }
    TORIDRAW_FREE(pixels);
    {
        struct ToriDraw_Sprite** sprites = TORIDRAW_MALLOC(sizeof(*sprites)); /* SceneSpriteAdd frees it */
        assert(sprites);
        sprites[0] = ToriDraw_SpriteNewFromArgbOwned(argb, APP_EDITOR_THUMB_PX, APP_EDITOR_THUMB_PX);
        assert(sprites[0]);
        if( ToriDraw_SceneSpriteHas(app->scene, id_base + index) )
            ToriDraw_SceneSpriteRemove(app->scene, id_base + index);
        ToriDraw_SceneSpriteAdd(app->scene, id_base + index, sprites, 1);
    }
    thumb->state = APP_THUMB_READY;
    app->need_redraw = 1;
    return id_base + index;
}
#endif /* !__EMSCRIPTEN__ */

/**
 * The strip's thumbnails: each shape's own models (wall decor 5..8 shape 4's,
 * the diagonal centrepiece shape 10's turned an eighth), rendered once per
 * loc and shape set into one sprite. Models still loading are asked for and
 * the strip waits a frame.
 */
static void
app_map_editor_shape_strip_update(struct App* app)
{
    static int last_loc = -1;
    static int last_count = 0;
    static int last_shapes[EDITOR_LOC_SHAPE_COUNT];
    static int requested_loc = -1;
    int shapes[EDITOR_LOC_SHAPE_COUNT];
    int loc_id;
    int view;
    int count;
    int slot_w;
    int strip_w;
    struct ToriRS_Location* cfg;
    int missing = 0;
    toripixel_t* pixels;
    uint32_t* argb;

    /* After the world's first load: a raster at boot ran before the scene's
     * tables were ready and drew slivers. */
    if( !app->editor_panel.ui || !app->scene || !app->provider || !app->world || !app->world->load_complete
        || app->world_load_inflight )
        return;
    count = Editor_Locs_ShapeStrip(app->editor_panel.ui, &loc_id, shapes, &view);
    if( count <= 0 )
        return;
    if( loc_id == last_loc && count == last_count && memcmp(shapes, last_shapes, sizeof(int) * (size_t)count) == 0 )
        return;
    cfg = CacheProvider_LocationGet(app->provider, loc_id);
    if( !cfg )
    {
        if( requested_loc != loc_id )
        {
            struct ToriRS_Task* task = CreateTask_LocLoad(app->provider, loc_id);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
            requested_loc = loc_id;
        }
        return;
    }
    for( int row = 0; row < cfg->shapes_and_model_count; row++ )
        for( int k = 0; cfg->models && cfg->lengths && k < cfg->lengths[row]; k++ )
            if( cfg->models[row][k] > 0 && !CacheProvider_ModelHas(app->provider, cfg->models[row][k]) )
            {
                struct ToriRS_Task* task = CreateTask_ModelLoad(app->provider, cfg->models[row][k]);
                if( task )
                    ToriRS_TaskQueue_Add(app->runner.queue, task);
                missing = 1;
            }
    if( missing )
        return;

    slot_w = APP_EDITOR_SHAPE_STRIP_W / count;
    if( slot_w > APP_EDITOR_SHAPE_THUMB_H + 8 )
        slot_w = APP_EDITOR_SHAPE_THUMB_H + 8;
    strip_w = slot_w * count;
    pixels = TORIDRAW_CALLOC((size_t)strip_w * APP_EDITOR_SHAPE_THUMB_H, sizeof(toripixel_t));
    assert(pixels);
    for( int i = 0; i < count; i++ )
    {
        int const model_shape = Editor_LocShapeModelShape(shapes[i]);
        struct ToriDraw_ModelHandle parts[10];
        struct ToriDraw_Model* models[10];
        int n = 0;
        for( int row = 0; row < cfg->shapes_and_model_count; row++ )
        {
            if( cfg->shapes ? cfg->shapes[row] != model_shape : row != 0 )
                continue;
            for( int k = 0; k < cfg->lengths[row] && n < 10; k++ )
            {
                struct ToriRS_Model* rs_model;
                if( cfg->models[row][k] <= 0 )
                    continue;
                rs_model = CacheProvider_ModelGet(app->provider, cfg->models[row][k]);
                assert(rs_model);
                models[n] = ToriDraw_ModelFromToriRS(rs_model);
                assert(models[n]);
                for( int c = 0; c < cfg->recolor_count; c++ )
                    ToriDraw_ModelRecolor(models[n], cfg->recolors_from[c], cfg->recolors_to[c]);
                ToriDraw_ModelSetBoundsCylinder(models[n]);
                ToriDraw_ModelDropNonSdTextures(app->provider, models[n]);
                memset(&parts[n], 0, sizeof(parts[n]));
                parts[n].kind = TORIDRAWMK_MODEL;
                parts[n].u.model.model = models[n];
                ToriDraw_LightModelScene(parts[n], cfg->contrast, cfg->ambient);
                n++;
            }
        }
        app_shape_thumb_raster(app, parts, n, pixels, strip_w, i * slot_w, slot_w, APP_EDITOR_SHAPE_THUMB_H,
            shapes[i] == 11 ? 256 : 0, EDITOR_PREVIEW_DEFAULT_PITCH);
        for( int k = 0; k < n; k++ )
            ToriDraw_ModelFree(models[k]);
    }
    argb = TORIDRAW_MALLOC((size_t)strip_w * APP_EDITOR_SHAPE_THUMB_H * sizeof(uint32_t));
    assert(argb);
    for( int i = 0; i < strip_w * APP_EDITOR_SHAPE_THUMB_H; i++ )
    {
        uint32_t const rgb = toripixel_to_argb8888(pixels[i]) & 0xFFFFFFu;
        argb[i] = rgb ? (rgb | 0xFF000000u) : 0u;
    }
    TORIDRAW_FREE(pixels);
    {
        struct ToriDraw_Sprite** sprites = TORIDRAW_MALLOC(sizeof(*sprites)); /* SceneSpriteAdd frees it */
        assert(sprites);
        sprites[0] = ToriDraw_SpriteNewFromArgbOwned(argb, strip_w, APP_EDITOR_SHAPE_THUMB_H);
        assert(sprites[0]);
        if( ToriDraw_SceneSpriteHas(app->scene, APP_EDITOR_SHAPE_STRIP_SPRITE_ID) )
            ToriDraw_SceneSpriteRemove(app->scene, APP_EDITOR_SHAPE_STRIP_SPRITE_ID);
        ToriDraw_SceneSpriteAdd(app->scene, APP_EDITOR_SHAPE_STRIP_SPRITE_ID, sprites, 1);
        /* The strip is a GRAPH now (workbench 6.3): the panel draws the
         * picture under a keyed slot per shape, so a thumbnail clicks. */
        (void)view;
        {
            static uint32_t version;
            Editor_Locs_ShapeStripSprite(app->editor_panel.ui, APP_EDITOR_SHAPE_STRIP_SPRITE_ID, strip_w,
                APP_EDITOR_SHAPE_THUMB_H, slot_w, ++version);
        }
    }
    last_loc = loc_id;
    last_count = count;
    memcpy(last_shapes, shapes, sizeof(int) * (size_t)count);
    app->need_redraw = 1;
}

void
app_map_editor_preview_update(struct App* app)
{
    static int last_kind = -1;
    static int last_id = -1;
    static int last_w = -1;
    static int last_h = -1;
    /** The subject the camera was framed for: a new one is framed afresh. */
    static int subject_kind = -1;
    static int subject_id = -1;
    /** The loc / npc config last asked for, so a config still loading is
     *  asked once, not once a frame. */
    static int requested_kind = -1;
    static int requested_id = -1;
    /** The subject whose missing models were asked for: once, not once a
     *  frame (seven npc parts a frame for the seconds a cold load takes
     *  piled thousands of duplicate loads into the queue). */
    static int models_kind = -1;
    static int models_id = -1;
    struct Editor_Panel* panel = &app->editor_panel;
    struct ToriDraw_Sprite* sprite = NULL;
    int cat_kind;
    int cat_view;
    /* Not just the pick: while a multiloc VARIANT row is chosen this is that
     * rung's loc, so the well shows the variant the catalog is reading out;
     * with nothing picked, the selected loc. */
    int preview_id;
    int box_w;
    int box_h;
    int well_w;
    int well_h;

    assert(app);
    app_map_editor_shape_strip_update(app);

    if( !app->editor || !panel->visible || !panel->ui )
        return;
    /* The Preview module owns the well (panels/edpanel_preview.c), the
     * catalog the pick (panels/edpanel_catalog.c). A closed Preview renders
     * nothing. */
    cat_view = Editor_Preview_ModelView(panel->ui);
    if( cat_view < 0 )
        return;
    if( !ToriRSChrome_ModelViewBox(&app->dbg_ui, cat_view, &box_w, &box_h, &well_w, &well_h) )
        return;
    if( !Editor_Preview_Subject(panel->ui, &cat_kind, &preview_id) )
    {
        ToriRSChrome_ModelViewSet(&app->dbg_ui, cat_view, 0);
        last_kind = -1;
        last_id = -1;
        return;
    }
    /* A new subject is a new model: the default pose and a fresh fit, as
     * Reset view gives -- a candle picked after a castle gate is framed for
     * itself, not left at the gate's distance. */
    if( cat_kind != subject_kind || preview_id != subject_id )
    {
        subject_kind = cat_kind;
        subject_id = preview_id;
        EditorPreviewCamera_Invalidate(&app->preview_camera, false);
    }
    /* A key moved the camera: re-render the same pick. Taking the render is
     * also what resets the framing for a NEW one, so the two cannot disagree
     * about whether this is the same model. A resized well is the same model
     * at the new size, framed the same (the projection follows the well). */
    if( EditorPreviewCamera_TakeRender(&app->preview_camera) || well_w != last_w || well_h != last_h )
    {
        last_kind = -1;
        last_id = -1;
    }
    if( cat_kind == last_kind && preview_id == last_id )
        return;
    last_w = well_w;
    last_h = well_h;

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
            sprite = app_preview_raster(app, hnd, well_w, well_h);
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
            ToriRSChrome_ModelViewSetSprite(&app->dbg_ui, cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID,
                EDITOR_PREVIEW_SPRITE_W, EDITOR_PREVIEW_SPRITE_H);
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
        {
            /* Not resident: a window whose world never held this loc (a
             * popped-out Preview following another window's pick). Ask once;
             * retry every frame until it lands. */
            if( requested_kind != cat_kind || requested_id != preview_id )
            {
                struct ToriRS_Task* task = CreateTask_LocLoad(app->provider, preview_id);
                if( task )
                    ToriRS_TaskQueue_Add(app->runner.queue, task);
                requested_kind = cat_kind;
                requested_id = preview_id;
            }
            return;
        }
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
            if( models_kind != cat_kind || models_id != preview_id )
            {
                struct ToriRS_Task* task = CreateTask_ModelLoad(app->provider, model_id);
                if( task )
                    ToriRS_TaskQueue_Add(app->runner.queue, task);
                models_kind = cat_kind;
                models_id = preview_id;
            }
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
            sprite = app_preview_raster(app, hnd, well_w, well_h);
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
            ToriRSChrome_ModelViewSetSprite(&app->dbg_ui, cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID,
                EDITOR_PREVIEW_SPRITE_W, EDITOR_PREVIEW_SPRITE_H);
            last_kind = cat_kind;
            last_id = preview_id;
        }
        return;
    }

    if( cat_kind == CACHEPROVIDER_CATALOG_NPC )
    {
        struct ToriRS_Npctype* npc = CacheProvider_NpctypeGet(app->provider, preview_id);
        int missing = 0;

        if( !npc )
        {
            /* Not resident (no such npc spawned in this window's world): ask
             * once, as for a loc above. */
            if( requested_kind != cat_kind || requested_id != preview_id )
            {
                struct ToriRS_Task* task = CreateTask_NpcLoad(app->provider, preview_id);
                if( task )
                    ToriRS_TaskQueue_Add(app->runner.queue, task);
                requested_kind = cat_kind;
                requested_id = preview_id;
            }
            return;
        }
        if( npc->models_count <= 0 )
        {
            ToriRSChrome_ModelViewSet(&app->dbg_ui, cat_view, 0);
            last_kind = cat_kind;
            last_id = preview_id;
            return;
        }
        for( int i = 0; i < npc->models_count; i++ )
            if( npc->models[i] > 0 && !CacheProvider_ModelHas(app->provider, npc->models[i]) )
            {
                if( models_kind != cat_kind || models_id != preview_id )
                {
                    struct ToriRS_Task* task = CreateTask_ModelLoad(app->provider, npc->models[i]);
                    if( task )
                        ToriRS_TaskQueue_Add(app->runner.queue, task);
                }
                missing = 1;
            }
        if( missing )
        {
            models_kind = cat_kind;
            models_id = preview_id;
            return; /* retry once the parts land */
        }

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
            sprite = app_preview_raster(app, hnd, well_w, well_h);
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
            ToriRSChrome_ModelViewSetSprite(&app->dbg_ui, cat_view, UITREE_SCENE_EDITOR_PREVIEW_ID,
                EDITOR_PREVIEW_SPRITE_W, EDITOR_PREVIEW_SPRITE_H);
            last_kind = cat_kind;
            last_id = preview_id;
        }
        return;
    }

    ToriRSChrome_ModelViewSet(&app->dbg_ui, cat_view, 0);
    last_kind = cat_kind;
    last_id = preview_id;
}

