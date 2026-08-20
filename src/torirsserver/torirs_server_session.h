#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_SESSION_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_SESSION_H

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

#include "torirs_server_transport.h"

#include <stdint.h>

struct Isaac;
struct ToriRSServer;
struct ToriRSServerPlayer;

enum
{
    /*
     * Undecoded inbound bytes. Only has to hold one login block (about 400
     * bytes) or one game packet plus whatever arrived behind it; the reader
     * compacts after every drain.
     */
    TORIRSSERVER_SESSION_IN_MAX = 16384,
};

enum ToriRSServerSessionState
{
    /** Awaiting INIT_GAME_CONNECTION (opcode 14). */
    TORIRSSERVER_SESSION_INIT = 0,
    /** Session id sent; awaiting GAMELOGIN (opcode 16) and its block. */
    TORIRSSERVER_SESSION_LOGIN,
    /**
     * A JS5 (cache download) connection.
     *
     * The same socket carries either service and the client picks with its
     * FIRST byte: 14 opens a game login, 15 opens JS5. That is why this is a
     * session state rather than a second listener -- a vanilla client will not
     * reach the login screen without JS5, so a server that only speaks the game
     * protocol shows a client that never starts.
     *
     * Nothing here is ciphered: JS5 runs before any ISAAC pair exists.
     */
    TORIRSSERVER_SESSION_JS5,
    /** Ciphers armed; the game stream is running. */
    TORIRSSERVER_SESSION_ONLINE,
    /** Peer gone, or the stream went unrecoverable. */
    TORIRSSERVER_SESSION_DEAD,
};

struct ToriRSServerSession
{
    struct ToriRSServerTransport transport;
    enum ToriRSServerSessionState state;

    uint8_t in[TORIRSSERVER_SESSION_IN_MAX];
    int in_len;

    struct Isaac* cipher_out; /* server -> client opcode scramble */
    struct Isaac* cipher_in;  /* client -> server opcode descramble */

    int verbose;

    /** The name typed at the login screen, copied onto the player when the
     *  world comes up. */
    char display_name[32];

    /**
     * Who this connection is, once the host has given it a pool slot.
     *
     * NULL until then, and NULL again after a logout. The session is what
     * *decides* which player a decoded packet belongs to — with one connection
     * "the world's player" happened to be the right answer, and with two it is
     * whichever one logged in first. `player->session` is the same edge read the
     * other way, and the encoders have used it since the encoder pass.
     */
    struct ToriRSServerPlayer* player;

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

    /** Raised when the game stream arms; cleared by ToriRSServer_SessionTakeLogin
     *  so the caller runs the world's login burst exactly once. */
    int login_raised;

    /**
     * The handshake was GAMERECONNECT (18), not GAMELOGIN (16).
     *
     * A client whose connection died — a browser tab the OS stopped
     * scheduling, a dropped socket — asks for its session back rather than
     * logging in again. The block is the same one either way; what differs is
     * the answer. A reconnect is owed RECONNECT_OK carrying the player-info
     * init block, because no REBUILD_LOGIN follows it, and that block cannot
     * be written until the player exists and its save has been read. So the
     * response is deferred to the world's login path, and this is how it knows
     * (see ToriRSServer_WorldLogin).
     */
    int reconnect;

    /** Advances for every successfully queued server->client frame. The
     * online decoder uses it to put an explicit SERVER_TICK_END after an
     * immediate input-response burst, rather than leaving the client waiting
     * for the next scheduled 600ms world tick. */
    uint64_t output_generation;
    /** Canonical name of the most recent framed game packet. Used to avoid
     * appending a duplicate response fence when a handler already sent one. */
    int last_output_packet_name;
};

/*
 * The server's public identity, for a client built in the same process.
 *
 * Over a socket the client gets these from its manifest and the two ends agree
 * by configuration. In-process there is no manifest, and a test that restated
 * the modulus would be a second copy that can drift from the private half in
 * torirs_server_session.c. The exponent is the protocol's fixed 10001.
 */
extern const char* const TORIRSSERVER_RSA_PUBLIC_EXPONENT;
extern const char* const TORIRSSERVER_RSA_PUBLIC_MODULUS;

/** Take over a transport. Does not touch it until the first pump. */
void
ToriRSServer_SessionInit(
    struct ToriRSServerSession* session,
    const struct ToriRSServerTransport* transport,
    int verbose);

void
ToriRSServer_SessionFree(struct ToriRSServerSession* session);

/**
 * Move whatever the transport has into the session and advance as far as the
 * bytes allow: complete the handshake, then decode and dispatch whole packets.
 *
 * Never blocks and never partially consumes a packet. Returns 0 once the
 * session is dead, which is the caller's cue to stop.
 */
int
ToriRSServer_SessionPump(
    struct ToriRSServerSession* session,
    struct ToriRSServer* srv);

/** 1 exactly once, on the pump that armed the game stream. The caller answers
 *  it by initialising the world and sending the login burst. */
int
ToriRSServer_SessionTakeLogin(struct ToriRSServerSession* session);

/** Frame-level write. Bytes are already scrambled and framed by the encoder. */
int
ToriRSServer_SessionSend(
    struct ToriRSServerSession* session,
    const uint8_t* data,
    int len);

int
ToriRSServer_SessionAlive(const struct ToriRSServerSession* session);

/** A descriptor to select() on, or -1 when there is nothing to wait on —
 *  which is what an embedded session always reports. */
int
ToriRSServer_SessionPollfd(const struct ToriRSServerSession* session);

void
ToriRSServer_SessionKill(struct ToriRSServerSession* session);

#endif
