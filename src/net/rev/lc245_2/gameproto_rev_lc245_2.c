#include "net/rev/gameproto_revisions.h"
#include "packetin.h"
#include "packetout.h"

static int
rev_packetin_size(int wire_opcode)
{
    return packetin_size_lc245_2(wire_opcode);
}

static int
rev_packetin_code(int wire_opcode)
{
    return packetin_code_lc245_2(wire_opcode);
}

static int
rev_packetin_wire(int pkt_name)
{
    return packetin_wire_lc245_2(pkt_name);
}

static int
rev_packetout_code(int pkt_out_name)
{
    return packetout_code_lc245_2(pkt_out_name);
}

/* Jag-archive CRCs of the rev 245_2 Lost City build (v0 loginproto values;
 * "TODO get from server" carried over — the CRC handshake fetch is future
 * work, so a different server build rejects login until then). */
static struct GameProtoRevTable const k_rev_lc245_2 = {
    .revision = GAMEPROTO_REVISION_LC245_2,
    .name = "lc245_2",
    .client_version = 245,
    .jag_checksum = { 0, -945108033, -323580723, 1539972921, -259567598,
                      260912122, -1840622973, -87627495, -1625923170 },
    .packetin_size = rev_packetin_size,
    .packetin_code = rev_packetin_code,
    .packetin_wire = rev_packetin_wire,
    .packetout_code = rev_packetout_code,
};

struct GameProtoRevTable const*
GameProtoRev_LC245_2(void)
{
    return &k_rev_lc245_2;
}
