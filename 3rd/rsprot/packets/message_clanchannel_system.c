/* MESSAGE_CLANCHANNEL_SYSTEM. Hand-written; see message_clanchannel_system.h. */
#include "message_clanchannel_system.h"

/* v1 -- revs 223-241. */
void
packet_message_clanchannel_system_v1_out(RsprotExec *x, MsgMessageClanchannelSystem *m)
{
    RSPROT_U1(x, m->clan_type);
    RSPROT_U2(x, m->world_id);
    RSPROT_U3(x, m->world_message_counter);
    RSPROT_REST(x, m->huffman, m->huffman_length);
}

const RsprotVersionRange rsprot_message_clanchannel_system_out[] = {
    { 223, 241, (RsprotCodecFn)packet_message_clanchannel_system_v1_out },
};

const int rsprot_message_clanchannel_system_out_count =
    (int)(sizeof(rsprot_message_clanchannel_system_out) / sizeof(rsprot_message_clanchannel_system_out[0]));
