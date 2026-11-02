#ifndef SRC_TORIRSMAPED_TORIRS_MAPED_NET_H
#define SRC_TORIRSMAPED_TORIRS_MAPED_NET_H

/*
 * ToriRSMapEd's loopback front door, as a unit: a listening socket on
 * 127.0.0.1 and the connections it accepted, each one a session of ONE
 * `struct ToriRSMapEd`. Both hosts of the server drive it:
 *
 *   - the daemon (torirs_maped_main.c) polls it in its reactor loop, with a
 *     timeout, forever;
 *   - the embedded server (server=embed, editor_host_remote.c) opens one on
 *     demand -- the first time the window pops a panel out into a second
 *     process -- on an ephemeral port, and polls it with no timeout from the
 *     client's frame loop (app/app_editor_view.c). The popped-out window then
 *     dials it exactly as it would dial the daemon.
 *
 * Raw ToriRSMapEd frames or RFC 6455 on the same port, chosen by the first
 * byte (a browser's connect() is a WebSocket upgrade). One thread; nothing
 * here blocks except the poll's own timeout. A connection that stops reading
 * is dropped at MAPED_SEND_QUEUE_MAX queued bytes.
 *
 * The connections share the server's session table with every other
 * transport of the same server (an embed's in-memory clients): a connection
 * accepted when the table is full is closed at once, never queued.
 *
 * POSIX only. On Windows and the web there is no listener:
 * ToriRSMapEd_ListenerOpen answers NULL and says why.
 */

struct ToriRSMapEd;
struct ToriRSMapEdListener;

/**
 * Bind 127.0.0.1:`port` (0 = an ephemeral port the kernel picks) and listen.
 * `who` prefixes the log lines ("torirsmaped", "editor: embedded maped").
 * NULL when the socket cannot be bound (the reason is logged) or on a
 * platform with no listener.
 */
struct ToriRSMapEdListener*
ToriRSMapEd_ListenerOpen(
    struct ToriRSMapEd* maped,
    int port,
    const char* who);

/** The port it listens on (the ephemeral one, resolved). */
int
ToriRSMapEd_ListenerPort(const struct ToriRSMapEdListener* listener);

/** Connections open now. */
int
ToriRSMapEd_ListenerConnections(const struct ToriRSMapEdListener* listener);

/**
 * Wait up to `timeout_ms` (0 = just look) for the socket or a connection to
 * be ready, then accept, flush and pump whatever is. Returns the number of
 * connections still open, or -1 when the wait itself failed (not EINTR).
 * Does not run ToriRSMapEd_Service: a session's pump runs it, and a host
 * whose connections are idle runs it itself.
 */
int
ToriRSMapEd_ListenerPoll(
    struct ToriRSMapEdListener* listener,
    int timeout_ms);

/** Close every connection and the socket. NULL is a no-op. */
void
ToriRSMapEd_ListenerClose(struct ToriRSMapEdListener* listener);

#endif
