/* IF_OPENTOP. See if_opentop.h — this file is the pattern every packet follows. */
#include "if_opentop.h"

/*
 * Transcribed from RSProt:
 *   protocol/osrs-230/.../codec/interfaces/IfOpenTopEncoder.kt
 *       buffer.p2Alt1(message.interfaceId)
 *   protocol/osrs-239/.../codec/interfaces/IfOpenTopEncoder.kt
 *       buffer.p2Alt2(message.interfaceId)
 *
 * One function per layout, and it runs in whichever direction the executor is
 * in — so the server's writer and the client's reader cannot be two different
 * transcriptions of this.
 */

void
packet_if_opentop_v6_out(RsprotExec *x, MsgIfOpenTop *m)
{
    RSPROT_U2_ALT1(x, m->interface_id);
}

void
packet_if_opentop_v13_out(RsprotExec *x, MsgIfOpenTop *m)
{
    RSPROT_U2_ALT2(x, m->interface_id);
}

/*
 * The gap between 230 and 239 is real, not an omission: revisions 231-238 have
 * their own layouts (v7 through v12) and this build does not speak them, so
 * `rsprot_version_pick` returns NULL there. That is a real answer the caller
 * reports once — not a fallback to a neighbouring revision's bytes, which would
 * frame cleanly and mean something else.
 */
const RsprotVersionRange rsprot_if_opentop_out[] = {
    { 227, 230, (RsprotCodecFn)packet_if_opentop_v6_out },
    { 239, 239, (RsprotCodecFn)packet_if_opentop_v13_out },
};

const int rsprot_if_opentop_out_count =
    (int)(sizeof(rsprot_if_opentop_out) / sizeof(rsprot_if_opentop_out[0]));
