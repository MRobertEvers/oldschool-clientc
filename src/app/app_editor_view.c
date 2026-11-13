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
#include "editor/editor_host.h"
#include "editor/editor_ui.h"
#include "editor/editor_view.h"
#include "torirsmaped/torirs_maped_net.h"
#include "asyncio.h"
#include "bootmanifest/bootmanifest.h"
#include "log/torirs_log.h"
#include "mem/torirs_mem.h"
#include "plugin/task_plugin_io.h"
#include "world/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
/** The embedded ToriRSMapEd's loopback door (server=embed): opened by the
 *  first pop-out, polled every frame, closed before the session. NULL until
 *  then, and always NULL in a process whose server is a daemon. */
static struct ToriRSMapEdListener* g_embed_listener;
static int g_applied_valid;
static struct ToriDraw_Position g_applied_eye;
static int g_applied_yaw;
static int g_applied_pitch;
/** A popped-out panel that draws no world (EditorView_AppHoldsNoWorld). */
static int g_no_world;
/** The Dock button closed this window: said once, then the main loop ends. */
static int g_dock_exit_said;

int
EditorView_AppIsView(void)
{
    return g_app_view_live;
}

int
EditorView_AppHoldsNoWorld(void)
{
    return g_no_world;
}

static int64_t
view_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
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
    /* A panel-only window keeps no world to put the camera in. */
    if( g_no_world )
    {
        g_pending_apply = 0;
        return;
    }
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
    /* Photographs of popped-out windows: a window inherits its leader's
     * environment, TORIRS_PRESENT_BMP included, so each takes its own file
     * under TORIRS_EDITOR_VIEW_BMP=<dir>[@<frame>] instead. */
    {
        char const* dir = getenv("TORIRS_EDITOR_VIEW_BMP");
        if( dir && dir[0] )
        {
            char path[512];
            char frame[16];
            int const dir_len = (int)strcspn(dir, "@");
            snprintf(path, sizeof(path), "%.*s/view_%s.bmp", dir_len, dir,
                EditorView_BootPanel() ? EditorView_BootPanel() : "viewer");
            setenv("TORIRS_PRESENT_BMP", path, 1);
            snprintf(frame, sizeof(frame), "%d", dir[dir_len] == '@' ? atoi(dir + dir_len + 1) : 400);
            setenv("TORIRS_PRESENT_BMP_FRAME", frame, 1);
            /* And its exit capture beside it, never over the leader's
             * TORIRS_EXIT_BMP (this window outlives the leader's last frame). */
            if( getenv("TORIRS_EXIT_BMP") )
            {
                snprintf(path, sizeof(path), "%.*s/view_%s_exit.bmp", dir_len, dir,
                    EditorView_BootPanel() ? EditorView_BootPanel() : "viewer");
                setenv("TORIRS_EXIT_BMP", path, 1);
            }
        }
    }
    if( EditorView_BootPanel() )
    {
        /* A popped-out panel: the command panels shown -- so this window's
         * session follows and drives the Client's tool, brush, level and
         * selection like the main window's -- every one but this one hidden
         * by the solo layout. A popped-out World edits: its clicks run the
         * shared tool, and the thin toolbar sits over it. Any other panel
         * draws no world (World is hidden) and has no toolbar. */
        const char* const module = EditorView_BootPanel();
        assert(app->editor_panel.built);
        /* Lighter panel windows (design #workbench "Every panel pops out"):
         * a window that holds no World loads no world. World and Preview
         * draw one and are unchanged; any other panel keeps the mirror, the
         * catalog link and the preview raster, which is what it needs, and
         * skips the squares around the camera -- the boot's default load
         * (app_boot.c) and the camera's (view_apply_or_load) both stand
         * down, and the first-sight load in the frame never starts. */
        Editor_PanelSetVisible(&app->editor_panel, &app->dbg_ui, 1);
        if( !Editor_UiModuleNeedsWorld(module) )
        {
            g_no_world = 1;
            app->world_load_attempted = 1;
            TORIRS_REPORT("editor view: a popped-out %s panel holds no world: skipping the world load around the "
                          "camera (the mirror, the catalog link and the preview raster stay)\n",
                module);
            Editor_UiLog(app->editor_panel.ui,
                "editor: this %s window loads no world (the main window's World shows it); squares open there",
                module);
        }
        if( !Editor_UiSetSolo(app->editor_panel.ui, module) )
            TORIRS_REPORT("editor view: there is no panel '%s' to show; showing the panels\n", module);
        else
            TORIRS_REPORT("editor view: a popped-out %s panel of Client %u\n", module,
                Editor_HostMapEdClientId(&app->editor->host));
        if( strcmp(module, "world") == 0 )
            EditorView_ToolbarBuild(&g_app_view, &app->dbg_ui);
    }
    else
        EditorView_ToolbarBuild(&g_app_view, &app->dbg_ui);
    EditorView_AskSelf(&g_app_view);
    /* The Client's state as it stands (selection, tool, brush, palette,
     * the pinned level): a window that joins late starts where the others
     * are. */
    if( Editor_HostIsMapEd(&app->editor->host) )
    {
        int const replayed = Editor_HostMapEdStateSync(&app->editor->host);
        /* A plain viewer has no panels to follow the pinned level: it takes
         * it once as its cap. A popped-out panel's session follows the
         * level (and every shared tool) live, so its view adds no cap. */
        if( replayed > 0 && !EditorView_BootPanel() )
            EditorView_ExpectSync(&g_app_view, replayed);
    }

    boot_set = EditorView_BootCamera(&boot);
    if( boot_set )
    {
        g_app_view.camera = boot;
        g_app_view.camera_valid = 1;
        g_pending_camera = boot;
        /* No world to apply it to in a panel-only window: the pose is kept
         * as the view's (it still says where the Client looks). */
        g_pending_apply = !g_no_world;
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
        /* Promotion: the same window becomes a full client of the Client
         * (a popped-out World leaves its solo layout for the whole set). */
        Editor_PanelSetVisible(&app->editor_panel, &app->dbg_ui, 1);
        if( app->editor_panel.ui && app->editor_panel.ui->solo_panel >= 0 )
            Editor_UiClearSolo(app->editor_panel.ui);
        TORIRS_LOG("editor view: panels opened in this window\n");
    }
    return action != EDITOR_VIEW_ACTION_NONE;
}

/* ---- headless drivers for photographs ------------------------------------
 *
 *   TORIRS_EDITOR_RESIZE=<module>,<l|r|t|b>,<dx>,<dy>[@<frame>]
 *       at main-loop editor frame <frame> (default 200) press the named
 *       panel's border on that edge, move by (dx, dy), release: a real chrome
 *       edge resize.
 *   TORIRS_EDITOR_POPOUT=<module>[,<module>...][@<frame>]
 *       at frame <frame> (default 300) pop each named panel out
 *       (Editor_UiPanelPopOut), exactly as its title-bar button does.
 *
 * One shot each, main window only (a popped-out window ignores them). */
static int g_drive_frame;

static int
view_drive_at(
    const char* spec,
    int fallback)
{
    char const* at = strchr(spec, '@');
    return at ? atoi(at + 1) : fallback;
}

static void
view_drive(struct App* app)
{
    char const* resize = getenv("TORIRS_EDITOR_RESIZE");
    char const* popout = getenv("TORIRS_EDITOR_POPOUT");
    struct EditorUi* ui = app->editor_panel.ui;

    g_drive_frame++;
    if( !ui || !app->editor_panel.visible )
        return;
    if( resize && resize[0] && g_drive_frame == view_drive_at(resize, 200) )
    {
        char module[32] = { 0 };
        char edge = 0;
        int dx = 0;
        int dy = 0;
        struct EditorUiPanel* panel;
        if( sscanf(resize, "%31[a-z0-9_],%c,%d,%d", module, &edge, &dx, &dy) == 4
            && (panel = Editor_UiFindPanel(ui, module)) && panel->chrome_panel >= 0 )
        {
            struct ToriRSChromeRect const r = app->dbg_ui.panels[panel->chrome_panel].last_rect;
            int x = r.x + r.w / 2;
            int y = r.y + r.h / 2;
            if( edge == 'l' )
                x = r.x + 1;
            else if( edge == 'r' )
                x = r.x + r.w - 2;
            else if( edge == 't' )
                y = r.y + 1;
            else
                y = r.y + r.h - 2;
            ToriRSChrome_MouseMove(&app->dbg_ui, x, y);
            ToriRSChrome_MouseDown(&app->dbg_ui, x, y);
            ToriRSChrome_MouseMove(&app->dbg_ui, x + dx, y + dy);
            ToriRSChrome_MouseUp(&app->dbg_ui, x + dx, y + dy);
            TORIRS_REPORT("editor: TORIRS_EDITOR_RESIZE %s edge %c by %d,%d: box %d,%d %dx%d -> %d,%d %dx%d\n",
                module, edge, dx, dy, r.x, r.y, r.w, r.h, app->dbg_ui.panels[panel->chrome_panel].x,
                app->dbg_ui.panels[panel->chrome_panel].y, app->dbg_ui.panels[panel->chrome_panel].fixed_w,
                app->dbg_ui.panels[panel->chrome_panel].fixed_h);
        }
        else
            TORIRS_REPORT("editor: TORIRS_EDITOR_RESIZE '%s' names no panel edge\n", resize);
    }
    if( popout && popout[0] && g_drive_frame == view_drive_at(popout, 300) )
    {
        char list[128];
        snprintf(list, sizeof(list), "%.*s", (int)strcspn(popout, "@"), popout);
        for( char* module = strtok(list, ","); module; module = strtok(NULL, ",") )
            (void)Editor_UiPanelPopOut(ui, module);
    }
}

void
EditorView_AppFrame(struct App* app)
{
    struct EditorViewCamera pose;

    assert(app);
    /* The embedded server's loopback door: popped-out windows' requests and
     * their broadcasts, a few rounds a frame so a window's boot (one
     * synchronous request after another) is not one round trip per frame. */
    if( g_embed_listener )
        for( int round = 0; round < 4; round++ )
            if( ToriRSMapEd_ListenerPoll(g_embed_listener, 0) <= 0 )
                break;
    if( !g_app_view_live )
    {
        view_drive(app);
        return;
    }

    /* The title bar's Dock (Editor_UiDockBack): the leader was told, so this
     * window goes -- natively the main loop ends on its next frame
     * (EditorView_ExitRequested), on the web the tab closes itself, which a
     * tab the page opened (torirsSpawnEditorView) may. */
    if( app->editor_panel.ui && app->editor_panel.ui->dock_exit && !g_dock_exit_said )
    {
        g_dock_exit_said = 1;
        TORIRS_REPORT("editor view: docked back into the main window; closing\n");
        EditorView_RequestExit();
#if defined(__EMSCRIPTEN__)
        emscripten_run_script("window.close()");
#endif
    }

    /* A popped-out World: the thin toolbar sits inside the World panel's
     * body, under its title bar (which names the Client's tool and level),
     * wherever the solo layout put the panel for this window's size. */
    if( g_app_view.toolbar.built && app->editor_panel.ui && app->editor_panel.ui->solo_panel >= 0
        && app->editor_panel.ui->solo_panel == app->editor_panel.ui->world_panel )
    {
        struct ToriRSChromeRect const body = ToriRSChrome_PanelBodyRect(&app->dbg_ui, app->editor_panel.ui->world_panel);
        int const pad = ToriRSChrome_Px(&app->dbg_ui, 8);
        struct ToriRSChromePanel const* bar = &app->dbg_ui.panels[g_app_view.toolbar.panel];
        if( body.w > 0 && (bar->x != body.x + pad || bar->y != body.y + pad) )
            ToriRSChrome_PanelMove(&app->dbg_ui, g_app_view.toolbar.panel, body.x + pad, body.y + pad);
    }

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
    if( (consumed || key == TORIRSMAPED_STATE_EDIT_LEVEL) && g_app_view.toolbar.built )
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
 * this boot's own address, for a page that was not given one; `panel` the
 * popped-out module ("" = the 3D view). */
EM_JS(int, editor_view_web_spawn, (const char* camera, unsigned group, const char* maped, const char* panel), {
    if( typeof window.torirsSpawnEditorView !== 'function' )
        return 0;
    return window.torirsSpawnEditorView(UTF8ToString(camera), group, UTF8ToString(maped), UTF8ToString(panel))
               ? 1
               : 0;
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
    TORIRS_REPORT("editor view: %.*s", size, (const char*)data);
    ToriMem_Free(ToriMem_Net(), data);
}

/**
 * The port a second process dials to join this session: the daemon's under
 * server=tcp; under server=embed the embedded server's own loopback door,
 * opened here the first time (an ephemeral 127.0.0.1 port, so it never
 * contends with a daemon on the manifest's port). 0 with the reason.
 */
static int
view_session_port(
    struct App* app,
    char* why,
    int why_size)
{
    struct ToriRSMapEd* server;

    if( app->cfg.editor_server == BOOTMANIFEST_EDITOR_SERVER_TCP )
        return app->cfg.editor_server_port > 0 ? app->cfg.editor_server_port : TORIRSMAPED_DEFAULT_PORT;
    if( g_embed_listener )
        return ToriRSMapEd_ListenerPort(g_embed_listener);
    server = Editor_HostIsMapEd(&app->editor->host) ? Editor_HostMapEdEmbedServer(&app->editor->host) : NULL;
    if( !server )
    {
        snprintf(why, (size_t)why_size, "this session has no ToriRSMapEd server another window could join");
        return 0;
    }
    g_embed_listener = ToriRSMapEd_ListenerOpen(server, 0, "editor: embedded maped");
    if( !g_embed_listener )
    {
        snprintf(why, (size_t)why_size,
            "the embedded ToriRSMapEd could not open a loopback socket (see the log); "
            "run osrs242-mapeditor-tcp to pop panels out");
        return 0;
    }
    TORIRS_REPORT("editor: the embedded ToriRSMapEd now also listens on 127.0.0.1:%d for popped-out windows\n",
        ToriRSMapEd_ListenerPort(g_embed_listener));
    return ToriRSMapEd_ListenerPort(g_embed_listener);
}
#endif

int
EditorView_AppSpawnModule(
    struct App* app,
    const char* module,
    const struct EditorViewCamera* camera,
    char* why,
    int why_size)
{
    uint32_t group;
    /* NULL: a plain viewer window (Open in new view). */
    int const is_viewer = module == NULL;
    struct EditorViewCamera pose;

    assert(app);
    assert(why);
    assert(why_size > 0);
    assert(app->editor);

    why[0] = '\0';
    group = Editor_HostMapEdClientId(&app->editor->host);
    /* No camera (a window with no world yet): the default orbit over the
     * boot square, so the window still loads somewhere sensible. */
    if( camera )
        pose = *camera;
    else
    {
        memset(&pose, 0, sizeof(pose));
        pose.x = (app->cfg.spawn_x * 64 + 32) * 128;
        pose.z = (app->cfg.spawn_z * 64 + 32) * 128;
        pose.pitch = 383;
        pose.zoom = 2600;
    }
#if defined(__EMSCRIPTEN__)
    {
        int32_t values[TORIRSMAPED_CAMERA_VALUES];
        char text[96];
        char maped[192];
        if( app->cfg.editor_server != BOOTMANIFEST_EDITOR_SERVER_TCP )
        {
            snprintf(why, (size_t)why_size,
                "a browser tab cannot reach the ToriRSMapEd embedded in this page; "
                "boot the page against a torirsmaped daemon (maped=) to pop panels out");
            return 0;
        }
        EditorView_CameraToValues(&pose, values);
        ToriRSMapEd_CameraFormat(values, text, sizeof(text));
        snprintf(maped,
            sizeof(maped),
            "ws://%s:%d",
            app->cfg.editor_server_host && app->cfg.editor_server_host[0]
                ? app->cfg.editor_server_host
                : "localhost",
            app->cfg.editor_server_port > 0 ? app->cfg.editor_server_port
                                            : TORIRSMAPED_DEFAULT_PORT);
        if( !editor_view_web_spawn(text, group, maped, is_viewer ? "" : module) )
        {
            snprintf(why, (size_t)why_size, "the page could not open a tab (allow popups for this site)");
            return 0;
        }
        return 1;
    }
#else
    {
        char url[192];
        char body[512];
        int length;
        int port;
        static int serial;

        /* A server with a SPAWN_VIEW binding starts a plain viewer on ITS
         * host, with its own binary and manifest -- the only way a remote
         * daemon's view can join it, and as good as the local launch for one
         * beside us. SPAWN_VIEW carries a camera and no panel, so a popped-out
         * panel (World included) is always this host's own launch. */
        if( is_viewer && (app->editor->capabilities & TORIRSMAPED_CAP_SPAWN_VIEW) )
        {
            int32_t values[TORIRSMAPED_CAMERA_VALUES];
            int pid = 0;
            enum EditorHost_Status status;
            EditorView_CameraToValues(&pose, values);
            status = Editor_HostMapEdSpawnView(&app->editor->host, values, &pid);
            if( status == EDITOR_HOST_OK )
            {
                TORIRS_REPORT("editor view: the server started a view (pid %d)\n", pid);
                return 1;
            }
            snprintf(why, (size_t)why_size, "the server refused SPAWN_VIEW (status %d)", (int)status);
            return 0;
        }
        port = view_session_port(app, why, why_size);
        if( port <= 0 )
            return 0;
        snprintf(url,
            sizeof(url),
            "tcp://%s:%d",
            app->cfg.editor_server == BOOTMANIFEST_EDITOR_SERVER_TCP && app->cfg.editor_server_host
                    && app->cfg.editor_server_host[0]
                ? app->cfg.editor_server_host
                : "127.0.0.1",
            port);
        length = EditorView_SpawnBody(url, group, &pose, module, body, (int)sizeof(body));
        assert(length > 0);
        ToriRS_TaskQueue_Add(app->frame_runner.queue,
            CreateTask_PluginLaunch("view", body, length, ++serial, view_spawn_delivered, NULL));
        return 1;
    }
#endif
}

int
EditorView_AppSpawn(
    struct App* app,
    const struct EditorViewCamera* camera)
{
    char why[192];

    assert(app);
    assert(camera);
    if( EditorView_AppSpawnModule(app, NULL, camera, why, (int)sizeof(why)) )
        return 1;
    TORIRS_REPORT("editor view: %s\n", why);
    return 0;
}

int
EditorView_AppWorldCamera(
    void* user_data,
    int level,
    struct EditorViewCamera* out)
{
    struct App* app = user_data;
    struct EditorViewCamera previous;

    assert(app);
    assert(out);
    assert(level >= 0 && level <= 3);
    if( !app->world || !app->frame_view )
        return 0;
    memset(&previous, 0, sizeof(previous));
    previous.level = level;
    previous.zoom = 1200;
    return view_read_from_app(app, &previous, out);
}

int
EditorView_AppWorldCameraSet(
    void* user_data,
    const struct EditorViewCamera* pose)
{
    struct App* app = user_data;

    assert(app);
    assert(pose);
    assert(pose->level >= 0 && pose->level <= 3);
    return view_apply_to_app(app, pose);
}

int
EditorView_AppSpawnPanel(
    void* user_data,
    const char* module,
    const struct EditorViewCamera* camera,
    char* why,
    int why_size)
{
    struct App* app = user_data;

    assert(app);
    return EditorView_AppSpawnModule(app, module, camera, why, why_size);
}

void
EditorView_AppShutdown(struct App* app)
{
    assert(app);
    (void)app; /* one listener per process, like the view */
    ToriRSMapEd_ListenerClose(g_embed_listener);
    g_embed_listener = NULL;
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
