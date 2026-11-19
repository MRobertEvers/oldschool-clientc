#ifndef SRC_NET_NET_KEEPALIVE_H
#define SRC_NET_NET_KEEPALIVE_H

/*
 * When the client has been quiet long enough to owe the server a keepalive.
 *
 * This is an idle timer, not a heartbeat. Every packet that goes out stamps
 * the clock; a keepalive is due once the stamp is `idle_ms` or more behind
 * the frame clock. During normal play something is always going out, so the
 * keepalive never fires; it is what keeps a player standing still from
 * idling the connection out.
 *
 * The stamp is not cleared when a session ends or a new one starts: the
 * first keepalive of a new session is measured from the last packet the
 * previous one sent. Before anything has been sent the stamp is 0.
 *
 * This does not know whether a session is in the game stream, what the
 * keepalive packet is, how long the idle bound is, or what the clock is. The
 * caller passes the frame clock and the bound, sends the packet when it is
 * due, and stamps every send it makes.
 */

#include <stdbool.h>
#include <stdint.h>

struct NetKeepalive
{
    /** Frame clock, in ms, when a packet last went out; 0 = none yet. */
    uint64_t last_send_ms;
};

/** Nothing sent yet. */
void
NetKeepalive_Reset(struct NetKeepalive* keepalive);

/** A packet went out on the frame stamped `now_ms`. */
void
NetKeepalive_NoteSend(
    struct NetKeepalive* keepalive,
    uint64_t now_ms);

/**
 * Whether nothing has gone out for `idle_ms` or more: `now_ms` less the last
 * send, in 64-bit unsigned arithmetic, against the bound.
 */
bool
NetKeepalive_Due(
    struct NetKeepalive const* keepalive,
    uint64_t now_ms,
    uint64_t idle_ms);

#endif /* SRC_NET_NET_KEEPALIVE_H */
