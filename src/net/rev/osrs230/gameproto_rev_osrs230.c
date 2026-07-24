#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"
#include "packetin.h"
#include "packetout.h"

#include "net/loginproto_osrs230.h"

#include <stdint.h>

/* osrs230_parse.c */
int
osrs230_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out);

static int
rev_packetin_size(int wire_opcode)
{
    return packetin_size_osrs230(wire_opcode);
}

static int
rev_packetin_code(int wire_opcode)
{
    return packetin_code_osrs230(wire_opcode);
}

static int
rev_packetin_wire(int pkt_name)
{
    return packetin_wire_osrs230(pkt_name);
}

static int
rev_packetout_code(int pkt_out_name)
{
    return packetout_code_osrs230(pkt_out_name);
}

/*
 * OSRS revision 230 (RSProt). Classic OSRS transport — raw TCP, ISAAC-scrambled
 * opcodes, RSA login block — so transport_kind=TCP and opcode_plaintext=0. Only
 * the login handshake and REBUILD_NORMAL payload differ from lc254, handled by
 * the login vtable and the parse override. See docs/MULTI_GENERATIONAL_PARITY.md.
 */
static struct GameProtoRevTable k_rev_osrs230 = {
    .revision = GAMEPROTO_REVISION_OSRS230,
    .name = "osrs230",
    .client_version = 230,
    .jag_checksum = { 0 },
    .packetin_size = rev_packetin_size,
    .packetin_code = rev_packetin_code,
    .packetin_wire = rev_packetin_wire,
    .packetout_code = rev_packetout_code,

    .transport_kind = NET_TRANSPORT_TCP,
    .opcode_plaintext = 0, /* ISAAC-scrambled opcodes */
    .server_tick_ms = 600,
    .login = &g_osrs230_login_vtable,
    .parse = osrs230_parse,
};

struct GameProtoRevTable const*
GameProtoRev_OSRS230(void)
{
    return &k_rev_osrs230;
}
