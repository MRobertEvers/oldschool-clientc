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
 * PAYLOAD LAYOUTS ARE THE lc254 ONES. net_out.c has a single set of builders
 * shared by every revision; only the opcode number is revision-specific. Real
 * rev-230 re-orders most of these fields through RSProt's alt byte orders, so
 * a packet built here would not parse on a real OldSchool server — but the
 * mock in src/net/mock decodes exactly what net_out.c writes, which is what
 * makes the pair testable. `osrs230_packetout_size` is the mock's framing
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

    /* Movement. The builders emit their own var-u8 length byte. */
    { PKTOUT_NAME_MOVE_GAMECLICK,      86,  PKTOUT_LENGTH_VARU8 },
    { PKTOUT_NAME_MOVE_OPCLICK,        30,  PKTOUT_LENGTH_VARU8 },
    { PKTOUT_NAME_MOVE_MINIMAPCLICK,   55,  PKTOUT_LENGTH_VARU8 },

    /* Interface component clicks: p2 component id. */
    { PKTOUT_NAME_IF_BUTTON,           40,  2 },
    { PKTOUT_NAME_RESUME_PAUSEBUTTON,  41,  2 },
    { PKTOUT_NAME_CLOSE_MODAL,         42,  0 },
    { PKTOUT_NAME_RESUME_P_COUNTDIALOG,43,  4 },

    /* Held-item ops: p2 obj, p2 slot, p2 component. OPHELD2 is wear/wield. */
    { PKTOUT_NAME_OPHELD1,             57,  6 },
    { PKTOUT_NAME_OPHELD2,             58,  6 },
    { PKTOUT_NAME_OPHELD3,             59,  6 },
    { PKTOUT_NAME_OPHELD4,             61,  6 },
    { PKTOUT_NAME_OPHELD5,             62,  6 },
    { PKTOUT_NAME_OPHELDT,             63,  8 },
    { PKTOUT_NAME_OPHELDU,             64,  12 },

    /* Inventory-component ops + drag. INV_BUTTOND is p2 com, p2 from, p2 to, p1 mode. */
    { PKTOUT_NAME_INV_BUTTON1,         44,  6 },
    { PKTOUT_NAME_INV_BUTTON2,         45,  6 },
    { PKTOUT_NAME_INV_BUTTON3,         46,  6 },
    { PKTOUT_NAME_INV_BUTTON4,         47,  6 },
    { PKTOUT_NAME_INV_BUTTON5,         48,  6 },
    { PKTOUT_NAME_INV_BUTTOND,         49,  7 },

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
