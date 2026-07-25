#ifndef SRC_NET_REV_OSRS230_PACKETIN_H
#define SRC_NET_REV_OSRS230_PACKETIN_H

/*
 * OSRS revision 230 server->client wire opcodes + payload sizes, from RSProt
 * (protocol/osrs-230-desktop/.../game/outgoing/prot/GameServerProt(Id).kt).
 * Sizes drive framing: >=0 fixed, -1 var-u8 (VAR_BYTE), -2 var-u16 (VAR_SHORT).
 *
 * Transport is classic OSRS: raw TCP, ISAAC-scrambled opcode byte, plaintext
 * length + payload. NOTE: opcodes >=128 use a 2-byte "pSmart" wire form; the
 * on-login burst this client targets uses only opcodes <128, so single-byte
 * decode suffices for now (128/129/134 are not sent).
 *
 * packetin_code maps only the packets we decode to canonical names; the rest
 * frame cleanly (correct size) and drop. REBUILD_NORMAL is handled by the
 * osrs230 `parse` override (payload differs from the lc254 layout).
 */

#include "net/rev/pktnames.h"

#ifndef PKTIN_LENGTH_VARU8
#define PKTIN_LENGTH_VARU8 (-1)
#define PKTIN_LENGTH_VARU16 (-2)
#endif

struct Osrs230PacketInDef
{
    int code; /* wire opcode */
    int length;
    int name; /* canonical GameProtoPktName, PKT_NAME_NONE if undecoded */
};

/* clang-format off */
static const struct Osrs230PacketInDef g_packet_in_definitions_osrs230[] = {
    { 68, PKTIN_LENGTH_VARU16, PKT_NAME_REBUILD_NORMAL }, /* also carries REBUILD_LOGIN */
    { 59, PKTIN_LENGTH_VARU16, PKT_NAME_NONE }, /* REBUILD_REGION */
    { 23, PKTIN_LENGTH_VARU16, PKT_NAME_PLAYER_INFO }, /* GPI (classic/Kronos-style bitstream) */
    { 104, PKTIN_LENGTH_VARU16, PKT_NAME_NONE }, /* NPC_INFO_SMALL_V5 */
    { 12, PKTIN_LENGTH_VARU16, PKT_NAME_NONE }, /* NPC_INFO_LARGE_V5 */
    { 0, 2, PKT_NAME_NONE },   /* SET_NPC_UPDATE_ORIGIN */
    { 35, 3, PKT_NAME_NONE },  /* VARP_SMALL */
    { 82, 6, PKT_NAME_NONE },  /* VARP_LARGE */
    { 7, 0, PKT_NAME_NONE },   /* VARP_RESET */
    { 88, 0, PKT_NAME_NONE },  /* VARP_SYNC */
    { 77, 2, PKT_NAME_NONE },  /* UPDATE_RUNENERGY */
    { 27, 2, PKT_NAME_NONE },  /* UPDATE_RUNWEIGHT */
    { 114, 7, PKT_NAME_NONE }, /* UPDATE_STAT_V2 */
    { 10, PKTIN_LENGTH_VARU16, PKT_NAME_UPDATE_INV_FULL },
    { 37, PKTIN_LENGTH_VARU16, PKT_NAME_UPDATE_INV_PARTIAL },
    { 80, 4, PKT_NAME_UPDATE_INV_STOP_TRANSMIT }, /* p4 combinedId */
    { 60, 2, PKT_NAME_IF_OPENTOP },  /* IF_OPENTOP (root interface) */
    { 6, 7, PKT_NAME_IF_OPENSUB },   /* IF_OPENSUB */
    { 36, 4, PKT_NAME_IF_CLOSESUB }, /* IF_CLOSESUB */
    { 53, PKTIN_LENGTH_VARU16, PKT_NAME_NONE },  /* IF_RESYNC */
    { 94, PKTIN_LENGTH_VARU16, PKT_NAME_NONE },  /* IF_SETTEXT */
    { 47, 12, PKT_NAME_NONE }, /* IF_SETEVENTS */
    { 84, PKTIN_LENGTH_VARU16, PKT_NAME_NONE },  /* RUNCLIENTSCRIPT */
    { 90, PKTIN_LENGTH_VARU8, PKT_NAME_NONE },   /* MESSAGE_GAME */
    { 29, PKTIN_LENGTH_VARU16, PKT_NAME_NONE },  /* MESSAGE_PRIVATE */
    { 2, 2, PKT_NAME_NONE },   /* SET_MAP_FLAG */
    { 75, PKTIN_LENGTH_VARU8, PKT_NAME_NONE },   /* SET_PLAYER_OP */
    { 76, 6, PKT_NAME_NONE },  /* HINT_ARROW */
    { 73, 1, PKT_NAME_NONE },  /* MINIMAP_TOGGLE */
    { 65, 0, PKT_NAME_NONE },  /* CAM_RESET */
    { 108, 0, PKT_NAME_NONE }, /* SERVER_TICK_END */
    { 103, 2, PKT_NAME_NONE }, /* UPDATE_REBOOT_TIMER */
    { 15, 0, PKT_NAME_NONE },  /* FRIENDLIST_LOADED */
    { 56, PKTIN_LENGTH_VARU16, PKT_NAME_NONE },  /* UPDATE_FRIENDLIST */
    { 21, PKTIN_LENGTH_VARU16, PKT_NAME_NONE },  /* UPDATE_IGNORELIST */
    { 20, 0, PKT_NAME_NONE },  /* LOGOUT */
    { 102, 5, PKT_NAME_NONE }, /* SYNTH_SOUND */
    { 57, 10, PKT_NAME_NONE }, /* MIDI_SONG_V2 */
    { 41, 3, PKT_NAME_NONE },  /* UPDATE_ZONE_FULL_FOLLOWS */
    { 106, 3, PKT_NAME_NONE }, /* UPDATE_ZONE_PARTIAL_FOLLOWS */
    { 38, PKTIN_LENGTH_VARU16, PKT_NAME_NONE },  /* UPDATE_ZONE_PARTIAL_ENCLOSED */
    { 55, 4, PKT_NAME_NONE },  /* SET_ACTIVE_WORLD_V1 */
    { 22, PKTIN_LENGTH_VARU16, PKT_NAME_NONE },  /* REFLECTION_CHECKER */
    { 52, 8, PKT_NAME_NONE },  /* SEND_PING */
};
/* clang-format on */

#define OSRS230_PACKET_IN_COUNT                                                                    \
    ((int)(sizeof(g_packet_in_definitions_osrs230) /                                               \
           sizeof(g_packet_in_definitions_osrs230[0])))

static inline int
packetin_size_osrs230(int wire_opcode)
{
    for( int i = 0; i < OSRS230_PACKET_IN_COUNT; i++ )
        if( g_packet_in_definitions_osrs230[i].code == wire_opcode )
            return g_packet_in_definitions_osrs230[i].length;
    return 0; /* unknown -> zero-length, framer completes + resets (no desync) */
}

static inline int
packetin_code_osrs230(int wire_opcode)
{
    for( int i = 0; i < OSRS230_PACKET_IN_COUNT; i++ )
        if( g_packet_in_definitions_osrs230[i].code == wire_opcode )
            return g_packet_in_definitions_osrs230[i].name;
    return PKT_NAME_NONE;
}

static inline int
packetin_wire_osrs230(int pkt_name)
{
    for( int i = 0; i < OSRS230_PACKET_IN_COUNT; i++ )
        if( g_packet_in_definitions_osrs230[i].name == pkt_name &&
            g_packet_in_definitions_osrs230[i].name != PKT_NAME_NONE )
            return g_packet_in_definitions_osrs230[i].code;
    return -1;
}

#endif
