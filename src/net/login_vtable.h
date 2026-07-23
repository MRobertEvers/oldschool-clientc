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
    void (*free_)(void* handle);
};

#endif
