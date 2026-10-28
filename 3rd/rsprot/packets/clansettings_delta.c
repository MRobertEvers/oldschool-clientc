/* CLANSETTINGS_DELTA. Hand-written; see clansettings_delta.h. */
#include "clansettings_delta.h"

/* v1 -- revs 222-241. */
void
packet_clansettings_delta_v1_out(RsprotExec *x, MsgClansettingsDelta *m)
{
    RSPROT_U1(x, m->clan_type);
    RSPROT_U8_12345678(x, m->owner);
    RSPROT_U4_1234(x, m->update_num);
    for (int32_t i = 0; RSPROT_MORE(x, m->update_count, i, MSG_CLANSETTINGS_DELTA_MAX_UPDATES); i++) {
        MsgClansettingsDeltaUpdate *u = &m->updates[i];

        RSPROT_U1(x, u->opcode);
        switch (RSPROT_BRANCH(x, u->opcode)) {
        case 0:
            /* The end, kept as an update so the layout round-trips. RSProx
             * stops here; whatever follows (8 bytes, in the recordings) is
             * kept whole. */
            m->update_count = i + 1;
            RSPROT_REST(x, m->trailing, m->trailing_length);
            return;
        case 1:
        case 3:
        case 13:
            RSPROT_U1(x, u->hash_top);
            if (RSPROT_BRANCH(x, u->hash_top) != 0xFF) {
                RSPROT_U3_123(x, u->hash_mid);
                RSPROT_U4_1234(x, u->hash_low);
            }
            RSPROT_JSTRNULL(x, u->name);
            if (u->opcode == 13)
                RSPROT_U2_12(x, u->join_rune_day);
            break;
        case 2:
            RSPROT_U2_12(x, u->index);
            RSPROT_U1(x, u->rank);
            break;
        case 4:
            RSPROT_U1(x, u->allow_unaffined);
            RSPROT_U1(x, u->talk_rank);
            RSPROT_U1(x, u->kick_rank);
            RSPROT_U1(x, u->lootshare_rank);
            RSPROT_U1(x, u->coinshare_rank);
            break;
        case 5:
        case 6:
        case 15:
            RSPROT_U2_12(x, u->index);
            break;
        case 7:
            RSPROT_U2_12(x, u->index);
            RSPROT_U4_1234(x, u->value);
            RSPROT_U1(x, u->start_bit);
            RSPROT_U1(x, u->end_bit);
            break;
        case 8:
            RSPROT_U4_1234(x, u->setting);
            RSPROT_U4_1234(x, u->value);
            break;
        case 9:
            RSPROT_U4_1234(x, u->setting);
            RSPROT_U8_12345678(x, u->lvalue);
            break;
        case 10:
            RSPROT_U4_1234(x, u->setting);
            RSPROT_JSTR(x, u->svalue);
            break;
        case 11:
            RSPROT_U4_1234(x, u->setting);
            RSPROT_U4_1234(x, u->value);
            RSPROT_U1(x, u->start_bit);
            RSPROT_U1(x, u->end_bit);
            break;
        case 12:
            RSPROT_JSTR(x, u->svalue);
            RSPROT_U4_1234(x, u->skip4);
            break;
        case 14:
            RSPROT_U2_12(x, u->index);
            RSPROT_U1(x, u->muted);
            break;
        default:
            rsprot_exec_fail(x, "CLANSETTINGS_DELTA: unknown update");
            return;
        }
    }
}

const RsprotVersionRange rsprot_clansettings_delta_out[] = {
    { RSPROT_OSRS_ID(222), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_clansettings_delta_v1_out },
    { RSPROT_OSRSM_ID(241), RSPROT_OSRSM_ID(241), (RsprotCodecFn)packet_clansettings_delta_v1_out },
};

const int rsprot_clansettings_delta_out_count =
    (int)(sizeof(rsprot_clansettings_delta_out) / sizeof(rsprot_clansettings_delta_out[0]));
