/*
 * Standalone TCP mock server for the OSRS rev-230 protocol.
 *
 * Accepts one client, performs the 230 login handshake (RSA-decrypts the login
 * block with the mock private key, arms both ISAAC ciphers), sends the
 * on-login burst, and then runs a real 600 ms game tick: the client walks,
 * npcs roam, items equip and drag, and every one of those is the server's
 * decision, echoed back through PLAYER_INFO / NPC_INFO / UPDATE_INV_PARTIAL.
 *
 *   make -C src mock230
 *   src/build/mock230 [port]
 *   src/torirs --manifest manifest_osrs230.ini --user test --pass test
 *
 * Env:
 *   MOCK230_VERBOSE=1   log every packet in and out
 *   MOCK230_CACHE=dir   cache to read obj metadata from (default cache.osrs230)
 *   MOCK230_SCRIPTS=dir compiled script pack (default src/net/mock/scripts/build)
 *   MOCK230_ZONE=x,z    origin zone to spawn in (default 426,408)
 *
 * The RSA keypair is fixed and matches manifest_osrs230.ini: the client
 * encrypts the login block with (E=10001, N), the mock decrypts with (D, N).
 */
#include "mock230.h"

#include "net/isaac.h"
#include "net/rev/osrs230/packetout.h"
#include "net/rsa.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <rsareabuf.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Mock private key (D) + modulus (N); the client uses (10001, N). */
static const char* MOCK_RSA_D =
    "795ae97353fb30c7d069891454220c40d0c006732e9180cd3b74f0fcd02a74980d363ffc0a19e8cfe13b7d556"
    "b8874e22576d0e3bb214b8bbfdcdee7295036a63ad32111d525f65546d3bae015c190654a52424f1a946d4135"
    "1f17a48e80fe0a33a0118c8c436f810f2585cf874828890d486c26ac1cd29a824d2fdac6032305";
static const char* MOCK_RSA_N =
    "c30fcbc01e071ff224ea1a6508052d1140f87abaf8f40f7004efa59926708e5d99e2bc832fdca8276482dd0d6"
    "90f644156850f47886f8032b3e9aa52508d24e8c9b7c50b8d8b8716fb8c3993bb6ce15e2124883edb7aaa7241"
    "a8b530f806c61cd1345879413fc105980a4f5fcdb3f0d743b14b16228b4d1496c83d3755a78a19";

#define SESSION_ID 0x0102030405060708ULL
#define MOCK230_TICK_MS 600

/* Lumbridge-ish default. zone = tile >> 3; scene origin = (zone - 6) * 8. */
#define DEFAULT_ZONE_X 426
#define DEFAULT_ZONE_Z 408

/* ------------------------------------------------------------------ */
/* Inbound framing                                                     */
/* ------------------------------------------------------------------ */

/*
 * The client stream is [ISAAC-scrambled opcode][optional length][payload].
 * Sizes come from the shared rev table, so client and server cannot disagree
 * about where a packet ends.
 *
 * The one thing this must never do is guess: an unknown opcode is framed as
 * zero-length, which resynchronises on the next byte instead of consuming a
 * made-up payload and desyncing the ISAAC stream for good.
 */
struct Mock230Reader
{
    uint8_t buf[8192];
    int len;
};

static int
read_available(
    struct Mock230Server* srv,
    struct Mock230Reader* reader)
{
    int space = (int)sizeof(reader->buf) - reader->len;
    int got;
    if( space <= 0 )
        return 1;
    got = (int)read(srv->fd, reader->buf + reader->len, (size_t)space);
    if( got == 0 )
        return 0;
    if( got < 0 )
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 1 : 0;
    reader->len += got;
    return 1;
}

/* Pull every complete packet out of the buffer and dispatch it. */
static void
drain_packets(
    struct Mock230Server* srv,
    struct Mock230Reader* reader)
{
    int pos = 0;

    for( ;; )
    {
        int opcode;
        int size;
        int header;
        int payload_len;
        int name;

        if( reader->len - pos < 1 )
            break;
        opcode = (reader->buf[pos] - isaac_next(srv->cipher_in)) & 0xff;
        size = osrs230_packetout_size(opcode);

        if( size == PKTOUT_LENGTH_VARU8 )
        {
            if( reader->len - pos < 2 )
            {
                /* Not enough to know the length yet — but the ISAAC keystream
                 * for this opcode has already been consumed, so the loop must
                 * not re-read it. Rewinding is not an option with a stream
                 * cipher; instead this is deliberately unreachable in practice
                 * because a var-u8 packet's length byte is always sent in the
                 * same write() as its opcode. Bail loudly if that changes. */
                fprintf(stderr, "mock230: split var-u8 header for op %d\n", opcode);
                break;
            }
            payload_len = reader->buf[pos + 1];
            header = 2;
        }
        else if( size == PKTOUT_LENGTH_VARU16 )
        {
            if( reader->len - pos < 3 )
            {
                fprintf(stderr, "mock230: split var-u16 header for op %d\n", opcode);
                break;
            }
            payload_len = (reader->buf[pos + 1] << 8) | reader->buf[pos + 2];
            header = 3;
        }
        else
        {
            payload_len = size;
            header = 1;
        }

        if( reader->len - pos < header + payload_len )
        {
            /* Same caveat as above: the opcode byte is already descrambled, so
             * consume it now and stash the rest for the next drain would
             * require a partial-packet state machine. The client writes each
             * packet with a single write(), so a torn packet means something
             * is wrong upstream. */
            fprintf(
                stderr,
                "mock230: truncated op %d (have %d, need %d)\n",
                opcode,
                reader->len - pos,
                header + payload_len);
            break;
        }

        name = osrs230_packetout_name(opcode);
        if( name == PKTOUT_NAME_NONE && srv->verbose )
            fprintf(stderr, "mock230: <- unknown op %d (%d bytes)\n", opcode, payload_len);
        else if( name != PKTOUT_NAME_NONE )
            mock230_world_handle(srv, name, reader->buf + pos + header, payload_len);

        pos += header + payload_len;
    }

    if( pos > 0 )
    {
        memmove(reader->buf, reader->buf + pos, (size_t)(reader->len - pos));
        reader->len -= pos;
    }
}

/* ------------------------------------------------------------------ */
/* Login                                                               */
/* ------------------------------------------------------------------ */

static int
read_full(
    int fd,
    uint8_t* buf,
    int count)
{
    int got = 0;
    while( got < count )
    {
        int step = (int)read(fd, buf + got, (size_t)(count - got));
        if( step <= 0 )
            return got;
        got += step;
    }
    return got;
}

/* Returns 1 once the game stream is armed. */
static int
handshake(
    struct Mock230Server* srv,
    int fd)
{
    uint8_t buf[2048];
    struct RSAreaBuf out;
    struct RSAreaBuf in;
    int payload_len;
    int rsa_size;
    struct rsa rsa;
    uint8_t plain[512];
    int plain_len;
    int32_t seed[4];
    int32_t seed_in[4];
    uint64_t session;
    char user[64];
    char pass[64];

    /* 1. INIT_GAME_CONNECTION. */
    if( read_full(fd, buf, 1) != 1 || buf[0] != 14 )
    {
        fprintf(stderr, "mock230: expected opcode 14, got %d\n", buf[0]);
        return 0;
    }
    rsab_wrap(&out, buf, sizeof(buf));
    rsab_p1(&out, 0);
    rsab_p8(&out, (int64_t)SESSION_ID);
    if( write(fd, buf, rsab_len(&out)) < 0 )
        return 0;

    /* 2. GAMELOGIN + u16 length + block. */
    if( read_full(fd, buf, 3) != 3 || buf[0] != 16 )
    {
        fprintf(stderr, "mock230: expected opcode 16\n");
        return 0;
    }
    payload_len = (buf[1] << 8) | buf[2];
    if( payload_len < 13 || payload_len > (int)sizeof(buf) ||
        read_full(fd, buf, payload_len) != payload_len )
    {
        fprintf(stderr, "mock230: short login block (%d)\n", payload_len);
        return 0;
    }

    rsab_wrap(&in, buf, (size_t)payload_len);
    (void)rsab_g4(&in); /* version */
    (void)rsab_g4(&in); /* subVersion */
    (void)rsab_g1(&in); /* clientType */
    (void)rsab_g1(&in); /* platformType */
    (void)rsab_g1(&in); /* externalAuth */
    rsa_size = rsab_g2(&in);
    if( !rsab_ok(&in) )
        return 0;

    if( rsa_init(&rsa, MOCK_RSA_D, MOCK_RSA_N) != 0 )
    {
        fprintf(stderr, "mock230: rsa_init failed\n");
        return 0;
    }
    plain_len = rsa_crypt(&rsa, buf + in.pos, (size_t)rsa_size, plain, sizeof(plain));
    if( plain_len <= 0 )
    {
        fprintf(stderr, "mock230: rsa decrypt failed\n");
        return 0;
    }

    rsab_wrap(&in, plain, (size_t)plain_len);
    (void)rsab_g1(&in); /* encryption check byte */
    for( int i = 0; i < 4; i++ )
        seed[i] = rsab_g4(&in);
    session = (uint64_t)rsab_g8(&in);
    (void)rsab_g1(&in); /* auth type */
    rsab_gjstr(&in, user, sizeof(user), RSAB_JSTR_NUL);
    rsab_gjstr(&in, pass, sizeof(pass), RSAB_JSTR_NUL);
    fprintf(
        stderr,
        "mock230: login user='%s' session=%s\n",
        user,
        session == SESSION_ID ? "ok" : "MISMATCH");

    /* 3. Success, then the game stream. */
    {
        uint8_t ok = 2;
        if( write(fd, &ok, 1) < 0 )
            return 0;
    }

    /* The client seeds its out-cipher with the raw seed and its in-cipher with
     * seed + 50, so the server is the mirror image. */
    for( int i = 0; i < 4; i++ )
        seed_in[i] = seed[i] + 50;
    srv->cipher_out = isaac_new(seed_in, 4);
    srv->cipher_in = isaac_new((int32_t*)seed, 4);
    return srv->cipher_out != NULL && srv->cipher_in != NULL;
}

/* ------------------------------------------------------------------ */
/* Session                                                             */
/* ------------------------------------------------------------------ */

/*
 * Where the compiled script pack lives.
 *
 * The ../ fallback mirrors mock230_objinfo_load: the mock is run both from the
 * repo root and from src/, and having it work either way is worth more than
 * insisting on one.
 */
static const char*
mock230_script_dir(void)
{
    static char resolved[512];
    const char* configured = getenv("MOCK230_SCRIPTS");
    struct stat info;

    if( configured )
        return configured;

    snprintf(resolved, sizeof(resolved), "src/net/mock/scripts/build");
    if( stat(resolved, &info) == 0 )
        return resolved;

    snprintf(resolved, sizeof(resolved), "../src/net/mock/scripts/build");
    return resolved;
}

static long
now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long)tv.tv_sec * 1000L) + (tv.tv_usec / 1000L);
}

static void
serve(
    struct Mock230Server* srv,
    int fd,
    int zone_x,
    int zone_z)
{
    struct Mock230Reader reader = { { 0 }, 0 };
    long next_tick;

    memset(srv, 0, sizeof(*srv));
    srv->fd = fd;
    srv->verbose = getenv("MOCK230_VERBOSE") != NULL;

    if( !handshake(srv, fd) )
        return;

    mock230_scripts_load(srv, mock230_script_dir());

    mock230_world_init(srv, zone_x, zone_z);
    mock230_world_login(srv);
    next_tick = now_ms() + MOCK230_TICK_MS;

    while( srv->fd >= 0 )
    {
        fd_set readable;
        struct timeval timeout;
        long wait_ms = next_tick - now_ms();

        if( wait_ms < 0 )
            wait_ms = 0;
        FD_ZERO(&readable);
        FD_SET(fd, &readable);
        timeout.tv_sec = wait_ms / 1000;
        timeout.tv_usec = (wait_ms % 1000) * 1000;

        if( select(fd + 1, &readable, NULL, NULL, &timeout) < 0 )
        {
            if( errno == EINTR )
                continue;
            break;
        }

        if( FD_ISSET(fd, &readable) )
        {
            if( !read_available(srv, &reader) )
                break;
            drain_packets(srv, &reader);
        }

        if( now_ms() >= next_tick )
        {
            mock230_world_tick(srv);
            /* Anchor to the schedule rather than to "now", so a slow tick does
             * not push every later tick out. */
            next_tick += MOCK230_TICK_MS;
            if( now_ms() - next_tick > 5 * MOCK230_TICK_MS )
                next_tick = now_ms() + MOCK230_TICK_MS;
        }
    }

    isaac_free(srv->cipher_out);
    isaac_free(srv->cipher_in);
    srv->cipher_out = NULL;
    srv->cipher_in = NULL;
}

int
main(
    int argc,
    char** argv)
{
    struct Mock230Server srv;
    int port = argc > 1 ? atoi(argv[1]) : 43595;
    int zone_x = DEFAULT_ZONE_X;
    int zone_z = DEFAULT_ZONE_Z;
    int listener;
    int reuse = 1;
    struct sockaddr_in addr;
    const char* cache_dir = getenv("MOCK230_CACHE");
    const char* zone_env = getenv("MOCK230_ZONE");

    if( zone_env )
        sscanf(zone_env, "%d,%d", &zone_x, &zone_z);
    mock230_objinfo_load(cache_dir ? cache_dir : "cache.osrs230");
    mock230_npcinfo_load(cache_dir ? cache_dir : "cache.osrs230");
    mock230_seqinfo_load(cache_dir ? cache_dir : "cache.osrs230");

    /* --selftest: run the game logic with no socket and exit. */
    if( argc > 1 && strcmp(argv[1], "--selftest") == 0 )
    {
        int failures = mock230_world_selftest();
        mock230_objinfo_free();
        return failures ? 1 : 0;
    }

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if( listener < 0 )
    {
        perror("socket");
        return 1;
    }
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if( bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
    {
        perror("bind");
        return 1;
    }
    listen(listener, 1);
    fprintf(stderr, "mock230: listening on 127.0.0.1:%d (zone %d,%d)\n", port, zone_x, zone_z);

    for( ;; )
    {
        int fd = accept(listener, NULL, NULL);
        if( fd < 0 )
            continue;
        fprintf(stderr, "mock230: client connected\n");
        serve(&srv, fd, zone_x, zone_z);
        close(fd);
        fprintf(stderr, "mock230: client disconnected\n");
    }

    mock230_objinfo_free();
    return 0;
}
