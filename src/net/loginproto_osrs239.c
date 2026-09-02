#include "loginproto_osrs239.h"

#include "loginproto.h" /* LOGINPROTO_SUCCESS / _ERROR / _AWAIT_* + seed_fn */
#include "net.h"
#include "pow_sha256.h"
#include "rev/osrs239/loginblock.h"

#include <assert.h>
#include <rsbuffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xteas.h>
#include "log/torirs_log.h"

/*
 * The handshake, as a state machine, because two of its steps can be reordered
 * by the server: after GAMELOGIN the next byte is either a verdict or a
 * proof-of-work challenge, and the challenge can arrive more than once.
 *
 *   SEND_CONNECT  -> op 14
 *   AWAIT_SEED    <- p1 status, p8 sessionId
 *   SEND_LOGIN    -> op 16 with the block from loginblock.h
 *   AWAIT_REPLY   <- one of:
 *                      69 PROOF_OF_WORK  -> solve, op 19, stay in AWAIT_REPLY
 *                       2 OK             -> read the fixed body, done
 *                      <anything else>   -> a numbered refusal
 */
enum osrs239_login_state
{
    OSRS239_SEND_CONNECT = 0,
    OSRS239_AWAIT_SEED,
    OSRS239_SEND_LOGIN,
    OSRS239_AWAIT_REPLY,
    OSRS239_DONE,
    OSRS239_ERR,
};

/* Bound on the proof-of-work search. Live OldSchool asks for 18 bits, i.e.
 * ~260k hashes on average; ten million is roughly 5 bits of headroom and still
 * finishes in seconds, so a difficulty nobody planned for fails loudly instead
 * of wedging the connection. */
#define OSRS239_POW_MAX_ATTEMPTS 10000000ull

/* Payload of LoginResponse.Ok, in bytes:
 *   p1 authenticator kind, p4 authenticator code
 *   p1 staffModLevel, p1 playerMod, p2 index, p1 member
 *   p8 accountHash, p8 userId, p8 userHash                                  */
#define OSRS239_OK_BODY_BYTES 34

struct Osrs239Login
{
    struct ToriRS_Network* net;
    char username[64];
    char password[64];
    enum osrs239_login_state state;

    /* The server's rejection byte, kept so the login screen can say which
     * refusal this was. -1 until one arrives. */
    int reply_code;

    int32_t seed[4];
    uint64_t session_id;

    /* The block is bigger than 230's — host platform stats and 23 CRCs — so
     * the staging buffer is sized for the whole thing rather than for a name
     * and a password. */
    uint8_t out[4096];
    int out_len;
    int out_off;

    /* The local player's slot, from LoginResponse.Ok. There is no UPDATE_PID
     * at this revision, so this is the only statement of it on the wire, and
     * PLAYER_INFO v5 is keyed on it. -1 until the response arrives. */
    int local_index;

    /* Inbound accumulator. The shapes it has to hold are the 9-byte seed
     * reply, a var-short proof-of-work challenge, and RECONNECT_OK's
     * var-short GPI init block — which is the largest of the three by far
     * (2047 low-resolution positions at 18 bits each). */
    uint8_t in[8192];
    int in_len;

    /* This handshake announces itself as GAMERECONNECT, presenting
     * `prev_seed` in place of the password. Both are copied off the network
     * struct at construction so the block builder reads one thing. */
    int reconnect;
    int32_t prev_seed[4];

    /* RECONNECT_OK's payload, held for the caller (see
     * NetLoginVTable.reconnect_block). Offset into `in`, since nothing else
     * uses the accumulator once the response is complete. */
    int reconnect_block_off;
    int reconnect_block_len;
};

static void
seed_isaac(struct Osrs239Login* h)
{
    /* out (client->server) = seed; in (server->client) = seed + 50. */
    isaac_seed(h->net->random_out, (int*)h->seed, 4);
    int32_t sin[4];
    for( int i = 0; i < 4; i++ )
        sin[i] = h->seed[i] + 50;
    isaac_seed(h->net->random_in, (int*)sin, 4);
}

/* --- the four CRC byte orders from loginblock.h ------------------------- */

static void
p4_order(struct RSCache_Buffer* b, int32_t v, int order)
{
    uint8_t const b3 = (uint8_t)(v >> 24), b2 = (uint8_t)(v >> 16);
    uint8_t const b1 = (uint8_t)(v >> 8), b0 = (uint8_t)v;
    switch( order )
    {
    case OSRS239_CRC_BE:
        p1(b, b3); p1(b, b2); p1(b, b1); p1(b, b0);
        break;
    case OSRS239_CRC_LE:
        p1(b, b0); p1(b, b1); p1(b, b2); p1(b, b3);
        break;
    case OSRS239_CRC_ALT2:
        p1(b, b1); p1(b, b0); p1(b, b3); p1(b, b2);
        break;
    default: /* OSRS239_CRC_ALT3 */
        p1(b, b2); p1(b, b3); p1(b, b0); p1(b, b1);
        break;
    }
}

/*
 * Host platform stats.
 *
 * A real client reports its OS, JVM, GPU and CPU here and a real server files
 * it for analytics; nothing in the protocol depends on the values. What DOES
 * depend on them is the length — the decoder reads every field in order, so a
 * field skipped here shifts the reader onto the client type, the reflection
 * constant and the whole CRC block. That is why this writes all of them,
 * zeroed, rather than "the ones that matter".
 *
 * `gjstr2` fields are version-prefixed strings: a leading 0 byte, then the
 * NUL-terminated text.
 */
static void
write_host_platform_stats(struct RSCache_Buffer* b)
{
    p1(b, 9);  /* version */
    p1(b, 3);  /* osType: 3 = mac */
    p1(b, 1);  /* os64Bit */
    p2(b, 0);  /* osVersion */
    p1(b, 0);  /* javaVendor */
    p1(b, 17); /* javaVersionMajor */
    p1(b, 0);  /* javaVersionMinor */
    p1(b, 0);  /* javaVersionPatch */
    p1(b, 1);  /* applet == 0 means "is an applet"; 1 = standalone */
    p2(b, 512); /* javaMaxMemoryMb */
    p1(b, 8);  /* javaAvailableProcessors */
    p1(b, 0);  /* systemMemory (p3) */
    p2(b, 0);
    p2(b, 0); /* systemSpeed */
    for( int i = 0; i < 4; i++ )
    {
        /* gpuDxName, gpuGlName, gpuDxVersion, gpuGlVersion */
        p1(b, 0);
        p1(b, 0);
    }
    p1(b, 0); /* gpuDriverMonth */
    p2(b, 0); /* gpuDriverYear */
    p1(b, 0); p1(b, 0); /* cpuManufacturer */
    p1(b, 0); p1(b, 0); /* cpuBrand */
    p1(b, 1); /* cpuCount1 */
    p1(b, 1); /* cpuCount2 */
    p4(b, 0); p4(b, 0); p4(b, 0); /* cpuFeatures */
    p4(b, 0);                     /* cpuSignature */
    p1(b, 0); p1(b, 0);           /* clientName */
    p1(b, 0); p1(b, 0);           /* deviceName */
}

static int
build_login_block(struct Osrs239Login* h)
{
    uint8_t rsabuf[512];
    uint8_t enc[512];
    uint8_t body[2048];
    struct RSCache_Buffer rbuf;
    struct RSCache_Buffer bbuf;
    struct RSCache_Buffer obuf;

    /* --- RSA plaintext ------------------------------------------------- */
    RSCache_BufferInit(&rbuf, rsabuf, sizeof(rsabuf));
    p1(&rbuf, 1); /* encryptionCheck */
    for( int i = 0; i < 4; i++ )
        p4(&rbuf, h->seed[i]);
    p8(&rbuf, (int64_t)h->session_id);
    if( h->reconnect )
    {
        /*
         * GAMERECONNECT's authentication section: the seed the previous
         * session's ciphers ran on, and nothing else.
         *
         * RSProt splits the two decoders exactly here (GameReconnectDecoder's
         * decodeAuthentication reads four ints where GameLoginDecoder reads
         * the OTP discriminator, the auth type and the password) — everything
         * around it, header and XTEA body alike, is byte-identical between the
         * two. Presenting the old key is what lets a server hand the character
         * back without a password round trip.
         */
        for( int i = 0; i < 4; i++ )
            p4(&rbuf, h->prev_seed[i]);
    }
    else
    {
        /* OTP_NONE, whose four payload bytes the decoder skips without reading. */
        p1(&rbuf, OSRS239_OTP_NONE);
        p4(&rbuf, 0);
        p1(&rbuf, OSRS239_AUTH_PASSWORD);
        pjstr(&rbuf, h->password, 0);
    }

    int enclen = rsa_crypt(&h->net->rsa, rsabuf, rbuf.position, enc, sizeof(enc));
    if( enclen <= 0 )
    {
        TORIRS_ERR("osrs239 login: rsa encrypt failed\n");
        return 0;
    }

    /* --- XTEA body ----------------------------------------------------- */
    RSCache_BufferInit(&bbuf, body, sizeof(body));
    pjstr(&bbuf, h->username, 0);
    p1(&bbuf, 0x2);  /* packedClientSettings: resizable, full detail */
    p2(&bbuf, 765);  /* width  */
    p2(&bbuf, 503);  /* height */
    for( int i = 0; i < OSRS239_LOGIN_UUID_BYTES; i++ )
        p1(&bbuf, 0);
    pjstr(&bbuf, "", 0); /* siteSettings */
    p4(&bbuf, 0);        /* affiliate */
    p1(&bbuf, 0);        /* deepLinkCount */
    write_host_platform_stats(&bbuf);
    p1(&bbuf, 0); /* secondClientType */
    p4(&bbuf, 0); /* reflectionCheckerConst */
    for( int i = 0; i < OSRS239_LOGIN_CRC_COUNT; i++ )
    {
        struct Osrs239CrcSlot slot = g_osrs239_crc_order[i];
        int32_t v = slot.index < 9 ? h->net->rev->jag_checksum[slot.index] : 0;
        p4_order(&bbuf, v, slot.order);
    }

    /* XTEA leaves any tail shorter than 8 bytes in the clear, and so does the
     * server's decrypt, so an odd length round-trips. */
    xteas_encrypt((char*)body, (int)bbuf.position, h->seed);

    /* --- outer packet -------------------------------------------------- */
    RSCache_BufferInit(&obuf, h->out, sizeof(h->out));
    p1(&obuf, h->reconnect ? OSRS239_LOGIN_GAMERECONNECT : OSRS239_LOGIN_GAMELOGIN);
    int header_len = 4 + 4 + 4 + 1 + 1 + 1;
    p2(&obuf, header_len + 2 + enclen + (int)bbuf.position);
    p4(&obuf, h->net->rev->client_version);
    p4(&obuf, 0); /* subVersion */
    p4(&obuf, 0); /* serverVersion */
    p1(&obuf, h->net->client_type);   /* clientType (LoginClientType) */
    p1(&obuf, h->net->platform_type); /* platformType (LoginPlatformType) */
    p1(&obuf, 0); /* externalAuthType */
    p2(&obuf, enclen);
    pbuf(&obuf, enc, enclen);
    pbuf(&obuf, body, bbuf.position);

    h->out_len = (int)obuf.position;
    h->out_off = 0;

    seed_isaac(h);
    return 1;
}

/*
 * Answer a proof-of-work challenge sitting whole in h->in.
 *
 * Returns 1 when the reply is staged, 0 when the challenge could not be met —
 * an unknown challenge type, or a difficulty past the attempt bound.
 */
static int
answer_proof_of_work(struct Osrs239Login* h, uint8_t const* payload, int len)
{
    struct RSCache_Buffer b = { .data = (uint8_t*)payload, .size = (uint32_t)len,
                                .position = 0 };
    int type = g1(&b);
    if( type != 0 )
    {
        TORIRS_ERR("osrs239 login: unknown proof-of-work type %d\n", type);
        return 0;
    }
    int version = g1(&b);
    int difficulty = g1(&b);
    char salt[256];
    int n = 0;
    while( b.position < b.size && n < (int)sizeof(salt) - 1 )
    {
        int c = g1(&b);
        if( c == 0 )
            break;
        salt[n++] = (char)c;
    }
    salt[n] = '\0';

    uint64_t result = 0;
    if( !pow_sha256_solve(version, difficulty, salt, OSRS239_POW_MAX_ATTEMPTS, &result) )
    {
        TORIRS_LOG("osrs239 login: proof of work unsolved (v=%d difficulty=%d) after %llu "
                "attempts\n",
                version, difficulty, (unsigned long long)OSRS239_POW_MAX_ATTEMPTS);
        return 0;
    }

    struct RSCache_Buffer o;
    RSCache_BufferInit(&o, h->out, sizeof(h->out));
    p1(&o, OSRS239_LOGIN_POW_REPLY);
    p2(&o, 8);
    p8(&o, (int64_t)result);
    h->out_len = (int)o.position;
    h->out_off = 0;
    TORIRS_LOG("osrs239 login: proof of work solved (difficulty %d) -> %llu\n",
            difficulty, (unsigned long long)result);
    return 1;
}

/* --- NetLoginVTable impl ----------------------------------------------- */

static void*
osrs239_new(struct ToriRS_Network* net, char const* username, char const* password)
{
    struct Osrs239Login* h = calloc(1, sizeof(*h));
    assert(h);
    h->net = net;
    h->reply_code = -1;
    snprintf(h->username, sizeof(h->username), "%s", username ? username : "");
    snprintf(h->password, sizeof(h->password), "%s", password ? password : "");
    h->state = OSRS239_SEND_CONNECT;
    h->local_index = -1;
    /*
     * A reconnect keeps the index it already had: RECONNECT_OK carries no
     * index field (the server is handing back a session, not opening one), so
     * losing it here would leave PLAYER_INFO v5 keyed on -1.
     */
    h->reconnect = net->reconnect && net->has_prev_seed &&
                   net->rev->reconnect_kind == NET_RECONNECT_SEED;
    if( h->reconnect )
    {
        memcpy(h->prev_seed, net->prev_seed, sizeof(h->prev_seed));
        h->local_index = net->local_index;
    }
    else if( net->reconnect )
    {
        /* Asked to reconnect with nothing to present: either no prior session,
         * so no seed to authenticate with, or a table that does not claim the
         * seed flavour. Fall back to a fresh GAMELOGIN rather than sending a
         * block the server cannot authenticate. */
        TORIRS_LOG("osrs239 login: %s; reconnecting as a fresh login\n",
                net->has_prev_seed ? "revision does not use the seed reconnect"
                                   : "no previous seed");
    }
    if( net->seed_fn )
        net->seed_fn(net->seed_user, h->seed);
    else
    {
        h->seed[0] = rand();
        h->seed[1] = rand();
    }
    h->seed[2] = rand();
    h->seed[3] = rand();
    return h;
}

/*
 * Hand what a future reconnect will need back to the network struct.
 *
 * The seed, because GAMERECONNECT presents the previous session's key in
 * place of a password, and the index, because RECONNECT_OK does not restate
 * it. Both are published on success only: a refused handshake has no session
 * worth reclaiming, and overwriting a good seed with its seed would make the
 * next reconnect unauthenticable.
 */
static void
publish_seed(struct Osrs239Login* h)
{
    memcpy(h->net->prev_seed, h->seed, sizeof(h->net->prev_seed));
    h->net->has_prev_seed = 1;
    h->net->local_index = h->local_index;
}

static int
osrs239_recv(void* handle, uint8_t const* data, int size)
{
    struct Osrs239Login* h = handle;
    int off = 0;

    while( off < size )
    {
        if( h->state == OSRS239_AWAIT_SEED )
        {
            while( h->in_len < 9 && off < size )
                h->in[h->in_len++] = data[off++];
            if( h->in_len < 9 )
                break;
            struct RSCache_Buffer rbuf = { .data = h->in, .size = 9, .position = 0 };
            int status = g1(&rbuf);
            uint64_t sid = (uint64_t)g8(&rbuf);
            if( status != 0 )
            {
                TORIRS_ERR("osrs239 login: connect rejected status=%d\n", status);
                h->state = OSRS239_ERR;
                return off;
            }
            h->session_id = sid;
            h->in_len = 0;
            h->state = OSRS239_SEND_LOGIN;
            break; /* poll builds the block */
        }
        else if( h->state == OSRS239_AWAIT_REPLY )
        {
            /* Accumulate; the two shapes are a bare verdict byte and a
             * var-short-framed challenge, and which one it is is the first
             * byte. Nothing is consumed until a whole message is present. */
            while( h->in_len < (int)sizeof(h->in) && off < size )
                h->in[h->in_len++] = data[off++];

            int reply = h->in[0];
            if( reply == OSRS239_LOGINRES_PROOF_OF_WORK )
            {
                if( h->in_len < 3 )
                    break;
                int len = (h->in[1] << 8) | h->in[2];
                if( h->in_len < 3 + len )
                    break;
                if( !answer_proof_of_work(h, h->in + 3, len) )
                {
                    h->state = OSRS239_ERR;
                    return off;
                }
                /* Shift out the challenge; stay in AWAIT_REPLY for the verdict
                 * (or, in principle, for another challenge). */
                memmove(h->in, h->in + 3 + len, (size_t)(h->in_len - (3 + len)));
                h->in_len -= 3 + len;
                continue;
            }
            if( reply == OSRS239_LOGINRES_OK )
            {
                /*
                 * The length byte over-reports by 3: RSProt adds the size of a
                 * byte and a short to it, and OldSchool has apparently always
                 * done so. Reading the body by its own fixed size rather than
                 * by the stated length is what keeps that harmless.
                 *
                 * On an embed pipe the login burst is already sitting behind
                 * this response in the same read. Those bytes must NOT be
                 * counted as consumed here: login_free drops h->in, and the
                 * server's ISAAC has already stepped through them. Returning
                 * only the OK size leaves the burst for gameproto with both
                 * ciphers at step 0.
                 */
                int const ok_size = 2 + OSRS239_OK_BODY_BYTES;
                if( h->in_len < ok_size )
                    break;
                struct RSCache_Buffer rbuf = { .data = h->in + 2,
                                               .size = OSRS239_OK_BODY_BYTES,
                                               .position = 0 };
                (void)g1(&rbuf); /* authenticator kind */
                (void)g4(&rbuf); /* authenticator code */
                (void)g1(&rbuf); /* staffModLevel */
                (void)g1(&rbuf); /* playerMod */
                int index = g2(&rbuf);
                h->local_index = index;
                TORIRS_LOG("osrs239 login: OK, local player index %d\n", index);
                h->state = OSRS239_DONE;
                publish_seed(h);
                {
                    int extra = h->in_len - ok_size;
                    h->in_len = 0;
                    return off - extra;
                }
            }
            if( reply == OSRS239_LOGINRES_RECONNECT_OK )
            {
                /*
                 * The session handed back. Var-short framed, and unlike OK the
                 * stated length is the real one: it has to be, because the
                 * payload is a variable-length bit block.
                 *
                 * No index, no staff level, no account fields — a reconnect
                 * restates none of it, which is why `local_index` was carried
                 * over at construction. What it does carry is the player-info
                 * init block, held here and applied by the caller once the
                 * index has been restated (see reconnect_block).
                 */
                int len;

                if( h->in_len < 3 )
                    break;
                len = (h->in[1] << 8) | h->in[2];
                if( len < 0 || 3 + len > (int)sizeof(h->in) )
                {
                    TORIRS_ERR("osrs239 login: RECONNECT_OK payload too large (%d)\n", len);
                    h->state = OSRS239_ERR;
                    return off;
                }
                if( h->in_len < 3 + len )
                    break;
                h->reconnect_block_off = 3;
                h->reconnect_block_len = len;
                TORIRS_LOG("osrs239 login: RECONNECT_OK, %d byte player-info init, index %d\n",
                        len, h->local_index);
                h->state = OSRS239_DONE;
                publish_seed(h);
                {
                    /* Same rule as OK: bytes of the burst that arrived behind
                     * the response are not ours to consume. `in` is kept (the
                     * block lives in it) and freed with the handle. */
                    int extra = h->in_len - (3 + len);
                    h->in_len = 3 + len;
                    return off - extra;
                }
            }
            TORIRS_ERR("osrs239 login: rejected reply=%d\n", reply);
            h->reply_code = reply;
            h->state = OSRS239_ERR;
            break;
        }
        else
        {
            break; /* nothing to consume in the other states */
        }
    }
    return off;
}

static int
osrs239_send(void* handle, uint8_t* out, int out_size)
{
    struct Osrs239Login* h = handle;
    int avail = h->out_len - h->out_off;
    if( avail <= 0 )
        return 0;
    if( avail > out_size )
        avail = out_size;
    memcpy(out, h->out + h->out_off, (size_t)avail);
    h->out_off += avail;
    if( h->out_off >= h->out_len )
        h->out_len = h->out_off = 0;
    return avail;
}

static int
osrs239_poll(void* handle)
{
    struct Osrs239Login* h = handle;
    switch( h->state )
    {
    case OSRS239_SEND_CONNECT:
        h->out[0] = OSRS239_LOGIN_INIT_GAME_CONNECTION;
        h->out_len = 1;
        h->out_off = 0;
        h->state = OSRS239_AWAIT_SEED;
        return LOGINPROTO_AWAIT_RECV;
    case OSRS239_AWAIT_SEED:
        return LOGINPROTO_AWAIT_RECV;
    case OSRS239_SEND_LOGIN:
        if( !build_login_block(h) )
        {
            h->state = OSRS239_ERR;
            return LOGINPROTO_ERROR;
        }
        h->state = OSRS239_AWAIT_REPLY;
        return LOGINPROTO_AWAIT_RECV;
    case OSRS239_AWAIT_REPLY:
        return LOGINPROTO_AWAIT_RECV;
    case OSRS239_DONE:
        return LOGINPROTO_SUCCESS;
    case OSRS239_ERR:
    default:
        return LOGINPROTO_ERROR;
    }
}

static void
osrs239_free(void* handle)
{
    free(handle);
}

static int
osrs239_local_index(void* handle)
{
    struct Osrs239Login* h = handle;
    return h ? h->local_index : -1;
}

static uint8_t const*
osrs239_reconnect_block(void* handle, int* out_len)
{
    struct Osrs239Login* h = handle;

    if( !h || h->reconnect_block_len <= 0 )
        return NULL;
    if( out_len )
        *out_len = h->reconnect_block_len;
    return h->in + h->reconnect_block_off;
}

static int
osrs239_reply_code(void* handle)
{
    struct Osrs239Login* h = handle;
    assert(h);
    return h->reply_code;
}

struct NetLoginVTable const g_osrs239_login_vtable = {
    .new_ = osrs239_new,
    .local_index = osrs239_local_index,
    .reconnect_block = osrs239_reconnect_block,
    .recv = osrs239_recv,
    .send = osrs239_send,
    .poll = osrs239_poll,
    .reply_code = osrs239_reply_code,
    .free_ = osrs239_free,
};
