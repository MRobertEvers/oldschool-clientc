#ifndef SRC_NET_LOGIN_LOGIN_OSRS_H
#define SRC_NET_LOGIN_LOGIN_OSRS_H

/*
 * The OSRS login driver (client side): RSProx's ClientLoginHandler block,
 * which every OldSchool revision past the 232 serverVersion field writes the
 * same -- one header, one RSA block, one XTEA body -- and whose LoginResponse.Ok
 * read is the same 34 bytes. The version written is the revision table's
 * `client_version`. Each supported revision names this driver as its login
 * (src/net/rev/revisions.ini, `login = osrs`).
 *
 * What it carries that a LostCity login (login_lc.c) does not, all of it
 * required by a real OldSchool server:
 *
 *   - the header carries `serverVersion`;
 *   - the RSA block carries an OTP discriminator before the auth type;
 *   - everything after the RSA envelope is XTEA-encrypted under the same seeds,
 *     and carries the username, window state, host platform stats and 23
 *     archive CRCs. The CRC block is opaque to RSProx, so its order past 239 is
 *     not stated anywhere this project can read; it is 239's, which our server
 *     does not verify;
 *   - the server may interpose a proof-of-work challenge before answering.
 *
 * The byte layout is net/login/login_osrs_block.h, which the server's decoder
 * reads from the same file.
 */

#include "login_vtable.h"

extern struct NetLoginVTable const g_login_osrs_vtable;

#endif
