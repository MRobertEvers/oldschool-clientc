/* CLANCHANNEL_FULL. Hand-written; see clanchannel_full.h. */
#include "clanchannel_full.h"

/* v1 -- revs 222-241. */
void
packet_clanchannel_full_v1_out(RsprotExec *x, MsgClanchannelFull *m)
{
    RSPROT_U1(x, m->clan_type);
    for (int32_t i = 0; RSPROT_MORE(x, m->joined, i, 1); i++) {
        int32_t versioned;

        RSPROT_U1(x, m->flags);
        versioned = (RSPROT_BRANCH(x, m->flags) & 0x4) != 0;
        if (versioned)
            RSPROT_U1(x, m->version);
        else
            m->version = 2;
        if ((RSPROT_BRANCH(x, m->flags) & 0x3) == 0) {
            rsprot_exec_fail(x, "CLANCHANNEL_FULL: neither base37 nor display names");
            return;
        }
        RSPROT_U8_12345678(x, m->clan_hash);
        RSPROT_U8_12345678(x, m->update_num);
        RSPROT_JSTR(x, m->clan_name);
        RSPROT_BOOL(x, m->discarded);
        RSPROT_U1(x, m->kick_rank);
        RSPROT_U1(x, m->talk_rank);
        RSPROT_COUNT2(x, m->member_count, MSG_CLANCHANNEL_FULL_MAX_MEMBERS);
        for (int32_t k = 0; k < m->member_count; k++) {
            if (RSPROT_BRANCH(x, m->flags) & 0x1)
                RSPROT_U8_12345678(x, m->members[k].name37);
            if (RSPROT_BRANCH(x, m->flags) & 0x2)
                RSPROT_JSTR(x, m->members[k].name);
            RSPROT_U1(x, m->members[k].rank);
            RSPROT_U2_12(x, m->members[k].world);
            if (versioned && RSPROT_BRANCH(x, m->version) >= 3)
                RSPROT_BOOL(x, m->members[k].discarded);
        }
    }
}

const RsprotVersionRange rsprot_clanchannel_full_out[] = {
    { RSPROT_OSRS_ID(222), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_clanchannel_full_v1_out },
};

const int rsprot_clanchannel_full_out_count =
    (int)(sizeof(rsprot_clanchannel_full_out) / sizeof(rsprot_clanchannel_full_out[0]));
