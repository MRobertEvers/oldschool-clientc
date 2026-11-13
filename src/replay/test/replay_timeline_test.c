/*
 * The replay timeline (replay/replay_timeline.h) over a recording built here
 * byte for byte in RSProx's BinaryBlob layout, as net/test/rsprox_replay_test.c
 * builds its own: the tick table, the time maps across a stall, every lane,
 * next / previous mark, the density pyramid and the page's JSON.
 *
 * The packets the lanes decode are written with RSProt's own rev-241 codecs
 * (3rd/rsprot/packets), the functions the client decodes them with.
 */
#include "mem/torirs_mem.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rsprox_recording.h"
#include "replay/replay_timeline.h"

#include "packets/if_opensub.h"
#include "packets/if_opentop.h"
#include "packets/message_game.h"
#include "packets/midi_song_v2.h"

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

#define LOCAL_INDEX 827
#define GPI_INIT_BYTES 4608
#define TIMESTAMP_MS 0x0000019a12345678LL

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
    w4(r, (uint32_t)(TIMESTAMP_MS >> 32));
    w4(r, (uint32_t)(TIMESTAMP_MS & 0xffffffff));
    w2(r, 631); /* worldId */
    w4(r, 1);   /* worldFlags */
    w1(r, 0);   /* worldLocation */
    wstr(r, "oldschool331.runescape.com");
    wstr(r, "-");
    w2(r, LOCAL_INDEX);
    w2(r, 0); /* accountHash */
    wstr(r, "RuneLite (Win)");
    w4(r, 0); /* js5MasterIndex */
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
    int pkt_out_name)
{
    int const op = rev->packetout_code(pkt_out_name);
    int const size = rev->packetout_size(op);

    TEST_CHECK(op >= 0);
    TEST_CHECK(size >= 0);
    w_packet_head(r, t_ms, 0, op);
    for( int i = 0; i < size; i++ )
        w1(r, 0x5a);
}

static void
w_reconnect(
    struct Rec* r,
    uint64_t t_ms)
{
    w_packet_head(r, t_ms, 1, RSPROX_RECORDING_RECONNECT);
    w2(r, GPI_INIT_BYTES);
    for( int i = 0; i < GPI_INIT_BYTES; i++ )
        w1(r, 0);
}

/* REBUILD_NORMAL_V2's trailing six bytes (StaticRebuildV2Decoder). */
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

/* RSProt's encoder for one message into `out`: the bytes written. */
#define ENCODE(out, codec, msg)                                                                    \
    ({                                                                                             \
        RsprotBuf b_;                                                                              \
        RsprotExec x_;                                                                             \
        rsprot_buf_wrap(&b_, (out), (int32_t)sizeof(out));                                         \
        rsprot_exec_encode(&x_, &b_);                                                              \
        codec(&x_, (msg));                                                                         \
        TEST_CHECK(rsprot_exec_ok(&x_));                                                           \
        (int)b_.wpos;                                                                              \
    })

static char const k_welcome[] = "Welcome to \"Gielinor\"\xa0!";

/*
 *   tick 0   login REBUILD, MIDI_SONG 76, IF_OPENTOP 161, MESSAGE_GAME,
 *            a client packet, the tick end
 *   tick 1   IF_OPENSUB modal 12, IF_OPENSUB overlay 13 (not a mark)
 *   tick 2   a VARP
 *   tick 3   a VARP after 7.8 s of silence: a stall, 2.8 s of it skipped
 *   tick 4   RSProx's RECONNECT, a REBUILD
 *   after    the recording's LOGOUT, beyond the last tick end
 */
static struct Rec
build_recording(struct GameProtoRevTable const* rev)
{
    struct Rec r;
    static uint8_t login_rebuild[GPI_INIT_BYTES + 6];
    uint8_t rebuild[6];
    uint8_t varp[3] = { 7, 0, 1 };
    uint8_t buf[256];
    int n;

    memset(&r, 0, sizeof(r));
    w_header(&r);

    memset(login_rebuild, 0, sizeof(login_rebuild));
    rebuild_tail(login_rebuild + GPI_INIT_BYTES, 365, 435);
    w_server(&r, rev, 3, PKT_NAME_REBUILD_NORMAL, login_rebuild, (int)sizeof(login_rebuild));
    {
        MsgMidiSongV2 m = { 0 };

        m.id = 76;
        n = ENCODE(buf, packet_midi_song_v2_osrs241_out, &m);
        w_server(&r, rev, 4, PKT_NAME_MIDI_SONG, buf, n);
    }
    {
        MsgIfOpenTop m = { 0 };

        m.interface_id = 161;
        n = ENCODE(buf, packet_if_opentop_osrs241_out, &m);
        w_server(&r, rev, 4, PKT_NAME_IF_OPENTOP, buf, n);
    }
    {
        MsgMessageGame m = { 0 };

        m.message = k_welcome;
        n = ENCODE(buf, packet_message_game_osrs241_out, &m);
        w_server(&r, rev, 5, PKT_NAME_MESSAGE_GAME, buf, n);
    }
    w_client(&r, rev, 5, PKTOUT_NAME_NO_TIMEOUT);
    w_server(&r, rev, 6, PKT_NAME_SERVER_TICK_END, NULL, 0);

    for( int type = 0; type <= 1; type++ )
    {
        MsgIfOpenSub m = { 0 };

        m.type = type;
        m.interface_id = 12 + type;
        m.destination_combined_id = (161 << 16) | 3;
        n = ENCODE(buf, packet_if_opensub_osrs241_out, &m);
        w_server(&r, rev, 600, PKT_NAME_IF_OPENSUB, buf, n);
    }
    w_server(&r, rev, 601, PKT_NAME_SERVER_TICK_END, NULL, 0);

    w_server(&r, rev, 1200, PKT_NAME_VARP_SMALL, varp, 3);
    w_server(&r, rev, 1201, PKT_NAME_SERVER_TICK_END, NULL, 0);

    w_server(&r, rev, 9000, PKT_NAME_VARP_SMALL, varp, 3);
    w_server(&r, rev, 9001, PKT_NAME_SERVER_TICK_END, NULL, 0);

    w_reconnect(&r, 9600);
    rebuild_tail(rebuild, 363, 1234);
    w_server(&r, rev, 9601, PKT_NAME_REBUILD_NORMAL, rebuild, 6);
    w_server(&r, rev, 9602, PKT_NAME_SERVER_TICK_END, NULL, 0);

    w_server(&r, rev, 10200, PKT_NAME_LOGOUT, NULL, 0);
    return r;
}

/* --- the checks ---------------------------------------------------------- */

static void
test_timeline(void)
{
    struct GameProtoRevTable const* rev = GameProtoRev_Osrs(241);
    struct Rec r = build_recording(rev);
    struct RsproxRecording rec;
    struct ReplayTimeline* tl;
    struct ReplayLane const* lane;
    char err[256];
    void* pi_before;
    void* pi_after;
    size_t pi_size;

    TEST_CHECK(RsproxRecording_Open(&rec, r.data, r.len, err, sizeof(err)) == 0);
    TEST_CHECK(RsproxRecording_Index(&rec, rev, err, sizeof(err)) == 0);
    TEST_CHECK(rec.frame_count == 16);
    TEST_CHECK(rec.tick_count == 5);
    TEST_CHECK(rec.client_packet_count == 1);
    TEST_CHECK(rec.inputs[0].frame == 4);

    /* The pass decodes a login REBUILD, which seeds the session's player
     * table; it must leave the table as it found it. */
    pi_size = rev->player_info_state_size();
    pi_before = malloc(pi_size);
    pi_after = malloc(pi_size);
    TEST_CHECK(pi_before);
    TEST_CHECK(pi_after);
    rev->player_info_set_local(LOCAL_INDEX);
    rev->player_info_state_save(pi_before);

    tl = ReplayTimeline_Build(&rec, rev);

    rev->player_info_state_save(pi_after);
    TEST_CHECK(memcmp(pi_before, pi_after, pi_size) == 0);
    free(pi_before);
    free(pi_after);

    TEST_CHECK(tl->tick_count == 5);
    TEST_CHECK(tl->duration_ms == 10200);
    TEST_CHECK(tl->timestamp_ms == TIMESTAMP_MS);

    /* The tick table. */
    {
        static uint32_t const first_frame[5] = { 0, 5, 8, 10, 12 };
        static uint32_t const t_ms[5] = { 3, 600, 1200, 9000, 9600 };
        static uint32_t const first_input[5] = { 0, 1, 1, 1, 1 };
        static uint16_t const scene[5] = { 0, 0, 0, 0, 1 };
        static uint16_t const flags[5] = {
            REPLAY_TICK_REBUILD | REPLAY_TICK_TOP_OPENED,
            0,
            0,
            REPLAY_TICK_STALL_BEFORE,
            REPLAY_TICK_RECONNECT | REPLAY_TICK_REBUILD,
        };
        static uint32_t const activity[5] = { 5, 3, 2, 2, 3 };

        for( int t = 0; t < 5; t++ )
        {
            TEST_CHECK(tl->ticks[t].first_frame == first_frame[t]);
            TEST_CHECK(tl->ticks[t].t_ms == t_ms[t]);
            TEST_CHECK(tl->ticks[t].first_input == first_input[t]);
            TEST_CHECK(tl->ticks[t].scene == scene[t]);
            TEST_CHECK(tl->ticks[t].flags == flags[t]);
            TEST_CHECK(tl->activity[t] == activity[t]);
            TEST_CHECK(rec.frames[first_frame[t]].tick == t);
        }
    }

    /* Time to tick and back, across the stall between ticks 2 and 3. */
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 0) == 0);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 3) == 0);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 599) == 0);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 600) == 1);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 1200) == 2);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 5000) == 2); /* inside the stall */
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 8999) == 2);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 9000) == 3);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 9599) == 3);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 9600) == 4);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 10199) == 4);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 10200) == 5);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 99999) == 5);
    for( int t = 0; t <= 5; t++ )
        TEST_CHECK(ReplayTimeline_TickAtMs(tl, ReplayTimeline_MsAtTick(tl, t)) == t);
    TEST_CHECK(ReplayTimeline_MsAtTick(tl, 3) == 9000);
    TEST_CHECK(ReplayTimeline_MsAtTick(tl, 5) == 10200);

    /* The lanes. */
    TEST_CHECK(tl->scene_count == 2);
    TEST_CHECK(tl->scenes[0].tick == 0);
    TEST_CHECK(tl->scenes[0].base_x == (365 - 6) * 8);
    TEST_CHECK(tl->scenes[0].base_z == (435 - 6) * 8);
    TEST_CHECK(tl->scenes[1].tick == 4);
    TEST_CHECK(tl->scenes[1].frame == 13);
    TEST_CHECK(tl->scenes[1].base_x == (363 - 6) * 8);
    TEST_CHECK(tl->scenes[1].base_z == (1234 - 6) * 8);
    lane = &tl->lanes[REPLAY_LANE_SCENE];
    TEST_CHECK(lane->count == 2);
    TEST_CHECK(lane->marks[1].tick == 4);
    TEST_CHECK(lane->marks[1].arg == 1);

    lane = &tl->lanes[REPLAY_LANE_MUSIC];
    TEST_CHECK(lane->count == 1);
    TEST_CHECK(lane->marks[0].tick == 0);
    TEST_CHECK(lane->marks[0].arg == 76);

    lane = &tl->lanes[REPLAY_LANE_SESSION];
    TEST_CHECK(lane->count == 3);
    TEST_CHECK(lane->marks[0].tick == 3);
    TEST_CHECK(lane->marks[0].kind == REPLAY_SESSION_STALL);
    TEST_CHECK(lane->marks[0].arg == 9000 - 1201 - REPLAY_MAX_GAP_MS);
    TEST_CHECK(lane->marks[1].tick == 4);
    TEST_CHECK(lane->marks[1].kind == REPLAY_SESSION_RECONNECT);
    TEST_CHECK(lane->marks[2].tick == 5);
    TEST_CHECK(lane->marks[2].kind == REPLAY_SESSION_LOGOUT);

    lane = &tl->lanes[REPLAY_LANE_INTERFACE];
    TEST_CHECK(lane->count == 2);
    TEST_CHECK(lane->marks[0].tick == 0);
    TEST_CHECK(lane->marks[0].kind == REPLAY_INTERFACE_TOP);
    TEST_CHECK(lane->marks[0].arg == 161);
    TEST_CHECK(lane->marks[1].tick == 1);
    TEST_CHECK(lane->marks[1].kind == REPLAY_INTERFACE_MODAL);
    TEST_CHECK(lane->marks[1].arg == 12);

    lane = &tl->lanes[REPLAY_LANE_CHAT];
    TEST_CHECK(lane->count == 1);
    TEST_CHECK(strcmp(ReplayTimeline_ChatText(tl, &lane->marks[0]), k_welcome) == 0);

    TEST_CHECK(tl->lanes[REPLAY_LANE_ACTIVITY].count == 0);

    /* Next and previous. */
    TEST_CHECK(ReplayTimeline_NextMark(tl, REPLAY_LANE_SESSION, 0) == 3);
    TEST_CHECK(ReplayTimeline_NextMark(tl, REPLAY_LANE_SESSION, 3) == 4);
    TEST_CHECK(ReplayTimeline_NextMark(tl, REPLAY_LANE_SESSION, 4) == 5);
    TEST_CHECK(ReplayTimeline_NextMark(tl, REPLAY_LANE_SESSION, 5) == -1);
    TEST_CHECK(ReplayTimeline_PrevMark(tl, REPLAY_LANE_SESSION, 5) == 4);
    TEST_CHECK(ReplayTimeline_PrevMark(tl, REPLAY_LANE_SESSION, 3) == -1);
    TEST_CHECK(ReplayTimeline_PrevMark(tl, REPLAY_LANE_SCENE, 4) == 0);
    TEST_CHECK(ReplayTimeline_PrevMark(tl, REPLAY_LANE_SCENE, 5) == 4);
    TEST_CHECK(ReplayTimeline_NextMark(tl, REPLAY_LANE_SCENE, 0) == 4);
    TEST_CHECK(ReplayTimeline_NextMark(tl, REPLAY_LANE_MUSIC, 0) == -1);
    TEST_CHECK(ReplayTimeline_PrevMark(tl, REPLAY_LANE_MUSIC, 1) == 0);
    TEST_CHECK(ReplayTimeline_NextMark(tl, REPLAY_LANE_ACTIVITY, 0) == -1);

    /* The density pyramid: marks per 2^k ticks, packets for activity. */
    lane = &tl->lanes[REPLAY_LANE_SESSION];
    TEST_CHECK(lane->density_cells[0] == 6);
    TEST_CHECK(lane->density[0][3] == 1);
    TEST_CHECK(lane->density[0][5] == 1);
    TEST_CHECK(lane->density[1][1] == 1);
    TEST_CHECK(lane->density[1][2] == 2);
    TEST_CHECK(lane->density[2][0] == 1);
    TEST_CHECK(lane->density[2][1] == 2);
    TEST_CHECK(lane->density_cells[REPLAY_LANE_LEVELS - 1] == 1);
    TEST_CHECK(lane->density[REPLAY_LANE_LEVELS - 1][0] == 3);
    lane = &tl->lanes[REPLAY_LANE_ACTIVITY];
    TEST_CHECK(lane->density[0][0] == 5);
    TEST_CHECK(lane->density[1][0] == 8);
    TEST_CHECK(lane->density[REPLAY_LANE_LEVELS - 1][0] == 15);

    /* The page's object, exactly. */
    {
        char* json = ReplayTimeline_Json(tl);
        char expect[2048];

        snprintf(expect, sizeof(expect),
                 "{\"ticks\":5,\"duration_ms\":10200,\"timestamp_ms\":%lld,"
                 "\"tick_ms\":[3,600,1200,9000,9600,10200],"
                 "\"activity\":[5,3,2,2,3],"
                 "\"lanes\":{"
                 "\"scene\":[{\"tick\":0,\"x\":2872,\"z\":3432},{\"tick\":4,\"x\":2856,\"z\":9824}],"
                 "\"music\":[{\"tick\":0,\"id\":76}],"
                 "\"session\":[{\"tick\":3,\"kind\":\"stall\",\"ms\":2799},"
                 "{\"tick\":4,\"kind\":\"reconnect\"},{\"tick\":5,\"kind\":\"logout\"}],"
                 "\"interface\":[{\"tick\":0,\"kind\":\"top\",\"id\":161},"
                 "{\"tick\":1,\"kind\":\"modal\",\"id\":12}],"
                 "\"chat\":[{\"tick\":0,\"text\":\"Welcome to \\\"Gielinor\\\"\\u00a0!\"}]"
                 "}}",
                 (long long)TIMESTAMP_MS);
        if( strcmp(json, expect) != 0 )
            fprintf(stderr, "got:    %s\nwanted: %s\n", json, expect);
        TEST_CHECK(strcmp(json, expect) == 0);
        free(json);
    }

    ReplayTimeline_Free(tl);
    ReplayTimeline_Free(NULL);
    RsproxRecording_Free(&rec);
}

/* A long chat line is cut to REPLAY_CHAT_TEXT_MAX bytes; a recording with no
 * ticks still answers. */
static void
test_edges(void)
{
    struct GameProtoRevTable const* rev = GameProtoRev_Osrs(241);
    struct RsproxRecording rec;
    struct ReplayTimeline* tl;
    struct Rec r;
    char err[256];
    char text[200];
    uint8_t buf[256];
    int n;

    memset(text, 'a', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    memset(&r, 0, sizeof(r));
    w_header(&r);
    {
        MsgMessageGame m = { 0 };

        m.message = text;
        n = ENCODE(buf, packet_message_game_osrs241_out, &m);
        w_server(&r, rev, 10, PKT_NAME_MESSAGE_GAME, buf, n);
    }
    TEST_CHECK(RsproxRecording_Open(&rec, r.data, r.len, err, sizeof(err)) == 0);
    TEST_CHECK(RsproxRecording_Index(&rec, rev, err, sizeof(err)) == 0);
    TEST_CHECK(rec.tick_count == 0);
    tl = ReplayTimeline_Build(&rec, rev);
    TEST_CHECK(tl->lanes[REPLAY_LANE_CHAT].count == 1);
    TEST_CHECK(strlen(ReplayTimeline_ChatText(tl, &tl->lanes[REPLAY_LANE_CHAT].marks[0])) ==
               REPLAY_CHAT_TEXT_MAX);
    TEST_CHECK(ReplayTimeline_TickAtMs(tl, 5) == 0);
    TEST_CHECK(ReplayTimeline_MsAtTick(tl, 0) == 10);
    {
        char* json = ReplayTimeline_Json(tl);

        TEST_CHECK(strncmp(json, "{\"ticks\":0,\"duration_ms\":10,", 28) == 0);
        TEST_CHECK(strstr(json, "\"tick_ms\":[10],\"activity\":[],") != NULL);
        free(json);
    }
    ReplayTimeline_Free(tl);
    RsproxRecording_Free(&rec);
}

int
main(void)
{
    test_timeline();
    test_edges();
    printf("replay_timeline_test: ok\n");
    return 0;
}
