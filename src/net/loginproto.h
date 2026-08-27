#ifndef SRC_NET_LOGINPROTO_H
#define SRC_NET_LOGINPROTO_H

/*
 * Login handshake state machine, ported from v0/osrs/loginproto.c.
 * Adaptations against v0: the buffer type is the 3rd/rscache RSCache_Buffer
 * (same g1/p1/pjstr macro surface), client_version + jag CRCs come from the
 * revision table instead of literals, and the client-seed RNG is injectable
 * so tests and record/replay are deterministic.
 *
 * Wire sequence (reference Client-TS login()):
 *   SEND_CONNECT      -> p1(14) p1(login_server = base37(user)>>16 & 0x1f)
 *   SEND_CREDENTIALS  <- 9 ignored bytes + g8 server seed
 *                     -> outer p1(16|18) p1(len) p1(version) p1(lowmem)
 *                        (18 = GAMERECONNECT, same block; see reconnect_kind)
 *                        9x p4(jag CRC) + RSA[p1(10) 4x p4(seed) p4(uid)
 *                        pjstr(user) pjstr(pass)]
 *                        then ISAAC out=seed, in=seed+50 per word
 *   LOGIN_RESPONSE    <- 1 byte; 2/18/19 = success
 */

#include "isaac.h"
#include "rev/gameproto_revisions.h"
#include "ringbuf.h"
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
    LOGINPROTO_LOGIN_SUCCESS_TAIL,
    LOGINPROTO_SUCCESS,
};

/** Client-seed source: fills seed[0] and seed[1] (seed[2]/[3] come from the
 *  server seed). NULL = rand()-based default. */
typedef void (*loginproto_seed_fn)(void* user, int32_t* seed);

struct LoginProto
{
    enum LoginProtoState state;
    struct Isaac* random_in;
    struct Isaac* random_out;
    struct rsa rsa;
    struct GameProtoRevTable const* rev;

    char const* username;
    char const* password;

    struct RingBuf* out;
    struct RingBuf* in;

    int await_recv_cnt;
    int staffmodlevel;

    /**
     * The server's rejection byte, or -1 while none has arrived.
     *
     * Kept because the code is the only thing that distinguishes "wrong
     * password" from "world is full" from "you were just in another world",
     * and a login screen that cannot tell them apart can only say that
     * something went wrong. What each code MEANS in words is the profile's
     * ([login_reply:<code>]), not this layer's.
     */
    int reply_code;

    int32_t seed[4];

    loginproto_seed_fn seed_fn;
    void* seed_user;

    /**
     * Announce this handshake as GAMERECONNECT (18) rather than GAMELOGIN
     * (16).
     *
     * A request, not a decision: `rev->reconnect_kind` settles whether the
     * revision has the opcode at all, and a revision that does not gets a
     * fresh login instead. See the opcode helper in loginproto.c.
     */
    int reconnect;

    uint8_t tempout[512];
    uint8_t tempin[512];
    uint8_t tempencrypt[512];
};

struct LoginProto*
loginproto_new(
    struct Isaac* random_in,
    struct Isaac* random_out,
    struct rsa const* rsa,
    struct GameProtoRevTable const* rev,
    char const* username,
    char const* password);

void
loginproto_set_seed_fn(
    struct LoginProto* loginproto,
    loginproto_seed_fn seed_fn,
    void* user);

/**
 * Ask for this handshake to be a reconnect (see LoginProto.reconnect).
 *
 * Must be called before the first poll, since the block is built there and
 * the opcode is its first byte.
 */
void
loginproto_set_reconnect(
    struct LoginProto* loginproto,
    int reconnect);

void
loginproto_free(struct LoginProto* loginproto);

/** Feed received bytes (up to await_recv_cnt). Returns bytes consumed. */
int
loginproto_recv(
    struct LoginProto* loginproto,
    uint8_t* data,
    int size);

/** Drain pending outbound bytes into `out`. Returns bytes written. */
int
loginproto_send(
    struct LoginProto* loginproto,
    uint8_t* out,
    int out_size);

/** Advance the machine. Returns LOGINPROTO_AWAIT_RECV / _AWAIT_SEND /
 *  LOGINPROTO_SUCCESS / negative error. */
int
loginproto_poll(struct LoginProto* loginproto);

#endif
