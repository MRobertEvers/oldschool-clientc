/*
 * An RSProx recording played through the replay transport into a real
 * rev-241 ToriRS_Network: the handshake the transport answers, the cipher it
 * mirrors from the seed the client published, the order and spacing the
 * server's packets arrive in, the map-build barrier, and the LOGOUT that ends
 * a recording which did not log out itself.
 *
 * The recording is built here, byte for byte in RSProx's BinaryBlob layout
 * (net/rsprox_recording.h), with client packets interleaved so the reader has
 * to frame both directions to find the server's.
 */
#include "cmd/cmdbus.h"
#include "net/net.h"
#include "net/net_out.h"
#include "net/rev/revpacket.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rsprox_recording.h"
#include "mem/torirs_mem.h"
#include "platform/net_transport.h"

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

/* manifests/manifest_osrs241_replay.ini's key: the login block needs one to
 * encrypt with, and nothing decrypts it. */
static char const* const TEST_RSA_E = "10001";
static char const* const TEST_RSA_N =
    "c30fcbc01e071ff224ea1a6508052d1140f87abaf8f40f7004efa59926708e5d99e2bc832fdca8276482dd0d690f"
    "644156850f47886f8032b3e9aa52508d24e8c9b7c50b8d8b8716fb8c3993bb6ce15e2124883edb7aaa7241a8b530"
    "f806c61cd1345879413fc105980a4f5fcdb3f0d743b14b16228b4d1496c83d3755a78a19";

#define LOCAL_INDEX 827
/* 30 + 2046 x 18 bits of GPI init, rounded up to a byte: what a login
 * REBUILD_NORMAL_V2 carries ahead of its six bytes (RSProx index 4377's is
 * 4614 bytes long). */
#define GPI_INIT_BYTES 4608

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
    w4(r, 1);   /* headerVersion */
    w4(r, 241); /* revision */
    w4(r, 1);   /* subRevision */
    w1(r, 1);   /* clientType */
    w1(r, 5);   /* platformType */
    w4(r, 0x0000019a);
    w4(r, 0x12345678); /* timestamp */
    w2(r, 631);        /* worldId */
    w4(r, 1);          /* worldFlags */
    w1(r, 0);          /* worldLocation */
    wstr(r, "oldschool331.runescape.com");
    wstr(r, "-");
    w2(r, LOCAL_INDEX);
    w2(r, 3); /* accountHash */
    w1(r, 0xaa);
    w1(r, 0xbb);
    w1(r, 0xcc);
    wstr(r, "RuneLite (Win)");
    w4(r, 2); /* js5MasterIndex */
    w1(r, 0);
    w1(r, 0);
}

/* pMidiVarLen of (direction | delta << 1), then the opcode. */
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

/* A client packet of `len` payload bytes: the reader must step over it. */
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

/* REBUILD_NORMAL_V2's trailing six bytes, in RSProx 241's read order
 * (StaticRebuildV2Decoder): g2 zoneZ, g2Alt1 zoneX, g2s worldArea. */
static void
rebuild_tail(
    uint8_t* out,
    int zone_x,
    int zone_z,
    int world_area)
{
    out[0] = (uint8_t)(zone_z >> 8);
    out[1] = (uint8_t)zone_z;
    out[2] = (uint8_t)zone_x;
    out[3] = (uint8_t)(zone_x >> 8);
    out[4] = (uint8_t)(world_area >> 8);
    out[5] = (uint8_t)world_area;
}

static struct Rec
build_recording(struct GameProtoRevTable const* rev)
{
    struct Rec r;
    static uint8_t login_rebuild[GPI_INIT_BYTES + 6];
    uint8_t rebuild[6];
    uint8_t varp[3] = { 7, 0, 1 };

    memset(&r, 0, sizeof(r));
    w_header(&r);

    /* tick 0: the login rebuild (GPI init + the six bytes), at 3 ms */
    memset(login_rebuild, 0, sizeof(login_rebuild));
    rebuild_tail(login_rebuild + GPI_INIT_BYTES, 365, 435, 0);
    w_server(&r, rev, 3, PKT_NAME_REBUILD_NORMAL, login_rebuild, (int)sizeof(login_rebuild));
    w_client(&r, rev, 5, PKTOUT_NAME_NO_TIMEOUT, rev->packetout_size(rev->packetout_code(
                                                     PKTOUT_NAME_NO_TIMEOUT)));
    w_server(&r, rev, 6, PKT_NAME_SERVER_TICK_END, NULL, 0);
    /* tick 1 */
    w_server(&r, rev, 600, PKT_NAME_VARP_SMALL, varp, 3);
    w_client(&r, rev, 600, PKTOUT_NAME_IF_SCRIPT_TRIGGER, 300);
    w_server(&r, rev, 601, PKT_NAME_SERVER_TICK_END, NULL, 0);
    /* tick 2: a rebuild with a non-zero worldArea, as Jagex sends under
     * Taverley (index 4377, tick 258) */
    rebuild_tail(rebuild, 363, 1234, 10);
    w_server(&r, rev, 1200, PKT_NAME_REBUILD_NORMAL, rebuild, 6);
    w_server(&r, rev, 1201, PKT_NAME_SERVER_TICK_END, NULL, 0);
    /* tick 3: behind the map-build barrier */
    varp[0] = 8;
    w_server(&r, rev, 1800, PKT_NAME_VARP_SMALL, varp, 3);
    w_server(&r, rev, 1801, PKT_NAME_SERVER_TICK_END, NULL, 0);
    return r;
}

/* --- the harness: one frame = one logic cycle (20 ms of replay clock) ---- */

struct Seen
{
    int names[64];
    int world_area[64];
    int count;
};

static void
frame(
    struct NetTransport* t,
    struct ToriRS_Network* net,
    struct ToriRS_CmdBus* bus,
    unsigned long long* cycles,
    struct Seen* seen)
{
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    struct RevPacket packet;

    (*cycles)++;
    NetTransport_ReplayClock(t, *cycles);
    NetTransport_Poll(t, net, bus);
    while( CmdBus_Pop(bus, &header, payload) )
        ToriRS_Network_HandleCmd(net, header.type, payload, (int)header.length);
    while( ToriRS_Network_PopPacket(net, &packet) )
    {
        TEST_CHECK(seen->count < 64);
        seen->names[seen->count] = packet.packet_type;
        seen->world_area[seen->count] =
            packet.packet_type == PKT_NAME_REBUILD_NORMAL ? packet._map_rebuild.world_area : -1;
        seen->count++;
        gameproto_free(&packet);
    }
}

static int
count_of(
    struct Seen const* seen,
    int name)
{
    int n = 0;

    for( int i = 0; i < seen->count; i++ )
        n += seen->names[i] == name;
    return n;
}

/* What the client sends once a rebuild is built: MAP_BUILD_COMPLETE, behind
 * a NO_TIMEOUT when `noise` so the transport has to frame its way to it. */
static void
map_built(
    struct GameProtoRevTable const* rev,
    struct ToriRS_Network* net,
    int noise)
{
    uint8_t out[16];
    int n;

    if( noise )
    {
        n = net_out_no_timeout(rev, net->random_out, out, (int)sizeof(out));
        TEST_CHECK(n > 0);
        ToriRS_Network_SendRaw(net, out, n);
    }
    n = net_out_map_build_complete(rev, net->random_out, out, (int)sizeof(out));
    TEST_CHECK(n > 0);
    ToriRS_Network_SendRaw(net, out, n);
}

static void
test_replay_plays_a_recording(void)
{
    struct GameProtoRevTable const* rev = GameProtoRev_Osrs(241);
    struct Rec r = build_recording(rev);
    struct RsproxRecording rec;
    struct ToriRS_Network net;
    static struct ToriRS_CmdBus bus;
    struct NetTransport* t;
    struct NetTransportReplayStatus st;
    struct Seen seen;
    unsigned long long cycles = 0;
    char err[256];

    TEST_CHECK(RsproxRecording_Open(&rec, r.data, r.len, err, sizeof(err)) == 0);
    TEST_CHECK(rec.revision == 241);
    TEST_CHECK(rec.local_player_index == LOCAL_INDEX);
    TEST_CHECK(strcmp(rec.world_host, "oldschool331.runescape.com") == 0);
    TEST_CHECK(strcmp(rec.client_name, "RuneLite (Win)") == 0);
    TEST_CHECK(strcmp(RsproxRecording_RevName(&rec), "osrs241") == 0);
    TEST_CHECK(RsproxRecording_Index(&rec, rev, err, sizeof(err)) == 0);
    /* Both client packets framed past, eight server packets kept. */
    TEST_CHECK(rec.frame_count == 8);
    TEST_CHECK(rec.client_packet_count == 2);
    TEST_CHECK(rec.tick_count == 4);
    TEST_CHECK(rec.frames[2].t_ms == 600);
    TEST_CHECK(rec.frames[2].tick == 1);

    ToriRS_Network_Init(&net, rev, TEST_RSA_E, TEST_RSA_N);
    CmdBus_Init(&bus);
    t = NetTransport_NewReplay(&rec, rev);
    TEST_CHECK(NetTransport_IsReplay(t));
    TEST_CHECK(rec.data == NULL); /* moved into the transport */
    NetTransport_ReplaySetUnlocked(t, 1);
    memset(&seen, 0, sizeof(seen));

    ToriRS_Network_ConnectLogin(&net, "replay", "replay", "replay");
    /* The handshake, then tick 0: the login rebuild is a barrier like any
     * other, so ten seconds of cycles release nothing behind it until the
     * client says it has built the map. */
    for( int i = 0; i < 500; i++ )
        frame(t, &net, &bus, &cycles, &seen);
    TEST_CHECK(net.state == TORIRS_NET_GAME);
    TEST_CHECK(net.local_index == LOCAL_INDEX);
    TEST_CHECK(count_of(&seen, PKT_NAME_REBUILD_NORMAL) == 1);
    TEST_CHECK(count_of(&seen, PKT_NAME_SERVER_TICK_END) == 1);
    TEST_CHECK(count_of(&seen, PKT_NAME_VARP_SMALL) == 0);
    NetTransport_ReplayGetStatus(t, &st);
    TEST_CHECK(st.waiting_map == 1);
    TEST_CHECK(st.clock_ms < 600);

    /* A NO_TIMEOUT first, which the transport has to decipher and step over
     * to find the MAP_BUILD_COMPLETE behind it. Then 80 cycles: ticks 1 and 2,
     * short of tick 3 (1.8 s), which the second barrier holds anyway. */
    map_built(rev, &net, 1);
    for( int i = 0; i < 80; i++ )
        frame(t, &net, &bus, &cycles, &seen);
    TEST_CHECK(count_of(&seen, PKT_NAME_REBUILD_NORMAL) == 2);
    TEST_CHECK(count_of(&seen, PKT_NAME_VARP_SMALL) == 1);
    /* The Taverley rebuild's worldArea arrives as sent; the exec rebuilds
     * the root regardless (task_gameproto_exec.c rebuild_view). */
    {
        int last = -1;

        for( int i = 0; i < seen.count; i++ )
            if( seen.names[i] == PKT_NAME_REBUILD_NORMAL )
                last = i;
        TEST_CHECK(seen.world_area[last] == 10);
    }

    for( int i = 0; i < 500; i++ )
        frame(t, &net, &bus, &cycles, &seen);
    TEST_CHECK(count_of(&seen, PKT_NAME_VARP_SMALL) == 1);
    NetTransport_ReplayGetStatus(t, &st);
    TEST_CHECK(st.waiting_map == 1);
    TEST_CHECK(strcmp(st.phase, "playing") == 0);
    TEST_CHECK(st.tick == 2);

    map_built(rev, &net, 0);
    for( int i = 0; i < 60; i++ )
        frame(t, &net, &bus, &cycles, &seen);
    TEST_CHECK(count_of(&seen, PKT_NAME_VARP_SMALL) == 2);
    TEST_CHECK(count_of(&seen, PKT_NAME_SERVER_TICK_END) == 4);
    /* The recording never logged out, so the transport does. */
    TEST_CHECK(seen.names[seen.count - 1] == PKT_NAME_LOGOUT);
    NetTransport_ReplayGetStatus(t, &st);
    TEST_CHECK(strcmp(st.phase, "finished") == 0);
    TEST_CHECK(st.tick == 3);
    TEST_CHECK(st.tick_count == 4);

    NetTransport_Free(t);
    ToriRS_Network_Free(&net);
}

static void
test_replay_refuses_what_it_cannot_frame(void)
{
    struct GameProtoRevTable const* rev = GameProtoRev_Osrs(241);
    struct RsproxRecording rec;
    char err[256];

    /* A payload that runs past the end of the file: the last packet is a
     * 2-byte SERVER_TICK_END, so 3 bytes off cuts into the VARP before it. */
    {
        struct Rec r = build_recording(rev);

        r.len -= 3;
        TEST_CHECK(RsproxRecording_Open(&rec, r.data, r.len, err, sizeof(err)) == 0);
        TEST_CHECK(RsproxRecording_Index(&rec, rev, err, sizeof(err)) == -1);
        TEST_CHECK(strstr(err, "past the end") != NULL);
        RsproxRecording_Free(&rec);
    }
    /* Not an RSProx header at all. */
    {
        struct Rec r = build_recording(rev);

        r.data[3] = 9;
        TEST_CHECK(RsproxRecording_Open(&rec, r.data, r.len, err, sizeof(err)) == -1);
        TEST_CHECK(strstr(err, "header version 9") != NULL);
    }
    /* An older revision plays on the data-driven family's table; one outside
     * 227-241 has no table and is refused by name. */
    {
        struct Rec r = build_recording(rev);

        r.data[7] = 239;
        TEST_CHECK(RsproxRecording_Open(&rec, r.data, r.len, err, sizeof(err)) == 0);
        TEST_CHECK(strcmp(RsproxRecording_RevName(&rec), "osrs239") == 0);
        RsproxRecording_Free(&rec);
    }
    {
        struct Rec r = build_recording(rev);

        r.data[7] = 222;
        TEST_CHECK(RsproxRecording_Open(&rec, r.data, r.len, err, sizeof(err)) == 0);
        TEST_CHECK(strcmp(RsproxRecording_RevName(&rec), "osrs222") == 0);
        RsproxRecording_Free(&rec);
    }
    {
        struct Rec r = build_recording(rev);

        /* No executor below 222 (src/net/rev/revisions.ini). */
        r.data[7] = 221;
        TEST_CHECK(RsproxRecording_Open(&rec, r.data, r.len, err, sizeof(err)) == 0);
        TEST_CHECK(RsproxRecording_RevName(&rec) == NULL);
        RsproxRecording_Free(&rec);
    }
}

/* One of RSProx's pseudo-packets (GameServerProt: PACKET_GROUP_END 0xFE, no
 * payload; RECONNECT 0xFF, var-short). */
static void
w_pseudo(
    struct Rec* r,
    uint64_t t_ms,
    int opcode,
    uint8_t const* payload,
    int len)
{
    w_packet_head(r, t_ms, 1, opcode);
    if( opcode == RSPROX_RECORDING_RECONNECT )
        w2(r, len);
    for( int i = 0; i < len; i++ )
        w1(r, payload[i]);
}

/*
 * A session that moved to a new connection mid-recording. The proxy's notes
 * never reach the client as bytes: PACKET_GROUP_END is dropped, and RECONNECT
 * arrives as PKT_NAME_RECONNECT between the packets either side of it -- the
 * VARP before it executes first -- carrying the index and the GPI block.
 */
static void
test_replay_reconnects_where_the_recording_did(void)
{
    struct GameProtoRevTable const* rev = GameProtoRev_Osrs(241);
    struct Rec r;
    struct RsproxRecording rec;
    struct ToriRS_Network net;
    static struct ToriRS_CmdBus bus;
    static uint8_t login_rebuild[GPI_INIT_BYTES + 6];
    static uint8_t gpi[GPI_INIT_BYTES];
    uint8_t group[2] = { 0, 9 };
    uint8_t varp[3] = { 7, 0, 1 };
    struct NetTransport* t;
    unsigned long long cycles = 0;
    char err[256];
    int seen_names[64];
    int seen_count = 0;
    int reconnect_at = -1;

    memset(&r, 0, sizeof(r));
    w_header(&r);
    memset(login_rebuild, 0, sizeof(login_rebuild));
    rebuild_tail(login_rebuild + GPI_INIT_BYTES, 365, 435, 0);
    w_server(&r, rev, 3, PKT_NAME_REBUILD_NORMAL, login_rebuild, (int)sizeof(login_rebuild));
    w_server(&r, rev, 6, PKT_NAME_SERVER_TICK_END, NULL, 0);
    /* tick 1: a group, the hop, and the new world's first packet */
    /* PACKET_GROUP_START (gen/osrs241_prot.h: 58, two bytes), which the
     * client frames and drops. */
    w_packet_head(&r, 600, 1, 58);
    w1(&r, group[0]);
    w1(&r, group[1]);
    w_server(&r, rev, 600, PKT_NAME_VARP_SMALL, varp, 3);
    w_pseudo(&r, 600, RSPROX_RECORDING_PACKET_GROUP_END, NULL, 0);
    for( int i = 0; i < GPI_INIT_BYTES; i++ )
        gpi[i] = (uint8_t)(i * 7);
    w_pseudo(&r, 601, RSPROX_RECORDING_RECONNECT, gpi, GPI_INIT_BYTES);
    varp[0] = 8;
    w_server(&r, rev, 602, PKT_NAME_VARP_SMALL, varp, 3);
    w_server(&r, rev, 603, PKT_NAME_SERVER_TICK_END, NULL, 0);

    TEST_CHECK(RsproxRecording_Open(&rec, r.data, r.len, err, sizeof(err)) == 0);
    TEST_CHECK(RsproxRecording_Index(&rec, rev, err, sizeof(err)) == 0);
    TEST_CHECK(rec.frame_count == 8);
    TEST_CHECK(rec.frames[5].opcode == RSPROX_RECORDING_RECONNECT);
    /* No camera names a player after it, so RSProx's rule stands: the
     * recording's own index. */
    TEST_CHECK(rsprox_recording_resolve_reconnects(GameProtoRev_Osrs(241), &rec) == 0);
    TEST_CHECK(rec.frames[5].reconnect_index == LOCAL_INDEX);
    /* A hop the resolver found, as the transport would carry it. */
    rec.frames[5].reconnect_index = 163;

    ToriRS_Network_Init(&net, rev, TEST_RSA_E, TEST_RSA_N);
    CmdBus_Init(&bus);
    t = NetTransport_NewReplay(&rec, rev);
    NetTransport_ReplaySetUnlocked(t, 1);
    ToriRS_Network_ConnectLogin(&net, "replay", "replay", "replay");
    for( int i = 0; i < 400; i++ )
    {
        struct ToriRS_CmdHeader header;
        static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
        struct RevPacket packet;

        if( i == 200 )
            map_built(rev, &net, 0);
        cycles++;
        NetTransport_ReplayClock(t, cycles);
        NetTransport_Poll(t, &net, &bus);
        while( CmdBus_Pop(&bus, &header, payload) )
            ToriRS_Network_HandleCmd(&net, header.type, payload, (int)header.length);
        while( ToriRS_Network_PopPacket(&net, &packet) )
        {
            TEST_CHECK(seen_count < 64);
            if( packet.packet_type == PKT_NAME_RECONNECT )
            {
                reconnect_at = seen_count;
                TEST_CHECK(packet._reconnect.local_player_index == 163);
                TEST_CHECK(packet._reconnect.length == GPI_INIT_BYTES);
                TEST_CHECK(memcmp(packet._reconnect.data, gpi, GPI_INIT_BYTES) == 0);
            }
            seen_names[seen_count++] = packet.packet_type;
            gameproto_free(&packet);
        }
    }
    TEST_CHECK(reconnect_at > 0);
    TEST_CHECK(seen_names[reconnect_at - 1] == PKT_NAME_VARP_SMALL);
    TEST_CHECK(seen_names[reconnect_at + 1] == PKT_NAME_VARP_SMALL);
    TEST_CHECK(seen_names[reconnect_at + 2] == PKT_NAME_SERVER_TICK_END);
    TEST_CHECK(seen_names[seen_count - 1] == PKT_NAME_LOGOUT);

    NetTransport_Free(t);
    ToriRS_Network_Free(&net);
}

int
main(void)
{
    test_replay_plays_a_recording();
    test_replay_refuses_what_it_cannot_frame();
    test_replay_reconnects_where_the_recording_did();
    printf("rsprox_replay_test: ok\n");
    return 0;
}
