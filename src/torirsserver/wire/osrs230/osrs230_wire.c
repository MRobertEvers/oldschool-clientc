#include "torirsserver/torirs_server_wire.h"

#include "net/rev/pktnames.h"
#include "torirsserver/wire/osrs230/packetin.h"
#include "torirsserver/wire/osrs230/packetout.h"

#include <stddef.h>

/*
 * Revision 230: the hybrid table this repo's own client speaks. Its payloads
 * are whatever torirs_server_encode.c has always written (`payload` NULL); only
 * the opcode numbering is this file's. See torirs_server_wire.h.
 */

/*
 * Both revisions resolve through the SAME tables the client reads
 * (`src/net/rev/<rev>/packetin.h`). That is the whole reason the numbers cannot
 * drift: there is one statement of "IF_OPENTOP is 96 at revision 239" and both
 * ends include it. The old file-local enum in torirs_server_encode.c was a second
 * copy, and a second copy of a number is a number that will disagree.
 */

static int
osrs230_opcode(int pkt_name)
{
    return packetin_wire_osrs230(pkt_name);
}

static int
osrs230_size(int wire_opcode)
{
    return packetin_size_osrs230(wire_opcode);
}

/*
 * Revision 230 addresses a zone sub-packet by its top-level opcode -- the
 * client resolves it in the same table -- so this is the same lookup.
 */
static int
osrs230_out_size(int wire_opcode)
{
    return osrs230_packetout_size(wire_opcode);
}

static int
osrs230_out_name(int wire_opcode)
{
    return osrs230_packetout_name(wire_opcode);
}

static int
osrs230_zone_sub(int pkt_name)
{
    return packetin_wire_osrs230(pkt_name);
}

static const struct ToriRSServerWire k_wire_osrs230 = {
    .name = "osrs230",
    .revision = 230,
    .opcode = osrs230_opcode,
    .payload_size = osrs230_size,
    .prot_name = NULL,
    .zone_sub_code = osrs230_zone_sub,
    .packetout_size = osrs230_out_size,
    .packetout_name = osrs230_out_name,
    .opcode_smart2 = 0, /* no opcode in this table reaches 0x80 */
    .payload = NULL,    /* the lc254-shaped set torirs_server_encode.c writes */
};

const struct ToriRSServerWire*
ToriRSServer_Wire230(void)
{
    return &k_wire_osrs230;
}
