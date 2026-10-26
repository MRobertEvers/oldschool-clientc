/* UPDATE_TRADINGPOST_V1. Hand-written; see update_tradingpost_v1.h. */
#include "update_tradingpost_v1.h"

/* v1 -- revs 223-241. */
void
packet_update_tradingpost_v1_v1_out(RsprotExec *x, MsgUpdateTradingpostV1 *m)
{
    RSPROT_U1(x, m->set);
    if (RSPROT_BRANCH(x, m->set) == 0)
        return;
    RSPROT_U8_12345678(x, m->age);
    RSPROT_U2_12(x, m->obj);
    RSPROT_U1(x, m->status);
    RSPROT_COUNT2(x, m->offer_count, MSG_UPDATE_TRADINGPOST_V1_MAX_OFFERS);
    for (int32_t i = 0; i < m->offer_count; i++) {
        RSPROT_JSTR(x, m->offers[i].name);
        RSPROT_JSTR(x, m->offers[i].previous_name);
        RSPROT_U2_12(x, m->offers[i].world);
        RSPROT_U8_12345678(x, m->offers[i].time);
        RSPROT_U4_1234(x, m->offers[i].price);
        RSPROT_U4_1234(x, m->offers[i].count);
    }
}

const RsprotVersionRange rsprot_update_tradingpost_v1_out[] = {
    { RSPROT_OSRS_ID(223), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_update_tradingpost_v1_v1_out },
};

const int rsprot_update_tradingpost_v1_out_count =
    (int)(sizeof(rsprot_update_tradingpost_v1_out) / sizeof(rsprot_update_tradingpost_v1_out[0]));
