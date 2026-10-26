/* MESSAGE_PUBLIC. Hand-written; see message_public.h. */
#include "message_public.h"

/* v1 -- revs 223-241. */
void
packet_message_public_v1_in(RsprotExec *x, MsgMessagePublic *m)
{
    RSPROT_U1(x, m->type);
    RSPROT_U1(x, m->colour);
    RSPROT_U1(x, m->effect);
    if (RSPROT_BRANCH(x, m->colour) >= 13 && RSPROT_BRANCH(x, m->colour) <= 20)
        for (int32_t i = 0; i < m->colour - 12; i++)
            RSPROT_U1(x, m->pattern[i]);
    RSPROT_REST_BUT(x, m->huffman, m->huffman_length, RSPROT_BRANCH(x, m->type) == 3 ? 1 : 0);
    if (RSPROT_BRANCH(x, m->type) == 3)
        RSPROT_U1(x, m->clan_type);
}

const RsprotVersionRange rsprot_message_public_in[] = {
    { 223, 241, (RsprotCodecFn)packet_message_public_v1_in },
};

const int rsprot_message_public_in_count =
    (int)(sizeof(rsprot_message_public_in) / sizeof(rsprot_message_public_in[0]));
