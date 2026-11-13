/*
 * The replay scrubber (replay/replay_scrub.h) driving the replay transport
 * (platform/net_transport_replay.c) into a real rev-241 ToriRS_Network.
 *
 * The harness stands in for the client: a packet popped is a packet executed,
 * SERVER_TICK_ENDs are counted as App.server_tick_serial counts them and
 * handed over every frame, a REBUILD is answered with MAP_BUILD_COMPLETE, and
 * a session the transport drops is re-established the way app_net_link_watch
 * does it (logout, then a reconnect). A reconnect the transport answers with
 * the recorded RECONNECT_OK continues the session: its executed ticks go on
 * counting, and it is not a login.
 *
 * The recording is built here (net/rsprox_recording.h's layout): tick t
 * carries one VARP_SMALL whose variable is t, so the last variable executed
 * says exactly which ticks have run.
 *
 *   tick 0       the login REBUILD
 *   tick t       VARP t at 600 t, the tick end 300 ms later
 *   tick 10      a recorded chatbox answer (RESUME_P_OBJDIALOG)
 *   tick 20      a REBUILD: the map-build barrier
 *   tick 25      RSProx's RECONNECT
 *   tick 31..40  8 s later than that: a stall before tick 31
 *   41 ticks; no LOGOUT, so the transport appends one
 */
#include "cmd/cmdbus.h"
#include "mem/torirs_mem.h"
#include "net/net.h"
#include "net/net_out.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"
#include "net/rsprox_recording.h"
#include "net/torirs_control.h"
#include "platform/net_transport.h"
#include "replay/replay_scrub.h"
#include "replay/replay_world.h"

#include "packets/varp_small.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CHECK(cond)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                     \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

static char const* const TEST_RSA_E = "10001";
static char const* const TEST_RSA_N =
    "c30fcbc01e071ff224ea1a6508052d1140f87abaf8f40f7004efa59926708e5d99e2bc832fdca8276482dd0d690f"
    "644156850f47886f8032b3e9aa52508d24e8c9b7c50b8d8b8716fb8c3993bb6ce15e2124883edb7aaa7241a8b530"
    "f806c61cd1345879413fc105980a4f5fcdb3f0d743b14b16228b4d1496c83d3755a78a19";

#define LOCAL_INDEX 827
#define GPI_INIT_BYTES 4608
#define TIMESTAMP_MS 0x0000019a12345678LL
#define TICKS 41
#define STALL_MS 8000

/* --- writing a recording ------------------------------------------------- */

struct Rec
{
    uint8_t* data;
    size_t len;
    size_t cap;
    uint64_t last_ms;
};

static void
w1(struct Rec* r, int v)
{
    if( r->len == r->cap )
    {
        r->cap = r->cap ? r->cap * 2 : 8192;
        r->data = ToriMem_Realloc(ToriMem_Net(), r->data, r->cap);
        assert(r->data);
    }
    r->data[r->len++] = (uint8_t)v;
}

static void
w2(struct Rec* r, int v)
{
    w1(r, v >> 8);
    w1(r, v);
}

static void
w4(struct Rec* r, uint32_t v)
{
    w2(r, (int)(v >> 16));
    w2(r, (int)(v & 0xffff));
}

static void
wstr(struct Rec* r, char const* s)
{
    while( *s )
        w1(r, *s++);
    w1(r, 0);
}

static void
w_header(struct Rec* r)
{
    w4(r, 1);
    w4(r, 241);
    w4(r, 1);
    w1(r, 1);
    w1(r, 5);
    w4(r, (uint32_t)(TIMESTAMP_MS >> 32));
    w4(r, (uint32_t)(TIMESTAMP_MS & 0xffffffff));
    w2(r, 631);
    w4(r, 1);
    w1(r, 0);
    wstr(r, "oldschool331.runescape.com");
    wstr(r, "-");
    w2(r, LOCAL_INDEX);
    w2(r, 0);
    wstr(r, "RuneLite (Win)");
    w4(r, 0);
}

static void
w_packet_head(
    struct Rec* r,
    uint64_t t_ms,
    int from_server,
    int opcode)
{
    uint32_t const packed = (uint32_t)((t_ms - r->last_ms) << 1) | (uint32_t)from_server;
    uint8_t groups[5];
    int n = 0;

    r->last_ms = t_ms;
    groups[n++] = (uint8_t)(packed & 0x7f);
    for( uint32_t v = packed >> 7; v; v >>= 7 )
        groups[n++] = (uint8_t)((v & 0x7f) | 0x80);
    while( n > 0 )
        w1(r, groups[--n]);
    if( from_server && opcode >= 0x80 )
    {
        w1(r, 0x80 | (opcode >> 8));
        w1(r, opcode & 0xff);
    }
    else
        w1(r, opcode);
}

static void
w_server(
    struct Rec* r,
    struct GameProtoRevTable const* rev,
    uint64_t t_ms,
    int pkt_name,
    uint8_t const* payload,
    int len)
{
    int const op = rev->packetin_wire(pkt_name);
    int const size = rev->packetin_size(op);

    TEST_CHECK(op >= 0);
    w_packet_head(r, t_ms, 1, op);
    if( size == PKTIN_LENGTH_VARU8 )
        w1(r, len);
    else if( size == PKTIN_LENGTH_VARU16 )
        w2(r, len);
    else
        TEST_CHECK(size == len);
    for( int i = 0; i < len; i++ )
        w1(r, payload ? payload[i] : 0);
}

static void
w_client(
    struct Rec* r,
    struct GameProtoRevTable const* rev,
    uint64_t t_ms,
    int pkt_out_name,
    int len)
{
    int const op = rev->packetout_code(pkt_out_name);
    int const size = rev->packetout_size(op);

    TEST_CHECK(op >= 0);
    w_packet_head(r, t_ms, 0, op);
    if( size == PKTIN_LENGTH_VARU8 )
        w1(r, len);
    else if( size == PKTIN_LENGTH_VARU16 )
        w2(r, len);
    else
        TEST_CHECK(size == len);
    for( int i = 0; i < len; i++ )
        w1(r, 0x5a);
}

static void
rebuild_tail(
    uint8_t* out,
    int zone_x,
    int zone_z)
{
    out[0] = (uint8_t)(zone_z >> 8);
    out[1] = (uint8_t)zone_z;
    out[2] = (uint8_t)zone_x;
    out[3] = (uint8_t)(zone_x >> 8);
    out[4] = 0;
    out[5] = 0;
}

static void
w_varp(
    struct Rec* r,
    struct GameProtoRevTable const* rev,
    uint64_t t_ms,
    int id)
{
    uint8_t buf[16];
    MsgVarpSmall m;
    RsprotBuf b;
    RsprotExec x;

    m.id = id;
    m.value = 1;
    rsprot_buf_wrap(&b, buf, (int32_t)sizeof(buf));
    rsprot_exec_encode(&x, &b);
    packet_varp_small_osrs241_out(&x, &m);
    TEST_CHECK(rsprot_exec_ok(&x));
    w_server(r, rev, t_ms, PKT_NAME_VARP_SMALL, buf, (int)b.wpos);
}

static uint64_t
tick_ms(int t)
{
    return (uint64_t)(600 * t) + (t >= 31 ? STALL_MS : 0);
}

static struct Rec
build_recording(struct GameProtoRevTable const* rev)
{
    struct Rec r;
    static uint8_t login_rebuild[GPI_INIT_BYTES + 6];
    static uint8_t gpi[GPI_INIT_BYTES];
    uint8_t rebuild[6];

    memset(&r, 0, sizeof(r));
    w_header(&r);
    memset(login_rebuild, 0, sizeof(login_rebuild));
    rebuild_tail(login_rebuild + GPI_INIT_BYTES, 365, 435);
    w_server(&r, rev, 3, PKT_NAME_REBUILD_NORMAL, login_rebuild, (int)sizeof(login_rebuild));
    w_server(&r, rev, 6, PKT_NAME_SERVER_TICK_END, NULL, 0);
    for( int t = 1; t < TICKS; t++ )
    {
        uint64_t const at = tick_ms(t);

        w_varp(&r, rev, at, t);
        if( t == 10 )
            w_client(&r, rev, at + 100, PKTOUT_NAME_RESUME_P_OBJDIALOG, 2);
        if( t == 20 )
        {
            rebuild_tail(rebuild, 363, 434);
            w_server(&r, rev, at + 1, PKT_NAME_REBUILD_NORMAL, rebuild, 6);
        }
        if( t == 25 )
        {
            memset(gpi, 0, sizeof(gpi));
            w_packet_head(&r, at + 1, 1, RSPROX_RECORDING_RECONNECT);
            w2(&r, GPI_INIT_BYTES);
            for( int i = 0; i < GPI_INIT_BYTES; i++ )
                w1(&r, gpi[i]);
        }
        w_server(&r, rev, at + 300, PKT_NAME_SERVER_TICK_END, NULL, 0);
    }
    return r;
}

/* --- the client stand-in ------------------------------------------------- */

struct Client
{
    struct GameProtoRevTable const* rev;
    struct ToriRS_Network net;
    struct ToriRS_CmdBus* bus;
    struct NetTransport* t;
    unsigned long long cycles;
    /** Logic cycles a frame (App's k). */
    int k;
    int frames;
    /** SERVER_TICK_ENDs executed, ever: App.server_tick_serial. */
    unsigned serial;
    unsigned session_serial;
    int in_game;
    int logins;
    /** The last VARP's variable this session (-1: none). */
    int last_varp;
    /** Every VARP executed, in order, ever. */
    int varps[512];
    int varp_count;
    int popped;
    int mid_tick;
    int logouts;
    int dialogs;
    /** Sessions continued over a recorded RECONNECT (the transport's
     *  `resumed`, last seen). */
    int resumed;
    int reconnects;
    /** varp_count when the dialog / RECONNECT arrived. */
    int dialog_at;
    int reconnect_at;
    /** Frame the VARP of tick 5 and of tick 31 arrived on (the clock). */
    int varp5_frame;
    int varp31_frame;
    /** The fast plan's control frames (net/torirs_control.h): chase tiles,
     *  keyframes (and how many asked for a rebuild); REBUILDs executed. */
    int coords;
    int worlds;
    int world_rebuilds;
    int world_tick;
    int rebuilds;
};

static struct ToriRS_CmdBus s_bus;

static void
client_open(
    struct Client* c,
    int k)
{
    struct GameProtoRevTable const* rev = GameProtoRev_Osrs(241);
    struct Rec r = build_recording(rev);
    struct RsproxRecording rec;
    char err[256];

    memset(c, 0, sizeof(*c));
    c->rev = rev;
    c->k = k;
    c->last_varp = -1;
    c->bus = &s_bus;
    TEST_CHECK(RsproxRecording_Open(&rec, r.data, r.len, err, sizeof(err)) == 0);
    TEST_CHECK(RsproxRecording_Index(&rec, rev, err, sizeof(err)) == 0);
    TEST_CHECK(rec.tick_count == TICKS);
    ToriRS_Network_Init(&c->net, rev, TEST_RSA_E, TEST_RSA_N);
    CmdBus_Init(c->bus);
    c->t = NetTransport_NewReplay(&rec, rev);
    /* The tests below pin the full-simulation plan's phases whatever the
     * default plan is; test_fast_seek asks for the fast one itself. */
    NetTransport_ReplaySetSeekFast(c->t, 0);
    NetTransport_ReplaySetUnlocked(c->t, 1);
    ToriRS_Network_ConnectLogin(&c->net, "replay", "replay", "replay");
}

static void
client_close(struct Client* c)
{
    NetTransport_Free(c->t);
    ToriRS_Network_Free(&c->net);
}

static int
executed(struct Client const* c)
{
    return c->in_game ? (int)(c->serial - c->session_serial) : 0;
}

static void
frame(struct Client* c)
{
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    struct RevPacket packet;
    int rebuilt = 0;

    c->frames++;
    c->cycles += (unsigned long long)c->k;
    NetTransport_ReplayClientTicks(c->t, c->serial);
    NetTransport_ReplayClock(c->t, c->cycles);
    NetTransport_Poll(c->t, &c->net, c->bus);
    while( CmdBus_Pop(c->bus, &header, payload) )
        ToriRS_Network_HandleCmd(&c->net, header.type, payload, (int)header.length);
    if( c->net.state == TORIRS_NET_GAME && !c->in_game )
    {
        struct NetTransportReplayStatus st;

        NetTransport_ReplayGetStatus(c->t, &st);
        c->in_game = 1;
        if( st.resumed > c->resumed )
        {
            /* The recorded RECONNECT's RECONNECT_OK: the same session. */
            c->resumed = st.resumed;
            c->reconnects++;
            c->reconnect_at = c->varp_count;
        }
        else
        {
            c->logins++;
            c->session_serial = c->serial;
            c->last_varp = -1;
        }
    }
    while( ToriRS_Network_PopPacket(&c->net, &packet) )
    {
        c->popped++;
        c->mid_tick = packet.packet_type != PKT_NAME_SERVER_TICK_END;
        if( packet.packet_type == PKT_NAME_SERVER_TICK_END )
            c->serial++;
        else if( packet.packet_type == PKT_NAME_VARP_SMALL )
        {
            c->last_varp = packet._varp_small.variable;
            TEST_CHECK(c->varp_count < (int)(sizeof(c->varps) / sizeof(c->varps[0])));
            c->varps[c->varp_count++] = c->last_varp;
            if( c->last_varp == 5 && !c->varp5_frame )
                c->varp5_frame = c->frames;
            if( c->last_varp == 31 && !c->varp31_frame )
                c->varp31_frame = c->frames;
        }
        else if( packet.packet_type == PKT_NAME_REBUILD_NORMAL )
        {
            rebuilt = 1;
            c->rebuilds++;
        }
        else if( packet._control && packet._control->type == TORIRS_CONTROL_REPLAY_COORD )
        {
            struct TorirsControlReplayCoord coord;

            TEST_CHECK(torirs_control_replay_coord_decode(packet._control, &coord));
            c->coords++;
        }
        else if( packet._control && packet._control->type == TORIRS_CONTROL_REPLAY_WORLD )
        {
            /* What the executor does with it: take the keyframe the frame
             * names -- the one in the mailbox -- and a rebuild is answered
             * with MAP_BUILD_COMPLETE, as the REBUILD arm answers. */
            struct TorirsControlReplayWorld w;
            struct ReplayWorldDelivery* d;

            TEST_CHECK(torirs_control_replay_world_decode(packet._control, &w));
            d = ReplayWorld_Take(w.serial);
            TEST_CHECK(d);
            TEST_CHECK(d->tick == w.tick);
            c->worlds++;
            c->world_tick = d->tick;
            TEST_CHECK(d->kf.tick == d->tick);
            if( d->rebuild )
            {
                TEST_CHECK(d->scene_bytes);
                TEST_CHECK(d->scene_len > 0);
                c->world_rebuilds++;
                rebuilt = 1;
            }
            ReplayWorldDelivery_Free(d);
        }
        else if( packet._control && packet._control->type == TORIRS_CONTROL_REPLAY_DIALOG )
        {
            int name = 0;

            TEST_CHECK(torirs_control_replay_dialog_decode(packet._control, &name));
            TEST_CHECK(name == PKTOUT_NAME_RESUME_P_OBJDIALOG);
            c->dialogs++;
            c->dialog_at = c->varp_count;
        }
        else if( packet.packet_type == PKT_NAME_LOGOUT )
            c->logouts++;
        gameproto_free(&packet);
    }
    if( rebuilt )
    {
        uint8_t out[16];
        int const n = net_out_map_build_complete(c->rev, c->net.random_out, out, (int)sizeof(out));

        TEST_CHECK(n > 0);
        ToriRS_Network_SendRaw(&c->net, out, n);
    }
    /* The server went away: what app_net_link_watch does about it. */
    if( c->in_game && c->net.state == TORIRS_NET_DISCONNECTED )
    {
        c->in_game = 0;
        ToriRS_Network_Logout(&c->net);
        TEST_CHECK(ToriRS_Network_Reconnect(&c->net));
    }
}

static struct NetTransportReplayStatus
status(struct Client* c)
{
    struct NetTransportReplayStatus st;

    NetTransport_ReplayGetStatus(c->t, &st);
    return st;
}

#define RUN_UNTIL(c, cond, max_frames)                                                             \
    do                                                                                             \
    {                                                                                              \
        for( int i_ = 0; i_ < (max_frames) && !(cond); i_++ )                                      \
            frame(c);                                                                              \
        TEST_CHECK(cond);                                                                          \
    } while( 0 )

static void
run_frames(
    struct Client* c,
    int n)
{
    for( int i = 0; i < n; i++ )
        frame(c);
}

/* Held at `tick`: every packet of ticks < tick executed and none later, and
 * nothing more arrives however long it holds; the clock stands still. */
static void
check_held_at(
    struct Client* c,
    int tick)
{
    int const popped = c->popped;
    struct NetTransportReplayStatus st = status(c);
    unsigned long long const clock_ms = st.clock_ms;

    TEST_CHECK(st.seeking == 0);
    TEST_CHECK(st.held == 1);
    TEST_CHECK(st.tick == tick);
    TEST_CHECK(st.seek_target == -1);
    TEST_CHECK(st.seek_from == -1);
    TEST_CHECK(strcmp(st.seek_phase, "") == 0);
    TEST_CHECK(executed(c) == tick);
    TEST_CHECK(c->last_varp == tick - 1);
    TEST_CHECK(!c->mid_tick);
    run_frames(c, 200);
    st = status(c);
    TEST_CHECK(c->popped == popped);
    TEST_CHECK(st.clock_ms == clock_ms);
    TEST_CHECK(st.tick == tick);
    TEST_CHECK(executed(c) == tick);
}

/* --- the checks ---------------------------------------------------------- */

/* Nothing asked of the scrubber: the recording plays as it always has. */
static void
test_normal_play(void)
{
    struct Client c;
    struct NetTransportReplayStatus st;

    client_open(&c, 1);
    RUN_UNTIL(&c, c.logouts == 1, 5000);
    TEST_CHECK(c.logins == 1);
    TEST_CHECK(c.serial == TICKS);
    TEST_CHECK(c.varp_count == TICKS - 1);
    for( int i = 0; i < c.varp_count; i++ )
        TEST_CHECK(c.varps[i] == i + 1);
    st = status(&c);
    /* The answer goes after tick 10's VARP; the connection ends after tick
     * 25's and the session goes on over the next one. */
    TEST_CHECK(c.dialogs == 1);
    TEST_CHECK(c.dialog_at == 10);
    TEST_CHECK(c.reconnects == 1);
    TEST_CHECK(c.reconnect_at == 25);
    TEST_CHECK(st.sessions == 2);
    TEST_CHECK(st.resumed == 1);
    /* On the recording's clock, one cycle a frame (20 ms): tick 5 at 3 s.
     * The stall before tick 31 is cut to REPLAY_MAX_GAP_MS. */
    TEST_CHECK(c.varp5_frame >= 150);
    TEST_CHECK(c.varp5_frame <= 165);
    TEST_CHECK(c.varp31_frame < (int)((tick_ms(30) + 300 + 5000) / 20) + 30);
    TEST_CHECK(c.varp31_frame > (int)((tick_ms(30) + 300) / 20) + 200);
    st = status(&c);
    TEST_CHECK(strcmp(st.phase, "finished") == 0);
    TEST_CHECK(st.tick == TICKS - 1);
    TEST_CHECK(st.tick_count == TICKS);
    TEST_CHECK(st.held == 0);
    TEST_CHECK(st.seeking == 0);
    TEST_CHECK(st.timestamp_ms == TIMESTAMP_MS);
    client_close(&c);
}

/* Ahead: the work goes on to the boundary. Behind: the session restarts
 * from the login and plays to it. Then a seek to where it already is. */
static void
test_seek_forward_and_back(void)
{
    struct Client c;
    struct NetTransportReplayStatus st;

    client_open(&c, 10);
    RUN_UNTIL(&c, executed(&c) >= 3, 1000);

    NetTransport_ReplayHold(c.t);
    NetTransport_ReplaySeek(c.t, 15);
    st = status(&c);
    TEST_CHECK(st.seeking == 1);
    TEST_CHECK(st.seek_target == 15);
    /* The client's count as it was handed over at the start of the frame. */
    TEST_CHECK(st.seek_from >= 2);
    TEST_CHECK(st.held == 1);
    TEST_CHECK(strcmp(st.seek_phase, "preroll") == 0);
    TEST_CHECK(NetTransport_ReplaySeeking(c.t));
    TEST_CHECK(NetTransport_ReplayUnlocked(c.t));
    RUN_UNTIL(&c, !status(&c).seeking, 2000);
    TEST_CHECK(c.logins == 1);
    check_held_at(&c, 15);

    /* Behind the playhead. */
    NetTransport_ReplaySeek(c.t, 5);
    st = status(&c);
    TEST_CHECK(st.seeking == 1);
    TEST_CHECK(st.seek_target == 5);
    TEST_CHECK(st.seek_from == 0);
    TEST_CHECK(strcmp(st.seek_phase, "restart") == 0);
    RUN_UNTIL(&c, !status(&c).seeking, 2000);
    TEST_CHECK(c.logins == 2);
    TEST_CHECK(status(&c).sessions == 2);
    check_held_at(&c, 5);

    /* Where it already is: still a seek until the next frame. */
    NetTransport_ReplaySeek(c.t, 5);
    TEST_CHECK(status(&c).seeking == 1);
    frame(&c);
    TEST_CHECK(c.logins == 2);
    check_held_at(&c, 5);

    /* Run: tick 5 is the next thing executed. */
    {
        int const from = c.varp_count;

        NetTransport_ReplayRun(c.t);
        TEST_CHECK(status(&c).held == 0);
        RUN_UNTIL(&c, c.varp_count > from, 200);
        TEST_CHECK(c.varps[from] == 5);
    }
    client_close(&c);
}

/* A hold mid-tick stops at the end of that tick; step forward, step back. */
static void
test_hold_and_step(void)
{
    struct Client c;

    client_open(&c, 1);
    RUN_UNTIL(&c, c.last_varp == 7 && c.mid_tick, 1000);
    NetTransport_ReplayHold(c.t);
    TEST_CHECK(status(&c).held == 1);
    TEST_CHECK(status(&c).tick == 8);
    run_frames(&c, 100);
    check_held_at(&c, 8);
    TEST_CHECK(status(&c).clock_ms <= tick_ms(8));

    NetTransport_ReplayStep(c.t, 3);
    TEST_CHECK(status(&c).seeking == 1);
    TEST_CHECK(status(&c).seek_target == 11);
    RUN_UNTIL(&c, !status(&c).seeking, 2000);
    check_held_at(&c, 11);
    TEST_CHECK(c.dialogs == 1); /* tick 10's answer went on the way */

    NetTransport_ReplayStep(c.t, -2);
    TEST_CHECK(status(&c).seek_target == 9);
    /* A step while a seek is in flight counts from its target. */
    NetTransport_ReplayStep(c.t, -1);
    TEST_CHECK(status(&c).seek_target == 8);
    RUN_UNTIL(&c, !status(&c).seeking, 4000);
    TEST_CHECK(c.logins == 2);
    check_held_at(&c, 8);
    client_close(&c);
}

/* A new target replaces a seek in flight: ahead of the work, it carries on;
 * behind it, the session restarts. */
static void
test_seek_replaced(void)
{
    struct Client c;
    int const logins_before = 1;

    client_open(&c, 10);
    RUN_UNTIL(&c, executed(&c) >= 2, 1000);
    NetTransport_ReplaySeek(c.t, 30);
    RUN_UNTIL(&c, executed(&c) >= 10, 2000);
    TEST_CHECK(status(&c).seeking == 1);
    NetTransport_ReplaySeek(c.t, 20);
    TEST_CHECK(status(&c).seek_target == 20);
    TEST_CHECK(strcmp(status(&c).seek_phase, "preroll") == 0);
    NetTransport_ReplayHold(c.t);
    RUN_UNTIL(&c, !status(&c).seeking, 2000);
    TEST_CHECK(c.logins == logins_before);
    check_held_at(&c, 20);
    /* No tick skipped or played twice on the way. */
    for( int i = 0; i < c.varp_count; i++ )
        TEST_CHECK(c.varps[i] == i + 1);

    NetTransport_ReplayRun(c.t);
    NetTransport_ReplaySeek(c.t, 38);
    RUN_UNTIL(&c, executed(&c) >= 26, 2000);
    TEST_CHECK(status(&c).seeking == 1);
    NetTransport_ReplaySeek(c.t, 10);
    NetTransport_ReplayHold(c.t);
    RUN_UNTIL(&c, !status(&c).seeking, 4000);
    TEST_CHECK(c.logins == 2);
    check_held_at(&c, 10);
    client_close(&c);
}

/* The end is a tick too: held there, the session stays; run, it ends. A
 * target past it is the end. */
static void
test_seek_to_the_end(void)
{
    struct Client c;

    client_open(&c, 10);
    RUN_UNTIL(&c, executed(&c) >= 1, 1000);
    NetTransport_ReplayHold(c.t);
    NetTransport_ReplaySeek(c.t, 1000);
    TEST_CHECK(status(&c).seek_target == TICKS);
    RUN_UNTIL(&c, !status(&c).seeking, 4000);
    check_held_at(&c, TICKS);
    TEST_CHECK(c.logouts == 0);
    TEST_CHECK(strcmp(status(&c).phase, "playing") == 0);
    NetTransport_ReplayRun(c.t);
    RUN_UNTIL(&c, c.logouts == 1, 100);
    TEST_CHECK(strcmp(status(&c).phase, "finished") == 0);
    client_close(&c);
}

/* Every VARP of this session's ticks 1..n-1 executed once, in order. */
static void
check_session_varps(
    struct Client const* c,
    int from_index,
    int n)
{
    TEST_CHECK(c->varp_count - from_index == n - 1);
    for( int i = 0; i < n - 1; i++ )
        TEST_CHECK(c->varps[from_index + i] == i + 1);
}

/*
 * The fast plan: chase (session packets only, a tile a tick), keyframe,
 * pre-roll. Forward far from the live client; backward through a restart
 * whose chase withholds tick 0's REBUILD too; backward into the scene the
 * client already has, which keeps it; a short step, which is a full
 * simulation.
 */
static void
test_fast_seek(void)
{
    struct Client c;
    struct NetTransportReplayStatus st;
    int session_from;

    client_open(&c, 10);
    NetTransport_ReplaySetSeekFast(c.t, 1);
    RUN_UNTIL(&c, executed(&c) >= 2, 1000);
    TEST_CHECK(c.rebuilds == 1); /* the login's, played normally */

    /* Forward far: P = 22, after tick 20's REBUILD, which the chase withholds
     * and the keyframe rebuilds from. */
    NetTransport_ReplayHold(c.t);
    NetTransport_ReplaySeek(c.t, 38);
    st = status(&c);
    TEST_CHECK(st.seeking == 1);
    TEST_CHECK(strcmp(st.seek_phase, "chase") == 0);
    RUN_UNTIL(&c, !status(&c).seeking, 4000);
    check_held_at(&c, 38);
    TEST_CHECK(c.logins == 1);
    TEST_CHECK(c.rebuilds == 1);
    TEST_CHECK(c.worlds == 1);
    TEST_CHECK(c.world_tick == 22);
    TEST_CHECK(c.world_rebuilds == 1);
    TEST_CHECK(c.coords >= 18);
    /* Session packets are never withheld: every VARP, once, in order; the
     * chase's dialog answer and the pre-roll's RECONNECT where they were. */
    check_session_varps(&c, 0, 38);
    TEST_CHECK(c.dialogs == 1);
    TEST_CHECK(c.dialog_at == 10);
    TEST_CHECK(c.reconnects == 1);
    TEST_CHECK(c.reconnect_at == 25);

    /* Backward into the scene the client has (the keyframe's, tick 20's):
     * a restart and a chase from the login, its REBUILD withheld, and no
     * scene built at all. */
    session_from = c.varp_count;
    NetTransport_ReplaySeek(c.t, 37);
    TEST_CHECK(strcmp(status(&c).seek_phase, "restart") == 0);
    RUN_UNTIL(&c, !status(&c).seeking, 4000);
    check_held_at(&c, 37);
    TEST_CHECK(c.logins == 2);
    TEST_CHECK(c.worlds == 2);
    TEST_CHECK(c.world_tick == 21);
    TEST_CHECK(c.world_rebuilds == 1);
    TEST_CHECK(c.rebuilds == 1);
    check_session_varps(&c, session_from, 37);

    /* Backward into another scene (the login's): one build, the keyframe's. */
    session_from = c.varp_count;
    NetTransport_ReplaySeek(c.t, 30);
    RUN_UNTIL(&c, !status(&c).seeking, 4000);
    check_held_at(&c, 30);
    TEST_CHECK(c.logins == 3);
    TEST_CHECK(c.worlds == 3);
    TEST_CHECK(c.world_tick == 14);
    TEST_CHECK(c.world_rebuilds == 2);
    TEST_CHECK(c.rebuilds == 2); /* tick 20's, in the pre-roll */
    check_session_varps(&c, session_from, 30);

    /* Within the pre-roll: every tick played, no keyframe. */
    NetTransport_ReplaySeek(c.t, 35);
    TEST_CHECK(strcmp(status(&c).seek_phase, "preroll") == 0);
    RUN_UNTIL(&c, !status(&c).seeking, 2000);
    check_held_at(&c, 35);
    TEST_CHECK(c.worlds == 3);
    TEST_CHECK(c.logins == 3);
    client_close(&c);
}

/*
 * Back across a recorded reconnect: held past tick 25's RECONNECT (one
 * session, two connections), a seek to 20 starts again from the login -- the
 * session there is the first connection's -- and lands with every tick once;
 * playing on crosses the RECONNECT again, reconnecting again, and a forward
 * seek across it from before it lands too. Both plans.
 */
static void
test_seek_back_across_a_reconnect(void)
{
    for( int fast = 0; fast <= 1; fast++ )
    {
        struct Client c;
        int session_from;

        client_open(&c, 10);
        NetTransport_ReplaySetSeekFast(c.t, fast);
        RUN_UNTIL(&c, executed(&c) >= 2, 1000);
        NetTransport_ReplayHold(c.t);
        NetTransport_ReplaySeek(c.t, 30);
        RUN_UNTIL(&c, !status(&c).seeking, 4000);
        check_held_at(&c, 30);
        TEST_CHECK(c.logins == 1);
        TEST_CHECK(c.reconnects == 1);
        TEST_CHECK(c.reconnect_at == 25);
        check_session_varps(&c, 0, 30);

        /* Back across it: the login again, executed counted from it. */
        session_from = c.varp_count;
        NetTransport_ReplaySeek(c.t, 20);
        RUN_UNTIL(&c, !status(&c).seeking, 4000);
        check_held_at(&c, 20);
        TEST_CHECK(c.logins == 2);
        TEST_CHECK(c.reconnects == 1);
        check_session_varps(&c, session_from, 20);

        /* Forward across it from before it: a reconnect on the way, the
         * tick count unbroken. */
        NetTransport_ReplaySeek(c.t, 40);
        RUN_UNTIL(&c, !status(&c).seeking, 4000);
        check_held_at(&c, 40);
        TEST_CHECK(c.logins == 2);
        TEST_CHECK(c.reconnects == 2);
        TEST_CHECK(c.reconnect_at - session_from == 25);
        check_session_varps(&c, session_from, 40);
        TEST_CHECK(status(&c).resumed == 2);
        client_close(&c);
    }
}

/* --- the stalled-seek watchdog, on a clock the test steps -------------------- */

/*
 * The scrubber alone (no transport), with the wall clock in the test's hand:
 * what the transport hands out goes into a queue the stand-in executes from,
 * a REBUILD can be made to take as long as the test likes, and the client's
 * "held on a load" is reported the way main.c reports it.
 */
struct Bare
{
    struct GameProtoRevTable const* rev;
    struct RsproxRecording rec;
    struct ReplayScrub* scrub;
    uint64_t wall;
    unsigned serial;
    int queue[1024];
    int queue_head;
    int queue_tail;
    /** A REBUILD after the login's is being built until this wall time. */
    uint64_t build_until;
    int building;
    int busy_reported;
    int restarts;
};

static void
bare_open(struct Bare* b)
{
    struct Rec r;
    char err[256];

    memset(b, 0, sizeof(*b));
    b->rev = GameProtoRev_Osrs(241);
    r = build_recording(b->rev);
    TEST_CHECK(RsproxRecording_Open(&b->rec, r.data, r.len, err, sizeof(err)) == 0);
    TEST_CHECK(RsproxRecording_Index(&b->rec, b->rev, err, sizeof(err)) == 0);
    b->scrub = ReplayScrub_New(&b->rec, b->rev);
    b->wall = 1000;
    ReplayScrub_Wall(b->scrub, b->wall);
    ReplayScrub_ClientTicks(b->scrub, 0);
    ReplayScrub_SessionBegin(b->scrub);
}

static void
bare_close(struct Bare* b)
{
    ReplayScrub_Free(b->scrub);
    RsproxRecording_Free(&b->rec);
}

/* One client frame `dt` ms after the last: the client executes what it holds
 * (`execute`: 0 is a client that stopped), then the poll. A REBUILD past tick 0
 * holds the client `build_ms` (reported busy when `report_busy`). */
static void
bare_frame(
    struct Bare* b,
    uint64_t dt,
    int execute,
    uint64_t build_ms,
    int report_busy)
{
    struct ReplayStep step;
    enum ReplayNext what;

    b->wall += dt;
    if( b->building && b->wall >= b->build_until )
    {
        b->building = 0;
        ReplayScrub_MapBuilt(b->scrub);
    }
    while( execute && !b->building && b->queue_head < b->queue_tail )
    {
        struct RsproxRecordingFrame const* f = &b->rec.frames[b->queue[b->queue_head++]];
        int const name = b->rev->packetin_code(f->opcode);

        if( name == PKT_NAME_SERVER_TICK_END )
            b->serial++;
        else if( name == PKT_NAME_REBUILD_NORMAL )
        {
            if( f->tick > 0 && build_ms > 0 )
            {
                b->building = 1;
                b->build_until = b->wall + build_ms;
            }
            else
                ReplayScrub_MapBuilt(b->scrub);
        }
    }
    b->busy_reported = report_busy && b->building;
    ReplayScrub_ClientTicks(b->scrub, b->serial);
    ReplayScrub_ClientBusy(b->scrub, b->busy_reported);
    ReplayScrub_Wall(b->scrub, b->wall);
    ReplayScrub_Advance(b->scrub, dt, b->wall);
    while( (what = ReplayScrub_Next(b->scrub, &step)) != REPLAY_NEXT_WAIT )
    {
        if( what == REPLAY_NEXT_RESTART )
        {
            b->restarts++;
            return;
        }
        if( what == REPLAY_NEXT_FRAME )
        {
            TEST_CHECK(b->queue_tail < (int)(sizeof(b->queue) / sizeof(b->queue[0])));
            b->queue[b->queue_tail++] = step.frame;
        }
        TEST_CHECK(what != REPLAY_NEXT_END);
    }
}

/*
 * A scene build that takes a minute -- a cold cache fetch in a browser: the
 * barrier holds the clock for its 10 s and plays on, then the seek stands
 * still for the best part of a minute behind a client whose pipeline is held
 * on the build. Nothing of that is a stall: the seek lands where it was going,
 * without a restart.
 */
static void
test_watchdog_long_map_wait(void)
{
    struct Bare b;
    struct ReplaySeekTimes t;
    int frames = 0;

    bare_open(&b);
    ReplayScrub_SeekTick(b.scrub, 30);
    while( ReplayScrub_Seeking(b.scrub) && frames < 20000 )
    {
        bare_frame(&b, 20, 1, 60000, 1);
        frames++;
    }
    TEST_CHECK(!ReplayScrub_Seeking(b.scrub));
    TEST_CHECK(b.restarts == 0);
    TEST_CHECK(ReplayScrub_Reached(b.scrub) == 30);
    /* It did wait out the whole build. */
    TEST_CHECK(frames * 20 > 60000);
    ReplayScrub_SeekTimes(b.scrub, &t);
    TEST_CHECK(t.restarted == 0);
    TEST_CHECK(t.target == 30);
    bare_close(&b);
}

/*
 * A client that goes away for minutes between frames -- a hidden tab -- is
 * not running, so the gaps barely count; a client that runs and executes
 * nothing for 20 s, with no load it is waiting on, is a stalled seek, and it
 * restarts from the login, loudly.
 */
static void
test_watchdog_real_stall(void)
{
    struct Bare b;
    int frames;

    bare_open(&b);
    ReplayScrub_SeekTick(b.scrub, 15);
    /* Fifty polls 5 s apart: over four minutes of wall clock, 12.5 s of
     * running time. */
    for( frames = 0; frames < 50; frames++ )
        bare_frame(&b, 5000, 0, 0, 0);
    TEST_CHECK(b.restarts == 0);
    TEST_CHECK(ReplayScrub_Seeking(b.scrub));
    /* Now running, 20 ms a frame, executing nothing: about 7.5 s more. */
    for( frames = 0; frames < 300 && !b.restarts; frames++ )
        bare_frame(&b, 20, 0, 0, 0);
    TEST_CHECK(b.restarts == 0);
    for( frames = 0; frames < 2000 && !b.restarts; frames++ )
        bare_frame(&b, 20, 0, 0, 0);
    TEST_CHECK(b.restarts == 1);
    bare_close(&b);
}

int
main(void)
{
    test_normal_play();
    test_seek_forward_and_back();
    test_hold_and_step();
    test_seek_replaced();
    test_seek_to_the_end();
    test_fast_seek();
    test_seek_back_across_a_reconnect();
    test_watchdog_long_map_wait();
    test_watchdog_real_stall();
    printf("replay_scrub_test: ok\n");
    return 0;
}
