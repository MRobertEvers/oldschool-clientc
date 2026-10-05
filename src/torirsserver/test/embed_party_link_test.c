/*
 * The party link's framing (torirs_server_embed.h), over a real socket pair.
 *
 * The three-client run (test/raids/README.md "A party run") proves the link
 * end to end with three real clients; this proves the part that a torn read
 * would break silently: a frame is taken only once whole, a DATA frame of any
 * size arrives byte-exact, frames come out in the order they went in, and a
 * closed peer reads as closed rather than as an empty link.
 *
 * And the lock step's own rules (raid seam21, "Lockstep, pinned"): SEAT
 * carries the protocol version and k; a member that runs F frames per
 * boundary is accepted at every boundary and one that runs F - 1 is refused
 * at its first judged READY; two members handed the same TICK read the same
 * tick and the same world digest, and the digest moves when a player's tile,
 * hitpoints or the npc count does.
 *
 * Run: make -C src test-embed-party-link
 */

#include "torirsserver/torirs_server_embed.h"
#include "torirsserver/torirs_server.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_failures;

/* Not assert(): these calls do the work, and a build with NDEBUG would drop
 * them along with the check. */
#define MUST(expr)                                                                   \
    do                                                                               \
    {                                                                                \
        if( !(expr) )                                                                \
        {                                                                            \
            fprintf(stderr, "party-link: %s:%d: %s failed\n", __FILE__, __LINE__,    \
                    #expr);                                                          \
            abort();                                                                 \
        }                                                                            \
    } while( 0 )

static void
check(
    int condition,
    const char* what)
{
    printf("party-link: %-60s %s\n", what, condition ? "ok" : "FAILED");
    if( !condition )
        g_failures++;
}

/* Fill until a frame is whole or `tries` fills found nothing more. */
static int
next_frame(
    int fd,
    struct ToriRSServerEmbedLinkReader* in,
    int* type,
    const uint8_t** payload,
    int* len)
{
    for( int tries = 0; tries < 200; tries++ )
    {
        if( ToriRSServer_EmbedLinkNext(in, type, payload, len) )
            return 1;
        if( ToriRSServer_EmbedLinkFill(fd, in, 50) < 0 )
            return ToriRSServer_EmbedLinkNext(in, type, payload, len);
    }
    return 0;
}

/* 4. SEAT: version, seat and k survive the wire; a v1 SEAT reads as v1. */
static void
lockstep_seat(void)
{
    uint8_t seat[TORIRSSERVER_EMBED_LINK_SEAT_LEN];
    uint8_t old_seat[4] = { 0, 0, 0, 2 };
    int version = 0;
    int number = 0;
    int cycles = 0;

    ToriRSServer_EmbedLinkSeatEncode(seat, 3, 10);
    check(ToriRSServer_EmbedLinkSeatDecode(seat, sizeof(seat), &version, &number, &cycles) &&
              version == TORIRSSERVER_EMBED_PARTY_PROTOCOL && number == 3 && cycles == 10,
          "SEAT carries the protocol version, the seat and k");
    check(ToriRSServer_EmbedLinkSeatDecode(old_seat, 4, &version, &number, &cycles) &&
              version == 1 && number == 2,
          "a 4-byte (protocol 1) SEAT reads as version 1, so it is refused by name");
    check(!ToriRSServer_EmbedLinkSeatDecode(seat, 7, &version, &number, &cycles),
          "a SEAT of any other length is a protocol break");
}

/*
 * One member: a clock that moves k x 20 ms per frame and says READY at each
 * 600 ms boundary with the frames it ran since the last one (the arithmetic
 * of net_transport_embed.c's embed_clock_step), skipping `short_at`'s frame
 * once to model a member that drifted. Its READYs go over a real socket and
 * the leader's judgement (ToriRSServer_EmbedPartyReadyCheck) reads them back.
 * Returns how many READYs the leader accepted before refusing one (or all).
 */
static int
member_boundaries(
    int cycles_per_frame,
    int boundaries,
    int short_at)
{
    int sv[2];
    struct ToriRSServerEmbedLinkReader in;
    int const expected = ToriRSServer_EmbedPartyFramesPerTick(cycles_per_frame);
    long clock = 0;
    long next_tick = 0;
    int frames = 0;
    int sent = 0;
    int accepted = 0;

    memset(&in, 0, sizeof(in));
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    while( sent < boundaries )
    {
        clock += 20L * cycles_per_frame;
        frames++;
        if( sent == short_at && sent > 0 && frames == 1 )
        {
            /* The drift: one frame of this interval never happened. */
            clock += 20L * cycles_per_frame;
        }
        if( next_tick == 0 || clock >= next_tick )
        {
            uint8_t ready[TORIRSSERVER_EMBED_LINK_READY_LEN];

            next_tick = next_tick == 0 ? clock + 600 : next_tick + 600;
            ToriRSServer_EmbedLinkReadyEncode(ready, frames);
            MUST(ToriRSServer_EmbedLinkSend(sv[0], TORIRSSERVER_EMBED_LINK_READY, ready,
                                            sizeof(ready)));
            frames = 0;
            sent++;
        }
    }
    close(sv[0]);
    for( int k = 0; k < boundaries; k++ )
    {
        int type = 0;
        const uint8_t* payload = NULL;
        int len = 0;
        int count = -1;

        MUST(next_frame(sv[1], &in, &type, &payload, &len));
        MUST(type == TORIRSSERVER_EMBED_LINK_READY);
        MUST(ToriRSServer_EmbedLinkReadyDecode(payload, len, &count));
        if( !ToriRSServer_EmbedPartyReadyCheck(2, count, expected, k == 0) )
            break;
        accepted++;
    }
    ToriRSServer_EmbedLinkReaderFree(&in);
    close(sv[1]);
    return accepted;
}

/* 5. F frames per boundary is accepted; F - 1 is refused. */
static void
lockstep_frames(int cycles_per_frame)
{
    char what[96];
    int const f = ToriRSServer_EmbedPartyFramesPerTick(cycles_per_frame);

    snprintf(what, sizeof(what), "k=%d: a member running F=%d frames per tick: 6 of 6 READYs",
             cycles_per_frame, f);
    check(member_boundaries(cycles_per_frame, 6, -1) == 6, what);
    snprintf(what, sizeof(what), "k=%d: a member running F-1=%d at boundary 3 is refused there",
             cycles_per_frame, f - 1);
    check(member_boundaries(cycles_per_frame, 6, 3) == 3, what);
}

/* 6. The digest: two members handed one TICK agree; the digest moves with
 * the world. */
static void
lockstep_digest(void)
{
    struct ToriRSServer* srv = (struct ToriRSServer*)calloc(1, sizeof(*srv));
    uint8_t tick[TORIRSSERVER_EMBED_LINK_TICK_LEN];
    uint32_t base;
    uint32_t got[2] = { 0, 0 };
    int ticks[2] = { 0, 0 };

    MUST(srv);
    check(ToriRSServer_EmbedWorldDigest(srv) == 0, "an unbuilt world's digest is 0");
    srv->world_built = 1;
    srv->tick = 149;
    srv->players[0].active = 1;
    srv->players[0].x = 3222;
    srv->players[0].z = 3218;
    srv->players[0].hitpoints = 99;
    srv->players[1].active = 1;
    srv->players[1].x = 3223;
    srv->players[1].z = 3218;
    srv->players[1].hitpoints = 99;
    srv->npc_slot_max = 3;
    srv->npcs[0].active = 1;
    srv->npcs[2].active = 1;
    base = ToriRSServer_EmbedWorldDigest(srv);
    ToriRSServer_EmbedLinkTickEncode(tick, srv->tick, base);
    for( int m = 0; m < 2; m++ )
    {
        int sv[2];
        struct ToriRSServerEmbedLinkReader in;
        int type = 0;
        const uint8_t* payload = NULL;
        int len = 0;

        memset(&in, 0, sizeof(in));
        MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        MUST(ToriRSServer_EmbedLinkSend(sv[0], TORIRSSERVER_EMBED_LINK_TICK, tick, sizeof(tick)));
        MUST(next_frame(sv[1], &in, &type, &payload, &len));
        MUST(type == TORIRSSERVER_EMBED_LINK_TICK);
        MUST(ToriRSServer_EmbedLinkTickDecode(payload, len, &ticks[m], &got[m]));
        ToriRSServer_EmbedLinkReaderFree(&in);
        close(sv[0]);
        close(sv[1]);
    }
    check(ticks[0] == 149 && ticks[1] == 149 && got[0] == base && got[1] == base,
          "two members handed one TICK read tick 149 and the leader's digest");
    check(ToriRSServer_EmbedWorldDigest(srv) == base, "the same world digests the same");
    srv->players[1].hitpoints = 98;
    check(ToriRSServer_EmbedWorldDigest(srv) != base, "one hitpoint moves the digest");
    srv->players[1].hitpoints = 99;
    srv->players[0].x = 3221;
    check(ToriRSServer_EmbedWorldDigest(srv) != base, "one tile moves the digest");
    srv->players[0].x = 3222;
    srv->npcs[1].active = 1;
    check(ToriRSServer_EmbedWorldDigest(srv) != base, "one more npc moves the digest");
    srv->npcs[1].active = 0;
    check(ToriRSServer_EmbedWorldDigest(srv) == base, "and putting it all back restores it");
    check(!ToriRSServer_EmbedLinkTickDecode(tick, 4, &ticks[0], &got[0]),
          "a 4-byte (protocol 1) TICK is a protocol break");
    free(srv);
}

int
main(void)
{
    int sv[2];
    struct ToriRSServerEmbedLinkReader in;
    int type = 0;
    const uint8_t* payload = NULL;
    int len = -1;
    uint8_t seat[4] = { 0, 0, 0, 3 };
    uint8_t* big;
    int big_len = 3 * 1024 * 1024 + 17;
    int big_ok = 1;

    memset(&in, 0, sizeof(in));
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    /* 1. A frame torn inside its header is not a frame yet. */
    {
        uint8_t header[5] = { TORIRSSERVER_EMBED_LINK_SEAT, 0, 0, 0, 4 };

        MUST(write(sv[0], header, 3) == 3);
        ToriRSServer_EmbedLinkFill(sv[1], &in, 100);
        check(!ToriRSServer_EmbedLinkNext(&in, &type, &payload, &len),
              "three bytes of a header are not a frame");
        MUST(write(sv[0], header + 3, 2) == 2);
        MUST(write(sv[0], seat, 2) == 2);
        ToriRSServer_EmbedLinkFill(sv[1], &in, 100);
        check(!ToriRSServer_EmbedLinkNext(&in, &type, &payload, &len),
              "a header and half its payload are not a frame");
        MUST(write(sv[0], seat + 2, 2) == 2);
        check(next_frame(sv[1], &in, &type, &payload, &len) && type == 'S' && len == 4 &&
                  payload[3] == 3,
              "the SEAT frame comes out whole once its last byte lands");
    }

    /* 2. DATA, READY, TICK keep their order; a DATA of 3 MB arrives exact. */
    big = (uint8_t*)malloc((size_t)big_len);
    MUST(big);
    for( int i = 0; i < big_len; i++ )
        big[i] = (uint8_t)(i * 31 + 7);
    {
        /* The writer blocks on a 3 MB frame, so it writes from a child. */
        pid_t child = fork();

        MUST(child >= 0);
        if( child == 0 )
        {
            uint8_t tick[4] = { 0, 0, 1, 2 };

            close(sv[1]);
            MUST(ToriRSServer_EmbedLinkSend(sv[0], TORIRSSERVER_EMBED_LINK_DATA, big, big_len));
            MUST(ToriRSServer_EmbedLinkSend(sv[0], TORIRSSERVER_EMBED_LINK_READY, NULL, 0));
            MUST(ToriRSServer_EmbedLinkSend(sv[0], TORIRSSERVER_EMBED_LINK_TICK, tick, 4));
            close(sv[0]);
            _exit(0);
        }
        close(sv[0]);
    }
    check(next_frame(sv[1], &in, &type, &payload, &len) && type == 'D' && len == big_len,
          "a 3 MB DATA frame is one frame of the right length");
    for( int i = 0; type == 'D' && len == big_len && i < big_len; i++ )
        if( payload[i] != big[i] )
        {
            big_ok = 0;
            break;
        }
    check(type == 'D' && len == big_len && big_ok, "and every byte of it is the byte sent");
    check(next_frame(sv[1], &in, &type, &payload, &len) && type == 'R' && len == 0,
          "READY follows the DATA it closes");
    check(next_frame(sv[1], &in, &type, &payload, &len) && type == 'T' && len == 4 &&
              payload[2] == 1 && payload[3] == 2,
          "TICK follows READY and carries its tick (258)");

    /* 3. The writer is gone: closed, not "nothing yet". */
    {
        int filled = 0;

        for( int tries = 0; tries < 50 && filled >= 0; tries++ )
            filled = ToriRSServer_EmbedLinkFill(sv[1], &in, 50);
        check(filled < 0, "a closed peer reads as closed (-1)");
        check(!ToriRSServer_EmbedLinkNext(&in, &type, &payload, &len),
              "and leaves no phantom frame behind");
    }

    ToriRSServer_EmbedLinkReaderFree(&in);
    close(sv[1]);
    free(big);

    lockstep_seat();
    lockstep_frames(1);
    lockstep_frames(10);
    lockstep_digest();

    printf("party-link: %s\n", g_failures ? "FAILURES" : "all ok");
    return g_failures ? 1 : 0;
}
