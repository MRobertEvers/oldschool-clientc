/* MESSAGE_PRIVATE_ECHO. Hand-written; see message_private_echo.h. */
#include "message_private_echo.h"

/* v1 -- revs 223-241. */
void
packet_message_private_echo_v1_out(RsprotExec *x, MsgMessagePrivateEcho *m)
{
    RSPROT_JSTR(x, m->recipient);
    RSPROT_REST(x, m->huffman, m->huffman_length);
}

const RsprotVersionRange rsprot_message_private_echo_out[] = {
    { RSPROT_OSRS_ID(223), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_message_private_echo_v1_out },
    { RSPROT_OSRSM_ID(241), RSPROT_OSRSM_ID(241), (RsprotCodecFn)packet_message_private_echo_v1_out },
};

const int rsprot_message_private_echo_out_count =
    (int)(sizeof(rsprot_message_private_echo_out) / sizeof(rsprot_message_private_echo_out[0]));
