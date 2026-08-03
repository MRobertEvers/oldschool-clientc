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
 *
 * ENTITY STREAMS ARE A DELIBERATE DEVIATION. Real rev-230 PLAYER_INFO (op 23)
 * and NPC_INFO_SMALL (op 104) carry RSProt's v5 high/low-resolution streams,
 * which this client has no decoder for. The names here bind those opcodes to
 * the classic (lc254 / Kronos) bitstreams the client does decode, and
 * src/net/mock encodes them to match. The pairing is self-consistent and
 * exercises every downstream system; it is NOT wire-compatible with a real
 * OldSchool 230 server. See docs/osrs230_mockserver.md.
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
    { 104, PKTIN_LENGTH_VARU16, PKT_NAME_NPC_INFO }, /* NPC_INFO_SMALL_V5 (see note above) */
    { 12, PKTIN_LENGTH_VARU16, PKT_NAME_NONE }, /* NPC_INFO_LARGE_V5 */
    { 0, 2, PKT_NAME_NONE },   /* SET_NPC_UPDATE_ORIGIN */
    { 35, 3, PKT_NAME_VARP_SMALL },  /* p2 id, p1 signed value — same as lc254 */
    { 82, 6, PKT_NAME_VARP_LARGE },  /* p2 id, p4 value — same as lc254 */
    { 7, 0, PKT_NAME_VARP_RESET },   /* zero both client + server copies */
    { 88, 0, PKT_NAME_VARP_SYNC },   /* restore client from server-authoritative */
    { 77, 2, PKT_NAME_UPDATE_RUNENERGY }, /* p2 hundredths — osrs230_parse override */
    { 27, 2, PKT_NAME_UPDATE_RUNWEIGHT }, /* p2 signed kg — same as lc254 */
    { 114, 7, PKT_NAME_UPDATE_STAT }, /* V2 layout — osrs230_parse override */
    { 10, PKTIN_LENGTH_VARU16, PKT_NAME_UPDATE_INV_FULL },
    { 37, PKTIN_LENGTH_VARU16, PKT_NAME_UPDATE_INV_PARTIAL },
    { 80, 4, PKT_NAME_UPDATE_INV_STOP_TRANSMIT }, /* p4 combinedId */
    { 60, 2, PKT_NAME_IF_OPENTOP },  /* IF_OPENTOP (root interface) */
    { 6, 7, PKT_NAME_IF_OPENSUB },   /* IF_OPENSUB */
    { 36, 4, PKT_NAME_IF_CLOSESUB }, /* IF_CLOSESUB */
    { 42, 8, PKT_NAME_IF_MOVESUB },  /* IF_MOVESUB: dest then source combinedIds */
    { 53, PKTIN_LENGTH_VARU16, PKT_NAME_NONE },  /* IF_RESYNC */
    /* The IF_SET* family. Only 94 is a real RSProt opcode; 95-98 are assigned
     * here the same way the client->server table's are (see
     * docs/osrs230_mockserver.md 3.5) — RSProt's encoder table is not
     * vendored, the mock is the only producer, so what matters is that both
     * ends agree. All five carry a p4 combined uid where lc254 uses a p2 flat
     * component id, hence the osrs230_parse overrides. */
    { 94, PKTIN_LENGTH_VARU16, PKT_NAME_IF_SETTEXT },
    { 95, 6, PKT_NAME_IF_SETNPCHEAD },
    { 96, 4, PKT_NAME_IF_SETPLAYERHEAD },
    { 97, 6, PKT_NAME_IF_SETANIM },
    { 98, 5, PKT_NAME_IF_SETHIDE },
    { 47, 12, PKT_NAME_IF_SETEVENTS },
    { 84, PKTIN_LENGTH_VARU16, PKT_NAME_RUNCLIENTSCRIPT },
    { 90, PKTIN_LENGTH_VARU8, PKT_NAME_MESSAGE_GAME }, /* osrs230_parse override */
    /* MESSAGE_PRIVATE: p8 from37, p4 messageId, p1 staffModLevel, then the
     * wordpacked text over the remaining len-13 bytes. The shared parser at
     * gameproto_parse.c reads exactly that, and it is also what LostCity's
     * MessagePrivateEncoder writes. */
    { 29, PKTIN_LENGTH_VARU16, PKT_NAME_MESSAGE_PRIVATE },
    /*
     * CHAT_FILTER_SETTINGS: p1 public, p1 private, p1 trade.
     *
     * Assigned, not transcribed — opcode 3 is free in this table and staying
     * under 128 keeps the framer on its single-byte opcode path. Same doctrine
     * as the IF_SET* family above (docs/osrs230_mockserver.md 3.5): the mock is
     * the only producer, so what matters is that the two ends agree.
     */
    { 3, 3, PKT_NAME_CHAT_FILTER_SETTINGS },
    /* SET_MAP_FLAG carries (x, y) with 255,255 meaning "clear". The mock only
     * ever sends the clear form, and the canonical UNSET_MAP_FLAG handler
     * ignores its payload, so the 2-byte frame maps straight onto it. */
    { 2, 2, PKT_NAME_UNSET_MAP_FLAG },
    { 75, PKTIN_LENGTH_VARU8, PKT_NAME_NONE },   /* SET_PLAYER_OP */
    { 76, 6, PKT_NAME_NONE },  /* HINT_ARROW */
    { 73, 1, PKT_NAME_NONE },  /* MINIMAP_TOGGLE */
    { 65, 0, PKT_NAME_NONE },  /* CAM_RESET */
    { 108, 0, PKT_NAME_NONE }, /* SERVER_TICK_END */
    { 103, 2, PKT_NAME_NONE }, /* UPDATE_REBOOT_TIMER */
    /*
     * The social trio.
     *
     * FRIENDLIST_LOADED's length is **1, not 0**: the payload is a status byte
     * (0 loading, 1 connecting, 2 online) and gameproto_parse.c reads it with
     * g1 and then asserts the frame was fully consumed, so a 0 here aborts the
     * client on the first one of these it receives. The 0 was never noticed
     * because nothing had ever sent the packet.
     *
     * UPDATE_FRIENDLIST is one entry per packet — p8 name37, p1 world — used
     * both for the login dump (one packet per friend) and for single-entry
     * deltas afterwards. World 0 means "offline, or not visible to you".
     * UPDATE_IGNORELIST is the whole list at once: p8 name37 * (len/8).
     */
    { 15, 1, PKT_NAME_FRIENDLIST_LOADED },
    { 56, PKTIN_LENGTH_VARU16, PKT_NAME_UPDATE_FRIENDLIST },
    { 21, PKTIN_LENGTH_VARU16, PKT_NAME_UPDATE_IGNORELIST },
    { 20, 0, PKT_NAME_NONE },  /* LOGOUT */
    { 102, 5, PKT_NAME_NONE }, /* SYNTH_SOUND */
    { 57, 10, PKT_NAME_NONE }, /* MIDI_SONG_V2 */
    /*
     * Zones and the sub-packets that follow them.
     *
     * 41 / 106 / 38 are real rev-230 opcodes. The sub-packet opcodes below are
     * assigned here, the same way the IF_SET* family's are (see 3.5 of
     * docs/osrs230_mockserver.md): RSProt's encoder table is not vendored, the
     * mock is the only producer, so what matters is that both ends agree. The
     * numbers are free in this table and the payload layouts are lc254's, which
     * is what gameproto_parse.c's readers already decode.
     *
     * A zone sub-packet's opcode is resolved through this same table
     * (`rev->packetin_code`), so these must not collide with a top-level
     * opcode — which is why they are not simply lc254's numbering: lc254 puts
     * OBJ_COUNT on 98 and MAP_ANIM on 114, and rev 230 has IF_SETHIDE and
     * UPDATE_STAT there.
     */
    /* Two bytes, not the three RSProt's table states. The payload is the
     * zone's base as a pair of scene-local tiles, which is what the shared
     * parser reads and what the mock writes; a third byte would leave the
     * parser's exact-consumption assert unsatisfied. Real rev-230 zone packets
     * carry a level as well — the mock is single-plane per scene and sends the
     * player's, so it has nothing to put there. */
    { 41, 2, PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS },
    { 106, 2, PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS },
    { 38, PKTIN_LENGTH_VARU16, PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED },
    { 70, 4, PKT_NAME_LOC_ADD_CHANGE },
    { 71, 2, PKT_NAME_LOC_DEL },
    { 72, 4, PKT_NAME_LOC_ANIM },
    { 120, 5, PKT_NAME_OBJ_ADD },
    { 121, 3, PKT_NAME_OBJ_DEL },
    { 122, 7, PKT_NAME_OBJ_COUNT },
    { 123, 7, PKT_NAME_OBJ_REVEAL },
    { 124, 6, PKT_NAME_MAP_ANIM },
    { 125, 15, PKT_NAME_MAP_PROJANIM },
    { 126, 14, PKT_NAME_LOC_MERGE },
    /*
     * UPDATE_PID: p2 the local player's index, p1 members flag.
     *
     * Assigned (127 is free here), lc254's payload. Without it the client never
     * learns which entity is *itself*: `esync.local_pid` stays -1, so
     * `world->local_pid` does too, and everything that asks "what is my combat
     * level" — the `(level-N)` suffix on an npc's right-click row, most
     * visibly — gets no answer. Other call sites paper over it with a 2047
     * sentinel; the minimenu deliberately does not, because the reference
     * gates the whole suffix on having a local player.
     */
    { 127, 3, PKT_NAME_UPDATE_PID },
    /*
     * P_COUNTDIALOG: open the "Enter amount" prompt and wait for a number.
     *
     * Assigned (128 is free here), and zero-length like lc254's, which is the
     * whole packet — the prompt has nothing to configure and the answer comes
     * back as RESUME_P_COUNTDIALOG. Modern OldSchool drives this through a CS2
     * script instead of a packet, so there is no real rev-230 opcode to
     * transcribe; the client already has the handler (RS_Chat.dialog_input),
     * and nothing but the number was reaching it.
     */
    { 128, 0, PKT_NAME_P_COUNTDIALOG },
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
