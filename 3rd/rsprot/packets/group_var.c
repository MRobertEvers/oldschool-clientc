/* GROUP_VAR. Hand-written -- see group_var.h. */
#include "group_var.h"

/*
 * v1 -- revs 239-241.
 * while readable: g1 index, g4 packed, then by (packed >>> 16) & 3:
 * g4 | g8 | gjstr | gdata(rest).
 */
void
packet_group_var_v1_out(RsprotExec *x, MsgGroupVar *m)
{
    for (int32_t i = 0; RSPROT_MORE(x, m->updates_count, i, MSG_GROUP_VAR_MAX_UPDATES); i++) {
        MsgGroupVarUpdate *u = &m->updates[i];
        int32_t type;

        RSPROT_U1(x, u->index);
        RSPROT_U4_1234(x, u->packed);
        type = (int32_t)(((uint32_t)RSPROT_BRANCH(x, u->packed) >> 16) & 0x3);
        if (type == 0)
            RSPROT_U4_1234(x, u->int_value);
        else if (type == 1)
            RSPROT_U8_12345678(x, u->long_value);
        else if (type == 2)
            RSPROT_JSTR(x, u->string_value);
        else
            RSPROT_REST(x, u->data, u->data_length);
    }
}

const RsprotVersionRange rsprot_group_var_out[] = {
    { RSPROT_OSRS_ID(239), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_group_var_v1_out },
    { RSPROT_OSRSM_ID(241), RSPROT_OSRSM_ID(241), (RsprotCodecFn)packet_group_var_v1_out },
};

const int rsprot_group_var_out_count =
    (int)(sizeof(rsprot_group_var_out) / sizeof(rsprot_group_var_out[0]));
