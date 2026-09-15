#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_HOST_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_HOST_H

/*
 * Every connection, one thread, one world.
 *
 * This is the socket server's half of what `torirs_server_embed.c` already was
 * in-process: N connections against one `struct ToriRSServer`, polled together
 * and advanced by one 600 ms tick. Before it, the socket server served exactly
 * one connection to completion — `serve()` owned the world, built it at that
 * client's login and reset it at their disconnect — and JS5 was made to work
 * beside it by forking a child per cache download (a detached thread on
 * Windows). So two clients could not see each other, and the process count
 * grew with the number of tabs.
 *
 * What replaces it is the shape LostCity runs: a poll over every descriptor,
 * work that cannot be done between ticks pushed onto a queue, and the queue
 * drained at the tick boundary.
 *
 *   - **Nothing blocks.** Accepted sockets are non-blocking, the WebSocket
 *     upgrade is a state machine (`ToriRSServer_ConnOpenStep`), and output that
 *     the kernel will not take is queued on the connection rather than waited
 *     on. Any one of these blocking would not stall a client, it would stall
 *     the world and every other player in it.
 *   - **JS5 shares the loop.** It is a session state, not a service, so a
 *     cache download is just another connection here. The fork is gone with
 *     it, which is what makes "single-threaded" true rather than nearly true.
 *   - **The world outlives a session.** It is built by the first login and
 *     stays up; players come and go through `ToriRSServer_World{Add,Remove}Player`
 *     against the pool that was always there (`TORIRSSERVER_PLAYER_MAX` slots).
 *
 * The work queue is what keeps that safe. A login has to build a world, load a
 * save and send a burst; a logout runs `[logout]`, saves, and frees the
 * player's containers. Neither may happen while a tick is mid-flight and both
 * are discovered between ticks, so both are queued and drained at the top of
 * the next tick, in arrival order — the reference's logout and login phases,
 * hoisted to the host because that is where a socket's death is observed.
 */

struct ToriRSServer;
struct ToriRSServerBootConfig;

enum
{
    /**
     * Connections held at once, game and JS5 together.
     *
     * Larger than `TORIRSSERVER_PLAYER_MAX` on purpose: a client holds a JS5
     * connection open for its whole session beside its game one, so a world
     * full of players is already at twice the player count, and a browser that
     * is still priming its cache has one before it has the other. The world
     * refuses the ninth *player* on its own (`ToriRSServer_WorldAddPlayer`);
     * this only bounds descriptors.
     */
    TORIRSSERVER_HOST_CONN_MAX = 24,
};

/**
 * Serve `listener` until the process ends.
 *
 * `srv` must already be a booted world's server struct — the wire chosen,
 * `ToriRSServer_BootLoad` run, shops seeded — because a host does not own any
 * of that; it owns descriptors and the tick. `config` supplies the script pack
 * and the home tile a login builds the world around, and must outlive the call.
 *
 * Returns only on a listener that cannot be polled, which is not a condition
 * any client can cause.
 */
int
ToriRSServer_HostRun(
    struct ToriRSServer* srv,
    const struct ToriRSServerBootConfig* config,
    int listener);

#endif
