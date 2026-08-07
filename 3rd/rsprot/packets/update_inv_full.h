#ifndef RSPROT_PACKET_UPDATE_INV_FULL_H
#define RSPROT_PACKET_UPDATE_INV_FULL_H

/*
 * UPDATE_INV_FULL -- server -> client. A whole container, slot by slot.
 *
 * HAND-WRITTEN. The generator refuses this one, and the refusal is right: the
 * Kotlin reads slots through accessors on an opaque packed value
 * (`InventoryObject.getCount(obj)`), clamps a count with an escape field, and
 * offsets the id -- three idioms it does not model. See
 * docs/RSPROT_CODEC_GENERATOR_PLAN.md and HAND_WRITTEN_PROTS in
 * tools/rsprot_gen_codec.py.
 *
 * ## The empty slot is not a special layout
 *
 * RSProt writes:
 *
 *     if (obj == InventoryObject.NULL) { p1Alt3(0); p2Alt1(0); continue }
 *
 * which reads like a second layout and is not one. The general path for
 * `count = 0, id = -1` emits exactly those bytes: the clamp of 0 is 0, `0 !=
 * 255` so no escape follows, and the `id + 1` offset turns -1 into 0. Decoding
 * is the same identity in reverse. So this codec has ONE path, and an empty
 * slot falls out of it -- which is also why the sentinel needs no code here.
 *
 * The offset direction is the trap: get it backwards and every item in the
 * container shifts by one, which renders perfectly and is wrong everywhere.
 */

#include "../include/rsprot_exec.h"

/** One slot. `id` is -1 for an empty slot; `count_lo` is the derived one-byte
 *  count the wire carries, and is meaningful only next to `count`. */
typedef struct MsgUpdateInvFullSlot
{
    int32_t count_lo;
    int32_t count;
    int32_t id;
} MsgUpdateInvFullSlot;

typedef struct MsgUpdateInvFull
{
    int32_t combined_id;
    int32_t inventory_id;
    /** Slot count, ON the wire -- unlike UPDATE_INV_PARTIAL's, which is not. */
    int32_t capacity;
    MsgUpdateInvFullSlot *slots;
} MsgUpdateInvFull;

/** Caller-supplied `slots` capacity. The wire's own field is two bytes, so a
 *  malformed packet can ask for up to 65535; the codec fails past this. */
#define MSG_UPDATE_INV_FULL_MAX_SLOTS 4096

/* v1 -- revs 221-239. The layout has not moved. */
void packet_update_inv_full_v1_out(RsprotExec *x, MsgUpdateInvFull *m);

#define packet_update_inv_full_rev221_out packet_update_inv_full_v1_out
#define packet_update_inv_full_rev230_out packet_update_inv_full_v1_out
#define packet_update_inv_full_rev239_out packet_update_inv_full_v1_out

extern const RsprotVersionRange rsprot_update_inv_full_out[];
extern const int rsprot_update_inv_full_out_count;

#endif
