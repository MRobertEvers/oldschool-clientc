/**
 * BUILD, RUN and the live link, end to end on a real ToriRSMapEd:
 *
 *   - BUILD streams FACT_BUILD_LINE then FACT_BUILD_DONE to every
 *     connection, and the runner was asked for exactly the make line the
 *     lane names (the TEST runner records it; nothing is built);
 *   - RUN's sequencing against a MOCK CONTROL-LINK PEER (a python game
 *     server speaking brief section 4 over real TCP): save -> packs -> client
 *     cache -> CTL_RELOAD naming the saved square, the saved spawn file, the
 *     client build's archives and the lane's script manifest ->
 *     FACT_RUN_DONE -> FACT_LIVE_STATUS, in that order on the wire;
 *   - RUN stops at the FIRST failure: a failed pack build starts no client
 *     build and sends no reload; a failed client build sends no reload; a
 *     failed reload keeps everything pending for the next RUN; a link that
 *     drops mid-reload stops RUN;
 *   - the live link: attach, a gate ticket refused, cheat lines, var
 *     subscriptions, detach, and LAUNCH (the peer started as the lane's
 *     torirsserver with the lane's environment, a fake js5_server beside it,
 *     both stopped on detach);
 *   - the lane refusing a live dir inside the content tree, the command
 *     formatter, the client.changed parser, the map list, STATE_LIVE_FOCUS;
 *   - the client model (editor/live/live_client.h) folding every fact;
 *   - the PLAY CLIENT (torirs_maped_live.h): the native launcher on a fake
 *     client binary (its manifest, the fixture save, its environment), and
 *     RUN's play step as a state machine against the mock peer's player list:
 *     lane down -> launched and started; online -> "reloaded in the running
 *     client", no restart; logged out -> restarted; died -> restarted; Play /
 *     Stop; a browser window; detach stops it; a lane that cannot launch says
 *     "Launch the lane first"; a server that does not report players.
 *
 *   live_editor_test <scratch-dir> <peer-script> <fake-server-script> <repo-root> <fake-play-script>
 *
 * The scratch dir holds a content tree with one square; everything written
 * lands under it.
 */

#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/live/live_client.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_live.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
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

/* ---- one raw connection on the server ---------------------------------------- */

struct fact
{
    uint32_t type;
    uint32_t length;
    uint8_t* data;
};

struct conn
{
    struct ToriRSMapEdBuf to_server;
    struct ToriRSMapEdBuf to_client;
    struct ToriRSMapEdSession session;
    struct fact facts[4096];
    int fact_count;
    /** Facts before this index were looked at already. */
    int mark;
    struct Editor_LiveModel model;
};

static int
conn_recv(
    void* ctx,
    uint8_t* dst,
    int max)
{
    struct conn* conn = ctx;
    return ToriRSMapEd_BufRead(&conn->to_server, dst, max);
}

static int
conn_server_send(
    void* ctx,
    const uint8_t* src,
    int len)
{
    struct conn* conn = ctx;
    return ToriRSMapEd_BufWrite(&conn->to_client, src, len);
}

static void
conn_open(
    struct conn* conn,
    struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdTransport transport;
    memset(conn, 0, sizeof(*conn));
    transport.ctx = conn;
    transport.recv = conn_recv;
    transport.send = conn_server_send;
    ToriRSMapEd_SessionInit(&conn->session, maped, &transport);
    Editor_LiveModelInit(&conn->model);
}

static void
conn_send(
    struct conn* conn,
    uint32_t type,
    const struct ToriRSMapEdBuf* payload)
{
    uint8_t header[8];
    int const length = payload ? ToriRSMapEd_BufAvailable(payload) : 0;
    ToriRSMapEd_WriteU32(header, type);
    ToriRSMapEd_WriteU32(header + 4, (uint32_t)length);
    ToriRSMapEd_BufWrite(&conn->to_server, header, 8);
    if( length > 0 )
        ToriRSMapEd_BufWrite(&conn->to_server, ToriRSMapEd_BufPeek(payload), length);
}

static void
conn_parse(struct conn* conn)
{
    while( ToriRSMapEd_BufAvailable(&conn->to_client) >= 8 )
    {
        const uint8_t* head = ToriRSMapEd_BufPeek(&conn->to_client);
        uint32_t type = ToriRSMapEd_ReadU32(head);
        uint32_t length = ToriRSMapEd_ReadU32(head + 4);
        struct fact* fact;
        if( ToriRSMapEd_BufAvailable(&conn->to_client) < 8 + (int)length )
            break;
        assert(conn->fact_count < 4096);
        fact = &conn->facts[conn->fact_count++];
        fact->type = type;
        fact->length = length;
        fact->data = malloc(length ? length : 1);
        assert(fact->data);
        memcpy(fact->data, head + 8, length);
        ToriRSMapEd_BufConsume(&conn->to_client, 8 + (int)length);
        if( Editor_LiveIsFact(type) )
            (void)Editor_LiveModelApply(&conn->model, type, fact->data, length);
    }
}

static struct conn* g_conns[4];
static int g_conn_count;

static void
pump(void)
{
    for( int i = 0; i < g_conn_count; i++ )
    {
        ToriRSMapEd_SessionPump(&g_conns[i]->session);
        conn_parse(g_conns[i]);
    }
}

/** The first fact of `type` at or after conn->mark, or -1. */
static int
find_fact(
    struct conn* conn,
    uint32_t type,
    int from)
{
    for( int i = from; i < conn->fact_count; i++ )
        if( conn->facts[i].type == type )
            return i;
    return -1;
}

static int64_t
now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/** Pump until `conn` holds a fact of `type` after its mark (or time out). */
static int
wait_fact(
    struct conn* conn,
    uint32_t type,
    int timeout_ms)
{
    int64_t const deadline = now_ms() + timeout_ms;
    for( ;; )
    {
        int at;
        pump();
        at = find_fact(conn, type, conn->mark);
        if( at >= 0 )
            return at;
        if( now_ms() > deadline )
            return -1;
        usleep(2000);
    }
}

static void
pump_for(int ms)
{
    int64_t const deadline = now_ms() + ms;
    while( now_ms() < deadline )
    {
        pump();
        usleep(2000);
    }
}

static void
hello(struct conn* conn)
{
    struct ToriRSMapEdBuf payload;
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, TORIRSMAPED_PROTO_VERSION);
    ToriRSMapEd_PutU32(&payload, TORIRSMAPED_ROLE_CONTROLLER);
    ToriRSMapEd_PutU32(&payload, 0);
    conn_send(conn, TORIRSMAPED_REQ_HELLO, &payload);
    ToriRSMapEd_BufFree(&payload);
}

static void
send_live(
    struct conn* conn,
    uint32_t type,
    struct ToriRSMapEdBuf* payload)
{
    conn_send(conn, type, payload);
    ToriRSMapEd_BufFree(payload);
    memset(payload, 0, sizeof(*payload));
}

/* ---- the mock peer's log ---------------------------------------------------------- */

static char g_peer_log[1024];

static char*
slurp(
    const char* path,
    size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;
    *out_size = 0;
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    data = malloc((size_t)size + 1);
    assert(data);
    if( size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_size = (size_t)size;
    return data;
}

/** How many log lines contain `needle`. */
static int
peer_count(const char* needle)
{
    size_t size;
    char* text = slurp(g_peer_log, &size);
    int count = 0;
    if( !text )
        return 0;
    for( char* line = strtok(text, "\n"); line; line = strtok(NULL, "\n") )
        if( strstr(line, needle) )
            count++;
    free(text);
    return count;
}

/** The last log line containing `needle`, copied into `out` (empty when none). */
static void
peer_last(
    const char* needle,
    char* out,
    size_t out_size)
{
    size_t size;
    char* text = slurp(g_peer_log, &size);
    out[0] = '\0';
    if( !text )
        return;
    for( char* line = strtok(text, "\n"); line; line = strtok(NULL, "\n") )
        if( strstr(line, needle) )
            snprintf(out, out_size, "%s", line);
    free(text);
}

static int
wait_peer(
    const char* needle,
    int at_least,
    int timeout_ms)
{
    int64_t const deadline = now_ms() + timeout_ms;
    while( now_ms() < deadline )
    {
        pump();
        if( peer_count(needle) >= at_least )
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
    addr.sin_port = 0;
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

static pid_t
spawn_peer(
    const char* script,
    int port)
{
    char port_text[16];
    char* argv[] = { (char*)"python3", (char*)script, port_text, NULL };
    pid_t pid;
    snprintf(port_text, sizeof(port_text), "%d", port);
    if( posix_spawnp(&pid, "python3", NULL, NULL, argv, environ) != 0 )
        return -1;
    return pid;
}

static void
stop_pid(pid_t pid)
{
    int status;
    if( pid <= 0 )
        return;
    kill(pid, SIGTERM);
    waitpid(pid, &status, 0);
}

static int
pid_alive(int pid)
{
    if( pid <= 0 )
        return 0;
    /* A reaped child is gone; an unreaped zombie would still answer kill. */
    return kill((pid_t)pid, 0) == 0;
}

/* ---- the documents RUN saves --------------------------------------------------------- */

static void
create_square(
    struct conn* conn,
    const char* key)
{
    struct ToriRSMapEdBuf payload;
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, EDITOR_DOC_SQUARE);
    ToriRSMapEd_PutString(&payload, key);
    ToriRSMapEd_PutString(&payload, "");
    ToriRSMapEd_PutU32(&payload, EDITOR_DOC_TEMPLATE_EMPTY);
    ToriRSMapEd_PutString(&payload, "");
    ToriRSMapEd_PutU32(&payload, 0);
    ToriRSMapEd_PutU32(&payload, 0);
    conn_send(conn, TORIRSMAPED_REQ_DOC_CREATE, &payload);
    ToriRSMapEd_BufFree(&payload);
}

static void
add_spawn(
    struct conn* conn,
    int abs_x)
{
    struct ToriRSMapEdBuf payload;
    struct Editor_Cmd command;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, EDITOR_DOC_SPAWN);
    ToriRSMapEd_PutString(&payload, "m50_50");
    conn_send(conn, TORIRSMAPED_REQ_DOC_OPEN, &payload);
    ToriRSMapEd_BufFree(&payload);

    memset(&command, 0, sizeof(command));
    command.doc_kind = EDITOR_DOC_SPAWN;
    snprintf(command.doc_key, sizeof(command.doc_key), "m50_50");
    command.kind = EDITOR_CMD_SPAWN;
    command.has_after = 1;
    snprintf(command.u.spawn.name, sizeof(command.u.spawn.name), "man");
    command.u.spawn.abs_x = abs_x;
    command.u.spawn.abs_z = 3205;
    command.u.spawn.level = 0;
    command.u.spawn.count = 1;
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_CmdEncode(&command, &payload);
    conn_send(conn, TORIRSMAPED_REQ_CMD, &payload);
    ToriRSMapEd_BufFree(&payload);
}

/* ---- facts ---------------------------------------------------------------------------- */

static uint32_t
fact_u32(
    const struct fact* fact,
    int index)
{
    return ToriRSMapEd_ReadU32(fact->data + 4 * index);
}

/** FACT_RUN_DONE's status and step. */
static void
run_done(
    const struct fact* fact,
    uint32_t* status,
    uint32_t* step)
{
    *status = fact_u32(fact, 0);
    *step = fact_u32(fact, 1);
}

static int
wait_run_done(
    struct conn* conn,
    uint32_t* status,
    uint32_t* step)
{
    int at = wait_fact(conn, TORIRSMAPED_FACT_RUN_DONE, 20000);
    *status = 0xffffffffu;
    *step = 0xffffffffu;
    if( at < 0 )
        return -1;
    run_done(&conn->facts[at], status, step);
    return at;
}

static void
mark(struct conn* conn)
{
    pump();
    conn->mark = conn->fact_count;
}

static uint32_t g_changed[16];
static int g_changed_count;

static void
changed_archive(
    void* user,
    uint32_t index,
    uint32_t archive)
{
    (void)user;
    if( g_changed_count < 8 )
    {
        g_changed[2 * g_changed_count] = index;
        g_changed[2 * g_changed_count + 1] = archive;
        g_changed_count++;
    }
}

int
main(
    int argc,
    char** argv)
{
    const char* scratch;
    const char* peer_script;
    const char* fake_script;
    const char* repo_root;
    const char* play_script;
    char content[1024];
    char live[1024];
    char path[2048];
    char line[4096];
    struct ToriRSMapEd* maped;
    struct ToriRSMapEdLane lane;
    struct ToriRSMapEdBuildMock* mock;
    struct ToriRSMapEdBuildRunner runner;
    struct ToriRSMapEdLiveLink link;
    static struct conn a;
    static struct conn b;
    static struct conn ro;
    struct ToriRSMapEdBuf payload;
    int port;
    pid_t peer = -1;
    uint32_t status;
    uint32_t step;
    int at;

    if( argc != 6 )
    {
        fprintf(stderr, "usage: live_editor_test <scratch> <peer-script> <fake-server> <repo-root> <fake-play>\n");
        return 2;
    }
    scratch = argv[1];
    peer_script = argv[2];
    fake_script = argv[3];
    repo_root = argv[4];
    play_script = argv[5];
    signal(SIGPIPE, SIG_IGN);
    ToriMem_Init();
    snprintf(content, sizeof(content), "%s/content", scratch);
    snprintf(live, sizeof(live), "%s/live", scratch);
    snprintf(g_peer_log, sizeof(g_peer_log), "%s/peer.log", scratch);
    setenv("MOCK_PEER_LOG", g_peer_log, 1);
    unsetenv("MOCK_PEER_TICKET");
    /* Open must not bind the native runner from the caller's environment:
     * empty is "no lane" (unset means the repo's build/live/default). */
    setenv("TORIRSMAPED_LIVE_DIR", "", 1);
    memset(&payload, 0, sizeof(payload));

    /* ---- pure pieces ---------------------------------------------------- */
    {
        char why[4096];
        struct ToriRSMapEdLane inside;
        struct ToriRSMapEdBuildJob job;
        struct ToriRSMapEdBuildCommand command;
        char unit[256];
        int32_t focus[TORIRSMAPED_LIVE_FOCUS_VALUES];
        char name[32];
        int32_t squares[4] = { 50, 50, 51, 49 };

        ToriRSMapEd_LaneDefaults(&inside, repo_root, content, content);
        check(!ToriRSMapEd_LaneValid(&inside, why, sizeof(why)), "lane: live dir == content dir refused");
        snprintf(inside.live_dir, sizeof(inside.live_dir), "%s/maps/live", content);
        check(!ToriRSMapEd_LaneValid(&inside, why, sizeof(why)), "lane: live dir under the content dir refused");
        check(strstr(why, "inside the content tree") != NULL, "lane: the refusal says why");
        ToriRSMapEd_LaneDefaults(&lane, repo_root, content, live);
        check(ToriRSMapEd_LaneValid(&lane, why, sizeof(why)), "lane: a live dir beside the content tree is valid");
        {
            char user[16];
            char pass[24];
            char url[1024];
            snprintf(path, sizeof(path), "%s/src/torirs", repo_root);
            check(strcmp(lane.play_bin, path) == 0, "lane: the play client defaults to src/torirs");
            check(strstr(lane.play_fixture, "fresh_lumbridge.ini") != NULL, "lane: the play fixture is fresh_lumbridge");
            ToriRSMapEd_LanePlayAccount(&lane, user, sizeof(user), pass, sizeof(pass));
            check(strcmp(user, "maped43620") == 0 && strcmp(pass, "lane43620run") == 0,
                "lane: the play account is derived from the game port");
            ToriRSMapEd_LaneWebPlayUrl(&lane, url, sizeof(url));
            check(url[0] == '\0', "lane: no web play URL without an io_server");
            snprintf(inside.io_manifest, sizeof(inside.io_manifest), "x.ini");
            ToriRSMapEd_LaneWebPlayUrl(&inside, url, sizeof(url));
            check(strstr(url, "http://127.0.0.1:43622/?args=--manifest,manifests/manifest_osrs241.ini,"
                              "--connect,127.0.0.1:43620,--user,maped43620,--pass,lane43620run")
                      && strstr(url, "&ws=ws://127.0.0.1:43620/&js5_url=ws://127.0.0.1:43621/"),
                "lane: the web play URL names io_server, the game port and the JS5");
        }
        /* This lane must never start the real server or client: RUN with it
         * unattached is the "cannot launch" case. */
        lane.server_bin[0] = '\0';
        lane.play_bin[0] = '\0';

        memset(&job, 0, sizeof(job));
        job.what = TORIRSMAPED_BUILD_PACKS;
        check(ToriRSMapEd_BuildCommand(&lane, &job, &command), "command: packs");
        ToriRSMapEd_BuildCommandFormat(&command, line, sizeof(line));
        snprintf(path, sizeof(path),
            "make -C %s/src --no-print-directory maped-lane-packs TORIRSSERVER_CONTENT_DIR=%s "
            "TORIRSSERVER_SCRIPT_OUT=%s/scripts TORIRSSERVER_PACKS_LANE_OUT=%s/scripts_lanes "
            "SERVPACK_OUT=%s/servpack OBJVERIFY_SKIP=1",
            repo_root, content, live, live, live);
        check(strcmp(line, path) == 0, "command: the packs make line");
        if( strcmp(line, path) != 0 )
            fprintf(stderr, "  got  %s\n  want %s\n", line, path);
        job.what = TORIRSMAPED_BUILD_CLIENT_CACHE;
        job.squares = squares;
        job.square_count = 2;
        check(ToriRSMapEd_BuildCommand(&lane, &job, &command), "command: client cache");
        ToriRSMapEd_BuildCommandFormat(&command, line, sizeof(line));
        snprintf(path, sizeof(path), "MAPED_LIVE_CACHE=%s/cache", live);
        check(strstr(line, " maped-client-cache ") && strstr(line, path), "command: the client cache target and cache");
        snprintf(path, sizeof(path), "MAPED_MAP_ARCHIVES=%s/map_archives.txt", live);
        check(strstr(line, path) != NULL, "command: the squares' map list");
        job.what = TORIRSMAPED_BUILD_RUN;
        check(ToriRSMapEd_BuildCommand(&lane, &job, &command), "command: RUN's build");
        ToriRSMapEd_BuildCommandFormat(&command, line, sizeof(line));
        snprintf(path, sizeof(path), "SERVPACK_OUT=%s/servpack", live);
        check(strstr(line, " maped-run-build ") && strstr(line, path) && strstr(line, "MAPED_LIVE_CACHE=")
                  && strstr(line, "MAPED_MAP_ARCHIVES=") && strstr(line, " OBJVERIFY_SKIP=1"),
            "command: RUN's build names both halves' outputs and skips the object verifier");
        job.what = TORIRSMAPED_BUILD_CS2;
        snprintf(job.quest, sizeof(job.quest), "%s", "bank_sort");
        check(ToriRSMapEd_BuildCommand(&lane, &job, &command), "command: cs2 compiles one script (workbench 6.6)");
        ToriRSMapEd_BuildCommandFormat(&command, line, sizeof(line));
        check(strstr(line, " maped-cs2-compile ") && strstr(line, "MAPED_CS2_SCRIPT=bank_sort"),
            "command: the cs2 target names its script");
        job.quest[0] = '\0';

        mkdir(live, 0755);
        job.what = TORIRSMAPED_BUILD_CLIENT_CACHE;
        check(ToriRSMapEd_BuildWriteMapList(&lane, &job), "map list written");
        {
            size_t size;
            char* text;
            snprintf(path, sizeof(path), "%s/map_archives.txt", live);
            text = slurp(path, &size);
            check(text && strstr(text, "maps=12850\n") && strstr(text, "maps=13105\n"),
                "map list: maps=(x<<8)|z per square");
            free(text);
        }
        check(ToriRSMapEd_BuildLineUnit("compiled  server/scripts/a/b.rs2 — edited", unit, sizeof(unit))
                  && strcmp(unit, "server/scripts/a/b.rs2") == 0,
            "unit: an sscompile line");
        check(ToriRSMapEd_BuildLineUnit("cachepack: repacked varp — its files changed", unit, sizeof(unit))
                  && strcmp(unit, "varp") == 0,
            "unit: a cachepack line");
        check(!ToriRSMapEd_BuildLineUnit("Server pack: 1 type(s) repacked", unit, sizeof(unit)),
            "unit: a summary is not a unit");
        {
            static const char* const failed[] = { "== script pack (base) -> live/scripts",
                "compiled  server/scripts/a.rs2 — edited",
                "sscompile: quests/quest_wanted/configs/wanted.varp:4: varp9514 lacks clientcode",
                "build_packs: script pack failed", "make[1]: *** [torirsserver-packs] Error 1",
                "make: *** [torirsserver-packs] Error 2" };
            static const char* const quiet[] = { "== packs", "progress 50%", "make: *** [x] Error 2" };
            char why[256];
            check(ToriRSMapEd_BuildFailureLine(failed, 6, why, sizeof(why))
                      && strcmp(why, "build_packs: script pack failed") == 0,
                "failure line: the newest error line, make's own skipped");
            check(ToriRSMapEd_BuildFailureLine(failed, 3, why, sizeof(why)) && strstr(why, "wanted.varp"),
                "failure line: the compiler's line when it is the newest error");
            check(ToriRSMapEd_BuildFailureLine(quiet, 3, why, sizeof(why)) && strcmp(why, "progress 50%") == 0,
                "failure line: no error word, the newest real line");
            check(!ToriRSMapEd_BuildFailureLine(quiet, 1, why, sizeof(why)) && why[0] == '\0',
                "failure line: only banners, none");
        }
        {
            struct ToriRSMapEdBuildSink sink;
            memset(&sink, 0, sizeof(sink));
            sink.archive = changed_archive;
            g_changed_count = 0;
            check(ToriRSMapEd_BuildParseChanged("archive 2 16 varp\narchive 2 6 loc\n", 34, &sink) == 2
                      && g_changed_count == 2 && g_changed[0] == 2 && g_changed[1] == 16 && g_changed[3] == 6,
                "client.changed: two rows, (2,16) and (2,6)");
            check(ToriRSMapEd_BuildParseChanged("garbage\n", 8, &sink) == -1, "client.changed: refused");
        }
        ToriRSMapEd_LiveFocusPack("Zezima", -1, -1, -1, focus);
        check(ToriRSMapEd_LiveFocusName(focus, TORIRSMAPED_LIVE_FOCUS_VALUES, name, sizeof(name))
                  && strcmp(name, "Zezima") == 0,
            "live focus: the name round-trips");
        check(!ToriRSMapEd_LiveFocusName(focus, 3, name, sizeof(name)), "live focus: a short field refused");
        {
            uint32_t varps[8];
            check(Editor_LiveParseVarps("281, 1048,7", varps, 8) == 3 && varps[1] == 1048, "varps: parsed");
            check(Editor_LiveParseVarps("281 x", varps, 8) == -1, "varps: refused");
            Editor_LiveTeleCoord(3222, 3218, 0, name, sizeof(name));
            check(strcmp(name, "0_50_50_22_18") == 0, "tele coord: level_mx_mz_lx_lz");
        }
    }

    /* ---- the server, a test runner, the native link ------------------------- */
    /* ---- the content-tree lock: a dead holder's is taken, a live one named ---- */
    {
        pid_t dead = fork();
        FILE* lock;
        int status_dead = 0;
        assert(dead >= 0);
        if( dead == 0 )
            _exit(0);
        waitpid(dead, &status_dead, 0);
        snprintf(path, sizeof(path), "%s/.editor-session.lock", content);
        lock = fopen(path, "wb");
        assert(lock);
        fprintf(lock, "pid %ld\nsince %lld\ncmd /x/src/torirs --manifest crashed.ini\n", (long)dead,
            (long long)time(NULL) - 3600);
        fclose(lock);
    }
    maped = calloc(1, sizeof(*maped));
    assert(maped);
    ToriRSMapEd_Open(maped, content, repo_root);
    check(maped->writable, "lock: a lockfile whose pid no longer runs is taken (stale)");
    {
        FILE* lock = fopen(path, "rb");
        char text[512] = "";
        char want[64];
        if( lock )
        {
            size_t got = fread(text, 1, sizeof(text) - 1, lock);
            text[got] = '\0';
            fclose(lock);
        }
        snprintf(want, sizeof(want), "pid %ld\nsince ", (long)getpid());
        check(strncmp(text, want, strlen(want)) == 0 && strstr(text, "\ncmd ") != NULL,
            "lock: the new lockfile names this pid, a start time and a command line");
        check(maped->read_only_reason[0] == '\0', "lock: a writable server has no read-only reason");
    }
    check(!(maped->capabilities & TORIRSMAPED_CAP_BUILD), "no runner until one is bound");
    {
        /* A second server on the same tree: the holder (this process) is live,
         * so it serves read-only and names it -- in its HELLO and in RUN. */
        struct ToriRSMapEd* second = calloc(1, sizeof(*second));
        char want[160];
        assert(second);
        ToriRSMapEd_Open(second, content, repo_root);
        snprintf(want, sizeof(want), "is held by pid %ld (", (long)getpid());
        check(!second->writable && strstr(second->read_only_reason, want) && strstr(second->read_only_reason, content)
                  && strstr(second->read_only_reason, "since "),
            "lock: a live holder is reported by pid, command and start");
        if( !strstr(second->read_only_reason, want) )
            fprintf(stderr, "  reason: %s\n", second->read_only_reason);
        conn_open(&ro, second);
        g_conns[g_conn_count++] = &ro;
        hello(&ro);
        at = wait_fact(&ro, TORIRSMAPED_FACT_HELLO, 2000);
        check(at >= 0 && ro.facts[at].length > 16 && fact_u32(&ro.facts[at], 1) == 0, "lock: HELLO says read-only");
        if( at >= 0 && ro.facts[at].length > 16 )
        {
            struct ToriRSMapEdReader reader;
            char reason[1024];
            ToriRSMapEd_ReaderInit(&reader, ro.facts[at].data + 16, ro.facts[at].length - 16);
            ToriRSMapEd_GetString(&reader, reason, sizeof(reason));
            check(ToriRSMapEd_ReaderDone(&reader) && strstr(reason, want), "lock: HELLO carries who holds the tree");
        }
        mark(&ro);
        send_live(&ro, TORIRSMAPED_REQ_RUN, &payload);
        at = wait_run_done(&ro, &status, &step);
        check(at >= 0 && status == EDITOR_HOST_LOCKED && step == TORIRSMAPED_RUN_STEP_SAVE
                  && strstr(ro.model.run_message, "read-only: ") && strstr(ro.model.run_message, want)
                  && strstr(ro.model.run_message, "run this window against the daemon"),
            "lock: a read-only RUN names the holder and what to do");
        if( !strstr(ro.model.run_message, want) )
            fprintf(stderr, "  run: %s\n", ro.model.run_message);
        g_conn_count--;
        ToriRSMapEd_SessionFree(&ro.session);
        ToriRSMapEd_Close(second);
        free(second);
        check(access(path, F_OK) == 0, "lock: the read-only server leaves the holder's lock alone");
    }
    mock = calloc(1, sizeof(*mock));
    assert(mock);
    mock->lane = lane;
    {
        static const char* const pack_lines[] = { "== script pack (base) -> live/scripts",
            "compiled  server/scripts/areas/edited/scripts/live_test.rs2 — edited",
            "cachepack: repacked npc — its files changed", "== packs: ok" };
        static const char* const client_lines[] = { "Packing client configs from content",
            "cachepack: repacked varp — its files changed; archive 2/16 changed",
            "Client pack: 1 type(s) repacked, 19 reused" };
        static const uint32_t client_archives[] = { 2, 16 };
        mock->scripts[TORIRSMAPED_BUILD_PACKS].lines = pack_lines;
        mock->scripts[TORIRSMAPED_BUILD_PACKS].line_count = 4;
        mock->scripts[TORIRSMAPED_BUILD_PACKS].status = EDITOR_HOST_OK;
        mock->scripts[TORIRSMAPED_BUILD_CLIENT_CACHE].lines = client_lines;
        mock->scripts[TORIRSMAPED_BUILD_CLIENT_CACHE].line_count = 3;
        mock->scripts[TORIRSMAPED_BUILD_CLIENT_CACHE].status = EDITOR_HOST_OK;
        mock->scripts[TORIRSMAPED_BUILD_CLIENT_CACHE].archives = client_archives;
        mock->scripts[TORIRSMAPED_BUILD_CLIENT_CACHE].archive_count = 1;
        /* RUN's build: both halves in one job (-j2 interleaves their lines). */
        static const char* const run_lines[] = { "== script pack (base) -> live/scripts",
            "Packing client configs from content",
            "compiled  server/scripts/areas/edited/scripts/live_test.rs2 — edited",
            "cachepack: repacked varp — its files changed; archive 2/16 changed",
            "cachepack: repacked npc — its files changed", "Client pack: 1 type(s) repacked, 19 reused",
            "== packs: ok" };
        mock->scripts[TORIRSMAPED_BUILD_RUN].lines = run_lines;
        mock->scripts[TORIRSMAPED_BUILD_RUN].line_count = 7;
        mock->scripts[TORIRSMAPED_BUILD_RUN].status = EDITOR_HOST_OK;
        mock->scripts[TORIRSMAPED_BUILD_RUN].archives = client_archives;
        mock->scripts[TORIRSMAPED_BUILD_RUN].archive_count = 1;
    }
    ToriRSMapEd_BuildRunnerMock(&runner, mock);
    check(ToriRSMapEd_SetLane(maped, &lane), "lane bound");
    ToriRSMapEd_SetBuildRunner(maped, &runner);
    check(ToriRSMapEd_LiveLinkNative(&link, &lane), "native link available");
    ToriRSMapEd_SetLiveLink(maped, &link);
    check((maped->capabilities & TORIRSMAPED_CAP_BUILD) && (maped->capabilities & TORIRSMAPED_CAP_LIVE),
        "capabilities: build and live");

    conn_open(&a, maped);
    conn_open(&b, maped);
    g_conns[g_conn_count++] = &a;
    g_conns[g_conn_count++] = &b;
    hello(&a);
    hello(&b);
    at = wait_fact(&a, TORIRSMAPED_FACT_HELLO, 2000);
    check(at >= 0 && a.facts[at].length == 16
              && (fact_u32(&a.facts[at], 3) & (TORIRSMAPED_CAP_BUILD | TORIRSMAPED_CAP_LIVE))
                     == (TORIRSMAPED_CAP_BUILD | TORIRSMAPED_CAP_LIVE),
        "FACT_HELLO reports build and live");

    /* ---- BUILD alone ---------------------------------------------------------- */
    mark(&a);
    mark(&b);
    ToriRSMapEd_PutU32(&payload, TORIRSMAPED_BUILD_PACKS);
    send_live(&a, TORIRSMAPED_REQ_BUILD, &payload);
    at = wait_fact(&b, TORIRSMAPED_FACT_BUILD_DONE, 2000);
    check(at >= 0, "BUILD: the OTHER connection hears FACT_BUILD_DONE too");
    if( at >= 0 )
    {
        int lines = 0;
        for( int i = b.mark; i < at; i++ )
            lines += b.facts[i].type == TORIRSMAPED_FACT_BUILD_LINE;
        check(lines == 4, "BUILD: four FACT_BUILD_LINEs before the done");
        check(fact_u32(&b.facts[at], 0) == TORIRSMAPED_BUILD_PACKS && fact_u32(&b.facts[at], 1) == EDITOR_HOST_OK
                  && fact_u32(&b.facts[at], 2) == 2,
            "BUILD: done OK with two units (compiled + repacked)");
    }
    check(mock->command_count == 1 && mock->command_whats[0] == TORIRSMAPED_BUILD_PACKS,
        "BUILD: the runner was asked once, for packs");
    check(strstr(mock->commands[0], "maped-lane-packs") && strstr(mock->commands[0], "SERVPACK_OUT=")
              && strstr(mock->commands[0], live),
        "BUILD: the recorded command builds into the live dir");
    check(b.model.last_build_status == 0 && b.model.last_build_units == 2 && b.model.log_count >= 5,
        "model: the build folded into the other window's model");

    /* BUILD while one runs: REFUSED, the running one unharmed. */
    mark(&a);
    mock->scripts[TORIRSMAPED_BUILD_PACKS].idle_polls = 30;
    ToriRSMapEd_PutU32(&payload, TORIRSMAPED_BUILD_PACKS);
    send_live(&a, TORIRSMAPED_REQ_BUILD, &payload);
    ToriRSMapEd_PutU32(&payload, TORIRSMAPED_BUILD_CLIENT_CACHE);
    send_live(&a, TORIRSMAPED_REQ_BUILD, &payload);
    pump();
    at = find_fact(&a, TORIRSMAPED_FACT_BUILD_DONE, a.mark);
    check(at >= 0 && fact_u32(&a.facts[at], 0) == TORIRSMAPED_BUILD_CLIENT_CACHE
              && fact_u32(&a.facts[at], 1) == EDITOR_HOST_REFUSED,
        "BUILD: a second build while one runs is REFUSED");
    {
        int64_t const deadline = now_ms() + 5000;
        at = -1;
        while( at < 0 && now_ms() < deadline )
        {
            pump();
            for( int i = a.mark; i < a.fact_count; i++ )
            {
                if( a.facts[i].type == TORIRSMAPED_FACT_BUILD_DONE
                    && fact_u32(&a.facts[i], 0) == TORIRSMAPED_BUILD_PACKS )
                    at = i;
            }
            usleep(2000);
        }
    }
    check(at >= 0 && fact_u32(&a.facts[at], 1) == EDITOR_HOST_OK, "BUILD: the slow build still finishes OK");
    mock->scripts[TORIRSMAPED_BUILD_PACKS].idle_polls = 0;
    ToriRSMapEd_PutU32(&payload, 99);
    mark(&a);
    send_live(&a, TORIRSMAPED_REQ_BUILD, &payload);
    at = wait_fact(&a, TORIRSMAPED_FACT_BUILD_DONE, 2000);
    check(at >= 0 && fact_u32(&a.facts[at], 1) == EDITOR_HOST_UNSUPPORTED, "BUILD: an unknown what is UNSUPPORTED");

    /* ---- RUN with no live server: builds, then stops at the reload ------------- */
    create_square(&a, "m60_60");
    add_spawn(&a, 3201);
    pump();
    mark(&a);
    mock->command_count = 0;
    send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
    at = wait_run_done(&a, &status, &step);
    check(at >= 0 && status == EDITOR_HOST_UNSUPPORTED && step == TORIRSMAPED_RUN_STEP_RELOAD,
        "RUN unattached: stops at the reload, UNSUPPORTED");
    check(strstr(a.model.run_message, "Launch the lane first") != NULL,
        "RUN unattached, a lane that cannot launch: \"Launch the lane first\"");
    check(find_fact(&a, TORIRSMAPED_FACT_SAVED, a.mark) >= 0, "RUN unattached: it saved first");
    check(mock->command_count == 1 && mock->command_whats[0] == TORIRSMAPED_BUILD_RUN,
        "RUN unattached: one build, packs and client cache at once");
    check(strcmp(mock->command_squares[0], "60_60") == 0, "RUN unattached: the build names the saved square");
    snprintf(path, sizeof(path), "%s/maps/m60_60.jm2", content);
    check(access(path, F_OK) == 0, "RUN unattached: the new square is on disk");
    check(a.model.pending_squares == 1 && a.model.pending_spawns == 1 && a.model.pending_archives == 1,
        "RUN unattached: square, spawn file and archive stay pending");

    /* ---- attach to the mock peer --------------------------------------------- */
    port = free_port();
    check(port > 0, "a free port for the peer");
    /* gated: HELLO must carry this ticket */
    setenv("MOCK_PEER_TICKET", "letmein", 1);
    peer = spawn_peer(peer_script, port);
    check(peer > 0, "the mock peer started");
    check(wait_peer("\"listening\"", 1, 10000), "the mock peer listens");

    mark(&a);
    snprintf(line, sizeof(line), "127.0.0.1:%d", port);
    Editor_LiveEncodeAttach(&payload, 0, line, "wrong");
    send_live(&a, TORIRSMAPED_REQ_LIVE_ATTACH, &payload);
    {
        int64_t const deadline = now_ms() + 10000;
        while( a.model.state != TORIRSMAPED_LIVE_FAILED && now_ms() < deadline )
        {
            pump();
            usleep(2000);
        }
    }
    check(a.model.state == TORIRSMAPED_LIVE_FAILED && strstr(a.model.message, "refused"),
        "attach: a wrong gate ticket fails, and says so");
    mark(&a);
    Editor_LiveEncodeAttach(&payload, 0, line, "letmein");
    send_live(&a, TORIRSMAPED_REQ_LIVE_ATTACH, &payload);
    {
        int64_t const deadline = now_ms() + 10000;
        while( a.model.state != TORIRSMAPED_LIVE_ATTACHED && now_ms() < deadline )
        {
            pump();
            usleep(2000);
        }
    }
    check(a.model.state == TORIRSMAPED_LIVE_ATTACHED, "attach: attached with the right ticket");
    check(strcmp(a.model.server_name, "mockworld") == 0 && a.model.players == 0, "attach: server name and players");
    check(a.model.players_known && a.model.player_row_count == 0,
        "attach: the server's player list (CTL_FACT_PLAYERS after HELLO) reached the model");
    check(b.model.state == TORIRSMAPED_LIVE_ATTACHED, "attach: the other window sees it too");
    peer_last("\"hello\"", line, sizeof(line));
    check(strstr(line, "\"ticket\": \"letmein\"") != NULL, "attach: HELLO carried the ticket after 0xED");
    {
        int64_t const deadline = now_ms() + 5000;
        while( a.model.stale_count == 0 && now_ms() < deadline )
        {
            pump();
            usleep(2000);
        }
    }
    check(a.model.stale_count == 1 && strstr(a.model.stale[0], "stale_example"), "status: the stale list arrived");

    /* ---- cheat and subscribe ----------------------------------------------------- */
    mark(&a);
    Editor_LiveEncodeCheat(&payload, "Zezima", "::tele 0_50_50_22_18");
    send_live(&a, TORIRSMAPED_REQ_LIVE_CHEAT, &payload);
    at = wait_fact(&a, TORIRSMAPED_FACT_LIVE_LINE, 5000);
    check(at >= 0, "cheat: a FACT_LIVE_LINE came back");
    check(wait_peer("\"line\": \"tele 0_50_50_22_18\"", 1, 3000), "cheat: the peer ran it without the ::");
    {
        uint32_t varps[2] = { 281, 1048 };
        mark(&b);
        Editor_LiveEncodeSubscribe(&payload, "Zezima", varps, 2);
        send_live(&a, TORIRSMAPED_REQ_LIVE_SUBSCRIBE, &payload);
        at = wait_fact(&b, TORIRSMAPED_FACT_LIVE_VARS, 5000);
        check(at >= 0, "subscribe: FACT_LIVE_VARS reaches every window");
        {
            int32_t value = 0;
            check(Editor_LiveVarValue(&b.model, 1048, &value) && value == 10480
                      && strcmp(b.model.vars_player, "Zezima") == 0,
                "subscribe: the model holds varp 1048 = 10480 for Zezima");
        }
    }

    /* ---- RUN: the whole sequence ---------------------------------------------------- */
    add_spawn(&a, 3202);
    pump();
    mark(&a);
    mark(&b);
    mock->command_count = 0;
    send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
    at = wait_run_done(&b, &status, &step);
    check(at >= 0 && status == EDITOR_HOST_OK && step == TORIRSMAPED_RUN_STEP_DONE, "RUN: done OK");
    if( at >= 0 )
    {
        /* The order on the wire, as the OTHER window heard it. */
        int saved = find_fact(&b, TORIRSMAPED_FACT_SAVED, b.mark);
        int build_done = -1;
        int live_line = -1;
        int progress_build = -1;
        int progress_reload = -1;
        int progress_play = -1;
        int status_after = find_fact(&b, TORIRSMAPED_FACT_LIVE_STATUS, at);
        for( int i = b.mark; i < at; i++ )
        {
            if( b.facts[i].type == TORIRSMAPED_FACT_BUILD_DONE && fact_u32(&b.facts[i], 0) == TORIRSMAPED_BUILD_RUN )
                build_done = i;
            if( b.facts[i].type == TORIRSMAPED_FACT_LIVE_LINE && live_line < 0 )
                live_line = i;
            if( b.facts[i].type == TORIRSMAPED_FACT_RUN_PROGRESS )
            {
                uint32_t const step = fact_u32(&b.facts[i], 0);
                if( step == TORIRSMAPED_RUN_STEP_BUILD && progress_build < 0 )
                    progress_build = i;
                if( step == TORIRSMAPED_RUN_STEP_RELOAD && progress_reload < 0 )
                    progress_reload = i;
                if( step == TORIRSMAPED_RUN_STEP_PLAY && progress_play < 0 )
                    progress_play = i;
            }
        }
        check(saved >= 0 && build_done > saved && live_line > build_done && at > live_line && status_after > at,
            "RUN: SAVED < build done < reload line < RUN_DONE < LIVE_STATUS");
        check(progress_build > saved && progress_build < build_done && progress_reload > build_done
                  && progress_play > progress_reload && progress_play < at,
            "RUN: progress rows for build, reload and play in order");
        if( progress_build >= 0 )
            check(fact_u32(&b.facts[progress_build], 1) == 2 && fact_u32(&b.facts[progress_build], 2) == 4
                      && fact_u32(&b.facts[progress_build], 3) < 100,
                "RUN: the build is step 2 of 4, under 100 %");
        check(fact_u32(&b.facts[at], 2) + 0 > 0, "RUN: the done carries a message");
    }
    check(mock->command_count == 1, "RUN: one build asked for");
    check(b.model.progress_serial > 0 && b.model.progress_count == 4, "model: the progress rows reached the model");
    peer_last("\"reload\"", line, sizeof(line));
    snprintf(path, sizeof(path), "\"manifest\": \"%s/scripts/pack.manifest\"", live);
    check(strstr(line, path) != NULL, "RELOAD: the lane's script manifest");
    check(strstr(line, "\"archives\": [[2, 16]]") != NULL, "RELOAD: the client build's archive");
    check(strstr(line, "\"squares\": [[60, 60]]") != NULL, "RELOAD: the square saved in the earlier RUN");
    check(strstr(line, "server/scripts/areas/edited/configs/m50_50.spawn") != NULL, "RELOAD: the spawn file");
    check(strstr(line, "\"well_formed\": true") != NULL, "RELOAD: the payload parses to its end");
    check(b.model.pending_squares == 0 && b.model.pending_spawns == 0 && b.model.pending_archives == 0,
        "RUN: nothing pending after a good reload");
    check(b.model.run_counts[0] == 3 && b.model.run_counts[1] == 1 && b.model.run_counts[2] == 1,
        "RUN: the reload's counts reached the model");

    /* ---- RUN stops at the first failure ------------------------------------------------ */
    add_spawn(&a, 3203);
    pump();
    {
        int const reloads_before = peer_count("\"reload\"");
        mark(&a);
        mock->command_count = 0;
        static const char* const failing_lines[] = { "== script pack (base) -> live/scripts",
            "sscompile: quests/quest_wanted/configs/wanted.varp:4: varp9514 lacks clientcode",
            "make[1]: *** [torirsserver-packs] Error 1", "make: *** [torirsserver-packs] Error 2" };
        const char* const* const ok_lines = mock->scripts[TORIRSMAPED_BUILD_RUN].lines;
        int const ok_count = mock->scripts[TORIRSMAPED_BUILD_RUN].line_count;
        mock->scripts[TORIRSMAPED_BUILD_RUN].lines = failing_lines;
        mock->scripts[TORIRSMAPED_BUILD_RUN].line_count = 4;
        mock->scripts[TORIRSMAPED_BUILD_RUN].status = EDITOR_HOST_BAKE_FAILED;
        send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
        at = wait_run_done(&a, &status, &step);
        check(at >= 0 && status == EDITOR_HOST_BAKE_FAILED && step == TORIRSMAPED_RUN_STEP_BUILD,
            "fail packs: RUN stops at the build");
        check(strstr(a.model.run_message, "the packs build failed")
                  && strstr(a.model.run_message, "wanted.varp:4: varp9514 lacks clientcode")
                  && strstr(a.model.run_message, "nothing was launched"),
            "fail packs: the RUN message names the packs half and the failing line, not make's");
        if( !strstr(a.model.run_message, "wanted.varp") )
            fprintf(stderr, "  run: %s\n", a.model.run_message);
        {
            int found = 0;
            for( int i = 0; i < a.model.log_count && i < 12; i++ )
            {
                const char* text = Editor_LiveLogLine(&a.model, i);
                if( text && strstr(text, "> ") == text && strstr(text, "  sscompile: quests/quest_wanted") )
                    found = 1;
            }
            check(found, "fail packs: the build's last lines are repeated at the end of the log");
        }
        check(mock->command_count == 1, "fail packs: the one build, nothing after it");
        pump_for(200);
        check(peer_count("\"reload\"") == reloads_before, "fail packs: no RELOAD sent");
        check(a.model.pending_spawns == 1, "fail packs: the spawn file stays pending");

        {
            static const char* const client_failing[] = { "Packing client configs from content",
                "cachepack: [tree] configs/all.loc:12: `shape` is not a loc key",
                "make[1]: *** [maped-client-cache] Error 1", "make: *** [maped-client-cache] Error 2" };
            mark(&a);
            mock->command_count = 0;
            mock->scripts[TORIRSMAPED_BUILD_RUN].lines = client_failing;
            mock->scripts[TORIRSMAPED_BUILD_RUN].line_count = 4;
            send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
            at = wait_run_done(&a, &status, &step);
            check(at >= 0 && status == EDITOR_HOST_BAKE_FAILED && step == TORIRSMAPED_RUN_STEP_BUILD,
                "fail client cache: RUN stops at the build");
            check(strstr(a.model.run_message, "the client cache build failed") != NULL,
                "fail client cache: the RUN message names the client cache half");
            check(mock->command_count == 1, "fail client cache: the one build");
            pump_for(200);
            check(peer_count("\"reload\"") == reloads_before, "fail client cache: no RELOAD sent");
        }
        mock->scripts[TORIRSMAPED_BUILD_RUN].lines = ok_lines;
        mock->scripts[TORIRSMAPED_BUILD_RUN].line_count = ok_count;
        mock->scripts[TORIRSMAPED_BUILD_RUN].status = EDITOR_HOST_OK;
    }

    /* a reload the game server refuses: pending stays, the next RUN names it again */
    mark(&a);
    Editor_LiveEncodeCheat(&payload, "Zezima", "failnext");
    send_live(&a, TORIRSMAPED_REQ_LIVE_CHEAT, &payload);
    check(wait_peer("\"line\": \"failnext\"", 1, 3000), "the peer will fail the next reload");
    mark(&a);
    send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
    at = wait_run_done(&a, &status, &step);
    check(at >= 0 && status == EDITOR_HOST_REFUSED && step == TORIRSMAPED_RUN_STEP_RELOAD,
        "fail reload: RUN stops at the reload, REFUSED");
    check(a.model.pending_spawns == 1, "fail reload: still pending");
    mark(&a);
    Editor_LiveEncodeCheat(&payload, "Zezima", "restartnext");
    send_live(&a, TORIRSMAPED_REQ_LIVE_CHEAT, &payload);
    check(wait_peer("\"line\": \"restartnext\"", 1, 3000), "the peer will ask for a restart");
    mark(&a);
    send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
    at = wait_run_done(&a, &status, &step);
    check(at >= 0 && status == EDITOR_HOST_OK, "after the failure: RUN succeeds");
    peer_last("\"reload\"", line, sizeof(line));
    check(strstr(line, "m50_50.spawn") != NULL, "after the failure: the reload names the spawn file again");
    check(a.model.restart_count == 1 && strstr(a.model.restart[0], "fields/npc.ini"),
        "restart-needed rows reach the model");

    /* a link that drops mid-reload stops RUN */
    add_spawn(&a, 3204);
    pump();
    mark(&a);
    Editor_LiveEncodeCheat(&payload, "Zezima", "dropnext");
    send_live(&a, TORIRSMAPED_REQ_LIVE_CHEAT, &payload);
    check(wait_peer("\"line\": \"dropnext\"", 1, 3000), "the peer will drop on the next reload");
    mark(&a);
    send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
    at = wait_run_done(&a, &status, &step);
    check(at >= 0 && status == EDITOR_HOST_IO_ERROR && step == TORIRSMAPED_RUN_STEP_RELOAD,
        "dropped link: RUN stops at the reload");
    pump_for(100);
    check(a.model.state == TORIRSMAPED_LIVE_FAILED, "dropped link: the link reads failed");

    /* a RUN while one is in flight is refused to the asker */
    mark(&a);
    mock->scripts[TORIRSMAPED_BUILD_RUN].idle_polls = 50;
    send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
    send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
    pump();
    at = find_fact(&a, TORIRSMAPED_FACT_RUN_DONE, a.mark);
    check(at >= 0 && fact_u32(&a.facts[at], 0) == EDITOR_HOST_REFUSED, "a second RUN in flight is REFUSED");
    at = wait_fact(&a, TORIRSMAPED_FACT_LIVE_STATUS, 100);
    {
        int64_t const deadline = now_ms() + 10000;
        int done = 0;
        while( !done && now_ms() < deadline )
        {
            pump();
            for( int i = a.mark; i < a.fact_count; i++ )
            {
                if( a.facts[i].type == TORIRSMAPED_FACT_RUN_DONE && fact_u32(&a.facts[i], 0) != EDITOR_HOST_REFUSED )
                    done = 1;
            }
            usleep(2000);
        }
        check(done, "the first RUN still finishes");
    }
    mock->scripts[TORIRSMAPED_BUILD_RUN].idle_polls = 0;

    /* detach */
    mark(&a);
    send_live(&a, TORIRSMAPED_REQ_LIVE_DETACH, &payload);
    pump();
    check(a.model.state == TORIRSMAPED_LIVE_DETACHED, "detach: detached");
    stop_pid(peer);
    peer = -1;

    /* ---- launch: the peer as the lane's torirsserver --------------------------------- */
    {
        struct ToriRSMapEdLane launch_lane = lane;
        struct ToriRSMapEdLiveLink launch_link;
        int pids[3];
        FILE* f;
        port = free_port();
        unsetenv("MOCK_PEER_TICKET");
        unlink(g_peer_log);
        snprintf(launch_lane.server_bin, sizeof(launch_lane.server_bin), "%s", peer_script);
        snprintf(launch_lane.js5_bin, sizeof(launch_lane.js5_bin), "%s", fake_script);
        launch_lane.io_bin[0] = '\0';
        launch_lane.game_port = port;
        launch_lane.js5_port = port + 1;
        check(ToriRSMapEd_SetLane(maped, &launch_lane), "launch: lane rebound");
        check(ToriRSMapEd_LiveLinkNative(&launch_link, &launch_lane), "launch: native link");
        ToriRSMapEd_SetLiveLink(maped, &launch_link);

        /* A lane RUN never built is not launched: the server would die at
         * once on a missing server pack. */
        mark(&a);
        Editor_LiveEncodeAttach(&payload, 1, "", "");
        send_live(&a, TORIRSMAPED_REQ_LIVE_ATTACH, &payload);
        pump_for(300);
        check(a.model.state != TORIRSMAPED_LIVE_ATTACHED && a.model.state != TORIRSMAPED_LIVE_CONNECTING
                  && strstr(a.model.message, "the lane has not been built") && strstr(a.model.message, "press RUN"),
            "launch: an unbuilt lane is refused with what to do");
        if( !strstr(a.model.message, "the lane has not been built") )
            fprintf(stderr, "  status: %s\n", a.model.message);
        snprintf(path, sizeof(path), "%s/servpack", live);
        mkdir(path, 0755);
        snprintf(path, sizeof(path), "%s/servpack/pack.manifest", live);
        f = fopen(path, "wb");
        assert(f);
        fclose(f);
        snprintf(path, sizeof(path), "%s/scripts", live);
        mkdir(path, 0755);
        snprintf(path, sizeof(path), "%s/scripts/pack.manifest", live);
        f = fopen(path, "wb");
        assert(f);
        fclose(f);
        snprintf(path, sizeof(path), "%s/cache", live);
        mkdir(path, 0755);

        /* A launched server that exits before it answers: its log's last
         * lines reach the panel, and the status says why. */
        {
            struct ToriRSMapEdLane dying_lane = launch_lane;
            struct ToriRSMapEdLiveLink dying_link;
            snprintf(path, sizeof(path), "%s/dying_server.sh", scratch);
            f = fopen(path, "wb");
            assert(f);
            fputs("#!/bin/sh\necho \"torirsserver: booting on port $1\"\n"
                  "echo \"torirsserver: no server pack at $TORIRSSERVER_PACK_DIR -- run the packs build\"\nexit 1\n",
                f);
            fclose(f);
            chmod(path, 0755);
            snprintf(dying_lane.server_bin, sizeof(dying_lane.server_bin), "%s", path);
            dying_lane.js5_bin[0] = '\0';
            check(ToriRSMapEd_SetLane(maped, &dying_lane), "dying: lane rebound");
            check(ToriRSMapEd_LiveLinkNative(&dying_link, &dying_lane), "dying: native link");
            ToriRSMapEd_SetLiveLink(maped, &dying_link);
            mark(&a);
            Editor_LiveEncodeAttach(&payload, 1, "", "");
            send_live(&a, TORIRSMAPED_REQ_LIVE_ATTACH, &payload);
            {
                int64_t const deadline = now_ms() + 10000;
                while( a.model.state != TORIRSMAPED_LIVE_FAILED && now_ms() < deadline )
                {
                    pump();
                    usleep(5000);
                }
            }
            check(a.model.state == TORIRSMAPED_LIVE_FAILED && strstr(a.model.message, "exited (status 1)")
                      && strstr(a.model.message, "no server pack at"),
                "dying: the status names the exit and the server's own last line");
            if( !strstr(a.model.message, "no server pack at") )
                fprintf(stderr, "  status: %s\n", a.model.message);
            {
                int found = 0;
                for( int i = 0; i < a.model.log_count && i < 8; i++ )
                {
                    const char* text = Editor_LiveLogLine(&a.model, i);
                    if( text && strstr(text, "torirsserver: booting on port") )
                        found = 1;
                }
                check(found, "dying: the server log's last lines are in the panel's log");
            }
            /* SetLiveLink freed launch_link's binding when dying_link
             * replaced it: make a new one. */
            ToriRSMapEd_SetLane(maped, &launch_lane);
            check(ToriRSMapEd_LiveLinkNative(&launch_link, &launch_lane), "launch: native link again");
            ToriRSMapEd_SetLiveLink(maped, &launch_link);
        }

        mark(&a);
        Editor_LiveEncodeAttach(&payload, 1, "", "");
        send_live(&a, TORIRSMAPED_REQ_LIVE_ATTACH, &payload);
        {
            int64_t const deadline = now_ms() + 15000;
            while( a.model.state != TORIRSMAPED_LIVE_ATTACHED && now_ms() < deadline )
            {
                pump();
                usleep(5000);
            }
        }
        check(a.model.state == TORIRSMAPED_LIVE_ATTACHED && a.model.launched, "launch: attached to what it launched");
        peer_last("\"start\"", line, sizeof(line));
        snprintf(path, sizeof(path), "\"argv\": [\"%d\", \"--rev\", \"osrs241\"]", port);
        check(strstr(line, path) != NULL, "launch: torirsserver <port> --rev <rev>");
        snprintf(path, sizeof(path), "\"TORIRSSERVER_CACHE\": \"%s/cache\"", live);
        check(strstr(line, path) != NULL, "launch: TORIRSSERVER_CACHE is the live cache");
        snprintf(path, sizeof(path), "\"TORIRSSERVER_PACK_DIR\": \"%s/servpack\"", live);
        check(strstr(line, path) != NULL, "launch: TORIRSSERVER_PACK_DIR is the live server pack");
        snprintf(path, sizeof(path), "\"TORIRSSERVER_SCRIPTS\": \"%s/scripts\"", live);
        check(strstr(line, path) != NULL, "launch: TORIRSSERVER_SCRIPTS is the live script pack");
        {
            /* The fake js5_server writes its line when it gets to it. */
            int64_t const deadline = now_ms() + 5000;
            int found = 0;
            snprintf(path, sizeof(path), "%s.js5", g_peer_log);
            snprintf(line, sizeof(line), "--cache %s/cache --revision 241 --port %d --max-pending 65535", live,
                port + 1);
            while( !found && now_ms() < deadline )
            {
                size_t size;
                char* js5 = slurp(path, &size);
                found = js5 && strstr(js5, line);
                free(js5);
                if( !found )
                    usleep(5000);
            }
            check(found, "launch: js5_server --cache <live>/cache --revision 241 --port ...");
        }
        ToriRSMapEd_LiveLinkNativePids(&launch_link, pids);
        check(pid_alive(pids[0]) && pid_alive(pids[1]) && pids[2] == 0,
            "launch: game server and js5 running, no io_server without a manifest");
        mark(&a);
        send_live(&a, TORIRSMAPED_REQ_LIVE_DETACH, &payload);
        pump();
        check(!pid_alive(pids[0]) && !pid_alive(pids[1]), "launch: detach stops what it launched");
        check(a.model.state == TORIRSMAPED_LIVE_DETACHED, "launch: detached");
    }

    /* ---- the play client: the native launcher and RUN's play step -------------------- */
    {
        struct ToriRSMapEdLane play_lane = lane;
        struct ToriRSMapEdLiveLink play_link;
        struct ToriRSMapEdPlayLauncher launcher;
        char user[16];
        char pass[24];
        char play_log[1100];
        char text[1024];
        int first_pid;
        int pid_before;
        FILE* f;

        port = free_port();
        unsetenv("MOCK_PEER_TICKET");
        unsetenv("MOCK_PEER_NO_PLAYERS");
        unlink(g_peer_log);
        snprintf(play_log, sizeof(play_log), "%s.play", g_peer_log);
        unlink(play_log);
        snprintf(play_lane.server_bin, sizeof(play_lane.server_bin), "%s", peer_script);
        play_lane.js5_bin[0] = '\0';
        play_lane.io_bin[0] = '\0';
        play_lane.game_port = port;
        play_lane.js5_port = port + 1;
        snprintf(play_lane.play_bin, sizeof(play_lane.play_bin), "%s", play_script);
        snprintf(path, sizeof(path), "%s/play_src", scratch);
        mkdir(path, 0755);
        snprintf(play_lane.play_manifest, sizeof(play_lane.play_manifest), "%s/play_src/manifest_test.ini", scratch);
        f = fopen(play_lane.play_manifest, "wb");
        assert(f);
        fputs("[cache:boot]\ndir=../cache.sparse\n\n[net:boot]\ntransport=embed\nhost=localhost\nport=1\n\n"
              "[ui:boot]\nrevconfig_ui=../revconfig/x.ini\nkeep=plain\n",
            f);
        fclose(f);
        snprintf(play_lane.play_fixture, sizeof(play_lane.play_fixture), "%s/play_src/fixture.ini", scratch);
        f = fopen(play_lane.play_fixture, "wb");
        assert(f);
        fputs("[player]\nversion = 1\nname = fresh_lumbridge\nx = 3206\n", f);
        fclose(f);
        /* The editor's own capture keys must not reach the play client; its
         * TORIRSMAPED_PLAY_ENV_ keys must. */
        setenv("TORIRS_SIM_HOTKEY", "10,ctrl+return", 1);
        setenv("TORIRSMAPED_PLAY_ENV_TORIRS_TEST_MARK", "yes", 1);
        check(ToriRSMapEd_SetLane(maped, &play_lane), "play: lane rebound");
        check(ToriRSMapEd_LiveLinkNative(&play_link, &play_lane), "play: native link");
        ToriRSMapEd_SetLiveLink(maped, &play_link);
        check(ToriRSMapEd_PlayLauncherNative(&launcher, &play_lane), "play: native launcher");
        ToriRSMapEd_SetPlayLauncher(maped, &launcher);
        ToriRSMapEd_LanePlayAccount(&play_lane, user, sizeof(user), pass, sizeof(pass));

        /* (1) the lane is down: RUN builds, launches it, starts the client */
        add_spawn(&a, 3205);
        pump();
        mark(&a);
        mock->command_count = 0;
        send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
        at = wait_run_done(&a, &status, &step);
        check(at >= 0 && status == EDITOR_HOST_OK && step == TORIRSMAPED_RUN_STEP_DONE,
            "play: RUN with the lane down finishes OK");
        check(strstr(a.model.run_message, "the lane was not up: launched it") != NULL
                  && strstr(a.model.run_message, "play client started") != NULL,
            "play: RUN says it launched the lane and started the play client");
        if( !strstr(a.model.run_message, "play client started") )
            fprintf(stderr, "  RUN said: %s\n", a.model.run_message);
        check(mock->command_count == 1, "play: the RUN built packs and the client cache first, in one build");
        check(a.model.pending_squares == 0 && a.model.pending_spawns == 0 && a.model.pending_archives == 0,
            "play: a launch on the new build owes no reload");
        {
            int64_t const deadline = now_ms() + 15000;
            while( (a.model.play_pid <= 0 || a.model.state != TORIRSMAPED_LIVE_ATTACHED) && now_ms() < deadline )
            {
                pump();
                usleep(5000);
            }
        }
        check(a.model.play_state == TORIRSMAPED_PLAY_STARTING && a.model.play_pid > 0,
            "play: the client was spawned once the lane's port answered");
        check(a.model.state == TORIRSMAPED_LIVE_ATTACHED && a.model.launched, "play: the launched lane attached");
        check(strcmp(a.model.play_account, user) == 0, "play: the model names the play account");
        first_pid = a.model.play_pid;
        peer_last("\"start\"", line, sizeof(line));
        snprintf(path, sizeof(path), "\"TORIRSSERVER_SAVES\": \"%s/saves\"", live);
        check(strstr(line, path) != NULL, "play: the launched server keeps its saves in the lane");
        check(strstr(line, "\"TORIRSSERVER_SKIP_WELCOME\": \"1\"") != NULL,
            "play: the launched server skips the welcome screen");
        {
            int64_t const deadline = now_ms() + 5000;
            size_t size = 0;
            char* recorded = NULL;
            while( now_ms() < deadline )
            {
                free(recorded);
                recorded = slurp(play_log, &size);
                if( recorded && strstr(recorded, "TORIRS_TEST_MARK") )
                    break;
                usleep(5000);
            }
            snprintf(text, sizeof(text), "--manifest %s/play/manifest.ini --js5 --js5-host 127.0.0.1 --js5-port %d "
                                         "--js5-fallback-port 0 --user %s --pass %s",
                live, port + 1, user, pass);
            check(recorded && strstr(recorded, text), "play: the client's command line (manifest, js5, account)");
            if( !(recorded && strstr(recorded, text)) )
                fprintf(stderr, "  want %s\n  got  %s\n", text, recorded ? recorded : "(nothing)");
            snprintf(text, sizeof(text), "env TORIRS_PREFS=%s/play/preferences.ini", live);
            check(recorded && strstr(recorded, text), "play: TORIRS_PREFS is the lane's own");
            check(recorded && strstr(recorded, "env TORIRS_SIM_HOTKEY=<unset>")
                      && strstr(recorded, "env TORIRSMAPED_LIVE_DIR=<unset>"),
                "play: the editor's capture and lane keys are stripped");
            check(recorded && strstr(recorded, "env TORIRS_TEST_MARK=yes"),
                "play: TORIRSMAPED_PLAY_ENV_<NAME> reaches the client as <NAME>");
            free(recorded);
        }
        {
            size_t size;
            char* manifest;
            snprintf(path, sizeof(path), "%s/play/manifest.ini", live);
            manifest = slurp(path, &size);
            snprintf(text, sizeof(text), "dir=%s/play/cache\n", live);
            check(manifest && strstr(manifest, text), "play manifest: the cache is the lane's play cache");
            snprintf(text, sizeof(text), "port=%d\n", port);
            check(manifest && strstr(manifest, text) && strstr(manifest, "host=127.0.0.1\n")
                      && strstr(manifest, "transport=tcp\n"),
                "play manifest: tcp to the lane's game port");
            snprintf(text, sizeof(text), "revconfig_ui=%s/play_src/../revconfig/x.ini\n", scratch);
            check(manifest && strstr(manifest, text) && strstr(manifest, "keep=plain\n"),
                "play manifest: relative paths made absolute, the rest kept");
            free(manifest);
            snprintf(path, sizeof(path), "%s/saves/%s.ini", live, user);
            manifest = slurp(path, &size);
            snprintf(text, sizeof(text), "name = %s\n", user);
            check(manifest && strstr(manifest, text) && strstr(manifest, "x = 3206"),
                "play: the fixture is the account's first save, named for it");
            free(manifest);
        }

        /* (2) it logs in: the server's player list says so */
        snprintf(text, sizeof(text), "join %s", user);
        Editor_LiveEncodeCheat(&payload, "", text);
        send_live(&a, TORIRSMAPED_REQ_LIVE_CHEAT, &payload);
        {
            int64_t const deadline = now_ms() + 8000;
            while( a.model.play_state != TORIRSMAPED_PLAY_ONLINE && now_ms() < deadline )
            {
                pump();
                usleep(5000);
            }
        }
        check(a.model.play_state == TORIRSMAPED_PLAY_ONLINE && a.model.play_x == 3222 && a.model.play_z == 3218,
            "play: on the server's player list -> logged in, with its tile");
        check(a.model.players_known && a.model.logins == 1 && a.model.player_row_count == 1,
            "play: the model holds the server's players and login count");
        Editor_LivePlayLine(&a.model, text, sizeof(text));
        check(strstr(text, "logged in as") && strstr(text, "m50_50"), "play: the panel line names the square");

        /* (3) a RUN with the client on the server: it took the reload */
        add_spawn(&a, 3206);
        pump();
        mark(&a);
        {
            int const reloads = peer_count("\"reload\"");
            send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
            at = wait_run_done(&a, &status, &step);
            check(at >= 0 && status == EDITOR_HOST_OK && step == TORIRSMAPED_RUN_STEP_DONE, "play: RUN online OK");
            check(peer_count("\"reload\"") == reloads + 1, "play: RUN online reloaded the server");
        }
        check(strstr(a.model.run_message, "reloaded in the running client") != NULL,
            "play: RUN online -> \"reloaded in the running client\"");
        check(a.model.play_pid == first_pid && a.model.play_starts == 1 && pid_alive(first_pid),
            "play: RUN online started nothing (same pid, one start)");

        /* (4) it logs out: the next RUN starts it again */
        snprintf(text, sizeof(text), "leave %s", user);
        Editor_LiveEncodeCheat(&payload, "", text);
        send_live(&a, TORIRSMAPED_REQ_LIVE_CHEAT, &payload);
        {
            int64_t const deadline = now_ms() + 8000;
            while( a.model.play_state != TORIRSMAPED_PLAY_LOGGED_OUT && now_ms() < deadline )
            {
                pump();
                usleep(5000);
            }
        }
        check(a.model.play_state == TORIRSMAPED_PLAY_LOGGED_OUT, "play: off the player list, process alive -> logged out");
        add_spawn(&a, 3207);
        pump();
        mark(&a);
        send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
        at = wait_run_done(&a, &status, &step);
        check(at >= 0 && status == EDITOR_HOST_OK && strstr(a.model.run_message, "restarted the play client"),
            "play: RUN logged out -> restarted the play client");
        pump_for(300);
        check(a.model.play_starts == 2 && a.model.play_pid > 0 && a.model.play_pid != first_pid
                  && !pid_alive(first_pid),
            "play: the logged-out client was stopped and a new one started");

        /* (5) it dies: the service pass reaps it, the next RUN starts it again */
        pid_before = a.model.play_pid;
        kill((pid_t)pid_before, SIGKILL);
        {
            int64_t const deadline = now_ms() + 5000;
            while( a.model.play_state != TORIRSMAPED_PLAY_EXITED && now_ms() < deadline )
            {
                pump();
                usleep(5000);
            }
        }
        check(a.model.play_state == TORIRSMAPED_PLAY_EXITED && a.model.play_exit == -SIGKILL,
            "play: a killed client reads exited, with its signal");
        mark(&a);
        send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
        at = wait_run_done(&a, &status, &step);
        check(at >= 0 && status == EDITOR_HOST_OK && strstr(a.model.run_message, "it had exited"),
            "play: RUN after it died -> restarted (it had exited)");
        pump_for(200);
        check(a.model.play_starts == 3 && a.model.play_pid > 0 && a.model.play_pid != pid_before,
            "play: a third start");

        /* (6) Play while it is on its way in changes nothing; Stop stops it */
        pid_before = a.model.play_pid;
        mark(&a);
        Editor_LiveEncodePlay(&payload, TORIRSMAPED_PLAY_START);
        send_live(&a, TORIRSMAPED_REQ_LIVE_PLAY, &payload);
        check(wait_fact(&a, TORIRSMAPED_FACT_LIVE_PLAY, 2000) >= 0 && a.model.play_starts == 3
                  && a.model.play_pid == pid_before,
            "play: Play while starting starts nothing new");
        mark(&a);
        Editor_LiveEncodePlay(&payload, TORIRSMAPED_PLAY_STOP);
        send_live(&a, TORIRSMAPED_REQ_LIVE_PLAY, &payload);
        check(wait_fact(&a, TORIRSMAPED_FACT_LIVE_PLAY, 2000) >= 0 && a.model.play_state == TORIRSMAPED_PLAY_NONE
                  && !pid_alive(pid_before),
            "play: Stop stops it");
        mark(&a);
        Editor_LiveEncodePlay(&payload, TORIRSMAPED_PLAY_START);
        send_live(&a, TORIRSMAPED_REQ_LIVE_PLAY, &payload);
        pump_for(300);
        check(a.model.play_state == TORIRSMAPED_PLAY_STARTING && a.model.play_starts == 4 && a.model.play_pid > 0,
            "play: Play starts it again");

        /* (7) detaching the launched lane takes the play client with it */
        pid_before = a.model.play_pid;
        mark(&a);
        send_live(&a, TORIRSMAPED_REQ_LIVE_DETACH, &payload);
        pump_for(200);
        check(a.model.state == TORIRSMAPED_LIVE_DETACHED && a.model.play_state == TORIRSMAPED_PLAY_NONE
                  && !pid_alive(pid_before),
            "play: detach stops the play client with the lane");

        /* (8) a server older than CTL_PLAYERS: never asked, the link holds */
        setenv("MOCK_PEER_NO_PLAYERS", "1", 1);
        add_spawn(&a, 3208);
        pump();
        mark(&a);
        send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
        at = wait_run_done(&a, &status, &step);
        check(at >= 0 && status == EDITOR_HOST_OK, "play: RUN relaunches the lane (no players reported)");
        {
            int64_t const deadline = now_ms() + 15000;
            while( (a.model.play_pid <= 0 || a.model.state != TORIRSMAPED_LIVE_ATTACHED) && now_ms() < deadline )
            {
                pump();
                usleep(5000);
            }
        }
        add_spawn(&a, 3209);
        pump();
        mark(&a);
        send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
        at = wait_run_done(&a, &status, &step);
        check(at >= 0 && status == EDITOR_HOST_OK && strstr(a.model.run_message, "does not report its players"),
            "play: RUN on a server without CTL_PLAYERS -> the running client, said so");
        pump_for(200);
        check(a.model.state == TORIRSMAPED_LIVE_ATTACHED && !a.model.players_known && peer_count("\"unknown\"") == 0,
            "play: no CTL_PLAYERS was sent to it; the link holds");
        unsetenv("MOCK_PEER_NO_PLAYERS");

        /* (9) a browser window plays in a tab: no native client, the URL */
        pid_before = a.model.play_pid;
        mark(&a);
        Editor_LiveEncodePlay(&payload, TORIRSMAPED_PLAY_BROWSER);
        send_live(&a, TORIRSMAPED_REQ_LIVE_PLAY, &payload);
        check(wait_fact(&a, TORIRSMAPED_FACT_LIVE_PLAY, 2000) >= 0 && a.model.play_browser
                  && a.model.play_pid <= 0 && !pid_alive(pid_before),
            "play: a browser window stops the native client");
        mark(&a);
        send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
        at = wait_run_done(&a, &status, &step);
        check(at >= 0 && status == EDITOR_HOST_OK && strstr(a.model.run_message, "no play tab")
                  && a.model.play_pid <= 0,
            "play: RUN for a browser window starts nothing and names the (missing) web page");

        mark(&a);
        send_live(&a, TORIRSMAPED_REQ_LIVE_DETACH, &payload);
        pump();
        unsetenv("TORIRS_SIM_HOTKEY");
        unsetenv("TORIRSMAPED_PLAY_ENV_TORIRS_TEST_MARK");
    }

    /* the server closes with no link bound left running */
    ToriRSMapEd_SessionFree(&a.session);
    ToriRSMapEd_SessionFree(&b.session);
    ToriRSMapEd_Close(maped);
    free(maped);
    free(mock);
    if( peer > 0 )
        stop_pid(peer);

    printf("live_editor_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
