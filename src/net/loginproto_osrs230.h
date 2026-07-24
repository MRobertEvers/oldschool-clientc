#ifndef SRC_NET_LOGINPROTO_OSRS230_H
#define SRC_NET_LOGINPROTO_OSRS230_H

/*
 * OSRS revision 230 login-handshake driver (NetLoginVTable seam). Classic OSRS
 * transport (raw TCP + ISAAC + RSA), modern-230 login block per RSProt
 * (LoginBlockDecoder / GameLoginDecoder):
 *   -> op 14 (INIT_GAME_CONNECTION)
 *   <- [status 0x00][sessionId i64]           (9 bytes, plaintext)
 *   -> op 16 (GAMELOGIN, var-short): header{ p4 version=230, p4 subVersion,
 *        p1 clientType, p1 platformType, p1 externalAuth } + p2 rsaSize +
 *        RSA{ p1 encCheck=1, 4x p4 seed, p8 sessionId, p1 authType=0,
 *             gjstr username, gjstr password }
 *   <- [0x02] success                          (plaintext), then GAME (ISAAC)
 *
 * ISAAC: out-cipher (client->server) = seed; in-cipher (server->client) =
 * seed+50 — matching the mock/server. Simplified vs full RSProt (no XTEA block,
 * OTP, UUID, CRCs, host stats): the mock decodes exactly this block.
 */

#include "login_vtable.h"

extern struct NetLoginVTable const g_osrs230_login_vtable;

#endif
