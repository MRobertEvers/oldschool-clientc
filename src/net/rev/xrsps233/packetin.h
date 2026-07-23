#ifndef SRC_NET_REV_XRSPS233_PACKETIN_H
#define SRC_NET_REV_XRSPS233_PACKETIN_H

/*
 * xrsps (OSRS rev 233) server->client wire opcodes + payload sizes, transcribed
 * from xrsps-typescript/src/shared/packets/ServerPacketId.ts (SERVER_PACKET_
 * LENGTHS). Sizes drive framing (packetbuffer): >=0 fixed, -1 var-u8, -2
 * var-u16. Correct sizes matter for EVERY opcode so the framer never desyncs,
 * even for packets we don't decode yet.
 *
 * Phase 1: packetin_code returns PKT_NAME_NONE for everything — the login
 * driver handles welcome/login_response/handshake at the login layer, and
 * post-login game packets are framed then dropped (visible via TORIRS_NET_DEBUG)
 * while the connection stays alive. Phase 2+ maps opcodes to canonical names.
 */

#include "net/rev/pktnames.h"

#ifndef PKTIN_LENGTH_VARU8
#define PKTIN_LENGTH_VARU8 (-1)
#define PKTIN_LENGTH_VARU16 (-2)
#endif

struct XrPacketInDef
{
    int code; /* wire opcode */
    int length;
};

/* clang-format off */
static const struct XrPacketInDef g_packet_in_definitions_xrsps233[] = {
    { 0, 8 },   /* WELCOME: tickMs(4)+serverTime(4) */
    { 1, 8 },   /* TICK: tick(4)+time(4) */
    { 2, PKTIN_LENGTH_VARU8 },  /* HANDSHAKE ack */
    { 3, PKTIN_LENGTH_VARU8 },  /* LOGIN_RESPONSE */
    { 4, PKTIN_LENGTH_VARU8 },  /* LOGOUT_RESPONSE */
    { 5, PKTIN_LENGTH_VARU8 },  /* PATH_RESPONSE */
    { 20, PKTIN_LENGTH_VARU16 }, /* PLAYER_SYNC (GPI) */
    { 21, PKTIN_LENGTH_VARU16 }, /* NPC_INFO (GNI) */
    { 22, 22 },  /* ANIM */
    { 40, 3 },   /* VARP_SMALL */
    { 41, 6 },   /* VARP_LARGE */
    { 42, 6 },   /* VARBIT */
    { 43, PKTIN_LENGTH_VARU8 },  /* VARP_BATCH */
    { 50, PKTIN_LENGTH_VARU16 }, /* INVENTORY_SNAPSHOT */
    { 51, PKTIN_LENGTH_VARU8 },  /* INVENTORY_SLOT */
    { 52, PKTIN_LENGTH_VARU16 }, /* BANK_SNAPSHOT */
    { 53, PKTIN_LENGTH_VARU8 },  /* BANK_SLOT */
    { 54, PKTIN_LENGTH_VARU16 }, /* GROUND_ITEMS */
    { 55, PKTIN_LENGTH_VARU16 }, /* GROUND_ITEMS_DELTA */
    { 70, PKTIN_LENGTH_VARU8 },  /* SKILLS_SNAPSHOT */
    { 71, PKTIN_LENGTH_VARU8 },  /* SKILLS_DELTA */
    { 80, PKTIN_LENGTH_VARU8 },  /* COMBAT_STATE */
    { 81, 2 },   /* RUN_ENERGY */
    { 82, PKTIN_LENGTH_VARU8 },  /* HITSPLAT */
    { 83, PKTIN_LENGTH_VARU8 },  /* SPOT_ANIM */
    { 84, PKTIN_LENGTH_VARU16 }, /* PROJECTILES */
    { 85, PKTIN_LENGTH_VARU16 }, /* SPELL_RESULT */
    { 86, PKTIN_LENGTH_VARU16 }, /* DEBUG_PACKET */
    { 87, 4 },   /* DESTINATION */
    { 100, 3 },  /* WIDGET_OPEN */
    { 101, 2 },  /* WIDGET_CLOSE */
    { 102, 2 },  /* WIDGET_SET_ROOT */
    { 103, PKTIN_LENGTH_VARU16 }, /* WIDGET_OPEN_SUB */
    { 104, 4 },  /* WIDGET_CLOSE_SUB */
    { 105, PKTIN_LENGTH_VARU16 }, /* WIDGET_SET_TEXT */
    { 106, 5 },  /* WIDGET_SET_HIDDEN */
    { 107, 10 }, /* WIDGET_SET_ITEM */
    { 108, 6 },  /* WIDGET_SET_NPC_HEAD */
    { 109, 12 }, /* WIDGET_SET_FLAGS_RANGE */
    { 110, PKTIN_LENGTH_VARU16 }, /* WIDGET_RUN_SCRIPT */
    { 111, 8 },  /* WIDGET_SET_FLAGS */
    { 114, 6 },  /* WIDGET_SET_ANIMATION */
    { 115, 4 },  /* WIDGET_SET_PLAYER_HEAD */
    { 116, PKTIN_LENGTH_VARU16 }, /* WIDGET_SET_QUEST_LIST */
    { 120, PKTIN_LENGTH_VARU8 },  /* CHAT_MESSAGE */
    { 130, PKTIN_LENGTH_VARU8 },  /* LOC_CHANGE */
    { 131, PKTIN_LENGTH_VARU8 },  /* SOUND */
    { 132, 5 },  /* PLAY_JINGLE */
    { 133, 10 }, /* PLAY_SONG */
    { 134, PKTIN_LENGTH_VARU8 },  /* LOC_ADD_CHANGE */
    { 135, PKTIN_LENGTH_VARU8 },  /* LOC_DEL */
    { 136, 10 }, /* LOC_ANIM */
    { 140, PKTIN_LENGTH_VARU16 }, /* REBUILD_REGION */
    { 141, PKTIN_LENGTH_VARU16 }, /* REBUILD_NORMAL */
    { 142, PKTIN_LENGTH_VARU16 }, /* REBUILD_WORLDENTITY */
    { 143, PKTIN_LENGTH_VARU8 },  /* WORLDENTITY_INFO */
    { 150, PKTIN_LENGTH_VARU16 }, /* SHOP_OPEN */
    { 151, PKTIN_LENGTH_VARU8 },  /* SHOP_SLOT */
    { 152, 1 },  /* SHOP_CLOSE (0-payload; 1 keeps a byte) */
    { 153, 2 },  /* SHOP_MODE */
    { 170, PKTIN_LENGTH_VARU16 }, /* RUN_CLIENT_SCRIPT */
    { 190, PKTIN_LENGTH_VARU16 }, /* COLLECTION_LOG_SNAPSHOT */
    { 200, PKTIN_LENGTH_VARU8 },  /* NOTIFICATION */
    { 210, PKTIN_LENGTH_VARU8 },  /* GAMEMODE_DATA */
    { 250, PKTIN_LENGTH_VARU16 }, /* DEBUG */
};
/* clang-format on */

#define XRSPS233_PACKET_IN_COUNT                                                                    \
    ((int)(sizeof(g_packet_in_definitions_xrsps233) /                                              \
           sizeof(g_packet_in_definitions_xrsps233[0])))

static inline int
packetin_size_xrsps233(int wire_opcode)
{
    for( int i = 0; i < XRSPS233_PACKET_IN_COUNT; i++ )
        if( g_packet_in_definitions_xrsps233[i].code == wire_opcode )
            return g_packet_in_definitions_xrsps233[i].length;
    /* Unknown opcode: 0 length so the framer completes one empty packet and
     * resets rather than treating the size as var-length and desyncing. */
    return 0;
}

static inline int
packetin_code_xrsps233(int wire_opcode)
{
    (void)wire_opcode;
    /* Phase 1: no game-packet decode yet — everything drops cleanly. */
    return PKT_NAME_NONE;
}

static inline int
packetin_wire_xrsps233(int pkt_name)
{
    (void)pkt_name;
    return -1;
}

#endif
