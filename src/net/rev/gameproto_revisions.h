#ifndef SRC_NET_REV_GAMEPROTO_REVISIONS_H
#define SRC_NET_REV_GAMEPROTO_REVISIONS_H

#include <stdint.h>

/*
 * Per-revision protocol tables — THE modularity seam of the net stack.
 * Everything server-build-specific (obfuscated opcodes, payload sizes,
 * client version, archive CRCs) hangs off one of these; the state machines
 * (loginproto, packetbuffer, ToriRS_Network) are revision-agnostic.
 *
 * lc245_2 is the Lost City rev 245_2 build the v0 client targets. A Client-TS
 * 254 server needs an lc254 table filled from its ServerProt/ClientProt —
 * payload sizes carry over, wire opcodes do not.
 */

enum GameProtoRevision
{
    GAMEPROTO_REVISION_INVALID = 0,
    GAMEPROTO_REVISION_LC254 = 1,
    GAMEPROTO_REVISION_LC245_2 = 2,
};

struct GameProtoRevTable
{
    enum GameProtoRevision revision;
    char const* name;
    int client_version;
    /** Login-block CRCs of the 9 jag archives (server-build specific). */
    int32_t jag_checksum[9];
    /** Inbound payload size for a wire opcode: >=0 fixed,
     *  PKTIN_LENGTH_VARU8 (-1) or PKTIN_LENGTH_VARU16 (-2). */
    int (*packetin_size)(int packet_type);
    /** Symbolic inbound name (PKTIN_*) for a wire opcode, 0 = unknown. */
    int (*packetin_code)(int packet_type);
};

struct GameProtoRevTable const*
GameProtoRev_LC245_2(void);

#endif
