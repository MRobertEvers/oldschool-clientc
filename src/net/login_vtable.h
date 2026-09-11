#ifndef SRC_NET_LOGIN_VTABLE_H
#define SRC_NET_LOGIN_VTABLE_H

/*
 * Pluggable login-handshake driver. When a revision table sets `->login`, net.c
 * drives the handshake through this vtable instead of the classic loginproto.c
 * state machine (raw TCP + RSA + ISAAC). The opaque handle owns whatever state
 * that generation's handshake needs.
 *
 * Return codes mirror loginproto.h so net.c's drive loop is generation-blind:
 * LOGINPROTO_SUCCESS / LOGINPROTO_ERROR / LOGINPROTO_AWAIT_RECV / _AWAIT_SEND.
 */

#include <stdint.h>

struct ToriRS_Network;

struct NetLoginVTable
{
    /** Create a handshake handle for this connection, reading what it needs
     * (rev->client_version, username/password, seed fn) off `net`. */
    void* (*new_)(struct ToriRS_Network* net, char const* username, char const* password);
    /** Feed received bytes (up to what the handshake currently awaits).
     * Returns bytes consumed. */
    int (*recv)(void* handle, uint8_t const* data, int size);
    /** Drain pending outbound bytes. Returns bytes written. */
    int (*send)(void* handle, uint8_t* out, int out_size);
    /** Advance the machine. Returns a loginproto.h status code. */
    int (*poll)(void* handle);
    /**
     * The local player's index, once the handshake has succeeded, or -1.
     *
     * Revision 239 has no UPDATE_PID packet: the client learns which slot is
     * itself from the login response and nowhere else, and the PLAYER_INFO v5
     * stream is keyed on that index. A revision whose handshake does not state
     * it leaves this NULL and keeps using UPDATE_PID.
     */
    int (*local_index)(void* handle);
    /**
     * The player-info init block a reconnect response carried, or NULL.
     *
     * RECONNECT_OK is not a bare verdict: it carries the block that a fresh
     * login receives inside REBUILD_LOGIN, because a re-established session
     * gets no rebuild of its own (RSProt's ReconnectOkResponseEncoder writes
     * the caller's buffer; the deob reads it at gameState 40 and hands it
     * straight to the player-info reader). Applying it is the caller's job —
     * it has to happen after the local index is restated, and the driver is
     * freed before that. Revisions without a reconnect leave this NULL.
     */
    uint8_t const* (*reconnect_block)(void* handle, int* out_len);
    /**
     * The server's own rejection byte, or -1 if it has not answered one.
     *
     * The code is protocol; the sentence a player reads for it is the
     * profile's ([login_reply:<code>]). Without this the caller has only
     * the generic loginproto's field, which these revisions do not use --
     * so every rejection they receive, whatever the server said, reaches
     * the login screen as "error connecting to server".
     *
     * A revision that surfaces no code leaves this NULL.
     */
    int (*reply_code)(void* handle);
    void (*free_)(void* handle);
};

#endif
