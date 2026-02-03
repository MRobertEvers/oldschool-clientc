#ifndef LOGINPROTO_H
#define LOGINPROTO_H

#include "datastruct/ringbuf.h"
#include "isaac.h"
#include "rsa.h"

#include <stdint.h>

#define LOGINPROTO_AWAIT_RECV -2
#define LOGINPROTO_AWAIT_SEND -3

enum LoginProtoState
{
    LOGINPROTO_ERROR = -1,
    LOGINPROTO_SEND_CONNECT = 0,
    LOGINPROTO_SEND_CREDENTIALS,
    LOGINPROTO_LOGIN_RESPONSE,
    LOGINPROTO_SUCCESS,
};

struct LoginProto
{
    enum LoginProtoState state;
    struct Isaac* random_in;
    struct Isaac* random_out;
    struct rsa rsa;

    char* username;
    char* password;

    struct RingBuf* out;
    struct RingBuf* in;

    int await_recv_cnt;

    uint32_t seed[4];
    int32_t jag_checksum[9];

    uint8_t tempout[512];
    uint8_t tempin[512];
    uint8_t tempencrypt[512];
};

struct LoginProto*
loginproto_new(
    struct Isaac* random_in,
    struct Isaac* random_out,
    struct rsa* rsa,
    char* username,
    char* password,
    int32_t* jag_checksum);

void
loginproto_recv(
    struct LoginProto* loginproto,
    uint8_t* data,
    int size);

int
loginproto_send(
    struct LoginProto* loginproto,
    uint8_t* out,
    int out_size);

int
loginproto_poll(struct LoginProto* loginproto);

#endif