/* UPDATE_INV_FULL. Hand-written -- see update_inv_full.h for why. */
#include "update_inv_full.h"

/*
 * Transcribed from RSProt:
 *   protocol/osrs-239/.../game/outgoing/codec/inv/UpdateInvFullEncoder.kt
 *
 *     buffer.pCombinedId(message.combinedId)
 *     buffer.p2(message.inventoryId)
 *     buffer.p2(capacity)
 *     for (i in 0..<capacity) {
 *         val obj = message.getObject(i)
 *         if (obj == InventoryObject.NULL) { p1Alt3(0); p2Alt1(0); continue }
 *         val count = InventoryObject.getCount(obj)
 *         buffer.p1Alt3(count.coerceAtMost(0xFF))
 *         if (count >= 255) buffer.p4Alt3(count)
 *         buffer.p2Alt1(InventoryObject.getId(obj) + 1)
 *     }
 */

void
packet_update_inv_full_v1_out(RsprotExec *x, MsgUpdateInvFull *m)
{
    RSPROT_COM(x, m->combined_id);
    RSPROT_U2(x, m->inventory_id);
    RSPROT_COUNT2(x, m->capacity, MSG_UPDATE_INV_FULL_MAX_SLOTS);

    for (int32_t i = 0; i < m->capacity; i++) {
        MsgUpdateInvFullSlot *slot = &m->slots[i];

        /* One byte, saturating at 255. Below that it IS the count; at 255 the
         * real value follows in four more bytes. */
        RSPROT_CLAMP(x, U1_ALT3, slot->count_lo, slot->count, 255);
        if (RSPROT_BRANCH(x, slot->count_lo) == 255)
            RSPROT_U4_ALT3(x, slot->count);

        /* The wire carries id + 1, so 0 means "empty slot" and id -1 is how
         * this struct says it. */
        RSPROT_XFORM(x, U2_ALT1, slot->id, 1);
    }
}

const RsprotVersionRange rsprot_update_inv_full_out[] = {
    { 221, 239, (RsprotCodecFn)packet_update_inv_full_v1_out },
};

const int rsprot_update_inv_full_out_count =
    (int)(sizeof(rsprot_update_inv_full_out) / sizeof(rsprot_update_inv_full_out[0]));
