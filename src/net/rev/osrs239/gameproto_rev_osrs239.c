#include "net/rev/gameproto_revisions.h"
#include "net/rev/packets/pkt_npc_info.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "net/rev/packets/pkt_player_info.h"
#include "net/rev/revpacket.h"
#include "packetin.h"
#include "packetout.h"

#include "net/loginproto_osrs239.h"

#include <stdint.h>

/* osrs239_entity_info.c -- the v5 entity streams. */
int
osrs239_player_info_read(uint8_t const* data, int len, struct PktPlayerInfoOp* ops, int cap);

int
osrs239_npc_info_read(uint8_t const* data, int len, struct PktNpcInfoOp* ops, int cap);

/* osrs239_parse.c */
int
osrs239_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out);

static int
rev_packetin_size(int wire_opcode)
{
    return packetin_size_osrs239(wire_opcode);
}

static int
rev_packetin_code(int wire_opcode)
{
    return packetin_code_osrs239(wire_opcode);
}

static int
rev_packetin_wire(int pkt_name)
{
    return packetin_wire_osrs239(pkt_name);
}

static int
rev_packetout_code(int pkt_out_name)
{
    return packetout_code_osrs239(pkt_out_name);
}

/* The appearance block moved shape at this revision (two slot arrays, a
 * different worn-obj tag, a string name, five trailing fields) — see
 * `APPEARANCE_ENC_V5` in pkt_player_appearance.h. Everything downstream of the
 * decode is unchanged: the reader emits the same command stream the classic
 * block does. */
static int
rev_appearance_decode(
    uint8_t const* data,
    int len,
    struct PktPlayerAppearance* out)
{
    return PktPlayerAppearance_DecodeAs(out, APPEARANCE_ENC_V5, data, len);
}

/*
 * OSRS revision 239 — the live OldSchool protocol, transcribed from RSProt
 * rather than assigned.
 *
 * This is the first revision table in the tree whose every number is real. The
 * osrs230 table beside it is a deliberate hybrid: about a third of its opcodes
 * were assigned by this project because RSProt's tables were not vendored, and
 * its PLAYER_INFO / NPC_INFO opcodes are bound to the classic lc254 bitstreams
 * instead of the v5 streams the revision actually carries. That pairing is
 * self-consistent and it is what the whole regression suite runs on, so it
 * stays; but it can only ever talk to this project's own server. This table can
 * talk to an OldSchool client, which is the entire reason it exists.
 *
 * Three things differ from 230 and each one is load-bearing:
 *
 *  - Opcodes reach 148, and anything >= 0x80 is written as TWO cipher bytes
 *    (RSProt's pSmart1Or2Enc). `opcode_smart2` turns that on in the framer. A
 *    revision that needs it and does not set it does not lose one packet, it
 *    loses the stream: the second byte is read as the next opcode.
 *
 *  - The login block is the real one — RSA-enveloped seeds and session id, then
 *    an XTEA-encrypted body carrying the username, the window state, the host
 *    platform stats and 23 archive CRCs. 230's block here stops after the
 *    username and password.
 *
 *  - There is no UPDATE_PID and no P_COUNTDIALOG. The local player's index
 *    arrives in the login response (LoginResponse.Ok carries it), and the
 *    count dialog is a clientscript. Both were assigned opcodes at 230 because
 *    the mock needed them; at 239 the honest answer is that they do not exist.
 */
static struct GameProtoRevTable k_rev_osrs239 = {
    .revision = GAMEPROTO_REVISION_OSRS239,
    .name = "osrs239",
    .client_version = 239,
    .jag_checksum = { 0 },
    .packetin_size = rev_packetin_size,
    .packetin_code = rev_packetin_code,
    .packetin_wire = rev_packetin_wire,
    .packetout_code = rev_packetout_code,

    .transport_kind = NET_TRANSPORT_TCP,
    .opcode_plaintext = 0, /* ISAAC-scrambled opcodes */
    .opcode_smart2 = 1,    /* ...and two of them once the opcode reaches 0x80 */
    .server_tick_ms = 600,
    .component_id_bytes = 4,
    /* These compatibility dimensions belong to the shared/classic reader.
     * OSRS239 uses the dedicated v5 reader below: a 16-bit per-client index,
     * a 14-bit initial type, and mask 0x1's transformed unsigned-16-bit type
     * replacement for ids above 0x3fff. */
    .npc_slot_bits = 14,
    .npc_type_bits = 14,
    .login = &g_osrs239_login_vtable,
    .parse = osrs239_parse,
    .appearance_decode = rev_appearance_decode,
    /*
     * PLAYER_INFO and NPC_INFO are a different CODEC at this revision, not a
     * different field order, which is why they are whole readers rather than
     * `case`s in the parse. The classic readers stay for every other revision.
     */
    .player_info_read = osrs239_player_info_read,
    .npc_info_read = osrs239_npc_info_read,
};

struct GameProtoRevTable const*
GameProtoRev_OSRS239(void)
{
    return &k_rev_osrs239;
}
