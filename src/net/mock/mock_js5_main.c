/*
 * Standalone JS5 server.
 *
 *     make -C src mock-js5 && src/build/mock_js5 cache.osrs239 43594
 *
 * WHY IT IS STANDALONE, AND WHAT THAT COSTS: an OldSchool client uses ONE port
 * for both JS5 and the game, choosing between them with the first byte it
 * sends (15 = JS5, 14 = game login). So this binary and the game server cannot
 * both own 43594 in a finished deployment -- the branch belongs inside
 * mock230_session's handshake, next to step_init.
 *
 * It exists separately anyway because it is the half that can be verified today
 * against a real client and against the real Jagex service:
 *
 *     tools/js5_probe.py --host 127.0.0.1 --rev 239 --archive 255 --group 255
 *     tools/js5_probe.py --host oldschool1.runescape.com --rev 239 ...
 *
 * and the two answers can be compared field by field. That is how the master
 * index's layout was established rather than guessed (see mock_js5.c).
 *
 * One connection at a time, blocking reads. A cache download is a single
 * client's serial request stream, so there is nothing for concurrency to buy
 * here that would not also be thrown away when this moves into the session.
 */

#include "mock_js5.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define JS5_INIT_OPCODE 15
#define JS5_INIT_PAYLOAD 20 /* p4 revision + p4 seed x4 */
#define JS5_REQUEST_BYTES 4 /* p1 opcode, p1 archive, p2 group */

enum
{
    JS5_REQ_PREFETCH = 0,
    JS5_REQ_URGENT = 1,
    JS5_REQ_PRIORITY_HIGH = 2,
    JS5_REQ_PRIORITY_LOW = 3,
    JS5_REQ_XOR_CHANGE = 4,
};

enum
{
    JS5_RESPONSE_OK = 0,
    JS5_RESPONSE_OUT_OF_DATE = 6,
};

static int
read_exact(int fd, uint8_t* buf, int n)
{
    int got = 0;
    while( got < n )
    {
        ssize_t r = read(fd, buf + got, (size_t)(n - got));
        if( r == 0 )
            return 0; /* peer closed */
        if( r < 0 )
        {
            if( errno == EINTR )
                continue;
            return -1;
        }
        got += (int)r;
    }
    return 1;
}

static int
write_all(int fd, uint8_t const* buf, int n)
{
    int sent = 0;
    while( sent < n )
    {
        ssize_t w = write(fd, buf + sent, (size_t)(n - sent));
        if( w <= 0 )
        {
            if( w < 0 && errno == EINTR )
                continue;
            return -1;
        }
        sent += (int)w;
    }
    return 0;
}

static void
serve(int fd, struct MockJs5* js5, int revision, int verbose)
{
    uint8_t head[1 + JS5_INIT_PAYLOAD];
    if( read_exact(fd, head, 1) != 1 )
        return;

    if( head[0] != JS5_INIT_OPCODE )
    {
        /*
         * Opcode 14 here is a game login arriving at a JS5-only listener. Say
         * so rather than dropping the connection silently -- it is the single
         * most likely way to misread this binary's scope.
         */
        fprintf(stderr,
                "js5: first byte %d is not a JS5 handshake%s\n", head[0],
                head[0] == 14 ? " (that is a game login; this binary serves JS5 only)"
                              : "");
        return;
    }
    if( read_exact(fd, head + 1, JS5_INIT_PAYLOAD) != 1 )
        return;

    int client_rev = (head[1] << 24) | (head[2] << 16) | (head[3] << 8) | head[4];
    if( client_rev != revision )
    {
        uint8_t out_of_date = JS5_RESPONSE_OUT_OF_DATE;
        fprintf(stderr, "js5: client revision %d, this cache serves %d -> out of date\n",
                client_rev, revision);
        write_all(fd, &out_of_date, 1);
        return;
    }

    uint8_t ok = JS5_RESPONSE_OK;
    if( write_all(fd, &ok, 1) < 0 )
        return;
    fprintf(stderr, "js5: client at revision %d accepted\n", client_rev);

    int cap = mock_js5_max_response_bytes(js5) + 4096;
    uint8_t* out = malloc((size_t)cap);
    if( !out )
        return;

    long served = 0;
    long bytes = 0;
    for( ;; )
    {
        uint8_t req[JS5_REQUEST_BYTES];
        int r = read_exact(fd, req, JS5_REQUEST_BYTES);
        if( r <= 0 )
            break;

        int opcode = req[0];
        int archive = req[1];
        int group = (req[2] << 8) | req[3];

        if( opcode == JS5_REQ_PRIORITY_HIGH || opcode == JS5_REQ_PRIORITY_LOW ||
            opcode == JS5_REQ_XOR_CHANGE )
        {
            /* Flow-control and obfuscation hints. A server is free to ignore
             * them; the XOR key only matters if we choose to use one, and we
             * do not. */
            continue;
        }
        if( opcode != JS5_REQ_PREFETCH && opcode != JS5_REQ_URGENT )
        {
            fprintf(stderr, "js5: unknown request opcode %d\n", opcode);
            break;
        }

        int n = mock_js5_build_response(js5, archive, group, out, cap);
        if( n <= 0 )
        {
            fprintf(stderr, "js5: no group %d/%d\n", archive, group);
            continue;
        }
        if( write_all(fd, out, n) < 0 )
            break;
        served++;
        bytes += n;
        if( verbose )
            fprintf(stderr, "js5: %d/%d -> %d bytes\n", archive, group, n);
    }

    fprintf(stderr, "js5: session ended, %ld groups / %ld bytes served\n", served, bytes);
    free(out);
}

int
main(int argc, char** argv)
{
    char const* cache_dir = argc > 1 ? argv[1] : "cache.osrs239";
    int port = argc > 2 ? atoi(argv[2]) : 43594;
    int revision = argc > 3 ? atoi(argv[3]) : 239;
    int verbose = getenv("JS5_VERBOSE") != NULL;

    signal(SIGPIPE, SIG_IGN);

    struct MockJs5* js5 = mock_js5_open(cache_dir);
    if( !js5 )
        return 1;

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if( listener < 0 )
    {
        perror("socket");
        return 1;
    }
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if( bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0 )
    {
        perror("bind");
        return 1;
    }
    if( listen(listener, 8) < 0 )
    {
        perror("listen");
        return 1;
    }
    fprintf(stderr, "js5: listening on port %d, serving revision %d from %s\n", port,
            revision, cache_dir);

    for( ;; )
    {
        struct sockaddr_in peer;
        socklen_t peerlen = sizeof(peer);
        int fd = accept(listener, (struct sockaddr*)&peer, &peerlen);
        if( fd < 0 )
        {
            if( errno == EINTR )
                continue;
            perror("accept");
            break;
        }
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        fprintf(stderr, "js5: connection from %s\n", inet_ntoa(peer.sin_addr));
        serve(fd, js5, revision, verbose);
        close(fd);
    }

    close(listener);
    mock_js5_close(js5);
    return 0;
}
