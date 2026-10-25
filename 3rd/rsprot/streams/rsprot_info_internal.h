#ifndef RSPROT_INFO_INTERNAL_H
#define RSPROT_INFO_INTERNAL_H

/*
 * Shared by the stream versions; not part of the public header.
 *
 * Every version reader (player_info_v1.c, npc_info_v5.c, worldentity_info_v7.c,
 * ...) is the same shape: the stream's sections, which are the same at every
 * revision and live here, then what its revision's constants say. Nothing
 * here knows a revision.
 */

#include "rsprot_info.h"

#include <stddef.h>

/* --- the op list ------------------------------------------------------------ */

/** Write the STOP op (once): `reason`, at the cursor. */
RsprotInfoOp *rsprot_info_stop(RsprotBuf *b, RsprotInfoList *out, int reason, int who);

/** End a read: a STOP for an error or trailing bytes nothing has stopped for
 *  yet. Returns the op count. */
int rsprot_info_finish(RsprotBuf *b, RsprotInfoList *out, int32_t len);

/* --- PLAYER_INFO: rsprot_info_player.c ---------------------------------------- */

/** One PLAYER_INFO read's player-side state: the table, and where this
 *  packet's movement op for each slot sits in the list, for the traversal and
 *  exact-move blocks that amend it (-1: the slot did not move). */
typedef struct RsprotPlayerPacket
{
    RsprotPlayerInfo *pi;
    int16_t movement_op[RSPROT_INFO_PLAYER_SLOTS];
} RsprotPlayerPacket;

/** The four bit sections, which read the same at every revision: moves the
 *  table and writes the movement ops, and lists in `extended` the slots whose
 *  extended info follows, in wire order. Returns how many; a malformed section
 *  sets the cursor's error. */
int rsprot_info_player_sections(
    RsprotPlayerPacket *pp, RsprotBuf *b, RsprotInfoList *out,
    int extended[RSPROT_INFO_PLAYER_SLOTS]);

/** The ops that address slot `idx` for what follows: SET_LOCAL_PLAYER for the
 *  local player, ADD_NEW otherwise. */
void rsprot_info_player_target(RsprotInfoList *out, const RsprotPlayerInfo *pi, int idx);

/* --- NPC_INFO: rsprot_info_npc.c ------------------------------------------------ */

/** One npc entering-view record (NpcInfoClient.processLowResolution) -- the
 *  fields after the 16-bit slot. */
typedef struct RsprotNpcRecord
{
    int type;
    int dx;
    int dz;
    int jump;
    int angle; /* NPC_TURN_ANGLES index */
    int has_spawn_cycle;
    uint32_t spawn_cycle;
    int extended;
} RsprotNpcRecord;

/** Reads one record. Every revision reorders its fields, so each revision
 *  generates its own (<stream>_osrs<N>.c); `delta_bits` is the prot's (SMALL
 *  6, LARGE 8). */
typedef void (*RsprotNpcRecordRead)(RsprotBuf *b, int delta_bits, RsprotNpcRecord *r);

/** decodeDelta: a signed `bits`-bit delta. */
int32_t rsprot_info_npc_delta(RsprotBuf *b, int bits);

#define RSPROT_INFO_NPC_EXTENDED_MAX 512

/** The high-resolution section and the entering-view records, which read the
 *  same at every revision but for `record`: writes the movement ops and lists
 *  in `extended` the list positions whose extended info follows. Returns how
 *  many. */
int rsprot_info_npc_sections(
    RsprotBuf *b, int delta_bits, RsprotNpcRecordRead record, RsprotInfoList *out,
    int extended[RSPROT_INFO_NPC_EXTENDED_MAX]);

/* --- WORLDENTITY_INFO: rsprot_info_worldentity.c ---------------------------------- */

/** A spawn record after its g2 index (generated per revision). */
typedef void (*RsprotWeSpawnRead)(RsprotBuf *b, RsprotInfoWeAdd *a);
/** An update entry's extended info: its g1 flag byte, then the payload
 *  (generated per revision). */
typedef void (*RsprotWeExtRead)(RsprotBuf *b, RsprotInfoWeExt *e);

/** The 2-bit-per-axis transform: a g1s of codes, LSB first (x, y, z, angle),
 *  each 0 / g1s / g2s / g4. */
void rsprot_info_we_fine(RsprotBuf *b, int32_t *x, int32_t *y, int32_t *z, int32_t *angle);

/** V3-V5: remove / idle / move entries, then spawns while 10 bytes remain. */
int rsprot_info_worldentity_plain_read(
    RsprotWeSpawnRead spawn, const uint8_t *data, int32_t len, RsprotInfoList *out);

/** V6+: idle and move entries end in extended info; spawns run while any byte
 *  remains. */
int rsprot_info_worldentity_extended_read(
    RsprotWeSpawnRead spawn, RsprotWeExtRead ext, const uint8_t *data, int32_t len,
    RsprotInfoList *out);

#endif
