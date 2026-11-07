/**
 * The Live panel on the real chrome model, over a raw connection to a real
 * ToriRSMapEd (test BuildRunner, native live link) and the mock control-link
 * peer (editor/live/test/mock_control_peer.py):
 *
 *   - registered hidden, shown by its chrome panel like any View-menu panel;
 *   - Attach with the address field -> the status line reads attached, the
 *     cheat and teleport buttons enable;
 *   - Follow (player + varps) -> LIVE_SUBSCRIBE at the peer, the vars list
 *     shows the values, STATE_LIVE_FOCUS published with the name;
 *   - Teleport to selection -> `tele level_mx_mz_lx_lz` for the player;
 *   - the cheat line -> the peer runs it, the line is cleared;
 *   - RUN -> the run line reads done, the log lists the build lines, and the
 *     play line follows the lane's play client (a fake launcher): RUN starts
 *     it, the peer's player list logs it in, the next RUN reads "reloaded in
 *     the running client"; Stop and Play;
 *   - Build packs -> FACT_BUILD_DONE folded;
 *   - STATE_LIVE_FOCUS from another window fills the player field;
 *   - Detach -> detached, cheat disabled.
 *
 *   live_panel_test <scratch-dir> <peer-script> <repo-root>
 */

#include "editor/editor_ui.h"
#include "editor/live/live_client.h"
#include "editor/live/live_panel.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_live.h"
#include "ui/uitree_debug_overlay.h"
#include "platform/launch_sessions.h"
#include "torirsmaped/torirs_maped_build.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    g_checks++;
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

/* ---- a play launcher with no process: the panel only sees its facts ---------- */

static struct
{
    int running;
    int starts;
    int stops;
} g_fake;

static int
fake_ready(void* ctx)
{
    (void)ctx;
    return 1;
}

static enum EditorHost_Status
fake_start(
    void* ctx,
    const struct ToriRSMapEdPlaySpec* spec,
    int* out_pid,
    char* why,
    size_t why_size)
{
    (void)ctx;
    (void)spec;
    (void)why;
    (void)why_size;
    g_fake.running = 1;
    g_fake.starts++;
    *out_pid = 90000 + g_fake.starts;
    return EDITOR_HOST_OK;
}

static int
fake_alive(
    void* ctx,
    int* out_status)
{
    (void)ctx;
    *out_status = 0;
    return g_fake.running;
}

static void
fake_stop(void* ctx)
{
    (void)ctx;
    if( g_fake.running )
        g_fake.stops++;
    g_fake.running = 0;
}

static struct ToriRSChrome g_chrome;
static struct EditorUi g_ui;
static struct ToriRSMapEd* g_maped;
static char g_peer_log[1024];

/* ---- a raw connection the panel's transport rides ------------------------------ */

static struct
{
    struct ToriRSMapEdBuf to_server;
    struct ToriRSMapEdBuf to_client;
    struct ToriRSMapEdSession session;
    int published;
    uint32_t published_key;
    int32_t published_values[TORIRSMAPED_STATE_VALUES_MAX];
    int published_count;
} g_conn;

static int
conn_recv(
    void* ctx,
    uint8_t* dst,
    int max)
{
    (void)ctx;
    return ToriRSMapEd_BufRead(&g_conn.to_server, dst, max);
}

static int
conn_send(
    void* ctx,
    const uint8_t* src,
    int len)
{
    (void)ctx;
    return ToriRSMapEd_BufWrite(&g_conn.to_client, src, len);
}

static int
transport_send(
    void* user,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    uint8_t header[8];
    (void)user;
    ToriRSMapEd_WriteU32(header, type);
    ToriRSMapEd_WriteU32(header + 4, length);
    ToriRSMapEd_BufWrite(&g_conn.to_server, header, 8);
    if( length )
        ToriRSMapEd_BufWrite(&g_conn.to_server, payload, (int)length);
    return 1;
}

/** The next live-editor fact; every other frame (FACT_HELLO, FACT_STATUS,
 *  FACT_SAVED...) is dropped, as the host's DrainFacts would route it
 *  elsewhere. */
static int
transport_take(
    void* user,
    uint32_t* out_type,
    uint8_t** out_body,
    uint32_t* out_length)
{
    (void)user;
    while( ToriRSMapEd_BufAvailable(&g_conn.to_client) >= 8 )
    {
        const uint8_t* head = ToriRSMapEd_BufPeek(&g_conn.to_client);
        uint32_t type = ToriRSMapEd_ReadU32(head);
        uint32_t length = ToriRSMapEd_ReadU32(head + 4);
        if( ToriRSMapEd_BufAvailable(&g_conn.to_client) < 8 + (int)length )
            return 0;
        if( Editor_LiveIsFact(type) )
        {
            uint8_t* body = malloc(length ? length : 1);
            assert(body);
            memcpy(body, head + 8, length);
            ToriRSMapEd_BufConsume(&g_conn.to_client, 8 + (int)length);
            *out_type = type;
            *out_body = body;
            *out_length = length;
            return 1;
        }
        ToriRSMapEd_BufConsume(&g_conn.to_client, 8 + (int)length);
    }
    return 0;
}

static void
transport_release(
    void* user,
    uint8_t* body)
{
    (void)user;
    free(body);
}

static int
transport_publish(
    void* user,
    uint32_t key,
    const int32_t* values,
    int count)
{
    (void)user;
    g_conn.published++;
    g_conn.published_key = key;
    g_conn.published_count = count;
    memcpy(g_conn.published_values, values, (size_t)count * sizeof(int32_t));
    return 1;
}

/* ---- frames --------------------------------------------------------------------- */

static int64_t
now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void
frame(void)
{
    ToriRSMapEd_SessionPump(&g_conn.session);
    ToriRSChrome_Build(&g_chrome);
    Editor_UiTick(&g_ui);
    ToriRSChrome_Build(&g_chrome);
}

static void
frames_for(int ms)
{
    int64_t const deadline = now_ms() + ms;
    while( now_ms() < deadline )
    {
        frame();
        usleep(2000);
    }
}

static int
find_panel(const char* title)
{
    for( int i = 0; i < g_chrome.panel_count; i++ )
        if( strcmp(g_chrome.panels[i].title, title) == 0 )
            return i;
    return -1;
}

static int
find_widget(
    int panel,
    int kind,
    const char* caption)
{
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[i];
        if( w->panel != panel || w->kind != kind )
            continue;
        if( !caption || strcmp(w->label, caption) == 0 || strcmp(w->text, caption) == 0 )
            return i;
    }
    return -1;
}

static void
press(int widget)
{
    assert(widget >= 0);
    g_chrome.activated = widget;
    frame();
}

static int
label_has(
    int widget,
    const char* needle)
{
    return widget >= 0 && strstr(ToriRSChrome_Text(&g_chrome, widget), needle) != NULL;
}

static int
wait_label(
    int widget,
    const char* needle,
    int timeout_ms)
{
    int64_t const deadline = now_ms() + timeout_ms;
    while( now_ms() < deadline )
    {
        frame();
        if( label_has(widget, needle) )
            return 1;
        usleep(2000);
    }
    fprintf(stderr, "  (label reads \"%s\")\n", widget >= 0 ? ToriRSChrome_Text(&g_chrome, widget) : "?");
    return 0;
}

static int
peer_has(const char* needle)
{
    FILE* f = fopen(g_peer_log, "rb");
    char line[4096];
    int found = 0;
    if( !f )
        return 0;
    while( fgets(line, sizeof(line), f) )
        if( strstr(line, needle) )
            found = 1;
    fclose(f);
    return found;
}

static int
wait_peer(
    const char* needle,
    int timeout_ms)
{
    int64_t const deadline = now_ms() + timeout_ms;
    while( now_ms() < deadline )
    {
        frame();
        if( peer_has(needle) )
            return 1;
        usleep(5000);
    }
    return 0;
}

static int
free_port(void)
{
    struct sockaddr_in addr;
    socklen_t length = sizeof(addr);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int port;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if( bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
    {
        close(fd);
        return 0;
    }
    getsockname(fd, (struct sockaddr*)&addr, &length);
    port = ntohs(addr.sin_port);
    close(fd);
    return port;
}

/** 1 when a row on screen in VLIST `list` contains `needle`. */
static int
vlist_has(
    int list,
    const char* needle)
{
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[list];
    for( int i = 0; i < w->rows; i++ )
    {
        const struct ToriRSChromeVRow* row = &g_chrome.vrows[w->vrow_first + i];
        if( strstr(row->text, needle) )
            return 1;
    }
    return 0;
}

int
main(
    int argc,
    char** argv)
{
    const char* scratch;
    const char* peer_script;
    const char* repo_root;
    char content[1024];
    char live[1024];
    char text[256];
    struct ToriRSMapEdLane lane;
    struct ToriRSMapEdBuildMock* mock;
    struct ToriRSMapEdBuildRunner runner;
    struct ToriRSMapEdLiveLink link;
    struct ToriRSMapEdTransport server_side;
    struct Editor_LiveTransport transport;
    int port;
    pid_t peer;
    int panel;
    int w_status;
    int w_run_line;
    int w_address;
    int w_attach;
    int w_detach;
    int w_run;
    int w_packs;
    int w_player;
    int w_varps;
    int w_follow;
    int w_cheat_input;
    int w_cheat;
    int w_tele;
    int w_vars;
    int w_log;
    int w_play_line;
    int w_play;
    int w_play_stop;
    const struct Editor_LiveModel* model;

    if( argc != 4 )
    {
        fprintf(stderr, "usage: live_panel_test <scratch> <peer-script> <repo-root>\n");
        return 2;
    }
    scratch = argv[1];
    peer_script = argv[2];
    repo_root = argv[3];
    signal(SIGPIPE, SIG_IGN);
    ToriMem_Init();
    /* The editor server starts its children through the host's spawner, as
     * the client and torirsmaped hand it theirs (torirs_maped_build.h). */
    ToriRSMapEd_SetSpawner(LaunchSessions_SystemSpawner());
    unsetenv("TORIRSMAPED_LIVE_DIR");
    unsetenv("MOCK_PEER_TICKET");
    snprintf(content, sizeof(content), "%s/content", scratch);
    snprintf(live, sizeof(live), "%s/live", scratch);
    snprintf(g_peer_log, sizeof(g_peer_log), "%s/peer.log", scratch);
    setenv("MOCK_PEER_LOG", g_peer_log, 1);

    /* the server */
    g_maped = calloc(1, sizeof(*g_maped));
    assert(g_maped);
    ToriRSMapEd_Open(g_maped, content, repo_root);
    ToriRSMapEd_LaneDefaults(&lane, repo_root, content, live);
    check(ToriRSMapEd_SetLane(g_maped, &lane), "lane bound");
    mock = calloc(1, sizeof(*mock));
    assert(mock);
    mock->lane = lane;
    {
        static const char* const lines[] = { "compiled  server/scripts/x.rs2 — edited", "== packs: ok" };
        static const uint32_t archives[] = { 2, 16 };
        mock->scripts[TORIRSMAPED_BUILD_PACKS].lines = lines;
        mock->scripts[TORIRSMAPED_BUILD_PACKS].line_count = 2;
        mock->scripts[TORIRSMAPED_BUILD_CLIENT_CACHE].archives = archives;
        mock->scripts[TORIRSMAPED_BUILD_CLIENT_CACHE].archive_count = 1;
    }
    ToriRSMapEd_BuildRunnerMock(&runner, mock);
    ToriRSMapEd_SetBuildRunner(g_maped, &runner);
    check(ToriRSMapEd_LiveLinkNative(&link, &lane), "native link");
    ToriRSMapEd_SetLiveLink(g_maped, &link);
    {
        struct ToriRSMapEdPlayLauncher fake;
        memset(&fake, 0, sizeof(fake));
        fake.ready = fake_ready;
        fake.start = fake_start;
        fake.alive = fake_alive;
        fake.stop = fake_stop;
        ToriRSMapEd_SetPlayLauncher(g_maped, &fake);
    }
    memset(&g_conn, 0, sizeof(g_conn));
    server_side.ctx = NULL;
    server_side.recv = conn_recv;
    server_side.send = conn_send;
    ToriRSMapEd_SessionInit(&g_conn.session, g_maped, &server_side);
    {
        uint8_t hello[12];
        ToriRSMapEd_WriteU32(hello, TORIRSMAPED_PROTO_VERSION);
        ToriRSMapEd_WriteU32(hello + 4, TORIRSMAPED_ROLE_CONTROLLER);
        ToriRSMapEd_WriteU32(hello + 8, 0);
        transport_send(NULL, TORIRSMAPED_REQ_HELLO, hello, 12);
    }

    /* the peer */
    port = free_port();
    {
        char port_text[16];
        char* peer_argv[] = { (char*)"python3", (char*)peer_script, port_text, NULL };
        snprintf(port_text, sizeof(port_text), "%d", port);
        check(posix_spawnp(&peer, "python3", NULL, NULL, peer_argv, environ) == 0, "the mock peer started");
    }

    /* the panel */
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1280, 800);
    Editor_UiInit(&g_ui, &g_chrome, NULL);
    transport.user = NULL;
    transport.send = transport_send;
    transport.take = transport_take;
    transport.release = transport_release;
    transport.publish = transport_publish;
    Editor_Live_RegisterWith(&g_ui, &transport);
    check(Editor_UiFindPanel(&g_ui, "live") != NULL, "registered as \"live\"");
    Editor_UiSetVisible(&g_ui, 1);
    panel = find_panel("Live");
    check(panel >= 0 && !g_chrome.panels[panel].visible, "the Live window starts hidden");
    ToriRSChrome_PanelSetVisible(&g_chrome, panel, 1);
    frame();
    model = Editor_LivePanelModel(&g_ui);
    check(model != NULL, "the panel's model");

    w_status = find_widget(panel, TORIRS_CHROME_W_LABEL, "detached");
    w_run_line = w_status + 1;
    w_address = find_widget(panel, TORIRS_CHROME_W_TEXTINPUT, "Address");
    w_attach = find_widget(panel, TORIRS_CHROME_W_BUTTON, "Attach");
    w_detach = find_widget(panel, TORIRS_CHROME_W_BUTTON, "Detach");
    w_run = find_widget(panel, TORIRS_CHROME_W_BUTTON, "RUN");
    w_packs = find_widget(panel, TORIRS_CHROME_W_BUTTON, "Build packs");
    w_player = find_widget(panel, TORIRS_CHROME_W_TEXTINPUT, "Player");
    w_varps = find_widget(panel, TORIRS_CHROME_W_TEXTINPUT, "Varps");
    w_follow = find_widget(panel, TORIRS_CHROME_W_BUTTON, "Follow");
    w_cheat_input = find_widget(panel, TORIRS_CHROME_W_TEXTINPUT, "Cheat");
    w_cheat = find_widget(panel, TORIRS_CHROME_W_BUTTON, "Send");
    w_tele = find_widget(panel, TORIRS_CHROME_W_BUTTON, "Teleport to selection");
    w_vars = find_widget(panel, TORIRS_CHROME_W_VLIST, "Vars");
    w_log = find_widget(panel, TORIRS_CHROME_W_VLIST, "Log");
    w_play_line = find_widget(panel, TORIRS_CHROME_W_LABEL, "play: not started");
    w_play = find_widget(panel, TORIRS_CHROME_W_BUTTON, "Play");
    w_play_stop = find_widget(panel, TORIRS_CHROME_W_BUTTON, "Stop");
    check(w_play_line >= 0 && w_play >= 0 && w_play_stop >= 0, "the play line, Play and Stop are on the panel");
    check(ToriRSChrome_Disabled(&g_chrome, w_play_stop), "no play client: Stop is disabled");
    check(w_status >= 0 && w_address >= 0 && w_attach >= 0 && w_detach >= 0 && w_run >= 0 && w_packs >= 0
              && w_player >= 0 && w_varps >= 0 && w_follow >= 0 && w_cheat_input >= 0 && w_cheat >= 0
              && w_tele >= 0 && w_vars >= 0 && w_log >= 0,
        "every control is on the panel");
    check(ToriRSChrome_Disabled(&g_chrome, w_cheat) && ToriRSChrome_Disabled(&g_chrome, w_tele),
        "detached: cheat and teleport are disabled");

    /* attach */
    check(wait_peer("\"listening\"", 10000), "the peer listens");
    snprintf(text, sizeof(text), "127.0.0.1:%d", port);
    ToriRSChrome_SetText(&g_chrome, w_address, text);
    press(w_attach);
    check(wait_label(w_status, "attached", 10000) && label_has(w_status, "mockworld"),
        "Attach: the status line reads attached to mockworld");
    check(!ToriRSChrome_Disabled(&g_chrome, w_cheat) && !ToriRSChrome_Disabled(&g_chrome, w_tele)
              && !ToriRSChrome_Disabled(&g_chrome, w_detach),
        "attached: cheat, teleport and detach enable");

    /* follow */
    ToriRSChrome_SetText(&g_chrome, w_player, "Zezima");
    ToriRSChrome_SetText(&g_chrome, w_varps, "281, 1048");
    press(w_follow);
    check(wait_peer("\"varps\": [281, 1048]", 5000), "Follow: LIVE_SUBSCRIBE reached the peer");
    frames_for(100);
    check(model->var_count == 2 && vlist_has(w_vars, "varp 281 = 2810"), "Follow: the vars list shows varp 281 = 2810");
    {
        char name[32];
        check(g_conn.published > 0 && g_conn.published_key == TORIRSMAPED_STATE_LIVE_FOCUS
                  && ToriRSMapEd_LiveFocusName(g_conn.published_values, g_conn.published_count, name, sizeof(name))
                  && strcmp(name, "Zezima") == 0,
            "Follow: STATE_LIVE_FOCUS published for Zezima");
    }

    /* teleport to the selection */
    g_ui.tools.tile_valid = 1;
    g_ui.tools.tile_x = 3222;
    g_ui.tools.tile_z = 3218;
    g_ui.tools.tile_level = 0;
    press(w_tele);
    check(wait_peer("\"player\": \"Zezima\", \"line\": \"tele 0_50_50_22_18\"", 5000),
        "Teleport: ::tele 0_50_50_22_18 for Zezima");

    /* the cheat line */
    ToriRSChrome_SetText(&g_chrome, w_cheat_input, "::xp 99");
    press(w_cheat);
    check(wait_peer("\"line\": \"xp 99\"", 5000), "Cheat: the peer ran xp 99");
    check(strcmp(ToriRSChrome_Text(&g_chrome, w_cheat_input), "") == 0, "Cheat: the line is cleared");

    /* RUN */
    press(w_run);
    check(wait_label(w_run_line, "RUN done", 10000), "RUN: the run line reads done");
    check(peer_has("\"req\": \"reload\""), "RUN: the peer was asked to reload");
    frames_for(50);
    check(model->log_count > 3 && vlist_has(w_log, "RUN done"), "RUN: the log lists the outcome");
    check(label_has(w_run_line, "started the play client") && g_fake.starts == 1,
        "RUN: with no play client, RUN started one (the run line says so)");
    check(wait_label(w_play_line, "play: starting", 3000) && !ToriRSChrome_Disabled(&g_chrome, w_play_stop),
        "RUN: the play line reads starting; Stop enables");

    /* the play client logs in: the server's list says so */
    ToriRSChrome_SetText(&g_chrome, w_cheat_input, "join maped43620");
    press(w_cheat);
    check(wait_label(w_play_line, "logged in as maped43620 at m50_50", 8000),
        "play: the line reads logged in as maped43620 at m50_50");

    /* a RUN with it on the server: reloaded in place */
    press(w_run);
    frames_for(20);
    check(wait_label(w_run_line, "reloaded in the running client", 10000) && g_fake.starts == 1,
        "RUN again: \"reloaded in the running client\", nothing started");

    /* it logs out (the process runs on), Stop, then Play */
    ToriRSChrome_SetText(&g_chrome, w_cheat_input, "leave maped43620");
    press(w_cheat);
    check(wait_label(w_play_line, "logged out", 8000), "play: off the server's list -> logged out");
    press(w_play_stop);
    check(wait_label(w_play_line, "play: not started", 3000) && g_fake.stops == 1 && !g_fake.running,
        "Stop: the play client stopped");
    press(w_play);
    check(wait_label(w_play_line, "play: starting", 3000) && g_fake.starts == 2, "Play: started again");

    /* Build packs */
    {
        int const before = model->build_serial;
        press(w_packs);
        frames_for(50);
        check(model->build_serial == before + 1 && model->last_build_status == 0
                  && model->last_build_what == TORIRSMAPED_BUILD_PACKS,
            "Build packs: FACT_BUILD_DONE folded into the panel");
    }

    /* another window followed someone */
    {
        int32_t values[TORIRSMAPED_LIVE_FOCUS_VALUES];
        ToriRSMapEd_LiveFocusPack("Lynx Titan", -1, -1, -1, values);
        Editor_UiApplyState(&g_ui, TORIRSMAPED_STATE_LIVE_FOCUS, values, TORIRSMAPED_LIVE_FOCUS_VALUES);
        frame();
        check(strcmp(ToriRSChrome_Text(&g_chrome, w_player), "Lynx Titan") == 0,
            "STATE_LIVE_FOCUS from another window fills the player field");
    }

    /* detach */
    press(w_detach);
    check(wait_label(w_status, "detached", 3000), "Detach: the status line reads detached");
    check(ToriRSChrome_Disabled(&g_chrome, w_cheat), "detached: cheat disabled again");
    check(wait_peer("\"req\": \"bye\"", 3000), "Detach: the peer heard BYE");

    Editor_UiFree(&g_ui);
    ToriRSMapEd_SessionFree(&g_conn.session);
    ToriRSMapEd_Close(g_maped);
    free(g_maped);
    free(mock);
    kill(peer, SIGTERM);
    waitpid(peer, NULL, 0);
    printf("live_panel_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
