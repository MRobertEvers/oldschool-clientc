/*
 * The map editor's VIEW glue: what puts an editor/editor_view.c view in a
 * viewer process. It moves the App's free camera to the view's pose and reads
 * it back, feeds the level mask to the painter, and spawns new views (the
 * launch service natively, a tab on the web).
 *
 * One translation unit of the App layer, split out of editor/editor_view.c
 * so the core there names no App (tools/appc_map.py check). Declared in
 * app/app_internal.h; editor_view.h has the view's contract.
 */

#include "app/app_internal.h"
#include "editor/editor_view.h"
#include "asyncio.h"
#include "bootmanifest/bootmanifest.h"
#include "log/torirs_log.h"
#include "mem/torirs_mem.h"
#include "plugin/task_plugin_io.h"
#include "world/world.h"

#include <time.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

/*
 * One view per process: a viewer process IS one window (platform_sdl2.c holds
 * exactly one), so the view lives beside it as file-static state, the way
 * app_map_editor.c keeps its EDIT shadows. The core above takes everything as
 * arguments, which is what lets the three-view test run in one process.
 */
static struct EditorView g_app_view;
static int g_app_view_live;
/** The editor boot's App, for the page's "spawn view" (any editor boot, view
 *  or main window). */
static struct App* g_app;
/** A pose waiting for its square to load before it can be applied. */
static int g_pending_apply;
static struct EditorViewCamera g_pending_camera;
/** The App camera as the last apply left it: while it is still exactly
 *  that, the view's pose stands as adopted rather than being re-derived from
 *  the eye (which, on a slope, would come back a few units off and make two
 *  views following each other drift). */
static int g_applied_valid;
static struct ToriDraw_Position g_applied_eye;
static int g_applied_yaw;
static int g_applied_pitch;

int
EditorView_AppIsView(void)
{
    return g_app_view_live;
}

static int64_t
view_now_ms(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}


/** Put the App's free camera at `pose`. 0 when the point is outside the
 *  loaded scene (the caller loads its square and retries). */
static int
view_apply_to_app(
    struct App* app,
    const struct EditorViewCamera* pose)
{
    struct App_WorldView* frame_view = app->frame_view;
    int scene_x;
    int scene_z;
    int extent;
    int target_y;
    int pitch;

    if( !app->world || !frame_view )
        return 0;
    scene_x = pose->x - app->world->_base_tile_x * 128;
    scene_z = pose->z - app->world->_base_tile_z * 128;
    extent = app->world->_scene_size * 128;
    if( scene_x < 0 || scene_z < 0 || scene_x >= extent || scene_z >= extent )
        return 0;

    pitch = app_world_clamp_pitch(app, pose->pitch);
    target_y = app_world_height(app, scene_x, scene_z, pose->level);
    {
        struct EditorViewCamera clamped = *pose;
        clamped.pitch = pitch;
        EditorView_PoseEye(&clamped,
            app->world->_base_tile_x * 128,
            app->world->_base_tile_z * 128,
            target_y,
            &frame_view->world_camera_pos);
    }
    frame_view->world_camera.pitch = pitch;
    frame_view->world_camera.yaw = pose->yaw & 0x7ff;
    /* The free camera: a view has no player to follow. */
    frame_view->camera_unlocked = 1;
    app->need_redraw = 1;
    g_applied_valid = 1;
    g_applied_eye = frame_view->world_camera_pos;
    g_applied_yaw = frame_view->world_camera.yaw;
    g_applied_pitch = frame_view->world_camera.pitch;
    return 1;
}

/** Read the App's free camera back as a pose: the point the eye looks at is
 *  where its view ray meets the ground under the eye; the level is the
 *  view's. */
static int
view_read_from_app(
    struct App* app,
    const struct EditorViewCamera* previous,
    struct EditorViewCamera* out)
{
    struct App_WorldView const* frame_view = app->frame_view;
    struct ToriDraw_Position const eye = frame_view->world_camera_pos;
    int extent;

    if( !app->world )
        return 0;
    extent = app->world->_scene_size * 128;
    if( eye.x < 0 || eye.z < 0 || eye.x >= extent || eye.z >= extent )
        return 0;
    EditorView_PoseFromEye(&eye,
        frame_view->world_camera.yaw,
        frame_view->world_camera.pitch,
        app_world_height(app, eye.x, eye.z, previous->level),
        app->world->_base_tile_x * 128,
        app->world->_base_tile_z * 128,
        previous->level,
        previous->zoom,
        out);
    return 1;
}

/** Load the square `pose` looks at, then apply it (next frames). */
static void
view_apply_or_load(
    struct App* app,
    const struct EditorViewCamera* pose)
{
    if( view_apply_to_app(app, pose) )
    {
        g_pending_apply = 0;
        return;
    }
    g_pending_camera = *pose;
    g_pending_apply = 1;
    if( !app->world_load_inflight )
    {
        int chunks[2] = { pose->x / (64 * 128), pose->z / (64 * 128) };
        TORIRS_LOG("editor view: loading m%d_%d for the camera\n", chunks[0], chunks[1]);
        app_world_load_begin(app, chunks, 1);
    }
}

void
EditorView_AppBoot(struct App* app)
{
    struct EditorViewCamera boot;
    int boot_set;

    assert(app);
    assert(app->editor);
    assert(!g_app_view_live);

    g_app = app;
    if( app->cfg.editor_panel != BOOTMANIFEST_EDITOR_PANEL_VIEW )
        return;

    EditorView_Init(&g_app_view, app->editor);
    g_app_view_live = 1;
    EditorView_ToolbarBuild(&g_app_view, &app->dbg_ui);
    EditorView_AskSelf(&g_app_view);

    boot_set = EditorView_BootCamera(&boot);
    if( boot_set )
    {
        g_app_view.camera = boot;
        g_app_view.camera_valid = 1;
        g_pending_camera = boot;
        g_pending_apply = 1;
    }
    if( app->frame_view )
        app->frame_view->camera_unlocked = 1;
    TORIRS_LOG("editor view: a viewer of Client %u%s\n",
        Editor_HostMapEdClientId(&app->editor->host),
        boot_set ? " at the --camera pose" : "");
}

int
EditorView_AppActivate(
    struct App* app,
    int widget)
{
    enum EditorViewAction action;

    assert(app);
    if( !g_app_view_live || widget < 0 )
        return 0;
    action = EditorView_ToolbarActivate(&g_app_view, &app->dbg_ui, widget);
    if( action == EDITOR_VIEW_ACTION_OPEN_PANELS )
    {
        /* Promotion: the same window becomes a full client of the Client. */
        Editor_PanelSetVisible(&app->editor_panel, &app->dbg_ui, 1);
        TORIRS_LOG("editor view: panels opened in this window\n");
    }
    return action != EDITOR_VIEW_ACTION_NONE;
}

void
EditorView_AppFrame(struct App* app)
{
    struct EditorViewCamera pose;

    assert(app);
    if( !g_app_view_live )
        return;

    /* The frame's one activation is ours while the panels are hidden; once
     * they show, their dispatcher drains it and offers it back. */
    if( !app->editor_panel.visible )
        EditorView_AppActivate(app, ToriRSChrome_TakeActivated(&app->dbg_ui));

    /* A pose waiting for its world, or one the view changed itself. */
    if( g_pending_apply && !app->world_load_inflight )
        view_apply_or_load(app, &g_pending_camera);
    if( EditorView_TakeCamera(&g_app_view, &pose) )
        view_apply_or_load(app, &pose);

    /* What the user did with the camera this frame, for the broadcast. An
     * untouched camera keeps the pose it was given. */
    {
        int const untouched = g_applied_valid && app->frame_view
                              && app->frame_view->world_camera_pos.x == g_applied_eye.x
                              && app->frame_view->world_camera_pos.y == g_applied_eye.y
                              && app->frame_view->world_camera_pos.z == g_applied_eye.z
                              && app->frame_view->world_camera.yaw == g_applied_yaw
                              && app->frame_view->world_camera.pitch == g_applied_pitch;
        if( !untouched )
            g_applied_valid = 0;
        if( untouched || g_pending_apply || !app->world || !app->frame_view )
            goto publish;
    }
    {
        struct EditorViewCamera previous = g_app_view.camera;
        if( !g_app_view.camera_valid )
        {
            memset(&previous, 0, sizeof(previous));
            previous.zoom = 1200;
        }
        if( view_read_from_app(app, &previous, &pose) )
            EditorView_SetCamera(&g_app_view, &pose);
    }
publish:
    EditorView_Tick(&g_app_view, view_now_ms());
}

int
EditorView_AppOnState(
    struct App* app,
    uint32_t key,
    const int32_t* values,
    int count)
{
    int consumed;

    assert(app);
    if( !g_app_view_live )
        return 0;
    consumed = EditorView_OnState(&g_app_view, key, values, count);
    if( consumed )
        EditorView_ToolbarRefresh(&g_app_view, &app->dbg_ui);
    return consumed;
}

uint8_t
EditorView_AppLevelMask(const struct App* app)
{
    assert(app);
    (void)app; /* one view per process: the App only proves the caller is one */
    if( !g_app_view_live )
        return 0;
    return EditorView_LevelMask(&g_app_view);
}

#if defined(__EMSCRIPTEN__)
/* torirs_host.js's torirsSpawnEditorView: the page knows its own maped URL,
 * manifest and servers, and is the only thing that may open a tab. `maped` is
 * this boot's own address, for a page that was not given one. */
EM_JS(int, editor_view_web_spawn, (const char* camera, unsigned group, const char* maped), {
    if( typeof window.torirsSpawnEditorView !== 'function' )
        return 0;
    return window.torirsSpawnEditorView(UTF8ToString(camera), group, UTF8ToString(maped)) ? 1 : 0;
});
#endif

#if !defined(__EMSCRIPTEN__)
/** A launch answer: logged, then freed (the deliver contract). */
static void
view_spawn_delivered(
    void* user,
    int serial,
    char const* path,
    void* data,
    int size)
{
    (void)user;
    (void)serial;
    (void)path;
    if( !data )
    {
        TORIRS_LOG("editor view: no launch service answered %s\n", path);
        return;
    }
    TORIRS_LOG("editor view: %.*s", size, (const char*)data);
    ToriMem_Free(ToriMem_Net(), data);
}
#endif

int
EditorView_AppSpawn(
    struct App* app,
    const struct EditorViewCamera* camera)
{
    uint32_t group;

    assert(app);
    assert(camera);
    assert(app->editor);

    group = Editor_HostMapEdClientId(&app->editor->host);
#if defined(__EMSCRIPTEN__)
    {
        int32_t values[TORIRSMAPED_CAMERA_VALUES];
        char pose[96];
        char maped[192];
        EditorView_CameraToValues(camera, values);
        ToriRSMapEd_CameraFormat(values, pose, sizeof(pose));
        snprintf(maped,
            sizeof(maped),
            "ws://%s:%d",
            app->cfg.editor_server_host && app->cfg.editor_server_host[0]
                ? app->cfg.editor_server_host
                : "localhost",
            app->cfg.editor_server_port > 0 ? app->cfg.editor_server_port
                                            : TORIRSMAPED_DEFAULT_PORT);
        if( !editor_view_web_spawn(pose, group, maped) )
        {
            TORIRS_LOG("editor view: the page could not open a view tab (allow popups)\n");
            return 0;
        }
        return 1;
    }
#else
    {
        char url[192];
        char body[512];
        int length;
        static int serial;

        /* A server with a SPAWN_VIEW binding starts the view on ITS host, with
         * its own binary and manifest -- the only way a remote daemon's view
         * can join it, and as good as the local launch for one beside us. */
        if( app->editor->capabilities & TORIRSMAPED_CAP_SPAWN_VIEW )
        {
            int32_t values[TORIRSMAPED_CAMERA_VALUES];
            int pid = 0;
            enum EditorHost_Status status;
            EditorView_CameraToValues(camera, values);
            status = Editor_HostMapEdSpawnView(&app->editor->host, values, &pid);
            if( status == EDITOR_HOST_OK )
            {
                TORIRS_LOG("editor view: the server started a view (pid %d)\n", pid);
                return 1;
            }
            TORIRS_LOG("editor view: the server refused SPAWN_VIEW (status %d)\n", (int)status);
            return 0;
        }
        if( app->cfg.editor_server != BOOTMANIFEST_EDITOR_SERVER_TCP )
        {
            TORIRS_LOG("editor view: a new view is a second process, and this session's "
                       "ToriRSMapEd is embedded in this one with no socket to reach it -- "
                       "run the torirsmaped daemon ([editor:boot] server=tcp) to open views\n");
            return 0;
        }
        snprintf(url,
            sizeof(url),
            "tcp://%s:%d",
            app->cfg.editor_server_host && app->cfg.editor_server_host[0]
                ? app->cfg.editor_server_host
                : "127.0.0.1",
            app->cfg.editor_server_port > 0 ? app->cfg.editor_server_port
                                            : TORIRSMAPED_DEFAULT_PORT);
        length = EditorView_SpawnBody(url, group, camera, body, (int)sizeof(body));
        assert(length > 0);
        ToriRS_TaskQueue_Add(app->runner.queue,
            CreateTask_PluginLaunch("view", body, length, ++serial, view_spawn_delivered, NULL));
        return 1;
    }
#endif
}

#if defined(__EMSCRIPTEN__)
/**
 * The page's "spawn view" button (index.html, torirs_host.js): a new view of
 * this session looking where this window looks -- the view's own pose in a
 * viewer tab, the free camera's in the main one. 0 when this boot has no
 * editor.
 */
EMSCRIPTEN_KEEPALIVE int
torirs_editor_view_spawn(void)
{
    struct EditorViewCamera camera;

    if( !g_app || !g_app->editor )
        return 0;
    if( g_app_view_live && g_app_view.camera_valid )
        camera = g_app_view.camera;
    else
    {
        struct EditorViewCamera previous;
        memset(&previous, 0, sizeof(previous));
        previous.zoom = 1200;
        if( !g_app->world || !g_app->frame_view
            || !view_read_from_app(g_app, &previous, &camera) )
            return 0;
    }
    return EditorView_AppSpawn(g_app, &camera);
}
#endif
