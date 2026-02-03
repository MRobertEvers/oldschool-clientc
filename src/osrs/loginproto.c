#include "loginproto.h"

#include "jbase37.h"
#include "rscache/rsbuf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int
rsbuf_rsaenc(
    struct RSBuffer* buffer,
    const struct rsa* rsa)
{
    int8_t* temp = malloc(buffer->position);
    memcpy(temp, buffer->data, buffer->position);

    int enclen = rsa_crypt(rsa, temp, buffer->position, buffer->data + 1, buffer->size);
    free(temp);

    buffer->data[0] = enclen;
    buffer->position = enclen + 1;

    return enclen;
}

// Generate random seed values
static void
generate_seed(int32_t* seed)
{
    seed[0] = (rand() % 99999999);
    seed[1] = (rand() % 99999999);
    // seed[2] and seed[3] will be set from server_seed
}

struct LoginProto*
loginproto_new(
    struct Isaac* random_in,
    struct Isaac* random_out,
    struct rsa* rsa,
    char* username,
    char* password,
    int32_t* jag_checksum)
{
    struct LoginProto* loginproto = malloc(sizeof(struct LoginProto));
    memset(loginproto, 0, sizeof(struct LoginProto));

    loginproto->random_in = random_in;
    loginproto->random_out = random_out;
    memcpy(&loginproto->rsa, rsa, sizeof(struct rsa));

    loginproto->out = ringbuf_new(512);
    loginproto->in = ringbuf_new(8192);

    loginproto->username = username;
    loginproto->password = password;

    // TODO: Get this from the server.
    loginproto->jag_checksum[0] = 0;
    loginproto->jag_checksum[1] = -945108033;
    loginproto->jag_checksum[2] = -323580723;
    loginproto->jag_checksum[3] = 1539972921;
    loginproto->jag_checksum[4] = -259567598;
    loginproto->jag_checksum[5] = -446588279;
    loginproto->jag_checksum[6] = -1840622973;
    loginproto->jag_checksum[7] = -87627495;
    loginproto->jag_checksum[8] = -1625923170;

    loginproto->state = LOGINPROTO_SEND_CONNECT;
    return loginproto;
}

void
loginproto_recv(
    struct LoginProto* loginproto,
    uint8_t* data,
    int size)
{
    if( size > 0 )
    {
        int to_read_cnt = loginproto->await_recv_cnt > size ? size : loginproto->await_recv_cnt;
        ringbuf_write(loginproto->in, data, to_read_cnt);
        loginproto->await_recv_cnt -= to_read_cnt;
    }
}

int
loginproto_send(
    struct LoginProto* loginproto,
    uint8_t* out,
    int out_size)
{
    assert(out_size >= ringbuf_avail(loginproto->out));
    int read_cnt = ringbuf_read(loginproto->out, out, out_size);
    return read_cnt;
}

int
loginproto_poll(struct LoginProto* loginproto)
{
    struct RSBuffer in;
    struct RSBuffer out;
    struct RSBuffer out_encrypted;

    if( ringbuf_used(loginproto->out) != 0 )
        return LOGINPROTO_AWAIT_SEND;

    switch( loginproto->state )
    {
    case LOGINPROTO_SEND_CONNECT:
    {
        rsbuf_init(&out, loginproto->tempout, sizeof(loginproto->tempout));

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
        rsbuf_init(&out, loginproto->tempout, sizeof(loginproto->tempout));
        rsbuf_init(&in, loginproto->tempin, sizeof(loginproto->tempin));

        // The server sends 9 bytes that don't matter,
        // then 8 bytes for the server seed.
        if( ringbuf_used(loginproto->in) < 9 + 8 )
            return LOGINPROTO_AWAIT_RECV;

        ringbuf_read(loginproto->in, loginproto->tempin, 9 + 8);

        in.position = 9;
        uint64_t server_seed = g8(&in);

        generate_seed(loginproto->seed);
        loginproto->seed[2] = (int32_t)(server_seed >> 32);
        loginproto->seed[3] = (int32_t)(server_seed & 0xFFFFFFFF);

        rsbuf_init(&out_encrypted, loginproto->tempencrypt, sizeof(loginproto->tempencrypt));
        p1(&out_encrypted, 10);
        p4(&out_encrypted, loginproto->seed[0]);
        p4(&out_encrypted, loginproto->seed[1]);
        p4(&out_encrypted, loginproto->seed[2]);
        p4(&out_encrypted, loginproto->seed[3]);
        p4(&out_encrypted, 1337); // uid
        pjstr(&out_encrypted, loginproto->username, '\n');
        pjstr(&out_encrypted, loginproto->password, '\n');

        if( rsbuf_rsaenc(&out_encrypted, &loginproto->rsa) < 0 )
        {
            loginproto->state = LOGINPROTO_ERROR;
            return -1;
        }

        int encrypted_len = out_encrypted.position;
        rsbuf_init(&out, loginproto->tempout, sizeof(loginproto->tempout));

        // reconnect
        if( false )
            p1(&out, 18);
        else
            p1(&out, 16);

        int client_version = 245;
        int low_memory = 0;
        p1(&out, encrypted_len + 36 + 1 + 1);
        p1(&out, client_version);
        p1(&out, low_memory ? 1 : 0);

        for( int i = 0; i < 9; i++ )
            p4(&out, loginproto->jag_checksum[i]);

        pwrite(&out, out_encrypted.data, encrypted_len);

        ringbuf_write(loginproto->out, loginproto->tempout, out.position);

        isaac_seed(loginproto->random_out, loginproto->seed, 4);

        int32_t seed_in[4];
        for( int i = 0; i < 4; i++ )
            seed_in[i] = loginproto->seed[i] + 50;

        isaac_seed(loginproto->random_in, seed_in, 4);

        loginproto->state = LOGINPROTO_LOGIN_RESPONSE;
        loginproto->await_recv_cnt = 1;
        return LOGINPROTO_AWAIT_SEND;
    }
    case LOGINPROTO_LOGIN_RESPONSE:
    {
        rsbuf_init(&in, loginproto->tempin, sizeof(loginproto->tempin));
        if( ringbuf_used(loginproto->in) < 1 )
            return LOGINPROTO_AWAIT_RECV;

        ringbuf_read(loginproto->in, in.data, 1);

        int8_t reply_byte = g1(&in);

        loginproto->await_recv_cnt = 0;
        if( reply_byte == 2 || reply_byte == 18 || reply_byte == 19 )
        {
            loginproto->state = LOGINPROTO_SUCCESS;
            return LOGINPROTO_SUCCESS;
        }
        else
        {
            loginproto->state = LOGINPROTO_ERROR;
            return LOGINPROTO_AWAIT_RECV;
        }
    }
    }

    return LOGINPROTO_ERROR;
}