#ifndef SRC_NET_REV_GAMEPROTO_REVISIONS_H
#define SRC_NET_REV_GAMEPROTO_REVISIONS_H

#include "pktnames.h"

#include <stdint.h>

/*
 * Per-revision protocol tables — THE modularity seam of the net stack.
 * Everything server-build-specific (obfuscated opcodes, payload sizes,
 * client version, archive CRCs) hangs off one of these; the state machines
 * (loginproto, packetbuffer, ToriRS_Network), the shared parser
 * (gameproto_parse) and the exec layer are revision-agnostic and key on
 * canonical GameProtoPktName / GameProtoPktOutName values.
 *
 * lc245_2 is the Lost City rev 245_2 build the v0 client targets; lc254 is
 * the authoritative LostCity_Server (Engine-TS branch 254) build — payload
 * sizes carry over between them, wire opcodes do not.
 */

#ifndef PKTIN_LENGTH_VARU8
#define PKTIN_LENGTH_VARU8 (-1)
#define PKTIN_LENGTH_VARU16 (-2)
#endif

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
    int (*packetin_size)(int wire_opcode);
    /** Wire opcode -> canonical GameProtoPktName; PKT_NAME_NONE (0) = unknown. */
    int (*packetin_code)(int wire_opcode);
    /** Canonical GameProtoPktName -> wire opcode; <0 = not in this revision.
     *  (Tests and the mock server build rev-independent scripts with this.) */
    int (*packetin_wire)(int pkt_name);
    /** Canonical GameProtoPktOutName -> wire opcode; <0 = not in this rev. */
    int (*packetout_code)(int pkt_out_name);
};

struct GameProtoRevTable const*
GameProtoRev_LC245_2(void);

struct GameProtoRevTable const*
GameProtoRev_LC254(void);

/** Resolve a revision by name ("lc254", "lc245_2"); NULL when unknown. */
struct GameProtoRevTable const*
GameProtoRev_ByName(char const* name);

#endif
