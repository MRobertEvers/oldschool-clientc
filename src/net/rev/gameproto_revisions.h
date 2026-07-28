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
    GAMEPROTO_REVISION_XRSPS233 = 3,
    GAMEPROTO_REVISION_OSRS230 = 4,
};

/** Wire transport a revision speaks. TCP is the classic raw stream; WS is a
 * WebSocket client framing (xrsps). The int lives on the rev table so src/net
 * stays platform-free; the platform layer maps it to a NetTransport impl. */
enum NetTransportKind
{
    NET_TRANSPORT_TCP = 0,
    NET_TRANSPORT_WS = 1,
};

/* Forward decls: the generation-module slots below take pointers only. */
struct RevPacket;
struct PktPlayerInfoOp;
struct PktNpcInfoOp;
struct PktPlayerAppearance;
struct NetLoginVTable;
struct NetOutVTable;

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

    /* --- generation-module extensions (NULL / 0 = classic lc254 behavior) ---
     * lc254 / lc245_2 leave every slot below at its zero default (designated
     * initializers), so their behavior is byte-for-byte unchanged; only revs
     * that diverge from the raw-TCP + ISAAC + classic-bitcodec shape fill them. */

    /** enum NetTransportKind. 0 = raw TCP. */
    int transport_kind;
    /** 1 = opcode byte is plaintext (no ISAAC subtract). 0 = ISAAC-scrambled. */
    int opcode_plaintext;
    /** Server tick in ms (xrsps 600); 0 = revision has no explicit tick clock. */
    int server_tick_ms;

    /* --- NPC_INFO new-entity field widths (0 = classic) ---------------------
     * The classic npc stream keeps its shape across revisions but widens
     * individual fields as the game's id space grows, and a width that is too
     * narrow does not fail — it truncates. An OSRS-era npc id of 3106 written
     * into an 11-bit field arrives as 1058, a perfectly valid *different* npc.
     * Stating the widths per revision is what keeps that drift visible; see
     * docs/osrs230_mockserver.md. */

    /**
     * Bytes an interface component id occupies in a client->server packet.
     *
     * 0 = classic 2, a flat component id. rev 230 addresses a component as a
     * packed (interface << 16) | child uid, which needs 4 — pushing that
     * through a 2-byte field truncates the interface half away, so the server
     * cannot tell 231:5 from 217:5 and a resume button can never be matched.
     */
    int component_id_bytes;

    /** Bits in the new-npc record's slot field. The all-ones value is the
     *  section terminator, so this sets that too. 0 = classic 14. */
    int npc_slot_bits;
    /** Bits in the new-npc record's npc-type field. 0 = classic 11, i.e. a
     *  maximum id of 2047 — far below the OSRS-era npc count. */
    int npc_type_bits;

    /** Login handshake driver. NULL = the classic loginproto.c state machine. */
    struct NetLoginVTable const* login;

    /** Per-rev packet parse override; NULL = shared gameproto_parse. A non-NULL
     * hook may return <0 to signal "not mine, fall back to gameproto_parse". */
    int (*parse)(
        struct GameProtoRevTable const* rev,
        int pkt_name,
        uint8_t const* data,
        int len,
        struct RevPacket* out);

    /** Modern entity-info readers emitting the shared op list. NULL = classic
     * bitcodec (pkt_player_info.c / pkt_npc_info.c). Wired in Phase 3. */
    int (*player_info_read)(uint8_t const* data, int len, struct PktPlayerInfoOp* ops, int cap);
    int (*npc_info_read)(uint8_t const* data, int len, struct PktNpcInfoOp* ops, int cap);
    /** Per-rev appearance-block decode. NULL = 254-era PktPlayerAppearance_Decode. */
    int (*appearance_decode)(uint8_t const* data, int len, struct PktPlayerAppearance* out);

    /** Resolve the scene base from a REBUILD packet. NULL = the (zone-6)*8
     * old-gen local-origin rule. Wired in Phase 2. */
    int (*scene_base)(struct RevPacket const* pkt, int* base_x, int* base_z);

    /** Outbound interaction serializer. NULL = net_out.c classic builders. */
    struct NetOutVTable const* out_vt;
};

struct GameProtoRevTable const*
GameProtoRev_LC245_2(void);

struct GameProtoRevTable const*
GameProtoRev_LC254(void);

struct GameProtoRevTable const*
GameProtoRev_XRSPS233(void);

struct GameProtoRevTable const*
GameProtoRev_OSRS230(void);

/** Resolve a revision by name ("lc254", "lc245_2", "xrsps233", "osrs230"); NULL when unknown. */
struct GameProtoRevTable const*
GameProtoRev_ByName(char const* name);

/* Boot-time login-parameter injection from a boot manifest. The tables are
 * static non-const singletons; these cast the const away by design and MUST be
 * called before ToriRS_Network_Init (nothing reads these fields until login).
 * The env fallbacks (TORIRS_JAG_CRC in GameProtoRev_LC254, etc.) still take
 * precedence — the app only calls these when the matching env is absent. */
void
GameProtoRev_SetJagChecksums(struct GameProtoRevTable const* rev, int32_t const crc[9]);

void
GameProtoRev_SetClientVersion(struct GameProtoRevTable const* rev, int version);

#endif
