/* GROUP_FULL. Hand-written -- see group_full.h. */
#include "group_full.h"

/*
 * v1 -- revs 239-241.
 * while readable: g1 index, gSmart1or2null id; id != -1: g8 uid, gdata(rest).
 */
void
packet_group_full_v1_out(RsprotExec *x, MsgGroupFull *m)
{
    for (int32_t i = 0; RSPROT_MORE(x, m->updates_count, i, MSG_GROUP_FULL_MAX_UPDATES); i++) {
        MsgGroupFullUpdate *u = &m->updates[i];

        RSPROT_U1(x, u->index);
        RSPROT_SMARTNULL(x, u->id);
        if (RSPROT_BRANCH(x, u->id) != -1) {
            RSPROT_U8_12345678(x, u->uid);
            RSPROT_REST(x, u->data, u->data_length);
        }
    }
}

const RsprotVersionRange rsprot_group_full_out[] = {
    { RSPROT_OSRS_ID(239), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_group_full_v1_out },
    { RSPROT_OSRSM_ID(241), RSPROT_OSRSM_ID(241), (RsprotCodecFn)packet_group_full_v1_out },
};

const int rsprot_group_full_out_count =
    (int)(sizeof(rsprot_group_full_out) / sizeof(rsprot_group_full_out[0]));
