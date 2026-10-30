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
 *   - the client model (editor/live/live_client.h) folding every fact.
 *
 *   live_editor_test <scratch-dir> <peer-script> <fake-server-script> <repo-root>
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
    struct ToriRSMapEdBuf payload;
    int port;
    pid_t peer = -1;
    uint32_t status;
    uint32_t step;
    int at;

    if( argc != 5 )
    {
        fprintf(stderr, "usage: live_editor_test <scratch> <peer-script> <fake-server> <repo-root>\n");
        return 2;
    }
    scratch = argv[1];
    peer_script = argv[2];
    fake_script = argv[3];
    repo_root = argv[4];
    signal(SIGPIPE, SIG_IGN);
    ToriMem_Init();
    snprintf(content, sizeof(content), "%s/content", scratch);
    snprintf(live, sizeof(live), "%s/live", scratch);
    snprintf(g_peer_log, sizeof(g_peer_log), "%s/peer.log", scratch);
    setenv("MOCK_PEER_LOG", g_peer_log, 1);
    unsetenv("MOCK_PEER_TICKET");
    /* Open must not bind the native runner from the caller's environment. */
    unsetenv("TORIRSMAPED_LIVE_DIR");
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

        memset(&job, 0, sizeof(job));
        job.what = TORIRSMAPED_BUILD_PACKS;
        check(ToriRSMapEd_BuildCommand(&lane, &job, &command), "command: packs");
        ToriRSMapEd_BuildCommandFormat(&command, line, sizeof(line));
        snprintf(path, sizeof(path),
            "make -C %s/src --no-print-directory torirsserver-packs TORIRSSERVER_CONTENT_DIR=%s "
            "TORIRSSERVER_SCRIPT_OUT=%s/scripts TORIRSSERVER_PACKS_LANE_OUT=%s/scripts_lanes "
            "SERVPACK_OUT=%s/servpack",
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
        job.what = TORIRSMAPED_BUILD_CS2;
        check(!ToriRSMapEd_BuildCommand(&lane, &job, &command), "command: cs2 has none (UNSUPPORTED)");

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
    maped = calloc(1, sizeof(*maped));
    assert(maped);
    ToriRSMapEd_Open(maped, content, repo_root);
    check(!(maped->capabilities & TORIRSMAPED_CAP_BUILD), "no runner until one is bound");
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
    check(strstr(mock->commands[0], "torirsserver-packs") && strstr(mock->commands[0], "SERVPACK_OUT=")
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
    check(find_fact(&a, TORIRSMAPED_FACT_SAVED, a.mark) >= 0, "RUN unattached: it saved first");
    check(mock->command_count == 2 && mock->command_whats[0] == TORIRSMAPED_BUILD_PACKS
              && mock->command_whats[1] == TORIRSMAPED_BUILD_CLIENT_CACHE,
        "RUN unattached: packs, then the client cache");
    check(strcmp(mock->command_squares[1], "60_60") == 0, "RUN unattached: the client build names the saved square");
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
    check(strcmp(a.model.server_name, "mockworld") == 0 && a.model.players == 2, "attach: server name and players");
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
        int packs_done = -1;
        int client_done = -1;
        int live_line = -1;
        int status_after = find_fact(&b, TORIRSMAPED_FACT_LIVE_STATUS, at);
        for( int i = b.mark; i < at; i++ )
        {
            if( b.facts[i].type == TORIRSMAPED_FACT_BUILD_DONE && fact_u32(&b.facts[i], 0) == TORIRSMAPED_BUILD_PACKS )
                packs_done = i;
            if( b.facts[i].type == TORIRSMAPED_FACT_BUILD_DONE
                && fact_u32(&b.facts[i], 0) == TORIRSMAPED_BUILD_CLIENT_CACHE )
                client_done = i;
            if( b.facts[i].type == TORIRSMAPED_FACT_LIVE_LINE && live_line < 0 )
                live_line = i;
        }
        check(saved >= 0 && packs_done > saved && client_done > packs_done && live_line > client_done
                  && at > live_line && status_after > at,
            "RUN: SAVED < packs done < client done < reload line < RUN_DONE < LIVE_STATUS");
        check(fact_u32(&b.facts[at], 2) + 0 > 0, "RUN: the done carries a message");
    }
    check(mock->command_count == 2, "RUN: two builds asked for");
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
        mock->scripts[TORIRSMAPED_BUILD_PACKS].status = EDITOR_HOST_BAKE_FAILED;
        send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
        at = wait_run_done(&a, &status, &step);
        check(at >= 0 && status == EDITOR_HOST_BAKE_FAILED && step == TORIRSMAPED_RUN_STEP_PACKS,
            "fail packs: RUN stops at packs");
        check(mock->command_count == 1, "fail packs: no client cache build started");
        pump_for(200);
        check(peer_count("\"reload\"") == reloads_before, "fail packs: no RELOAD sent");
        check(a.model.pending_spawns == 1, "fail packs: the spawn file stays pending");
        mock->scripts[TORIRSMAPED_BUILD_PACKS].status = EDITOR_HOST_OK;

        mark(&a);
        mock->command_count = 0;
        mock->scripts[TORIRSMAPED_BUILD_CLIENT_CACHE].status = EDITOR_HOST_BAKE_FAILED;
        send_live(&a, TORIRSMAPED_REQ_RUN, &payload);
        at = wait_run_done(&a, &status, &step);
        check(at >= 0 && status == EDITOR_HOST_BAKE_FAILED && step == TORIRSMAPED_RUN_STEP_CLIENT_CACHE,
            "fail client cache: RUN stops there");
        check(mock->command_count == 2, "fail client cache: packs then the client build");
        pump_for(200);
        check(peer_count("\"reload\"") == reloads_before, "fail client cache: no RELOAD sent");
        mock->scripts[TORIRSMAPED_BUILD_CLIENT_CACHE].status = EDITOR_HOST_OK;
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
    mock->scripts[TORIRSMAPED_BUILD_PACKS].idle_polls = 50;
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
    mock->scripts[TORIRSMAPED_BUILD_PACKS].idle_polls = 0;

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
