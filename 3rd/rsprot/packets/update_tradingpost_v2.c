/* UPDATE_TRADINGPOST_V2. Hand-written -- see update_tradingpost_v2.h. */
#include "update_tradingpost_v2.h"

/*
 * v1 -- revs 241.
 * g1 hasUpdate; != 0: g8 age, g2 obj, g1 status, g2 offerCount,
 * [gjstr name, gjstr previousName, g2 world, g8 time, g8 price, g4 count]*.
 */
void
packet_update_tradingpost_v2_v1_out(RsprotExec *x, MsgUpdateTradingpostV2 *m)
{
    RSPROT_U1(x, m->has_update);
    if (RSPROT_BRANCH(x, m->has_update) == 0)
        return;
    RSPROT_U8_12345678(x, m->age);
    RSPROT_U2_12(x, m->obj);
    RSPROT_U1(x, m->status);
    RSPROT_COUNT2(x, m->offer_count, MSG_UPDATE_TRADINGPOST_V2_MAX_OFFERS);
    for (int32_t i = 0; i < m->offer_count; i++) {
        MsgTradingPostOffer *o = &m->offers[i];

        RSPROT_JSTR(x, o->name);
        RSPROT_JSTR(x, o->previous_name);
        RSPROT_U2_12(x, o->world);
        RSPROT_U8_12345678(x, o->time);
        RSPROT_U8_12345678(x, o->price);
        RSPROT_U4_1234(x, o->count);
    }
}

const RsprotVersionRange rsprot_update_tradingpost_v2_out[] = {
    { RSPROT_OSRS_ID(241), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_update_tradingpost_v2_v1_out },
};

const int rsprot_update_tradingpost_v2_out_count =
    (int)(sizeof(rsprot_update_tradingpost_v2_out) / sizeof(rsprot_update_tradingpost_v2_out[0]));
