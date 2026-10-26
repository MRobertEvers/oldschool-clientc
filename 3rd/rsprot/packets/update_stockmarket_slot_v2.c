/* UPDATE_STOCKMARKET_SLOT_V2. Hand-written -- see update_stockmarket_slot_v2.h. */
#include "update_stockmarket_slot_v2.h"

/*
 * v1 -- revs 241.
 * g1 slot, g1 firstState, [g1 version, g1 status], g2 obj, g8|g4 price,
 * g4 count, g4 completedCount, g8|g4 completedGold, [g4 extLength, gdata].
 */
void
packet_update_stockmarket_slot_v2_v1_out(RsprotExec *x, MsgUpdateStockmarketSlotV2 *m)
{
    int32_t version = 1;

    RSPROT_U1(x, m->slot);
    RSPROT_U1(x, m->first_state);
    if ((RSPROT_BRANCH(x, m->first_state) & 7) == 7) {
        RSPROT_U1(x, m->version);
        RSPROT_U1(x, m->status);
        version = RSPROT_BRANCH(x, m->version);
    }
    RSPROT_U2_12(x, m->obj);
    if (version >= 2)
        RSPROT_U8_12345678(x, m->price);
    else
        RSPROT_U4_1234(x, m->price32);
    RSPROT_U4_1234(x, m->count);
    RSPROT_U4_1234(x, m->completed_count);
    if (version >= 2) {
        RSPROT_U8_12345678(x, m->completed_gold);
        RSPROT_COUNT_AS(x, U4_1234, m->extension_length, MSG_UPDATE_STOCKMARKET_SLOT_V2_MAX_EXTENSION);
        RSPROT_DATA(x, m->extension, m->extension_length);
    } else {
        RSPROT_U4_1234(x, m->completed_gold32);
        /* The frame is a fixed 34 bytes at 241 (GameServerProt): the escaped
         * version-2 form with no extension. The version-1 form is 20, so 14
         * bytes trail it; RSProx leaves them unread. */
        RSPROT_DATA(x, m->padding, 14);
    }
}

const RsprotVersionRange rsprot_update_stockmarket_slot_v2_out[] = {
    { 241, 241, (RsprotCodecFn)packet_update_stockmarket_slot_v2_v1_out },
};

const int rsprot_update_stockmarket_slot_v2_out_count =
    (int)(sizeof(rsprot_update_stockmarket_slot_v2_out) / sizeof(rsprot_update_stockmarket_slot_v2_out[0]));
