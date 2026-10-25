#ifndef RSPROT_INFO_INTERNAL_H
#define RSPROT_INFO_INTERNAL_H

/* Shared between the stream readers; not part of the public header. */

#include "rsprot_info.h"

#include <stddef.h>

/** The op list being written. */
typedef struct RsprotInfoOut
{
    RsprotInfoOp *ops;
    int cap;
    int count;
    /** Set once an op did not fit; the decode carries on so the table stays in
     *  step, and the caller is told the list is incomplete. */
    int overflow;
} RsprotInfoOut;

/** The next op, zeroed, with `kind` and `who` set. Past `cap` it is a scratch
 *  op nothing reads, so a writer never has to test. */
RsprotInfoOp *rsprot_info_emit(RsprotInfoOut *out, RsprotInfoOpKind kind, int who);

/** The byte-aligned extended-info tail: for each of `count` entities in
 *  `who[]`, a flag word and its blocks in `side`'s order. Returns 0 at the
 *  first flag or block it cannot read; everything before it was written. */
int rsprot_info_tail(
    RsprotBuf *b, const RsprotInfoSide *side, const int *who, int count, RsprotInfoOut *out,
    RsprotNpcTypeLengths lengths, void *ctx);

/** PLAYER_INFO with one layout's extended-info tables (rsprot_info_player.c). */
int rsprot_info_player_read(
    const RsprotInfoSide *side, RsprotPlayerInfo *pi, const uint8_t *data, int32_t len,
    RsprotInfoOp *ops, int cap, int *complete);

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

/** Reads one record. Every revision reorders the fields and SMALL / LARGE
 *  differ in the delta width, so each npc layout generates its own. */
typedef void (*RsprotNpcRecordRead)(RsprotBuf *b, RsprotNpcRecord *r);

/** NPC_INFO with one layout's record reader and extended-info tables
 *  (rsprot_info_npc.c). */
int rsprot_info_npc_read(
    const RsprotInfoSide *side, RsprotNpcRecordRead record, const uint8_t *data, int32_t len,
    RsprotNpcTypeLengths lengths, void *ctx, RsprotInfoOp *ops, int cap, int *complete);

/* rsprot_info_worldentity.c -- the two WORLDENTITY_INFO loops. */

/** A spawn record after its g2 index (generated per layout). */
typedef void (*RsprotWeSpawnRead)(RsprotBuf *b, RsprotInfoWeAdd *a);
/** An update entry's extended info: its g1 flag byte, then the payload
 *  (generated per layout). */
typedef void (*RsprotWeExtRead)(RsprotBuf *b, RsprotInfoWeExt *e);

/** The 2-bit-per-axis transform: a g1s of codes, LSB first (x, y, z, angle),
 *  each 0 / g1s / g2s / g4. */
void rsprot_info_we_fine(RsprotBuf *b, int32_t *x, int32_t *y, int32_t *z, int32_t *angle);

/** V3-V5: remove / idle / move entries, then spawns while 10 bytes remain. */
int rsprot_info_worldentity_plain_read(
    RsprotWeSpawnRead spawn, const uint8_t *data, int32_t len, RsprotInfoOp *ops, int cap,
    int *complete);

/** V6+: idle and move entries end in extended info; spawns run while any byte
 *  remains. */
int rsprot_info_worldentity_extended_read(
    RsprotWeSpawnRead spawn, RsprotWeExtRead ext, const uint8_t *data, int32_t len,
    RsprotInfoOp *ops, int cap, int *complete);

#endif
