#ifndef RSPROT_STREAMS_INFO_RS_H
#define RSPROT_STREAMS_INFO_RS_H

/*
 * PLAYER_INFO and NPC_INFO for the rs family: the Lost City era builds
 * (245.2, 254, 289). Hand-written, unlike the OSRS streams beside it: three
 * revisions and one shape, which a spec would only restate.
 *
 * Laid out like the OSRS streams: each revision has an entry point,
 * <prot>_rs<N>, which calls its prot's version, <prot>_rs_v<X>, a straight-line
 * reader with no revision branching. What a revision changes and the version
 * does not -- the new-npc record's field widths -- is a constants struct.
 *
 *   player_info_rs245 / _rs254 / _rs289  ->  player_info_rs_v1
 *   npc_info_rs245 / _rs254              ->  npc_info_rs_v1   (slot 14, type 11)
 *   npc_info_rs289                       ->  npc_info_rs_v2   (v1 + the new-npc jump bit)
 *
 * 289 changed one field from 254, in one place: the new-npc record carries a
 * jump flag between its (dx, dz) and its extended-info bit (webclient
 * Client.getNpcPosNewVis, branch 289). Everything else, the player stream
 * included, is byte-for-byte 254's. Missing the bit does not produce a slightly
 * wrong npc: every later record is read one bit off and the section runs past
 * the end of the packet.
 *
 * Every reader writes the list every other revision's readers write
 * (rsprot_info.h). The streams are stateless across packets: the tracked list
 * is the consumer's. A read that runs out of payload ends the list with a STOP
 * (RSPROT_STOP_ERROR), as the OSRS readers do.
 */

#include "rsprot_info.h"

#include <stdint.h>

/* Player extended-info mask bits (a second byte follows BIG_UPDATE). */
#define RS_PLAYER_MASK_APPEARANCE 0x01
#define RS_PLAYER_MASK_SEQUENCE 0x02
#define RS_PLAYER_MASK_FACE_ENTITY 0x04
#define RS_PLAYER_MASK_SAY 0x08
#define RS_PLAYER_MASK_DAMAGE 0x10
#define RS_PLAYER_MASK_FACE_COORD 0x20
#define RS_PLAYER_MASK_CHAT 0x40
#define RS_PLAYER_MASK_BIG_UPDATE 0x80
#define RS_PLAYER_MASK_SPOTANIM 0x100
#define RS_PLAYER_MASK_EXACT_MOVE 0x200
#define RS_PLAYER_MASK_DAMAGE2 0x400

/* Npc extended-info mask bits. */
#define RS_NPC_MASK_DAMAGE2 0x1
#define RS_NPC_MASK_ANIM 0x2
#define RS_NPC_MASK_FACE_ENTITY 0x4
#define RS_NPC_MASK_SAY 0x8
#define RS_NPC_MASK_DAMAGE 0x10
#define RS_NPC_MASK_CHANGE_TYPE 0x20
#define RS_NPC_MASK_SPOTANIM 0x40
#define RS_NPC_MASK_FACE_COORD 0x80

/*
 * 2047, twice over, meaning two unrelated things:
 *
 *  - as an extended-info queue entry it is "the local player", a slot no
 *    tracked player can occupy;
 *  - as a new-player pid it is the end of the new-player section.
 *
 * They collide only because 11 bits is 11 bits. Naming them apart is what
 * keeps a reader from looking like it compares the two.
 */
#define RS_PLAYER_INFO_LOCAL_PLAYER_IDX 2047
#define RS_PLAYER_INFO_NEW_TERMINATOR 2047

/*
 * The new-npc record's slot and type widths. The stream keeps its shape while
 * fields widen with the game's id space, so a version reads them from its
 * revision's constants. A too-narrow width truncates silently into a different,
 * usually valid, id; the slot field's all-ones value ends the section.
 */
typedef struct NpcInfoRs
{
    int slot_bits;
    int type_bits;
} NpcInfoRs;

#define RS_NPC_INFO_BITS_MAX 16

/* --- versions ----------------------------------------------------------- */

int
player_info_rs_v1(
    uint8_t const* data,
    int len,
    RsprotInfoList* out);
int
npc_info_rs_v1(
    NpcInfoRs const* k,
    uint8_t const* data,
    int len,
    RsprotInfoList* out);
int
npc_info_rs_v2(
    NpcInfoRs const* k,
    uint8_t const* data,
    int len,
    RsprotInfoList* out);

/* --- entry points, by revision ----------------------------------------- */

int
player_info_rs245(uint8_t const* data, int len, RsprotInfoList* out);
int
player_info_rs254(uint8_t const* data, int len, RsprotInfoList* out);
int
player_info_rs289(uint8_t const* data, int len, RsprotInfoList* out);
int
npc_info_rs245(uint8_t const* data, int len, RsprotInfoList* out);
int
npc_info_rs254(uint8_t const* data, int len, RsprotInfoList* out);
int
npc_info_rs289(uint8_t const* data, int len, RsprotInfoList* out);

#endif
