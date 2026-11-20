/*
 * Packet groups across a seek, on a real recording.
 *
 * A PACKET_GROUP_START states how many bytes its members take, and the client
 * holds the members until that many have come (net.c gameproto_drain), with
 * an assert that the count comes out exactly. A server sends nothing between
 * a group's members. The replay transport is the server here, and a seek that
 * starts while a group is half on the bus must not change that: the client
 * would count a chase's control frames and the next tick into the group, as
 * an asserts-live client replaying recording 96 did ('net->group_left == 0').
 *
 * This plays a recording through the real transport and scrubber into a real
 * ToriRS_Network, with a stand-in client (replay_scrub_test.c's: a popped
 * packet is executed, a REBUILD or keyframe is answered with
 * MAP_BUILD_COMPLETE). For every recorded group whose members arrive after
 * its header (RSProx timestamps them as they came off the socket), it seeks
 * to the group's tick, plays one 20 ms cycle at a time until the client
 * holds the group open, then seeks ahead -- and checks that the seek lands
 * and that the client never counted into the group a byte its header did not
 * state (group_left below zero: the assert an OPT=1 build compiles out).
 * Every group of the recording is also checked to add up, framed as the
 * transport frames it.
 *
 * Not a suite test: it needs a recording.
 *   make -C src OPT=1 PLATFORM_OBJ_BASE=<objdir> replay-group-check RECORDINGS=<file.bin>
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
#include "replay/replay_world.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A throwaway key pair: the transport reads the cipher seed from the client
 * (net->prev_seed), so the key only has to be one the client can use. */
static char const* const CHECK_RSA_E = "10001";
static char const* const CHECK_RSA_N =
    "c30fcbc01e071ff224ea1a6508052d1140f87abaf8f40f7004efa59926708e5d99e2bc832fdca8276482dd0d690f"
    "644156850f47886f8032b3e9aa52508d24e8c9b7c50b8d8b8716fb8c3993bb6ce15e2124883edb7aaa7241a8b530"
    "f806c61cd1345879413fc105980a4f5fcdb3f0d743b14b16228b4d1496c83d3755a78a19";

struct Client
{
    struct GameProtoRevTable const* rev;
    struct ToriRS_Network net;
    struct ToriRS_CmdBus bus;
    struct NetTransport* t;
    unsigned long long cycles;
    unsigned serial;
    unsigned session_serial;
    int in_game;
    /** group_left seen below zero after a frame. */
    int overran;
};

static struct Client s_client;

static void
frame(struct Client* c)
{
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    struct RevPacket packet;
    int rebuilt = 0;

    c->cycles += 1;
    NetTransport_ReplayClientTicks(c->t, c->serial);
    NetTransport_ReplayClock(c->t, c->cycles);
    NetTransport_Poll(c->t, &c->net, &c->bus);
    while( CmdBus_Pop(&c->bus, &header, payload) )
        ToriRS_Network_HandleCmd(&c->net, header.type, payload, (int)header.length);
    if( c->net.state == TORIRS_NET_GAME && !c->in_game )
    {
        c->in_game = 1;
        c->session_serial = c->serial;
    }
    if( c->net.group_left < 0 )
        c->overran = 1;
    while( ToriRS_Network_PopPacket(&c->net, &packet) )
    {
        if( packet.packet_type == PKT_NAME_SERVER_TICK_END )
            c->serial++;
        else if( packet.packet_type == PKT_NAME_REBUILD_NORMAL || packet.packet_type == PKT_NAME_REBUILD_REGION )
            rebuilt = 1;
        else if( packet._control && packet._control->type == TORIRS_CONTROL_REPLAY_WORLD )
        {
            struct TorirsControlReplayWorld w;
            struct ReplayWorldDelivery* d;

            if( !torirs_control_replay_world_decode(packet._control, &w) )
                abort();
            d = ReplayWorld_Take(w.serial);
            if( d )
            {
                if( d->rebuild )
                    rebuilt = 1;
                ReplayWorldDelivery_Free(d);
            }
        }
        gameproto_free(&packet);
    }
    if( rebuilt )
    {
        uint8_t out[16];
        int const n = net_out_map_build_complete(c->rev, c->net.random_out, out, (int)sizeof(out));

        if( n <= 0 )
            abort();
        ToriRS_Network_SendRaw(&c->net, out, n);
    }
    if( c->in_game && c->net.state == TORIRS_NET_DISCONNECTED )
    {
        c->in_game = 0;
        ToriRS_Network_Logout(&c->net);
        if( !ToriRS_Network_Reconnect(&c->net) )
            abort();
    }
}

static int
seeking(struct Client* c)
{
    struct NetTransportReplayStatus st;

    NetTransport_ReplayGetStatus(c->t, &st);
    return st.seeking;
}

/* Seek to `tick` and play until it lands. 0 on a seek that never lands. */
static int
seek_and_land(
    struct Client* c,
    int tick)
{
    NetTransport_ReplaySeek(c->t, tick);
    for( int i = 0; i < 200000 && seeking(c); i++ )
        frame(c);
    return !seeking(c);
}

/* One group of the recording: its header's frame and its tick. Copied out
 * before the transport takes the recording over (NetTransport_NewReplay
 * moves it). */
struct Group
{
    int header;
    int tick;
};

static int
check_group(
    struct Client* c,
    struct Group const* g)
{
    /* Held at the start of the group's tick, then one cycle a frame until
     * the client holds the group open. */
    NetTransport_ReplayHold(c->t);
    if( !seek_and_land(c, g->tick) )
    {
        printf("FAIL: tick %d: the seek to the group's tick never landed\n", g->tick);
        return 0;
    }
    NetTransport_ReplayRun(c->t);
    for( int i = 0; i < 2000 && !c->net.group_open; i++ )
        frame(c);
    if( !c->net.group_open )
    {
        printf("skip: tick %d: the group never stood open between two polls\n", g->tick);
        return 1;
    }
    /* A seek far enough ahead for a chase, begun with the group half in. */
    NetTransport_ReplayHold(c->t);
    c->overran = 0;
    if( !seek_and_land(c, g->tick + 40) )
    {
        struct NetTransportReplayStatus st;

        NetTransport_ReplayGetStatus(c->t, &st);
        printf("FAIL: tick %d: the seek ahead never landed (seek phase %s, tick %d, waiting for a map %d, "
               "group open %d with %d bytes owed)\n",
               g->tick, st.seek_phase, st.tick, st.waiting_map, c->net.group_open, c->net.group_left);
        return 0;
    }
    if( c->overran )
    {
        printf("FAIL: tick %d: the client counted more bytes into the group than its header stated\n",
               g->tick);
        return 0;
    }
    printf("ok: tick %d: a seek begun inside the group landed, and the group added up\n", g->tick);
    return 1;
}

int
main(
    int argc,
    char** argv)
{
    int failed = 0;

    if( argc < 2 )
    {
        fprintf(stderr, "usage: replay_group_check <recording.bin> ...\n");
        return 2;
    }
    for( int a = 1; a < argc; a++ )
    {
        struct Client* c = &s_client;
        struct RsproxRecording rec;
        struct GameProtoRevTable const* rev;
        char err[256];
        struct Group groups[64];
        int tick_count;
        int ngroups = 0;
        int checked = 0;

        memset(&rec, 0, sizeof(rec));
        if( RsproxRecording_OpenFile(&rec, argv[a], err, (int)sizeof(err)) != 0 )
        {
            printf("FAIL: %s: %s\n", argv[a], err);
            failed++;
            continue;
        }
        rev = RsproxRecording_Rev(&rec);
        if( !rev || RsproxRecording_Index(&rec, rev, err, (int)sizeof(err)) != 0 )
        {
            printf("FAIL: %s: %s\n", argv[a], rev ? err : "no executor for its revision");
            failed++;
            continue;
        }
        /* The groups whose members do not all arrive with the header. */
        for( int i = 0; i < rec.frame_count && ngroups < (int)(sizeof(groups) / sizeof(groups[0])); i++ )
        {
            struct RsproxRecordingFrame const* f = &rec.frames[i];
            struct RevPacket p;
            int left;
            int j;

            if( f->opcode >= RSPROX_RECORDING_PACKET_GROUP_END ||
                rev->packetin_code(f->opcode) != PKT_NAME_PACKET_GROUP_START )
                continue;
            memset(&p, 0, sizeof(p));
            if( rev->parse_wire(rev, f->opcode, rec.data + f->offset, (int)f->length, &p) <= 0 )
                continue;
            left = p._packet_group_start.length;
            for( j = i + 1; j < rec.frame_count && left > 0; j++ )
            {
                struct RsproxRecordingFrame const* m = &rec.frames[j];
                int const size = rev->packetin_size(m->opcode);

                if( m->opcode >= RSPROX_RECORDING_PACKET_GROUP_END )
                    continue;
                left -= (m->opcode >= 0x80 ? 2 : 1) +
                        (size == PKTIN_LENGTH_VARU8 ? 1 : size == PKTIN_LENGTH_VARU16 ? 2 : 0) + (int)m->length;
            }
            if( left != 0 )
            {
                printf("FAIL: %s: frame %d: the group's members do not add up to its header (%d left)\n",
                       argv[a], i, left);
                failed++;
            }
            if( rec.frames[j - 1].t_ms > f->t_ms && f->tick > 0 )
            {
                groups[ngroups].header = i;
                groups[ngroups].tick = f->tick;
                ngroups++;
            }
        }
        printf("%s: rev %d, %d frames, %d ticks; %d group(s) whose members arrive after the header\n",
               argv[a], rec.revision, rec.frame_count, rec.tick_count, ngroups);
        tick_count = rec.tick_count;

        memset(c, 0, sizeof(*c));
        c->rev = rev;
        ToriRS_Network_Init(&c->net, rev, CHECK_RSA_E, CHECK_RSA_N);
        CmdBus_Init(&c->bus);
        c->t = NetTransport_NewReplay(&rec, rev);
        NetTransport_ReplaySetSeekFast(c->t, 1);
        NetTransport_ReplaySetUnlocked(c->t, 1);
        ToriRS_Network_ConnectLogin(&c->net, "replay", "replay", "replay");
        for( int i = 0; i < 20000 && !c->in_game; i++ )
            frame(c);
        if( !c->in_game )
        {
            printf("FAIL: %s: never logged in\n", argv[a]);
            failed++;
        }
        else
        {
            for( int g = 0; g < ngroups; g++ )
            {
                if( groups[g].tick + 40 >= tick_count )
                    continue;
                if( !check_group(c, &groups[g]) )
                    failed++;
                checked++;
            }
        }
        printf("%s: %d group(s) checked\n", argv[a], checked);
        NetTransport_Free(c->t);
        ToriRS_Network_Free(&c->net);
    }
    printf(failed ? "replay_group_check: %d FAILED\n" : "replay_group_check: ok\n", failed);
    return failed ? 1 : 0;
}
