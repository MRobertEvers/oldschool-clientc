#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Revision-239 parse override (server -> this client).
 *
 * Most of revision 239's payloads are revision 230's, so this delegates to
 * `osrs230_parse` rather than copying it, and states here — as a list, not as a
 * hope — the packets where that delegation is WRONG. Each of those returns -1
 * ("not mine"), which drops the packet cleanly instead of decoding it with the
 * wrong layout.
 *
 * The distinction matters because the two revision tables have different
 * relationships to reality. osrs230's is a hybrid: roughly a third of its
 * opcodes were assigned by this project and its payloads are lc254's, so
 * "delegate to 230" means "read the layout our own server writes". That is
 * correct for the packets whose layout genuinely did not move and it is exactly
 * wrong for the ones below.
 *
 * DELIBERATELY NOT DELEGATED, with the divergence measured from RSProt:
 *
 *   REBUILD_NORMAL   V2 dropped the XTEA key array entirely (map archives are
 *                    stored plain at OldSchool >= 237) and reordered the rest:
 *                    p2Alt1 worldArea, p2Alt2 zoneZ, p2Alt2 zoneX. The 230
 *                    layout is p2 worldArea, p2Alt2 zoneX, p2 zoneZ, p2 keyCount,
 *                    keys. Reading a V2 packet as a V1 one recovers a plausible
 *                    zone and then walks into a key count that is not there.
 *   REBUILD_REGION   same V2 treatment.
 *   IF_SETEVENTS     V2 carries TWO 32-bit event masks, not one, and writes
 *                    them as p2Alt2 end, p4 events2, p4Alt2 events1, p2 start
 *                    -- a different field order as well as a different width.
 *                    12 bytes became 16.
 *   IF_SETMODEL      V2 addresses the component by a 4-byte combined uid.
 *   CAM_MOVETO       V2/V3 moved to absolute coordinates; the 230 reader
 *   CAM_LOOKAT       expects the classic scene-local 6-byte body.
 *   PLAYER_INFO      the v5 high/low-resolution stream. The 230 table binds
 *   NPC_INFO         this opcode to the classic lc254 bitstream, which is a
 *                    stated deviation there and would be a silent corruption
 *                    here.
 *
 * The client is therefore not at rev-230 feature parity when it speaks 239, and
 * that is the honest state: this revision exists so the SERVER can talk to an
 * OldSchool client, and the server side is where the v5 encoders live. Filling
 * these in is a client-parity task, not a blocker for that.
 */

int
osrs230_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out);

int
osrs239_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out)
{
    switch( pkt_name )
    {
    case PKT_NAME_REBUILD_NORMAL:
    case PKT_NAME_REBUILD_REGION:
    case PKT_NAME_IF_SETEVENTS:
    case PKT_NAME_IF_SETMODEL:
    case PKT_NAME_CAM_MOVETO:
    case PKT_NAME_CAM_LOOKAT:
    case PKT_NAME_PLAYER_INFO:
    case PKT_NAME_NPC_INFO:
        /* Layout moved at this revision; see the header comment. Dropping is
         * the only answer that cannot corrupt state. */
        return -1;
    default:
        return osrs230_parse(rev, pkt_name, data, len, out);
    }
}
