/* MESSAGE_CLANCHANNEL. Hand-written; see message_clanchannel.h. */
#include "message_clanchannel.h"

/* v1 -- revs 223-241. */
void
packet_message_clanchannel_v1_out(RsprotExec *x, MsgMessageClanchannel *m)
{
    RSPROT_U1(x, m->clan_type);
    RSPROT_JSTR(x, m->name);
    RSPROT_U2_12(x, m->world_id);
    RSPROT_U3_123(x, m->world_message_counter);
    RSPROT_U1(x, m->chat_crown_type);
    RSPROT_REST(x, m->huffman, m->huffman_length);
}

const RsprotVersionRange rsprot_message_clanchannel_out[] = {
    { RSPROT_OSRS_ID(223), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_message_clanchannel_v1_out },
};

const int rsprot_message_clanchannel_out_count =
    (int)(sizeof(rsprot_message_clanchannel_out) / sizeof(rsprot_message_clanchannel_out[0]));
