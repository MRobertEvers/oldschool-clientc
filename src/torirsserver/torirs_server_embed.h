#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_EMBED_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_EMBED_H

/*
 * The server hosted inside another process, with no socket anywhere.
 *
 * The client half of this already worked: `struct ToriRS_Network` never touches
 * a descriptor — bytes arrive through ToriRS_Network_HandleCmd(NET_RECV) and
 * leave through ToriRS_Network_PopOut, and the platform layer is what bridges
 * that to a real socket. So an in-process game is just those two byte streams
 * crossed over with the server's, which is exactly what this exposes:
 *
 *      client PopOut  --->  ToriRSServer_EmbedWrite   (client -> server)
 *      client NET_RECV <---  ToriRSServer_EmbedRead   (server -> client)
 *
 * with ToriRSServer_EmbedPump in between to let the server act on what it was
 * given. See src/platform/net_transport_embed.c for that bridge, and
 * src/torirsserver/test/embed_test.c for the whole loop driven by hand.
 *
 * What made this possible is not this file. It is that the login handshake
 * stopped blocking (torirs_server_session.c): client and server share one thread
 * here, so a server that waits for bytes is a server that waits forever.
 *
 * Threading: none. Every call must come from the same thread, and the server
 * only advances inside ToriRSServer_EmbedPump — there is no background tick.
 */

#include <stdint.h>

enum
{
    /**
     * Connections one embedded world can hold.
     *
     * A *client* limit, not a world limit: several clients in one embed is the
     * point of the array, and `ToriRSServer_EmbedStart` refusing a second embed is
     * about the process-wide cache tables, not about players. Kept at or below
     * TORIRSSERVER_PLAYER_MAX so a connection that handshakes always gets a slot.
     */
    TORIRSSERVER_EMBED_CLIENT_MAX = 4,
};

struct ToriRSServerEmbed;
struct ToriRSServer;
struct ToriRSServerPlayer;

/**
 * Bring up the static data and open one embedded world, with client 0 already
 * connected.
 *
 * `rev_name` is the connecting client's protocol ("osrs230", "osrs239"). The
 * embed and its client are two ends of one in-process queue pair, so unlike the
 * socket server there is no deployment where they may legitimately differ — a
 * mismatch reads the login block's RSA size out of position and dies as "rsa
 * decrypt failed". Pass the client's revision and the wire follows it; NULL
 * falls back to TORIRSSERVER_REV, then to the default (hosts with no client
 * revision of their own, e.g. embed_test).
 *
 * Returns NULL when the loaders failed hard enough that there is nothing to
 * serve. A missing content tree is *not* that — the server runs without one.
 * Loading is the same sequence the socket server uses (torirs_server_boot.c), so an
 * embedded world and a socket world differ in no respect but their transport.
 */
struct ToriRSServerEmbed*
ToriRSServer_EmbedStart(char const* rev_name);

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
ToriRSServer_EmbedConnect(struct ToriRSServerEmbed* embed);

/**
 * Close one client, as a dropped socket closes one.
 *
 * The same three calls the socket server makes when `ToriRSServer_SessionAlive`
 * goes false (torirs_server_main.c `serve`'s tail): release the pool slot, free the
 * session, free the byte queues. It is deliberately *not* the whole of that
 * tail — `ToriRSServer_BankShutdown`, `ToriRSServer_ScriptsFree` and
 * `ToriRSServer_WorldReset` there are the world going away with the last
 * connection, which is exactly the single-connection assumption an embed does
 * not share.
 *
 * Added because there was no in-process way to log a player *out*, so nothing
 * that happens on a logout — the follower broadcast, the logout notification,
 * the slot release — had a test it could be asserted in. Returns 1 if a client
 * was closed.
 */
int
ToriRSServer_EmbedDisconnect(
    struct ToriRSServerEmbed* embed,
    int client_id);

void
ToriRSServer_EmbedStop(struct ToriRSServerEmbed* embed);

/**
 * Client -> server. Returns `len`, or -1 once the session is gone.
 *
 * The bytes are the same ones that would have gone down a socket: the login
 * hello, the RSA block, then ISAAC-scrambled packets. Nothing is special-cased
 * for being in-process, which is what keeps this path honest — an embedded
 * session exercises the real protocol, not a shortcut around it.
 */
int
ToriRSServer_EmbedWrite(
    struct ToriRSServerEmbed* embed,
    int client_id,
    const uint8_t* data,
    int len);

/** Server -> client. Returns the byte count, 0 when nothing is pending, or -1
 *  once the session is gone and drained. */
int
ToriRSServer_EmbedRead(
    struct ToriRSServerEmbed* embed,
    int client_id,
    uint8_t* dst,
    int max);

/** Bytes waiting to be read. */
int
ToriRSServer_EmbedPending(
    const struct ToriRSServerEmbed* embed,
    int client_id);

/**
 * Let the server act.
 *
 * Decodes whatever ToriRSServer_EmbedWrite left for *every* client, runs the login
 * burst for any whose handshake just completed, and advances one 600 ms game
 * tick when `run_tick` is set. The tick is the world's and runs once however
 * many clients are attached. A host with a real frame clock passes 1 every
 * 600 ms and 0 otherwise; a test passes 1 every call and runs the world as fast
 * as it likes.
 *
 * Returns 0 once every session is dead.
 */
int
ToriRSServer_EmbedPump(
    struct ToriRSServerEmbed* embed,
    int run_tick);

/** The world, for a host that wants to assert on game state directly rather
 *  than through the wire. NULL before the first handshake completes. */
struct ToriRSServer*
ToriRSServer_EmbedWorld(struct ToriRSServerEmbed* embed);

/** 1 once this client's login handshake finished. */
int
ToriRSServer_EmbedOnline(
    const struct ToriRSServerEmbed* embed,
    int client_id);

/** The pool slot this client logged in to, or NULL before it did. */
struct ToriRSServerPlayer*
ToriRSServer_EmbedPlayer(
    struct ToriRSServerEmbed* embed,
    int client_id);

#endif
