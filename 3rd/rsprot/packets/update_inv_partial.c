/* UPDATE_INV_PARTIAL. Hand-written -- see update_inv_partial.h for why. */
#include "update_inv_partial.h"

/*
 * Transcribed from RSProt:
 *   protocol/osrs-239/.../game/outgoing/codec/inv/UpdateInvPartialEncoder.kt
 *
 *     buffer.pCombinedId(message.combinedId)
 *     buffer.p2(message.inventoryId)
 *     for (i in 0..<message.count) {
 *         val obj = message.getObject(i)
 *         buffer.pSmart1or2(InventoryObject.getSlot(obj))
 *         val id = InventoryObject.getId(obj)
 *         if (id == -1) { buffer.p2(0); continue }
 *         buffer.p2(id + 1)
 *         val count = InventoryObject.getCount(obj)
 *         buffer.p1(count.coerceAtMost(0xFF))
 *         if (count >= 0xFF) buffer.p4(count)
 *     }
 *
 * `message.count` never reaches the wire, which is what makes the loop
 * count-less. Note also that the id here is p2 while UPDATE_INV_FULL's is
 * p2Alt1, and the count is p1/p4 against FULL's p1Alt3/p4Alt3 -- two packets
 * carrying the same three fields in four different byte orders.
 */

void
packet_update_inv_partial_v1_out(RsprotExec *x, MsgUpdateInvPartial *m)
{
    RSPROT_COM(x, m->combined_id);
    RSPROT_U2(x, m->inventory_id);

    for (int32_t i = 0;
         RSPROT_MORE(x, m->count, i, MSG_UPDATE_INV_PARTIAL_MAX_SLOTS);
         i++) {
        MsgUpdateInvPartialSlot *slot = &m->slots[i];

        RSPROT_SMART(x, slot->slot);
        /* id + 1, so 0 is "this slot is now empty". */
        RSPROT_XFORM(x, U2, slot->id, 1);
        /* Legal: `id` was transferred on the line above. An empty slot really
         * is shorter here -- no count follows -- so unlike UPDATE_INV_FULL this
         * branch is a layout and not a redundancy. */
        if (RSPROT_BRANCH(x, slot->id) != -1) {
            RSPROT_CLAMP(x, U1, slot->count_lo, slot->count, 255);
            if (RSPROT_BRANCH(x, slot->count_lo) == 255)
                RSPROT_U4(x, slot->count);
        }
    }
}

const RsprotVersionRange rsprot_update_inv_partial_out[] = {
    { 221, 239, (RsprotCodecFn)packet_update_inv_partial_v1_out },
};

const int rsprot_update_inv_partial_out_count =
    (int)(sizeof(rsprot_update_inv_partial_out) / sizeof(rsprot_update_inv_partial_out[0]));
