#ifndef RSPROT_PACKET_UPDATE_INV_PARTIAL_H
#define RSPROT_PACKET_UPDATE_INV_PARTIAL_H

/*
 * UPDATE_INV_PARTIAL -- server -> client. Changed slots only, each addressed.
 *
 * HAND-WRITTEN, for the same reasons as UPDATE_INV_FULL plus one more: the
 * entry list carries NO count. `for (i in 0..<message.count)` iterates a
 * server-side field that is never written, so the reader consumes entries until
 * the payload runs out. RSPROT_MORE reconciles that.
 *
 * ## Where this differs from UPDATE_INV_FULL, and why it matters
 *
 * Both encoders have an "empty slot" branch, and they are NOT the same shape:
 *
 *   FULL     if (obj == NULL) { p1Alt3(0); p2Alt1(0); continue }
 *            -- writes BOTH fields, identical to the general path for
 *               count=0/id=-1, so the branch is redundant and this codec drops
 *               it.
 *
 *   PARTIAL  if (id == -1) { p2(0); continue }
 *            -- writes the id and SKIPS the count entirely. An empty slot here
 *               is genuinely shorter on the wire, so the branch is real and
 *               has to survive.
 *
 * It survives legally: the branch tests `id`, and `id` is transferred on the
 * line above it. Kotlin tests the value before writing it; a bidirectional
 * codec writes first and then tests what it wrote, which is the same bytes in
 * the same order and is decodable.
 */

#include "../include/rsprot_exec.h"

/** One changed slot. `id` is -1 for "now empty", in which case no count
 *  follows on the wire. */
typedef struct MsgUpdateInvPartialSlot
{
    int32_t slot;
    int32_t id;
    int32_t count_lo;
    int32_t count;
} MsgUpdateInvPartialSlot;

typedef struct MsgUpdateInvPartial
{
    int32_t combined_id;
    int32_t inventory_id;
    /** NOT on the wire. Encoding reads it; decoding writes it. */
    int32_t count;
    MsgUpdateInvPartialSlot *slots;
} MsgUpdateInvPartial;

/** Caller-supplied `slots` capacity. A count-less array has no length field to
 *  bound it, so the buffer is the bound and the codec fails past it. */
#define MSG_UPDATE_INV_PARTIAL_MAX_SLOTS 256

/* v1 -- revs 221-239. */
void packet_update_inv_partial_v1_out(RsprotExec *x, MsgUpdateInvPartial *m);

#define packet_update_inv_partial_rev221_out packet_update_inv_partial_v1_out
#define packet_update_inv_partial_rev230_out packet_update_inv_partial_v1_out
#define packet_update_inv_partial_rev239_out packet_update_inv_partial_v1_out

extern const RsprotVersionRange rsprot_update_inv_partial_out[];
extern const int rsprot_update_inv_partial_out_count;

#endif
