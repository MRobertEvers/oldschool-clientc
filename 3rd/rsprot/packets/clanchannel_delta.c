/* CLANCHANNEL_DELTA. Hand-written; see clanchannel_delta.h. */
#include "clanchannel_delta.h"

/* v1 -- revs 222-241. */
void
packet_clanchannel_delta_v1_out(RsprotExec *x, MsgClanchannelDelta *m)
{
    RSPROT_U1(x, m->clan_type);
    RSPROT_U8_12345678(x, m->clan_hash);
    RSPROT_U8_12345678(x, m->update_num);
    for (int32_t i = 0; RSPROT_MORE(x, m->event_count, i, MSG_CLANCHANNEL_DELTA_MAX_EVENTS); i++) {
        MsgClanchannelDeltaEvent *e = &m->events[i];

        RSPROT_U1(x, e->opcode);
        switch (RSPROT_BRANCH(x, e->opcode)) {
        case 0:
            /* The end, kept as an event so the layout round-trips. RSProx
             * stops here; whatever follows (8 bytes, in the recordings) is
             * kept whole. */
            m->event_count = i + 1;
            RSPROT_REST(x, m->trailing, m->trailing_length);
            return;
        case 1:
            RSPROT_U1(x, e->skip_a);
            RSPROT_JSTRNULL(x, e->name);
            RSPROT_U2_12(x, e->world);
            RSPROT_U1(x, e->rank);
            break;
        case 2:
            RSPROT_U2_12(x, e->index);
            RSPROT_U1(x, e->rank);
            RSPROT_U2_12(x, e->world);
            RSPROT_U8_12345678(x, e->skip8);
            RSPROT_JSTR(x, e->name);
            break;
        case 3:
            RSPROT_U2_12(x, e->index);
            RSPROT_U1(x, e->skip_a);
            RSPROT_U1(x, e->skip_b);
            break;
        case 4:
            RSPROT_JSTRNULL(x, e->name);
            if (e->name) {
                RSPROT_U1(x, e->skip_a);
                RSPROT_U1(x, e->talk_rank);
                RSPROT_U1(x, e->kick_rank);
            }
            break;
        case 5:
            RSPROT_U1(x, e->skip_a);
            RSPROT_U2_12(x, e->index);
            RSPROT_U1(x, e->rank);
            RSPROT_U2_12(x, e->world);
            RSPROT_U8_12345678(x, e->skip8);
            RSPROT_JSTR(x, e->name);
            RSPROT_U1(x, e->skip_b);
            break;
        default:
            rsprot_exec_fail(x, "CLANCHANNEL_DELTA: unknown event");
            return;
        }
    }
}

const RsprotVersionRange rsprot_clanchannel_delta_out[] = {
    { RSPROT_OSRS_ID(222), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_clanchannel_delta_v1_out },
    { RSPROT_OSRSM_ID(241), RSPROT_OSRSM_ID(241), (RsprotCodecFn)packet_clanchannel_delta_v1_out },
};

const int rsprot_clanchannel_delta_out_count =
    (int)(sizeof(rsprot_clanchannel_delta_out) / sizeof(rsprot_clanchannel_delta_out[0]));
