#include "loginproto.h"

#include "jbase37.h"

#include <assert.h>
#include <rsbuffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

static int
rsbuf_rsaenc(
    struct RSCache_Buffer* buffer,
    const struct rsa* rsa)
{
    int8_t* temp = malloc(buffer->position);
    memcpy(temp, buffer->data, buffer->position);

    int enclen = rsa_crypt(
        (struct rsa*)rsa, temp, buffer->position, buffer->data + 1, buffer->size);
    free(temp);

    buffer->data[0] = enclen;
    buffer->position = enclen + 1;

    return enclen;
}

static void
default_seed_fn(
    void* user,
    int32_t* seed)
{
    (void)user;
    seed[0] = (rand() % 99999999);
    seed[1] = (rand() % 99999999);
    /* seed[2] and seed[3] come from the server seed. */
}

struct LoginProto*
loginproto_new(
    struct Isaac* random_in,
    struct Isaac* random_out,
    struct rsa const* rsa,
    struct GameProtoRevTable const* rev,
    char const* username,
    char const* password)
{
    struct LoginProto* loginproto = malloc(sizeof(struct LoginProto));
    assert(loginproto);
    assert(rev);
    memset(loginproto, 0, sizeof(struct LoginProto));

    loginproto->random_in = random_in;
    loginproto->random_out = random_out;
    memcpy(&loginproto->rsa, rsa, sizeof(struct rsa));
    loginproto->rev = rev;

    loginproto->out = ringbuf_new(512);
    loginproto->in = ringbuf_new(8192);

    loginproto->username = username;
    loginproto->password = password;

    loginproto->seed_fn = default_seed_fn;
    loginproto->seed_user = NULL;

    loginproto->state = LOGINPROTO_SEND_CONNECT;
    return loginproto;
}

void
loginproto_set_seed_fn(
    struct LoginProto* loginproto,
    loginproto_seed_fn seed_fn,
    void* user)
{
    assert(loginproto);
    loginproto->seed_fn = seed_fn ? seed_fn : default_seed_fn;
    loginproto->seed_user = user;
}

void
loginproto_set_reconnect(
    struct LoginProto* loginproto,
    int reconnect)
{
    assert(loginproto);
    loginproto->reconnect = reconnect;
}

void
loginproto_free(struct LoginProto* loginproto)
{
    if( !loginproto )
        return;
    if( loginproto->out )
    {
        ringbuf_free(loginproto->out);
        loginproto->out = NULL;
    }
    if( loginproto->in )
    {
        ringbuf_free(loginproto->in);
        loginproto->in = NULL;
    }
    free(loginproto);
}

int
loginproto_recv(
    struct LoginProto* loginproto,
    uint8_t* data,
    int size)
{
    if( size <= 0 )
        return 0;
    assert(loginproto);
    assert(data);

    {
        int to_read_cnt = loginproto->await_recv_cnt > size ? size : loginproto->await_recv_cnt;
        ringbuf_write(loginproto->in, data, to_read_cnt);
        loginproto->await_recv_cnt -= to_read_cnt;
        return to_read_cnt;
    }
}

int
loginproto_send(
    struct LoginProto* loginproto,
    uint8_t* out,
    int out_size)
{
    assert(out_size >= ringbuf_avail(loginproto->out));
    return ringbuf_read(loginproto->out, out, out_size);
}

enum
{
    LOGINPROTO_OP_GAMELOGIN = 16,
    LOGINPROTO_OP_GAMERECONNECT = 18,
};

/*
 * The first byte of the login block, which is the whole of what separates a
 * reconnect from a fresh login on the wire at every revision this machine
 * drives: LostCity's client writes `reconnect ? 18 : 16` and then the same
 * bytes either way (Client-TS Client.ts login()), and its server reads the
 * block with one decoder and branches on the opcode to decide whether the
 * character it already has is handed back or logged in from a save.
 *
 * Announcing it is therefore free where the revision has it, and not free
 * where it doesn't -- a server that does not know opcode 18 drops the
 * connection on the first byte, which presents as "the reconnect never
 * connected" rather than as a rejected login. So the table decides.
 */
static int
login_opcode(struct LoginProto* loginproto)
{
    if( !loginproto->reconnect )
        return LOGINPROTO_OP_GAMELOGIN;

    /*
     * The seed flavour is not this machine's block to write: it replaces the
     * authentication section wholesale, and a revision that wants it brings a
     * login vtable that does (loginproto_osrs239.c). A table that selects it
     * and leaves `->login` NULL is a table bug, not a runtime state.
     */
    assert(loginproto->rev->reconnect_kind != NET_RECONNECT_SEED);

    if( loginproto->rev->reconnect_kind != NET_RECONNECT_CREDS )
    {
        TORIRS_LOG(
            "loginproto: %s has no reconnect handshake; re-establishing as a fresh login\n",
            loginproto->rev->name);
        return LOGINPROTO_OP_GAMELOGIN;
    }
    return LOGINPROTO_OP_GAMERECONNECT;
}

int
loginproto_poll(struct LoginProto* loginproto)
{
    struct RSCache_Buffer in;
    struct RSCache_Buffer out;
    struct RSCache_Buffer out_encrypted;

    if( ringbuf_used(loginproto->out) != 0 )
        return LOGINPROTO_AWAIT_SEND;

    switch( loginproto->state )
    {
    case LOGINPROTO_SEND_CONNECT:
    {
        RSCache_BufferInit(&out, loginproto->tempout, sizeof(loginproto->tempout));

        uint64_t username37 = strtobase37(loginproto->username);
        int login_server = (int)((username37 >> 16) & 0x1F);

        p1(&out, 14);
        p1(&out, login_server);

        ringbuf_write(loginproto->out, loginproto->tempout, out.position);

        loginproto->await_recv_cnt = 9 + 8;
        loginproto->state = LOGINPROTO_SEND_CREDENTIALS;
        return LOGINPROTO_AWAIT_RECV;
    }
    case LOGINPROTO_SEND_CREDENTIALS:
    {
        RSCache_BufferInit(&out, loginproto->tempout, sizeof(loginproto->tempout));
        RSCache_BufferInit(&in, loginproto->tempin, sizeof(loginproto->tempin));

        /* The server sends 9 bytes that don't matter, then the 8-byte seed. */
        if( ringbuf_used(loginproto->in) < 9 + 8 )
            return LOGINPROTO_AWAIT_RECV;

        ringbuf_read(loginproto->in, loginproto->tempin, 9 + 8);

        in.position = 9;
        uint64_t server_seed = g8(&in);

        loginproto->seed_fn(loginproto->seed_user, loginproto->seed);
        loginproto->seed[2] = (int32_t)(server_seed >> 32);
        loginproto->seed[3] = (int32_t)(server_seed & 0xFFFFFFFF);

        RSCache_BufferInit(
            &out_encrypted, loginproto->tempencrypt, sizeof(loginproto->tempencrypt));
        p1(&out_encrypted, 10);
        p4(&out_encrypted, loginproto->seed[0]);
        p4(&out_encrypted, loginproto->seed[1]);
        p4(&out_encrypted, loginproto->seed[2]);
        p4(&out_encrypted, loginproto->seed[3]);
        p4(&out_encrypted, 1337); /* uid */
        pjstr(&out_encrypted, loginproto->username, '\n');
        pjstr(&out_encrypted, loginproto->password, '\n');

        if( rsbuf_rsaenc(&out_encrypted, &loginproto->rsa) < 0 )
        {
            loginproto->state = LOGINPROTO_ERROR;
            return -1;
        }

        {
            int encrypted_len = out_encrypted.position;
            int low_memory = 0;
            RSCache_BufferInit(&out, loginproto->tempout, sizeof(loginproto->tempout));

            p1(&out, login_opcode(loginproto));

            /* The revision is one byte, with 255 as the escape to a two-byte
             * one. Builds past 254 (LostCity's 274 and 289 branches) cannot
             * be written any other way, and the length byte has to grow with
             * it -- a server reads exactly the count it was promised, so
             * getting this wrong desyncs the whole block rather than failing
             * the version check. */
            int rev_bytes = loginproto->rev->client_version > 254 ? 3 : 1;

            p1(&out, encrypted_len + 36 + rev_bytes + 1);
            if( rev_bytes == 3 )
            {
                p1(&out, 255);
                p2(&out, loginproto->rev->client_version);
            }
            else
            {
                p1(&out, loginproto->rev->client_version);
            }
            p1(&out, low_memory ? 1 : 0);

            for( int i = 0; i < 9; i++ )
                p4(&out, loginproto->rev->jag_checksum[i]);

            pbuf(&out, out_encrypted.data, encrypted_len);

            ringbuf_write(loginproto->out, loginproto->tempout, out.position);
        }

        isaac_seed(loginproto->random_out, (int*)loginproto->seed, 4);

        {
            int32_t seed_in[4];
            for( int i = 0; i < 4; i++ )
                seed_in[i] = loginproto->seed[i] + 50;
            isaac_seed(loginproto->random_in, (int*)seed_in, 4);
        }

        loginproto->state = LOGINPROTO_LOGIN_RESPONSE;
        loginproto->await_recv_cnt = 1;
        return LOGINPROTO_AWAIT_SEND;
    }
    case LOGINPROTO_LOGIN_RESPONSE:
    {
        RSCache_BufferInit(&in, loginproto->tempin, sizeof(loginproto->tempin));
        if( ringbuf_used(loginproto->in) < 1 )
            return LOGINPROTO_AWAIT_RECV;

        ringbuf_read(loginproto->in, in.data, 1);

        {
            int reply_byte = g1(&in);

            loginproto->await_recv_cnt = 0;
            if( reply_byte == 2 )
            {
                /* Success is 3 bytes total: 2, staffmodlevel, mouse-tracked
                 * flag (LostCity World.ts). Consume the tail before handing
                 * the stream over, or those 2 bytes desync the ISAAC-framed
                 * game stream. */
                loginproto->state = LOGINPROTO_LOGIN_SUCCESS_TAIL;
                loginproto->await_recv_cnt = 2;
                return LOGINPROTO_AWAIT_RECV;
            }
            if( reply_byte == 15 )
            {
                /*
                 * The reconnect verdict: a single byte, no tail, and no
                 * REBUILD_LOGIN behind it -- the server is handing back a
                 * session rather than opening one, so it restates neither the
                 * staff level nor the mouse-tracking flag that a `2` carries.
                 *
                 * Accepted on a fresh login too, because the reference does
                 * (Client-TS `response === 15`): a server that answers a 16
                 * with a 15 has decided the character is already in the world,
                 * and the stream that follows is a game stream either way.
                 */
                loginproto->state = LOGINPROTO_SUCCESS;
                return LOGINPROTO_SUCCESS;
            }
            TORIRS_ERR("loginproto: login rejected, reply=%d\n", reply_byte);
            loginproto->state = LOGINPROTO_ERROR;
            return LOGINPROTO_AWAIT_RECV;
        }
    }
    case LOGINPROTO_LOGIN_SUCCESS_TAIL:
    {
        RSCache_BufferInit(&in, loginproto->tempin, sizeof(loginproto->tempin));
        if( ringbuf_used(loginproto->in) < 2 )
            return LOGINPROTO_AWAIT_RECV;
        ringbuf_read(loginproto->in, in.data, 2);
        loginproto->staffmodlevel = g1(&in);
        (void)g1(&in); /* mouse-tracked flag; tracking replies are stubbed */
        loginproto->await_recv_cnt = 0;
        loginproto->state = LOGINPROTO_SUCCESS;
        return LOGINPROTO_SUCCESS;
    }
    case LOGINPROTO_SUCCESS:
        return LOGINPROTO_SUCCESS;
    case LOGINPROTO_ERROR:
        return LOGINPROTO_ERROR;
    }

    return LOGINPROTO_ERROR;
}
