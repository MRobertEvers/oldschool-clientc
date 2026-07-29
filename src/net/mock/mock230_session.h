#ifndef SRC_NET_MOCK_MOCK230_SESSION_H
#define SRC_NET_MOCK_MOCK230_SESSION_H

/*
 * One client's connection: the login handshake, the two ISAAC ciphers, and the
 * inbound frame reader.
 *
 * This is a *state machine*, and that is the whole point of the file. The
 * handshake used to be a straight-line function that blocked on
 * `recv_full(1)`, `recv_full(3)`, `recv_full(len)`. That is fine against a
 * socket with a thread to spare and impossible in-process: an embedded server
 * shares a thread with the client that is supposed to be feeding it, so a
 * blocking read is not a stall, it is a deadlock. Every wait here is instead
 * "do I have enough bytes yet?", re-entered on the next pump.
 *
 * It is also what the socket path always should have been. The old code had two
 * `fprintf(stderr, "split var-u8 header")` bailouts admitting it could not cope
 * with a packet torn across two reads, and stayed correct only because the
 * client happens to write each packet with one write(). That assumption is now
 * gone rather than documented.
 *
 * What is NOT here: game state, and the tick. A session carries bytes and says
 * when the stream came up; the world does the rest.
 */

#include "mock230_transport.h"

#include <stdint.h>

struct Isaac;
struct Mock230Server;

enum
{
    /*
     * Undecoded inbound bytes. Only has to hold one login block (about 400
     * bytes) or one game packet plus whatever arrived behind it; the reader
     * compacts after every drain.
     */
    MOCK230_SESSION_IN_MAX = 16384,
};

enum Mock230SessionState
{
    /** Awaiting INIT_GAME_CONNECTION (opcode 14). */
    MOCK230_SESSION_INIT = 0,
    /** Session id sent; awaiting GAMELOGIN (opcode 16) and its block. */
    MOCK230_SESSION_LOGIN,
    /** Ciphers armed; the game stream is running. */
    MOCK230_SESSION_ONLINE,
    /** Peer gone, or the stream went unrecoverable. */
    MOCK230_SESSION_DEAD,
};

struct Mock230Session
{
    struct Mock230Transport transport;
    enum Mock230SessionState state;

    uint8_t in[MOCK230_SESSION_IN_MAX];
    int in_len;

    struct Isaac* cipher_out; /* server -> client opcode scramble */
    struct Isaac* cipher_in;  /* client -> server opcode descramble */

    int verbose;

    /** The name typed at the login screen, copied onto the player when the
     *  world comes up. */
    char display_name[32];

    /*
     * A packet whose opcode has been descrambled but whose body has not all
     * arrived. -1 when there is none.
     *
     * This field is what makes a torn packet survivable, and it exists because
     * ISAAC cannot be rewound: descrambling the opcode spends a byte of
     * keystream whether or not the rest of the packet is present, so the byte
     * is consumed here the instant it is read and the opcode is remembered
     * until the body catches up. The old reader instead printed "split var-u8
     * header" and gave up, and was only ever correct because the client writes
     * each packet with a single write().
     */
    int pending_opcode;

    /** Raised when the game stream arms; cleared by mock230_session_take_login
     *  so the caller runs the world's login burst exactly once. */
    int login_raised;
};

/*
 * The server's public identity, for a client built in the same process.
 *
 * Over a socket the client gets these from its manifest and the two ends agree
 * by configuration. In-process there is no manifest, and a test that restated
 * the modulus would be a second copy that can drift from the private half in
 * mock230_session.c. The exponent is the protocol's fixed 10001.
 */
extern const char* const MOCK230_RSA_PUBLIC_EXPONENT;
extern const char* const MOCK230_RSA_PUBLIC_MODULUS;

/** Take over a transport. Does not touch it until the first pump. */
void
mock230_session_init(
    struct Mock230Session* session,
    const struct Mock230Transport* transport,
    int verbose);

void
mock230_session_free(struct Mock230Session* session);

/**
 * Move whatever the transport has into the session and advance as far as the
 * bytes allow: complete the handshake, then decode and dispatch whole packets.
 *
 * Never blocks and never partially consumes a packet. Returns 0 once the
 * session is dead, which is the caller's cue to stop.
 */
int
mock230_session_pump(
    struct Mock230Session* session,
    struct Mock230Server* srv);

/** 1 exactly once, on the pump that armed the game stream. The caller answers
 *  it by initialising the world and sending the login burst. */
int
mock230_session_take_login(struct Mock230Session* session);

/** Frame-level write. Bytes are already scrambled and framed by the encoder. */
int
mock230_session_send(
    struct Mock230Session* session,
    const uint8_t* data,
    int len);

int
mock230_session_alive(const struct Mock230Session* session);

/** A descriptor to select() on, or -1 when there is nothing to wait on —
 *  which is what an embedded session always reports. */
int
mock230_session_pollfd(const struct Mock230Session* session);

void
mock230_session_kill(struct Mock230Session* session);

#endif
