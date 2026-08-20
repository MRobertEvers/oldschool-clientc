#ifndef SRC_NET_REV_OSRS230_PACKETOUT_H
#define SRC_NET_REV_OSRS230_PACKETOUT_H

/*
 * OSRS rev 230 client->server opcodes, plus the payload size each one frames
 * to. The login trio (14/16) is built by the osrs230 login driver, not via
 * this table.
 *
 * OPCODE NUMBERS: NO_TIMEOUT (0), MAP_BUILD_COMPLETE (54), MOVE_MINIMAPCLICK
 * (55) and MOVE_GAMECLICK (86) are the real RSProt GameClientProt values. The
 * rest are assigned here — RSProt's client-prot table is not vendored in this
 * repo, and the mock server is the only thing that reads them, so an
 * unambiguous set the two ends agree on is what matters. They stay inside the
 * rev-230 opcode space and collide with nothing else in this table.
 *
 * PAYLOAD LAYOUTS mostly follow the lc254 builders in net_out.c. The one
 * deliberate divergence: MOVE_GAMECLICK is a fixed 5-byte destination body
 * (rev 230/239), not the classic var-u8 waypoint packet. See
 * docs/OSRS_PATHING_LOS.md. `osrs230_packetout_size` is the mock's framing
 * table: >=0 fixed, -1 var-u8 (the builder writes the length byte itself).
 * See docs/osrs230_mockserver.md.
 */

#include "net/rev/pktnames.h"

#ifndef PKTOUT_LENGTH_VARU8
#define PKTOUT_LENGTH_VARU8 (-1)
#define PKTOUT_LENGTH_VARU16 (-2)
#endif

struct Osrs230PacketOutDef
{
    int name;   /* canonical GameProtoPktOutName */
    int code;   /* wire opcode */
    int length; /* payload bytes after the opcode, or PKTOUT_LENGTH_* */
};

/* clang-format off */
static const struct Osrs230PacketOutDef g_packet_out_definitions_osrs230[] = {
    { PKTOUT_NAME_NO_TIMEOUT,          0,   0 },
    { PKTOUT_NAME_IDLE_TIMER,          16,  0 },
    { PKTOUT_NAME_MAP_BUILD_COMPLETE,  54,  0 },
    { PKTOUT_NAME_EVENT_APPLET_FOCUS,  71,  1 },

    /* Movement. Rev 230/239 MOVE_GAMECLICK is a fixed 5-byte destination
     * body (keyCombination, x, z) — no waypoints, no MOVE_OPCLICK. Minimap
     * still carries the classic trailer after a var-u8 length byte. */
    { PKTOUT_NAME_MOVE_GAMECLICK,      86,  5 },
    { PKTOUT_NAME_MOVE_MINIMAPCLICK,   55,  PKTOUT_LENGTH_VARU8 },

    /* Interface component clicks: p2 component id. */
    /* 4, not 2: rev 230 sends a packed (interface << 16) | child uid. See
     * GameProtoRevTable.component_id_bytes. */
    { PKTOUT_NAME_IF_BUTTON,           40,  4 },

    /* IF_BUTTON1..10 (RSProt If3Button): p4 combined component uid, p2 sub.
     * `sub` is the dynamic-child index a grid cell was clicked in, or -1 for a
     * plain widget. Opcodes assigned here like the rest of this table. */
    { PKTOUT_NAME_IF_BUTTON1,          90,  6 },
    { PKTOUT_NAME_IF_BUTTON2,          91,  6 },
    { PKTOUT_NAME_IF_BUTTON3,          92,  6 },
    { PKTOUT_NAME_IF_BUTTON4,          93,  6 },
    { PKTOUT_NAME_IF_BUTTON5,          94,  6 },
    { PKTOUT_NAME_IF_BUTTON6,          95,  6 },
    { PKTOUT_NAME_IF_BUTTON7,          96,  6 },
    { PKTOUT_NAME_IF_BUTTON8,          97,  6 },
    { PKTOUT_NAME_IF_BUTTON9,          98,  6 },
    { PKTOUT_NAME_IF_BUTTON10,         99,  6 },

    /* CLICK_WORLD_MAP: p4 packed coord (level << 28 | x << 14 | z). */
    { PKTOUT_NAME_CLICK_WORLD_MAP,     100, 4 },
    { PKTOUT_NAME_RESUME_PAUSEBUTTON,  41,  4 },
    { PKTOUT_NAME_CLOSE_MODAL,         42,  0 },
    { PKTOUT_NAME_RESUME_P_COUNTDIALOG,43,  4 },

    /* Held-item ops: p2 obj, p2 slot, p4 component. OPHELD2 is wear/wield.
     * The component is 4 bytes for the same reason IF_BUTTON's is — rev 230
     * names a component by the packed (interface << 16) | child uid, which is
     * rsprot's If3Button.combinedId. `slot` is the sub id beside it: the
     * inventory slot for the backpack, the dynamic child index for a worn
     * slot (rsprot If3Button.sub). */
    { PKTOUT_NAME_OPHELD1,             57,  8 },
    { PKTOUT_NAME_OPHELD2,             58,  8 },
    { PKTOUT_NAME_OPHELD3,             59,  8 },
    { PKTOUT_NAME_OPHELD4,             61,  8 },
    { PKTOUT_NAME_OPHELD5,             62,  8 },
    { PKTOUT_NAME_OPHELDT,             63,  12 },
    { PKTOUT_NAME_OPHELDU,             64,  16 },

    /* Inventory-component ops + drag. INV_BUTTOND is the rev-230 IfButtonD
     * shape (real ClientProt 48): 16 bytes naming both endpoints
     * (srcCom LE, srcObj LE, srcSlot LE+128, dstCom BE, dstObj BE+128,
     * dstSlot BE+128). INV_BUTTON5 moved off 48 so the drag opcode can sit
     * on the real number without colliding. */
    { PKTOUT_NAME_INV_BUTTON1,         44,  8 },
    { PKTOUT_NAME_INV_BUTTON2,         45,  8 },
    { PKTOUT_NAME_INV_BUTTON3,         46,  8 },
    { PKTOUT_NAME_INV_BUTTON4,         47,  8 },
    { PKTOUT_NAME_INV_BUTTON5,         50,  8 },
    { PKTOUT_NAME_INV_BUTTOND,         48,  16 },

    /* NPC ops: p2 npc slot. */
    { PKTOUT_NAME_OPNPC1,              9,   2 },
    { PKTOUT_NAME_OPNPC2,              10,  2 },
    { PKTOUT_NAME_OPNPC3,              11,  2 },
    { PKTOUT_NAME_OPNPC4,              13,  2 },
    { PKTOUT_NAME_OPNPC5,              15,  2 },
    { PKTOUT_NAME_OPNPCT,              17,  4 },
    { PKTOUT_NAME_OPNPCU,              18,  8 },

    /* Loc ops: p2 x, p2 z, p2 loc. */
    { PKTOUT_NAME_OPLOC1,              19,  6 },
    { PKTOUT_NAME_OPLOC2,              20,  6 },
    { PKTOUT_NAME_OPLOC3,              21,  6 },
    { PKTOUT_NAME_OPLOC4,              22,  6 },
    { PKTOUT_NAME_OPLOC5,              24,  6 },
    { PKTOUT_NAME_OPLOCT,              25,  8 },
    { PKTOUT_NAME_OPLOCU,              26,  12 },

    /* Ground-obj ops: p2 x, p2 z, p2 obj. */
    { PKTOUT_NAME_OPOBJ1,              31,  6 },
    { PKTOUT_NAME_OPOBJ2,              32,  6 },
    { PKTOUT_NAME_OPOBJ3,              33,  6 },
    { PKTOUT_NAME_OPOBJ4,              34,  6 },
    { PKTOUT_NAME_OPOBJ5,              35,  6 },
    { PKTOUT_NAME_OPOBJT,              36,  8 },
    { PKTOUT_NAME_OPOBJU,              37,  12 },

    /* Chat. */
    { PKTOUT_NAME_MESSAGE_PUBLIC,      75,  PKTOUT_LENGTH_VARU8 },
    { PKTOUT_NAME_CLIENT_CHEAT,        76,  PKTOUT_LENGTH_VARU8 },

    /*
     * Social. Opcodes 3..8 are assigned here like most of this table — they
     * were free (the used set below 128 is 0,9..11,13,15..22,24..26,30..37,
     * 40..48,50,54,55,57..59,61..64,71,75,76,86,90..100) and 14/16 are the login
     * driver's and deliberately avoided.
     *
     * All six builders already existed in net_out.c and had no rev-230 opcode,
     * so `packetout_code_osrs230` returned -1 and `out_begin` refused: every
     * social send this client made at rev 230 wrote nothing. These rows are what
     * turn them on. Payloads are lc254's, which is what net_out.c writes and
     * what src/torirsserver decodes:
     *
     *   FRIENDLIST_ADD/DEL, IGNORELIST_ADD/DEL  p8 name37
     *   CHAT_SETMODE                            p1 public, p1 private, p1 trade
     *   MESSAGE_PRIVATE                         p8 to37 + wordpacked text
     */
    { PKTOUT_NAME_FRIENDLIST_ADD,      3,   8 },
    { PKTOUT_NAME_FRIENDLIST_DEL,      4,   8 },
    { PKTOUT_NAME_IGNORELIST_ADD,      5,   8 },
    { PKTOUT_NAME_IGNORELIST_DEL,      6,   8 },
    { PKTOUT_NAME_CHAT_SETMODE,        7,   3 },
    { PKTOUT_NAME_MESSAGE_PRIVATE,     8,   PKTOUT_LENGTH_VARU8 },

    /* WINDOW_STATUS: p1 clientMode (0/1/2), p2 width, p2 height. Opcode 101 is
     * mock-local — RSProt uses 10, which is OPNPC2 in this table. */
    { PKTOUT_NAME_WINDOW_STATUS,       101, 5 },
};
/* clang-format on */

#define OSRS230_PACKET_OUT_COUNT                                                                   \
    ((int)(sizeof(g_packet_out_definitions_osrs230) /                                              \
           sizeof(g_packet_out_definitions_osrs230[0])))

/** Canonical PKTOUT_NAME_* -> wire opcode; -1 = absent from this rev. */
static inline int
packetout_code_osrs230(int pkt_out_name)
{
    for( int i = 0; i < OSRS230_PACKET_OUT_COUNT; i++ )
        if( g_packet_out_definitions_osrs230[i].name == pkt_out_name )
            return g_packet_out_definitions_osrs230[i].code;
    return -1;
}

/** Wire opcode -> payload size for the receiving side (the mock server).
 *  Returns 0 for an unknown opcode, which frames as an empty packet and lets
 *  the stream resynchronise instead of desyncing on a guessed length. */
static inline int
osrs230_packetout_size(int wire_opcode)
{
    for( int i = 0; i < OSRS230_PACKET_OUT_COUNT; i++ )
        if( g_packet_out_definitions_osrs230[i].code == wire_opcode )
            return g_packet_out_definitions_osrs230[i].length;
    return 0;
}

/** Wire opcode -> canonical PKTOUT_NAME_*, or PKTOUT_NAME_NONE. */
static inline int
osrs230_packetout_name(int wire_opcode)
{
    for( int i = 0; i < OSRS230_PACKET_OUT_COUNT; i++ )
        if( g_packet_out_definitions_osrs230[i].code == wire_opcode )
            return g_packet_out_definitions_osrs230[i].name;
    return PKTOUT_NAME_NONE;
}

#endif
