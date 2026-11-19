#ifndef SRC_NET_SESSION_END_H
#define SRC_NET_SESSION_END_H

/*
 * How a session the player or the server chose to end gets ended: a logout
 * the player asked for, and a move to another world the server ordered.
 *
 * A logout is a request, not an ending. The click raises it; the next logic
 * tick drains it, after the button's IF_BUTTON is already in the outbound
 * ring, and arms a wait for the server's answer. The wait has no end of its
 * own: nothing counts it down, so a logout the server refuses leaves the
 * player in the world they are still playing. What ends it is the server --
 * its LOGOUT packet, or the socket it closes instead -- and a closed socket
 * while a logout is waiting, or still undrained, is that answer rather than a
 * lost connection. With no session in the game stream there is nobody to
 * wait for, and the request is the ending.
 *
 * A LOGOUT_TRANSFER packet names the world the session moves to. It is parked
 * here and taken once, at the head of the next frame, so the session is not
 * torn down in the middle of the packet pipeline that is still running it.
 *
 * Nothing here opens, reads or closes a socket, and nothing here prints. The
 * caller says whether a session is up, performs the action that comes back
 * (ending the session, closing it to log in to the hop's world), and logs
 * what it did.
 */

#include <stdbool.h>

/**
 * The value the wait is armed with when a logout is requested.
 *
 * The rev-239 client's logoutTimer value: its CS2 logout op (5630) and CS1
 * clientCode 205 both store 250. It is NOT counted down, there or here: the
 * rev-239 client has no decrement of that field, and the session ends only
 * when the server ends it (a LOGOUT packet, or the socket it closes).
 */
#define SESSION_END_LOGOUT_WAIT_CYCLES 250

struct SessionEnd
{
    /**
     * The player asked to leave, and the request has not been acted on yet.
     *
     * Deferred rather than done at the click, so the button's own IF_BUTTON is
     * already in the outbound ring when the wait below is armed behind it --
     * the server hears the request instead of a bare FIN. Raised by the CS1
     * logout clientCode and by the CS2 host's LOGOUT opcode, drained by
     * SessionEnd_Step.
     */
    int logout_requested;
    /**
     * Logic cycles left to wait for the server to answer that request.
     *
     * Non-zero means the request is on the wire and the session has NOT ended:
     * the world is still up, the player is still in it, and the screen has not
     * changed. It stays armed until the session ends: nothing counts it down
     * (@see SESSION_END_LOGOUT_WAIT_CYCLES).
     */
    int logout_wait_cycles;
    /** A LOGOUT_TRANSFER executed and has not been taken yet. */
    int hop_pending;
    /** The world it moves the session to; meaningful while hop_pending. */
    int hop_world;
};

/** What the caller has to go and do after SessionEnd_Step. */
enum SessionEndAction
{
    /** Nothing: no request, or a request while the wait is already armed. */
    SESSION_END_IDLE = 0,
    /** The request was drained and the wait for the server's answer armed. */
    SESSION_END_WAIT_ARMED,
    /** No session to answer the request: end it now. */
    SESSION_END_LOGOUT,
};

/** No request, no wait, no hop. What a session that ended leaves behind. */
void
SessionEnd_Reset(struct SessionEnd* end);

/** The player asked to leave. Drained by the next SessionEnd_Step. */
void
SessionEnd_RequestLogout(struct SessionEnd* end);

/**
 * Drain the logout request, once per logic tick, and say what to do about it.
 *
 * `session_up` is whether a session in the game stream is there to answer
 * the request. A second request while the wait is armed changes nothing: its
 * IF_BUTTON goes out beside the first one's, and the wait stays armed.
 */
enum SessionEndAction
SessionEnd_Step(
    struct SessionEnd* end,
    bool session_up);

/**
 * The player has asked to leave and the session has not ended: a wait is
 * armed, or the request is still undrained. A closed socket now is the
 * server's answer, not a lost connection.
 */
bool
SessionEnd_LogoutPending(struct SessionEnd const* end);

/** A LOGOUT_TRANSFER moves the session to `world`. A later one replaces it. */
void
SessionEnd_Hop(
    struct SessionEnd* end,
    int world);

/**
 * Take the parked hop, once. True and `*out_world` filled when there was one;
 * false and `*out_world` untouched when there was not.
 */
bool
SessionEnd_TakeHop(
    struct SessionEnd* end,
    int* out_world);

#endif /* SRC_NET_SESSION_END_H */
