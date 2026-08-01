#ifndef SRC_NET_MOCK_MOCK230_EMBED_H
#define SRC_NET_MOCK_MOCK230_EMBED_H

/*
 * The server hosted inside another process, with no socket anywhere.
 *
 * The client half of this already worked: `struct ToriRS_Network` never touches
 * a descriptor — bytes arrive through ToriRS_Network_HandleCmd(NET_RECV) and
 * leave through ToriRS_Network_PopOut, and the platform layer is what bridges
 * that to a real socket. So an in-process game is just those two byte streams
 * crossed over with the server's, which is exactly what this exposes:
 *
 *      client PopOut  --->  mock230_embed_write   (client -> server)
 *      client NET_RECV <---  mock230_embed_read   (server -> client)
 *
 * with mock230_embed_pump in between to let the server act on what it was
 * given. See src/platform/net_transport_embed.c for that bridge, and
 * src/net/mock/test/embed_test.c for the whole loop driven by hand.
 *
 * What made this possible is not this file. It is that the login handshake
 * stopped blocking (mock230_session.c): client and server share one thread
 * here, so a server that waits for bytes is a server that waits forever.
 *
 * Threading: none. Every call must come from the same thread, and the server
 * only advances inside mock230_embed_pump — there is no background tick.
 */

#include <stdint.h>

enum
{
    /**
     * Connections one embedded world can hold.
     *
     * A *client* limit, not a world limit: several clients in one embed is the
     * point of the array, and `mock230_embed_start` refusing a second embed is
     * about the process-wide cache tables, not about players. Kept at or below
     * MOCK230_PLAYER_MAX so a connection that handshakes always gets a slot.
     */
    MOCK230_EMBED_CLIENT_MAX = 4,
};

struct Mock230Embed;
struct Mock230Server;
struct Mock230Player;

/**
 * Bring up the static data and open one embedded world, with client 0 already
 * connected.
 *
 * Returns NULL when the loaders failed hard enough that there is nothing to
 * serve. A missing content tree is *not* that — the server runs without one.
 * Loading is the same sequence the socket server uses (mock230_boot.c), so an
 * embedded world and a socket world differ in no respect but their transport.
 */
struct Mock230Embed*
mock230_embed_start(void);

/**
 * Open another connection to the same world. Returns its client id, or -1.
 *
 * This is the whole of what "two players in one process" needs from the host
 * side: a second byte-queue pair and a second session. Everything after it —
 * the handshake, the pool slot, the login burst — is the same code the first
 * client ran, which is what makes the test a test of the *server* rather than
 * of a second code path.
 */
int
mock230_embed_connect(struct Mock230Embed* embed);

/**
 * Close one client, as a dropped socket closes one.
 *
 * The same three calls the socket server makes when `mock230_session_alive`
 * goes false (mock230_main.c `serve`'s tail): release the pool slot, free the
 * session, free the byte queues. It is deliberately *not* the whole of that
 * tail — `mock230_bank_shutdown`, `mock230_scripts_free` and
 * `mock230_world_reset` there are the world going away with the last
 * connection, which is exactly the single-connection assumption an embed does
 * not share.
 *
 * Added because there was no in-process way to log a player *out*, so nothing
 * that happens on a logout — the follower broadcast, the logout notification,
 * the slot release — had a test it could be asserted in. Returns 1 if a client
 * was closed.
 */
int
mock230_embed_disconnect(
    struct Mock230Embed* embed,
    int client_id);

void
mock230_embed_stop(struct Mock230Embed* embed);

/**
 * Client -> server. Returns `len`, or -1 once the session is gone.
 *
 * The bytes are the same ones that would have gone down a socket: the login
 * hello, the RSA block, then ISAAC-scrambled packets. Nothing is special-cased
 * for being in-process, which is what keeps this path honest — an embedded
 * session exercises the real protocol, not a shortcut around it.
 */
int
mock230_embed_write(
    struct Mock230Embed* embed,
    int client_id,
    const uint8_t* data,
    int len);

/** Server -> client. Returns the byte count, 0 when nothing is pending, or -1
 *  once the session is gone and drained. */
int
mock230_embed_read(
    struct Mock230Embed* embed,
    int client_id,
    uint8_t* dst,
    int max);

/** Bytes waiting to be read. */
int
mock230_embed_pending(
    const struct Mock230Embed* embed,
    int client_id);

/**
 * Let the server act.
 *
 * Decodes whatever mock230_embed_write left for *every* client, runs the login
 * burst for any whose handshake just completed, and advances one 600 ms game
 * tick when `run_tick` is set. The tick is the world's and runs once however
 * many clients are attached. A host with a real frame clock passes 1 every
 * 600 ms and 0 otherwise; a test passes 1 every call and runs the world as fast
 * as it likes.
 *
 * Returns 0 once every session is dead.
 */
int
mock230_embed_pump(
    struct Mock230Embed* embed,
    int run_tick);

/** The world, for a host that wants to assert on game state directly rather
 *  than through the wire. NULL before the first handshake completes. */
struct Mock230Server*
mock230_embed_world(struct Mock230Embed* embed);

/** 1 once this client's login handshake finished. */
int
mock230_embed_online(
    const struct Mock230Embed* embed,
    int client_id);

/** The pool slot this client logged in to, or NULL before it did. */
struct Mock230Player*
mock230_embed_player(
    struct Mock230Embed* embed,
    int client_id);

#endif
