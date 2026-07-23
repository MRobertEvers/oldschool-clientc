#ifndef SRC_NET_LOGINPROTO_XRSPS_H
#define SRC_NET_LOGINPROTO_XRSPS_H

/*
 * xrsps login-handshake driver behind the NetLoginVTable seam. Plaintext,
 * WebSocket-framed (the transport strips WS frames), no RSA/ISAAC. Sequence
 * (xrsps-typescript/server/src/network/LoginHandshakeService.ts):
 *   <- welcome(0)                 (may arrive any time; skipped)
 *   -> hello(200)  {client, version}
 *   -> login(204)  {user, pass, revision int = rev->client_version}
 *   <- login_response(3)          {success, errorCode, error, displayName}
 *   -> handshake(202) {name, hasAppearance=0, clientType=0}
 *   == SUCCESS -> GAME; the handshake(2) ack + world burst arrive via the
 *      GAME packet path.
 */

#include "login_vtable.h"

/* The vtable the xrsps233 rev table points its ->login slot at. */
extern struct NetLoginVTable const g_xr_login_vtable;

#endif
