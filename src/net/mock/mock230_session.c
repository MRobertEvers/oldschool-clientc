/*
 * The login handshake and the inbound frame reader, as a state machine.
 *
 * See mock230_session.h for why it is one. The short version: every wait is
 * "have enough bytes arrived yet?", answered against a buffer, so nothing here
 * blocks and an in-process client can feed the server on the same thread.
 */

#include "mock230_session.h"

#include "net/rev/osrs239/loginblock.h"

#include "mock_js5.h"

#include <stdlib.h>

#include <xteas.h>

#include "mock230.h"

#include "net/isaac.h"
#include "net/rev/osrs230/packetout.h"
#include "net/rsa.h"

#include <rsareabuf.h>

#include <stdio.h>
#include <string.h>

/*
 * The server's identity. Fixed, and matching manifest_osrs230.ini: the client
 * encrypts the login block with (E=10001, N), this decrypts with (D, N).
 */
static const char* MOCK_RSA_D =
    "795ae97353fb30c7d069891454220c40d0c006732e9180cd3b74f0fcd02a74980d363ffc0a19e8cfe13b7d556"
    "b8874e22576d0e3bb214b8bbfdcdee7295036a63ad32111d525f65546d3bae015c190654a52424f1a946d4135"
    "1f17a48e80fe0a33a0118c8c436f810f2585cf874828890d486c26ac1cd29a824d2fdac6032305";

/* The modulus, stated once and shared by both halves. */
const char* const MOCK230_RSA_PUBLIC_EXPONENT = "10001";
const char* const MOCK230_RSA_PUBLIC_MODULUS =
    "c30fcbc01e071ff224ea1a6508052d1140f87abaf8f40f7004efa59926708e5d99e2bc832fdca8276482dd0d6"
    "90f644156850f47886f8032b3e9aa52508d24e8c9b7c50b8d8b8716fb8c3993bb6ce15e2124883edb7aaa7241"
    "a8b530f806c61cd1345879413fc105980a4f5fcdb3f0d743b14b16228b4d1496c83d3755a78a19";

#define MOCK_RSA_N MOCK230_RSA_PUBLIC_MODULUS

#define SESSION_ID 0x0102030405060708ULL

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

void
mock230_session_init(
    struct Mock230Session* session,
    const struct Mock230Transport* transport,
    int verbose)
{
    memset(session, 0, sizeof(*session));
    session->transport = *transport;
    session->state = MOCK230_SESSION_INIT;
    session->verbose = verbose;
    /* Not memset's 0, which is a real opcode — a zeroed session would decode
     * its first byte as the body of a packet nobody sent. */
    session->pending_opcode = -1;
}

void
mock230_session_free(struct Mock230Session* session)
{
    isaac_free(session->cipher_out);
    isaac_free(session->cipher_in);
    session->cipher_out = NULL;
    session->cipher_in = NULL;
    if( session->transport.close )
        session->transport.close(session->transport.ctx);
    session->state = MOCK230_SESSION_DEAD;
}

void
mock230_session_kill(struct Mock230Session* session)
{
    session->state = MOCK230_SESSION_DEAD;
}

int
mock230_session_alive(const struct Mock230Session* session)
{
    return session->state != MOCK230_SESSION_DEAD;
}

int
mock230_session_pollfd(const struct Mock230Session* session)
{
    if( !session->transport.pollfd )
        return -1;
    return session->transport.pollfd(session->transport.ctx);
}

int
mock230_session_send(
    struct Mock230Session* session,
    const uint8_t* data,
    int len)
{
    if( session->state == MOCK230_SESSION_DEAD || !session->transport.send )
        return -1;
    return session->transport.send(session->transport.ctx, data, len);
}

int
mock230_session_take_login(struct Mock230Session* session)
{
    int raised = session->login_raised;

    session->login_raised = 0;
    return raised;
}

/* ------------------------------------------------------------------ */
/* Buffer                                                             */
/* ------------------------------------------------------------------ */

/** Drop `count` consumed bytes off the front. */
static void
consume(
    struct Mock230Session* session,
    int count)
{
    if( count <= 0 )
        return;
    if( count >= session->in_len )
    {
        session->in_len = 0;
        return;
    }
    memmove(session->in, session->in + count, (size_t)(session->in_len - count));
    session->in_len -= count;
}

/*
 * Pull one read's worth. Returns 0 when the peer is gone.
 *
 * Exactly one recv, never a loop, and that is a correctness requirement rather
 * than a simplification: an accepted socket is blocking, so the caller's
 * select() is what guarantees the *first* read returns. A second read has no
 * such guarantee and would park the whole server until the client happened to
 * say something else.
 *
 * The cost is that a burst larger than the buffer spans several pumps, which
 * costs nothing — each pump consumes what it can and the next one continues.
 */
static int
fill(struct Mock230Session* session)
{
    int space = MOCK230_SESSION_IN_MAX - session->in_len;
    int got;

    if( space <= 0 )
    {
        /*
         * A full buffer that the reader could not consume from means the peer
         * sent a packet longer than MOCK230_SESSION_IN_MAX, which no revision
         * does. Dropping the session beats spinning on a read that can never
         * make progress.
         */
        fprintf(stderr, "mock230: inbound buffer full (%d bytes), dropping session\n",
                session->in_len);
        return 0;
    }

    got = session->transport.recv(session->transport.ctx, session->in + session->in_len, space);
    if( got < 0 )
        return 0;
    /* got == 0 is "nothing available yet" — not an error, and specifically not
     * end-of-stream: a WebSocket read can deliver less than one whole frame. */
    session->in_len += got;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Handshake                                                          */
/* ------------------------------------------------------------------ */

/**
 * Does this world speak the revision-239 login block?
 *
 * Read off the wire adapter rather than a second setting: "which bytes we
 * write" and "which bytes we expect" are one decision, and letting them be two
 * is how a server ends up parsing one revision and answering in another.
 */
static int
login_is_239(const struct Mock230Server* srv)
{
    const struct Mock230Wire* wire = (srv && srv->wire) ? srv->wire
                                                        : mock230_wire_default();
    return wire->revision >= 239;
}

/* ------------------------------------------------------------------ */
/* JS5                                                                 */
/* ------------------------------------------------------------------ */

/*
 * The cache the JS5 service serves, opened once for the process.
 *
 * Lazy and shared rather than per-session: it is a pair of read-only file
 * handles plus a computed master index, and a client opens JS5 and the game as
 * two separate connections, so per-session would rebuild the master index on
 * every reconnect.
 *
 * MOCK230_JS5_CACHE overrides the directory. It defaults to the same cache the
 * world boots from, because serving one revision's content while the world
 * reads another is a mismatch nothing downstream can detect.
 */
static struct MockJs5* g_js5;
static int g_js5_tried;

static struct MockJs5*
js5_cache(void)
{
    if( !g_js5_tried )
    {
        char const* dir = getenv("MOCK230_JS5_CACHE");
        if( !dir )
            dir = getenv("MOCK230_CACHE");
        if( !dir )
            dir = "cache.osrs239";
        g_js5_tried = 1;
        g_js5 = mock_js5_open(dir);
    }
    return g_js5;
}

/**
 * The JS5 handshake: p1 15, p4 revision, p4 seed x4 -> p1 status.
 *
 * `MOCK230_JS5_REV` is the revision to accept, because the revision a client
 * announces and the revision whose content a cache holds are two different
 * facts that drift apart between the day a cache is archived and the day the
 * game updates. Answering 6 (out of date) is what a client reports as
 * `error_game_js5connect_outofdate`.
 */
static int
step_js5_init(struct Mock230Session* session)
{
    uint8_t status;
    int client_rev;
    char const* want = getenv("MOCK230_JS5_REV");
    int accept_rev = want ? atoi(want) : 239;

    if( session->in_len < 1 + 20 )
        return 0; /* the whole handshake has not arrived */

    client_rev = (session->in[1] << 24) | (session->in[2] << 16) |
                 (session->in[3] << 8) | session->in[4];
    consume(session, 1 + 20);

    if( !js5_cache() )
    {
        fprintf(stderr, "mock230: JS5 requested but no cache could be opened\n");
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }
    if( client_rev != accept_rev )
    {
        fprintf(stderr, "mock230: JS5 client revision %d, serving %d -> out of date\n",
                client_rev, accept_rev);
        status = 6;
        mock230_session_send(session, &status, 1);
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }

    status = 0;
    if( mock230_session_send(session, &status, 1) < 0 )
    {
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }
    fprintf(stderr, "mock230: JS5 session opened at revision %d\n", client_rev);
    session->state = MOCK230_SESSION_JS5;
    return 1;
}

/**
 * Serve whatever whole JS5 requests are buffered.
 *
 * A request is four bytes: opcode, archive, group. Opcodes 2, 3 and 4 are
 * flow-control and XOR hints a server may ignore; 0 and 1 are prefetch and
 * urgent, which differ only in queue priority and not in the answer.
 */
static int
step_js5_serve(struct Mock230Session* session)
{
    struct MockJs5* js5 = js5_cache();
    int served = 0;

    while( session->in_len >= 4 )
    {
        int opcode = session->in[0];
        int archive = session->in[1];
        int group = (session->in[2] << 8) | session->in[3];
        int cap;
        int n;
        uint8_t* out;

        consume(session, 4);
        served = 1;

        if( opcode == 2 || opcode == 3 || opcode == 4 )
            continue;
        if( opcode != 0 && opcode != 1 )
        {
            fprintf(stderr, "mock230: unknown JS5 request opcode %d\n", opcode);
            session->state = MOCK230_SESSION_DEAD;
            return 1;
        }

        cap = mock_js5_max_response_bytes(js5) + 4096;
        out = malloc((size_t)cap);
        if( !out )
        {
            session->state = MOCK230_SESSION_DEAD;
            return 1;
        }
        n = mock_js5_build_response(js5, archive, group, out, cap);
        if( n > 0 && mock230_session_send(session, out, n) < 0 )
        {
            free(out);
            session->state = MOCK230_SESSION_DEAD;
            return 1;
        }
        /*
         * Log served groups too, not only missing ones.
         *
         * Logging failures alone reads as silence when everything is fine AND
         * when the client is asking for nothing at all — and those are very
         * different situations. A black screen with a healthy packet stream is
         * exactly the case where the difference matters: it says whether the
         * client is failing to load the map or failing to ask for it.
         */
        if( session->verbose )
        {
            if( n > 0 )
                fprintf(stderr, "mock230: JS5 %s %d/%d -> %d bytes\n",
                        opcode == 1 ? "urgent" : "prefetch", archive, group, n);
            else
                fprintf(stderr, "mock230: JS5 has no %d/%d\n", archive, group);
        }
        free(out);
    }
    return served;
}

/*
 * Step 1: INIT_GAME_CONNECTION. One byte in, nine out.
 *
 * Returns 1 when it advanced, 0 when it needs more bytes.
 */
static int
step_init(struct Mock230Session* session)
{
    uint8_t out[16];
    struct RSAreaBuf buf;

    if( session->in_len < 1 )
        return 0;
    /*
     * One socket, two services, chosen by this byte: 14 is a game login and 15
     * is a JS5 cache download. A vanilla OldSchool client always opens JS5
     * first and treats a refusal as "unable to connect to the update server",
     * so answering only 14 produces a client that never reaches its login
     * screen -- which is not a symptom that points anywhere near here.
     */
    if( session->in[0] == 15 )
        return step_js5_init(session);

    if( session->in[0] != 14 )
    {
        fprintf(stderr, "mock230: expected opcode 14 or 15, got %d\n", session->in[0]);
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }
    consume(session, 1);

    rsab_wrap(&buf, out, sizeof(out));
    rsab_p1(&buf, 0);
    rsab_p8(&buf, (int64_t)SESSION_ID);
    if( mock230_session_send(session, out, (int)rsab_len(&buf)) < 0 )
    {
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }
    session->state = MOCK230_SESSION_LOGIN;
    return 1;
}

/*
 * Step 2: GAMELOGIN, its RSA block, and arming both ciphers.
 *
 * Returns 1 when it advanced, 0 when it needs more bytes. Unlike the old
 * straight-line version this re-enters cleanly on a block split across reads —
 * nothing is consumed until the whole thing is present.
 */
static int
step_login(
    struct Mock230Session* session,
    struct Mock230Server* srv)
{
    struct RSAreaBuf in;
    struct rsa rsa;
    uint8_t plain[512];
    int payload_len;
    int rsa_size;
    int plain_len;
    int32_t seed[4];
    int32_t seed_in[4];
    uint64_t claimed;
    char user[64];
    char pass[64];
    int body_off = 0;
    int body_len = 0;
    uint8_t ok = 2;

    if( session->in_len < 3 )
        return 0;
    if( session->in[0] != 16 )
    {
        fprintf(stderr, "mock230: expected opcode 16, got %d\n", session->in[0]);
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }
    payload_len = (session->in[1] << 8) | session->in[2];
    if( payload_len < 13 || payload_len > MOCK230_SESSION_IN_MAX - 3 )
    {
        fprintf(stderr, "mock230: bad login block length (%d)\n", payload_len);
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }
    if( session->in_len < 3 + payload_len )
        return 0; /* the rest of the block is still in flight */

    /*
     * The header, whose SHAPE is per-revision.
     *
     * Revision 237 inserted `serverVersion` between subVersion and clientType.
     * Four bytes, and nothing downstream would notice: the three bytes after it
     * are all small ints, so a 230 reader on a 239 block picks up plausible
     * values for clientType/platformType/externalAuth and then reads the RSA
     * size two bytes out of position — which surfaces as "rsa decrypt failed",
     * a message that points at the key rather than at the offset.
     *
     * The layout for both is stated once in src/net/rev/osrs239/loginblock.h;
     * the client driver writes it from the same file.
     */
    rsab_wrap(&in, session->in + 3, (size_t)payload_len);
    (void)rsab_g4(&in); /* version */
    (void)rsab_g4(&in); /* subVersion */
    if( login_is_239(srv) )
        (void)rsab_g4(&in); /* serverVersion, new at revision 237 */
    (void)rsab_g1(&in); /* clientType */
    (void)rsab_g1(&in); /* platformType */
    (void)rsab_g1(&in); /* externalAuth */
    rsa_size = rsab_g2(&in);
    /*
     * Where the XTEA body starts, captured now because `in` is about to be
     * re-wrapped over the RSA plaintext and the offset would be gone.
     */
    body_off = 3 + (int)in.pos + rsa_size;
    body_len = payload_len - (int)in.pos - rsa_size;
    if( !rsab_ok(&in) )
    {
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }

    if( rsa_init(&rsa, MOCK_RSA_D, MOCK_RSA_N) != 0 )
    {
        fprintf(stderr, "mock230: rsa_init failed\n");
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }
    plain_len = rsa_crypt(&rsa, session->in + 3 + in.pos, (size_t)rsa_size, plain,
                          sizeof(plain));
    if( plain_len <= 0 )
    {
        fprintf(stderr, "mock230: rsa decrypt failed\n");
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }

    rsab_wrap(&in, plain, (size_t)plain_len);
    {
        int check = rsab_g1(&in);
        /*
         * The one place a wrong RSA key announces itself.
         *
         * Everything after this is read out of a buffer that decrypted to
         * noise, so without this check the failure appears as a garbage
         * username or a seed that makes every subsequent packet unreadable —
         * both of which look like protocol bugs rather than key mismatch.
         */
        if( check != 1 )
        {
            fprintf(stderr,
                    "mock230: RSA check byte is %d, expected 1 — the client's "
                    "modulus does not match this server's key\n",
                    check);
            session->state = MOCK230_SESSION_DEAD;
            return 1;
        }
    }
    for( int i = 0; i < 4; i++ )
        seed[i] = rsab_g4(&in);
    claimed = (uint64_t)rsab_g8(&in);
    if( login_is_239(srv) )
    {
        /*
         * The OTP discriminator, ahead of the auth type at this revision. Its
         * four payload bytes mean different things per kind (a trusted-computer
         * identifier, a 3-byte authenticator key plus a pad, or nothing) and
         * this server authenticates none of them — but they must be CONSUMED,
         * because the username and password follow immediately after.
         */
        int otp = rsab_g1(&in);
        (void)rsab_g4(&in);
        if( otp < 0 || otp > 3 )
        {
            fprintf(stderr, "mock230: unknown OTP kind %d in login block\n", otp);
            session->state = MOCK230_SESSION_DEAD;
            return 1;
        }
    }
    (void)rsab_g1(&in); /* auth type: 0 password, 2 token */
    if( login_is_239(srv) )
    {
        /*
         * At 239 the RSA block ends with the password; the USERNAME lives in
         * the XTEA body, which this server does not read (see below). The 230
         * block carries both here, in the other order.
         */
        rsab_gjstr(&in, pass, sizeof(pass), RSAB_JSTR_NUL);
        /*
         * The username is the first field of the XTEA body, keyed on the seeds
         * we just recovered. Decrypting in place is safe: the block is fully
         * buffered by the length check above and consumed immediately after.
         *
         * The rest of the body -- window state, uuid, host platform stats and
         * 23 archive CRCs -- is read by nothing here. That is a decision, not
         * an oversight: this server verifies no cache CRCs, so parsing them
         * would produce values with no consumer. The fields are documented in
         * loginblock.h for whenever one appears.
         */
        snprintf(user, sizeof(user), "%s", "Player");
        if( body_len > 0 && body_off + body_len <= session->in_len )
        {
            struct RSAreaBuf body;
            xteas_decrypt((char*)session->in + body_off, body_len, seed);
            rsab_wrap(&body, session->in + body_off, (size_t)body_len);
            rsab_gjstr(&body, user, sizeof(user), RSAB_JSTR_NUL);
            if( !rsab_ok(&body) || !user[0] )
                snprintf(user, sizeof(user), "%s", "Player");
        }
    }
    else
    {
        rsab_gjstr(&in, user, sizeof(user), RSAB_JSTR_NUL);
        rsab_gjstr(&in, pass, sizeof(pass), RSAB_JSTR_NUL);
    }
    (void)pass;

    /*
     * The session keeps the name; it does NOT write it onto the player here.
     * mock230_world_init memsets the whole player struct and runs after this,
     * so a name written now is a name silently erased — which is exactly what
     * used to happen, and why `displayname` in a script reported nothing. The
     * caller copies it across with mock230_world_set_display_name once the
     * world is up.
     */
    (void)srv;
    snprintf(session->display_name, sizeof(session->display_name), "%s",
             user[0] ? user : "Player");
    fprintf(stderr, "mock230: login user='%s' session=%s\n", user,
            claimed == SESSION_ID ? "ok" : "MISMATCH");

    consume(session, 3 + payload_len);

    if( login_is_239(srv) )
    {
        /*
         * LoginResponse.Ok — 34 bytes of body behind a var-byte length.
         *
         * A bare `0x02` is what this server used to send, and at 239 that is
         * not "a shorter response", it is a desync: the client reads a length
         * and then 34 bytes, so it swallows the first 35 bytes of the login
         * burst as though they were the response body. The burst that follows
         * then starts mid-packet. Nothing reports an error — the client simply
         * never sees the packets it ate, which reads as "the server didn't send
         * them".
         *
         * The length byte over-reports by 3. RSProt adds `Byte + Short` to it
         * for this response alone, and OldSchool has apparently always done so;
         * matching it matters more than explaining it.
         *
         * `index` is the slot the client will treat as itself, and it must be
         * the same number the GPI init block skipped and PLAYER_INFO keys on.
         */
        uint8_t body[64];
        struct RSAreaBuf out;
        int index = mock230_wire_local_index(
            session->player ? session->player->pid : 0);

        rsab_wrap(&out, body, sizeof(body));
        rsab_p1(&out, OSRS239_LOGINRES_OK);
        rsab_p1(&out, 34 + 3);
        rsab_p1(&out, 0); /* no authenticator */
        rsab_p4(&out, 0); /* ...and no code */
        rsab_p1(&out, 0); /* staffModLevel */
        rsab_p1(&out, 0); /* playerMod */
        rsab_p2(&out, index);
        rsab_p1(&out, 1); /* member */
        rsab_p8(&out, 0); /* accountHash */
        rsab_p8(&out, 0); /* userId */
        rsab_p8(&out, 0); /* userHash */
        if( mock230_session_send(session, body, (int)rsab_len(&out)) < 0 )
        {
            session->state = MOCK230_SESSION_DEAD;
            return 1;
        }
    }
    else if( mock230_session_send(session, &ok, 1) < 0 )
    {
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }

    /* The client seeds its out-cipher with the raw seed and its in-cipher with
     * seed + 50, so the server is the mirror image. */
    for( int i = 0; i < 4; i++ )
        seed_in[i] = seed[i] + 50;
    session->cipher_out = isaac_new(seed_in, 4);
    session->cipher_in = isaac_new((int32_t*)seed, 4);
    if( !session->cipher_out || !session->cipher_in )
    {
        fprintf(stderr, "mock230: isaac_new failed\n");
        session->state = MOCK230_SESSION_DEAD;
        return 1;
    }

    session->state = MOCK230_SESSION_ONLINE;
    session->login_raised = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Game stream                                                        */
/* ------------------------------------------------------------------ */

/*
 * Decode whole packets and hand them to the world.
 *
 * Two rules carry the whole function.
 *
 * **Never guess.** An unknown opcode is framed as zero-length, which
 * resynchronises on the next byte instead of consuming a made-up payload and
 * desyncing the ISAAC stream for good.
 *
 * **The opcode byte is spent when it is read.** ISAAC is a keystream cipher and
 * cannot be rewound, so there is no way to look at an opcode and put it back.
 * It is therefore consumed immediately and parked in `pending_opcode` until the
 * body arrives, which is what lets a packet torn across two reads survive. The
 * length prefix is *not* scrambled, so re-reading that costs nothing.
 */
static int
step_online(
    struct Mock230Session* session,
    struct Mock230Server* srv)
{
    int progressed = 0;
    /* Which revision's client-prot table frames what arrives. See
     * `packetout_size` in mock230_wire.h for why this is not the 230 one. */
    const struct Mock230Wire* wire =
        (srv && srv->wire) ? srv->wire : mock230_wire_default();

    for( ;; )
    {
        int size;
        int len_bytes;
        int payload_len;
        int name;

        if( session->pending_opcode < 0 )
        {
            if( session->in_len < 1 )
                break;
            session->pending_opcode =
                (session->in[0] - isaac_next(session->cipher_in)) & 0xff;
            consume(session, 1);
        }

        size = wire->packetout_size(session->pending_opcode);
        if( size == PKTOUT_LENGTH_VARU8 )
            len_bytes = 1;
        else if( size == PKTOUT_LENGTH_VARU16 )
            len_bytes = 2;
        else
            len_bytes = 0;

        if( session->in_len < len_bytes )
            break; /* length prefix still in flight; opcode stays parked */

        if( len_bytes == 1 )
            payload_len = session->in[0];
        else if( len_bytes == 2 )
            payload_len = (session->in[0] << 8) | session->in[1];
        else
            payload_len = size;

        if( session->in_len < len_bytes + payload_len )
            break; /* body still in flight; nothing consumed */

        name = wire->packetout_name(session->pending_opcode);

        /*
         * MOCK230_TRACE_IN=1 -- the client->server half, the mirror of
         * MOCK230_TRACE_OUT. Prints the revision's own name for the opcode and
         * whether anything routed it, because the two failures look identical
         * from the game's side: a click that produces nothing because the
         * opcode is unmapped, and one that produces nothing because the body
         * was translated into the wrong numbers.
         */
        {
            static int trace = -1;

            if( trace < 0 )
            {
                char const* v = getenv("MOCK230_TRACE_IN");
                trace = (v && *v && *v != '0') ? 1 : 0;
            }
            if( trace )
            {
                char const* prot = wire->packetout_prot_name
                                       ? wire->packetout_prot_name(session->pending_opcode)
                                       : NULL;

                fprintf(stderr, "mock230: <- op %3d %-24s %d byte(s)%s\n",
                        session->pending_opcode, prot ? prot : "?", payload_len,
                        name == PKTOUT_NAME_NONE ? "  [no canonical name]" : "");
            }
        }

        /*
         * The body may not be the one the handlers read. `translate_in`
         * rewrites it and can RENAME it -- at 239 one opcode covers what 230
         * splits into twenty -- so the name is re-read from its return value
         * rather than kept from the table. See mock230_wire.h.
         */
        if( wire->translate_in )
        {
            uint8_t xlat[MOCK230_SESSION_IN_MAX];
            int xlat_len = 0;
            int translated = wire->translate_in(session->pending_opcode, name,
                                                session->in + len_bytes, payload_len, xlat,
                                                (int)sizeof(xlat), &xlat_len);

            if( translated == PKTOUT_NAME_NONE )
            {
                if( session->verbose )
                    fprintf(stderr, "mock230: <- op %d (%d bytes) not translated, dropped\n",
                            session->pending_opcode, payload_len);
            }
            else
            {
                (void)srv;
                mock230_world_handle(session->player, translated, xlat, xlat_len);
            }
        }
        else if( name == PKTOUT_NAME_NONE )
        {
            if( session->verbose )
                fprintf(stderr, "mock230: <- unknown op %d (%d bytes)\n",
                        session->pending_opcode, payload_len);
        }
        else
        {
            /* Addressed to whoever this connection is, not to "the world's
             * player": with two sessions the second one's clicks used to move
             * the first one's character. */
            (void)srv;
            mock230_world_handle(session->player, name, session->in + len_bytes, payload_len);
        }

        consume(session, len_bytes + payload_len);
        session->pending_opcode = -1;
        progressed = 1;
    }

    return progressed;
}

/* ------------------------------------------------------------------ */
/* Pump                                                               */
/* ------------------------------------------------------------------ */

int
mock230_session_pump(
    struct Mock230Session* session,
    struct Mock230Server* srv)
{
    if( session->state == MOCK230_SESSION_DEAD )
        return 0;

    if( !fill(session) )
    {
        session->state = MOCK230_SESSION_DEAD;
        return 0;
    }

    /*
     * Loop rather than a single step: one fill can carry the whole handshake
     * plus the first game packets, which an embedded host routinely does
     * because it writes everything it has before yielding. Each step returns 0
     * when it wants more bytes, which is the only way out.
     */
    for( ;; )
    {
        int advanced = 0;

        switch( session->state )
        {
        case MOCK230_SESSION_INIT:
            advanced = step_init(session);
            break;
        case MOCK230_SESSION_LOGIN:
            advanced = step_login(session, srv);
            break;
        case MOCK230_SESSION_JS5:
            advanced = step_js5_serve(session);
            break;
        case MOCK230_SESSION_ONLINE:
            advanced = step_online(session, srv);
            break;
        case MOCK230_SESSION_DEAD:
            return 0;
        }

        if( !advanced )
            break;
        /* The login burst has to run before any packet behind it is decoded,
         * so hand control back the moment the stream arms. */
        if( session->login_raised )
            break;
    }

    return session->state != MOCK230_SESSION_DEAD;
}
