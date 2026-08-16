#include "net/rev/gameproto_revisions.h"
#include "packetin.h"
#include "packetout.h"

#include "net/loginproto_xrsps.h"

static int
rev_packetin_size(int wire_opcode)
{
    return packetin_size_xrsps233(wire_opcode);
}

static int
rev_packetin_code(int wire_opcode)
{
    return packetin_code_xrsps233(wire_opcode);
}

static int
rev_packetin_wire(int pkt_name)
{
    return packetin_wire_xrsps233(pkt_name);
}

static int
rev_packetout_code(int pkt_out_name)
{
    return packetout_code_xrsps233(pkt_out_name);
}

/*
 * xrsps (OSRS rev 233) over WebSocket. Unlike the lc revisions this speaks a
 * plaintext, WebSocket-framed protocol (no RSA/ISAAC/JS5): transport_kind=WS,
 * opcode_plaintext=1, and a custom login driver. See
 * docs/MULTI_GENERATIONAL_PARITY.md and the xrsps-typescript server.
 */
static struct GameProtoRevTable k_rev_xrsps233 = {
    .revision = GAMEPROTO_REVISION_XRSPS233,
    .name = "xrsps233",
    .client_version = 233,
    .jag_checksum = { 0 },
    .packetin_size = rev_packetin_size,
    .packetin_code = rev_packetin_code,
    .packetin_wire = rev_packetin_wire,
    .packetout_code = rev_packetout_code,

    .transport_kind = NET_TRANSPORT_WS,
    .opcode_plaintext = 1,
    .server_tick_ms = 600,
    .login = &g_xr_login_vtable,
    /* parse / entity readers / scene_base / out_vt land in Phases 2-5. */
};

struct GameProtoRevTable const*
GameProtoRev_XRSPS233(void)
{
    return &k_rev_xrsps233;
}
