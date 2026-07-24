/*
 * Standalone TCP mock server for the OSRS rev-230 protocol. Accepts one client,
 * performs the 230 login handshake (RSA-decrypts the login block with the mock
 * private key, arms ISAAC), then sends REBUILD_NORMAL + a small on-login burst
 * and idles — no other interaction. Lets the real client
 *   src/torirs --manifest manifest_osrs230.ini --connect localhost:PORT
 * log in and build the world offline.
 *
 * Build:  make -C src mock230     Run:  src/build/mock230 [port]
 *
 * The RSA keypair is fixed (see mock230_rsa scratch / manifest_osrs230.ini): the
 * client encrypts the login block with (E=10001, N); the mock decrypts with
 * (D, N). Both are the same modexp (rsa_crypt).
 */
#include "net/isaac.h"
#include "net/rsa.h"

#include <rsbuffer.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Mock private key (D) + modulus (N); client uses (10001, N). */
static const char* MOCK_RSA_D =
    "795ae97353fb30c7d069891454220c40d0c006732e9180cd3b74f0fcd02a74980d363ffc0a19e8cfe13b7d556"
    "b8874e22576d0e3bb214b8bbfdcdee7295036a63ad32111d525f65546d3bae015c190654a52424f1a946d4135"
    "1f17a48e80fe0a33a0118c8c436f810f2585cf874828890d486c26ac1cd29a824d2fdac6032305";
static const char* MOCK_RSA_N =
    "c30fcbc01e071ff224ea1a6508052d1140f87abaf8f40f7004efa59926708e5d99e2bc832fdca8276482dd0d6"
    "90f644156850f47886f8032b3e9aa52508d24e8c9b7c50b8d8b8716fb8c3993bb6ce15e2124883edb7aaa7241"
    "a8b530f806c61cd1345879413fc105980a4f5fcdb3f0d743b14b16228b4d1496c83d3755a78a19";

#define SESSION_ID 0x0102030405060708ULL

/* Lumbridge-ish login spot. zone = tile>>3; scene base = (zone-6)*8. */
#define LOGIN_ZONE_X 402
#define LOGIN_ZONE_Z 402

static int
read_full(int fd, uint8_t* buf, int n)
{
    int got = 0;
    while( got < n )
    {
        int r = (int)read(fd, buf + got, (size_t)(n - got));
        if( r <= 0 )
            return got;
        got += r;
    }
    return got;
}

/* Frame + ISAAC-scramble one server->client packet and send it. */
static void
send_packet(int fd, struct Isaac* enc, int opcode, uint8_t const* payload, int len, int var)
{
    uint8_t frame[8192];
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, frame, sizeof(frame));
    p1(&buf, (opcode + isaac_next(enc)) & 0xff);
    if( var == 1 )
        p1(&buf, len);
    else if( var == 2 )
        p2(&buf, len);
    if( len > 0 )
        pbuf(&buf, (uint8_t*)payload, len);
    write(fd, frame, (size_t)buf.position);
}

static void
send_rebuild_normal(int fd, struct Isaac* enc)
{
    uint8_t body[4096];
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, body, sizeof(body));

    /* RSProt RebuildNormalEncoder: worldArea, zoneX(p2Alt2), zoneZ, keyCount,
     * keyCount*4 XTEA ints. Map squares span the 104-tile scene: base=(zone-6)*8. */
    p2(&buf, 0); /* worldArea */
    /* p2Alt2 zoneX: writeByte(v>>8); writeByte(v+128) */
    p1(&buf, (LOGIN_ZONE_X >> 8) & 0xff);
    p1(&buf, (LOGIN_ZONE_X + 128) & 0xff);
    p2(&buf, LOGIN_ZONE_Z);

    int base_x = (LOGIN_ZONE_X - 6) * 8;
    int base_z = (LOGIN_ZONE_Z - 6) * 8;
    int sq_x0 = base_x >> 6, sq_x1 = (base_x + 103) >> 6;
    int sq_z0 = base_z >> 6, sq_z1 = (base_z + 103) >> 6;
    int count = (sq_x1 - sq_x0 + 1) * (sq_z1 - sq_z0 + 1);
    p2(&buf, count);
    /* Zero keys for now (unencrypted regions load; real keys wired later). */
    for( int i = 0; i < count * 4; i++ )
        p4(&buf, 0);

    send_packet(fd, enc, 68, body, buf.position, 2);
    fprintf(
        stderr,
        "mock230: sent REBUILD_NORMAL zoneX=%d zoneZ=%d squares=%d\n",
        LOGIN_ZONE_X,
        LOGIN_ZONE_Z,
        count);
}

static void
send_login_burst(int fd, struct Isaac* enc)
{
    send_rebuild_normal(fd, enc);

    /* A few representative on-login packets (Kronos/lc254 order). */
    { /* VARP_SMALL(35): id(2)+val(1) */
        uint8_t b[3] = { 0x00, 0x2A, 0x05 };
        send_packet(fd, enc, 35, b, 3, 0);
    }
    { /* UPDATE_RUNENERGY(77): 2 bytes */
        uint8_t b[2] = { 100, 0 };
        send_packet(fd, enc, 77, b, 2, 0);
    }
    { /* UPDATE_RUNWEIGHT(27): 2 bytes */
        uint8_t b[2] = { 0, 0 };
        send_packet(fd, enc, 27, b, 2, 0);
    }
    { /* MESSAGE_GAME(90): var-byte. type(smart)+... keep minimal: a string */
        uint8_t b[64];
        struct RSCache_Buffer m;
        RSCache_BufferInit(&m, b, sizeof(b));
        p1(&m, 0); /* type */
        pjstr(&m, "Welcome to the mock 230 world.", 0);
        send_packet(fd, enc, 90, b, m.position, 1);
    }
    fprintf(stderr, "mock230: sent on-login burst\n");
}

static int
handle_client(int fd)
{
    uint8_t buf[2048];

    /* 1. INIT_GAME_CONNECTION (opcode 14). */
    if( read_full(fd, buf, 1) != 1 || buf[0] != 14 )
    {
        fprintf(stderr, "mock230: expected opcode 14, got %d\n", buf[0]);
        return -1;
    }
    /* Reply [status 0x00][sessionId i64]. */
    {
        struct RSCache_Buffer o;
        RSCache_BufferInit(&o, buf, sizeof(buf));
        p1(&o, 0);
        p4(&o, (int32_t)(SESSION_ID >> 32));
        p4(&o, (int32_t)(SESSION_ID & 0xffffffff));
        write(fd, buf, (size_t)o.position);
    }

    /* 2. GAMELOGIN (opcode 16) + u16 len + block. */
    if( read_full(fd, buf, 3) != 3 || buf[0] != 16 )
    {
        fprintf(stderr, "mock230: expected opcode 16\n");
        return -1;
    }
    int payload_len = (buf[1] << 8) | buf[2];
    if( payload_len < 13 || read_full(fd, buf, payload_len) != payload_len )
    {
        fprintf(stderr, "mock230: short login block (%d)\n", payload_len);
        return -1;
    }
    {
        struct RSCache_Buffer in;
        in.data = buf;
        in.size = (uint32_t)payload_len;
        in.position = 0;
        (void)g4(&in); /* version */
        (void)g4(&in); /* subVersion */
        (void)g1(&in); /* clientType */
        (void)g1(&in); /* platformType */
        (void)g1(&in); /* externalAuth */
        int rsa_size = g2(&in);

        /* RSA-decrypt the block with the private key. */
        struct rsa rsa;
        if( rsa_init(&rsa, MOCK_RSA_D, MOCK_RSA_N) != 0 )
        {
            fprintf(stderr, "mock230: rsa_init failed\n");
            return -1;
        }
        uint8_t plain[512];
        int plen = rsa_crypt(&rsa, buf + in.position, (size_t)rsa_size, plain, sizeof(plain));
        if( plen <= 0 )
        {
            fprintf(stderr, "mock230: rsa decrypt failed\n");
            return -1;
        }

        struct RSCache_Buffer rb;
        rb.data = plain;
        rb.size = (uint32_t)plen;
        rb.position = 0;
        int enc_check = g1(&rb);
        int32_t seed[4];
        for( int i = 0; i < 4; i++ )
            seed[i] = (int32_t)g4(&rb);
        uint64_t sid = (uint64_t)g8(&rb);
        int auth_type = g1(&rb);
        char const* user = gcstring(&rb);
        char const* pass = gcstring(&rb);
        fprintf(
            stderr,
            "mock230: login encCheck=%d authType=%d sid=%s user='%s' pass-len=%zu\n",
            enc_check,
            auth_type,
            sid == SESSION_ID ? "ok" : "MISMATCH",
            user ? user : "",
            pass ? strlen(pass) : 0);

        /* 3. Success response (plaintext) then GAME stream. */
        uint8_t ok = 2;
        write(fd, &ok, 1);

        /* Arm the server out-cipher = seed+50 (client in-cipher matches). */
        int32_t sin[4];
        for( int i = 0; i < 4; i++ )
            sin[i] = seed[i] + 50;
        struct Isaac* enc = isaac_new(sin, 4);

        /* 4. On-login burst. */
        send_login_burst(fd, enc);

        /* 5. Idle: drain client bytes so the socket stays healthy. */
        fprintf(stderr, "mock230: idling (client logged in)\n");
        for( ;; )
        {
            int r = (int)read(fd, buf, sizeof(buf));
            if( r <= 0 )
                break;
        }
        isaac_free(enc);
    }
    return 0;
}

int
main(int argc, char** argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 43594;

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if( srv < 0 )
    {
        perror("socket");
        return 1;
    }
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if( bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
    {
        perror("bind");
        return 1;
    }
    listen(srv, 1);
    fprintf(stderr, "mock230: listening on 127.0.0.1:%d\n", port);

    for( ;; )
    {
        int fd = accept(srv, NULL, NULL);
        if( fd < 0 )
            continue;
        fprintf(stderr, "mock230: client connected\n");
        handle_client(fd);
        close(fd);
        fprintf(stderr, "mock230: client disconnected\n");
    }
    return 0;
}
