/* UPDATE_STOCKMARKET_SLOT_V1. Hand-written; see update_stockmarket_slot_v1.h. */
#include "update_stockmarket_slot_v1.h"

/* v1 -- revs 223-241. */
void
packet_update_stockmarket_slot_v1_v1_out(RsprotExec *x, MsgUpdateStockmarketSlotV1 *m)
{
    RSPROT_U1(x, m->slot);
    RSPROT_U1(x, m->status);
    if (RSPROT_BRANCH(x, m->status) != 0) {
        RSPROT_U2_12(x, m->obj);
        RSPROT_U4_1234(x, m->price);
        RSPROT_U4_1234(x, m->count);
        RSPROT_U4_1234(x, m->completed_count);
        RSPROT_U4_1234(x, m->completed_gold);
    } else {
        /* The prot is 20 bytes either way: a reset pads (RSProt skipWrite(18)). */
        RSPROT_DATA(x, m->padding, 18);
    }
}

const RsprotVersionRange rsprot_update_stockmarket_slot_v1_out[] = {
    { RSPROT_OSRS_ID(223), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_update_stockmarket_slot_v1_v1_out },
};

const int rsprot_update_stockmarket_slot_v1_out_count =
    (int)(sizeof(rsprot_update_stockmarket_slot_v1_out) / sizeof(rsprot_update_stockmarket_slot_v1_out[0]));
