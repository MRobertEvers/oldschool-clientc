/*
 * ToriRSMapEd's front door: a loopback TCP daemon serving one content tree.
 *
 *   src/build/torirsmaped [port] [--content-dir <dir>] [--repo-root <dir>]
 *                         [--view-binary <torirs> --view-manifest <editor.ini>]
 *                         [--live-dir <dir>]
 *
 * Defaults: port 43610 (TORIRSMAPED_DEFAULT_PORT), content dir
 * $TORIRSMAPED_CONTENT_DIR or OSRS-Content/osrs-content, repo root
 * $TORIRSMAPED_REPO_ROOT or "." (pass --repo-root '' to disable baking).
 *
 * Serves several connections at once — that is the point of the daemon. A
 * client is one stateful connection and a session may be spread across
 * many: a world-viewer process, a controller process with no world at all,
 * a late-joining second viewer. The server relays document facts and shared
 * state between them; the writer arbitration is the server's own (it holds
 * the tree lock and its document is the only one that matters), so
 * connections never contend with each other over files.
 *
 * SPAWN_VIEW (torirs_maped_view.h): with --view-binary and --view-manifest
 * (or $TORIRSMAPED_VIEW_BINARY / $TORIRSMAPED_VIEW_MANIFEST) the daemon binds
 * the launch service's `view` verb (platform/launch_sessions.h), so a client
 * can ask THIS host to open a viewer window joined to its Client:
 * `<torirs> --manifest <editor.ini> --editor-view --maped
 * tcp://127.0.0.1:<port> --maped-group <group> --camera <pose>`. Without
 * both, SPAWN_VIEW answers UNSUPPORTED and FACT_HELLO leaves the capability
 * bit clear. Views end with the daemon.
 *
 * The socket is bound BEFORE the tree is opened — ToriRSServer's rule, kept
 * even though this boot is fast today: a client launched beside the daemon
 * dials immediately, and a connection refused is terminal for it, while a
 * connection accepted late is just slow.
 */

/* realpath() for --view-binary; glibc hides it under a strict -std=c11. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "torirs_maped.h"
#include "torirs_maped_live.h"
#include "torirs_maped_accounts.h"
#include "torirs_maped_net.h"
#include "torirs_maped_view.h"

#include "mem/torirs_mem.h"
#include "platform/launch_sessions.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>


/* ---- SPAWN_VIEW: the launch service's `view` verb ------------------------ */

/** The daemon's viewer launcher: one per process, like the server itself. */
struct daemon_views
{
    struct LaunchSessions* table;
    int port;
};
static struct daemon_views g_views;

static enum EditorHost_Status
daemon_spawn_view(
    void* ctx,
    uint32_t group,
    const int32_t* camera,
    int* out_pid)
{
    struct daemon_views* views = ctx;
    char pose[96];
    char body[256];
    char* answer;
    char const* pid_line;
    int size = 0;
    enum EditorHost_Status status = EDITOR_HOST_OK;

    ToriRSMapEd_CameraFormat(camera, pose, sizeof(pose));
    snprintf(
        body,
        sizeof(body),
        "maped=tcp://127.0.0.1:%d\ngroup=%u\ncamera=%s\n",
        views->port,
        (unsigned)group,
        pose);
    answer = LaunchSessions_Answer(views->table, "view", body, (int)strlen(body), "local", &size);
    pid_line = strstr(answer, "\npid=");
    if( strncmp(answer, "ok\n", 3) != 0 || !pid_line )
    {
        fprintf(stderr, "torirsmaped: SPAWN_VIEW for Client %u: %s", (unsigned)group, answer);
        status = EDITOR_HOST_IO_ERROR;
    }
    else
        *out_pid = atoi(pid_line + 5);
    ToriMem_Free(ToriMem_Net(), answer); /* LaunchSessions_Answer's */
    return status;
}

static void
daemon_views_free(void)
{
    LaunchSessions_Free(g_views.table);
    g_views.table = NULL;
}

/** Binds SPAWN_VIEW when both paths are known; says why not otherwise. */
static void
daemon_views_bind(
    struct ToriRSMapEd* maped,
    const char* view_binary,
    const char* view_manifest,
    int port)
{
    static char binary[PATH_MAX];
    struct LaunchSessions_Config config;
    struct ToriRSMapEdViewSpawner spawner;
    char const* reason = LaunchSessions_UnsupportedReason();

    if( !view_binary || !view_binary[0] || !view_manifest || !view_manifest[0] )
    {
        fprintf(stderr,
            "torirsmaped: no --view-binary/--view-manifest: SPAWN_VIEW is not offered\n");
        return;
    }
    if( reason || !LaunchSessions_SystemSpawner() )
    {
        fprintf(stderr, "torirsmaped: SPAWN_VIEW is not offered: %s\n",
            reason ? reason : "no spawner on this platform");
        return;
    }
    /* Absolute, because a view's working directory is the daemon's and the
     * flag may have been relative to the shell that started it. */
    if( !realpath(view_binary, binary) )
    {
        fprintf(stderr, "torirsmaped: --view-binary %s: %s — SPAWN_VIEW is not offered\n",
            view_binary, strerror(errno));
        return;
    }

    memset(&config, 0, sizeof(config));
    config.binary_path = binary;
    config.manifest_path = view_manifest;
    config.launch_directory = "build/launch";
    config.spawner = LaunchSessions_SystemSpawner();
    config.clock_ms = LaunchSessions_MonotonicMs;
    g_views.table = LaunchSessions_New(&config);
    g_views.port = port;
    atexit(daemon_views_free);

    spawner.ctx = &g_views;
    spawner.spawn = daemon_spawn_view;
    ToriRSMapEd_SetViewSpawner(maped, &spawner);
    fprintf(stderr, "torirsmaped: SPAWN_VIEW opens %s --manifest %s --editor-view\n",
        binary, view_manifest);
}

/** The reactor: accept up to TORIRSMAPED_SESSION_MAX connections and pump
 *  whichever have bytes. One thread — the sessions never block, so neither
 *  does this loop, and a broadcast provoked by one connection's intent is
 *  sent to the others from inside that connection's pump. */
/* SIGTERM / SIGINT: leave the loop so ToriRSMapEd_Close runs -- it stops a
 * launched live lane (torirs_maped_live.h), which would otherwise outlive the
 * daemon as an orphan holding the lane's ports. */
static volatile sig_atomic_t g_daemon_stop;

static void
daemon_on_stop(int signal_number)
{
    (void)signal_number;
    g_daemon_stop = 1;
}

static void
serve_forever(
    struct ToriRSMapEd* maped,
    struct ToriRSMapEdListener* listener)
{
    while( !g_daemon_stop )
    {
        /* A build, a RUN or a connecting live link is advanced by the
         * service pass below: wake often while one is in flight. */
        if( ToriRSMapEd_ListenerPoll(listener, ToriRSMapEd_Busy(maped) ? 20 : 500) < 0 )
            return;
        /* At most every half second (the poll timeout): views that exited. */
        if( g_views.table )
            (void)LaunchSessions_Reap(g_views.table);
        /* Builds, RUN and the live link (torirs_maped_live.h): the sessions
         * only pump when their sockets are readable, so a build streaming
         * lines while every client is idle is advanced here. */
        ToriRSMapEd_Service(maped);
        /* Logins (queued account cheats), checkpoints landing
         * (torirs_maped_accounts.h). */
        ToriRSMapEd_AccountsService(maped);
    }
}

int
main(
    int argc,
    char** argv)
{
    const char* content_dir = getenv("TORIRSMAPED_CONTENT_DIR");
    const char* repo_root = getenv("TORIRSMAPED_REPO_ROOT");
    const char* view_binary = getenv("TORIRSMAPED_VIEW_BINARY");
    const char* view_manifest = getenv("TORIRSMAPED_VIEW_MANIFEST");
    const char* live_dir = NULL;
    const char* lane_name = getenv("TORIRSMAPED_LANE");
    int port = TORIRSMAPED_DEFAULT_PORT;
    struct ToriRSMapEdListener* listener;
    static struct ToriRSMapEd maped;

    if( !content_dir )
        content_dir = "OSRS-Content/osrs-content";
    if( !repo_root )
        repo_root = ".";

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--content-dir") == 0 && i + 1 < argc )
            content_dir = argv[++i];
        else if( strcmp(argv[i], "--repo-root") == 0 && i + 1 < argc )
            repo_root = argv[++i];
        else if( strcmp(argv[i], "--view-binary") == 0 && i + 1 < argc )
            view_binary = argv[++i];
        else if( strcmp(argv[i], "--view-manifest") == 0 && i + 1 < argc )
            view_manifest = argv[++i];
        else if( strcmp(argv[i], "--live-dir") == 0 && i + 1 < argc )
            live_dir = argv[++i];
        else if( strcmp(argv[i], "--lane") == 0 && i + 1 < argc )
            lane_name = argv[++i];
        else if( argv[i][0] >= '0' && argv[i][0] <= '9' )
            port = atoi(argv[i]);
        else
        {
            fprintf(
                stderr,
                "usage: torirsmaped [port] [--content-dir <dir>] [--repo-root <dir>] "
                "[--view-binary <torirs> --view-manifest <editor.ini>] [--live-dir <dir>]\n");
            return 1;
        }
    }
    if( repo_root[0] == '\0' )
        repo_root = NULL;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, daemon_on_stop);
    signal(SIGINT, daemon_on_stop);

    /* Bound before the tree is opened (the header's rule); the listener
     * touches the server only from its first poll. */
    listener = ToriRSMapEd_ListenerOpen(&maped, port, "torirsmaped");
    if( !listener )
        return 1;

    /* --live-dir beats $TORIRSMAPED_LIVE_DIR (which Open reads): BUILD, RUN
     * and the live link, everything a build writes under that dir. */
    if( live_dir )
        setenv("TORIRSMAPED_LIVE_DIR", live_dir, 1);
    /* A lane switched in the editor (build/lanes/lane.ini, the Lanes
     * panel's Switch and Scratch lane) wins over --lane: this daemon cannot
     * re-point its lane while it runs, so the switch waits for this boot. */
    if( repo_root )
    {
        static char boot_lane[TORIRSMAPED_LANE_NAME_MAX + 8];
        char note[256];
        if( ToriRSMapEdLanes_BootLane(repo_root, content_dir, lane_name, boot_lane, sizeof(boot_lane), note,
                sizeof(note)) )
            lane_name = boot_lane;
        if( note[0] )
            fprintf(stderr, "torirsmaped: %s\n", note);
    }
    /* --lane <name> (or $TORIRSMAPED_LANE): serve the lane's mirror of
     * --content-dir and build into the lane's live dir (torirs_maped_lanes.h);
     * "main" is --content-dir itself. */
    if( lane_name && lane_name[0] && strcmp(lane_name, "main") != 0 )
    {
        char why[512];
        if( !ToriRSMapEd_OpenLane(&maped, content_dir, repo_root, lane_name, why, sizeof(why)) )
        {
            fprintf(stderr, "torirsmaped: lane '%s': %s\n", lane_name, why);
            return 1;
        }
        fprintf(stderr, "torirsmaped: lane '%s': %s, mirrors %s (live %s)\n", maped.lane.name, maped.lane.content,
            maped.lane.base, maped.lane.live);
    }
    else
        ToriRSMapEd_Open(&maped, content_dir, repo_root);
    daemon_views_bind(&maped, view_binary, view_manifest, port);

    fprintf(
        stderr,
        "torirsmaped: serving %s on 127.0.0.1:%d%s\n",
        content_dir,
        port,
        repo_root ? "" : " (baking disabled)");

    serve_forever(&maped, listener);

    ToriRSMapEd_ListenerClose(listener);
    ToriRSMapEd_Close(&maped);
    return 0;
}
