/*
 * The party link's framing (torirs_server_embed.h), over a real socket pair.
 *
 * The three-client run (test/raids/README.md "A party run") proves the link
 * end to end with three real clients; this proves the part that a torn read
 * would break silently: a frame is taken only once whole, a DATA frame of any
 * size arrives byte-exact, frames come out in the order they went in, and a
 * closed peer reads as closed rather than as an empty link.
 *
 * Run: make -C src test-embed-party-link
 */

#include "torirsserver/torirs_server_embed.h"

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
    printf("party-link: %s\n", g_failures ? "FAILURES" : "all ok");
    return g_failures ? 1 : 0;
}
