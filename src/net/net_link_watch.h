#ifndef SRC_NET_NET_LINK_WATCH_H
#define SRC_NET_NET_LINK_WATCH_H

/*
 * When a session is dead, when to dial again, and when to stop.
 *
 * Three different things can end a session and they are not interchangeable,
 * which is the whole reason this is a policy rather than a timeout:
 *
 *   - The SERVER went quiet. Fifteen seconds without a packet, the reference's
 *     own bound, and only in the game stream -- the login handshake
 *     legitimately sits silent while a proof-of-work is solved.
 *   - THIS CLIENT stopped running. A hidden browser tab, a suspended machine:
 *     the process was not scheduled while the server kept writing, so what is
 *     queued behind the socket is a backlog to abandon rather than a stream to
 *     replay. Coming back and replaying it is what made a returning tab spend
 *     seconds fast-forwarding with input ignored.
 *   - The SOCKET is gone, and the transport says so outright.
 *
 * None of them is observable after the fact. A session that ends and comes
 * back looks, from the outside, exactly like a session that was never lost,
 * and a session that ends and does NOT come back looks like a slow server.
 * That is what this is tested for.
 *
 * Nothing here opens a socket or reads one. The caller reports what it saw and
 * performs whatever comes back, which is what lets the policy be run against a
 * clock rather than against a network.
 */

#include <stdbool.h>
#include <stdint.h>

/*
 * The thresholds, which are two different questions and two different numbers.
 *
 * TIMEOUT is the reference's: Client-TS gives up 15s after the last packet
 * (Client.ts:2443), and a server sends often enough that a healthy link never
 * comes close.
 *
 * STALL is not "has the server gone quiet" but "was this client running", and
 * the quantity it tests is the PREVIOUS FRAME'S WHOLE WALL DURATION -- work
 * included -- not the time the process spent descheduled. That conflation is
 * harmless where a frame is always short, and was not harmless on the Windows
 * XP lane: at the earlier value of 4000 a client working flat out concluded
 * twelve times in a thousand frames that it had not been running, and dropped
 * a healthy session each time. The worst legitimate frame measured there --
 * hydrating a sparse cache over JS5 while rebuilding the world -- was 11.04s
 * (docs/winxp_profiles/new-run1.csv). 30000 clears it by ~2.7x while staying
 * far below any real suspend, which is minutes.
 *
 * The retry figures: the reference retries once and falls back to the login
 * screen. A browser client that a phone backgrounded deserves more than one
 * try, but not an unbounded loop against a server that is gone.
 */
enum
{
    NET_LINK_TIMEOUT_MS = 15000,
    NET_LINK_STALL_MS = 30000,
    NET_LINK_RECONNECT_DELAY_MS = 2000,
    NET_LINK_RECONNECT_MAX_ATTEMPTS = 5,
};

/** Everything the policy remembers between frames. */
struct NetLinkWatch
{
    /** Wall clock when a server packet last arrived; 0 = never, this session. */
    uint64_t last_recv_ms;
    /** Wall clock when the first packet of this session arrived -- the origin
     *  the drop test hook measures from. */
    uint64_t first_recv_ms;
    /** Wall clock at which the next attempt may be made. */
    uint64_t reconnect_at_ms;
    /** Non-zero while the connection is gone and being re-established. */
    int lost;
    /** Attempts made since it was lost. */
    int reconnect_attempts;
    /** Set once the attempts are exhausted: lost, and not coming back. */
    int reconnect_failed;
};

/** What the caller saw this frame. Nothing here is read from a socket by the
 *  policy; every one of these is the caller's observation of one. */
struct NetLinkSighting
{
    uint64_t now_ms;
    /**
     * The previous frame's whole wall duration, 0 before the first frame.
     * See NET_LINK_STALL_MS: this is not the time the process spent
     * descheduled, and the threshold is sized for that.
     */
    uint64_t frame_gap_ms;
    /** The handshake has reached the game stream. */
    bool in_game;
    /** An attempt is still in flight -- the login machine is running. */
    bool logging_in;
    /** The transport says the socket is closed or failed AND the session
     *  machine has fallen back to disconnected. Before a session was ever up,
     *  that is merely the initial state and means nothing. */
    bool socket_closed;
    /** A harness asked for the link to be severed now. The headless
     *  equivalent of the reference's `::clientdrop`, because a harness has no
     *  chat box to type into and this path is otherwise reached only by
     *  genuinely losing a socket. */
    bool drop_requested;
};

/** What the caller has to go and do. */
enum NetLinkAction
{
    /** Nothing. A healthy link, or a retry that is not due yet. */
    NET_LINK_IDLE = 0,
    /** The session is dead: tear it down and start trying to get it back. */
    NET_LINK_LOST,
    /** It came back. */
    NET_LINK_REESTABLISHED,
    /** Dial again now. */
    NET_LINK_RECONNECT,
    /** Out of attempts. Say so; nothing else will. */
    NET_LINK_GAVE_UP,
};

/**
 * Arm the watch for a session that has not been heard from yet.
 *
 * Also what a deliberate logout needs. Every detector below is armed off
 * `last_recv_ms` -- a session that WAS heard from and then went quiet -- so a
 * socket the client closed on purpose would otherwise read as a connection
 * that was lost, and the client would spend its way back to the title screen
 * redialling the world the player just left.
 */
void
NetLinkWatch_Reset(struct NetLinkWatch* watch);

/** A packet arrived. */
void
NetLinkWatch_NotePacket(
    struct NetLinkWatch* watch,
    uint64_t now_ms);

/**
 * Advance the policy one frame and say what to do about it.
 *
 * `out_reason` is filled for NET_LINK_LOST with a phrase naming which detector
 * fired, because the three are not interchangeable and a log that does not say
 * which one it was cannot be read afterwards. Untouched for every other
 * action.
 *
 * Call it once a frame, AHEAD of the logic ticks: a frame that decides the
 * backlog is stale must not first spend five ticks draining it.
 */
enum NetLinkAction
NetLinkWatch_Step(
    struct NetLinkWatch* watch,
    struct NetLinkSighting const* seen,
    char const** out_reason);

/**
 * Declare the session dead from outside the watch, and say whether that was
 * news. False when it was already lost, so the caller's teardown runs once.
 *
 * For a drop that arrives between frames rather than being observed by one:
 * the reference keeps `::clientdrop` for itself, which severs the connection
 * locally so this whole path can be exercised on demand.
 */
bool
NetLinkWatch_Drop(struct NetLinkWatch* watch);

/**
 * The dial the last NET_LINK_RECONNECT asked for could not even be started.
 *
 * Distinct from an attempt that fails later: this one never reached the login
 * machine, so there is nothing in flight to wait for and no point counting
 * down to another try.
 */
void
NetLinkWatch_NoteReconnectRefused(struct NetLinkWatch* watch);

/** Lost and still trying. */
bool
NetLinkWatch_Lost(struct NetLinkWatch const* watch);

/** Lost, out of attempts, and not coming back -- what the connection-lost
 *  overlay says out loud. */
bool
NetLinkWatch_GaveUp(struct NetLinkWatch const* watch);

#endif /* SRC_NET_NET_LINK_WATCH_H */
