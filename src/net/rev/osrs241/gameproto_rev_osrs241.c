#include "net/rev/gameproto_revisions.h"
#include "streams/rsprot_info.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "net/rev/revpacket.h"
#include "packetin.h"
#include "packetout.h"

#include "net/loginproto_osrs241.h"

#include <stddef.h>
#include <stdint.h>

/* osrs241_entity_info.c -- the streams' readers over the session's table. */
int
osrs241_player_info_read(uint8_t const* data, int len, struct RsprotInfoList* out);
int
osrs241_npc_info_read(uint8_t const* data, int len, struct RsprotInfoList* out);
int
osrs241_npc_info_read_large(uint8_t const* data, int len, struct RsprotInfoList* out);

void
osrs241_playerinfo_set_local(int local_index);
void
osrs241_playerinfo_init(uint8_t const* data, int len);
size_t
osrs241_playerinfo_state_size(void);
void
osrs241_playerinfo_state_save(void* out);
void
osrs241_playerinfo_state_load(void const* in);

/* osrs241_parse.c */
int
osrs241_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out);

static int
rev_packetin_size(int wire_opcode)
{
    return packetin_size_osrs241(wire_opcode);
}

static int
rev_packetin_code(int wire_opcode)
{
    return packetin_code_osrs241(wire_opcode);
}

static int
rev_packetin_wire(int pkt_name)
{
    return packetin_wire_osrs241(pkt_name);
}

static int
rev_packetout_code(int pkt_out_name)
{
    return packetout_code_osrs241(pkt_out_name);
}

/* 239's v5 appearance block, except the worn-obj customisation, which is
 * self-delimiting at 241 -- see `APPEARANCE_ENC_V6` in pkt_player_appearance.h. */
static int
rev_appearance_decode(
    uint8_t const* data,
    int len,
    struct PktPlayerAppearance* out)
{
    return PktPlayerAppearance_DecodeAs(out, APPEARANCE_ENC_V6, data, len);
}

/*
 * OSRS revision 241 — a COPY of the osrs239 table (gameproto_rev_osrs239.c),
 * changed to 241's wire and nothing else. Everything said there about why the
 * 239 table exists holds here; what moved between the two is stated in
 * RSProx's osrs-239 / osrs-240 / osrs-241 decoders, which are this module's
 * source (RSProt never vendored 240+):
 *
 *  - Every opcode was renumbered, server and client side. packetin.h /
 *    packetout.h / zoneprot.h are generated from RSProt's 241 table
 *    (tools/rsprot_gen_rev.py 241; 3rd/rsprot/spec/tables/osrs241.ini, which
 *    RSProt ported from RSProx).
 *
 *  - Most fixed-layout packets reshuffled their field order and byte
 *    transforms. The generated codecs carry a 241 row for each
 *    (3rd/rsprot/spec/packets); the client -> server op packets are one
 *    table both ends run (clientprot.h).
 *
 *  - CAM_MOVETO_V2 / CAM_LOOKAT_V2 were deleted; the canonical packets are the
 *    V3 prots (one extra height-relative byte). NPC_INFO is NPC_INFO_*_V6, a
 *    new low-resolution add record. PLAYER_INFO's extended-info flags were
 *    renumbered and reordered, and the worn-obj customisation block changed.
 *
 *  - The login block is unchanged (RSProx's ClientLoginHandler has no revision
 *    branch past the 232 serverVersion field); the version is 241.
 */
static struct GameProtoRevTable k_rev_osrs241 = {
    .revision = GAMEPROTO_REVISION_OSRS241,
    .name = "osrs241",
    .client_version = 241,
    .jag_checksum = { 0 },
    .packetin_size = rev_packetin_size,
    .packetin_code = rev_packetin_code,
    .packetin_wire = rev_packetin_wire,
    .packetout_code = rev_packetout_code,
    .packetout_size = osrs241_packetout_size,

    .transport_kind = NET_TRANSPORT_TCP,
    .opcode_plaintext = 0, /* ISAAC-scrambled opcodes */
    .opcode_smart2 = 1,    /* ...and two of them once the opcode reaches 0x80 */
    .server_tick_ms = 600,
    .component_id_bytes = 4,
    .login = &g_osrs241_login_vtable,
    /* RSProt separates the two decoders: GAMERECONNECT's authentication
     * section is the previous session's cipher seed, and the answer is
     * RECONNECT_OK with the player-info init block. */
    .reconnect_kind = NET_RECONNECT_SEED,
    .parse = osrs241_parse,
    .appearance_decode = rev_appearance_decode,
    /*
     * PLAYER_INFO and NPC_INFO are a different CODEC at this revision, not a
     * different field order, which is why they are whole readers rather than
     * `case`s in the parse. The classic readers stay for every other revision.
     */
    .player_info_read = osrs241_player_info_read,
    .npc_info_read = osrs241_npc_info_read,
    .npc_info_read_large = osrs241_npc_info_read_large,
    .player_info_set_local = osrs241_playerinfo_set_local,
    .player_info_init = osrs241_playerinfo_init,
    .player_info_state_size = osrs241_playerinfo_state_size,
    .player_info_state_save = osrs241_playerinfo_state_save,
    .player_info_state_load = osrs241_playerinfo_state_load,
};

struct GameProtoRevTable const*
GameProtoRev_OSRS241(void)
{
    return &k_rev_osrs241;
}
