#include "net/net_link_watch.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

void
NetLinkWatch_Reset(struct NetLinkWatch* watch)
{
    assert(watch);
    memset(watch, 0, sizeof(*watch));
}

void
NetLinkWatch_NotePacket(
    struct NetLinkWatch* watch,
    uint64_t now_ms)
{
    assert(watch);
    watch->last_recv_ms = now_ms;
    if( !watch->first_recv_ms )
        watch->first_recv_ms = now_ms;
}

bool
NetLinkWatch_Lost(struct NetLinkWatch const* watch)
{
    assert(watch);
    return watch->lost != 0;
}

bool
NetLinkWatch_GaveUp(struct NetLinkWatch const* watch)
{
    assert(watch);
    return watch->reconnect_failed != 0;
}

void
NetLinkWatch_NoteReconnectRefused(struct NetLinkWatch* watch)
{
    assert(watch);
    watch->reconnect_failed = 1;
}

/*
 * Declare the session dead.
 *
 * Idempotent, because every detector can fire in the same frame as another --
 * a stalled tab both misses packets and reports a huge frame gap -- and the
 * first one to arrive owns the transition.
 */
static enum NetLinkAction
net_link_lost(
    struct NetLinkWatch* watch,
    char const* why,
    char const** out_reason)
{
    if( watch->lost )
        return NET_LINK_IDLE;

    watch->lost = 1;
    watch->reconnect_attempts = 0;
    watch->reconnect_failed = 0;
    /* Immediately: the first attempt is the one most likely to work, and the
     * delay exists to space out RETRIES. */
    watch->reconnect_at_ms = 0;
    if( out_reason )
        *out_reason = why;
    return NET_LINK_LOST;
}

bool
NetLinkWatch_Drop(struct NetLinkWatch* watch)
{
    assert(watch);
    return net_link_lost(watch, NULL, NULL) == NET_LINK_LOST;
}

enum NetLinkAction
NetLinkWatch_Step(
    struct NetLinkWatch* watch,
    struct NetLinkSighting const* seen,
    char const** out_reason)
{
    assert(watch);
    assert(seen);

    if( !watch->lost )
    {
        /*
         * Every detector here is gated on having heard from the server at
         * least once. A boot frame can legitimately run long -- a cold cache,
         * a browser IO round trip -- and there is no session to lose yet.
         */
        if( !watch->last_recv_ms )
            return NET_LINK_IDLE;

        if( seen->drop_requested )
            return net_link_lost(watch, "drop requested", out_reason);

        /* 1. This process stopped running. */
        if( seen->frame_gap_ms >= (uint64_t)NET_LINK_STALL_MS )
            return net_link_lost(watch, "client was not running", out_reason);

        /*
         * 2. The server stopped speaking -- in the game stream only.
         *
         * Measured only forwards. These are unsigned milliseconds, so a frame
         * stamped EARLIER than the last packet subtracts to something near
         * 2^64 and reads as a silence of half a billion years -- the timeout
         * fires instantly on a session that has just been spoken to. A clock
         * that went backwards is not evidence of anything about the server,
         * and the stall detector above is already handed its gap with the same
         * ordering test applied (app_net_link_watch); this is that test, said
         * about the other detector.
         */
        if( seen->in_game && seen->now_ms > watch->last_recv_ms &&
            seen->now_ms - watch->last_recv_ms >= (uint64_t)NET_LINK_TIMEOUT_MS )
            return net_link_lost(watch, "no packets for 15s", out_reason);

        /* 3. The transport says the socket is gone. */
        if( seen->socket_closed )
            return net_link_lost(watch, "socket closed", out_reason);

        return NET_LINK_IDLE;
    }

    /* Re-established: the handshake reached the game stream again. */
    if( seen->in_game )
    {
        watch->lost = 0;
        watch->reconnect_attempts = 0;
        /* Stamped rather than left where it was, or the first frame back would
         * measure its silence from before the outage and drop the session it
         * has only just recovered. */
        watch->last_recv_ms = seen->now_ms;
        return NET_LINK_REESTABLISHED;
    }

    /* An attempt is still in flight while the login machine runs; only a
     * machine that fell back to disconnected has failed. */
    if( seen->logging_in )
        return NET_LINK_IDLE;
    if( watch->reconnect_failed )
        return NET_LINK_IDLE;
    if( seen->now_ms < watch->reconnect_at_ms )
        return NET_LINK_IDLE;

    if( watch->reconnect_attempts >= NET_LINK_RECONNECT_MAX_ATTEMPTS )
    {
        watch->reconnect_failed = 1;
        return NET_LINK_GAVE_UP;
    }

    watch->reconnect_attempts++;
    watch->reconnect_at_ms = seen->now_ms + (uint64_t)NET_LINK_RECONNECT_DELAY_MS;
    return NET_LINK_RECONNECT;
}
