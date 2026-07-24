#include "loginproto_osrs230.h"

#include "loginproto.h" /* LOGINPROTO_SUCCESS / _ERROR / _AWAIT_* + seed_fn */
#include "net.h"

#include <assert.h>
#include <rsbuffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum osrs230_login_state
{
    OSRS230_SEND_CONNECT = 0, /* stage op 14 */
    OSRS230_AWAIT_SEED,       /* waiting for [status][sessionId] */
    OSRS230_SEND_CREDS,       /* build + stage op 16 */
    OSRS230_AWAIT_RESPONSE,   /* waiting for [0x02] */
    OSRS230_DONE,
    OSRS230_ERR,
};

struct Osrs230Login
{
    struct ToriRS_Network* net;
    char username[64];
    char password[64];
    enum osrs230_login_state state;

    int32_t seed[4];
    uint64_t session_id;

    uint8_t out[1024];
    int out_len;
    int out_off;

    uint8_t in[32];
    int in_len;
};

static void
seed_isaac(struct Osrs230Login* h)
{
    /* out (client->server) = seed; in (server->client) = seed+50. */
    isaac_seed(h->net->random_out, (int*)h->seed, 4);
    int32_t sin[4];
    for( int i = 0; i < 4; i++ )
        sin[i] = h->seed[i] + 50;
    isaac_seed(h->net->random_in, (int*)sin, 4);
}

static void
build_login_block(struct Osrs230Login* h)
{
    /* RSA plaintext block. */
    uint8_t rsabuf[512];
    struct RSCache_Buffer rbuf;
    RSCache_BufferInit(&rbuf, rsabuf, sizeof(rsabuf));
    p1(&rbuf, 1); /* encryptionCheck */
    p4(&rbuf, h->seed[0]);
    p4(&rbuf, h->seed[1]);
    p4(&rbuf, h->seed[2]);
    p4(&rbuf, h->seed[3]);
    p4(&rbuf, (int32_t)(h->session_id >> 32));
    p4(&rbuf, (int32_t)(h->session_id & 0xffffffff));
    p1(&rbuf, 0); /* authType = 0 (password) */
    pjstr(&rbuf, h->username, 0);
    pjstr(&rbuf, h->password, 0);

    uint8_t enc[512];
    int enclen = rsa_crypt(&h->net->rsa, rsabuf, rbuf.position, enc, sizeof(enc));
    assert(enclen > 0);

    /* Outer GAMELOGIN packet (op 16, var-short length). */
    struct RSCache_Buffer obuf;
    RSCache_BufferInit(&obuf, h->out, sizeof(h->out));
    p1(&obuf, 16);
    int payload_len = 11 /* header */ + 2 /* rsaSize */ + enclen;
    p2(&obuf, payload_len);
    /* header */
    p4(&obuf, h->net->rev->client_version); /* 230 */
    p4(&obuf, 0);                           /* subVersion */
    p1(&obuf, 0);                           /* clientType */
    p1(&obuf, 0);                           /* platformType */
    p1(&obuf, 0);                           /* externalAuth */
    /* rsa block */
    p2(&obuf, enclen);
    pbuf(&obuf, enc, enclen);

    h->out_len = obuf.position;
    h->out_off = 0;

    seed_isaac(h);
}

/* --- NetLoginVTable impl --- */

static void*
osrs230_new(struct ToriRS_Network* net, char const* username, char const* password)
{
    struct Osrs230Login* h = calloc(1, sizeof(*h));
    assert(h);
    h->net = net;
    snprintf(h->username, sizeof(h->username), "%s", username ? username : "");
    snprintf(h->password, sizeof(h->password), "%s", password ? password : "");
    h->state = OSRS230_SEND_CONNECT;
    /* Client seed: seed_fn fills [0],[1]; we fill [2],[3] too (230 uses 4
     * client-chosen ints, unlike 2004's 2-from-server). */
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

static int
osrs230_recv(void* handle, uint8_t const* data, int size)
{
    struct Osrs230Login* h = handle;
    int off = 0;

    while( off < size )
    {
        if( h->state == OSRS230_AWAIT_SEED )
        {
            /* Accumulate 9 bytes: status(1) + sessionId(8). */
            while( h->in_len < 9 && off < size )
                h->in[h->in_len++] = data[off++];
            if( h->in_len < 9 )
                break;
            struct RSCache_Buffer rbuf = { .data = h->in, .size = 9, .position = 0 };
            int status = g1(&rbuf);
            uint64_t sid = (uint64_t)g8(&rbuf);
            if( status != 0 )
            {
                fprintf(stderr, "osrs230 login: connect rejected status=%d\n", status);
                h->state = OSRS230_ERR;
                return off;
            }
            h->session_id = sid;
            h->in_len = 0;
            h->state = OSRS230_SEND_CREDS;
            break; /* poll stages creds */
        }
        else if( h->state == OSRS230_AWAIT_RESPONSE )
        {
            int reply = data[off++];
            if( reply == 2 )
                h->state = OSRS230_DONE;
            else
            {
                fprintf(stderr, "osrs230 login: rejected reply=%d\n", reply);
                h->state = OSRS230_ERR;
            }
            break;
        }
        else
        {
            break; /* nothing to consume in other states */
        }
    }
    return off;
}

static int
osrs230_send(void* handle, uint8_t* out, int out_size)
{
    struct Osrs230Login* h = handle;
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
osrs230_poll(void* handle)
{
    struct Osrs230Login* h = handle;
    switch( h->state )
    {
    case OSRS230_SEND_CONNECT:
        h->out[0] = 14; /* INIT_GAME_CONNECTION, no payload */
        h->out_len = 1;
        h->out_off = 0;
        h->state = OSRS230_AWAIT_SEED;
        return LOGINPROTO_AWAIT_RECV;
    case OSRS230_AWAIT_SEED:
        return LOGINPROTO_AWAIT_RECV;
    case OSRS230_SEND_CREDS:
        build_login_block(h);
        h->state = OSRS230_AWAIT_RESPONSE;
        return LOGINPROTO_AWAIT_RECV;
    case OSRS230_AWAIT_RESPONSE:
        return LOGINPROTO_AWAIT_RECV;
    case OSRS230_DONE:
        return LOGINPROTO_SUCCESS;
    case OSRS230_ERR:
    default:
        return LOGINPROTO_ERROR;
    }
}

static void
osrs230_free(void* handle)
{
    free(handle);
}

struct NetLoginVTable const g_osrs230_login_vtable = {
    .new_ = osrs230_new,
    .recv = osrs230_recv,
    .send = osrs230_send,
    .poll = osrs230_poll,
    .free_ = osrs230_free,
};
