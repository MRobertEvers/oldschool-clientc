#ifndef SRC_NET_LOGINPROTO_OSRS239_H
#define SRC_NET_LOGINPROTO_OSRS239_H

/*
 * Revision-239 login driver (client side).
 *
 * Differences from the osrs230 driver next door, all of them required by a real
 * OldSchool server rather than by our own:
 *
 *   - the header carries `serverVersion`, added at revision 237;
 *   - the RSA block carries an OTP discriminator before the auth type;
 *   - everything after the RSA envelope is XTEA-encrypted under the same seeds,
 *     and carries the username, window state, host platform stats and 23
 *     archive CRCs. The 230 driver stops at the password and sends no body at
 *     all, which our own server accepts because it is the only thing it talks
 *     to;
 *   - the server may interpose a proof-of-work challenge before answering.
 *
 * See src/net/rev/osrs239/loginblock.h for the byte layout, which the mock
 * server's decoder reads from the same file.
 */

#include "login_vtable.h"

extern struct NetLoginVTable const g_osrs239_login_vtable;

#endif
