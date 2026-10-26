/* MESSAGE_PRIVATE. Hand-written; see message_private.h. */
#include "message_private.h"

/* v1 -- revs 223-241. */
void
packet_message_private_v1_out(RsprotExec *x, MsgMessagePrivate *m)
{
    RSPROT_JSTR(x, m->sender);
    RSPROT_U2_12(x, m->world_id);
    RSPROT_U3_123(x, m->world_message_counter);
    RSPROT_U1(x, m->chat_crown_type);
    RSPROT_REST(x, m->huffman, m->huffman_length);
}

const RsprotVersionRange rsprot_message_private_out[] = {
    { RSPROT_OSRS_ID(223), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_message_private_v1_out },
};

const int rsprot_message_private_out_count =
    (int)(sizeof(rsprot_message_private_out) / sizeof(rsprot_message_private_out[0]));

/* v1 -- revs 223-241. */
void
packet_message_private_v1_in(RsprotExec *x, MsgMessagePrivateClient *m)
{
    RSPROT_JSTR(x, m->name);
    RSPROT_REST(x, m->huffman, m->huffman_length);
}

const RsprotVersionRange rsprot_message_private_in[] = {
    { RSPROT_OSRS_ID(223), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_message_private_v1_in },
};

const int rsprot_message_private_in_count =
    (int)(sizeof(rsprot_message_private_in) / sizeof(rsprot_message_private_in[0]));
