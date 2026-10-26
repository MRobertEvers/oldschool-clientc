/* MESSAGE_FRIENDCHANNEL. Hand-written; see message_friendchannel.h. */
#include "message_friendchannel.h"

/* v1 -- revs 223-241. */
void
packet_message_friendchannel_v1_out(RsprotExec *x, MsgMessageFriendchannel *m)
{
    RSPROT_JSTR(x, m->sender);
    RSPROT_U8_12345678(x, m->channel_name);
    RSPROT_U2_12(x, m->world_id);
    RSPROT_U3_123(x, m->world_message_counter);
    RSPROT_U1(x, m->chat_crown_type);
    RSPROT_REST(x, m->huffman, m->huffman_length);
}

const RsprotVersionRange rsprot_message_friendchannel_out[] = {
    { RSPROT_OSRS_ID(223), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_message_friendchannel_v1_out },
};

const int rsprot_message_friendchannel_out_count =
    (int)(sizeof(rsprot_message_friendchannel_out) / sizeof(rsprot_message_friendchannel_out[0]));
