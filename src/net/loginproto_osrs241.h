#ifndef SRC_NET_LOGINPROTO_OSRS241_H
#define SRC_NET_LOGINPROTO_OSRS241_H

/*
 * Revision-241 login driver (client side) -- a COPY of loginproto_osrs239.c.
 *
 * Nothing in the block moved between 239 and 241: RSProx's ClientLoginHandler
 * (proxy/.../client/ClientLoginHandler.kt) decodes one header, one RSA block
 * and one XTEA body for every OldSchool revision past the 232 serverVersion
 * field, and its LoginResponse.Ok read is the same 34 bytes. The version
 * written is `rev->client_version`, 241. The archive CRC block is opaque to
 * RSProx, so its 241 order is not stated anywhere this project can read; it is
 * 239's (rev/osrs241/loginblock.h), which this server does not verify.
 *
 * What follows is the 239 driver's own description, still true here:
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
 * See src/net/rev/osrs241/loginblock.h for the byte layout, which the mock
 * server's decoder reads from the same file.
 */

#include "login_vtable.h"

extern struct NetLoginVTable const g_osrs241_login_vtable;

#endif
