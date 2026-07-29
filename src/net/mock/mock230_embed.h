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

struct Mock230Embed;
struct Mock230Server;

/**
 * Bring up the static data and open one embedded session.
 *
 * Returns NULL when the loaders failed hard enough that there is nothing to
 * serve. A missing content tree is *not* that — the server runs without one.
 * Loading is the same sequence the socket server uses (mock230_boot.c), so an
 * embedded world and a socket world differ in no respect but their transport.
 */
struct Mock230Embed*
mock230_embed_start(void);

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
    const uint8_t* data,
    int len);

/** Server -> client. Returns the byte count, 0 when nothing is pending, or -1
 *  once the session is gone and drained. */
int
mock230_embed_read(
    struct Mock230Embed* embed,
    uint8_t* dst,
    int max);

/** Bytes waiting to be read. */
int
mock230_embed_pending(const struct Mock230Embed* embed);

/**
 * Let the server act.
 *
 * Decodes whatever mock230_embed_write left, runs the login burst if the
 * handshake just completed, and advances one 600 ms game tick when `run_tick`
 * is set. A host with a real frame clock passes 1 every 600 ms and 0 otherwise;
 * a test passes 1 every call and runs the world as fast as it likes.
 *
 * Returns 0 once the session is dead.
 */
int
mock230_embed_pump(
    struct Mock230Embed* embed,
    int run_tick);

/** The world, for a host that wants to assert on game state directly rather
 *  than through the wire. NULL before the handshake completes. */
struct Mock230Server*
mock230_embed_world(struct Mock230Embed* embed);

/** 1 once the login handshake finished and the world is up. */
int
mock230_embed_online(const struct Mock230Embed* embed);

#endif
